# UNIKERN MVP 验收矩阵（R1-R10）— Phase 8

- 日期：2026-10-27（独立复核修订版 v2：修复三项测试覆盖缺口，证据产物入库；**v3 收尾审计：修复 ε 实算缺陷并重采真机证据，见文末补遗**）
- 规格：`.monkeycode/specs/unikern-mvp/requirements.md` v1.1（R1-R10）
- 构建环境：Windows 11 + Git Bash；CMake 4.4.0 + Ninja；MinGW g++ 13.2.0（C++20，`-static` 链接，E2 修复）
- 构建类型：**Release**（`build/CMakeCache.txt:25 CMAKE_BUILD_TYPE:STRING=Release`；编译旗标 `build/build.ninja:278 FLAGS = -O3 -DNDEBUG -std=c++20 -Wall -Wextra`，`CONFIG = Release`）——MinGW/GNU 的 Release 默认即 `-O3 -DNDEBUG`；E5 前提（Release 会剥除断言）在此配置下被真实行使，测试文件 `#undef NDEBUG` 保断言存活，ctest 2/2 绿
- 真机：**AMD Radeon RX 6600M（gfx1032，8 GB，14 CU）**，ROCm 7.1（`HIP_PATH=D:\Program Files\AMD\ROCm\7.1\`）
- 代码基线：commit `c7339d4`（Phase 8 首版）+ 独立复核修复（tests 三处补测 + 本报告修订 + 证据入库）+ 收尾审计修复（`src/main.cpp` LEDGER COMMIT 的 ε 改为 |C − T| 实算）
- 验收测试：`tests/acceptance.cpp`（`#undef NDEBUG`，E5）→ ctest 目标 `acceptance`；R6/R8 真机项用真实 `unikern run` 命令收集，**不在单测中伪造**
- 证据产物：**本目录引用的命令输出已作为文本转录入库** `docs/evidence/phase8/`（run/solve/ctest/acceptance/backends 转录 + 三份生成源 + 账本行转储），fresh checkout 可直接比对，也可按转录内命令原样复跑

## 状态图例

| 标记 | 含义 |
|------|------|
| ✅ 本机已验 | 本 session 用真实命令执行并通过（命令与输出摘录见下） |
| 🟡 部分已验 | CPU 侧已验，存在明确缺口（缺口如实列出） |
| ⛔ 真机受阻 | 需要 Apple Silicon（Metal 工具链）或 Intel Arc B（ocloc / Level Zero）机器，超出本机能力；本机仅完成源生成与结构校验 |

## 汇总矩阵

| 需求 | 验收方式 | 关键证据 | 状态 |
|------|----------|----------|------|
| R1 KML 前端与原语树 | 单测：canonical_form 唯一性 + 前端类型名拒绝 | `ctest` acceptance `[R1]`；结构等价树同串、异树异串 | 🟡（R1.2 结构校验缺口，见下） |
| R2 多面体可行域 F | 单测：独立复算 5×5 网格过滤期望 | `[R2] filter 22/25, violation records + \|X\| OK` | ✅ |
| R3 确定性代价 C | 单测：10⁴ 次同线程逐位一致 + 常量采纳 | `[R3] C bitwise-stable over 10^4 repeats` | ✅（版本号机制为 stub，见下） |
| R4 唯一最优解 | 单测：等价类字典序裁决 + 不可行终止 | `[R4] argmin (128,128) via canonical key` | ✅ |
| R5 三厂商后端生成 | 单测：三源非空 + 头部 HCS 指纹 + 纯函数 + 跨厂商 API 隔离；CLI `--emit` 行数 | `EMIT lines=390/61/53`，头含 `fp=8039464947ce` | 🟡（AMD 构件 ✅；MSL/OpenCL 构件 ⛔ 真机） |
| R6 差分验证 | 单测：容差函数 verify_fp32 五路径；真机：生成程序自带 CPU 对拍（2⁻⁵）+ CLI run | `[R6] fp32 graded tolerance ... OK`；`verified=PASS max_rel=0`（1024³ 与 32768³ 两档） | 🟡（AMD ✅；Apple/Intel ⛔ 真机） |
| R7 解账本 SQLite | 单测：往返/命中含 tile/事务崩溃恢复；CLI 二次 run | `LEDGER HIT ... (from ledger)`；`[R7] crash mid-transaction ... OK` | ✅ |
| R8 多级存储与流式 | 单测：场景 A/B 放置记录内容断言；真机 streamed | `[R8] scenario A/B placement records explicit + distinct OK`；`mode=streamed shards=64 prefetch_d=2`，e2e 40.5 s | 🟡（AMD ✅；Apple 真机 ⛔） |
| R9 HCS 导入 | 单测：必填缺失全量报告 + 无误报 + 指纹 | `[R9] complete missing-field rejection + fingerprint OK` | ✅ |
| R10 UCM 能力覆盖 | 单测：三厂商映射 + availability 语义 + 跨厂商 API 词汇隔离；CLI `backends` | `[R10] ... vendor-API isolation OK`；`amd yes / apple no / intel no`（诚实探测） | 🟡（语义 ✅；Intel 真机 ⛔） |

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

**状态：🟡 部分已验**。R1.3 唯一性本机已验。R1.2 的缺口如实记录：MVP 的 KML 树为程序化构造（CLI demo tree，`src/main.cpp:89`），**没有 KML 文本解析器**，因此"结构不完整拒绝"没有用户可提交的入口；MVP 落地的是前端类型名拒绝（非法 op/dtype 名 → nullopt），结构化校验器随 KML 文本前端（V1）引入。design.md「Error Handling: KML 非法 → 前端解析」在该范围内成立。

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

**状态：✅ 本机已验（附如实注记）**。R3.2 完整通过。R3.3 的"采用新常量"已验；"常量版本号"字段存在且随解报告（生成产物头部也带 `constants_v=1`），但**版本号自增机制是 stub**（`src/solver.cpp:99` 恒为 1），HCS 无版本演进通道——版本号递增留待校准流水线（R7.3 常量回写）接入时补。

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

**状态：🟡 部分已验**。R5.1（独立生成 + 仅 KML/HCS 输入）与 R5.3 的 HCS 指纹本机已验；指纹注记：生成头部记录 HCS 指纹 + `constants_v` 常量版本指纹，**HCS 的 `toolchain` 字段本身未进入产物头**（见 findings 缺口表）。R5.2 分后端：AMD 完整可运行程序已在本机真编译真执行（见 R6/R7 证据）；MSL / OpenCL C 为库形态内核源，**编译执行需用户 Mac / Intel 机器，⛔ 真机受阻**（产物自带 R8 honesty 注释说明此边界，`src/codegen_other.cpp:37-39`）。

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

**状态：🟡 部分已验**。R6.2 容差策略本体（`verify_fp32`）已由单测覆盖（2026-10-27 复核补测——首版报告误称"由 smoke 覆盖"，实为零覆盖，已修复）；AMD 真机对拍两尺寸全 PASS、失败不入账本的路径由 CLI 代码保证（负测护栏见 E12：错误架构码对象 → `UNIKERN_ERROR` 退 4）。**Apple / Intel 后端的对拍 ⛔ 真机受阻**（无 Metal / Intel 工具链）。

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

**状态：🟡 部分已验**。AMD 场景 B 显式流式 + 双缓冲本机真机已验；R8.5 的"缺页迁移率超阈告警"在 MVP 中以显式路径为唯一处理方式落地（生成程序自选 resident|streamed），运行时页迁移监测器属真机长测项。**Apple 场景 A 真机执行 ⛔ 受阻**（需用户 Mac）。

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

**验收方式**：单测断言 `make_backends()` 恰好覆盖 {Amd, Apple, Intel} 三厂商；`available()` 语义与环境事实一致（AMD=hipcc 探测、Apple=编译期 `__APPLE__`、Intel=`OCL_ICD_VENDORS`）；**工具链缺席的后端仍然生成自身厂商语言源码**，且**跨厂商 API 词汇零泄漏**（Metal 源不含 `hip`、`__global__`；OpenCL 源不含 `metal_stdlib`、`#include <metal`、`[[buffer`——按 API 词汇检查，而非英文单词：诚实注释中的 "no Metal / Intel toolchain" 措辞合法保留），能力缺口以 availability 标注与 F 约束呈现，绝不静默替换为其他后端。CLI `backends` 表格同理。

**证据**（单测段 + CLI；转录入库 `docs/evidence/phase8/acceptance_tests.txt`、`backends.txt`）：

```
[R10] vendor mapping + availability semantics + vendor-API isolation OK

$ ./build/unikern.exe backends
vendor    backend      available  note
amd       amd:hip     yes        source-gen ready (Phase 5 full)
apple     apple:metal     no        source-gen ready (Phase 5 full)
intel     intel:ocl     no        source-gen ready (Phase 5 full)
```

（本机事实：有 hipcc → amd yes；无 Metal 工具链 → apple no；无 Intel OpenCL ICD → intel no。与 findings.md Phase 5 探测一致。）

**状态：🟡 部分已验**。能力映射与不静默退化语义本机已验；**Intel 真机链路（ocloc/SPIR-V 构件、Level Zero 执行）⛔ 受阻**，需用户 Intel Arc B 机器。

## 缺口与如实记录（验收不掩盖项）

| # | 缺口 | 影响 | 去向 |
|---|------|------|------|
| G1 | KML 无文本解析器，R1.2 结构校验无用户入口 | R1.2 仅类型名拒绝落地 | KML 文本前端（V1） |
| G2 | `constants_version` 恒 1（stub） | R3.3 版本号字段在、递增机制缺 | R7.3 校准回写接入时补 |
| G3 | 生成产物头未含 HCS `toolchain` 字段 | R5.3 工具链版本指纹以 `constants_v` + HCS 指纹近似 | 产物头加 `toolchain=` 一行（低成本） |
| G4 | Apple / Intel 真机执行与对拍 | R5.2/R6/R8 的对应后端项 | 用户 Mac / Intel 机器执行本报告同款命令 |
| G5 | R8.5 运行时缺页迁移率监测器 | 仅显式路径 + 模式自选落地 | 真机长测（AMD 可做，未列入 MVP 单测） |

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

## 真机执行边界（诚实声明）

- 本 session 真机证据全部来自 **AMD RX 6600M（gfx1032）**：1024³ resident、32768³ streamed、账本命中回写，命令与输出均为实际执行结果。
- **Apple Silicon（Metal）与 Intel Arc B（ocloc / Level Zero）的真机执行超出本机能力**：本机无 macOS、无 Intel 工具链（findings.md Phase 5 探测），对应项只能做到源生成与结构校验，状态一律标 ⛔，不伪造。
- 上游 session 证据（Phase 6/7 记录）：真机闭环 PASS、账本命中 true、流式演示 mode=streamed；其 ε 数值当时打印的是容差常量（见审计补遗），本 session 已按实算口径重采。
