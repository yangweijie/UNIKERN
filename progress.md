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
