# Phase 8 证据产物（docs/evidence/phase8/）

本目录是 `docs/acceptance-mvp.md` 引用的**命令输出原文转录**，随仓库跟踪，
fresh checkout 可直接比对，也可在同等环境（AMD RX 6600M + ROCm 7.1 +
MinGW g++ 13.2）按下列命令原样复跑。数值为运行时实测，逐次略有波动属正常。

| 文件 | 生成命令 | 对应验收项 |
|------|----------|-----------|
| ctest_release.txt | `ctest --test-dir build --output-on-failure`（Release：-O3 -DNDEBUG） | 全部 |
| acceptance_tests.txt | `./build/unikern_acceptance.exe`（ctest 目标 acceptance 的直跑输出） | R1-R10 CPU 侧 |
| run_1024_first.txt | `./build/unikern.exe run --hcs samples/hcs_amd.json --dims 1024 1024 1024 --workdir uk_evidence/run1 --ledger uk_evidence/audit.sqlite3` | R5.2/R6/R7.3 |
| run_1024_second_ledger_hit.txt | 同上命令**再次执行**（同键） | R7.2 命中跳过搜索 |
| run_32768_streamed.txt | `./build/unikern.exe run --hcs samples/hcs_amd.json --dims 32768 32768 32768 --workdir uk_evidence/run_stream --ledger uk_evidence/stream.sqlite3` | R8.2/R8.3 真机流式 |
| calibrate.txt | `./build/unikern.exe calibrate --hcs samples/hcs_amd.json --ledger uk_evidence/cal.sqlite3 --out docs/evidence/phase8/hcs_amd.calibrated.json` | R7.3/R3.3 实测→互联带宽校准（v4） |
| run_calibrated.txt | `./build/unikern.exe run --hcs docs/evidence/phase8/hcs_amd.calibrated.json --dims 1024 1024 1024 --workdir uk_evidence/run1 --ledger uk_evidence/cal.sqlite3` | R7.3 校准后 ε 收敛（4.83→0.12） |
| hcs_amd.calibrated.json | 上条 calibrate 命令写出的校准 HCS（constants_version=2，link_bw_gbps=实测中位数 5.429 GB/s） | R3.3/R7.3 校准产物 |
| solve_emit.txt | `./build/unikern.exe solve --hcs samples/hcs_amd.json --emit uk_evidence/emit --ledger uk_evidence/solve.sqlite3` | R5.1/R5.3 |
| emit/uk_amd.cpp, emit/uk_apple.metal, emit/uk_intel.cl | 上条命令写入 `uk_evidence/emit/` 后原样拷贝（uk_amd.cpp 因 .gitignore 的 `uk_*.cpp` 构建产物模式以 `git add -f` 入库——此处是验收证据而非构建产物） | R5 产物本体 |
| backends.txt | `./build/unikern.exe backends` | R10 availability 语义 |
| ledger_dump.txt | `python -c`（sqlite3 只读 SELECT）转储 `uk_evidence/cal.sqlite3` 行（含 kernel_ms 校准输入列） | R7.3 实测回写 + 校准输入 |

注意：`*.sqlite3` 二进制本身不入库（.gitignore 构建产物口径）；账本内容以
ledger_dump.txt 文本为准，二进制可由上表命令复跑重建。
