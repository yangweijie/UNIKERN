# UNIKERN MVP 验收矩阵（R1-R10）— Phase 8/9/10

- 日期：2026-10-27（v2 复核修订；v3 收尾审计 ε 实算；v4 校准闭环：R7.3/R3.3 实测→常量校准 + 版本号递增 + R5.3 toolchain 指纹，G2/G3 闭合；**v5 Phase 9：Intel 真机验证（G4 Intel 半闭合）——ocloc AOT + 进程内 OpenCL runner + 真机对拍/账本闭环/校准，Intel 行 ⛔→✅，Apple 维持受阻**；**v6 Phase 10：Apple 真机验证（G4 全闭合）——Metal 运行时 JIT（无需 Xcode）+ 进程内 ObjC++ runner + 真机对拍/账本闭环，Apple 行 ⛔→✅**；**v6.1：Phase 10 Apple 证据全量重采**，本文件 Apple 段的每个 ε/带宽数字均与 `docs/evidence/phase10/` 当前转录一致（此前引用的 `ε_rel=99.6471`、`5.18 GB/s` 来自未入库的一次性读数，已按重采读数替换；ε_rel 口径为比值 |C−T|/T，非百分数**）；**v7 Phase 11：G7 闭合——非 GEMM 树不再被静默当作 GEMM 执行/回写，改为 `op_support_gap()` 谓词 + 生成物自拒 + runner 入口自拒 + CLI 门三层显式拒绝，逐层"摘掉即红"（docs/evidence/phase11/）；R10 行据实改判 🟡，并新增 G6/G8 入册**）；**v8 Phase 12：KML 文本前端（R1.1 声明半边 + R1.2 完整违规清单，三算子按 arity 校验、layout 白名单、children 路径前缀）+ R10.3 每后端覆盖报告三轴 + G9 账本键按算子 arity 编码（gemm 键逐位不变）；R10 行回改 ✅、R1 行细化为"声明已验 / 执行半边仍缺"；G8/G9 入册并闭合**；**v9 Phase 13：conv/reduce 执行面（Apple 先行）——MSL 的 uk_conv/uk_reduce 内核 + per-backend 能力面（`capability_gap`）+ 异构 emit，M4 上 verify=PASS max_rel=0 与账本 COMMIT/HIT；G6 闭合（实测 Metal 工作集上限 12.7 GiB 并入第二道门）；新增 [R13] 段，R1/R5/R6/R7/R10 行据实更新**；**v10 Phase 14：Intel/OpenCL 的 conv/reduce——`ocl_kernel_source` 三算子内核 + `ocl_op_gap`/per-backend 覆盖 + runner_ocl 按算子分派；三份 .cl 过 clang 的 OpenCL 前端（负测可红），runner host 侧仅编译检查（本机无 Intel 设备，运行未验）；能力面变三向；新增 [R14]，Phase 13 中"OpenCL 仍 gemm-only"的结论据实标记取代**；**v11 Phase 15（2026-10-28）：AMD 收尾——`hip_runnable_program` 按算子分派，conv 出 `uk_conv`、reduce 出 `uk_reduce_part`+`uk_reduce_sum`（各自独立程序、CPU 参考内嵌、gemm 程序逐位不变）；`op_support_gap()` 翻转为三算子 covered（layout 是唯一拒绝轴）；RX 6600M 真机 conv/reduce/gemm 全部 verify=PASS max_rel=0 → COMMIT → 同键 HIT；新增 [R15]，R5/R6/R10 行据实更新，[R10.2] 的 op 拒绝臂改为"不可用后端诚实失败"臂**；**v12 验收收尾（2026-10-28）：两项真机成果落账——① G1 执行半边的 AMD 侧闭合确认（G1 全闭合，缺口表据实改写）；② G5/R8.5 驻留守卫（Phase 16，ea76c2f）真机验证：HIP 三程序内嵌 uk_mmalloc 驻留断言，RX 6600M 三算子真机 run 全 `migrations=0`，当前二进制上复核重采（backends / emit 三源行数 / 三 run×COMMIT+HIT / ctest 2/2 绿，`docs/evidence/phase15/phase15-recheck-v12.txt`）；Intel conv/reduce 真机运行维持如实标注（本机无 Intel 设备，backends 实测 intel=no）**；**v12.1 独立复核修订（2026-10-28）：① 汇总矩阵 R1 行与 R1 详段系 v8/v9 旧文未随 v11/v12 更新（仍写"AMD/Intel 执行半边待下一批"与"rc=2 且不建账本"），与本文件 G1 全闭合结论矛盾——已据实改判（AMD 真机 COMMIT→HIT、Intel 本机实测 `COVER covered`+`RUN FAILED` rc=1 零落库，旧 rc=2 读数标注为 Phase 12 时代）；② Phase 14 补遗引用的失败消息「no Intel GPU platform/device found」改注出处（Mac 侧构建 + `uk_ocl_log`（`src/runner_ocl.cpp:516`）），并补 Windows 构建的实测消息（消息随构建而异，rc=1 与零落库两承重事实不变）；③ Phase 9 ocloc AOT 证据产物三件（build/phase9-emit/）工作区误删已由 git 恢复；④ 自捉修复：`unikern ledger <path>` 子命令静默忽略路径参数（main.cpp 硬编码 kDefaultLedger，G7 同类静默）——已改为尊重参数并重验**
- 规格：`.monkeycode/specs/unikern-mvp/requirements.md` v1.1（R1-R10）
- 构建环境：Windows 11 + Git Bash；CMake 4.4.0 + Ninja；MinGW g++ 13.2.0（C++20，`-static` 链接，E2 修复）；**Phase 9（Intel 段）另用 MSVC 2022 Community（cl.exe 14.44 + vcvarsall 环境）构建，OpenCL 经 oneAPI 2026.1/lib/OpenCL.lib 导入库链接 + third_party/CL 厂商头（18 头，Apache-2.0），构建标志含 -DUK_HAS_OCL**（MSVC 编译旗标 /W4 + /utf-8 + _CRT_SECURE_NO_WARNINGS；GCC 风格 -Wall/-Wextra 按编译器翻译）
- 构建类型：**Release**（`build/CMakeCache.txt:25 CMAKE_BUILD_TYPE:STRING=Release`；编译旗标 `build/build.ninja:278 FLAGS = -O3 -DNDEBUG -std=c++20 -Wall -Wextra`，`CONFIG = Release`）——MinGW/GNU 的 Release 默认即 `-O3 -DNDEBUG`；E5 前提（Release 会剥除断言）在此配置下被真实行使，测试文件 `#undef NDEBUG` 保断言存活，ctest 2/2 绿
- 真机：**AMD Radeon RX 6600M（gfx1032，8 GB，14 CU）**，ROCm 7.1（`HIP_PATH=D:\Program Files\AMD\ROCm\7.1\`）；**Intel Arc B390（OpenCL 3.0 NEO，驱动 32.0.101.9150，96 CU，27207761920 B 全局显存，2.5 GHz）**，oneAPI 2025.3/2026.1 + System32 OpenCL/ze_loader（Phase 9 实机；设备事实经 OpenCL API 实测，不照抄平台标签 "(integrated)" 字样）；**Apple M4（Metal 4，unified memory，macOS 26.6.2 arm64，CommandLineTools SDK）**（Phase 10 实机；设备事实经 MTLDevice API 实测——name=Apple M4、hasUnifiedMemory=1、maxThreadsPerThreadgroup=1024；本 SDK 无 CU 数量 API，HCS compute_units=40 为声明值，不照抄）
- 代码基线：commit `c7339d4`（Phase 8 首版）+ 独立复核修复（tests 三处补测 + 本报告修订 + 证据入库）+ 收尾审计修复（`src/main.cpp` LEDGER COMMIT 的 ε 改为 |C − T| 实算）+ Phase 9（Intel：runner_ocl.cpp 进程内 OpenCL runner + BackendIntel 真探测 + `main.cpp --vendor` + CMake OpenCL 接入 + acceptance R10 镜像；commit 见 Phase 9 会话记录）
- 验收测试：`tests/acceptance.cpp`（`#undef NDEBUG`，E5）→ ctest 目标 `acceptance`；R6/R8 真机项用真实 `unikern run` 命令收集，**不在单测中伪造**
- 证据产物：**本目录引用的命令输出已作为文本转录入库** `docs/evidence/phase8/`（run/solve/ctest/acceptance/backends 转录 + 三份生成源 + 账本行转储），fresh checkout 可直接比对，也可按转录内命令原样复跑

## 状态图例

| 标记 | 含义 |
|------|------|
| ✅ 本机已验 | 本 session 用真实命令执行并通过（命令与输出摘录见下） |
| 🟡 部分已验 | CPU 侧已验，存在明确缺口（缺口如实列出） |
| ⛔ 真机受阻 | ~~仅剩 Apple Silicon~~ **符号已退役（v6）**：AMD/Intel/Apple 三厂商真机执行均于 Phase 8/9/10 完成，矩阵中无残留 ⛔ 项 |

## 汇总矩阵

| 需求 | 验收方式 | 关键证据 | 状态 |
|------|----------|----------|------|
| R1 KML 前端与原语树 | 单测：canonical_form 唯一性 + 前端类型名拒绝；v7 增 `--op` 声明位；v8 增 KML 文本前端；v9 增"声明→Apple 真机执行"闭环；**v12.1 据实改判（本行原为 v8/v9 旧文未随 v11/v12 更新）：AMD 的声明执行已真机闭合（G1 全闭合），Intel 侧声明过能力门后因本机无设备诚实失败** | `[R1.1/2] KML text front end ... OK`；`run --vendor apple --kml samples/kml_conv.json` → `verify=PASS max_rel=0`、`LEDGER COMMIT`；**AMD 同一声明真机 COMMIT→HIT（v12 复采，`docs/evidence/phase15/phase15-recheck-v12.txt`）**；`run --hcs samples/hcs_amd.json --vendor intel --kml samples/conv.kml`（本机，全量命令转录 `docs/evidence/phase15/intel-r1-command.txt`）→ `COVER intel:ocl primitive=covered` + `RUN FAILED` rc=1、solution 零落库（2026-10-28 本机实测；旧"`--vendor amd/intel` rc=2 且不建账本"读数属 Phase 12 能力拒绝时代，彼时为真，已被 v11 能力面翻转取代） | ✅（R1.1 声明+执行：Apple 真机 ✅、AMD 真机 ✅、Intel 编译级待真机（本机无设备）；R1.2 ✅、R1.3 ✅） |
| R2 多面体可行域 F | 单测：独立复算 5×5 网格过滤期望 | `[R2] filter 22/25, violation records + \|X\| OK` | ✅ |
| R3 确定性代价 C | 单测：10⁴ 次同线程逐位一致 + 常量采纳 + 版本随 HCS 透传 | `[R3] C bitwise-stable over 10^4 repeats`；`CALIBRATED constants_version 1 -> 2` | ✅ |
| R4 唯一最优解 | 单测：等价类字典序裁决 + 不可行终止 | `[R4] argmin (128,128) via canonical key` | ✅ |
| R5 三厂商后端生成 | 单测：三源非空 + 头部指纹与 toolchain + 纯函数 + 跨厂商 API 隔离；CLI `--emit` 行数；v9 增 per-op 内核结构臂；v10 增 OpenCL 三算子内核结构臂；**v11 增 HIP 三算子程序结构臂（[R15]）** | `[R14] OpenCL sources per op ... OK`、`[R15] HIP sources per op: conv/reduce programs carry only their own kernels ... OK`；conv/reduce 的 HIP 程序过 `hipcc --offload-arch=gfx1032` 真编译（`docs/evidence/phase15/phase15-compile-and-gemm-identity.txt`） | ✅（生成侧三厂商三算子；**执行侧 Apple 三算子真机、AMD 三算子真机、Intel 三算子编译级**） |
| R6 差分验证 | 单测：容差函数 verify_fp32 五路径 + v9 的 conv/reduce 共享 CPU 参考手算用例；真机：生成程序/runner 自带 CPU 对拍 + CLI run | `[R13] conv/reduce CPU references exact ... OK`；Apple 的 gemm/conv/reduce 均 `verify=PASS max_rel=0`；**AMD 的 conv/reduce/gemm 也 verify=PASS max_rel=0（v11，RX 6600M；v12 复采六轮同 PASS 且 UNIKERN_RESULT 尾 `migrations=0`，`docs/evidence/phase15/phase15-recheck-v12.txt`）**；Intel 侧 verify 代码路径只过编译检查（本机无 Intel 设备，运行未验） | ✅（Apple 三算子真机；AMD 三算子真机；Intel 三算子待真机） |
| R7 解账本 SQLite | 单测：往返/命中含 tile/事务崩溃恢复/实测与 kernel_ms 持久化；CLI 校准闭环；R7.3 场景 A 校准门控四臂；v8 增 R7.1/G9 键身份臂；**v9 增 conv/reduce 实测行端到端** | `[R7.1] op_signature encodes the op's full arity ... OK`；conv `LEDGER COMMIT ε_rel=334.619` → 同键 `LEDGER HIT` → 再 COMMIT `ε_rel=243.931`；reduce `ε_rel=10.9052` / HIT 轮 `8.86814`（`docs/evidence/phase13/phase13-conv-reduce-device.txt`） | ✅ |
| R8 多级存储与流式 | 单测：场景 A/B 放置记录内容断言；真机 streamed | `[R8] scenario A/B placement records explicit + distinct OK`；`mode=streamed shards=64 prefetch_d=2`，e2e 40.5 s（AMD）；**Apple M4 场景 A 真机 resident 执行 PASS（零拷贝语义，placement=unified_memory 单测已验）** | 🟡（AMD 流式 ✅；Apple 场景 A resident ✅；**Intel/Apple resident-only，流式不实现**——生成程序横幅声明此边界，payload > 0.85×上限时 runner 如实 `UNIKERN_ERROR what=stream_refused` 拒绝（R10 无静默降级），如实标注） |
| R9 HCS 导入 | 单测：必填缺失全量报告 + 无误报 + 指纹 | `[R9] complete missing-field rejection + fingerprint OK` | ✅ |
| R10 UCM 能力覆盖 | 单测：三厂商映射 + availability 语义 + 跨厂商 API 词汇隔离；CLI `backends`；v7 增 op 能力面三层拒绝臂；v8 增 R10.3 覆盖报告三轴；v9 增 per-backend 异构臂；v10 增三向能力面；**v11 增 [R15]：HIP 三算子 covered（layout 是共享谓词唯一拒绝轴），HIP 程序按算子分派 + 每源只带自己内核** | `[R14] OpenCL sources per op ... OK`、`[R15] HIP sources per op ... OK`；三后端 × 三算子全 `COVER ... primitive=covered` 同表共存，layout 拒绝仍点名 `layout=colmajor`；toolchain 轴单列（本机 apple=absent） | ✅（R10.1/R10.2/R10.3 均有实现与回归位；**R10.2 的 F 约束合流仍属 V1**） |

## 逐项验收

### R1 — KML 前端与原语树

**要求**：R1.1 三类算子的 tile/layout/原语树声明；R1.2 非法树拒绝并返回含违规节点的结构化错误；R1.3 每棵合法树唯一 canonical_form。

**验收方式**：`tests/acceptance.cpp` R1 段 + ctest。唯一性用四组性质检验：同一树恒等；可交换子节点两种插入序同串；深层树递归归一（子树多重集相同、顺序不同 → 同串）；tile/dtype/dims 任一不同 → 异串。类型拒绝检验 `op_from_string("gemmm")`、`dtype_from_string("fp64")` 等返回 `std::nullopt`（`src/kml.cpp:26-38`）。

**证据**（本 session 实际命令；全量转录 `docs/evidence/phase8/ctest_release.txt`）：

```
$ ctest --test-dir build --output-on-failure
Test project D:/git/c/gpu-math/build
    Start 1: smoke
1/2 Test #1: smoke ............................   Passed    0.11 sec
    Start 2: acceptance
2/2 Test #2: acceptance .......................   Passed    0.25 sec

100% tests passed out of 2

$ ./build/unikern_acceptance.exe
[R1] canonical uniqueness + illegal type-name rejection OK
```

**状态：✅ 本机已验（v12.1 据实改判；v8-v10 期间曾因执行半边未齐保留 🟡，本段旧文同 R1 行一并过期）**。R1.3 唯一性本机已验。R1.2 缺口在 v8 闭合：KML 文本前端落地（`kml_import`/`kml_export`），非法声明一次列全所有违规（未知/拼写错误字段、op 与 dtype 名、dims 个数与非整数与非正数、layout 不支持、子节点带路径前缀），拒绝即 rc=2 且不进求解。R1.1 的执行半边已按厂商逐家闭合：Apple 真机（v9 声明驱动 512³ COMMIT/HIT；Phase 13 conv/reduce 真机）；**AMD 真机（v11/Phase 15：conv/reduce/gemm 全 COMMIT→HIT，v12 复采含 migrations=0）**；**Intel 侧为编译级**（v10/Phase 14：三算子 .cl 过 clang 的 OpenCL 前端；本机无 Intel 设备——2026-10-28 实测 `run --hcs samples/hcs_amd.json --vendor intel --kml samples/conv.kml`（全量转录 `docs/evidence/phase15/intel-r1-command.txt`）以 `COVER intel:ocl primitive=covered` + `RUN FAILED` rc=1 收场、solution 零落库，是诚实失败而非能力拒绝（转录内根因逐字可见：本机构建期无 OpenCL 导入库 → runner stub，且本机无 Intel 设备/平台），真机运行待 Intel 机器）。v8 也说清 layout 口径：只接受 "NT"（三后端 emitted kernel 实际按 A[M][K]/B[K][N]/C[M][N] 行主序索引的标签），其它值在**前端**就拒——不是收下后按行主序默默执行，那正是 G7 的同类静默。

### R2 — 多面体可行域判定（F）

**要求**：R2.2 可行候选入 X；R2.3 不可行候选输出约束违反记录并淘汰；R2.4 报告 |X|。

**验收方式**：构造 vram=200 KiB 的 HCS，使文档化的 5×5 tile 网格 {16,32,64,128,256}²（`tm`/`tn` 在 `sketch` `src/solver.cpp:16-17`）被谓词 `m*n*4B*3 ≤ cap`（`feasible` 在 `src/solver.cpp:42-57`）切成 22 可行 / 3 不可行；测试内**独立复算**该期望值（不复制 solver 状态），再断言 `feasible_count`、`worst` 违反记录（约束名 + `tile=/need=/cap=` 明细）。

**证据**：

```
[R2] filter 22/25, violation records + |X| OK
```

违反记录样例（首条，枚举序首个越界者）：`constraint="tile_working_set<=device_capacity"`，`detail` 含 `tile=128x256 need=393216B cap=204800B`。CLI 侧同口径：`solve` 输出 `|X|=25`（满容量）/ 不可行时 `INFEASIBLE` 退 3（`INFEASIBLE` 打印在 `src/main.cpp:269`，`print_infeasible`）。

**状态：✅ 本机已验**。

### R3 — 确定性解析代价模型（C）

**要求**：R3.2 同线程同一 x 恒同值（无随机、无学习参数、无可变全局状态）；R3.3 HCS 常量更新后下次求解采用新常量并记录常量版本号。

**验收方式**：design.md Test Strategy #1 —— 同输入重复求解 **10⁴ 次**，断言 Solution 全字段逐位相等（`==` 于 double 与各字段）。常量采纳：2³⁰ 归约树（访存主导，8 GiB 必迁字节），统一内存带宽 400→200 GB/s，断言 C 恰好约翻倍（1.95×–2.05×）且求解报告 `constants_version`。

**证据**：

```
[R3] C bitwise-stable over 10^4 repeats + HCS constants adopted OK
```

**状态：✅ 本机已验**。R3.2 完整通过。R3.3 完整通过（v4）：版本号随 HCS 透传
（`solve` 报告所用 HCS 的 `constants_version`，`src/solver.cpp`），自增机制在
calibration 流程里——`calibrate` 子命令从实测行导出校准 HCS 时 `constants_version`
+1（真机实测：1 → 2，`docs/evidence/phase8/calibrate.txt`），下次求解采用新常量并
报告新版本。P1 确定性不受影响：`solve` 仍是 (hcs, tree) 的纯函数，账本状态只在
calibration（构造新 HCS 文件）时进入系统。

### R4 — 唯一最优解求解器

**要求**：R4.1 按 (C, canonical_form) 字典序求 argmin；R4.2 同线程逐字节同解；R4.4 X 为空时返回不可行结论 + 首个违反记录并终止。

**验收方式**：满容量下占用惩罚对面积 ≥ 128×128 的 tile 恒为 1.0，形成**等 C 平局组** {(64,256),(128,128),(128,256),(256,64),(256,128),(256,256)}——枚举序先遇 (64,256)，而规范键 `"m=128,n=128"` 字典序最小，故 argmin 必为 (128,128)：返回它即证明选择由 (C, 键) 驱动而非枚举序驱动（P2 唯一性）。不可行终止用 vram=3000 B（小于最小 tile 工作集 3072 B）。

**证据**：

```
[R4] argmin (128,128) via canonical key + infeasible termination OK
```

CLI 同口径：`SOLVED  x* tile=128x128  c_ms=12.5387  |X|=25`（与 progress.md Phase 2 记录一致）。不可行解：`feasible_count=0`、`c_ms=0`、`placement_record` 空、`worst[0].detail` 含 `tile=16x16`。

**状态：✅ 本机已验**。

### R5 — 三厂商后端代码生成

**要求**：R5.1 三后端独立生成、输入仅限 KML 与 HCS（P7）；R5.2 可加载执行的可执行构件；R5.3 产物记录 HCS 指纹与工具链版本指纹。

**验收方式**：单测遍历 `make_backends()`：源非空、头部 5 行含 `fp=<hcs_fingerprint 前 12 hex>`、各厂商语言标记（`hip/hip_runtime.h` / `metal_stdlib` / `__kernel`）、同输入二次生成逐字节相同、**跨厂商 API 词汇隔离**（Metal 源无 `hip`/`__global__`，OpenCL 源无 `metal_stdlib`/`#include <metal`/`[[buffer`，见 R10 段）；再以树 dims（4096→1024）与 HCS vram（8→4 GiB）变化验证产物只随 (KML, HCS) 输入变化。CLI `solve --emit` 产出三文件并报行数。

**证据**（转录入库 `docs/evidence/phase8/solve_emit.txt` 与 `emit/` 三源）：

```
$ ./build/unikern.exe solve --hcs samples/hcs_amd.json --emit uk_evidence/emit --ledger uk_evidence/solve.sqlite3
SOLVED  x* tile=128x128  c_ms=12.5387  |X|=25  hcs_fp=80394649
EMIT amd:hip -> uk_evidence/emit/uk_amd.cpp lines=390
EMIT apple:metal -> uk_evidence/emit/uk_apple.metal lines=61 (toolchain absent)
EMIT intel:ocl -> uk_evidence/emit/uk_intel.cl lines=53 (toolchain absent)

$ wc -l uk_evidence/emit/uk_amd.cpp uk_evidence/emit/uk_apple.metal uk_evidence/emit/uk_intel.cl
  390 uk_evidence/emit/uk_amd.cpp
   61 uk_evidence/emit/uk_apple.metal
   53 uk_evidence/emit/uk_intel.cl

$ head -2 uk_evidence/emit/uk_amd.cpp
// unikern generated — AMD HIP / RDNA (independent generation, R5.1)
// hcs=amd/RDNA gfx103x (8GB discrete) fp=8039464947ce constants_v=1
```

（`uk_apple.metal`、`uk_intel.cl` 头部同样携带 `fp=8039464947ce`。）

**状态：✅ 本机已验（v6 三后端全闭合）**。R5.1（独立生成 + 仅 KML/HCS 输入）与
R5.3 本机已验：生成头部记录 HCS 指纹 + `toolchain=`（v4 闭合 G3，`toolchain=rocm-7.13`）+
`constants_v` 常量版本指纹。R5.2 分后端：AMD 完整可运行程序已在本机真编译真执行
（见 R6/R7 证据）；**Intel 构件 uk_intel.cl 已于 Phase 9（2026-10-27）经 ocloc AOT
编译（`-device bmg-g21`，产出 .bin + .spv）——真编译证据见
`docs/evidence/phase9/phase9-ocloc-probe.txt`**；**Apple 构件 uk_apple.metal 已于
Phase 10（2026-10-27）经 Metal 运行时 JIT 真编译真执行**（`newLibraryWithSource`
进程内编译，无需 Xcode/metal 编译器，见 R6 真机对拍证据
`docs/evidence/phase10/phase10-verify.txt`）。

### R6 — 正确性差分验证

**要求**：R6.1 与 CPU 参考比对；R6.2 分级容差（FP32 ≤ 2⁻⁵）；R6.3 比对失败拒绝入账本。

**口径演进（Phase 47，用户选定 D8 口径 A）**：R6.1 的"CPU 参考"按 `requirements.md:10` 本是**两档**（标量 + 分块），reduce 自本相起以**分块档**为基准，分片数 `kRefReduceWidth=1024` 与内核 pass-1 宽度解耦；R6.2 的 2⁻⁵ 一字未改，Phase 19 记录的"更准的一方被判 FAIL"因此消失。真机五档仍全 `verify=PASS max_rel=0`，gemm/conv 产物空 diff（`docs/evidence/phase47/`）。

**验收方式**：两个层面。**CPU 侧（单测）**：容差函数 `verify_fp32`（`src/runtime.cpp:182`，R6.2 策略本体）五条路径——逐元素相等 → ok 且 max_rel=0；1/1024 相对误差（< 2⁻⁵ 默认容差）→ ok 且报告 worst 误差；10% 相对误差 → 拒绝并点名 `index 3`；可配置更紧容差（1/4096）→ 拒绝默认通过者；尺寸不匹配 → 明确拒绝（"size mismatch"），绝不假通过。**真机侧**：生成的 AMD 程序自带确定性初始化 + CPU 参考实现 + 2⁻⁵ 分级对拍（`UNIKERN_RESULT ... verify=` 契约行）；CLI `run` 只在 `verified=PASS` 时 LEDGER COMMIT（P4 不变量的真身是 `r.verified` 那道拒绝，`src/main.cpp:540`；这里原本写的是 `ledger_commit_gates_on_verify`，Phase 58 全仓 grep 证明**该符号从不存在**，按现码改指）。真机执行两个尺寸：1024³（resident）与 32768³（streamed）。

**证据**（单测段 + 真机实测；转录入库 `docs/evidence/phase8/acceptance_tests.txt`、`run_1024_first.txt`）：

```
[R6] fp32 graded tolerance exact/within/beyond/config/size-mismatch OK
```

```
$ ./build/unikern.exe run --hcs samples/hcs_amd.json --dims 1024 1024 1024 --workdir uk_evidence/run1 --ledger uk_evidence/audit.sqlite3
RUN amd:hip built=yes ran=yes verified=PASS max_rel=0 measured_ms=2.84178
-- log tail --
offload-arch: gfx1032
UNIKERN_RESULT backend=amd verify=PASS max_rel=0.000000e+00 measured_ms=2.841780 kernel_ms=0.553284 mode=resident payload_mb=12.000000 perf=1024x1024x1024
LEDGER COMMIT predicted_ms=0.490829 measured_ms=2.84178 epsilon_abs=2.35095 epsilon_rel=4.78976
```

32768³ streamed 档同样 `verify=PASS max_rel=0`（见 R8）。

**状态：✅ 本机已验（v6 三真机全闭合）**。R6.2 容差策略本体（`verify_fp32`）已由
单测覆盖（2026-10-27 复核补测——首版报告误称"由 smoke 覆盖"，实为零覆盖，已修复）；
AMD 真机对拍两尺寸全 PASS、失败不入账本的路径由 CLI 代码保证（负测护栏见 E12：
错误架构码对象 → `UNIKERN_ERROR` 退 4）。**Intel Arc B390 后端对拍已于 Phase 9
（2026-10-27）真机验证：256³ 差分 `verify=PASS max_rel=0.000000e+00`（进程内
OpenCL runner，恒程序，in-process 执行与 CLI 完全一致，证据
`docs/evidence/phase9/phase9-verify.txt`）**；**Apple M4 后端对拍已于 Phase 10
（2026-10-27）真机验证：256³ 差分 `verify=PASS max_rel=0.000000e+00`（进程内
ObjC++ Metal runner，`newLibraryWithSource` 运行时 JIT，证据
`docs/evidence/phase10/phase10-verify.txt`）**。三真机均位级一致
（max_rel=0，确定性 2⁻⁴ 初始化使 FP32 逐位可比）。

### R7 — Kernel Solution Ledger（本地 SQLite）

**要求**：R7.1 三元组键读写；R7.2 命中返回记录解并跳过枚举/可行域/代价认证；R7.3 实测回写；R7.4 事务写入，中断无半条记录。

**验收方式**（`tests/acceptance.cpp` R7 段）：SQLite 账本往返（故意植入**离格** tile 48×80，命中返回恰为 (48,80) 即证明服务的是记录解而非重新求解，R7.2 `from_ledger=true`）；`measured_ms` 用原生 SQL 直查（`ILedger::lookup` 只回解不回实测，直查补证 R7.3 持久化）；异 HCS 指纹 → miss（键隔离）；子进程 `BEGIN IMMEDIATE + INSERT` 后 `_Exit(9)`（marker 文件证明确实执行到未提交事务），重开账本断言无半条 + 恢复可用（R7.4，hot-journal 回滚）。CLI 二次 run 命中。

**证据**（单测段 + CLI；转录入库 `docs/evidence/phase8/acceptance_tests.txt`、`run_1024_second_ledger_hit.txt`、`ledger_dump.txt`）：

```
[R7] sqlite round-trip + hit(off-grid tiles) + measured_ms persisted + isolation OK
[R7] crash mid-transaction leaves no half row OK
```

```
$ ./build/unikern.exe run ... --dims 1024 1024 1024 ...   # 第二次同键运行
LEDGER HIT  c_cached_ms=0.490829  tile=128x128  (from ledger)
placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)
RUN amd:hip built=yes ran=yes verified=PASS max_rel=0 measured_ms=2.5866
-- log tail --
offload-arch: gfx1032
UNIKERN_RESULT backend=amd verify=PASS max_rel=0.000000e+00 measured_ms=2.586600 kernel_ms=0.545620 mode=resident payload_mb=12.000000 perf=1024x1024x1024
LEDGER COMMIT predicted_ms=0.490829 measured_ms=2.5866 epsilon_abs=2.09577 epsilon_rel=4.26986
```

账本行转储（`ledger_dump.txt`，python sqlite3 直读）证实 R7.3 实测回写：`gemm:fp32:1024x1024x1024|8039464947ce283c|cbe87f0b3467756b | c_ms=0.4908288930909091 | measured_ms=2.5866 | tm=128 | tn=128 | placement=...`。

**状态：✅ 本机已验**（SQLite amalgamation 3.47.2 静态库；`.jsonl` 文件账本为兼容保留）。
R7.3 的"回写并用于校准"完整闭环（v4）：`run` 把实测 ms 与 kernel_ms 参考一并入库
（SQLite 迁移列 / `.jsonl` 7 字段格式，两种账本都向后兼容旧格式行），`calibrate`
子命令读取该 HCS 指纹下的实测行、按"compulsory bytes / (measured − kernel)"算每行
有效互联带宽、取确定性中位数导出校准 HCS——真机数字见文末校准补遗。

### R8 — 多级存储与显式流式调度（场景 A / 场景 B）

**要求**：R8.1 场景 A 零拷贝全驻留 + 标记统一内存；R8.2 场景 B 超显存按分片粒度与预取距离显式流式；R8.3 双缓冲重叠；R8.6 显式放置决策记录。

**验收方式**：单测断言两条放置记录的**内容**（`[R8]` 段，`tests/acceptance.cpp`）：场景 A 统一内存解的 `placement_record` 含 `placement=unified_memory`、`scenario A`、`zero-copy`、`bw=400`，路径标记 `Path::Unified`；场景 B 离散解含 `placement=vram+ram+nvme`、`scenario B`、`path=pinned_copy`、`shard=64MiB`、`prefetch_d=2`，路径标记 `Path::PinnedCopy`；两记录互异。MSL/OpenCL 产物携带 R8.1/R8.6 注释（R10 段断言）、HIP 产物发射 `UK_VRAM_BYTES / UK_SHARD_ROWS / UK_PREFETCH_D` 常量与 resident|streamed 双路径（R5/smoke 断言）；真机跑 32768³（载荷 12 GiB > 0.85×8 GiB）强制走流式路径。

**证据**（单测段 + 真机实测；转录入库 `docs/evidence/phase8/acceptance_tests.txt`、`run_32768_streamed.txt`）：

```
[R8] scenario A/B placement records explicit + distinct OK
```

```
$ ./build/unikern.exe run --hcs samples/hcs_amd.json --dims 32768 32768 32768 --workdir uk_evidence/run_stream --ledger uk_evidence/stream.sqlite3
LEDGER HIT  c_cached_ms=3601.23  tile=128x128  (from ledger)
RUN amd:hip built=yes ran=yes verified=PASS max_rel=0 measured_ms=55354.1
-- log tail --
offload-arch: gfx1032
UNIKERN_INFO mode=streamed shards=64 shard_rows=512 prefetch_d=2
UNIKERN_RESULT backend=amd verify=PASS max_rel=0.000000e+00 measured_ms=55354.097656 kernel_ms=71354.828125 mode=streamed payload_mb=12288.000000 perf=32768x32768x32768
LEDGER COMMIT predicted_ms=3601.23 measured_ms=55354.1 epsilon_abs=51752.9 epsilon_rel=14.3709
```

端到端 55.4 s **低于**单流串行内核参考 71.4 s——搬运与计算重叠生效的直接观测（R8.3 双缓冲；两组数字各次运行有波动，但 e2e < 串行内核的不变式每次复跑都成立）。放置决策记录（R8.6）：`placement=vram+ram+nvme (scenario B, path=pinned_copy, shard=64MiB, prefetch_d=2, link=32GB/s)`；场景 A 侧记录内容（`placement=unified_memory (scenario A, zero-copy, bw=400GB/s)`）由单测 `[R8]` 段逐词断言。

**状态：🟡 部分已验**。AMD 场景 B 显式流式 + 双缓冲本机真机已验；R8.5 的"缺页迁移率超阈告警"在 MVP 中以显式路径为唯一处理方式落地（生成程序自选 resident|streamed），**运行时驻留守卫已于 v12/Phase 16 实现并真机验证**：HIP 三程序把分配与驻留检查折叠进 `uk_mmalloc()`（`hipPointerGetAttributes` 断言），非设备驻留 → `UNIKERN_ERROR what=oversubscription` 退 5，UNIKERN_RESULT 尾报 `migrations=<n>`；RX 6600M 三算子六轮真机全 `migrations=0`（`docs/evidence/phase15/phase15-recheck-v12.txt`），拒绝分支经 host 指针代位验证 exit=5（`docs/evidence/phase16/`）。缺页迁移率的**长测**（长时间/大负载下的迁移率统计与超阈重求解）仍开放，如实保留。**Apple 场景 A 真机 resident 执行已于 Phase 10（2026-10-27）验证：`mode=resident`、`verify=PASS max_rel=0`、placement 记录为 `unified_memory (scenario A, zero-copy)`（单测逐词断言 + 真机输出一致，证据 `docs/evidence/phase10/phase10-run.txt`）**——场景 A 零拷贝语义下无显式流式路径（缓冲区别名统一内存），payload > 0.85×unified_bytes 时 runner 如实 `UNIKERN_ERROR what=stream_refused` 拒绝，与 HIP/OCL runner 同构。**Intel 真机侧如实标注：resident 执行已验（Phase 9，verify=PASS max_rel=0、实测回写），显式流式不实现**——intel:ocl 后端生成源自带 R8 honesty 注释声明此边界，payload > 0.85×vram 时 runner 如实 `UNIKERN_ERROR what=stream_refused` 拒绝（R10 无静默降级）。

### R9 — HCS 导入（MVP 半自动）

**要求**：R9.2 校验必填字段；缺失时**列出全部**缺失字段并拒绝导入。

**验收方式**：单测 `hcs_import("{}")` 断言错误信息同时含 vendor、device、tiers[vram].bytes、tiers[device].bw_gbps、link_bw_gbps、flops_gflops、compute_units 全部七项；单缺 `flops_gflops` 的输入只报该字段（无误报）；完整导入成功且指纹随建模字段（tier 带宽）变化。

**证据**：

```
[R9] complete missing-field rejection + fingerprint OK
```

CLI 同口径：`HCS rejected: missing required fields: ...`（`rejected` `src/main.cpp:213`；被拒字段串本身在 `required` `src/hcs.cpp:95`）；`hcs` 子命令 `VALID amd/RDNA gfx103x ... fp=80394649`。

**状态：✅ 本机已验**。

### R10 — UCM 能力覆盖与分层退化

**要求**：R10.1 每个抽象原语映射三厂商实现方式；R10.2 后端无法覆盖的区间纳入 F 约束，绝不静默降级。

**验收方式**：单测断言 `make_backends()` 恰好覆盖 {Amd, Apple, Intel} 三厂商；`available()` 语义与环境事实一致（AMD=hipcc 探测、Apple=编译期 `__APPLE__` + 运行时 `metal_available()`（`MTLCreateSystemDefaultDevice` 真实探测，Phase 10 修正——旧 `__APPLE__` 编译期判定在 Apple 硬件上恒真但无运行时证据）、**Intel=真实 OpenCL ICD 运行时探测（`pick_intel_gpu` 枚举平台/设备，不接受 OCL_ICD_VENDORS 环境启发式）**）；**工具链缺席的后端仍然生成自身厂商语言源码**，且**跨厂商 API 词汇零泄漏**（Metal 源无 `hip`/`__global__`；OpenCL 源无 `metal_stdlib`/`#include <metal`/`[[buffer`——按 API 词汇检查，而非英文单词：诚实注释中的措辞合法保留），能力缺口以 availability 标注与 F 约束呈现，绝不静默替换为其他后端。CLI `backends` 表格同理。acceptance R10 镜像用独立 Win32 `LoadLibrary("OpenCL.dll") + GetProcAddress` 枚举平台/设备做对照（不经后端代码路径），保证期望来自环境本身而非被测代码。

**证据**（单测段 + CLI；转录入库 `docs/evidence/phase8/acceptance_tests.txt`、`backends.txt`、`docs/evidence/phase9/phase9-backends.txt`、`phase9-ctest.txt`、`phase9-acceptance.txt`）：

```
[R10] vendor mapping + availability semantics + vendor-API isolation OK

$ ./build/unikern.exe backends      # Windows 构建（Phase 8 记录，apple 工具链缺席）
vendor    backend      available  note
amd       amd:hip     no        source-gen ready (Phase 5 full)
apple     apple:metal     no        source-gen ready (Phase 5 full)
intel     intel:ocl     yes        source-gen ready (Phase 5 full)
```

（**下面是 Phase 8 时点、那台 Windows 开发机的读数**，不是"本机事实"的当前口径：当时该机
无 hipcc → amd no；`__APPLE__` 编译期判定 → apple no；Intel OpenCL ICD 在位（System32
OpenCL/ze_loader + oneAPI 2026.1，HKLM Khronos Vendors 注册 intelocl64.dll）→ intel yes。
**2026-10-28 复核**：同一台机器现在实测为 `amd=yes / apple=no / intel=no`（ROCm 7.1 的 hipcc
已装；oneAPI 安装目录不存在、`ocloc` 不在 PATH、无 Khronos Vendors 注册表键），
`docs/evidence/phase15/README.md:52` 与 `findings.md` 的"本机画像"条目一致——**以实测为准，
上方代码块只作历史时点保留**。
**Apple 主机事实（Phase 10，在 macOS 机器上取得，见
`docs/evidence/phase10/phase10-environment.txt`）：apple=yes——`available()` 改为
运行时 `metal_available()`（`MTLCreateSystemDefaultDevice` 真实探测），非编译期
`__APPLE__` 判定；AMD/Intel 在该主机为 no。**
**Intel Arc B390 真机链路（Phase 9）在另一台 Windows 机器上完成**，证据见
`docs/evidence/phase9/`；在当前这台机器上 `run --vendor intel` 必然 rc=1 且 solution 零落库，
那是诚实拒绝而不是回归（见文末"真机执行边界"）。）

**状态：✅ 本机已验（v6 三真机全闭合）**。能力映射与不静默退化语义本机已验；
**Intel 真机链路已于 Phase 9（2026-10-27）全验**：ocloc AOT（bmg-g21 → .bin+.spv）、
进程内 OpenCL runner 对拍/计时/账本闭环/校准，见 `docs/evidence/phase9/`；
**Apple 真机链路已于 Phase 10（2026-10-27）全验**：Metal 运行时 JIT
（`newLibraryWithSource` 进程内编译，无需 Xcode/metal 编译器）、进程内 ObjC++
runner 对拍/计时/账本闭环（LEDGER COMMIT/HIT），见 `docs/evidence/phase10/`。

## 缺口与如实记录（验收不掩盖项）

| # | 缺口 | 影响 | 去向 |
|---|------|------|------|
| G1 | ~~KML 无文本解析器，R1.2 结构校验无用户入口~~ **声明半边已闭合（v8）**；**执行半边 Apple（v9）+ Intel（v10）+ AMD（v11）已全部闭合 → G1 全闭合（v12 复采确认）** | 三算子可声明、可求解、可进键、可生成；Apple 真机三算子已验（M4）、AMD 真机三算子已验（RX 6600M，conv/reduce/gemm COMMIT→HIT；v12 复采含 `migrations=0`）、Intel 三算子实现 + 过 clang 的 OpenCL 前端语法检查（本机无 Intel 设备，backends 实测 intel=no，真机运行仍未验，如实标注） | 更复杂的原语树组合语法按 V1 |
| G2 | ~~`constants_version` 恒 1（stub）~~ **已闭合（v4）** | — | `calibrate` 递增版本号，实测 1→2 |
| G3 | ~~生成产物头未含 HCS `toolchain` 字段~~ **已闭合（v4）** | — | 产物头 `toolchain=rocm-7.13`（三后端一致） |
| G4 | ~~Apple 真机执行与对拍；Intel 真机执行与对拍~~ **已闭合（v6）** | ~~R5.2/R6/R8 的对应后端项~~ — | **Intel 半侧 2026-10-27 Phase 9 闭合**（ocloc AOT + 进程内 OpenCL runner 真机验证，见 `docs/evidence/phase9/`）；**Apple 半侧 2026-10-27 Phase 10 闭合**（Metal 运行时 JIT + 进程内 ObjC++ runner 真机验证，见 `docs/evidence/phase10/`）。G4 全闭合。 |
| G5 | ~~R8.5 运行时超订静默（缺页迁移率监测器）~~ **驻留守卫已实现并真机验证（v12 / Phase 16，ea76c2f）** | HIP 生成程序（gemm/conv/reduce 三份）内嵌 `uk_mmalloc()`：分配与 `hipPointerGetAttributes` 驻留断言不可分离，非设备驻留（managed/host）→ `UNIKERN_ERROR what=oversubscription ptr=<名> type=<n>` 退 5，UNIKERN_RESULT 尾追加 `migrations=<n>`；RX 6600M 三算子六轮真机全 `migrations=0`（复采 `docs/evidence/phase15/phase15-recheck-v12.txt`），拒绝分支经 host 指针代位验证 exit=5（`docs/evidence/phase16/`）；`[R8.5]` 单测段 + 两条摘除即红腿 | 缺页迁移率的**长测**（长时间/大负载下的迁移率统计与超阈重求解触发）仍开放，如实保留 |
| G6 | ~~Apple resident 预算取 HCS 声明容量，未与 Metal `recommendedMaxWorkingSetSize` 交叉校验~~ **已闭合（v9 / Phase 13）** | "payload ≤ 0.85×容量"此前只对声明容量成立——本机实测工作集上限 13631488000 B（12.7 GiB），仅声明容量的 47%，旧口径放过的最大载荷是设备上限的 2.1 倍 | 两道门并列（`0.85×声明容量` 与 `recommendedMaxWorkingSetSize`），各自报自己的名字，不叠乘；UK_DEVICE 行打印实测值；arm A/B/C 见 `docs/evidence/phase13/phase13-g6-working-set.txt`。判别证据是"带内拒绝"，不是"摘掉报警"（摘门会让 20 GB 载荷走到分配阶段） |
| G7 | ~~非 GEMM 树被静默当作 GEMM 执行并按 `reduce:...` 键回写账本~~ **已闭合（v7 / Phase 11）** | 修复前会污染 R7.3 校准输入；R10.2"绝不静默降级"被破 | `op_support_gap()` + 生成物自拒 + runner 入口自拒 + CLI 门，三层各有"摘掉即红"证明（`docs/evidence/phase11/`）；conv/reduce **可用的**内核生成仍属 G1 |
| G8 | ~~R10.3"输出每个解在各后端的原语覆盖情况"零实现~~ **已闭合（v8 / Phase 12）** | 修复前只有拒绝消息代偿，"不支持"不是可查产物 | `primitive_coverage()` + `COVER` 行进 solve/run/kml 报告；三轴（primitive / execution / toolchain）分别取自 `op_support_gap()`、`supports_streaming()`、`available()`，无第二张表可漂移；`[R10.3]` 库+CLI 臂守着，把 execution 轴写死即红（`docs/evidence/phase12/phase12-arm-probes.txt` 第 6 腿） |
| G9 | ~~`op_signature` 只编进 dims[0..2]，conv 两种形状共用一个账本键~~ **已闭合（v8 / Phase 12）** | 修复前不是活故障（conv 在落库前即被 G7 拒绝），但 G1 的 conv 声明一落地就会变活 | 签名与 canonical_form 改按 `kml_arity()` 取维；`[R7.1]` 臂断言 gemm 键逐位不变（已入库证据无需迁移）+ conv 三形状互异 + reduce 未用槽位不泄漏进键或哈希 |
| G10 | **reduce 的差分验证固定在 2²⁰ 实例**（发射点是 `Differential`（`src/codegen_hip.cpp:883`） 那句 `Differential verify: 2^20 …`），而折叠宽度 `width` 来自**声明的 L**（`:829`）⇒ 计时规模上的 fp32 折叠误差（Phase 31 实测 5.96e−08 ~ 1.60e−04，`docs/evidence/phase31/fold-exactness-grid.txt`）距门限 2⁻⁵=3.125e−02 有 ≥195 倍余量，**任何出厂门禁都不会红** | R6.2/R6.3 的**证据范围**：账本里的 `max_rel=0` 是"两趟结构正确"的证据，**不是"该工作负载的求和精确"**的证据；触发条件也不是"n 不被 nP 整除"，而是**`(n, 宽度)` 二维共同决定**（Phase 33 只测 2 的幂宽度即否证"2 的幂可保精确"：n=5×10⁷ 时四档全偏，含现役 4096 的 +5.44e−05；3×10⁷ 时反而只有 16384 偏 +2.41e−04） | 量级与机制已实测并入库（v24 / Phase 31），纯 CPU 臂 `[R6.5-blind]`（`tests/acceptance.cpp:1634/1639/1647`；Phase 31 当时是 :1454/1459/1467）把"三条可独立失败"钉进 ctest；**扩大验证实例 / 改容差 / 改宽度公式都属 D8、D9 口径，等用户定** |
| G11 | ~~**发射内核的索引算术是 32 位，可行负载能让它溢出**~~ **Phase 37 撤销本条**：发射代码在乘法前就转 64 位——`A[(size_t)(bm + j) * K + k0 + i]`（conv 同形），未转型的只有共享内存 tile 下标；且 `shard_rows = shard_bytes / (K*4)`（`:963`）使流式索引空间恒 `<= 64MiB/4 = 16777216` 个元素（7 档 K x 3 档 M 实测，`docs/evidence/phase37/index-exposure.txt`）。**同一次扫描真正的发现**：`ledger` `src/main.cpp:304` 用 `std::atoi` 解析 `--dims`，超过 `INT32_MAX` 的维度**静默饱和成 2147483647**，`--dims 3000000000 0 0` 返回 rc=0 并打印 `LEDGER HIT`，发射产物写 `const int L = 2147483647;`——请求 3.0e9 个元素的用户拿到 2^31-1 的程序且无任何警告，且该请求与 `--dims 2147483647` **共用同一个账本键** | **输入边界（不是执行越界）**：声明维度是用户可控输入，饱和后求解/发射/记账全部按错误的 L 继续，账本里一条证据会被两个不同工作负载命中；求解谓词与生成器都没有 `2^31` 检查。reduce 的 `uk_reduce_part(..., int n, int nP)` 使可寻址长度上界就是 `2^31-1`，所以正确行为应是 loud 拒绝而不是饱和 | ~~未闭合~~ **已闭合（Phase 40：两入口显式上界校验 + 臂 `[R1.4-dimrange]`，发射字节未变；原 G11/D10 的“升索引类型”选项作废——不需要）**：修复点**不止** CLI `--dims` 分支的 `std::atoi`：KML 文件入口也饱和——实测 `"dims":[3000000000]` 同样 rc=0、同样发射 `const int L = 2147483647;`，并与 `2147483647` 的请求**共用同一个 ledger 键**（`LEDGER HIT c_cached_ms=570.425`，`docs/evidence/phase38.txt`）。文件侧的路径是 `src/kml_front.cpp:108` 的 `parse_int` 写入 `int v`，而 `:113` 的校验只有下界（`v <= 0`）与元数，**上界不在清单里** ⇒ 需两条之一：在 `parse_int` 内判 `> INT32_MAX` 即失败（一处覆盖两入口），或在 CLI 与 dims 循环各加一条违规；并把该谓词补进 R1.2 的违规类别。**不碰发射字节**（447/542/239 不受影响）。因改变用户可见的拒绝行为，按既有纪律待用户点头 = **D10（改写后）** |
| G12 | **dtype 进入成本与规范键，但不进入生成产物**：前端明确允许 `fp32\|fp16\|int8`（`dt_txt` `src/kml_front.cpp:106` / `:93`），`dtype_size(t.dtype)` 参与字节与代价计算（`dtype_size` `src/kml.cpp:117`），`to_string(t.dtype)` 进规范键（`to_string` `src/kml.cpp:107`）；而 `src/codegen_hip.cpp` 与 `src/codegen_other.cpp` **通篇不出现 `dtype`** ⇒ 三种声明发射的是同一个 fp32 程序 | **证据口径 + 成本口径**：实测同形状 gemm 512³ 的 `c_ms` = fp32 `0.110506`、fp16 `0.0613536`、int8 `0.0367776`，三者发射行数同为 448、值类型只有 `float`/`double`（`docs/evidence/phase39/input-boundaries-deconfounded.txt`）⇒ 账本里 `gemm:fp16:…` 这条记录的成本描述的是一个并不存在的 fp16 内核；容差表里的 `fp16_rel = 2^-4`（`fp16_rel` `include/unikern/components.hpp:133`）目前只在该文件中出现，未见被读取，所以真正的错配发生在成本与产物之间，不在判定阈值上 | **未闭合（Phase 39 新增）**：三条路待用户定 = **D11**：①按 dtype 生成真实类型（`half`/`int8` 内核 + 对应容差，代价是三个算子全部重写发射与基线）；②前端只接受 `fp32` 并把 `fp16\|int8` 变成 `op_unsupported` 式 loud 拒绝（改动最小、与 layout 的既有拒绝口径一致）；③维持现状但在产物头与账本行标注 `dtype=fp16 -> emitted fp32`。本轮只取证，未动代码 **Phase 45 闭合（D11），走的是「拒绝 + 唯一的表」而非补齐生成能力**：`dtype_gap()` 接进三个后端谓词（`src/codegen_hip.cpp:71`），非 fp32 在 `run`/`verify`/`solve`/`--emit` 每条路径上点名 `dtype` 拒绝；R6.2 三档由 `tolerance_for()`（`src/runtime.cpp:182`）唯一给出，产物判定行与两个 runner 都从它取值。修这条时发现**账本才是漏得最狠的面**：`solve` 的拒绝原来挂在 `--emit` 上，不带 `--emit` 的 int8/fp16 树照常 `commit()`（实测 `solutions` 1 行），即上面那句「成本描述的是一个并不存在的内核」真的入了库；现已与 `--emit` 解耦并补 no-emit 腿 + 控制 C6。选项 ①（真类型内核）仍未做，FP16/INT8 的**生成能力**依旧不存在（见未闭合列表） |
| G13 | **reduce 的 pass-1 宽度跨后端不一致（Phase 51 拆成两半）**：HIP 程序自 Phase 44 起向设备要容量；OpenCL runner 现在同样查 `CL_DEVICE_MAX_COMPUTE_UNITS × CL_DEVICE_MAX_WORK_GROUP_SIZE`（`plan_reduce_width` `src/runner_ocl.cpp:190`，同一套 2 的幂 + 256 地板，查询失败退回 4096 并打印 `source=props_unavailable`）；**Metal 保持常量**，但来源标签诚实写作 `metal_no_capacity_api`（`plan_reduce_width` `src/runner_metal.mm:136`）——Metal 公共 API 既不报 CU 数也不报每 CU 线程数 | R6/R8 跨后端可比性：三处现在共用一个钉值旋钮 `UK_REDUCE_WIDTH`（生效打印 `source=env width=`，不可用值 `what=reduce_width_pin_unfit` 拒绝执行），所以宽度 A/B 有共同词汇；**但本机无 Intel/Apple 设备，该侧没有任何数值读数** | **部分闭合（Phase 51）**：能落地的一半（共同词汇、OpenCL 设备查询、拒绝语义、门 #6 的编译检查、`[R15d-runner-width]` + 三条归属不同的控制）已做完；剩下的一半**不是工作项而是主机限制**——Metal 没有可诚实查询的容量 API，Intel/Apple 的数值验证要等有设备的相位。代理量够不够，等 Intel 真机第一次跑时按读数重判 |

## 独立审计补遗（2026-10-27，收尾审计）

收尾审计（工作流完成后）发现并修复一项独立复核漏网缺陷：`LEDGER COMMIT` 行的
`epsilon_abs / epsilon_rel` 打印的是**硬编码常量**（对拍容差 2⁻⁵ 与其分母下限 1e-6），
并非任务计划要求的 ε = |C − T| 实算值——首版证据中三组不同实测（2.61ms / 2.99ms /
40518ms）对应同一组 ε 常量即其暴露点，工作流独立复核未发现，收尾审计人工审读发现。
已改为实算（`src/main.cpp`）并重采全部真机证据（本版 R6/R7/R8 三个证据块与
`docs/evidence/phase8/` 三份转录均已刷新）：

- 1024³ resident：predicted 0.4908ms vs measured 2.8418ms → **ε_abs=2.35ms（ε_rel=4.79）**
- 32768³ streamed：predicted 3.601s vs measured 55.354s → **ε_abs=51.75s（ε_rel=14.37）**

大 ε 的解读（口径如实）：C(x) 的拷贝项按 HCS 互联带宽常量 32 GB/s 计，而端到端实测
看到的是真实 PCIe 有效带宽（低于标称）加上启动/同步开销——**ε 正是 R7.3"实测校准
层级常量"要吃的校准信号本身**，按实测报告、不做修饰。双缓冲重叠结论不受影响：
streamed 端到端（55.4s）仍显著低于串行内核参考（71.4s）。

## 校准闭环补遗（v4，2026-10-27）

R7.3"实测回写**并用于校准** C 的层级常量"与 R3.3"记录常量版本号"在真机上闭环：

```
$ ./build/unikern.exe run --hcs samples/hcs_amd.json --dims 1024 1024 1024 --ledger uk_evidence/cal.sqlite3 ...
UNIKERN_RESULT backend=amd verify=PASS max_rel=0.000000e+00 measured_ms=2.860180 kernel_ms=0.542460 ...
LEDGER COMMIT predicted_ms=0.490829 measured_ms=2.86018 epsilon_abs=2.36935 epsilon_rel=4.82724

$ ./build/unikern.exe calibrate --hcs samples/hcs_amd.json --ledger uk_evidence/cal.sqlite3 --out docs/evidence/phase8/hcs_amd.calibrated.json
CALIBRATED link_bw_gbps 32 -> 5.429 constants_version 1 -> 2
usable_rows=1 median_link_bw=5.429004GB/s [gemm:fp32:1024x1024x1024 bytes=12582912 copy_ms=2.31772 eff_bw=5.429GB/s; ]
PREDICTED demo c_ms 12.5387 -> 43.3307

$ ./build/unikern.exe run --hcs docs/evidence/phase8/hcs_amd.calibrated.json --dims 1024 1024 1024 ...
UNIKERN_RESULT backend=amd verify=PASS max_rel=0.000000e+00 measured_ms=2.707170 kernel_ms=0.569090 ...
LEDGER COMMIT predicted_ms=2.41533 measured_ms=2.70717 epsilon_abs=0.291837 epsilon_rel=0.120827
```

解读（口径如实）：样例 HCS 的互联带宽常量 32 GB/s 是标称值，实测有效值只有
**5.43 GB/s**（差 5.9 倍）——这正是校准前 ε_rel=4.8 的全部来源。校准把 C(x) 的
拷贝项修正到真实链路后，同一负载的预测 2.415ms 对实测 2.707ms，**ε_rel 收敛到
0.12**；对拍（max_rel=0）与双缓冲结论均不受影响。`calibrate` 只消费账本实测行、
产出新 HCS 文件——`solve` 保持 (hcs, tree) 纯函数（P1/R4.2 不被账本状态污染），
常量演进以 `constants_version` 1→2 显式留痕。转录入库：`calibrate.txt`、
`run_calibrated.txt`、`hcs_amd.calibrated.json`、`ledger_dump.txt`（含 kernel_ms 列）。

## Phase 18 补遗（v13）— R8.5 后半：迁移率告警 → 违规记录 → 重新求解

R8.5 的第二半（"IF 运行时缺页迁移率超过告警阈值 THEN 记录违规事件并触发重新求解"）此前只有
生成侧的信号——Phase 16 的注释写着 exit 5 是"the upper layer's re-solve signal"，但上层没有
消费者：`cmd_run` 把 exit 5 与一切失败并成 `RUN FAILED` rc=1。本轮补上消费方并据实定口径：

- **率的口径（D7）**：`rate = migrations / checks`。`checks` 是生成程序内驻留探针次数
  （实测 reduce 干净跑 = 6：verify 3 + timed 3），`migrations` 是其中非设备驻留者。阈值
  `--migration-threshold`（比率）默认 **0.0**——与 Phase 16"任一非驻留即退 5"语义等价，
  放宽必须显式；`nan`/负数在 parse 即拒（rc=2，且不建账本）。
- **不是缺页计数器**：gfx1032 的 Windows 应用运行时路径无可读缺页/迁移硬件计数器
  （HSA perf counters 不在此路径），故该率是 R8.5 的**可测代理**（显式驻留探针率），
  文档与代码注释都点名自己是代理；程序显式发起的 pinned 分片搬运不计入迁移。
- **违规事件入库**：`ILedger::record_violation` + `violation_count()`（SQLite 新 `violations`
  表；文件账本 `#VIOLATION` 前缀行），与 `size()` 严格分离——事件永远不会被读成一条解（R6.3）。
- **重新求解只用实测量**：取生成程序打出的 `hipMemGetInfo` 空闲容量把 HCS 设备层改成实测值
  （`confidence=Measured`）再解一次；取不到实测即 `RESOLVE STOP` 退 5，绝不用臆造常数（E13）。
  重试行落在 `ledger_key(tightened, tree)` 下，不把另一容量上测得的行挂在旧指纹下（R7.1）。
- **真机证据**（RX 6600M；`docs/evidence/phase18/`）：
  ① 场景 B 流式在守卫落地后首次重跑无回归——`mode=streamed shards=64 shard_rows=512
  prefetch_d=2`、`verify=PASS max_rel=0`、`migrations=0`、rc=0、COMMIT ε_rel=5.05902
  （`phase18-streamed-round1.txt`）。序列里另两轮流式为 ε_rel=5.05066（`21789.8/20627.0` ms）
  与 5.43178（`23162.3/31288.3` ms），见 `longtest-series.txt`。**kernel_ms 在本机流式路径不是
  稳定量**：同一二进制相邻两轮就分别给出 kernel<e2e 与 kernel>e2e，与 Phase 8 记的
  `55354.1/71354.8` 同形——所以这不是"Phase 8 旧缺陷被推翻"，而是该参考通道至今不可信，
  任何由 kernel_ms 推导的量（含 `calibrate` 的链路带宽）都带着这条边界。旧转录不改写。
  ② 干净驻留跑 `migrations=0 checks=6 vram_free=8407547904 vram_total=8573157376`
  （声明 8589934592）（`phase18-resident-tokens.txt`）。
  ③ **开火控制**（本机正常路径不可能产生非驻留设备分配，所以 `migrations=0` 单独不可采信）：
  `probe/phase18_alarm_probe.py` 把 `uk_mmalloc` 的 `hipMalloc` 注入成 `hipHostMalloc`，
  实测整条链 `oversubscription(... migrations=1 checks=1 vram_free=8425177088)` →
  `UNIKERN_VIOLATION rate=1 threshold=0 committed=no` → `RESOLVE re-solve
  device_capacity=8589934592->8425177088` → 重试再违例 → `RESOLVE STOP`，**rc=5、solution
  零落库、账本两条 `#VIOLATION` 事件行**；探针 try/finally 复原 + 残标断言（`alarm-firing-control.txt`）。
  ④ **摘除即红**四条腿全承重（`removal-controls.txt`）：守卫拒绝行与三份程序的 result 行各删一次，
  acceptance 都红。首版臂用的是 `find()!=npos`，删掉 result 行仍绿——因为守卫自己那份 token 满足了断言；
  这条不承重事实被实测抓到后改成出现次数判据（每份程序 ` checks=`/` vram_free=`/` vram_total=` 各恰好 2 次）。
- **行数锚点演进**：gemm 425→447、conv 253→275、reduce 217→239（各 +22 行）；v12 记录的是历史时点。
- **长测序列已跑（如实）**：15 轮驻留 + 2 轮流式，全 rc=0，每轮 `migrations=0 checks=6`，
  率的分布只有一个取值 0——本机正常路径不会产生非驻留设备分配，所以超阈行为必须由注入开火
  控制证明（已证）。Apple/Intel 在 MVP 里是 resident-only，该率只在 HIP 路径上有意义。

## Phase 20 补遗（v14）— `uk_conv` 输出通道分块，分块档由设备自报容量选

R8 调优方向（计划开放项 ④ 的第二半）。基线是 Phase 19 的实测吞吐（`conv-baseline.txt`：
256² 19.4 GFLOPS、128² 61.9 GFLOPS，同机寄存器分块 `uk_gemm` 512³ 为 2275 GFLOPS）。

- **做法**：`uk_conv` 改为 `template <int OCB>`，一个线程算同一像素的 OCB 个输出通道，
  于是一次 X 取值服务 OCB 个通道；**每个通道的累加次序一字未动**（`c→kh→kw` 升序、每步一次
  FMA），所以结果与 OCB 无关。发射统一走 `uk_conv_launch`（verify/warm/timed/kernel-only
  四处同一入口，验证档与计时档不可能不一致）。
- **档不写死**：`uk_conv_pick_ocb` 从 {8,4,2,1} 取"仍能填满设备"的最大档，容量取
  `hipOccupancyMaxActiveBlocksPerMultiprocessor(逐档实测) × dp.multiProcessorCount × 64`，
  每档的 `threads / resident_capacity / cu` 与选中值都打进 `UNIKERN_INFO`；探针失败退回 1
  并打 `UNIKERN_ERROR what=occupancy_probe`，不静默。规则本身是用程序自带的 `UK_CONV_OCB`
  校准旋钮扫出来的（15 次运行全部入库 `docs/evidence/phase20/ocb-sweep.txt`）。
- **实测（kernel_ms）**：256×256×32/Oc64 124.426(OCB=1) → **19.062**(OCB=8) ＝ **6.53×**，
  吞吐 19.4 → **126.7 GFLOPS**；128×128×16/Oc32 2.361 → **0.547** ＝ 4.31×，61.9 → 276.3 GFLOPS；
  32×32×8/Oc16 **符号相反**（0.01258(1) → 0.04311(8)＝0.29×，线程填不满 14 个 CU），自动档在此
  尺寸退回 1，实测 0.01275 ms，仍优于 Phase 19 基线 0.01396。**自动档在三个尺寸上都选中了该尺寸
  的实测最优档（1/8/8），没有一个靠猜。**
- **硬事实**：15 次运行、四个档、三个尺寸，`max_rel` **恒为 0.000000e+00**。这是"分块只改访存
  复用、不改加法顺序"的直接证据，因此本优化是可证明安全的性能改动。
- **验收**：`[R20]` 臂按字节钉住整个 FMA 嵌套（含缩进）、钉住四档实例化与 occupancy 探测的
  **出现次数**、钉住唯一发射点与 op 隔离；三条腿逐一摘除全红
  （把容量式里的 `dp.multiProcessorCount` 换成抄来的标称 `14` / 换掉一次 occupancy 探针 /
  反转 kw 循环），`probe/phase20_conv_arm_probe.py` rc=0，复原 byte-identical，ctest 2/2 绿。
  过程记录了两条不承重断言的成因（E21/E24 同族：同一符号出现两处时 `find()` 恒真）。
- **仍开放（性能）**：只做了通道维分块，**空间维/共享内存 tile 未做**——调优后 conv 126.7 GFLOPS
  距同机 gemm 2275 GFLOPS 仍约 18×。conv 生成程序 275 → **372 行**（历史行数锚点按演进标注，不改写）。

## Phase 21 补遗（v15）— conv 空间维共享内存 tile，(tile, OCB) 联合选档

- 256×256×32/Oc64 的 kernel 时间 **124.571（Phase 19 基线）→ 19.062（Phase 20）→
  7.153 / 7.167 / 7.175 ms**（重复三次，累计 **17.4×**，337.5 GFLOPS）；128×128×16/Oc32
  0.547 → **0.287 ms**（8.5×，524.9 GFLOPS）；32×32×8/Oc16 为 10.70–14.18 µs，与 Phase 20 的
  12.58/12.75 µs **同分布**——该尺寸上差值小于重复散布，如实报"分辨不出差别"。
- 手段：带 halo 的输入 tile 协作搬进 `__shared__`（动态共享内存，尺寸只在 `uk_conv_smem` 一处算），
  `__syncthreads()` 在边界 return **之前**，随后每线程仍按 `c→kh→kw` 升序累加，只把取数来源
  从全局内存换成 LDS。**因此结果逐位不变：所有形状 × 所有 tile × 所有 OCB 的 24 次真机结果行
  `max_rel` 恒为 0.000000e+00。**
- 选档教训（第一版实测错的）：只按"共享内存装不装得下"选 tile，32² 上选了 `tile=16`
  （仅 4 块＝1024 线程，而设备自报可常驻 21504 线程）→ 慢 25%。现在把
  `tile∈{16,8,0} × ocb∈{8,4,2,1}` 当**一个组合空间**枚举，填满度由
  `hipOccupancyMaxActiveBlocksPerMultiprocessor × dp.multiProcessorCount` 判，取其中复用度最大者，
  全不填满则退回 `tile=0/ocb=1`。三个形状随后全部自动选中各自的实测最优组合；
  `UK_CONV_TILE` / `UK_CONV_OCB` 两个校准旋钮保留了这套规则的复测路径。
- 验收：`[R20]` 断言全部改为**出现次数 + 按字节钉住的两条 FMA 嵌套 + 屏障先于边界 return 的次序**，
  四条腿逐一摘除全红（抄来的标称 14 / occupancy 探针指错内核变体 / 屏障移到 return 之后 /
  LDS 索引错一元素），`probe/phase21_conv_arm_probe.py` rc=0、复原 byte-identical、ctest 2/2 绿。
  转录 `docs/evidence/phase21/`（README + `tiled-sweep.txt` + `r20-arm-removal.txt`）。
- 无交叉回归：gemm 447 / reduce 239 行未变，同批 gemm 512³ 与 reduce 1048576 真机
  `verify=PASS max_rel=0 rc=0`；conv 生成程序 372 → 506 行（历史行数锚点按演进标注，不改写）。
- 剩余性能空间：距同机寄存器分块 gemm 2275 GFLOPS 还差 6.7×（Phase 20 时是 18×），再往上需要
  权重进 LDS 的完整隐式 GEMM 分块，属另开一相。

## Phase 22 补遗（v16）— 主机口径标注、agent 入口、锚点机检门

- **"本机"这个词**：R10 段里那段"Windows 本机事实：无 hipcc → amd no；… intel yes"是
  **Phase 8 时点、那台开发机的读数**，现已标注为历史时点；同段补入 2026-10-28 实测
  `amd=yes / apple=no / intel=no`（ROCm 7.1 的 hipcc 在位；oneAPI 目录不存在、`ocloc` 不在 PATH、
  无 Khronos Vendors 注册表键）。规则：**任何"本机"结论前必须先跑 `./build/unikern.exe backends`
  并在句子里点明主机**——Intel（Phase 9）与 Apple（Phase 10）的真机链路分别来自另外两台机器，
  在当前这台机器上 `run --vendor intel|apple` 必然 rc=1 且 solution 零落库，那是诚实拒绝而非回归。
  旧转录与旧读数一律保留原样，只加时点标注。
- **agent 入口**：仓库根新增 `AGENTS.md`（三条命令、主机口径、改生成器的三条硬约束、
  探针对被跟踪源码的恢复要求、记录纪律、待决的 D8 与 R8.5 代理口径）。
- **锚点机检**：`probe/anchors_check.py` 注册为 ctest 第 3 门 `anchors`（**套件 2/2 → 3/3**；
  无 Python 解释器时诚实不注册而不是报绿）。它检查 AGENTS.md/规划文件里每条 `path:line`
  引用：文件存在、行号不越界、且引用行邻域点名的代码符号确实在该处 ±25 行内。
  首跑即在 34 条锚点中查出 4 条真失效并据此改准（`verify_fp32`→`src/runtime.cpp:182`、
  LEDGER COMMIT 门→`r.verified` 把关的 `src/main.cpp:540`、`INFEASIBLE`→`src/main.cpp:269`、`HCS rejected`→
  `hcs_import` `src/main.cpp:213`，而被拒字段串本体在 `src/hcs.cpp:95`——那是归错文件而非行号漂移）。
  门本身可失败已验证：把一处锚点改回失效值 → validator rc=1、`ctest -R anchors` Failed；复原后绿。
- **写法约定**（本轮实践逼出的）：`path:line` 形式是"现在还能跳过去"的**活断言**，归这道门管；
  记录历史时点的行号请写成"第 N 行"。理由很直接——同一条抽验记录里的行号在我自己一天三轮改动
  之后又漂了一次（95→151→182）。

## Phase 23 补遗（v17）— conv 2×2 输出微块：按预登记判负并回退，以及"回退"的举证口径

- **验收方式（负结果同样要有仪器）**：256²×32/Oc64 上扫 `tile∈{16,8} × ocb∈{8,4,2}` 六组 + 自动档两跑，
  保留线在跑之前写死为 `kernel_ms ≤ 5.97 ms`（Phase 21 的 7.16 ms / 1.2）。实测
  59.650 / **29.272** / 37.649 / 80.167 / 40.786 / 52.251 ms（自动档 59.679 / 59.717——**这两格只取自
  回退前的终端输出，不在任何转录里且已无法再采**，判负不依赖它们），
  **七次全部 `max_rel=0.000000e+00`、全部比 1×1 慢**，最好的也比基线慢 4.1×，且 ocb 越大越糟
  → 按预登记规则回退，**不因为"正确"而保留一份更慢的复杂度**。转录
  `docs/evidence/phase23/microtile-sweep.txt`，判据与归因见同目录 `README.md`。
- **"改对了但更慢"是性能决定，不是正确性事故**：微块只提升 LDS 取值的复用度（每线程累加器
  `OCB → PY*PX*OCB`），每输出的 `c→kh→kw` 升序一格未动，所以 `max_rel` 恒 0 是预期的；
  代价是块内线程 256→64（每 CU 常驻 warp 变少）与 px 循环的 LDS 步长/bank 行为。
  三条与"ocb 越大越糟"的实测形状吻合，但**没有 profiler 把它们拆开，因此只作为归因记录，不写成结论**。
  **（v18 更正：其中 bank 一条已被单独实验判负，见下面 v18 段。）**
- **字节钉住的臂在真实改动上执行了门**：微块一落地 `ctest` 从 3/3 变 2/3
  ——`[R20]` 的 `count_of("acc[o] += xv * Wt[") == 2` 与两条按字节钉住的 FMA 嵌套都不认这次改写，
  回退后自动复原 3/3。这条臂此前只被"人为摘除"验过，这次是被一次非人为注入的（且错误的）优化验过的。
- **"回退"从此要有可 diff 的产物，不能只给行数**（本节是对我自己一句过头话的更正）：
  回退后重新发射的 conv HIP 程序三个形状都是 **508 行**，Phase 21 记的是 506 → 行数与 KML 形状无关，
  所以 2 行是真实增量。微块代码零残留（`micro|2x2|px|PY|subtile` 全文 0 命中），增量落在 env 旋钮分支：
  `src/codegen_hip.cpp:463` 的 ` source=env` 后缀（该 token 在 `docs/evidence/phase21/tiled-sweep.txt`
  出现 0 次、在今天出现 1 次）与 `uk_conv_plan` 里钉 tile=0 后的 `smem` 重置 `src/codegen_hip.cpp:435`，二者作为诊断保留。
  **因此"逐字节回到 Phase 21"不成立，成立的是"行为回到 Phase 21"**（补采原始转录
  `docs/evidence/phase23/postrevert-verify.txt`：两次 fresh-ledger run `kernel_ms=7.158920 / 7.127580`，
  加上回退当轮的 7.158620 / 7.173225 与 Phase 21 的 7.153/7.167/7.175，共 7 次读数全落在
  7.1276–7.1750 ms；`conv_plan ocb=8 tile=16 smem=41472`、`max_rel=0.000000e+00`、rc=0）。
  当前发射结果已入库为基线 `docs/evidence/phase23/conv-hip-postrevert.txt`
  （508 行，`sha256=8464d86cf3f07beb733a874a7cae9b0d538adb091d858e69f74b4b5a8e42bb28`）。
- **顺带修正一条取证习惯**：告警式打印在某份转录里 0 次命中，**不代表代码里没有它**——
  `conv_tile_skip` 在 Phase 21 转录里就一次没出现，因为那个形状下它没开火。缺席只证明没触发，不证明不存在。

## Phase 24 补遗（v18）— LDS bank 冲突假设被 A/B 判负，以及选档器与 smem 的耦合

- **被检验的是 v17 自己写下的归因**，不是新想法：`uk_sX` 按像素的步长为 `C`，块 16×16、gfx1032 用 wave32
  ⇒ 256² 用例（C=32、TW=18）里 `32 mod 32 = 0` 且 `18*32 = 576 mod 32 = 0`，模型上 32 条 lane 命中同一 bank。
  改法只动地址（`CP = C + 1`，padding 槽不参与算术；kernel 索引与 host 侧 smem 用同一处规则）。
- **四条预登记判据的结果**（先写死后跑，全文在 `task_plan.md` "Phase 24 预登记"）：
  ① 保留线 **未达**——256² `kernel_ms=7.135740 / 7.145340 ms`，仍在未加 padding 的 7.1276–7.1750 ms 带内；
  ② 判别性控制 **未获支持**——128²（C=16）`0.302160 / 0.295439 ms` 比未加 padding 的 0.287 ms **慢 2.9–5.3%**，
  两个形状都没有增益；③ 负对照 **移动**（见下条）；④ 累加次序 **成立**——7 条结果行全部
  `max_rel=0.000000e+00`。**结论：LDS 同 bank 冲突不是这个内核在这张卡上的限制因素。**
- **本轮最耐用的发现是测量学而不是性能**：`uk_conv_plan` 把 `cand.smem` 交给
  `hipOccupancyMaxActiveBlocksPerMultiprocessor`，接受条件是 `threads >= per_cu * multiProcessorCount`；
  步长改成 `C+1` 后 32² 的 tile=8 smem 由 3200 → 3600，可常驻容量下降，谓词由假变真，
  **选档从后备路径 `tile=0` 跳到 `tile=8`**，实测 0.016357 / 0.020372 / 0.015565 ms（慢约 1.5×）。
  即：**任何改 smem 公式的布局实验都会顺带改选档**，"只改布局"在 tiny 形状上不是可观测的孤立变量——
  后续同类实验必须先用 `UK_CONV_TILE` / `UK_CONV_OCB` 把方案钉住再计时。
  反直觉方向也一并记下：smem 变大 → `per_cu` 变小 → 更容易被判成"填满设备"。
- **回退的举证口径升级**（v17 承认过"只能说行为一致"）：本轮回退后重新发射
  `diff` 与 `docs/evidence/phase23/conv-hip-postrevert.txt` **空输出**、`sha256` 相等
  （`8464d86cf3f07beb733a874a7cae9b0d538adb091d858e69f74b4b5a8e42bb28`，508 行），真机复现
  `kernel_ms=7.152313 / 7.152326`、`ctest` 3/3（`docs/evidence/phase24/postrevert-confirm.txt`）。
  **规则：动手前先存基线，回退的举证才只是一条 `diff`。**
- **`[R20]` 的字节钉住第二次在真实改动上开门**：padding 版让 `tests/acceptance.cpp:2633` 的 `nest_tile`
  当场变红，处理方式与 v17 同——先让旧钉住红、再决定去留，绝不先把断言改得迁就实现。
- **机制断言的边界（诚实）**：本机无 profiler——`rocprof` / `rocprofv2` / `rocgdb` 不在 PATH，
  `therock/gfx103X-7.13.0/bin` 目录内也没有 profiler 二进制（本轮实测确认）。因此"冲突不存在/被掩盖"
  这类话只能由 A/B 计时支撑，不能写成计数器结论。
- **下一手（靶，非决定）**：读发射出的内循环，每步是 1 次 LDS 取值 + **8 次 `Wt` 全局（L2）取** + 8 次 FMA，
  取指令比 FMA 多；本块权重只有 `ocb*Kh*Kw*C = 2304` 个字（≈9 KB）。该动的是权重的放置，
  这也解释了 v17 微块为什么失败（它加的是 LDS 取值数，而大头在权重侧）。

## Phase 25 补遗（v19）— conv"为什么慢"的三条解释全部由测量关闭；设备共享内存事实改口径

本节不改任何验收断言，改的是**解释的口径**：v14–v18 把 conv 从 19.4 推到 337.5 GFLOPS 之后，
剩下的差距需要一条机制说法，而三条候选说法现在都被真机读数否证了。

- **设备事实（`probe/phase25_smem_probe.cpp`，独立程序，两种模式各 7 档，全 rc=0；
  `docs/evidence/phase25/smem-preflight.txt`）**：RX 6600M / gfx1032 / ROCm 7.1 报
  `sharedMemPerBlock = sharedMemPerMultiprocessor = maxSharedMemoryPerBlockOptin = 65536`、`warpSize=32`、
  `maxThreadsPerMultiProcessor=2048`；occupancy 的 `per_cu` = 12800 B → **5**、≥41472 B → **1**。
  因此现役 `tile=16/C=32`（41472 B）是**每 CU 常驻 1 块 = 256 线程 = 线程槽 12.5%**。
- **v15 计划里那条"48 KB 默认上限、需 `hipFuncAttributeMaxDynamicSharedMemorySize`"在本机不成立**：
  65536 B 动态请求在不做任何 opt-in 的进程里 launch+sync 都成功。已在 `task_plan.md` Phase 21 行就地划掉并登 E26；
  **"tile 尺寸必须按设备自报值实测选、不许抄标称"这半条依然有效**（它来自 Phase 21 的那次 25% 反例）。
- **三条候选解释的判决**（全部 `max_rel=0.000000e+00`，所以都不属正确性，只属性能解释）：
  ① X 的复用度不足 → 2×2 输出微块把复用提到 4×OCB，**慢 4.1×**（v17）；
  ② LDS bank 冲突 → `C → C+1` padding，256² 无变化、128² 慢 3–5%（v18）；
  ③ 常驻块数/occupancy → 本轮钉住测 `tile=8`（5 块/CU、320 线程/CU）对 `tile=16`（1 块/CU、256 线程/CU）：
  **ocb=8/4/2 分别慢 35.2% / 28.8% / 11.4%，ocb=1 差值在散布内**（`pinned-matrix.txt`，
  tile=16 = 7.129367 / 10.514896 / 18.079030 / 31.571097 ms；tile=8 = 9.638396 / 13.543800 / 20.141341 / 31.416699 ms），
  预登记保留线 `≤ 5.97 ms` 八组无一达到。
- **仍然成立的只有 OCB 单轴的效应本身**：同一 tile 下 ocb 1 → 8 快 **4.43×**（次线性于 1/OCB）。
  读码给出下一步的靶：每步 1 次 LDS 取值 + 8 次权重全局（L2）取 + 8 次 FMA ⇒ 要动的是**权重的放置**
  （LDS/寄存器）或换算法（完整隐式 GEMM），`tile`/`ocb` 两个旋钮已榨干。
  对照组同时给出选档规则的第二次独立确认：不钉旋钮时选档器选中 `tile=16/ocb=8`，实测 7.150933 ms
  正是八组里的最优。
- **一处注释口径的更正（E26，代码事实而非猜测）**：发射程序有**两处** `uk_conv_plan` 调用
  （verify 段用内嵌小实例 C=8 → 打 `smem=10368`；计时段用真实 C=32 → `smem=41472`），
  所以"任何一段都不会用与校验不同的方案发射"（`conv-hip-postrevert.txt:249-250`）说的只是发射函数单一。
  真正被证明的性质是**所有 (tile, OCB) 方案逐位相同**——这才是"校验小实例、计时大形状"安全的依据。
  注释重写会动发射字节，故必须与 `sha256` 基线重录绑成一个动作（下一相）。
- **机制断言的仪器边界**：本机无 profiler（rocprof/rocprofv2/rocgdb 实测缺席），以上全部只由 A/B 计时与
  occupancy API 自报值支撑；`per_cu` 是自报值而非实际驻留计数，这一点在证据里点名。

## Phase 26 补遗（v20）— 验证覆盖口径：差分校验的必须是被计时的实例化

- **这一相改的是"证据范围"，不是性能**。实测缺口（直接运行发射程序取到，`docs/evidence/phase26/README.md`）：
  修复前 conv 程序里两处 `uk_conv_plan` 各推各的方案——verify 段按内嵌 32×32×8 实例（C=8）推得
  `tile=0 ocb=1 smem=0 nothing fills the device`，计时段按声明形状（C=32）推得
  `tile=16 ocb=8 smem=41472 fills=device`。于是默认流程里 **`max_rel=0.000000e+00` 证明的是后备内核
  `uk_conv_g<1>`，被计时的却是 LDS 分块内核 `uk_conv<8>`**。
- **对既有验收读数的口径修正（不推翻、只缩小到真实范围）**：R15/R20/R21 各段里"分块/分档后仍逐位一致"
  的真机读数都是钉住 `UK_CONV_TILE`/`UK_CONV_OCB` 的扫描产物，那些运行里两段自然同方案，所以读数有效；
  **但"不钉旋钮的默认流程也校验了分块实例化"这一条在 v19 之前不成立**，现在成立。
- **修法与门**：`uk_plan` 在 `main()` 里从声明形状推导一次，verify/warm/timed/kernel-only 全用它
  （`ocb` 有 `rem` 保护，`smem` 按真实 C 计算属超配、安全）。`[R20]` 增加四条计数腿——
  `uk_conv_plan(` == 2（定义 + 唯一调用点）、`uk_conv_launch(uk_plan,` == 4、
  `uk_conv_launch(uk_plan, VoW` == 1、`uk_conv_launch(plan,` 必须不存在；
  三条摘除控制全部当场红（`[R6.6]` `tests/acceptance.cpp:1694`、`:1697`，rc=3221226505），复原 byte-identical，
  `ctest` **3/3** 绿。旧腿 `count_of("<<<") == 8` 只保证**发射函数**唯一、不保证**方案**唯一，
  缺口正是活在这两条的缝隙里——这是本相最值得留下的一条。
- **回归读数**（全 `verify=PASS max_rel=0.000000e+00`、rc=0）：conv 256² `kernel_ms=7.160367`（在
  7.1276–7.1750 带内）、128² 0.284069、32² 0.012980；gemm 512³ 0.123837、reduce 1048576 0.164954。
  发射程序 508 → **517 行**，`diff` 恰 **+21/−12** 行且全部落在这一处设计变更上；gemm 447 / reduce 239 未变。
- **基线换代**：新基线 `docs/evidence/phase26/conv-hip-single-plan.txt`
  （517 行，`sha256=161d8e190da1286454e8429ea366b939bfcce54fe0c6e73890278ac92df27f3f`）取代
  `docs/evidence/phase23/conv-hip-postrevert.txt`（508 行，`8464d86c…42bb28`）作为后续"是否真回退"的比对对象；
  旧文件保留并标历史时点（转录不改写的既有约定）。
- **两条测量学副产物**：① 同一个二进制**单独跑**读 7.024740 ms、**经 runner** 读 7.160367 ms（差约 1.9%）
  ⇒ 带内比较必须同上下文；② 第四次踩 E22——`--dims 1048576` 单参数调用 reduce 时 CLI 打 usage 文本，
  被 grep 过滤后转录静默少一格（正确形式是 `--dims 1048576 0 0`），
  且**管道尾端的 `$?` 不是被测 CLI 的退出码**。

## Phase 27 补遗（v21）— OCB=16 判负：选档规则的"越复用越好"前提第一次有反例

- **测的是规则的前提，不是又一个旋钮**。v14/v15 把分块档定义成"仍能填满设备的最大复用档"，
  其隐含前提是复用度单调改善时间；Phase 25 的钉住矩阵给出该前提在本机的量化形状
  （256²、tile=16：ocb 1/2/4/8 = 31.571097 / 18.079030 / 10.514896 / 7.129367 ms，
  局部指数 β = 0.803 / 0.781 / 0.561，递减）。本轮加一整档把外推推到会被否证的地方，
  **两个分支与外推区间都先写死**（乐观 4.85 ms / 悲观 6.02 ms，保留线 5.97 ms）。
- **结果**（`docs/evidence/phase27/ocb16-sweep.txt`，方案钉住、上下文固定为 runner、全 `max_rel=0.000000e+00`）：
  256²（C=32）`ocb=16` = **7.858002 / 7.854852 ms** 对 `ocb=8` = 7.160519 ⇒ **慢 9.7%**，保留线未达；
  **128²（C=16）`ocb=16` = 0.266870 对 0.288485 ⇒ 快 7.5%** ⇒
  **最优 OCB 是 (C, Oc) 的函数**，v14/v15 那句"最大复用档"作为通用规则不成立，
  但在**现役候选集 {8,4,2,1} 内**仍然给出实测最优（Phase 25/27 两次独立确认）。
- **为什么不留这一档**（三条独立理由）：① 预登记保留线绑 256²，未达；② 候选集含 16 时自动档在 256²
  会选中 16（7.833645，比该选的 8 慢 9.4%），等于把错误偏好写进 shipping 路径；③ 要让 16 生效需要一条
  按 C 的规则，而证据是每个 C 只有一个形状的两个点——本项目的选择规则要求**由设备自报量推导**，
  两点不足以定规则（也不允许在生成器里写死拟合阈值）。⇒ 整档回退。
- **回退举证**（与 v17/v18 同一口径，第三次执行）：发射程序与
  `docs/evidence/phase26/conv-hip-single-plan.txt` **`diff` 空**（517 行），生成器内
  `uk_conv<16>` / `uk_conv_g<16>` / `step < 5` 全文 0 命中，`ctest` 3/3，
  真机复现 256² **7.156521**、128² **0.284850** 且选档回到 `ocb=8 tile=16`（`postrevert-confirm.txt`）。
- **验收矩阵不受影响**：本轮是性能实验且已回退，R8/R15/R20/R21 各臂的断言与计数一字未改；
  conv 现役读数仍是 256² 337.5 GFLOPS、128² 524.9 GFLOPS（距同机 gemm 2275 GFLOPS 为 6.7× / 4.3×）。
- **下一手已改成可判定的测量**：固定 tile=16，扫 `C ∈ {8,16,32,64} × ocb ∈ {8,16}`、每组合 ≥3 跑，
  找符号翻转的位置、检验它是否与 `Oc/C` 一致——有那条曲线才有资格谈"按 C 分档"的选择规则。

## Phase 28/29 补遗（v22）— 选档规则被曲线否证（不采纳）；校准旋钮必须过设备这一关（入库）

- **v21 留了一句待验证的猜想**："最优 OCB 是 (C, Oc) 的函数"。Phase 28 把它测成曲线后**否证了它自己**：
  tile=8、Oc=64、256² 固定，只变 C，`ocb ∈ {8,16}` 各 3 跑（极差 ≤2.21%，全 `max_rel=0.000000e+00`），
  16 相对 8 的增益是 **C=8 +23.4% / C=16 +22.0% / C=32 −9.0% / C=64 +11.5%** ⇒ 符号 `+ + − +`，**非单调**。
  不存在能用 C 表达的翻转点，也就没有"按 C 选档"这种规则可以进生成器；
  能诚实写进的只有：**最优 OCB 取决于 (C, Oc, tile, 网格大小) 的联合作用，机制在本机未定**（无 profiler）。
  ⇒ **候选集保持 `{8,4,2,1}`，`uk_conv_plan` 未因这两相改动**；R8/R20/R21 各臂与既有读数不受影响。
  证据 `docs/evidence/phase28/`（README + `sweep.txt` + `sweep-tile8.txt`，脚本 `probe/phase28_ocb_c_sweep.py`）。
- **验收方式的一条口径**：预登记的三条门槛里"每 C 极差 < 3%"与"C 上符号单调"是**两条独立判据**，
  脚本第一版把它们合并成一个 `spread gate` 标签，导致 C=64 的**缺格**被读成**噪声**。
  已改成分别报告；第一版转录保留不改（记录里写清哪个是证据、哪个是仪器缺陷造成的错标）。
- **Phase 29 是这次测量暴露的真缺陷，已修并入库**：env 校准分支在 `hipGetDeviceProperties` 之前就返回，
  于是 `UK_CONV_TILE=16` 在 C=64 上要求 82944 B（设备报 65536），运行死在
  `UNIKERN_ERROR what=launch verify msg=invalid argument`、rc=4 —— 失败点既提名也让不出上限。
  现在钉住的方案也要过**同一条** `smem > dp.sharedMemPerBlock` 判据：越限则打
  `UNIKERN_ERROR what=knob_tile_unfit tile=… smem=… sharedMemPerBlock=… -> running the global-load path (OCB pin kept)`，
  且**该行先于 `conv_plan` 行**；`hipGetDeviceProperties` 失败同样保守退回全局路径。
- **三条对照（`docs/evidence/phase29/knob-refusal.txt`，全 rc=0、`verify=PASS max_rel=0.000000e+00`）**：
  装不下（C=64/tile=16）→ 拒绝并继续跑；**装得下（C=64/tile=8）→ 不误报**；原形状（C=32/tile=16）→ 行为不变
  （7.050036 ms，带内）。**"总是拒绝的检查"与"从不拒绝的检查"一样没用**，第二条是这次修改的主要证据。
- **门禁**：`[R20]` 加两条腿——`count_of("what=knob_tile_unfit") == 1` 与顺序断言
  `hc.find("knob_tile_unfit") < hc.find("source=env")`；`probe/phase29_knob_arm_probe.py`（rc=0）
  证明两条各管一件事：改名拒绝 token 只红计数腿，在检查前多打一条 `source=env` **只红顺序腿**
  （`[R6.6]` `tests/acceptance.cpp:1712`）。复原 byte-identical，`ctest` 3/3。
- **发射基线（第 4 次换代，口径不变）**：`docs/evidence/phase29/conv-hip-knob-check.txt`
  542 行，`sha256=599a4e7183982b7101ed9441a8582bec59f9ae23bf450dffc8323a2bf79e3f18`，
  对 `docs/evidence/phase26/conv-hip-single-plan.txt`（517 行）`diff` 恰 **+25 / −0**。
  入库过程中纠正了一次自己的错：第一次存的产物来自 **C=64 的 KML**（头部 `dims=` 与 `const int C` 两行不同，
  diff 出现 −2）——**基线必须与它要比较的输入同源**，已按同一 C=32 KML 重发。gemm 447 / reduce 239 行未变。

## Phase 30 补遗（v23）— reduce 首次归因；启动宽度与对拍精确性耦合（代码未动，升 D9）

- **对象**：R6/R15 的 reduce 一线程一通道从未调优。发射程序跑 `uk_reduce_part`（nP 条线程跨步折叠）+
  `uk_reduce_sum`（**一条线程**串行折叠 nP 个部分和），`nP = min(L, 4096)` 在生成期定死
  （`:829`），运行期不看设备自报量。
- **归因仪器**：`probe/uk_reduce_width_probe.cpp` —— 折叠次序与发射内核逐字一致；数据取全域 2⁻⁶ 使
  **期望值可以先算**（精确和 = n/64），探针每行先报 `result`/`rel` 再报时间；n ∈ {2²⁰,2²²,2²⁴,2²⁶}、
  nP ∈ {1024…32768}（`docs/evidence/phase30/`，三份转录）。
- **预登记判据的对账（判据在跑之前写死）**：
  ① 我预测 `sum_share < 30%` ⇒ **实测 68.2%**（n=2²⁰、nP=4096）——方向预测错，
  **主项是串行 pass-2**，不是 pass-1 欠并行；② `part_GBps` 89.5 → 315.6（比值 3.53 ≥ 1.8）⇒ pass-1 确实欠并行；
  ③ `sum` 随 nP 单调增但**次线性**；④ 采纳门槛**不满足** ⇒ **生成器与测试零改动**。
- **√n 律被第四个点否证**：`T ≈ a·n/nP + b·nP ⇒ nP* ∝ √n`，锚在实测最优的现役 4096 后预测
  4096/8192/16384/32768，实测最优 4096/4096/16384/**16384**（2²⁶ 处 32768 慢 38%），
  因为 pass-1 时间随 nP **非单调**（1.4317 → 1.2665 → 1.6110 ms）。收益数字是真的：
  **1.67×（2²⁴）与 2.49×（2²⁶），小 n 不退步（≤1.05%）**——但能锁住这些点的上限是**本机拟合常数**。
- **本轮新增的验收口径（此前无文档记录）**：**启动宽度决定对拍是否精确**。
  n=2²⁶ 时只有非 2 的幂的 nP 出现偏差（14336 → `rel=−1.602e−04`、28672 → `−1.373e−04`，
  全部 2 的幂档 `rel=0`）。机制是算术：`n/nP` 非 2 的幂时，部分和折叠到 2²⁰ 量级需要超过 fp32 的 24 bit 尾数。
  **偏差量仍在 2⁻⁵ 容差内 ⇒ 门禁不拦、账本照收，变的只是"全库 `max_rel=0`"这个口径**；
  因此 `AGENTS.md` 新增第 7 条：改宽度之前必须先有一条"非幂档必须报红"的开火控制。
  这也是"设备自报量"两条路线实测出局的原因——满载 `2048×14=28672` 慢 8.7% **且**破坏精确性，
  `4 块/CU×256×14=14336` 与 16384 打平**但同样破坏精确性**。
- **交给用户的 D9（新增，见 task_plan 决策表）**：三选一——接受拟合常数 16384（拿 1.67×~2.49×，
  放弃"宽度只来自设备自报量"）、保持现状、或授权把 pass-2 并行化（消掉该取舍，但**改变最终和的折叠次序**，
  与未决的 **D8** 同类）。本轮一律不擅动。
- **现役读数与产物规模不变**：reduce 1048576 `kernel_ms=0.164954`、`verify=PASS max_rel=0`；
  conv 542 行 / gemm 447 行 / reduce 239 行；`ctest` 3/3、anchors 全绿。

## Phase 31 补遗（v24）— `max_rel=0` 的证据范围被量化：验证实例固定在 2²⁰

- **本轮不改任何判据，改的是"这条证据证明到哪一步"的口径**（登记为新缺口 **G10**）。
  读码事实：发射的 reduce 程序里差分验证段跑**固定 2²⁰ 个 2⁻⁶**（生成点是 `Differential`（`src/codegen_hip.cpp:883`） 那句
  `Differential verify`），而折叠宽度 `width` 由**声明的 L** 决定（`:829`，
  发射成 `UK_REDUCE_WIDTH` 的位置在 `src/codegen_hip.cpp:860`）——
  所以 `UNIKERN_RESULT` 的 `max_rel` 从来描述不了计时规模上的折叠。
- **网格实测**（18 格，`docs/evidence/phase31/fold-exactness-grid.txt`；仪器先报 `result`/`rel` 再报时间，
  期望值 = n/64 提前算好）：现役宽度 4096 在 `n=2²⁴/2²⁵/2²⁶` 上 `rel=0`；
  `n=2²⁶+4096` 上 `rel=−4.577e−05`；非幂宽度 14336 在 `2×10⁷ / 2²⁵ / 2²⁶` 上
  `+1.154e−04 / −3.052e−05 / −1.602e−04`；3072 从 2²⁵ 起非 0。
  **全部非零值与 2⁻⁵ 门限差 ≥195 倍** ⇒ 这类误差**不可能**被现有门禁拦住。
- **判据被数据改写一次**（这是本轮最有教育意义的一条）：预登记里我写的触发条件是"`n` 不被 `nP` 整除"，
  而 `n=2²⁶+4096` 与 4096 **是整除的**（67112960/4096 = 16385）却 `rel≠0`
  ⇒ 真正的条件是**每线程 chunk 是否为 2 的幂**（16385 非幂 ⇒ 部分和 256.015625 ⇒ 累加到 2²⁰ 量级时
  fp32 量子 2⁻⁵ 已经容不下 2⁻⁶ 的步长）。这条只在计时规模上可见——2²⁰ 实例里两种情况都是 0。
- **机制不需要真机**：CPU 侧按同一次序折叠 fp32 复现到最后一位
  （`4096 × 256.015625 → 4.57736e−05` 对真机 `−4.577e−05`；`14336 × 73.140625 → 1.60222e−04`
  对真机 `−1.602e−04`；幂 chunk 两者都 0）⇒ 是 fp32 算术事实，不是驱动/硬件行为。
- **进套件**：新增纯 CPU 臂 `[R6.5-blind]`，三条可独立失败的断言——
  ①幂 chunk 必须逐位等于精确和；②非幂 chunk 必须 `rel>0` 且 `<1e−4`；
  ③两者都必须低于 `Tolerance{}.fp32_rel / 100`（把"盲区是结构性的、不是调阈值能消的"钉住）。
  摘除控制三条全红（`arm-removal.txt`，`probe/phase31_arm_probe.py` rc=0；
  `[R15]` `tests/acceptance.cpp:1454`/`:1459`/`:1467`），复原 byte-identical，`ctest` **3/3**。
- **与两个未决项的接口**（不代决）：D8 多一条同向事实（不止"CPU 参考更不准"，还有"验证实例规模让
  折叠误差根本不出场"）；D9 多一条**硬约束**（任何候选宽度必须让 chunk 是 2 的幂，否则连
  "验证实例恒精确"这层保护都会退化成对拍数据规模的偶然）。
- **现役数字未变**：conv 542 / gemm 447 / reduce 239 行；reduce 1048576 `kernel_ms=0.164954`、
  `verify=PASS max_rel=0`。**未改**验证实例规模、容差、宽度公式——三者任一都会让已入库的
  `max_rel=0` 证据改含义。

## Phase 32 补遗（v25）— `anchors` 门同时核表格列数；扩门首跑抓出三条真实文档缺陷

- **v16 交付的门只管一半**：它核 `path:line` 锚点，**不核 Markdown 表格结构**。而本项目把工作记忆
  （`task_plan.md` / `findings.md` / `progress.md` 的表）当交付物与索引读，一张渲染错列的表就是错的索引。
  缺口是自己制造出来的：同一天里用"长单行表格行的**前缀**"当 Edit 锚点，把行撕开过三次（登记为 **E27**）。
- **新增检查**：`probe/anchors_check.py` 的 `table_shape_problems()` —— 以"表头行 + 分隔行"记下列数基线，
  往下逐行比较直到非 `|` 开头；``` 围栏内部跳过（证据转录里有管道字符）。
  **必须处理的一处**：GFM 里代码段内的裸竖线**也是**分隔符，所以计数要把转义竖线当内容——
  不加这条，错误台账里 E3 那行（键内含转义竖线）会被误判成 5 列；
  **会误报的检查没有资格进 ctest**（与 Phase 22 定下的"过粗与过松同样无效"同一条）。
- **首跑抓出的三条，均为真缺陷且已修**：
  ①Phase 27 表行的行内代码之间是裸竖线 → 4 格 vs 表头 3 格；
  ②D8 行里绝对值写法 `both−exact` 两侧裸竖线 → 5 格 vs 3 格；
  ③G5 缺口行的正文写在三个物理行上 → 渲染成 1 格（表头 2 格）。
  修法：前两条转义，第三条并回一行。**门第一次跑就值钱，与 Phase 22 首跑抓出 4 条失效锚点同类。**
- **门能红**：给 Phase 31 表行故意补一格 → `rc=1` 并点名该行（该行第 172 行）；
  复原 byte-identical → `rc=0`；`ctest` 3/3。双向验证沿用既有口径。
- **门禁现状**：`ctest` = `smoke` / `acceptance` / `anchors` 三门；`anchors` 一次跑核
  **93 条 `path:line` 锚点 + 全部规划与证据文档的表格列数**（27 份文档）。
- **写记录的新约定**已写进 `AGENTS.md`：改 Markdown 记录时锚点必须取整行（含结尾竖线），
  表格与代码段里的竖线一律转义。

## Phase 33 补遗（v26）— "2 的幂宽度可保精确"被否证；G10 严重度上调；D9 换成纯速度取舍

- **本轮只测 2 的幂宽度**，为的是把一个问题问干净：Phase 30/31 留下的可用说法是"2 的幂 ⇒ 折叠精确"，
  而机制（运行和越过 `2²⁴·2⁻⁶ = 262144` 后 ulp 粗于数据步长 `2⁻⁶`）说明**精确性主要由 `n` 决定**。
  32 格（8 个 n × 宽度 ∈ {2048, 4096, 8192, 16384}，`docs/evidence/phase33/pow2-widths.txt`）：

| n | 期望和 | 2048 | 4096（现役） | 8192 | 16384 |
|---|---|---|---|---|---|
| 2²⁴ | 262144.0 | 0 | 0 | 0 | 0 |
| 2²⁴+1 | 262144.0 | −5.96e−08 | −5.96e−08 | −5.96e−08 | −5.96e−08 |
| 2×10⁷ | 312500.0 | −1.64e−05 | **0** | −6.60e−05 | **0** |
| 3×10⁷ | 468750.0 | 0 | 0 | 0 | **+2.41e−04** |
| 2²⁵ | 524288.0 | 0 | 0 | 0 | 0 |
| 5×10⁷ | 781250.0 | +2.69e−05 | **+5.44e−05** | +7.94e−05 | +7.94e−05 |
| 2²⁶ | 1048576.0 | 0 | 0 | 0 | 0 |
| 2²⁶+4096 | 1048640.0 | −3.05e−05 | −4.58e−05 | −3.05e−05 | 0 |

- **结论一（否证）**：**没有任何宽度全格精确**，n=5×10⁷ 时四档全偏，**包括出货在用的 4096**。
  ⇒ 我此前"2 的幂可保精确"的说法过宽，已在 `AGENTS.md` 第 7 条、`findings.md` Phase 31 段、
  本文件 G10 就地收窄（原文按"补遗不改写、只标演进"处理）。
  **G10 的表述同时收紧**（初稿说过重，Phase 34 读码后改口）：不是"已出货的宽度就不精确而验证看不见"
  ——计时段的 `Y` 拷回主机后没有任何代码使用（发射点 `hipMemcpyAsync`（`src/codegen_hip.cpp:946`）），`max_rel` 只来自 2²⁰ 验证段；
  准确说法是"**该规模的结果不在验证范围内**"，即**覆盖缺口**而非证据污染。
- **结论二（精确性的正确表述）**：越过 `n ≈ 1.678×10⁷` 之后精确性是 `(n, 宽度)` 的二维函数——
  3×10⁷ 只有 16384 偏、2×10⁷ 只有 2048/8192 偏，**不存在单变量法则**；本轮不外推。
- **结论三（速度，与 D9 重述）**：`16384` 在**八个 n 上全部最快**，对现役 4096 为
  **1.65× / 1.65× / 1.80× / 2.04× / 2.06× / 2.31× / 2.49× / 2.50×**；
  且 `16384 = 2^floor(log2(maxThreadsPerMultiProcessor × multiProcessorCount)) = 2^floor(log2(28672))`
  ——**可由设备自报量推导**（沿用 conv 选档"只用设备自报量"的同一口径）。
  ⇒ D9 原有的两条反对理由（"拟合常数"、"会破坏精确性"）**同时消失**，剩下的只是纯取舍：
  用 1.65×~2.50× 换一个从未在出货路径上验过的宽度。**本轮按预登记的分支 2 未动代码**，等用户定。
- **门禁与证据不变**：`[R6.5-blind]`（三条断言只声称"存在精确的、存在不精确的、两者都远低于门限"，
  在 Phase 33 之后仍然成立）、`[R20]`、`ctest` 3/3、`anchors`（`path:line` + 表格列数 + 必需章节标题）。
  生成器、容差、验证实例规模、折叠次序：**一字未动**。

## Phase 34 补遗（v27）— 折叠误差变成可预测模型；G10 的说法从"证据被污染"收回为"覆盖缺口"

- **① 模型与对账**：`k_t = floor((n-1-t)/nP)+1`，pass-1 部分和 `k_t·fill` 在 `k_t < 2²⁴` 时精确
  ⇒ 误差全部来自 pass-2 的串行 fp32 累加，**可在 CPU 上逐格预测**。
  与 `docs/evidence/{phase30,phase31,phase33}` 里**全部 41 格真机 `rel` 读数对账 `mismatch=0`**
  （零/非零一致、量级 5% 内，对应转录 4 位有效数字）。附带结论：真机 pass-2 未被编译器重结合。
- **② 新臂 `[R6.6-model]`（纯 CPU，无设备）**：19 格实测三元组 + 两条结构断言
  （2²⁰ 验证实例在任何可选宽度下精确；模型最坏偏离 `2.40667e−04` ≪ 门限 `3.125e−02`）。
  摘除控制三条各自能红（`[R6.2/R6.4]` `tests/acceptance.cpp:1576`），复原 byte-identical，`ctest` 3/3。
- **③ 一条方法学，写进验收口径**：控制的第一版（把填充换成 `0.5f`）**不会红，也不会 ever 红**——
  按 2 的幂整体缩放不改变 fp32 的相对舍入。换成非 2 的幂 `0.1f` 后才会红。
  ⇒ 本项目的"摘除即红"标准从此明确为：**必须说明"若实现错，这条为什么会红"**，
  而不是"跑过了控制"。（这是"会误报的门"与"从不红的门"同族的第三种失效：看似严其实无判别力。）
- **④ 修正 v26/G10 的严重性表述**（v26 正文与 G10 行已就地标注改口）：
  计时段的 `Y` 拷回主机后**没有任何代码使用**（发射点 `hipMemcpyAsync`（`src/codegen_hip.cpp:946`）），
  账本记的是 ε 与时间，`max_rel` 只来自 2²⁰ 验证段
  ⇒ G10 是**覆盖缺口**（该规模的结果不在验证范围内），**不是"已入库证据被污染"**。
  模型与 `[R6.6-model]`、`[R6.5-blind]` 均不受此次改口影响（它们只声称误差可算/可存在）。
- **⑤ 对 D9 的影响**：宽度选择从此可以在跑之前**算**出每格的精确性，
  所以 D9 不再需要"保守怕破坏精确性"这一维——该维本来就是中性（两侧都不保证）。
  仍是用户取舍，未动生成器、容差、验证实例规模、折叠次序。

## 真机执行边界（诚实声明）

- 本 session 真机证据全部来自 **AMD RX 6600M（gfx1032）**（Phase 8 段）：1024³ resident、32768³ streamed、账本命中回写，命令与输出均为实际执行结果。
- **Apple M4（2026-10-27 Phase 10）真机验证完成**，证据见 `docs/evidence/phase10/`：设备事实经 MTLDevice API 实测（name=Apple M4、hasUnifiedMemory=1、maxThreadsPerThreadgroup=1024，本 SDK 无 CU 数量 API，HCS compute_units=40 为声明值）、Metal 运行时 JIT（newLibraryWithSource，无需 Xcode）、256³ 差分对拍 `verify=PASS max_rel=0`、512³ run `LEDGER COMMIT`（ε_abs=1.26059ms / ε_rel=122.097，实算；ε_rel 是比值 |C−T|/T，约 10² 量级属预期——MVP roofline 按声明峰值 26000 GFLOPS 取预测，而本次内核有效吞吐仅约 0.54 TFLOPS，残留即计算率缺口，如实标注）→ 同键 HIT → calibrate **场景 A 诚实门控**（默认拒绝：零拷贝语义下 measured−kernel 是主机调度开销而非链路拷贝时间，默认拒绝；`--allow-unified-calibrate` 显式强制的产物如实标注为伪影）。
- **Intel Arc B390（2026-10-27 Phase 9）真机验证完成**，证据见 `docs/evidence/phase9/`：环境事实经 OpenCL API 实测（单位 96、显存 27207761920 B、host_unified=1 等，不照抄平台标签）、ocloc AOT 真编译（bmg-g21 → .bin + .spv）、进程内 OpenCL runner 的 256³ 差分对拍 `verify=PASS max_rel=0`、1024³ 实测回写（ε_abs=1.31ms / ε_rel=2.72，首轮）→ LEDGER COMMIT → 同键 HIT → calibrate（link 32→18.5877 GB/s，constants_version 1→2）→ 校准后实测（ε_rel=1.49，残留为 HCS flops 常数（24000 GFLOPS 预测峰值 vs B390 约 2 TFLOPS 有效吞吐）精度问题，如实报告，非运行器缺陷；链路校准已生效）。
- 上游 session 证据（Phase 6/7 记录）：真机闭环 PASS、账本命中 true、流式演示 mode=streamed；其 ε 数值当时打印的是容差常量（见审计补遗），v3 已按实算重采，v4 校准闭环后 ε_rel 收敛至 0.12（AMD）。

## Phase 9 Intel 真机验证闭合补遗（2026-10-27，v5）

Intel 半侧 G4 闭合的完整事实（命令输出转录在 `docs/evidence/phase9/`，本段引用其要点）：

- 环境：Arc B390（OpenCL 3.0 NEO，驱动 32.0.101.9150）在位；System32 OpenCL/ze_loader +
  oneAPI 2025.3/2026.1（ocloc、CL 头）；oneAPI 自带 OpenCL.lib 导入库 →
  CMake 直接链接（无需 MinGW；本机构建工具链为 MSVC 2022 + vcvarsall，OpenCL 接入
  走 oneAPI 导入库 + third_party/CL 厂商头 18 个，UK_HAS_OCL 门）。
- 构建：`cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release`（OpenCL 发现 → -DUK_HAS_OCL；
  MSVC 编译旗标 /W4 + /utf-8 + _CRT_SECURE_NO_WARNINGS；ctest 2/2 全绿，含 R10 镜像）。
- R5：`solve --emit` 生成 uk_intel.cl（53 行）+ ocloc AOT（`-device bmg-g21` →
  uk_intel_bmg.bin + uk_intel_bmg.spv）；Intel 后端 available() 为真实 ICD 运行时探测（intel=yes）。
- R6：256³ 差分 `verify=PASS max_rel=0.000000e+00`（2 次 run 与 verify 一致，位级一致）。
- R7.3：1024³ run `LEDGER COMMIT ε_abs=1.31315 ε_rel=2.72046`（ε 实算，E13 口径）；
  同键二次 run `LEDGER HIT`；calibrate 得 link 18.5877 GB/s（v1→2）；校准后 run
  `ε_abs=1.14054 ε_rel=1.48813`——link 项已校准，残留由 HCS flops 常数（预测峰值
  24000 GFLOPS vs 实测约 2 TFLOPS 有效）精度主导，属模型常数精度（L4 roofline 标定范围），
  如实报告，不修饰。
- runner 设计（与 AMD 口径对齐）：进程内执行（无子进程）、256³ 对拍、树 dims 16 对齐
  下限 32 计时、warm 2/timed 10、e2e=H2D+kernel+D2H（标记事件）、kernel_ms 参考、
  resident-only（payload > 0.85×vram 诚实拒绝 UNIKERN_ERROR，R10 无静默降级）；
  网格上限（B390 max_wi=1024/维）诚实拒绝路径。
- R10：acceptance 镜像改为独立 Win32 枚举（LoadLibrary+GetProcAddress），与后端真探测
  在本机一致；三厂商映射不变。
- 遗留边界：~~Apple 真机（需用户 Mac）~~ **已闭合（Phase 10）**；Intel 流式双缓冲（Phase 9 明确不实现，诚实标注）；Apple/Intel 场景 A/B 真机长测项（R8.5 缺页迁移监测器、ε 残留的 roofline 标定，均如实标注，属模型精度/长测范围而非运行器缺陷）。

## Phase 10 Apple 真机验证闭合补遗（2026-10-27，v6）

Apple 半侧 G4 闭合的完整事实（命令输出转录在 `docs/evidence/phase10/`，本段引用其要点）：

- 环境：Apple M4（macOS 26.6.2 arm64）；Xcode.app 存在但**无 Xcode SDK**，Metal/Foundation
  链接自 CommandLineTools SDK（`/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`）；
  **Metal 运行时 JIT**（`MTLDevice newLibraryWithSource`）——进程内编译 MSL，无需
  `metal` 编译器 / meta-cpp 头 / Xcode。构建：CMake `-G Ninja -DCMAKE_BUILD_TYPE=Release`，
  `project(... OBJCXX)`，ObjC++（`-fobjc-arc`），ctest 2/2 全绿。
- R5：`solve --emit` 生成 uk_apple.metal（61 行），头部携带 HCS 指纹 + toolchain
  + 修复后的 R8 honesty 横幅（resident 真机已验，流式留待 milestone 1——横幅如实）。
- R6：256³ 差分 `verify=PASS max_rel=0.000000e+00`（进程内 Metal runner，恒程序，
  确定性 2⁻⁴ 初始化使 FP32 逐位可比，位级一致）。
- R7.3：512³ run `LEDGER COMMIT predicted_ms=0.0103244 measured_ms=1.27091
  ε_abs=1.26059 ε_rel=122.097`（ε = |C−T|/T 实算，E13 口径；ε_rel 为**比值**非百分数）；
  同键二次 run `LEDGER HIT`（c_cached_ms 复用，再 COMMIT 为设计行为——HIT
  跳过搜索但内核照常运行、验证 PASS 后以新测量更新账本行），该轮
  `measured_ms=0.838425 → ε_abs=0.8281 ε_rel=80.2078`。两轮读数区间 80.2–122.1，
  同机同负载抖动约 1.5 倍，不构成趋势结论。量级解读：HCS 声明
  flops=26000 GFLOPS 为 Apple 自报 SoC 峰值，本次 512³ 内核实测
  2·512³/0.494375ms ≈ **0.54 TFLOPS**（e2e 1.27ms ≈ 0.21 TFLOPS）——预测被声明峰值
  主导，残留即"声明峰值 vs 有效计算率"缺口；链接校准在场景 A 下语义不适用（见下）。
- **calibrate 场景 A 诚实门控（v6 新增机制）**：场景 A 零拷贝语义下
  `measured_ms − kernel_ms` 是主机端调度开销（GPU 时间戳跨度含队列提交），不是
  链路拷贝时间；用它反推 link_bw_gbps 会产出与真实统一内存带宽相差两个数量级的
  "精确伪影"（重采两轮分别得 9.097 GB/s 与 5.986 GB/s——同一份账本口径下"精确"
  却差 1.5 倍，本身就是它不是带宽测量的证据），比诚实 SKIP 更危险。默认
  `CALIBRATE SKIP`（rc=3），仅 `--allow-unified-calibrate` 显式强制，且产物如实
  标注为伪影。link_bw_gbps 本身是 scenario-B 常数（求解器仅在非 Unified 路径
  使用，`link_bw_gbps` `src/solver.cpp:102`），对 Apple 预测无影响——**这条有实测背书**：强制
  校准把 link 600→9.097、constants_version 1→2 之后，`PREDICTED demo c_ms
  5.28611 → 5.28611` 逐位不变。
- **门控不只是文档事实，本机有单测约束**：`tests/acceptance.cpp` 新增 R7.3 四臂经真实
  `unikern` 二进制执行（`argv[0]` 同目录定位 CLI，`[R7.3] scenario-A calibrate gate ... OK`）——
  臂 1 场景 A 无旗标 → rc=3 且消息为 scenario A、`--out` 未落盘；臂 2 同输入加旗标 →
  rc=3 但消息换成 `no measured rows`（顺序判别器：证明拒绝出自门控而非空账本——
  单看 rc 两者不可分）；臂 3 场景 A + 旗标 + 一行已知实测（512³ bytes=3145728、
  copy=0.5ms）→ rc=0 且产物 link_bw_gbps=**6.291456**、constants_version 1→2（旗标不是空操作）；
  臂 4 场景 B 无旗标 → rc=0 且 `CALIBRATED link_bw_gbps 32 -> 6.29146`（门控不吞 AMD/Intel
  链路校准路径）。摘掉门控后臂 1 转红（`docs/evidence/phase10/phase10-gate-arm-probe.txt`）。
  踩坑如实记录：POSIX `system()` 返回 wait 状态，exit(3) 读回 768，须 `WEXITSTATUS` 归一化
  （既有 crash-tx 臂只断言 `rc != 0` 故此前未暴露）。**边界：四臂在 macOS 上实跑；Windows
  分支沿用本文件 crash-tx 已验证的 cmd.exe 外层引号写法，但本轮未在 Windows 主机复跑**。
- runner 设计（与 HIP/OCL 口径对齐）：进程内执行（无子进程）、256³ 对拍、树 dims
  16 对齐下限 32 计时、warm 2/timed 10、e2e=dispatch+wait（steady_clock）、
  kernel_ms 取 GPUStartTime/GPUEndTime、resident-only（payload > 0.85×unified_bytes
  诚实拒绝 UNIKERN_ERROR，R10 无静默降级）；网格上限诚实拒绝路径。
- R10：`available()` 由编译期 `__APPLE__` 修正为运行时 `metal_available()`
  （`MTLCreateSystemDefaultDevice` 真实探测）——Apple 主机 `backends` 表
  `apple yes`，与 findings.md Phase 10 探测一致。
- **G4 全闭合**：Intel（Phase 9）+ Apple（Phase 10）真机执行与对拍均完成。


## Phase 11 G7 闭合补遗（2026-10-27，v7）

非 GEMM 算子的能力面闭合（转录在 `docs/evidence/phase11/`，本段引用其要点）：

- **缺陷读数（A 臂，谓词摘掉的等效状态）**：`hip_runnable_program(reduce 树)` 产出
  `const int PM = 1048576, PN = 4096, PK = 4096;` 的 `uk_gemm`，零条拒绝诊断，账本键写成
  `reduce:fp32:1048576x4096x4096`（PN/PK 是结构体默认维被顺带补进去的）。真机侧同根：
  `run_metal(gemm 内核 + reduce 树)` 报 `perf=1048576x32x32 verify=PASS measured_ms≈5.3`——
  声明一个负载、测另一个，再按前者的键回写，直接污染 R7.3 校准的输入源。
- **修复层次（B 臂，当前提交态）**：① 单一判据 `op_support_gap()`；② 生成物自拒——HIP 程序打
  `UNIKERN_ERROR what=op_unsupported` 退 4，MSL/OpenCL 文件不含内核（编译期即响亮失败）；
  ③ runner **入口**自拒，位置排在写 workdir 与编译**之前**（否则拒绝用的空源文件会盖掉
  上一次的真产物，而那个产物是证据本体）；④ CLI 门排在 ledger 任何读写**之前**，
  被拒的树留不下账本行（`--emit` 同样不落目录）。
- **每层都被证明是承重墙**：`probe/phase11_arm_probes.py` 逐层摘除 →
  谓词：红于断言 `op_support_gap(*pr.first).empty() == (op == Gemm)`；CLI 门：红于 `rc == 2`（run 臂）；
  runner 入口门：真机探针转红（错配跑出 `verify=PASS` 的测量），**同态下 ctest 仍全绿**。
  最后一条决定了一个诚实分级：**runner 层的回归位是探针，不是单测**——
  `run_metal`/`run_ocl` 需真设备，CPU 侧 acceptance 调不到，勿据此以为"全链路都有测试"。
  收尾校验：源码残留 `ARM-PROBE` 标记 0 处，ctest 2/2 绿。
- **CLI 四臂读数**（`phase11-cli-refusals.txt`）：`run --op reduce` → rc=2 且账本文件
  未创建；`solve --op reduce --emit` → rc=2 且目录未创建；`--op conv` → rc=2
  `INVALID op=conv`（六维声明 vs 三维 CLI，接受即在隐式默认上求解，属 R1.2）；
  对照 `solve --op gemm --dims 64 64 64 --emit` → rc=0、三源齐发、产物内
  `const int PM = 64, PN = 64, PK = 64;`（静默 4096 默认位从此不存在）。
- **`solve --op reduce` 仍然可解**：roofline 模型对 conv/reduce 本已成立，
  故允许求解但打印 model-only 提示并拒绝产出源——把"模型支持"与"后端可执行"分开，
  正是 R10.2 要的形态。conv/reduce 真正可用的内核生成属 **G1**，本轮刻意不做。
- **平台边界**：`src/runner_ocl.cpp` 本轮经 `-fsyntax-only -DUK_HAS_OCL` 编译检查，
  未在 Intel 真机链接/运行；AMD 的 hipcc 真编译路径本轮未执行（其拒绝由生成物文本承担，
  acceptance 做文本级断言）。Windows/ROCm 复跑与 R7.3 四臂的 Windows 侧复跑同批待做。
- **量测自捉**：第一次录 runner 拒绝对照时误用"摘门控"态编译的旧探针二进制，量到的正是
  错配执行本身；此后转录内同录当次构建的库 md5（A 臂 `7993adce…` / B 臂 `17b4384e…`）并要求重编译。
  再实测发现静态库 `.a` 的 md5 在同源码重建下也会变（ar 头含成员时间戳，`b6e0a56b…` → `f4ee8f49…`），
  所以 md5 只用于标识"读数出自哪一次构建"，同源性由臂内自带的行为判据（`op_support_gap` 返回空串 vs
  点名 reason）证明；CLI 转录改用实测稳定的可执行文件 md5（`22afe479…`）作锚。

## Phase 12 算子面闭合补遗（2026-10-27，v8）

KML 文本前端 + R10.3 覆盖报告 + G9 键身份（转录在 `docs/evidence/phase12/`）：

- **前端落地**：`src/kml_front.cpp` 的 `kml_import`/`kml_export`，flat-JSON 解析与 HCS
  共用同一个内部读取器（`src/json_flat.{hpp,cpp}`，hcs.cpp 已改为复用——两份扫描逻辑
  迟早在不一致的转义上漂移）。三类算子各自硬要 arity：gemm 3（M,N,K）、
  conv 6（H,W,C,Kh,Kw,outC）、reduce 1（L），** arity 的权威只有一处 `kml_arity()`**，
  前端校验、`kml_export`、`op_signature`、`canonical_form` 全读它。
- **R1.2 的形态**：一次列全违规（样例文件同时命中 4 条：未知字段 `dims_`、
  `op='gemmm'`、`dtype='fp64'`、`layout='colmajor'`），rc=2，求解器根本不被调用。
  子节点违规带路径前缀（`children[0].missing dtype`）。children 的作用域在读取父级
  标量**之前**被切掉，否则父会借子的 `op` 而报错报错了对象——这一条有专门的反测臂。
- **layout 不是装饰**：只接受 "NT"（三个后端 emitted kernel 实际按 A[M][K]/B[K][N]/
  C[M][N] 行主序索引的标签）；其它值在前端即拒。收下不实现的 layout 再按行主序默默
  执行，与 G7 是同一类静默。
- **R10.3 覆盖报告**：`COVER <backend> primitive=covered/unsupported reason=...
  execution=resident+streamed|resident-only toolchain=present|absent`，三轴分别取自
  `op_support_gap()`、后端对象的 `supports_streaming()`、`available()`——不设第二张表，
  报告与生成器不可能各说各话。进 `solve`（SOLVED 与 LEDGER HIT 两支）、`run`（执行前）
  与 `kml` 三个入口。
- **口径澄清（重要）**：R10.2 的"将该区间纳入 F 约束"目前的真实形态是
  *生成/执行拒绝 + 报告点名*。`solve()` 仍是 (hcs, tree) 纯函数、不知道后端是谁，
  因此 F 里没有"某后端不支持"这条谓词。把覆盖合进 F 需要给解空间加一条后端轴，属 V1；
  写在这里是为了没人把 R10.2 读成"约束层已按后端过滤"。
- **G9（规划期自查出来的键身份缺陷）**：修复前 `op_signature` 只编码 dims[0..2]，
  conv 的形状差在 Kh/Kw/outC 上的两个声明会共用同一账本键。现按 `kml_arity()` 编码，
  `[R7.1]` 臂同时断言 **gemm 键逐位不变**（Phase 8/9/10 已入库证据无需迁移）与
  reduce 未用槽位（含结构体默认 4096）既不泄漏进签名也不泄漏进 canonical form。
- **由声明驱动的 Apple 真机闭环**：`run --vendor apple --kml samples/kml_gemm.json` →
  `verify=PASS max_rel=0`、`LEDGER COMMIT ε_rel=88.7514`、同键 HIT 再 COMMIT
  `ε_rel=84.2782`（512³，比值口径同 v6.1）。conv/reduce 的声明在 ledger 任何访问前被拒，
  **账本文件根本不被创建**（fresh path 断言）。
- **六条新腿逐一摘除即红**（`probe/phase12_arm_probes.py`）：arity 校验→`:323`、
  未知字段拒绝→`:313`、children 作用域切割→`:345`、签名 arity→`:783`、
  canonical arity→`:796`、覆盖表 execution 轴→`:1133`；收尾残留标记 0、ctest 2/2 绿。
- **本轮两次自捉**：① 键身份臂最初写"reduce 只设 dims[0]"，被自己的断言抓出
  **结构体默认维会漏进签名**（这才把 arity 收成单一权威）；② R10.3 的"无账本行"臂
  复用了 solve 用过的 ledger 路径，而 `solve` 连只读查找都会创建文件 → 断言变恒真，
  改用独立新路径后才有证明力。
- **仍未做（如实）**：conv/reduce 可用的内核生成＝G1 的执行半边；
  更复杂的原语树组合语法；`--kml` 在 AMD/Intel 真机的端到端复跑（本轮只在 Apple 实跑，
  `runner_ocl.cpp` 仍只有 `-fsyntax-only` 编译检查）。

## Phase 13 算子执行面补遗（2026-10-27，v9，Apple 先行）

conv/reduce 在 Apple 上真机执行 + G6 工作集上限（转录在 `docs/evidence/phase13/`）：

- **能力面按后端拆开**：`IBackend::capability_gap()`（默认沿用共享谓词）——Apple 覆盖
  gemm/conv/reduce，HIP 程序与 OpenCL 源仍 gemm-only。CLI 的 run 门按**所选后端**判，
  ~~（本句的"OpenCL 源仍 gemm-only"已被 v10 / Phase 14 推翻：OpenCL 现在也有 conv/reduce
  内核，见本文件 Phase 14 补遗与 `docs/evidence/phase14/`；HIP 仍为 gemm-only。）~~
  `--emit` 按"任一后端覆盖"判，覆盖表因此出现异构行（`apple:covered` 与
  `amd/…unsupported reason=…op=conv` 同表）。R10.2 的"绝不静默"在异构下依然成立：
  被拒的树在 ledger 任何访问前停下，账本文件不创建。
- **MSL 内核**：`uk_conv`（implicit-GEMM、valid padding、一线程一输出——直白实现，
  不是调优实现，ε_rel 334/244 是对它的诚实测量）；`uk_reduce`（pass 1 分段求和，
  pass 2 单线程按序折叠）。verify 用的 2^-6×2^20=16384 数据在任意次序下 fp32 精确，
  所以 max_rel=0 证明的是**结构正确**，不是浮点次序鲁棒性——这条写进了源码注释。
- **共享真值**：`src/reference.cpp` 的 `cpu_conv`/`cpu_reduce` 同时供 runner 与单测使用，
  并用四组手算用例（1×1 退化、identity kernel 的迹、多通道多输出、空区间）钉住。
- **G6**：Metal 的新门与容量门**并列不叠乘**；本机实测 `recommendedMaxWorkingSetSize`
  =13631488000 B（12.7 GiB），而 0.85×声明容量=29.2 GB——旧口径放过的最大载荷是设备上限
  的 2.1 倍。arm A（落在两界之间）由新门拒绝且容量门沉默，arm B（超容量）仍由容量门拒绝。
  **判别证据是"带内拒绝"**：直接摘门会让 20 GB 载荷走到分配阶段，那既可能压垮机器，
  量到的也只是分配失败而非判定。
- **自捉**：reduce 首跑 `verify=FAIL max_rel=1.0`——pass-1 线程组取了 4096，超过
  `maxTotalThreadsPerThreadgroup`（1024），Metal 静默不执行，读到全零。修法是组宽
  `min(p,64)`；另补命令缓冲状态检查作纵深防御，但**这次抓到它的不是状态检查而是 verify**
  （该失败模式下缓冲仍报 Completed），如实记录以免把两层保护说混。
- **仍缺（如实）**：AMD（HIP 程序）与 Intel（OpenCL 源）的 conv/reduce 内核；
  `--kml` 在这两个后端的真机端到端复跑；conv/reduce 的调优（分块/tiling）与浮点次序鲁棒性验证；
  R10.2 的 F 约束合流（V1）。

## Phase 14 执行面补齐补遗（2026-10-28，v10，Intel/OpenCL）

- **生成**：`ocl_kernel_source` 按算子分派——conv 出 `uk_conv`（valid padding，一线程一输出），
  reduce 出 `uk_reduce_part` + `uk_reduce_sum`（分段求和 + 单线程按序折叠），gemm 路径未动。
  每份源只带自己算子的内核，`[R14]` 逐算子断言（跨算子内核出现即红）。
- **能力面三向**：`ocl_op_gap()` 与 `msl_op_gap()` 同构（layout 白名单 + 三算子），
  `BackendIntel::capability_gap()` 覆盖后，覆盖表为 `amd unsupported / apple covered /
  intel covered`；toolchain 轴单独列（本机 intel=absent），"覆盖 ≠ 可运行"不混谈。
- **本机可得的验证（两层，别混）**：
  ① 生成的 `.cl` 过 **clang 的 OpenCL 前端**（`-x cl -cl-std=CL1.2 -fsyntax-only`）：
     三算子 rc=0、0 诊断，负测（未声明标识符）rc=1 —— 这道检查会红；
  ② `runner_ocl.cpp` 的 host 侧（plan/launch/verify/timing 分支）过
     `-fsyntax-only -DUK_HAS_OCL` 的编译检查。
  **运行没有验**：`run --vendor intel --kml conv` 以 rc=1 收场、solution 零落库——主机
  事实，不是能力拒绝；Intel 真机的运行时验证仍待做。**失败消息随构建而异**（v12.1 注记：
  本段原文引用的「no Intel GPU platform/device found」不在所引转录的捕获输出里，出处应
  写明）——该字串出自 Mac 侧构建（UK_HAS_OCL 已定义，`build-apple/unikern`，见
  `phase14/README.md:25` 与源码 `uk_ocl_log`（`src/runner_ocl.cpp:516`）），转录
  `phase14-ocl-and-regression.txt:47-52` 只捕获了 rc=1 与 ledger_created=no；Windows
  MinGW 构建（UK_HAS_OCL 未定义）则报「OpenCL runner stub: no OpenCL import library was
  found at build time (UK_HAS_OCL undefined) …」（2026-10-28 本机实测 rc=1）。两种消息
  同属 runner 入口的诚实失败。
- **verify/计时口径与 Metal 一致**：conv 用 32×32×8 / 3×3 / 16 通道的同一结构，reduce 用
  2^-6×2^20（任意次序精确），CPU 参考仍是 `src/reference.cpp` 同一份；计时为标记事件括住的
  e2e（H2D+kernel+D2H），kernel-only 为 N 次连续 launch。
- **被本阶段推翻的旧结论（保留原文并标记）**：Phase 13 补遗里"HIP 程序与 OpenCL 源仍
  gemm-only"对 OpenCL 已不成立；`phase13-conv-reduce-device.txt` 里 intel 对 conv/reduce
  rc=2 的读数在当时为真，现在 Intel 会通过能力门并在运行期因无设备失败。
- **仍未做**：AMD（HIP 程序）的 conv/reduce（无 hipcc，连语法都验不了）、Intel 真机运行、
  conv/reduce 调优与浮点次序鲁棒性、R10.2 的 F 约束合流（V1）。

## Phase 15 AMD 收尾补遗（2026-10-28，v11，G1 执行半边闭合）

- **生成**：`hip_runnable_program` 按算子分派——conv 出 `uk_conv`（implicit GEMM 视角的
  valid-padding 二维卷积，一线程一输出，六维 H W C Kh Kw Oc），reduce 出 `uk_reduce_part` +
  `uk_reduce_sum`（分段 strided 部分和 + 单线程按序折叠；pass-1 宽度 = min(L,4096) 向上取整到
  256 线程块倍数）。每份程序只带自己算子的内核；**gemm 程序逐位不变**（生成器内原样搬运，
  fresh-solve 对 fresh-solve 的 diff 为空，`docs/evidence/phase15/phase15-compile-and-gemm-identity.txt`）。
  生成的程序独立可编译：CPU 参考内嵌（语义对齐 `src/reference.cpp`，不含核心库头），
  conv/reduce 程序过 `hipcc --offload-arch=gfx1032` 真编译（rc=0、0 诊断）。
- **能力面**：`op_support_gap()` 的 HIP 谓词 conv/reduce 从 unsupported 改 covered——三后端 ×
  三算子全覆盖；共享谓词剩下的唯一拒绝轴是 layout（`layout=colmajor` 仍点名拒绝，拒绝产物
  `UNIKERN_ERROR what=op_unsupported` 保留）。Apple/Intel 分支不动。
- **验收臂**：新增 `[R15]`（conv 源只含 uk_conv、reduce 源只含 uk_reduce_part/uk_reduce_sum、
  gemm 源不含另两者、provenance 头六字段、计时维取声明树、能力面三算子 covered）；
  `[R1.1/2]/[R10.2]/[R10.3]/[R13]` 同步为 amd=covered 期望。摘掉即红两条腿
  （分派落回 gemm → 红 @1317；谓词退回 gemm-only → 红 @305），`probe/phase15_arm_probes.py`。
- **真机闭环（RX 6600M，gfx1032）**：两条门禁命令逐字执行——
  `run --hcs samples/hcs_amd.json --kml samples/conv.kml`（conv 64×64×32/3×3/64）：
  verify=PASS max_rel=0 → LEDGER COMMIT ε_rel=67.9993 → 同键 HIT；`run --op reduce --dims
  1048576 0 0`：verify=PASS max_rel=0 → COMMIT ε_rel=0.687299 → HIT；gemm 4096³ 回归：
  PASS max_rel=0 → COMMIT ε_rel=3.42314 → HIT，UNIKERN_RESULT 字段与 Phase 8 同形。
  转录：`docs/evidence/phase15/phase15-real-device-runs.txt`（二进制 md5 同录）。
- **拒绝臂的让位（如实）**：HIP conv/reduce 不再被 CLI 门/生成物/runner 拒绝，原"rc=2
  op=reduce 拒绝且不建账本"臂改为"不可用后端诚实失败"臂（Windows 上 `--vendor apple`：
  rc=1 RUN FAILED、无 UNIKERN_RESULT、无 LEDGER COMMIT；macOS 上 apple 可用，该臂为
  _WIN32-only）。runner 层（run_metal/run_ocl 入口自拒）未动。
- **新样例**：`samples/conv.kml`（六维 64,64,32,3,3,64）与 `samples/reduce.kml`（L=1048576），
  `unikern kml` 两份均过且 OP_SIG 与 Phase 12 的 arity 口径一致。
- **被本阶段推翻的旧结论（保留原文并标记）**：Phase 14 补遗里"仍缺：AMD（HIP 程序）的
  conv/reduce（无 hipcc，连语法都验不了）"已不成立——本机 ROCm 7.1 在位，conv/reduce/gemm
  三算子真机闭环如上；`[R13]` 时代"HIP=gemm-only"的能力面表述已过时（三向 covered）。
- **仍未做**：Intel conv/reduce 真机运行（需 Intel 机器）；conv/reduce 调优与浮点次序鲁棒性
  （reduce verify 数据刻意取 2^-6×2^20，验结构不验次序）；R10.2 的 F 约束合流（V1）；
  G5 R8.5 驻留守卫长测。

## Phase 15 验收收尾 / G5 驻留守卫补遗（2026-10-28，v12）

本轮两项真机成果落账（上游事实 + 本 session 复核重采，转录
`docs/evidence/phase15/phase15-recheck-v12.txt`，README 含复现命令表）：

- **G1 执行半边的 AMD 侧闭合确认（→ G1 全闭合）**：在 Phase 16 驻留守卫后的当前二进制
  （md5 `e554574a9c618f2676fd460651fedc8a`，构建 `ninja: no work to do.` 与当时代码同源；
  **v12.1 b9cd4e8 修改 src/main.cpp 后当前锚已为 `9759d55a74e90acd11017d4d6b67a09a`——
  run/solve 求值路径未动，本节转录读数仍有效，md5 全演进见
  `docs/evidence/phase15/binary-md5.txt`**）上
  复核重采——backends 实测 `amd=yes / apple=no / intel=no`（本机无 Intel 设备）；`solve --emit`
  三算子行数 conv=253 / reduce=217 / gemm=425（= v11 首轮 219/183/390 各 +34/35 行，
  差额即 Phase 16 守卫并入三程序，每份 `uk_resident` 计数 2）；三算子真机 run
  （fresh ledger）全部 verify=PASS max_rel=0 → COMMIT → 同键 HIT：conv ε_rel=68.0154/67.4707、
  reduce 0.354586/1.10948、gemm 3.43836/3.41299——与 v11/phase16 转录同量级，真机波动
  不构成趋势结论，结论只依赖 PASS/FAIL、migrations=0 与 COMMIT→HIT 结构。
- **G5/R8.5 驻留守卫已实现并真机验证（Phase 16，commit ea76c2f）**：HIP 生成程序内
  `uk_mmalloc()` 把分配与 `hipPointerGetAttributes` 驻留断言折叠为不可分离的单点，
  非设备驻留（managed/host）→ `UNIKERN_ERROR what=oversubscription` 退 5；
  UNIKERN_RESULT 尾 `migrations=<n>`（显式拷贝路径恒 0），本轮三算子六轮真机全
  **migrations=0**。设备事实修正同 R10 口径：ROCm 7.1 的字段名是 `type` 而非任务口径的
  `memoryType`（首跑 hipcc 真编译抓到，`hip_runtime_api.h:276-284` 实测）。
  拒绝分支的设备级证据：生成程序自身路径到不了非设备驻留（hipMalloc 恒返回设备内存），
  用 hipHostMalloc 的 host 指针代位——`what=oversubscription ptr=hipHostMalloc h type=1`
  + exit=5（`probe/phase16_residency_refuse.cpp`，`docs/evidence/phase16/`）。
  `tests/acceptance.cpp` 的 `[R8.5]` 段断言三源都含守卫 token 且无裸 `hipMalloc(&` 直调点，
  `probe/phase16_arm_probes.py` 两条摘除即红腿（:1394/:1403）。
- **"真机长测"仍开放（如实保留）**：驻留守卫验证的是单次 run 的超订拒绝与 migrations
  计数归零；R8.5 的完整形态（长时间/大负载下的缺页迁移率统计、超阈触发重求解）属真机
  长测项，本轮不做、不冒充。其余开放缺口：Intel conv/reduce 真机运行验证（需 Intel 机器；
  本机 backends 实测 intel=no）、conv/reduce 调优与浮点次序鲁棒性、R10.2 的 F 约束合流（V1）。
- **本机可做项已清零**：三厂商 × 三算子的生成、能力面、本机可执行的验证路径（AMD 真机
  / Apple 真机 / Intel 编译级）全部完成；ctest 2/2 绿（0.72 s，转录同上）。

## Phase 35 补遗（v28）— out-of-sample 拆掉一个会说谎的模型：pass-1 越界是**冻结**

- **本轮动的是验收臂，不是产品**。Phase 34 的 `[R6.6-model]` 对 41 格真机读数"全中"，但那些格
  全落在 `fill=2^-6 ∧ k <= 2^24` 区域，而该区域内"连加 k 次"与"乘一次"**逐位相同** ⇒ 那批通过
  不含信息（E21/E24 家族：不能失败的对照本身就是缺陷）。
- **真机否证**（RX 6600M，`docs/evidence/phase35/`）：naive 模型在 22 个新格上判错 5——
  `fill=0.1` 一格把 `rel` 的**符号**算反（预测 `+4.8336e-06` / 实测 `-1.092e-05`），
  `k=17500000` 一格差出七个数量级（预测 `0` / 实测 `-4.130e-02`）；忠实连加模型 29 格全中。
  机制是悬崖不是斜坡：`2^-6` 加进 `2^18` 是 round-half-to-even 平局，部分和此后不动，
  `rel` 随 `n` 无界（`L=33554433, width=1` 实测 `result=262144.0 / rel=-5.000e-01`）。
- **臂的当前口径**（`[R6.6-model]`）：41 格 = in-sample 19 + out-of-sample 22；
  `safe=34 / split=7`，naive 判错 7 格并作为活控制入库（`assert(naive_bad >= 5)`）。
  打印件：`worst |rel| reachable=0.000240667 overall=0.5 vs gate=0.03125`。
- **不新增 G 行，理由如实**：触发需 `ceil(n/width) > 2^24`，而现役 `width >= 256` **由发射产物自证**
  （`L=100` 的树仍发 `const int UK_REDUCE_WIDTH = 256;`）⇒ 需 `n > 4.3e9` 元素 = 17.2 GB fp32，
  是本仓最大 vram 档 8589934592 B（`docs/evidence/phase8/hcs_amd.calibrated.json:12`）的两倍
  ⇒ 现役配置不可达，故这是**规格边界**而非 MVP 缺口；边界已写入生成器注释
  `FREEZES`（`src/codegen_hip.cpp:762`）与臂的前置条件（`[R6.6]` `tests/acceptance.cpp:1777`、`:1632`），
  并有两条**各自跑过**的摘除控制（边界 `2^24→2^20` 红在 `naive != pred`；地板 `256→4`
  红在发射产物断言；两者均 byte-identical 复原、复跑 rc=0）。
- **G10 口径不变**：计时规模的结果仍不在验证范围内。本轮改变的是"该规模会不会精确"这件事
  从模糊变成可算，并证明我原先的算法在两个独立轴上都会错（可表示性上限、非 2 的幂填充）。
- **仪器**：`probe/uk_reduce_width_probe.cpp` 新增 `argv[3]=fill`；所有 HIP 调用失败改印
  `msg=hipGetErrorString` 并 `return 2`（`n=10^8 nP=4` 段两次都是 `unspecified launch failure`，
  首版探针丢掉设备后还继续 `continue` 空转 = E29）；补测一 n 一进程。
- **文档一致性**：给生成器加注释使 4 个被 5 份文档引用的行号锚点整体下移 12，`anchors` 的
  ±25 行窗口**没有报红**——已按符号逐个重算（E30）。
- **未动**：生成器发射字节（conv 542 / gemm 447 / reduce 239）、容差 `2^-5`、验证实例 `2^20`、
  折叠次序、D8/D9 取舍。**本机可做项仍已清零**。

## Phase 42 补遗（v30）— harness #5 闭合：复原保证成为 ctest 门禁，并揪出一条恒真的臂判据

- **#5 范围纠正**：phase11-14 不是"缺 finally"，而是 ROOT 硬编码 macOS 路径 + `build-apple` + `/tmp`
  产物，**本机无法运行**，复原路径从未被执行；phase15/16 可移植但复原靠显式调用。
- **机械保证**：`probe/arm_probe_guard.py`（按字节快照 → 唯一写路径 → 退出重读比对 → 全 `src/`
  残留 `ARM-PROBE` 扫描 → 与进入时的 `git status src/` 比较漂移）；六个探针统一 `guard_main(main)` 入口。
  Apple 专属步骤由 `has_apple_toolchain()` 闸门打印 SKIPPED，把"没测"与"测过且过"分开写。
- **新 ctest 测试 `probe_hygiene`**（第 4 条）：任何写 `src/` 的探针必须有守卫化写路径、守卫下入口、
  推导式 ROOT。开火控制跑过：去守卫副本被点名 4 条并 rc=1，删除后 rc=0。
- **真跑而非纸面**：`phase15`（2 腿）、`phase12`（6 腿，本机首次能跑）、`phase16`（2 腿）
  每条腿都产生具名失败断言，收尾一律 byte-identical、无残留、无漂移。
- **臂的真缺陷（E38）**：`[R8.5]` 判"结果行必须带 `migrations=`"用存在性 `find()`，
  同程序拒绝行带同一 token ⇒ 移除结果行发射仍绿。改为按程序计数 `== 2` 后，
  `phase16/reduce_migrations_token` 腿立即红在 `Assertion failed: n == 2`
  （`[R6.8]` `tests/acceptance.cpp:1923`）。这是 E21/E24 家族第三次复发，潜伏 26 个相。
  **固化：没跑过摘除控制的腿不算腿。**
- 过期站点两处已重定位（`op_support_gap`→`msl_op_gap`、`UK_MIGRATIONS << std::endl`→拆链形状），
  存活性盘点工具 `probe/phase42_probe_site_audit.py`：21/21 站点活。
- 发射字节未动（conv 对 phase29 基线空 diff、gemm 447、reduce 239）；`ctest` 4/4。
  harness 报告的本机可做项至此只剩 **#4**。

## Phase 43 补遗（v32）— harness #4 闭合：证据输入进版本控制，新增 `record_audit` 门禁

- **实质**：`.gitignore` 忽略 `build/`、`uk_run/` 是正确的；错在**声明式输入住在被忽略的树里**——
  六份证据 README 的复现命令依赖 `uk_run/phase19conv/conv_*.json`，克隆拿不到； （旧路径）
  同时三件 Phase 9 Intel 证据被强制跟踪在忽略树 `build/phase9-emit/`（gitignore 不影响已跟踪文件， （旧路径）
  因此它们在 `git status` 里隐形）。
- **落地**：`git mv` 三件产物 → `docs/evidence/phase9/emit/`（搬前搬后 sha256 一致，索引里记为 rename，
  **未提交**）；conv 声明输入 → `workloads/conv/`（`git add` + `git ls-files` 实证跟踪）；
  README 复现命令改指新路径；从新路径重新发射 conv，与 `docs/evidence/phase29` 基线**空 diff**。
- **新 ctest 测试 `record_audit`**（第 5 条）：扫规划文件与全部 `docs/evidence/*/README.md` 中
  带扩展名的路径引用，报 MISSING / IGNORED INPUT。两轮收紧：首版 21 条噪声里只有 1 条真
  （`docs/AGENTS.md`，该文件在仓库根，已改）；逐条 `git check-ignore` 慢至 21 s，
  改为一次 `--stdin` 批处理。开火控制：注入忽略树 `.json` 引用 ⇒ rc=1；清除后 rc=0。
- **自酿错误**：E39（`.gitignore` 追加说明时目录名单独成行 = 忽略规则，新目录反而不被跟踪，
  靠真跑 `git add` 才暴露）、E40（目录级替换把同目录运行产物账本一起迁走，**被本相新门禁当场抓到**）。
- 另补 `.gitignore` 的 `__pycache__/`、`*.pyc`。**harness 本机项至此全部闭合（#1–#5）**；
  产品代码与发射产物未动，`ctest` 5/5，Phase 18–43 全部工作仍未提交，D8 / D9 / D11 待用户定。


## Phase 44 补遗（v33）— D9 落地，并把我自己引用的提速数字降级

- **改了什么**：reduce 的 pass-1 宽度由发射程序**运行时**按设备自报量推导
  （`maxThreadsPerMultiProcessor × multiProcessorCount` 向下取 2 的幂，256 地板；
  查询失败回退 4096 并打印 `source=props_unavailable`）。本仓 HCS 声明 `compute_units: 36`
  而设备自报 14 —— 这正是不能用名义值的理由（按 36 会得 65536）。
- **顺带关闭**：reduce 发射程序 verify 段原先写死 `VP = 4096`，与计时段不同配置（Phase 26 在
  conv 上修过的同形缺口）。现在全 run 一个宽度，真机三档 `verify=PASS max_rel=0.000000e+00`，
  并打印 `reduce_width source=device width=16384 cap=28672`。
- **被证伪的是我自己的数字**：出厂路径两种宽度 `kernel_ms` 差 ≤5%（16.7M 0.745 vs 0.782、
  67.1M 1.895 vs 1.911），而 Phase 30/33 的探针对同一对宽度报 1.65–2.50×。分歧在**基线测量**
  （探针 4096 的 pass-1 单项 1.161 ms > 出厂同尺寸总 kernel 时间 0.78 ms）。
  ⇒ D9 的账面上收益改为：设备自报量选择 + verify/计时同配置 + pass-1 冻结余量变宽；
  **速度增益不成立**，成因待下一相查（候选：warmup/迭代编排、L2 驻留、事件覆盖跨度、pass-2 计入方式）。
- **发射产物**：reduce 239 → 268 行（新基线与 sha 在 `docs/evidence/phase44/`）；
  conv 对 `docs/evidence/phase29/conv-hip-knob-check.txt` **空 diff**、gemm 447 行不变。
- **臂**：`= 4096` 常量与 `= 256` 地板两条断言按新形状重写（钉算术文本与设备读数），
  新增单一计划计数腿；没有删除任何断言。`ctest` 5/5。
- **新缺口 G13**：Metal/OpenCL 的 `reduce_width()` 仍是 4096 常量（本机无设备可验，不猜改）。
## 附录 v34（2026-10-28，Phase 45：D11 闭合 + 第 6 条门 `runner_tu`）

- **本轮闭合**：G12（dtype 只进成本与规范键、不进生成能力，且能把不存在的内核定价入库）。
  路径 = 能力区间拒绝（`dtype_gap`）+ 分级容差唯一表（`tolerance_for`）；不是补齐 half/int 内核。
- **口径演进（不改写历史）**：Phase 11/12 的转录里 `solve --op reduce`「可解但打印 model-only、
  拒绝 `--emit`」是当时事实；自 Phase 45 起 **model-only 分支已不可达并被删除**，未覆盖的声明树
  在 `ledger_key()` 之前就以 rc=2 退出，账本文件不被创建。相应地
  `docs/evidence/phase29/README.md` 的 conv 基线（542 行、`sha256=599a4e71…`）与 Phase 44 记录的
  「conv 对 phase29 基线空 diff」今天起恰好差 **1 行**（判定行 `0.03125f` → `0.031250f`）；
  新 conv `sha256=09e9b570…`、新 reduce `sha256=c3a26462…`，gemm 首次有字节基线 `7a451491…`。
- **门禁**：ctest 由 5 条变 **6 条**，新增 `runner_tu` —— 没有 Intel SDK 时 `src/runner_ocl.cpp` 的身体
  被 `#ifdef` 掉、`runner_metal.mm` 不在目标列表里，这两处"存在但从不编译"的源码第一次有了
  能编译其中一具的检查（`-fsyntax-only -DUK_HAS_OCL`，用镜像 CL 头，无需设备/链接），
  另一具只能由 `[R6.8-dtype]` 的源码文本子腿覆盖并如实打印覆盖模式。
- **诚实记录**：控制 C5 表明"写死成与表相同的字面量"今天无法被任何腿区分（只有 fp32 档能到达产物）；
  `verify_fp32()` 仍只接 fp32 档，整型位级目前只存在于拒绝路径；D8（差分参考/容差基准口径）未动。

## 附录 v35（2026-10-28，Phase 47：D8 口径 A 落地——reduce 的基准换成规格的第二档）

- **决定的来源**：Phase 46 的取证 + 用户在 A（换分块档）/ B（容差随 n）/ C（维持现状）里选 A。
  B、C 作废；容差 2⁻⁵/2⁻⁴/整型位级三档自 Phase 45 起已由唯一的表 `tolerance_for()` 给出，本相没动它。
- **实现**：`kRefReduceWidth = 1024`（`kRefReduceWidth` `include/unikern/core.hpp:124`）＝参考自己的分片数；
  `cpu_reduce_blocked`（`src/reference.cpp:36`）；发射程序的 `cpu_reduce_ref` 与两个进程内 runner 同口径。
- **这条改动最大的风险被写成断言而不是注释**：基准若与内核同形状（同分片数、同折叠次序），`max_rel`
  恒 0 —— 仪器给自己打分。`[R6.9-basis]`（`tests/acceptance.cpp:2314`）钉 `basis != kern` 与
  `div_new > 0`；控制 B1 把宽度对齐到 16384 时首条红断言就是 `basis != kern`。
- **中性是前置条件而非事后检查**：Phase 46 先在 shipped 验证输入上证明两档逐位相同，本相才落地；
  真机五档全 `verify=PASS max_rel=0.000000e+00`（reduce 1M/16.7M/67.1M、gemm 512³、conv 256²×32），
  gemm/conv 产物 **空 diff**，reduce 268→276 行 `sha256=44231aef…`。
- **病理消失**：交替 `1e6/1e-6` @ n=2²⁴，旧基准与内核差 `4.160e-02`（> 2⁻⁵ ⇒ 更准的一方被判 FAIL），
  新基准差 `2.413e-04`（≤ 2⁻⁵）。
- **范围与不粉饰**：gemm/conv **不换**（它们的参考与 `[R20]` 按字节钉住的累加次序同形，换档＝改结论）；
  第二档与内核同属部分和顺序折叠家族，**不是绝对真值**，宽输入下谁更准会反转（Phase 46 结论 3）。
- **本相还顺手补了一个记录的洞（E46）**：`record_audit` 现在核对 AGENTS.md 自述的硬约束条数与实际
  编号列表——Phase 45 我改了标题的"八条"而正文从未落地，这件事当时没有任何门能看见。

## 附录 v37（2026-10-28，Phase 49：D9 的提速主张恢复；reduce 有了自报身份的钉值通道）

- **纠正的不是代码行为而是结论**：Phase 44 写下「1.65–2.50× 不成立，只能算探针现象」。
  本相实测证明那是**一次从未生效的 A/B**：Phase 44 没有宽度钉值通道，改用 `sed` 手改已发射源码造
  baseline-4096 臂，而变换静默没生效 —— 它的转录里那条臂**不打印宽度横幅**，`kernel_ms` 与
  runtime 臂几乎相同（0.7816 vs 0.7452；1.9108 vs 1.8950），也就是**两臂同为 16384**。
- **出厂路径实测**（`UK_REDUCE_WIDTH` 钉值、每臂自报 `source=env width=`、交替 3 轮取中位，
  全部 `verify=PASS max_rel=0`）：4096→16384 = **1.685× @16.7M / 2.499× @67.1M / 2.787× @134M**；
  8192 与 32768 都劣于 16384 ⇒ 设备自报量 `2048×14=28672 → 16384` 落在采样最优附近。
  R6.4 的「出厂收益」这一列因此恢复为速度收益，而不只是配置来源与 verify/计时同配置。
- **落地的通道**：发射程序的 `uk_reduce_width_plan` 先读 `UK_REDUCE_WIDTH`（生效打印 `source=env`，
  值不在 `[256, n]` 打印 `what=reduce_width_pin_unfit` 并 `exit(2)`，只读一次），
  与 conv 的 `UK_CONV_TILE`/`UK_CONV_OCB` 同口径（硬约束第 4 条已补写）。
- **新臂与基线**：`[R15c-width-pin]`（`tests/acceptance.cpp:2139`）按**单调锚点序列**核顺序；
  `[R5.4-artifact-bytes]` 的证据基线移到 `docs/evidence/phase49/emit/`（reduce 276→**290 行**
  `b8862c99…`，gemm/conv 逐字节不变）；`probe/phase48_artifact_build_run.py` 同步指向新基线，
  三份存盘字节仍 `build rc=0 verify=PASS`。
- **方法上的收获（E50）**：控制必须能归属到具体条款。影子 `SOURCE_DIR`（把 `[R5.4]` 变成构造性为真）
  当场抓出一条**惰性控制**（替换串没匹配上，等于没改字节）和一条**相邻而非序列**的弱顺序断言。
- **仍未闭合**：P3 那种"产物编不过"的缺陷在 ctest 里看不见（文本臂不编译），只能靠需要 hipcc 的
  脚本；宽度曲线只测了 4 档 × 3 尺寸；`runner_metal.mm` 本机仍无法编译；half/int 内核不存在。

## 附录 v38（2026-10-28，Phase 50：ctest 第 7 条门 `hip_programs`——生成的程序第一次被编译器解析）

- **补的洞**：acceptance 的臂把发射产物当**文本**读，门 #6 只编译 runner 的 TU；在此之前没有任何腿
  让 hipcc 解析过一份**生成的程序**。Phase 49 的控制 P3 已经证明"产物编不过而全绿"是可达状态
  （我当时把它记成"仍未闭合"）。
- **门的做法**：CLI 现场发射 gemm/conv/reduce → `hipcc -fsyntax-only -O3 --offload-arch=<设备自报
  gcnArchName>`；架构由 host-only 探针问设备（与 `src/runner.cpp` 的 `kArchProbeSrc` 同形），
  不写死；首行是 broken-source 控制。7.8 s / 全套 12 s。
- **三条控制期望各不相同**：K1（把 gemm 程序里的 `e0` 改名）→ 归属后 acceptance **GREEN**、门 **RED**
  （覆盖面证据）；K3（只改注释）→ 两边都绿（证明它不是"任何字节变化都红"）；
  K2（Phase 49 的 P3）→ 两边都红（与文本臂重叠，可接受）。
- **跳过语义**：无 hipcc 或有 hipcc 但看不见 ROCm 设备，两条路径都实测退 **77**，
  `SKIP_RETURN_CODE 77` 使 ctest 报 **Skipped 而不是 Passed**；`--arch <name>` 是标注过的无卡旁路。
- **不越界**：语法检查 ≠ `verify=PASS`。上机执行仍由 `probe/phase48_artifact_build_run.py`
  （存盘字节自己编译运行）与 `run --vendor amd` 负责；MSL/OpenCL 产物在各自主机上才有工具链可解析。
  **最后一行的 OpenCL 半句由 Phase 52 实测否证（见附录 v40）**：ROCm 自带 clang 能解析 OpenCL C，
  所以 `.cl` 产物在 AMD 主机上就进了门；仍成立的是 MSL 半句，门也因此改名 `generated_programs`。

## 附录 v39（2026-10-28，Phase 51：G13 拆开——OpenCL 查设备、Metal 诚实保留常量、三后端共用 UK_REDUCE_WIDTH）

- **能落地的部分先落地**：`plan_reduce_width` `src/runner_ocl.cpp:190` 用 `CL_DEVICE_MAX_COMPUTE_UNITS ×
  CL_DEVICE_MAX_WORK_GROUP_SIZE` 推 pass-1 宽度，走与 HIP 程序相同的 2 的幂 + 256 地板算术；
  任一查询失败退回 4096 且打印 `source=props_unavailable`。两个 runner 的 verify 段改为复用
  计时趟的 `plan.P`（rule 6 在 runner 侧的同形缺口，顺带补掉）。
- **查不到就说查不到**：Metal 公共 API 不报 CU 数、也不报每 CU 线程数，于是
  `plan_reduce_width` `src/runner_metal.mm:136` **保持常量**并把来源标签写成 `metal_no_capacity_api`。把常量包装成
  测量值是这个仓库明令禁止的一类错误（"设备自报量、不抄名义值"），所以这一半缺口**保留而不硬凑**。
- **共同词汇**：三后端读同一个 `UK_REDUCE_WIDTH`，语义一致——生效打印
  `UNIKERN_INFO reduce_width source=env width=<w>`；值不在 `[256, n]` 打印
  `what=reduce_width_pin_unfit` 并**拒绝执行**。跨后端的宽度比较从此可比，且这套语义不需要
  Intel/Apple 设备就能被证明。
- **验证边界（明说）**：本机没有 Intel 设备、没有 macOS ⇒ **G13 相关没有任何 Intel/Apple 数值读数**。
  能做的机械检查都做了：门 #6 `-fsyntax-only -DUK_HAS_OCL` 真的编译 OpenCL 身体；
  `[R15d-runner-width]`（`tests/acceptance.cpp:2195`）核旋钮/拒绝/自报/`VP = plan.P`/单次读取；
  三条控制**期望各不相同**——R1（丢地板算术）与 R2（不合适的钉值不拒绝）只有文本腿红，
  R3（设备查询里打错变量名）文本腿全绿而门 #6 红。AMD 侧 reduce/conv 真机仍
  `verified=PASS max_rel=0 migrations=0 checks=6`。
- 一次老毛病的第三次复发被门接住了（E52）：我把真换行写进 C++ 字符串，落在被 `#ifdef` 掉的
  身体里，`cmake --build` 与 `ctest` 都不会看见——门 #6 在 0.1 s 内报出并指到行号。
- `ctest` 7/7；`probe/phase51_g13_removal_probe.py` 归属表入库 `docs/evidence/phase51/`。

## 附录 v40（2026-10-29，Phase 52：门 #7 覆盖面实测——OpenCL 产物在 AMD 主机上就能解析，门改名 `generated_programs`）

- **上一相的边界声明被当成待测命题**：`docs/evidence/phase51/README.md` 与附录 v38 都写着
  "MSL/OpenCL 产物要各自主机的工具链"。实测：ROCm 7.1 自带
  `D:\Program Files\AMD\ROCm\7.1\bin\clang.exe`，`clang -fsyntax-only -x cl -Xclang
  -finclude-default-header` 对三份 `uk_intel.cl` 全 rc=0，追加一个坏 kernel 立刻 rc=1。
  ⇒ Intel 目标的**语法**不需要 Intel SDK、不需要 Intel 设备就能检查；MSL 仍是真缺口。
- **门的形状改成"每个后端三段"**：可用性探针（`find_hipcc`/`find_clang`，由探测决定，不由名字决定）
  → 该后端自己的 broken-source 控制（良好源码必须被接受**且**坏源码必须被拒绝，否则
  `PROG SKIP <backend> reason=control:`）→ 同一份生成结果的真产物。`checked=6` = 3 op × 2 后端。
  跳过码 77 的条件收紧为"一段都没跑"；两条报警方向都用假前端实测过（恒接受 → `decorative`，
  恒拒绝 → `not usable here`），见 `docs/evidence/phase52/gate-output.txt` 第 3 节。
- **三条控制期望各不相同**（`probe/phase52_clparse_control_probe.py`，全部走影子 `SOURCE_DIR` 归属）：
  Q1 破坏生成的 OpenCL C（conv kernel 签名里塞 `}`）→ 归属后 acceptance GREEN、门 #6 GREEN、
  门 #7 **红在 ocl 半边**；Q2 只改注释措辞 → **全绿**（它检查语法不是字节指纹）；
  Q3 破坏 HIP 程序（gemm 宿主签名塞 `EXTRA_TOKEN`）→ 门 #7 红在 hip 半边而 **ocl 仍绿**（两半独立，
  所以"6 项"是 6 项而不是"一件事数两遍"）。
- **锚点唯一性当场被抓**（E21/E24 同族）：Q3 最初取 `int M, int N, int K) {`，`src/codegen_hip.cpp`
  里出现 **2** 次（`:1054` 与 `:1089`），预检查直接 `found 2 times -- aborting`，换成
  `std::vector<float>& C, int M, int N, int K) {` 才唯一。
- **顺手量到两条事实**：(1) 发射的 `uk_intel.cl` 是纯 kernel 源码、没有宿主 `main`，因此
  `UK_REDUCE_WIDTH` 出现 0 次（HIP 那份 9 次）——Intel 侧宽度自报在 `src/runner_ocl.cpp` 的
  `plan_reduce_width()`，这不是硬约束第 4 条的洞；(2) 门 #7 只解析文本、**不钉字节**，而
  `[R5.4-artifact-bytes]` 只覆盖三份 `uk_amd.cpp` ⇒ **OpenCL 产物的字节锚仍缺**。
  **（此条由下一相补上：Phase 53 落地 `[R5.4-artifact-bytes-cl]`，见附录 v41。本相按"基线先入库、
  腿随后补"的顺序做，当时确实没有腿。）**
- 改名带来的死引用由门 #5 接住：`progress.md` 与 `docs/evidence/phase50/README.md` 里的旧路径各报一次
  `MISSING`，按"转录不改写、只标时点演进"处理（旧名写成不带目录的名字，可复现命令指向新文件）。
- 成本 9.8 s；`ctest` **7/7**。证据：`docs/evidence/phase52/`（README + `controls.txt` + `gate-output.txt`）。

## 附录 v41（2026-10-29，Phase 53：OpenCL 产物拿到自己的字节锚 `[R5.4-artifact-bytes-cl]`）

- **补的是 v40 自己写下的后半句**（"这只是基线，不是腿"）。洞的形状与 Phase 48 补 HIP 那份同形：
  门 #7 只解析语法并明说自己不是字节指纹（Q2 的期望就是 GREEN），`[R5.4-artifact-bytes]` 只钉
  `uk_amd.cpp` ⇒ **改一句发射出去的 OpenCL 注释，全套仪器一件都不红**。
- **新臂 `[R5.4-artifact-bytes-cl]`**（`tests/acceptance.cpp:2999`）：CLI 现场发射 gemm/conv/reduce，把每份 `uk_intel.cl` 与
  入库基线逐字节比较；**逐 op 先打印 `identical`/`DRIFT`，再断言 `drift==0 && identical==3`**
  `identical`（`tests/acceptance.cpp:3052`）。打印先于断言是因为一个二进制里有两条字节腿时"哪条 banner 没出现"
  只是顺序假象，不是归属。
- **四条控制，期望各不相同**：C1 改发射的 OpenCL 注释 → **只有新腿红**（门 #7 绿、HIP 腿绿）；
  C2 conv 签名塞 `}` → 新腿与门 #7 同时红（重叠，如实记）；C3 改发射的 HIP 注释 → 红在**另一条轴**
  （HIP 腿 banner 不出现，新腿未跑到）；C4 改生成器自己不发射的注释 → 全绿（不是"改动即红"）。
- **本相最承重的一条是方法**：新臂落地后影子 `SOURCE_DIR` 归属**失效**——装置原先只把 HIP 基线
  重发射进影子树，于是新臂在每个影子控制上都因"文件不存在"假红，Phase 52 的 Q1/Q2/Q3 期望集体被打掉。
  修的是**装置**（`probe/phase49_widthpin_removal_probe.py:113` 把同一 `--emit` 目录里的 `uk_intel.cl`
  也种进影子树），**不是**改旧期望；改完后 phase52 三条控制一条期望都不必动就重新全中。
  ⇒ 规则入库 AGENTS：**归属装置必须覆盖全部字节腿，一条都不算"够"**。
- **期望的粒度就是控制的粒度**（E55）：第一版把"哪个 op 漂移"解析成 `split()[2]`（应为 `[1]`），
  归属列变成字面量 `DRIFT`；**是预登记里具体到 op 名的期望把它抓住的**——若 `want` 只写"acceptance
  应该红"，这个 bug 会安静地跟着绿过去。同批第二处：转录目录未预建，四条控制跑完约 3 分钟后才在
  写文件的最后一行崩，输出全丢 ⇒ 探针现在动手前先检查输出路径的父目录。
- **两条分支都实测**：`SOURCE_DIR` 设 → `3/3 identical`（2594/1877/1708 B，基线未被覆盖）；
  `env -u SOURCE_DIR` → 两条字节腿并列打印 `SKIPPED`、rc=0（不是静默通过）。
- 成本：acceptance 2.0 s → 2.4 s；`ctest` **7/7**。证据：`docs/evidence/phase53/`
  （README + `controls.txt` + `leg-output.txt`）。
- **仍未闭合的同类缺口**：MSL 产物既没有语法检查（本机无 Metal 前端）也没有字节锚。
  **（此句写于 Phase 53；Phase 54 把"没有字节锚"这半关掉，"没有语法检查"那半的原因从断言变成读数，
  见附录 v42。）**

## 附录 v42（2026-10-29，Phase 54：Metal 的语法 = 主机事实（读数），Metal 的字节 = 本机第三条腿）

- **两半被分开**，此前它们混在一句"本机没有 Metal 工具链"里：
  语法检查是**主机事实**，字节锚**不需要前端**——所以后一半在本机就能闭合，前一半只能老实报缺口。
- **先量再写**：门 #7 原先打印的 `PROG gap msl reason=Metal has no front end reachable…` 是**断言**。
  现在改成逐个探可及的 clang（`clang_list()` → 两步 `metal_probe()`：先问语言、再问 `<metal_stdlib>`），
  本机两个候选都停在第一步：ROCm 7.1 clang 21（`Compute-Mirrors/llvm-project` 裁剪版）与
  wasi-sdk 34 clang 23.1-rc3 都报 `error: language not recognized: 'metal'`（`-x metalpp` 同）。
  ⇒ 缺口行带着前端原话；**认 Metal 的主机上同一段代码自动继续跑控制与真产物**，升级不依赖有人记得改代码。
- **新臂 `[R5.4-artifact-bytes-msl]`**（`tests/acceptance.cpp:3063`，断言 `:2962`）：逐 op 现场发射
  `uk_apple.metal` 与入库基线逐字节比较，先打印 `identical`/`DRIFT` 再断言。基线 61/40/36 行 =
  2893/2105/1885 B + sha256 入库 `docs/evidence/phase54/emit/`；旧基线一字未动。
- **三条控制逐字段命中 `want`**：M1 改**发射出去**的 Metal 注释 → 全套仪器里**只有这条腿红**
  （门 #7 GREEN、HIP 腿打印、CL 腿 `3/3`）＝这条腿存在的理由；M2 改 OpenCL 注释 → CL 腿先 assert，
  **MSL 腿一行都不打**（顺序伪装成归属——Phase 53 那条方法第二次被实证为必要条件）；
  M3 改生成器不发射的注释 → 全绿。
- **归属装置的规则在第二次生效时被复验**：`shadow_root` 现在把 `uk_amd.cpp`/`uk_intel.cl`/
  `uk_apple.metal` 三份都种进影子树；改完**重跑 phase49/50/51/52 四条旧影子控制套件**，
  它们的 `want` 一个字节都没改就仍然全中。⇒ AGENTS 那条"装置必须覆盖全部字节腿"从此有两次观测。
- **一条运气如实记下**：Metal 与 OpenCL 的 reduce 注释文本本来不同（`one per thread of the launch.` /
  `one per global work item.`），所以 M1/M2 天然各属一个后端；文本若相同，归属要另想办法。
- **明说的未 exercised 半**：Metal 分支的"控制 + 真产物"在本机无法执行（前端不存在）；
  `.metal` 从未被任何前端解析过，Apple 执行证据只在 `docs/evidence/phase10/`（macOS 主机）。
- 成本：`ctest` **7/7**，acceptance 2.0–2.4 s（三次额外发射）。证据：`docs/evidence/phase54/`
  （README + `controls.txt` + `msl-probe.txt` + `leg-output.txt` + `emit/`）。

## 附录 v43（2026-10-29，Phase 55：字节红不等于轴红——`[R20-nonhip]` 钉住两个非 HIP 后端的累加次序）

- **补的区分**：三条字节腿（Phase 48/53/54）能对**任何**产物改动报红，但红里不含"哪条轴"。
  硬约束第 3 条把"改折叠次序"（已入库读数改含义）与"改注释/布局"（仍可比）分开；HIP 侧有 `[R20]`
  按字节钉嵌套，**OpenCL/Metal 侧此前没有任何臂**。这不是抽象风险：Intel/Apple 的入库读数
  正是 Phase 9/10 用那几段嵌套跑出来的。
- **新臂 `[R20-nonhip]`**（`tests/acceptance.cpp:2811`）：进程内调用 `ocl_kernel_source`/`msl_kernel_source`
  （零额外 CLI 发射），2 后端 × 3 算子 = 6 个 pin，各钉一条**单调锚点序列**（E50：相邻不算）加
  **累加语句恰好 1 次**（E21/E24：存在性判据会被第二次出现杀死）；臂**自己数打印的判决行数**
  （`assert(printed == 6)`，`tests/acceptance.cpp:2836`）——少打印的臂等于没有臂（E38 同族）。
  臂排在三条字节腿之前，因为顺序本身就是归属手段。
- **归属方法换掉**：CRT 消息带**表达式 + 行号**，探针在运行时读 `tests/acceptance.cpp` 的
  `  // -------- R` 臂头表，把行号映射回**拥有它的臂**。臂表实测：`R20-nonhip 2747-2845`、
  `R5.4-cl 2846-2909`、`R5.4-msl 2910-2976`。这比"哪条 banner 没出现"强，后者只知道谁先跑。
- **五条控制，归属各不相同**：N1 **只在 `ocl_conv_kernel` 内**把 conv 外层 `c` 循环改成倒序 →
  RED 归 `R20-nonhip`（`order_ok == 6`），broken **只有 `ocl/conv`**（`msl/conv` 仍 `order=ok`），
  门 #7 **GREEN**（产物仍是合法 C++）＝只有次序臂会说话的真改动；N2/N2b 改发射出去的注释 →
  红分别归 `R5.4-cl`/`R5.4-msl`，次序臂先打印 6 条全 ok；N3 改 HIP 注释 → 归 `R5.4` 而次序臂
  **一行都不打印**（排在其后）＝方法的局限，如实记；N4 改生成器不发射的注释 → 全绿。
- **两次设计自我修正**：(1) 原打算对调 `kh`/`kw` 两行循环头——会破坏 `{`/`}` 配对、把门 #7 一起拖红，
  归属就脏了；改成反转外层 `c`（合法 C++、真实折叠序变化）。(2) 这些针在两个后端各一份
  （整文件计数 2），所以补丁必须**按函数切片**并断言片内恰好一次；整文件 `replace(...,1)` 会命中
  先出现的那份（Metal）却把红报给 OpenCL（E50 与 E21/E24 的合流）。
- **本相最值钱的副产品是一条仪器盲区，带读数**：新臂插在字节腿之前，使 Phase 53/54 写下的五个
  acceptance.cpp 引用（当时写在 :2747/:2787/:2800/:2811/:2863）整体偏移约 99 行，而门 #3 报
  `all doc->code anchors agree`（全绿）。原因在规则本身：它只在被引用行**带有可核对的代码符号**时
  才核 ±25 窗口，而那些行带的是 `[R5.4-artifact-bytes-cl]` 这类方括号标签，被符号过滤器排除
  ⇒ 检查在该点空转。本轮按 E23 把五个引用全部 grep 现取改指；
  **"无法核对的引用必须显式计数或失败"记为下一相的靶子**（静默通过的判据比红更危险，是本仓一贯的判据）。
- **自己的两处期望缺陷（E56/E57）**：判决行在转录落盘**之后**才打印 ⇒ 入库的 `controls.txt`
  不含自己的结论；把"六条都 ok"塞进 `want["broken"]`（该字段按定义只收破的行）⇒ 三条控制
  结构上永远不可能匹配。两条都属于"期望必须具体到字段"这一族（E55 继续生效）。
- 成本：门 #2 2.4 s；`ctest` **7/7**，守卫 byte-identical。证据：`docs/evidence/phase55/`
  （README + `controls.txt` + `leg-output.txt`）。
## 附录 v44（2026-10-29，Phase 56：门 #3 的静默段拆成三层——臂标签按臂段归属，两条上限只许下降）

- **补的是 v43 自己记下的靶子**（"无法核对的引用必须显式计数/失败，不能静默通过"）。先数再改：
  落地前 288 条 `path:line` 引用里 **132 条**所在行没有任何可核对符号——本仓引用臂的写法是
  `` `[R5.4-artifact-bytes-cl]`（`path:line`）``，方括号标签被旧的符号过滤器排除，于是门一路报 agree。
- **三层判定**（`probe/anchors_check.py`）：①邻接符号 → ±25 窗口；②**臂标签 → 臂段归属**：标签映射到
  `  // -------- R` 臂头表**加上该臂自己 `std::cout` 打出的 banner**（只按头表会漏长形 banner，
  只按"标签在文件里出现过"就又退回 E21/E24 的存在性判据），被引行必须落在该臂段内；
  ③段落有符号而引用无邻接符号 → **weak**；④附近什么都没有 → **unverifiable**。③④各有一条**只许下降**的上限。
- **第一次运行就抓到 19 条真实陈旧引用**：`docs/acceptance-mvp.md`(3)、`docs/evidence/phase24|26|31` 各 1、
  `findings.md`(5)、`progress.md`(2)、`task_plan.md`(5)、`AGENTS.md`(1)。没有一条是"标签指向不存在的臂"，
  全是行号随代码增长漂移；全部 grep 现取改指并保留"当时行号"注记。其中一条是**跨文件指控**：
  HIP 侧 `dtype_tol_literal` 被邻接到 runner 的旧行号，runner 真正读的是
  `tolerance_for`（`src/runner_ocl.cpp:683`）与 `tolerance_for`（`src/runner_metal.mm:623`）；
  已改写成两句各带自己的符号——符号必须与被引号**同一物理行**，否则就落进 weak 层。
- **棘轮当天响了三次，三次都修记录**：上限写成 weak 116 / unverifiable 93 后，本相新写的记录先把
  weak 顶到 **117**、收到 109 之后又两次被自己的新文档顶回 **113** 与 **110**，每次都让门变红；
  三次都靠把引用改成"符号紧贴自己的 `path:line`"或补臂标签解决，**上限从未上调**。
  收口时实测 **weak 109 / unverifiable 91**，
  臂段归属核对从 34 条升到 **43** 条。窗口 ±25 全程未放宽——所有新红都来自规则。
- **四条控制互不相同**（`docs/evidence/phase56/controls.txt`）：L1 标签指向别的臂段 → 红，消息同时给出
  标签主人与行主人；L2 同标签指到自己段首 → 绿且两计数不动（没有 L2 就分不清"任何标签都指控"与
  "归属错才指控"）；L3 符号在上一行 → weak +1 并被上限压红；L4 整段无符号 → unverifiable +1 并被上限压红。
  临时注入文件写在 `finally` 里删除：留一条引用会改变全套报告的数字。
- **仍然打开的债（如实）**：weak + unverifiable = **200 条**引用今天仍没被核过内容；上限只保证不再增长。
  逐条偿清要靠给引用配邻接符号或臂标签——这是有意留下的**可见**债务，不再是一片沉默。
- 另记 **E58**：第四次把代码文本经非 raw 的 Python 字符串搬运（`
` 被吞成真换行 → `SyntaxError`），
  这次的差别是"写完立刻运行"在一个回合内拦下（E52 同族）。
- 成本：门 #3 0.13 s，`ctest` **7/7**。证据：`docs/evidence/phase56/`（README + `controls.txt` + `gate-output.txt`）。
## 附录 v45（2026-10-29，Phase 57：次序之外还有形状——`[R20-nonhip-expr]`）

- **为什么要第二条臂**：硬约束第 3 条说的是累加次序**与算式形状**任一变动，`max_rel` 就不再恒 0。
  HIP 侧 `[R20]` 按字节钉整段嵌套，两者一起覆盖；Phase 55 的 `[R20-nonhip]` 只钉次序，
  于是 `X[((ho + kh) * W + (wo + kw)) * C + c]` 改成 `X[((kh + ho) * W + (kw + wo)) * C + c]`
  这种"次序不变、形状变了"的改动只有字节腿会红，而字节腿从不说是哪条轴。
- **新臂 `[R20-nonhip-expr]`**（`tests/acceptance.cpp:2910`）：进程内生成，2 后端 × 3 算子 = 6 个 pin，每 pin 是
  **逐字节多行**片段（conv 的 xv/wv/FMA、reduce 的 pass-1 + pass-2、gemm 的 As/Bs/内积），
  按**次数 = 1** 核（E21/E24）、先打印判决再断言、臂自己数打印行数（E38）。片段全部 grep 现取。
- **成对控制**（`docs/evidence/phase57/controls.txt`）：E1 只在 `ocl_conv_kernel` 内改索引式 →
  次序臂 6/6 全 ok、表达式臂点名 `ocl/conv`（`exact == 6` 归属 `R20-nonhip-expr`）、门 #7 绿；
  E3 反转同一函数里的 `c` 循环 → 次序臂红、表达式臂排在后面沉默；
  E2 只改注释 → 两臂全绿、红归 `R5.4-cl`（证明新臂不是字节指纹）；E4 中性全绿。
  **两根轴要靠两条控制才分得开**——这是设计，不是巧合。
- **上一相仪器的第一次上岗**：新臂把两条字节腿推后 89 行，`ctest` 当场报门 #3 失败并点名 13 条引用
  （其中 5 条来自臂标签归属规则），全部 grep 现取改指；`weak 107 / unverifiable 91` 两条上限**一格未调**。
- **棘轮对新记录又响两次**（E59）：本相 README 与结果段里各有一条 `path:line` 所在行没有可核符号，
  unverifiable 91→92 直接压红。修法是写引用时把符号或臂标签放在**同一物理行**
  （`[R20-nonhip-expr]`（`tests/acceptance.cpp:2910`））。⇒ 写文档与写代码同律：**改完记录立刻跑门 #3**。
- 自己的期望又错一次（E51 家族第 4 次）：预登记写"E1 时次序臂一行都不打印"，实际次序臂先跑且干净，
  证据反而更强。
- 边界：钉的是发射文本的形状，不是等价性；等价改写同样会红，红不解释等价，等价只能真机重采 `max_rel`。
- 成本：门 #2 2.4–3.0 s；`ctest` **7/7**，守卫 byte-identical，三条字节基线未动。

## 附录 v46（2026-10-29，Phase 58：一条组引用的每个数字都被核——门 #3 的第二个静默段）

- **本相数出来的洞不是"没符号"，而是"只看第一个数"**：`ANCHOR` 原先只匹配 `路径:行号(-行号)?`，
  而本仓把多个行号写在同一个路径后面的**组引用**共 15 处、**40 个数字里 30 个从来没被核过**。
  `expand_citations()` 拆成逐数核对后，读数是（记录全部写完之后取）：行声明 310→**363**、尾巴
  0→**31**、臂归属核 90→**99**。
  臂归属核不涨就等于"拆了个寂寞"——第一版正是这样（尾巴拿到空 ctx），当场把 weak 顶到 89。
- **两个仪器错法都由本相自己犯、自己修、各自有前后读数**：
  ①尾巴共享组上下文之前的空 ctx（`arm checks` 90 不动就是信号）；
  ②符号是在**切出来的子串**里配的，而子串常常从上一段 span 的收尾反引号开始 ⇒
  一行里第二个及以后引用的符号全部隐形。改为整行配对 + 按位置过滤，`weak` 一次从 83 掉到 **61**
  （22 条被误计为盲的引用复活），并**当场指出一条文档引用了代码里从不存在的符号名**——
  那条属于 R6.3 的 P4 不变量断言本身是真的，位置也是真的：拒绝在 `r.verified`（`src/main.cpp:540`）。
- **收紧一条腿 = 重读它照到的所有句子**：重读债目抓出四条假引用（三后端 `initA` 四处行号全错、
  dtype 允许表指到 `op` 那行、一条不存在的符号名、四行"当时位置"被写成活引用），全部 grep 现取改指。
  这是 E21/E24 的**对偶**：多项目只验一项会放过"错的那几项"，而**从不存在的名字**被引用 20+ 个相
  也能一直绿——只要那两个盲区叠在一起。
- **控制**：`probe/phase58_groupcitation_control.py` 六条（G1 尾巴越界必须点名、G2 组标签必须对组内
  每个数都成立、G3 同臂两组绿且 debt delta=0、G4 一行两组不互相开脱、G5 一数一引用同样红、
  G6 落在上一段收尾反引号之后的符号仍须被读到且**报文点名**）。转录在
  `docs/evidence/phase58/group-controls.txt`。
- **仪器自身失效一次**（E61）：第 56 相的控制脚本写死了臂首行号，Phase 57 插入新臂后它注入了
  一条真正错位的引用、报"门没行为正确"——现改为每次运行从臂表现取。
- **占位符不算符号**：本相记录里写 `path:NNN` 描述规则本身时被当成符号、指控了五条真引用，
  故 `code_tokens` 增加大小写敏感的 `小写:全大写` 排除，并配三双向 self-test
  （占位符必须被拒 / `initA` 与 `std::cout` 必须留下）。
- **棘轮只降不升**：`WEAK_CAP` 104→**61**、`UNVERIFIABLE_CAP` 91→**59**；剩余 120 条计数债全量列在
  `docs/evidence/phase58/debt-list.txt`（48 条"那一行没有可当符号的代码标识符"、59 条"这句没提到那个
  符号"、3 条围栏内转录按"转录不改写"跳过、其余已可核/歧义）。本相证据与记录各顶红一次（E62：
  描述"过去错在哪"的句子必须不构成活引用），改的是写法不是阈值。
- 成本：门 #3 0.15–0.2 s；`ctest` **7/7**，三条字节基线与 `[R20-nonhip-expr]` 的六个片段一字未动。

## 附录 v47（2026-10-29，Phase 59：逐条还计数债——20+ 个错行号浮出，门 #3 学会读引用之后的主语）

- **本相是上一相棘轮的下游**：Phase 58 把"没人看"变成 120 行清单，本相逐条读它。分类后最大的意外是
  **多数 dead 债不是缺符号，而是行号漂了**：`:887` 8 处（今天 `hipMemcpyAsync` 那句在 946）、
  `:840` 3 处（`Differential` 在 883）、`:587` 4 处（`max_rel` 那句在 610）、`:739` 2 处
  （`FREEZES` 在 762）、`:805`/`:562`/`:530` 一组（`cpu_reduce_ref` 在 807、`cpu_reduce_blocked`
  在 660/593）。这些句子在整个 Phase 34–57 期间"全绿"，因为它们的引用从来没有邻接可核符号。
  ⇒ **计数债不是装饰**：它把"看不见"变成一个可以逐条关闭的清单。
- **反例同样重要**：`plan_reduce_width`（`src/runner_metal.mm:136`）的 136 一直是对的，只是句子没主语。
  "行号错"与"没主语"只能逐条读出来分辨，抽查必然混 ⇒ 修法的分类学（重指 / 补主语 / 降级为文本 / 留着）。
- **仪器新增有界尾随归因**：文档也写「引用在前、主语在后」。旧规则完全不读引用之后的文本（曾经因此把
  下一小句的符号算到前一条账上）。现在读，但硬截断：子句分隔符 `、,;；。）|`、下一条引用、40 字。
  控制五条（H1–H5）各自证明一次，尤其 H2 要求**报文点名**那个尾随符号——否则"变红"分不清是窗口违规
  还是 cap 被顶。转录 `docs/evidence/phase59/trailing-controls.txt`。
- **堵住"能变绿≠在断言"这个洞**：修债的工具提议把 `double` 插到引用前；`double` 在本仓每个 C++ 文件
  都有，那条引用会从此永久绿而什么都不核（E64）。`code_tokens` 现在拒绝 STOPWORD，`self_test()` 双向：
  占位符 `path:NNN` 拒、`initA`/`std::cout` 留、`double`/`static`/`return` 拒。
- **自动化边界（人审真的改变了结果）**：只改换行的 join（`phase59_debt_rewrap.py`，7 处，一字不改，
  拒绝表行/围栏/引用块/列表标记）可以自动；补符号（`phase59_debt_adjacency.py`）**必须 dry + 人审**——
  本相 8 条候选里 3 条不能落笔（停用词、从相邻表格行借词、主语错位），于是工具一条都没自动写盘，
  全部手工重写。
- **控制也会过期，且过期的是"对常数的假定"**：第 56 相的控制假定 cap==实测，债一降就误指控门（E63），
  现改为从门的汇总行现读上限；顺带抓出 `run_gate` 的分组下标错（打印成 unverifiable 的其实是 weak 的 cap）。
- **棘轮**：weak 61→**37**、unverifiable 59→**46**、债 120→**83**、被核行声明 329→**383**。
  本相证据 README 自己两次把"过去错在哪"写成活引用（E65，E62 家族第 5 次），改写法不动阈值。
  剩余 83 条约 80 条是"断言对象是注释文本"，再降需要重写句子（内容工作，不是仪器工作）。
- 成本：门 #3 0.2 s；`ctest` **7/7**，三条字节基线、`[R20-nonhip]` 与 `[R20-nonhip-expr]` 的片段未动。

## 附录 v48（2026-10-29，Phase 60：注释文本型债务 + 臂名里的 `/`——判据的词法必须等于文档的词法）

- **11 条 dead 债读完**：8 组是行号又漂、3 组是句子引用发射出去的注释文本。完整对照表在
  `docs/evidence/phase60/README.md`；此处只给四条今天的活引用，每条各自带主语：
  `uk_conv_plan`（`src/codegen_hip.cpp:435`）、`FREEZES`（`src/codegen_hip.cpp:762`）、
  `epsilon_rel`（`src/main.cpp:552`）、`parse_int_wide`（`src/kml_front.cpp:127`）。
- **四条的结论本身也已过期**（文件侧上界校验、Metal G6 交叉校验、D9 运行时宽度、CPU 参考第二档）：
  仪器能保证"号码指的是它说的那个东西"，**不能保证那句话仍然为真**。处理是历史降级为文本 +
  现状态另给活引用，而不是把旧句子改成今天看起来对。
- **判据词法 vs 文档词法**：`tests/acceptance.cpp` 有一条臂的标题就是 `R6.2/R6.4`（一条臂覆盖两条
  需求行），文档有 6 处用 `[R6.2/R6.4]` 引用它，而 `arm_labels()` 的字符类不含 `/` ⇒ 这 6 处被分类为
  "没有任何符号"、进了 `unverifiable`。放行之后 `arm-range checks` 99→**104**。
  ⇒ 与 E21/E24 同族但方向相反：不是断言太弱，是**判据读不到断言**。
- **控制也要被控制**：新增 L5/L6 两条，其中 L6 第一次注入写成没有方括号的样子 ⇒ "该红不红"，
  看起来像门不承重；补上括号才报出双主人。新控制第一次跑必须人读它报的**具体内容**（E38）。
- **上一相的不完整修法被本相抓到**：给重置行配的符号 `smem` 只有 4 个字母，过不了 `code_tokens`
  的 ≥5 规则，于是"改过"之后债一字未降。顺序固化：**先长度、后窗口**。
- **棘轮**：债 83→**69**、`weak` 37→**30**、`unverifiable` 46→**39**。本相证据文件自己顶红一次
  （同一行第二个引用没配符号，E62 家族第 6 次），改写法不动阈值。剩余 69 条里约 40 条要重写句子（内容工作）。
- 成本：门 #3 0.2 s；`ctest` **7/7**；三条字节基线与两个非 HIP 语义臂片段未动。

## 附录 v49（2026-10-29，Phase 61：三个判据洞让债 69→19，一条句子都没重写）

- **本相的产出不改文档而改门**：`anchors_check` 有两处"读不到文档实际写的形状"的洞——
  `TOKEN` 要求 span ≥2 字符（于是 `` `Y` `` 无法配对，同行后面每一对反引号错位，
  `hipMemcpyAsync` 这类主语凭空消失，9 条引用被计成盲）；`SKIP_TOKEN` 的 `^[A-Z]+\d` 在 `re.I` 下
  连真标识符 `fp16_rel` 一起吞（3 条）。第三个洞是我自己工具的插入位置，把定位名写进引用的
  开引号之内，破 span 20 处且**门不报红**（E68）。
- 两个判据修复各带**双向 self-test + 真实摘除控制**：还原 `{1,80}` 或给 `ARMISH` 加回 `re.I`，
  门都会报 `GATE IS MUTE OR LOOSE` 并点名案例。第一次的"验证"其实是临时副本 SyntaxError 造成的
  rc=1 ⇒ **控制生效的证据是报文，不是退出码**（E63 的读法再兑现一次）。
- 顺带重指 6 组过期行号（`r.verified` 在 `src/main.cpp:540`、`uk_ocl_log` 在 `src/runner_ocl.cpp:516`、
  codegen_hip 的 463、`dt_txt` 在 `src/kml_front.cpp:106`、`shard_rows` 在 `src/codegen_hip.cpp:1014`、
  `maxThreadsPerThreadgroup` 在 `src/runner_metal.mm:76`），显式列表落在
  `probe/phase61_stale_repoint.py`（一次 heredoc 重定向把 `cat` 喂进自己的输出、写出 100 MB 垃圾文件
  之后，一次性脚本一律入文件；E69）。
- **19 条是下界而不是"再努力一点"**：断言对象是发射文本/消息串/围栏转录/当时的英文状态；
  用 `std::endl` 之类的词去压计数器被明确拒绝（E64：能变绿≠在断言）。
- 读数：`weak` 30→**8**、`unverifiable` 39→**11**、被核行声明 420；Phase 59→61 连做 **120 → 19**。
- 成本：门 #3 0.2 s；`ctest` **7/7**；三条字节基线、`[R20-nonhip]` 与 `[R20-nonhip-expr]` 片段未动。
