# Phase 60 — 剩下的"注释文本"债：8 组错行号 + 一个把臂名挡在门外的正则

主机 `yangweijie-pc`（Windows，`amd=yes / apple=no / intel=no`）。纯仪器与记录工作，无真机读数。

## 这一相做了什么

Phase 59 结束时挂着 83 条计数债，其中 **11 条 dead**（引用的那一行今天没有 ≥5 字的代码标识符）。
逐条读下来，dead 的真面目是两类：

1. **句子引用的是"发射出去的注释文本"** —— 那一行确实是那句话指的东西，但它里面没有可以当符号用的
   标识符。修法是重写句子去引用**包含它的函数/变量**，而不是给注释配一个假符号。
2. **行号已经漂了** —— 与上一相同的老问题，本相又抓出 8 组。

| 指标 | 相初 | 相末 |
|---|---|---|
| 计数债行数 | 83 | **69** |
| `weak` | 37 | **30** |
| `unverifiable` | 46 | **39** |
| 臂归属核 | 99 | **104** |
| 被核行声明 | 383 | **413** |

```bash
python probe/phase59_debt_triage.py docs/evidence/phase60/triage-sheet.txt
python probe/phase56_anchorlabel_control.py            # 6 条（新增 L5/L6）
python probe/anchors_check.py
```

## 仪器的一处真缺口：臂名里的 `/`

`tests/acceptance.cpp` 有一个臂的标题就是 **`R6.2/R6.4`**（一条臂同时覆盖两条需求行），
文档有 6 处用 `[R6.2/R6.4]` 这种**完全可核**的形状引用它——而 `arm_labels()` 的正则
`[A-Z][\w:.-]` 不含 `/`，于是这 6 条被判定为"没有任何符号"，进了 `unverifiable`。
放行 `/` 之后：`arm-range checks` 99→**104**、`unverifiable` 少 6。

⇒ 这条与 E21/E24 相反方向但同源：**判据的形状与文档实际写的形状必须一致**，否则门在自己的
词法里把真实断言当成沉默。新控制两条（L5/L6，随臂表现取）：

- **L5** `R6.2/R6.4` 指向自己段内的行 → 绿且 `weak/unverifiable` delta 都是 0。
- **L6** 同一个标签指向**下一个臂**的行 → 红，且报文同时点名两个主人。

L6 第一次跑出来是 **GREEN**，因为我把标签注入成了 `` `R1.1/R1.2` ``（没有方括号）——
即"控制自己不成立"的经典形状；补上括号后才报红。这条按 E38 记着：新腿的控制自己也要被验。

## 一个自己刚造的错：`smem` 只有 4 个字母

Phase 59 给 `src/codegen_hip.cpp` 的重置行配了 `` `smem` `` 当符号——它过不了 `code_tokens` 的
**≥5 字**规则（这条规则存在是因为 `run`/`int` 这类短词会匹配到 500 行外造成噪声）。
于是那两条引用从"没符号"变成"配了个不被认的符号"，债没减。本相改配
`uk_conv_plan`（`src/codegen_hip.cpp:451`，在 435 的 ±25 窗口内）才真的降下来。
⇒ 配符号时要同时过两道：出现在那一行附近，**并且长度≥5**。

## 重读之后改的 8 组行号（全部 grep 现取，门随后复核）

| 记录里的写法 | 那个号码今天是什么 | 改为 |
|---|---|---|
| tile=0 时 smem 的重置在 codegen_hip 的 412 行（2 处） | 412 是 `: p.ocb == 4` 那行发射文本 | `uk_conv_plan` 里的 `smem` 重置在 `src/codegen_hip.cpp:435` |
| 边界写进生成器注释（739） | 739 是"any FP32 order"那句；"partial FREEZES"已在 762 | `FREEZES`（`src/codegen_hip.cpp:795`） |
| 计时段 d2h 发射点 844（phase33 / phase34 两份证据） | 844 是 D9 的解释性注释 | `hipMemcpyAsync`（`src/codegen_hip.cpp:946`），并注明原文号码是当时的位置 |
| 发射程序 CPU 参考 805、runner 的 562/530（phase47 表格） | 805 是注释；那两个号码今天是 kernel 名选择与 gemm 发射 | `cpu_reduce_ref`（`src/codegen_hip.cpp:840`）、`cpu_reduce_blocked`（`src/runner_ocl.cpp:660`）与 `cpu_reduce_blocked`（`src/runner_metal.mm:593`） |
| ε 口径在 main.cpp 的 311-312 | 311 是一条 `--emit` 的注释 | `epsilon_rel`（`src/main.cpp:552`） |
| 文件侧 dims 走 kml_front 的 108/113（2 处） | 108 今天是 dtype 的拒绝；且"上界不在清单里"这一条**已被 D10/R1.2 关掉** | 历史部分降级为文本；活引用给 `parse_int_wide`（`src/kml_front.cpp:127`） |
| Metal 的 resident 预算取声明的 32 GiB（110 行） | 110 今天是 `int TM = 0, TN = 0;`；G6 后来落地了 | 数字降级为文本 + 现状态指向 `max_ws`（`src/runner_metal.mm:112`）与 `ws_refuse`（`src/runner_metal.mm:213`） |
| `nP = min(L, 4096)` 生成期定死（829） | 829 是 D9 的注释 | 降级为文本；活引用给 `uk_reduce_width_plan`（`src/codegen_hip.cpp:874`） |

**值得单独说一句**：最后四行的"结论"本身也已经过期（上界校验、G6 交叉校验、D9 运行时宽度）。
仪器只能保证"号码指的是什么"是对的；**句子对不对要靠重读**，这正是逐条还债的产出。

## 剩下 68 条

`--dry` 两个自动化都报 **0 条**可安全落笔。剩下的构成：约 40 条"句子与号码之间没有共同可核 token"
（要重写句子，属于内容工作）、5 条引用行仍无 ≥5 字标识符、3 条围栏内转录（不改写）。
棘轮钉在 `weak 30 / unverifiable 39`（记录写完后再量一次，仍是这两个数）。

## 全套与守卫

```bash
cmake --build build && ctest --test-dir build --output-on-failure   # 7/7
python probe/phase56_anchorlabel_control.py                          # 6/6
python probe/phase58_groupcitation_control.py                        # 6/6
python probe/phase59_trailing_control.py                             # 5/5
```

- `triage-sheet.txt` 是本相结束时的全量债目并排印；`gate-output.txt` / `debt-list.txt` 是最终读数。
- 探针不改 `src/`，`arm_probe_guard` 退出时字节一致、无残留标记、`git status` 与进入时相同。
- 三条字节腿基线（`docs/evidence/phase49|52|54/emit/`）与两个非 HIP 语义臂的片段一字未动。
