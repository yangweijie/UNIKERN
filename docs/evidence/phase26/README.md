# Phase 26 证据 — 验证覆盖缺口：verify 现在校验的就是被计时的那个实例化

本机：Windows 11 + MinGW + AMD **RX 6600M（gfx1032）**，ROCm 7.1。日期 2026-10-28。
这一相不是性能相，是**证据口径相**：它修的是"我们声称被验证的东西"与"实际被计时的东西"不一致。

## 缺口是怎么被发现的（测量，不是推理）

Phase 25 记下"发射程序有两处 `uk_conv_plan`"之后，直接把发射出来的 256² conv 程序单独跑起来
（绕开 runner 的输出裁剪）：

```bash
hipcc --offload-arch=gfx1032 uk_run/p25-check/uk_amd.cpp -o uk_run/phase26/uk_amd.exe
./uk_run/phase26/uk_amd.exe    # stderr 全文 26 行
```

修复前，两处调用打出**不同的方案**：

```
UNIKERN_INFO conv_plan ocb=1 tile=0  smem=0      nothing fills the device   <- verify 段（内嵌 32x32x8 实例，C=8）
UNIKERN_INFO conv_plan ocb=8 tile=16 smem=41472  fills=device              <- 计时段（声明形状 C=32）
```

即：`uk_conv_g<1>`（全局加载后备内核）被差分校验，`uk_conv<8>`（LDS 分块内核）被计时。
**默认流程里 `max_rel=0.000000e+00` 不是关于被计时实例化的证据。**
回头看，此前所有"分块内核逐位正确"的真机读数都来自**我把旋钮钉住了**的扫描
（`UK_CONV_TILE/UK_CONV_OCB` 会让两段选到同一方案），这层差别一直没被写清——
`[R20]` 当时的"单一发射点"断言只保证发射函数唯一，不保证方案唯一。

## 修法（最小）

真实形状的 `H/W/C/Kh/Kw/Oc` 与 `uk_plan` 提到 `main()` 开头、verify 块之前，全运行只有一个方案；
verify 段用 `uk_plan` 发射自己的小实例（`ocb` 有 `rem` 保护，`smem` 按真实 C 算是超配，安全）。
发射程序 `508 → 517` 行；`diff` 的改动面恰为 **+21 / −12 行**，全部落在这一处设计变更上
（无顺手编辑）。gemm 447 / reduce 239 行未变。

## 实测（`device-runs.txt`，全部 `verify=PASS max_rel=0.000000e+00`、rc=0）

| 用例 | 修复后 `conv_plan`（每运行恰好一条） | `kernel_ms` |
|------|-----------------------------------|-------------|
| conv 256²×32/Oc64 | `ocb=8 tile=16 smem=41472 fills=device` | 7.160367（带内 7.1276–7.1750） |
| conv 128²×16/Oc32 | `ocb=8 tile=16 smem=20736 fills=device` | 0.284069（Phase 21 记 0.287） |
| conv 32²×8/Oc16 | `ocb=1 tile=0 smem=0 nothing fills the device` | 0.012980（带内 10.7–14.2 µs） |
| gemm 512³（回归） | — | 0.123837 |
| reduce 1048576（回归） | — | 0.164954 |

单独运行同一程序也只剩一条方案行（`conv_plan ocb=8 tile=16 smem=41472 fills=device`），
`kernel_ms=7.024740`——**注意读数量的是上下文**：单独跑比经 runner 跑的 7.160367 低约 1.9%，
所以带内比较必须 runner-vs-runner，不跨上下文。

`device-runs.txt` 里还留了一条我自己的失误（第四次踩 E22）：reduce 第一次用
`--dims 1048576` 单参数调用，CLI 打的是 usage 文本，被 grep 过滤后转录里**静默少了这一格**；
补录用正确形式 `--dims 1048576 0 0`。失误写进转录而不是删掉。

## 门禁（`r20-arm-removal.txt`，`probe/phase26_plan_arm_probe.py` rc=0）

`[R20]` 新增四条计数腿：`uk_conv_plan(` == 2（定义 + 唯一调用点）、
`uk_conv_launch(uk_plan,` == 4、`uk_conv_launch(uk_plan, VoW` == 1（verify 那一条）、
且 `uk_conv_launch(plan,` 必须不存在。三条摘除控制全部当场变红（rc=3221226505）：

1. verify 段恢复自己推导方案 → `count_of("uk_conv_plan(") == 2` 红（`tests/acceptance.cpp:2624`；Phase 26 当时是 :1694）；
2. 计时三处改成本地方案 → `count_of("uk_conv_launch(uk_plan,") == 4` 红（`:1697`）；
3. verify 换成别的 plan 对象 → 同一条 `== 4` 红（`:1697`）。

复原 byte-identical，末轮 `ctest` 3/3 绿。

## 新基线

`conv-hip-single-plan.txt`（517 行，
`sha256=161d8e190da1286454e8429ea366b939bfcce54fe0c6e73890278ac92df27f3f`）取代
`docs/evidence/phase23/conv-hip-postrevert.txt`（508 行，`8464d86c…42bb28`）成为后续回退比对的基线；
旧文件按本项目惯例**保留不改**，只标历史时点。
