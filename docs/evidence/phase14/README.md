# Phase 14 证据产物（docs/evidence/phase14/）

执行面补齐 · **Intel（OpenCL）** 的 conv/reduce：生成、能力面、runner 分派与**本机可得的验证**。

| 文件 | 生成命令 | 证明的事 |
|------|----------|----------|
| phase14-ocl-and-regression.txt | `probe/phase14_capture.sh` | 三算子的 `uk_intel.cl` 全部通过 **clang 的 OpenCL 前端**（`-x cl -cl-std=CL1.2`：rc=0、0 条诊断），且负测（未声明标识符）rc=1 —— 这道检查是会红的；覆盖表变为三向（amd unsupported / apple+intel covered）；`run --vendor intel --kml conv` 在本机 rc=1「无 Intel 设备」（**不是**能力拒绝，账本不创建）；Apple 三算子回归仍 PASS |
| phase14-arm-probes.txt | `python3 probe/phase14_arm_probes.py` | 三条新腿逐一摘除即红（下表），收尾残留 0 + ctest 绿 |
| phase14-ctest.txt | `ctest --test-dir build-apple --output-on-failure` | 当前树 2/2 全绿（含新增 `[R14]`） |
| `probe/phase14_capture.sh`、`probe/phase14_arm_probes.py` | — | 转录脚本与带弹性探针本体 |

## 三条新腿的带弹性证明

| 摘掉的腿 | 转红的断言（acceptance.cpp 行号，本轮坐标） |
|----------|-------------------------------------------|
| OpenCL 覆盖三算子（`ocl_op_gap` 退回全局谓词） | `ocl_red.find("__kernel void uk_reduce_part")…` @ 1042 |
| Intel 的 per-backend 覆盖（`capability_gap` 退回默认） | `c.primitive_covered && c.reason.empty()` @ 1303 |
| OpenCL 按算子分派（conv 落到 gemm 宏） | `oc.find("__kernel void uk_conv")…` @ 1249 |

## 关键事实与边界（如实记录）

- **本机能给的验证是真的，但不是全部**：生成的 OpenCL C 过了 **clang 的 OpenCL 前端**
  （真语法/类型检查，负测可红），runner 的 host 侧（plan/launch/verify/计时分支）过了
  `-fsyntax-only -DUK_HAS_OCL` 的编译检查；**运行没验**——本机没有 Intel 设备，
  `run --vendor intel` 在这里以「no Intel GPU platform/device found」收场（rc=1，仍不建账本）。
  这条边界在 `phase14-ocl-and-regression.txt` 里当场可见，别把它读成"已真机验证"。
- **三向能力面**：`ocl_op_gap()` 与 `msl_op_gap()` 同构（layout 白名单 + 三算子），
  `BackendIntel::capability_gap()` 覆盖后，覆盖表呈 `amd unsupported / apple covered /
  intel covered`。「覆盖 ≠ 可运行」这件事由 toolchain 轴单独承担（本机 intel=absent），
  两个维度不混谈。
- **verify 与计时都复用同一套口径**：conv 的 verify 用 32×32×8/3×3/16 通道的同一结构，
  reduce 用 2^-6×2^20=16384（任意次序精确），CPU 参考仍是 `src/reference.cpp` 那一份——
  与 Metal 路径共用真值，不另写一份。
- **Phase 13 的两条旧结论已被本阶段推翻**（保留原文、标记取代）：
  ① "AMD/Intel 对同一份声明 rc=2 拒绝"——现在只有 AMD 如此，Intel 会走到运行期并以
  「无设备」失败；② "conv/reduce 目前只有 MSL 实现"——OpenCL 源也实现了。
  证据分别是 `phase13-conv-reduce-device.txt`（当时为真）与本目录的
  `phase14-ocl-and-regression.txt`（当前为真）。
- **仍未做**：AMD（HIP 程序）的 conv/reduce —— 本机无 hipcc，连语法都验不了，
  留 Phase 15 并要求 ROCm 主机复跑；Intel 真机的运行时验证；conv/reduce 的调优与
  浮点次序鲁棒性验证。
