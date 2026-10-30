# Phase 29 证据 — 校准旋钮必须过设备这一关（真缺陷，已修并入库）

本机：Windows 11 + AMD **RX 6600M（gfx1032）**，ROCm 7.1。日期 2026-10-28。

## 缺陷是这么暴露的

Phase 28 的 tile=16 曲线在 C=64 上 6 格全部"没有结果行、rc=4"。直接复现：

```
UK_CONV_TILE=16 UK_CONV_OCB=8 ./uk_run/phase28/conv_c64.exe
UNIKERN_INFO conv_plan ocb=8 tile=16 smem=82944 source=env
UNIKERN_ERROR what=launch verify msg=invalid argument
rc=4
```

自动选档路径会检查 `cand.smem > dp.sharedMemPerBlock` 并打印 `conv_tile_skip`，
但 **env 旋钮分支在 `hipGetDeviceProperties` 之前就 `return p`** ——
钉住的方案完全绕过设备这一关。后果有两层：

1. 失败点离原因很远（死在 `launch verify`，错误串里既没有旋钮名也没有被越过的上限），
   看日志的人第一反应是"内核写坏了"，而不是"这台卡装不下这个 tile"；
2. 更危险的是**静默的错读**：如果脚本只看 `kernel_ms` 是否存在，就会把这一格当成噪声丢掉；
   反过来若某个组合"能跑但选错了路径"，钉住实验的结论就整体失真。

## 修法

旋钮分支里补上同一条设备检查（沿用自动路径的判据，不另立标准）：
越限就**响亮拒绝这个 pin**——打印请求字节数、设备上限、以及"改走全局加载路径、OCB 的 pin 保留"，
再把 tile 归零；`hipGetDeviceProperties` 本身失败同样拒绝钉住的 tile（保守到全局路径）。
顺序有意为之：**ERROR 行先于 `conv_plan` 行**，扫描脚本才会在读到"调整后的配置"之前先读到原因。

## 三条对照（`knob-refusal.txt`，全部 rc=0、`verify=PASS max_rel=0.000000e+00`）

| 对照 | 期望 | 实测 |
|---|---|---|
| C=64 钉 tile=16（82944 B > 65536） | 必须响亮拒绝并仍能跑 | `what=knob_tile_unfit tile=16 smem=82944 sharedMemPerBlock=65536` → `conv_plan ocb=8 tile=0 smem=0 source=env`，`kernel_ms=41.817249` |
| C=64 钉 tile=8（25600 B 装得下） | **不许**误报 | 无 ERROR，`conv_plan ocb=8 tile=8 smem=25600 source=env`，`kernel_ms=41.358890` |
| C=32 钉 tile=16（41472 B 装得下） | 行为不变 | 无 ERROR，`kernel_ms=7.050036`（历史带内） |

**"总是拒绝的检查"和"从不拒绝的检查"一样没用**，所以第二条是这次修改的主要防误报证据。

## 门禁（`arm-removal.txt`，`probe/phase29_knob_arm_probe.py` rc=0）

`[R20]` 两条新腿（计数先取自发射产物再写断言）：

- `count_of("what=knob_tile_unfit") == 1`
- `hc.find("knob_tile_unfit") < hc.find("source=env")`（顺序断言）

摘除控制两条全红：

1. 把拒绝 token 改名 → `count_of(...) == 1` 红；
2. 在检查之前多打一条含 `source=env` 的行 → **计数腿仍绿，只有顺序腿红**
   （`[R6.6]` `tests/acceptance.cpp:1712`）——这条正是"新腿能被单独触发"的证明，不是重复覆盖。

复原 byte-identical，末轮 `ctest` 3/3 绿。

## 新的发射基线

`conv-hip-knob-check.txt`（**542 行**，`sha256=599a4e7183982b7101ed9441a8582bec59f9ae23bf450dffc8323a2bf79e3f18`），
与 `docs/evidence/phase26/conv-hip-single-plan.txt` 的 `diff` 恰为 **+25 / −0** 行
（全部落在 env 分支这一段检查上）。gemm 447 行、reduce 239 行未变（跨算子无回归）。
旧基线保留并标历史时点。

## 与 Phase 28 的关系

Phase 28 的 C=64 缺格由这个缺陷造成；修好之后 C=64 在 tile=16 下**仍然装不下**（设备只有 64 KB/CU），
那不是缺陷而是硬件边界——所以 tile=16 的曲线在 C=64 处天然断点，
可比对的完整曲线是 tile=8 那条（`docs/evidence/phase28/sweep-tile8.txt`）。
