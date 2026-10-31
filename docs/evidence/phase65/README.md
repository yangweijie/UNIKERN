# Phase 65 增量 1 — dtype 轴：fp16 gemm 全链路（本机闭合记录）

日期：2026-10-31。主机：Windows，RX 6600M / gfx1032，ROCm 7.1（`backends` =
amd=yes / apple=no / intel=no）。Phase 63 差距审计第②项（fp32-only）的
第一片：fp16 gemm 从 KML 声明到真机差分验证全链路走通。

## 交付了什么

- **能力区间变为 op 感知**：`dtype_gap` 只对 F16+Gemm 放行；F16 其他 op、
  I8、fp8/Q4_K_M 维持具名拒绝（消息仍含 `dtype=<x>`，[R6.8] 匹配的 token）。
- **非 HIP 后端各自具名拒绝**：`msl_op_gap`/`ocl_op_gap` 不再委托 `dtype_gap`，
  各自声明"本后端只发射 fp32"——否则 fp16 树会把 fp32 MSL/OpenCL 内核写进
  fp16 的账本键（D11 老洞）。非 HIP 产物走既有 `refusal_source` 通道
  （纯注释文件，门 #7 的 OCL 语法腿可解析）。
- **求解器工作集按 dtype**：`feasible()` 的 `elem` 从硬编码 4 改为
  `dtype_size(tree.dtype)`（F16=2）——fp16 树的字节工作集减半，不再按 fp32
  定价可行域。
- **新生成器 `hip_gemm_f16_program`**（258 行产物）：
  - **resident-only**：fp32 程序的 R8.3 流式机器不复制；payload > 0.85·vram
    时 `stream_refused` 拒绝（conv/reduce 先例，R10：绝不静默回落）；
  - 宿主保留 fp32 init 值 → half 转换在主机侧；内核 half 载入 → fp32 累加
    （k 升序，与 CPU 参考同序）→ half 存储；
  - **CPU 参考消费同一份半量化输入**：max_rel 只度量 half 舍入，不混入输入分歧；
  - verify 门 = `dtype_tol_literal(F16)` = 表值 0.062500（2⁻⁴），不是裸字面量；
  - 账本键：`op_signature` 本就编码 dtype → fp16 行与 fp32 行天然隔离（R7.1）。

## 真机读数（逐字复现）

```
./build/unikern.exe solve --hcs samples/hcs_amd.json --kml samples/kml_gemm_f16.json \
    --emit build/evidence65/gemm_f16 --ledger build/evidence65/s.sqlite3
./build/unikern.exe run --vendor amd --hcs samples/hcs_amd.json \
    --kml samples/kml_gemm_f16.json --ledger build/evidence65/s.sqlite3 --emit build/evidence65/gemm_f16
```

| 次 | verify | max_rel | measured_ms | kernel_ms | 门 |
|----|--------|---------|-------------|-----------|-----|
| 1 | PASS | 0.000000e+00 | 0.7725 | 0.1166 | 2⁻⁴ |
| 2 | PASS | 0.000000e+00 | 0.8434 | 0.1185 | 2⁻⁴ |

**max_rel=0 是诚实读数**：init 锯齿值全是 2⁻⁸ 的倍数且和保持在小幅值内，
半精度输出恰好逐位等于 fp32 参考这一形状（fp32 程序同款现象）。fp16 kernel_ms
≈ fp32 的一半（字节减半）。epsilon_rel ≈ 12：c_ms 模型对 fp16 无校准数据
（与 Phase 64 三原语同族），实测已按 R7.3 入账本待 calibrate。
诚实拒绝：`run --vendor intel --kml <fp16>` 打具名 UNSUPPORTED、零落库。

## 门与臂的变化

- `[R6.7-init-parity]`：a/b 计数 5/2 → **6/3**（+fp16 gemm 的 initA/initB）。
- `[R6.8-dtype]`：追加 F16+Gemm 覆盖腿——恰好一个后端 covered、非 HIP 拒绝
  理由含 `dtype=fp16`、fp16 程序的门是表值且**恰好一次**；F16+reduce 拒绝腿
  原样存活（区间没有静默变宽）。**fp16 无裸字面量腿**：容差 2⁻⁴=0.0625 与
  init 锯齿常数 `* 0.0625f` 数值相同，子串搜索无法区分两种合法用途——
  计数腿承担全部钉住职责（这条边界写进了臂内注释）。
- 门 #7：SPECS 6→7（gemm-f16）；HIP 腿解析**真 fp16 程序**（12 710 B），
  OCL 腿解析拒绝产物；`checked=14` PASS。
- `[R5.4]` 三条字节腿：fp32 gemm/conv/reduce 产物逐字节不变（本相未触碰
  fp32 发射文本），门下实测确认。

## 既知缺口（如实记录）

- fp16 的 conv/reduce/softmax/rmsnorm/channelscale、bf16、int8/fp8/Q4_K_M：
  后续增量；fp16 gemm 的流式执行（R8.3 机器）与字节腿、calibrate 也是。

## 基线清单（sha256，2026-10-31 现取）

```
9595ac4a3515204ed4aa24e0148d53ca205abfec6d0237a1a12650306637e02a  emit/gemm_f16/uk_amd.cpp
5bea304240395fb8146ecff3fcd8bac2a8392cf53602ea546aabe44a734df247  emit/gemm_f16/uk_apple.metal
a32ca36e001a99568d23902f442f4809328e213f4b7568dba95f7d33ef209b34  emit/gemm_f16/uk_intel.cl
```

`phase65-amd-run.txt` 是两次真机 run 的转录。
