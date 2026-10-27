# UNIKERN MVP 验收矩阵（R1-R10）— Phase 8/9/10

- 日期：2026-10-27（v2 复核修订；v3 收尾审计 ε 实算；v4 校准闭环：R7.3/R3.3 实测→常量校准 + 版本号递增 + R5.3 toolchain 指纹，G2/G3 闭合；**v5 Phase 9：Intel 真机验证（G4 Intel 半闭合）——ocloc AOT + 进程内 OpenCL runner + 真机对拍/账本闭环/校准，Intel 行 ⛔→✅，Apple 维持受阻**；**v6 Phase 10：Apple 真机验证（G4 全闭合）——Metal 运行时 JIT（无需 Xcode）+ 进程内 ObjC++ runner + 真机对拍/账本闭环，Apple 行 ⛔→✅**；**v6.1：Phase 10 Apple 证据全量重采**，本文件 Apple 段的每个 ε/带宽数字均与 `docs/evidence/phase10/` 当前转录一致（此前引用的 `ε_rel=99.6471`、`5.18 GB/s` 来自未入库的一次性读数，已按重采读数替换；ε_rel 口径为比值 |C−T|/T，非百分数**）；**v7 Phase 11：G7 闭合——非 GEMM 树不再被静默当作 GEMM 执行/回写，改为 `op_support_gap()` 谓词 + 生成物自拒 + runner 入口自拒 + CLI 门三层显式拒绝，逐层"摘掉即红"（docs/evidence/phase11/）；R10 行据实改判 🟡，并新增 G6/G8 入册**）；**v8 Phase 12：KML 文本前端（R1.1 声明半边 + R1.2 完整违规清单，三算子按 arity 校验、layout 白名单、children 路径前缀）+ R10.3 每后端覆盖报告三轴 + G9 账本键按算子 arity 编码（gemm 键逐位不变）；R10 行回改 ✅、R1 行细化为"声明已验 / 执行半边仍缺"；G8/G9 入册并闭合**
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
| R1 KML 前端与原语树 | 单测：canonical_form 唯一性 + 前端类型名拒绝；v7 增 `--op` 声明位与 conv arity 拒绝；**v8 增 KML 文本前端（三算子声明、完整违规清单、arity、layout 白名单、children 路径）** | `ctest` acceptance `[R1]`；`[R1.1/2] KML text front end: three op families, round-trip, complete rejection list, arity, layout, children paths OK`；畸形声明 rc=2 且一次列全 4 条违规（`docs/evidence/phase12/phase12-kml-and-device.txt`） | 🟡（R1.1 声明半边 ✅、R1.2 ✅、R1.3 ✅；**conv/reduce 可用的内核生成仍缺**，执行仍被点名拒绝——即 G1 执行半边） |
| R2 多面体可行域 F | 单测：独立复算 5×5 网格过滤期望 | `[R2] filter 22/25, violation records + \|X\| OK` | ✅ |
| R3 确定性代价 C | 单测：10⁴ 次同线程逐位一致 + 常量采纳 + 版本随 HCS 透传 | `[R3] C bitwise-stable over 10^4 repeats`；`CALIBRATED constants_version 1 -> 2` | ✅ |
| R4 唯一最优解 | 单测：等价类字典序裁决 + 不可行终止 | `[R4] argmin (128,128) via canonical key` | ✅ |
| R5 三厂商后端生成 | 单测：三源非空 + 头部 HCS 指纹与 toolchain + 纯函数 + 跨厂商 API 隔离；CLI `--emit` 行数 | `EMIT lines=390/61/53`，头含 `fp=8039464947ce toolchain=rocm-7.13`；**Intel 构件 uk_intel.cl 经 ocloc AOT 编译（bmg-g21，产出 .bin + .spv）**；**Apple 构件 uk_apple.metal 经 Metal 运行时 JIT 真编译真执行（newLibraryWithSource，见 R6）** | ✅（三后端生成已验；AMD 真编译真执行、Intel ocloc AOT + 进程内执行、Apple JIT 真编译真执行） |
| R6 差分验证 | 单测：容差函数 verify_fp32 五路径；真机：生成程序自带 CPU 对拍（2⁻⁵）+ CLI run | `[R6] fp32 graded tolerance ... OK`；`verified=PASS max_rel=0`（1024³ 两档、AMD 与 Intel B390 均实测 PASS，max_rel=0 位级一致；**Apple M4 实测 PASS max_rel=0，256³ 对拍位级一致**） | ✅（AMD/Intel/Apple 三真机均 PASS） |
| R7 解账本 SQLite | 单测：往返/命中含 tile/事务崩溃恢复/实测与 kernel_ms 持久化；CLI 校准闭环；R7.3 场景 A 校准门控四臂；**v8 增 R7.1/G9 键身份臂（op_signature 与 canonical_form 按算子 arity 编码）** | `LEDGER HIT ...`；`[R7] crash ...`；`[CAL] ...`；`CALIBRATED link_bw 32 -> 5.429`；`[R7.3] scenario-A calibrate gate: ... OK`（摘门控即红，`docs/evidence/phase10/phase10-gate-arm-probe.txt`）；`[R7.1] op_signature encodes the op's full arity ... OK`（gemm 键逐位不变、conv 三形状互异） | ✅ |
| R8 多级存储与流式 | 单测：场景 A/B 放置记录内容断言；真机 streamed | `[R8] scenario A/B placement records explicit + distinct OK`；`mode=streamed shards=64 prefetch_d=2`，e2e 40.5 s（AMD）；**Apple M4 场景 A 真机 resident 执行 PASS（零拷贝语义，placement=unified_memory 单测已验）** | 🟡（AMD 流式 ✅；Apple 场景 A resident ✅；**Intel/Apple resident-only，流式不实现**——生成程序横幅声明此边界，payload > 0.85×上限时 runner 如实 `UNIKERN_ERROR what=stream_refused` 拒绝（R10 无静默降级），如实标注） |
| R9 HCS 导入 | 单测：必填缺失全量报告 + 无误报 + 指纹 | `[R9] complete missing-field rejection + fingerprint OK` | ✅ |
| R10 UCM 能力覆盖 | 单测：三厂商映射 + availability 语义 + 跨厂商 API 词汇隔离；CLI `backends`；v7 增 op 能力面三层拒绝臂；**v8 增 R10.3 覆盖报告三轴（库 + CLI 臂，execution 轴写死即红）** | `[R10] ... vendor-API isolation OK`；`[R10.2] non-gemm ops refused ... OK`；`[R10.3] coverage report: three backends ... OK`；`COVER amd:hip primitive=covered execution=resident+streamed toolchain=absent` 等三行（`docs/evidence/phase12/`） | ✅（R10.1/R10.2/R10.3 均有实现与回归位；**口径注意**：R10.2 的"纳入 F 约束"现表现为生成/执行拒绝 + 报告点名，solve 仍是 (hcs, tree) 纯函数、不知后端，见 Phase 12 补遗） |

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

**状态：🟡 部分已验（v8 细化）**。R1.3 唯一性本机已验。R1.2 缺口在 v8 闭合：KML 文本前端落地（`kml_import`/`kml_export`），非法声明一次列全所有违规（未知/拼写错误字段、op 与 dtype 名、dims 个数与非整数与非正数、layout 不支持、子节点带路径前缀），拒绝即 rc=2 且不进求解。仍保留 🟡 的原因是 **R1.1 的执行半边**：conv/reduce 可声明、可求解、可进键，但还没有可用的内核生成，执行由 Phase 11 的三层拒绝点名拦着（`solve --op reduce` 打印 model-only 提示）。v8 也说清 layout 口径：只接受 "NT"（三后端 emitted kernel 实际按 A[M][K]/B[K][N]/C[M][N] 行主序索引的标签），其它值在**前端**就拒——不是收下后按行主序默默执行，那正是 G7 的同类静默。

### R2 — 多面体可行域判定（F）

**要求**：R2.2 可行候选入 X；R2.3 不可行候选输出约束违反记录并淘汰；R2.4 报告 |X|。

**验收方式**：构造 vram=200 KiB 的 HCS，使文档化的 5×5 tile 网格 {16,32,64,128,256}²（`src/solver.cpp:16-17`）被谓词 `m*n*4B*3 ≤ cap`（`src/solver.cpp:42-57`）切成 22 可行 / 3 不可行；测试内**独立复算**该期望值（不复制 solver 状态），再断言 `feasible_count`、`worst` 违反记录（约束名 + `tile=/need=/cap=` 明细）。

**证据**：

```
[R2] filter 22/25, violation records + |X| OK
```

违反记录样例（首条，枚举序首个越界者）：`constraint="tile_working_set<=device_capacity"`，`detail` 含 `tile=128x256 need=393216B cap=204800B`。CLI 侧同口径：`solve` 输出 `|X|=25`（满容量）/ 不可行时 `INFEASIBLE` 退 3（`src/main.cpp:199-201`）。

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

**验收方式**：两个层面。**CPU 侧（单测）**：容差函数 `verify_fp32`（`src/runtime.cpp:95`，R6.2 策略本体）五条路径——逐元素相等 → ok 且 max_rel=0；1/1024 相对误差（< 2⁻⁵ 默认容差）→ ok 且报告 worst 误差；10% 相对误差 → 拒绝并点名 `index 3`；可配置更紧容差（1/4096）→ 拒绝默认通过者；尺寸不匹配 → 明确拒绝（"size mismatch"），绝不假通过。**真机侧**：生成的 AMD 程序自带确定性初始化 + CPU 参考实现 + 2⁻⁵ 分级对拍（`UNIKERN_RESULT ... verify=` 契约行）；CLI `run` 只在 `verified=PASS` 时 LEDGER COMMIT（`src/main.cpp:274-282`，P4 不变量）。真机执行两个尺寸：1024³（resident）与 32768³（streamed）。

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

**状态：🟡 部分已验**。AMD 场景 B 显式流式 + 双缓冲本机真机已验；R8.5 的"缺页迁移率超阈告警"在 MVP 中以显式路径为唯一处理方式落地（生成程序自选 resident|streamed），运行时页迁移监测器属真机长测项。**Apple 场景 A 真机 resident 执行已于 Phase 10（2026-10-27）验证：`mode=resident`、`verify=PASS max_rel=0`、placement 记录为 `unified_memory (scenario A, zero-copy)`（单测逐词断言 + 真机输出一致，证据 `docs/evidence/phase10/phase10-run.txt`）**——场景 A 零拷贝语义下无显式流式路径（缓冲区别名统一内存），payload > 0.85×unified_bytes 时 runner 如实 `UNIKERN_ERROR what=stream_refused` 拒绝，与 HIP/OCL runner 同构。**Intel 真机侧如实标注：resident 执行已验（Phase 9，verify=PASS max_rel=0、实测回写），显式流式不实现**——intel:ocl 后端生成源自带 R8 honesty 注释声明此边界，payload > 0.85×vram 时 runner 如实 `UNIKERN_ERROR what=stream_refused` 拒绝（R10 无静默降级）。

### R9 — HCS 导入（MVP 半自动）

**要求**：R9.2 校验必填字段；缺失时**列出全部**缺失字段并拒绝导入。

**验收方式**：单测 `hcs_import("{}")` 断言错误信息同时含 vendor、device、tiers[vram].bytes、tiers[device].bw_gbps、link_bw_gbps、flops_gflops、compute_units 全部七项；单缺 `flops_gflops` 的输入只报该字段（无误报）；完整导入成功且指纹随建模字段（tier 带宽）变化。

**证据**：

```
[R9] complete missing-field rejection + fingerprint OK
```

CLI 同口径：`HCS rejected: missing required fields: ...`（`src/main.cpp:110`）；`hcs` 子命令 `VALID amd/RDNA gfx103x ... fp=80394649`。

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

（Windows 本机事实：无 hipcc → amd no；`__APPLE__` 编译期判定 → apple no；Intel
OpenCL ICD 在位（System32 OpenCL/ze_loader + oneAPI 2026.1，HKLM Khronos Vendors
注册 intelocl64.dll）→ intel yes。**Apple 主机事实（Phase 10，见
`docs/evidence/phase10/phase10-environment.txt`）：apple=yes——`available()` 改为
运行时 `metal_available()`（`MTLCreateSystemDefaultDevice` 真实探测），非编译期
`__APPLE__` 判定；AMD/Intel 在该主机为 no。Intel 真机链路见 Phase 9 证据，Apple
真机链路见 Phase 10 证据。）

**状态：✅ 本机已验（v6 三真机全闭合）**。能力映射与不静默退化语义本机已验；
**Intel 真机链路已于 Phase 9（2026-10-27）全验**：ocloc AOT（bmg-g21 → .bin+.spv）、
进程内 OpenCL runner 对拍/计时/账本闭环/校准，见 `docs/evidence/phase9/`；
**Apple 真机链路已于 Phase 10（2026-10-27）全验**：Metal 运行时 JIT
（`newLibraryWithSource` 进程内编译，无需 Xcode/metal 编译器）、进程内 ObjC++
runner 对拍/计时/账本闭环（LEDGER COMMIT/HIT），见 `docs/evidence/phase10/`。

## 缺口与如实记录（验收不掩盖项）

| # | 缺口 | 影响 | 去向 |
|---|------|------|------|
| G1 | ~~KML 无文本解析器，R1.2 结构校验无用户入口~~ **声明半边已闭合（v8 / Phase 12）**：`kml_import`/`kml_export` + `--kml` + `kml` 子命令 | 三类算子可按 arity 声明、违规清单一次列全、layout 只接受已实现标签；执行半边仍开放 | conv/reduce **可用的内核生成**（下一批）；更复杂的原语树组合语法按 V1 |
| G2 | ~~`constants_version` 恒 1（stub）~~ **已闭合（v4）** | — | `calibrate` 递增版本号，实测 1→2 |
| G3 | ~~生成产物头未含 HCS `toolchain` 字段~~ **已闭合（v4）** | — | 产物头 `toolchain=rocm-7.13`（三后端一致） |
| G4 | ~~Apple 真机执行与对拍；Intel 真机执行与对拍~~ **已闭合（v6）** | ~~R5.2/R6/R8 的对应后端项~~ — | **Intel 半侧 2026-10-27 Phase 9 闭合**（ocloc AOT + 进程内 OpenCL runner 真机验证，见 `docs/evidence/phase9/`）；**Apple 半侧 2026-10-27 Phase 10 闭合**（Metal 运行时 JIT + 进程内 ObjC++ runner 真机验证，见 `docs/evidence/phase10/`）。G4 全闭合。 |
| G5 | R8.5 运行时缺页迁移率监测器 | 仅显式路径 + 模式自选落地 | 真机长测（AMD 可做，未列入 MVP 单测） |
| G6 | Apple resident 预算取 HCS 声明容量，未与 Metal `recommendedMaxWorkingSetSize` 交叉校验（`src/runner_metal.mm:110`） | "payload ≤ 0.85×容量"只对声明容量成立；本机 payload 3 MB 远低于两者，读数不受影响 | 需一次真机上限探测臂（Phase 10 收尾如实入册，未顺手实现） |
| G7 | ~~非 GEMM 树被静默当作 GEMM 执行并按 `reduce:...` 键回写账本~~ **已闭合（v7 / Phase 11）** | 修复前会污染 R7.3 校准输入；R10.2"绝不静默降级"被破 | `op_support_gap()` + 生成物自拒 + runner 入口自拒 + CLI 门，三层各有"摘掉即红"证明（`docs/evidence/phase11/`）；conv/reduce **可用的**内核生成仍属 G1 |
| G8 | ~~R10.3"输出每个解在各后端的原语覆盖情况"零实现~~ **已闭合（v8 / Phase 12）** | 修复前只有拒绝消息代偿，"不支持"不是可查产物 | `primitive_coverage()` + `COVER` 行进 solve/run/kml 报告；三轴（primitive / execution / toolchain）分别取自 `op_support_gap()`、`supports_streaming()`、`available()`，无第二张表可漂移；`[R10.3]` 库+CLI 臂守着，把 execution 轴写死即红（`docs/evidence/phase12/phase12-arm-probes.txt` 第 6 腿） |
| G9 | ~~`op_signature` 只编进 dims[0..2]，conv 两种形状共用一个账本键~~ **已闭合（v8 / Phase 12）** | 修复前不是活故障（conv 在落库前即被 G7 拒绝），但 G1 的 conv 声明一落地就会变活 | 签名与 canonical_form 改按 `kml_arity()` 取维；`[R7.1]` 臂断言 gemm 键逐位不变（已入库证据无需迁移）+ conv 三形状互异 + reduce 未用槽位不泄漏进键或哈希 |

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
  使用，`src/solver.cpp:75`），对 Apple 预测无影响——**这条有实测背书**：强制
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
