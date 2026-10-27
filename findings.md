# Findings — UNIKERN MVP

## 硬件与厂商事实（用户现役设备 = 验收矩阵）
- AMD 离散卡（RDNA）→ 场景 B：HIP SDK（Windows 版存在）；编译入口 comgr / hipcc
- Apple Silicon（M 系列）→ 场景 A：统一内存零拷贝；Metal 工具链仅 macOS，本机只能做源生成 stub
- Intel Arc B 系列（用户称 B390）→ 场景 B：Level Zero loader 随驱动分发；编译入口 ocloc（SPIR-V）
- 无 NVIDIA：CPU 参考实现为对拍基准（标量 + 分块两档）；GPUDirect Storage 通路属 V1

## 已确认技术口径（2026-10-26 评审）
- 决策 1 接受：多面体 = 结构化枚举 + 谓词过滤；违反约束清单作证书降级形式
- 决策 2 接受：后端生成到厂商源语言；ISA 直出留 V1+
- 决策 3 接受：C++20

## 规格要点（防遗忘）
- R4.2 确定性：solve 纯函数，同线程逐字节同解；P1 验证 = 重复求解输出哈希一致
- R6.2 容差：整型/索引位级一致；FP32 ≤ 2^-5 ULP、FP16 ≤ 2^-4（可配置）
- R7 Ledger：键 = (算子签名, HCS 指纹, 原语树哈希)；SQLite 事务；命中跳过枚举/可行域/代价认证
- R8 场景 B：显式流式唯一路径；缺页迁移率超阈告警并重求解；禁隐式超订
- R10：后端不支持的能力区间 → 纳入 F 约束，绝不静默降级

## 环境事实（待 Phase 1 填充）
- （待探测：cmake / g++ / clang / MSVC / SQLite / HIP / Level Zero 可用性）

## 环境事实（Phase 1 探测结果）
- 构建：CMake 4.4.0 + Ninja（Meson 附带）+ MinGW g++ 13.2.0（C++20）✓；MSVC 2022 BuildTools 备选（d:\Program Files (x86)\...\2022\BuildTools）
- AMD：HIP SDK 存在（TheRock gfx103X 7.13.0，路径 D:/Users/Administrator/.cache/lemonade/bin/therock）→ 本机可真编译 HIP
- Apple：Metal 工具链 macOS 限定 → 本机仅源生成 stub（__APPLE__ 分支）
- Intel：System32 无 ze_loader.dll → 运行时未装或非标准路径；Phase 5 探测 ocloc（随显卡驱动）
- SQLite：无 CLI、mingw64 无头文件 → Phase 2 账本用 JSON-lines stub，Phase 6 引入 amalgamation（勿静默安装，需请示）

## Phase 2 构建与运行事实
- 构建命令：`cmake -G Ninja -B build && cmake --build build`（MinGW g++ 13.2 + Ninja）
- **必须静态链接**：本机存在两套 MinGW 运行时（C:\ProgramData\mingw64 与 Git /mingw64），
  动态链接会加载两份 libwinpthread-1.dll，导致 -O2 以上 ifstream 路径段错误
- 冒烟测试：场景 A/B 求解确定性、canonical 稳定性、HCS 拒绝路径、Ledger 往返 + 键隔离
- CLI 已验证：solve（miss→solve→hit）/ hcs / backends / ledger
- 实测数值：AMD 8GB 离散（场景 B）x*=128x128 c=12.54ms |X|=9；
  Apple 统一内存（场景 A）x*=128x128 c=5.29ms（无拷贝项，符合场景 A 零拷贝预期）

## Phase 5 真机探测（2026-10-27）
- **AMD 真机可用**：hipcc 在 `D:\Program Files\AMD\ROCm\7.1\bin\hipcc.exe`（HIP_PATH 已设），
  设备 = **AMD Radeon RX 6600M, gfx1032, 8176 MB, 14 CU**；`hipGetDeviceCount` 返回 1
- OpenCL：仅有 "AMD Accelerated Parallel Processing" 平台；**无 Intel OpenCL 平台**
- Intel：**无 ocloc.exe**，无 `HKLM\SOFTWARE\Khronos\OpenCL\Vendors` 条目 → Intel Arc B390 应在另一台机器
  （**2026-10-27 更正**：该结论已被 Phase 9 环境事实推翻——工具链实际在位，见文末 Phase 9 节；
  本行保留作为当时探测记录，不改写历史）
- 结论：本机只能真编译+真执行 AMD；Apple(MSL)/Intel(OpenCL) 走源生成 + 结构校验，
  真机验证需在用户 Mac / Intel 机器上进行（诚实边界）
- 教训复用：单行 shell 里 `\n` 会被折叠 → 一律 heredoc 写文件（E1）

## Phase 5 产出与边界（2026-10-27）
- AMD 代码生成 = **完整可运行程序**（不只内核）：确定性初始化 + CPU 参考实现 +
  相对误差 2^-5 对拍 + hipEvent 计时 + `UNIKERN_RESULT` 单行结果契约
  → 一个产物同时满足 R5（生成）、R6（对拍）、R7.3（实测回写）所需数据
- 验证尺寸 256³（CPU 参考可承受）；计时尺寸 1024³（上限，按 tree.dims 收缩）
- 块 tile 钳制：solved tile → [32,96] 且 16 倍数（RPT×RPN ≤ 36 累加器）；
  钳制时源码注释 + 输出报告均标注，不静默改变解
- MSL / OpenCL C：与 HIP 同构的寄存器分块结构，结构对齐但**本机无法编译验证**
  → 真机验证需用户 Mac（Metal）与 Intel 机器（ocloc/Level Zero）
- 传输层事实（影响一切后续编辑）：
  - 单行 shell 中反斜杠会被折叠；heredoc 中 `\`→`\` 不可靠
  - 超长 heredoc 会被截断（约 150 行处）
  → 规则（2026-10-27 更新）：**Write/Edit 文件工具不经 shell 传输，反斜杠与长文件都安全**
    → 源码编辑一律用 Write/Edit；Bash 仅用于构建/运行；源码路径字符串仍约定前斜杠

## Phase 5 HIP 真机闭环事实（2026-10-27，E12）
- **hipcc（ROCm 7.1 Windows）无 `--offload-arch` 时默认 gfx906**，本机 gfx1032 上
  每次 launch 返回 error 200 "device kernel image is invalid"；hipDeviceSynchronize
  仍返回 success，hipMalloc 显存是驱动清零的 → 对拍 max_rel 恰为 1.0、计时 ~0，
  症状与"算错"完全一致，实际是"没跑"
- 本机探测架构的可用路径 = host-only 探针（无 `__global__` 函数 → 无码对象 → 默认
  target 无关）：编译运行打印 `gcnArchName`；`amdgpu-arch.exe`/`hipInfo.exe`/
  `--offload-arch=native` 在本机全部失效
- 生成程序的 launch 检查必须用 `hipGetLastError()`：本平台 launch 表达式返回 void，
  不能赋给 hipError_t
- 修复后闭环：1024³ GEMM 实测 0.554 ms（≈3.9 TFLOP/s FP32），max_rel=0（确定性
  初始化全是 2^-4 倍数，FP32 部分和全部精确可表示，对拍位级一致）
- 负测护栏：错误架构码对象 → `UNIKERN_ERROR what=launch verify` 退 4，
  不再产出貌似合理的 UNIKERN_RESULT

## Phase 8 验收事实（2026-10-27）
- 验收面：`tests/acceptance.cpp`（#undef NDEBUG，ctest 目标 `acceptance`）+
  `docs/acceptance-mvp.md` 矩阵；`ctest --test-dir build --output-on-failure`
  **2/2 全绿**（smoke + acceptance；构建配置 Release：`FLAGS = -O3 -DNDEBUG
  -std=c++20`，MinGW/GNU Release 默认 -O3 非 -O2，E5 修复有效——NDEBUG 在场
  且 #undef NDEBUG 保断言存活）
- R1 诚实边界：MVP 无 KML 文本解析器（树为程序化构造），R1.2 结构校验
  无用户入口，落地面 = 前端类型名拒绝（`op_from_string`/`dtype_from_string`
  nullopt）；canonical_form 唯一性四种性质全过
- R3 诚实注记：10⁴ 次逐位确定 ✓、HCS 常量采纳 ✓（统一内存带宽减半 →
  归约 C 恰翻倍）；`constants_version` 恒 1（stub，`solver.cpp:99`），
  递增机制待校准回写接入
- R4 平局裁决可观测：面积 ≥ 128×128 的 tile 占用惩罚恒 1.0 → 等 C 平局组
  6 个候选，枚举序先遇 (64,256)，规范键 "m=128,n=128" 字典序最小 →
  solve 返回 (128,128) 证明选择由 (C, 键) 驱动而非枚举序（P2 实证）
- R5 指纹注记：三源头部均带 `fp=<hcs_fingerprint 前 12 hex>` + constants_v；
  HCS 的 `toolchain` 字段未进产物头（缺口 G3，低成本补齐项）
- R6/R8 真机复测（本 session，RX 6600M）：1024³ resident verify=PASS
  max_rel=0 measured_ms=2.691（kernel 0.564）；32768³ streamed
  mode=streamed shards=64 prefetch_d=2，**e2e 30.8 s < 串行内核参考
  48.5 s → 双缓冲重叠生效的直接观测**；verify=PASS
- 账本命中口径：二次 run `LEDGER HIT c_cached_ms=0.490829 tile=128x128
  (from ledger)`；单测用离格 tile (48,80) 证明命中返回记录解（R7.2），
  measured_ms 直查 SQL 证明持久化（R7.3），crash-tx 子进程 + marker 文件
  证明事务回滚（R7.4）
- 测试编写教训（新增防重复）：canonical_form 深树等价断言必须构造**相同
  子树多重集**——首版误用 kid1/kid2 两个不同子树致断言失败（测试代码
  缺陷，非产品缺陷），改 `{kid1,c1}` vs `{c2,kid1}`（c1≡c2）后通过
- Apple / Intel 真机执行仍超出本机能力（无 macOS、无 Intel 工具链），
  验收矩阵逐项标注 ⛔ 受阻，不伪造
  （**2026-10-27 更正**：Intel 半侧不再受阻——Arc B390 + oneAPI/OpenCL 3.0 在位，
  Phase 9 进行中；Apple 半侧维持受阻）

## Phase 8 独立复核修复（2026-10-27，v2）
- 复核 5 项全数属实并处理：3 项补测试（可修）、1 项证据入库、1 项构建配置纠正
  - 首版报告「verify_fp32 由 smoke 覆盖」不实（grep tests/ 零命中）→
    acceptance.cpp 新增 R6 段：容差函数五路径（相等/容差内/超差点名/
    可配置收紧/尺寸不匹配）
  - 「单测断言场景 A 放置标记」不存在 → 新增 R8 段：场景 A 记录逐词断言
    （placement=unified_memory / scenario A / zero-copy / bw=400），
    场景 B 断言（vram+ram+nvme / pinned_copy / shard=64MiB / prefetch_d=2），
    两记录互异
  - 「Metal 源不含 HIP 词汇、OpenCL 源不含 Metal 词汇」测试没有且字面不实
    （uk_intel.cl:13 诚实注释含 "Metal"）→ R10 改为 API 词汇级负向断言
    （hip/__global__ 对 MSL；metal_stdlib/#include <metal/[[buffer 对 OCL），
    文档措辞同步改为「API 词汇隔离 + 诚实注释例外」
  - 证据产物未跟踪 → 重跑全部证据命令，原文转录入库 docs/evidence/phase8/
    （run×3 / solve_emit / ctest / acceptance / backends / ledger_dump /
    emit 三源；uk_amd.cpp 撞 .gitignore 的 uk_*.cpp 构建产物模式，
    以 git add -f 作为验收证据入库，README 说明）
  - build/ 的 CMAKE_BUILD_TYPE 为空（实为 -O0 无 NDEBUG 跑绿）→ 重配置
    `-DCMAKE_BUILD_TYPE=Release`，实测 `FLAGS = -O3 -DNDEBUG -std=c++20`
    （build/build.ninja:278）后 ctest 2/2 绿；注意 MinGW/GNU 的 Release
    默认是 -O3 -DNDEBUG 而非 -O2，此前的「Release -O2」口径一并纠正
- 复核后真机复测（Release 构建）：1024³ verify=PASS measured_ms=2.61237 →
  二次 run LEDGER HIT tile=128x128；32768³ streamed measured_ms=40517.8
  kernel_ms=63700.3（e2e < 串行参考 → 重叠生效）；verify=PASS
- 教训：验收报告的每一句「单测断言 X」必须能指到断言行；写作时逐条对照
  测试源码，引用的输出摘录必须与入库转录逐字一致

## 收尾审计事实（2026-10-27，v3）
- 独立复核漏网项：LEDGER COMMIT 的 epsilon_abs/epsilon_rel 是硬编码常量
  （1e-6 与容差 2⁻⁵），非 ε=|C−T| 实算；暴露特征 = **三组不同实测对应同一 ε 常量**。
  教训：复核"报告是否诚实"时要专门核对「数值是否随输入变化」——常量冒充计算值
  是自动化复核的盲区（复核员验证了"数字来自真实命令输出"，但命令本身输出的是常量）
- 修复后实测（RX 6600M）：1024³ resident ε_abs=2.35ms（predicted 0.49 vs e2e 2.84ms）；
  32768³ streamed ε_abs=51.75s（predicted 3.60s vs e2e 55.35s）
- ε 大的口径解读：C(x) 拷贝项按 HCS 互联标称 32GB/s 计，实测看到真实 PCIe 有效带宽
  + 启动/同步开销 → ε 即 R7.3 校准信号，按实测报告不修饰；双缓冲不变式
  （streamed e2e < 串行 kernel_ms）三次复跑全部成立（40.5<63.7 / 22.9<27.6 / 55.4<71.4）
- 真机数据波动大（同负载 e2e 22.9s→40.5s→55.4s）：后台负载未受控的桌面环境，
  结论只依赖不变式与 PASS/FAIL，不依赖单点绝对值

## 校准闭环事实（v4，2026-10-27）
- 样例 HCS 互联标称 32 GB/s，RX 6600M 实测有效值 **5.43 GB/s**（12.58MB/
  2.32ms 拷贝段）——差 5.9 倍，正是校准前 ε_rel≈4.8 的全部来源
- 校准口径：eff_bw = compulsory_bytes / (measured_e2e − kernel_only)，
  多行取确定性中位数（偶数行取两中均值）；只吃 kernel_ms>0 的行
- 校准后 1024³：predicted 2.415ms vs measured 2.707ms → **ε_rel=0.12**
- 设计约束：solve 保持 (hcs,tree) 纯函数（P1）——账本状态只在 calibration
  （构造新 HCS 文件，constants_version+1）时进入系统；指纹不含
  toolchain/constants_version（同一硬件一个指纹）
- 工程教训复用：hcs_export 首版用默认 6 位精度写浮点 → 导出再导入值不等，
  被单测当场抓住（同 E4）→ 任何序列化浮点的出口一律 max_digits10

## Phase 9 Intel 环境事实（2026-10-27，推翻 Phase 5 探测结论）
- **设备**：Intel Arc B390 GPU OpenCL 3.0 NEO，驱动 32.0.101.9150（sycl-ls 可见）；
  平台标签含 "(integrated)" 字样——设备类型/显存以 OpenCL API 实测为准，不照抄标签
  （HCS 样例写 16GB 离散，与标签矛盾，证据里如实记录实测值）
- **加载器**：`C:\Windows\System32\OpenCL.dll`（Khronos ICD loader）+
  `ze_loader.dll`；HKLM\SOFTWARE\Khronos\OpenCL\Vendors 已注册 intelocl64.dll
- **工具链**：oneAPI 2025.3 + 2026.1 已安装——
  ocloc.exe ×2 版本（`C:\Program Files (x86)\Intel\oneAPI\ocloc\{2025.3,2026.1}\bin`）、
  CL 头在 `C:/Program Files (x86)/Intel/oneAPI/2026.1/include/CL/`、
  sycl-ls.exe（2026.1\bin）
- **CPU 平台**：Core Ultra X7 358H OpenCL 3.0（枚举时注意与 GPU 平台区分）
- **验证路径选定**：进程内 OpenCL runner（clBuildProgram 用驱动编译器即时编译生成的
  uk_intel.cl，跑 256³ 对拍 + 计时）+ ocloc AOT 编译（R5 真编译证据）；
  不走 hipcc 式子进程模式——OpenCL ICD 加载器直接链接进主程序更简单可靠
- 链接方式 sanity 未做：GNU ld 对 System32/OpenCL.dll 的直接链接待子项 a 验证
  （若失败，备选：LoadLibrary + GetProcAddress 动态解析符号）
- **与 AMD runner 的口径对齐**：warm 2 / timed 10（小）或 1/1（大）；e2e 用
  clEnqueueMarkerWithWaitList 包裹；kernel_ms 用内核参考事件；UNIKERN_RESULT 字段一致
- **流式不实现**（intel:ocl 后端源横幅已声明 resident-only）：payload > 0.85×vram
  时诚实拒绝并报告，R10 不静默降级
- 历史结论保留不改写：Phase 5"无 ocloc/无 Intel 平台"为当时探测记录，已被本节推翻

## Phase 9 Intel 真机验证闭合（2026-10-27，G4 Intel 半闭合）
- 构建工具链为 **MSVC 2022 Community**（vcvarsall + Ninja，OpenCL 经 oneAPI 2026.1 的
  OpenCL.lib 导入库链接 + third_party/CL 厂商头 18 个，UK_HAS_OCL 编译期门；
  MSVC 旗标 /W4 + /utf-8 + _CRT_SECURE_NO_WARNINGS；无 MinGW）
- R5 真编译：生成的 uk_intel.cl 经 ocloc AOT（`-device bmg-g21`）编译成功，产出
  uk_intel_bmg.bin + uk_intel_bmg.spv
- R6 真机对拍（Arc B390）：256³ 差分 `verify=PASS max_rel=0.000000e+00`（位级一致）
- R7.3 闭环：1024³ run → LEDGER COMMIT（ε_abs=1.31315，ε_rel=2.72046，ε 实算）
  → 同键 LEDGER HIT → calibrate（link 32→18.5877 GB/s，constants_version 1→2）
  → 校准后 run ε_rel=1.48813：**link 项已校准收敛，残留 ε 由 HCS flops 常数
  （24000 GFLOPS 预测峰值 vs B390 实测约 2 TFLOPS 有效吞吐）精度主导**——模型常数
  精度问题（L4 roofline 标定范围），非运行器/链路缺陷，如实报告不修饰
- 设备实测事实（OpenCL API，非标签）：Arc B390 OpenCL 3.0 NEO，驱动
  32.0.101.9150，**96 CU、全局显存 27207761920 B、2.5 GHz、host_unified=1、
  max_wi=1024/维**（HCS 样例的 16GB/32 CU 与平台标签 "(integrated)" 均不采信，
  已更新 samples/hcs_intel.json 为实测值）
- runner 与 AMD 口径对齐：256³ 对拍、树 dims 16 对齐下限 32、warm2/timed10、
  e2e=H2D+kernel+D2H（标记事件）、kernel_ms 参考、resident-only（payload > 0.85×vram
  诚实拒绝 UNIKERN_ERROR，R10 无静默降级）、网格超限诚实拒绝
- acceptance R10 镜像改为独立 Win32 枚举（LoadLibrary + GetProcAddress），与后端
  真探测在本机一致（踩坑：OpenCL 枚举函数指针必须带 3 参
  num_entries/platforms/num_platforms，且 windows.h 定义 near/small/big 宏）
- 遗留边界：Apple 真机（需用户 Mac）；Intel 流式双缓冲（不实现，诚实标注）
- 真机证据：docs/evidence/phase9/（环境/ocloc/solve_emit/verify/run/hit/calibrate/
  calibrated-run/backends/ctest/acceptance 转录 + emit/uk_intel.cl 与 aot 产物，
  -f 入库）；acceptance-mvp.md v5

## Phase 10 Apple 真机验证闭合（2026-10-27，G4 Apple 半闭合 → G4 全闭合）
- 运行环境：**Apple M4 / macOS 26.6.2 arm64**，Apple clang 21（CommandLineTools SDK）。
  Xcode.app 在位但**无 Xcode SDK**（`xcrun --sdk macosx --show-sdk-path` 落到
  `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`）——Metal/Foundation 均链接自 CLT SDK
- **无需 Xcode、无需 metal 编译器、无需 metal-cpp 头**：`MTLDevice newLibraryWithSource`
  即运行时 JIT 编译 MSL，`newFunctionWithName` + `newRenderPipelineState`/compute pipeline
  即可执行——Apple 后端因此从"仅源生成"升为"真编译真执行"
- 设备事实经 MTLDevice API 实测：`name=Apple M4`、`hasUnifiedMemory=1`、
  `maxThreadsPerThreadgroup.width=1024`（`src/runner_metal.mm:65-66`）；**本 SDK 无 CU 数量 API**，
  HCS 的 `compute_units=40` 是声明值，不冒充实测
- **边界（本轮如实标注，未扩范围去实现）**：runner 的 resident 预算取
  `hcs.tier_bytes("unified")`（`src/runner_metal.mm:110`，即 34359738368 B = 32 GiB 的 HCS 声明值），
  **没有**与 Metal 的 `recommendedMaxWorkingSetSize`（Metal 对单设备可驻留工作集的实际上限，
  通常低于物理容量）交叉校验。因此"payload ≤ 0.85×unified"是相对声明容量成立，
  不是相对 Metal 驻留上限成立——本机负载远小于该阈值，本次读数不受影响，但这是一条
  已知的口径缺口（记入 task_plan 的 G6，与"HCS 声明值 vs API 实测"这一类问题同根）
- 场景 A（统一内存）执行语义：`MTLResourceStorageModeShared` 零拷贝，无显式 H2D/D2H 路径，
  因此 e2e 计量的**不是**链路拷贝；`kernel_ms` 取 `GPUStartTime/GPUEndTime`，
  管线为 `newLibraryWithSource` → `newFunctionWithName` → `newComputePipelineStateWithFunction`
- **R7.3 校准在场景 A 语义不适用（新增诚实门控）**：`calibrate` 用
  `measured_ms − kernel_ms` 反推 `link_bw_gbps`，而该差值在零拷贝下是主机端队列提交/调度开销。
  重采两轮的"精确伪影"分别得 9.097 与 5.986 GB/s（同一份账本口径差 1.5 倍），
  正是它不是带宽测量的直接证据 → 默认 `CALIBRATE SKIP`（rc=3），
  仅 `--allow-unified-calibrate` 显式强制。**门控正确性有实测背书**：强制校准把
  link 600→9.097、version 1→2 后 `PREDICTED demo c_ms 5.28611 → 5.28611` 逐位不变
  （`src/solver.cpp:75` 仅在非 Unified 路径取链路项）
- **ε 口径**：`epsilon_rel = |C−T|/T` 是**比值**不是百分数（`src/main.cpp:311-312`）。
  512³ 首轮 `predicted 0.0103244 / measured 1.27091 → ε_rel=122.097`，同键 HIT 轮
  `measured 0.838425 → ε_rel=80.2078`；量级来自 HCS 声明峰值 26000 GFLOPS（Apple 自报 SoC 峰）
  对实达内核吞吐 2·512³/0.494375ms ≈ 0.54 TFLOPS 的缺口——模型常数精度（L4 roofline 标定范围），
  非运行器缺陷。同机同尺寸抖动约 1.5 倍，故不据单轮读数下趋势结论
- **文档数字必须是入库读数的可解析引用**（本轮收尾抓到的三类缺陷）：
  acceptance 引用的 `ε_rel=99.6471`、README 引用的 `5.18 GB/s`、task_plan 引用的
  `1.029/99.6` 均出自未入库的一次性读数，与转录不符 → 全量重采后按当前转录替换，
  并在文档头声明重采事实与口径
- **.gitignore 声称≠事实**：phase10/README 曾写"`build-apple/` 整体被 .gitignore 忽略"，
  实际 `git status` 里它是未跟踪项；已补 `build-apple/`、`uk_run/`、`probe/uk_metal_probe`
  （探针源码入库、编译产物不入库）
- **新增 CLI 参数须被测试证明它移动了**：`--allow-unified-calibrate` 起初只有真机转录、
  零单测约束 → acceptance 补 R7.3 四臂（经真实 `unikern` 二进制，argv[0] 同目录定位），
  含顺序判别臂（rc=3 在"门控拒绝"与"空账本"两种原因下不可分，须断言消息）与
  场景 B 特异性对照臂；摘掉门控后臂 1 转红 → 该门控有回归位
- 踩坑（POSIX/Windows 分叉）：`std::system` 在 POSIX 返回 **wait 状态**，`exit(3)` 读回 768，
  断言 rc 须 `WIFEXITED/WEXITSTATUS` 归一化；既有 crash-tx 臂只断言 `rc != 0` 故未暴露。
  Windows 分支沿用 cmd.exe 外层引号写法（本文件既有先例），本轮未在 Windows 主机复跑
- 历史结论保留不改写：本节上方 Phase 5/9 的"Metal 工具链仅 macOS，本机只能做源生成
  stub""Apple 真机需用户 Mac"为当时（Windows 主机）探测记录，已被本节推翻
- 遗留边界（如实）：Apple/Intel 流式（场景 B 显式双缓冲）不实现，resident-only 诚实拒绝；
  R8.5 缺页迁移监测器属真机长测项（G1 KML 文本解析、G5 属 V1/长测）；
  Apple 有效计算率的 roofline 标定属模型精度范围
- 真机证据：docs/evidence/phase10/（environment/emit/verify/run/hit-run/calibrate/
  calibrate-forced/ctest/gate-arm-probe 转录 + emit/uk_apple.metal 构件本体 + README）；
  acceptance-mvp.md v6.1

## Phase 11 G7 闭合：非 GEMM 算子的能力面必须显式（2026-10-27）
- **缺陷本体的读数**（不是推测）：把 reduce 树（L=2²⁰，workload 1 048 576 ops / 8 388 608 B）
  直调 `hip_runnable_program()` → 产物是 `const int PM = 1048576, PN = 4096, PK = 4096;` 的
  `uk_gemm`，**零条拒绝诊断**，账本键 `reduce:fp32:1048576x4096x4096`（PN/PK 是结构体默认维
  被顺带补进去的）。真机侧同理：`run_metal` 拿到 gemm 内核 + reduce 树时
  `perf=1048576x32x32 verify=PASS measured_ms≈5.3` ——即"声明一个负载、测另一个"，
  还把结果按前者的键回写，直接污染 R7.3 校准的输入源
- 三个静默默认位点同根：`src/codegen_hip.cpp` 的计时尺寸、`src/runner_metal.mm` 与
  `src/runner_ocl.cpp` 的 `TimingPlan`（都是 `if (op == Gemm) {...}` 无 else，落回 4096³ 默认）
- **拒绝层次（每层各自被证明是承重墙）**：① 单一判据 `op_support_gap()`（谓词摘掉 →
  红于断言 `op_support_gap(*pr.first).empty() == (op == Gemm)`）；② 生成物自拒（HIP 程序打
  `UNIKERN_ERROR what=op_unsupported` 退 4；MSL/OpenCL 文件不含内核，编译期即失败）；
  ③ runner **入口**自拒——必须排在写 workdir 与编译之前，否则拒绝用的空源文件会盖掉
  上一次的真产物（Apple 侧 uk_run/uk_apple.metal 是证据本体）；④ CLI 门排在 ledger
  任何读写之前（被拒的树留不下账本行，红的是 run 臂的 rc 契约）
- **回归位分级（诚实口径）**：谓词与 CLI 门有 ctest 覆盖；**runner 入口门没有**——
  `run_metal`/`run_ocl` 要真设备，acceptance 在 CPU 侧调不到，摘掉它时 ctest 仍全绿。
  它的回归位是 `probe/uk_op_refuse_probe.mm`（错配调用应被拒，`probe_rc=0`）。
  不要把这条误读成"全链路都有测试"
- **`--op` 只是声明面第一片**：`gemm|reduce` 可选，reduce 单长度并把其余维清零（否则
  默认维会混进账本键）；conv 在 parse 阶段按 arity 拒绝——声明需要六维而 CLI 只暴露三维，
  接受它等于在隐式默认上求解（R1.2）。`solve --op reduce` 允许解（roofline 模型本已覆盖），
  但打印 model-only 提示且拒绝 `--emit`
- **R10.3 无人认领（G8）**：`grep R10.3` 在验收文档/代码/测试里零命中——requirements R10
  第 3 条"输出每个解在各后端的原语覆盖情况"没有任何实现或断言。现状由拒绝消息部分代偿，
  但没有 op × backend 的覆盖表。与 G1 同批做最省
- 两处自捉的工具/量测坑：`-x objective-c++` 会作用到其后所有输入，静态库前须 `-x none`
  复位，否则 clang 把 `.a` 当源码编译；以及第一次录 runner 拒绝转录时用了"摘门控"状态下
  编译的探针二进制 → **量到过期产物**（那次显示的正是错配执行）。教训：转录必须与
  被测二进制的 md5 同录，重编译后才作数。另需记一刀口径：**静态库 `.a` 的 md5 不能当同源性证明**——
  归档头含成员时间戳，同一源码重建两次 md5 也不同（实测 `b6e0a56b…` → `f4ee8f49…`，源码未改）；
  它只标识"读数出自哪一次构建"，同源性由每臂自带的行为判据（谓词返回空串 vs 点名 reason）承担。
  可执行文件 `build-apple/unikern` 的 md5 经两次重建实测不变（`22afe479…`），CLI 类转录以它作锚
- 平台边界：`src/runner_ocl.cpp` 本轮只经 `-fsyntax-only -DUK_HAS_OCL` 编译检查，
  未在 Intel 真机链接/运行；AMD 的 hipcc 真编译路径本轮未执行（其拒绝由生成物文本承担，
  acceptance 文本级断言）。Windows/ROCm 复跑仍待做

## Phase 12 KML 文本前端 + 覆盖报告 + 键身份（2026-10-27，G1 声明半边 / G8 / G9）
- 解析器只能有一份：HCS 与 KML 的 flat-JSON 读取合并到 `src/json_flat.{hpp,cpp}`，
  hcs.cpp 改为复用。判据是行为不变——`hcs_apple.json` 的指纹仍是 `f01e6704`，
  且 ctest 全绿。两份扫描逻辑迟早在不一致的转义/数组边界上漂移
- children 必须先切作用域：`json_field` 找的是**第一处**键名，若不先把
  `"children": [...]` 整段摘掉，父对象的 `op` 会被子对象的 `op` 满足（报错对象错位）。
  这条由反测臂守着：摘掉切割后连"合法父子"都解析失败（红点 `:345`）
- arity 的权威必须单点：`kml_arity()` 同时供前端校验、`kml_export`、`op_signature`、
  `canonical_form`。起因是自捉——测试里 `reduce` 只设 `dims[0]`，结构体默认 4096
  泄漏进了签名（`reduce:fp32:4194304x4096x4096`）。"尾零裁剪"不够，得按算子声明取维
- 键身份的兼容口径：按 arity 编码后 **gemm 签名逐位不变**（4096³ 仍是三维），
  所以 Phase 8/9/10 已入库的账本键与指纹不受影响；reduce 的 canonical 从 `L,0,0` 变 `L`，
  而 reduce 从未有过已入库实测行（Phase 11 起它在落库前即被拒）→ 无历史产物需迁移
- layout 是能力轴不是字符串：只接受 "NT"（三后端 emitted kernel 实际的行主序索引），
  其它值在**前端**拒。收下一个不实现的 layout 再按行主序默默执行＝G7 同一类静默
- R10.3 的覆盖报告三轴分别来自 `op_support_gap()` / `supports_streaming()` / `available()`，
  不建第二张表——报告与生成器不可能各说各话；execution 轴写死即红（`[R10.3]` 臂）
- **R10.2"纳入 F 约束"的真实形态要说清**：`solve()` 仍是 (hcs, tree) 纯函数、不知后端，
  覆盖信息表现为*生成/执行拒绝 + 报告点名*，不是 F 里的一条谓词。合进 F 需给解空间
  加后端轴（V1）。文档若写成"已按后端过滤可行域"就是夸大
- 真机读数（声明驱动）：`run --vendor apple --kml samples/kml_gemm.json` →
  verify=PASS max_rel=0；COMMIT ε_rel=88.7514；同键 HIT 再 COMMIT 84.2782（比值口径）。
  conv/reduce 声明 rc=2 拒绝，且账本文件不被创建（gate 在 make_ledger 之前）
- 断言设计坑（自捉第 2 处）：`solve` 连只读查找都会创建账本文件，因此"拒绝后无账本行"
  必须用**未被触碰的新路径**断言，否则恒真
- 跨阶段可复现性会被自己的重构打断：`op_support_gap()` 函数体在本轮加了 layout 轴，
  Phase 11 的 `probe/phase11_arm_probes.py` 锚点失配 → 同步改写锚点；这类脚本是
  fail-closed 的（锚点数≠1 即中止），所以不会半改，但也提醒：改到被转录引用的函数体
  就要回验那条转录的复现命令
- 遗留（如实）：G1 执行半边（conv/reduce 可用内核生成）、更复杂的原语树组合语法、
  `--kml` 在 AMD/Intel 真机端到端复跑；`runner_ocl.cpp` 仍只有 `-fsyntax-only` 检查

## Phase 13 conv/reduce 执行面（Apple 先行）+ G6（2026-10-27）
- **G6 的读数说服力在哪**：`recommendedMaxWorkingSetSize` 在本机是 13631488000 B（12.7 GiB），
  而旧口径（0.85×HCS 声明 32 GiB）允许到 29.2 GB——差 2.1 倍。所以这不是"理论更严谨"，
  而是旧门真的会放过一个设备装不下的载荷。两道门**并列不叠乘**：叠乘（0.85×0.75×容量）
  会重复打折，且报错时说不清是谁挡的
- **为什么 G6 不用"摘掉即红"**：摘门 = 允许 20 GB 载荷走到分配阶段（设备上限 12.7 GB），
  既可能压垮机器，量到的也只是分配失败而非判定。判别证据改为三条带内臂：
  两界之间的载荷被新门拒 + 容量门沉默 / 超容量仍由容量门拒 / 正常载荷照常跑通。
  这是一次"证据形式随代价调整"的取舍，写清楚而不是假装做了统一形式的探针
- **per-backend 能力面**：`IBackend::capability_gap()` 默认沿用共享谓词、Apple 覆盖为三算子。
  三个消费点（CLI run 门、runner 入口门、覆盖表）全部读同一答案；`--emit` 改按
  "任一后端覆盖"判 → 产出可以异构（一个真内核 + 两份点名拒绝），这正是真实状态，
  比"全拒"或"全假装"都诚实。HIP/OCL 的谓词与产物没动
- **reduce 的失败与诊断链**（本轮最有价值的一条）：首跑 `verify=FAIL max_rel=1.0`。
  根因是 pass-1 线程组宽 4096 > `maxTotalThreadsPerThreadgroup`(1024)，Metal 静默不执行，
  输出保持全零。**抓到它的是 verify，不是命令缓冲状态检查**（失败模式下缓冲仍报 Completed）——
  两层保护的分工必须说清，否则下次会误以为状态检查够用。修法：组宽 `min(p,64)`
- **verify 数据的选择是一种声明**：reduce 用 2^-6×2^20=16384，任意求和次序 fp32 精确，
  故 max_rel=0 只说"结构对"。想验浮点次序鲁棒性得用会让部分和相消的数据——本轮不做，
  并在源码注释里写明，而不是让 0 去暗示更多
- **conv 的直白实现也是声明**：一线程一输出、valid padding，没有分块/寄存器复用。
  ε_rel 334（e2e）与 244（HIT 轮）就是这个内核的真实水平；没有拿它冒充优化实现
- **共享真值**：cpu_conv/cpu_reduce 在 `src/reference.cpp`，runner 与 acceptance 同源；
  四组手算用例（1×1 退化 / identity 核的迹 / 多通道多输出 / 空区间）把"真值"自己也钉住了
- **gemm 回归**：同一份 `samples/kml_gemm.json` 读数（PASS max_rel=0，ε_rel 66–75）与 Phase 12
  同量级；UNIKERN_RESULT 的形状与字段未变，`perf=` 对 gemm 仍是 `PMxPNxPK`
- 遗留（如实）：AMD/Intel 的 conv/reduce 内核；这两家的 `--kml` 真机复跑；
  conv/reduce 调优与浮点次序鲁棒性验证；R10.2 的 F 约束合流（V1）

## Phase 14 Intel/OpenCL 的 conv/reduce（2026-10-28）
- **没有真机也能做真验证**：clang 自带 OpenCL 前端，`clang -x cl -cl-std=CL1.2 -fsyntax-only`
  对生成的三份 `.cl` 全部 rc=0、0 诊断；负测（未声明标识符）rc=1。这比"只做文本断言"强一档，
  但要写清它证明的是**可编译**，不是可运行
- **"覆盖 ≠ 可运行"必须由两个轴承担**：覆盖表里 `intel:ocl primitive=covered` 与
  `toolchain=absent` 同时为真；本机 `run --vendor intel --kml conv` 因此以
  「no Intel GPU platform/device found」rc=1 收场（不是能力拒绝，也仍不建账本）。
  两种失败原因在输出里分得开，这是 R10 要的形态
- runner_ocl 泛化与 Metal 同构：plan 按算子、`launch_gemm/launch_conv/launch_reduce`、
  共用 `time_passes`（标记事件括 e2e；kernel-only = N 次连续 launch）；reduce 要**两个** kernel
  对象（part + sum），OclAll 相应加 `ker2`
- conv 的 verify 计时用**临时 plan**（`vp`）承载 verify 的形状，避免把 timing 的六维改掉——
  这类"借用同一个结构体装两种尺寸"的地方最容易串味，注释里点明
- **Phase 13 的两条旧结论当场被推翻并保留原文**：①"AMD/Intel 对 conv/reduce rc=2 拒绝"
  （现在只有 AMD）；②"OpenCL 源仍 gemm-only"（现在不是）。旧转录保留、标注取代，新读数在
  `docs/evidence/phase14/phase14-ocl-and-regression.txt`
- 仍缺：AMD 的 HIP 程序（无 hipcc，连语法都验不了）、Intel 真机运行、调优与浮点次序鲁棒性

## Phase 15 AMD 收尾：conv/reduce 的 HIP 程序 + 真机闭环（2026-10-28，G1 执行半边闭合）
- **"逐位不变"要选对对照口径**：fresh-solve 对 fresh-solve 才是 gemm 程序同一性的正确比较；
  ledger-HIT 重建的解**不携带 shard 参数**（SQLite 行只存 tm/tn），会让 R8 横幅多出
  "(defaults: solution carried no shard params)" 后缀——这是账本形状的既有属性
  （`hip_runnable_program` 的 `defaulted` 分支，Phase 7 起），不是本轮代码生成差异。
  首次对照就踩了它：第二次 emit 命中账本后 diff 出一行差异，换成新账本后 diff 为空。
- **生成程序分派的结构**：`hip_runnable_program` 只做 layout 拒绝 + 按算子转发；
  conv/reduce/gemm 三个 builder 各自独立，gemm body 原样搬运。每份程序自带 CPU 参考
  （语义对齐 reference.cpp，不含核心库头，独立过 hipcc）；R5/R8 横幅六字段
  （hcs/fp/toolchain/constants_v/op 行/placement）三份同构。
- **reduce pass-1 宽度与 Metal/OpenCL 的差异**：OCL runner 启动恰好 P=min(L,4096) 个
  work-item（内核无越界）；HIP 用 256 线程块启动 ceil(P/256)×256 个线程，宽度向上取整到
  块倍数、内核加 `tid >= nP` 卫语句——多出的线程写零部分和，折叠吸收。verify 形状
  （2^-6×2^20=16384）对任意宽度都精确，两后端在账本相关的形状上 P 同为 4096。
- **HIP conv 计时网格**：blk(8,8,1) × grd(ceil(Wo/8), ceil(Ho/8), Oc)；gridDim.y/z 上限
  65535（RDNA），程序在 launch 前显式拒绝 `grid_too_large` 而不是让 launch 报错兜底——
  这是 conv 特有的显式预检（gemm 程序没有，它的网格按 tile 分块天然小）。
- **[R10.2] 的 op 拒绝臂没有消失而是换了诚实形态**：HIP covered 后 `--vendor amd` 的
  reduce run 会真跑设备（ctest 不能干这个），臂改成 `--vendor apple`（Windows 无 Metal
  工具链）→ rc=1 RUN FAILED、无 UNIKERN_RESULT、无 LEDGER COMMIT；macOS 上 apple 可用，
  故 _WIN32-only。rc=2 能力拒绝路径在 CLI 上已不可达（layout 在 parse 就被前端拒）——
  "门让位"是有意的，不是回归。
- **转录纪律同 Phase 11**：arm-probe 重编译后二进制 md5 会变，转录与 `binary-md5.txt`
  同录；真机 run 的六条转录（3 op × COMMIT/HIT）都在 md5 锚定的当前二进制下重采。
- 仍缺：Intel conv/reduce 真机运行、conv/reduce 调优与浮点次序鲁棒性、R10.2 的 F 约束
  合流（V1）、G5 R8.5 驻留守卫长测。

## Phase 16 G5/R8.5 驻留守卫 + Phase 15 验收收尾复采（2026-10-28，v12）
- **守卫结构**：`emit_residency_guard85()` 向 HIP 三程序（gemm/conv/reduce）发射守卫——
  `hipMalloc` 与 `hipPointerGetAttributes` 驻留断言折叠进单个 `uk_mmalloc()`（分配与检查
  不可分离，新增裸 `hipMalloc(&` 直调点即被 `[R8.5]` 断言抓出）；非设备驻留
  （managed/host）→ `UNIKERN_ERROR what=oversubscription ptr=<名> type=<n>` 退 5
  （普通 HIP 失败仍是退 4，两种失败在退出码上分得开）
- **设备事实修正（与 Phase 9/10 同类）**：任务口径的 `attributes.memoryType` 在 ROCm 7.1
  不存在——首跑 hipcc 真编译即拒（`no member named 'memoryType'`）；实测
  `hip_runtime_api.h:276-284` 字段名为 **`type`**（Device=2/Host=1/Managed=3）。
  "设备事实经 API 实测，不照抄标签"再次生效
- **migrations token 的诚实语义**：`RunOutcome.migrations`（-1=本后端不报告）；RUN 行只在
  后端实际报告时打印——Metal/OpenCL 进程内 runner 不打印，不冒充实测值
- **拒绝分支的验证路径选择**：生成程序自身的 hipMalloc 路径在本机硬件上到不了
  非设备驻留（这正是 migrations=0 的 happy path），改用 hipHostMalloc 的 host 指针
  代位非设备驻留条件——`what=oversubscription ptr=hipHostMalloc h type=1` + exit=5；
  happy path（hipMalloc 指针）rc=0（`probe/phase16_residency_refuse.cpp`）
- **复采口径（v12 验收收尾）**：当前二进制（md5 `e554574a…`，与 HEAD 同源）上重采
  backends（amd=yes/apple=no/intel=no）、`solve --emit` 三源行数（conv 253/reduce 217/
  gemm 425 = 首轮各 +34/35 行，即守卫并入三程序；`uk_resident` 计数各 2）、三算子
  run×COMMIT+HIT（六轮全 verify=PASS max_rel=0 且尾 migrations=0）、ctest 2/2 绿
  ——转录 `docs/evidence/phase15/phase15-recheck-v12.txt`，README 补复现命令表
- **文档引用纪律（E14 复用）**：v12 的全部数字只出自本次入库转录；首轮 phase15/phase16
  转录保留不覆盖，acceptance v11 引用的旧读数仍可解析对照。ε_rel 同量级波动
  （conv 67–72 / reduce 0.35–1.11 / gemm 3.41–3.46）如实注明"不构成趋势结论"
- **本机可做项清零后的剩余开放缺口**：Intel conv/reduce 真机运行（需 Intel 机器，
  backends 实测 intel=no）、G5 长测（迁移率统计与超阈重求解）、R10.2 F 约束合流（V1）、
  conv/reduce 调优与浮点次序鲁棒性

## v12.1 独立复核修订（2026-10-28）
- **版本注记只声明"更新了哪些行"不等于"其余行免检"**：汇总矩阵 R1 行与 R1 详段是 v8/v9
  旧文（"AMD/Intel 执行半边待下一批"、"`--vendor amd/intel` rc=2 且不建账本"），v11/v12
  只改了 R5/R6/R8/R10 与缺口表，R1 行漏更 → 与 G1 全闭合结论直接矛盾。复核员的对照方法
  值得复用：把每条矩阵行的状态注记与缺口表/G 闭合结论对账，凡"待下一批"字样都要找它的
  "下一批"是否已发生。已据实改判：AMD 真机 COMMIT→HIT（引 v12 复采转录）、Intel 编译级
  待真机；旧 rc=2 读数标注为 Phase 12 能力拒绝时代（当时为真，被 v11 翻转取代）
- **失败消息是"随构建而异的变量"，不是常量**：Phase 14 补遗原文引用的
  「no Intel GPU platform/device found」实际出自 Mac 侧构建（`build-apple/unikern`，
  UK_HAS_OCL 已定义；源码 `src/runner_ocl.cpp:434`），转录
  `phase14-ocl-and-regression.txt:47-52` 只捕获 rc=1 与 ledger_created=no（log tail 位置
  是解释性注释）。Windows MinGW 构建（UK_HAS_OCL 未定义）同命令实测报
  「OpenCL runner stub: no OpenCL import library was found at build time …」rc=1。
  承重事实（rc=1、solution 零落库）跨构建一致；文档引用消息字串必须注明出处构建
- **自捉：`unikern ledger <path>` 静默忽略路径参数**（`src/main.cpp:604` 硬编码
  kDefaultLedger——G7 同类静默，grep tests/evidence 零依赖确认无回归面）→ 改为
  `make_ledger(argc >= 3 ? argv[2] : kDefaultLedger)`，重验两后端均报 0（intel 失败 run
  未写任何行）；二进制 md5 变为 `9759d55a74e90acd11017d4d6b67a09a`（run/solve 代码路径
  未动，v12 复采转录仍锚 e554574a 有效）
- **"不建账本"的精确语义（本次实测辨析）**：失败的 run 是否创建账本**文件**随后端而异——
  `.jsonl` 不创建（Phase 12/14 读数口径）；`.sqlite3` 由只读查找创建空库（`solutions`
  表零行）。两种后端下承重事实相同：**solution 零落库**（R6.3）。文档应写"零落库"而非
  一刀切的"不建账本"
- 工作区卫生：三件已入库的 Phase 9 ocloc AOT 证据产物（`build/phase9-emit/` 的
  uk_intel.cl + aot/*.bin/*.spv，R5.2 Intel 真编译证据本体）被工作区误删且删除未提交，
  已 `git restore` 恢复（HEAD 84b2438 校验和一致）

## E17 + 本机画像 + v12.2 清尾事实（2026-10-28）
- **E17（跨平台可选语言）**：无条件 `project(... OBJCXX)` 让任何无 Objective-C++ 编译器
  的机器配置失败（MinGW 的 OBJCXX 探测直接 broken）。教训：可选语言一律放平台分支内
  `enable_language()`，源文件按平台进编译列表；非 Apple 用 stub 翻译单元兑现同一符号面
  （`metal_available()=false` / `run_metal` 诚实拒绝），链接错误变诚实报告（commit 4224d42）
- **本机画像（Phase 15 工作流启动前探测，与 backends 诚实探测一致）**：hipcc/ROCm 7.1 在位
  （AMD 可做）；**无 Intel 设备**（无 `HKLM\SOFTWARE\Khronos\OpenCL\Vendors` 键、无
  ze_loader.dll、无 ocloc——Phase 9 的 Intel 真机验证在另一台机器完成，与本机 Phase 8 前的
  探测一致）；无 Metal。多机仓库协作时，"本机能做的"必须以当前机器实测为准，不照抄计划册
  其他机器的"本机"记录
- **v12.2 复核残留教训**：验收文档引用的命令必须**可逐字复现**——R1 行 intel 读数的命令
  形式缺 `--hcs`，按字面复现得 usage rc=2；已补全并转录实测输出
  （`docs/evidence/phase15/intel-r1-command.txt`，根因逐字可见：本机构建期无 OpenCL 导入
  库 → runner stub 且无 Intel 设备）。md5 锚是**时点快照**：v12.1 改 main.cpp 后当前锚
  `9759d55a…`，全演进记录 `binary-md5.txt`，文档引用历史锚必须标注时点
- 收尾状态：ctest 2/2 绿、工作区净（仅 .serena/ 会话前即存在）、HEAD df44dd8
