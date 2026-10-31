#!/usr/bin/env python3
"""Doc-anchor validator (better-harness finding doc-code-anchors-drift-unvalidated).

docs/*.md cite code locations like `src/runtime.cpp:95` to say where a behaviour
lives. Those citations are measurements taken whenever someone typed them, and
they go stale silently — three of four spot-checked anchors were already wrong
(src/runtime.cpp:95 vs the real 151; src/main.cpp:274-282 vs 433; a string
attributed to src/main.cpp:110 that lives in src/hcs.cpp:95). Nothing in the
test suite could see any of it.

Check per anchor, in the same markdown line:
  * the cited file exists, and the cited line number is within its length;
  * if that line also names code tokens (`like_this` or "quoted string"), at
    least one of those tokens must occur within +/-WINDOW lines of the cited
    line. A token that only exists 56 lines away is a stale anchor.

Exit 1 with the full list of failures (R9.2 discipline: report every violation
at once, not the first).

Usage:  python probe/anchors_check.py                # check docs/ and AGENTS.md
        python probe/anchors_check.py --verbose      # also print the checked count
"""
import datetime
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WINDOW = 25
TARGETS = ["docs/acceptance-mvp.md", "AGENTS.md", "task_plan.md", "findings.md",
           "progress.md"]
ANCHOR = re.compile(r"(?:^|[\s`(])((?:src|include|tests|third_party|probe|docs)/"
                    r"[\w./-]+?\.(?:cpp|hpp|h|mm)):(\d+(?:[-/]\d+)*)")


class Citation:
    """One line-number claim, reported as if ANCHOR had matched exactly that number.

    Phase 58 measured the hole the old regex left: these docs cite a group of lines as
    `tests/acceptance.cpp:2747/2787/2800/2811/2863`, and ANCHOR returned only the FIRST number, so
    25 of the 40 numbers in this repo's citations were never looked at while the gate printed "all
    agree" (same family as E21/E24: a multi-item claim verified on one item). Expanding into one
    Citation per number keeps the whole existing check body intact.
    """

    __slots__ = ("path", "first", "last", "gs", "ge", "tail")

    def __init__(self, path, first, last, gs, ge, tail):
        self.path, self.first, self.last = path, first, last
        self.gs, self.ge, self.tail = gs, ge, tail

    def group(self, i):
        if i == 1:
            return self.path
        if i == 2:
            return str(self.first)
        if i == 3:
            return str(self.last) if self.last != self.first else None
        return None

    def start(self):
        # The GROUP's position, not this number's: the claim ("[R20-nonhip] owns these lines") is
        # written once, before the first number, and it has to hold at every number in the group.
        return self.gs

    def end(self):
        return self.ge


def expand_citations(line):
    for m in ANCHOR.finditer(line):
        for n, part in enumerate(m.group(2).split("/")):
            a, _, b = part.partition("-")
            yield Citation(m.group(1), int(a), int(b) if b else int(a), m.start(), m.end(), n > 0)
# {1,80}, not {2,80}: a ONE-character code span (`Y`) is legal Markdown, and requiring two
# characters made the regex refuse to pair it - so the scan continued from its closing backtick and
# every later span on the line paired one quote out of phase. The subject then vanished: 9 citations
# of `hipMemcpyAsync`（`src/codegen_hip.cpp:946`）were counted blind purely because the sentence
# before them contained `Y` (Phase 61). Short spans are still dropped by the >=5 length rule below;
# the only change is that pairing stays correct.
TOKEN = re.compile(r"`([^`\n]{1,80})`")
# A backticked span only counts as a code symbol if it looks like one. Without
# this the checker "found" tokens such as [R8.5-long] (a test-section label,
# printed ~110 lines from the section header it cites), :942-943 (the tail of
# another anchor) and maxThreadsPerThreadgroup.width=1024 (prose), and reported
# them as stale anchors.
IDENT = re.compile(r"^[A-Za-z_~][\w:~]*$")
SKIP_TOKEN = re.compile(r"[\s/=<>|^&.,*-]|\.(?:cpp|hpp|h|md|txt|json|py|csv|db)$|"
                        r"^(?:src|include|tests|docs|probe|third_party)/",
                        re.I)
# Kept OUT of the case-insensitive blob above on purpose. `^[A-Z]+\d` exists to drop ARM/requirement
# labels (`R5.4`, `G10`, `T1`) as symbols. Under re.I it also ate real identifiers that carry a
# digit - `fp16_rel` is the whole subject of three citations in include/unikern/components.hpp:133,
# so the gate counted those blind (Phase 61, third instance of "the criterion can't read what the
# docs actually write": slice pairing, 1-char spans, and now this).
ARMISH = re.compile(r"^[A-Z]+\d")

# Phase 56: the window check above only bites when the citing line carries a checkable symbol.
# Measured on this repo: 177 of 287 anchors sat on lines whose only backticked spans were ARM
# LABELS like `[R5.4-artifact-bytes-cl]` - which SKIP_TOKEN rejects - so the gate printed
# "all agree" while 62% of the citations were never checked at all. Inserting one arm in Phase 55
# moved five of those citations ~99 lines and the gate stayed green. Two ratchet-shaped changes:
#   * arm labels become checkable against the arm-header table of the file they cite;
#   * whatever is STILL not checkable is counted and capped, so the blind spot can only shrink.
ARM_MARK = "  // -------- "
ARM_LABEL = re.compile(r"^\[?([A-Z][\w:.-]{2,60})\]?$")
# A section's own printed banners only: any streamed string that STARTS with the bracketed id
# (`<< "[R6.9-basis] shipped input ..."` as well as `<< "[R5.4-artifact-bytes-cl] "`). Requiring
# the closing quote right after `]` lost the long-form banners and made those citations NA again;
# collecting every bracketed id in the range instead would let a section claim arms it merely
# MENTIONS (R15's comment cites [R20]) and the gate would then accuse the wrong owner.
LABEL_IN_CODE = re.compile(r'<<\s*"\[([A-Z][\w:.-]{2,60})\]')
# Measured the day this rule landed (Phase 56): 288 citations, of which the arm-label rule now
# resolves 33 and the identifier rule resolves the rest — 132 sit on lines that assert NOTHING
# about what lives at the cited number. That is the size of the remaining blind spot, written down
# so it cannot grow quietly: the cap may only ever be LOWERED, never raised to absorb new citations.
UNVERIFIABLE_CAP = 11   # measured 2026-10-29 at the close of Phase 61, lower only
# The middle tier: the paragraph carries a symbol but the citation itself does not (wrapped
# markdown). Measured 2026-10-29 after Phase 58 stopped tokenizing a SLICE of the line (a slice
# starting on a closing backtick pairs the quotes wrong and hid the symbol): 61. Same rule - the cap
# may only ever be LOWERED.
WEAK_CAP = 8   # measured 2026-10-29 at the close of Phase 61 (three tokenizer-blindness fixes), lower only


def arm_table(lines):
    """[(label, start, end, aliases)] from a source file's `  // -------- R..` section headers.

    Docs cite the PRINTED banner (``[R5.4-artifact-bytes-cl]``) while the header says ``R5.4-cl``,
    so each section also collects the bracketed ids it prints; matching on the header alone would
    have been a second way to make the rule look alive while it checked nothing.
    """
    heads = [i for i, l in enumerate(lines) if l.startswith(ARM_MARK)]
    out = []
    for n, i in enumerate(heads):
        end = heads[n + 1] if n + 1 < len(heads) else len(lines)
        raw = lines[i].split(ARM_MARK, 1)[1]
        # "R20 (Phase 20): ..." is the same arm the docs call [R20].
        label = raw.split(":")[0].strip().split(" (")[0].strip()
        aliases = set(LABEL_IN_CODE.findall("\n".join(lines[i:end])))
        out.append((label, i + 1, end, aliases))
    return out


def arm_labels(md_ctx, lo=-1, hi=None):
    """Bracketed arm labels, selected by position (see code_tokens for why slicing is wrong).

    `/` is allowed inside the id because the acceptance file really has a section named
    `R6.2/R6.4` (one arm covering two requirement lines), and 6 citations wrote that label while the
    regex rejected it - so those citations were counted blind even though the doc gave a checkable
    owner (Phase 60).
    """
    return [m.group(1) for m in re.finditer(r"`\[([A-Z][\w:./-]{2,60})\]`", md_ctx)
            if not (lo >= 0 and not (m.start() >= lo and (hi is None or m.end() <= hi)))]


def check_arm_label(label, start, table):
    """None if `label` owns the cited line, a violation string if it does not, 'NA' if the label
    names no section here (then the identifier rule keeps jurisdiction).

    A header label beats an alias: an arm's body quotes other arms (e.g. R20-nonhip's comment
    mentions `[R20]`), and matching on that first would report the wrong owner. Multiple sections
    may legitimately carry the same id, so the cited line only has to land in ONE of them.
    """
    exact = [s for s in table if s[0] == label]
    cands = exact or [s for s in table if label in s[3]]
    if not cands:
        return "NA"
    if any(s[1] <= start <= s[2] for s in cands):
        return None
    inside = next((s for s in table if s[1] <= start <= s[2]), None)
    owners = "、".join("%s (%d-%d)" % (c[0], c[1], c[2]) for c in cands)
    return (f"{label} owns {owners} but line {start} falls in "
            + (f"{inside[0]} ({inside[1]}-{inside[2]})" if inside else "no section"))


def code_tokens(md_line, lo=-1, hi=None):
    """Backticked spans that are plausibly code symbols, after two measured loosenings.

    The original filter rejected `tolerance_for()` (trailing parens) and `dp.multiProcessorCount`
    (the dot), which is how the gate ended up declaring 216 of 287 citations "nothing asserted":
    the two shapes these docs actually use were the two shapes the filter could not read.

    `lo`/`hi` select by POSITION IN THIS LINE instead of letting the caller tokenize a slice: a
    sliced ctx starts wherever the previous anchor ended, which is often inside another backticked
    span, and the pair then reads "`）两处发射、`" as one token and loses the `initA` after it. Found
    while writing docs/evidence/phase58/README.md, where three correct citations of `initA` were
    counted as blind.
    """
    out = []
    for m in TOKEN.finditer(md_line):
        if lo >= 0 and not (m.start() >= lo and (hi is None or m.end() <= hi)):
            continue
        raw = m.group(1)
        t = raw.strip()
        bare = re.sub(r"[({\[].*", "", t).strip()          # `f(x) -> f`, `a[i] -> a`
        if bare and re.fullmatch(r"[A-Za-z_]\w*(?:\.\w+)+", bare):
            out.append(bare.split(".")[-1])                 # member access: check the member
            continue
        # A metasyntactic placeholder (`path:NNN`, the shape docs use when they describe the
        # citation rule itself) is not a symbol a line number could be stale about. Caught by
        # Phase 58 writing its own record: `path:NNN` accused five real citations. Case-SENSITIVE
        # on purpose - SKIP_TOKEN is compiled with re.I, which would also drop `std::cout`.
        if re.fullmatch(r"[a-z_]+:[A-Z]{2,}", bare):
            continue
        # >=5 chars: short prose words that happen to be backticked (`run`, `int`)
        # are CLI/subcommand or keyword names, not symbols a line number could be
        # stale about — they matched text 500 lines away and produced noise.
        if len(bare) < 5 or not IDENT.match(bare) or SKIP_TOKEN.search(bare):
            continue
        if ARMISH.match(bare):          # R5.4 / G10 / T1 - an arm or requirement label, not a symbol
            continue
        # Language keywords and container nouns occur in literally every file here, so a citation
        # next to one of them is CHECKED WITHOUT SAYING ANYTHING (Phase 59 found the repair tool
        # about to graft `double` in front of a KML citation - it would have passed forever).
        if bare.lower() in STOPWORD:
            continue
        out.append(bare)
    return out



def cells(line):
    # An escaped pipe (`\|`, which Markdown renders as a literal "|") is content,
    # not a separator: the E3 row of the error ledger carries one and first read as
    # a 5-cell row in a 4-column table.
    return len(re.findall(r"(?<!\\)\|", line)) - 1


# A section heading is the one thing an Edit anchored on "## X" silently deletes, and
# that happened five times in one day in these files (E27). The table check cannot see
# it -- the orphaned table is still well-formed -- so the skeleton is asserted here.
REQUIRED_HEADINGS = {
    "task_plan.md": ["## Goal", "## Current Phase", "## Phases", "## Decisions",
                     "## Constraints", "## Errors Encountered"],
    "docs/acceptance-mvp.md": ["## 缺口与如实记录（验收不掩盖项）",
                              "## 真机执行边界（诚实声明）"],
}


def missing_headings(rel, doc):
    want = REQUIRED_HEADINGS.get(rel.replace(os.sep, "/"), [])
    if not want:
        return []
    # A heading may carry a parenthetical suffix ("## Errors Encountered（防重复）"), so
    # the test is prefix-matched -- equality would report a heading that is present.
    heads = [line for line in doc if line.startswith("## ")]
    return ["%s: required section heading is gone: %s" % (rel, head)
            for head in want if not any(h.startswith(head) for h in heads)]


def table_shape_problems(doc, rel):
    """Every row of a Markdown table must have the header's cell count.

    An anchor-based edit whose old_string is a PREFIX of a long single-line row
    splits that row (observed twice in this repo: a phase row grew a 4th cell, and
    a new gap row absorbed the previous row's tail cells). The line still parses as
    "something with pipes", so only the cell count reports it.
    """
    out = []
    fence = False
    i = 0
    while i < len(doc) - 1:
        line, nxt = doc[i], doc[i + 1]
        if line.lstrip().startswith("```"):
            fence = not fence
            i += 1
            continue
        if fence or not line.startswith("|") or not nxt.startswith("|"):
            i += 1
            continue
        sep = all(c in "|-: " for c in nxt)      # header | --- | --- |
        if not sep:
            i += 1
            continue
        want = cells(line)
        j = i + 2
        while j < len(doc) and doc[j].startswith("|"):
            if not all(c in "|-: " for c in doc[j]):
                got = cells(doc[j])
                if got != want:
                    out.append("%s:%d: table row has %d cells, header says %d"
                               % (rel, j + 1, got, want))
            j += 1
        i = j
    return out


def gather_files():
    files = [t for t in TARGETS if os.path.isfile(os.path.join(ROOT, t))]
    ev = os.path.join(ROOT, "docs", "evidence")
    for dirpath, _dirs, names in os.walk(ev):
        for n in names:
            if n.endswith(".md"):
                files.append(os.path.relpath(os.path.join(dirpath, n), ROOT))
    return files


def self_test():
    """Every verdict this gate can give must be reachable, or the gate reads as coverage.

    Phase 55 measured the hole: a citation whose line carried only an arm label was skipped as if
    it had passed. These four synthetic cases prove the three new outcomes fire and that the old
    window rule still fires.
    """
    table = [("R5.4-cl", 100, 199, {"R5.4-artifact-bytes-cl"}),
             ("R5.4-msl", 200, 299, {"R5.4-artifact-bytes-msl"})]
    checks = [
        ("arm label owning the cited line must pass",
         check_arm_label("R5.4-artifact-bytes-cl", 150, table), None),
        ("arm label cited 60 lines out of its section must be reported",
         check_arm_label("R5.4-artifact-bytes-cl", 250, table), "REPORT"),
        ("an unknown label must fall through to the identifier rule, not pass silently",
         check_arm_label("R9.9-not-here", 150, table), "NA"),
    ]
    for name, got, want in checks:
        if want == "REPORT" and not got:
            print("GATE IS MUTE: " + name)
            return False
        if want == "REPORT" and got == "NA":
            print("GATE IS MUTE: " + name + " (got NA instead of a violation)")
            return False
        if want != "REPORT" and got != want:
            print("GATE IS MUTE: " + name + f" (got {got!r}, want {want!r})")
            return False
    real = arm_table(open(os.path.join(ROOT, "tests", "acceptance.cpp"),
                          encoding="utf-8").read().splitlines())
    # code_tokens in BOTH directions: a placeholder must not qualify, a real symbol must not be
    # dropped. One direction alone is how a filter quietly becomes a mute button (E44's shape).
    for txt, want in [
        ("（`path:NNN`）", []),                       # metasyntactic, not a symbol
        ("`initA`（", ["initA"]),                    # the shape docs actually use
        ("`std::cout`（", ["std::cout"]),            # re.I must not eat the colons
        # Phase 61's real bug: a 1-char span knocked every later span out of phase, so
        # `hipMemcpyAsync` disappeared behind `Y` in 9 citations.
        ("计时段的 `Y` 拷回主机（发射点 `hipMemcpyAsync`）", ["hipMemcpyAsync"]),
        # An identifier carrying a digit is a symbol; an ARM-style label is not.
        ("`fp16_rel`（", ["fp16_rel"]),
        ("`R5.4-artifact-bytes-cl`（", []),
    ]:
        got = code_tokens(txt)
        if got != want:
            print(f"GATE IS MUTE OR LOOSE: code_tokens({txt!r}) = {got}, want {want}")
            return False
    # A stopword beside a citation checks nothing, so it must not count as an assertion either.
    for txt in ("`double`（", "`static`（", "`return`（"):
        if code_tokens(txt):
            print(f"GATE IS LOOSE: {txt!r} accepted as a symbol - it occurs in every C++ file here")
            return False
    if len(real) < 10:
        print(f"GATE IS MUTE: the arm table of tests/acceptance.cpp parsed only {len(real)} sections")
        return False
    if not any(aliases for _l, _s, _e, aliases in real):
        print("GATE IS MUTE: no printed banner ids were collected, so label citations cannot resolve")
        return False
    # Both banner shapes must resolve, or the rule is alive only for the arms written one way.
    for want in ("R5.4-artifact-bytes-cl", "R6.9-basis"):
        if check_arm_label(want, 1, real) == "NA":
            print(f"GATE IS MUTE: the printed banner {want} resolves to no section")
            return False
    return True


IDENT_SEED = re.compile(r"[A-Za-z_]\w{4,}")
STOPWORD = {"false", "true", "const", "return", "static", "void", "auto", "std", "string",
            "double", "float", "long", "short", "unsigned", "signed", "struct", "class", "uint",
            "int32", "int64", "size_t", "nullptr_t", "constexpr", "explicit", "template",
            "size", "find", "count", "empty", "push", "back", "begin", "endif", "ifdef",
            "define", "include", "cout", "cerr", "main", "else", "nullptr", "struct"}


def code_context(line):
    """The line with comments and string literals removed.

    Without this, the suggester happily offers `Differential` for
    `<< "// Differential verify: 2^20 values ..."` - a word inside a comment. Inserting it next to a
    citation would make the gate green while teaching nobody anything: false checkability is worse
    than counted debt, because counted debt says out loud that nothing was looked at.

    The generator-specific exception: in a code GENERATOR the emitted program is written as a
    string body, so `<< "static int uk_reduce_width_plan(int n) {"` really IS the code the docs cite
    that function for. A streamed body is therefore kept, then re-stripped for its own "//" comments
    - which is how `<< "  // Differential verify: ..."` still ends up suggesting nothing.
    """
    streams = '<< "' in line
    out, i, n = [], 0, len(line)
    while i < n:
        ch = line[i]
        if ch == "/" and i + 1 < n and line[i + 1] == "/":
            break
        if ch == "/" and i + 1 < n and line[i + 1] == "*":
            break
        if ch == '"':
            i += 1
            body = []
            while i < n and line[i] != '"':
                if line[i] == "\\":
                    body.append(line[i:i + 2])
                    i += 2
                    continue
                body.append(line[i])
                i += 1
            if streams:
                out.append("".join(body))
            i += 1
            continue
        out.append(ch)
        i += 1
    text = "".join(out)
    return "\n".join(seg.split("//")[0] for seg in text.split("\n"))


def suggest(cited, start, src_lines, table):
    """A symbol that would make this citation checkable AND truthful - taken from the code.

    Two sources: an arm label if the cited line falls inside a section that has one (the docs cite
    arms by label, so that is the shape that actually fixes it), otherwise an identifier that
    really occurs in CODE (not comment, not string text) on the cited line. Both are verified by the
    same window/label rule afterwards; when neither exists the honest answer is 'no suggestion',
    which means demote the citation or repoint it - not invent a token.
    """
    if table:
        for lab, s, e, aliases in table:
            if s <= start <= e:
                cand = sorted(aliases | {lab})
                cand = [c for c in cand if c not in ("CAL",) and len(c) >= 3]
                if cand:
                    return "label:" + cand[0]
    if start - 1 < len(src_lines):
        coded = code_context(src_lines[start - 1])
        for tok in sorted(set(IDENT_SEED.findall(coded)), key=len, reverse=True):
            if tok.lower() not in STOPWORD:
                return "symbol:" + tok
    return ""


def arm_for(path, src, arms):
    if path not in arms:
        arms[path] = arm_table(src)
    return arms[path]


CLAUSE_END = re.compile(r"[、,;；。）|]|--")   # clause delimiters: text past one belongs to the next claim


def trailing_tokens(line, from_pos, next_anchor, cap=40):
    """Symbols written AFTER a citation, inside the same clause.

    Phase 59 measured 24 of 120 open debt rows in this shape: 「（`src/codegen_hip.cpp:887` 那句
    `Differential verify`）」 - the number first, its subject right after it. The adjacency rule had
    been refusing all of them, because taking text after an anchor is what produced the original
    smearing bug ("LEDGER COMMIT -> main.cpp:529、INFEASIBLE -> main.cpp:241" made 529 responsible for
    INFEASIBLE). The fix is not to keep refusing; it is to bound the tail: stop at the next clause
    delimiter, at the next citation, and after `cap` characters. Within one clause there is only one
    claim, so the symbol cannot belong to anybody else's anchor.
    """
    stop = min(next_anchor, from_pos + cap, len(line))
    m = CLAUSE_END.search(line, from_pos)
    if m and m.start() < stop:
        stop = m.start()
    return code_tokens(line, from_pos, stop)


def self_test_suggestions():
    """The listing must be able to say something, and must not invent: proved on real files."""
    acc = open(os.path.join(ROOT, "tests", "acceptance.cpp"), encoding="utf-8").read().splitlines()
    table = arm_table(acc)
    got = suggest("tests/acceptance.cpp", 2846, acc, table)
    if not got.startswith("label:"):
        print("GATE IS MUTE: no label suggestion for a line inside an arm section")
        return False
    if got[len("label:"):] not in " ".join(acc):
        print(f"GATE IS MUTE: suggested label {got} does not exist in the file")
        return False
    hip = open(os.path.join(ROOT, "src", "codegen_hip.cpp"), encoding="utf-8").read().splitlines()
    got2 = suggest("src/codegen_hip.cpp", 96, hip, [])
    if not got2.startswith("symbol:") or got2[len("symbol:"):] not in hip[95]:
        print(f"GATE IS MUTE: symbol suggestion {got2} is not on the cited line")
        return False
    if suggest("src/codegen_hip.cpp", 887, hip, []):
        print("GATE IS LOOSE: a line with no identifier must not get a symbol suggestion")
        return False
    # And a word that only exists inside a comment/string must not qualify either: 891 is
    # `<< "  // Differential verify: 2^20 values ..."` - `Differential` is prose, not code.
    bad = suggest("src/codegen_hip.cpp", 916, hip, [])
    if bad == "symbol:Differential":
        print("GATE IS LOOSE: suggested the word `Differential` out of a comment/string")
        return False
    if any(w in code_context(hip[915]) for w in ("Differential", "verify", "values")):
        print("GATE IS MUTE: comment/string words survived code_context on the cited line")
        return False
    # The other side of the same rule: a generator's streamed body IS code, so line 853
    # (`<< "static int uk_reduce_width_plan(int n) {"`) must yield that function, not nothing.
    got3 = suggest("src/codegen_hip.cpp", 874, hip, [])
    if got3 != "symbol:uk_reduce_width_plan":
        print(f"GATE IS MUTE: the emitted-source body at :853 suggested {got3!r} instead of "
              f"uk_reduce_width_plan")
        return False
    return True


def main():
    if not self_test():
        sys.exit(1)
    if not self_test_suggestions():
        sys.exit(1)
    verbose = "--verbose" in sys.argv
    list_debt = "--list-debt" in sys.argv
    cache = {}
    arms = {}
    checked = 0
    expanded = 0
    arm_checked = 0
    weak = []
    unverifiable = []
    debt = []
    bad = []
    for rel in sorted(gather_files()):
        try:
            with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
                doc = f.read().splitlines()
        except UnicodeDecodeError:
            continue
        bad.extend(missing_headings(rel, doc))
        bad.extend(table_shape_problems(doc, rel))
        for lineno, line in enumerate(doc, 1):
            # A markdown paragraph wraps: the symbol is often on the PREVIOUS physical line while
            # the citation lands on this one (AGENTS.md's rule 8 was exactly that shape). Joining
            # one line back is what makes those citations checkable instead of counted as blind.
            joined = (doc[lineno - 2] + " " + line) if lineno >= 2 else line
            prev_end = 0   # previous anchor GROUP on this line: a list like
                           # "`verify_fp32` -> `src/runtime.cpp:182`、LEDGER COMMIT
                           # 门 -> `src/main.cpp:529`" must not let the first item's
                           # symbol accuse the second anchor (it did, twice).
            group_at = -1  # start of the group being expanded right now
            group_floor = 0
            group_starts = sorted({mm.start() for mm in ANCHOR.finditer(line)})
            for m in expand_citations(line):
                path, start = m.group(1), int(m.group(2))
                end = int(m.group(3) or start)
                checked += 1
                if m.tail:
                    expanded += 1
                if path not in cache:
                    try:
                        with open(os.path.join(ROOT, path), encoding="utf-8") as g:
                            cache[path] = g.read().splitlines()
                    except OSError:
                        cache[path] = None
                src = cache[path]
                if src is None:
                    bad.append(f"{rel}:{lineno}: cited file does not exist: {path}")
                    continue
                if end > len(src):
                    bad.append(f"{rel}:{lineno}: {path}:{start}-{end} is past the "
                               f"end of the file ({len(src)} lines)")
                    continue
                lo = max(0, start - 1 - WINDOW)
                hi = min(len(src), end + WINDOW)
                nearby = "\n".join(src[lo:hi])
                whole = "\n".join(src)
                # Only tokens that sit BEFORE the citation are attributed to it:
                # these docs are written as "symbol（`path:line`）", and taking text
                # after the anchor smeared the NEXT clause's symbol onto this one
                # (a "LEDGER COMMIT -> main.cpp:529、INFEASIBLE -> main.cpp:241"
                # sentence made 529 responsible for INFEASIBLE and reported false
                # staleness on the day the gate was written).
                # A `path:A/B/C` group is ONE claim written once, before the first number, so every
                # number in it is checked against that same text. The first version of this rule
                # advanced prev_end on the group's head, which left every tail with an empty slice
                # and reported 30 checkable numbers as blind - a strengthening that measured worse
                # than the hole it closed, caught by counting heads and tails apart.
                if m.gs != group_at:
                    group_at, group_floor = m.gs, prev_end
                    prev_end = m.end()
                ctx_at = max(group_floor, m.gs - 60)
                named = [t for t in code_tokens(line, ctx_at, m.gs) if t != path]
                nxt = next((s for s in group_starts if s >= m.ge), len(line))
                for t in trailing_tokens(line, m.end(), nxt):
                    if t != path and t not in named:
                        named.append(t)
                # Arm labels: the shape these docs actually use for the acceptance arms. They were
                # the whole blind spot (SKIP_TOKEN rejects them), so they now resolve against the
                # file's own section table instead of being thrown away. Same adjacency discipline
                # as the identifier rule below: a label elsewhere on the line belongs to another
                # anchor, and accusing with it would be the smearing bug this gate already had once.
                labels = arm_labels(line, ctx_at, m.gs)
                resolved = False
                if labels:
                    if path not in arms:
                        arms[path] = arm_table(src)
                    for lab in labels:
                        verdict = check_arm_label(lab, start, arms[path])
                        if verdict == "NA":
                            continue
                        resolved = True
                        arm_checked += 1
                        if verdict:
                            bad.append(f"{rel}:{lineno}: {path}:{start} — {verdict}")
                if not named:
                    # Three tiers, because "adjacent" and "checkable" are not the same word.
                    #   resolved          - an arm label owns the cited section: checked;
                    #   named (ctx)       - a symbol sits right before the citation: checked;
                    #   weak              - the paragraph carries a symbol but the citation does not
                    #                       (markdown wraps, so `tolerance_for()` often sits on the
                    #                       line above). NOT accused - that smearing bug bit twice -
                    #                       but counted, with its own cap, so "nobody looked at this"
                    #                       is a number instead of a silence;
                    #   unverifiable      - nothing anywhere near the citation. Also capped.
                    if resolved:
                        continue
                    if code_tokens(line) or code_tokens(joined):
                        weak.append(f"{rel}:{lineno}: {path}:{start}")
                        debt.append(("weak", rel, lineno, path, start,
                                     suggest(path, start, src, arm_for(path, src, arms))))
                        continue
                    unverifiable.append(f"{rel}:{lineno}: {path}:{start}")
                    debt.append(("unverifiable", rel, lineno, path, start,
                                 suggest(path, start, src, arm_for(path, src, arms))))
                    continue
                if any(t in nearby for t in named):
                    continue
                if any(t in whole for t in named):
                    hits = [t for t in named if t in whole]
                    # Distance is measured to the NEAREST occurrence, not the first in the file:
                    # "small.find(...) cited at :1632" used to be accused because `find` appears at
                    # line 316 - a generic method name, and the nearest real use was on the cited
                    # line. Reporting the first match made every generic token look stale.
                    def nearest(t):
                        lines_at = [i + 1 for i, l in enumerate(src) if t in l]
                        return min(lines_at, key=lambda n: min(abs(n - x) for x in (start, end)))
                    where = min((nearest(t) for t in hits), key=lambda n: min(abs(n - x) for x in (start, end)))
                    if not (start - WINDOW <= where <= end + WINDOW):
                        bad.append(f"{rel}:{lineno}: {path}:{start}-{end} names "
                                   f"{hits}, but the nearest match sits at {path}:{where} — outside "
                                   f"+-{WINDOW} lines (stale anchor?)")
                # a token that exists nowhere in the file is a wrong-file claim
                else:
                    bad.append(f"{rel}:{lineno}: {path}:{start}-{end} names "
                               f"{named}, none of which occur anywhere in that file "
                               f"(wrong file, or the symbol was renamed)")
    if len(weak) > WEAK_CAP:
        bad.append(f"weakly-anchored citations grew: {len(weak)} > the recorded cap {WEAK_CAP}. "
                   f"The paragraph names a symbol but the citation does not - put the symbol "
                   f"immediately before its own `path:line` so the window check can reach it.")
    if len(unverifiable) > UNVERIFIABLE_CAP:
        bad.append(f"unverifiable anchors grew: {len(unverifiable)} > the recorded cap "
                   f"{UNVERIFIABLE_CAP}. A citation on a line with no checkable symbol is NOT a "
                   f"pass - give it a symbol (an arm label like `[R5.4-cl]`, or the function "
                   f"name) or lower the cap by fixing them.")
    print(f"anchors checked: {checked} in {len(gather_files())} docs "
          f"(+{expanded} numbers beyond the first of a `:A/B/C` group, which the old regex "
          f"never looked at; window +-{WINDOW} lines, arm-range checks {arm_checked}, "
          f"weak {len(weak)}/{WEAK_CAP}, unverifiable {len(unverifiable)}/{UNVERIFIABLE_CAP})"
          f"  [{datetime.date.today()}]")
    if list_debt:
        for kind, rel, lineno, path, start, sug in debt:
            print(f"debt | {kind} | {rel}:{lineno} | {path}:{start} | {sug or '(no suggestion)'}")
        print(f"debt listed: {len(debt)}  (unverifiable {len(unverifiable)}, weak {len(weak)})  "
              f"caps in force: weak {WEAK_CAP}, unverifiable {UNVERIFIABLE_CAP}")
    if verbose:
        for u in unverifiable:
            print("  unverifiable " + u)
        for u in weak[:20]:
            print("  weak " + u)
    if bad:
        print(f"STALE OR WRONG ANCHORS: {len(bad)}")
        for b in bad:
            print("  " + b)
        return 1
    print("all doc->code anchors agree with the files they cite")
    return 0


if __name__ == "__main__":
    sys.exit(main())
