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
