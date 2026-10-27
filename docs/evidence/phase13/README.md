# Phase 13 证据产物（docs/evidence/phase13/）

G6（Metal 工作集上限）与 Phase 13（**Apple 先行**的 conv/reduce 执行面）的命令输出原文转录。
所有命令在 Apple M4 / macOS 26.6.2 arm64 主机实跑；CLI md5 随录；毫秒读数逐次浮动。

| 文件 | 生成命令 | 证明的事 |
|------|----------|----------|
| phase13-g6-working-set.txt | `probe/uk_ws_probe.mm`（设备探针） | 实测 `recommendedMaxWorkingSetSize=13631488000`（12.7 GiB），而旧口径允许到 `0.85×declared=29205777612`（27.3 GB）——旧门放过的最大载荷是设备实际上限的 2.1 倍。arm A（20.4 GB，落在两界之间）→ `working_set_refused`，且**容量门沉默**；arm B（31.3 GB）→ 仍是 `stream_refused`（容量门优先级不变）；arm C（64³）→ 正常 PASS 并打印实测值 |
| phase13-conv-reduce-device.txt | `probe/phase13_capture.sh` | conv / reduce 由 KML 声明驱动在 M4 上 **verify=PASS max_rel=0** → `LEDGER COMMIT` → 同键 `HIT` 再 COMMIT；gemm 回归（同形状读数与 Phase 12 同量级）；AMD/Intel 对同一份声明 rc=2 拒绝且**账本文件不创建**；异构产出（`uk_apple.metal` 40 行含 `uk_conv`，另两个是 8/4 行的点名拒绝文件） |
| phase13-arm-probes.txt | `python3 probe/phase13_arm_probes.py` | 五条新腿逐一摘除各自转红（见下表），收尾残留标记 0 + ctest 全绿 |
| phase13-ctest.txt | `ctest --test-dir build-apple --output-on-failure` | 当前树 2/2 全绿（acceptance 含新增 `[R13]` 段与改写的 `[R10.2]/[R10.3]` 异构臂） |
| `probe/uk_ws_probe.mm`、`probe/uk_op_kernel_probe.mm`、`probe/phase13_capture.sh`、`probe/phase13_arm_probes.py` | — | 探针与转录脚本本体（编译产物不入库） |

## 五条新腿的带弹性证明

> 行号是本轮坐标；引用请以"摘掉的腿 + 断言文本"为准（漂移处理见 phase11/phase12 的 README）。

| 摘掉的腿 | 转红的断言 / 设备读数 |
|----------|------------------------|
| MSL 覆盖三算子（`msl_op_gap` 退回全局谓词） | `msl_red.find("uk_reduce_part")…` @ 1044 |
| Apple 的 per-backend 覆盖（`capability_gap` 退回默认） | emit 臂 `rc == 0` @ 1080（不再异构产出） |
| `--emit` 按"任一后端覆盖"放行 | 同上 @ 1080（reduce 的 emit 被整体拒绝） |
| conv 取 `uk_conv` 管线 | 设备探针：`UNIKERN_ERROR what=no_function name=uk_gemm`，`both_pass=0`（reduce 不受累） |
| reduce 线程组上界（65..1024） | 设备探针：`verify=FAIL max_rel=1.000000e+00`（conv 不受累） |

## 关键事实与边界（如实记录）

- **G6 的判别证据是"带内拒绝"，不是"摘掉报警"**：把这道门直接摘掉会让一个 20 GB 载荷
  继续走到分配阶段（设备上限 12.7 GB），那既可能压垮机器、量到的也只是分配失败，
  不是判定。因此这里的证据是三条臂：落在两界之间的载荷被**新门**拒绝且旧门沉默、
  超出容量界仍由旧门拒绝、正常载荷照常跑通并且 UK_DEVICE 行打印实测上限。
- **两道门不叠乘**：`0.85×声明容量` 与 `recommendedMaxWorkingSetSize` 各自独立成门、
  各自报名字。乘起来（0.85×0.75×容量）会重复打折，且说不清是谁挡的。
- **Phase 13 先把能力面拆成 per-backend**：`IBackend::capability_gap()`（默认沿用共享谓词），
  Apple 覆盖为 gemm/conv/reduce；CLI 的 run 门按**所选后端**判，`--emit` 按**任一后端覆盖**判
  （此时产出会真实地异构：一个真内核 + 两份点名拒绝），覆盖表随之出现异构行。
  HIP/OCL 的谓词与产物没变，仍对 conv/reduce 点名拒绝。
- **conv 是 implicit-GEMM 的直白实现**（valid padding，一线程一输出），因此读数离 roofline
  很远（ε_rel 334/244）——这是对"未调优内核"的诚实测量，不是模型缺陷；没有假装它是优化实现。
- **reduce 的 verify 数据是刻意选的**：2^-6 × 2^20 = 16384，任意求和次序在 fp32 下都精确，
  因此 max_rel=0 证明的是**结构正确**，不是浮点次序鲁棒性。这一条写进源码注释，而不是
  让 0 去暗示更多。CPU 参考与 runner 共用 `src/reference.cpp`（一份"真值"，不是两份）。
- **本轮自捉（值得记）**：reduce 首次真机运行 `verify=FAIL max_rel=1.0`——根因是 pass-1 的
  线程组宽度取了 4096，超过 `maxTotalThreadsPerThreadgroup`（1024），Metal 静默不执行，
  读到全零。修法是组宽取 `min(p,64)`；同时补了命令缓冲状态检查作为额外网。
  但要如实说明：**这次抓到它的不是状态检查，是 verify**（该失败模式下缓冲仍报 Completed），
  状态检查只是纵深防御。诊断链是 verify → 读源码 → 定位 dispatch 参数。
- **平台边界**：conv/reduce 目前只有 MSL 实现；AMD（HIP 程序）与 Intel（OpenCL 源）仍为
  gemm-only，执行侧按 per-backend 谓词拒绝。`--vendor amd/intel` 的 conv/reduce 真机复跑
  仍待 ROCm/oneAPI 主机；`runner_ocl.cpp` 依旧只有 `-fsyntax-only` 编译检查。
- 未做：conv/reduce 的调优内核（分块/tiling）、reduce 的浮点次序鲁棒性验证、
  benchmark 级别的对比（本阶段的验收是"可执行 + 对拍 + 账本闭环"，不是性能）。
