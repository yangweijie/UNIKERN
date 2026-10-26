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
