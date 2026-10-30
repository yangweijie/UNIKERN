# Phase 54 — MSL 的两半：语法检查改成"按探针决定"，字节锚是本机能补的那一半

主机：**Windows 11 + AMD RX 6600M（gfx1032）**。这一相同时处理两件以前被混为一谈的事：
"这台机器能不能解析 Metal 语法"（不能，且现在有了原因）与
"这台机器能不能看见 Metal 产物被改过"（**能**，字节比较不需要前端）。

## 1. 先量，再写：这台机器上没有任何前端认 Metal 语言

`msl-probe.txt` 是原样读数。门 #7 现在**逐个**探本机可及的 clang（`clang_list()`，
`probe/generated_program_check.py:108`），两步（`metal_probe()`，`probe/generated_program_check.py:125`）：
第一步编一段不依赖头的源码问"`-x metal` 认识吗"，第二步才问"`#include <metal_stdlib>` 有吗"。
本机两个候选都停在第一步：

```text
PROG probe msl 7.1/clang.exe=no ... clang: error: language not recognized: 'metal'
PROG probe msl wasi-sdk-34.0-rc.3-x86_64-windows/clang.EXE=no ... language not recognized: 'metal'
PROG gap msl reason=no clang here recognizes -x metal (probed 2 front end(s), ...)
```

（手测同样两条：ROCm 7.1 的 clang 21 是 `Compute-Mirrors/llvm-project` 的裁剪版，
wasi-sdk 34 的 clang 23.1-rc3 只带 wasm 目标；`-x metalpp` 也一样被拒。）

**这条改动的价值不在结果而在形状**：以前 `PROG gap msl` 后面跟的是一句**断言**
（"没有 Metal 前端、没有 Metal 头"），现在跟的是**探针读到的原话**，并且如果哪台主机的 clang 真的
认 Metal，同一段代码会接着跑控制、再解析三份 `.metal`——跳过路径会自动升级成开火路径，
不需要有人记得来改代码。

**没被 exercised 的一半，明说**：本机无法执行"Metal 分支的控制 + 真产物"那条路径（前端不存在）。
它与 `runner_metal.mm` 连 `-fsyntax-only` 都做不了是同一类缺口：代码在、路径未跑。

## 2. 字节锚不需要前端：`[R5.4-artifact-bytes-msl]`

`[R5.4-artifact-bytes-msl]` `tests/acceptance.cpp:3070` 起，形状与 Phase 53 那条一致：逐 op 现场发射，把 `uk_apple.metal`
与入库基线逐字节比较，**先打印 `identical`/`DRIFT` 再断言计数**（`tests/acceptance.cpp:3051`）。

```text
  [R5.4-byte-msl] gemm identical fresh=2893B stored=2893B
  [R5.4-byte-msl] conv identical fresh=2105B stored=2105B
  [R5.4-byte-msl] reduce identical fresh=1885B stored=1885B
[R5.4-artifact-bytes-msl] 3/3 stored Metal artifacts are byte-identical ...
```

基线入库 `emit/<op>/uk_apple.metal`：

| 产物 | 行数 | sha256 |
| --- | --- | --- |
| `emit/gemm/uk_apple.metal` | 61 | `f25c6461a9dd4dd5f806374036c2634652141b14c777db32636c4027c00f7528` |
| `emit/conv/uk_apple.metal` | 40 | `cd28317ac289fe9dff457e966ffaed49d0f387cefdc6d17e13a9144a0cd0f8db` |
| `emit/reduce/uk_apple.metal` | 36 | `c4cf8d302bc041a91dc836c53dc9c31a97f2483dc3659fd9155db39a861819d8` |

## 3. 三条控制（`controls.txt`），期望各不相同

| 控制 | 破坏方式 | acceptance | HIP 腿 | CL 腿 | MSL 腿 | 门 #7 | 含义 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M1 | 改**发射出去**的 Metal 注释 | **RED** | 打印 | `3/3` | `reduce=DRIFT` | GREEN | 全套仪器里只有这条腿看得见 MSL 产物 |
| M2 | 改发射出去的 OpenCL 注释 | **RED** | 打印 | `reduce=DRIFT`（先红） | 一行都没打 | GREEN | 三条字节腿是三条独立轴 |
| M3 | 改生成器自己不发射的注释 | GREEN | 打印 | `3/3` | `3/3` | GREEN | 中性对照：不是"改动即红" |

M1 就是这条腿存在的理由：同一次改动在 Phase 53 结束时什么都不红——门 #7 不解析 `.metal`
（探针已证明它连语言都不认），HIP/CL 两条字节腿都不看 `.metal`。
M2 里 MSL 腿**没有输出**是因为 CL 腿排在它前面先 assert——顺序假象，不是证据；
这正是三条腿都**先打印再断言**的原因。

## 4. 归属装置跟着补第三条腿（Phase 53 的规则第二次生效）

`shadow_root` 现在把同一 `--emit` 目录里的 `uk_amd.cpp`/`uk_intel.cl`/`uk_apple.metal` 三份都种进
影子树。**不补的后果是实测过的**（Phase 53 的 `shadow-pre-fix.txt` 就是同一失效形状）：第三条腿会在
**每个**影子控制上因"基线不存在"假红。补完之后重跑所有用影子归属的控制套件：

```text
phase49 全部按期望落地（attributed RED 由 reads==1 那条 assert 归属）
phase50 all 3 controls matched (K1: gate-only; K2: both; K3: neither)
phase51 all 3 controls matched (R1/R2 text-arm territory, R3 gate #6 territory)
phase52 all 3 controls matched (Q1 ocl half, Q2 not a byte fingerprint, Q3 hip half independent)
```

四条旧套件的 `want` 一个字节都没改。

## 边界

- 字节一致 ≠ 能编译 ≠ 跑通。`.metal` 在本机**从未被任何前端解析过**；Apple 侧执行证据在
  `docs/evidence/phase10/`（macOS 主机）。
- M1/M2 只覆盖发射文本的**注释**；算术文本由 `[R20]` 按字节钉住，那是另一条轴。
- 基线只有 fp32 三算子的形状。

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure                     # 7 条门
python probe/generated_program_check.py | grep msl             # 两个前端的探针原话
python probe/phase54_mslbytes_control_probe.py docs/evidence/phase54/controls.txt
sha256sum docs/evidence/phase54/emit/*/uk_apple.metal          # 与上表三条哈希对上
env -u SOURCE_DIR ./build/unikern_acceptance.exe | grep msl    # SKIPPED 分支
```
