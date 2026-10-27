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
  失败消息改注出处（Mac 侧构建 + `src/runner_ocl.cpp:434`，转录只捕获 rc=1/零落库），
  补 Windows 构建实测消息；③ Phase 9 ocloc AOT 三件产物 `git restore` 恢复
- [x] 自捉修复：`unikern ledger <path>` 静默忽略路径参数（`src/main.cpp:604` 硬编码
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
