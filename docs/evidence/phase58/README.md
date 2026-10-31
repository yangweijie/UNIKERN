# Phase 58 — 引用组里的每一个数字都被核；把两档"看不见"降到实测值

主机 `yangweijie-pc`（Windows，`amd=yes / apple=no / intel=no`）。本相全部是 CPU 仪器与记录工作，
没有任何真机读数，因此没有设备数字需要转录。

## 这一相改了什么

`anchors_check.py`（ctest 第 3 条门）原先只匹配 `path:NNN` 加可选 `-NNN`，所以一条 acceptance.cpp
的**组引用**（五个数字 2747/2787/2800/2811/2863 写在一个 `path:` 后面）**只有第一个数**被核。本仓实测
这种组引用共 15 处、40 个数字里 **30 个（9% 的全部引用）从未被看过** —— 与 E21/E24 同族：多项声明只验一项，却打印 `all agree`。

现在 `ANCHOR` 抓 `(\d+(?:[-/]\d+)*)`，`expand_citations()` 把一组拆成若干 `Citation`，逐个数走同一条
窗口/臂归属规则；组前的符号或臂标签对**组内每个数**都成立（一条声明 = 多个断言）。

## 关键副产品：本相自己的仪器错了两次，都有前后读数

**错法一**：第一版把 `prev_end` 在组的**头部**就推进到组尾，于是每个尾巴拿到**空 ctx**——尾巴全部落进
`weak/unverifiable`。"收紧"当场测得比原洞更糟：

| 状态 | 被核的数 | 尾巴数 | arm 归属核 | weak | unverifiable |
|---|---|---|---|---|---|
| 只核头部（改前） | 310 | 0 | 90 | 80 | 69 |
| 拆组但尾巴吃空 ctx（本相第一版，错） | 340 | 30 | 90 | 89 | 90 |
| 尾巴共享组上下文（修正后） | 340 | 30 | **98** | 84 | 86 |

`arm-range checks` 从 90→98 是"尾巴真的在被核"的唯一直接读数；只看 `checked` 会把它读成多算了 30
次。判据写进了 `Citation`/`group_floor` 旁边的注释，控制 G3 负责在回归时变红。

**错法二（写本相证据时被抓）**：`named = code_tokens(ctx)` 里 `ctx` 是**切出来的子串**，而子串常常
**从一个引用 span 的收尾反引号开始**，于是 `TOKEN` 把 "`）两处发射、`" 配成一对、真正的
`` `initA` `` 反而不在任何配对里 ⇒ **一行里第二个及以后的引用，前面那个符号一律看不见**。改为按
**绝对位置**在整行上取 span（`code_tokens(line, lo, hi)` / `arm_labels(line, lo, hi)`）后：
`weak` 一次从 83 掉到 **61**（22 条被误计为盲的引用重新变成可核），并且当场报出一条**从不存在的符号**
（下一节）。这条由控制 **G6** 守着：它要求的不是"变红"，而是**报文点名 `order_ok`**——只变红分不清
"窗口违规"与"cap 被顶"，前者才是这条腿承重的证据。

## 控制（每条腿都有自己的摘除控制）

`probe/phase58_groupcitation_control.py`，六条注入，逐条对着真门跑，转录在
`group-controls.txt`（baseline rc=0 才注入，临时文件在 `finally` 删除）：

- **G1** 尾巴号越界 → 红，且报文点名的就是那个尾巴数（证明尾巴被读）。
- **G2** `[R20-nonhip]` + `2800/2863` → 红，报文同时点名两个主人：
  `R20-nonhip owns R20-nonhip (2747-2845) but line 2863 falls in R20-nonhip-expr (2846-2934)`。
  **这正是四行真实记录的形状**——收紧先抓出债，不是先改文档再找理由。
- **G3** 两个数都在自己臂内 → 绿且 `weak/unverifiable` delta 均为 0。这条就是上面那个空-ctx bug
  的回归守卫：它若复发，尾巴会被计成债，delta 就不是 0。
- **G4** 一行两组，第二组含尾巴 999999 → 红；第一组的符号不为第二组开脱（`prev_end` 纪律在拆分后
  仍然成立）。
- **G5** 同一批声明改成"一数一引用"同样红 → 证明 G2 的红来自**数字**，不是"标签在场就红"。
- **G6** 一行两个引用，第二个的数字错位、且它的符号恰好落在上一段 span 的收尾反引号之后 → 红，
  并且**报文必须点名那个符号**（`names ['order_ok'], but the nearest match sits at :2802`）。
  这是"错法二"的守卫：符号看不见时这条也会红，但红的是 cap 增长，不是窗口违规。

```bash
python probe/phase58_groupcitation_control.py            # 退出码 0 = 六条都按预期
python probe/anchors_check.py                            # 第 3 条门
```

## 收紧之后重读债目，抓出四条真错引用（全部 grep 现取）

| 记录里的旧写法（下面的数字都是**文本**，不是活引用：它们当时就是错的） | 那个数字上实际是什么 | 现在的活引用 |
|---|---|---|
| codegen_hip 的 548/549 与 996/997、runner_ocl 的 133/134、runner_metal 的 84/85（"三后端的 init 逐字相同"） | 548/549 是 conv 的 launch 维度、996/997 是 gemm 的 tile 常量；ocl 133/134 与 metal 84/85 是**注释**行 | `initA`（`src/codegen_hip.cpp:604/1122`）两处发射、`initA`（`src/runner_ocl.cpp:136`）、`initA`（`src/runner_metal.mm:87`）；HIP 侧多一个 `static`，"逐字相同"就表达式而言成立 |
| kml_front 的 89/93（"前端明确允许 fp32\|fp16\|int8"） | 89 是未知键拒绝、93 是 `op` | `dtype`（`src/kml_front.cpp:106/108/112`） |
| kml 的 107/117（"成本与规范键认 dtype"） | **两个数都对**：107 是规范键里的 `to_string(t.dtype)`，117 是 `dtype_size` | 只缺邻接符号，现写作 `dtype`（`src/kml.cpp:107/117`） |
| 四行"Phase 53/54 写下的 acceptance.cpp 2747/2787/2800/2811/2863 整体偏移约 99 行" | 那些数是**当时**的位置；且 2863 本就不属于 `[R20-nonhip]` | 降级为"当时写在 :2747/:2787/:2800/:2811/:2863"（不带 `path:` 前缀就不是活引用；G2 证明它带上前缀就会红） |

前两条是**假引用**：门 #3 从来没看过它们，因为组里被核的第一个数恰好也有符号可核（或落在窗口内）。
这就是"每条新腿必须跑自己的摘除控制"之外的那一半——**收紧一条腿，等于重读它照到的所有句子**。

## "错法二"当场抓出的一条：规格文档引用了一个全仓不存在的符号

`docs/acceptance-mvp.md` 的 R6.3 一节旧写法是：「CLI `run` 只在 verified=PASS 时 LEDGER COMMIT
（符号名 ledger_commit_gates_on_verify，引用 main.cpp 的 557 行，P4 不变量）」——这里**故意不把它写成
`path:line`**：那是一句已被否证的话，写成活引用就等于重新断言它。按位置取符号之后，门 #3 第一次
读到那个符号，`grep -rn` 的结果是**该标识符只出现在这一行文档里，代码中从未存在**；而它引用的 557 行
实际是函数收尾的 `return 0;`。不变量本身是真的，位置也是真的：拒绝在
`r.verified`（`src/main.cpp:540`），提交行打印在 553 行。已按现码改指，并在原句里注明旧符号不存在。

这条是本仓"存在性判据死于第二次出现"（E21/E24）的**对偶**：一个从未出现过的名字被引用了 20 多个相，
而门一直绿——因为**只有第一个数被核**、且**切片的反引号配错了队**。两个盲区叠在一起，一个假符号就
等价于一个真不变量。

## 第 56 相那条控制自己失效了（同一类错误，换了个位置）

`probe/phase56_anchorlabel_control.py` 把 `CL_ARM_LINE = 2846` 写死（当时是 `[R5.4-…-cl]` 臂首行）。
Phase 57 插入 `[R20-nonhip-expr]` 后 2846 属于另一个臂，L2 于是**注入了一条真正错位的引用**，
报出 `CONTROLS THAT DID NOT BEHAVE` —— 门没错，控制过期。现改为每次运行从 `arm_table()` 现取
（本轮读到 `own=2936 neighbour=2847`），控制若找不到该标签所属的臂直接 `sys.exit` 而不是悄悄绿。
写死行号的代价本相实测两次：一次在文档里，一次在**检查文档的仪器**里。

## 债的下线与棘轮

`probe/phase58_debt_repair.py` 重写成**共用门自己的解析器**（`import anchors_check`）：第一版自带
一条更严的正则（必须有反引号、只取第一个数），于是 16 条债被报成"citation not found in that
line"——定位器 bug 冒充作者错误（E50 别名族：一条规则两份实现，悄悄不同）。修复后：

- `citation not found` 类：**16 → 0**；
- 新增诚实跳过类 `inside a fenced transcript`（3 条，phase31 证据 README 里 781/724/808 三行转录）——
  **转录不改写**，为了一个计数器去动逐字记录是本仓明确禁止的事。
  以后若某条新转录把 cap 顶红，规定动作是在围栏**外面**补一句带符号的活引用，围栏内容不动；
- 本相结束时再跑一次 `--dry`：**可安全落笔的还剩 0 条**，120 条债的构成是
  `symbol not mentioned` 59（代码里有符号但那一句没提到它，插进去就是编）/
  `no truthful suggestion` 48（引用的那一行本身没有可当符号的代码标识符，要人重读）/
  `already adjacent` 9（已经核上了）/ `fenced transcript` 3 / `ambiguous` 1。
  这些留在计数债里是**正确的**：棘轮的作用不是让它们变绿，而是让"没人看"保持是一个数。

棘轮按实测收紧，**只降不升**：

| 档 | 改前 cap | 本相最终测得 | 新 cap |
|---|---|---|---|
| `WEAK_CAP` | 104 | 61 | **61** |
| `UNVERIFIABLE_CAP` | 91 | 59 | **59** |

被核的行声明数从 310 涨到 **363**，其中 **31 个是以前从不看的尾巴**；臂归属核从 90 涨到 **99**。
（**读数在全部记录写完之后取**：本相的 README、findings、progress、附录每加一条 `path:line`，分母就
涨一次——这本身是有意义的事实，仪器的覆盖面是被记录喂大的。）
（本相证据文件自己就顶红过一次 cap：写第一版表格时把旧错误行号当活引用抄了进去，`weak` 从 79 冲到
101。修的是**写法**——旧数字降级成文本、活引用各自带符号——不是阈值。这是 E59 的同族第四次。）

## 全套与守卫

```bash
cmake --build build && ctest --test-dir build --output-on-failure   # 7/7
python probe/phase56_anchorlabel_control.py                          # 4/4（行号现取）
python probe/phase58_groupcitation_control.py                        # 6/6
```

- `gate-output.txt` 是最终门输出（`weak 61/61, unverifiable 59/59`，绿）。
- `debt-list.txt` 是 `--list-debt` 的 120 行全量债目（每行带 suggester 的建议或
  `(no suggestion)`），下一相接手时从这里读，不用重新猜。
- `group-controls.txt` 是六条控制的完整转录，末尾自带结论行（E56）。
- 两个探针都走 `arm_probe_guard`，退出时核 `src/` 字节未变、无 `ARM-PROBE` 残留、
  `git status --porcelain src/` 与进入时一致。
