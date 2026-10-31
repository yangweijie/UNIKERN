# Phase 59 — 还计数债时抓出 20+ 个错行号；尾随符号改按子句边界归属

主机 `yangweijie-pc`（Windows，`amd=yes / apple=no / intel=no`）。本相全是 CPU 仪器与记录工作，
无真机读数。

## 这一相做了什么

Phase 58 结束时门 #3 挂着 **120 条计数债**。本相不是把它们"降到好看"，而是**逐条重读**——
结果是：其中一批根本不是"没符号"，而是**行号已经错了**，只是过去没人能核。

| 指标 | 相初 | 相末（记录写完后） |
|---|---|---|
| 被核行声明 | 329 | **383** |
| `weak` / cap | 61 / 104 | **37 / 37** |
| `unverifiable` / cap | 59 / 91 | **46 / 46** |
| 计数债行数 | 120 | **83** |

```bash
python probe/phase59_debt_triage.py docs/evidence/phase59/triage-sheet.txt   # 每条债 + 它指的代码行
python probe/phase59_debt_rewrap.py --dry                                     # 只换行、不改字的 join 计划
python probe/phase59_debt_adjacency.py --dry                                  # 候选符号插入（必须人审）
python probe/phase59_trailing_control.py docs/evidence/phase59/trailing-controls.txt
python probe/anchors_check.py
```

## 仪器的两处实质变化（各自有控制/自测）

1. **尾随归因（bounded trailing）**。这些文档也写「引用在前、主语在后」的形状
   （发射点 codegen_hip 的 887 行；那句 `Differential verify` 写在 840 行的引用之后），
   而旧规则只看引用**前面**的 60 字——因为历史上"取引用之后的文本"曾把下一小句的符号算到前一条账上。
   现在取"之后"的部分，但**边界**是硬的：遇子句分隔符（`、,;；。）|`）、下一条引用、或 40 字即停。
   五条控制证明它既不放水也不误伤：`probe/phase59_trailing_control.py`
   H1 形状对→绿且不计数，H2 数字错→红且**报文点名那个符号**，H3 一行两条款时只报错的那条，
   H4 符号在 `、` 之后→不归因（计数 +1），H5 符号距引用 >40 字→不归因（计数 +1）。
   转录在 `trailing-controls.txt`。
2. **停用词不再算符号**。修债的工具差点把 `double` 插到一条 KML 引用前面——它就"永久绿"了，
   因为 `double` 在这仓每个 C++ 文件里都有。`code_tokens` 现在拒绝 STOPWORD 成员，
   `self_test()` 两个方向都核：占位符 `path:NNN` 被拒、`initA` 与 `std::cout` 必须留下、
   `double`/`static`/`return` 必须被拒。**能变绿不等于在断言**，这是 E21/E24 同族的第三种形态。

另外：第 56 相的控制脚本原先**假定 cap 就等于实测**（"多一条 weak 必然压红"）。债一降下来，
它就去指控一条正常的门——现改为从门的汇总行**读**上限，再决定该期望红还是绿（E61 家族第二次，
这次是"控制对常数做了假设"而不只是"行号会漂"）。

## 还债时抓出来的错行号（全部 grep 现取，随后由门复核）

| 记录里写的 | 那行今天是什么 | 改为 |
|---|---|---|
| 计时段 Y 拷回主机的发射点 `:887`（8 处：AGENTS / acceptance-mvp / findings / progress / task_plan） | 887 是 `<< "  {"`，一个裸花括号 | `hipMemcpyAsync`（`src/codegen_hip.cpp:946`）的 `"d2h tY"` |
| 差分验证实例发射点 `:840`（3 处） | 840 是 `<< "}"` | `Differential`（`src/codegen_hip.cpp:916`） |
| conv/gemm 共用 init 的精确性注释 `:587`（4 处，含 phase36 证据 README） | 587 是 `acc += xv * wv;`，没有 ≥5 字的标识符 | `max_rel`（`src/codegen_hip.cpp:610`）那句发射注释 |
| "the partial FREEZES" 的生成器注释 `:739`（2 处） | 739 是另一句注释（"any FP32 order…"），FREEZES 已漂到 762 | `FREEZES`（`src/codegen_hip.cpp:795`） |
| 三后端的 CPU 参考同口径 `:805` 与 runner 的 `:562`/`:530`（findings） | 805 是注释；runner 那两行今天是 kernel 名选择与 gemm 发射 | `cpu_reduce_ref`（`src/codegen_hip.cpp:840`）、`cpu_reduce_blocked`（`src/runner_ocl.cpp:660`）与 `cpu_reduce_blocked`（`src/runner_metal.mm:593`） |
| Metal 侧宽度旋钮（AGENTS rule 4 原先写成 runner_metal.mm 的 136 行，但那一格没有邻接符号） | 136 是 `ReduceWidthPlan plan_reduce_width(int64_t L) {` ✓ **行号是对的** | 补上它自己的函数名 `plan_reduce_width`（`src/runner_metal.mm:136`） |
| phase30 README 的"宽度在生成期定死 `:724`" | 724 今天是一条 payload_mb 输出；而且 **D9 之后这个结论本身已不成立** | 数字降级为文本 + 就地注时点演进，活引用给 `uk_reduce_width_plan`（`src/codegen_hip.cpp:874`）与 `UK_REDUCE_WIDTH`（`src/codegen_hip.cpp:880`） |
| findings 的"验证实例发射点 `:840`、宽度 `:829`"（Phase 30 口径） | 829 今天是 D9 的解释性注释 | 同上行重写：历史口径留作文本，活引用只给今天为真的两条 |

**这批错号的共同点**：它们不是新写的，而是**旧记录在新仪器下第一次被读**。
之前 `:887`/`:840`/`:587` 一直"绿"，是因为这些行的引用从来没有邻接符号可核 ——
计数债的意义正在此处：它把"没人看过"变成一个数，而不是让门假装看过。

## 三种修法与各自的边界

- **join（只改换行）** `probe/phase59_debt_rewrap.py`：把一个被 Markdown 折行的引用并回上一物理行，
  **一字不改**。前提是被并入的符号本来就通过了门的窗口判据。7 处成功。
  它**拒绝**表格行、围栏、引用块、以及以列表标记开头的行——并错行会吞掉 `- ` 让条目变成正文。
- **补符号** `probe/phase59_debt_adjacency.py`：只允许"句子已经写了这个 token、且该 token 真的在那个
  号码 ±25 内"。`--dry` 打印每条候选与它所在句子**供人审**；本相审出三条不能落笔的：
  `double`（停用词，见上）、phase30 的 `uk_reduce_part`（句子的主语是 `uk_reduce_sum`）、
  phase47 表格里的 `kRefReduceWidth`（从**相邻表格行**借来的词，不是本行的主张）。
  ⇒ 这个工具今天**一条都没自动落笔**，全部改成手工逐条重写；工具留着做 dry 审阅面。
- **降级为文本**：凡是"当时写在 :NNNN"的历史位置，去掉 `path:` 前缀。历史数字不改写，
  但也不许冒充今天的活引用（AGENTS 记录约定，E59/E62 家族）。

## 剩下 83 条是什么

两个自动化都重跑 `--dry`：现在**0 条**还能由"并回折行"或"把句子已有的主语挪到引用前"修好。构成是：

- **80 条**：句子与自己引用的那个号码之间**没有共同的可核 token**。其中 16 条的引用行本身没有
  ≥5 字的代码标识符——它们指向**发射出去的注释文本**（`<< "  // …"` 那种），要再降必须把句子重写为
  引用**函数名**而不是引用注释；那是内容判断，不是仪器能替作者做的。
- **3 条**在围栏转录内（`docs/evidence/phase31/README.md`）——**转录不改写**。
- "表行 17 / 隔空行 10 / 列表标记 8" 是**修法**的分类，不是债的分类（同一批行的不同切法）。

棘轮钉在 **weak 37 / unverifiable 46**：这两个数字是本相逐条重读之后的实测值，下降的原因是**读**，
不是放松判据。

## 全套与守卫

```bash
cmake --build build && ctest --test-dir build --output-on-failure   # 7/7
python probe/phase59_trailing_control.py                             # 5/5
python probe/phase58_groupcitation_control.py                        # 6/6
python probe/phase56_anchorlabel_control.py                          # 4/4（上限改为现读）
```

- `gate-output.txt` / `debt-list.txt` / `triage-sheet.txt` / `trailing-controls.txt` 是本相结束时的读数。
- 三个新探针都走 `arm_probe_guard`：不改 `src/`，退出时字节一致、无残留标记、`git status` 与进入时同。
- 三条字节腿基线（`docs/evidence/phase49|52|54/emit/`）与两个非 HIP 语义臂的片段一字未动。
