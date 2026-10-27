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
- [x] 新边界如实入册（不在本轮顺手实现）：**G6** — Metal runner 的 resident 预算取 HCS
  声明的 32 GiB（`src/runner_metal.mm:110`），未与 `recommendedMaxWorkingSetSize` 交叉校验；
  本机 payload 3 MB 远低于两者，读数与结论不受影响，但"0.85×容量"目前只对声明容量成立
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
  `probe/phase11_arm_probes.py` 逐层摘除验红：谓词 → `:895`，CLI 门 → `:927`，
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
