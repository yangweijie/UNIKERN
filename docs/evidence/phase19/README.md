> **[Phase 43]** the declared conv workload files moved from the ignored `uk_run/phase19conv/` to the tracked `workloads/conv/`; commands below use the new path. `.txt` transcripts in this directory keep their original wording per the no-rewrite convention. （旧路径）

# Phase 19 证据 — 次序鲁棒性实测 + conv 性能基线（计划开放项 ④）

本机：Windows 11 + MinGW g++ 13.2.0 + AMD **RX 6600M（gfx1032）**，ROCm 7.1。日期 2026-10-28。

## 复现命令

| 文件 | 命令 | 证什么 |
|------|------|--------|
| `reduce-order-grid.txt` | `hipcc -O3 --offload-arch=gfx1032 probe/uk_reduce_order_probe.cpp -o probe/uk_reduce_order_probe.exe && ./probe/uk_reduce_order_probe.exe` | 生成的两轮归约在**次序敏感**输入上的真实误差栅格：n∈{2²⁰,2²⁴} × 8 种幅值模式 × nP∈{1024,16384,65536}，同时报"与 fp32 顺序 CPU 参考之差"和"两者各自与 double 精确和之差" |
| `order-arm-removal.txt` | `python probe/phase19_order_arm_probe.py docs/evidence/phase19/order-arm-removal.txt` | `[R6.4-order]` 臂承重：把 `src/reference.cpp` 的 `cpu_reduce` 累加器从 `float` 加宽到 `double`，acceptance 当场在 `[R15]` `tests/acceptance.cpp:1465`（`seq_err > 1e-4`）报红；复原 byte-identical，ctest 2/2 绿 |
| `conv-baseline.txt` | `./build/unikern.exe run --hcs samples/hcs_amd.json --kml <shape>.json --workdir uk_run/phase19conv --ledger uk_run/phase19conv/conv.sqlite3`（三种 conv 形状）+ `run --op gemm --dims 512 512 512`（同目录） | `uk_conv`（一线程一输出、隐式 GEMM）与分块 `uk_gemm` 的实测吞吐对照 |

## 次序鲁棒性的实测结论（推翻了两处此前的说法）

1. **出厂差分验证看不见这件事**：生成程序的 verify 数据是 2²⁰ 个 2⁻⁶，任意次序都精确等于
   16384（生成程序注释自己写明"verifies structure, not floating-point order"）。所以
   `max_rel=0` 从来不是次序鲁棒性的证据。本轮把它连同两种模式一起在 CPU 侧复算，
   `[R6.4-order]` 臂实测 `seq_err=7.23881e-03`、`two_pass_err=4.69154e-14`、
   `divergence=7.18679e-03`，与真机 nP=1024 行的 `4.691537e-14 / 7.186789e-03` 逐位同形——
   这条臂因此是真机事实的确定性代理，而不是又一段转录。
2. **更准的内核会被判 FAIL（本轮真正的发现）**：交替 1e6 / 1e-⁶ 的宽动态范围输入下，
   两轮归约离精确和的距离（4.69e-14）比 fp32 顺序 CPU 参考（7.19e-03）**小五个数量级**；
   而 R6.2/R6.3 的判据是"与 CPU 参考的相对差 ≤ 2⁻⁵"。到 n=2²⁴，两者之差涨到
   `3.97e-02 / 3.99e-02 / 4.00e-02`（nP 三档），**超过 2⁻⁵=3.125e-02** →
   生成程序会打 `verify=FAIL` 并拒绝入账本。也就是说：在这个输入形状上，被判错的一方
   恰恰是更准的那一方，而参考自己离精确值就差 `3.988e-02`。
3. **两条预登记预测落空（如实记）**：① 预测"半 1.0 / 半 2⁻²⁴ 的模式会给出 max_rel>0"——
   实测为 0，因为两种求和方式把小值吞掉的方式相同，该模式测的是吸收不是次序；
   ② 预测"降幅值模式一定比升幅值差"——只在 nP=1024 成立（1.70e-06 vs 1.43e-06），
   nP≥4096 时反了（降=0，升=2.68e-07）。
4. **不需要现在改容差**：本轮没有动 R6.2 的 2⁻⁵。理由：现有全部入库真机证据都建立在
   `max_rel=0` 的精确可叠合数据上，改容差会让那些读数的口径失效；真正的口径问题是
   **差分参考的选择**（fp32 顺序和不是"真值"），属规格级决定，已作为开放决策上报。

## conv 性能基线（调优的靶子）

| 形状 | kernel_ms | FLOP | 实测吞吐 |
|------|-----------|------|----------|
| conv 32×32×8 k3×3 Oc16 | 0.013960 | 3.68e+06 | ~263 GFLOPS（小尺寸下 e2e 0.2998 ms 由拷贝主导，此值不可作结论） |
| conv 128×128×16 k3×3 Oc32 | 2.438981 | 1.51e+08 | **61.9 GFLOPS** |
| conv 256×256×32 k3×3 Oc64 | 124.570947 | 2.416e+09 | **19.4 GFLOPS** |
| gemm 512³（寄存器分块，D6 tile 128×128） | 0.117979 | 2.684e+08 | **2275 GFLOPS** |

结论：`uk_conv` 现在是一线程一输出、内层循环裸读全局内存（C·Kh·Kw 次 FMA 无复用），
比同样在本机上跑的分块 `uk_gemm` 低约 **两个数量级**（19.4 vs 2275 GFLOPS），且随尺寸
**下降**（61.9 → 19.4），是算术强度/访存受限的典型形状。调优（共享内存分块 + 复用）
就是 Phase 20 要打的靶子，验收基线即本表。

## 仍开放

- conv 分块内核的实现与验证（Phase 20），本表为其基线。
- reduce 侧若引入树形/成对求和（pairwise），需同时重估差分参考的选择——与上面第 4 点是同一个规格决定。
- 栅格未覆盖：fp16 通路（R6.2 的 2⁻⁴）与非 2 的幂长度。
