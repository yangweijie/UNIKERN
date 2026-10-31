# Phase 64 — 新原语：softmax / RMSNorm / 逐通道缩放（本机闭合记录）

日期：2026-10-30/31。主机：Windows，RX 6600M / gfx1032，ROCm 7.1（`backends` =
amd=yes / apple=no / intel=no）。本相把 Phase 63 差距审计的第①项（缺 Transformer
核心原语）推进到三原语全链路可解、可发射、HIP 真机差分验证通过。

## 交付了什么

- **KML 前端**：`softmax` / `rmsnorm` / `channelscale` 三个 op（arity=2：L,C；
  work model 各自独立：softmax 5·L·C ops / rmsnorm 7·L·C / channelscale L·C）。
- **三后端发射器**：HIP 程序（自验证 + 计时 harness）、MSL 内核、OpenCL C 内核。
- **CLI 面**：三原语经 `--kml` 可达（`--op` 仍只收 gemm|reduce）。
  样例声明：`samples/kml_softmax.json`、`samples/kml_rmsnorm.json`、
  `samples/kml_channelscale.json`（dims 65536×256）。

## 真机读数（逐字复现）

```
./build/unikern.exe solve --hcs samples/hcs_amd.json --kml samples/kml_<op>.json \
    --emit build/evidence64/<op> --ledger build/evidence64/s.sqlite3
./build/unikern.exe run --vendor amd --hcs samples/hcs_amd.json \
    --kml samples/kml_<op>.json --ledger build/evidence64/s.sqlite3 --emit build/evidence64/<op>
```

| op | verify | max_rel | measured_ms | kernel_ms | 容差门 |
|----|--------|---------|-------------|-----------|--------|
| softmax | PASS | 0.000000e+00 | 32.45 / 35.44 | 19.01 / 19.62 | 2⁻⁵ |
| rmsnorm | PASS | 2.072579e-07 | 28.96 / 26.98 | 14.45 / 14.65 | 2⁻⁵ |
| channelscale | PASS | 0.000000e+00 | 13.91 / 14.05 | 6.78 / 8.10 | 2⁻⁵ |

两次读数 = 当日两次独立 run；`LEDGER HIT`→`LEDGER COMMIT` 往返正常（R7）。
`emit/` 存三后端产物基线，sha256 见下方清单。`epsilon_rel` 1.08–4.30 是**如实读数**：
求解器的 roofline 对三原语暂无校准数据（c_ms=6.68 三者同值、明显低估），
实测 `measured_ms` 已按 R7.3 入账本，供后续 calibrate（R7.3/Phase 66 的输入）。

## 本相抓到并修掉的真实缺陷（全部由门或真机实测暴露，不是检视出来的）

1. **三个 HIP 程序真机编不过**（15–19 个 hipcc error）——ctest 七道门此前只覆盖
   gemm/conv/reduce，文本臂看不见「这份产物编不编得过」（E51 同形）：
   - 老式 `uk_mmalloc`（三份独立拷贝）里 `hipError_t e` 声明两次；
   - `hipFree(const_cast<void*>(p))` 作用在 `void** p` 上（非法转换且语义错）；
   - 计时段引用 verify 块作用域内的 X/Y（W/S）——出块即失效；
   - 计时段 `hipFree` 后多一个 `}`，main 提前闭合，`UNIKERN_RESULT` 落到文件域。
2. **OCL softmax 行指针缺 `__global` 限定**——门 #7 扩到三新 op 的第一次运行就红
   （`initializing 'const __private float *__private' with an expression of type
   'const __global float *' changes address space`）。
3. **MSL rmsnorm/channelscale 的 `L` 绑定到 [[buffer(2)]] 与 `Y` 冲突**（检视发现，
   本机无 Metal 前端无法编译验证——按Phase 54 纪律如实记为源码文本腿 + 明说的缺口）。

## 门的变化

- `generated_programs`（门 #7）SPECS 从 3 op 扩到 6 op：HIP + OCL 两腿各 6 产物
  语法检查（本机 `checked=12` PASS）；MSL 半仍由探针决定（本机两个 clang 均不认
  `-x metal`，`PROG gap msl` 照旧）。
- 已知缺口（如实记录，不掩盖）：三新 op 尚无 `[R5.4-artifact-bytes-*]` 字节腿、
  无 acceptance 臂、`[R20-nonhip]` 次序钉不覆盖三新 op 的累加次序。这些是
  Phase 64 收尾/Phase 70 仪器的既知范围。

## 基线清单（sha256，2026-10-31 现取）

```
c5233cd147e7321ce0fdf7056d8fc1144a65b1aa51243a8c6919e55a90c91637  emit/channelscale/uk_amd.cpp
9c0429f85c87dfe0dd50560fee4acf3a32f01374e81671b48e6c747f2aba0aa8  emit/channelscale/uk_apple.metal
94f677e17e4a8fae1e114d2465a7574850337f295265e1a272d56b5f85d1c938  emit/channelscale/uk_intel.cl
9788f179ffdb0c7c9e2abaeaa5c7024196a0dec882cd18d4bb566d65b98e587b  emit/rmsnorm/uk_amd.cpp
51bd48b7e96aad752fee42d388a267d2393c5650c272a586d27805bbf62d2b77  emit/rmsnorm/uk_apple.metal
70044be3eba2fdc5c517399ed15786a59f85014ead9a1a9a6418e2464a5a0495  emit/rmsnorm/uk_intel.cl
cbbff87491b4bbfd716ed9810255b899a10043b6977162fc123a6faf55369f91  emit/softmax/uk_amd.cpp
c125a0c37be8b2fbdb90644829ef4b42ad3d305283e7ebb5a5811fd9d196dce1  emit/softmax/uk_apple.metal
62cb4aa8e16bc51ced1487f797bbcdfa3e74890dcc6d9b8dc38d8c0c734112be  emit/softmax/uk_intel.cl
```

`phase64-amd-run.txt` 是两次真机 run 的转录。
