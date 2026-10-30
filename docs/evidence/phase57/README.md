# Phase 57 — 次序钉过了，**表达式形状**现在也有钉：`[R20-nonhip-expr]`

主机：**Windows 11 + AMD RX 6600M（gfx1032）**。纯 CPU/纯文本，不需要 hipcc，也不需要 Intel/Apple 设备。

## 补的是哪一条轴

硬约束第 3 条管两件事：累加**次序**与算式**形状**——两者任一变动，`max_rel` 就不再恒 0，
`docs/evidence/phase9|10` 的读数的含义就变。现状是：

| 后端 | 次序 | 表达式形状 |
| --- | --- | --- |
| HIP | `[R20]`（整段 `nest_tile`/`nest_glob` 按字节，两者都覆盖） | 同一条臂 |
| OpenCL / Metal | `[R20-nonhip]`（Phase 55） | **本相之前没有** |

于是把 `X[((ho + kh) * W + (wo + kw)) * C + c]` 改写成 `X[((kh + ho) * W + (kw + wo)) * C + c]`
——循环一模一样、次序一模一样、算术表达式换了形——**次序臂全绿，只有字节腿红**，
而字节腿的红永远不说"这是算术形状"。本相补的就是这句话。

## 臂

`[R20-nonhip-expr]`（`tests/acceptance.cpp:2917`）与次序臂同形：进程内生成
（`ocl_kernel_source`/`msl_kernel_source`），2 后端 × 3 算子 = 6 个 pin，
每个 pin 是**逐字节多行**的表达式片段，按**次数 = 1** 核（E21/E24），逐 pin 先打印判决再断言，
并自己数打印行数（`assert(printed == 6)`，E38）：

| 算子 | 钉住的表达式片段（按字节） |
| --- | --- |
| conv | `const float xv = X[((ho + kh) * W + (wo + kw)) * C + c];` + `wv` 行 + `acc += xv * wv;`（连续三行） |
| reduce | pass-1 的 `for (int i = tid; ...) s += X[i];` + `P[tid] = s;`（OpenCL 形）/ `int(tid)`、`int(nthreads)` 形（Metal）；pass-2 的 `for (int i = 0; i < np; ++i) s += P[i];` + `Y[0] = s;` |
| gemm | `As[i][j] = (bm + j < M && k0 + i < K) ? A[(bm + j) * K + k0 + i] : 0.f;`、`Bs[...]` 同行形状、`for (int c = 0; c < RPN; ++c) acc[r][c] += a[r] * b[c];` |

片段是 **grep 现取**的（含缩进，E23；上一相刚因"凭记忆写符号名"被抓过）。

```text
  [R20-nonhip-expr] ocl/conv expr=ok pins=1 counts=1
  [R20-nonhip-expr] msl/conv expr=ok pins=1 counts=1
  [R20-nonhip-expr] ocl/reduce expr=ok pins=2 counts=1/1
  [R20-nonhip-expr] msl/reduce expr=ok pins=2 counts=1/1
  [R20-nonhip-expr] ocl/gemm expr=ok pins=3 counts=1/1/1
  [R20-nonhip-expr] msl/gemm expr=ok pins=3 counts=1/1/1
[R20-nonhip-expr] 6/6 expression pins ...
```

## 四条控制（`controls.txt`），E1 与 E3 成对

| 控制 | 补丁（都限定在单个函数内） | 次序臂 | 表达式臂 | 归属（assert 表达式 → 臂） | 门 #7 |
| --- | --- | --- | --- | --- | --- |
| E1 | `ocl_conv_kernel` 内把 xv 的索引式改成 `(kh + ho)`/`(kw + wo)` | **6 条全 ok** | `ocl/conv expr=BAD MISSING`，其余 5 条 ok | `exact == 6` → `R20-nonhip-expr` | GREEN |
| E2 | 只改 `ocl_reduce_kernels` 里一句注释 | 6 条全 ok | 6 条全 ok | `drift == 0` → `R5.4-cl`（字节轴） | GREEN |
| E3 | `ocl_conv_kernel` 内反转 conv 的 `c` 循环（Phase 55 的 N1） | `ocl/conv` missing | **未跑到**（次序臂在前） | `order_ok == 6` → `R20-nonhip` | GREEN |
| E4 | 只改生成器自己不发射的注释 | 6 全 ok | 6 全 ok | 无 assert | GREEN |

E1 是同一次运行里两根臂**各自表态**的样本：次序没动（次序臂干净），形状动了（表达式臂点名 `ocl/conv`）。
E3 只有次序臂说话——表达式臂排在它后面，一 abort 就什么都不剩（Phase 55 记过同一个局限）。
所以这对轴需要**两条控制**而不是一条：单看 E1 或单看 E3 都不足以证明"两根臂不重叠"。

## 上一相的仪器第一次上岗（顺带记）

新臂插在两条字节腿之前，把它们的位置整体推后 89 行。`ctest` 当场报门 #3 失败，
**13 条引用全部被点名**（其中 5 条是 Phase 56 新加的**臂标签归属**——`R5.4-artifact-bytes-cl owns
R5.4-cl (2935-2998) but line 2846 falls in R20-nonhip-expr`）。全部按 grep 现取改指：
`2846→2935`、`2886→2975`、`2899→2988`、`2910→2999`、`2962→3051`；
`weak/unverifiable` 两条上限**没有动**（107/91 原样）。
上一相建的仪器在这一相立刻回本——这正是它存在的目的。

## 期望错在我这边（E51/E55 家族再一次）

预登记里我写"E1 会让次序臂一行都不打印"——理由是把两臂的先后记反了。实际次序臂**先**跑并且
干净通过，反而给出更强的证据（一次运行里两臂各自表态）。改成按实况写死后 4 条全中。
**规则照旧：控制不符合期望时先怀疑期望。**

## 边界

- 钉的是**发射文本的形状**。等价的代数改写（例如把同一个乘积写成另一条同形但换元的式子）会红；
  把两段循环合并成算术等价但文本完全不同的写法，会由这条臂与字节腿一起红——但**红不解释等价性**，
  等价性只能靠真机重采 `max_rel`。
- 片段取自当前发射产物；换 dtype/换算子形状需要新的基线，不是把这条臂放宽。
- HIP 侧仍由 `[R20]` 负责（它同时覆盖次序与形状），本相不重复钉。

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure                        # 7 条门
SOURCE_DIR=$PWD ./build/unikern_acceptance.exe | grep R20-nonhip  # 两根非 HIP 臂
python probe/phase57_expr_control_probe.py docs/evidence/phase57/controls.txt
```
