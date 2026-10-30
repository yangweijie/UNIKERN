# Phase 20 证据 — `uk_conv` 输出通道分块 + 由设备自报容量选档

本机：Windows 11 + MinGW g++ 13.2.0 + AMD **RX 6600M（gfx1032）**，ROCm 7.1。日期 2026-10-28。
靶子来自 Phase 19 基线（`docs/evidence/phase19/conv-baseline.txt`）。

## 复现命令

| 文件 | 命令 | 证什么 |
|------|------|--------|
| `ocb-sweep.txt` | `for ocb in 1 2 4 8 auto: UK_CONV_OCB=$ocb ./build/unikern.exe run --hcs samples/hcs_amd.json --kml <shape>.json --workdir uk_run/phase20 --ledger uk_run/phase20/s.sqlite3` | 三个尺寸 × 四个分块档 + 自动档的实测 kernel_ms；自动档选中值与每次探测的 `threads/resident_capacity/cu` 都打在 `UNIKERN_INFO conv_probe` 行里 |
| `r20-arm-removal.txt` | `python probe/phase20_conv_arm_probe.py docs/evidence/phase20/r20-arm-removal.txt` | `[R20]` 三条腿逐一摘除全红（rc=3221226505=assert abort），复原 byte-identical，末轮 ctest 2/2 绿 |

## 实测结果（kernel_ms，越小越好）

| 形状 | OCB=1 | OCB=2 | OCB=4 | OCB=8 | **自动档选中** | vs Phase 19 基线 |
|------|-------|-------|-------|-------|----------------|------------------|
| conv 32×32×8 k3×3 Oc16 | **0.012580** | 0.026020 | 0.030780 | 0.043107 | ocb=1 → 0.012750 | 0.013960 → **1.10×** |
| conv 128×128×16 k3×3 Oc32 | 2.361320 | 1.254465 | 0.728518 | **0.546550** | ocb=8 → 0.555560 | 2.438981 → **4.39×** |
| conv 256×256×32 k3×3 Oc64 | 124.426172 | 64.877631 | 35.740997 | **19.061880** | ocb=8 → 18.987881 | 124.570947 → **6.53×** |

吞吐（由同批 FLOP 与实测 kernel_ms 直接算出）：256² 从 **19.4 → 126.7 GFLOPS**，128² 从
**61.9 → 276.3 GFLOPS**。误差方向上分块**收益随尺寸增大**，而 32² 这种小形状分块反而变慢
（线程数不够填满 14 个 CU）——这就是分块档不能写死的原因。

## 为什么不是"写死 8"或"写死阈值"

选档用设备自己报的容量：`hipOccupancyMaxActiveBlocksPerMultiprocessor(每个档) ×
hipDeviceProp::multiProcessorCount × 64 线程/块` = 该机可常驻线程数；从 {8,4,2,1} 里
取**仍能填满设备的最大档**，填不满就退回更小的档（更多线程）。三个尺寸的
`threads / resident_capacity / cu` 全部逐次打印在 `ocb-sweep.txt`，选中值打在
`UNIKERN_INFO conv_ocb=`，所以选择过程可审计，不需要任何从别处抄来的阈值。
探针失败时退回 OCB=1（对任何设备都正确）并打 `UNIKERN_ERROR what=occupancy_probe`，
不静默。

`UK_CONV_OCB`（1/2/4/8）是这套数据自己的校准旋钮：本表就是用它扫出来的，留着它
才能让"选档规则"以后还能被复测而不是被信仰。

## 次序不变性（本 Phase 真正的硬事实）

**15 次运行、四个分块档、三个尺寸，`max_rel` 全部是 0.000000e+00。** 原因不是巧合：
每个通道的累加次序（`c → kh → kw` 升序、每步一次 FMA）与 OCB 取值无关，
分块只改"一次 X 取值服务几个通道"，不改任何加法顺序。因此这条改写是可证明安全的
性能改动，而 `[R20]` 臂把整个 FMA 嵌套按字节钉住（含缩进），谁将来"顺手优化"成
倒序或重组合，测试当场红。

## 控制过程中抓到的一次断言失效

`[R20]` 初版用 `hc.find("dp.multiProcessorCount") != npos` 判"容量来自设备自报"。
第一条腿（把容量式子里的 `dp.multiProcessorCount` 换成抄来的标称 `14`）**没有变红**——
因为这个符号在程序里出现两次（容量式 + `cu=` 审计打印行），存在性判据被打印行满足了。
这与 Phase 18 的 `checks=` 断言失效是同一类错误（见 E21/E24）。改成出现次数判据
（`== 2`）后三条腿全部逐一摘除即红，转录见 `r20-arm-removal.txt`。

## 仍然开放

- 分块只做到了通道维；**空间维/共享内存 tile 未做**：调优后的 conv 仍只有 126.7 GFLOPS，
  同机寄存器分块的 `uk_gemm` 512³ 是 2275 GFLOPS（约 18× 差距）。
- 生成的 conv 程序行数从 275 → **372**（Phase 18 记的 253/275 系列锚点再次演进，历史读数不改写）。
- `UK_CONV_OCB` 只影响 conv；gemm/reduce 路径未变（reduce 回归检查同批 rc=0、max_rel=0）。
