# -*- coding: utf-8 -*-
"""Phase 64 续:修复合并引入的 acceptance-mvp.md 引用债(5 处,显式清单)。

合并 5ae8a2a 带来远端改写的 acceptance-mvp.md,其中 5 条引用行号漂移或形状弱。
逐条 grep 现取新行号(E23),符号紧贴被引 path:line(可核形状)。
"""
import io

P = 'docs/acceptance-mvp.md'
s = io.open(P, encoding='utf-8').read()

# (old, new, expected_count)
PAIRS = [
    # :3 弱 — 失败消息出处行号漂移(434→532),补符号 pick_intel_gpu(529,窗口内)
    ('失败消息「no Intel GPU platform/device found」改注出处（Mac 侧构建 + `src/runner_ocl.cpp:434`）',
     '失败消息「no Intel GPU platform/device found」改注出处（`pick_intel_gpu` 拒绝路径的日志行（Mac 侧构建 + `src/runner_ocl.cpp:532`））',
     1),
    # :65 弱 — tile 网格行号正确但无符号;补 sketch(14,窗口内)
    ('{16,32,64,128,256}²（`src/solver.cpp:16-17`）',
     '{16,32,64,128,256}²（`sketch` 的 tile 网格（`src/solver.cpp:16-17`））',
     1),
    # :65 弱 — 谓词行号漂移(42-57→51-70 feasible),补符号
    ('谓词 `m*n*4B*3 ≤ cap`（`src/solver.cpp:42-57`）',
     '谓词 `m*n*4B*3 ≤ cap`（`feasible`（`src/solver.cpp:51-70`））',
     1),
    # :255 不可核 — HCS rejected 漂移(110→213),补符号 hcs_import(212,窗口内)
    ('`HCS rejected: missing required fields: ...`（`src/main.cpp:110`）',
     '`HCS rejected: missing required fields: ...`（`hcs_import` 拒绝时打印（`src/main.cpp:213`））',
     1),
    # :412 不可核 — link_bw 使用点漂移(75→79 cost_ms),补符号
    ('（求解器仅在非 Unified 路径\n  使用，`src/solver.cpp:75`）',
     '（求解器仅在非 Unified 路径\n  使用，`cost_ms` 的数据通路项（`src/solver.cpp:79`））',
     1),
    # :564 不可核 — OCL 拒绝日志漂移(434→532),补符号
    ('见\n  `phase14/README.md:25` 与源码 `src/runner_ocl.cpp:434`），转录',
     '见\n  `phase14/README.md:25` 与源码 `pick_intel_gpu` 拒绝日志（`src/runner_ocl.cpp:532`）），转录',
     1),
]

for old, new, want in PAIRS:
    n = s.count(old)
    assert n == want, 'count %d != %d for: %s...' % (n, want, old[:40])
    s = s.replace(old, new)

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('applied %d repoints' % len(PAIRS))
