# Phase 24 证据 — LDS bank padding（C → C+1）：假设被判负，已按字节回退

本机：Windows 11 + MinGW（GCC 13）+ AMD **RX 6600M（gfx1032）**，ROCm 7.1 app runtime。日期 2026-10-28。
靶子：Phase 21 的 tiled 内核 `tile=16 / ocb=8`，256² `kernel_ms` 七次读数带 **7.1276–7.1750 ms**。

## 要证的东西（跑之前写死，全文在 `task_plan.md` 的"Phase 24 预登记"）

Phase 23 把微块失败归因到"occupancy 与 bank"。本轮只查其中**可单独动手的那条腿**：
`uk_sX` 按像素的步长是 `C` 个字，块 16×16、gfx1032 用 wave32 ⇒ 一个 wavefront 的 32 条 lane =
同行 16 条（x 步长 `C`）+ 次行 16 条（行步长 `TW*C`）。256² 用例 C=32、TW=18：
`32 mod 32 = 0`、`18*32 = 576 mod 32 = 0` ⇒ 模型上 32 条 lane 命中同一 bank，
每次 LDS 取值付 32 倍串行，而每线程每输出要做 `C*Kh*Kw = 288` 次。

改法只动地址：每像素多分配 1 个字（`CP = C + 1`，padding 槽不参与算术），加载写 `hw*CP + c`、
计算读 `((ty+kh)*TW + (tx+kw))*CP + c`，`uk_conv_smem` 同一处规则算容量。
**本机没有 profiler**（`rocprof`/`rocprofv2`/`rocgdb` 不在 PATH，`therock/gfx103X-7.13.0/bin` 里也没有），
所以判据只能是 A/B 计时——正因如此才把四条判据先写死。

## 实测（`padded-sweep.txt`）

| 判据 | 预测 | 实测 | 判定 |
|------|------|------|------|
| ① 保留线 | 256² ≤ 5.97 ms | **7.135740 / 7.145340 ms**（在未加 padding 的 7.1276–7.1750 带内） | **未达** |
| ② 判别性控制 | C=16 的 128² 增益应明显小于 C=32 | 128² = 0.302160 / 0.295439 ms，对未加 padding 的 0.287 ms 反而**慢 2.9–5.3%** | 两边都没增益 ⇒ 预测得不到支持 |
| ③ 负对照 | 32² 走 `tile=0` 且留在 10.7–14.2 µs 分布内 | 32² 选档变成 **`tile=8 smem=3600 fills=device`**，实测 0.016357 / 0.020372 / 0.015565 ms（**慢约 1.5×**） | **对照移动**，本轮 32² 读数不能当作布局的孤立效应 |
| ④ 次序不变 | `max_rel` 恒 0 | 6 条结果行 + 1 条追踪行全部 `max_rel=0.000000e+00` | 成立 |

结论：**bank 冲突不是这只卡上这个内核的限制因素**。按判据①回退。

## 判据③为什么会动（这是本轮最有用的发现，不是失误）

`uk_conv_plan` 的接受条件是 `threads >= per_cu * dp.multiProcessorCount`，而 `per_cu` 来自
`hipOccupancyMaxActiveBlocksPerMultiprocessor(..., p.smem)` ——**共享内存字节数直接进入选档判据**。
把 `C` 改成 `C+1` 让 tile=8/32² 的 smem 从 3200 涨到 3600，可常驻块数下降，于是"网格 ≥ 可常驻容量"
这条谓词由假变真，选档器从后备路径跳到 tile=8。也就是说：

> 凡是改动 smem 公式的实验，都会顺带改动选档结果； tiny 形状上"只改布局"这个前提在选档器面前不成立。
> 下一轮同类实验必须用 `UK_CONV_TILE` / `UK_CONV_OCB` **把方案钉住**再测（旋钮本来就在），
> 否则读数是"布局 + 选档"的混合效应。

顺带一条独立于本假设的事实：容量谓词的方向是**反直觉**的——smem 变大 → `per_cu` 变小 → `cap` 变小 →
更容易"填满设备"。所以"装得下"与"填得满"两条轴在 tiny 形状上会互相打架（Phase 21 已记过一次联合搜索，
本轮记下它们的耦合方式）。

## 回退的举证（与 Phase 23 的对比）

Phase 23 回退后发射程序 508 行 vs Phase 21 记的 506，当时只能说"行为一致"。本轮把
`conv-hip-postrevert.txt` 当基线用上了：回退后重新发射
`sha256=8464d86cf3f07beb733a874a7cae9b0d538adb091d858e69f74b4b5a8e42bb28`，与基线文件
**`diff` 空输出**、`lines=508` ⇒ 这次是真正的逐字节回退。
`postrevert-confirm.txt`：两次真机 fresh-ledger run `kernel_ms=7.152313 / 7.152326 ms`、
`verify=PASS max_rel=0.000000e+00`、`migrations=0 checks=6`、rc=0；`ctest` 3/3 绿
（`[R20]` 的字节钉住在 padding 版下于 `tests/acceptance.cpp:2569` 当场变红（Phase 24 当时是 :1682），回退后自动复原——
先让旧钉住变红再决定去留，不是事后追认）。

## 下一手（不是决定，只是靶）

未解释的事实是：288 次 LDS 取值 × 32 倍冲突模型没有代价。看发射出的内循环，每步其实是
**1 次 LDS 取值 + 8 次 `Wt` 全局（L2）取 + 8 次 FMA** ——取指令数比 FMA 还多。
所以值得测的是把本块 `ocb × Kh × Kw × C`（tile=16/ocb=8/C=32 时 2304 个字 = 9 KB）的权重搬进
`__shared__` 或寄存器，而不是继续动 X 的布局。判据沿用：`kernel_ms ≤ 5.97 ms`、`max_rel` 恒 0、
**选档必须用旋钮钉住**、32² 作为负对照。

## 复现

```bash
# 加 padding 的那版已回退，无法直接重放；如需重做，按 README 描述的四处地址/容量改动实现后跑：
for k in conv_256x256x32_k3x3_o64 conv_128x128x16_k3x3_o32 conv_32x32x8_k3x3_o16; do
  ./build/unikern.exe run --hcs samples/hcs_amd.json --kml workloads/conv/$k.json \
    --workdir uk_run/phase24 --ledger uk_run/phase24/$k.sqlite3
done
# 回退后的字节等价性：
./build/unikern.exe solve --hcs samples/hcs_amd.json \
  --kml workloads/conv/conv_256x256x32_k3x3_o64.json --emit /tmp/p24 && \
  diff /tmp/p24/uk_amd.cpp docs/evidence/phase23/conv-hip-postrevert.txt
```
