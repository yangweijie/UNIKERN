# Phase 49 — D9 的提速主张**恢复**：Phase 44 的收回建立在一个从未生效的 A/B 上

主机：Windows 11 + AMD RX 6600M（gfx1032），hipcc 取 `HIP_PATH`（`D:\Program Files\AMD\ROCm\7.1`），
`-O3 --offload-arch=gfx1032`。

## 结论先说

| n | 宽度 4096 | 8192 | 16384 | 32768 | 4096→16384 |
| --- | --- | --- | --- | --- | --- |
| 16,777,216 | 1.2612 ms | 0.8074 | **0.7485** | 1.2355 | **1.685×** |
| 67,108,864 | 4.6965 ms | 2.6711 | **1.8798** | 2.7652 | **2.499×** |
| 134,217,728 | 9.2963 ms | 5.1283 | **3.3291** | 4.2776 | **2.787×** |

（每格是 3 轮交替取的中位数，`verify=PASS max_rel=0` 全程成立；原始行在 `width-env-matrix.txt`。）

所以 Phase 30/33 探针当年报的 **1.65–2.50× 是真的**，出厂路径同样测得出来，
而且 32768（= 名义 2048×14 向上取整的下一档）在三个尺寸上都**比 16384 慢** ——
设备自报量推出的 `cap=28672 → 2^floor → 16384` 确实落在采样最优附近。
**Phase 44 写下的"提速主张收回"是误判，本相更正**（历史行不改写，按时间标注演进）。

## 为什么 Phase 44 会误判（这是本相真正的内容）

Phase 44 没有钉宽度的通道，于是它用 `sed` 手改**已发射的源码**造出 "baseline-4096" 变体。
它的转录（`docs/evidence/phase44/device-width-plan.txt`）里那条变体**没有打印任何宽度横幅**，
而它的 `kernel_ms` 与 runtime 变体几乎相同（16.7M：0.7816 vs 0.7452；67.1M：1.9108 vs 1.8950）。
今天的实测把这两件事连起来了：**手改没生效，两臂都跑了 16384**，所以"出厂不复现"是
一次**同一配置对同一配置的比较**，而它被当成了 A/B，进而把真提速判死。

三条可复用的教训：

1. **处理组必须自报身份。** 一个不能自己声明"我这次用的是哪个配置"的 A/B，等于没有 A/B。
   conv 从 Phase 24 起就有 `UK_CONV_TILE`/`UK_CONV_OCB` 正是为了这件事（AGENTS 第 4 条）；
   reduce 缺这个通道，于是踩了坑。
2. **"两臂几乎相等"应当是警报，不是结论。** 我把它读成"提速不存在"，而它更可能意味着
   "两臂是同一个东西"。本相的对照实验证实了后者。
3. **计时要交替跑。** 单发首测在编译刚结束、GPU 空闲降频时会偏高：134M 第一次读到 3.819 ms，
   与交替后的 3.33 ms 差 15%。交替三轮之内同一配置复现到 ±3%。

## 落地的修法：`UK_REDUCE_WIDTH` 钉值通道

发射程序里的 `uk_reduce_width_plan()` 现在先读环境变量，再查设备：

- 生效时打印 `UNIKERN_INFO reduce_width source=env width=<w>`（`cap=0` 表示这是人工钉值）；
- 值不落在 `[256, n]` 时打印 `UNIKERN_ERROR what=reduce_width_pin_unfit` 并 **`exit(2)`**
  ——绝不静默回落到设备计划，否则"我钉过了"就是一句假话（R10.2 的口径）；
- 只读一次（两处读会让横幅与 launch 各说各话）。

上表就是用它测的：一个二进制，宽度在运行时决定并自报，手改源码不再参与。

## 臂与控制

`[R15c-width-pin]`（`tests/acceptance.cpp:2146` 起）核四件事：读在设备声明之前、
`source=env` 横幅存在、不可用值走 `exit(2)`、`getenv` 只出现一次。
`[R5.4-artifact-bytes]` 的证据基线随本相移到 `docs/evidence/phase49/emit/`
（reduce 276→**290 行**，`sha256=b8862c99…`；gemm 447 行 `7a451491…`、conv 542 行 `09e9b570…`
与 Phase 47/48 逐字节相同 —— 改动只落在 reduce）。

四条控制（`removal-controls.txt`）**都做了归属**：先常规跑（树红），再用一个影子
`SOURCE_DIR` 把 `[R5.4]` 变成恒真后重跑，红才能归到具体条款。这一步抓到我自己两个错误：

| 控制 | 破坏的条款 | 归属后的首条红断言 |
| --- | --- | --- |
| P1 | `getenv` 改读别的变量名 | `pin != npos`（钉值根本不存在） |
| P2 | 去掉 `std::exit(2)`（不可用值静默回落） | `src.find("std::exit(2)")` |
| P3 | 把声明挪到"使用它的那个 if 块之后" | `pin < use && use < refuse && refuse < decl && decl < query` |
| P4 | 同一个 knob 读两次 | `reads == 1` |

P3 是本轮最有价值的一条：**第一版臂只断言 `pin < query`（两个字符串的相邻关系），P3 照样绿**——
声明被移到查询前一行时"pin 在 query 之前"仍然成立，而程序已经是使用早于声明的死代码。
改成**单调锚点序列**之后 P3 才红。另外第一版探针里 P1 的替换字符串根本没匹配上
（改名后的文本 == 原文），一条**不改变任何字节的控制**会被记成"腿不承重"；
现在探针先断言变换确实改了字节，否则报 `INERT control`。

## 仍未闭合（如实）

- **P3 造出的产物其实编不过**（`pin` 先用后声明），而 ctest 里没有任何一条腿能看见这件事 ——
  文本臂不编译产物，`runner_tu` 只查 runner。目前的缓解是 `probe/phase48_artifact_build_run.py`
  （已指向 phase49 基线，三份全 `build rc=0 verify=PASS`），它需要 hipcc、因此**不在 ctest 里**。
- 32768/其它宽度的曲线只测了 4 档 × 3 尺寸；更细的宽度扫描与 conv 那种 occupancy 联动没有做。
- `iters` 的本机离散度仍然是"登记不解释"的口径（AGENTS"这台机器"段）。

## 关于 `width-ab.txt`（本相的第一遍）

它用 `sed` 手改已发射源码造两臂（4096 / 16384），交替三轮，结果与后来的钉值通道一致
（16.7M：1.2578 / 1.3358 / 1.3347 对 0.7485 / 0.8173 / 0.8141）。它证明"改动真的生效时，手改也测得对"，
但它**不能自报身份**——正是 Phase 44 栽的那个坑。所以最终数据以 `width-env-matrix.txt`
（每行都带 `source=env width=<w>`）为准，`width-ab.txt` 只作为交叉验证保留。

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure                    # 6/6
./build/unikern.exe solve --hcs samples/hcs_amd.json --op reduce --dims 16777216 0 0 \
  --emit build/p49/n --ledger build/p49/ln.sqlite3
hipcc "$(cygpath -w build/p49/n/uk_amd.cpp)" -O3 --offload-arch=gfx1032 -o build/p49/n.exe
for w in 4096 8192 16384 32768; do UK_REDUCE_WIDTH=$w ./build/p49/n.exe 2>&1 | grep -o "source=env width=[0-9]*\|kernel_ms=[0-9.]*"; done
UK_REDUCE_WIDTH=100 ./build/p49/n.exe; echo "rc=$?"          # 2 = 拒绝，不是回落
python probe/phase49_widthpin_removal_probe.py docs/evidence/phase49/removal-controls.txt
python probe/phase48_artifact_build_run.py                   # 存盘字节自己上机
```
