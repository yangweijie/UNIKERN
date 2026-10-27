# Task Plan — UNIKERN MVP 骨架与实现

## Goal
按已批准规格实现 UNIKERN MVP：三厂商（AMD HIP / Apple Metal / Intel OpenCL）
"KML→求解→生成→验证→Ledger"闭环，交付可构建的 C++20 CLI 与组件，并逐阶段替换 stub。

## Current Phase
Phase 10 收尾复核（Apple 真机验证 G4 全闭合后的证据重采 + 门控补测，2026-10-27）
—— 规格 Phase 0-10 全部 complete；剩余为如实标注的开放缺口 G1/G5/G6（见"缺口闭合"表）

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
| 8 | 硬件在环验收：R1-R10 测试矩阵（AMD 卡 / Apple Silicon / Intel Arc B） | complete（验收矩阵 docs/acceptance-mvp.md；Apple 真机项如实标注受阻） |
| 9 | Intel 真机验证（G4 Intel 半闭合）：OpenCL 执行 runner + 差分验证 + 账本闭环 + ocloc AOT 证据 | **complete（2026-10-27；本机 Arc B390 + oneAPI/OpenCL 3.0 在位，ocloc AOT + 进程内 runner 全验，ε 如实报告；Apple 半侧维持受阻）** |
| 10 | Apple 真机验证（G4 Apple 半闭合）：Metal 执行 runner + 差分验证 + 账本闭环 + 运行时 JIT 编译证据 | **complete（2026-10-27；本机 Apple M4 + Metal 4 运行时 JIT（newLibraryWithSource，无需 Xcode）；256³ 对拍 max_rel=0、512³ LEDGER COMMIT（ε 实算）、HIT、calibrate 场景 A 诚实门控；G4 全闭合）** |

### Phase 10 子任务（Apple 真机验证，2026-10-27）
| 子项 | 状态 |
|------|------|
| a. 环境 sanity：Objective-C++ 编译链接 Metal.framework（CommandLineTools SDK，无需 Xcode）；MTLDevice 枚举 + hasUnifiedMemory 探测；`newLibraryWithSource` 运行时 JIT 编译 MSL | complete（probe/uk_metal_probe.mm：UK_DEVICE name=Apple M4、JIT OK、256³ verify=PASS；Xcode.app 在位但无 SDK，Metal 链接自 CLT SDK） |
| b. `src/runner_metal.mm`：进程内 Metal 执行 runner（确定性 init + 256³ CPU 对拍 + 计时 + UNIKERN_RESULT），UK_HAS_METAL 编译期门 | complete（384 行（`wc -l` 实数），与 HIP/OCL runner 同构：UK_MT_CK 显式失败、initA/initB/cpu_ref 位一致、warm 2/timed 10、GPUStartTime/GPUEndTime、resident-only 诚实拒绝） |
| c. 接线：`BackendApple::available()` 真实运行时探测（MTLCreateSystemDefaultDevice）+ `codegen.hpp` 声明 + `main.cpp --vendor apple` + CMake Objective-C++ 接入 | complete（available() 改 metal_available() 运行时探测；--vendor apple 端到端；CMake OBJCXX + find_library Metal/Foundation + PUBLIC 链接） |
| d. `tests/acceptance.cpp` crash-tx POSIX 路径修复 + ctest 全绿 | complete（crash-tx 子进程 spawn 跨平台化：绝对路径 + POSIX/Windows 分支；Apple 构建 ctest 2/2 绿） |
| e. 真机闭环：run → LEDGER COMMIT → HIT → calibrate（场景 A 零拷贝语义 + ε 如实报告） | complete（COMMIT predicted 0.0103244ms vs measured 1.27091ms → ε_abs=1.26059 / ε_rel=122.097（比值口径）；HIT 轮 measured 0.838425 → ε_rel=80.2078；残留为 HCS 声明峰值 26000 GFLOPS vs 内核有效约 0.54 TFLOPS 的计算率缺口，如实报告；calibrate 默认 SKIP——场景 A 下 measured−kernel 为主机调度开销非链路拷贝时间，新增 --allow-unified-calibrate 显式强制门控，且强制后 PREDICTED c_ms 逐位不变证明该常数在场景 A 路径上惰性） |
| f. 证据转录入库 + acceptance-mvp.md v6 + 规划文件 + ctest 全绿 + commit | complete（docs/evidence/phase10/ 转录 + README + emit/uk_apple.metal 构件；acceptance v6/v6.1 Apple 行 ⛔→✅；codegen 横幅修正；G4 全闭合） |
| g. 收尾复核（本轮）：证据全量重采 + 文档数字对齐 + 门控补测 + .gitignore 补齐 | complete（① 三处文档数字与转录不符（acceptance 的 ε_rel=99.6471、README 的 5.18 GB/s、task_plan 的 1.029/99.6）→ 按 20:28 重采读数全部替换并声明 ε_rel 为比值 \|C−T\|/T 口径；② 新门控 `--allow-unified-calibrate` 原仅有真机转录、零单测约束 → acceptance 补 R7.3 四臂经真实 CLI（拒绝消息/旗标过门/旗标真校准 6.291456 GB/s/场景 B 不受害），摘门控转红见 phase10-gate-arm-probe.txt；③ .gitignore 补 build-apple/、uk_run/、probe 二进制——此前 README 声称"build-apple/ 已忽略"实际未忽略；④ POSIX system() wait 状态坑修正） |

### Constraints
- 无 NVIDIA 硬件与 SDK：验收全链路不依赖 NVIDIA；GPUDirect Storage 属 V1
- 本机真机：AMD RX 6600M（gfx1032）+ Intel Arc B390（OpenCL 3.0 NEO，驱动 32.0.101.9150）+ Apple M4（Metal 4，macOS 26.6.2 arm64）——**三厂商真机均已验证（Phase 8/9/10），G4 全闭合**
- Intel 工具链在位：System32 OpenCL.dll + ze_loader.dll、oneAPI 2025.3/2026.1（ocloc、CL 头）；`intel:ocl` 后端 available() 真实运行时探测
- Apple 工具链：CommandLineTools SDK（Metal/Foundation），运行时 JIT（newLibraryWithSource）；Xcode.app 在位但无 SDK——链接自 CLT SDK，探测事实见 docs/evidence/phase10/phase10-environment.txt
- 不静默安装系统级依赖；缺失工具链先报告再请示

### 约束提醒（Phase 10 复用）
- 生成源码零反斜杠（E10）；路径前斜杠；源码编辑一律 Write/Edit 文件工具
- Metal 运行时 JIT 编译不产生 .metallib/.air 中间产物——源即编译单元（与 HIP/OCL 口径一致）
- LEDGER COMMIT 的 ε = |C−T| 实算，禁止常量冒充（E13）
- 设备事实（显存/类型/核心数）经 Metal API 实测记录，不照抄平台标签（M4 标签含 "(integrated)" 字样，HCS 样例写 34GB 统一内存——以实测为准）
- 计时口径与 AMD/Intel runner 对齐：warm 2 / timed 10（小）或 1/1（大）；e2e = H2D+kernel+D2H，kernel_ms 为内核参考
- 场景 A 零拷贝：Apple Silicon 统一内存，无显式拷贝路径，e2e 测量的是纯内核+内存流量（不重复 AMD 的 PCIe 拷贝项）
- payload 超过 0.85×unified 时诚实拒绝，不静默降级（R10）

### Phase 5 子任务
| 子项 | 状态 |
|------|------|
| HIP 可运行程序生成器（内核 + 确定性初始化 + CPU 参考 + 自对拍 + 事件计时） | done |
| MSL / OpenCL C 内核源生成（与 HIP 同构的寄存器分块结构） | done（本机无法编译验证，真机受阻已标注） |
| 真机探测：hipcc / 设备 / Intel 工具链 | done |
| `src/runner.cpp` 编译修复（反斜杠折叠破坏，E10） | done（Write 工具重写 + 前斜杠 + --offload-arch，E12） |
| `src/main.cpp` 适配新签名 `generate(sol, hcs, tree)` | done（solve --emit/--dims + run/verify 接入 Runner，94ec757） |
| HIP 真编译 + 真运行 + 与 CPU 参考对拍（RX 6600M） | done（1024³ resident verify=PASS；32768³ streamed verify=PASS） |
| Ledger 写入实测 ms 并报告 ε = \|C − T\| | done（SQLite 账本；收尾审计修正 ε 为实算——首版误打容差常量，见 E13；1024³ ε_rel=4.79，32768³ ε_rel=14.37） |
| Phase 8 验收：tests/acceptance.cpp（R1-R10 CPU 项）+ docs/acceptance-mvp.md | done（ctest 2/2 全绿） |

### 缺口闭合（v4，2026-10-27）
| 缺口 | 状态 |
|------|------|
| G2 constants_version stub | **closed**：版本随 HCS 透传 + `calibrate` 递增（真机 1→2）；R7.3 校准闭环（账本实测行→中位数有效互联带宽 32→5.429 GB/s→校准 HCS），ε_rel 4.83→0.12 |
| G3 产物头无 toolchain | **closed**：三后端产物头 `toolchain=<hcs.toolchain>` |
| G1 KML 文本解析 / G5 R8.5 迁移监测 | 保留（V1 / 真机长测） |
| G4 真机验证 | **已全闭合**：Intel 半侧（Phase 9，2026-10-27，ocloc AOT + 进程内 OpenCL runner 真机验证，证据 docs/evidence/phase9/）+ Apple 半侧（Phase 10，2026-10-27，Metal 运行时 JIT + 进程内 ObjC++ runner，证据 docs/evidence/phase10/） |
| G6 Apple resident 预算口径（Phase 10 收尾新发现，如实入册） | **保留（记录，未实现）**：`src/runner_metal.mm:113` 的 resident 拒绝阈值取 `hcs.tier_bytes("unified")`（HCS 声明的 32 GiB），未与 Metal `recommendedMaxWorkingSetSize`（实际可驻留工作集上限，通常更小）交叉校验。本机 payload 3 MB 远低于两者，本次读数与结论不受影响；但"payload ≤ 0.85×容量"目前只对**声明容量**成立。属"HCS 声明值 vs API 实测"同类缺口，闭合它需要新增一次真机上限探测臂（不在 Phase 10 范围内，故不顺手实现） |

### Phase 9 子任务（Intel 真机验证，2026-10-27 起）
| 子项 | 状态 |
|------|------|
| a. 环境 sanity：MinGW g++ 链接 System32/OpenCL.dll + 厂商 CL 头枚举平台/设备；ocloc 设备名探测 | done（MSVC 2022 + oneAPI OpenCL.lib 导入库链接 + third_party/CL；设备名 bmg-g21；失败根因：函数指针签名 arity 错 + CL_API_CALL 取值，已修复并隔离验证） |
| b. 厂商 CL 头文件归入 `third_party/CL/`（镜像 sqlite amalgamation 先例） | done（Khronos OpenCL-Headers 经 conda-forge opencl-headers 2025.06.13 提取，18 头 + Apache-2.0 + README 来源注记） |
| c. `src/runner_ocl.cpp`：进程内 OpenCL 执行 runner（确定性 init + 256³ CPU 对拍 + 计时 + UNIKERN_RESULT），UK_HAS_OCL 编译期门 | done（语义逐项对齐 AMD runner：256³/容差 2⁻⁵/树 dims 16 对齐下限 32/warm2timed10/e2e 标记事件/kernel_ms 参考；resident-only 诚实拒绝；网格上限诚实拒绝） |
| d. 接线：`BackendIntel::available()` 真实运行时探测（替换 OCL_ICD_VENDORS 启发式）+ `codegen.hpp` 声明 + `main.cpp --vendor` + CMake | done（pick_intel_gpu 平台/设备探测；acceptance R10 镜像改独立 Win32 枚举） |
| e. `tests/acceptance.cpp` R10 镜像断言同步更新（LoadLibrary("OpenCL.dll") 独立枚举） | done（Win32 LoadLibrary+GetProcAddress 镜像，与后端真探测本机一致；处理了 windows.h 宏污染 near/small/big） |
| f. ocloc AOT 编译生成的 uk_intel.cl（R5 真编译证据） | done（`-device bmg-g21` → uk_intel_bmg.bin + uk_intel_bmg.spv） |
| g. Intel 真机闭环：run → LEDGER COMMIT → HIT → calibrate（ε 如实报告） | done（verify=PASS max_rel=0；run COMMIT ε_abs=1.31315 ε_rel=2.72046 → HIT → calibrate 32→18.5877 GB/s v1→2 → 校准后 ε_rel=1.48813，残留为 HCS flops 常数精度，如实报告） |
| h. 证据转录入库 + acceptance-mvp.md v5 + 规划文件 + ctest 全绿 + commit | done（docs/evidence/phase9/ 11 文件转录 + emit/uk_intel.cl 与 aot 产物 add -f 入库；acceptance-mvp.md v5；task_plan/progress/findings 更新；ctest 2/2 绿） |

### 约束提醒（Phase 9 复用）
- 生成源码零反斜杠（E10）；路径前斜杠；源码编辑一律 Write/Edit 文件工具
- MinGW 静态链接（E2：-static -static-libgcc -static-libstdc++）
- LEDGER COMMIT 的 ε = |C−T| 实算，禁止常量冒充（E13）
- 设备事实（显存/类型）经 OpenCL API 实测记录，不照抄平台标签（B390 标签含 "(integrated)" 字样，HCS 样例写 16GB 离散——以实测为准）
- 计时口径与 AMD runner 对齐：warm 2 / timed 10（小）或 1/1（大）；e2e = 标记事件包裹，kernel_ms 为内核参考
- 流式双缓冲本期不实现（intel:ocl 后端源横幅已声明 resident-only）；payload 超过 0.85×vram 时诚实拒绝，不静默降级（R10）

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
- 本机真机：以文件顶部"Constraints"一节（Phase 10 已刷新）为唯一权威口径——AMD RX 6600M +
  Intel Arc B390 + Apple M4 三厂商真机均已验证；此处不再重复列举，避免同文件两处陈述分歧
  （此段曾是"Apple 仅源生成、真机需用户 Mac"的 Phase 8 前记录，已被 Phase 10 推翻）
- Intel 工具链在位：System32 OpenCL.dll + ze_loader.dll、oneAPI 2025.3/2026.1（ocloc、CL 头）；
  `intel:ocl` 后端 available() 已改为真实 ICD 运行时探测（Phase 9 闭合）
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
| E13 | LEDGER COMMIT 的 epsilon_abs/epsilon_rel 打印硬编码常量（容差 2⁻⁵ 与 1e-6），非 ε=\|C−T\| 实算——工作流独立复核未发现，收尾审计人工审读发现（三组不同实测对应同一常量即暴露点） | 1 | main.cpp 改实算 ε，真机证据三份转录 + 验收文档 R6/R7/R8 全部重采（ε_rel 4.79 / 4.27 / 14.37） |
| E14 | Phase 10 收尾复核：文档里的 Apple 数字是**未入库的一次性读数**——acceptance-mvp.md 引 `ε_rel=99.6471`/`ε_abs=1.0288`、证据 README 引 `5.18 GB/s`、task_plan 引 `1.029/99.6`，三者与 `docs/evidence/phase10/` 当时的转录（98.3738 / 92.8058 / 5.986）全不符；且 README 声称"`build-apple/` 整体被 .gitignore 忽略"而 .gitignore 里根本没有该条 | 1 | 真机链路全量重采（backends/emit/verify/run/hit/calibrate/calibrate-forced/ctest 一次性对齐），文档数字改为**只引用入库转录**并在文档头声明重采事实；补 ε_rel 口径说明（比值 \|C−T\|/T，非百分数）；.gitignore 补 `build-apple/`、`uk_run/`、`probe/uk_metal_probe` |
| E15 | 新写的 R7.3 门控臂在 macOS 上 `assert(rc == 3)` 失败：POSIX `std::system()` 返回 **wait 状态**，`exit(3)` 读回 768（Windows cmd.exe 直返退出码，故 Phase 8/9 的 `rc != 0` 断言从未暴露此差异） | 1 | 归一化 `WIFEXITED/WEXITSTATUS`（POSIX 分支），Windows 分支保持原样；同时发现"门控摘掉后 rc 仍是 3（原因换成 no measured rows）"——rc 不具判别力，改为断言**拒绝消息**并增设加旗标的顺序判别臂 |
| E16 | 技能自带 `check-complete.sh` 报 "Task in progress (0/3 phases complete)"，看似本计划仍有 3 个未完成阶段 | 1 | 读脚本判据后定性为**工具口径不合而非状态缺陷**：它数 `grep -c "### Phase"` 作分母、`**Status:** complete` 或 `[complete]` 作分子，而本计划用 markdown 表格记状态（分母 3 实为三个 `### Phase N 子任务` 标题，分子 0 因表格无该标记）。不为迁就脚本重构状态记法（会造出同文件两处权威陈述）；结论以表格 + Phase 0-10 全 complete + ctest 实证为准，本行留作下轮的"勿据此判红"注记 |
