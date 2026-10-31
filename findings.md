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
  `maxThreadsPerThreadgroup.width=1024`（`maxThreadsPerThreadgroup` `src/runner_metal.mm:76`）；**本 SDK 无 CU 数量 API**，
  HCS 的 `compute_units=40` 是声明值，不冒充实测
- **边界（本轮如实标注，未扩范围去实现）**：runner 的 resident 预算取
  `hcs.tier_bytes("unified")`（`src/runner_metal.mm:203`，即 34359738368 B = 32 GiB 的 HCS 声明值），
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
  （`link_bw_gbps` `src/solver.cpp:102` 仅在非 Unified 路径取链路项）
- **ε 口径**：`epsilon_rel = |C−T|/T` 是**比值**不是百分数（`epsilon_rel` 在 `src/main.cpp:552`）。
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
  上一次的真产物（Apple 侧 uk_run/uk_apple.metal 是证据本体）；④ CLI 门排在 ledger （旧路径）
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
  UK_HAS_OCL 已定义；源码 `uk_ocl_log`（`src/runner_ocl.cpp:516`）），转录
  `phase14-ocl-and-regression.txt:47-52` 只捕获 rc=1 与 ledger_created=no（log tail 位置
  是解释性注释）。Windows MinGW 构建（UK_HAS_OCL 未定义）同命令实测报
  「OpenCL runner stub: no OpenCL import library was found at build time …」rc=1。
  承重事实（rc=1、solution 零落库）跨构建一致；文档引用消息字串必须注明出处构建
- **自捉：`unikern ledger <path>` 静默忽略路径参数**（`measured_rows` `src/main.cpp:604` 硬编码
  kDefaultLedger——G7 同类静默，grep tests/evidence 零依赖确认无回归面）→ 改为
  `make_ledger(argc >= 3 ? argv[2] : kDefaultLedger)`，重验两后端均报 0（intel 失败 run
  未写任何行）；二进制 md5 变为 `9759d55a74e90acd11017d4d6b67a09a`（run/solve 代码路径
  未动，v12 复采转录仍锚 e554574a 有效）
- **"不建账本"的精确语义（本次实测辨析）**：失败的 run 是否创建账本**文件**随后端而异——
  `.jsonl` 不创建（Phase 12/14 读数口径）；`.sqlite3` 由只读查找创建空库（`solutions`
  表零行）。两种后端下承重事实相同：**solution 零落库**（R6.3）。文档应写"零落库"而非
  一刀切的"不建账本"
- 工作区卫生：三件已入库的 Phase 9 ocloc AOT 证据产物（`build/phase9-emit/` 的 （旧路径）
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


## Phase 17 Intel 三算子真机验证闭合（2026-10-28，开放缺口①）
- **机器身份口径**：本仓库由多台机器并行推进——v12/v12.1 写的"本机无 Intel 设备、
  backends intel=no"是 AMD 机器侧会话的机器相关读数；本会话在 Intel Arc B390 机器上
  同一命令实测 intel=yes。两份转录并存（phase15/intel-r1-command.txt 与
  phase17/phase17-backends.txt），文档引用时必须注明机器侧
- 真机结果（B390，驱动 32.0.101.9150）：conv 首次真机执行 verify=PASS max_rel=0
  （64×64×32×3×3×64 位级一致）→ COMMIT ε_rel=13.5646 → HIT；reduce 首次真机执行
  verify=PASS max_rel=0 → COMMIT ε_rel=0.883955 → HIT；gemm 回归 1024³
  verify=PASS max_rel=0 → COMMIT ε_rel=2.4714 → HIT（与 Phase 9 同键同 tile 不变）
- **E18（规格误读，Phase 9 引入）**：`CL_DEVICE_MAX_WORK_ITEM_SIZES` 限制的是
  **本地工作组**每维大小，不是全局网格——规格里 global_work_size 无每维上限；
  runner 此前把它当全局上限做 launch 前检查，reduce 的 P=4096 被错误拒绝。
  暴露点："verify=PASS 与 refused 并存"（verify 阶段同一发射在设备上真实执行且
  位级一致）。修复：删错误检查，代之以真值门（gemm 显式 local 16×16=256 对
  CL_DEVICE_MAX_WORK_GROUP_SIZE；conv/reduce 传 nullptr local 由驱动自选）。
  HIP conv 程序的 grid_too_large（gridDim.y/z ≤ 65535）是真实硬件限值，不受影响
- **E19（计时竞态）**：事件完成前读 clGetEventProfilingInfo——规格应返回
  CL_PROFILING_INFO_NOT_AVAILABLE，本机 Intel NEO 驱动实测返回 CL_SUCCESS+全零
  → 间歇性 measured_ms=0。Phase 9-16 的证据恰好每次都赢了该竞态（宿主入队慢于
  设备执行）。修复：读前 clWaitForEvents 等 pass 末尾标记——与生成的 HIP 程序
  "hipEventSynchronize 后再读"同一纪律；e2e 与 kernel-only 两括号都加
- 分级：两缺陷均在 runner 设备执行路径上，ctest（CPU 侧）无回归位——沿 Phase 11 d
  先例以转录承载"摘除即红"（前后对照 phase17/phase17-defects-before-after.txt）；
  E19 竞态间歇性，无法在探针中确定性复红，如实分级
- 教训：真机执行路径的"从未在真设备上跑过"的代码（Phase 14 只有 clang 语法检查）
  首跑即暴露两个潜伏缺陷——语法检查不等于执行验证；证据里 verify 与计时自相矛盾的
  读数（PASS 但 measured=0）永远值得当场追查，不是噪声

## Phase 18 G5 长测半边（R8.5 后半：告警率 → 违规记录 → 重新求解，2026-10-28）

- **规格原文核对**（`.monkeycode/specs/unikern-mvp/requirements.md:98-103` R8.5）：要求两件事——
  ①显式流式是场景 B 显存不足的唯一处理方式（Phase 16 已闭合）；②运行时缺页迁移率超告警阈值 →
  **记录违规事件 + 触发重新求解**。Phase 16 的注释早已把 exit 5 写成"the upper layer's
  re-solve signal"，但**上层从来没有这个消费者**——`cmd_run` 把 exit 5 和任何失败一起归进
  `RUN FAILED` rc=1，信号被吞。本 Phase 补的就是这个消费方。
- **流式（场景 B）在守卫落地后首次真机重跑无回归**：`--dims 32768 32768 32768` →
  `UNIKERN_INFO mode=streamed shards=64 shard_rows=512 prefetch_d=2`、`verify=PASS max_rel=0`、
  `migrations=0`、rc=0、LEDGER COMMIT（ε_rel=5.05902）。pinned 分片走 `hipHostMalloc` 不经守卫
  （`src/codegen_hip.cpp` 的 streamed 分支里那两行 `hipHostMalloc`——这里故意不写行号：该文件
  今天被我自己改过三轮，行号锚点必然再漂；初稿曾写 `:942-943`，Phase 19 复测为 967-968，
  Phase 21 后已在 1195 附近，见 E23）所以守卫没有把合法流式误判成超订——这是先验结构核对，
  不是运气。转录 `docs/evidence/phase18/phase18-streamed-round1.txt`。
- **读数演进与一次自我推翻**：同一 32768³ 流式用例 Phase 8 记 `measured_ms=55354.1 /
  kernel_ms=71354.8`（kernel>e2e）。本 Phase 跑了两轮流式：第 1 轮 `21789.8 / 20627.0`
  （kernel<e2e），第 2 轮 `23162.3 / 31288.3`（**kernel 又 >e2e**）。所以正确结论不是"Phase 8
  的口径被推翻"，而是**本机 `kernel_ms` 在流式路径上至今不是稳定量**——同一用例、同一二进制、
  相邻两轮就能一个小于一个大于 e2e。我先前写的"kernel<e2e 即搬运与计算真正重叠"是过度断言，
  已按两轮的实测改回。Phase 8 转录仍不改写，只作历史时点读数。
- **率的可测口径（D7）**：分子 `migrations`（非设备驻留的设备分配数）、分母 `checks`
  （`uk_resident` 探针次数，实测 resident reduce=6 = verify 3 + timed 3）。阈值
  `--migration-threshold`（比率）默认 **0.0**，与"任一非驻留即 exit 5"的既有语义等价。
  边界如实声明：gfx1032 的 Windows 应用运行时路径**没有**可读的缺页/迁移硬件计数器
  （HSA perf counters 不在此路径），所以这个率是 R8.5 的**可测代理**（显式驻留探针率），
  不是计数器直读；程序自身显式发起的 pinned 分片搬运不计入迁移。
- **重求解的输入必须是实测量**（E13 教训的直接继承）：违规后用生成程序打出的
  `vram_free/vram_total`（`hipMemGetInfo`）把 HCS 的设备层改成实测空闲量并标
  `confidence=Measured`，再 `solve()` 一次；实测本机 `vram_free=8407547904`、
  `vram_total=8573157376`（声明 8589934592）。拿不到实测（`vram_free==0`）时
  `RESOLVE STOP` 退 5，绝不用臆造常数重解。重试键用 `ledger_key(tightened, tree)`，
  避免把在另一容量上测得的行挂在旧指纹下（R7.1）。
- **开火控制（真机端到端）**：本机正常路径不可能产生非驻留设备分配，所以 `migrations=0`
  本身不可采信。`probe/phase18_alarm_probe.py` 把 `uk_mmalloc` 的 `hipMalloc` 换成
  `hipHostMalloc`（带 try/finally 复原 + 残标断言），实测整条链：
  `UNIKERN_ERROR what=oversubscription ptr=vX type=1 migrations=1 checks=1
  vram_free=8425177088 vram_total=8573157376` → `UNIKERN_VIOLATION what=migration_rate
  rate=1 threshold=0 ... committed=no` → `RESOLVE re-solve device_capacity=8589934592->
  8425177088 (measured by hipMemGetInfo)` → 重试再违例 → `RESOLVE STOP`，**rc=5、零落库**；
  账本里两条 `#VIOLATION` 事件行、没有一行 solution 行。复原后 ctest 2/2 绿。
  转录 `docs/evidence/phase18/alarm-firing-control.txt`。
- **`[R8.5-long]` 臂**（`tests/acceptance.cpp:2490` 起，紧邻其后的 R10.3 段在 :2610）：率的六条边界（含
  `checks=0 → rate=1.0` 与"后端不报 token ≠ 0"）、三份生成程序都带 `checks=/vram_free=/
  vram_total=` 且 `++UK_RESIDENT_CHECKS` 恰好一处、两种账本格式都"事件计数与 solution
  行数分离"（`violation_count()` 新接口，`size()` 不再把事件算作解）、阈值 `nan/-1`
  在 parse 即拒（rc=2）且不建账本文件。
- **坑（值得记住）**：`ctest` 在上一次构建**失败**时仍然报 2/2 绿——它跑的是磁盘上的旧
  `unikern_acceptance.exe`。判定必须先确认链接步骤真的发生过。
- **探针判据自捉**：首版断言"违规文件只有一行"，实际初始告警 + 重试告警是**两条事件**，
  这是实现的正确行为而非缺陷；判据改为"每一非空行都以 `#VIOLATION` 开头且共两行"。

- **新臂不承重的实测（重要）**：首版 `[R8.5-long]` 用 `find("\" checks=\"")!=npos` 判 token 存在，
  摘除即红**没通过**——删掉 conv 程序 result 行的 `checks=` 发射后 acceptance 仍绿，因为守卫的拒绝行
  自己带同一 token。改成出现次数判据（每份程序 `" checks="`/`" vram_free="`/`" vram_total="` 各恰好
  2 次：拒绝行 + result 行）后，四条腿逐一摘除全红：`probe/phase18_leg_removal_probe.py` rc=0
  （复原 byte-identical，末轮 ctest 2/2 绿，转录 `docs/evidence/phase18/removal-controls.txt`）。
  一般结论：**同一 token 在两处出现时，存在性断言恒真**，必须用计数或分层判据。
- **Phase 8 的 kernel_ms>e2e 现象在本机仍未消失（不是被推翻的旧缺陷）**：见上条两轮流式读数
  （20627<21790 与 31288>23162）——流式的 kernel-only 参考通道在这块卡上不可信，任何用它做的
  推导（含 `calibrate` 的链路带宽）都必须带这条边界。
- **`vram_free` 是"探针时刻的点采样"，含程序自己的活分配（对重解语义有直接影响）**：
  驻留 reduce 15 轮里 `vram_free=8407547904` 逐轮一致（只有 4 MB 级设备占用）；而流式
  32768³ 轮实测 `vram_free=3848339456`——同一台 8 GB 卡，因为此刻程序自己已占住
  ~4.7 GB（整块 B + 两份 A 分片 + 两份 C 分片）。所以"用实测空闲容量重解"重解的是
  **当下真正还能放得下的量**，不是设备标称容量；注入开火控制里拒绝发生在第一个分配
  （`vX`）时，故那次报 8425177088 接近整卡。文档与代码注释都按点采样口径写，避免
  把它读成"设备容量测量"。另外流式轮 `checks=6`（驻留 3 + 流式 sB/sA2/sC2 3）。
- **长测序列已跑（G5 字面意义闭合）**：驻留 reduce **15 轮** + 流式 32768³ **2 轮**，全部 rc=0，
  每轮都 `migrations=0 checks=6`，率恒为 0/6=0；`vram_free` 在 15 轮驻留里恒为 8407547904、
  流式两轮恒为 3848339456（同 8 GB 卡的差值即程序自身活分配）。驻留 e2e 分布
  0.35075–0.49046 ms（kernel 0.1624–0.1667 ms），COMMIT ε_rel 0.259299–0.7609。
  结论：**本机正常路径不可能产生非驻留设备分配，所以"率的分布"只有一个取值 0**，
  超阈行为必须由注入开火控制来证（已证，rc=5 零落库），这一点必须写进文档而不是留给读者猜。
  转录 `docs/evidence/phase18/longtest-series.txt`。
- **仍开放（如实）**：他机项（Intel 真机 conv/reduce 运行验证、Apple 侧长测——MVP 里两者是
  resident-only，该率只在 HIP 路径有意义）与 V1 项（R10.2 的 F 约束合流、conv/reduce 调优与
  浮点次序鲁棒性）。G5 至此不再有需要本机补的机制项。

## Phase 19 次序鲁棒性与 conv 基线（2026-10-28，开放项 ④ 第一半）

- **出厂 verify 的结构盲区（已被实测坐实）**：生成程序的 reduce 对拍数据是 2²⁰ 个 2⁻⁶，
  任意次序都等于 16384，`max_rel=0` 只证明结构、永远证明不了次序鲁棒性（生成程序注释自己写了
  "verifies structure, not floating-point order"）。所以此前所有 `max_rel=0` 读数都不构成
  浮点次序的证据——这条是本轮实测的前提，不是推测。
- **真机栅格**（`probe/uk_reduce_order_probe.cpp`，n∈{2²⁰,2²⁴} × 8 模式 × nP∈{1024,16384,65536}，
  gfx1032）：全部模式中只有宽动态范围的一类产生显著差异。交替 1e6/1e-6（G 模式）：
  n=2²⁰ 时 `max_rel_vs_cpu=7.19e-03`（仍在容差内）而 **GPU 离精确和只有 4.69e-14，CPU 参考自己有
  7.19e-03**；n=2²⁴ 时 `max_rel_vs_cpu` 三档 = 3.97e-02 / 3.99e-02 / 4.00e-02，**超过 2⁻⁵=3.125e-02**
  → 生成程序判 `verify=FAIL`、拒绝入账本（R6.3 生效，非静默通过），而此刻 GPU 离精确和是
  1.86e-04 / 5.56e-05 / 1.71e-04，参考是 3.988e-02。**判据把更准的一方判错**。
- **均匀/等比幅值模式全部安全**：A（2⁻⁶ 均匀）恒 0；C/D/E/F（2⁻²⁴、2⁻¹⁰⁰ 等比升降）最大也只有
  1.70e-06，且误差随 nP 增大而**减小**（1024→65536），即分片越多越准；H（1 与 2⁻⁶₀ 混合）恒 0
  ——小于 fp32 eps 的尾数两种求和都吞掉，测不到次序。
- **两条预登记预测落空（如实）**：B 模式预测 max_rel>0，实测 0（它测的是吸收不是次序）；
  "降幅值一定比升幅值差"只在 nP=1024 成立，nP≥4096 反转（降 0 vs 升 2.68e-07）。
- **确定性臂 `[R6.4-order]`**（`tests/acceptance.cpp:1542` 起，段尾在 :1546，下一段 R6.5 在 :1547）：CPU 侧复算同一事实，
  实测 `seq_err=7.23881e-03 / two_pass_err=4.69154e-14 / divergence=7.18679e-03`，与真机 nP=1024
  行**逐位同形**，所以它是真机事实的可复现代理。臂调的是**生产** `cpu_reduce`（不是本地副本循环），
  控制证明：把 `src/reference.cpp` 的累加器加宽成 double → `[R15]` `tests/acceptance.cpp:1465` 当场报红，
  复原 byte-identical 后 ctest 2/2 绿（`probe/phase19_order_arm_probe.py` rc=0）。
  容差上界写成 `Tolerance{}.fp32_rel` 而不是字面量，改策略会移动这条臂。
- **conv 基线（调优靶子）**：`uk_conv` 一线程一输出、内层裸读全局内存，实测
  128²×16→**61.9 GFLOPS**、256²×32→**19.4 GFLOPS**（吞吐随尺寸**下降**）；同一台卡上寄存器分块的
  `uk_gemm` 512³ 实测 kernel 0.117979 ms → **2275 GFLOPS**。约两个数量级的差距，Phase 20 的
  验收就以此为基线。
- **未改容差、未改判据**：这是规格级决定（D8），三个候选都改变已入库证据的含义。
- **反身事实（同类错误我自己当场又犯了一次）**：上两条锚点初稿写的是 `1416-1500` 与 `1370-1425`，
  都是错的——插入 R6.4-order 段之后，R8.5-long 段整体下移了 69 行，而我按记忆写行号。
  这正是 Phase 17 Harness 复核里 `doc-code-anchors-drift-unvalidated` 那条 finding 的同款错误，
  本轮实测到它发生在我自己身上。**结论：行号锚点必须在写下的那一刻用 grep 现取，不能事后回忆**；
  仓库里那条"锚点无机械校验"的修复项（finding #3）优先级因此上调。
- **再次踩到同一个坑（第 3 次）**：`cmake --build` 失败的那几轮（缺 `<cmath>`、most vexing parse），
  `ctest` 照旧报 2/2 绿——它跑的是磁盘上的旧 exe。判绿前必须确认链接真的发生。

## Phase 20 `uk_conv` 通道分块与选档实测（2026-10-28，开放项 ④ 第二半）

- **收益实测**（kernel_ms，同机同二进制，档由程序运行时选）：256²×32/Oc64 从 124.426(OCB=1)
  → 19.062(OCB=8)＝**6.53×**，128²×16/Oc32 2.361→0.547＝4.31×；吞吐 19.4→**126.7 GFLOPS**、
  61.9→276.3 GFLOPS。**32²×8/Oc16 的符号相反**：0.01258(1) → 0.04311(8)＝**0.29×**，
  因为线程数不足以填满 14 个 CU。所以"取最优档"不是修辞，三尺寸里最优档分别是 8/8/1。
- **分块档不写死、也不抄标称值**：`uk_conv_pick_ocb` 从 {8,4,2,1} 里取"仍能填满设备"的最大档，
  容量 = `hipOccupancyMaxActiveBlocksPerMultiprocessor(每档实测) × dp.multiProcessorCount × 64`，
  每次探测的 `threads/resident_capacity/cu` 与选中值都打进 `UNIKERN_INFO`，所以选择过程可审计；
  探针失败退回 1（对任何设备都正确）并打 `UNIKERN_ERROR what=occupancy_probe`，不静默。
  这条规则是用程序自己的 `UK_CONV_OCB` 校准旋钮扫出来的（15 次运行全部入库）。
- **硬事实：`max_rel` 在四个档、三个尺寸、15 次运行里恒为 0.000000e+00。** 机制是每通道的累加
  次序（`c→kh→kw` 升序、每步一次 FMA）与 OCB 取值无关，分块只改"一次 X 取值服务几个通道"。
  `[R20]` 因此把整个 FMA 嵌套**按字节（含缩进）钉住**，倒序或重组合会当场红。
- **发射点唯一**：verify/warm/timed/kernel-only 四处全部走 `uk_conv_launch(ocb, …)`，
  所以"验证用的档"与"计时的档"不可能不一致（臂用 `count_of("<<<") == 4` 钉住）。
- **两条不承重断言在同一天被抓（E21、E24 同族）**：`find("dp.multiProcessorCount")` 在把容量式
  换成抄来的 `14` 后没红——该符号还出现在 `cu=` 打印行里。改出现次数判据 `== 2` 后三条腿全红
  （抄标称 14 / 换掉一次 occupancy 探针 / 反转 kw 循环），`probe/phase20_conv_arm_probe.py` rc=0，
  复原 byte-identical。**固化规则：写"源里必须有 X"之前先数 X 出现几次。**
- **仍开放的性能空间**：只做了通道维分块，**空间维/共享内存 tile 未做**；调优后 conv 126.7 GFLOPS
  对同机寄存器分块 `uk_gemm` 512³ 的 2275 GFLOPS 仍差约 18×。conv 生成程序 275→372 行
  （Phase 18 记的 253/275/425 系列锚点再次演进，历史读数不改写）。

## Phase 21 conv 空间共享内存 tile 与联合选档（2026-10-28）

- **性能**：256²×32/Oc64 的 kernel 时间 124.571（Phase 19 基线）→ 19.062（Phase 20 通道分块）→
  **7.153/7.167/7.175 ms**（重复三次），累计 **17.4×**、337.5 GFLOPS；128² 0.547→**0.287 ms**（8.5×）。
  32² 在三形状里最小（10.70–14.18 µs 的散布，Phase 20 是 12.58/12.75 µs）——**同分布，
  既不称退化也不称改进**，这是本轮唯一一次"差值小于噪声"的诚实处理。
- **逐位不变是硬证据不是说法**：所有形状 × 所有 tile（含不 tile 的后备路径）× 所有 OCB，
  `max_rel` 恒 `0.000000e+00`；连同 Phase 20 的 15 次共 24 次真机结果行。机制是每个线程仍按
  `c→kh→kw` 升序做同一串 FMA，改的只是取数来源（全局内存 → LDS）。
- **第一版的选档错在只按共享内存装不装得下选 tile**：32² 上选了 `tile=16`（只有 4 个块＝1024 线程，
  而设备自报可常驻 21504 线程）→ 慢 25%。修正＝把 `tile∈{16,8,0} × ocb∈{8,4,2,1}` 当**一个组合空间**
  枚举，用 `hipOccupancyMaxActiveBlocksPerMultiprocessor × dp.multiProcessorCount` 判填满度，
  在填满者里取 `tile×ocb` 最大；一个都不填满时退回 `tile=0/ocb=1`（最多线程的那条路径）。
  三个形状随后全部自动选中各自实测最优组合。教训：**"装得下"和"填得满"是两个约束，
  必须同时作为一次搜索的两个轴，分开选会在小尺寸上选错。**
- **`__syncthreads()` 与边界 return 的先后是正确性问题，不只是风格**：tile 协作加载之后必须全员到屏障；
  若边界检查在屏障之前 return，部分线程跳过 `__syncthreads()` 在 HIP 上是未定义行为。
  `[R20]` 现在断言两者位置次序（把屏障移到 return 之后 → 当场红）。
- **四条腿全部承重**（`probe/phase21_conv_arm_probe.py`）：容量换成抄来的标称 14 / occupancy 探针
  指到另一个内核变体 / 屏障移到 return 之下 / LDS tile 索引错一个元素——每次摘除 acceptance 都红
  （3221226505 = assert abort），复原 byte-identical，末轮 ctest 2/2 绿。断言计数全部取自实测
  （`<<<` 8 次、每条 occupancy 调用形状 4 次、`dp.multiProcessorCount` 2 次、三条升序嵌套各计），
  并沿用了 E21/E24 的教训：先测数量，再写断言。
- 规模与无回归：conv 生成程序 372 → **506 行**；gemm 447 / reduce 239 未变，同批真机
  gemm 512³ 与 reduce 1048576 仍 `verify=PASS max_rel=0 rc=0`。

## Phase 22 Harness 修复三条与锚点机检（2026-10-28）

- **`probe/anchors_check.py` 首跑战果**：34 条 `path:line` 锚点查出 16 条可疑，其中 4 条确证失效并改准
  （`verify_fp32` → `src/runtime.cpp:182`；LEDGER COMMIT 门 → `r.verified` 把关的 `src/main.cpp:540`；`INFEASIBLE` → `src/main.cpp:269`；
  `HCS rejected` → `rejected` `src/main.cpp:213`，而被拒字段串本体在 `missing`（`src/hcs.cpp:95`）——
  原文把它记成了 main.cpp 的第 110 行，那是**归错文件**不是行号漂移）。现在 29 条全绿。
- **门已注册**：`ctest --test-dir build` 从 2/2 变 **3/3**（smoke / acceptance / anchors）。
  摘除即红：把 `tolerance_for` `src/runtime.cpp:182` 改回 `:95` → validator rc=1 且 `ctest -R anchors` Failed(8)，
  复原后 rc=0。这一步不是形式主义——它证明这个门真的会响。
- **工具自己的四轮误报（都是我的判据太粗，与 E21/E24 同族，记为 E25）**：① 一行文档常同时引用三处代码，
  把整行所有反引号 token 都摊给每个锚点 → 必然假红；② 邻域取到锚点**之后**，并列句里后一项的符号
  替前一项的锚点顶罪；③ 收窄到"前面 60 字符"后，同一句里**前一个锚点自带的 token** 又落进后一个
  锚点窗口——最终规则是"上一个锚点结束处 → 本锚点起始处"；④ `[R8.5-long]`、`:942-943`、
  `maxThreadsPerThreadgroup.width=1024`、`` `run` `` 这类不是代码符号，加标识符形状 + 长度 ≥5 过滤。
  每轮修正都双向验证：改坏一个锚点必须报红、复原必须闭嘴——**判据的误报率决定它能不能进 ctest**。
- **历史与活断言必须可区分**：`path:line` 这种形式是"现在还能跳过去"的活断言，归机检管；
  记录历史时点的行号请写成"runtime.cpp 第 95 行"。我已把 findings 里那段锚点抽验记录改成后者
  ——因为**同一条记录里的行号在我自己一天三轮改动之后又漂了一次**（95→151→182），
  这正是机检存在的理由。
- **两条待授权的没动**：build/ 内的 E5 证据搬迁（要动 Phase 9 产物与 .gitignore）、
  phase11–16 旧探针的 try/finally 加固（要动别人阶段的脚本）。

## Phase 23 conv 2×2 输出微块：负结果与自动报警（2026-10-28）

- **预登记的保留线**：`kernel_ms ≤ 5.97 ms`（Phase 21 的 7.16 ms / 1.2）。
- **实测（`microtile-sweep.txt`，256²×32/Oc64）**：2×2 在 tile=16 上 ocb=8/4/2 =
  59.650 / **29.272** / 37.649 ms，tile=8 上 = 80.167 / 40.786 / 52.251 ms，自动档两跑 59.679 / 59.717
  （后两格仅取自回退前终端输出、不在转录里，且代码已回退无法再采——引用时须这样标注，判负不依赖它们）；
  **最好的一种仍比 1×1 慢 4.1×**，且 **ocb 越大越糟**（8 比 4 慢一倍）→ 按规则回退，
  回退后复现实测 7.158620 / 7.173225 ms 与 Phase 21 一致，`ctest` 3/3；
  **复现已补原始转录** `docs/evidence/phase23/postrevert-verify.txt`（两次 fresh-ledger run
  `kernel_ms=7.158920 / 7.127580`，`verify=PASS max_rel=0.000000e+00`、`migrations=0 checks=6`、rc=0），
  连同 Phase 21 的三读数共 7 次，全落在 7.1276–7.1750 ms。
- **正确性一条没丢**：所有微块运行 `max_rel` 依旧 `0.000000e+00`——微块确实只改复用度。
  所以这不是"改错了"，是"改对了但更慢"：一个纯性能决定，按预登记门槛判负。
- **归因（未分离，不当结论写）**：2×2 把每线程活跃累加器抬到 16–32 个 float，块内线程数
  从 `tile²=256` 掉到 `(tile/2)²=64`（每 CU 常驻 warp 变少 → 延迟隐藏变差），且 px 循环内
  相邻线程以 C 个字为步长访问 LDS（每个 `(c,kh,kw)` 步发 4 次 LDS 取值）。与"ocb 越大越糟"
  的形状吻合，但没有 profiler 证据把三者拆开。**下一手该先解决 occupancy 与 bank，
  不是继续加大微块。**
  **（Phase 24 更正：其中 bank 这条腿已被 A/B 判负——`C→C+1` padding 后 256² 纹丝不动、128² 更慢，
  见本文件 Phase 24 段。三条里现在只剩 occupancy 未被检验，且权重取指令数 > FMA 数这条读码事实接管了"下一手"。）**
- **`[R20]` 的字节钉住在真实改动上响了**：微块一落地 `ctest` 从 3/3 变 2/3
  （`count_of("acc[o] += xv * Wt[") == 2` 与嵌套字面量都不认），回退后自动复原。
  这条臂的作用被一次非人为注入的变更验证了一次——比我自己设计的摘除控制更有说服力。
- **又一次自记的坑（第三次）**：首版扫描用 `grep -oE "max_rel=… kernel_ms=…"` 抽取，
  两个 token 在结果行里并不相邻，于是转录只剩 rc 行——**过滤式转录必须在写入后确认里面
  真的有数**，否则它就是一个看起来很整齐的零结果。
- **回退的字节账（事后复采，2026-10-28）**：回退后重新发射的 conv HIP 程序是 **508 行**，Phase 21 记的
  是 506，三个 conv 形状（256²/128²/32²）都发 508 行 → 行数与形状无关，这 2 行是真实增量。
  微块代码本身零残留（`micro|2x2|px|PY|subtile` 全文 0 命中），增量定位在 env 旋钮分支：
  `src/codegen_hip.cpp:463` 的 ` source=env` 后缀与 `:412` 的 `if (p.tile == 0 && p.smem != 0) p.smem = 0;`，
  前者在 `docs/evidence/phase21/tiled-sweep.txt` 出现 **0 次**、在今天的发射里出现 1 次（token 计数，
  不是记忆），是"这两行晚于 Phase 21"的直接证据。**所以"逐字节回到 Phase 21"这句不成立**，
  成立的是"行为回到 Phase 21（7.158620/7.173225 ms、`conv_plan ocb=8 tile=16 smem=41472`、max_rel=0）
  ＋ 保留 2 行仍然有用的诊断"。
  **固化规则：凡声称"回退"，必须留下可 diff 的产物而不只是行数——已把当前发射入库为基线
  `docs/evidence/phase23/conv-hip-postrevert.txt`（508 行，sha256=8464d86c…42bb28）。**
  同族教训：`conv_tile_skip` 在 Phase 21 转录里 0 次命中并不代表代码里没有它——
  **告警式打印在转录中的缺席只能证明没开火，不能证明不存在**（这条正是我自己记过的"零读数陷阱"）。

## Phase 24 LDS bank 冲突：假设被 A/B 否证，以及"改 smem 就会改选档"（2026-10-28）

- **被否证的正是我上一轮写下的归因**：Phase 23 我把微块失败记成"occupancy 与 bank"。本轮单独动 bank 那条腿
  （`uk_sX` 每像素步长 `C → C+1`，padding 槽不参与算术）后，256²（C=32，模型上 32 条 lane 命中同一 bank）
  的 `kernel_ms` 是 **7.135740 / 7.145340 ms**，与未加 padding 的 7.1276–7.1750 带重合；128²（C=16）
  **0.302160 / 0.295439 ms 反而比 0.287 慢 2.9–5.3%**。**所以在这张卡上这个内核里，LDS 同 bank 冲突不是限制因素**；
  "32-way 冲突"是我从 wave32 + 步长推导出来的模型，它没有换来任何可测效应——模型不成立或被别的东西掩盖，
  本机没有 profiler（`rocprof`/`rocprofv2`/`rocgdb` 不在 PATH，`therock/gfx103X-7.13.0/bin` 里也没有），
  无法再往下拆，只能把这条腿从归因表里划掉。
- **真正耐用的发现是选档器与 smem 的耦合**：`uk_conv_plan` 用
  `threads >= per_cu * dp.multiProcessorCount` 接受候选，而 `per_cu` 来自
  `hipOccupancyMaxActiveBlocksPerMultiprocessor(..., p.smem)`。把步长改成 `C+1` 让 32² 的 tile=8 smem
  从 3200 → 3600，可常驻容量下降，谓词由假变真，**选档从后备路径 `tile=0` 跳到 `tile=8`**，
  实测 0.016357 / 0.020372 / 0.015565 ms，比原来慢约 1.5×。含义：
  **凡是改 smem 公式的布局实验，都会顺带改选档结果**，"只改布局"这个前提在 tiny 形状上不成立；
  下一轮同类实验必须先用 `UK_CONV_TILE` / `UK_CONV_OCB` 把方案钉住再计时（旋钮本来就在，这轮是我没用它做对照）。
- **谓词方向反直觉，值得单独记**：smem 变大 → `per_cu` 变小 → `resident_capacity` 变小 →
  **更容易判成"填满设备"**。所以"装得下"与"填得满"两条轴在网格很小的形状上会互相拉扯，
  这是 Phase 21"必须联合搜索"教训的机制版。
- **回退的举证方式升级**：Phase 23 的回退只能说"行为一致"（发射程序 506→508，2 行是事后才定位的诊断）；
  本轮因为那天把发射结果入库成了基线，回退后用 `diff` 空输出 + `sha256` 相等直接证明
  **逐字节相同**（`lines=508`，`8464d86c…42bb28`），并复现 `kernel_ms=7.152313 / 7.152326`、`ctest` 3/3。
  **固化规则：任何"可回退"的改造，动手前先把产物与摘要存成基线，回退的举证成本会从"说不清"降到一条 `diff`。**
- **`[R20]` 字节钉住的第二次真实检验**：padding 版让 `tests/acceptance.cpp:2672` 的 `nest_tile` 当场变红（Phase 24 当时是 :1682），
  我是**先让旧钉住红、再决定去留**（不是先改断言迁就实现），回退后自动复原 3/3。
- **下一手的靶（只是靶，不是决定）**：内循环每步实际是 **1 次 LDS 取值 + 8 次 `Wt` 全局（L2）取 + 8 次 FMA**，
  取指令比 FMA 还多；本块权重只有 `ocb*Kh*Kw*C = 2304` 个字（≈9 KB）。
  要动的是**权重的放置**，而不是继续调 X 的布局——这也顺手解释了为什么 Phase 23 提高 X 复用度会失败：
  它增加的是每步的 LDS 取值数，而每步的取指令大头本来在权重那边。

## Phase 25 occupancy 与设备共享内存事实（2026-10-28，零代码改动的测量相）

- **设备自报的共享内存事实**（`probe/phase25_smem_probe.cpp`，独立程序、不引 unikern 头，
  两种模式各 7 档全 rc=0，`docs/evidence/phase25/smem-preflight.txt`）：RX 6600M / gfx1032 / ROCm 7.1 报
  `sharedMemPerBlock = sharedMemPerMultiprocessor = maxSharedMemoryPerBlockOptin = 65536`、
  `multiProcessorCount=14`、`warpSize=32`、`maxThreadsPerMultiProcessor=2048`、`regsPerBlock=131072`。
  occupancy 的 `per_cu`：**请求 12800 B → 5 块/CU；≥41472 B → 1 块/CU**。
- **由此否证一条我自己写下的平台断言**：Phase 21 计划里写"48 KB 默认上限，需
  `hipFuncAttributeMaxDynamicSharedMemorySize`"。实测**动态请求 65536 B 在整个进程一次 setattr 都不做的情况下
  launch+sync 成功**（`--noattr` 模式）。那句是从通用 CUDA/HIP 常识搬来的，不是从这台机器取的 → 已登 E26，
  并在 Phase 21 表行就地划掉。
- **现役最优方案的真实驻留度**：`tile=16 / C=32` 每块 41472 B（= 每 CU 64 KB 的 63%）⇒ **每 CU 只常驻 1 块
  = 256 线程 = 线程槽的 12.5%**。这个数字本身就是"为什么慢"的候选解释，所以下一条是它的判决。
- **occupancy 解释被判负**（256²×32/Oc64，八组用 `UK_CONV_TILE`/`UK_CONV_OCB` 钉住，
  `conv_plan` 行均带 `source=env`；全部 `max_rel=0.000000e+00`）：
  tile=16（1 块/CU）= 7.129367 / 10.514896 / 18.079030 / 31.571097 ms（ocb=8/4/2/1）；
  tile=8（5 块/CU、320 线程/CU）= 9.638396 / 13.543800 / 20.141341 / 31.416699 ms。
  **把常驻块数从 1/CU 抬到 5/CU 换来的是慢 11–35%**（ocb=1 时差值在散布内）。预登记的保留线
  `≤ 5.97 ms` 八组无一达到。合理解释（未反证，只作记录）：tile=8 的 halo 冗余更高
  （`10×10/8×8 = 1.5625` 像素/输出 vs `18×18/16×16 = 1.2656`），且每块只有 2 个 wave。
- **三条解释至此全部关闭**，都不许再当"为什么慢"用：X 的复用度不足（Phase 23：2×2 微块慢 4.1×）、
  LDS bank 冲突（Phase 24：padding 无效应、128² 更慢）、常驻块数/occupancy（本轮：更块小换 5 块反而慢）。
  **唯一还活着的强信号是 OCB 单轴本身**：同一 tile 下 ocb 1 → 8 快 **4.43×**（31.571 → 7.129 ms），
  且随 1/OCB 呈次线性——每线程每步是 1 次 LDS + 8 次权重取 + 8 次 FMA，把 FMA/取数比再抬高需要
  **改权重的放置**（进 LDS/寄存器）或换算法（完整隐式 GEMM），tile/ocb 这两个旋钮已经榨干。
- **选档规则在更宽的面上再次成立**：不钉旋钮的对照组 `conv_plan ocb=8 tile=16 smem=41472 fills=device`
  实测 `kernel_ms=7.150933`，正是八组里的最优。这是 Phase 21 联合搜索规则的第二次独立确认
  （第一次是 Phase 23 的微块实验之前）。
- **读到一条关于自己代码的事实，纠正一处过满的注释口径**（E26）：发射程序里 `uk_conv_plan` 有**两处**调用
  ——verify 段用内嵌小校验实例（C=8，所以它的 `conv_plan` 行打 `smem=10368`），计时段用真实形状（C=32，
  `smem=41472`）。于是"一个 pass 不可能用与校验不同的方案发射"（`conv-hip-postrevert.txt:249-250`）
  说的是发射函数单一，**不是方案单一**。它不构成正确性漏洞，因为真正的保证是
  **所有 (tile, OCB) 方案逐位相同**（本轮 9 条 `max_rel=0` 加上 Phase 20–24 的全部读数）。
  注释要按这个口径重写；改注释会动发射字节，所以必须与"重录 `sha256` 基线"绑成一个动作，别分两次做。

## Phase 26 验证覆盖缺口：默认流程校验的不是被计时的实例化（2026-10-28，已修）

- **事实（直接运行发射程序取到的，`docs/evidence/phase26/README.md`）**：修复前 256²×32/Oc64 的 conv 程序里
  两处 `uk_conv_plan` 打出不同方案——verify 段 `ocb=1 tile=0 smem=0 nothing fills the device`（内嵌 32×32×8 实例，C=8），
  计时段 `ocb=8 tile=16 smem=41472 fills=device`（声明形状 C=32）。
  即被差分校验的是 `uk_conv_g<1>`，被计时的是 `uk_conv<8>`；**`max_rel=0.000000e+00` 在被修好的流程里
  才第一次成为"关于被计时的那个实例化"的证据**。
- **代价的诚实口径**：Phase 20/21/23/24/25 的分块内核逐位读数是真的，但它们全都来自**我把
  `UK_CONV_TILE`/`UK_CONV_OCB` 钉住**的扫描（钉住后两段自然选到同一方案）；默认（不钉）流程从未校验过分块路径。
  这不是"以前错了"，是"**以前的证据范围被写得比实际范围大**"。
- **一条断言为什么不够**：`[R20]` 早就有"单一发射点"（`count_of("<<<") == 8`）这条腿，它保证的是
  发射函数唯一，**不保证方案唯一**——缺口正是从这两者的缝隙里活下来的。所以新腿按计数钉：
  `uk_conv_plan(` == 2（定义 + 唯一调用点）、`uk_conv_launch(uk_plan,` == 4、
  `uk_conv_launch(uk_plan, VoW` == 1（verify 那一条），并断言 `uk_conv_launch(plan,` 不存在。
  三条摘除控制全红（`:1694`/`:1697`），复原 byte-identical，末轮 `ctest` 3/3。
- **改动的形状可核对**：发射程序 508 → 517 行，`diff` 恰 **+21 / −12** 行，全部落在这一处设计变更
  （dims 与 `uk_plan` 提到 verify 之前、两处 per-pass 推导删除、注释改口径），无顺手编辑；
  gemm 447 / reduce 239 行未变。真机 `kernel_ms`：256² 7.160367（带内）、128² 0.284069、32² 0.012980，
  gemm 512³ 0.123837、reduce 1048576 0.164954，全 `max_rel=0.000000e+00`、rc=0。
- **上下文教训（第二次记）**：同一个二进制**单独跑**读到 `kernel_ms=7.024740`，比经 runner 跑的 7.160367
  低约 1.9% ⇒ **带内比较必须同上下文**（runner-vs-runner），跨上下文的数字不能互相判回归/改进。
- **又踩 E22（第四次）**：reduce 首次用 `--dims 1048576` 单参数调用，CLI 打 usage 文本，被 grep 过滤后
  转录里静默少了一格；补录用正确形式 `--dims 1048576 0 0`，并把失误留在转录里。
  **同类教训：`pipeline exit=$?` 打的是管道尾端的码，不是 CLI 的**——过滤式采证时别把它当被测命令的退出码。
- **新基线**：`docs/evidence/phase26/conv-hip-single-plan.txt`（517 行，
  `sha256=161d8e190da1286454e8429ea366b939bfcce54fe0c6e73890278ac92df27f3f`）取代 Phase 23 的
  `conv-hip-postrevert.txt`（508 行）；旧文件保留并标历史时点，符合"转录不改写"的既有约定。

## Phase 27 OCB=16 外推：幂律在 8 之后断裂，且断裂方向随 C 变号（2026-10-28，负结果已回退）

- **本轮测的是唯一还有量化支持的规律**（Phase 25 钉住矩阵 tile=16，256²：ocb 1/2/4/8 =
  31.571097 / 18.079030 / 10.514896 / 7.129367 ms，局部指数 β = 0.803 / 0.781 / **0.561**，递减）。
  悲观到乐观的外推都写进了预登记：β=0.561 → 4.85 ms、β=0.4 → 5.40 ms、β=0.25 → 6.02 ms（过不了 5.97 的线）。
- **实测（`docs/evidence/phase27/ocb16-sweep.txt`，全 `max_rel=0.000000e+00`）**：
  256²（C=32）`ocb=16` = **7.858002 / 7.854852 ms**（两跑差 0.003 ms，重复性极好）对 `ocb=8` = 7.160519
  ⇒ **慢 9.7%**，判负走分支②（与 Phase 23 微块同签名：活跃累加器 8→16 开始付寄存器代价）。
- **但真正的新事实是符号变号**：128²（C=16，Oc=32）`ocb=16` = **0.266870** 对 `ocb=8` = 0.288485
  ⇒ **快 7.5%**。所以
  **"越复用越好"不是普遍规律，最优 OCB 是 (C, Oc) 的函数**——`uk_conv_plan` 里那条
  "取仍能填满设备的最大复用档"的单调偏好，其前提在本机数据上第一次出现反例。
- **为什么仍然整档回退**（三条独立理由，一条就够）：① 预登记的保留线绑 256²，未达；
  ② 自动档在 256² 会选中 16（7.833645，比该选的 8 慢 9.4%）⇒ 保留等于把错误偏好带进 shipping 路径；
  ③ 要用 16 就得有一条按 C 的规则，而证据是**每个 C 各一个形状的两个点**——本项目的选择规则必须
  由设备自报量推导（`findings.md` Phase 20 段第 604 行），两点定不出这种规则。
- **回退举证（第三次用这条口径）**：发射程序与 `docs/evidence/phase26/conv-hip-single-plan.txt`
  **`diff` 空**、517 行；生成器 `uk_conv<16>|uk_conv_g<16>|step < 5` 全文 0 命中；`ctest` 3/3；
  真机 `postrevert-confirm.txt` 复现 256² **7.156521**、128² **0.284850**，选档器重新落在 `ocb=8 tile=16`。
- **下一手是统计量，不是新想法**：固定 tile=16，扫 `C ∈ {8,16,32,64} × ocb ∈ {8,16}`，每组合 ≥3 跑，
  找符号翻转发生在哪个 C、是否与 `Oc/C` 一致；那才有资格谈"按 C 分档"的选择规则。
  conv 现役读数不变：256² 337.5 GFLOPS、128² 524.9 GFLOPS（距同机 gemm 2275 尚有 6.7×/4.3×）。

## Phase 28/29 曲线否证了"按 C 选档"，并暴露校准旋钮绕过设备检查的真缺陷（2026-10-28）

- **一条曲线杀死一条规则**：Phase 27 的两点（C=32 慢 9.7%、C=16 快 7.5%）足够编出
  "ocb=16 当 C ≤ 16"这样的选档规则。把 C 轴测成 4 点后（tile=8，Oc=64，256²，每格 3 跑、极差 ≤2.21%，
  全 `max_rel=0.000000e+00`）16 相对 8 的增益是
  **C=8 +23.4%、C=16 +22.0%、C=32 −9.0%、C=64 +11.5%** ⇒ 符号 `+ + − +`，**非单调**。
  所以不存在能用 C 表达的翻转点，Phase 27 的猜想被自己的数据否证；
  诚实的表述是"**最优 OCB 取决于 (C, Oc, tile, 网格大小) 的联合作用，机制在本机未定**"。
  按预登记的门槛 2 不通过 ⇒ **生成器候选集保持 `{8,4,2,1}`**，一条也没加。
  （tile=16 曲线在 C=64 天然断一格：82944 B > 每 CU 65536 B，这是硬件边界，不是缺陷。）
- **仪器的失败标签必须区分"数据差"与"数据缺"**：脚本第一版把两者合并成同一个 `spread gate = FAIL`，
  于是 C=64 的缺格被读成噪声。第二版分开报告（`sweep-tile8.txt` 尾行 PASS）。
- **Phase 29 是被这次测量暴露的真缺陷，不是重构**：`UK_CONV_TILE=16` 在 C=64 上打出
  `conv_plan … smem=82944 source=env` 然后 `UNIKERN_ERROR what=launch verify msg=invalid argument`、rc=4。
  根因：**env 分支在 `hipGetDeviceProperties` 之前就 `return p`**，钉住的方案完全绕过自动路径的
  `smem > dp.sharedMemPerBlock` 检查 ⇒ 失败点离原因很远，日志里既没有旋钮名也没有被越过的上限；
  更危险的是这类格在只看 `kernel_ms` 的脚本里会被当成噪声丢弃或（更糟）当成有效读数。
- **修法与它守住的性质**：env 分支补上**同一条**判据（不另立标准），越限则响亮拒绝该 pin
  （请求字节、设备上限、"改走全局路径、OCB pin 保留"），并把 **ERROR 行排在 `conv_plan` 行之前**，
  这样扫描脚本先读到原因再读到"调整后的配置"。`hipGetDeviceProperties` 失败同样保守退回全局路径。
- **三条对照里最重要的是"不许误报"**：C=64 钉 tile=16 → 拒绝且仍能跑（41.817249 ms）；
  C=64 钉 tile=8（25600 B 装得下）→ **无 ERROR**（41.358890 ms）；C=32 钉 tile=16 → 行为不变（7.050036 ms，带内）。
  **"总是拒绝的检查"与"从不拒绝的检查"一样没用**，第二条才是这次修改的主要防误报证据。
- **门禁能分别触发**（`probe/phase29_knob_arm_probe.py` rc=0，两条腿各自唯一负责）：
  改名拒绝 token → 计数腿红；在检查之前多打一条含 `source=env` 的行 → **计数腿仍绿、只有顺序腿红**
  （`[R6.6]` `tests/acceptance.cpp:1712`）。复原 byte-identical，`ctest` 3/3。
- **基线换代（第 4 次执行这条口径）**：`docs/evidence/phase29/conv-hip-knob-check.txt`
  （542 行，`sha256=599a4e7183982b7101ed9441a8582bec59f9ae23bf450dffc8323a2bf79e3f18`），
  对 Phase 26 基线 `diff` 恰 **+25 / −0**。过程中还纠了自己一个错：第一次存的是**从 C=64 KML 发射**的产物
  （头部 `dims=` 与 `const int C` 两行不同，`diff` 出现 −2），基线必须与既有基线**同源**才可比，
  已按同一 C=32 KML 重发。**教训：入库的"基线"要绑定产生它的输入；否则 diff 里混进形状差异，
  下一次回退举证就不可信。**
- gemm 447 行 / reduce 239 行未变（跨算子无回归）。

## Phase 30 reduce 首次归因：串行 pass-2 才是主项，且宽度受差分检查约束（2026-10-28，未改代码）

- **归因（`probe/uk_reduce_width_probe.cpp`，折叠次序与发射内核逐字一致，6 个 n×nP 组合都先报 `rel` 再报时间）**：
  现役 `nP=4096`、n=2²⁰ 时 `part=0.0469 ms / sum=0.1079 ms` ⇒ **`sum_share=68.2%`**。
  我在预登记里写的是"<30%"，**方向预测错**：主项是**一条线程串行折叠 4096 个部分和**，不是 pass-1 欠并行
  （pass-1 也确实欠：`part_GBps` 89.5→315.6 从 4096→16384，比值 3.53）。
- **√n 律被自己的第四个数据点否证**：`T ≈ a·n/nP + b·nP` 给出 `nP* ∝ √n`，锚在实测最优的现役 4096 后
  预测 4096/8192/16384/32768；实测最优是 4096/4096/16384/**16384**——2²⁶ 处 32768 比 16384 **慢 38%**。
  机制：pass-1 的时间随 nP **非单调**（2²⁶：1.4317@16384 → 1.2665@28672 → 1.6110@32768），
  所以"和 pass-2 的线性项平衡"只在饱和之前成立。**结论：任何把上限写成 √n 的规则都会在 2²⁶ 上选错。**
- **本轮最有用的一条（此前无任何文档写过）**：n=2²⁶ 时**只有非 2 的幂的 nP 出现偏差**
  （14336 → `rel=−1.602e−04`，28672 → `−1.373e−04`；而 1024/2048/4096/8192/16384/32768 全档 `rel=0`）。
  机制是算术：全域 2⁻⁶ 时部分和是 `k/64`，`k` 非 2 的幂 ⇒ 折叠到 2²⁰ 量级需要 >24 bit 尾数 ⇒ fp32 舍入。
  ⇒ **启动宽度被差分对拍本身约束**：宽度不只是性能旋钮，它决定 `max_rel` 是否为 0
  （偏差量仍在 2⁻⁵ 容差内，账本会照收——也就是说这个退化**不会被门禁拦住**，只能靠口径写清）。
  **固化规则：凡是会让"对拍数据的精确性"依赖启动参数的改动，必须显式加一条非幂档的开火控制。**
- **由此两条"设备自报量"路线实测出局**：满载 `2048×14=28672` 慢 8.7%（且破坏精确性），
  `4 块/CU×256×14=14336` 与 16384 打平但同样破坏精确性；
  只剩"本机拟合常数 16384"能把四个点说对（收益 n=2²⁴ **1.67×**、n=2²⁶ **2.49×**，n ≤ 2²² 不退步 ≤1.05%）。
- **因此不动生成器**，升为 **D9**（等用户定口径）；并行化 pass-2 能从根上消掉这个取舍，
  但它改变最终和的折叠次序（AGENTS 第 3 条）→ 与 D8 同类，同样不擅动。
- **仪表自校先于读数**：探针在每个 n 上**先**打印 `result` 与 `rel`（期望值 n/64 提前算好），
  6 个组合里 4 个 `rel=0`、2 个非幂档 `rel≠0` —— 正是这个"预期数与读数的对照"让精度退化没有变成暗雷。
- 现役数字未变：reduce 1048576 端到端 `kernel_ms=0.164954`、`verify=PASS max_rel=0`；
  gemm 447 行 / reduce 239 行 / conv 542 行均未动。

## Phase 31 reduce 对拍的结构性盲区（量级 + 进臂，2026-10-28）

- **盲区是读码读出来的**：发射程序里差分验证跑**固定 2²⁰ 实例**，而折叠宽度**当时**来自声明的 L
  （Phase 30 的口径；D9 之后改由设备自报量在运行时推导）⇒ `UNIKERN_RESULT` 的 `max_rel`
  描述的是那个小实例，**不是计时规模**。今天这两个发射点是
  `Differential`（`src/codegen_hip.cpp:916`）与 `UK_REDUCE_WIDTH`（`src/codegen_hip.cpp:880`）；
  Phase 30 原文写的 840/829 是当时的位置，已降级为文本、不作活引用。
- **网格实测**（`docs/evidence/phase31/fold-exactness-grid.txt`，18 格，仪器先报 `rel` 再报时间）：
  现役宽度 4096 在 `n=2²⁴/2²⁵/2²⁶` 上 `rel=0`；`n=2²⁶+4096` 上 `rel=−4.577e−05`；
  非幂宽度 14336 在 `2×10⁷/2²⁵/2²⁶` 上 `rel=+1.154e−04/−3.052e−05/−1.602e−04`；
  3072 在 2²⁴ 上是 0、在 2²⁵ 起非 0。**所有非零值与门限 2⁻⁵ 差 ≥195 倍** ⇒ 无门禁可红。
- **判据被数据纠正了一次**（重要）：我原先以为触发条件是"`n` 不被 `nP` 整除"。
  实测 `n=2²⁶+4096` 与 4096 是**整除**的（67112960/4096 = 16385）却 `rel=−4.577e−05`
  ⇒ 真正的判据是**每个线程的 chunk 是否为 2 的幂**，与整除性无关。
  这条只有在把 `n` 推到计时规模才看得见——验证实例（2²⁰）里两种情况都是 0。
- **机制不需要真机**：CPU 侧按同样次序折叠 fp32 复现到最后一位
  （4096×256.015625 → `4.57736e−05` 对真机 `−4.577e−05`；14336×73.140625 → `1.60222e−04`
  对真机 `−1.602e−04`；4096×256.0 → 0）。所以这是 fp32 算术事实（总值到 2²⁰ 量级时量子为 2⁻⁵，
  数据步长 2⁻⁶，非 2 的幂的加数会在某一步丢掉低位），**不是驱动或硬件的怪异**。
- **进了测试套件**：`[R6.5-blind]`（`tests/acceptance.cpp:1634/1639/1647`；Phase 31 当时是 :1454/1459/1467）三条可独立失败的断言——
  幂 chunk 必须逐位精确、非幂 chunk 必须不精确且 `rel<1e−4`、两者都必须低于门限的 1/100
  （最后一条把"盲区是结构性的、不是阈值可调的"钉住）。`probe/phase31_arm_probe.py` 三条摘除全红、
  复原 byte-identical、`ctest` 3/3。**臂是纯 CPU 的**：不需要真机、不占设备时间、改容差会立刻移动它。
- **对两个未决项的影响（都如实登记，不越权代决）**：
  D8 多了一条同向事实（不只是"CPU 参考更不准"，还有"验证实例的规模让折叠误差根本不出场"）；
  D9 多了一条**硬约束**（候选宽度必须让 chunk 为 2 的幂，否则连"验证实例恒精确"这层保护都会
  变成对拍数据规模的偶然）。**本轮未动验证实例规模、容差、宽度公式。**
- 现役读数与产物规模不变：conv 542 / gemm 447 / reduce 239 行；reduce 1048576 `kernel_ms=0.164954`。

## Phase 32 文档完整性进了 `anchors` 门（表格列数），首跑抓出自己造成的三条缺陷（2026-10-28）

- **为什么要扩门**：同一天里我用"长单行表格行的**前缀**"当 Edit 锚点撕开过三行文档记录（E27）。
  `path:line` 有机器核对，表格结构没有——而本项目把工作记忆当交付物读，
  **一张渲染错列的表就是错的索引**。
- **新增检查**（`probe/anchors_check.py` 的 `table_shape_problems()`）：以"表头 + 分隔行"为基线记下列数，
  往下逐行比较，直到非 `|` 开头的行为止；跳过 ``` 围栏内部（转录里有管道字符）。
  **关键实现坑**：GFM 里代码段内的裸竖线**也是**分隔符，所以计数必须忽略转义竖线——
  不处理这条，E3 那行（键内含转义竖线）会被误判成 5 列，一个会误报的门进不了 ctest。
- **首跑抓出的三条，全是真缺陷**（不是误报）：
  ① Phase 27 表行的 `uk_conv<16>`…`step < 5` 之间有裸竖线 → 该行 4 格 vs 表头 3 格；
  ② D8 行里绝对值写法两侧裸竖线 → 5 格 vs 3 格；
  ③ G5 缺口行的正文被写在三个物理行上 → 渲染成 1 格（表头 2 格）。
  修法分别是转义、转义、并线。**这就是"门第一次跑就值钱"的又一例**（与 Phase 22 首跑抓出 4 条失效锚点同类）。
- **门自己能红已验**：给 Phase 31 行故意补一格 → `rc=1` 且点名 该行第 172 行；
  复原 byte-identical → `rc=0`；`ctest` **3/3**。**规则不变：进 ctest 的检查必须双向验证**
  （会误报的与从不红的同样无效）。
- 现在 `anchors` 门一次跑核两件事：**93 条 `path:line` 锚点 + 全部规划/证据文档的表格列数**。

## Phase 33 只测 2 的幂宽度：现役 4096 也会偏，"宽度保证精确"这条说法被否证（2026-10-28）

- **对 Phase 31 的收窄（同一份文档里的演进标注，不改写原文）**：Phase 31 说"全部 2 的幂档 `rel=0`"，
  那是对**所测的 2²⁴/2²⁵/2²⁶ 与两个 +k 变体**的陈述；把它推广成"2 的幂 ⇒ 精确"是我说宽的。
  Phase 33 只测 2 的幂宽度就否证了它：**n=5×10⁷ 时 2048/4096/8192/16384 四档全部 `rel≠0`**，
  包括出货在用的 4096（`+5.44e−05`）。`[R6.5-blind]` 的三条断言不受影响
  （它们只声称"存在精确的、存在不精确的、两者都远低于门限"，这三件事依然成立）。
- **精确性不是宽度的单变量函数**：3×10⁷ 时 2048/4096/8192 精确而 16384 偏 `+2.41e−04`；
  2×10⁷ 时反过来是 2048/8192 偏、4096/16384 精确。⇒ 需要 `(n, nP)` 二维网格才说得清边界，
  **本轮不再外推成法则**（越过 `n = 1.678×10⁷` 之后只能逐点测）。
- **速度侧结论干净**：`nP=16384` 在全部 8 个 n 上都是最快档，对现役 4096 分别
  **1.65 / 1.65 / 1.80 / 2.04 / 2.06 / 2.31 / 2.49 / 2.50 倍**（随 n 单调增大）。
- **16384 不是拟合常数**：它可由设备自报量推导 —— `maxThreadsPerMultiProcessor(2048) × cu(14) = 28672`，
  向下取 2 的幂 = **16384**，与实测最优同值。⇒ D9 的"常数来源"这一维被拆掉，剩下的只是纯取舍。
- **交付形态**：按预登记的分支 2，**不动生成器**；把 D9 从"要不要抄拟合常数"改写成
  "要不要用 1.65×~2.50× 换一个从未验过的宽度（精确性不站在任何一边）"，并把 G10 的说法改准确。
  （初稿写成"已出货的宽度在某些规模上就不精确，而验证看不见"，那暗示已入库证据被污染；Phase 34 读码核对：
  计时段的 `Y` 拷回主机后**没有任何代码使用**（发射点 `hipMemcpyAsync`（`src/codegen_hip.cpp:946`）），`max_rel` 只来自 2²⁰ 验证段
  ⇒ 准确表述是"**该规模的结果根本不在验证范围内**"——**覆盖缺口，不是证据错误**。）
- **又一次自记的坑（E22 族第 6 次）**：网格首轮 `mkdir` 没建输出目录 → 重定向失败、一条读数都没采到。
  先建目录、再采、写完后确认文件里真的有数（本次 `rows=32 nonzero_rel=14`）。

## Phase 34 折叠误差的模型（41 格全中）+ 收回上一相写宽的 G10 说法（2026-10-28）

- **模型**：部分和 `= k_t · fill`，`k_t = floor((n-1-t)/nP)+1`，`k_t < 2²⁴` 时**精确**
  ⇒ 其余误差全部来自 pass-2 的**串行 fp32 累加**，因此可以在 CPU 上逐格算出 `rel`。
- **对账：41 格真机读数 `mismatch=0`**（Phase 28/30/31/33 记录过的全部 `rel=`；零/非零一致，
  量级在 5% 内——转录只打印 4 位有效数字）。副产物：真机 pass-2 若被编译器重结合，
  模型不可能逐格对上 ⇒ **它没被重排**。
- **可预测性的实际价值**（不是玄学）：给定 `(n, 宽度, 填充)` 就能在跑之前说这格精不精确；
  Phase 33 那些"反直觉格"（`5×10⁷` 四档全偏、`3×10⁷` 只有 16384 偏、`2×10⁷` 只有 2048/8192 偏）
  全部被同一个模型解释，无需分案例。
- **臂 `[R6.6-model]`**（纯 CPU）：19 格实测三元组 + 两条结构断言（2²⁰ 验证实例在任何可选宽度下精确；
  模型最坏偏离 `2.40667e−04` 远低于门限）。摘除控制三条各自变红（`[R6.2/R6.4]` `tests/acceptance.cpp:1576`）。
- **本轮最该记的方法学**：**腿 2 的第一版是绿的，而且"绿得有道理"**——把填充换成 `0.5f` 只是
  按 2 的幂整体缩放，fp32 的**相对**舍入不变 ⇒ 那条控制什么都没测。换成非 2 的幂 `0.1f` 才红。
  **"控制能跑"与"控制能失败"是两件事；写控制的时候要问"如果实现是错的，这条会红吗？"**
- **收回一处上一相写宽的话**（六处就地改口 + v27）：G10 不是"账本会安静收下带 fp32 折叠误差的结果"。
  读码：计时段的 `Y` 拷回主机后**没有任何代码使用**（发射点 `hipMemcpyAsync`（`src/codegen_hip.cpp:946`）），
  账本记的是 ε 与时间，`max_rel` 只来自 2²⁰ 验证段 ⇒ 准确表述是
  "**该规模的结果根本不在验证范围内**"——**覆盖缺口，不是证据污染**。
  模型与臂不受这次改口影响（它们只声称"误差可算"，没声称"已入库证据错了"）。
- **又一次自己踩坑并被抓**：把 Current Phase 的段落误插进 D9 表格行，行被撕成 8 个物理行——
  **本相刚扩出来的 `anchors` 门（表格列数检查）当场报红**，按行合并复原（cells 校验通过后才写盘）。
  这是 E27 那一类的第 7 次；差别是这次**从犯下到发现只隔一条命令**。

## Harness 复核事实（2026-10-28，Phase 17 / better-harness）

- **证据采集口径**：`harness evidence-bundle --depth normal --language zh-CN`（窗口
  2026-09-28→2026-10-28，evidenceLimit 5）返回 `status=complete`，三道（sessionEvidence /
  projectHarness / agentCustomize）与 lead 全部 `available`，`incomplete/partial/unavailable`
  均为空 → normal 模式不阻断。CLI 的 node 所有者：Qoder Electron 二进制 +
  `ELECTRON_RUN_AS_NODE=1`（PATH 上的 node 是另一版本，按技能要求不自选运行时）。
- **会话道为"不可达"而非"不存在"（关键辨析）**：封套报 `eligibleSessions=0` 带
  `missing-optional-root`/`disabled-source-root`；`session-analysis sources` 显示的默认提供者
  主目录是 `.qoder`，该主目录下根本没有本工作区的项目存储根。本机 Qoder CN 构建实际写入
  `.qoder-cn`，其中本工作区会话存储与日志俱在（1 个 JSONL 约 313 KB、事件日志约 701 KB），
  以显式主目录覆盖参数重跑两侧 `exists:true`、`sessions:1`——而该唯一会话就是本次复核自身
  （0 个可采信 Task Episode）。同一主目录口径使 Memory 标题级复核覆盖 0 条
  （`scanned-empty`/`titleCount=0`）而 `inventory.summary.memories=1`。`disabled-source-root`
  已排除为干扰项（加 cache/global 枚举后仍 0）。结论：行为类断言一律保持 unobserved，
  不得写成"本项目没有 agent 历史"。
- **资产面实测**：`projectAssets=0`、`entrypoints=0`、Rules/Hooks/Plugins 无 coverage 行；
  coverageRows 仅四行且 scopes 全 Global——Skills 10 / Commands 2 / Custom Agents 158 / MCP 8。
  与 `git ls-files`（162 文件，无根 README/AGENTS.md/QODER.md）互证。
- **文档↔代码锚点抽验 4 处，3 处失效**（Harness 复核当场核对，时点 2026-10-28 17:00；
  下列**历史**行号按当时的文件状态记录，故意不写成 `path:line` 形式——那种形式是
  "现在也能跳过去"的活断言，已由 `probe/anchors_check.py` 机检）：
  `docs/acceptance-mvp.md` R6 段当时称 `verify_fp32` 在 runtime.cpp 第 95 行，实测在第 151 行；同段称
  LEDGER COMMIT 门在 main.cpp 第 274-282 行，实测在第 433 行；R9 段把 `missing required fields:`
  归到 main.cpp 第 110 行，该串实际在 hcs.cpp 第 95 行（main.cpp 那一行是无关的 op/dims 判断）。
  **同一批锚点在几小时内又漂了一次**：本轮我在 `src/runtime.cpp` 加了违规记录函数、在
  `src/main.cpp` 加了 R8.5 消费方之后，`verify_fp32` 已到第 182 行、LEDGER COMMIT 已到第 529 行、
  `HCS rejected` 已到第 185 行——三个数字全部由 `probe/anchors_check.py` 现取并据此改准了文档。
  一次改动就重排十几次行号，这就是锚点必须机械化校验的直接理由。
  正向对照同样成立：docs/ 引用的 32 个 evidence 路径全在、66 个证据文件全被 README 提及——
  失效的只有代码行锚，且无任何机械校验位。
- **"本机"口径冲突实锤**：`docs/acceptance-mvp.md:277-283` 写"Windows 本机事实：无 hipcc → amd no；
  Intel OpenCL ICD 在位 → intel yes"；本机实测为 ROCm 7.1 的 `HIP_PATH` 在位且 `hipcc.exe` 存在
  （amd=yes），oneAPI 目录不存在、`ocloc` 不在 PATH（intel=no），与
  `docs/evidence/phase15/README.md:52` 的 `amd=yes/apple=no/intel=no`、`findings.md` 本机画像一致。
- **证据落点风险**：`docs/acceptance-mvp.md:6` 以 `build/CMakeCache.txt:25`（Release）与
  `build/build.ninja:278`（`-O3 -DNDEBUG`）作 E5 前提证据，但 `git ls-files build/CMakeCache.txt`
  为 0（未跟踪），且第 25 行现为空值、build.ninja 现 441 行且 :278 不再是 FLAGS 行。
  `CMakeLists.txt:11-13` 仍默认 Release，故行为无恙、证据面不可复核。
- **摘除即红探针缺少恢复保证**：`probe/phase15_arm_probes.py` 的 `def patch`（:27）就地重写被跟踪
  源文件，`def main`（:44）按 patch→build→run→patch(restore) 执行且无 try/finally，末尾残腿自检
  仅在无异常时可达；phase16 同形；phase11–14 的
  8 个脚本硬编码本机不存在的 `build-apple/` 可执行路径。本次未运行这些脚本（会改写源码）。
- **报告与分级**：7 条 finding——High `acceptance-host-facts-contradict-current-box`、
  Medium `no-agent-facing-entrypoint` / `doc-code-anchors-drift-unvalidated` /
  `evidence-cites-untracked-build-tree` / `arm-probe-rewrites-source-without-restore-guard`、
  Low `skill-frontmatter-invisible-due-to-bom`（用户级 `web-design-guidelines` 的 SKILL.md 首 3 字节
  `ef bb bf`，故 frontmatter 未解析——本次会话内该 Skill 描述渲染为字面 `---` 即现存后果；全用户级
  Skills 中仅此一个带 BOM）/ `custom-agent-prompt-over-portability-cap`。
  维度分：任务理解 66 / 可控执行 45 / 改动验证 68 / 可靠交付 45 / 经验沉淀 48。
  支持轨道 = Bootstrap（缺失的是基础性导航与校验路由）。deferred（不入报告）：用户级 MCP 配置中的
  明文 API key（配置态即该文件的用途，lint 分类不构成现存后果）、`custom-agent-unbounded-tools`
  advisory、`.serena/project.yml` 的 changeDrift 提示（该文件是语言服务器自身配置，非项目设置）。

## Phase 35 — 一个只会解释训练数据的模型等于一条不能失败的对照

- **本轮否证的是我自己的模型**（Phase 34 入库的 `[R6.6-model]`）。它对 41 格全中，但那 41 格
  全部落在 `fill=2⁻⁶ ∧ k ≤ 2²⁴` 区域里——在这个区域"连加 k 次"与"乘一次"**逐位相同**，
  所以那 41 个"通过"不含信息量。把它推到没见过的输入上，naive 版 22 格里判错 5 格，
  其中 `fill=0.1` 一格把 `rel` 的**符号**算反（预测 +4.8336e−06 / 实测 −1.092e−05），
  `k=17500000` 一格差出七个数量级（预测 0 / 实测 −4.130e−02）。
- **机制是悬崖不是斜坡**：`2⁻⁶` 加进 `2¹⁸` 是 round-half-to-even 平局，结果仍是 `2¹⁸`，
  于是部分和**冻结**在 `2²⁴·fill` 上，此后 `k` 再大也不动。误差随 `n` 无界：
  `L=33554433, width=1` 的答案正好短少一半（`result=262144.0 / rel=−5.000e−01`，
  超容差 16 倍）。⇒ 这类"精确性模型"的价值不在拟合已测区，而在**说出边界在哪**；
  naive 版的错误不是数值差一点，是把"判 FAIL"说成"精确"。
- **split 有两个独立轴**：①可表示性上限（`k ≤ 2²⁴`，与 `fill` 是否 2 的幂无关的那一半）；
  ②`fill` 非 2 的幂 ⇒ 每个 `k` 的 `k·fill` 本身就不可精确表示。前者需要大 `n` 小 `width`
  才看得见，后者一格 `fill=0.1` 就看得见——**两个轴都只测一个，模型仍然会说谎**。
- **设备丢失要有名字**：`n=10⁸, nP=4` 段的 pass-1 内核被杀掉，加印原因后是
  `msg=unspecified launch failure`；而首版探针 `continue` 之后又跑出 5 次 `warm` 失败和一次
  `what=malloc`——一台已经不存在的设备继续"产生空格子"。仪器在失去设备后还能输出行，
  就是它把失败伪装成结果的能力（已改为 `return 2`，补测一 n 一进程）。
- **更便宜的替身往往更强**：预登记挑的判据格（`nP=4, n=10⁸`）本机采不到，而
  `nP=1` 只需 80 MB、单线程纯连加，判据反而最干净（`2^24` 精确 / `20M` −1.611e−01 /
  `33.55M` −5.000e−01 三点把边界钉住）。设计判据时先问"有没有更少的活动件能给出同一个分裂"。
- **下一相同一类的未验断言**（本轮只记录，不动）：`max_rel`（`src/codegen_hip.cpp:610`）发射的注释
  "2^-4-multiple init keeps every FP32 partial sum exact, so max_rel=0" 与本轮被拆掉的
  断言**同形**——它隐含"中间和始终可表示"，其上限应是 `2²⁴·2⁻⁸ = 65536`（gemm 累加
  `K=4096` 项、每项 2⁻⁸ 的倍数），从未按声明形状核过，也从未被 out-of-sample 推过。

## Phase 36 — "exact" 在两处意味着两件不同的事；一条真溢出从读码里掉出来

- **conv/gemm 的差分数据确实精确，但理由必须说对**：`initA` 模 17、`initB` 模 13，
  两者**互素**且各自零均值 ⇒ `k → (k%17, k%13)` 在 `lcm=221` 的周期内遍历全部 221 对，
  于是**任何**位移、**任何** stride 的一周期内积和 `= (Σf)(Σg) = 0`（整数格里逐位为零）。
  部分和因此不增长：`max |partial| = 182` 个 `2^-8` 单位 = `0.710938`，与 `K` 无关
  （`K=4×10⁶` 实测同值），而 fp32 精确表示 `m·2^-8` 只需 `|m| ≤ 2^24 = 16777216`
  ⇒ 现役数据模式下任何可达 `K` 都精确，`max_rel=0` 是结构保证。
- **与 reduce 的对照正是本轮的价值**：reduce 用全域 `2^-6` 常量填充 ⇒ 部分和**单调增长**，
  到 `2^24·fill` 撞上 round-half-to-even 平局就**冻结**（Phase 35）；gemm/conv 用零均值
  周期乘积 ⇒ 部分和**有界**。同一句注释里同一个词 exact，两处的成立机制相反——
  所以"这条断言为真"不能从"那条断言为真"外推，两处都得单独证。
- **承重前提被指认（对照实验）**：模数换成非互素的 `13/13` ⇒ 一周期和 = 182 单位 ≠ 0 ⇒
  部分和线性增长，`K≈1.2×10⁶` 越过 `|m| ≤ 2^24`，`K=4×10⁶` 处 fp32 连加偏离精确值 `-5051`。
  反面同样重要：给 `initA` 加直流偏置**不**破坏精确性（`off·Σg = 0`）——我最初挑的就是这条
  无效对照，算完才发现并换掉。
- **预测阶段的仪器 bug（E32）**：`np.cumsum(p.astype(np.float32))` 的返回值仍是 float32，
  我却把它当"fp64 参考"，于是第一版"mutant 仍 rel=0"是恒等式而不是结果。
  numpy 的 `cumsum/sum` 保持输入 dtype、不做隐式提升 ⇒ **凡"参考实现"先问它的位宽是否等于被判的那一侧**。
- **G11（新缺口，读码所得，非测量所得）**：发射的 `uk_gemm(..., int M, int N, int K)` 内核内部
  `A[m*K + k]` 是 32 位有符号；流式只限制**驻留**行数（`shard_rows=4096`），分配算术又是 64 位的
  （`a_shard_f = (size_t)p->SR * p->K`），所以 `K ≥ 524289` 时程序**照常启动并越界读**。
  可达性不是假设：`--op gemm --dims 2048 64 2000000` 在把 vram 档改成 16 GiB 后直接
  `SOLVED → EMIT lines=447`；而 `samples/hcs_amd.json` 本来就声明 ram 64 GiB，A 按分片驻留即可。
  链路上没有任何"张量元素数 < 2^31"的检查，差分验证因固定 256³ 也覆盖不到（G10 的同一形状）。
- **本轮不改发射**：改索引类型会让三份产物字节全变、`[R20]` 的 FMA 字节钉与既有基线全部重录，
  属规格级取舍 ⇒ 作为 D10 交出去（升类型 vs 可行性谓词加约束）。

## Phase 37 — 我写的缺口 G11 被自己的扫描否证；真正的缺陷在 CLI 的维度解析

- **否证过程（这是本轮的主要产出）**：G11 说发射的 `uk_gemm` 用 32 位索引、可行负载会越界读。
  两条都错，且错法不同：
  ① **常量来源错**：我把 banner 里 `shard_mb=64MiB (shard_rows=4096)` 当成固定值，实际
  `shard_rows = int(shard_bytes / (K*4))`（`shard_rows` `src/codegen_hip.cpp:1047`）随 K 反比收缩。7 档 K
  （1e5…1.6e7）× 3 档 M 的扫描里 `SR*K` 落在 `[6.4e6, 1.68e7]`，恒 ≤ `64MiB/4 = 16777216`
  ⇒ 流式启动的索引空间被分片大小钉住，**结构上不可能溢出**。
  ② **类型来源错**：我看到签名 `int M, int N, int K` 就断言 `m*K + k` 是 32 位乘法；
  实际发射的表达式是 `A[(size_t)(bm + j) * K + k0 + i]`——**乘法前有 `(size_t)` 转型**。
  全库 grep 后，未转型的乘法只有共享内存 tile 下标（`ty + r*TN` 一类），受 tile 尺寸界定。
- **教训的形状**：缺口必须引用"出问题的那一行"，不能引用"类型长这样"。签名里的 `int` 只是参数
  宽度，索引宽度在表达式里；banner 里的数字是某一次实例化的值，不是常量。**一条错误的缺口记录
  比没有记录更贵**——它会引导用户去批准一次改三份产物字节、作废 `[R20]` 字节钉与全部基线的"修复"。
- **扫描真正的发现（G11 已改写为这条）**：`src/main.cpp` 的 `--dims` 分支用 `std::atoi` 解析 `--dims`。
  `atoi` 对越界输入的行为是饱和：`--dims 3000000000 0 0` → rc=0、打印 `LEDGER HIT`、
  发射产物里写 `const int L = 2147483647;`。也就是说**声明 3.0e9 个元素的用户拿到 2^31-1 的程序，
  没有任何警告**，并且这条请求与显式 `--dims 2147483647` **命中同一个 ledger 键**——一条证据被两个
  不同工作负载共用。reduce 一侧还叠加了可寻址性：`uk_reduce_part(..., int n, int nP)` 使上界就是
  `2^31-1` 个元素，所以正确语义是**拒绝**而不是饱和。
- **修复点很小（Phase 40 已落地）**：`--dims` 分支与 `parse_int` 都改成显式范围校验检查，超界
  `ERROR: dim out of range` 并非零退出；不碰发射字节。因为它改变用户可见的拒绝行为，
  按项目既有纪律（超订要 loud 拒绝、改动需点头）交用户定 = D10（已按本轮结论改写）。
- **未动**：生成器、发射产物、容差、验证实例；`docs/evidence/phase36/g11-int-index.txt` 作为
  转录保留（不改写），其"越界读"的解读由本相 README 与 G11 行标注撤销。

## Phase 38 — 声明维度的越界饱和在两个入口都存在，而且都会复用账本键

- **问题**：Phase 37 把缺陷记在 `--dims` 分支的 `std::atoi` 上。如果只有那一个入口，修复就是
  解析层 5 行；如果 KML 文件入口也有，修复面与验证方式都不同。
- **实测（stock 8 GiB HCS，两条独立路径）**：
  - CLI：`--op reduce --dims 3000000000 0 0` → rc=0，发射产物写 `const int L = 2147483647;`
  - 文件：`{"op":"reduce","dims":[3000000000]}` → 同样 rc=0、同样 `const int L = 2147483647;`
  ⇒ 不是 `atoi` 独有。文件侧**当时**（Phase 39）把 JSON token 经 `parse_int` 写进 `int v`，
  上界根本不在校验清单里（原文引用 kml_front 的 108/112 行）。今天这条已由 D10/R1.2 修掉：
  文件侧走 `parse_int_wide`（`src/kml_front.cpp:127`），上界检查就紧跟其后。
- **可观察后果比"尺寸不对"更糟**：在新鲜 ledger 上先跑 `2147483647`（SOLVED + COMMIT），
  再跑 `3000000000` 得到 `LEDGER HIT c_cached_ms=570.425 tile=128x128`，反向亦命中
  ⇒ **一次越界声明复用了他人的求解证据**，而工具以 rc=0 结束。
- **对 R1.2 的含义**：`KML rejected (2 violations): dims[1]=0 must be positive`、
  `dims: reduce needs 1 dim (L), got 6` 都工作正常——front end 的"完整违规清单"承诺没坏，
  缺的是**一条"维度必须在可寻址范围内"的谓词类别**。所以这是规格缺项，不是实现走样。
- **对 D10 的影响**：只在 CLI 加校验不够（文件入口仍会静默饱和）。可选形状：
  ① 在 `parse_int` 内对 `INT32_MAX` 以上直接判失败（一处改动覆盖两个入口中最重要的一条路径）；
  ② 在 CLI 的 `--dims` 与文件入口的 dims 循环各加一次上界违规；
  ③ 同时把 reduce 的可寻址上界（`uk_reduce_part(..., int n, …)` ⇒ `2^31-1` 元素 = 8 GiB fp32）
  写成显式约束。**本轮未动代码**：它改变用户可见拒绝行为并触及 R1.2 验收面。
- **探针纪律（E34）**：`cmd | grep …; echo rc=$?` 读的是 grep 的 rc——我据此一度把
  "被拒绝"看成"被接受"，是同一次运行里另一条未经管道的调用（真 rc=2）纠正的。

## Phase 39 — 把"缺谓词"当成一类来扫：先被自己上一相发现的缓存碰撞坑了一次

- **自我否证（E35）**：第一遍 tile/dtype 扫描里，4 档不同 `tile_m/tile_n` 声明全部返回
  `LEDGER HIT c_cached_ms=0.110506 tile=128x128`。我以为读到的是"本次请求的钳制结果"，
  实际是**上一次请求留下的产物**。这个同键碰撞正是 Phase 38 刚写进 G11 的缺陷，
  它先坑了我的探针。 ⇒ 重跑时**每个用例配一个独立 `--ledger`**，结论才算测过。
- **tile 面**（清洗后）：`0` 与负值被拒（`KML rejected (2 violations)`，rc=2）；`4` 与 `100000`
  都被接受并解到同一个 `tile=128x128`，发射 `#define TM 96 / TN 96 / TK 16`，
  且带 D6 承诺的注释行 `// solved tile=128x128 -> emitted block tile=96x96 (clamped for
  register budget)` ⇒ **D6 的"钳制要标注"兑现**。但声明的 tile 提示不进入求解：
  `64x128`、`4x4`、`100000x100000` 三档同解同速（`c_ms=0.110506`）——即"提示被忽略"这件事
  本身没有被点名（报告只说 solved tile）。
- **dtype 面（新缺口 G12）**：前端**明确允许** `fp32|fp16|int8`（`dtype`、`src/kml_front.cpp:106/108/112`），
  `dtype_size` 进字节/代价（`src/kml.cpp:117`），`dtype` 进规范键（`src/kml.cpp:107`），
  但两个后端生成器**通篇不出现 `dtype`** ⇒ 发射恒为 fp32。实测同一 gemm 512³：
  `fp32 c_ms=0.110506` / `fp16 0.0613536` / `int8 0.0367776`，而发射同为 448 行、
  值类型只有 `float`/`double`。⇒ 账本可以收下"按 fp16 计成本、对应 fp32 内核"的证据行，
  且 `gemm:fp16:…` 与 `gemm:fp32:…` 会作为**不同键**共存，其中一条的代价与产物不一致。
  容差表的 `fp16_rel = 2^-4` 只出现在 `fp16_rel` `include/unikern/components.hpp:133`，
  未见被判定路径读取 ⇒ 错配在**成本/产物**之间，不在判定阈值上。
- **HCS 数字面（干净）**：`bw_gbps` 缺失或为 0、`bytes` 为 0 或负值，一律
  `HCS rejected: missing required fields: tiers[…].…` 且 rc=2；未知 KML 键同样被拒
  （`KML rejected (1 violations)`）。这一面的谓词是齐的，与 Phase 38 的 dims 上界缺失形成对照。
- **CLI 维度面（比饱和更严重）**：`--dims abc 0 0` → `std::atoi` 得 0，而 `o->dims[0]` 的"全零即未覆盖"判断在 `src/main.cpp:75`，于是把"用户没有覆盖" ⇒ 静默去解**演示负载**，发射 `const int L = 4096;`、rc=0。
  即一个非法输入会让工具**换掉整个工作负载**，且报告成功。
- **Phase 38 的饱和面再扩一档**：`--dims 99999999999999999999 0 0` 同样 rc=0 + `L = 2147483647;`。

## Phase 40 — D10 落地：把"静默替换"变成"拒绝"，两个声明入口一起补

- **为什么这不算校验收紧**：`std::atoi` 对越界返回 `INT32_MAX`、对非数字返回 `0`，两个返回值都
  "看起来合法"，于是**太大、拼错、没给**三类完全不同的错误被压成同一条通过路径。修的的是错误语义，
  不只是范围。
- **实测对照（每例独立 ledger，见 `docs/evidence/phase40/README.md`）**：
  `2147483647` 改前改后都 rc=0 且发 `const int L = 2147483647;`（边界值必须可用）；
  `2147483648`、`3000000000`、`99999999999999999999` 改前 rc=0 + 发 `L = 2147483647`（静默替换），
  改后 rc=2 并回显非法值；`--dims abc 0 0` 改前 rc=0 且**改解演示负载**（`L = 4096`），改后 rc=2；
  KML 侧 `dims:[3000000000]` 改后 violation 为
  `exceeds 2147483647, the largest element index the generated kernels can address`；`tile_m` 同口径。
- **产物零改动**（这条决定了风险面）：校验发生在求解与发射之前，所以 conv 对
  `docs/evidence/phase29/conv-hip-knob-check.txt` 仍是**空 diff**，gemm/reduce 仍 447/239 行，
  `[R20]` 的字节钉与全部已入库读数含义不变。
- **臂 `[R1.4-dimrange]`** 驱动真实二进制（复述条件只会测到复述），并带两条**反向**对照：
  `INT32_MAX` 必须仍通过且发出自己的长度（写成 `>= INT_MAX` 即红），`1048576` 必须照旧通过
  （"拒绝一切"也红）。
- **本轮新增的自我修正（E37）**：我写的锚点自动重算器在 dry-run 里就有 3 条错误归因
  （`size_t` / `LEDGER HIT` / `std::atoi`），若 apply 会把**正确**的引用改坏；工具已删，改手工。 另：批量整词替换 `src/main.cpp:86` 在 task_plan 里造出过嵌套反引号的坏 Markdown，自查 diff 时发现并修。
- **没顺手做的**：G12 / D11（dtype 进成本与规范键、不进产物）是**策略选择**，不是解析 bug；
  D8/D9 会改变已入库证据的含义。三条都等用户定。
- **三条摘除控制全部跑过并各自能红**（`docs/evidence/phase40/controls.txt`，复原 byte-identical）：
  ①CLI 退回 `std::atoi` ⇒ `Assertion failed: r.first != 0`；②删掉 KML 上界分支 ⇒
  `Assertion failed: kcap.find("exceeds 2147483647")`；③把 `> INT_MAX` 写成 `>= INT_MAX` ⇒
  `Assertion failed: edge.first == 0`。第三条是那条"更严总比不严好"会静默通过的对照——
  边界值必须可用不是修辞，是一条腿。

## Phase 41 — 三后端的差分数据确实同形，但"确实"以前只是巧合；现在它有腿，也修正了我自己的一个数字

- **先查覆盖范围再谈证明**：Phase 36 的互素论证是从 `src/codegen_hip.cpp` 读出来的，而那句
  "2^-4-multiple init keeps every FP32 partial sum exact" 的注释只存在于 HIP 发射器里。
  实测三后端的 init 表达式**逐字相同**（HIP 发射的多一个 `static`；本轮把 2026-10-28 写下的四处
  引用按现值重取：`initA` 在 `src/codegen_hip.cpp:604/1122` 两个实例、`initA` 在 `src/runner_ocl.cpp:136`、
  `initA` 在 `src/runner_metal.mm:87`），所以证明对 conv/gemm 的三后端都成立，
  Phase 36 的说法**没有过宽**。
- **但没有任何东西守着这份一致**：四处副本里任何一处被改（例如 `% 17` 改成 `% 16`），
  互素前提当场失效，而发射的注释与全部 `max_rel=0` 读数的含义都会悄悄改变——
  这正是 E21/E24 那一族（存在性/巧合一致不能当判据）。
- **新臂 `[R6.7-init-parity]`**：按出现**次数**钉四份副本（HIP 2/2、每个 runner 1/1，合计 4/4），
  并且在测试时**重算**而不是引用我的文档：整数格 `m·2^-8` 上遍历 7 组 stride × 221 个位移，
  断言 `gcd(17,13)==1`、整周期乘积和 `== 0`、`max |partial| <= 2^24`，
  再用非互素的 `13/13`（周期和 `!= 0`）做反向对照——即"承重的是互素"这件事本身也是腿的一部分。
- **修正我自己的数字（口径以腿为准）**：Phase 36 写的 `max |partial| = 182` 个 `2^-8` 单位是
  **单一 stride** 下的值；腿扫遍 stride 后最坏是 **364** 单位（= 1.42）。界与结论都不变
  （364 << 16777216），但今后引用要用 364。
- **控制能红**（`docs/evidence/phase41/parity-controls.txt`）：改 OpenCL runner 的模数 ⇒
  `Assertion failed: a == b`；只改 HIP 发射器两处中之一 ⇒ 同一条红（证明"计数"而不是"相等"在承重）。
  两者均 byte-identical 复原；`diff` 证明 conv 发射产物与 `docs/evidence/phase29` 基线逐字节一致
  （这条臂只读源文件，不碰发射）。

## Phase 42 — harness #5 落地；一条恒真判据因此现形

- **#5 的说法偏轻**：它说 phase11-16 探针"缺 try/finally"。实况是 phase11-14 的 `ROOT` 写死成
  `/Volumes/data/git/c/UNIKERN` 并调 `build-apple` 与 `/tmp` 产物——**这台 Windows 机器上根本跑不起来**，
  所以不只是"复原没保证"，而是"复原路径从未被执行"。phase15/16 已推导 ROOT，但复原靠循环里显式调用。
- **保证做成机械的**：`probe/arm_probe_guard.py` 进入时按字节快照、唯一写路径 `GUARD.write`、
  退出时**重读并比对**、全 `src/` 扫残留 `ARM-PROBE`、`git status src/` 与进入时做**集合比较**。
  集合比较这一条是修正后的设计：第一版要求"干净"，而本仓有正当未提交工作，
  于是守卫在 phase15 首跑时一进来就 GUARD FAILURE——仪器的判据错了，不是被测对象错了。
- **不能编译就说不能编译**：Apple 专属步骤（clang++/-fobjc-arc/Metal framework）由
  `has_apple_toolchain()` 闸门拦住并打印 SKIPPED，把"这台机器没测过"和"测过且通过"分开。
- **新门禁**：`probe/probe_hygiene_check.py` 注册为 ctest 第 4 条 `probe_hygiene`
  （写 src/ 的探针必须守卫化写路径 + 守卫下入口 + 推导式 ROOT）。
  开火控制：一份去掉守卫的 phase14 副本被点名 4 条、rc=1；删除后 rc=0。
- **两处过期站点**：`phase11/runner_gate` 找 `op_support_gap(tree)`（Phase 13 后端化成 `msl_op_gap`）、
  `phase16/reduce_migrations_token` 找 `<< UK_MIGRATIONS << std::endl;`（Phase 29 把 token 拆链成独立行）。
  探针的"站点恰好 1 处"自检让它们在写入**之前**中止——不毁树，但静默失去全部证据力。
  新增 `probe/phase42_probe_site_audit.py`（纯读、不写入）：现在 21/21 站点活。
- **真正的收获**：修好站点后 `reduce_migrations_token` 腿**仍然指认不了**，ctest 4/4 全绿。
  根因在臂：`[R8.5]` 用 `src->find("\" migrations\"") != npos` 判定"结果行带该 token"，
  而同一程序的**拒绝行**也带它 ⇒ 结果行发射被删也照样满足 ⇒ 这是一条恒真判据，
  自 Phase 16 起绿了 26 个相（E21 的 `checks=`、E24 的 `dp.multiProcessorCount` 之后第三次）。
  改成按程序计数 `== 2`（拒绝行 + 结果行）后，该腿立刻红在
  `Assertion failed: n == 2, `[R6.8]`、tests/acceptance.cpp:1859`。
- **纪律再固化**：每条新腿都要跑它自己的摘除控制；**没跑过控制的腿等于没有腿**。
- 未动：产品代码与发射产物（conv 对 phase29 基线空 diff、gemm 447、reduce 239）。

## Phase 43 — “可复现”的前提是“在版本控制里”；新门禁在同一相内就抓到了我自己的迁移错误

- **#4 的实质**：`.gitignore` 忽略 `build/` 与 `uk_run/` 是对的，问题在反面——
  ① 六份 `docs/evidence/*/README.md` 的复现命令依赖 `uk_run/phase19conv/conv_*.json` 这些**声明输入**， （旧路径）
  而 `uk_run/` 被忽略 ⇒ 克隆里没有这些文件，命令不可执行；
  ② 三件 Phase 9 Intel 证据（`uk_intel.cl`、`aot/uk_intel_bmg.bin`、`aot/uk_intel_bmg.spv`）
  是被强制跟踪在忽略树 `build/phase9-emit/` 里的异常体：gitignore 不影响已跟踪文件， （旧路径）
  所以它们在常规 `git status` 里完全隐形。
- **搬动先证再搬**：产物 `git mv` → `docs/evidence/phase9/emit/`（三件搬前搬后 sha256 一致，
  在索引里是 rename，未提交）；声明输入 → `workloads/conv/`（`git add` + `git ls-files` 实证）。
  再从新路径发射 conv，与 `docs/evidence/phase29/conv-hip-knob-check.txt` **空 diff** ⇒ 搬迁不影响产物。
- **新门禁 `record_audit`（ctest 第 5 条）**：只认“带扩展名的路径引用”，分 MISSING 与 IGNORED INPUT 两类，
  覆盖规划文件 + 全部证据 README。判据两轮收紧的教训：首版把 glob（`probe/phase1[89]_*.py`）、
  模板占位（`docs/evidence/phaseNN/`）、`build/CMakeCache.txt` 都当路径报，21 条噪声里只有 1 条真
  （`docs/AGENTS.md`——该文件在仓库根，已改）。性能：逐条 `git check-ignore` 让门禁跑 21 秒，
  改成一次 `--stdin` 批处理；**慢门禁会被跳过，那等于没有门禁**。
- **开火控制**：注入一条指向 `uk_run/control_input.json` 的引用 ⇒ `rc=1` 并点名 IGNORED INPUT； （旧路径）
  清除后 `rc=0`（393 条引用、0  findings）。退出码用 `subprocess.run` 直接读——
  管道后的 `$?` 是管道末段的（E34 第三次险情）。
- **我自己酿的两条**：E39（往 `.gitignore` 追加“说明文字”时，目录名单独成行 = 一条忽略规则，
  刚建的 `workloads/` 反而不被跟踪；只有真跑 `git add` 才暴露，读 diff 看不出）、
  E40（目录级整串替换把同目录下的运行产物账本 `conv.sqlite3` 一起迁进受跟踪目录，
  **被本相刚写的门禁当场抓到**——这是本项目里第一次“新自动化检查立刻抓到作者本人的错”）。
- 未动：产品代码与发射产物。

## Phase 43 续记 — 门禁自己被证明是哑的两次，最后靠"自测"治住（E41）

- 为把 `record_audit` 从 21 s 降下来做了三次批处理尝试，每一次都让判据**静默失效**而 ctest 仍然绿：
  ① 393 次 `git check-ignore` 子进程；② `--stdin` 批处理在 Windows 文本层留 `
` 且 git 按
  `core.quotePath` 加引号 ⇒ 集合里是 `'uk_run/<x>.json
'` 这种带引号带 CR 的字符串，成员测试恒假；
  ③ 换 `-z` 时忘了它同时把**输入**改成 NUL 分隔 ⇒ 整份清单被当成一个路径（集合大小 1）。
- **最终形态**：不调用 git，直接从 `.gitignore` 读目录规则做前缀判定（0.4 s），并在门禁开头加
  `self_test()`：用两条合成引用断言 `MISSING` 与 `IGNORED INPUT` 两种判据**都能触发**，否则打印
  `GATE IS MUTE` 并 rc=1。
- 自测上线后**第一次运行就报红**，抓到第四个 bug：存在性判断排在忽略判断之前，于是"只在本地存在的
  忽略树文件"才会被判 IGNORED INPUT——而声明输入的正确语义是"克隆里永远没有"，与本机有无无关。
- 固化：**重构任何仪器（哪怕只为提速）之后必须复跑它的开火控制**；更稳的做法是把"我能失败"实现进
  仪器自身，而不是依赖我记得去跑。
- 历史路径的引用统一标注"（旧路径）"：门禁据此豁免（见 `DEFUNCT_MARKERS`），代价是同一行里若真有
  活的复现命令也会被豁免——这条漏报风险写在代码注释里，不藏。



## Phase 44 — D9 落地：宽度改由设备自报量推导；而"1.65–2.50×"被我自己的出厂测量证伪

- **名义值 vs 自报值**：`samples/hcs_amd.json` 声明 `compute_units: 36`，RX 6600M 自报 14 CU。
  按声明推会得到 `2048×36 → 65536`，既非实测最优又把 pass-2 串行折叠长度乘 4。所以 D9 的实现必须是
  **运行时**查 `hipGetDeviceProperties`（与 `uk_conv_plan` 同一纪律），HCS 的 CU 数不参与；
  查询失败回退 4096 且打印 `source=props_unavailable`（静默回退等于把选择藏起来）。
- **顺带关闭的 rule 6 缺口**：reduce 发射程序的 verify 段原先写死 `VP = 4096`，计时段用 `UK_REDUCE_WIDTH`
  ——两趟不是同一个配置，与 Phase 26 在 conv 上修掉的是同形问题（pass-2 折叠长度正是由宽度决定的量）。
  现在全 run 只有一个宽度值，真机三档 `max_rel=0`。
- **出厂路径不复现探针的提速**：
  | n | 探针 4096 both | 探针 16384 both | 出厂 4096 kernel_ms | 出厂 16384 kernel_ms |
  |---|---|---|---|---|
  | 16,777,216 | 1.252 ms | 0.772 ms | 0.782 ms | 0.745 ms |
  | 67,108,864 | 4.692 ms | 1.865 ms | 1.911 ms | 1.895 ms |
  两列"出厂"都在噪声内，而两列"探针"差 2.5×。分歧点是**基线**：出厂程序在 16.7M 的总 kernel 时间
  (0.78 ms) 已低于探针 pass-1 单项 (1.161 ms)。 ⇒ 探针的绝对时间不能当作出厂收益引用；
  D9 的净效果是"配置选择来自设备 + verify/计时同配置 + pass-1 冻结余量更宽（`k=ceil(L/16384)`）"，
  不是速度。**待查（Phase 45）**：两台仪器为何对同一内核同一尺寸给出不同时间（候选：warmup/迭代编排、
  L2 驻留、事件对覆盖的跨度、pass-2 是否被计入同一区间）。
- **纪律复证**：计数判据当场抓到我写错的期望值（`VL, VP` 不是 `VL, UK_REDUCE_WIDTH`）；
  发射产物用 diff 证明只动 reduce（239→268），conv/gemm 空 diff/447 不变。

## Phase 45 — D11 闭合：dtype 只作能力轴、容差回到唯一的表；账本漏洞是收证据时看见的

- **改前的三个事实**（都是实测，不是推断）：`fp16_rel` `include/unikern/components.hpp:133` 的 `fp16_rel`
  在整个代码库里没有任何读取点；三个 HIP 算子的判定行写死 `0.03125f`
  （原 `src/codegen_hip.cpp` 三处，现为 `dtype_tol_literal` 于 `:638`、`:878`、`:1212`）；
  前端接受 `fp32|fp16|int8` 而生成器只会 fp32，于是**一棵 int8 树会被按相对 2⁻⁵ 判**，
  规格要求的"整型位级"根本没有实现。
- **账本洞（E43）**：`solve` 的拒绝条件原为 `!gap.empty() && !o.emit_dir.empty()`。不带 `--emit`
  的 int8/fp16 树因此走完整条 solve 并 `commit()`；实测 SQLite `solutions` 表 **1 行**。
  修法是去掉 `--emit` 条件并把拒绝排在 `ledger_key()` 之前 —— 连 `.sqlite3` 文件都不被创建。
  `.jsonl` 账本只在 commit 时开文件，所以**用文件型账本当证据看不见这条**。
- **发射产物的真实位移**：`std::to_string` 固定 6 位小数 ⇒ `0.03125f` → `0.031250f`。
  conv 对 `docs/evidence/phase29/conv-hip-knob-check.txt` **恰好差 1 行**（542 行不变，
  `sha256=09e9b5706cba661cbc861bb4d96d450dd62520d21c2c3b602f09ce3ec7c80d3f`）；
  reduce 对 `docs/evidence/phase44/reduce-hip-width-plan.txt` **恰好差 1 行**（268 行不变，
  `sha256=c3a26462ed663e5550a65dcd0b071f9989a97fd8e90912fd4047633beeb66ef0`）。
  **gemm 从来没有逐字节基线**（Phase 44 只记了"447 行"），今天起 `docs/evidence/phase45/emit/gemm/uk_amd.cpp`
  是基线（447 行，`sha256=7a451491ed9b28f8c02ebe00159dc5b5a7ed8f85816755ec3cef79f43d1d6479`）；
  这一算子的"差 1 行"目前只能由共享 helper + 控制 C2 支撑，不能由 diff 支撑。
- **真机（RX 6600M，`docs/evidence/phase45/device-verify.txt`）**：三算子在新判定行下全
  `verify=PASS max_rel=0.000000e+00`，gemm `kernel_ms=0.118910`、conv `kernel_ms=7.198660` 且
  `conv_plan ocb=8 tile=16 fills=device`（与 Phase 29 基线同档）、reduce `kernel_ms=0.459194` 且
  `reduce_width source=device width=16384 cap=28672`（D9 结果未被动），三者 `migrations=0 checks=6`。
- **新门 #6 `runner_tu` 的事实依据**：没有 Intel SDK 时 CMake 把 `UK_HAS_OCL` 置 FALSE，
  `src/runner_ocl.cpp` 的身体**根本不参与编译**（`runner_metal.mm` 干脆不在 Windows 的目标列表里）。
  用 `third_party/CL` 的镜像头跑 `c++ -fsyntax-only -DUK_HAS_OCL` 就能在本机真的检查它：控制 C4
  （体内放语法错误）**只有 `runner_tu` 红，acceptance 全绿** —— 这就是这条门的存在理由。
  `runner_metal.mm` 本机连这一步都做不到，只能由源码文本子腿覆盖。
- **分类器两个方向的失败（E44）**：mention-based ⇒ 假阳性（只读门被当写手）；
  proximity-based（写入点 6 行内）⇒ 假阴性（8 个真写手被漏，`TARGET` 定义在 57 行外）。
  终态判据是「守卫 API，或写调用与 src 字面量同行／同变量」，并由 `MUST_BE_CLASSIFIED`
  （14 个已知写手）+ `self_test()` 双向控制钉住；收紧第一版**当场**被前者抓红。
- **仍为真的限制**：half/int 内核依然不存在（D11 关的是"按 fp32 定价并入库"这条路径，不是补齐生成能力）；
  `verify_fp32()`（`src/runtime.cpp:191`）仍只接 `tol.fp32_rel`，今天不是缺陷，但整型位级档目前
  只在拒绝路径上成立；C5 那种"写死成与表相同的值"没有任何腿能区分；Phase 44 的两台仪器对 4096
  基线的分歧（探针 1.161 ms vs 出厂 ~0.75 ms）**本相未查**。

## Phase 46 — D8 取证：规格的"两档 CPU 参考"只实现了一档，而换档被证明是证据中性的

- **规格文本**（本轮重读，不是转述）：`requirements.md:10`「正确性验证以 CPU 参考实现为基准
  （**标量 + 分块两档**）」，`requirements.md:34` 与 `design.md:132` 重复同一句。出厂只有标量档：
  `cpu_reduce` = `float acc=0.f; for i: acc += x[i]`（`src/reference.cpp:24`）。所以"分块档"
  不是新方案，是**规格要求里缺的那一半**。
- **换档的中性证明**（`tiers.txt` 的 `shipped` 段）：常量 `fill=2^-6`，n=2²⁰ 与 2²⁴，
  宽度 16384 与 4096 —— 四格里两档 fp32 和**逐位相同**，且都**恰好等于闭式精确值**
  （16384 / 262144，`rel_vs_exact=+0.000000e+00`）。⇒ 已入库的 reduce `max_rel=0` 证据
  不会因为换参考而改含义；D8 卡住的那条理由只对**没测过的**算子仍然可能成立。
- **D8 病理复现（同一输入形状，Phase 19 的交替 `1e6/1e-6`）**：
  n=2²⁰ 标量 `+7.238812e-03` / 分块 `−5.556250e-05`；n=2²⁴ 标量 `+4.154000e-02`（**越 2⁻⁵**）
  / 分块 `−5.556250e-05`；n=2²⁴ width=1024 分块 `+1.856875e-04`。被判 FAIL 的是更准的一方，
  而且**换档即可消掉，不需要动容差**。
- **方向不是输入类别的函数**：另一类宽输入（`2^linspace(−40,40)` × 确定性 LCG 符号）里
  标量档反而更准（n=2²⁰ `+1.146e-08` vs 分块 `+1.943e-06`；n=2²⁴ `+6.646e-08` vs `−4.358e-07`）。
  这把 D8 从"参考选错了"升级成"**单一档参考这个仪器形式就是错的**"——三条候选口径都还可辩护。
- **第二台仪器的对账**（与 Phase 19 真机/臂）：`seq_err@2²⁰` 逐位一致（`7.23881e-03`），
  两轮归约 `1.86e-04`（我 `+1.856875e-04`）与 `5.56e-05`（我 `−5.556250e-05`）一致；
  **但"参考 vs 精确 @ 2²⁴"我 `+4.154e-02` 而记录 `3.988e-02`，差 4%，未对齐**。
  最可能是当时精确和的取法与交替序列奇偶约定不同（本相闭式为 `(n/2)·(fp32(1e6)+fp32(1e-6))`
  写在 `long double`——`fp32` 里那两项相加会直接吞掉 `1e-6`，所以闭式**不能写在被判的精度里**）。
  这条记成分歧而不是"复现成功"。
- **仪器自报精度**：`sizeof(long double)=16, digits=64`（MinGW-w64 GCC 13.2 上就是 64 位尾数），
  所以 Kahan 列只是**有标签的替身**，凡有闭式的行都以闭式为准（E32 的教训的延长线）。
- **范围限制**：只测 reduce。gemm/conv 的 verify 输入是零均值锯齿（`initA mod 17` / `initB mod 13`），
  `max_rel=0` 是结构性的，本轮没有测它们的两档差异。**生成器、容差、判据一字未动。**

## Phase 47 — D8 落地：reduce 的基准换成规格第二档；"换参考"最大的风险是把它换成被测对象自己

- **实现口径**：`kRefReduceWidth = 1024`（`kRefReduceWidth` `include/unikern/core.hpp:124`）是**参考自己的**分片数，
  与内核 `UK_REDUCE_WIDTH`（本机 16384）解耦；`cpu_reduce_blocked`（`src/reference.cpp:36`）
  = 跨步部分和 + 索引序折叠。发射程序里的 `cpu_reduce_ref`（`src/codegen_hip.cpp:840`）与两个进程内
  runner 的 `cpu_reduce_blocked` 在 `src/runner_ocl.cpp:660` 与 `cpu_reduce_blocked` 在 `src/runner_metal.mm:593`，同口径
  （2026-10-29 按现码重取；原先写的 805/562/530 是 Phase 47 当时的位置，已被后续插入推走）。
- **为什么这是硬约束而不是风格**：如果基准用与内核**相同的形状**（同分片数、同折叠次序），
  `max_rel` 恒 0 —— 仪器给自己打分，门永远绿而什么都不测（E32 的同形失败）。所以 `[R6.9-basis]`
  里同时写 `basis != kern` 与 `div_new > 0`，控制 B1 把宽度改成 16384 时首条红断言就是
  `basis != kern`。**一条只测"换档后还过不过"的腿，正好放得过最坏的实现。**
- **中性（真机复证 Phase 46 的 CPU 预言）**：常量 `2^-6` 输入上两档逐位相同 ⇒ reduce 1M/16.7M/67.1M、
  gemm 512³、conv 256²×32 全部 `verify=PASS max_rel=0.000000e+00`，`migrations=0 checks=6`；
  gemm/conv 的产物**空 diff**（447/542 行，`7a451491…`/`09e9b570…` 与 Phase 45 字节基线一致），
  reduce 268→**276 行**（`44231aef…`）。
- **D8 本体消失**：交替 `1e6/1e-6`、n=2²⁴ —— 旧基准 vs 内核 `4.160e-02`（> 2⁻⁵，所以更准的一方被判
  FAIL）；新基准 vs 内核 `2.413e-04`（≤ 2⁻⁵）。同一内核、同一数据，只是基准换成规格的第二档。
- **范围有理由**：gemm/conv 的 CPU 参考与内核共享 `[R20]` 按字节钉死的嵌套累加次序（AGENTS 第 3 条），
  给它们换档 = 改折叠次序 = 让全部已入库 `max_rel=0` 改含义。reduce 能安全换，恰是因为 Phase 46
  证明了它的两档在 shipped 输入上逐位相同 —— **中性证明是换档的前置条件，不是换档之后的检查**。
- **别把第二档写成"真值"**：它与内核同属部分和顺序折叠家族；Phase 46 结论 3 已实测"谁更准"随输入
  结构反转。这一档是**规格要求的口径**，不是绝对正确性。
- 一次同族复发（E45）：`long double(x)` 函数式转换编译失败，而 `ctest` 因未重新链接照报 6/6 绿 ——
  这次是**库先链接、测试后失败**的"部分成功"构建，比整建失败更难察觉。

## Phase 48 — 存盘证据字节自己上机；顺带让「改证据文件」变成一件会被门看见的事

- **此前的真实状态**（不是推测，是流程事实）：`run --vendor amd` 让 runner 在 workdir 里**现场重新
  生成**程序再编译，因此 `docs/evidence/**/emit/uk_amd.cpp` 从来没有作为**这些字节**被编译过。
  证据链里「存下来的程序 = 跑出那个数的程序」这句话，只验了「它长得像生成器输出」这一半。
- **两半各自的仪器**：`[R5.4-artifact-bytes]`（`tests/acceptance.cpp:2132`）用 CLI 重新发射同样的三个
  形状并**逐字节**比较（先 `size()` 后内容，避免只比行数的假象）；
  `probe/phase48_artifact_build_run.py` 用与 runner 同源的 hipcc（`HIP_PATH/bin/hipcc.exe`）、
  `-O3`、显式 `--offload-arch=gfx1032` 编译存盘文件并直接运行。
- **读数**（本机 RX 6600M）：三份全 `verify=PASS max_rel=0.000000e+00 migrations=0 checks=6`，
  `perf=` 字段与形状一致；`kernel_ms` = 0.115690 / 7.253811 / 0.468358，runner 侧是
  0.118630 / 7.210240 / 0.459194（差 0.6%~2.4%，本机计时离散度是已记录的事实，登记不解释）。
- **A2 才是这一相的新能力**：给存盘证据文件追加一个换行，门立刻红在
  `fresh.size() == stored.size()`（`tests/acceptance.cpp:2178`）。以前改动 `docs/evidence/` 里的
  产物不会让任何东西变红——**「证据不可变」以前是靠约定的，现在有一条腿看着它**。
- **两条自己的坑**：E47（手动绿/ctest 红，cwd 不同导致相对 `--kml` 路径解析不了 ⇒ 会 spawn CLI 的腿
  必须用绝对输入路径，并且必须在 ctest 下跑过一次才算数）；E48（脚本里混进 U+200B 零宽空格，会静默
  改掉文件名；用码点扫描发现，别靠眼睛）。

## Phase 49 — D9 恢复：所谓「两台仪器不一致」，其实是我的一次 A/B 从未生效

- **出厂路径实测**（`docs/evidence/phase49/width-env-matrix.txt`，钉值自报 `source=env width=`，
  交替三轮取中位，全部 `verify=PASS max_rel=0`）：

  | n | 4096 | 8192 | 16384 | 32768 | 4096→16384 |
  |---|---|---|---|---|---|
  | 16,777,216 | 1.2612 | 0.8074 | **0.7485** | 1.2355 | **1.685×** |
  | 67,108,864 | 4.6965 | 2.6711 | **1.8798** | 2.7652 | **2.499×** |
  | 134,217,728 | 9.2963 | 5.1283 | **3.3291** | 4.2776 | **2.787×** |

  8192 与 32768 都劣于 16384（分别慢 1.08×/1.42×/1.54× 与 1.65×/1.47×/1.28×）
  ⇒ 设备自报量 `2048×14=28672 → 向下取 2 的幂 = 16384` 落在采样最优附近，**不必分段就能说**。
- **Phase 44 的「收回」是测量事故**：它没有钉值通道，便用 `sed` 手改**已发射的源码**造 baseline 臂；
  其转录里那条臂**不打印宽度横幅**，`kernel_ms` 与 runtime 臂几乎相同
  （0.7816 vs 0.7452；1.9108 vs 1.8950）⇒ 两臂同为 16384。「差在噪声内」是**同一配置对自己**的必然结果。
  **规律：处理组必须自报身份；两臂几乎相等是警报，不是结论。**
- **首测不是数**：134M 单发首测 3.819 ms，交替后 3.32–3.34 ms（差 15%）。编译期间 GPU 空闲降频，
  第一跑带着时钟爬坡。协议改成"交替 + 中位"。
- **落地**：`uk_reduce_width_plan` 先读 `UK_REDUCE_WIDTH`，生效打印 `source=env width=<w>`；
  值不在 `[256, n]` 时打印 `UNIKERN_ERROR what=reduce_width_pin_unfit` 并 `exit(2)`
  ——**静默回落等于把「我钉过了」变成假话**；`getenv` 只出现一次。conv 早有 `UK_CONV_TILE`/`UK_CONV_OCB`，
  reduce 缺这个通道正是这次事故的根。
- **新臂 `[R15c-width-pin]`**（`tests/acceptance.cpp:2193`）：单调锚点序列
  `pin < use < refuse < decl < query`、`source=env` 横幅、`exit(2)`、单次读取。
  `[R5.4-artifact-bytes]` 基线移到 `docs/evidence/phase49/emit/`：reduce 276→**290 行**
  （`sha256=b8862c99…`），gemm 447 行 `7a451491…` 与 conv 542 行 `09e9b570…` **逐字节不变**。
- **归属方法（本相的方法论收获）**：每条控制先常规跑，再用**影子 `SOURCE_DIR`**（把 `samples/`、
  `workloads/`、`src/runner_*.{cpp,mm}` 与重新发射的产物复制进去）让 `[R5.4]` 恒真后重跑，
  红才能归到具体条款。它当场抓出两个我的错误：P3 在"相邻断言"版本下**不红**（改成序列后才红），
  以及 P1 的替换串没匹配上＝**惰性控制**（现先断言字节真的变了，否则报 `INERT`）。
- **仍然看不见的东西**：P3 造出的产物编不过（`pin` 先用后声明），ctest 里没有任何腿能发现——
  文本臂不编译产物。缓解是 `probe/phase48_artifact_build_run.py`（已指向 phase49 基线，
  三份 `build rc=0 verify=PASS`），它要 hipcc，所以按设计不进 ctest。

## Phase 50 — 第 7 条门 `hip_programs`：生成的程序第一次被编译器解析

- **结构性缺口**：acceptance 的臂把发射产物当文本读，门 #6 只编译 runner 的 TU，
  所以"产物编不过"在 ctest 里零可见度——Phase 49 的控制 P3 就是活例（我当时记成"仍未闭合"）。
- **门的形状**：CLI 现场发射三个算子 → `hipcc -fsyntax-only -O3 --offload-arch=<设备自报 gcnArchName>`；
  架构由 host-only 探针问设备（与 `src/runner.cpp` 的 `kArchProbeSrc` 同形，零 `__global__`），
  因为目标三元组错了就不是在检真机那份代码（E12 同源）。门第一行是开火控制
  （故意写坏的 C++ 必须被拒绝）。
- **三条控制期望各不相同**（这是本轮最讲究的部分）：K1 `e0`→`ze0`，没有任何臂提到 `e0`
  → 归属后 acceptance GREEN 而门 RED（编译器 `use of undeclared identifier 'e0'`），
  证明覆盖面；K3 只改一行注释 → 两边都绿，证明它不是"任何字节变化都红"；
  K2 = Phase 49 的 P3 → 两边都红，说明新门与文本臂有意重叠而不是互相替代。
- **跳过语义是实测的**：无 hipcc → exit 77 印 `no hipcc`；有 hipcc 但看不见设备 → exit 77 印
  `UK_ARCH_FAIL no ROCm-capable device is detected`；`SKIP_RETURN_CODE 77` 让 ctest 报
  **Skipped 而非 Passed**。`--arch <name>` 是给无卡主机的旁路，输出里明标 override。
- **成本**：本门 7.8 s，`ctest` 全套 12 s。仍然只是一次性付得起的钱——如果它涨到分钟级，
  按本仓"慢门等于跳过门"的口径就得重新设计。
- **不越界的说法**：语法检查不等于 `verify=PASS`；上机执行由
  `probe/phase48_artifact_build_run.py`（存盘字节自己编译运行）与 `run --vendor amd` 负责。
- 我自己的错（E51）：第一版给 K1/K3 写了"acceptance 保持绿"这个**结构上不可能**的期望，
  因为忘了 `[R5.4]` 之后任何生成器改动都会先红。控制第一次跑"不符合期望"时，先怀疑期望。

## Phase 51 — G13 拆开：OpenCL 查设备容量，Metal **没有可诚实查询的 API** 就保留常量

- **OpenCL 侧**：`plan_reduce_width()`（`src/runner_ocl.cpp:190`）=
  `CL_DEVICE_MAX_COMPUTE_UNITS × CL_DEVICE_MAX_WORK_GROUP_SIZE` 作容量 → `uk_pow2_floor` →
  256 地板 → 由 L 封顶；查询失败退回 4096 且 `source=props_unavailable`（不静默）。
  **这个代理量与 HIP 的 `maxThreadsPerMultiProcessor × CUs` 是同类事实但不是同一个量**，
  注释里写明，等 Intel 真机第一次跑时按读数重判够不够。
- **Metal 侧**：`plan_reduce_width` `src/runner_metal.mm:136` **保持常量**，来源标签改成
  `metal_no_capacity_api`。这不是偷懒而是纪律：把常量包装成"设备自报"正是本仓反复禁止的那类错。
- **共同词汇比"对齐数值"更值钱**：三后端同一个 `UK_REDUCE_WIDTH`，同一套
  `source=env` / `what=reduce_width_pin_unfit` + 拒绝执行 —— 跨后端宽度 A/B 有共同口径，
  且这套语义在本机（无 Intel/Apple 设备）也能被证明与测试。
- **顺带修掉 rule 6 在 runner 侧的同形缺口**：两个 runner 的 verify 段原先各自
  `reduce_width(VL)` 重算宽度，现在复用计时趟的 `plan.P`（仅因 verify 实例更短而封顶）。
- **控制归属表**（`docs/evidence/phase51/controls.txt`）：R1 丢地板→文本腿红；
  R2 不拒绝→文本腿红（红在 `what=reduce_width_pin_unfit`）；R3 打错设备查询里的变量名→
  **文本腿全绿、门 #6 红**。三条期望各不相同，说明三台仪器覆盖面确实不同。
- **本机的诚实边界**：Intel/Apple 侧**零数值读数**；AMD 侧 reduce/conv 真机未回归
  （`verified=PASS max_rel=0 migrations=0 checks=6`，`source=device width=16384 cap=28672`）。
- 门 #6 的本轮第二次价值（E52）：我把真换行写进被 `#ifdef` 掉的 C++ 身体里，
  构建与 ctest 都看不见，门 #6 0.1 s 报出并指到行号——同族第三次，这次由仪器接住。

## Phase 52 — 上一相的"覆盖面边界"有一条没测：OpenCL 产物在 AMD 主机上就能解析

- **实测命令与读数**（本机 `apple=no intel=no`，但这条只依赖前端是否存在）：
  `D:\Program Files\AMD\ROCm\7.1\bin\clang.exe`（ROCm 7.1 自带）用
  `-fsyntax-only -x cl -Xclang -finclude-default-header` 解析三份发射出的 `uk_intel.cl`
  ——gemm 53 行 / 2594 B、conv 32 行 / 1877 B、reduce 31 行 / 1708 B，**全部 rc=0**；
  往末尾追加一个坏 kernel 立刻 rc=1。⇒ 不需要 Intel SDK、不需要 Intel 设备、也不需要 `ocloc`。
- **门 #7 的形状由此改成"每个后端三段"**：可用性探针（`find_hipcc`/`find_clang`，能力由探测决定）
  → 该后端自己的 broken-source 控制（良好源码必须被接受**且**坏源码必须被拒绝）→ 同一份生成结果的
  真产物。`checked=6` = 3 op × 2 后端；时长 7.8 s → **9.78 s**，全套 `ctest` 约 14 s / 7 条门。
- **控制的两条报警方向都能响**（用假前端直接调 `control()`）：恒接受 →
  `BROKEN source ACCEPTED - the sub-check would be decorative`；恒拒绝 →
  `sanity source refused (rc=1): the checker is not usable here`。只有单向输出的控制等于没有控制。
- **跳过语义收紧**：77 的条件是"**一段都没跑**"。剥掉 `HIP_PATH` 与 PATH 后两个后端各打一行
  `PROG SKIP … reason=`，`RESULT SKIPPED checked=0`，rc=77；MSL 那一半永远显式打
  `PROG gap msl reason=…`，不会静默变绿。
- **`.cl` 是纯 kernel 源码，没有宿主 `main`**：所以 `UK_REDUCE_WIDTH` 在 `uk_intel.cl` 里出现 **0** 次 （HIP 那份 9 次）。**这不是硬约束第 4 条的洞**——Intel 侧宽度自报/拒绝在 `plan_reduce_width` `src/runner_ocl.cpp:190`
  的 `plan_reduce_width()` 里，产物本身不选宽度。记下来是因为这个"0 次"看上去极像缺口。
- **行列号能反查控制**：Q1 把 conv kernel 签名行尾的 `,` 换成 ` }`。入库副本 `emit/conv/uk_intel.cl`
  第 15 行长度 **72 字符** ⇒ 那个 `}` 正落在**第 73 列**，与 clang 报的 `15:73: error: expected ')'`
  一致。这是"报错真的指向这份字节"的最小交叉核对。
- **锚点唯一性第三次以同一形态出现**（E21/E24 同族）：`int M, int N, int K) {` 在
  `src/codegen_hip.cpp` 有 **2** 处（1054 的 device kernel、1089 的宿主 `cpu_ref`），
  预检查 `found 2 times -- aborting` 才没让控制悄悄改到另一处。
- **两条本轮新记的边界**：(1) OpenCL 产物**只有基线没有腿**——`[R5.4-artifact-bytes]` 只钉 HIP 三份，
  `.cl` 的字节比较腿（含它自己的摘除控制）未做；(2) MSL 仍是真缺口，本机没有任何 Metal 前端。
  **（演进：(1) 由 Phase 53 补上；(2) 的语法侧在 Phase 54 变成探针读数（本机两个 clang 都不认
  `-x metal`），字节侧由 `[R5.4-artifact-bytes-msl]` 关掉。本相写这两条时它们都还成立。）**

## Phase 53 — OpenCL 产物拿到字节锚；"归属装置必须覆盖全部字节腿"是一条实测

- **洞是真的**（先证明再补）：改发射出去的**一句 OpenCL 注释**，Phase 52 的全部仪器一件都不红——
  门 #7 的 Q2 期望本来就是 GREEN（它明说自己不是字节指纹），HIP 字节腿不看 `.cl`。
- **新臂 `[R5.4-artifact-bytes-cl]`**（`tests/acceptance.cpp:3053`）：CLI 现场发射三份 `uk_intel.cl`
  与入库基线逐字节比较；逐 op 先打印 `identical`/`DRIFT` 与字节数，**再**断言
  `identical`（`tests/acceptance.cpp:3052`）。
  打印先于断言的实测理由：一个二进制里有两条字节腿时，CRT assert 会让后一条**根本没机会说话**——
  影子 replay 里"逐 op 三行一行都没打印"就是这条差别的样子。
- **四条控制的读数**（`docs/evidence/phase53/controls.txt`）：
  C1 改 OpenCL 注释 → acceptance **RED**、HIP 腿 banner 在、`drift=['reduce']`、
  `identical=['conv','gemm']`、门 #7 **GREEN**（新腿专属）。
  C2 conv 签名塞 `}` → acceptance RED、`drift=['conv']`、门 #7 RED（ocl 半边）——重叠，如实记。
  C3 改 HIP 注释 → acceptance RED、HIP banner **不在**、新腿未跑到（无 per-op 行）、门 #7 GREEN。
  C4 改生成器不发射的注释 → 全绿、`identical=['conv','gemm','reduce']`（中性对照）。
  四条都与事先写死的 `want` 一致；成本 acceptance 2.0 s → 2.4 s。
- **方法上最重要的一条（有读数）**：新腿落地后，影子 `SOURCE_DIR` 只中和 **HIP** 那一份基线 ⇒
  `[R5.4-artifact-bytes-cl]` 在**每个**影子控制上因"基线文件不存在"假红。
  `probe/phase53_shadow_pre_fix_replay.py` 把这件事测出来了：影子树带 `.cl` ⇒ acceptance **rc=0**；
  只删掉那三份 `.cl` ⇒ **rc=3221226505**，`Assertion failed: !fresh.empty() && !stored.empty()`
  （臂 `[R5.4-artifact-bytes-cl]` 的 `tests/acceptance.cpp:3053`），且逐 op 行 **0** 条。⇒ 修**装置**（`shadow_root` 现在也种 `.cl`，
  `probe/phase49_widthpin_removal_probe.py:113`），Phase 52 的 Q1/Q2/Q3 期望一条都不必改就重新全中。
  **如果当时去改期望，改的是仪器故障而不是被测性质。**
- **期望的粒度=控制的粒度**（E55）：`split()[2]` 应为 `[1]`，于是"漂移到哪个 op"读成字面量 `DRIFT`；
  是预登记里**具体到 op 名**的期望抓住它的。写成"acceptance 应该红"就会安静放过。
- **两条分支都跑过**：`SOURCE_DIR` 未设 ⇒ 两条字节腿并列打印 `SKIPPED`、rc=0（不是静默通过）。
- 基线**没有被覆盖**：2594 / 1877 / 1708 B 与 Phase 52 存盘逐位相同。

## Phase 54 — Metal：语法是**主机事实**（本机两个 clang 都不认这个语言，有读数），字节是本机就能钉的

- **本机没有任何 Metal 前端**，而且原因不是"缺头"：门 #7 逐个探可及的 clang，
  两步走（先问语言、再问头）。两个候选都在第一步被拒——
  `D:\Program Files\AMD\ROCm\7.1\bin\clang.exe`（21.0.0git，`Compute-Mirrors/llvm-project` 裁剪版）与
  `D:\git\c\wasi-sdk-34.0-rc.3-x86_64-windows\bin\clang.exe`（23.1.0-rc3，只有 wasm 目标）：
  `clang: error: language not recognized: 'metal'`；`-x metalpp` 同样被拒。读数在
  `docs/evidence/phase54/msl-probe.txt`。**以前那句 gap 理由是断言，现在是前端自己的话。**
- **形状上的改进**：认 Metal 的主机上同一段代码会自动继续（控制 → 三份 `.metal`），
  跳过行不会伪装成绿，也不需要有人记得在未来改代码。
- **新臂 `[R5.4-artifact-bytes-msl]`**（`tests/acceptance.cpp:3117`，断言 `[R5.4-artifact-bytes-msl]` 的 `tests/acceptance.cpp:3117`）：
  字节比较不需要前端，所以"MSL 产物被改过"这件事在本机从今天起可见。基线入库
  `emit/<op>/uk_apple.metal` = 61/40/36 行、**2893 / 2105 / 1885 B**，sha256 记在 README。
- **三条控制的读数**：M1 改发射出去的 Metal 注释 → acceptance RED、`{gemm:identical,
  conv:identical, reduce:DRIFT}`、HIP 腿打印、CL 腿 `3/3`、门 #7 **GREEN**
  ⇒ **全套仪器里只有这条腿看得见 MSL 产物**。
  M2 改发射出去的 OpenCL 注释 → CL 腿先 assert（banner 缺席）、**MSL 腿一行都不打**
  ⇒ 顺序会伪装成归属，"先打印再断言"第二次被当作必要条件而不是风格。
  M3 改生成器自己不发射的注释 → 三条腿全 `identical`、全绿。
- **归属装置的规则第二次生效（这次是复验）**：`shadow_root` 现在种 `uk_amd.cpp` + `uk_intel.cl` +
  `uk_apple.metal` 三份；补完后**重跑 phase49/50/51/52 四条旧影子控制套件**，
  它们的 `want` 一个字节没改就仍然全中（`all 3 controls matched` 各一次）。
- **一条运气，写下来是因为下次不一定有**：Metal 与 OpenCL 的 reduce 注释文本本来不同
  （`one per thread of the launch.` / `one per global work item.`），
  所以 M1/M2 天然各属一个后端。若两处文本相同，同一控制就会同时红两条腿，归属要另想办法。
- **仍未 exercised 的一半**：Metal 分支的"控制 + 真产物"路径本机无法执行（前端不存在）；
  `.metal` 从未被任何前端解析过，Apple 执行证据只在 `docs/evidence/phase10/`（macOS 主机）。

## Phase 55 — 字节红不等于轴红：非 HIP 两个后端的累加次序由 `[R20-nonhip]` 钉住

- **区分是什么**：硬约束第 3 条说改算术顺序 = 改结论。HIP 侧有 `[R20]` 按字节钉嵌套；
  OpenCL/Metal 侧此前**没有任何臂**——改折叠次序与改一句注释在仪器上是同一种红（都只说"字节变了"）。
  后果不抽象：Intel/Apple 的入库读数来自 Phase 9/10 的那几段嵌套，次序一动它们就不再可比。
- **臂的形状** `[R20-nonhip]`（`tests/acceptance.cpp:2865`）：进程内调用 `ocl_kernel_source`/`msl_kernel_source`
  （**不新增 CLI 发射**），2 后端 × 3 算子 = 6 个 pin，每个 pin 钉两件事：一条**单调锚点序列**
  （conv `c→kh→kw→FMA→存储`；reduce `s=0→X 累加→P 写→P 折叠→Y 写`；gemm `k0→k→r/c→FMA→存储`）
  与**累加语句恰好 1 次**。序列而非相邻（E50）、次数而非存在（E21/E24）。
  臂**自己数打印的判决行数**（`assert(printed == 6)`，`tests/acceptance.cpp:2836`）：
  一条少打印的臂等于没有臂（E38 的形状——恒真判据可以绿很多相）。
- **归属方法换了，且是实测换的**：CRT 消息带**表达式 + 行号**，探针运行时读
  `tests/acceptance.cpp` 的 `  // -------- R` 臂头表把行号映射回拥有它的臂。这比"哪条 banner 没出现"
  强——后者只知道谁先跑。本轮读到的臂表：`R20-nonhip 2747-2845`、`R5.4-cl 2846-2909`、
  `R5.4-msl 2910-2976`。
- **五条控制的读数**（`docs/evidence/phase55/controls.txt`）：
  N1 **只在 `ocl_conv_kernel` 内**把 conv 外层 `c` 循环改倒序 → RED，表达式 `order_ok == 6`，
  归属 `R20-nonhip`，broken **只有 `ocl/conv`**（`msl/conv` 仍 `order=ok`），门 #7 **GREEN**
  （产物仍是合法 C++）⇒ 一次"只有次序臂会说话"的真改动。
  N2 改发射出去的 OpenCL 注释 → 红归 `R5.4-cl`，次序臂先打印 6 条全 ok；
  N2b 改 Metal 注释 → 红归 `R5.4-msl` ⇒ 两条字节轴也分后端。
  N3 改 HIP 注释 → 红归 `R5.4`（表达式 `fresh.size() == stored.size()`），次序臂**一行都不打印**
  （它排在后面）＝方法的局限，如实记成局限而不是"独立性证据"。
  N4 改生成器自己不发射的注释 → 全绿。
- **控制设计上的两次自我修正**：(1) 原打算对调 `kh`/`kw` 两行循环头——会破坏 `{`/`}` 配对，
  把门 #7 一起拖红、归属变脏；改成反转外层 `c`（合法 C++、真实折叠序变化、门 #7 保持绿）。
  (2) `kh`/`kw`/`c` 在 `conv_msl_kernel` 与 `ocl_conv_kernel` 各出现一次（整文件计数 2），
  补丁必须**按函数切片**并断言片内恰好一次；整文件 `replace(...,1)` 会命中先出现的那份（Metal），
  然后把红报给 OpenCL（E50 惰性/别名与 E21/E24 次数的合流）。
- **本相最值钱的副产品是一条仪器盲区，且有读数**：新臂插在字节腿之前 ⇒ Phase 53/54 写下的
  `tests/acceptance.cpp` 五个引用（Phase 53/54 当时写在 :2747/:2787/:2800/:2811/:2863）整体偏移约 99 行，
  而门 #3 报 `anchors checked: 273 … all agree`（**全绿**）。原因是它的规则本身：
  只有当引用所在行带有**可核对的代码符号**时才核 ±25 窗口，而那些行带的是
  `[R5.4-artifact-bytes-cl]` 这类方括号标签，被符号过滤器排除 ⇒ 检查在这一点上**空转**。
  ⇒ "无法核对的引用必须显式计数或失败"记为下一相的靶子；本轮先按 E23 全部 grep 现取改指。
- **两处自己的期望缺陷（E56/E57）**：判决行在转录落盘**之后**才打印 ⇒ 入库的 `controls.txt`
  不含自己的结论；把"六条都 ok"塞进 `want["broken"]`（该字段按定义只收破的行）⇒ 三条控制
  结构上永远不可能匹配。两条仍属"期望要具体到字段"这一族（E55 继续生效）。
- 成本：门 #2 2.4 s（6 次进程内生成，无额外 CLI 发射）；`ctest` **7/7**，守卫 byte-identical。
## Phase 56 — 记录与代码之间的核对仪器：臂标签归属 + 两条只降不升的上限

- **仪器的洞有多大（先数）**：落地前实测 288 条 `path:line` 引用，**132 条**所在行没有任何可核对
  符号；本仓引用臂的写法 `` `[R5.4-artifact-bytes-cl]`（`path:line`）`` 里的方括号标签被旧的符号
  过滤器排除，所以那 132 条从来没被核过，而门报 `all doc->code anchors agree`。
  Phase 55 已经付过一次账：插一条新臂把 5 个引用挪了约 99 行，门全绿。
- **改成三层**（`probe/anchors_check.py`）：邻接符号 → ±25 窗口；**臂标签 → 臂段归属**（标签映射到
  `  // -------- R` 臂头表**加上该臂自己 `std::cout` 打出的 banner**——只按头表会漏掉长形 banner，
  只按"标签在文件里出现过"就又退回存在性判据 E21/E24）；段落有符号而引用无邻接符号 → **weak**；
  附近什么都没有 → **unverifiable**。后两层各有**一条只许下降的上限**。
- **抓到 19 条真实陈旧引用，一次运行**：横跨 `docs/acceptance-mvp.md`(3)、`docs/evidence/phase24|26|31`
  各 1、`findings.md`(5)、`progress.md`(2)、`task_plan.md`(5)、`AGENTS.md`(1)。
  **没有一条是"标签指向不存在的臂"**，全是行号随代码增长漂移。全部 grep 现取改指，并保留
  "当时行号"注记（转录不改写）。其中一条是**跨文件指控**：HIP 侧的 `dtype_tol_literal` 被邻接到
  `src/runner_ocl.cpp` 的旧行号——runner 真正读的是 `tolerance_for`（`src/runner_ocl.cpp:683`）与
  `tolerance_for`（`src/runner_metal.mm:623`），已改写成两句各带自己的符号。
- **棘轮当天就抓到作者自己**：上限写成 weak 116 / unverifiable 93 之后，本相新写的记录把 weak 顶到
  **117**，门立刻红。修的是**引用写法**（给引用配邻接臂标签），不是上限；收口时上限降到
  **weak 109 / unverifiable 91**，同时臂段归属核对从 34 条升到 **43** 条。
  窗口 ±25 全程没有放宽——所有新红都来自规则，不是阈值。
- **四条开火控制互不相同**（`docs/evidence/phase56/controls.txt`）：L1 标签指向别的臂段 → 红，消息
  同时给出"标签主人"和"行主人"；L2 同一标签指到自己段首 → 绿且两个计数不动（没有 L2，
  "任何标签都指控"与"归属错才指控"分不开）；L3 符号在上一行 → weak **+1** 并被上限压红；
  L4 整段无符号 → unverifiable **+1** 并被上限压红。临时注入文件写在 `finally` 里删除——
  留一条引用就会改变全套报告的数字。
- **仍然打开的债（如实）**：weak + unverifiable = **200 条**引用今天仍没被核过内容；上限只保证
  不再增长。逐条偿清要靠给引用配邻接符号或臂标签，这是有意留下的可见债务，不再是一片沉默。
- 另记一条方法上的重复：**E58** ——第四次把代码文本经非 raw 的 Python 字符串搬运，`
` 被吞成真换行，
  写出 `SyntaxError: unterminated string literal`；这次的差别是"写完立刻运行"一个回合就拦下。
- `ctest` **7/7**（门 #3 0.13 s，293 条引用 / 50 份文档）。
## Phase 57 — 次序之外还有形状：`[R20-nonhip-expr]` 把两个非 HIP 后端的索引式按字节钉住

- **轴的区分**：`[R20]`（HIP）钉的是整段嵌套的**字节**，次序与表达式一起在内；Phase 55 的
  `[R20-nonhip]` 只钉**次序**。于是 `((ho + kh) * W + (wo + kw))` 换成 `((kh + ho) * W + (kw + wo))`
  ——循环、次序、FMA 位置都不变——只让字节腿红，而字节腿不说"算术形状变了"。第 3 条要求的正是这句区分。
- **臂 `[R20-nonhip-expr]`**（`tests/acceptance.cpp:2964`）：6 个 pin（2 后端 × 3 算子），每 pin 是逐字节多行片段，
  次数必须 = 1（E21/E24），逐 pin 先打印 `expr=ok/BAD + counts` 再断言，且臂自己数打印行数。
  片段**从当前发射文本 grep 现取**（含缩进），凭记忆写会在下一拍被门 #3 抓（上一相刚示范过）。
- **两条控制才证得开两根轴**：E1 在 `ocl_conv_kernel` 内改索引式 → **次序臂 6/6 干净 + 表达式臂点名
  `ocl/conv`**（同一次运行里两臂各自表态，这是最强的归属形状）；E3 反转同一函数里的 `c` 循环 →
  次序臂红、**表达式臂沉默**（它排在后面，abort 之后没有机会说话）。E2 只改注释 → 两臂全绿、红归
  `R5.4-cl`；E4 中性。⇒ 单条控制无法同时给出"两轴各自能单独红"，这个成对关系是设计出来的，不是顺手。
- **上一相的仪器第一次上岗（有读数）**：新臂把两条字节腿推后 **89 行**，`ctest` 立刻报门 #3 失败，
  点名 **13 条**引用，其中 5 条走的是新加的臂标签归属规则
  （`R5.4-artifact-bytes-cl owns R5.4-cl (2935-2998) but line 2846 falls in R20-nonhip-expr`）。
  全部 grep 现取改指（`2846→2935`、`2886→2975`、`2899→2988`、`2910→2999`、`2962→3051`）。
- **棘轮对新文档也响（两次）**：本相 README/结果段引入的 `path:line` 有两条所在行没有可核符号，
  `unverifiable` 91→92 直接压红。改法是写引用时把符号或臂标签放在**同一物理行**
  （`[R20-nonhip-expr]`（`tests/acceptance.cpp:2964`）），**上限一格没调**。
  ⇒ 记 E59：写文档与写代码同律——引用形状要就地可核；且文档改完立刻跑门 #3（它 0.15 s）。
- 自己的期望又错一次：预登记断言"E1 时次序臂一行都不打印"，实际次序臂先跑且干净——**先怀疑期望**
  这条第四次生效。
- 边界：钉的是发射文本的形状，不是等价性；等价改写同样会红，红不解释等价，等价只能靠真机重采。
- 成本：门 #2 2.4 s → 3.0 s 之间（6 次进程内生成，无额外 CLI 发射）；`ctest` **7/7**，
  守卫 byte-identical，三条基线字节未动。

## Phase 58 — 组引用的每个数字都被核：门 #3 的第二类静默段，实测 30/40 从未被看

- **数出来的洞与上一相不同**：不是"引用行没有符号"，而是**一条组引用只核第一个数**。
  `ANCHOR` 原先匹配 `路径:行号(-行号)?`，本仓把多个行号写在同一个路径后面的组引用共 **15 处**，
  40 个数字里 **30 个**（占全部行声明的 9%）从不进入核对。`expand_citations()` 逐数拆开后读数：
  行声明 310→**363**、尾巴 0→**31**、臂归属核 90→**99**、两档计数债 80/69 → **61/59**
  （**读数在记录全部写完之后取**：本相每写一条引用，分母就涨一次——仪器的覆盖面是被记录喂大的）。
- **"收紧"必须自己可观测，否则它是一条更贵的错**：第一版把 `prev_end` 在组头部就推进到组尾，
  尾巴拿到空 ctx ⇒ `weak` 80→89、`unverifiable` 69→90，而 `anchors checked` 涨到 340 看着像"覆盖变宽"。
  **`arm-range checks` 一字不动就是唯一能识破它的读数**；修好（组内共享 ctx）后 90→98。
  这条已经写成注释与控制 G3（要求绿 **且** debt delta=0）。
- **第二个仪器错法：按切片配反引号**。`code_tokens(ctx)` 的 `ctx` 是切出来的子串，而子串常常从上一段
  span 的**收尾反引号**开始 ⇒ `TOKEN` 把 "`）两处发射、`" 配成一对，真正的 `` `initA` `` 不在任何配对里。
  后果不是误报而是**漏报**：同一行里第二个及以后引用的符号全部隐形。改成整行配对 + 按绝对位置过滤
  （`code_tokens(line, lo, hi)` / `arm_labels(line, lo, hi)`）后 `weak` 83→**61**（22 条复活）。
- **漏报保护了一个不存在的符号**：`docs/acceptance-mvp.md` 的 R6.3 一句把 P4 不变量写成"某符号名在
  main.cpp 的 557 行"，`grep -rn` 证明**该标识符只出现在那一行文档里，代码中从未存在**，而 557 行其实
  是 `return 0;`。不变量为真、位置为真：拒绝在 `r.verified`（`src/main.cpp:540`），提交行打印在 553 行。
  ⇒ E21/E24 的对偶：**多项目只验一项** + **符号看不见** 两个盲区叠起来，一个假符号就等价于一个真断言。
- **重读债目抓出的其余三条**（都是收紧当场照出来的，不是先改文档再找理由）：三后端 init 的四处行号
  全错（现为 `initA`（`src/codegen_hip.cpp:604/1122`）、`initA`（`src/runner_ocl.cpp:136`）、
  `initA`（`src/runner_metal.mm:87`），HIP 侧多一个 `static`，"表达式逐字相同"成立）；
  dtype 允许表被写到 `op` 那行（现为 `dtype`（`src/kml_front.cpp:106/108/112`））；
  `dtype`（`src/kml.cpp:107/117`）两个数其实都对，只缺邻接符号。
- **占位符不是符号**：写本相记录时用 `path:NNN` 描述规则本身，被当成符号指控了五条真引用；
  `code_tokens` 增加大小写敏感的 `小写:全大写` 排除（`SKIP_TOKEN` 带 `re.I`，不能用它——`std::cout`
  会被吃掉），并配三双向 self-test：占位符必须被拒、`` `initA` `` 与 `` `std::cout` `` 必须留下。
- **检查文档的仪器也会过期**：`probe/phase56_anchorlabel_control.py` 写死的臂首行号（2846）被
  Phase 57 的新臂作废，L2 于是注入了一条**真的错位**引用并打印 `CONTROLS THAT DID NOT BEHAVE`；
  现在每次运行从 `arm_table()` 现取（本轮 own=2936 / neighbour=2847），找不到就 `sys.exit`。
- **围栏内的转录不修**：仓里 3 条位于 ``` 块内的 `path:line`（phase31 证据 README）按"转录不改写"
  显式跳过并点名原因；若将来顶红 cap，规定动作是在围栏**外**补一句带符号的活引用，不动围栏内容。
- 边界：门 #3 核"那句话的符号是否出现在被引号附近"，不核语义、也不核那一行是不是句子真正想指的函数。
  剩余 120 条计数债（`docs/evidence/phase58/debt-list.txt` 全量）就是这一层的活；棘轮的作用不是让它们
  变绿，而是让"没人看"保持是一个数。
- 成本：门 #3 0.15–0.2 s；`ctest` **7/7**，三条字节基线与 `[R20-nonhip-expr]` 的六个片段一字未动。

## Phase 59 — 计数债逐条重读：20+ 个错行号浮出水面，门 #3 学会读"引用之后的主语"

- **并排印再判断**（`probe/phase59_debt_triage.py`）：120 条债按"引用行今天是什么"分类后是
  dead 33 / 引用行无 ≥5 字标识符 6 / 表格行 19 / 隔空行 10 / 列表标记开头 8 / ``` 围栏转录 3。
  分类本身就是产出——它决定了每条债要"补符号"还是"重指"还是"降级为文本"。
- **最大的一条发现：多数 dead 不是缺符号，而是行号漂了。**逐条 grep 现取后的对照：
  计时段 d2h 发射点写 `:887` 8 处（今天是 `hipMemcpyAsync` 那句，`src/codegen_hip.cpp:946`）、
  差分验证实例写 `:840` 3 处（`Differential` 在 `src/codegen_hip.cpp:916`）、
  conv/gemm 的 init 精确性注释写 `:587` 4 处（`max_rel` 那句在 `src/codegen_hip.cpp:610`）、
  "the partial FREEZES" 写 `:739` 2 处（`FREEZES` 已在 `src/codegen_hip.cpp:795`）、
  CPU 参考同口径写 `:805`/`:562`/`:530`（现分别为 `cpu_reduce_ref` 在 `src/codegen_hip.cpp:840`、
  `cpu_reduce_blocked` 在 `src/runner_ocl.cpp:660` 与 `cpu_reduce_blocked` 在 `src/runner_metal.mm:593`）。
  这些号码在整个 Phase 34–57 期间一直"绿"，因为它们的句子**从来没有可核邻接符号**。
  ⇒ 计数债的价值不是好看，而是把"没人看过"变成可操作的清单。
- **反例（行号是对的，只是没人配符号）**：Metal 的宽度旋钮在 `plan_reduce_width`（`src/runner_metal.mm:136`）
  这一行——136 一直是对的，AGENTS 那条只是缺主语。区分这两类是逐条重读唯一的意义，抽查会混。
- **仪器新能力：有界尾随归因**。文档写「引用在前、主语在后」，旧规则因怕串味而不看引用之后的文本；
  现在看，但硬截断在子句分隔符（`、,;；。）|`）、下一条引用、40 字。控制 H1–H5（`trailing-controls.txt`）：
  形状对→绿不计数、数字错→红且**报文点名**、一行两条款只报错的那条、`、` 之后不归因、>40 字不归因。
- **真空洞：停用词当符号**。还债工具提议插 `double`（`code_tokens` 原先接受它）——它在每个 C++ 文件里
  都有，插进去那条引用永久绿。现在 `code_tokens` 拒绝 STOPWORD，`self_test()` 双向三对：占位符拒、
  `initA`/`std::cout` 留、`double`/`static`/`return` 拒。
- **自动修法的人审结果**：`phase59_debt_adjacency.py --dry` 的 8 条候选里 3 条不能落笔——
  `double`（停用词）、`kRefReduceWidth`（从**相邻表格行**借的词，不是本行的主张）、
  `uk_reduce_part`（那句主语是 `uk_reduce_sum`）。工具补了"表行/引用块/围栏不跨越上下文"的护栏，
  本相**没有一条自动写盘**，全部手工重写 ⇒ "先 dry 再人审"这条规则第一次真正改变了结果。
- **只改换行的 join 是安全的自动化**：`phase59_debt_rewrap.py` 把被折行的引用并回上一物理行，一字不改；
  7 处成功；它拒绝表行、围栏、引用块与列表标记开头的行（并入会吞掉 `- ` 让条目变正文）。
- **控制对常数的假定也会过期**：第 56 相的控制假定"多一条 weak 必然压红"，债一降 cap 就有余量，
  它于是误指控门；现改为现读上限（E63）。同一次修复还暴露了 `run_gate` 的分组下标错——
  打印出来的"unverifiable=61"其实是 weak 的上限。
- **读数**：weak 61→**37**、unverifiable 59→**46**、债 120→**83**、被核行声明 329→**383**。
  剩余 83 条约 80 条属于"断言对象是注释文本"，要再降必须重写句子去引用函数名（内容工作）。
- 成本：门 #3 0.2 s；`ctest` **7/7**；三条字节基线、`[R20-nonhip]` 与 `[R20-nonhip-expr]` 的片段未动。

## Phase 60 — 注释文本型债务与臂名里的 `/`：判据的词法必须等于文档的词法

- **11 条 dead 债的真面目**：8 组是**行号又漂了**，3 组是句子引用**发射出去的注释文本**（那一行确实是
  所指对象，但里面没有 ≥5 字标识符可配）。重指的八组，每条各配自己的主语（完整对照见
  `docs/evidence/phase60/README.md`）：
  `uk_conv_plan`（`src/codegen_hip.cpp:435`）、`FREEZES`（`src/codegen_hip.cpp:795`）、
  `hipMemcpyAsync`（`src/codegen_hip.cpp:946`）、`cpu_reduce_ref`（`src/codegen_hip.cpp:840`）、
  `cpu_reduce_blocked`（`src/runner_ocl.cpp:660`）、`cpu_reduce_blocked`（`src/runner_metal.mm:593`）、
  `epsilon_rel`（`src/main.cpp:552`）、`parse_int_wide`（`src/kml_front.cpp:127`）、
  `max_ws`（`src/runner_metal.mm:112`）与 `ws_refuse`（`src/runner_metal.mm:213`）。
- **四条的"结论"本身也已过期**（文件侧上界校验、Metal G6 交叉校验、D9 运行时宽度、CPU 参考第二档）。
  ⇒ 仪器只能保证"号码指的是什么"为真；**句子对不对要靠重读**。处理是历史降级为文本 + 现状态另给活引用。
- **判据词法与文档词法不一致＝假沉默**：有一条臂的标题就是 `R6.2/R6.4`（一条臂覆盖两条需求行），
  文档用 `[R6.2/R6.4]` 引用它共 6 处，而 `arm_labels()` 的字符类 `[A-Z][\w:.-]` 没有 `/` ⇒
  这 6 条被分类为"没有任何符号"。放行 `/` 后：臂归属核 99→**104**、`unverifiable` −6。
  两条新控制 L5/L6 各自证一次（L6 故意指向下一个臂，必须红且点名两个主人）。
- **控制自己不成立的一次现场演示**：L6 第一次注入写成没有方括号的 `` `R1.1/R1.2` `` ⇒ 门读不到标签 ⇒
  "该红不红"。补上括号才报双主人。⇒ 新控制第一次跑必须**人读它报的具体内容**（E38 形状）。
- **上一相留下的不完整修法**：`smem` 只有 4 个字母，过不了 `code_tokens` 的 ≥5 规则，于是 Phase 59
  那两条"改过"的债一字未降。⇒ 配符号的顺序是**先长度、后窗口**；本相改配 `uk_conv_plan`（418 行，
  在 435 的 ±25 内）才降。
- **读数**：债 83→**69**、`weak` 37→**30**、`unverifiable` 46→**39**、臂归属核 **104**。
  剩余 69 条中约 40 条需要重写句子（内容工作）、5 条引用行仍无 ≥5 字标识符、3 条围栏转录不改写。
- 成本：门 #3 0.2 s；`ctest` **7/7**；三条字节基线未动。

## Phase 61 — 债从 69 掉到 19：三个"判据读不到文档"的洞，没有一个靠重写句子

- **洞一（漏报，杀伤最大）**：`TOKEN` 要求 code span 内容 ≥2 字符。`` `Y` `` 是合法 Markdown，但正则
  拒绝配对它 ⇒ 扫描从它的**收尾反引号**继续 ⇒ **同一行后面每一对都错一位**，真正的主语
  （`hipMemcpyAsync`）被配成含空白的范围而丢弃。实测影响：9 条 `src/codegen_hip.cpp:946` 的引用
  全部被计成盲。**改法** `{1,80}`：短 span 仍被 ≥5 长度规则丢掉，唯一变化是配对不再错位。
- **洞二**：`SKIP_TOKEN` 带 `re.I`，其中 `^[A-Z]+\d` 本意是排除臂名/需求号（`R5.4`、`G10`），
  结果把**带数字的真标识符**一起吞了——`fp16_rel` 被丢弃，而它正是三条
  `include/unikern/components.hpp:133` 引用的主语。**改法**：拆出大小写敏感的 `ARMISH`。
  现在 `fp16_rel` 可核、`R5.4-artifact-bytes-cl` 仍不算符号，两个方向都进 `self_test()`。
- **洞三（我自己的工具）**：定位名的插入点用 `text.index(cited, m.start())`，落在引用**开引号之后**
  ⇒ 产出破 span **20 处**；门没报红、债数字还变好，只有逐行读 dry 打印才看得见（E68）。
- **摘除控制真的跑了两次**：还原 `{1,80}` ⇒ `GATE IS MUTE OR LOOSE: code_tokens('计时段的 `Y` …') = []`；
  给 `ARMISH` 加回 `re.I` ⇒ 同一位置报 `fp16_rel`。第一次"验证"被我自己写坏的临时副本骗过
  （rc=1 来自 SyntaxError 而非断言）——**报文内容才是证据**。
- **顺带重指 6 组过期行号**：main.cpp 557→540（`r.verified` 把关提交）、runner_ocl 434→516
  （`uk_ocl_log`）、codegen_hip 415→463、kml_front 89→106、codegen_hip 963→1014（`shard_rows`）、
  runner_metal 65-66→76（`maxThreadsPerThreadgroup`）。
- **19 条是下界，不是"待办清零"**：5 条断言发射文本（` source=env`、写死的 `0.03125f`、`SR*K`）、
  3 条是消息串、4 条在围栏/引用块转录内、5 条是"当时状态"的英文记录、2 条是本相写文档自己新增。
  **拒绝**用 `std::endl` 这类"能变绿但什么都不核"的词消音（E64 同族）。
- **一次性脚本一律入文件**：一条 heredoc 的重定向让 `cat` 读进自己的输出，写出 100 MB 垃圾文件
  （E69，已删）；此后批量改文档只走 `probe/phase61_*.py` 的显式列表 + 按文件门回滚。
- 读数：`weak` 30→**8**、`unverifiable` 39→**11**、债 69→**19**、被核行声明 **420**；
  三相连做（59→61）**120 → 19**。`ctest` **7/7**；三条字节基线与两个非 HIP 语义臂片段未动。

## Phase 62（2026-10-30）：R8.4 mmap 通路的实测事实

- **平台能力（RX 6600M / ROCm 7.1 Windows，本机探针）**：`MapViewOfFile` 视图上
  `hipHostRegister` 无论 Default 还是 Mapped 一律 `invalid argument`——驱动不能页锁定
  不归它管的文件页；`VirtualAlloc` 堆 + Mapped 注册 OK；`hipHostMallocMapped` +
  `hipHostGetDevicePointer` OK。⇒ **GPU 可见的文件映射在本平台不存在，映射设备指针读存在**。
  另：hipcc `--offload-arch=native` 在本机报 "No AMD GPU detected in the system"——
  这台机必须显式给架构（硬约束 2 的又一实例）。
- **映射读带宽**：LDS staging 语义下 A 的每个元素被 N/tile_n 个 block 重读；12416³
  （reuse=97，A≈616.7 MB，内核侧链路流量 ≈59.8 GB）实测 kernel-only 17.41 / 21.13 s
  ⇒ 映射读有效带宽 **3.44 / 2.83 GB/s**。模型的 mmap 成本项按 HCS 官方链路 32 GB/s
  定价 ⇒ **低估约一个量级**；支配性结论（mmap = pinned + nvme 项 + reuse 放大 ⇒
  C 对任何有限 nvme 选 pinned）不受影响，但成本数值在按实测标定前不可信。
- **仿真页真机数字（12416³，合成 vram=2GiB HCS）**：pinned 1378.09 ms（kernel 1214.89）/
  mmap 54383.98 ms（kernel 21126.60），双臂 `verify=PASS max_rel=0`；mmap 臂的验证段
  也走映射路径（rule 6 的 R8.4 形态）。checks：mmap 4（verify 无 dA、流式无 dA2）、
  pinned 6——计数按路径形态如实变化。kernel_ms 的 run-to-run 离散 ~21%（17.41 vs 21.13），
  与本机计时离散度已知事实同族。
- **账本路径身份（E70）**：env 钉臂的测量与解的规划路径不一致时 `LEDGER SKIP` 零落库；
  同键行实测列只能属于规划路径的测量。污染行由 pinned 复跑恢复。
- **磁盘约束**：D: 2.1 GiB 空闲 ⇒ 真超订流式形状（payload > 0.85×8 GiB 时 A 文件
  ≥ payload/3 ≈ 2.27 GiB）放不下；合成 vram=2GiB HCS 让 12416³ 真实触发流式，
  读数表征 mmap 数据路径、不表征真超订。真超订 mmap 读数留待有盘空间的机器。
- 读数：anchors 431 全绿（重指 70 条被插入推移的引用）；weak **8**/8、unverifiable **11**/11
  （棘轮未升，本相自己的两条 weak 靠把符号与引用放回同一行解决）；
  摘除控制三件全 PASS；`ctest` **7/7**。仍未提交。

## Phase 63（2026-10-30）：真实厂商模型形状与设备实测事实

- **GGUF 头事实（Qwen3-Embedding-4B Q4_K_M，2,496,703,776 B）**：arch=qwen3，
  398 张量；embedding_length=2560、block_count=36、head=32/kv=8、head_dim=128、
  ff=9728、context=40960、pooling_type=3（MEAN）、vocab=151665；每层 7 个 GEMM
  （q 2560→4096、k/v 2560→1024、o 4096→2560、gate/up 2560→9728、down 9728→2560）。
- **设备自报 vs 规格（E26 纪律的两次现身）**：①multiProcessorCount=14（RX 6600M 规格说
  16 CU）——设备自报胜出；②memoryClockRate=875 MHz（命令时钟）→ GDDR6 ×16 推导
  224 GB/s（规格 256）；③`clockRate`=1881 MHz 是探针时刻值——实测 GEMM 吞吐
  3.85–3.89 TFLOPS 高于按它推导的 3370 GFLOPS"峰值"。**承重数字只有真机实测。**
- **真实形状的链路读数（seq=512）**：五形状全 `verify=PASS max_rel=0`；
  kernel_ms：q 2.71 / o 2.61 / gate 6.64 / down 6.53 / reduce(1.31M) 0.43；
  出厂 HCS ε_rel 1.4–1.8 → calibrate（link 32→5.7479 中位）后 **0.0053–0.032**。
  e2e−kernel 的有效带宽 4.98–5.93 GB/s，与 Phase 15 的 5.43 同族。
- **reduce 行的校准离群**：reduce 行 copy_ms=0.207 ms 只含 10 MB H2D → eff_bw=50.7 GB/s
  噪声主导；中位数设计吸收了它。校准池按算子构成（compute-bound gemm 的 copy 项
  vs reduce 的微型 d2h）值得后续显式分层——记为观察。
- **模型级能力边界**：fp32 权重 16 GB > 8 GB vram；Q4_K_M 2.33 GiB 装得下但反量化
  不是原语；softmax/RMSNorm/批量池化不在三原语词汇表；生成物是自验证 harness 非
  运行时。512-token prefill 仅 GEMM 下界外推 ≈954 ms（≈537 tok/s）。
- KML 前端按 R1.2 拒绝未知字段 `comment`——声明文件里塞注释的"便利"被仪器挡下，
  出处说明必须住在证据 README。`ctest` 7/7。

- **审计范围**：对照 AGENTS.md 一句话目标（KML→Solve→Generate→Verify→Ledger）与 Requirements R1-R10，基于 Phase 63 证据（GGUF 解析、设备实测 HCS、5 真实形状真机 PASS、calibrate 闭环、模型级外推）。
- **差距 10 类**（按优先级）：
  1. **缺 Transformer 核心原语**（V1，Phase 64）：softmax、RMSNorm、注意力、Q4_K_M 反量化、逐通道缩放/批量归约。
  2. **fp32-only 限制**（V1，Phase 65）：需 fp16/bf16/dtype 轴 + 量化路径（Q4_K_M 反量化原语）。
  3. **KML 扩展支持新 dtype/新原语**（V1，Phase 65）：KML 前端/求解/生成全链路支持非 fp32 dtype + 新原语节点。
  4. **求解增强**（V1，Phase 66）：mmap 成本按实测带宽标定；缺能力直接 F 不可行（R10.2 合流）；calibrate 池分层。
  5. **生成物运行时形态**（V1+，Phase 67）：从自验证 harness 迈向 per-layer 内核库 + 图执行器（V1+）。
  6. **Intel/Apple 真机闭合**（本机可做/V1，Phase 68）：Intel conv/reduce 真机；Apple MSL 真编译上机。
  7. **量化生成路径**（V1+，Phase 69）：Q4_K_M / int8 / fp8 反量化原语 + 量化感知生成。
  8. **calibrate 池分层**（本机可做，Phase 66）：reduce 行噪声离群被中位吸收，建议显式分层。
  9. **仪器盲区修复**（本机可做，Phase 66）：`[R2]`/`[R4]` 个位数臂名盲区；calibrate 门分层。
- **验收**：`ctest` 7/7 全绿；anchors 457 绿；weak/unverifiable 棘轮 8/11 未升。本相产物未提交（纪律）。

## Phase 64 实测事实（2026-10-31，本机 RX 6600M / gfx1032）

- **三新原语真机读数**（65536×256，fp32，两次独立 run 取两行）：softmax
  verify=PASS max_rel=0 measured_ms 35.44/32.45 kernel_ms 19.62/19.01；
  rmsnorm PASS max_rel=2.072579e-07 measured 26.98/28.96 kernel 14.65/14.45；
  channelscale PASS max_rel=0 measured 14.05/13.91 kernel 8.10/6.78。
  容差门 2⁻⁵ 全部富余 5 个数量级以上。
- **求解器对三原语无校准**：c_ms 三者同值 6.68467（roofline 通用工作模型），
  epsilon_rel 实测 1.08–4.30——已按 R7.3 入账本等 calibrate，不是缺陷是待标定。
- **生成器缺陷家族**：三新 op 的 HIP 程序各自内嵌一份老式 `uk_mmalloc`
  （非 guard85 版），四处缺陷（重复声明 e / hipFree 误转型 / 计时段越界引用
  verify 块向量 / 多余右括号）在 ctest 七门下全绿地存活到真机编译才暴露——
  「新 op 落地必须同时进门 #7 的 SPECS」应成为纪律，否则文本臂永远看不见。
- **OCL C 地址空间**：行指针 `const float* x = X + row*C` 在 OpenCL C 是
  private 指针接 global 表达式，clang 拒绝；MSL 无此问题（device 限定写对了）。
  两个后端对同一缺陷的敏感度不同，语法腿两边都要。
- **MSL buffer 绑定纪律**：标量参数继续排在最后一个 buffer 之后（conv 的
  X=0..Oc=8 是家法）；rmsnorm/channelscale 第一版把 L 绑到 buffer(2) 与 Y 冲突，
  已改为 L=3/C=4。本机无 Metal 前端，此修复是检视级，真编译验证待 Metal 主机。

## Phase 65 增量 1 实测事实（2026-10-31，本机 RX 6600M / gfx1032）

- **fp16 gemm 真机读数**（512³，half 存储 / fp32 累加 / resident-only，两次
  run）：verify=PASS max_rel=0 measured 0.7725/0.8434 kernel 0.1166/0.1185。
  max_rel=0 的机制：init 锯齿全是 2⁻⁸ 倍数且周期 221 把和压在 half 精确可表
  的幅值内——**不能外推**为"fp16 gemm 恒 max_rel=0"，换输入分布即会出现
  ~5e-4 级 half 舍入（门 2⁻⁴ 仍富余）。
- **fp16 kernel_ms ≈ fp32 一半**（0.117 vs ~0.25 @512³）：字节减半的直接收益，
  roofline 方向性在真机上成立。
- **能力区间 op 感知后的拒绝拓扑**：F16+Gemm 恰好 AMD covered；Apple/Intel
  的拒绝理由各自命名自己的生成器（"msl/ocl ... dtype=fp16 has no ... yet"），
  不再借用 dtype_gap 的措辞——三个后端三个理由，coverage 报告说真话。
- **容差与 init 常数撞值**：fp16 容差 2⁻⁴ 与 init 锯齿步长 1/16 数值相同，
  "裸字面量不存在"型断言在 fp16 上不可写（E21 家族的边界案例）——只能靠
  计数腿钉住。写容差检查前先问"这个值还出现在哪"。
- **求解器 elem 按 dtype 后的 |X| 变化**：fp32 树不受影响（dtype_size(F32)=4），
  [R2] 的 22/25 网格腿原样绿；fp16 树的工作集减半意味着更大 tile 可行，
  c_ms=0.061（fp32 同形状 0.111）——cost 模型对 dtype 的方向性正确。
