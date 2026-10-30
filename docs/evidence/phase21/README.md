# Phase 21 证据 — `uk_conv` 空间维共享内存 tile + 联合 (tile, OCB) 选档

本机：Windows 11 + MinGW g++ 13.2.0 + AMD **RX 6600M（gfx1032）**，ROCm 7.1。日期 2026-10-28。
靶子＝Phase 20 的 19.062 ms @256²（通道分块），基线＝Phase 19 的 124.57 ms（一线程一输出）。

## 复现命令

| 文件 | 命令 | 证什么 |
|------|------|--------|
| `tiled-sweep.txt` | `./build/unikern.exe run --hcs samples/hcs_amd.json --kml workloads/conv/<shape>.json --workdir uk_run/phase21 --ledger uk_run/phase21/t<i>.sqlite3`（三形状 ×3 次 + 一次完整候选网格打印） | 每个形状的选档过程与结果；小形状"没有任何组合能填满设备"的实测证据 |
| `r20-arm-removal.txt` | `python probe/phase21_conv_arm_probe.py docs/evidence/phase21/r20-arm-removal.txt` | `[R20]` 四条腿逐一摘除全红：容量改用抄来的标称 14 / occupancy 探针指向另一内核变体 / `__syncthreads()` 移到边界 return 之下 / LDS tile 索引错一个元素 |

## 实测（kernel_ms，同一二进制重复三次）

| 形状 | Phase 19 基线 | Phase 20 通道分块 | **Phase 21 空间 tile** | 自动选中的方案 | 累计 |
|------|---------------|-------------------|------------------------|----------------|------|
| conv 256×256×32 k3×3 Oc64 | 124.571 | 19.062 | **7.153 / 7.167 / 7.175** | `ocb=8 tile=16 smem=41472` | **17.4×**（337.5 GFLOPS） |
| conv 128×128×16 k3×3 Oc32 | 2.439 | 0.547 | **0.2866 / 0.2882** | `ocb=8 tile=16 smem=20736` | **8.5×**（524.9 GFLOPS） |
| conv 32×32×8 k3×3 Oc16 | 0.01396 | 0.01258 | **0.01070 / 0.01338 / 0.01304** | `ocb=1 tile=0`（"nothing fills the device"） | 噪声内（见下） |

32² 一档：kernel 时间在三形状里最小（10.7–14.2 µs，跨度约 3.5 µs），与 Phase 20 的 12.6/12.8 µs
**同一量级同一分布**，所以"退化"不成立、"改进"也不成立——这个尺寸上单次差值小于重复散布，
只能报"分辨不出差别"。这也是为什么小形状必须退回不 tile 的路径：任何 tile 都会把块数压到
1–4 个（256–1024 线程），而设备自报可常驻 21504 线程。

## 做法与为什么这样选

- 一个线程块 `T×T` 线程，先把带 halo 的输入 tile `(T+Kh-1)×(T+Kw-1)×C` 用全块协作搬进
  `__shared__`（动态共享内存，尺寸由 `uk_conv_smem` 一处算出），**`__syncthreads()` 必须在
  边界 return 之前**（否则部分线程跳过屏障＝HIP 上未定义行为）。
- 之后每个线程仍按 `c → kh → kw` **升序**累加，只是取值来源从全局内存换成 LDS。
  因此结果逐位不变——**max_rel 在所有形状、所有 tile、所有 OCB 上恒为 0.000000e+00**，
  这是"只改访存、不改算术"最直接的仪器证据（9 次结果行 + Phase 20 的 15 次，共 24 次）。
- **(tile, OCB) 必须联合选**：第一版只按共享内存是否装得下选 tile，结果在 32² 上选了 `tile=16`
  （4 个块＝1024 线程）→ 0.0157 ms，比 Phase 20 的 0.0126 慢 25%。实测把这条教训写进代码注释，
  改成枚举 `tile∈{16,8,0} × ocb∈{8,4,2,1}`，用设备自报的
  `hipOccupancyMaxActiveBlocksPerMultiprocessor × multiProcessorCount` 判"这个组合填不填得满"，
  在所有填得满的组合里取复用度（`tile×ocb`）最大者；一个都填不满时退回 `tile=0/ocb=1`
  （全局加载、最多线程）。三个形状全部选中了各自的实测最优组合。
- 保留 `UK_CONV_TILE` / `UK_CONV_OCB` 两个校准旋钮：上面的规则就是用它们扫出来的。

## 回归与规模

- gemm/reduce 未受影响（同批实测）：gemm 512³ `verify=PASS max_rel=0 rc=0`、
  reduce 1048576 `verify=PASS max_rel=0 rc=0`；三者 emit 行数 gemm **447**、reduce **239**（不变）、
  conv **372 → 506**（Phase 18 记的 253/275/425 与 Phase 20 记的 372 都是历史时点）。
- `ctest --test-dir build` 2/2 绿；探针 rc=0，复原 byte-identical。

## 仍开放

- conv 337 GFLOPS 距同机寄存器分块 gemm 512³ 的 2275 GFLOPS 仍约 **6.7×**（Phase 20 时是 18×）；
  再往上要的是隐式 GEMM 的完整分块（权重也进 LDS/SIMD 矩阵乘路径），属另开一相。
- D8（差分参考口径）未决；Intel 真机 conv/reduce 需那台机器；V1 的 R10.2 F 约束合流。
