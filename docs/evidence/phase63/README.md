# Phase 63 — 差距审计 + 用真实厂商模型（Qwen3-Embedding-4B GGUF）驱动完整流程

日期：2026-10-30。主机：本 Windows 机器（RX 6600M / gfx1032，ROCm 7.1；
`backends` = amd=yes / apple=no / intel=no）。

## 一、GGUF 头解析（真实维度，纯 struct 解析无依赖）

`probe/phase63_gguf_header.py` 读
`Qwen3-Embedding-4B-Q4_K_M.gguf`（2,496,703,776 B）：
arch=qwen3，kv_count=36，tensor_count=398；
**embedding_length=2560、block_count=36、head_count=32、head_count_kv=8、key_length=128、
feed_forward_length=9728、context_length=40960、pooling_type=3（MEAN）**。
每层 7 个 GEMM 权重（GGUF dims 内维在前，已映射为 K→N 视图）：
attn_q 2560→4096、attn_k 2560→1024、attn_v 2560→1024、attn_output 4096→2560、
ffn_gate 2560→9728、ffn_up 2560→9728、ffn_down 9728→2560；token_embd 151665→2560。
转录：`gguf-header.txt`。

## 二、设备厂商资料（HCS，R9 导入路径）

`probe/phase63_device_probe.cpp`（hipcc `--offload-arch=gfx1032`）取设备自报量
（E26 纪律：平台断言由探针取数，不抄规格表）：
name=AMD Radeon RX 6600M、gfx1032、**multiProcessorCount=14**、warpSize=32、
maxThreadsPerMultiProcessor=2048、clockRate=1881 MHz、memoryClockRate=875 MHz（命令时钟）、
bus=128-bit、totalGlobalMem=8573157376、L2=2 MiB、sharedMemPerBlock=65536。
派生带宽（GDDR6 ×16 公式）= 875 MHz × 16 × 16 B = **224 GB/s**（厂商规格表说 16 Gbps/pin
= 256 GB/s——以设备自报为准，两数并存于记录）。
设备自报 14 CU 与 RX 6600M 规格的 16 CU 不一致——**设备自报胜出**（与 Phase 25 的独立
探针读数一致）。
`hcs_amd_device.json`：vram bw=224（measured）、flops=3370（14 CU×64×2×1881 MHz 推导，
后见"时钟注意"）、link=32（官方，设备不可自报——后由 R7.3 校准为 5.75）、nvme=7（measured）。

**时钟注意（实测推翻探针推导）**：ffn_gate 真机核内吞吐 3.85–3.89 TFLOPS，高于 3370 GFLOPS
的"峰值"推导 ⇒ `clockRate` 是探针时刻的变量值不是 boost 峰值——E26 教训的又一实例
（即使探针取的数也可能是时刻值；真机实测吞吐才是承重数字）。

## 三、完整流程（KML 声明 → solve → 生成 → 真机差分 → 账本 → 校准 → 复解）

KML 声明在 `workloads/llm/`（5 个，seq=512 的真实形状；前端按 R1.2 拒绝了我第一版塞进
JSON 的 `comment` 字段——未知字段拒绝是对的，出处说明移到本 README）。

| workload | KML dims | verify | max_rel | measured_ms | kernel_ms | ε_rel（出厂 HCS） | 校准后 ε_rel |
|---|---|---|---|---|---|---|---|
| qwen3_attn_q | gemm 512 4096 2560 | PASS | 0 | 13.68 | 2.71 | 1.78 | **0.0053** |
| qwen3_attn_output | gemm 512 2560 4096 | PASS | 0 | 12.28 | 2.61 | 1.49 | — |
| qwen3_ffn_gate | gemm 512 9728 2560 | PASS | 0 | 31.70 | 6.64 | 1.76 | **0.032** |
| qwen3_ffn_down | gemm 512 2560 9728 | PASS | 0 | 27.57 | 6.53 | 1.40 | — |
| qwen3_pool_reduce | reduce 1310720 | PASS | 0 | 0.634 | 0.427 | 0.69 | — |

全部 `migrations=0 checks=6`、`mode=resident stream_path=pinned`、rc=0。
转录：`run-<name>.txt`（出厂 HCS）与 `run-ffn_gate-calibrated.txt`、`run-attn_q-calibrated.txt`。

**calibrate（R7.3）**：5 行实测 → link 32 → **5.7479 GB/s**（中位）、constants_version 1→2
（`hcs_amd_device.calibrated.json`）。逐行 eff_bw：gemm 行 4.98–5.93 GB/s（与 Phase 15 的
5.43 同族）；reduce 行 50.7 GB/s 是**噪声主导的离群值**（copy_ms=0.207 ms 只含 10 MB H2D），
被中位数设计吸收——校准池的行类型构成值得后续注意。

## 四、模型级外推（只以 roofline 形式给出，边界明说）

- 实测 fp32 GEMM 核内吞吐：**3.85–3.89 TFLOPS**（ffn_gate：2×512×2560×9728 FLOP / 6.6 ms）。
- 每 token GEMM 工作量（fp32 密集视图）：36 层 × 202 MFLOP = **7.27 GFLOP/token**
  （不含注意力分数、softmax、norm）。
- 512-token prefill 外推（仅 GEMM 项）：每层 ≈26.5 ms（q 2.7 + k/v 各 0.68 + o 2.6 +
  gate/up 各 6.6 + down 6.5）× 36 层 ≈ **954 ms ≈ 537 tok/s**。
  注意力/softmax/norm 内核未在 MVP 原语内，实际更低——这是下界式外推。

## 五、能力边界（为什么"跑这个 GGUF"本身不在 MVP 内）

1. 原语词汇表：只有 gemm/conv/reduce——softmax、RMSNorm、注意力、Q4_K_M 反量化都不是
   KML 原语；RMSNorm 的逐通道缩放与 pooling 的逐通道批量归约需要批量原语。
2. dtype：fp32-only（D11 口径，非 fp32 全路径 loud 拒绝）。fp32 权重 ≈16 GB > 8 GB vram
   ⇒ 该模型 fp32 权重**装不下**；Q4_K_M（2.33 GiB）装得下但反量化不可表达。
3. 生成物形态：D5 决策的"完整自验证程序"（计时 + 差分 + 账本回写），不是可链接的
   per-layer 内核库——模型级运行需要运行时形态的生成物（后续阶段）。
4. 结论：**形状级完整流程验证 ✅（本文件第三节）；模型级运行 ❌（MVP 能力外，V1 语义）**。

## 复现命令

```bash
python probe/phase63_gguf_header.py "D:/Users/Administrator/.herdsman/models/qwen3-embedding-4b/windows/amd64/amd/Qwen3-Embedding-4B-Q4_K_M.gguf" > docs/evidence/phase63/gguf-header.txt
"/d/Program Files/AMD/ROCm/7.1/bin/hipcc.exe" -O2 -o build/p63_device_probe.exe probe/phase63_device_probe.cpp --offload-arch=gfx1032
./build/p63_device_probe.exe | tee docs/evidence/phase63/device-probe.json
./build/unikern.exe run --hcs docs/evidence/phase63/hcs_amd_device.json --kml workloads/llm/qwen3_ffn_gate.json --vendor amd --workdir build/p63/ffn_gate
./build/unikern.exe calibrate --hcs docs/evidence/phase63/hcs_amd_device.json --ledger ledger.sqlite3 --out docs/evidence/phase63/hcs_amd_device.calibrated.json
./build/unikern.exe run --hcs docs/evidence/phase63/hcs_amd_device.calibrated.json --kml workloads/llm/qwen3_ffn_gate.json --vendor amd --workdir build/p63/cal_ffn_gate
```
