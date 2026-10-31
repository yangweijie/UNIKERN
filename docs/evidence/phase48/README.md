# Phase 48 — 证据目录里的产物字节，第一次被独立编译并在卡上跑通

主机：Windows 11 + AMD RX 6600M（gfx1032），`hipcc = D:\Program Files\AMD\ROCm\7.1\bin\hipcc.exe`
（就是 `HIP_PATH` 里那个，runner 用的也是它）。

## 这件事之前为什么是半验状态

所有真机读数都来自 `run --vendor amd`，而 runner 是**现场重新生成**程序再编译它。
所以 `docs/evidence/**/emit/uk_amd.cpp` 这些"被存下来当证据的文件"，
在 Phase 48 之前**从未被当作自己的字节编译运行过**。"证据文件就是被运行的程序"这句话有两半：

1. 文件字节 == 当次生成器的输出 —— 现在是一条常驻腿 `[R5.4-artifact-bytes]`
   （`[R5.4]` `tests/acceptance.cpp:2132`，纯 CPU，靠 `SOURCE_DIR` 定位仓库）；
2. 文件能被 hipcc 编译并在设备上 verify=PASS —— 现在是一条可复现脚本
   `probe/phase48_artifact_build_run.py`。

两半都必须有：只有 (1) 的话，存的字节可能根本编不出来；只有 (2) 的话，跑的可能不是存的那份。

## 三份产物的身份与读数

| 算子 | 行数 | 字节 | sha256（前 16） | runner 的 kernel_ms | 独立编译运行的 kernel_ms | max_rel / migrations |
| --- | --- | --- | --- | --- | --- | --- |
| gemm 512³ | 447 | 22543 | `7a451491ed9b28f8` | 0.118630 | 0.115690 | 0 / 0 |
| conv 256²×32·3×3×64 | 542 | 26956 | `09e9b5706cba661c` | 7.210240 | 7.253811 | 0 / 0 |
| reduce 2²⁰ | 276 | 13280 | `44231aef0e9567eb` | 0.459194 | 0.468358 | 0 / 0 |

`verify=PASS max_rel=0.000000e+00 migrations=0 checks=6` 两侧**完全一致**；
`kernel_ms` 差 2.4%/0.6%/2.0% —— 这台机器的 `kernel_ms` 离散度是已知事实
（AGENTS"这台机器"段 + 记忆里的实测），所以这点差异不构成任何结论，只登记不解释。

## 摘除控制（2/2 按预期红）

| 控制 | 改了什么 | 首条红断言 |
| --- | --- | --- |
| A1 | 生成器发射的字面量里换一个词（产物仍可编译可运行，只是字节动了） | `assert(fresh == stored)` 方向（`tests/acceptance.cpp:2060` 一系） |
| A2 | 直接给存盘证据文件追加一个换行 | `fresh.size() == stored.size()`（`tests/acceptance.cpp:2178`） |

A2 是这条腿存在的另一半理由：**证据文件应当是不可动的**。以前改动 `docs/evidence/` 里的产物
不会让任何门变红；现在会。守卫复原 byte-identical，末轮 `ctest` 6/6。

## 两台机器的诚实边界

- `--offload-arch` 必须显式给（AGENTS 第 2 条）：缺省时 hipcc 默认 gfx906，在这只卡上所有 launch
  静默失败而对拍全错。脚本因此把架构作为参数并默认 `gfx1032`（设备自报值），而不是让 hipcc 决定。
- 在没有 AMD 工具链的主机上脚本打印 `BUILD_SKIPPED` 并说明「这是对主机的断言，不是通过」，
  沿用 `has_apple_toolchain()` 的口径。
- 独立运行与 runner 运行是同一份字节，但**不是同一个进程环境**（runner 会准备 workdir 与日志）。
  差异只影响计时，不影响 verdict——这正是要对账的两列一致。

## 范围（以及为什么早期证据文件不在这条腿里）

被钉的是**当前口径下的三份记录**（`docs/evidence/phase47/emit/*`）。更早的
`docs/evidence/phase8/emit/uk_amd.cpp`、`phase15/emit/*`、`phase29/conv-hip-knob-check.txt`
**故意不核**：它们的生成器已经被 D9（运行时宽度）与 D8（差分基准换档）改掉，逐字节比较必然假红，
它们的价值是**历史时点的转录**（AGENTS 记录约定：数字不改写、口径变了标演进）。所以对旧证据文件，
"被运行的程序 = 存下的字节"这句话只能就**当时**成立；本相的腿与脚本核的是**今天**的口径。

## 复现

```bash
ctest --test-dir build --output-on-failure                  # 6/6，含 [R5.4-artifact-bytes]
python probe/phase48_artifact_build_run.py                  # hipcc 编译 + 上机运行存盘字节
python probe/phase48_artifact_removal_probe.py docs/evidence/phase48/removal-controls.txt
```
