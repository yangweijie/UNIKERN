# Phase 10 证据产物（docs/evidence/phase10/）

Apple 真机验证（G4 Apple 半闭合 → **G4 全闭合**）的**命令输出原文转录**，
随仓库跟踪，fresh checkout 可直接比对，也可在同等环境（Apple M4 / macOS
26.6.2 arm64 + CommandLineTools SDK + Metal 4 运行时 JIT）按下列命令原样
复跑。数值为运行时实测，逐次略有波动属正常。

本目录为 2026-10-27（本机 20:28 一轮）**全量重采**结果；`docs/acceptance-mvp.md`
与 `task_plan.md` 的 Apple 数字均以本目录转录为准（口径：ε_rel = |C−T|/T，
是**比值**不是百分数）。

| 文件 | 生成命令 | 对应项 |
|------|----------|--------|
| phase10-environment.txt | `./build-apple/unikern backends` + 工具链事实核实 | R10 availability 真探测（apple=yes）、工具链事实（Xcode.app 存在但无 SDK，Metal 链接自 CLT SDK） |
| phase10-emit.txt | `./build-apple/unikern solve --hcs samples/hcs_apple.json --emit /tmp/uk_emit_p10` | R5.1/R5.3 Apple 源生成（uk_apple.metal 61 行，含 HCS 指纹 + toolchain + 修复后的 R8 honesty 横幅） |
| emit/uk_apple.metal | 同上命令的产物本体 | R5 Apple 生成源码原文（与转录内嵌内容逐字节一致，可直接喂给 Metal JIT） |
| phase10-verify.txt | `./build-apple/unikern verify --hcs samples/hcs_apple.json --vendor apple --dims 512 512 512` | R6 真机差分对拍（runner 内固定 256³ CPU 参考比对，verify=PASS max_rel=0）+ 512³ 计时 + UK_DEVICE 事实 |
| phase10-run.txt | `./build-apple/unikern run --hcs samples/hcs_apple.json --vendor apple --dims 512 512 512 --ledger <db>`（首次，空账本） | R6.3/R7.3 真机实测回写（LEDGER COMMIT，ε=\|C−T\| 实算） |
| phase10-hit-run.txt | 上条**再次执行**（同键） | R7.2 账本命中复用记录解（LEDGER HIT + 再次 COMMIT 为设计行为） |
| phase10-calibrate.txt | `./build-apple/unikern calibrate --hcs samples/hcs_apple.json --ledger <db>` | R7.3 场景 A 诚实门控（默认拒绝，rc=3） |
| phase10-calibrate-forced.txt | 上条加 `--allow-unified-calibrate` | 显式强制后产物（9.097 GB/s 为主机调度伪影，**非带宽测量**） |
| phase10-ctest.txt | `ctest --test-dir build-apple --output-on-failure` | 构建全绿（2/2） |
| phase10-gate-arm-probe.txt | `ctest` 绿态 + 摘掉门控后 `./build-apple/unikern_acceptance` 转红 | R7.3 场景 A 校准门控的**带弹性探针**（含 POSIX `system()` wait 状态口径与 rc 不具判别力的如实记录） |
| src/runner_metal.mm | 进程内 Metal 执行 runner（源码本体，非转录） | Phase 10b 产物：对拍/计时/UNIKERN_RESULT 契约与 HIP/OCL runner 同构 |

注意：账本二进制（ledger.sqlite3）不入库（.gitignore 口径），账本内容以
运行输出转录为准，二进制可由上表命令复跑重建。`build-apple/`、`uk_run/`
与 `probe/` 下的可执行文件同样被 .gitignore 忽略（探针源码 `probe/uk_metal_probe.mm` 入库）。

## 关键事实与边界（如实记录）

- **设备事实经 API 实测**：UK_DEVICE name=Apple M4、host_unified=1、
  max_threads=1024。HCS 的 compute_units=40 为声明值——本 SDK 的
  MTLDevice 无 CU 数量 API，不照抄平台标签。
- **对拍口径与 AMD/Intel 一致**：runner 内部固定用 256³ 与标量 CPU 参考做差分
  （容差 2⁻⁵），`max_rel=0.000000e+00`；`--dims 512 512 512` 只决定计时负载的
  形状，不参与对拍判定。
- **ε 如实报告（比值口径）**：本次重采两轮 LEDGER COMMIT 读数
  `predicted_ms=0.0103244` 对 `measured_ms=1.27091` → **ε_abs=1.26059，ε_rel=122.097**；
  同键 HIT 后再测 `measured_ms=0.838425` → **ε_abs=0.8281，ε_rel=80.2078**。
  即 ε_rel 落在此区间内，本机同尺寸同负载的抖动量级约 1.5 倍，不构成趋势结论。
  量级本身（约 10² 倍，而非"100%"）属预期：预测项按 HCS 声明的
  `flops_gflops=26000` 取峰值，而 512³ 实测内核吞吐只有
  2·512³/0.494375ms ≈ **0.54 TFLOPS**（e2e 1.27ms 则 ≈0.21 TFLOPS），
  残留即"声明峰值 vs 有效计算率"缺口（本 MVP 的 roofline 常数不拟合 Apple M4），
  如实标注而非粉饰。
- **calibrate 场景 A 门控（本阶段新增的诚实机制）**：场景 A 零拷贝语义下
  `measured_ms − kernel_ms` 是主机端调度开销（GPUStartTime/GPUEndTime
  跨度含队列提交），不是链路拷贝时间；用它反推 link_bw_gbps 会产出
  与真实统一内存带宽相差两个数量级的"精确伪影"，比诚实 SKIP 更危险。
  默认拒绝（rc=3），仅 `--allow-unified-calibrate` 显式强制，且强制
  产物如实标注为伪影。
- **门控的正确性有实测支撑，不只是说辞**：强制校准把 `link_bw_gbps` 从
  600 改到 9.097（constants_version 1→2），而 `PREDICTED demo c_ms 5.28611 →
  5.28611` **逐位不变**——说明该常数在统一内存（场景 A）求解路径上确实不被使用
  （`src/solver.cpp:75`：拷贝项仅在 `x.path != Path::Unified` 时计入），改它不会污染 Apple 的预测。
- **伪影读数本身也抖**：同一份账本，强制校准上一轮得 5.986 GB/s、本轮得
  9.097 GB/s（`usable_rows=1`）——两次都"精确"却差 1.5 倍，正是"主机调度开销
  不是链路带宽"的直接证据，也是默认拒绝的理由。
- **LEDGER HIT 后再次 COMMIT 是设计行为**（非 bug）：HIT 复用账本记录
  的解（R7.2，跳过搜索），但内核照常运行，验证 PASS 后以新测量值
  更新账本行（R7.3），ε 随之重算。
