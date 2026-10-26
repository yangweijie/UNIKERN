# Progress Log — UNIKERN MVP

## Session 2026-10-26（第一段）
- [x] Phase 0 规格：requirements.md v1.1（R1-R10）+ design.md → commit f0ea230
- [x] 用户确认三项技术决策（枚举谓词过滤 / 厂商源语言级 / C++20）
- [x] 创建规划文件；无历史会话需恢复
- [x] Phase 1 探测：g++ 13.2 / cmake 4.4 / ninja ✓；HIP SDK ✓；MSVC 备选；SQLite 头缺失
- [x] Phase 2 骨架：构建通过；CLI 5 子命令；三后端源生成 stub → commit d4b0f8f
- [x] 修复 4 个真实缺陷：双 libwinpthread 段错误、Ledger 键分隔符、浮点精度、NDEBUG 断言

## Session 2026-10-26（第二段）
- [x] Phase 3 核心域：KML op/dtype/dims[6]/tile/layout；canonical_form 尾零归一 + 子节点排序；
      workload_of 覆盖 GEMM/Conv/Reduce
- [x] HCS schema v2：tiers[]（vram/unified/ram/nvme + 带宽 + 置信度），兼容 v1 扁平；缺失字段全量报告
- [x] Phase 4 求解核心：5×5 tile 网格（|X|≤25）、容量谓词、分层 roofline、字典序 argmin → commit f606879
- [x] 三厂商求解验证：AMD 12.54ms / Apple 5.29ms（零拷贝）/ Intel 12.02ms

## Session 2026-10-27（Phase 5）
- [x] 真机探测：**AMD Radeon RX 6600M, gfx1032, 8176 MB, 14 CU**，hipcc 可用（ROCm 7.1）
- [x] Intel 探测结论：无 ocloc、无 Intel OpenCL 平台，仅 AMD OpenCL → Intel 后端本机不可验证
- [x] `include/unikern/codegen.hpp`：代码生成 + RunOutcome 契约
- [x] `src/codegen_hip.cpp`：HIP **完整可运行程序**生成器（分块追加完成，171 行）
      — 寄存器分块 GEMM、确定性初始化、CPU 参考、2^-5 分级对拍、hipEvent 计时、UNIKERN_RESULT 输出
- [x] `src/codegen_other.cpp`：MSL / OpenCL C 内核源（与 HIP 同构的寄存器分块）
- [x] `src/backends.cpp`：三后端改调 codegen；AMD 用 find_hipcc() 真实探测
- [x] 清理：`build*/` 与生成物加入 .gitignore 并 `git rm -r --cached`（原被误提交）
- [ ] **阻塞 1**：`src/runner.cpp` 编译失败 — 反斜杠折叠（E10），需零反斜杠重写
- [ ] **阻塞 2**：`src/main.cpp` 未适配 `generate(sol, hcs, tree)` 新签名
- [ ] 未完成：HIP 真编译 + 真运行 + 对拍；Ledger 写实测 ms；ε 报告

## 下一步（按序）
1. 零反斜杠重写 runner.cpp（前斜杠路径 + char(92) 判断 + 分块写入 ≤80 行）
2. main.cpp 适配三参 generate，加 `build`/`run` 子命令
3. 构建 → 生成 HIP 程序 → hipcc 编译 → 在 RX 6600M 上运行 → 读 UNIKERN_RESULT
4. 用实测 ms 覆盖 Ledger 的 c_ms 占位，报告 ε = |C − T|
