# Phase 17 证据产物（docs/evidence/phase17/）

Intel conv/reduce/gemm 三算子**真机运行验证**（开放缺口①闭合）——Phase 14 的
OpenCL conv/reduce 只在无 Intel 设备的会话里做过 clang 语法检查（该会话的"本机"
是 AMD 机器）；本会话在 Intel Arc B390 机器上首次真机执行全部三算子并闭合
COMMIT→HIT，顺带修复两个真机暴露缺陷（E18/E19，见 phase17-defects-before-after.txt）。

环境：Intel Arc B390（OpenCL 3.0 NEO，驱动 32.0.101.9150，96 CU，27207761920 B
全局显存）+ oneAPI 2026.1 OpenCL.lib 导入库 + third_party/CL 厂商头；
构建 MSVC 2022 Community（vcvarsall + Ninja，Release，-DUK_HAS_OCL）。

| 文件 | 生成命令 | 对应项 |
|------|----------|--------|
| phase17-backends.txt | `build/unikern.exe backends` | R10 真探测：**本机（Intel 机器）intel=yes**——修正 Phase 14/15 会话在其"本机"（AMD 机器）写入的 intel=no 口径（那是机器相关读数，非代码变化） |
| phase17-ctest.txt | `ctest --test-dir build --output-on-failure` | 基线与收尾全绿（smoke + acceptance 2/2，含 Phase 10-16 全部验收臂） |
| phase17-gemm-commit.txt / -hit.txt | `build/unikern.exe run --vendor intel --hcs samples/hcs_intel.json --dims 1024 1024 1024 --workdir build/phase17/g2 --ledger build/phase17/capture.sqlite3`（同命令二次） | gemm 回归：verify=PASS max_rel=0 → COMMIT ε_rel=2.4714 → 同键 HIT（与 Phase 9 同键同 tile，行为不变） |
| phase17-conv-commit.txt / -hit.txt | `build/unikern.exe run --vendor intel --hcs samples/hcs_intel.json --kml samples/conv.kml --workdir build/phase17/c2 --ledger build/phase17/capture.sqlite3`（同命令二次） | **R6/R7 conv 真机首验**：verify=PASS max_rel=0 → COMMIT ε_rel=13.5646 → HIT |
| phase17-reduce-commit.txt / -hit.txt | `build/unikern.exe run --vendor intel --hcs samples/hcs_intel.json --op reduce --dims 1048576 0 0 --workdir build/phase17/r2 --ledger build/phase17/capture.sqlite3`（同命令二次） | **R6/R7 reduce 真机首验**：verify=PASS max_rel=0 → COMMIT ε_rel=0.883955 → HIT |
| phase17-defects-before-after.txt | 修复前后原文对照（E18/E19） | 真机暴露缺陷全记录 + 分级说明（runner 层无 ctest 回归位，沿 Phase 11 d 先例） |

注意：
- 账本二进制（capture.sqlite3）不入库（.gitignore 构建产物口径）；账本内容以
  各转录的 LEDGER 行为准，可按上表命令复跑重建。
- ε_rel 口径同前：\|C−T\|/T，T 取 HCS 常量预测。conv/reduce 的 ε 大源自 HCS
  flops 常数（预测峰值）与真实内核吞吐的差距 + 计时尺寸下的小负载开销占比——
  与 Phase 9/15 的口径一致，按实测报告不修饰。
- E18 修复后的 reduce 走的是**同一 P=4096 的发射**（修复前被错误拒绝）——
  修复不改变任何生成内核或发射参数，只删掉了错误的检查。
