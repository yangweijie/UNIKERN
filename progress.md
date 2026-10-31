# Progress Log — UNIKERN MVP

## Session 2026-10-26（第一段）
- [x] Phase 0 规格：requirements.md v1.1（R1-R10）+ design.md → commit f0ea230
- [x] 用户确认三项技术决策（枚举谓词过滤 / 厂商源语言级 / C++20）
- [x] 创建规划文件；无历史会话需恢复
- [x] Phase 1 探测：g++ 13.2 / cmake 4.4 / ninja ✓；HIP SDK ✓；MSVC 备选；SQLite 头缺失
- [x] Phase 2 骨架：构建通过；CLI 5 子命令；三后端源生成 stub → commit d4b0f8f
- [x] 修复 4 个真实缺陷：双 libwinpthread 段错误、Ledger 键分隔符、浮点精度、NDEBUG 断言

## Session 2026-10-26（第二段）
- [x] Phase 3 核心域：KML op/dtype/dims[6]/tile/layout；canonical_form 尾零归一 + 子节点排序；
      workload_of 覆盖 GEMM/Conv/Reduce
- [x] HCS schema v2：tiers[]（vram/unified/ram/nvme + 带宽 + 置信度），兼容 v1 扁平；缺失字段全量报告
- [x] Phase 4 求解核心：5×5 tile 网格（|X|≤25）、容量谓词、分层 roofline、字典序 argmin → commit f606879
- [x] 三厂商求解验证：AMD 12.54ms / Apple 5.29ms（零拷贝）/ Intel 12.02ms

## Session 2026-10-27（Phase 5）
- [x] 真机探测：**AMD Radeon RX 6600M, gfx1032, 8176 MB, 14 CU**，hipcc 可用（ROCm 7.1）
- [x] Intel 探测结论：无 ocloc、无 Intel OpenCL 平台，仅 AMD OpenCL → Intel 后端本机不可验证
- [x] `include/unikern/codegen.hpp`：代码生成 + RunOutcome 契约
- [x] `src/codegen_hip.cpp`：HIP **完整可运行程序**生成器（分块追加完成，171 行）
      — 寄存器分块 GEMM、确定性初始化、CPU 参考、2^-5 分级对拍、hipEvent 计时、UNIKERN_RESULT 输出
- [x] `src/codegen_other.cpp`：MSL / OpenCL C 内核源（与 HIP 同构的寄存器分块）
- [x] `src/backends.cpp`：三后端改调 codegen；AMD 用 find_hipcc() 真实探测
- [x] 清理：`build*/` 与生成物加入 .gitignore 并 `git rm -r --cached`（原被误提交）
- [x] **阻塞 1 已解**：`src/runner.cpp` 已用 Write 工具重写（不经 shell 传输，路径前斜杠，filesystem 探测 hipcc）
- [x] **阻塞 2 已解**（工作流「CLI 修复工程师」）：`src/main.cpp` 适配三参 generate + run/verify 子命令 → commit 94ec757
- [x] 工作流「UNIKERN MVP 收官」完成全部剩余阶段：HIP 真机闭环（ff966fa）/ SQLite 账本（8c2c980）/ 流式双缓冲（1eb16d9）/ R1-R10 验收矩阵（c7339d4 + 独立复核修复 859e79e）

## 下一步（按序）
1. 零反斜杠重写 runner.cpp（前斜杠路径 + char(92) 判断 + 分块写入 ≤80 行）
2. main.cpp 适配三参 generate，加 `build`/`run` 子命令
3. 构建 → 生成 HIP 程序 → hipcc 编译 → 在 RX 6600M 上运行 → 读 UNIKERN_RESULT
4. 用实测 ms 覆盖 Ledger 的 c_ms 占位，报告 ε = |C − T|

## Session 2026-10-27（Phase 6/7 收尾，后续 session）
- [x] SQLite 账本上线（amalgamation 3.47.2 静态库）+ R7.4 事务 + R7.2 命中含 tile → commit 8c2c980
- [x] R8 场景 B 流式双缓冲 + 端到端口径 + 计时尺寸去上限 → commit 1eb16d9

## Session 2026-10-27（Phase 8 — MVP 验收收官）
- [x] `tests/acceptance.cpp`（#undef NDEBUG）加入 CMake+ctest：R1 canonical 唯一性
      + 非法类型名拒绝；R2 独立复算 22/25 过滤 + 违反记录 + |X|；R3 10⁴ 次逐位
      确定性 + HCS 常量采纳 + constants_version 注记；R4 等 C 平局组键裁决 (128,128)
      + 不可行终止；R5 三源非空 + 头部 HCS 指纹 + (KML,HCS)-only 输入；R7 SQLite
      往返/命中含 tile/measured_ms 直查/crash-tx 事务；R9 缺失字段全量报告 + 无误报；
      R10 三厂商映射 + availability 语义 + 无静默替换。R6/R8 真机项不进单测
- [x] `ctest --test-dir build --output-on-failure` → **2/2 全绿**（smoke 0.05s +
      acceptance 0.16s，100%）；首版 R1 深树断言子树多重集写错（测试缺陷）已修
- [x] 真机证据（RX 6600M, gfx1032）：`run --dims 1024 1024 1024` → verify=PASS
      max_rel=0 measured_ms=2.69137 mode=resident → LEDGER COMMIT（ε_rel=0.03125）；
      同键二次 run → **LEDGER HIT c_cached_ms=0.490829 tile=128x128 (from ledger)**；
      `run --dims 32768 32768 32768` → mode=streamed shards=64 prefetch_d=2
      measured_ms=30806.4 kernel_ms=48465.8 verify=PASS（e2e < 串行参考 → 重叠生效）
- [x] `solve --emit`：EMIT amd lines=390 / apple lines=61 / intel lines=53，
      三源头部均含 fp=8039464947ce；`backends` 表 amd yes / apple no / intel no
- [x] `docs/acceptance-mvp.md`：R1-R10 逐条 → 验收方式 → 证据（真实命令+输出摘录）
      → 状态（✅ 本机已验 / 🟡 部分已验 / ⛔ Apple、Intel 真机受阻）；5 项缺口
      （G1-G5）如实列出
- [x] task_plan.md 全阶段落定（Phase 0-8 complete）；findings.md 增 Phase 8 事实
- [x] commit：tests/acceptance.cpp + CMakeLists.txt + docs/acceptance-mvp.md +
      task_plan.md + findings.md + progress.md

## MVP 状态
Phase 0-9 全部完成（Phase 9 = Intel 真机验证，G4 Intel 半闭合：ocloc AOT + 进程内
OpenCL runner 真机对拍/账本闭环/校准，ε 如实报告，证据 docs/evidence/phase9/）。
剩余真机项：Apple Metal 执行（需用户 Mac）；Intel 流式双缓冲（Phase 9 明确不实现，诚实标注）。

## Session 2026-10-27（Phase 8 独立复核修复，v2）
- [x] 独立复核 5 项全数属实，逐条处理：
  - [x] 「verify_fp32 由 smoke 覆盖」不实（grep tests/ 零命中）→ acceptance.cpp
        补 R6 段：容差函数五路径（相等/容差内/超差点名 index/可配置/尺寸不匹配）
  - [x] 「场景 A 放置标记单测断言」不存在 → 补 R8 段：A/B 两记录逐词断言 + 互异
  - [x] 「跨厂商词汇隔离」测试没有且 uk_intel.cl:13 字面含 "Metal" → R10 改为
        API 词汇级负向断言（hip/__global__ / metal_stdlib/#include<metal/[[buffer），
        文档改「API 词汇隔离 + 诚实注释例外」
  - [x] 证据产物未跟踪 → 重跑全部证据命令，原文转录入库 docs/evidence/phase8/
        （README 列复现命令；emit/uk_amd.cpp 以 add -f 入库并注明原因）
  - [x] build/ CMAKE_BUILD_TYPE 空（-O0 跑绿）→ 重配置 Release，实证
        FLAGS=-O3 -DNDEBUG（build/build.ninja:278），ctest 2/2 绿；纠正
        「Release -O2」口径（MinGW/GNU Release 默认 -O3）
- [x] 复核后真机复测：1024³ PASS measured_ms=2.61237 + LEDGER HIT；
      32768³ streamed PASS measured_ms=40517.8 kernel_ms=63700.3
- [x] docs/acceptance-mvp.md v2：三处夸大表述改为与测试源码一致；引用转录路径；
      头部标明构建类型与 NDEBUG 证据
- [x] commit（tests + docs + evidence + findings + progress）

## Session 2026-10-27（收尾审计 v3 — ε 实算缺陷修复）
- [x] 人工审读 docs/acceptance-mvp.md 发现独立复核漏网缺陷：LEDGER COMMIT 打印的
      epsilon_abs=1e-06 / epsilon_rel=0.03125 是**硬编码常量**（对拍容差 2⁻⁵ 及其分母
      下限），非任务计划要求的 ε=|C−T| 实算——三组不同实测对应同一常量即暴露点
- [x] src/main.cpp 修复：epsilon_abs=|measured−predicted|、epsilon_rel=ε_abs/max(c_ms,1e-9)；
      误导性注释（"口径一致"）重写为诚实口径说明（HCS link 32GB/s 标称 vs 真实 PCIe 有效带宽，
      大 ε 即 R7.3 校准信号）→ 构建 0 告警，ctest 2/2 绿
- [x] 真机证据全量重采（RX 6600M）：1024³ resident ε_abs=2.35ms ε_rel=4.79（miss→commit）
      → 二跑 LEDGER HIT ε_rel=4.27；32768³ streamed ε_abs=51.75s ε_rel=14.37
      （e2e 55.4s < 串行内核 71.4s，重叠不变式保持）
- [x] docs/acceptance-mvp.md v3：R6/R7/R8 证据块刷新 + 审计补遗一节；三份转录与
      ledger_dump.txt、README 复现命令同步；task_plan 增 E13；findings 见下
- [x] commit（src/main.cpp + docs + evidence + 规划文件）

## Session 2026-10-27（v4 校准闭环 — G2/G3 闭合）
- [x] G2 闭合：`Hcs.constants_version` 随 HCS 透传（solver stub 移除，P1 保持纯函数）；
  `ILedger::measured_rows(fp)` + `commit(key, sol, measured, kernel_ms)`（SQLite
  PRAGMA 迁移加列 / .jsonl 7 字段格式，双向兼容旧格式行）；`calibrated_link_bw`
  = compulsory bytes/(measured−kernel) 的确定性中位数；`hcs_export`（max_digits10，
  E4 教训复用——首版 6 位精度被单测当场抓住）往返 hcs_import；CLI 新增 `calibrate`
- [x] G3 闭合：三后端产物头 `toolchain=<hcs.toolchain>`（R5.3 完全符合）
- [x] 验收单测扩充：[CAL] 段（三种账本行格式 + 中位数合成用例 + 版本透传 + 导出往返
  + 指纹与版本无关）；R3 版本透传断言；R5 toolchain 头断言；R7 kernel_ms 往返
- [x] 真机校准闭环（RX 6600M）：run（ε_rel=4.83）→ calibrate（32→5.429 GB/s，v1→2）
  → 校准后 run：predicted 2.415ms vs measured 2.707ms，**ε_rel=0.12**
- [x] docs/acceptance-mvp.md v4：R3/R5/R7 矩阵行与章节刷新，G2/G3 标记闭合，
  校准闭环补遗一节；证据新增 calibrate.txt / run_calibrated.txt / hcs_amd.calibrated.json，
  solve_emit + emit 三源 + ledger_dump 同步刷新（含 kernel_ms 列）
- [x] ctest 2/2 全绿；commit（11 源文件 + 文档 + 证据 + 规划）

## Session 2026-10-27（Phase 9 — Intel 真机验证，G4 Intel 半闭合）
- [x] 环境重探测（此前"无 Intel 工具链"结论已过时）：
  - **Intel Arc B390 GPU OpenCL 3.0 NEO** 在位（驱动 32.0.101.9150；sycl-ls 可见；
    平台标签含 "(integrated)" 字样，设备事实须经 OpenCL API 实测，不照抄）
  - System32 有 OpenCL.dll（Khronos ICD loader）+ ze_loader.dll；
    HKLM\SOFTWARE\Khronos\OpenCL\Vendors 已注册 intelocl64.dll
  - oneAPI 2025.3 + 2026.1 已安装：ocloc.exe ×2 版本、CL 头在
    `C:/Program Files (x86)/Intel/oneAPI/2026.1/include/CL/`、sycl-ls.exe
  - 无 MinGW g++（本机构建工具链为 MSVC 2022 Community + vcvarsall + Ninja；
    OpenCL 经 oneAPI OpenCL.lib 导入库链接；Python conda-forge opencl-headers
    用于 vendor Khronos CL 头（18 头 + Apache-2.0，third_party/CL）
  - CPU 平台：Core Ultra X7 358H OpenCL 3.0（枚举时注意与 GPU 平台区分）
  - 结论：G4 的 Intel 半侧阻塞解除，本机可做 ocloc AOT + 进程内 OpenCL 真验证
- [x] Phase 9 规划落定（task_plan.md：阶段表 + 子项 a-h + 约束提醒）；
  findings.md 过时行（"无 ocloc / 无 Intel 平台"）更正
- [x] 子项 a sanity：MSVC + oneAPI OpenCL.lib 链接 + 枚举平台/设备经 API 实测
  （B390：96 CU、27207761920 B 显存、2.5 GHz、host_unified=1；平台标签
  "(integrated)" 字样不采信）；ocloc 设备名 bmg-g21（AOT 编译产出 .bin+.spv）
  - 踩坑：OpenCL 枚举函数指针签名必须与 API 三参（num_entries/platforms/
  num_platforms）一致，且 windows.h 会定义 near/small/big 宏污染变量名
- [x] 子项 b/c/d/e/f：runner_ocl.cpp（256³ 对拍/计时/UNIKERN_RESULT，UK_HAS_OCL
  门）+ BackendIntel 真探测 + main.cpp --vendor + CMake OpenCL 接入
  + acceptance R10 独立 Win32 镜像（ctest 2/2 全绿）
- [x] 子项 g 真机闭环（samples/hcs_intel.json 更新为实测设备事实）：
  verify=PASS max_rel=0 → run LEDGER COMMIT（ε_abs=1.31315 ε_rel=2.72046）
  → 同键 HIT → calibrate（link 32→18.5877 GB/s，v1→2）→ 校准后 run
  （ε_rel=1.48813，残留为 HCS flops 常数精度，如实报告）
- [x] 子项 h 收尾：docs/evidence/phase9/ 11 文件转录入库（emit/uk_intel.cl 与
  aot 产物 add -f 入库）+ acceptance-mvp.md v5 + task_plan/progress/findings
  更新
## Phase 9 闭合记录（2026-10-27，提交 92d17a2）
- G4 Intel 半侧完成：ocloc AOT + 进程内 OpenCL runner 真机验证（对拍/账本闭环/校准，
  ε 如实报告），证据 docs/evidence/phase9/；acceptance-mvp.md v5；task_plan/progress/findings 落定
- ctest 2/2 全绿（Release，含 R10 Intel 镜像）；工作区干净
- 目标状态：task_plan 全部 9 阶段 complete，check-complete 口径（全部阶段 complete +
  无 in_progress）通过；遗留边界如实标注：Apple Metal 真机（需用户 Mac）、Intel 流式
  双缓冲（不实现）

## Session 2026-10-27（Phase 10 — Apple 真机验证，G4 全闭合）
- [x] 环境事实核实（Apple M4 / macOS 26.6.2 arm64 / Apple clang 21）：Xcode.app 在位但**无 Xcode SDK**，
  `xcrun --sdk macosx --show-sdk-path` → CommandLineTools SDK；Metal/Foundation 均链接自 CLT SDK；
  **Metal 运行时 JIT**（`newLibraryWithSource`）无需 `metal` 编译器与 meta-cpp → Apple 后端从
  "仅源生成"升为"真编译真执行"
- [x] 子项 a sanity：`probe/uk_metal_probe.mm` 枚举设备（Apple M4 / hasUnifiedMemory / maxThreads 1024）
  + JIT 编译 MSL + 256³ 对拍 PASS
- [x] 子项 b/c/d：`src/runner_metal.mm`（进程内 ObjC++ runner，与 HIP/OCL 同构契约）+
  `BackendApple::available()` 改运行时 `metal_available()` + `--vendor apple` 端到端 +
  CMake OBJCXX/`-fobjc-arc`/find_library + acceptance crash-tx 子进程跨平台化（绝对路径 + POSIX 分支）
- [x] 子项 e 真机闭环：verify PASS max_rel=0 → run LEDGER COMMIT（ε 实算）→ 同键 HIT →
  calibrate **场景 A 诚实门控**（默认 rc=3 SKIP；`--allow-unified-calibrate` 显式强制），
  并用"强制后 `PREDICTED demo c_ms` 逐位不变"反证该常数在统一内存路径上确实惰性
- [x] 子项 f 收尾：docs/evidence/phase10/ 转录入库 + acceptance-mvp.md v6（Apple 行 ⛔→✅，
  ⛔ 符号退役）+ codegen R8 honesty 横幅改为"resident 已真机验证"
- [x] 子项 g 收尾复核（本轮，发现三类如实缺陷并修）：
  1. **文档数字与入库转录不符**（acceptance `ε_rel=99.6471`、README `5.18 GB/s`、
     task_plan `1.029/99.6` 皆出自未入库一次性读数）→ 真机链路全量重采 10 份转录 +
     `emit/uk_apple.metal` 构件本体入库，文档只引用当前转录读数（ε_abs=1.26059 /
     ε_rel=122.097，HIT 轮 0.8281/80.2078），并在文档头声明重采与 ε_rel 比值口径；
     另发现"强制校准伪影"读数本身在 5.986↔9.097 GB/s 间抖 1.5 倍，反过来印证门控理由
  2. **新门控零单测约束**（`--allow-unified-calibrate` 仅有真机转录）→ acceptance 补 R7.3
     **四臂经真实 `unikern` 二进制**：拒绝消息 / 加旗标过门（顺序判别器）/ 加旗标真校准
     （已知实测行 → link 恰为 6.291456 GB/s、version 1→2）/ 场景 B 对照（32→6.29146，
     门控不吞 AMD/Intel 路径）；**摘掉门控后臂 1 转红**（转录 phase10-gate-arm-probe.txt），
     故该门控有回归位；顺带修 POSIX `system()` 返回 wait 状态（exit(3)→768）的断言坑（E15）
  3. **.gitignore 声称≠事实**：README 写"build-apple/ 已忽略"而实际未忽略 → 补
     `build-apple/`、`uk_run/`、`probe/uk_metal_probe`（源码入库、产物不入库）
- [x] 新边界如实入册（不在本轮顺手实现）：**G6** — 当时 Metal runner 的 resident 预算只取 HCS
  声明的 32 GiB（runner_metal 的 110 行），未与 `recommendedMaxWorkingSetSize` 交叉校验；
  本机 payload 3 MB 远低于两者，读数与结论不受影响，但"0.85×容量"目前只对声明容量成立。
  **时点演进（2026-10-29 注）**：这条已在 R10.3 落地——设备自报值现在装进 `max_ws`（`src/runner_metal.mm:112`）
  并由 `ws_refuse`（`src/runner_metal.mm:213`）与 payload 交叉校验，不再只对声明容量成立。
- [x] ctest 2/2 全绿（acceptance 0.55s，含新增四臂）；findings/task_plan 落定

## Phase 10 闭合记录（2026-10-27，提交 308e56e）
- G4 全闭合：AMD（Phase 8）+ Intel（Phase 9）+ Apple（Phase 10）三厂商真机执行与对拍均完成；
  证据 docs/evidence/phase10/（10 份命令转录 + 生成构件 + README + 带弹性探针）
- acceptance-mvp.md v6.1；task_plan/progress/findings 全部落定；.gitignore 补齐
- ctest 2/2 全绿（Release，Apple 主机）；R7.3 场景 A 门控具备回归位（摘门控即红）
- 目标状态：task_plan 全部 10 阶段 complete。遗留边界如实标注：G1 KML 文本解析（V1）、
  G5 R8.5 缺页迁移监测（真机长测）、Intel/Apple 显式流式双缓冲（不实现，横幅声明）、
  **G6 Apple resident 预算未与 Metal 工作集上限交叉校验（新记录，未实现）**、
  ε 残留的 roofline 常数标定（模型精度范围）
- 平台边界如实标注：R7.3 四臂在 macOS 实跑；Windows 分支沿用同文件既有 cmd.exe 引号写法，
  本轮未在 Windows 主机复跑

## Session 2026-10-27（Phase 11 — G7 非 GEMM 静默错执行闭合）
- [x] 缺陷定位到读数级：库级直调证明 reduce 树拿到的是 gemm 内核 + 混着默认维的账本键
  （`PM = 1048576, PN = 4096, PK = 4096`、零拒绝）；真机侧错配调用 `perf=1048576x32x32
  verify=PASS measured_ms≈5.3` 回写 `reduce:...` 键 → 会污染 R7.3 校准输入
- [x] 三层拒绝：`op_support_gap()` 单一判据 → 生成物自拒（HIP 退 4；MSL/OpenCL 无内核）→
  runner 入口自拒（排在写 workdir/编译之前）→ CLI 门（排在 ledger 读写之前）
- [x] `--op gemm|reduce` 声明面第一片；conv 按 arity 在 parse 拒绝（六维 vs 三维，
  接受即在隐式默认上求解）；`solve --op reduce` 可解但打印 model-only 且拒绝 `--emit`
- [x] acceptance 新增 R10.2 臂（库/产物/CLI + gemm 对照 `PM = 64` 三源齐发）；
  `probe/phase11_arm_probes.py` 逐层摘除验红：谓词 → 断言 `op_support_gap(...)==(op==Gemm)`，CLI 门 → run 臂 `rc == 2`，
  runner 门 → 真机探针转红而 **ctest 仍绿**（该层无单测回归位，如实分级并记录）
- [x] 自捉两处坑并修正：`-x objective-c++` 吞掉后续静态库输入（须 `-x none`）；
  第一次录 runner 拒绝对照时用了"摘门控"态编译的旧探针 → 量到过期产物，
  改为转录内同录库 md5 并重编译（B 臂 `17b4384e...` vs A 臂 `7993adce...`）
- [x] 证据入库 docs/evidence/phase11/（A/B 缺陷重放、CLI 四臂、runner 拒绝、
  三层带弹性探针、ctest）+ 探针本体五份 + README；.gitignore 增两条探针二进制；
  引用行号漂移修正（runner_metal.mm:113 → :110，Phase 10 的 G6 陈述与 progress 同步）

## Phase 11 闭合记录（2026-10-27，提交 3e1f09c）
- G7 closed：非 GEMM 的"声明即执行"不再可能——不支持即点名拒绝，且被拒的树落不下账本行
- 新增开放项 G8（R10.3 每后端原语覆盖输出无人认领，`grep R10.3` 在验收文档/代码/测试零命中）；
  G1（conv/reduce 真正可用的内核生成与 KML 文本前端）保持开放，本轮刻意不做
- ctest 2/2 绿（Apple 主机）；残留探针标记校验为 0；平台边界：OpenCL 仅语法检查、
  AMD hipcc 真编译路径未复跑，Windows/ROCm 复跑待做

## Session 2026-10-27（Phase 12 — 算子面闭合：G1 声明半边 + G8 覆盖报告 + G9 键身份）
- [x] 单一 JSON 解析器：新增 `src/json_flat.{hpp,cpp}`（trim/field/objects/numbers/keys），
  `src/hcs.cpp` 改为复用；行为不变的判据是 `hcs_apple.json` 指纹仍 `f01e6704` + ctest 绿
- [x] KML 文本前端 `src/kml_front.cpp`：三算子（gemm 3 / conv 6 / reduce 1）按 arity 声明，
  违规**一次列全**（未知与拼写错误字段、op/dtype 名、dims 个数/非整数/非正数、layout 不支持、
  children 带路径）；children 先切作用域再读父标量（防父借子字段）；export 恒全显式、
  round-trip canonical 相同
- [x] layout 口径：只接受 "NT"（三后端 kernel 实际行主序索引），其它前端即拒——
  收下不实现再默默按行主序执行＝G7 同类静默
- [x] G9 键身份：`op_signature` 与 `canonical_form` 改按新的单一权威 `kml_arity()` 取维；
  `[R7.1]` 臂断言 gemm 键逐位不变（已入库证据无需迁移）+ conv 三形状互异 +
  reduce 未用槽位不泄漏。发现路径是**自捉**：测试里 reduce 只设 dims[0]，
  结构体默认 4096 漏进了签名
- [x] R10.3 覆盖报告 `primitive_coverage()` + `COVER` 行（primitive / execution / toolchain
  三轴分别取自 `op_support_gap()`、`supports_streaming()`、`available()`，无第二张表），
  进 solve（SOLVED 与 HIT 两支）/ run / `kml` 三入口；并在文档明确
  "R10.2 纳入 F 约束"目前的真实形态（solve 不知后端，表现为拒绝 + 点名）
- [x] CLI：`kml <file>` 子命令、`--kml` 接入 solve/run/verify、usage/help 同步；
  新增三样例 `samples/kml_{gemm,conv,reduce}.json`
- [x] acceptance 新增 `[R1.1/2]`、`[R7.1]`、`[R10.3]` 三段；`probe/phase12_arm_probes.py`
  六腿逐一摘除各自转红（红点行号 323/313/345/783/796/1133），收尾残留 0 + ctest 2/2 绿；
  Phase 11 的摘除脚本锚点因 `op_support_gap()` 加 layout 轴而失配，已同步改写
- [x] 真机闭环（声明驱动）：Apple M4 上 `run --kml samples/kml_gemm.json` →
  verify=PASS max_rel=0、COMMIT ε_rel=88.7514、同键 HIT 84.2782；
  conv/reduce 声明 rc=2 且账本文件不创建（gate 在 make_ledger 之前）
- [x] 自捉第 2 处：`solve` 连只读查找都会创建账本文件 → "无账本行"断言必须用未触碰的新路径

## Phase 12 闭合记录（2026-10-27，提交 1f20b90）
- G1 **声明半边**闭合（三算子可从文本声明、被求解、被规范化、进账本键，非法声明完整拒绝）；
  G8 闭合（R10.3 覆盖报告三轴有实现与回归位）；G9 闭合（键按算子 arity 编码，gemm 键逐位不变）
- G1 **执行半边仍开放**：conv/reduce 可用的内核生成未做，执行仍由 Phase 11 三层拒绝点名拦着
- 其余开放项不变：G5（R8.5 缺页迁移监测，长测）、G6（Apple resident 预算未与 Metal 工作集上限
  交叉校验）；R10.2 的 F 约束合流属 V1（口径已在文档写明）
- ctest 2/2 绿（Apple 主机，acceptance 含三段新臂）；平台边界：`--kml` 端到端只在 Apple 实跑，
  AMD/Intel 真机与 Windows/ROCm 复跑待做，`runner_ocl.cpp` 仍只有语法级检查

## Session 2026-10-27（Phase 13 — conv/reduce 执行面（Apple 先行）+ G6）
- [x] G6：`recommendedMaxWorkingSetSize` 作为**并列**上限（不与 0.85×声明容量叠乘），
  UK_DEVICE 行打印实测值；本机 12.7 GiB vs 旧门放行的 27.3 GB（差 2.1 倍）。
  三条带内臂见 `docs/evidence/phase13/phase13-g6-working-set.txt`；不给"摘门"探针，
  理由（会让 20 GB 载荷走到分配阶段）写进了 README
- [x] 能力面 per-backend：`IBackend::capability_gap()`（默认共享谓词、Apple 覆盖三算子）；
  CLI run 按所选后端判、`--emit` 按任一覆盖判；覆盖表出现异构行
- [x] MSL：`uk_conv`（implicit GEMM/valid padding）与 `uk_reduce`（两段）；
  `src/reference.cpp` 提供 cpu_conv/cpu_reduce 作唯一真值
- [x] runner_metal 泛化：plan 按算子（gemm 三维 / conv 六维 / reduce L+P）、三条 enc_* 与
  time_passes、perf 按算子声明打印；**自捉** reduce 首跑 FAIL：pass-1 线程组 4096 超上限
  被 Metal 静默不执行（读到全零）→ 组宽 min(p,64)，并补命令缓冲状态检查作纵深防御
  （但抓到问题的是 verify，不是状态检查——如实记录）
- [x] 真机闭环：conv 64×64×32×3×3×64 与 reduce L=4194304 均 verify=PASS max_rel=0 →
  COMMIT（ε_rel 334.6 / 10.9）→ 同键 HIT → 再 COMMIT（243.9 / 8.87）；gemm 回归不变；
  AMD/Intel 对同一份声明 rc=2 且账本文件不创建；`solve --emit` 异构产出实测
- [x] acceptance 新增 `[R13]`（参考实现手算 + per-op 内核结构 + 能力面拆分），改写
  `[R10.2]/[R10.3]` 为异构期望；`probe/phase13_arm_probes.py` 五腿逐一摘除即红
  （:1044 / :1080 / :1080 / 设备 no_function / 设备 verify=FAIL），残留 0 + ctest 2/2 绿

## Phase 13 闭合记录（2026-10-27，提交 6ac32a5）
- G6 closed（Metal 工作集上限并入第二道门，实测值入证据）；G1 的 **Apple 执行半边** closed
  （conv/reduce 真机对拍 + 账本闭环）；R10 覆盖报告出现异构行并被臂约束
- 仍开放：G1 的 **AMD/Intel 执行半边**（conv/reduce 的 HIP/OCL 内核）、这两家的 `--kml`
  真机复跑、G5（R8.5 长测）、R10.2 的 F 约束合流（V1）、conv/reduce 调优与浮点次序鲁棒性
- ctest 2/2 绿（Apple 主机）；平台边界：conv/reduce 只在 Apple 真机跑过，
  `runner_ocl.cpp` 仍仅 `-fsyntax-only`

## Session 2026-10-28（Phase 14 — Intel/OpenCL 的 conv/reduce）
- [x] 生成：`ocl_kernel_source` 按算子分派（uk_conv / uk_reduce_part+uk_reduce_sum）；
  每份源只带自己算子的内核，`[R14]` 逐算子断言
- [x] 能力面三向：`ocl_op_gap()` + `BackendIntel::capability_gap()`；覆盖表
  amd unsupported / apple+intel covered；toolchain 轴单列（本机 intel=absent）
- [x] runner_ocl 泛化：plan 按算子、三个 launcher + 共用 `time_passes`、reduce 双 kernel
  （OclAll 加 ker2）、grid 上限按算子取数、perf 按算子声明打印
- [x] 本机验证两层：三份 .cl 过 `clang -x cl` 语法检查（负测可红）；runner host 侧过
  `-fsyntax-only -DUK_HAS_OCL`；**运行未验**（本机无 Intel 设备，`run --vendor intel`
  以 no-device rc=1 收场，账本不建）
- [x] acceptance：改 `[R10.2]/[R10.3]/[R13]` 为三向期望，新增 `[R14]`；
  `probe/phase14_arm_probes.py` 三腿逐一摘除即红（:1042 / :1303 / :1249），残留 0、ctest 绿
- [x] Apple 三算子回归仍 PASS（gemm/conv/reduce 全 verify=PASS max_rel=0）

## Phase 14 闭合记录（2026-10-28，提交 dc4af98）
- G1 执行半边的 **Intel** 侧闭合（生成+能力面+runner 分派；编译级验证，运行待真机）
- 只剩 **AMD 的 HIP 程序**：本机无 hipcc，连语法都验不了 → 需 ROCm 主机，留 Phase 15
- Phase 13 的两条旧结论（Intel 也拒 conv/reduce；OpenCL 仍 gemm-only）已据实标记取代

## Session 2026-10-28（Phase 15 验收收尾 + Phase 16 驻留守卫落账，v12）
（前序：Phase 15 本体见提交 46e117b——conv/reduce 的 HIP 程序 + RX 6600M 真机闭环；
Phase 16 本体见提交 ea76c2f——G5/R8.5 驻留守卫。本轮把两项真机成果落进验收矩阵与规划文件。）
- [x] 复核重采（当前二进制 md5 `e554574a…`，`cmake --build build` → `ninja: no work to do.`，
  与 HEAD 同源）：`backends`（amd=yes / apple=no / intel=no）、`solve --emit` 三源行数
  （conv 253 / reduce 217 / gemm 425 = 首轮各 +34/35 行即 Phase 16 守卫；`uk_resident` 计数各 2）、
  三算子真机 run×（COMMIT+HIT，fresh ledger）——六轮全 verify=PASS max_rel=0 且
  UNIKERN_RESULT 尾 **migrations=0**（COMMIT ε_rel conv 68.0154 / reduce 0.354586 /
  gemm 3.43836；HIT 轮 67.4707 / 1.10948 / 3.41299）、`ctest --test-dir build
  --output-on-failure` 2/2 绿
- [x] 证据入库：`docs/evidence/phase15/phase15-recheck-v12.txt`（全量转录，二进制 md5 同录）+
  README 补 v12 复现命令表；首轮 phase15/phase16 转录保留不覆盖，旧引用仍可解析对照（E14）
- [x] acceptance-mvp.md **v12**：头部版本注记；R6 行补 migrations=0 复采；R8 行 R8.5 句改写
  （驻留守卫已实现并真机验证，长测如实保留开放）；**G1 行改写为全闭合**（AMD 执行半边
  v11 已闭合，v12 复采确认）；**G5 行改写**（守卫实现 + migrations=0 真机 + 拒绝分支
  exit=5 + 长测开放）；新增"Phase 15 验收收尾 / G5 驻留守卫补遗"段
- [x] 规划文件：task_plan.md Current Phase 改写为**本机可做项已清零** + 剩余开放缺口清单
  （Intel conv/reduce 真机运行、G5 长测、R10.2 F 约束合流 V1、conv/reduce 调优与浮点次序
  鲁棒性）；缺口闭合表新增 G5 行（closed，长测开放）；Phase 15 子任务表补 f 行（v12 复采）；
  findings.md 补 Phase 16/复采事实段
- [x] ctest 全绿后 `git add` 具体文件提交

## Session 2026-10-28 续（v12.1 独立复核修订）
- [x] 复核三条全处理：① 汇总矩阵 R1 行 + R1 详段（v8/v9 旧文与 G1 全闭合矛盾）据实改判
  ——AMD 声明执行真机 COMMIT→HIT（引 v12 复采转录）、Intel `COVER covered`+`RUN FAILED`
  rc=1 零落库（本机新二进制实测），旧 rc=2 读数标注为 Phase 12 时代；② Phase 14 补遗的
  失败消息改注出处（Mac 侧构建 + `uk_ocl_log`（`src/runner_ocl.cpp:516`），转录只捕获 rc=1/零落库），
  补 Windows 构建实测消息；③ Phase 9 ocloc AOT 三件产物 `git restore` 恢复
- [x] 自捉修复：`unikern ledger <path>` 静默忽略路径参数（`measured_rows` `src/main.cpp:604` 硬编码
  kDefaultLedger）→ 尊重参数；重验 `ledger uk_run/v13/intel_conv.sqlite3` → 0（intel
  失败 run 零落库）、jsonl 同 0；二进制 md5 → `9759d55a74e90acd11017d4d6b67a09a`
- [x] 实测辨析"不建账本"语义：.jsonl 失败 run 不建文件；.sqlite3 建空库（零 solution 行）
  ——文档改写为"solution 零落库"（R6.3 承重事实）
- [x] ctest 全绿后 git add 具体文件提交

## Session 2026-10-28 续2（E17 修复 + 本机盘点 + v12.2 复核残留清尾）
- [x] 本机重新盘点（并行会话 Phase 9-14 落地后）：本机 hipcc/ROCm 7.1 在位；**无 Intel 设备**
  （无 Khronos Vendors 注册表键、无 ze_loader.dll、无 ocloc——Phase 9 的 Intel 真机验证在
  另一台机器完成）；无 Metal（Apple 侧已在 macOS 机器闭合）→ "本机能做的" = Phase 15
  （AMD conv/reduce）+ G5 驻留守卫
- [x] E17 修复：并行会话 Phase 10 的无条件 `project(... OBJCXX)` 使 Windows 配置直接失败
  → OBJCXX 仅 APPLE `enable_language`、runner_metal.mm 仅 Apple 编译、非 Apple 链接诚实
  stub `runner_metal_stub.cpp`（4224d42）；基线恢复：配置成功（OpenCL 诚实 stub）、构建
  零告警、ctest 2/2 绿、backends 诚实（amd=yes/apple=no/intel=no）
- [x] 动态工作流（dwfrun-af8f4aec）执行 Phase 15+16 收官：conv/reduce HIP 程序 + RX 6600M
  三算子真机闭环（46e117b）；G5/R8.5 驻留守卫 migrations=0 + 超订退 5（ea76c2f）；
  验收矩阵 v12 落账（84b2438）；v12.1 独立复核修订（b9cd4e8）
- [x] v12.2 复核残留清尾（df44dd8）：① R1 行 intel 读数补全 `--hcs` 可复现命令形式 +
  实测转录 `docs/evidence/phase15/intel-r1-command.txt`（COVER covered + RUN FAILED rc=1
  + 零落库，根因逐字可见：本机构建期无 OpenCL 导入库 → runner stub 且无 Intel 设备）；
  ② v12 补遗 md5 锚标注为历史时点 + 当前锚注记，`binary-md5.txt` 记录全演进
  （当前 unikern.exe `9759d55a…` / acceptance `1c0f8620…` 不变）
- [x] ctest 2/2 绿；工作区净（仅 .serena/ 未跟踪，会话前即存在）；HEAD = df44dd8

## Session 2026-10-28（Phase 17 — Intel 三算子真机运行验证，开放缺口①闭合）
- [x] 机器口径澄清：v12 的"本机无 Intel 设备、intel=no"是 AMD 机器侧并行会话的读数；
  本会话运行于 Intel Arc B390 机器，同一 backends 命令实测 intel=yes（两份转录并存）
- [x] 本机基线：Phase 10-16 变更后的 CMake 重新配置（OpenCL 发现 → -DUK_HAS_OCL）
  + 构建 + ctest 2/2 绿
- [x] gemm 回归：1024³ verify=PASS max_rel=0 → COMMIT ε_rel=2.4714 → 同键 HIT
  （与 Phase 9 同键同 tile，行为不变）
- [x] conv 真机首验：--kml samples/conv.kml → verify=PASS max_rel=0 → COMMIT
  ε_rel=13.5646 → HIT（Phase 14 只做过 clang 语法检查，首次真机执行）
- [x] reduce 真机首验 + E18 修复：首跑被错误拒绝（grid_too_large——
  CL_DEVICE_MAX_WORK_ITEM_SIZES 是本地工作组上限而非全局网格上限，verify=PASS 与
  refused 并存即暴露点）→ 删错误检查、代之以 gemm local 256 对
  CL_DEVICE_MAX_WORK_GROUP_SIZE 的真值门 → verify=PASS max_rel=0 → COMMIT
  ε_rel=0.883955 → HIT
- [x] E19 修复：time_passes 事件完成前读 profiling——本机驱动返回全零而非
  NOT_AVAILABLE → 间歇性 measured_ms=0（本会话实测捕获一次）；读前 clWaitForEvents
  （与 HIP 程序 hipEventSynchronize 同纪律），e2e 与 kernel-only 两括号都加
- [x] 证据入库 docs/evidence/phase17/（backends/ctest/三算子 commit+hit 转录 +
  E18/E19 前后对照 + README）+ acceptance-mvp v13 + task_plan（Phase 17 行 +
  E18/E19）/findings/progress 落定
- [x] 目标核验：本机可做项清零——剩余开放缺口 ②（AMD 机器侧长测）③（V1）
  ④（开放型 R&D）均非本机任务，据实分类


## Session 2026-10-28 续4（Phase 18 — G5 长测半边 / R8.5 后半，本机真机）
- [x] 先测量后定标：32768³ 流式在 Phase 16 守卫落地后**首次**真机重跑 →
  `mode=streamed shards=64 shard_rows=512 prefetch_d=2`、verify=PASS max_rel=0、migrations=0、
  rc=0、COMMIT ε_rel=5.05902；预登记四项全中，**无回归**（结构核对：pinned 分片走
  `hipHostMalloc` 不经守卫）。顺带推翻 Phase 8 的 kernel_ms>e2e 读数（55354.1/71354.8 →
  21819.9/20610.6，旧转录不改写）
- [x] 规格核对：R8.5 第二半"率超阈 → 记录违规 → 触发重新求解"在本机是**缺失的消费方**——
  exit 5 被 `cmd_run` 归进 `RUN FAILED` rc=1，注释里写的"上层重求解信号"从来没人接
- [x] 实现：D7 率口径 `migrations/checks`（实测 checks=6）；`--migration-threshold` 默认 0.0
  （与 Phase 16 语义等价，放宽需显式，`nan`/负数 parse 即拒）；`ILedger::record_violation` +
  `violation_count()`（SQLite `violations` 表 / 文件 `#VIOLATION` 行，`size()` 排除事件）；
  重求解只取 `hipMemGetInfo` 实测空闲容量并标 `Measured`，无实测即 STOP，一次有界重试，
  重试行按 `ledger_key(tightened, tree)` 落账
- [x] 真机开火控制 `probe/phase18_alarm_probe.py`（rc=0）：注入 `hipMalloc→hipHostMalloc` 后
  整条链跑通，oversubscription(migrations=1 checks=1 vram_free=8425177088) → UNIKERN_VIOLATION
  rate=1 → RESOLVE re-solve 8589934592→8425177088 → 重试再违例 → RESOLVE STOP，**rc=5、
  solution 零落库、账本两条 `#VIOLATION`**；try/finally 复原 + 残标断言，复原后 ctest 2/2 绿
- [x] 摘除即红：首版 token 臂**实测不承重**（守卫自带同一 token，`find` 恒真，见 E21），
  改出现次数判据后 `probe/phase18_leg_removal_probe.py` 四条腿逐一摘除全红、复原 byte-identical
- [x] 证据入库 `docs/evidence/phase18/`（README + 4 份转录）；acceptance-mvp.md v13 补遗；
  规划三件同步（Phase 18 行/子任务表/缺口表 G5 行/E20 E21/Current Phase）
- [x] 踩坑 E20：构建失败那一轮 ctest 仍报 2/2 绿（跑的是旧 exe）——判绿前必须确认链接真的发生
- [x] 长测序列本体（18 e 项）：驻留 reduce **15 轮** + 流式 32768³ **2 轮**，全 rc=0、每轮
  `migrations=0 checks=6`，率恒 0；`vram_free` 驻留恒 8407547904、流式恒 3848339456（差值=程序
  自身活分配，所以它是"探针时刻的点采样"，不是设备标称容量）。序列首版转录是**零读数**（E22：
  ledger 目录不存在 + grep 白名单吞掉失败行），重跑后每轮显式打 rc
- [x] **自我推翻一次**：我先前写的"Phase 8 的 kernel_ms>e2e 被实测推翻（kernel<e2e 即真重叠）"
  是过度断言——序列里第 2 轮流式又给出 kernel=31288 > e2e=23162。正确结论是 `kernel_ms` 在本机
  流式路径**不是稳定量**；findings / acceptance-mvp v13 / README / task_plan 四处已据实改回
- 状态：改动 12 个文件 + 2 个新探针 + `docs/evidence/phase18/`（README + 5 份转录）；
  **ctest 2/2 绿**、两个探针均 rc=0、工作树无残留注入；**未提交**（提交需明确指示）。
  G5 无待补机制项；仍开放的是他机/V1 项（Intel 真机 conv/reduce、R10.2 F 约束合流、调优与浮点次序）

## Session 2026-10-28 续5（Phase 19 — 次序鲁棒性实测 + conv 基线，开放项 ④ 第一半）
- [x] 读规格与代码定靶：确认出厂 reduce 对拍数据（2²⁰ 个 2⁻⁶）任意次序精确，`max_rel=0`
  从来不是次序证据（生成程序注释自己写了 "verifies structure, not floating-point order"）
- [x] 写并跑栅格探针 `probe/uk_reduce_order_probe.cpp`（hipcc -O3 --offload-arch=gfx1032 真机）：
  n∈{2²⁰,2²⁴} × 8 幅值模式 × nP∈{1024,16384,65536}，同时报"与 fp32 顺序参考之差"和
  "两者各自与 double 精确和之差"
- [x] **实测出真发现**：宽动态范围（交替 1e6/1e-6）下两轮归约离精确和 4.69e-14，而 CPU 参考离
  精确和 7.19e-03——**更准的一方是内核**；n=2²⁴ 时两者之差 3.97e-02–4.00e-02 **越过 2⁻⁵ 容差**
  → `verify=FAIL` 拒入账本（R6.3 生效）。容差/判据未擅自改，升为规格级决策 **D8** 上报
- [x] 两条预登记预测落空并如实记录：B 模式预测 >0 实测 0（测的是吸收不是次序）；
  "降幅值必比升幅值差"只在 nP=1024 成立、nP≥4096 反转
- [x] 新增确定性臂 `[R6.4-order]`（调**生产** `cpu_reduce`，容差上界取 `Tolerance{}.fp32_rel`），
  CPU 实测值与真机 nP=1024 行逐位同形；控制 `probe/phase19_order_arm_probe.py`：把参考累加器
  加宽成 double → `[R15]` `tests/acceptance.cpp:1465` 当场报红，复原 byte-identical、ctest 2/2 绿
- [x] conv 性能基线：128²→61.9 GFLOPS、256²→19.4 GFLOPS（随尺寸**下降**），同机分块 gemm 512³
  →2275 GFLOPS，两个数量级差距即 Phase 20 的验收靶；证据 `docs/evidence/phase19/`（README + 3 份转录）
- [x] 自我纠正两处：① 我在 findings 里凭记忆写的行号锚点（1416-1500 / 1370-1425）全错，
  插入新段后旧段已下移——正是 Phase 17 报告 `doc-code-anchors-drift-unvalidated` 的同款错误，
  已 grep 现取改为 :1485/:1362 并记为 E23；② 又一次"构建失败但 ctest 报绿"（缺 `<cmath>`、
  most vexing parse），第 3 次踩同一坑，判绿前必须确认链接发生
- 状态：改动 `tests/acceptance.cpp` + 新增 3 个探针文件 + `docs/evidence/phase19/`；
  **ctest 2/2 绿**、两个探针 rc=0、`git status src/` 干净；**未提交**（提交需明确指示）。
  待用户定：D8 差分参考口径；待开工：Phase 20 conv 分块调优

## Session 2026-10-28 续6（Phase 20 — `uk_conv` 通道分块 + 实测选档，开放项 ④ 第二半）
- [x] 第一版写死 OCB=4 真机测：256² 124.57→36.35 ms（3.4×）、128² 2.44→0.73（3.35×），
  但 **32² 从 0.0140 退到 0.0306**（线程填不满 14 个 CU）→ 预登记的"≥1.2× 才算收益"在小尺寸上不成立，
  不能靠忽略小尺寸蒙过去
- [x] 改为**由设备自报容量选档**：`template <int OCB>` 四档 + `uk_conv_pick_ocb` 用
  `hipOccupancyMaxActiveBlocksPerMultiprocessor × dp.multiProcessorCount` 取"仍能填满设备"的最大档，
  每档 threads/capacity/cu 与选中值全部打印；探针失败退回 1 并打 `UNIKERN_ERROR`，不静默；
  四处发射统一走 `uk_conv_launch`（验证档与计时档不可能不一致）；保留 `UK_CONV_OCB` 校准旋钮
- [x] 15 次扫描实测（1/2/4/8 × 三尺寸 + 自动）：自动档在三尺寸上**都选中了该尺寸的实测最优档**
  （1/8/8），256² **6.53×**、128² 4.31×、32² 1.10×；吞吐 19.4→**126.7 GFLOPS**、61.9→276.3；
  **`max_rel` 全档全尺寸恒 0.000000e+00**（每通道累加次序与档无关，这条是本 Phase 的硬事实）
- [x] `[R20]` 臂入库 + 三条腿摘除即红：抄标称 14 / 换掉一次 occupancy 探针 / 反转 kw 循环。
  过程中**又抓到一条不承重断言**（E24，与 E21 同族）：`find("dp.multiProcessorCount")` 被
  `cu=` 打印行满足而没红，改出现次数判据 `== 2` 后才红；`probe/phase20_conv_arm_probe.py` rc=0，
  复原 byte-identical，末轮 ctest 2/2 绿
- [x] 编译期两处坑（`uk_ok` 在本块之后才定义 → 改为自带错误检查；`std::getenv/atoi` 缺 `<cstdlib>`
  与 `std::string` 依赖 → 就地 include + 单字符比较），全部由真机 hipcc 报出、当轮修掉
- 状态：改动 `src/codegen_hip.cpp`、`tests/acceptance.cpp`、`docs/acceptance-mvp.md`、规划三件 +
  新增 3 个探针文件与 `docs/evidence/phase20/`（README + 2 份转录）；**ctest 2/2 绿**；
  **未提交**（提交需明确指示）。本机已无待做的 MVP 机制项；仍开放：Intel 真机项（他机）、
  V1 的 R10.2 F 约束合流、conv 空间/共享内存 tile（可选优化，距同机 gemm 尚差约 18×）、
  D8 差分参考口径（待用户定）、Phase 17 报告的修复（待授权）

## Session 2026-10-28 续7（Phase 21 — conv 空间共享内存 tile + 联合 (tile,OCB) 选档）
- [x] 按计划开工 Phase 21：`uk_conv` 加 halo 的输入 tile 进 `__shared__`（动态共享内存，尺寸只在
  `uk_conv_smem` 一处算），`__syncthreads()` 置于边界 return **之前**；每线程仍按 `c→kh→kw` 升序累加
- [x] 实测 256² kernel **19.06 → 7.153/7.167/7.175 ms**（累计 17.4×、337.5 GFLOPS）、128²
  0.547 → 0.287；32² 与 Phase 20 同分布（10.70–14.18 µs）→ 如实报"分辨不出差别"，不吹不退
- [x] **自我纠正一次设计错误**：第一版只按共享内存装不装得下选 tile，32² 选了 tile=16 → 慢 25%；
  改为 tile 与 OCB **联合**枚举、用设备自报 occupancy 判填满度，三个形状全部选中实测最优；
  教训（"装得下"与"填得满"必须同轴搜索）写进代码注释与 findings
- [x] 24 次真机结果行 `max_rel` 恒 0.000000e+00（含全部 tile/OCB 组合与后备路径）——分块只改访存的直接证据
- [x] `[R20]` 升级为双路径计数 + 两条按字节钉住的 FMA 嵌套 + 屏障/边界 return 次序断言；
  四条腿逐一摘除全红（`probe/phase21_conv_arm_probe.py` rc=0，复原 byte-identical，ctest 2/2 绿）；
  所有断言计数先测后写（E21/E24 规则的落地）
- [x] 无交叉回归：gemm 447 行 / reduce 239 行未变，同批真机 gemm 512³ 与 reduce 1048576 仍
  `verify=PASS max_rel=0 rc=0`；conv 生成程序 372 → 506 行（行数锚点按演进标注）
- [x] 又踩一次自己记过的坑：转录重定向到尚不存在的 `docs/evidence/phase21/` → 命令直接失败；
  以及一次 grep 过滤器把"目录不存在导致的 sqlite ledger open failed"吞成空输出（E22 同族），
  两次都在当轮改为"先 mkdir、再不过滤看原始输出"
- 状态：改动 `src/codegen_hip.cpp`、`tests/acceptance.cpp`、规划三件 + 新增
  `probe/phase21_conv_arm_probe.py` 与 `docs/evidence/phase21/`（README + 2 份转录）；
  **ctest 2/2 绿**；**未提交**（提交需明确指示）。仍开放：conv 到 gemm 级的完整隐式 GEMM 分块
  （6.7× 差距，需另开一相）、D8 口径、Intel 真机项（他机）、Phase 17 修复（待授权）

## Session 2026-10-28 续8（Phase 22 — Harness 复核的三条修复 + 锚点机检门）
- [x] High `acceptance-host-facts-contradict-current-box`：`docs/acceptance-mvp.md` 的
  "Windows 本机事实"段改为**带时点的历史读数**，并补 2026-10-28 实测 `amd=yes / apple=no /
  intel=no`、三机归属（Intel=另一台 Windows 机、Apple=macOS 机）与"当前机器以 `backends` 实测为准"
  的规则；Phase 8 原转录不改写
- [x] Medium `no-agent-facing-entrypoint`：新建根 `AGENTS.md`——三条命令（cmake/ctest/`backends`）、
  "本机"必须点明主机、改内核生成器的三条硬约束（零反斜杠 / `--offload-arch` / 不动累加次序）、
  探针对被跟踪源码的恢复要求、`path:line` 与断言计数的记录纪律、待决 D8 与 R8.5 代理口径
- [x] Medium `doc-code-anchors-drift-unvalidated`：新增 `probe/anchors_check.py` 并注册为 ctest
  第 3 门 `anchors`（无解释器时诚实不注册，而不是报绿）；首跑在 34 条锚点里查出 4 条真失效并改准
  （`tolerance_for` `src/runtime.cpp:182`、`r.verified` 把关的 `src/main.cpp:540`、`INFEASIBLE` `src/main.cpp:269`、`hcs_import` `src/main.cpp:213` +
  字段串实际在 `src/hcs.cpp:95`——那一条是归错文件）；当前 29 条全绿
- [x] 摘除即红：把一处锚点改回失效值 → validator rc=1、`ctest -R anchors` Failed(8)，复原后 rc=0；
  **ctest 由 2/2 变 3/3**
- [x] 工具自身四轮误报（含"上一个锚点为界"这一条）全部当轮修掉并记进 findings（邻域归因 / 标识符形状过滤 / token 长度下限）；
  顺带确立一条写法约定：**`path:line` 是活断言、历史行号写成"第 N 行"**——我自己的那条抽验记录
  在一天内又漂了一次（95→151→182），正好是这条约定的由来
- [x] 未做（等点头）：build/ 内 E5 证据搬迁、phase11–16 旧探针 try/finally 加固
- 状态：改动 `AGENTS.md`(新)、`CMakeLists.txt`、`docs/acceptance-mvp.md`、规划三件 +
  `probe/anchors_check.py`(新)；**ctest 3/3 绿**；**未提交**

## Session 2026-10-28 续9（Phase 23 — conv 2×2 输出微块：负结果、回退、以及回退的字节账）
- [x] 按预登记开工：`uk_conv` 的 LDS 取值复用度从 `OCB` 提到 `PY*PX*OCB`（2×2 输出微块），
  **累加次序一字不动**（每输出仍 `c→kh→kw` 升序）——这是唯一能让 `max_rel` 继续恒 0 的写法
- [x] **保留线跑之前写死**：`kernel_ms ≤ 5.97 ms`（Phase 21 的 7.16 / 1.2），达不到就回退
- [x] 实测（`microtile-sweep.txt`，256²×32/Oc64）tile=16 ocb=8/4/2 = **59.650 / 29.272 / 37.649 ms**、
  tile=8 = 80.167 / 40.786 / 52.251 ms、自动档两跑 59.679 / 59.717 → **全部 `max_rel=0.000000e+00`
  但全部更慢**，最好的一种比 1×1 慢 **4.1×**，且 **ocb 越大越糟**（8 比 4 慢一倍）；
  按规则**回退**，回退后真机复现 7.158620 / 7.173225 ms、`conv_plan ocb=8 tile=16 smem=41472`、`ctest` 3/3；
  **本轮又补采了两次并入库为原始转录** `docs/evidence/phase23/postrevert-verify.txt`
  （`kernel_ms=7.158920 / 7.127580`，`verify=PASS max_rel=0.000000e+00`、`migrations=0 checks=6`、rc=0）
- [x] 归因只作为归因记（活跃累加器 16–32 个 + 块内线程 256→64 掉 occupancy + px 循环的 LDS 步长/bank），
  **没有 profiler 证据把三者拆开就不写成结论**；下一手该先解决 occupancy 与 bank
- [x] **`[R20]` 的按字节钉住在一次真实改动上当场响了**：微块一落地 `ctest` 3/3 → 2/3
  （`count_of("acc[o] += xv * Wt[") == 2` 与嵌套字面量都不认），回退后自动复原。
  这比我自设的摘除控制更有说服力——那条臂不是装饰，是被一次非人为注入的变更验出来的
- [x] 第三次踩"过滤式转录变零结果"（E22 同族）：首版用 `grep -oE "max_rel=… kernel_ms=…"` 抽取，
  两个 token 在结果行里并不相邻 → 转录只剩 rc 行；改为不过滤采原始输出、写入后确认里面真的有数
- [x] 脚本化回退的自身行为：**多处断言在写入之前就 abort**（字符串等值/正则猜错），所以没损坏文件；
  一次替换留下的破损字面量（`<< "` + 换行 + `"`）用 Edit 工具当场修好——**再次印证：
  对被跟踪源码的批量改写，断言要写在写之前、失败即止**
- [x] **纠正我自己写下的"回退＝逐字节复原"**：重新发射的 conv HIP 程序三个形状都是 **508 行**，
  Phase 21 记 506 → 行数与形状无关，2 行是真实增量。微块代码零残留（`micro|2x2|px|PY|subtile` 全文 0 命中），
  增量定位在 env 旋钮分支的 ` source=env` 后缀与 `tile=0 → smem=0` 重置
  （`src/codegen_hip.cpp:463` / `:412`）；前者在 Phase 21 转录里 **0 次**、今天 **1 次**（token 计数取证）。
  → 已把当前发射入库为基线 `docs/evidence/phase23/conv-hip-postrevert.txt`（508 行，
  sha256=8464d86c…42bb28），**"回退"从此必须有可 diff 的对象而不是一句行数**
- [x] 顺手抓到自己记过的"零读数陷阱"的新变体：`conv_tile_skip` 在 Phase 21 转录里 0 次命中
  **并不代表代码里没有它**——告警式打印在转录中的缺席只能证明没开火
- 状态：改动 `src/codegen_hip.cpp`（净效果＝Phase 21 行为 + 2 行诊断）、规划三件（含 Phase 23 表行
  格式修复）、新增 `docs/evidence/phase23/`（README + 2 份转录 + 发射基线）；
  **build 无待编译项、ctest 3/3 绿、anchors 定稿后实测 55 条全绿**（Phase 22 记的 29 是当时存量，
  本 Phase 的记录又新增了一批引用行——这个数是 `probe/anchors_check.py` 报的，不是推算的）；**未提交**（提交需明确指示）。
  仍开放：D8 口径（待用户定）、Intel 真机项（他机）、R10.2 F 约束合流（V1）、
  Phase 17 报告 #4/#5（待授权）；conv 距同机 gemm 尚差 6.7×，再往上＝完整隐式 GEMM 分块（另开一相）

## Session 2026-10-28 续10（Phase 24 — LDS bank 冲突假设的 A/B 否证 + 选档与 smem 的耦合）
- [x] 承接 Phase 23 自己的归因，**把它当成待证假设而不是既定事实**：只动 `uk_sX` 的每像素步长
  （`C → C+1`，padding 槽不参与算术），kernel 索引与 host 侧 `uk_conv_smem` 共用一处规则
  （否则 smem 算小就是越界写共享内存，不是答错）
- [x] **四条判据跑之前写死**（`task_plan.md` 的"Phase 24 预登记"）：① 256² ≤ 5.97 ms 才留；
  ② 128²（C=16，16-way）增益应明显小于 256²（32-way）；③ 32² 走 `tile=0` 路径、必须留在原分布内（负对照）；
  ④ `max_rel` 恒 0 且**先让 `[R20]` 的旧字节钉住变红再决定去留**
- [x] 实测（`docs/evidence/phase24/padded-sweep.txt`）：① **未达**——256² `7.135740 / 7.145340 ms`
  仍在未加 padding 的 7.1276–7.1750 带内；② **未获支持**——128² `0.302160 / 0.295439 ms` 反而比 0.287
  慢 2.9–5.3%；④ **成立**——7 条结果行全 `max_rel=0.000000e+00`。**bank 不是限制因素**，按判据①回退
- [x] ③ **负对照移动了**，且原因是结构性的：`uk_conv_plan` 把 `cand.smem` 交给 occupancy 探针，
  接受条件 `threads >= per_cu*multiProcessorCount` 会随 smem 变化——32² 的 tile=8 smem 3200→3600 后
  谓词由假变真，选档从 `tile=0` 跳到 `tile=8`，慢约 1.5×。**教训升级为规则：改 smem 公式的实验
  必然同时改选档，布局类 A/B 今后必须先用 `UK_CONV_TILE`/`UK_CONV_OCB` 把方案钉住**（旋钮一直在，
  这轮是我没拿它做对照——写进 `AGENTS.md`）
- [x] 机制断言的边界如实记录：本机 **没有 profiler**（`rocprof`/`rocprofv2`/`rocgdb` 不在 PATH，
  TheRock `gfx103X-7.13.0/bin` 内也没有），所以"冲突被掩盖/不存在"只能由 A/B 计时说，不能写成计数器结论
- [x] **回退举证升级**：Phase 23 那次只能说"行为一致"（506→508 事后才定位）；这次因为动手前存了基线，
  回退后 `diff docs/evidence/phase23/conv-hip-postrevert.txt` **空输出** + sha256 相等 + `lines=508`
  ⇒ 真正逐字节回退；真机复现 `7.152313 / 7.152326 ms`、`ctest` 3/3（`postrevert-confirm.txt`）
- [x] `[R20]` 字节钉住第二次在真实改动上开门：padding 版让 `tests/acceptance.cpp:2633` 的 `nest_tile`（Phase 24 当时是 :1682）
  当场变红，我是先让它红、再决定回退，没有反过来改断言迁就实现
- [x] 顺手把 Phase 23 里被本轮否证的那条归因**在每一处写过的地方标注更正**（task_plan 表行、
  findings Phase 23 段、`docs/evidence/phase23/README.md`、`docs/acceptance-mvp.md` v17 段）——
  留一条错误的归因在四处文档里，比留一条没写的更危险
- [x] 读码得到的下一条靶（只登记不动手）：内循环每步 = 1 次 LDS 取值 + **8 次 `Wt` 全局/L2 取** + 8 次 FMA，
  取指令比 FMA 多；本块权重只有 `ocb*Kh*Kw*C = 2304` 字（≈9 KB）⇒ 该动的是权重的放置。
  这也回头解释了微块为什么失败：它增加的是 LDS 取值数，而大头在权重侧
- 状态：改动 `src/codegen_hip.cpp`（**净效果为零**：发射程序与基线 `diff` 空 + sha256 相等，
  生成器里 `uk_conv_cp|CP` 全文 0 命中，无残留）、规划三件、
  `docs/acceptance-mvp.md`(v18 + v17 更正)、`AGENTS.md`（新增钉住规则）、
  新增 `docs/evidence/phase24/`（README + 2 份转录）；**ctest 3/3 绿、anchors 定稿后实测 59 条全绿**；
  **未提交**

## Session 2026-10-28 续11（Phase 25 — 设备共享内存 pre-flight + 钉住 (tile,OCB) 矩阵：occupancy 也判负）
- [x] 按 Phase 24 立下的方法学走：**先测事实，再测假设，全程不改生成器**。独立探针
  `probe/phase25_smem_probe.cpp`（不引 unikern 头）+ 两种模式（有/无 `hipFuncSetAttribute`）各 7 档，全 rc=0
- [x] 设备自报（`smem-preflight.txt`）：`sharedMemPerBlock = sharedMemPerMultiprocessor =
  maxSharedMemoryPerBlockOptin = 65536`、`warpSize=32`、`maxThreadsPerMultiProcessor=2048`；
  `per_cu`：**12800 B → 5 块/CU，≥41472 B → 1 块/CU** ⇒ 现役 `tile=16/C=32`（41472 B）
  **每 CU 只常驻 1 块 = 256 线程 = 线程槽 12.5%**
- [x] **否证我自己写下的一条平台断言**：Phase 21 计划的"48 KB 默认上限，需
  `hipFuncAttributeMaxDynamicSharedMemorySize`"——65536 B 请求在完全不 setattr 的进程里 launch+sync 成功。
  已在 Phase 21 表行就地划掉、登 **E26**、并在 `docs/acceptance-mvp.md` v19 改口径
  （"tile 必须按设备自报值实测选"那半条仍然有效）
- [x] 256² 八组钉住矩阵（`UK_CONV_TILE`/`UK_CONV_OCB`，`conv_plan` 全部带 `source=env`）：
  tile=16 = **7.129367** / 10.514896 / 18.079030 / 31.571097 ms（ocb=8/4/2/1），
  tile=8 = 9.638396 / 13.543800 / 20.141341 / 31.416699 ms ⇒ **常驻块数 1/CU → 5/CU 换来慢 11–35%**；
  预登记保留线 ≤5.97 ms 无一达到，9 条结果行全部 `max_rel=0.000000e+00`。**occupancy 这条腿判负**
- [x] 对照组（不钉旋钮）：选档器选中 `tile=16/ocb=8`，实测 7.150933 ms = 八组最优
  ⇒ Phase 21 的联合搜索规则在更宽的读数面上第二次得到独立确认
- [x] **三条候选解释至此全部关闭**（X 复用度 4.1×慢 / bank 冲突无效应 / occupancy 反效果），
  唯一活着的强信号是 OCB 单轴 4.43×；下一步的靶改指**权重的放置**（每步 1 LDS + 8 权重取 + 8 FMA），
  tile/ocb 两个旋钮已榨干——这条写进 findings/验收文档，不再写进"归因"
- [x] 顺手抓到一处**关于自己代码的过满注释**（E26 第二条）：发射程序有两处 `uk_conv_plan`
  （verify 用内嵌小实例 C=8 → `smem=10368`；计时用真实 C=32 → `smem=41472`），
  所以"任何一段都不会用与校验不同的方案发射"只是发射函数单一；真正的保证是"所有方案逐位相同"。
  注释重写会动发射字节 ⇒ 与 `sha256` 基线重录绑成下一相的一个动作，本轮不动
- 状态：**本轮生成器零改动**（发射程序仍与 `conv-hip-postrevert.txt` 逐字节相等，`ctest` 3/3 绿）；
  新增 `probe/phase25_smem_probe.cpp`、`docs/evidence/phase25/`（README + 2 份转录）；
  规划三件 + `docs/acceptance-mvp.md`(v19) 同步；**anchors 定稿后实测 59 条全绿（21 份文档）**，
  发射程序 `diff` 仍与基线逐字节相等（`lines=508`）；**未提交**

## Session 2026-10-28 续12（Phase 26 — 验证覆盖缺口：verify 校验的必须是被计时的实例化）
- [x] 顺着 Phase 25 记下的一条代码事实**把它做成测量**：把发射出来的 256² conv 程序单独跑
  （`hipcc --offload-arch=gfx1032` 后直接执行，绕开 runner 的输出裁剪），stderr 26 行里
  **两条 `conv_plan` 打的方案不一样**——verify 段 `tile=0 ocb=1 smem=0`（内嵌 32×32×8 实例，C=8），
  计时段 `tile=16 ocb=8 smem=41472`（声明形状 C=32）⇒ 默认流程里 `max_rel=0` 校验的是
  `uk_conv_g<1>`，计时的是 `uk_conv<8>`
- [x] 诚实登记代价：此前 Phase 20/21/23/24/25 关于分块内核逐位正确的读数都是真的，
  但**全部来自我钉住旋钮的扫描**；默认（不钉）流程从未校验过分块路径。写成的不是"以前错了"，
  而是"**以前证据的范围被写得比实际范围大**"
- [x] 修复取最小形状：`H/W/C/Kh/Kw/Oc` 与 `uk_plan` 提到 `main()` 开头（verify 之前），
  全运行只推导一次，verify 用同一个方案发射自己的小实例（`rem` 保护 ocb，smem 按真实 C 算属超配、安全）；
  发射程序 508 → **517 行**，`diff` 恰 **+21/−12** 且全在这一处设计上（无顺手编辑），gemm 447 / reduce 239 未变
- [x] 真机五用例全 `verify=PASS max_rel=0.000000e+00`：256² `kernel_ms=7.160367`（带内）、
  128² 0.284069、32² 0.012980、gemm 512³ 0.123837、reduce 1048576 0.164954；
  单独运行同一二进制读到 7.024740 ms（比经 runner 低约 1.9%）⇒ **带内比较必须同上下文**，这条也记下
- [x] `[R20]` 新腿按计数钉（四条：`uk_conv_plan(`==2、`uk_conv_launch(uk_plan,`==4、verify 那条==1、
  `uk_conv_launch(plan,` 必须不存在）。**为什么旧的"单一发射点"腿没抓住它**：那条断言管的是发射函数唯一，
  不是方案唯一——缺口正好活在这两者的缝隙里
- [x] `probe/phase26_plan_arm_probe.py` 三条摘除控制全部当场红（`[R6.6]` `tests/acceptance.cpp:1694`/`:1697`，
  rc=3221226505），复原 byte-identical，末轮 `ctest` **3/3 绿**，probe rc=0
- [x] 第四次踩 E22：reduce 用 `--dims 1048576` 单参数调用 → CLI 打 usage → 被 grep 过滤后转录静默少一格；
  补录正确形式 `--dims 1048576 0 0`，并把失误留在转录里。顺带一条同类陷阱：
  **管道尾端的 `$?` 不是被测 CLI 的退出码**，过滤式采证时别把它当结果
- [x] 基线换代：新基线 `docs/evidence/phase26/conv-hip-single-plan.txt`（517 行，
  sha256=161d8e19…27f3f）取代 Phase 23 的 508 行文件；旧文件按"转录不改写"保留并标历史时点。
  **Phase 23 那次"说不清的 2 行"就是因为当时没有基线——这条规则的第二次收益**
- 状态：改动 `src/codegen_hip.cpp`（发射结构）、`tests/acceptance.cpp`（四条计数腿）、
  `AGENTS.md`（新增"verify 必须发射被计时的方案"一条）、规划三件 + `docs/acceptance-mvp.md`(v20)；
  新增 `probe/phase26_plan_arm_probe.py` 与 `docs/evidence/phase26/`（README + 3 份文件）；
  **ctest 3/3 绿、anchors 定稿后实测 64 条全绿（22 份文档）**；**未提交**

## Session 2026-10-28 续13（Phase 27 — OCB=16 外推：判负、符号随 C 变号、按字节回退）
- [x] 承接 Phase 25 唯一活着的量纲（OCB 幂律 β=0.803/0.781/0.561 递减），**先写死两个分支**再动手：
  16 档 ≤5.97 ms ⇒ "取数指令数受限"继续成立；16 档反而更慢 ⇒ 天花板是每线程寄存器/ILP。
  外推区间也写死（乐观 4.85 / 悲观 6.02 ms），这样"没达到"是可判定的而不是"再试一次"
- [x] 实现是一整档的机械扩展（`uk_conv<16>`/`uk_conv_g<16>` 两处实例化 + occupancy 两条链 +
  两个 switch + 候选 `step < 5` + `UK_CONV_OCB` 合法值 + 选档注释），未碰累加次序
- [x] 实测（全 `max_rel=0.000000e+00`，方案钉住、上下文固定为 runner）：
  256²（C=32）ocb=16 = **7.858002 / 7.854852 ms** 对 ocb=8 = 7.160519 ⇒ **慢 9.7%**（分支②命中）；
  **128²（C=16）ocb=16 = 0.266870 对 0.288485 ⇒ 快 7.5%** ⇒ 本轮真正的发现是
  **最优 OCB 是 (C, Oc) 的函数**，选档器里"越复用越好"的单调偏好第一次出现反例
- [x] 自动档实测：候选集含 16 时 256² 会选中 16（7.833645，比该选的 8 慢 9.4%）
  ⇒ **保留这一档＝把错误偏好带进 shipping 路径**，这是独立于保留线的第二条否决理由
- [x] **不拿两个点拟合按-C 规则**（每个 C 各只有一个形状；本项目的选择规则要求由设备自报量推导，
  见 `findings.md` Phase 20 段第 604 行）⇒ 整档回退，而不是"给 128² 开小灶"
- [x] 回退举证（第三次走这条口径）：发射程序与 `conv-hip-single-plan.txt` **`diff` 空**、517 行；
  生成器 `uk_conv<16>|uk_conv_g<16>|step < 5` 全文 0 命中；`ctest` **3/3**；
  真机复现 256² **7.156521**、128² **0.284850**，选档器回到 `ocb=8 tile=16`
- [x] 更新记录的纪律也照做：预登记段只**追加"结果"两行**，跑之前写死的判据一字未改
- 状态：改动 `src/codegen_hip.cpp`（**净效果为零**，diff 空）、规划三件、
  新增 `docs/evidence/phase27/`（README + 2 份转录）；`docs/acceptance-mvp.md` 加 v21；
  **ctest 3/3 绿**；**未提交**。
  conv 这条线的下一步已改成"可判定的测量"：`C ∈ {8,16,32,64} × ocb ∈ {8,16}` × ≥3 跑找翻转点

## Session 2026-10-28 续14（Phase 28 曲线否证"按 C 选档" + Phase 29 修掉校准旋钮绕过设备的真缺陷）
- [x] Phase 28 按预登记跑成两条曲线（脚本只在发射期间给生成器打 ocb=16 的补丁，四份程序落盘即在
  `finally` 里 byte-identical 还原并重建 CLI ⇒ **计时阶段不处于源码被改动态**）
- [x] tile=8 完整曲线：16 相对 8 = **C=8 +23.4% / C=16 +22.0% / C=32 −9.0% / C=64 +11.5%**，
  极差 ≤2.21%、42 次有结果运行全 `max_rel=0.000000e+00` ⇒ 符号 **`+ + − +` 非单调**，
  **门槛 2 不通过 ⇒ 不加档、不改选档器**。Phase 27 我亲手提的"最优 OCB 是 C 的函数"被自己的数据否证；
  能写的只剩"(C, Oc, tile, 网格) 联合作用，机制在本机未定"。**这一相的产出是"少了一条错规则"**
- [x] 工具自己的失败标签修了一次：第一版把"缺格"和"极差超标"合并成同一个 FAIL，会让人把 C=64 的
  缺格读成噪声 ⇒ 脚本改成两条独立门槛，并如实保留第一版转录
- [x] C=64 缺格复现出**真缺陷**：`UK_CONV_TILE=16` → `conv_plan … smem=82944 source=env` →
  `UNIKERN_ERROR what=launch verify msg=invalid argument`、rc=4。根因：env 分支在
  `hipGetDeviceProperties` 之前就 return，钉住的方案绕过自动路径的 `smem > sharedMemPerBlock` 检查
- [x] Phase 29 修法沿用**同一条判据**（不另立标准）：越限就响亮拒绝该 pin（报请求字节、设备上限、
  改走全局路径且保留 OCB pin），且 **ERROR 行先于 `conv_plan` 行**；props 查询失败也保守退回全局路径
- [x] 三条对照（`knob-refusal.txt`）：装不下→拒绝并仍能跑；**装得下→不误报**（这是主要防误报证据）；
  原形状→行为不变（7.050036 ms，带内）。全 rc=0、`max_rel=0.000000e+00`
- [x] `[R20]` 两条新腿各管一件事，摘除控制**分别触发**：改名 token→计数腿红；提前打印 `source=env`→
  **只有顺序腿红**（`[R6.6]` `tests/acceptance.cpp:1712`）。`probe/phase29_knob_arm_probe.py` rc=0、
  复原 byte-identical、末轮 `ctest` **3/3**
- [x] 基线第 4 次换代：`conv-hip-knob-check.txt` 542 行、sha256=599a4e71…e3f18，对 Phase 26 基线
  `diff` +25/−0；**过程中纠了自己的错**——第一次存的是从 C=64 KML 发射的产物（`dims=` 头与
  `const int C` 两行不同，diff 出现 −2），基线必须与既有基线同源才可比 ⇒ 按同一 C=32 KML 重发入库。
  gemm 447 / reduce 239 未变
- 状态：改动 `src/codegen_hip.cpp`（Phase 29 的检查，**入库**）、`tests/acceptance.cpp`（两条腿）、
  规划三件 + `docs/acceptance-mvp.md`(v22)；新增 `probe/phase28_ocb_c_sweep.py`、
  `probe/phase29_knob_arm_probe.py`、`docs/evidence/phase28/`、`docs/evidence/phase29/`；
  **ctest 3/3 绿、anchors 定稿后实测 70 条全绿（25 份文档）**；**未提交**

## Session 2026-10-28 续15（Phase 30 — reduce 首次归因：串行 pass-2 是主项；宽度受对拍精确性约束）
- [x] 挑的是全天**唯一没被碰过的执行面**（conv 调了 9 相，reduce 一次没有）；先读码定现状：
  `uk_reduce_part` 用 nP 条线程跨步折叠、`uk_reduce_sum` 用**一条线程**串行折叠 nP 个部分和， `nP = min(L, 4096)` 在**生成期**定死（当时 codegen_hip 的 829 行；D9 之后由设备自报量推导，今天发射点是 `uk_reduce_width_plan`（`src/codegen_hip.cpp:841`）），运行期不看设备自报量
- [x] **四条判据跑之前写死**（含我先算好的期望值：全域 2⁻⁶ ⇒ 精确和 = n/64）；
  探针 `probe/uk_reduce_width_probe.cpp` 的折叠次序与发射内核逐字一致，所以读数描述的是出货代码
- [x] **判据 1 我预测错方向并如实记下**：`sum_share` 实测 **68.2%**（我写 <30%）
  ⇒ 主项是串行 pass-2；判据 2 成立（pass-1 从 89.5 → 315.6 GB/s，比值 3.53 ⇒ 确实欠并行）；判据 3 成立但次线性
- [x] **√n 律在第四个点被否证**：预测 2²⁶ → 32768，实测最优 **16384**（32768 慢 38%）；
  机制是 pass-1 时间随 nP **非单调**（1.4317 → 1.2665 → 1.6110 ms）⇒ 任何"√n 外推"的上限规则都会在 2²⁶ 选错
- [x] **本轮最值钱的发现是一条被漏掉的约束**：n=2²⁶ 时只有**非 2 的幂**的宽度出现偏差
  （14336 → rel=−1.602e−04、28672 → −1.373e−04；1024…32768 全档 rel=0）。
  机制是算术（`n/nP` 非 2 的幂 ⇒ 折叠到 2²⁰ 量级需要 >24 bit 尾数）。
  **含义比数字更重要**：启动宽度会决定 `max_rel` 是否为 0，而偏差量仍在 2⁻⁵ 容差内 ⇒ **门禁不会拦，只会静默换口径**
- [x] 因此"把上限写成设备自报量"的两条路**实测出局**（28672 慢 8.7% 且破坏精确性；14336 打平但同样破坏精确性），
  只剩本机拟合常数 16384 能把四个点说对（1.67× / 2.49×，小 n 不退步 ≤1.05%）
  ⇒ **不动生成器**，升为 **D9** 等用户定；并行 pass-2 属改折叠次序（AGENTS 第 3 条）→ 与 D8 同类，也不擅动
- [x] 自我修正一处措辞：step 2 的 README 里我把"occupancy 推导会越过实测最优"写成**推测**，
  step 3 补测后改成**证据**（并把"14336/28672 本轮没测"那句删掉）——推测不许留在已被测量的文档里
- 状态：**生成器与测试零改动**；新增 `probe/uk_reduce_width_probe.cpp`、`docs/evidence/phase30/`
  （README + 3 份转录）；规划三件 + `docs/acceptance-mvp.md`(v23) + `AGENTS.md`（新增宽度/精确性耦合一条）；
  **未提交**

## Session 2026-10-28 续16（Phase 31 — `max_rel=0` 的证据范围被量化，并进了测试套件）
- [x] 承接 Phase 30 的精确性发现，先**读码定结构**：差分验证段是**固定 2²⁰ 实例**
  （发射点是 `Differential`（`src/codegen_hip.cpp:883`）），而 `UK_REDUCE_WIDTH` **当时**来自声明的 L
  （`:736`/`:820` 是 Phase 30 的位置）；D9 之后宽度改由设备自报量在运行时推导，今天的发射点是
  `uk_reduce_width_plan`（`src/codegen_hip.cpp:841`）与 `UK_REDUCE_WIDTH`（`src/codegen_hip.cpp:880`）
  ⇒ `UNIKERN_RESULT` 的 `max_rel` 描述的从来不是计时规模——这是一个**证据范围**问题，不是新故障
- [x] 预登记四条预期后再测：18 格网格（`fold-exactness-grid.txt`，仪器每行先报 `result/rel` 再报时间，
  期望值 n/64 提前算好）。现役 4096 在 2²⁴/2²⁵/2²⁶ 全 `rel=0`；`n=2²⁶+4096` → `−4.577e−05`；
  14336 在 2×10⁷/2²⁵/2²⁶ → `+1.154e−04 / −3.052e−05 / −1.602e−04`；
  **所有非零值距 2⁻⁵ 门限 ≥195 倍 ⇒ 门禁在结构上拦不住这一类**
- [x] **我自己的判据被数据纠正**：预登记写的触发条件是"`n` 不被 `nP` 整除"，
  但 `67112960/4096 = 16385` 是**整除**的却 `rel≠0` ⇒ 真条件是**每线程 chunk 是否为 2 的幂**。
  这条只在计时规模上暴露（2²⁰ 实例里两种情况都是 0）——**"验证实例选小了"会连带让判据本身也测不准**
- [x] **机制不需要真机**：CPU 侧同次序折叠 fp32 复现到最后一位
  （`4.57736e−05` vs 真机 `−4.577e−05`；`1.60222e−04` vs `−1.602e−04`；幂 chunk 两边都是 0）
  ⇒ 是算术事实（2²⁰ 量级处 fp32 量子 2⁻⁵ 容不下 2⁻⁶ 步长），不是驱动/硬件的怪异
- [x] 新增**纯 CPU 臂 `[R6.5-blind]`**：①幂 chunk 必须逐位等于精确和；②非幂 chunk 必须 `rel>0` 且 `<1e−4`；
  ③两者都必须 < `Tolerance{}.fp32_rel / 100`——第三条把"盲区是结构性的、不是调阈值能消的"钉住。
  `probe/phase31_arm_probe.py` 三条摘除控制**各自独立变红**（`[R15]` `tests/acceptance.cpp:1454`/`:1459`/`:1467`），
  复原 byte-identical，末轮 `ctest` **3/3** 绿，probe rc=0
- [x] 把它登记成**缺口 G10**（"验收不掩盖项"表）而不是只写一段补遗：
  `max_rel=0` 是"两趟结构正确"的证据，不是"该工作负载求和精确"的证据；v24 补遗给方法与量级
- [x] 接口两个未决项而不越权：D8 多一条同向事实；**D9 多一条硬约束**——任何候选宽度都必须让
  chunk 是 2 的幂，否则连"验证实例恒精确"这层保护都会退化成数据规模的偶然
- [x] 三样都没动：验证实例规模、容差、宽度公式（任一都会改变已入库 `max_rel=0` 证据的含义）
- 状态：改动 `tests/acceptance.cpp`（新臂）、规划三件 + `docs/acceptance-mvp.md`（G10 + v24）；
  新增 `probe/phase31_arm_probe.py`、`docs/evidence/phase31/`（README + 2 份转录）；
  **ctest 3/3 绿**；**未提交**

## Session 2026-10-28 续17（Phase 32 — `anchors` 门扩展到表格列数，首跑抓出自己撕开的三条）
- [x] 动机是自记的教训（**E27**，同一天第三次）：用长单行表格行的**前缀**当 Edit 锚点会把那一行撕开；
  `path:line` 有机器核，表格结构没有。本项目把工作记忆当交付物读，**渲染错列的表就是错的索引**
- [x] `probe/anchors_check.py` 新增 `table_shape_problems()`：以"表头 + 分隔行"记列数基线，逐行比到非 `|` 行为止，
  并跳过 ``` 围栏内部（转录里有管道字符）。**实现坑一条**：计数必须把转义竖线当内容——
  否则 E3 那行（键内含转义竖线）被误判成 5 列，**会误报的门进不了 ctest**
- [x] 首跑抓出三条**真**缺陷并全修：①Phase 27 行内代码里的裸竖线（4 格 vs 表头 3）；
  ②D8 行绝对值写法两侧裸竖线（5 格 vs 3）；③G5 缺口行写在三个物理行上（渲染 1 格）→ 转义 + 并线。
  与 Phase 22"首跑抓出 4 条失效锚点"是同一价值形态
- [x] 门自己能红已验：给 Phase 31 行故意补一格 → `rc=1` 且点名 该行第 172 行；
  复原 byte-identical → `rc=0`。**进 ctest 的检查一律双向验证**（从不红的与总红的一样无效）
- [x] 修脚本时又走了一遍"先断言后写"：第二处锚点我按记忆断言 `count==2`，实测 1 →
  **脚本在任何写入之前 abort，文件未受损**；改正断言后才落盘。这正是 E 系列反复记过的做法
- [x] 顺带把 Phase 31 记录里的两条 `path:line` 引用从 scratch 产物（`uk_run/...`，会被清掉）
  改指生成器行号——**anchors 门当场指出我新写的锚点归属错**（`UK_REDUCE_WIDTH` 在 808 而非 724/781），
  这是扩门之外本相该记的第二次自捕
- [x] **写这一相的记录时我又犯了正在记的那个错**：为了描述"裸竖线把行撕开"，我在 Phase 32 表行里
  把坏味道原样引了一遍（`uk_conv<16>` 与 `step < 5` 之间的竖线、`both−exact` 两侧的绝对值竖线），
  于是那一行自己变成 6 格 —— **门在同一轮就把它报了**（该行第 180 行）。
  这比任何说服都好用：**描述缺陷的写法本身受同一条规则约束**，转义后才落盘
- 状态：改动 `probe/anchors_check.py`（扩检查）、`task_plan.md`（三处修复 + Phase 32 行 + E27）、
  `findings.md`、`progress.md`、`docs/acceptance-mvp.md`(v25)、`AGENTS.md`（记录约定加一条）；
  **ctest 3/3 绿、anchors 定稿实测 94 条 path:line + 表格列数全绿（27 份文档）**；**未提交**。
  未决项不变：**D8、D9、harness #4/#5 授权、提交指令**

## Session 2026-10-28 续18（Phase 33 — 只测 2 的幂宽度：现役 4096 也会偏，D9 换成纯速度取舍）
- [x] 问的是自己留下的说法：Phase 30/31 说"2 的幂宽度 ⇒ 折叠精确"，机制却说精确性主要由 `n` 决定
  ⇒ **只测 2 的幂宽度**（8 个 n × {2048,4096,8192,16384}），先写死三分支判读再跑
- [x] **落在分支 2（否证）**：n=5×10⁷ 时四档全部 `rel≠0`，**包括出货在用的 4096（+5.44e−05）**；
  3×10⁷ 时反过来只有 16384 偏（+2.41e−04）、2×10⁷ 只有 2048/8192 偏
  ⇒ 精确性是 `(n, 宽度)` 的二维函数，**没有单变量法则**；本轮不外推、不动代码
- [x] 把过宽的说法在**四处**按"原文不改、就地标演进"收窄：`AGENTS.md` 第 7 条、
  `findings.md` Phase 31 段、`docs/acceptance-mvp.md` 的 G10 与 v26、`task_plan.md` 的 D9 行——
  这是我自己定的纪律（"发现写错的断言，改掉它被用到的每一个地方，而不只是那一行"）第四次执行
- [x] **G10 表述收紧（初稿过重，Phase 34 改口）**：不是"已出货的宽度在某些规模上就不精确，
  且固定 2²⁰ 的验证实例永远看不见**"；`[R6.5-blind]` 三条断言经核对仍然成立（它们只声称
  "存在精确的、存在不精确的、两者都远低于门限"），**没有为了迁就新数据去改断言**
- [x] 速度事实同样干净且可推导：`16384` 在八个 n 上**全部最快**（对现役 1.65×~2.50×），
  且 `= 2^floor(log2(maxThreadsPerMultiProcessor × cu)) = 2^floor(log2(28672))` ——
  **D9 的两条反对理由（拟合常数 / 破坏精确性）同时消失**，只剩纯速度取舍交给用户
- [x] 又踩一次自己记过的坑（E22 族第 6 次）：网格首轮没建输出目录 → 重定向失败、**零读数**；
  补 `mkdir` 后重跑并当场核对 `rows=32 nonzero_rel=14`。**采证后必须确认文件里真的有数**
- [x] `anchors` 门在本相开跑前又扩了一次：加"必需章节标题存在"检查（我一天内把 `## Decisions` 弄丢五次），
  首跑就把我自己的 `## Errors Encountered（防重复）` 误报为丢失 → 改成前缀匹配（**会误报的检查不进 ctest**），
  并用"删掉 `## Constraints` → rc=1 点名 → 复原 byte-identical → rc=0"证明它能红
- [x] 新门**当轮就抓到我自己造成的两处结构损坏**并都修回：①`## Decisions` 标题被我第五次拿整行当锚点
  替换时吃掉（→ 加"必需章节标题"检查，首跑还把我的前缀匹配写法误报成 `## Errors Encountered` 丢失，
  改成前缀匹配后才对）；②重写 D9 决策行时把一行写成 5 个物理行（`table row has 2 cells, header says 3`），
  按行合并复原。**门在写完的同一轮里把记录本身核对一遍，这正是它进 ctest 的理由**
- 状态：**生成器、容差、验证实例规模、折叠次序一字未动**；改动 `probe/anchors_check.py`（必需标题检查 +
  前缀匹配修正）、`task_plan.md`（Phase 33 行 + 预登记结果 + D9 重写 + 两处结构复原）、
  `findings.md`、`AGENTS.md`、`docs/acceptance-mvp.md`（G10 + v26）、`progress.md`；
  新增 `docs/evidence/phase33/`（README + 32 格转录）；
  **ctest 3/3 绿、anchors 定稿实测 96 条 path:line + 表格列数 + 必需标题全绿（28 份文档）**；**未提交**。
  未决项不变：**D8、D9（已改写为纯速度取舍）、harness #4/#5 授权、提交指令**

## Session 2026-10-28 续19（Phase 34 — 折叠误差可预测模型 41 格全中；收回上一相写宽的 G10 说法）
- [x] 先把模型立起来再写断言：pass-1 每线程个数 `k_t = floor((n-1-t)/nP)+1`，部分和 `k_t·fill` 在
  `k_t < 2²⁴` 时精确 ⇒ 误差全在 pass-2 的串行 fp32 累加 ⇒ **CPU 上可逐格算**
- [x] **与 41 格真机读数对账 `mismatch=0`**（Phase 28/30/31/33 的全部 `rel=`；零/非零一致、量级 5% 内）；
  顺带得到一条副产品：真机 pass-2 **没有**被编译器重结合（否则不可能逐格对上）
- [x] 入库 `[R6.6-model]`（纯 CPU，无设备）：19 格实测三元组 + "2²⁰ 验证实例在任何宽度下精确" +
  "模型最坏偏离 `2.40667e−04` 远低于门限"；`probe/phase34_model_arm_probe.py` 三条摘除控制各自能红
  （`[R6.2/R6.4]` `tests/acceptance.cpp:1576`），复原 byte-identical，`ctest` 3/3
- [x] **抓到一条"不会失败的控制"**：腿 2 第一版把填充换成 `0.5f` 居然是绿的，而且绿得有理——
  2 的幂整体缩放不改变 fp32 相对舍入，那条控制等于没测；换成非 2 的幂 `0.1f` 才红。
  已把这条写进记录与 README：**写控制时问"如果实现错了，它会红吗"**
- [x] **收回上一相自己写宽的话**（六处就地改口 + v27）：G10 不是"账本会安静收下带 fp32 折叠误差的结果"；
  计时段的 `Y` 拷回后无人使用（`hipMemcpyAsync`（`src/codegen_hip.cpp:946`）），账本记 ε 与时间 ⇒ 准确说法是
  "**该规模的结果不在验证范围内**"（覆盖缺口，不是证据污染）
- [x] 自己又撕了一次表格行（Current Phase 段落误插进 D9 行，行裂成 8 个物理行）——
  **本相刚扩的 `anchors` 门当场报红**，按行合并复原（先校验 cells 再写盘）。E27 那一类第 7 次，
  差别是这次从犯到发现只隔一条命令，这就是把纪律做成仪器的意义
- 状态：**生成器、容差、验证实例规模、折叠次序一字未动**；改动 `tests/acceptance.cpp`（新臂）、
  `task_plan.md`（Phase 34 行 + 预登记结果 + Current Phase + D9 复原）、`findings.md`、`AGENTS.md`、
  `progress.md`、`docs/acceptance-mvp.md`(v27 + v26/G10 改口)、`docs/evidence/phase33/README.md`（改口）；
  新增 `probe/phase34_model_arm_probe.py` 与 `docs/evidence/phase34/`（README + 控制转录）；
  **ctest 3/3、anchors 定稿实测 106 条 path:line + 表格列数 + 必需标题全绿（29 份文档）**；**未提交**

## Session 2026-10-28 续20（Phase 35 — out-of-sample 把我的折叠模型判死，臂改为忠实连加）

- 预登记（跑前写死）10 格 + 判定规则；跑完**只另起结果段，一条预登记未改**。
- 真机：`docs/evidence/phase35/out-of-sample.txt`（22 格）、`-cells-2.txt`（补 4 格）、
  `-cells-3.txt`（`nP=1` 判据替身 3 格）。`n=10⁸ nP=4` 两次都是
  `msg=unspecified launch failure`（设备被杀），已如实记为未采到，不猜值。
- 判定：**设备站在连加一边**（naive 5/22 判错、含一格符号反；faithful 0/29）。
  预登记第 10 格的强分支成立 ⇒ `k > 2²⁴` 时 reduce **判 FAIL**（`−5.000e−01`，
  机制=部分和冻结在 `2²⁴·fill`），已按要求同时写进生成器注释（`FREEZES` 那句，`src/codegen_hip.cpp:762`）
  与臂的前置条件（`[R6.6]` `tests/acceptance.cpp:1777` 的 `naive != pred` 分桶 + 容量算术）。
- `[R6.6-model]` 重写：41 格（in-sample 19 + out-of-sample 22），`safe=34 / split=7`，
  naive 判错 7 格并作为**活控制**入库（`assert(naive_bad >= 5)`）；冻结短路用前缀扫描，
  整个 accept 二进制 0.53 s。地板 256 由发射产物自证（`L=100` 仍发 `UK_REDUCE_WIDTH = 256`）。
- 控制两条各自跑过并 byte-identical 复原：边界 `2²⁴→2²⁰` 红；生成器 `256→4` 红在产物断言。
- 自己犯的两条已入账：E28（Windows `long` 32 位，`8589934592L/4` 绕负让**为真**的断言报红）、
  E29（探针丢掉设备后 `continue` 空转产数、且不印原因）。
- 探针 `uk_reduce_width_probe.cpp` 加 `argv[3]=fill` 与失败原因；生成器**发射字节未动**
  （542/447/239），容差/验证实例/折叠次序未动；**未提交**。

## Session 2026-10-28 续21（Phase 36 — gemm/conv 精确性断言拿到成立条件，读码读出 G11）

- 预登记与结果同段写（本轮没有"跑前预测 vs 跑后"的分歧点：**证据是整数格算术，不是真机读数**），
  唯一真机动作是可行性取证：`solve --op gemm --dims 2048 64 2000000`（16 GiB vram 档）
  → `SOLVED … EMIT amd:hip lines=447`，入库 `docs/evidence/phase36/g11-int-index.txt`。
- 结论：`17/13` 互素 + 双零均值 ⇒ 一周期内积和逐位为零 ⇒ `max |partial| = 182·2^-8 = 0.710938`
  与 `K` 无关 ⇒ 发射注释那句 exact **为真**；对照 `13/13`（非互素）时 `K=4×10⁶` 偏离精确值 `-5051`
  ⇒ 承重的是互素而非"2 的幂"。无效对照一条也已入账（给 `initA` 加直流偏置不会破坏精确性）。
- E32：预测脚本里 `np.cumsum(float32)` 被当成 fp64 参考 = 恒等式；改整数格 + `Fraction` 重算。
- 新增缺口 **G11**（`docs/acceptance-mvp.md` 表行）：内核索引算术 32 位 + 可行负载可越界，
  修复路线两条留给用户 = **D10**。生成器/发射**一字未动**（447/542/239 与基线一致）。
- `anchors` 门这次立刻抓到我撕开的 G11 行（"2 cells, header says 4"）并修好；**未提交**（48 个路径）。

## Session 2026-10-28 续22（Phase 37 — 自己否证自己写的缺口 G11，找到真正的 CLI 维度饱和缺陷）

- 动机：D10（要不要为 32 位索引升类型）必须是**可判定的**，于是去测暴露面而不是重复上一相的读码。
- 扫描（`probe/phase37_index_exposure.py` → `docs/evidence/phase37/index-exposure.txt`）：
  7 档 K × 3 档 M，stock 8 GiB 与 16 GiB 两档 vram。结果 `shard_rows = shard_bytes/(K*4)`，
  `SR*K ∈ [6.4e6, 1.68e7]` 恒 ≤ `16777216` ⇒ **流式不可能溢出**；再 grep 索引表达式：
  `A[(size_t)(bm + j) * K + k0 + i]` ⇒ **乘法前已转 64 位**。⇒ **G11 撤销**（E33：从签名推断类型、
  把 banner 的一次实例化值当常量），D10 的"升索引类型"选项作废。
- 真正的缺陷（G11 改写为这条）：`CLI `--dims` 分支（原 `main.cpp:86`）` `std::atoi` 解析 `--dims` ⇒
  `--dims 3000000000 0 0` rc=0 + `LEDGER HIT` + 发射 `const int L = 2147483647;`：
  超范围维度**静默饱和**，并与 `2147483647` 请求**共用同一个账本键**。修复 = 解析层范围校验，
  不碰发射；因改变拒绝行为待用户定。
- 本轮**未动代码**：生成器/发射字节/容差/验证实例一字未改（447/542/239 不变）。
- 记录面：G11 行就地划掉并改写、Phase 36 行/结果段/Current Phase 就地标注否证（原文不删）、
  findings 新增 Phase 37 段、progress 本条、`docs/evidence/phase36/README.md` 加演进注。

## Session 2026-10-28 续23（Phase 38 — 越界维度在两个入口都静默饱和，并复用账本键）

- 提问方式决定修复面：把 Phase 37 的缺陷问成"一个入口还是两个入口"。
- 取证（`docs/evidence/phase38.txt`）：CLI `--dims 3000000000 0 0` 与 KML 文件
  `"dims":[3000000000]` **都** rc=0、**都**发射 `const int L = 2147483647;`；
  新鲜 ledger 双向跑证明它们与 `2147483647` **共用同一键**（`LEDGER HIT c_cached_ms=570.425`）。
- 定位：文件侧是 `src/kml_front.cpp:108` 的 `parse_int` → `int v`，而 `:113` 只有下界与元数校验，
  **没有上界这一类** ⇒ R1.2 缺一条谓词（不是实现走样）。
- D10 改写为"两入口共用/各加上界校验 + 显式可寻址上界"；**本轮未动代码**
  （生成器、前端、发射一字未改；447/542/239 不变）。
- 自己的一条新错误入账 E34：管道后的 `$?` 不是 CLI 的 rc（E20 家族复发）。
- 记录面：Phase 38 表行 + Current Phase + findings + 本条 + G11 行扩面（`docs/acceptance-mvp.md`）。

## Session 2026-10-28 续24（Phase 39 — 缺谓词按一类扫描：新增 G12，并被自己的缓存碰撞坑了一次）

- 方法：把"一个入口还是两个入口"式的**类别问题**用在输入面上（tile / dtype / HCS 数字 / 未知键 / CLI 维度）。
- 自我否证：第一遍全部 `LEDGER HIT` ⇒ 读的是上次请求的产物；改每例独立 `--ledger` 重跑（E35）。
  清洗后的取证在 `docs/evidence/phase39/input-boundaries-deconfounded.txt`。
- 结果：D6 的钳制标注**兑现**（发射含 `clamped for register budget` 行），但声明 tile 不进入求解；
  **新缺口 G12**：dtype 进成本与规范键、不进产物（fp32 `0.110506` / fp16 `0.0613536` /
  int8 `0.0367776`，产物同 448 行 float）；HCS 数字面谓词**齐全**（一律 rc=2 明确拒绝）；
  `--dims abc` 被当作"无覆盖"⇒ 静默改解演示负载（`L = 4096`）rc=0，比 Phase 38 的饱和更严重。
- 未动代码；新增决策 **D11**（dtype 三选一：真按 dtype 生成 / 前端只收 fp32 并 loud 拒绝 / 现状+标注）。
- 记录面：Phase 39 表行 + E35 + Current Phase + findings + 本条 + `docs/acceptance-mvp.md` G12 行。

## Session 2026-10-28 续25（Phase 40 — D10 落地：两入口显式上界校验 + `[R1.4-dimrange]`）

- 用户指令"完成本机能做的一切" ⇒ 只挑**不需要口径决策**的一条：D10（把静默替换变成拒绝）。
  D8 / D9 / D11 改变已入库证据的含义或属产品取舍，**没有动**。
- 改动：`src/main.cpp` 新增 `parse_dim`（`strtoll` + `errno` + 整词消费 + `[0, INT32_MAX]`）取代
  `--dims` 的 `std::atoi`；`src/kml_front.cpp` 拆出 `parse_int_wide`，dims 循环先判上界并点名
  `2147483647`。手工对照 + 新臂双管：`ctest` 3/3，`[R1.4-dimrange]` 打印 OK。
- 发射字节未变：conv 对 phase29 基线空 diff，gemm 447 / reduce 239（这是本轮最重要的边界证据）。
- 两条自我修正入账：E37（我写的锚点自动重算器 dry-run 中 3 条归因错误，若 apply 会改坏正确引用；
  工具删除改手工），以及批量替换造出的嵌套反引号坏 Markdown（自查 diff 抓到）。
- 剩余：D8 / D9 / D11 待用户定；harness #4/#5；Intel 真机；**未提交**（54+ 路径）。
- 控制：C1 CLI 退回 atoi 红、C2 删 KML 上界红、C3 `>= INT_MAX` 红；三条都 byte-identical 复原，
  复原后 `ctest` 3/3、`anchors` 全绿（`docs/evidence/phase40/controls.txt`）。

## Session 2026-10-28 续26（Phase 41 — 精确性证明做成腿 + 三后端 init 一致性钉住；修正自己的 182->364）

- 先回答"我的 Phase 36 结论覆盖谁"：三后端 init **逐字同形**（grep 四处），结论不过宽。
- 再回答"这份覆盖由什么保证"：原先**没有任何东西**保证。新增 `[R6.7-init-parity]`：
  计数判据（HIP 2/2、runner 1/1、合计 4/4）+ 测试时重算界（`gcd==1`、周期和 `==0`、
  `max|partial|=364 <= 2^24`）+ 非互素反向对照。
- 自我修正：Phase 36 的 182 是单 stride 值，扫遍 stride 应为 364；结论不变，数字以腿为准。
- 控制两条各自红（改 runner 模数 / 改 HIP 一处模数 → `Assertion failed: a == b`），
  复原 byte-identical；conv 发射对 phase29 基线空 diff（臂只读源）。
- `ctest` 3/3、`anchors` 全绿。同一会话内本轮还落了 **Phase 40（D10）**：两入口维度上界校验 +
  `[R1.4-dimrange]`（三条控制红）。仍未提交；D8 / D9 / D11 待用户定。

## Session 2026-10-28 续28（Phase 42 — harness #5 闭合：探针复原成为门禁，并查出臂里一条恒真判据）

- 新增 `probe/arm_probe_guard.py`（快照 / 唯一写路径 / 退出重读比对 / 残留扫描 / 与进入时的
  `git status src/` 做集合比较）与 `probe/probe_hygiene_check.py`，后者注册成 ctest 第 4 条。
- 实况纠正：phase11-14 的 ROOT 是硬编码 macOS 路径，本机**从未跑起来过**；现已可移植，
  Apple 步骤无工具链时打印 SKIPPED 而不是假装通过。
- 守卫自己的判据错过一次：要求 `git status src/` 干净 ⇒ 与"本仓有正当未提交工作"冲突，
  phase15 首跑就 GUARD FAILURE；改成"与进入时比较漂移"才对。
- 真跑：phase15（2 腿）、phase12（6 腿，本机首跑）、phase16（2 腿）全部指认 + 零漂移复原；
  开火控制（去守卫副本）报 4 条 rc=1。
- 过期站点两处重定位（Phase 13 后端化 / Phase 29 拆链），并留不写入的 `phase42_probe_site_audit.py`：21/21 活。
- **最重要**：`[R8.5]` 的 `migrations=` 是存在性判据、被拒绝行满足 ⇒ 恒真、绿了 26 个相；
  改为计数 `== 2` 后 phase16 的腿红在 `[R6.8]` `tests/acceptance.cpp:1923`（E38，家族第 3 次）。
- `ctest` 4/4、`anchors` 全绿、`probe_hygiene` 绿；发射字节未动。**仍未提交**（66 路径）。
  剩余：D8 / D9 / D11 待用户定；harness 只剩 #4。

## Session 2026-10-28 续29（Phase 43 — harness #4：证据输入进版本控制 + ctest 第 5 条 record_audit）

- 实质：不是“忽略不够”，而是 6 份证据 README 的复现依赖**被忽略的** conv 声明输入；
  另 3 件 Phase 9 产物被强制跟踪在忽略树 `build/phase9-emit/` 里、在 git status 中隐形。 （旧路径）
- 先证再搬：`git mv` 三件产物 → `docs/evidence/phase9/emit/`（sha256 一致、rename、未提交）；
  声明输入 → `workloads/conv/`（`git ls-files` 实证）；从新路径发射 conv 与 phase29 基线**空 diff**。
- 新门禁 `probe/record_audit.py` = ctest 第 5 条：带扩展名路径引用扫 MISSING / IGNORED INPUT。
  两轮收紧：首版 21 条噪声里只有 1 条真（`docs/AGENTS.md`，已改）；逐条 check-ignore 慢到 21 s
  ⇒ 改一次 `--stdin` 批处理。**慢门禁等于没门禁。**
- 开火控制：注入忽略树 `.json` 引用 ⇒ 直接读退出码 rc=1；清除后 rc=0（E34 第三次险情：管道后的 `$?`）。
- 自酿两条入账：E39（`.gitignore` 里把目录名写成忽略规则，新目录反而不被跟踪）、
  E40（目录级替换把同目录运行产物一起迁走，**被本相新门禁自己抓到**）。
- `.gitignore` 补 `__pycache__/`、`*.pyc`。`ctest` 5/5；发射字节未动；**仍未提交**。
- harness 本机项至此**全部闭合**（#1–#5）。剩余待用户定：D8 / D9 / D11。
- E41（同日续记）：批处理提速两次把判据做哑（CR/引号、`-z` 的 NUL 输入约定），ctest 一直绿。
  最终改为读 `.gitignore` 做静态前缀判定（21 s → 0.4 s）+ 门禁开头 `self_test()` 断言两种判据可触发，
  否则 `GATE IS MUTE` rc=1。自测第一次运行就抓到"存在性判断早于忽略判断"的第四个 bug。
  历史路径统一标注"（旧路径）"，豁免的漏报风险写在注释里。`ctest` 5/5 全绿。


## Session 2026-10-29 续30（Phase 44 — D9 落地：运行时设备自报量推导宽度，并证伪我自己引用的提速）

- 实现：`uk_reduce_width_plan()`（设备自报 `maxThreadsPerMultiProcessor × multiProcessorCount` →
  向下取 2 的幂 → 256 地板；查询失败回退 4096 并打印 `source=props_unavailable`）。
  `hcs.compute_units=36` 与自报 14 的分歧是不用名义值的理由。
- 顺带关 rule 6 的 reduce 侧缺口：verify 原先写死 `VP=4096`，现与计时同一宽度；
  真机 1M/16.7M/67M 全 `verify=PASS max_rel=0.000000e+00`，本机自报 `width=16384 cap=28672`。
- **提速主张收回**：出厂两种宽度 `kernel_ms` 差 ≤5%（0.745/0.782、1.895/1.911），
  而 Phase 30/33 探针同对宽度报 2.5× ⇒ 仪器对 4096 基线的测量不一致，"1.65–2.50×"降级为探针现象。
  D9 决策行与 AGENTS 第 7 条都按实况改写；成因留 Phase 45（候选：warmup/迭代编排、L2 驻留、事件跨度）。
- 臂按新形状重写（`= 4096` 常量、`= 256` 地板、单一计划计数）；我第一版计数写错被自己的断言抓红。
  发射产物 reduce 239→268，conv 对 phase29 基线**空 diff**、gemm 447 不变。
- 锚点又漂（`:924→:963` 等），按符号逐个重算；裸 `:NNN` 历史条目只加"当时实况"注记不改写。
- `ctest` 5/5；D11 / D8 未动（下一步）；**仍未提交**。

## Session 2026-10-29 续31（Phase 45 — D11：dtype 能力区间 + 容差回到唯一的表；新增第 6 条门 `runner_tu`）

- 实现：`dtype_gap()` 接进三个后端谓词；`tolerance_for()` 成为 R6.2 三档的唯一来源；三个 HIP 算子的
  判定行改由 `dtype_tol_literal()`（`src/codegen_hip.cpp:81`）从表生成；两个进程内 runner 也读同一张表（`tolerance_for`（`src/runner_ocl.cpp:683`）与 `tolerance_for`（`src/runner_metal.mm:623`），Phase 45 当时是 :583/:559）
  两个进程内 runner 也从 `0.03125f` 改成读表。
- 收证据时发现并修掉账本洞（E43）：`solve` 的拒绝不再挂在 `--emit` 上；`NOTE … model-only` 分支
  随之不可达并删除。实测改前 `solutions` 表 1 行、改后 rc=2 且 `.sqlite3` 不被创建。
- 新臂 `[R6.8-dtype]`（`tests/acceptance.cpp:1912` 起）：三档数值 + 可配置性、三后端各自点名 dtype 的
  拒绝、每具程序**恰好 1 处**判定行且等于表值、两条 runner 的源码文本（并按模式拼装避免自匹配）、
  CLI 的 `--emit` 与**不带** `--emit` 两条路径、以及 fp32 反控制。R1.1 的"声明树不携带未声明的 gap"
  改成按 dtype 分支——那个 int8 fixture 过去是**偶然通过**的。
- 摘除控制 6 条全部按预期（`docs/evidence/phase45/removal-controls.txt`）：C1/C2/C3/C6 红、
  C4 = 只有 `runner_tu` 红、C5 = 绿的盲区如实入库。守卫复原 byte-identical，末轮 `ctest` 6/6。
- 新增 ctest 第 6 条 `runner_tu`（`probe/runner_tu_check.py`，2.7 s）：`-fsyntax-only` 检查本机被
  `#ifdef` 掉的 TU，自带 broken-TU 开火控制；`runner_metal.mm` 作为具名缺口打印而不是静默跳过。
- 重写 `probe/probe_hygiene_check.py` 的分类器（按写入点判定 + 排除自身 + `MUST_BE_CLASSIFIED`
  + `self_test()`）；规则编号与文档对齐（旧的 `rule 3` 其实同时对应两条规则）。
- 证据目录 `docs/evidence/phase45/`：`README.md`、`emit/`（三份产物）、`emit-diff.txt`、
  `dtype-refusal.txt`、`device-verify.txt`、`gates.txt`、`backends.txt`、`removal-controls.txt`。
  历史基线一律不动，只在 README 与 `docs/acceptance-mvp.md` v34 标时点演进。
- `ctest` 6/6；**D11 闭合，D8 未动**；Phase 44 的仪器分歧未查；**仍未提交**。

## Session 2026-10-29 续32（Phase 46 — D8 取证：两档 CPU 参考的实测；不替用户决定）

- 重读规格发现 D8 的前提被我此前写窄了：`requirements.md:10`/`:34` 与 `design.md:132` 要求的 CPU
  参考本来就是**标量 + 分块两档**，出厂只有标量档 ⇒ "换参考"在规格文本上是**补齐**，不是替换。
- 新仪器 `probe/phase46_reference_tiers.cpp`（纯 CPU，`c++ -O2 -std=c++20`，跑完 <1 s）：
  两档 fp32 求和 + Kahan 替身 + 闭式精确值 + "如果另一档当参考会不会越 2⁻⁵"。
  仪器第一行自报 `sizeof(long double)=16 digits=64`。
- 三条结论：**(1)** shipped 输入（常量 `2^-6`，n=2²⁰/2²⁴，宽度 16384/4096）上两档**逐位相同且等于
  闭式** ⇒ 换分块档对已入库 reduce 证据中性；**(2)** Phase 19 的交替 `1e6/1e-6` 上标量档在 n=2²⁴
  越门（`+4.154e-02`）而分块档 `−5.556e-05` ⇒ 换档即可消掉 D8，**容差一字不必改**；
  **(3)** 另一类宽输入里方向反过来 ⇒ "哪档当真值"不能按输入类别口头定。
- 与 Phase 19 四行对账：三行一致（`7.23881e-03` 逐位、`1.86e-04`、`5.56e-05`），
  **一行差 4%（我 `4.154e-02` vs 记录 `3.988e-02`）记为未对齐**，不写成复现成功。
- 记录：`docs/evidence/phase46/{README.md,tiers.txt}`；task_plan Phase 46 行 + 预登记/结果 +
  Current Phase + D8 行注记；findings Phase 46 段；**AGENTS.md 待决事项里 D8 那条把"换参考会改含义"
  改成实测后的口径（中性，仅 reduce；gemm/conv 未测）**，仍明确写"等用户定口径 A/B/C"。
- `src/` 与 `tests/` 本相一字未动；`ctest` 6/6（承 Phase 45 的 6 条门）；**仍未提交**。
- D11 已闭合（Phase 45），D9 已落地并收回提速主张（Phase 44），D8 = 取证完毕、决定权在用户。

## Session 2026-10-29 续33（Phase 47 — D8 口径 A 落地：reduce 基准换成规格的第二档）

- 用户在 Phase 46 的三条口径里选 **A（换分块档参考）**；B（容差随 n）与 C（维持现状）作废。
- 实现：`kRefReduceWidth = 1024` 与内核宽度解耦 + `cpu_reduce_blocked` + 发射程序与两个 runner 同口径；
  新腿 `[R6.9-basis]` 同时钉**中性**（两档在 shipped 输入逐位相同）与**测量性**
  （`basis != kern`、`div_new > 0`、旧基准 `4.160e-02 > 2⁻⁵` vs 新基准 `2.413e-04 ≤ 2⁻⁵`）。
- 真机：五档全 `verify=PASS max_rel=0`（reduce 1M/16.7M/67.1M、gemm 512³、conv 256²×32），
  `migrations=0 checks=6`；产物 diff —— gemm/conv **空**，reduce 268→276 行 `44231aef…`。
- 摘除控制 4/4 红（B1 红在 `basis != kern`，是本相最有价值的一条：它证明这条腿能区分
  "换了更准的参考"和"把参考调成和被测对象一样"）。守卫 byte-identical，`ctest` 6/6。
- 撞坑一次（E45）：`long double(x)` 编译失败 + 陈旧二进制照报 6/6 绿；靠 grep `error:` 拦住。
- 记录：`docs/evidence/phase47/{README.md,device-verify.txt,emit-diff.txt,removal-controls.txt,emit/}`；
  task_plan Phase 47 行 + 预登记/结果 + Current Phase + **D8 行标为已按 A 闭合**；findings Phase 47 段。
- 至此本相目标里的三项：**D9 已落地（Phase 44，提速主张收回）／D11 已闭合（Phase 45）／
  D8 已按用户口径落地（Phase 47）**。开放项剩下：Phase 44 的仪器分歧、G13（Metal/OpenCL 宽度）、
  half/int 内核本身、`verify_fp32` 的整型档。**仍未提交。**

## Session 2026-10-29 续34（Phase 48 — 存盘产物字节独立编译上机 + [R5.4-artifact-bytes] 常驻腿）

- 用户要求：把 AMD 的 conv/reduce **存盘产物本身**在 ROCm 主机上编译运行补证据。
  先确认仓库里没有「未编译未运行」这个字面标记，再按用户选定口径执行。
- 前置：三份存盘产物（gemm/conv/reduce）与当次 CLI 发射**逐字节相同**（`diff-lines=0`、sha 一致）。
- 独立编译运行：`hipcc`（`HIP_PATH`，与 runner 同源）+ `-O3 --offload-arch=gfx1032`，三份全
  `verify=PASS max_rel=0 migrations=0 checks=6`，`kernel_ms` 与 runner 侧差 0.6%~2.4%（只登记）。
- 新增常驻腿 `[R5.4-artifact-bytes]`（`tests/acceptance.cpp:2087`）：重新发射并逐字节比较，
  纯 CPU、`SOURCE_DIR` 控制、缺失时自我声明 SKIPPED。控制 A1（改生成器一个词）与
  A2（改证据文件一个换行）各自把腿弄红 —— **A2 让「改动已入库证据」第一次变得可见**。
- 证据 `docs/evidence/phase48/`：`README.md`、`artifact-build-run.txt`、`byte-identity.txt`、
  `removal-controls.txt`；脚本 `probe/phase48_artifact_build_run.py`、
  `probe/phase48_artifact_removal_probe.py`。
- 自己的坑两条入库：E47（手动绿 / ctest 红：cwd 不同 ⇒ spawn CLI 的腿必须用绝对输入路径）、
  E48（U+200B 零宽空格进脚本，靠码点扫描发现）。
- `ctest` 6/6；守卫复原 byte-identical；**仍未提交**。

## Session 2026-10-29 续35（Phase 49 — D9 提速主张恢复；reduce 的宽度钉值通道；控制归属方法）

- 起点是 Phase 44 遗留的「两台仪器对 4096 基线不一致」。结论：**没有两台仪器的问题，是一次 A/B 从未生效**。
  先证 kernel 文本与 launch 配置两边**完全相同**（排除掉最直观的解释），再看 Phase 44 转录里
  baseline 臂**不打横幅且时间≈另一臂**，最后用真正钉了 4096 的二进制重测：1.255 ms vs 0.751 ms。
- 出厂路径实测（自报 `source=env width=`、交替三轮）：**1.685× @16.7M / 2.499× @67.1M / 2.787× @134M**，
  8192 与 32768 都劣于 16384 ⇒ 设备自报量的选择是采样最优，**D9 的提速主张恢复**
  （Phase 44 的「收回」按时间标注纠正，历史行不改写）。
- 落地 `UK_REDUCE_WIDTH` 钉值通道：读在设备查询之前、生效打印 `source=env`、
  不可用值 `what=reduce_width_pin_unfit` + `exit(2)`（拒绝而非静默回落）、只读一次。
  新臂 `[R15c-width-pin]` 钉**单调锚点序列** `pin < use < refuse < decl < query`。
- 控制 P1–P4 全部加**归属**（影子 `SOURCE_DIR` 让 `[R5.4]` 恒真）。这一步抓到：
  P3 在相邻断言版本下不红（→ 改序列断言）、P1 的替换串没匹配上＝惰性控制（→ INERT 检测）。
- 基线搬家：`[R5.4-artifact-bytes]` 指向 `docs/evidence/phase49/emit/`（reduce 276→290 行
  `b8862c99…`；gemm/conv 逐字节不变）；`probe/phase48_artifact_build_run.py` 同步指向新基线，
  三份存盘字节仍 `build rc=0 verify=PASS`。
- 证据：`docs/evidence/phase49/`（README、`width-env-matrix.txt`、`width-ab.txt`、
  `removal-controls.txt`、`artifact-build-run.txt`、`emit/`）；脚本
  `probe/phase49_widthpin_removal_probe.py`。
- 记 E49（无自报的 A/B 不算 A/B；两臂近似＝警报）、E50（惰性控制 + 相邻≠序列 + 归属缺失）。
- `ctest` 6/6；守卫 byte-identical；**仍未提交**。

## Session 2026-10-29 续36（Phase 50 — ctest 第 7 条门 hip_programs：发射产物被编译器解析）

- 补 Phase 49 记为"仍未闭合"的洞：以前没有任何腿让 hipcc 解析过**生成的程序**。
  新门（当时名为 hip_program_check.py，**Phase 52 改名为 `probe/generated_program_check.py`**，
  并把 OpenCL 产物一起纳入）：CLI 发射 gemm/conv/reduce → `hipcc -fsyntax-only -O3`
  + `--offload-arch=<设备自报 gcnArchName>`（host-only 探针问设备，不写死），首行是 broken-source 控制。
- 三条控制期望各不相同：K1（`e0`→`ze0`）只有门红 → 覆盖面成立；K3（只改注释）两边都绿 →
  不是"改动即红"；K2（Phase 49 的 P3）两边都红 → 与文本臂重叠。
- 两条跳过路径都实测（无 hipcc / 无可见设备），都退 77 并印原因；`SKIP_RETURN_CODE 77` 已注册，
  ctest 报 Skipped 而不是 Passed。`--arch` 是明标 override 的无卡旁路。
- 成本 7.8 s，`ctest` 全套 7/7、12 s。守卫 byte-identical。
- 记 E51：我第一版给控制写的期望（"acceptance 保持绿"）在 `[R5.4]` 存在后结构上不可能成立；
  控制不符合期望时先怀疑期望，并用 Phase 49 的影子 `SOURCE_DIR` 做归属再判定。
- 证据 `docs/evidence/phase50/`（README、`controls.txt`、`gate-output.txt`）。
- 剩余开放项不变：G13（Metal/OpenCL 宽度常量）、half/int 内核、`runner_metal.mm` 本机不可编译、
  MSL/OpenCL 产物的语法检查需要各自主机的工具链。**仍未提交。**（此句写于 Phase 50 当时；OpenCL
  半句由 Phase 52 实测否证——ROCm 自带 clang 就能解析，见续38；MSL 半句仍成立）

## Session 2026-10-29 续37（Phase 51 — G13 拆开：OpenCL 查设备、Metal 保留标注常量、三后端共用 UK_REDUCE_WIDTH）

- `plan_reduce_width` `src/runner_ocl.cpp:190` 改为按 `CL_DEVICE_MAX_COMPUTE_UNITS × CL_DEVICE_MAX_WORK_GROUP_SIZE`
  推导 pass-1 宽度（同 HIP 程序的 2 的幂 + 256 地板算术；查询失败退回 4096 并打印
  `source=props_unavailable`）。
- `plan_reduce_width` `src/runner_metal.mm:136` **不假装查询**：Metal 公共 API 没有 CU 数/每 CU 线程数，
  宽度仍是常量，但来源标签诚实写作 `metal_no_capacity_api`。
- 三后端共用 `UK_REDUCE_WIDTH`：生效打印 `source=env width=`，值不在 `[256, n]` 时
  `what=reduce_width_pin_unfit` 且**拒绝执行**（HIP 侧 exit 2 / runner 侧在任何分配之前返回）。
- 顺带补 rule 6 在 runner 侧的同形缺口：verify 段复用计时趟的 `plan.P`（仅按 verify 实例长度封顶）。
- 新臂 `[R15d-runner-width]`（`tests/acceptance.cpp:2202`）+ 三条**期望各不相同**的控制：
  R1/R2 归文本腿，R3（设备查询里打错变量名）文本腿全绿、门 #6 红 —— 三台仪器的覆盖面被钉住。
- 本机边界如实：无 Intel/Apple 设备 ⇒ 该侧零数值读数；AMD 侧 reduce/conv 真机未回归、
  仍自报 `source=device width=16384`。`ctest` 7/7。
- E52：第三次把真换行写进 C++ 字符串（这次落在被 `#ifdef` 掉的体内，构建与 ctest 都看不见），
  **由门 #6 在 0.1 s 内抓出**——前两次同类都靠人肉读编译错误。
- 记录：`docs/evidence/phase51/`（README、`controls.txt`、`amd-sanity.txt`）；
  G13 行改为"部分闭合 + 剩下一半是主机限制"；acceptance-mvp 附录 v39；task_plan Phase 51 行、
  预登记/结果、Current Phase、E52。

## Session 2026-10-29 续38（Phase 52 — 门 #7 改名 `generated_programs`：OpenCL 产物在这台 AMD 主机上也能解析）

- **起点是把上一相自己写的"边界"当待测命题**：Phase 51 README 与附录 v38 都写"MSL/OpenCL 产物要
  各自主机的工具链"。实测 ROCm 7.1 自带 `clang.exe`，`-fsyntax-only -x cl -Xclang
  -finclude-default-header` 对三份 `uk_intel.cl` 全 rc=0、追加坏 kernel rc=1 ⇒ OpenCL 半句被否证，
  MSL 半句仍成立。**旧转录不改写**，改为在 `docs/evidence/phase51/README.md`、`docs/acceptance-mvp.md`
  v38 与 phase50 README 就地标时点演进。
- **门改成"每后端三段"**：可用性探针 → 该后端自己的 broken-source 控制 → 真产物；
  `checked=6` = 3 op × 2 后端；77 的条件收紧为"一段都没跑"（剥掉 HIP_PATH/PATH 实测两行
  `PROG SKIP … reason=` + `checked=0` + rc=77）；MSL 显式打 `PROG gap msl`。控制的两条报警方向
  都用假前端实测能响（恒接受 → `decorative`，恒拒绝 → `not usable here`）。
- **三条控制期望各不相同**且全部影子归属：Q1 破坏 `.cl` → 门红在 ocl 半边而归属后的 acceptance 与
  门 #6 都绿；Q2 只改注释 → 全绿（门不是字节指纹）；Q3 破坏 HIP 程序 → 红在 hip 半边而 ocl 仍绿
  （两半独立）。三条全按期望落地，守卫 byte-identical、`git drift vs entry: none`。
- **两个自己写的错被门接住**：E53 复现命令 `env -u HIP_PATH PATH=/c/Windows/system32 …` 把 python
  自己也剥掉了（rc=127，照抄不可复现）；E54 把 `build/` 下的瞬态 `.cl` 当证据引用，门 #5 报
  `IGNORED INPUT` 并退 1 ⇒ 修法是把三份字节入库 `docs/evidence/phase52/emit/` 并记 sha256
  （不是放宽检查器）。入库 conv 第 15 行长度 72 字符 ⇄ clang 报的列 73 交叉核对成功。
- **一条容易误读的事实**：`.cl` 是纯 kernel 源码、没有宿主 `main`，`UK_REDUCE_WIDTH` 出现 0 次 （HIP 那份 9 次）。宽度自报在 `src/runner_ocl.cpp:190`，**不是 rule 4 的洞**。
- 改名留下的死引用（`progress.md`、phase50 README）由门 #5 各报一次 `MISSING`，按转录约定把旧名写成
  不带目录的名字、把可复现命令指向新文件。
- 记录：`docs/evidence/phase52/`（README、`controls.txt`、`gate-output.txt`、`emit/` 三份 `.cl`）；
  acceptance-mvp 附录 v40；task_plan Phase 52 行 + 预登记/结果 + Current Phase + E53/E54；
  findings Phase 52 段；AGENTS.md 门列表第 7 条改写为 `generated_programs`。`ctest` **7/7**，
  门 #7 由 7.8 s → 9.78 s。**仍未提交。**
- 明确记下还没做的一半：OpenCL 产物**有基线、无腿**——`.cl` 的字节锚需要一条新腿加它自己的摘除控制。

## Session 2026-10-29 续39（Phase 53 — OpenCL 产物拿到字节锚 `[R5.4-artifact-bytes-cl]`）

- 先把 Phase 52 那句"这只是基线不是腿"变成**实测的洞**：改发射出去的 OpenCL 注释，
  `ctest` 里没有任何腿会红（门 #7 的 Q2 期望本来就是绿，HIP 字节腿不看 `.cl`）。
- 新臂 `[R5.4-artifact-bytes-cl]` 照 Phase 48 钉 HIP 的形状做（`tests/acceptance.cpp:3006`）：现场发射 → 与入库 `.cl` 基线
  **逐字节**比较；**逐 op 先打印 `identical`/`DRIFT` 再断言计数**，因为一个二进制里有两条字节腿时
  "哪条 banner 没出现"是顺序假象不是归属。基线没有被覆盖（2594/1877/1708 B）。
- 四条控制期望各不相同且全部按 `want` 落地：C1 专属红（门 #7 绿 / HIP 腿绿 / `drift=['reduce']`）、
  C2 与门 #7 重叠、C3 红在另一条字节轴、C4 中性全绿。
- **本轮最承重的一条有读数**：新腿让影子归属失效（装置只中和 HIP 那一份基线），
  `probe/phase53_shadow_pre_fix_replay.py` 实测——影子树带 `.cl` ⇒ rc=0；只删那三份 `.cl` ⇒
  rc=3221226505、`!fresh.empty() && !stored.empty()`、逐 op 行 0 条。
  于是修的是**装置**（`shadow_root` 也种 `.cl`），Phase 52 的三条控制**一条期望都不必改**就重新全中。
- E55（两处自身缺陷）：`split()[2]` 应为 `[1]`（"漂到哪个 op"读成 `DRIFT`）——**被具体到 op 名的
  预登记期望抓住**；转录目录没预建，四条控制跑完 3 分钟后才在写文件时崩，现在探针开头先检查父目录。
- 记录：`docs/evidence/phase53/`（README、`controls.txt`、`leg-output.txt`、`shadow-pre-fix.txt`）；
  task_plan Phase 53 行 + 预登记/结果 + Current Phase + E55；findings Phase 53 段；acceptance-mvp
  附录 v41 与 v40 的演进注记；AGENTS 探针段新增两条（归属装置必须覆盖**全部**字节腿；
  控制期望必须具体到能区分失效模式的字段），并清掉一处 E27 型残留行。
- `ctest` **7/7**（门 #2 由 2.0 s → 2.4 s）。**仍未提交。**剩下同类缺口：**MSL 产物既无语法检查
  也无字节锚**。

## Session 2026-10-29 续40（Phase 54 — Metal 的语法变成探针读数、字节变成第三条腿）

- 先量再写：门 #7 不再**断言**"本机没有 Metal 前端"，改成逐个探可及的 clang，两步（先问
  `-x metal` 认不认，再问 `<metal_stdlib>` 有没有）。本机两个候选都停在第一步
  （ROCm 7.1 clang 21 裁剪版、wasi-sdk 34 clang 23.1-rc3）：`language not recognized: 'metal'`，
  `-x metalpp` 同。读数入库 `docs/evidence/phase54/msl-probe.txt`。
  ⇒ 跳过行带着前端原话；换成认 Metal 的主机，同一段代码会自动继续跑控制与真产物。
- 字节这半与前端无关：新臂 `[R5.4-artifact-bytes-msl]`（`tests/acceptance.cpp:3070`）照 Phase 53
  形状——逐 op 先打印 `identical`/`DRIFT` 再断言计数。基线入库 61/40/36 行 = 2893/2105/1885 B +
  sha256，旧基线一个字节没动。
- 三条控制与 `want` 逐字段一致：M1 改**发射出去**的 Metal 注释 → **只有这条腿红**
  （门 #7 GREEN、HIP/CL 两条腿都 3/3）；M2 改 OpenCL 注释 → CL 腿先 assert 而 MSL 腿一行都不打
  （顺序伪装成归属，第二次实证）；M3 改不发射的注释 → 全绿。
- **Phase 53 的规则第二次生效，而且这次是复验**：`shadow_root` 现在种三份产物
  （`uk_amd.cpp`/`uk_intel.cl`/`uk_apple.metal`），补完后重跑 **phase49/50/51/52 四条旧影子控制套件**，
  它们的 `want` 一字未改仍全中。
- 本轮没有新的自身缺陷：Phase 53 那两条（转录目录、锚点唯一性）都被探针的前置检查挡住。
  记一条运气：Metal 与 OpenCL 的 reduce 注释文本本来不同，所以 M1/M2 天然各属一个后端。
- 记录：`docs/evidence/phase54/`（README、`controls.txt`、`msl-probe.txt`、`leg-output.txt`、`emit/`）；
  task_plan Phase 54 行 + 预登记/结果 + Current Phase；findings Phase 54 段；acceptance-mvp 附录 v42
  与 v41 的演进注记；AGENTS 门 #7 那条改写（Metal 按探针决定）+ 字节腿清单。
- `ctest` **7/7**。**仍未提交。**明说的未 exercised 半：Metal 分支的"控制 + 真产物"路径本机跑不了。

## Session 2026-10-29 续41（Phase 55 — 累加次序：字节红不等于轴红）

- 洞：三条字节腿（Phase 48/53/54）能对**任何**产物改动报红，但红里不含"哪条轴"。硬约束第 3 条
  （改算术顺序 = 改结论）在 HIP 侧由 `[R20]` 按字节钉住嵌套，OpenCL/Metal 侧**什么都没有** ⇒
  改折叠次序与改一句注释是同一种红；而 Intel/Apple 的入库读数恰恰依赖那几段嵌套。
- 新臂 `[R20-nonhip]`（`tests/acceptance.cpp:2818`）：进程内生成（`ocl_kernel_source`/`msl_kernel_source`，零额外 CLI
  发射），2 后端 × 3 算子 = 6 个 pin，各钉一条**单调锚点序列**（E50）+ **累加语句次数 = 1**（E21/E24），
  并**自己数打印的判决行数**（`assert(printed == 6)`，`:2836`）；臂排在字节腿之前——顺序本身就是归属。
- 归属方法换掉：CRT 消息带**表达式 + 行号**，探针运行时读 `  // -------- R` 臂头表把行号映射回
  拥有它的臂，不再靠"哪条 banner 没出现"。臂表本轮读数：`R20-nonhip 2747-2845` /
  `R5.4-cl 2846-2909` / `R5.4-msl 2910-2976`。
- 五条控制全按 `want` 落地：N1 只在 `ocl_conv_kernel` 内把 conv 的 `c` 循环改倒序 → 红归次序臂、
  broken 只点名 `ocl/conv`（`msl/conv` 仍 ok）、门 #7 绿（产物仍合法 C++）；N2/N2b 分别归
  `R5.4-cl`/`R5.4-msl`；N3 归 `R5.4` 且次序臂一行都不打（记为方法局限）；N4 中性全绿。
- 两次设计自我修正：对调 `kh`/`kw` 两行会破坏花括号、把门 #7 一起拖红 ⇒ 改成反转外层 `c`；
  嵌套针在两后端各一份（整文件计数 2）⇒ 补丁必须**按函数切片**并断言片内恰好一次，否则会命中
  另一份再把红报给错误的后端（E50 惰性/别名与 E21/E24 次数的合流）。
- **本相最值钱的是一条仪器盲区，且有读数**：新臂插到字节腿之前，把 Phase 53/54 写下的 5 个
  `tests/acceptance.cpp` 的 :2747/2787/2800/2811/2863` 整体挪了约 99 行，而门 #3 报
  `all doc->code anchors agree`——它只在被引行**带有可核对代码符号**时核 ±25 窗口，而那些行带的是
  `[R5.4-…]` 方括号标签（被过滤器排除）⇒ 检查在这里空转。已按 E23 全部 grep 现取改指；
  "无法核对的引用必须显式计数或失败"记为下一相的靶子。
- E56/E57：判决行在转录落盘之后才打印 ⇒ 入库 `controls.txt` 不含自己的结论；
  把"六条都 ok"塞进 `want["broken"]`（该字段只收破的行）⇒ 三条控制结构上永远不可能匹配；
  N3 期望写成第二条断言而非第一条。
- 记录：`docs/evidence/phase55/`（README、`controls.txt`、`leg-output.txt`）；task_plan Phase 55 行 +
  预登记/结果 + Current Phase + E56/E57；findings Phase 55 段；acceptance-mvp 附录 v43；
  AGENTS 第 3 条补非 HIP 侧。**仍未提交。**`ctest` **7/7**，门 #2 2.4 s。
## Session 2026-10-29 续42（Phase 56 — 门 #3 的静默段拆成三层，"没核过"变成只降不升的数字）

- 先数再改：288 条 `path:line` 引用里 **132 条**所在行没有可核对符号——本仓引用臂写 `` `[R5.4-…]`（`path:line`）``，
  方括号标签被旧符号过滤器排除，于是门一直报 agree。Phase 55 已付过一次账（插一条臂挪了 5 个引用约 99 行）。
- 三层判定落地（`probe/anchors_check.py`）：邻接符号 → ±25 窗口；**臂标签 → 臂段归属**（标签映射到臂头表
  加上该臂 `std::cout` 打的 banner，只按头表会漏长形 banner，只按"出现过"就退回存在性判据）；
  段落有符号而引用无邻接符号 → weak；什么都没有 → unverifiable。后两层各一条**只许下降**的上限。
- **第一次运行抓到 19 条真实陈旧引用**（acceptance-mvp 3 / phase24,26,31 各 1 / findings 5 / progress 2 /
  task_plan 5 / AGENTS 1），全是行号漂移而不是标签失效；全部 grep 现取改指并保留"当时行号"。
  其中一条是跨文件指控（HIP 的 `dtype_tol_literal` 被邻接到 runner），runner 真正读的是
  `tolerance_for`（`src/runner_ocl.cpp:683`、`src/runner_metal.mm:623`）。
- **棘轮当天抓到作者**：本相新写的记录把 weak 从 116 顶到 117 → 门红；修引用写法而不是改上限，
  收口时上限降到 weak 109 / unverifiable 91，臂段归属核对 34 → 43 条。窗口 ±25 一次没放宽。
- 四条控制各不相同：L1 错位的臂标签 → 红且给出标签主人与行主人；L2 同标签指到自己段首 → 绿且计数不动
  （没有 L2 就分不清"任何标签都指控"与"归属错才指控"）；L3 符号在上一行 → weak +1 被上限压红；
  L4 整段无符号 → unverifiable +1 被上限压红。临时文件写在 `finally` 里删除。
- E58：第四次把代码文本经非 raw 的 Python 字符串搬运（`
` 变真换行 → SyntaxError），
  这次靠"写完立刻运行"一个回合拦下。
- 记录：`docs/evidence/phase56/`（README、`controls.txt`、`gate-output.txt`）；task_plan Phase 56 行 +
  预登记/结果 + Current Phase + E58；findings Phase 56 段；acceptance-mvp 附录 v44；
  AGENTS 的 `anchors` 那条重写成"引用要写成门能看见的形状 + 两条只降的上限"。
- `ctest` **7/7**。**仍未提交。**打开的债：weak+unverifiable = 200 条引用仍未被核内容，上限只保证不增长。
## Session 2026-10-29 续43（Phase 57 — 表达式形状：非 HIP 两后端的索引式按字节钉）

- 洞：第 3 条管次序**和**形状；Phase 55 只钉了次序 ⇒ 换索引式不动循环，只有字节腿红，红里不说轴。
- 新臂 `[R20-nonhip-expr]`（`tests/acceptance.cpp:2917`）：6 pin（2 后端 × 3 算子）、逐字节多行片段、
  次数=1、先打印判决再断言、臂自己数行数；片段全部 grep 现取。
- 控制成对：E1 在 `ocl_conv_kernel` 内改索引式 → 次序臂 6/6 干净 + 表达式臂点名 `ocl/conv`；
  E3 反转 `c` 循环 → 次序臂红、表达式臂沉默（顺序局限 ⇒ 一根控制证不开两根轴）；
  E2 只改注释 → 两臂全绿、红归 `R5.4-cl`；E4 中性全绿。四条全按 `want` 落地。
- **上一相仪器第一次上岗**：字节腿被推后 89 行，`ctest` 立刻失败并点名 13 条引用（5 条走臂标签归属）；
  全部 grep 现取改指，两条上限未调。**棘轮自己也响了两次**（本相 README/结果段的引用形状不可核，
  unverifiable 91→92），改写法解决 → 记 E59：写文档与写代码同律，改完立刻跑门 #3（0.15 s）。
- 又错一次期望（两臂先后记反，实际证据更强）；基线一字节未动；`ctest` 7/7。**仍未提交。**

## 续44（2026-10-29，Phase 58：组引用的每个数字都被核）

- 接着上一相留下的两个未覆盖形状动手。先数再改：`ANCHOR` 只取组引用第一个数 ⇒ 本仓 15 处组引用、
  40 个数字里 30 个从不被核。`expand_citations()` 拆成逐数核对，判据是**臂归属核必须涨**（90→99），
  只看 `anchors checked` 会把一次错改读成覆盖变宽。
- **自己的仪器当场错两次，都有前后读数**：①尾巴吃到空 ctx（weak 80→89、unverifiable 69→90，臂核不动
  = 信号）；②按**切片**配反引号，切片常从上一段 span 的收尾反引号开始 ⇒ 一行第二个及以后引用的符号
  隐形。改整行配对 + 位置过滤后 weak 83→61（22 条复活），并且**当场指出一条文档引用了代码里从不存在的
  符号名**（P4 不变量为真、位置也真：拒绝在 `r.verified`（`src/main.cpp:540`））。记 E60。
- 重读债目抓出四条假引用：三后端 `initA` 四处行号全错、dtype 允许表指到 `op` 那行、
  `dtype`（`src/kml.cpp:107/117`）其实两个数都对只缺邻接符号、四行"当时位置"被写成活引用。全部 grep 现取
  改指，**没有一条靠调阈值解决**。
- 六条控制 `probe/phase58_groupcitation_control.py` 全命中（G1 尾巴越界点名、G2 组标签对组内每个数
  都要成立、
  G3 同臂绿 + debt delta=0、G4 一行两组不互相开脱、G5 一数一引用同样红、G6 切片位置的符号仍须读到
  且报文点名）。`docs/evidence/phase58/` 存 README + 控制转录 + 门输出 + 全量债目 120 行。
- 第 56 相的控制脚本因写死臂首行号而失效一次：它报的是"门没行为正确"，实际是控制过期（E61）——
  改为每次运行从臂表现取，找不到就 `sys.exit` 而不是悄悄绿。
- 占位符 `path:NNN` 被当成符号指控了五条真引用 ⇒ `code_tokens` 加大小写敏感的排除 + 三双向 self-test
  （占位符拒、`initA` 与 `std::cout` 留）。
- 棘轮只降不升：`WEAK_CAP` 104→61、`UNVERIFIABLE_CAP` 91→59。本相的证据 README 与记录各顶红一次，
  其中一次形状很新：**描述"过去错在哪"的句子如果写成 `path:line`，它就是一条活的假引用**（E62）。
  围栏内 3 条转录按"转录不改写"显式跳过。
- `ctest` **7/7**；三条字节基线与 `[R20-nonhip-expr]` 的六个片段一字未动；**仍未提交**（用户没要求）。

## 续45（2026-10-29，Phase 59：逐条还计数债，抓出 20+ 个错行号）

- 相初 120 条债。先做**并排印**（每条债 + 它指的代码行今天是什么），再逐句分类：
  补符号 / 重指行号 / 降级为文本 / 留作计数。**最大的一条发现**：多数"没符号"的债其实是**行号漂了**——
  887（8 处）、840（3 处）、587（4 处）、739（2 处）、805/562/530 各 1 组，全部 grep 现取重指，
  门随即复核。⇒ 计数债的意义是**把"没人看过"变成可操作清单**，不是让数字好看。
- 反例也很有价值：`plan_reduce_width`（`src/runner_metal.mm:136`）的 136 一直是**对的**，只是句子没主语。
  这两类只能逐条读出来，抽查会混。
- 仪器：新增**有界尾随归因**（引用之后、同一子句内的符号也算），截断在子句分隔符 / 下一条引用 / 40 字；
  控制 `probe/phase59_trailing_control.py` 五条 H1–H5 全命中，其中 H2 要求**报文点名**那个符号
  （只看到"变红"分不清窗口违规与 cap 被顶）。
- 堵住一个真空洞：`code_tokens` 原先接受 `double`/`static` 这类词——插在任何引用前都能让那条引用**永久绿**
  而不核任何东西（E64，"能变绿≠在断言"）。`self_test()` 现在双向三对：占位符拒、`initA`/`std::cout` 留、
  停用词拒。
- 两条自动化 + 人审：`phase59_debt_rewrap.py`（只改换行、一字不改，7 处成功；拒绝表行/围栏/引用块/列表标记）
  与 `phase59_debt_adjacency.py`（只允许句子已写过的 token，且必须过窗口判据）。**本相审出 3 条不能落笔**：
  `double`、从相邻表格行借的 `kRefReduceWidth`、主语错位（`uk_reduce_sum` 那句提议插 `uk_reduce_part`）
  ⇒ 工具一条都没自动写盘，全部手工重写。这是"先 dry 再人审"第一次真正改变结果。
- 第 56 相的控制**对常数做了假定**（以为 cap 恒等于实测），债一降它就指控正常的门（E63）；改为现读上限。
  修的时候还抓出 `run_gate` 的分组下标错——打印的"unverifiable=61"其实是 weak 的 cap。
- 本相自己的 README 两次把"过去错在哪"写成活引用（E62 家族第 5 次）；改写法，不动阈值。
- 读数：weak 61→**37**、unverifiable 59→**46**、债 120→**83**、被核行声明 329→**383**。
  `ctest` **7/7**；三条字节基线未动；**仍未提交**。

## 续47（2026-10-29，Phase 60：注释文本型债务 + 臂名里的 `/`）

- 接着清上一相的 83 条。11 条 dead 逐条读：8 组是**行号又漂了**、3 组是句子引用**发射出去的注释**。
  重指清单见 `docs/evidence/phase60/README.md` 与 findings 的 Phase 60 节（412→435、739→762、
  844→946、805→807、562/530→660/593、main 311-312→552、kml_front 108/113→127、metal 110→112/213）。
- **四条的结论本身也已过期**（上界校验 / G6 交叉校验 / D9 运行时宽度 / CPU 参考第二档）——
  ⇒ 仪器管"号码指什么"，句子对不对只能靠重读；处理是历史降级为文本 + 现状态另给活引用。
- 仪器侧的**词法不一致**：臂标题可以是 `R6.2/R6.4`，文档也用 `[R6.2/R6.4]` 引用，但 `arm_labels()`
  的字符类没有 `/` ⇒ 6 条可核引用被当成沉默。放行后臂归属核 99→**104**、`unverifiable` −6。
  新控制 L5/L6；**L6 第一次跑是绿的**，因为我注入成没有方括号的样子——控制的 bug 长得像门的行为，
  只有读"报的具体内容"才能分开（E38 家族）。
- 抓到上一相自己的不完整修法：给重置行配的符号 `smem` 只有 4 个字母，过不了 ≥5 规则 ⇒ "改了但没降"。
  改配 `uk_conv_plan`（`src/codegen_hip.cpp:418`）才真的降。顺序固化：**先长度、后窗口**。
- 读数：债 83→**69**、`weak` 37→**30**、`unverifiable` 46→**39**。本相证据 README 自己也顶红一次
  （同一行第二个引用没配符号，E62 家族第 6 次）——改写法，不动阈值。
- `ctest` **7/7**；三条字节基线、`[R20-nonhip]`/`[R20-nonhip-expr]` 片段未动；**仍未提交**。

## 续48（2026-10-29，Phase 61：三个判据洞，一条句子都没重写）

- 上一相的结论是"剩下 40 条要重写句子"。本相先问**为什么非重写不可**：如果那一行明明有 ≥5 字标识符
  而门没认，那是判据的洞。结果债从 69 掉到 **19**，全部来自修门：
  ① `TOKEN` 的 ≥2 字符要求让 `` `Y` `` 无法配对 ⇒ 同行后面每对反引号错位，`hipMemcpyAsync` 这种主语
  凭空消失（9 条）；② `^[A-Z]+\d` 带 `re.I` 把 `fp16_rel` 当臂名吞掉（3 条）；
  ③ 我自己工具的插入点落在引用开引号**之内**，破 span 20 处——门不报红，是 dry 打印读出来的（E68）。
- 两个判据修复都跑了**真实摘除控制**（还原后 `self_test()` 报 MUTE OR LOOSE 并点名案例）。
  第一次的"验证"其实是我把临时副本写崩了产生的 SyntaxError——rc=1 不等于控制生效，**要看报文**。
- 顺带重指 6 组过期行号（557→540、434→516、415→463、89→106、963→1014、65-66→76），
  并把一次性批处理全部落成 `probe/phase61_*.py` 的显式列表：上一轮一条 heredoc 的重定向让 `cat`
  读进自己的输出，写出 100 MB 垃圾文件（E69，已删；D 盘一度只剩 3.6 GB）。
- 拒绝消音：剩下 19 条里 5 条断言的是发射文本、3 条是消息串、4 条在围栏/引用块转录内、
  5 条是"当时状态"的英文记录——**不用** `std::endl` 这类"能变绿但什么都不核"的符号压数字。
- 读数：`weak` 30→**8**、`unverifiable` 39→**11**、被核行声明 420；三相连做 **120 → 19**。
  `ctest` **7/7**；三条字节基线未动；**仍未提交**。

## 续49（2026-10-30，Phase 62：R8.4 的 mmap 通路——被 61 个相漏掉的真 SHALL 缺口）

- 起手是例行的"继续"：构建最新、`ctest` 7/7、四项决策全闭合、Phase 61 后没有预登记。
  做了一次规格正向扫描（Phase 16 之后的 45 个相全是被动修缺陷，从没逐条对过规格），
  第一颗就命中：规格要求场景 B 在 pinned/mmap 两候选数据通路间由 C 择优，实现里
  `MmapStream` 只在枚举出现一次，61 个相的记录零提及，验收文档 R8 行也没有——
  判定为真缺口而非有意识的降级。
- 平台探针先行（Phase 25 纪律）：文件映射视图连 Default 注册都被拒（驱动不能页锁定
  不归它管的页）、映射设备指针读可行（Mapped 堆注册与 MallocMapped 都 OK）
  ⇒ 真 mmap 不可行，实现采用**仿真页**：数据真落盘、fread 按预取距离 refill 两个
  映射舞台缓冲、内核经映射设备指针读 A（kernel 体零改动 = 加法次序不动）。
- solver：gemm 双候选（|X| 25→50）+ nvme 层 F 约束（R10.2 形态）+ reuse-aware 成本；
  支配性先说破再写码：C 对任何有限 nvme 带宽选 pinned，是模型的结论不是省略；
  conv/reduce 保持 pinned-only（能力区间 = gemm-A）。新臂 `[R8.4]` + 摘除控制三件全 PASS。
- codegen：`uk_stream_pass_mmap` + `UK_GEMM_STREAM_PATH` 旋钮（unfit 退 2、横幅自报）
  + mmap 时验证段也走映射路径（rule 6 的 R8.4 形态）。gemm 产物 447→721 行重录基线；
  conv/reduce 逐字节不变（与 phase49 锚一致的三条 sha256 在改动前先验过）。
- 真机双臂 12416³（合成 vram=2GiB HCS：D 盘只剩 2.1 GiB，真超订形状的 A 文件
  ≥2.27 GiB 放不下，声明写在 HCS 头部）：pinned 1378.09 ms / mmap 54383.98 ms，
  `verify=PASS max_rel=0` 双臂；映射读有效带宽实测 2.8–3.4 GB/s，模型按官方链路
  定价低估一个量级——支配性结论不变，数值记为模型边界。
- E70：mmap 臂第一次真机跑就把测量提交进 pinned 解的账本键（同键行被 237× 离群值
  污染）。修复 = stream_path 进 RunOutcome + 结果行解析 + `LEDGER SKIP` 提交门；
  被污染行由 pinned 复跑恢复。自报身份必须接到落库层，否则只装饰不设防。
- anchors：本相插入推移 70 条引用；`probe/phase62_stale_repoint.py` 机械 +64 重指
  （例外 2014→2087 / 1599→1777 逐条验证；第一版用 git checkout 做回滚在这个几十个相
  未提交的仓里是致命的，当场改内存快照）；weak/unverifiable 棘轮未升。
- `ctest` **7/7**。仍未提交（用户未要求）。

## 续50（2026-10-30，Phase 63：差距审计 + 真实厂商模型形状驱动完整流程）

- 应用户两问：①当前实现距 AGENTS.md 一句话目标多大差距；②能否用 Qwen3-Embedding-4B
  GGUF 验证完整流程。预登记先行，能力边界先说破：模型级运行是 MVP 外（三原语、fp32-only、
  反量化非原语、fp32 权重 16 GB > 8 GB vram、harness 非运行时），形状级完整流程可行。
- `probe/phase63_gguf_header.py` 纯 struct 解析 GGUF 头：398 张量、36 KV，真实维度
  hidden=2560 / ff=9728 / 36 层 / head_dim=128 / pooling=MEAN；每层 7 个 GEMM。
- 设备厂商资料：`phase63_device_probe` 实测自报量（14 CU——与规格 16 CU 不一致，设备胜出；
  GDDR6 ×16 推导 224 GB/s；clockRate 1881 MHz 是时刻值——后被实测吞吐 3.85 TFLOPS 推翻，
  E26 再现）→ `hcs_amd_device.json`。
- KML 声明 5 个真实形状（workloads/llm/）：第一版塞了 comment 字段被前端 R1.2 当场拒绝
  （未知字段拒绝是对的）→ 字段化后五个形状真机全 PASS（max_rel=0）并 COMMIT。
- calibrate（R7.3）：5 行 → link 32 → 5.7479 GB/s 中位（gemm 行 4.98–5.93 与 5.43 同族；
  reduce 行 50.7 噪声离群被中位吸收）；校准 HCS（v2）复解复跑：ε_rel 1.4–1.8 → 0.0053/0.032。
- 差距审计：五段链路本机全闭合；真实差距 = 语义覆盖面（三原语）、dtype（fp32-only）、
  Intel conv/reduce 真机、生成物形态（harness 而非内核库）、V1 项。
- `ctest` 7/7。本相产物未提交（纪律）。

## 续51（2026-10-30，Phase 63 差距审计与后续阶段计划）

- 完成 Phase 63 差距审计：对照 AGENTS.md 目标（KML→Solve→Generate→Verify→Ledger）与 Requirements R1-R10，产出 10 项差距表（写入 task_plan.md）。
- 核心差距 10 类：①缺 Transformer 核心原语（softmax/RMSNorm/注意力/反量化/逐通道缩放）；②fp32-only（需 fp16/bf16/dtype 轴 + 量化路径）；③KML 扩展支持新 dtype/新原语；④mmap 成本数值标定 + 能力→F 合流 + calibrate 分层；⑤生成物形态迈向 per-layer 内核库 + 图执行器；⑥ Intel conv/reduce 真机 + Apple MSL 真编译；⑧ 量化生成路径；⑨ calibrate 池分层；⑩ 仪器盲区（臂标签词法、calibrate 门分层）。
- 制定 Phase 64-70 七阶段计划（写入 task_plan.md 表格）：Phase 64 新原语（softmax/RMSNorm/逐通道缩放）→ Phase 65 dtype 轴（fp16/bf16/量化）→ Phase 66 求解增强（mmap 标定/能力→F/calibrate 分层）→ Phase 67 运行时形态（per-layer 库+图执行器）→ Phase 68 真机闭合（Intel/Apple）→ Phase 69 量化路径 → Phase 70 仪器完善。
- `ctest` 7/7 全绿。本相产物未提交（纪律）。

## 续52（2026-10-31，Phase 64 闭合：新原语真机闭环 + 门 #7 扩 6 op）

- 起点：`5ae8a2a` 合并带来远端改写的文档（acceptance-mvp.md 1557→1082 行）与
  两条引用漂移 + weak/unverifiable 超上限；Phase 64 的实现代码（softmax/rmsnorm/
  channelscale）本来就在本地 ad36876 里。先还仪器债再干活。
- **合并回归修复**：`print_infeasible` 退 3 重指 `src/main.cpp:318-319`、
  `verify_fp32` 重指 `src/runtime.cpp:191`、`cmd_run` 提交门重指 540-566（主语
  换成真在行上的 `measured_ms`）；acceptance-mvp.md 五条弱/不可核引用逐条 grep
  现取重指（`pick_intel_gpu:532`、`sketch:16-17`、`feasible:51-70`、
  `hcs_import:213`、`cost_ms:79`）。anchors 门恢复全绿（weak 7/8、unver 10/11）。
- **真机实测抓出四个生成器缺陷**（三新 op 的 HIP 程序从未被任何门编译过——
  E51 同形）：老式 `uk_mmalloc` 重复声明 `hipError_t e`；`hipFree(const_cast<
  void*>(p))` 作用在 `void** p`（非法且语义错，guard85 的 const void* 版本不动）；
  计时段引用 verify 块作用域里的 X/Y/W/S；计时段多一个 `}` 把 main 提前关掉。
  修后三 op 真机全 `verify=PASS`（softmax/channelscale max_rel=0，rmsnorm
  2.07e-07），`LEDGER HIT`→`COMMIT` 往返正常。
- **门 #7 扩 6 op**：第一次运行当场抓到 OCL softmax 行指针缺 `__global`
  （address space 错误）——门扩到哪里，哪里就不再是文本盲区。检视另抓 MSL
  rmsnorm/channelscale 的 `L [[buffer(2)]]` 与 `Y` 绑定冲突（本机无 Metal 前端，
  记为源码文本腿 + 明说缺口）。修后 `checked=12` PASS。
- **工具教训（E69 家族再现）**：heredoc 里的 `\\n` 被吃掉一层，替换串静默不匹配
  ——源码级批量替换一律落成 Write 的脚本文件（fix_phase64_nonhip.py），
  heredoc 只用于无反斜杠的纯文本。
- 证据：`docs/evidence/phase64/`（emit 九份基线 + sha256 + 两次真机 run 转录 +
  README 写明可逐字复现命令与既知缺口）。`ctest` **7/7**。
