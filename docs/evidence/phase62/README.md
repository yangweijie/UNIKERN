# Phase 62 — R8.4 的 mmap 流式通路（C 择优结构 + 仿真执行路径 + 真机双臂）

日期：2026-10-30。主机：本 Windows 机器（RX 6600M / gfx1032，ROCm 7.1，hipcc 在位；
`backends` = amd=yes / apple=no / intel=no）。

## 缺口

`requirements.md:105`（R8.4）要求场景 B 在 pinned / mmap 两候选通路间**由 C 择优**；
`design.md:145` 明说两者均属 MVP（移到 V1 的只有 GPUDirect Storage）。实现里
`MmapStream`（`include/unikern/core.hpp:70`）只在枚举出现一次，
`sketch`（`src/solver.cpp:14`）只发 PinnedCopy——61 个相没有任何记录提过 R8.4。
判定：真缺口，不是有意识的降级。

## 平台探针（先探后建，Phase 25 纪律）

`probe/phase62_mmap_probe.cpp`（hipcc `--offload-arch=gfx1032` 编译；`--offload-arch=native`
在这台机报 "No AMD GPU detected"，故显式给架构）。读数（64 MiB 文件）：

```text
P0 device=gfx1032 file_mb=64
P1 PASS file=p62_mmap_probe.bin bytes=67108864
P2 FAIL hipHostRegister(mapped) msg=invalid argument
V1 register(mapped-view, Default) -> invalid argument
V2 register(VirtualAlloc heap, Mapped) -> OK
V3 hipHostMalloc(Mapped)+GetDevicePointer -> OK
VERDICT mapped-file streaming NOT supportable on this platform
```

结论：**真 mmap（GPU 可见文件映射）本平台不可行**（V1 连 Default 注册都被拒——驱动不能
页锁定不归它管的文件页）；**映射设备指针读本身可行**（V2/V3）。因此采用**仿真页**形态：
操作数真落盘、按分片 fread 进 `hipHostMallocMapped` 舞台缓冲、内核经映射设备指针读——
A 不走显式 H2D（链路流量从 1× 变 reuse×，在内核里真实发生），宿主 RAM 占用有界（舞台 2 分片，
不是整块 A）。

## 基线（硬约束 5：动手前先存）

`docs/evidence/phase62/baseline/`（改动前的三厂商产物，HIP 三条的 sha256 与 phase49 锚一致：
gemm `7a451491`、conv `09e9b570`、reduce `b8862c99`）。改动后 gemm 产物长大（447→**721 行**，
`sha256=6351e595…`），重录到 `docs/evidence/phase62/emit/gemm/`；conv/reduce 逐字节不变
（`cmp` 空输出，`[R5.4-artifact-bytes]` 的 conv/reduce 继续指向 phase49 基线）。

## 求解器（C 择优）

- `MmapStream`（`src/solver.cpp:40`）：gemm 场景 B 每个 tile 发 pinned + mmap 孪生候选；
  conv/reduce 不发（它们没有 mmap 执行路径——发候选会造成 solver 选程序里没有的东西）。
- `feasible`（`src/solver.cpp:56`）：mmap 候选以"nvme 层带宽 > 0"为 F 约束（R10.2 形态），
  没有该常量的 HCS 上 mmap 不可行并给出具名违反记录。
- `cost_ms`（`src/solver.cpp:94`）：mmap 成本 = nvme 页入项 + reuse×链路项 + 其余字节链路项，
  gemm-A 的 reuse = ceil(N/tile_n)。**支配性（先说破）**：mmap 成本 = pinned 成本 + nvme 项
  + reuse 放大 ⇒ 对任何有限 nvme 带宽 C 选 pinned——这是模型的结论不是省略；mmap 真正的
  优势（宿主 RAM 占用有界）不在时间模型里，如实记录为模型边界。
- |X|：gemm 场景 B 25→**50**（`expect_feasible`（`tests/acceptance.cpp:491`）为 44、
  `feasible_count`（`tests/acceptance.cpp:562`）为同 HCS 的 50）；conv/reduce 保持 25。
  新臂 `[R8.4]`（`tests/acceptance.cpp:1037`）钉全部上述事实 + 支配性。
- 摘除控制三件（`probe/phase62_r84_removal_probe.py`，转录
  `docs/evidence/phase62/removal-control.txt`）：R1 摘 mmap 候选 → 红归最早的计数臂
  `[R2]`；R2 摘 nvme 层 F 约束 → 红归 `[R8.4]` 自己的独有子腿；R3 只改注释 → 全绿。

## 生成程序（mmap 执行路径 + 旋钮）

- `uk_stream_pass_mmap`（`src/codegen_hip.cpp:1250`）：A 落盘（`uk_gemm_streamA.bin`，
  PM×PK×4 字节同 0.5f 图样）、fread 按 prefetch 距离 refill 两个映射舞台缓冲、内核经
  `hipHostGetDevicePointer` 的设备指针读 A（kernel 体零改动 = 加法次序不动，硬约束 3 的
  取数来源轴）；staging refill 由 done 事件门控；C 分片 D2H 与 pinned 同形。
- kref（kernel-only 参考）：预填两个舞台缓冲后 S 个分片内核背靠背——映射读的纯内核时间。
- `UK_GEMM_STREAM_PATH`（`src/codegen_hip.cpp:1339`）：env 旋钮，值 ∉ {auto, pinned, mmap}
  时 `what=stream_path_pin_unfit` 退 2（仿 `UK_REDUCE_WIDTH` 先例）；横幅与
  UNIKERN_RESULT 行自报 `stream_path=<pinned|mmap>` 与 `source=<plan|env>`（硬约束 4：
  处理组必须自报身份，两臂禁止 sed 手改源码）。
- rule 6 的 R8.4 形态：`eff_path`（`src/codegen_hip.cpp:1333`）为 mmap 时**验证段的 A 也走
  映射舞台路径**——verify=PASS 覆盖的就是被计时的执行。
- 字节腿：`[R5.4-artifact-bytes]` 的 gemm 基线随本相移到 `docs/evidence/phase62/emit/gemm/`；
  conv/reduce 不变。

## 账本路径身份门（E70）

第一版让 env 钉的 mmap 臂把 measured_ms 提交进了**与 pinned 解相同的账本键**（同键行实测列
被 237× 离群值污染）。修复：`stream_path`（`include/unikern/codegen.hpp:81`）进 RunOutcome，
`r.stream_path`（`src/runner.cpp:236`）由结果行解析，`r.stream_path`（`src/main.cpp:550`）
与解的规划路径不一致时提交门打 `LEDGER SKIP` 并 rc=0 不落库——env 臂是仪器读数，
不是解的测量。被污染的行由 pinned 臂复跑恢复（measured_ms 回到 1378.09）。

## 真机读数（RX 6600M）

复现命令（工作树根）：

```bash
# 平台探针
"/d/Program Files/AMD/ROCm/7.1/bin/hipcc.exe" -O2 -o build/p62_mmap_probe.exe \
  probe/phase62_mmap_probe.cpp --offload-arch=gfx1032
./build/p62_mmap_probe.exe 64

# resident 512^3（出厂 HCS）
./build/unikern.exe run --hcs samples/hcs_amd.json --op gemm --dims 512 512 512 --workdir build/p62/res

# 流式双臂 12416^3（合成 vram=2GiB HCS，理由见下）
./build/unikern.exe run --hcs docs/evidence/phase62/hcs_vram2g.json \
  --op gemm --dims 12416 12416 12416 --workdir build/p62/pin3
UK_GEMM_STREAM_PATH=mmap ./build/unikern.exe run --hcs docs/evidence/phase62/hcs_vram2g.json \
  --op gemm --dims 12416 12416 12416 --workdir build/p62/mm2

# 旋钮 unfit 值
UK_GEMM_STREAM_PATH=garbage ./build/unikern.exe run --hcs samples/hcs_amd.json \
  --op gemm --dims 512 512 512 --workdir build/p62/res3
```

**合成 HCS 声明**（`hcs_vram2g.json` 头部亦写明）：本机 D: 只剩 2.1 GiB、C: 487 MB，任何
payload > 0.85×8 GiB 的真超订形状其 A 文件 ≥ payload/3 ≈ 2.27 GiB 放不下 ⇒ 声明 vram=2 GiB
让 12416³（payload 1.85 GB）真实触发流式。**该读数表征 mmap 数据路径，不是真超订**；
真超订 mmap 读数（32768³ 级）受磁盘约束，留待有盘空间的机器。

| 臂 | verify | max_rel | measured_ms | kernel_ms | migrations/checks | 账本 |
|----|--------|---------|-------------|-----------|-------------------|------|
| resident 512³ | PASS | 0 | 0.935 | 0.123 | 0/6 | COMMIT（pinned，路径一致） |
| streamed pinned #1 | PASS | 0 | 1244.67 | 1091.93 | 0/6 | COMMIT |
| streamed pinned #2 | PASS | 0 | 1378.09 | 1214.89 | 0/6 | COMMIT（恢复被污染的行） |
| streamed mmap #1 | PASS | 0 | 55222.68 | 17413.82 | 0/4 | COMMIT（E70：修复前） |
| streamed mmap #2 | PASS | 0 | 54383.98 | 21126.60 | 0/4 | **SKIP**（env_path_pin） |

- mmap 臂的 verify 走的就是映射路径（rule 6）：`verify=PASS max_rel=0` 覆盖被计时执行。
- 旋钮 unfit 值：`UNIKERN_ERROR what=stream_path_pin_unfit val=garbage`，CLI rc=1，零落库。
- 横幅自报：pinned 臂 `stream_path=pinned source=plan plan=pinned`；mmap 臂
  `stream_path=mmap source=env plan=pinned`。

## 模型对账（如实，不修饰）

- reuse = ceil(12416/128) = 97；A = 12416²×4 B ≈ 616.7 MB ⇒ 映射模式下内核侧链路流量
  ≈ 59.8 GB。kernel_ms 17.41 s / 21.13 s ⇒ 映射读有效带宽 ≈ **3.44 / 2.83 GB/s**。
- 模型的 mmap 成本项按 HCS 官方链路 32 GB/s 定价 ⇒ 预测 ~2.0 s（copy 项），实测 e2e
  54.4 s——**模型把映射读吞吐高估约一个量级**（HCS 的链路常量描述 copy engine，
  不描述内核逐访问的映射读）。支配性**结论**不受影响（任何现实常量下 mmap 都被支配，
  C 选 pinned 不变），但 mmap 成本项的**数值**在接入映射读实测带宽标定之前不可信——
  记为模型边界，与本仓"模型错了就记模型错"的口径一致。
- pinned 臂 ε_rel≈4.9 与既有读数同族（官方链路 32 vs 实际 ~5.4 GB/s，R7.3 校准流程的
  已知输入），非本相引入。

## ctest

7/7（smoke / acceptance / anchors / probe_hygiene / record_audit / runner_tu /
generated_programs）。anchors 全绿前重指了 70 条被本相插入推移的引用
（`probe/phase62_stale_repoint.py`，机械 +64、例外 2014→2087 与 1599→1777，门仲裁）。
