# Task Plan — UNIKERN MVP 骨架与实现

## Goal
按已批准规格实现 UNIKERN MVP：三厂商（AMD HIP / Apple Metal / Intel OpenCL）
"KML→求解→生成→验证→Ledger"闭环，交付可构建的 C++20 CLI 与组件，并逐阶段替换 stub。

## Current Phase
Phase 8（R1-R10 验收矩阵与报告）— **complete（MVP 收官）**

## Phases
| # | Phase | Status |
|---|-------|--------|
| 0 | 规格与决策：requirements v1.1 + design.md + 白皮书 v1.0 | complete |
| 1 | 环境探测：编译器 / CMake / SQLite / 厂商 SDK 可用性 | complete |
| 2 | MVP 骨架：CMake + 目录 + 接口 + CLI + stub 后端 | complete |
| 3 | 核心域：KML（形状/精度/规范化/工作负载模型）、HCS（层级+置信度+校验） | complete |
| 4 | 求解核心：F 谓词 + 分层 roofline C + 字典序 argmin + 场景 A/B | complete |
| 5 | 三后端代码生成（HIP 可运行程序 / MSL / OpenCL C）+ Runner 适配器（R5, R10） | complete |
| 6 | 差分验证接入运行结果 + SQLite Ledger（R6, R7） | complete |
| 7 | 多级存储：场景 A 零拷贝 / 场景 B 流式双缓冲执行（R8） | complete |
| 8 | 硬件在环验收：R1-R10 测试矩阵（AMD 卡 / Apple Silicon / Intel Arc B） | complete（验收矩阵 docs/acceptance-mvp.md；Apple/Intel 真机项如实标注受阻） |

### Phase 5 子任务
| 子项 | 状态 |
|------|------|
| HIP 可运行程序生成器（内核 + 确定性初始化 + CPU 参考 + 自对拍 + 事件计时） | done |
| MSL / OpenCL C 内核源生成（与 HIP 同构的寄存器分块结构） | done（本机无法编译验证，真机受阻已标注） |
| 真机探测：hipcc / 设备 / Intel 工具链 | done |
| `src/runner.cpp` 编译修复（反斜杠折叠破坏，E10） | done（Write 工具重写 + 前斜杠 + --offload-arch，E12） |
| `src/main.cpp` 适配新签名 `generate(sol, hcs, tree)` | done（solve --emit/--dims + run/verify 接入 Runner，94ec757） |
| HIP 真编译 + 真运行 + 与 CPU 参考对拍（RX 6600M） | done（1024³ resident verify=PASS；32768³ streamed verify=PASS） |
| Ledger 写入实测 ms 并报告 ε = \|C − T\| | done（SQLite 账本 + LEDGER COMMIT epsilon_rel=0.03125） |
| Phase 8 验收：tests/acceptance.cpp（R1-R10 CPU 项）+ docs/acceptance-mvp.md | done（ctest 2/2 全绿） |

## Decisions
| # | 决策 | 状态 |
|---|------|------|
| D1 | 多面体 = 结构化枚举 + 谓词过滤（isl/Farkas 完整证书留 V1） | 已确认 |
| D2 | 后端目标 = 厂商源语言级（HIP C++ / MSL / OpenCL C）；ISA 直出留 V1+ | 已确认 |
| D3 | 主语言 C++20；SDK：HIP / Metal-cpp / Level Zero / SQLite | 已确认 |
| D4 | HCS schema v2 = tiers[]（vram/unified/ram/nvme + 带宽 + 置信度），兼容 v1 扁平 | 已实现 |
| D5 | AMD 生成"完整可运行程序"（自带 CPU 参考对拍 + 计时），一次性满足 R5+R6+R7 测量 | 已实现 |
| D6 | 生成的块 tile 钳制在 [32,96] 且 16 的倍数（寄存器预算），钳制时在报告与源码注释中标注 | 已实现 |

## Constraints
- 无 NVIDIA 硬件与 SDK：验收全链路不依赖 NVIDIA；GPUDirect Storage 属 V1
- 本机真机只有 AMD RX 6600M（gfx1032）；Apple/Intel 仅源生成，真机验证需用户 Mac / Intel 机器
- Intel 本机无 ocloc、无 Intel OpenCL 平台 → `intel:ocl` 后端 `available()=false`（诚实报告）
- 不静默安装系统级依赖；缺失工具链先报告再请示
- 规格：.monkeycode/specs/unikern-mvp/{requirements.md, design.md}；白皮书：unikern-design.html

## Errors Encountered（防重复）
| # | Error | 尝试 | Resolution |
|---|-------|------|------------|
| E1 | perl -pi 编辑含转义的 C++ 源码反复破坏文件 | 4 | 源码一律 heredoc 整文件重写；perl 只做确定性单行替换 |
| E2 | Release(-O2/-O3) `solve` 段错误，崩在 ifstream | 1 | 进程内两份 libwinpthread → CMake `-static` |
| E3 | Ledger 永远 miss（键内含 `\|`） | 1 | 行分隔符改 TAB |
| E4 | Ledger 浮点往返丢精度 | 1 | `max_digits10` |
| E5 | Release 下断言被禁用 | 1 | 测试文件 `#undef NDEBUG` |
| E6 | gdb 启动 0xc0000139（MinGW PATH 冲突） | 1 | 用 stderr 探针二分定位，不依赖 gdb |
| E7 | canonical_form 尾零判断三层嵌套易错 | 1 | 先算有效维度长度再遍历 |
| E8 | HCS schema 升级后测试/样例仍用旧字段 | 1 | 同步重写测试与三份样例 |
| E9 | 大 heredoc 被传输层截断（codegen_hip.cpp 停在 151 行） | 1 | **分块追加，每块 ≤ 80 行，写完立即 wc -l 校验** |
| E10 | 反斜杠折叠再现（`\` → `\`），破坏 runner.cpp 与 codegen_hip.cpp 的字符串字面量 | 2 | **生成器改用 `<iostream>` 输出、路径一律前斜杠、源码中零反斜杠**；仍需修 runner.cpp |
| E11 | `build/` `build-dbg/` `build-o2/` 被 git 跟踪（已提交进 d4b0f8f） | 1 | `.gitignore` 加三项 + `git rm -r --cached` |
| E12 | HIP 真机闭环 verify=FAIL max_rel=1、measured_ms=0.00038：hipcc 无 `--offload-arch` 时默认 gfx906，gfx1032 上所有 launch 静默失败（error 200 device kernel image is invalid），dC 保持驱动清零 → 对拍全错；且生成程序不检查 launch 返回值（launch 表达式在本平台返回 void，须用 hipGetLastError） | 1 | runner 编译 host-only 探针（无 kernel，默认 target 无关）取 gcnArchName，构建加 `--offload-arch=<arch>`；生成程序全部 HIP 调用过 uk_ok 检查，失败打 UNIKERN_ERROR 退 4（负测：gfx906 码对象 → 大声失败） |
