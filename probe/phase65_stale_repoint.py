# -*- coding: utf-8 -*-
"""Phase 65:机械重指因 f16 生成器插入而漂移的文档引用(phase62_stale_repoint 家法)。

数据来源是门自己的裁决(probe/anchors_check.py 的 STALE 输出):
  - 符号型:"names [SYM] ... nearest match sits at PATH:NEW" → 重指到 NEW,并 grep 验证
  - 臂型:"LABEL owns LABEL (START-END) but line N falls in X" → 该引用写的是臂起点,
    重指到 START(grep 臂头注释验证)
  - 臂中段特指(无符号、语义上指向臂内某行)→ 打 MANUAL,人工核
每次运行幂等:重指过的行不再出现在 STALE 里。
"""
import io
import re
import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

# 运行门,收集 STALE 行(排除 GATE IS MUTE / 汇总行)
out = subprocess.run([sys.executable, 'probe/anchors_check.py'],
                     capture_output=True, text=True).stdout
stale = [l.strip() for l in out.splitlines() if l.strip().startswith(('AGENTS.md', 'docs', 'findings.md', 'progress.md', 'task_plan.md'))
         and ('stale anchor' in l or 'falls in' in l or 'past the' in l or 'names' in l)]
print('stale citations reported:', len(stale))

SYM = re.compile(r'^(.+?):(\d+): (\S+?):(\d+)(?:-(\d+))? names \[(.+?)\], but the nearest match sits at \S+?:(\d+)')
ARM = re.compile(r'^(.+?):(\d+): (\S+?):(\d+)(?:-(\d+))? — (.+?) owns .+? \((\d+)-(\d+)\) but line \d+ falls in')

doc_cache = {}
def doc_lines(p):
    if p not in doc_cache:
        doc_cache[p] = io.open(p, encoding='utf-8').read().split('\n')
    return doc_cache[p]

src_cache = {}
def src_text(p):
    if p not in src_cache:
        src_cache[p] = io.open(p, encoding='utf-8').read().split('\n')
    return src_cache[p]

repoints = []   # (doc, lineno(1based), old_token, new_token, kind)
manual = []
for l in stale:
    m = SYM.match(l)
    if m:
        doc, dl, path, old, _oldend, syms, new = m.groups()
        syms = [s.strip().strip("'") for s in syms.split(',')]
        src = src_text(path)
        hit = any(s in src[int(new) - 1] for s in syms)
        if not hit:
            manual.append(('symbol-not-on-reported-line', l))
            continue
        repoints.append((doc, int(dl), '%s:%s' % (path, old), '%s:%s' % (path, new), 'symbol'))
        continue
    m = ARM.match(l)
    if m:
        doc, dl, path, old, _oldend, label, start, end = m.groups()
        src = src_text(path)
        # 臂头行:start-1 是 0 基;臂头形状 "  // -------- LABEL..."
        head = src[int(start) - 1] if int(start) - 1 < len(src) else ''
        if ('-------- ' + label) in head or ('-------- ' + label.split('-')[0]) in head:
            repoints.append((doc, int(dl), '%s:%s' % (path, old), '%s:%s' % (path, start), 'arm'))
        else:
            manual.append(('arm-head-not-at-reported-start', l))
        continue
    manual.append(('unparsed', l))

print('auto repoints:', len(repoints), '| manual:', len(manual))
for kind, l in manual:
    print('  MANUAL', kind, '|', l[:150])

# 应用:同一 doc 行可能有多条;逐行替换 token(带计数断言)
per_doc = {}
for doc, dl, old_t, new_t, kind in repoints:
    per_doc.setdefault(doc, []).append((dl, old_t, new_t, kind))
changed = 0
for doc, items in per_doc.items():
    lines = doc_lines(doc)
    for dl, old_t, new_t, kind in items:
        line = lines[dl - 1]
        n = line.count(old_t)
        if n == 0:
            print('  SKIP(already repointed?)', doc, dl, old_t)
            continue
        assert n == 1, (doc, dl, old_t, n)
        lines[dl - 1] = line.replace(old_t, new_t)
        changed += 1
    io.open(doc, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print('applied', changed, 'repoints')
