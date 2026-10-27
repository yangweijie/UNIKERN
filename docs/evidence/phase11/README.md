# Phase 11 证据产物（docs/evidence/phase11/）

G7 闭合：**非 GEMM 算子被静默当作 GEMM 执行/回写**这一缺陷面，以及为它加的三层拒绝与
带弹性探针。所有命令在 Apple M4 / macOS 26.6.2 arm64 主机上实跑；数值为运行时实测。

| 文件 | 生成命令 | 证明的事 |
|------|----------|----------|
| phase11-defect-replay.txt | `probe/uk_op_probe_prerefusal.cpp` 的 A/B 两臂（A=摘掉 `op_support_gap` 谓词，B=当前提交态，库 md5 各自记录） | 缺陷本体当场可复跑：A 臂 `reduce:fp32:1048576x4096x4096` 的账本键下发出 `const int PM = 1048576, PN = 4096, PK = 4096;` 的 uk_gemm 且**零拒绝**；B 臂同一调用不 emit 任何内核、改发 `op_unsupported` |
| phase11-cli-refusals.txt | `probe/phase11_cli_capture.sh` | CLI 四条臂的 rc 与输出：run/verify 拒绝（rc=2、账本文件**不创建**）、`--emit` 拒绝（目录**不创建**）、`--op conv` 在 parse 阶段按 arity 拒绝、`--op gemm` 对照仍解出 `PM = 64` 并三源齐发 |
| phase11-runner-refusal.txt | `probe/uk_op_refuse_probe.mm` 编译并运行 | runner 层单独可证：内核取自 gemm 树、tree 参数取 reduce 树的**错配调用**被入口拒绝（probe_rc=0）；含本轮自捉的"量到过期产物"坑与 `-x none` 编译坑 |
| phase11-arm-probes.txt | `python3 probe/phase11_arm_probes.py` | 三层各自"摘掉即红"：谓词 → 红于断言 `op_support_gap(*pr.first).empty() == (op == Gemm)`；CLI 门 → 红于 run 臂的 `rc == 2`；runner 入口门 → 探针转红（真机上跑出 `perf=1048576x32x32 verify=PASS` 的错配测量），且此态下 ctest 仍绿 → **runner 层的回归位是探针，不是单测**；收尾校验残留标记为 0 并 ctest 全绿 |
| phase11-arm-probes.txt 末尾复验块 | Phase 12 之后复跑同一脚本 | 三层结论不变；**行号坐标会随测试文件插入漂移**（Phase 12 在前部插入 `[R1.1/2]` 段后，谓词腿改停在更早的 `:304`、CLI 门改到 `:1060`），故引用这三条腿以腿名+断言文本为准，行号只作当次坐标 |
| phase11-ctest.txt | `ctest --test-dir build-apple --output-on-failure` | 当前树 2/2 全绿（含 acceptance 新增 R10.2 臂） |
| `probe/uk_op_gap_probe.cpp`、`probe/uk_op_probe_prerefusal.cpp`、`probe/uk_op_refuse_probe.mm`、`probe/phase11_cli_capture.sh`、`probe/phase11_arm_probes.py` | — | 探针源码本体（编译产物按 .gitignore 不入库） |

## 关键事实与边界（如实记录）

- **缺陷为什么重要**：修复前，`--op`/树里声明的算子与真正执行的负载无关——非 GEMM 树会
  走 GEMM 内核、拿结构体默认维补出的尺寸计时，再以 `reduce:...` 的键把这份测量回写账本。
  于是 R7.3 的校准输入会被"另一种工作的时间"污染，而项目自己立的 R10.2
  （"能力覆盖不了就标记不支持，绝不静默降级"）正好被破。
- **三层拒绝的分工**：谓词 `op_support_gap()`（单一判据）→ 生成物自拒（HIP 程序打
  `UNIKERN_ERROR what=op_unsupported` 退 4；MSL/OpenCL 文件不含内核，编译期即响亮失败）→
  runner 入口自拒（在写 workdir 与编译**之前**，避免把拒绝用的源文件盖掉上一次的真产物）→
  CLI 门（在 ledger 任何读写**之前**，被拒的树不可能留下账本行）。
- **md5 口径（本轮实测纠正）**：静态库 `libunikern_core.a` 的归档头含成员时间戳，同一源码
  重建两次 md5 也会不同（实测 `b6e0a56b…` → `f4ee8f49…`，源码未改）——因此目录内转录所录的 `.a`
  md5 只标识"这份读数出自哪一次构建"，**不是同源性证明**；同源性由每臂自带的行为判据
  （`op_support_gap(reduce)` 为空串 vs 返回 `kernel generation covers gemm only; op=reduce`）承担。
  可执行文件 `build-apple/unikern` 的 md5 经两次重建实测不变（`22afe4796943da3d3d5f05753a36b6fa`），
  CLI 类转录以它作锚。
- **`--op` 只是声明面的第一片**：`gemm|reduce` 可选，reduce 单长度（`--dims L 0 0`，其余维
  清零以免进账本键）；`conv` 在 parse 阶段直接拒绝——它的声明需要六维（H,W,C,Kh,Kw,outC），
  而 CLI 只暴露三个，接受它等于在隐式默认上求解（R1.2 结构完整性）。
- **conv/reduce 的可用执行仍缺**：本轮只做"显式标记不支持"，没有补上卷积/归约内核生成——
  那是 G1（R1.1 KML 前端）的活；`solve` 的 roofline 模型对二者本已成立，故
  `solve --op reduce` 允许解、但明确打印"model-only，无源产出"。
- **平台边界**：`src/runner_ocl.cpp` 本轮在 Apple 主机以 `-fsyntax-only -DUK_HAS_OCL`
  编译检查通过，但**未在 Intel 真机上链接/运行**；AMD 侧拒绝靠生成物自拒（文本级由
  acceptance 断言），其 hipcc 真编译路径本轮未执行。Windows/ROCm 主机的复跑仍待做。
- **回归位的诚实分级**：谓词与 CLI 门有 ctest 回归位（摘掉即红，行号已录）；
  runner 入口门**没有** ctest 覆盖（`run_metal`/`run_ocl` 需真设备，acceptance 在 CI
  侧调不到），它的回归位是 `probe/uk_op_refuse_probe.mm` —— 这一点在本目录里明确写下，
  免得日后误当作"全链路有测试"。
