# -*- coding: utf-8 -*-
"""Phase 65 增量 1 收尾:task_plan 行 + progress/findings 记录(显式清单,带断言)。"""
import io

# ---------- task_plan.md ----------
P = 'task_plan.md'
s = io.open(P, encoding='utf-8').read()
old = '**in_progress（2026-10-31，增量 1 预登记）**：本增量只做 **fp16 gemm 全链路（AMD 真机）**'
new = '**complete（2026-10-31，增量 1，本机口径）**：fp16 gemm 全链路 AMD 真机闭合——'
assert s.count(old) == 1
s = s.replace(old, new)
old2 = '`[R6.8]` 追加 F16+Gemm 覆盖腿（既有 F16+reduce/I8 拒绝腿原样存活，已核对）；门 #7 加 gemm-f16；bf16/fp8/Q4_K_M 是后续增量 (V1) |'
new2 = ('`[R6.8]` 追加 F16+Gemm 覆盖腿（恰好一后端 covered、门=表值且恰好一次、F16+reduce 拒绝腿原样存活）；'
        '`[R6.7]` 计数 5/2→6/3；门 #7 加 gemm-f16（checked=14 PASS，fp32 三产物字节腿逐字节不变）；'
        '真机两 run 全 PASS max_rel=0（半量化输入逐位一致形状），fp16 kernel_ms ≈ fp32 一半；'
        '证据 `docs/evidence/phase65/`；fp16 其余 op/bf16/int8/fp8/Q4_K_M/流式/字节腿/calibrate 是既知缺口。`ctest` 7/7 |')
assert s.count(old2) == 1
s = s.replace(old2, new2)
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('task_plan.md updated')

# ---------- progress.md ----------
P = 'progress.md'
s = io.open(P, encoding='utf-8').read()
entry = '''
## 续53（2026-10-31，Phase 65 增量 1：fp16 gemm 全链路真机闭合）

- 预登记先行（task_plan Phase 65 行）：只做 fp16 gemm；非 HIP 各自具名拒绝；
  fp16 程序 resident-only（R8.3 流式机器不复制——大段复制是维护灾难，
  conv/reduce 的 stream_refused 先例正好是家法）。
- 落地五件：①`dtype_gap` 变 op 感知（F16+Gemm 放行，消息仍含 `dtype=<x>`）；
  ②`msl_op_gap`/`ocl_op_gap` 不再委托 `dtype_gap`，各自声明只发射 fp32
  （否则 fp16 树发射 fp32 内核 = D11 老洞复发）；③solver `feasible()` 的
  elem 按.dtype_size（F16=2）；④`hip_gemm_f16_program`：宿主 fp32 init→
  half 转换、内核 half 载入→fp32 累加（k 升序与参考同序）→half 存储、
  CPU 参考消费同一份半量化输入、verify 门=表值 2⁻⁴；⑤[R6.7] 计数 5/2→6/3、
  [R6.8] 追加覆盖腿、门 #7 SPECS 6→7。
- 真机：两 run 全 `verify=PASS max_rel=0`（init 全 2⁻⁸ 倍数、和保持小幅，
  half 输出逐位等于 fp32 参考——fp32 程序同款形状）；fp16 kernel_ms ≈ fp32
  一半（字节减半）。`run --vendor intel` 对 fp16 树具名拒绝零落库。
- 过程抓自己两个 bug（都是发射文本标识符没发射定义）：VM/VN/VK、PM/PN/PK
  在生成器里是局部变量，发射的 main 却没定义它们——-Wunused 警告当场暴露。
  修法对齐 fp32 家法：发射 `const int VM = ...` 行。
- 工具教训第三次：heredoc 吃反斜杠让替换串静默不匹配（两连击）——E69 纪律
  升级为：含引号/反斜杠的替换一律 Write 脚本文件，heredoc 只跑无转义纯文本。
- anchors：f16 生成器 +290 行把 codegen_hip.cpp/acceptance.cpp 引用全面推移
  （115 条 STALE）——`probe/phase65_stale_repoint.py` 门输出驱动机械重指
  （符号型 grep 验证、臂型取新段起点、组引用 `/1086`→`/1122` 手工 5 处），
  门恢复全绿（weak 7/8、unver 10/11）。
- fp16 的裸字面量腿删了：容差 2⁻⁴=0.0625 与 init 常数 `* 0.0625f` 数值相同，
  子串搜索分不开两种合法用途——计数腿（门=表值且恰好一次）承担钉住职责，
  边界写进臂内注释。证据 `docs/evidence/phase65/`。`ctest` **7/7**。
'''
assert s.count(entry) == 0
s = s.rstrip('\n') + '\n' + entry
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('progress.md appended')

# ---------- findings.md ----------
P = 'findings.md'
s = io.open(P, encoding='utf-8').read()
entry = '''
## Phase 65 增量 1 实测事实（2026-10-31，本机 RX 6600M / gfx1032）

- **fp16 gemm 真机读数**（512³，half 存储 / fp32 累加 / resident-only，两次
  run）：verify=PASS max_rel=0 measured 0.7725/0.8434 kernel 0.1166/0.1185。
  max_rel=0 的机制：init 锯齿全是 2⁻⁸ 倍数且周期 221 把和压在 half 精确可表
  的幅值内——**不能外推**为"fp16 gemm 恒 max_rel=0"，换输入分布即会出现
  ~5e-4 级 half 舍入（门 2⁻⁴ 仍富余）。
- **fp16 kernel_ms ≈ fp32 一半**（0.117 vs ~0.25 @512³）：字节减半的直接收益，
  roofline 方向性在真机上成立。
- **能力区间 op 感知后的拒绝拓扑**：F16+Gemm 恰好 AMD covered；Apple/Intel
  的拒绝理由各自命名自己的生成器（"msl/ocl ... dtype=fp16 has no ... yet"），
  不再借用 dtype_gap 的措辞——三个后端三个理由，coverage 报告说真话。
- **容差与 init 常数撞值**：fp16 容差 2⁻⁴ 与 init 锯齿步长 1/16 数值相同，
  "裸字面量不存在"型断言在 fp16 上不可写（E21 家族的边界案例）——只能靠
  计数腿钉住。写容差检查前先问"这个值还出现在哪"。
- **求解器 elem 按 dtype 后的 |X| 变化**：fp32 树不受影响（dtype_size(F32)=4），
  [R2] 的 22/25 网格腿原样绿；fp16 树的工作集减半意味着更大 tile 可行，
  c_ms=0.061（fp32 同形状 0.111）——cost 模型对 dtype 的方向性正确。
'''
assert s.count(entry) == 0
s = s.rstrip('\n') + '\n' + entry
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('findings.md appended')
