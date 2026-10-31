# -*- coding: utf-8 -*-
"""Phase 64 收尾:progress.md / findings.md 追加记录(显式清单,带断言)。"""
import io

# ---------- progress.md ----------
P = 'progress.md'
s = io.open(P, encoding='utf-8').read()
entry = '''
## 续52（2026-10-31，Phase 64 闭合：新原语真机闭环 + 门 #7 扩 6 op）

- 起点：`5ae8a2a` 合并带来远端改写的文档（acceptance-mvp.md 1557→1082 行）与
  两条引用漂移 + weak/unverifiable 超上限；Phase 64 的实现代码（softmax/rmsnorm/
  channelscale）本来就在本地 ad36876 里。先还仪器债再干活。
- **合并回归修复**：`print_infeasible` 退 3 重指 `src/main.cpp:318-319`、
  `verify_fp32` 重指 `src/runtime.cpp:191`、`cmd_run` 提交门重指 540-566（主语
  换成真在行上的 `measured_ms`）；acceptance-mvp.md 五条弱/不可核引用逐条 grep
  现取重指（`pick_intel_gpu:532`、`sketch:16-17`、`feasible:51-70`、
  `hcs_import:213`、`cost_ms:79`）。anchors 门恢复全绿（weak 7/8、unver 10/11）。
- **真机实测抓出四个生成器缺陷**（三新 op 的 HIP 程序从未被任何门编译过——
  E51 同形）：老式 `uk_mmalloc` 重复声明 `hipError_t e`；`hipFree(const_cast<
  void*>(p))` 作用在 `void** p`（非法且语义错，guard85 的 const void* 版本不动）；
  计时段引用 verify 块作用域里的 X/Y/W/S；计时段多一个 `}` 把 main 提前关掉。
  修后三 op 真机全 `verify=PASS`（softmax/channelscale max_rel=0，rmsnorm
  2.07e-07），`LEDGER HIT`→`COMMIT` 往返正常。
- **门 #7 扩 6 op**：第一次运行当场抓到 OCL softmax 行指针缺 `__global`
  （address space 错误）——门扩到哪里，哪里就不再是文本盲区。检视另抓 MSL
  rmsnorm/channelscale 的 `L [[buffer(2)]]` 与 `Y` 绑定冲突（本机无 Metal 前端，
  记为源码文本腿 + 明说缺口）。修后 `checked=12` PASS。
- **工具教训（E69 家族再现）**：heredoc 里的 `\\\\n` 被吃掉一层，替换串静默不匹配
  ——源码级批量替换一律落成 Write 的脚本文件（fix_phase64_nonhip.py），
  heredoc 只用于无反斜杠的纯文本。
- 证据：`docs/evidence/phase64/`（emit 九份基线 + sha256 + 两次真机 run 转录 +
  README 写明可逐字复现命令与既知缺口）。`ctest` **7/7**。
'''
assert s.count(entry) == 0
s = s.rstrip('\n') + '\n' + entry
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('progress.md appended')

# ---------- findings.md ----------
P = 'findings.md'
s = io.open(P, encoding='utf-8').read()
entry = '''
## Phase 64 实测事实（2026-10-31，本机 RX 6600M / gfx1032）

- **三新原语真机读数**（65536×256，fp32，两次独立 run 取两行）：softmax
  verify=PASS max_rel=0 measured_ms 35.44/32.45 kernel_ms 19.62/19.01；
  rmsnorm PASS max_rel=2.072579e-07 measured 26.98/28.96 kernel 14.65/14.45；
  channelscale PASS max_rel=0 measured 14.05/13.91 kernel 8.10/6.78。
  容差门 2⁻⁵ 全部富余 5 个数量级以上。
- **求解器对三原语无校准**：c_ms 三者同值 6.68467（roofline 通用工作模型），
  epsilon_rel 实测 1.08–4.30——已按 R7.3 入账本等 calibrate，不是缺陷是待标定。
- **生成器缺陷家族**：三新 op 的 HIP 程序各自内嵌一份老式 `uk_mmalloc`
  （非 guard85 版），四处缺陷（重复声明 e / hipFree 误转型 / 计时段越界引用
  verify 块向量 / 多余右括号）在 ctest 七门下全绿地存活到真机编译才暴露——
  「新 op 落地必须同时进门 #7 的 SPECS」应成为纪律，否则文本臂永远看不见。
- **OCL C 地址空间**：行指针 `const float* x = X + row*C` 在 OpenCL C 是
  private 指针接 global 表达式，clang 拒绝；MSL 无此问题（device 限定写对了）。
  两个后端对同一缺陷的敏感度不同，语法腿两边都要。
- **MSL buffer 绑定纪律**：标量参数继续排在最后一个 buffer 之后（conv 的
  X=0..Oc=8 是家法）；rmsnorm/channelscale 第一版把 L 绑到 buffer(2) 与 Y 冲突，
  已改为 L=3/C=4。本机无 Metal 前端，此修复是检视级，真编译验证待 Metal 主机。
'''
assert s.count(entry) == 0
s = s.rstrip('\n') + '\n' + entry
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('findings.md appended')
