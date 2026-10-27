# Task Plan — UNIKERN MVP 骨架与实现

## Goal
按已批准规格实现 UNIKERN MVP：三厂商（AMD HIP / Apple Metal / Intel OpenCL）
"KML→求解→生成→验证→Ledger"闭环，交付可构建的 C++20 CLI 与组件，并逐阶段替换 stub。

## Current Phase
**本机可做项已清零（2026-10-28，Phase 15 验收收尾闭合）**——Phase 0-15 全 complete；
G5/R8.5 驻留守卫已实现并真机验证 migrations=0（Phase 16，commit ea76c2f；当前二进制复采
`docs/evidence/phase15/phase15-recheck-v12.txt`）。本机事实：AMD RX 6600M 真机三算子闭环
（conv/reduce/gemm 全 verify=PASS max_rel=0 → COMMIT → 同键 HIT）；Apple 侧已在 macOS 机器上
于 Phase 10/13 闭合；Intel 三算子实现 + 过 clang 的 OpenCL 前端语法检查（backends 实测
intel=no——本机无 Intel 设备）。验收矩阵 v12：G1 全闭合、G5 驻留守卫落地。
剩余开放缺口（本机做不了的 / 属 V1）：① Intel conv/reduce 真机运行验证（需 Intel 机器）；
② G5 的"长测"部分（长时间/大负载缺页迁移率统计与超阈重求解）；③ R10.2 的 F 约束合流（V1）；
④ conv/reduce 调优与浮点次序鲁棒性。

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
| 11 | G7 闭合：非 GEMM 算子的能力面显式化（谓词 + 生成物自拒 + runner 入口自拒 + CLI 门），拒绝不得静默替换负载 | **complete（2026-10-27；三层各自"摘掉即红"已录，A/B 缺陷重放入库；--op gemm/reduce 声明面第一片，conv 按 arity 在 parse 拒绝；证据 docs/evidence/phase11/）** |
| 12 | 算子面闭合：G1 KML 文本前端（三类算子的 tile/layout/原语树声明 + R1.2 完整结构化拒绝）与 G8 R10.3 每后端原语覆盖报告，顺带 G9 账本键 arity 身份 | **complete（2026-10-27；三算子可声明、可解、可进键；由 KML 声明驱动的 512³ Apple 真机 COMMIT/HIT 入库；六条新腿逐一摘除即红；conv/reduce **可用的内核生成**仍缺＝G1 执行半边）** |
| 13 | 算子执行面（**先 Apple**）：conv/reduce 的 MSL 内核生成 + 进程内真机对拍/计时/账本闭环；G6 Metal 工作集上限交叉校验 | **complete（2026-10-27；conv/reduce 在 M4 上 verify=PASS max_rel=0 → COMMIT → 同键 HIT；gemm 回归不变；G6 实测 12.7 GiB 上限并入第二道门；AMD/Intel 的 conv/reduce 仍为下一批）** |
| 14 | 执行面补齐 · **Intel（OpenCL）**：conv/reduce 的 OpenCL C 内核 + 进程内 runner 分派 + per-backend 能力面 | **complete（2026-10-28；三算子 .cl 全过 clang 的 OpenCL 前端、负测可红；能力面三向；runner host 侧仅编译检查——本机无 Intel 设备，运行未验，如实标注）** |
| 15 | 执行面收尾 · **AMD（HIP）**：conv/reduce 的 HIP 程序 + `op_support_gap` 三算子 covered + RX 6600M 真机闭环 | **complete（2026-10-28；conv=uk_conv / reduce=uk_reduce_part+uk_reduce_sum 独立程序、CPU 参考内嵌、gemm 程序逐位不变；hipcc 真编译 + 两条门禁命令 COMMIT→HIT 全 PASS max_rel=0；[R15] 两条摘除即红；证据 docs/evidence/phase15/）** |
| 16 | G5/R8.5 驻留守卫：hipMalloc 与 `hipPointerGetAttributes` 驻留断言折叠进 `uk_mmalloc()`、超订 loud 拒绝退 5、UNIKERN_RESULT 尾 `migrations=<n>` | **complete（2026-10-28，commit ea76c2f；RX 6600M 三算子六轮真机 migrations=0（v12 复采），拒绝分支 host 指针代位实测 exit=5；[R8.5] 单测 + 摘除即红；"长测"部分如实开放，见缺口表 G5 行）** |

### Phase 15 子任务（AMD 收尾，2026-10-28）
| 子项 | 状态 |
|------|------|
| a. 生成：`hip_runnable_program` 按算子分派；conv 程序带 uk_conv、reduce 程序带 uk_reduce_part+uk_reduce_sum；每份程序只带自己算子的内核；CPU 参考内嵌（语义对齐 reference.cpp，不含核心库）；gemm 程序逐位不变 | complete（fresh-solve 对 fresh-solve 的 uk_amd.cpp/uk_apple.metal/uk_intel.cl diff 全空；ledger-HIT 重建解不带 shard 参数会让 R8 行多 "(defaults:…)" 后缀——账本形状既有属性，非代码生成差异，转录已注明口径） |
| b. 能力面：`op_support_gap()` HIP 谓词 conv/reduce unsupported→covered（Apple/Intel 分支不动）；COVER 行与 [R10.2]/[R10.3] 臂同步 amd=covered；layout 成为共享谓词唯一拒绝轴，拒绝产物保留 | complete（[R15] 断言 layout=colmajor 仍点名拒绝且 `UNIKERN_ERROR what=op_unsupported` 产物在） |
| c. CLI 样本：samples/conv.kml（六维）与 samples/reduce.kml；两条门禁命令端到端 | complete（`run --hcs samples/hcs_amd.json --kml samples/conv.kml` 与 `run … --op reduce --dims 1048576 0 0` 逐字执行，转录 docs/evidence/phase15/phase15-real-device-runs.txt） |
| d. 验收：[R15] 段（每源只带自己内核 / provenance 头 / 计时维取声明树 / 三算子 covered）+ [R1.1/2]/[R10.2]/[R10.3]/[R13] 同步；摘除即红（分派腿红 @1317、谓词腿红 @305）；probe/phase15_arm_probes.py；phase11 探针锚点同步 | complete（ctest 2/2 绿） |
| e. 真机（RX 6600M，gfx1032，ROCm 7.1）：conv/reduce/gemm 三算子 run 全部 verify=PASS max_rel=0 → LEDGER COMMIT → 同键 HIT；conv/reduce 程序 hipcc 真编译（rc=0） | complete（conv ε_rel=67.9993→HIT；reduce ε_rel=0.687299→HIT；gemm ε_rel=3.42314→HIT，行为逐位不变由 diff + UNIKERN_RESULT 同形双证） |
| f. 验收收尾（v12 复采，Phase 16 守卫后的当前二进制）：backends + `solve --emit` 三源行数 + 三算子 run×（COMMIT+HIT）+ ctest 全量重采；文档只引用入库读数（E14） | complete（emit 行数 conv=253/reduce=217/gemm=425 = 首轮 219/183/390 各 +34/35 行即 Phase 16 守卫；三 run 六轮全 verify=PASS max_rel=0 且 UNIKERN_RESULT 尾 migrations=0，COMMIT ε_rel 68.0154/0.354586/3.43836 → HIT 67.4707/1.10948/3.41299；backends 实测 amd=yes/apple=no/intel=no；ctest 2/2 绿；转录 `docs/evidence/phase15/phase15-recheck-v12.txt` + README 复现命令表；acceptance v12 G1 全闭合 + G5 驻留守卫行更新） |

### Phase 14 子任务（Intel/OpenCL，2026-10-28）
| 子项 | 状态 |
|------|------|
| a. 生成：`ocl_kernel_source` 增 `uk_conv` / `uk_reduce_part`+`uk_reduce_sum`；每份源只带自己算子的内核 | complete（`[R14]` 逐算子断言：conv 源无 uk_gemm/uk_reduce，reduce 源无 uk_gemm/uk_conv，gemm 源无另两者；三份都带 R5/R8 横幅） |
| b. 能力面：`ocl_op_gap()` + `BackendIntel::capability_gap()` 覆盖；覆盖表变为 apple+intel covered / amd unsupported | complete（与 `msl_op_gap` 同构：layout 白名单 + 三算子；实测 `COVER amd=unsupported / apple=covered / intel=covered`；toolchain 轴单列，本机 intel=absent） |
| c. runner_ocl：按算子建 plan（conv 六维 / reduce L+P）、clCreateKernel 按名取、verify 用共享 cpu_conv/cpu_reduce、计时同 gemm 口径 | complete（plan 增 op/conv 六维/L/P；`launch_gemm/launch_conv/launch_reduce` + 共用 `time_passes`（标记事件括住 e2e，kernel-only 为 N 次连续 launch）；reduce 另建 `uk_reduce_sum` 内核；grid 上限检查按算子取数） |
| d. 本机能给的验证：生成的 .cl 过 `clang -x cl -cl-std=CL1.2 -fsyntax-only`（三算子各一份，含一条负测）；runner_ocl 过 `-fsyntax-only -DUK_HAS_OCL` | complete（三份 rc=0/0 诊断；负测 rc=1；runner_ocl 语法检查通过。**运行未验**：本机 `run --vendor intel` 以「no Intel GPU platform/device found」rc=1 收场且不建账本） |
| e. 验收：acceptance 增 `[R14]`（每源只带自己 kernel / 能力面异构 / cpu 参考已由 R13 覆盖）；arm 探针；证据 docs/evidence/phase14/ + 文档 + commit | complete（`[R14]` 落地；三腿逐一摘除即红（:1042 / :1303 / :1249）；`[R10.2]/[R10.3]` 与 `[R13]` 改写为三向期望；Apple 三算子回归仍 PASS） |

### Phase 13 子任务（先 Apple，2026-10-27 起）
| 子项 | 状态 |
|------|------|
| a. G6：`recommendedMaxWorkingSetSize` 作为独立上限（与 0.85×HCS 声明容量**并列**两道门，不叠乘），并在 UK_DEVICE 行打印实测值 | complete（实测 13631488000 B；旧口径允许到 0.85×34359738368=29205777612 B，是设备上限的 2.1 倍；arm A 落在两界之间被新门拒绝且容量门沉默，arm B 仍由容量门拒绝——判别证据是"带内拒绝"而非"摘掉报警"，理由见 phase13/README） |
| b. 能力面按后端拆分：`IBackend::capability_gap()`（默认沿用全局谓词），Apple 覆盖为 gemm/conv/reduce；CLI run 按所选后端判、`--emit` 按"任一后端覆盖"判；覆盖表随之呈现异构行 | complete（HIP/OCL 谓词与产物未变；异构产出实测：`uk_apple.metal` 含 uk_conv，另两份是点名拒绝；`[R10.2]/[R10.3]` 臂改写为异构期望） |
| c. MSL：`uk_conv`（implicit GEMM，valid padding，一线程一输出）与 `uk_reduce`（两段：分段求和 + 终归约）；CPU 参考实现进核心库供 runner 与单测共用 | complete（`src/reference.cpp` 的 cpu_conv/cpu_reduce 为唯一真值，acceptance 用四组手算用例校验；reduce verify 数据刻意取 2^-6×2^20=16384，任意次序精确——验的是结构不是浮点次序鲁棒性，已写入注释） |
| d. runner_metal 泛化：按算子建 plan（launch 尺寸/载荷/verify 尺寸）、按算子取 kernel 名与 dispatch、`perf=` 打印算子声明的维（gemm 行为保持不变） | complete（plan 增 op/conv 六维/L/P；三条 enc_* 与 time_passes 复用；命令缓冲状态检查作为纵深防御；**自捉**：reduce 首跑 verify=FAIL max_rel=1.0，根因是 pass-1 线程组 4096 超过 maxTotalThreadsPerThreadgroup=1024 被 Metal 静默不执行——抓到它的是 verify 不是状态检查，已如实记录） |
| e. 验收：acceptance 增能力面异构臂 + CPU 参考正确性臂；真机 verify=PASS → COMMIT → HIT；`probe/` 带弹性探针；证据 docs/evidence/phase13/ + 文档 + commit | complete（新增 `[R13]` 段；五腿逐一摘除即红：MSL 覆盖 `:1044`、Apple per-backend 覆盖 `:1080`、`--emit` 任一覆盖 `:1080`、conv 管线名（设备探针 no_function name=uk_gemm）、reduce 线程组上界（设备探针 verify=FAIL）；conv/reduce COMMIT ε_rel 334.6/10.9 与 HIT 轮 243.9/8.87） |

### Phase 12 子任务（KML 文本前端 + 覆盖报告，2026-10-27）
| 子项 | 状态 |
|------|------|
| a. 设计口径：KML JSON schema（op/dtype/dims/tile/layout/children），每算子的**维度 arity 硬约束**，layout 只接受产物真正实现的那一种（"NT"=A/B/C 行主序索引），其余显式拒绝——不得让前端接受一个 codegen 未实现的标签（那是 G7 同一类静默） | complete（arity 收敛到单一权威 `kml_arity()`，前端校验 / `kml_export` / `op_signature` / `canonical_form` 共用；layout 白名单与执行拒绝共用 `codegen_layout_supported()`） |
| b. 前端实现 `kml_import`/`kml_export`：flat-JSON 解析与 hcs_import 共用同一内部工具（避免两份解析器漂移）；R1.2 拒绝**一次列全**所有违规字段/节点（R9.2 的既有教训） | complete（新增 `src/json_flat.{hpp,cpp}`，hcs.cpp 改为复用同一解析器；children 先切作用域再读父标量，杜绝"父借子字段"；违规覆盖未知字段/op/dtype/dims 个数与非整数与非正数/layout/子路径；export 恒全显式且 round-trip canonical 相同） |
| c. G9：`op_signature` 只编进 dims[0..2] → conv 的 6 维形状在后两个非零维上共用一键（规划期发现，属 R7.1 键身份缺口；gemm 现有键逐位不变，故已入库证据不受影响） | complete（签名与 canonical_form 均改按 `kml_arity()` 取维；`[R7.1]` 臂断言 gemm 签名逐位不变、conv 三形状互异、reduce 未用槽位（含结构体默认 4096）不泄漏进键或哈希） |
| d. CLI：`kml <file>` 解析打印；`solve/run/verify --kml <file>`；R10.3 覆盖报告（op × backend 支持/不支持 + 单一判据 reason，并与 toolchain availability 分列，两个维度不得混谈） | complete（`COVER <backend> primitive=covered/unsupported reason=... execution=resident+streamed/resident-only toolchain=present/absent`；三轴分别取自 `op_support_gap()`、后端对象 `supports_streaming()`、`available()`，不设第二张表；solve 的 SOLVED 与 LEDGER HIT 两支、run 执行前都打印） |
| e. acceptance 臂（round-trip、完整拒绝清单、arity、layout、children、两形状 conv 键相异、覆盖表内容、CLI 端到端）+ 摘掉即红 + 证据 docs/evidence/phase12/ + 文档 + commit | complete（新增 `[R1.1/2]`、`[R7.1]`、`[R10.3]` 三段；六腿摘除即红，红点行号 323/313/345/783/796/1133 见 `docs/evidence/phase12/phase12-arm-probes.txt`；Apple 真机由声明驱动 512³ COMMIT/HIT；自捉两处：默认维泄漏进键、"无账本行"断言复用已被 solve 创建的路径而变恒真） |


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

### Phase 11 子任务（G7 非 GEMM 静默错执行闭合，2026-10-27）
| 子项 | 状态 |
|------|------|
| a. 缺陷定位与可复现读数 | complete（库级探针直调 `hip_runnable_program(reduce 树)`：发出 `const int PM = 1048576, PN = 4096, PK = 4096;` 的 uk_gemm、零拒绝诊断、账本键 `reduce:fp32:1048576x4096x4096`；三处同模式静默默认位点 codegen_hip / runner_metal / runner_ocl 全部定位） |
| b. 三层拒绝 | complete（① 单一判据 `op_support_gap()`；② 生成物自拒：HIP 程序打 `UNIKERN_ERROR what=op_unsupported` 退 4，MSL/OpenCL 文件不含内核（编译期即响亮失败）；③ runner 入口自拒：置于写 workdir 与编译**之前**，避免拒绝用源文件盖掉上一次真产物；④ CLI 门置于 ledger 任何读写**之前**，被拒的树留不下账本行） |
| c. 声明面第一片 `--op` | complete（gemm/reduce 可选；reduce 单长度 `--dims L 0 0` 且其余维清零免进账本键；conv 在 parse 阶段按 arity 拒绝——声明需六维而 CLI 只暴露三维，接受它等于在隐式默认上求解（R1.2）；`solve --op reduce` 允许解并打印 model-only 提示） |
| d. acceptance R10.2 臂 + 带弹性探针 | complete（库/产物/CLI 三层断言 + gemm 对照 `PM = 64` 三源齐发；`probe/phase11_arm_probes.py` 逐层摘除：谓词红于 acceptance.cpp:895、CLI 门红于 :927、runner 门红在真机探针（错配跑出 `perf=1048576x32x32 verify=PASS`）而 ctest 仍绿 → **runner 层无 ctest 回归位，如实分级**；残留标记校验 0，ctest 2/2 绿） |
| e. 证据入库 + 文档 + commit | complete（docs/evidence/phase11/ 五份转录 + 五份探针本体 + README；acceptance-mvp.md v7 R10 行与 Phase 11 补遗；findings/progress 落定；.gitignore 增两条探针二进制） |

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

### 缺口闭合（v7 累计，2026-10-27）
| 缺口 | 状态 |
|------|------|
| G2 constants_version stub | **closed**：版本随 HCS 透传 + `calibrate` 递增（真机 1→2）；R7.3 校准闭环（账本实测行→中位数有效互联带宽 32→5.429 GB/s→校准 HCS），ε_rel 4.83→0.12 |
| G3 产物头无 toolchain | **closed**：三后端产物头 `toolchain=<hcs.toolchain>` |
| G1 KML 文本解析 | **声明半边闭合（Phase 12）**；**执行半边 Apple（Phase 13）+ Intel（Phase 14）+ AMD（Phase 15）已全部闭合**——三算子可声明、可求解、可进键、可生成；Apple 真机已验、AMD 真机已验（conv/reduce/gemm COMMIT→HIT）、OpenCL 源已实现并过 clang 语法检查（真机运行仍待 Intel 机器）；更复杂的原语树组合语法按 V1 |
| G4 真机验证 | **已全闭合**：Intel 半侧（Phase 9，2026-10-27，ocloc AOT + 进程内 OpenCL runner 真机验证，证据 docs/evidence/phase9/）+ Apple 半侧（Phase 10，2026-10-27，Metal 运行时 JIT + 进程内 ObjC++ runner，证据 docs/evidence/phase10/） |
| G6 Apple resident 预算口径（Phase 10 收尾新发现） | **closed（Phase 13）**：Metal `recommendedMaxWorkingSetSize` 作为独立上限并入第二道门（与 0.85×HCS 声明容量并列、不叠乘），UK_DEVICE 行打印实测值（本机 13631488000 B = 12.7 GiB，仅为 0.85×声明容量的 47%）；arm A/B/C 三条臂见 docs/evidence/phase13/phase13-g6-working-set.txt。**判别证据是"带内拒绝"**：直接摘门会让 20 GB 载荷走到分配阶段，既可能压垮机器、量到的也只是分配失败而非判定 |
| G7 非 GEMM 算子被静默当作 GEMM 执行/回写（Phase 10 收尾排查发现） | **closed（Phase 11，2026-10-27）**：单一判据 `op_support_gap()` + 生成物自拒 + runner 入口自拒 + CLI 门（置于 ledger 读写之前）；acceptance R10.2 臂与真机探针逐层"摘掉即红"，A/B 缺陷重放见 `docs/evidence/phase11/phase11-defect-replay.txt`。仍未做（属 G1 范围）：conv/reduce 的**可用**内核生成——本轮只把"不支持"变成显式的 |
| G8 R10.3 每后端原语覆盖输出无人认领（Phase 11 排查发现） | **closed（Phase 12）**：`primitive_coverage()` + `COVER` 行进 solve/run/kml 报告，三轴（primitive / execution / toolchain）分别取自 `op_support_gap()`、后端对象的 `supports_streaming()` 与 `available()`，无第二张表可漂移；`[R10.3]` 库+CLI 臂守着，摘掉 execution 轴即红（`phase12-arm-probes.txt` 第 6 腿）。**注意**：R10.2 的"纳入 F 约束"目前形态是*生成/执行拒绝 + 报告点名*，solve 仍是 (hcs, tree) 纯函数、不知后端——差异已写入 phase12/README.md 以免误读 |
| G9 `op_signature` 只编进 dims[0..2]（Phase 12 规划期发现） | **closed（Phase 12）**：签名与 canonical_form 改按 `kml_arity()` 取维。gemm 键逐位不变（`[R7.1]` 断言 `gemm:fp32:4096x4096x4096`），故 Phase 8/9/10 已入库证据无需迁移；reduce 的 canonical 由 `L,0,0` 变 `L`，而 reduce 从未有过已入库实测行（Phase 11 起它在落库前即被拒），无历史产物受影响。附带收益：结构体默认维不再可能泄漏进键 |
| G5 R8.5 运行时驻留/超订静默（Phase 15 收尾口径曾只算"长测开放"） | **closed（Phase 16，2026-10-28，commit ea76c2f；真机验证 migrations=0）**：HIP 三程序内嵌 `uk_mmalloc()`——分配与 `hipPointerGetAttributes` 驻留断言不可分离，非设备驻留（managed/host）→ `UNIKERN_ERROR what=oversubscription` 退 5，UNIKERN_RESULT 尾 `migrations=<n>`；RX 6600M 三算子六轮真机全 migrations=0（复采 `docs/evidence/phase15/phase15-recheck-v12.txt`），拒绝分支经 host 指针代位验证 exit=5（`docs/evidence/phase16/`）；`[R8.5]` 单测 + 两条摘除即红腿。**"长测"部分仍开放**（迁移率统计与超阈重求解，属真机长测，如实保留） |

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
| E17 | 并行会话 Phase 10 为 macOS 引入的无条件 `project(... OBJCXX)` 使 Windows 配置直接失败（MinGW 无 Objective-C++ 编译器，CMake 报 OBJCXX broken），且 `runner_metal.mm` 仅 Apple 可编译——非 Apple 链接 `run_metal` undefined | 1 | OBJCXX 改为 APPLE 分支内 `enable_language`；`runner_metal.mm` 仅 APPLE 进编译列表，非 Apple 链接 `runner_metal_stub.cpp`（`metal_available()=false` / `run_metal` 诚实拒绝，R10 语义不变）；本机基线恢复：配置成功（OpenCL 诚实 stub）、构建零告警、ctest 2/2 绿（commit 4224d42） |
