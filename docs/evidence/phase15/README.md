# Phase 15 证据产物（docs/evidence/phase15/）

G1 执行半边的 **AMD 收尾**：conv/reduce 的 HIP 可执行程序 + 能力面翻转 + RX 6600M（gfx1032）真机闭环。
转录二进制锚：`binary-md5.txt`（unikern.exe `535c3c9e4a53459866459661155ae403`、
unikern_acceptance.exe `980cc3ca6c88f8d04aa57bad471908b9`；ROCm 7.1 hipcc / HIP 7.13.99004，
`--offload-arch=gfx1032`）。

| 文件 | 生成命令 | 证明的事 |
|------|----------|----------|
| phase15-real-device-runs.txt | 两条门禁命令逐字（见文件内 `$` 行） | conv（`--kml samples/conv.kml`）与 reduce（`--op reduce --dims 1048576 0 0`）首跑 verify=PASS max_rel=0 → **LEDGER COMMIT**（ε=\\|C−T\\| 实算），同键二次 run **LEDGER HIT**；gemm 回归（新账本）同规律；三条 UNIKERN_RESULT `backend=amd`，perf 字段分别为 `64x64x32x3x3x64` / `1048576` / `4096x4096x4096` |
| phase15-compile-and-gemm-identity.txt | emit + `hipcc … --offload-arch=gfx1032` + `diff` | 生成的 conv/reduce 程序**真编译**（rc=0、0 诊断）+ E10 审计（生成源零反斜杠）+ **gemm 程序逐位不变**（fresh-solve vs fresh-solve 的 diff 为空；MSL/OpenCL 源同样不变） |
| phase15-arm-probes.txt | `python probe/phase15_arm_probes.py` | 两条新腿逐一摘除即红（下表），收尾残留 0 + ctest 绿 |
| phase15-ctest.txt / phase15-acceptance-sections.txt | `ctest --test-dir build --output-on-failure` / acceptance 直跑 | 2/2 全绿；`[R15]` 段在场，`[R10.2]/[R10.3]/[R13]` 呈 amd=covered 的三向期望 |
| emit/uk_amd_conv.cpp、uk_amd_reduce.cpp、uk_amd_gemm.cpp | `solve … --emit`（`git add -f`，撞 `uk_*.cpp` 忽略模式） | 三份程序本体：conv 只含 `uk_conv`、reduce 只含 `uk_reduce_part/uk_reduce_sum`、gemm 只含 `uk_gemm`；同带 R5/R8 横幅 + fp + toolchain + constants_v + placement |
| emit/uk_apple_conv.metal、uk_apple_reduce.metal、uk_intel_conv.cl、uk_intel_reduce.cl | 同上 | Apple/Intel 侧在本次翻转后不变（回归对照） |

## 两条新腿的带弹性证明

| 摘掉的腿 | 转红的断言（acceptance.cpp 行号，本轮坐标） |
|----------|-------------------------------------------|
| HIP 按算子分派（conv 落回 gemm 程序） | `hc.find("__global__ void uk_conv")…` @ 1317（[R15]） |
| `op_support_gap` 退回 gemm-only | `op_support_gap(*pr.first).empty()` @ 305（[R1.1/2]；[R15] 的 conv/reduce 能力断言同样依赖它） |

## 关键事实与边界（如实记录）

- **真机闭环的是 AMD**：conv/reduce 在 RX 6600M 上 verify=PASS max_rel=0 → COMMIT → HIT；
  reduce 的 ε_rel=0.69（模型在这条内存-bound 负载上本来就近），conv 的 ε_rel≈68–71 与
  Phase 12/13 的量级一致（HCS 声明 flops 对直白内核的缺口，模型常数精度，非运行器缺陷）。
- **gemm 逐位不变的双重证据**：① 生成源 diff 为空（fresh-solve 对 fresh-solve；
  注意 ledger-HIT 重建的解不带 shard 参数，会让 R8 行多出 "(defaults: …)" 后缀——
  这是账本形状的既有属性，不是本轮代码生成差异，转录里注明了口径）；
  ② 真机 run 的 UNIKERN_RESULT 字段与 Phase 8 同形（PASS max_rel=0，perf=4096³）。
- **runner 门与 CLI 门自然让位**：HIP conv/reduce 不再有 `op_unsupported` 拒绝产物；
  layout 是共享谓词剩下的唯一拒绝轴（[R15] 断言其拒绝产物仍在）。原 rc=2 op-拒绝臂
  相应改为"不可用后端诚实失败"臂（`--vendor apple` on Windows：rc=1 RUN FAILED，
  无 UNIKERN_RESULT、无 LEDGER COMMIT；macOS 上 apple 可用，故该臂为 _WIN32-only）。
- **out-of-band 编译证明的是"可编译"**，真机 run 才证明"可运行"——两者都在本转录里，
  但别把前者读成后者。
