# Phase 52 — 门 #7 从"只查 HIP"扩到"每条能在这里编译的产物都查"：`generated_programs`

主机：**Windows 11 + AMD RX 6600M（gfx1032）**，ROCm 7.1。这台机器上 `apple=no`、`intel=no`，
但下面的结论只依赖**前端是否存在**，不依赖 Intel/Apple 设备。

## 这一相关掉的是 Phase 51 自己写下的一条边界

Phase 51 记录门 #7 的覆盖面时写着"MSL/OpenCL 产物语法要等各自主机"。**前半句仍然成立，后半句被
实测否证**：ROCm 7.1 自带 `clang.exe`（`D:\Program Files\AMD\ROCm\7.1\bin\clang.exe`），它对
OpenCL C 有内建头，于是 Intel 目标的产物在 AMD 主机上就能解析：

```text
clang -fsyntax-only -x cl -Xclang -finclude-default-header build/uk_generated_programs/<op>/uk_intel.cl
```

三份产物（gemm 53 行 / conv 32 行 / reduce 31 行）全部 rc=0；往末尾追加一个坏 kernel 立刻 rc=1。
不需要 Intel SDK，不需要 Intel 设备，也不需要 `ocloc`——检查的是**语法**，不是 SPIR-V 代码生成。

## 门的结构（每个后端三段，缺一不算腿）

`probe/generated_program_check.py`（Phase 50 的 hip_program_check.py 改名而来）：

1. **可用性探针**——`find_hipcc()` 走 `HIP_PATH`/PATH，`find_clang()` 优先取 hipcc 隔壁的 clang，
   再退 PATH；"能不能用"由探测决定，不由名字决定。
2. **该后端自己的控制**——良好源码必须被接受 **且** 坏源码必须被拒绝，否则打印
   `PROG SKIP <backend> reason=control: ...`（见 `gate-output.txt` 第 3 节：两条报警方向都用假前端
   实测过——恒接受 → `BROKEN source ACCEPTED - the sub-check would be decorative`；
   恒拒绝 → `sanity source refused`）。
3. **真产物**——CLI 现场发射 gemm/conv/reduce，同一份生成结果喂给两个后端（`checked=6` = 3 op × 2 后端）。

跳过码仍是 `SKIP_RETURN_CODE 77`，但语义收紧为"**没有任何一段能跑**才跳"：本机两段都跑 → PASS；
把 `HIP_PATH` 与 PATH 剥掉 → 两个后端各打一行 `PROG SKIP ... reason=`，`RESULT SKIPPED checked=0`，
rc=77（`gate-output.txt` 第 2 节是原样命令与输出）。MSL 那一半永远打印 `PROG gap msl reason=...`，
不静默消失。

## 三条控制，期望各不相同（`controls.txt`）

| 控制 | 破坏方式 | 归属后的 acceptance | 门 #6 | 门 #7 | 含义 |
| --- | --- | --- | --- | --- | --- |
| Q1 | `src/codegen_other.cpp` 的 conv kernel 签名里塞一个 `}` | GREEN | GREEN | **RED，红在 ocl 半边** | OpenCL 子检查真的在看产物 |
| Q2 | 只改一句注释的措辞 | GREEN | GREEN | **GREEN** | 它检查语法，不是字节指纹 |
| Q3 | `src/codegen_hip.cpp` 的 gemm 宿主签名里塞 `EXTRA_TOKEN` | GREEN | GREEN | **RED，红在 hip 半边，ocl 仍绿** | 两半独立，"6 项"是 6 项而不是"一件事数两遍" |

编译器原话：Q1 `conv/uk_intel.cl:15:73: error: expected ')'`；Q3 `gemm/uk_amd.cpp:63:64: error: expected ')'`
（`build/uk_generated_programs/<op>/` 是门自己的产物目录，重跑即重建，所以这里的行号以下面
**入库的字节**为准）。
三条都通过影子 `SOURCE_DIR` 归属（`phase49_widthpin_removal_probe.shadow_root`），否则 `[R5.4]`
会先把红吞掉，控制就回答不了"是哪一条款红的"（E50）。Q3 的锚点第一次跑就撞上
`GEMM_ARG found 2 times`——`int M, int N, int K) {` 在 `__restrict__` `src/codegen_hip.cpp:1054` 与 `:1089` 各一次，
换成 `std::vector<float>& C, int M, int N, int K) {` 才唯一（E21/E24 同族：锚点唯一性要在写下的
那一刻按次数取，不许凭记忆）。

## 顺手量到的两条事实（写下来免得下一个人误读）

- 发射出的 `uk_intel.cl` 是**纯 kernel 源码**，不含宿主 `main`，因此里面 `UK_REDUCE_WIDTH` 出现 **0** 次
  （HIP 那份是 9 次）。这不是硬约束第 4 条的洞：Intel 侧的宽度自报/拒绝在宿主 runner
  `src/runner_ocl.cpp` 的 `plan_reduce_width()` 里，产物本身不选宽度。
- 门 #7 只解析文本，**不钉字节**。产物字节的锚 `tests/acceptance.cpp` 的 `[R5.4-artifact-bytes]`
  目前只覆盖 HIP 三份 `uk_amd.cpp`，OpenCL 三份 `.cl` 没有对应腿——本轮先把它们存进 `emit/`
  （下一节），**腿与它的摘除控制仍未闭合**。

## 入库的 OpenCL 产物字节（有基线，仍然没有腿）

硬约束第 5 条要求动生成器之前先存基线。门 #7 的两个后端里，HIP 三份早由
`[R5.4-artifact-bytes]` 按字节钉住，`.cl` 一份都没有——本轮把发射出的三份原样存进 `emit/`：

| 产物 | 行数 | sha256 |
| --- | --- | --- |
| `emit/gemm/uk_intel.cl` | 53 | `833ea59b72d75b81f796803681359ad2679feab0e907e86a5956421e4622ef8a` |
| `emit/conv/uk_intel.cl` | 32 | `df5f068a8fc75ac554e23529e98b134a2d5a33dbf9ffa05a203ed0151f2fb253` |
| `emit/reduce/uk_intel.cl` | 31 | `5d463258c4609b7af1e898b24dd237b8eee9bb29f2db2abdae7053aaaa05bcba` |

Q1 的报错行列能对上这份字节：入库 conv 的第 15 行是
`__kernel void uk_conv(__global const float* X, __global const float* Wt,`，长度 **72 字符**，
控制把行尾的 `,` 换成 ` }` ⇒ 那个 `}` 正落在**第 73 列**，与 clang 报的 `15:73` 一致。
**但这是基线，不是腿**：acceptance 里没有任何臂把这三份字节与当次生成结果比较，所以字节锚缺口
仍然打开（需要一条新腿 + 它自己的摘除控制）。
**（演进注记：这条腿由下一相 Phase 53 补上，`[R5.4-artifact-bytes-cl]`，见 `docs/evidence/phase53/`；
本相写作时它确实不存在，四条 Q/C 控制的期望也都是按"没有字节腿"的实况写的。）**

## 边界

- 语法检查 ≠ 为目标生成代码 ≠ 跑通。`verify=PASS` 的数字只来自 `docs/evidence/**` 的真机运行。
- OpenCL 子检查用的是 clang 的 OpenCL C 前端 + 内建头，不是 Intel 编译器；它能拒绝语法错误，
  不能声称 Intel 工具链接受该产物。
- MSL 仍只能文本读。

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure                      # 7 条门
python probe/generated_program_check.py                         # 本机：RESULT PASS checked=6
python probe/phase52_clparse_control_probe.py docs/evidence/phase52/controls.txt
sha256sum docs/evidence/phase52/emit/*/uk_intel.cl          # 与上表三条哈希逐一对上
python -c 'import os,sys,runpy; os.environ.pop("HIP_PATH",None); os.environ["PATH"]=r"C:\Windows\System32"; sys.argv=["generated_program_check.py"]; runpy.run_path("probe/generated_program_check.py", run_name="__main__")'   # 无前端：rc=77
```
