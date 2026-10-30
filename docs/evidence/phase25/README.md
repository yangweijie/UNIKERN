# Phase 25 证据 — 设备共享内存事实 + (tile, OCB) 钉住矩阵：occupancy 这条腿也被判负

本机：Windows 11 + MinGW + AMD **RX 6600M（gfx1032）**，ROCm 7.1
（`hipcc HIP version: 7.13.99004-3309c6114a`）。日期 2026-10-28。
本轮**不改代码**：先用独立探针把设备事实测清楚，再用现成旋钮把方案钉住做纯测量。

## 一、pre-flight：`smem-preflight.txt`

独立程序 `probe/phase25_smem_probe.cpp`（不含 unikern 头文件，读数只来自设备）：

```
hipcc --offload-arch=gfx1032 probe/phase25_smem_probe.cpp -o uk_run/phase25/probe.exe
./uk_run/phase25/probe.exe            # 每档请求都先 hipFuncSetAttribute
./uk_run/phase25/probe.exe --noattr   # 整个进程一次都不 setattr（生成程序现在的处境）
```

设备自报：`sharedMemPerBlock = sharedMemPerMultiprocessor = maxSharedMemoryPerBlockOptin = 65536`，
`multiProcessorCount=14`、`warpSize=32`、`maxThreadsPerMultiProcessor=2048`、`regsPerBlock=131072`。

字节请求 → occupancy 的 `per_cu` 与实际 launch 结果（两种模式各 7 档，全 rc=0）：

| 请求字节 | 12800 | 41472 | 49152 | 50688 | 53248 | 64512 | 65536 |
|---|---|---|---|---|---|---|---|
| `per_cu` | **5** | 1 | 1 | 1 | 1 | 1 | 1 |
| launch/sync（有 setattr） | OK | OK | OK | OK | OK | OK | OK |
| launch/sync（**无 setattr**） | OK | OK | OK | OK | OK | OK | OK |

两条结论：

1. **Phase 21 计划里写的"48 KB 默认上限，需 `hipFuncAttributeMaxDynamicSharedMemorySize`"在这台机器上不成立**——
   65536 B 的动态共享请求在不做任何 opt-in 的进程里也 launch+sync 成功。那句是我未测就写下的平台断言，
   已在 Phase 21 表行与验收文档就地标注更正。
2. **现役最优方案 `tile=16 / C=32`（41472 B）每 CU 只常驻 1 个块 = 256 线程 = 线程槽的 12.5%**；
   要 5 块/CU 得把每块字节压到 12800（即 `tile=8 / C=32`）。这就是 Phase 25 要判的那条腿。

## 二、钉住矩阵：`pinned-matrix.txt`（256²×32 / Oc64，`kernel_ms`，全部 `max_rel=0.000000e+00`）

方案用 `UK_CONV_TILE` / `UK_CONV_OCB` 钉死，`conv_plan` 行必须带 `source=env`（Phase 24 的教训：
不钉住就读不到"只改布局/只改 tile"的孤立效应）。

| OCB | tile=16（1 块/CU，256 线程/CU） | tile=8（5 块/CU，320 线程/CU） | tile=8 相对 tile=16 |
|-----|------------------------------|------------------------------|--------------------|
| 8 | **7.129367 ms** | 9.638396 ms | **慢 35.2%** |
| 4 | 10.514896 ms | 13.543800 ms | 慢 28.8% |
| 2 | 18.079030 ms | 20.141341 ms | 慢 11.4% |
| 1 | 31.571097 ms | 31.416699 ms | 快 0.5%（差值在散布内） |

对照组（不钉，选档器自己挑）：`conv_plan ocb=8 tile=16 smem=41472 fills=device`，
`kernel_ms=7.150933 ms`——**选档器在八组钉住读数之上仍然选中了实测最优**，
Phase 21 的选档规则在比当时更宽的组合面上再次成立。

### 预登记判据的结果

- 保留线 `kernel_ms ≤ 5.97 ms`：**八组无一达到**（最好的就是现役 7.13 ms）。
- 我跑之前写死的预期是"若延迟隐藏受限，tile=8 家族应给出 1.1–1.4×；若八组全落在 7.13–7.18 带内则
  occupancy 也被否证"。实测落在两者之间且偏向否证一侧：**tile=8 把常驻块数从 1/CU 抬到 5/CU
  （线程槽 256 → 320）反而慢 11–35%**。所以
  **"每 CU 只常驻 1 块所以慢"这条解释不成立**——多块并不换来时间，halo 冗余（tile=8 时每输出要搬
  `10×10/8×8 = 1.5625` 个像素 vs tile=16 的 `18×18/16×16 = 1.2656`）与块内屏障开销把收益吃掉了。
- `max_rel` 恒 `0.000000e+00`：9 条 `UNIKERN_RESULT`（8 组钉住 + 1 条不钉的对照）全部逐位一致。
- 因本轮不改生成器，**不存在回退问题**，发射程序仍与 `docs/evidence/phase23/conv-hip-postrevert.txt`
  逐字节相等。

### 现在被测量关闭的三条腿（都不许再当解释用）

| 腿 | 出自 | 判负方式 |
|----|------|----------|
| X 的复用度不够 | Phase 23 的动机 | 2×2 微块把复用提到 4×OCB → 慢 4.1× |
| LDS bank 冲突 | Phase 23 归因之一 | `C → C+1` padding → 256² 无变化、128² 慢 3–5% |
| 常驻块数 / occupancy | Phase 24 之后唯一活着的腿 | `tile=8`（5 块/CU）→ 慢 11–35%（本轮） |

仍然在的、但**本轮没有测**的一条：`OCB` 单轴的巨大效应（同一 tile 下 1 → 8 快 **4.43×**，
且随 1/OCB 次线性），它把矛头指向**每线程的 LDS 取值次数与 FMA 之比**（每步 1 次 LDS + 8 次权重取 +
8 次 FMA），也就是权重的放置方式——那是算法级改动（权重进 LDS/寄存器、完整隐式 GEMM），
不是再调 tile/ocb 能碰到的轴。

## 三、顺带读到的一条代码事实（不是本轮判据，单独立项）

发射程序里有**两处** `uk_conv_plan` 调用：verify 段用小的内嵌校验实例
（`uk_conv_plan(VoW, VoH, VOc, VC, VKh, VKw)`，VC=8），计时段用真实形状
（`uk_conv_plan(Wo, Ho, Oc, C, Kh, Kw)`，C=32）。因此**两段各自推导方案，方案可以不同**
（C 不同 → smem 不同 → `per_cu` 不同 → 谓词不同）。
`uk_conv_launch` 之上那句注释 "a pass can never launch with a different plan than the one
verified"（`docs/evidence/phase23/conv-hip-postrevert.txt:249-250`）**说得过满**：
单一发射点保证的是"发射形状不会分叉"，而方案本身确实是每段重算的。
安全性来自另一条已经逐位证明的性质——**所有 (tile, OCB) 方案产出逐位相同的结果**（本轮 9 条
`max_rel=0` 与 Phase 20/21/23/24 的全部读数都是这条的证据），所以"校验的方案 ≠ 计时的方案"
不构成正确性漏洞。注释要按这个口径改；改注释会动发射字节，故与基线重录一并留到下一相做。
