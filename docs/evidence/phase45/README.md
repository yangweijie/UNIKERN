# Phase 45 — D11：dtype 能力区间 + R6.2 分级容差真的接进闸门

主机：Windows 11 + AMD RX 6600M（gfx1032），ROCm 7.1 hipcc；`apple=no`、`intel=no`
（`backends.txt`）。所有读数都是这台机器产生的，Metal/OpenCL 的路径只到"源码 + 语法检查"为止。

## 这一相做了什么

规格 `requirements.md:86` / `design.md:132` 的分级容差是三条：整型与索引结果**位级一致**、
FP32 ≤ 2⁻⁵、FP16 ≤ 2⁻⁴，且可配置。改之前：

1. `Tolerance::fp16_rel` 是一个**没有任何代码读的字段**；
2. 整型档根本没有 0 容差这条；
3. 三个 HIP 发射器把 `0.03125f` **写死**在产物里（`src/codegen_hip.cpp:638`、`:878`、`:1212`）；
4. 前端接受 `fp32|fp16|int8`，但生成器只会产 fp32 内核 —— 于是一棵 `int8` 树会被按**相对**
   2⁻⁵ 判，并且（这一条是收证据时才发现的）**能进账本**。

D11 因此做两件事，而不只是一件事：

* **能力区间**：`dtype_gap()`（`src/codegen_hip.cpp:71`）接进 `msl_op_gap` / `ocl_op_gap` /
  `op_support_gap`，非 fp32 一律点名 `dtype=<x>` 拒绝，绝不布局/算子混谈；
* **口径接线**：`tolerance_for(DType, Tolerance)`（`src/runtime.cpp:182`）成为唯一的表，
  产物里的判定行由 `dtype_tol_literal()`（`src/codegen_hip.cpp:81`）从表生成；
  两个不能在本机编译的进程内 runner（`initA` `src/runner_ocl.cpp:583`、`newBufferWithLength` `src/runner_metal.mm:559`）
  也从写死的 `0.03125f` 改成读表。

## 收证据时发现的真缺陷（G12 的后半）

`solve` 的拒绝条件原本挂的是 `--emit`：`solve --kml <int8 树>` **不带 `--emit`** 时打印一句
"model-only" 就照常 `ledger->commit()`。实测（改动前）：

```
$ ./build/unikern.exe solve --hcs samples/hcs_amd.json --kml …k_int8.json --ledger l2.sqlite3
SOLVED  x* tile=128x128 …            # rc=0
$ python -c "…select count(*) from solutions"   ->  solutions (1,)
```

也就是说账本里可以存下一条**没有任何后端能生成内核**的 `reduce:int8:…` 的 `c_ms`，
下一次同键 lookup 会把它当作可用方案返回。这不是"证据错了"，是**入库口径漏了一个面**。
现在该条件与 `--emit` 无关（`UNSUPPORTED` `src/main.cpp:297` 那段）：非覆盖树在 `ledger_key()` 之前退出，
rc=2，`.sqlite3` 文件根本不被创建 —— 见 `dtype-refusal.txt`。

同时删掉了因此不可达的 `NOTE … model-only` 分支。该行为在
`docs/evidence/phase11/README.md` 与 `docs/evidence/phase12/` 的转录里是当时的事实，
按"历史转录不改写"保留；演进记在这里与 `docs/acceptance-mvp.md` 附录。

## 发射产物的真实影响（差分，不是嘴说）

`emit-diff.txt`（`python probe/phase45_emit_impact.py` 可逐字复现）：

| 算子 | 行数 | 对比的最新基线 | 变了什么 |
| --- | --- | --- | --- |
| conv | 542 | `docs/evidence/phase29/conv-hip-knob-check.txt` | **恰 1 行**：`0.03125f` → `0.031250f` |
| reduce | 268 | `docs/evidence/phase44/reduce-hip-width-plan.txt` | **恰 1 行**：同上 |
| gemm | 447 | 无字节基线（Phase 44 只记了"447 行"这个数） | 与 Phase 15 文件差 85 行，全是 Phase 16–44 的累积漂移 |

新 sha256（`std::to_string` 固定 6 位小数，所以字节必然动）：

```
conv   09e9b5706cba661cbc861bb4d96d450dd62520d21c2c3b602f09ce3ec7c80d3f
reduce c3a26462ed663e5550a65dcd0b071f9989a97fd8e90912fd4047633beeb66ef0
gemm   7a451491ed9b28f8c02ebe00159dc5b5a7ed8f85816755ec3cef79f43d1d6479
```

**演进注记**：Phase 29 的 conv 基线 `sha256=599a4e7183982b7101ed9441a8582bec59f9ae23bf450dffc8323a2bf79e3f18`
与 Phase 44 的 "conv vs phase29 baseline: EMPTY diff" 都是当时点为真；自 Phase 45 起那条 diff
恰好是上面这一行判定行。旧文件与旧转录一律不动，今天的 `emit/gemm/uk_amd.cpp` 起作为 gemm 的
**字节基线**（此前它从来没有被逐字节存过，这是这次顺带补上的覆盖缺口）。

真机复核（`device-verify.txt`）：三个算子在新判定行下全部 `verify=PASS max_rel=0`，
conv 选档仍是 `ocb=8 tile=16`，reduce 仍是 `width=16384 source=device cap=28672` —— 即 D9 的结果没被
D11 动到，`migrations=0 checks=6`。

## 门禁与摘除控制（`removal-controls.txt`，6/6 按预期）

`ctest` 现在是 **6 条**门：`smoke` / `acceptance` / `anchors` / `probe_hygiene` / `record_audit` /
**新增 `runner_tu`**。新腿 `dtype` 三档表在 `tests/acceptance.cpp:1905`。

| 控制 | 改了什么 | acceptance | runner_tu |
| --- | --- | --- | --- |
| C1 | 三处 `dtype` 接线拆掉 | 红（`tests/acceptance.cpp:316` 的 `dtype=` 分支） | 绿 |
| C2 | 判定行写死成 fp16 档 `0.062500` | 红（`:1899`，产物不再等于表） | 绿 |
| C3 | intel runner 改回 `0.03125f` | 红（`:1923`，源码文本腿） | 绿 |
| C4 | 在 `#ifdef UK_HAS_OCL` **体内**放语法错误 | **绿** | **红** |
| C5 | 判定行写死成与表相同的 `0.031250` | **绿（盲区，如实记）** | 绿 |
| C6 | `solve` 拒绝条件重新挂回 `--emit` | 红（`:1977`，新增的 no-emit 账本腿） | 绿 |

C4 是 `runner_tu` 存在的理由：**本机的构建根本不编译那具身体**（没有 Intel SDK 时
`UK_HAS_OCL` 为 FALSE，`runner_metal.mm` 干脆不进目标列表），所以它对 acceptance 的所有腿都是隐形的；
而 `third_party/CL` 的镜像头足够 `-fsyntax-only`，于是不需要 Intel 工具链、不需要设备、不需要链接，
就能真的做一次编译检查。门自带控制（一段故意写坏的 TU 必须被拒绝），否则会是一个装饰。

C3 暴露的口径也要写清：runner 那两处的**语义**在本机不可执行验证（没有 Intel 设备/工具链，没有 macOS），
能被机械核到的只是"它读表而不是抄字面量"这一层文本事实。

C5 是诚实的代价：只有 fp32 档能到达产物，所以任何腿都区分不了"从表里读"和"写死成同一个数"。
将来落地 half/int 内核时，这条必须换成能区分的检查（例如让两个 dtype 各出一份产物并比对其判定行）。

## 顺手修的第二个问题：分类器不能靠"提到过"

`probe_hygiene_check.py`（`src_touching` 在 `probe/probe_hygiene_check.py:63`）原来的判据是
"文件里出现带引号的 `src/` 路径 **且** 出现字母 w"。`runner_tu_check.py` 只读 `src/`，却被判成
"会改写 src/ 的探针"而要求它套上还原守卫 —— 那是假阳性。改成**按写入点判定**：走守卫 API，
或者写调用与 src 路径字面量在同一行/由同一变量相连；同时把门自己排除在外（它自己的正则字面量
恰好包含它要 grep 的每个模式，旧版是"靠巧合过关"的），并加 `MUST_BE_CLASSIFIED`（14 个已知真写手）
与 `self_test()` 两个方向的控制。

收紧的第一版把 8 个真写手漏掉了（窗口相邻规则追不到 `TARGET` 定义在 57 行外的写法），
`MUST_BE_CLASSIFIED` 立刻把它报成"门被解除武装"而不是绿 —— 这条控制按设计生效了一次。

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure          # 6/6
python probe/runner_tu_check.py                     # 含它自己的 broken-TU 控制
python probe/probe_hygiene_check.py --verbose       # 15 个 src 写手
python probe/phase45_emit_impact.py                    # 三份产物 vs 最新基线的 diff
python probe/phase45_d11_removal_probe.py docs/evidence/phase45/removal-controls.txt
./build/unikern.exe run --hcs samples/hcs_amd.json --op gemm --dims 512 512 512 --vendor amd \
  --ledger build/p45_dev/gemm.sqlite3               # 真机 verify=PASS
```

`dtype-refusal.txt` 里每条命令都是逐字可跑的（`build/p45_refuse/` 下的 kml 由该转录里
的 `printf` 生成；`--emit` 与不带 `--emit` 两条路径都覆盖）。

## 仍未闭合（不粉饰）

* **half/int 内核本身**不存在。D11 关闭的是"把不可生成的声明当成 fp32 定价并入库"这条路径，
  不是补齐规格里的 FP16/INT8 生成能力（G13 的另一半）。
* `runner_metal.mm` 在本机连语法检查都做不了（Objective-C++ + Metal framework）。
* `verify_fp32()`（`src/runtime.cpp:191`）仍只接 `tol.fp32_rel`；它只服务 fp32 向量，
  所以今天不是缺陷，但**整型位级档目前只在拒绝路径上成立**，一旦有 int 内核就要改它的签名。
* D8（差分参考/容差基准口径）与 D11 无关，仍未动。
