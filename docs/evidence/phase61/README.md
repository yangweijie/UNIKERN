# Phase 61 — 三个"判据读不到文档"的洞：一个反引号、一个大小写、一个插入位置

主机 `yangweijie-pc`（Windows，`amd=yes / apple=no / intel=no`）。纯仪器与记录工作，无真机读数。

## 读数

| 指标 | Phase 60 收口 | 本相收口 |
|---|---|---|
| 计数债行数 | 69 | **19** |
| `weak` / cap | 30 / 30 | **8 / 8** |
| `unverifiable` / cap | 39 / 39 | **11 / 11** |
| 被核行声明 | 413 | 413 |

三相连做（59→60→61）：**120 → 19**。本相一条句子都没重写——债掉下去的全部原因是**门自己读不到断言**，
三个洞都在门这一侧。

```bash
python probe/anchors_check.py                         # 汇总行含 weak/unverifiable 与上限
python probe/phase59_debt_triage.py docs/evidence/phase61/triage-sheet.txt
python probe/phase61_locator_names.py --dry           # 只加"那一行本来就是它"的定位名
python probe/phase61_stale_repoint.py --dry           # 显式列表：一条一行号，逐条有理由
ctest --test-dir build                                # 7/7
```

## 洞一：一个字符的 code span 把整行的反引号配错队

`TOKEN` 原先要求 span 内容 ≥2 字符。`` `Y` `` 是合法 Markdown，但正则拒绝配对它之后，
扫描从它的**收尾反引号**继续 —— 于是这一行后面每一对都错一位，`hipMemcpyAsync` 这种主语直接消失。
后果不是误报而是**漏报**：9 条形状完全正确的引用被计成盲。改成 `{1,80}` 后短 span 照旧被 ≥5 长度规则
丢掉，唯一的差别是配对不再错位。
`self_test()` 双向四对：占位符 `path:NNN` 拒、`initA`/`std::cout` 留、停用词拒、
**`Y` 之后的 `hipMemcpyAsync` 必须留**。摘除控制：把 `{1,80}` 改回 `{2,80}` 跑门 ⇒
`GATE IS MUTE OR LOOSE: code_tokens('计时段的 `Y` …') = [], want ['hipMemcpyAsync']`（实测过，rc=1）。

## 洞二：`^[A-Z]+\d` 带着 re.I，把带数字的真标识符一起吞了

这条规则是为了排除 `R5.4`/`G10`/`T1` 这类臂名与需求号。可 `SKIP_TOKEN` 是 `re.I` 编译的，
于是 `fp16_rel` 也被当成"臂名"丢掉——而那三条引用的主语就是
`fp16_rel`（`include/unikern/components.hpp:133`）。把它拆成大小写敏感的独立判据后：
`fp16_rel` 可核、`R5.4-artifact-bytes-cl` 仍不算符号（两个方向都进 `self_test()`）。
摘除控制：给 `ARMISH` 加回 `re.I` ⇒ 门报 ``GATE IS MUTE OR LOOSE: code_tokens('`fp16_rel`（') = []``。

## 洞三：我自己的工具把名字插进了引用的反引号**里面**

`probe/phase61_locator_names.py` 第一版用 `text.index(cited, m.start())` 定位插入点——那是**开引号之后**
的位置，于是产出 `` ``plan_reduce_width` src/runner_ocl.cpp:190` `` 这种破 span，共 **20 处**。
门没有立刻报红（引用仍能匹配），是打印的 dry 输出里那对连续反引号让我看见的。
修法是插入点先跨过引用自己的开引号，并把被破坏的 20 处按 `` `X` `path:line` `` 复原。
⇒ 这是 E58 家族（"写进去了"不等于"语法还在"）在 Markdown 上的同形：**改文档的工具也要在改完立刻读它产出的形状**，
只看 rc 会把工具的 bug 当成门的行为。

## 本相重指的过期行号（`probe/phase61_stale_repoint.py` 的显式列表，逐条带理由）

下表的"旧写法"一列**故意不写成 `path:line`**：那些号今天都是别的东西，写成活引用就等于重新主张它
（E62/E65 家族，本相之前已响过 6 次）。活引用只在"改为"一列，且各自带主语。

| 旧写法（文本） | 那行今天是什么 | 改为 |
|---|---|---|
| main.cpp 的 557 行（4 处，Phase 22 的"门 #3 首跑战果"清单） | 557 是 `}`；提交由 540 的 `r.verified` 拒绝把关 | `r.verified` 把关的 `src/main.cpp:540` |
| runner_ocl 的 434 行（4 处） | 434 是 `double total = 0.0;`；那句 "no Intel GPU platform/device found" 在 516 追加 | `uk_ocl_log`（`src/runner_ocl.cpp:516`） |
| codegen_hip 的 415 行（3 处） | 415 是 occupancy 查询；` source=env` 在 463 发射 | 463 行（发射 ` source=env` 的地方，行内没有可当符号的标识符） |
| kml_front 的 89 行 | 89 是未知键拒绝；dtype 字段在 106 读 | `dt_txt` 读到的 `src/kml_front.cpp:106` |
| codegen_hip 的 963 行 | 963 是 kernel-only 计时行；`shard_rows` 在 1014 计算 | `shard_rows`（`src/codegen_hip.cpp:1047`） |
| runner_metal 的 65-66 行 | 65 是 `UNIKERN_ERROR` 宏；线程组上限报告在 76 | `maxThreadsPerThreadgroup`（`src/runner_metal.mm:76`） |

## 剩下 19 条，以及为什么**故意**留着

全量清单在 `debt-list.txt` 与 `triage-sheet.txt`（每条都印着"句子 + 它指的代码行"）。分类：

- **5 条指向 codegen_hip 的 463 / 638 / 924 行**：句子说的是**发射出去的文本**（` source=env` 后缀、
  写死的 `0.03125f`、`SR*K` 分片式）。那一行上没有任何标识符能代表这个断言，配上 `std::endl`
  之类只会造出"能变绿但什么都不核"的假可核（E64 的同族）。
- **3 条指向 hcs.cpp 的 95 行**：`os << "missing required fields:";`——主语是一条消息串，同样不给假符号。
- **4 条在 ``` 围栏或引用块内**（phase31 的三行转录、phase36 的 blockquote）：**转录不改写**。
- **5 条是"当时状态"的英文记录**（main.cpp 的 86 行 atoi、D9 之前 OCL/Metal 仍返回 `min(L,4096)`
  的 164/116 行、Phase 44 注释所在的 809 行）：位置已随 D9/R1.2 过期。`stale_repoint` 里那条长替换
  没匹配上原文，所以这 5 行仍计数——它们等的是一次逐条重读，而不是工具的一次批量写盘。
- **2 条是本相自己新造的**：`uk_reduce_width_plan` 的发射点、E67 那一行的第二个引用。
  债表会因为写文档而上涨，这本身就是要能看见的东西。

⇒ 债的下界不是 0，而是"句子与号码之间确实没有共同标识符"的那部分。把它清零的唯一诚实办法
是重写句子去引用函数名——那是内容判断，一次一条，不能被批量工具代劳。

## 守卫

`probe/phase61_locator_names.py` 与 `probe/phase61_stale_repoint.py` 都按文件回滚：改完立刻跑门，
红则整文件复原并打印报文。两个都不写 `src/`。
三条字节腿基线（`docs/evidence/phase49|52|54/emit/`）与 `[R20-nonhip]` / `[R20-nonhip-expr]` 的片段一字未动。
