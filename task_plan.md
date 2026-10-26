# Task Plan — UNIKERN MVP 骨架与实现

## Goal
按已批准规格实现 UNIKERN MVP：三厂商（AMD HIP / Apple Metal / Intel Level Zero）
"KML→求解→生成→验证→Ledger"闭环，交付可构建的 C++20 CLI 与组件，并逐阶段替换 stub。

## Current Phase
Phase 3

## Phases
| # | Phase | Status |
|---|-------|--------|
| 0 | 规格与决策：requirements v1.1 + design.md + 白皮书 v1.0 | complete |
| 1 | 环境探测：编译器 / CMake / SQLite / 厂商 SDK 可用性 | complete |
| 2 | MVP 骨架：CMake + 目录 + 接口 + CLI + stub 后端，构建与冒烟测试通过 | complete |
| 3 | 核心域：KML 原语树（形状/精度/规范化）、HCS 导入与校验（R1, R9） | in_progress |
| 4 | 求解核心：F 谓词 + 分层 roofline C + 字典序 argmin（R2-R4） | pending |
| 5 | 三后端代码生成 + Runner 适配器（R5, R10） | pending |
| 6 | 差分验证（CPU 参考、分级容差）+ SQLite Ledger（R6, R7） | pending |
| 7 | 多级存储：场景 A 零拷贝 / 场景 B 流式双缓冲（R8） | pending |
| 8 | 硬件在环验收：R1-R10 测试矩阵（AMD 卡 / Apple Silicon / Intel Arc B） | pending |

## Decisions
| # | 决策 | 状态 |
|---|------|------|
| D1 | 多面体 = 结构化枚举 + 谓词过滤（isl/Farkas 完整证书留 V1） | 已确认 |
| D2 | 后端目标 = 厂商源语言级（HIP C++ / MSL / OpenCL C）；ISA 直出留 V1+ | 已确认 |
| D3 | 主语言 C++20；SDK：HIP / Metal-cpp / Level Zero / SQLite | 已确认 |

## Constraints
- 无 NVIDIA 硬件与 SDK：验收全链路不依赖 NVIDIA；GPUDirect Storage 属 V1
- Apple Metal 工具链仅 macOS：本机（Windows）只做源生成，编译验证在用户 Mac
- Intel Level Zero loader 未探测到（System32 无 ze_loader.dll）：Phase 5 深探 ocloc
- 不静默安装系统级依赖；缺失工具链先报告再请示
- 规格：.monkeycode/specs/unikern-mvp/{requirements.md, design.md}；白皮书：unikern-design.html

## Errors Encountered（全部已解决，防重复）
| # | Error | Attempt | Resolution |
|---|-------|---------|------------|
| E1 | perl -pi 编辑 C++ 源码 4 次破坏文件（`\n` 被折叠成真实换行，截断字符串字面量） | 1-4 | **规则：源码改动一律用 heredoc 整文件重写，禁用 perl 做多行/含转义编辑** |
| E2 | Release(-O2/-O3) 下 `solve` 段错误，Debug(-O0) 正常；崩点在 `std::ifstream` | 1 | 进程内加载两份 `libwinpthread-1.dll`（ProgramData g++13.2 构建 vs Git `/mingw64` 运行时 DLL）→ **CMake 加 `-static -static-libgcc -static-libstdc++`** |
| E3 | Ledger 永远 miss：键本身含 `\|`，lookup 用 `find('\|')` 切分错位 | 1 | 行分隔符改 TAB（`\t`），键内 `\|` 保留 |
| E4 | Ledger 浮点往返丢精度（默认 6 位有效数字，`stod` 后 != 原值） | 1 | 写入设 `max_digits10`（17 位） |
| E5 | `tests/smoke.cpp` Release 下断言被 `NDEBUG` 禁用 | 1 | 测试文件头部 `#undef NDEBUG` |
| E6 | gdb 启动失败（0xc0000139，MinGW PATH 冲突） | 1 | 改用 stderr 探针二分定位，不依赖 gdb |
