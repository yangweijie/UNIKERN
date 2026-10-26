# Task Plan — UNIKERN MVP 骨架与实现

## Goal
按已批准规格实现 UNIKERN MVP：三厂商（AMD HIP / Apple Metal / Intel Level Zero）
"KML→求解→生成→验证→Ledger"闭环，交付可构建的 C++20 CLI 与组件，并逐阶段替换 stub。

## Current Phase
Phase 5

## Phases
| # | Phase | Status |
|---|-------|--------|
| 0 | 规格与决策：requirements v1.1 + design.md + 白皮书 v1.0 | complete |
| 1 | 环境探测：编译器 / CMake / SQLite / 厂商 SDK 可用性 | complete |
| 2 | MVP 骨架：CMake + 目录 + 接口 + CLI + stub 后端 | complete |
| 3 | 核心域：KML（形状/精度/规范化/工作负载模型）、HCS（三层级+置信度+校验） | complete |
| 4 | 求解核心：F 谓词 + 分层 roofline C + 字典序 argmin + 场景 A/B | complete |
| 5 | 三后端代码生成（HIP/MSL/OpenCL C）+ Runner 适配器（R5, R10） | in_progress |
| 6 | 差分验证（CPU 参考、分级容差）+ SQLite Ledger（R6, R7） | pending |
| 7 | 多级存储：场景 A 零拷贝 / 场景 B 流式双缓冲执行（R8） | pending |
| 8 | 硬件在环验收：R1-R10 测试矩阵（AMD 卡 / Apple Silicon / Intel Arc B） | pending |

## Decisions
| # | 决策 | 状态 |
|---|------|------|
| D1 | 多面体 = 结构化枚举 + 谓词过滤（isl/Farkas 完整证书留 V1） | 已确认 |
| D2 | 后端目标 = 厂商源语言级（HIP C++ / MSL / OpenCL C）；ISA 直出留 V1+ | 已确认 |
| D3 | 主语言 C++20；SDK：HIP / Metal-cpp / Level Zero / SQLite | 已确认 |
| D4 | HCS schema v2 = tiers[] 数组（vram/unified/ram/nvme + 各自带宽与置信度），兼容 v1 扁平字段 | 本阶段实现 |

## Constraints
- 无 NVIDIA 硬件与 SDK：验收全链路不依赖 NVIDIA；GPUDirect Storage 属 V1
- Apple Metal 工具链仅 macOS：本机（Windows）只做源生成，编译验证在用户 Mac
- Intel Level Zero loader 未探测到：Phase 5 深探 ocloc / OpenCL ICD
- 不静默安装系统级依赖；缺失工具链先报告再请示
- 规格：.monkeycode/specs/unikern-mvp/{requirements.md, design.md}；白皮书：unikern-design.html

## Errors Encountered（防重复）
| # | Error | Attempt | Resolution |
|---|-------|---------|------------|
| E1 | perl -pi 编辑含转义的 C++ 源码多次破坏文件 | 4 | **源码改动一律 heredoc 整文件重写；perl 仅用于确定性单行替换** |
| E2 | Release(-O2/-O3) `solve` 段错误，崩在 ifstream | 1 | 进程内两份 libwinpthread → **CMake `-static`** |
| E3 | Ledger 永远 miss（键含 `\|`） | 1 | 行分隔符改 TAB |
| E4 | Ledger 浮点往返丢精度 | 1 | `max_digits10` |
| E5 | Release 下断言被禁用 | 1 | 测试 `#undef NDEBUG` |
| E6 | gdb 启动 0xc0000139 | 1 | 改用 stderr 探针二分 |
| E7 | canonical_form 尾零判断写成三重条件嵌套，易错 | 1 | 改为「先算有效维度长度再遍历」 |
| E8 | HCS schema 升级后测试/样例仍用旧字段 | 1 | 同步重写测试与三份样例（含 ram/nvme 层级） |
