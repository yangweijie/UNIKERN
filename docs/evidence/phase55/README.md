# Phase 55 — 非 HIP 两个后端的**累加次序**有了自己的钉（`[R20-nonhip]`）

主机：**Windows 11 + AMD RX 6600M（gfx1032）**。这条臂是纯文本/纯 CPU 的，不需要 hipcc、
不需要 Intel 或 Apple 设备。

## 补的洞：三条字节腿会说"有东西动了"，不会说"动的是哪条轴"

硬约束第 3 条：改算术顺序 = 改结论（`max_rel` 不再恒 0，已入库读数改含义）。
HIP 侧由 `[R20]` 按字节钉住那几段嵌套；OpenCL/Metal 侧**什么都没有**——
`[R13]`/`R14` 只钉 banner 与精度文本。于是本仓今天的样子是：

| 改动 | 会红吗 | 红的说法 |
| --- | --- | --- |
| 改 `.cl`/`.metal` 里一句注释 | 红（字节腿） | "字节变了"——正确：读数仍可比 |
| 改 `.cl`/`.metal` 的**累加次序** | 红（字节腿） | "字节变了"——**信息不足**：读数不再可比 |

新臂把第二种情况单独命名出来。

## 臂

`[R20-nonhip]` `tests/acceptance.cpp:2811` 起（`[R20-nonhip]`），用**进程内**生成（`ocl_kernel_source` /
`msl_kernel_source`，不新增 CLI 发射），对两个后端 × 三个算子各钉一条**单调锚点序列**与
**累加语句的次数**：

| 算子 | 钉住的次序（5 或 4 个锚点） | 累加语句，期望次数 1 |
| --- | --- | --- |
| conv | `c` → `kh` → `kw` → `acc += xv * wv;` → 存储 | `acc += xv * wv;` |
| reduce | `float s = 0.f;` → `s += X[i];` → `P[tid] = s;` → `s += P[i];` → `Y[0] = s;` | `s += X[i];` |
| gemm | `k0` → `k` → `r/c` → `acc[r][c] += a[r] * b[c];` → 存储 | `acc[r][c] += a[r] * b[c];` |

两条既有纪律被刻意用上：**序列而非相邻**（E50，"把声明移到使用之后"式的弱断言在这里同样无效）、
**次数而非存在**（E21/E24）。逐 pin 先打印一行判决、再断言，并且**臂自己数打印行数**
（`assert(printed == 6)`，`tests/acceptance.cpp:2836`）——一条quietly 少打印的臂等于没有臂（E38 同族）。

```text
  [R20-nonhip] ocl/conv order=ok acc=1/1 anchors=5
  [R20-nonhip] ocl/reduce order=ok acc=1/1 anchors=5
  [R20-nonhip] ocl/gemm order=ok acc=1/1 anchors=4
  [R20-nonhip] msl/conv order=ok acc=1/1 anchors=5
  [R20-nonhip] msl/reduce order=ok acc=1/1 anchors=5
  [R20-nonhip] msl/gemm order=ok acc=1/1 anchors=4
[R20-nonhip] 6/6 accumulation sequences ...
```

臂排在三条字节腿**之前**（`R20-nonhip` 2747-2845 / `R5.4-cl` 2846-2909 / `R5.4-msl` 2910-2976），
顺序本身就是归属手段之一——见下。

## 五条控制与"按 assert 行号认臂"（`controls.txt`）

新的归属办法：CRT 消息带**表达式和行号**，探针在运行时读 `tests/acceptance.cpp` 的
`  // -------- R` 臂头表，把行号映射回**拥有它的臂**。这比"哪条 banner 没出现"强，
后者只知道谁先跑。

| 控制 | 破坏方式 | acceptance | assert 表达式 | 归属臂 | 门 #7 | 含义 |
| --- | --- | --- | --- | --- | --- | --- |
| N1 | **只在 `ocl_conv_kernel` 内**把 conv 的 `c` 循环改成倒序 | RED | `order_ok == 6` | `R20-nonhip` | GREEN | 次序动了、语法没动；只有这条臂说话，且 broken 只有 `ocl/conv`（`msl/conv` 仍 ok） |
| N2 | 改发射出去的 OpenCL 注释 | RED | `drift == 0` | `R5.4-cl` | GREEN | 次序臂先打印 6 条全 ok ⇒ 这是字节轴 |
| N2b | 改发射出去的 Metal 注释 | RED | `drift == 0` | `R5.4-msl` | GREEN | 两条字节轴也分后端 |
| N3 | 改发射出去的 HIP 注释 | RED | `fresh.size() == stored.size()` | `R5.4` | GREEN | 次序臂**一行都没打印**（它排在后面）——记为方法的局限，不记作独立性 |
| N4 | 改生成器自己不发射的注释 | GREEN | 无 | — | GREEN | 中性对照 |

N1 是这一相的靶心：它是一次**真的折叠次序改变**（第 3 条禁止的那类），
发射出来的程序仍然是合法 C++，所以门 #7 绿、字节腿排在它后面根本轮不到说话。
把 `kh`/`kw` 两行头部直接对调做不到这一点——那会破坏 `{`/`}` 配对，把门 #7 一起拖红，
归属就脏了（写在 `swap_in_fn` 的注释里）。

N1 还必须**按函数切片**打补丁：`kh`/`kw`/`c` 这些针在 `conv_msl_kernel` 与 `ocl_conv_kernel`
各出现一次（整文件计数 2）。整文件 `replace(...,1)` 会悄悄命中先出现的那份（Metal），
然后把红报给 OpenCL——E50 的别名/惰性失效与 E21/E24 的次数规则在这里同时生效。

## 我自己写的期望错了两处（E51/E55 同族，都被这套字段抓住）

- `want["broken"]` 里被我塞进了 6 条 `order=ok` 行，而 `got["broken"]` 只收**破了**的行
  ⇒ N2/N2b/N4 结构上永远不可能匹配（第一次运行就是这么报的）。
- N3 期望写成 `fresh == stored`，而那条臂**先**断言长度 ⇒ 实际是
  `fresh.size() == stored.size()`。同一条臂、同一个结论，不同的表达式。
- 第三处在探针自己身上：它先写转录文件、后打印总结行 ⇒ 存下来的 `controls.txt`
  **不含自己的结论**（第一次运行就这样丢了）。现在先算好判决行再落盘。

## 边界与仍然打开的缺口

- 这条臂钉的是**发射文本的次序**，不是运行时折叠顺序。它钉不住"两个循环合并成一个但次序等价"
  这类改写；那种要靠真机 `max_rel` 复采。
- 只覆盖 fp32 三算子的现有形状（与三条字节腿同一批工作负载）。
- **门 #3 的一条真实盲区（本相实测）**：`anchors_check.py` 只在被引行**带有可核对的代码符号**时
  才核 ±25 窗口。本相新臂插到字节腿之前，把 Phase 53/54 写下的四个 acceptance.cpp 引用
  （当时写在 :2747/:2800/:2811/:2863）
  整体挪了约 99 行，而那些行引用的是 `[R5.4-…]` 这类**方括号标签**（被符号过滤器排除）⇒
  门 #3 全绿而引用已经失效。本轮已手工全部改指（`grep` 现取，E23），
  但**这条盲区本身还在**，留给下一相：把"无法核对的引用"从静默通过变成显式计数/失败。

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure                     # 7 条门
SOURCE_DIR=$PWD ./build/unikern_acceptance.exe | grep R20-nonhip
python probe/phase55_nesting_control_probe.py docs/evidence/phase55/controls.txt
```
