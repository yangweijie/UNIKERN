# Phase 47 — D8 落地（用户选定口径 A）：reduce 的差分基准换成规格本来就要求的**分块档**

主机：Windows 11 + AMD RX 6600M（gfx1032），ROCm 7.1。决定发生在 Phase 46 取证之后，
由用户选定 **A 换分块档参考**；B（容差随 n 绑定）与 C（维持现状）被放弃。

## 改了什么（四行代码 + 一条腿）

| 位置 | 内容 |
| --- | --- |
| `kRefReduceWidth` `include/unikern/core.hpp:124` | `kRefReduceWidth = 1024` —— 参考档**自己**的分片数，与内核的 pass-1 宽度无关 |
| `cpu_reduce_blocked` `src/reference.cpp:36` | `cpu_reduce_blocked`：跨步部分和 + 索引序折叠（规格第二档） |
| `cpu_reduce_ref`（`src/codegen_hip.cpp:840`） | 发射程序里的参考实现由标量序改成第二档（本表原文写 805，是 Phase 47 当时的位置） |
| `cpu_reduce_blocked`（`src/runner_ocl.cpp:660`）与 `cpu_reduce_blocked`（`src/runner_metal.mm:593`） | 两个进程内 runner 同口径（原文写 562/530，已被后续插入推走） |
| `[R5.4]` `tests/acceptance.cpp:2132` | `[R6.9-basis]` 新腿（含反恒等断言） |

**关键设计约束：参考的分片数绝不能等于内核的 pass-1 宽度。**相同形状 = 仪器给自己打分，
`max_rel` 会恒 0 而不再测量任何东西（本仓 E32 的同形失败）。这条不是注释里的道德要求，
而是腿里的断言 `basis != kern` 与 `div_new > 0`，并由控制 B1 证明它能红。

## 中性与有效性，都要实测

1. **对已入库证据中性**（Phase 46 的预言，本相在真机上验证）：shipped verify 输入
   （常量 `2^-6`，n=2²⁰/2²⁴）上两档**逐位相同**且等于闭式 ⇒ `ctest` 与真机读数不变。
   真机五档全 `verify=PASS max_rel=0.000000e+00`（`device-verify.txt`）：
   reduce 1M/16.7M/67.1M、gemm 512³ `kernel_ms=0.118630`、conv 256²×32 `kernel_ms=7.210240`，
   `migrations=0 checks=6` 全部保持。
2. **D8 的假 FAIL 被消掉**：交替 `1e6/1e-6`、n=2²⁴ ——
   旧基准（标量档）与内核之差 `4.160e-02 > 2⁻⁵` ⇒ 更准的一方被判 FAIL；
   新基准（分块档 @1024）与内核之差 `2.413e-04 ≤ 2⁻⁵` ⇒ 同一内核同一数据判 PASS。
   比值 172 倍，腿里以下界 10 倍钉住（不冻结单次噪声读数）。
3. **产物字节**：reduce 268 → **276 行**，`sha256=44231aef0e9567eb96a015bc18272fa24b861f65691577524b6111845387c5e4`；
   **gemm 与 conv 空 diff**（447 行 `7a451491…`、542 行 `09e9b570…`，与 Phase 45 存的基线逐字节相同）
   ——见 `emit-diff.txt`。跨算子无回归这条靠 diff 证明，不靠叙述。

## 摘除控制（4/4 按预期红，`removal-controls.txt`）

| 控制 |  sabotage | 首条红断言 |
| --- | --- | --- |
| B1 | `kRefReduceWidth := 16384`（等于内核宽度） | `basis != kern`（`:2065`）——反恒等腿承重 |
| B2 | 分块循环塌成 1 个部分和（算术上=标量档） | `a == b`（`:2037`）——中性断言承重 |
| B3 | 发射产物里的基准分片数退化成 1 | `UK_REF_REDUCE_WIDTH = 1024` 缺失（`:2081`） |
| B4 | runner 改回 `cpu_reduce` | runner 源码文本子腿（`:2104`），`runner_tu` 仍绿 |

守卫复原 byte-identical（4 个文件、零残留、零 git 漂移），末轮 `ctest` 6/6。

## 为什么只改 reduce（不是偷懒）

gemm/conv 的 CPU 参考与内核**共享同一段嵌套累加次序**，那是 `[R20]` 按字节钉住的硬约束
（AGENTS 第 3 条：改算术顺序 = 改结论，全部已入库 `max_rel=0` 会改含义）。给它们"换档"等于
改折叠次序，性质上属于 B/C 类口径变更；reduce 之所以能换，是因为它的两档在 shipped 输入上
被证明逐位相同。这一层差别如果哪天要给 gemm/conv 也上第二档，必须重新做 Phase 46 那种中性证明。

## 仍未闭合

- 第二档与内核仍同属"部分和顺序折叠"家族，只是分片数不同。它**不是**高精度真值：
  宽输入上两档都离闭式更远或更近都可能（Phase 46 结论 3）。规格要的就是这两档，所以这是
  合规口径，但别把它写成"绝对正确"。
- `verify_fp32()`（`src/runtime.cpp:191`）仍只接 fp32 档；整型位级档目前只在拒绝路径上成立（D11 遗留）。
- Phase 44 的仪器分歧（探针 vs 出厂对 4096 基线的 `kernel_ms`）仍未查。

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure        # 6/6
python probe/phase47_basis_removal_probe.py docs/evidence/phase47/removal-controls.txt
./build/unikern.exe run --hcs samples/hcs_amd.json --op reduce --dims 16777216 0 0 \
  --vendor amd --ledger build/p47_dev/reduce24.sqlite3
c++ -O2 -std=c++20 -o build/uk_ref_tiers.exe probe/phase46_reference_tiers.cpp
./build/uk_ref_tiers.exe basis                     # 换档前的设计度量
```
