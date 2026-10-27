# Phase 9 证据产物（docs/evidence/phase9/）

Intel 真机验证（G4 Intel 半闭合）的**命令输出原文转录**，随仓库跟踪，
fresh checkout 可直接比对，也可在同等环境（Intel Arc B390 + oneAPI 2026.1 +
System32 OpenCL/ze_loader + MSVC 2022）按下列命令原样复跑。数值为运行时
实测，逐次略有波动属正常（本机桌面环境后台负载未受控，见 findings.md 收尾
审计事实）。

| 文件 | 生成命令 | 对应项 |
|------|----------|--------|
| phase9-environment.txt | `build/ocl-sanity/ocl_sanity.exe`（OpenCL 枚举 + 最简内核构建/运行/对拍） | 环境事实（平台/设备经 API 实测 + 运行时链路证明） |
| phase9-ocloc-probe.txt | `ocloc compile -file <emit>/uk_intel.cl -device bmg-g21 -out_dir emit/aot` | R5 ocloc AOT 真编译证据 |
| phase9-emit.txt | `build/unikern.exe solve --hcs samples/hcs_intel.json --emit build/phase9-emit --dims 1024 1024 1024` | R5.1/R5.3 三后端源码生成（intel 53 行） |
| phase9-verify.txt | `build/unikern.exe verify --vendor intel --hcs samples/hcs_intel.json --dims 1024 1024 1024` | R6 真机差分对拍（256³，verify=PASS，max_rel=0） |
| phase9-run.txt | `build/unikern.exe run --vendor intel --hcs samples/hcs_intel.json --dims 1024 1024 1024`（首次，LEDGER COMMIT） | R6.3/R7.3 真机实测回写（ε 实算） |
| phase9-hit-run.txt | 上条**再次执行**（同键） | R7.2 账本命中复用记录解 |
| phase9-calibrate.txt | `build/unikern.exe calibrate --hcs samples/hcs_intel.json --ledger ledger.sqlite3 --out samples/hcs_intel.calibrated.json` | R7.3/R3.3 实测→互联带宽校准（32→18.5877，v1→2） |
| phase9-calibrated-run.txt | `build/unikern.exe run --vendor intel --hcs samples/hcs_intel.calibrated.json --dims 1024 1024 1024` | 校准后 ε（残留为计算率缺口，如实报告） |
| phase9-backends.txt | `build/unikern.exe backends` | R10 availability 真探测（intel=yes） |
| phase9-ctest.txt | `ctest --test-dir build --output-on-failure` | 构建全绿（含 acceptance R10 镜像） |
| phase9-acceptance.txt | `./build/unikern_acceptance.exe`（直跑输出） | R1-R10 单测全绿 |
| hcs_intel.calibrated.json | phase9-calibrate 命令写出的校准 HCS（link_bw_gbps=18.5877，constants_version=2） | R3.3/R7.3 校准产物 |
| emit/uk_intel.cl, emit/aot/uk_intel_bmg.bin, emit/aot/uk_intel_bmg.spv | phase9-emit 生成的源 + ocloc AOT 产物原样拷贝 | R5 产物本体（见下方 git add -f 说明） |

注意：账本二进制（ledger.sqlite3）不入库（.gitignore 构建产物口径），
账本内容以运行输出转录为准，二进制可由上表命令复跑重建。

git 跟踪说明：`build/` 整体被 .gitignore 忽略，故上列产物（`emit/uk_intel.cl`、
`emit/aot/*.bin/.spv`）以 `git add -f` 入库——它们是验收证据本体而非构建产物。
