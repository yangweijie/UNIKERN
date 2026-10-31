# Phase 53 — OpenCL 产物从"有基线"变成"有腿"：`[R5.4-artifact-bytes-cl]`

主机：**Windows 11 + AMD RX 6600M（gfx1032）**。本相不需要 hipcc、不需要 Intel 设备——
新腿只做 CPU 侧的"发射 + 按字节比较"。

## 补的是 Phase 52 自己写下的后半句

Phase 52 的 README 原话：**"这仍然只是基线，不是腿"**。缺口的形状和 Phase 48 补 HIP 那一份时完全同形：

- 门 #7 解析 `.cl` 的**语法**，并且明说它不是字节指纹（它的 Q2 控制期望就是 GREEN）；
- `[R5.4-artifact-bytes]` 只钉 **HIP** 三份 `uk_amd.cpp`。

⇒ "把 OpenCL 文本里一句注释改掉"这件事，在本相之前**没有任何腿会红**。

## 新腿

`[R5.4-artifact-bytes-cl]` `tests/acceptance.cpp:3053` 起（`[R5.4-artifact-bytes-cl]`，臂 id 与 R5.4 同族）：CLI 现场发射
gemm/conv/reduce，取每份 `uk_intel.cl` 与入库基线
`docs/evidence/phase52/emit/<op>/uk_intel.cl` 逐字节比较；逐 op 先打印
`identical` / `DRIFT` 与实际字节数，**然后**才断言 `drift == 0 && identical == 3`
（`[R5.4-artifact-bytes-cl]` `tests/acceptance.cpp:3053`）。打印先于断言是因为归属不能靠"哪条 banner 没出现"——
一个二进制里有两条字节腿，谁先 assert 就会把后面的挤掉，那是顺序假象不是证据。

```text
$ SOURCE_DIR=$PWD ./build/unikern_acceptance.exe | grep R5.4
[R5.4-artifact-bytes] 3/3 stored evidence artifacts are byte-identical to what this binary emits today (...)
  [R5.4-byte-cl] gemm identical fresh=2594B stored=2594B
  [R5.4-byte-cl] conv identical fresh=1877B stored=1877B
  [R5.4-byte-cl] reduce identical fresh=1708B stored=1708B
[R5.4-artifact-bytes-cl] 3/3 stored OpenCL artifacts are byte-identical to what this binary emits today
```

基线字节**没有被覆盖**：三份 `.cl` 仍是 Phase 52 存盘那三份（2594 / 1877 / 1708 B），
预登记 5 要求"若生成器与存盘时刻有任何差异就该红"，实测逐位相同。

## 四条控制，期望各不相同（`controls.txt`）

| 控制 | 破坏方式 | acceptance | HIP 字节腿 | 新腿 drift | 门 #7 | 含义 |
| --- | --- | --- | --- | --- | --- | --- |
| C1 | 改发射文本里一句 OpenCL 注释 | **RED** | 打印（绿） | `['reduce']` | **GREEN** | 新腿专属：门 #7 的 Q2 期望就是绿 |
| C2 | conv 签名里塞 `}`（Phase 52 Q1 同款） | **RED** | 打印（绿） | `['conv']` | RED（ocl 半边） | 与门 #7 重叠，如实记 |
| C3 | 改发射文本里一句 **HIP** 注释 | **RED** | **未打印** | 未跑到 | GREEN | 两条字节腿是两条独立轴 |
| C4 | 改生成器自己不发射的注释 | GREEN | 打印 | 无 | GREEN | 中性控制：不是"改动即红" |

C1 是这条腿存在的理由：它红的东西，`ctest` 里别的仪器一件都不红。
C4 是它的对面：没有 C4，"反应灵敏"与"只反应坏代码"无法区分（Phase 50 的 K3 干同一件事）。

## 归属装置必须中和**所有**字节腿（本轮最重要的一条方法）

新腿一落地，`probe/phase49_widthpin_removal_probe.py` 的影子 `SOURCE_DIR` 就不够用了：它只把
**HIP** 那一份基线重新发射进影子树，于是 `[R5.4-artifact-bytes-cl]` 在每个影子控制上都因为
"文件根本不存在"而红——Phase 52 的 Q1/Q2/Q3 期望（"归属后 acceptance GREEN"）集体变成假红。

修的是**装置**而不是期望：`shadow_root` 现在把同一个 `--emit` 目录里的 `uk_intel.cl` 也种进影子树
（`probe/phase49_widthpin_removal_probe.py:113`）。修完后 phase52 的三条控制**原期望全部重新成立**，
一个字节都没改它们的 `want`。这条与硬约束第 8 条"每条路径都要测"是同一类：一条腿不够，就是没闭合。

**这一段是读数，不是推理**（`probe/phase53_shadow_pre_fix_replay.py` → `shadow-pre-fix.txt`）：
先按修好的样子建影子树，acceptance 在影子归属下 **rc=0**；然后**只删掉刚种进去的 `.cl` 基线**
（回到修之前的影子状态），同一条命令立刻 **rc=3221226505**，报
`Assertion failed: !fresh.empty() && !stored.empty(), `[R5.4-artifact-bytes-cl]`、tests/acceptance.cpp:2975`，
并且**逐 op 那三行一行都没打印**。这就是两种红的差别：真漂移先说出"哪个 op 漂了"再红，
缺基线是什么都不说就红。当时如果去改 Phase 52 的期望，改的是**仪器故障**，不是被测性质。

## 期望没有被改，但被两条自己的缺陷挡了一次（记 E55）

- 解析归属时取错了字段（`split()[2]` 应是 `[1]`），于是"drift 的 op 名"读成了字面量 `DRIFT`。
  这条**被预登记的期望抓住**（`want drift=['reduce']` vs `got ['DRIFT']`）——期望写得足够具体，
  才让脚本自身的 bug 无处藏；写成"acceptance 应该红"就漏过了。
- 转录目录 `docs/evidence/phase53/` 没先建，控制全套跑完（约 3 分钟）才在写文件的最后一行崩掉。
  现在探针开头就检查父目录并在动手前退出。

## 边界

- 与 HIP 那份一样：这条腿只声称**字节一致**，不声称这些字节在 Intel 设备上跑过
  （本机 `intel=no`；上机读数在 `docs/evidence/phase9/`）。
- 基线只有 fp32 三算子的工作负载形状；换形状/换 dtype 需要新的基线，不是让这条腿变宽。
- MSL 产物仍然既无语法检查也无字节锚。
  **（演进注记：字节锚这半由下一相 Phase 54 补上（`[R5.4-artifact-bytes-msl]`）；
  语法那半的原因从本相的断言变成探针读数——本机两个 clang 都不认 `-x metal` 这个语言。
  见 `docs/evidence/phase54/`。）**

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure                     # 7 条门
SOURCE_DIR=$PWD ./build/unikern_acceptance.exe | grep R5.4     # 两条字节腿
python probe/phase53_clbytes_control_probe.py docs/evidence/phase53/controls.txt
python probe/phase53_shadow_pre_fix_replay.py                       # 影子装置缺第二条腿时的读数
python probe/phase52_clparse_control_probe.py docs/evidence/phase52/controls.txt   # 旧期望仍成立
```
