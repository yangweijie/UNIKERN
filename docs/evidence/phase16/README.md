# Phase 16 — G5 / R8.5 运行时驻留守卫（AMD HIP 生成程序）

日期：2026-10-28。兑现 R8.5 的正向行为：**显式流式是唯一路径、超订必须大声报警而非静默**。

## 落地内容

1. **生成器（`src/codegen_hip.cpp`，gemm/conv/reduce 三程序同模式）**：`emit_residency_guard85()`
   向每份 HIP 程序发射守卫——`hipMalloc` 与 `hipPointerGetAttributes` 驻留断言折叠进单个
   `uk_mmalloc()` 调用（分配与检查不可分离）；`attributes.type == hipMemoryTypeDevice`
   之外（managed/host）→ `UNIKERN_ERROR what=oversubscription ptr=<名> type=<n>` 并退 5
   （上层重求解信号；普通 HIP 失败仍是退 4）。`UNIKERN_RESULT` 行尾追加
   ` migrations=<n>`（非设备驻留分配计数；显式拷贝路径恒 0）——脚本门禁判据，一字不差。
2. **Runner/CLI**：`RunOutcome.migrations`（-1 = 本后端不报告该 token）；`run_hip` 解析
   `migrations=`；`main.cpp` 的 RUN 行在后端实际报告时打印 ` migrations=<n>`（Metal/OpenCL
   进程内 runner 不打印，不冒充实测值）。
3. **验收（`tests/acceptance.cpp` 新增 `[R8.5]` 段）**：三份生成源都含
   hipPointerGetAttributes / `a.type != hipMemoryTypeDevice` / `what=oversubscription`
   / `" migrations="` 精确 token / `return 5;`；并断言生成源中不再存在裸 `hipMalloc(&` 直调点
   （新增未检查的分配点即红）。摘除即红两条腿见 `phase16-arm-probes.txt`
   （:1394 守卫发射、:1403 migrations token）。

## 关键设备事实（诚实修正）

任务口径的 `attributes.memoryType` 在真机工具链 **ROCm 7.1** 上不存在——首次真机编译即被
hipcc 拒绝（`no member named 'memoryType' in 'hipPointerAttribute_t'`）。实测
`hip_runtime_api.h:276-284`：字段名为 **`type`**（hipMemoryTypeDevice=2 / Host=1 /
Managed=3）。守卫改按真实头文件字段名 `a.type` 断言，语义不变——与 Phase 9/10
"设备事实经 API 实测，不照抄标签"同类修正。

## 真机证据（RX 6600M, gfx1032）

命令与 Phase 15 相同；账本键已存在故为 LEDGER HIT 轮（HIT 复用记录解，生成程序仍在
设备上完整执行）。二进制锚 `build/unikern.exe` md5 = `e554574a9c618f2676fd460651fedc8a`。

| op | verify | migrations | UNIKERN_RESULT 尾 token | LEDGER |
|----|--------|-----------|------------------------|--------|
| conv 64x64x32/3x3/64 | PASS max_rel=0 | 0 | `perf=64x64x32x3x3x64 migrations=0` | COMMIT ε_rel=71.7147 |
| reduce L=1048576 | PASS max_rel=0 | 0 | `perf=1048576 migrations=0` | COMMIT ε_rel=0.490443 |
| gemm 4096³ | PASS max_rel=0 | 0 | `perf=4096x4096x4096 migrations=0` | COMMIT ε_rel=3.45965 |

拒绝分支的设备级证据（`probe/phase16_residency_refuse.cpp`）：生成程序自身路径在本机硬件上
到不了拒绝分支（hipMalloc 恒返回设备内存），用 hipHostMalloc 的 host 指针代位非设备驻留条件——
`UNIKERN_ERROR what=oversubscription ptr=hipHostMalloc h type=1` + **exit=5**，
happy path（hipMalloc 指针）rc=0。

## 转录

- `phase16-real-device-runs.txt`：三次 run + 拒绝探针 + ctest + 字段名设备事实
- `phase16-arm-probes.txt`：两条摘除即红腿（残留标记 0、还原后 ctest 2/2 绿）
