# Phase 23 证据 — conv 每线程 2×2 输出微块：负结果，已回退

本机：Windows 11 + MinGW + AMD **RX 6600M（gfx1032）**，ROCm 7.1。日期 2026-10-28。
靶子：Phase 21 的 `tile=16 / ocb=8`，kernel_ms 7.153 / 7.167 / 7.175。

## 预登记的判据（跑之前写死）

`kernel_ms ≤ 7.16 / 1.2 = 5.97 ms` 才保留，否则回退；`max_rel` 必须仍为 `0.000000e+00`
（微块只应改复用度，不改任何输出的 `c→kh→kw` 累加次序）。

## 实测（`microtile-sweep.txt`）

| 方案（2×2 微块） | kernel_ms | max_rel |
|------|-----------|---------|
| tile=16 ocb=8 | 59.650 | 0 |
| tile=16 ocb=4 | **29.272**（微块内最好） | 0 |
| tile=16 ocb=2 | 37.649 | 0 |
| tile=8 ocb=8 | 80.167 | 0 |
| tile=8 ocb=4 | 40.786 | 0 |
| tile=8 ocb=2 | 52.251 | 0 |
| 自动档（选档器）×2 | 59.679 / 59.717 | 0 |

> **这六行在 `microtile-sweep.txt` 里；自动档那两行不在任何转录里**——它们是回退前从终端输出读下来的，
> 回退后代码里已不存在微块，无法再采一次。所以本表里这两格的可追溯性低于其余六格，
> 引用它们时应当这样标注。结论不依赖这两格：判负由六组显式配置 + 5.97 ms 保留线就成立。

判据结论：**最差也满足"正确"，最好 29.27 ms 比 1×1 的 7.16 ms 慢 4.1×**，5.97 ms 的保留线远远没摸到 →
按预登记规则回退。第二次扫描的转录也如实记着：首版抽取式没匹配上，只留下 rc 行（E22 同类，我自己第三次犯）。

## 为什么"复用度上升"这次是反作用（归因，未反证）

微块把每线程活跃累加器从 OCB 抬到 `PY*PX*OCB`（16 或 32 个 float），同时块内线程数从
`tile²=256` 掉到 `(tile/2)²=64`——每 CU 能常驻的 warp 少了，延迟隐藏变差；而 px 循环里相邻线程
以 C 个字为步长访问 LDS，每个 `(c,kh,kw)` 步还要发 4 次 LDS 取值，bank 行为比 1×1 更差。
这三条与实测形状一致（ocb 越大越糟：8 比 4 慢一倍），但**没有 profiler 证据把它们分开**，
所以只作为归因记下，不写成结论。RDNA2 上想再拿回这份复用，得先解决 occupancy 与 bank，
而不是先加大微块。

> **Phase 24 更正（2026-10-28，同日下一相）**：上面三条里的 **bank 一条已被单独实验判负**——
> 把 `uk_sX` 的每像素步长从 `C` 改成 `C+1`（padding 槽不参与算术）之后，256² 的 `kernel_ms`
> 仍在未加 padding 的 7.1276–7.1750 ms 带内，128² 反而慢 2.9–5.3%。
> 见 `docs/evidence/phase24/README.md`。所以本节剩下的开放归因只有 occupancy 一条，
> 而"下一手"已被读码结果改写：每步是 1 次 LDS + **8 次权重全局取** + 8 次 FMA，该搬的是权重。

## 附带收获：字节钉住的臂在真实改动上自己响了

微块落地后 `ctest` 立刻 **3 → 2**：`[R20]` 的 `count_of("acc[o] += xv * Wt[") == 2` 与
按字节钉住的 FMA 嵌套都不认这次改写；回退后恢复 3/3，并实测复现
`kernel_ms=7.158620 / 7.173225`、`conv_plan ocb=8 tile=16 smem=41472`、`max_rel=0`。
**回退后的行为复现已补原始转录**：`postrevert-verify.txt`（本轮补采，两次 fresh-ledger run
`kernel_ms=7.158920 / 7.127580`，`verify=PASS max_rel=0.000000e+00`、`migrations=0 checks=6`、rc=0）。
连同 Phase 21 的 7.153/7.167/7.175，回退前后共 **7 次读数全部落在 7.1276–7.1750 ms 这一条带上**
（取 7 个读数的最小/最大值，不是新增测量）。
这条臂不是装饰——它在一次真实（且错误的）优化上执行了"要么不动顺序、要么你得当面解释"。

## 复现

```bash
for t in 16 8; do for o in 8 4 2; do
  UK_CONV_TILE=$t UK_CONV_OCB=$o ./build/unikern.exe run --hcs samples/hcs_amd.json \
    --kml workloads/conv/conv_256x256x32_k3x3_o64.json \
    --workdir uk_run/phase23 --ledger uk_run/phase23/s.sqlite3
done; done
```

注：`UK_CONV_TILE/UK_CONV_OCB` 旋钮是 Phase 21 为校准选档规则留下的，本 Phase 之后
生成器已回退到 1×1 微块，旋钮仍然有效。

## 回退的字节账（2026-10-28 复采）

回退后重新发射的 conv HIP 程序 **508 行**（三个 conv 形状 256²/128²/32² 全部 508，行数与形状无关），
Phase 21 当时记的是 506。这 2 行**不是微块残留**（按 `micro|2x2|px|PY|subtile` 全文匹配 0 命中），
而是本 Phase 扫描期间加在 env 旋钮分支上的两处诊断，回退时被判定"仍然有用"而保留：

- `src/codegen_hip.cpp:463` 的 ` source=env` 后缀——该 token 在 `docs/evidence/phase21/tiled-sweep.txt`
  出现 **0 次**、在今天的发射结果里出现 1 次，这是它晚于 Phase 21 的直接证据（自动档与旋钮档的
  选档来源从此可区分，下一次扫描才不会把"旋钮选出的档"误当成"选档器的选择"）；
- `uk_conv_plan` 里的 `smem` 重置在 `src/codegen_hip.cpp:435`：`if (p.tile == 0 && p.smem != 0) p.smem = 0;`——旋钮钉 tile=0 时
  打印真实 smem，不再打印上一个 tile 的残留值。

内核本体与选档逻辑（`uk_conv`/`uk_conv_g`/`uk_conv_plan` 的搜索顺序与判据）未留微块代码，
真机复现同分布（7.158620 / 7.173225 ms 对 Phase 21 的 7.153/7.167/7.175）；但**"逐字节回到 Phase 21"
这句不成立**，成立的是"回到 Phase 21 的行为 + 这 2 行诊断"。为此把当前发射结果入库为可比对基线：
`conv-hip-postrevert.txt`（508 行，
`sha256=8464d86cf3f07beb733a874a7cae9b0d538adb091d858e69f74b4b5a8e42bb28`）——
下一次再声称"回退"时应有可 diff 的对象，而不是只有一句行数。
