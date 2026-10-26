# Progress Log — UNIKERN MVP

## Session 2026-10-26
- [x] Phase 0 规格：requirements.md v1.1（R1-R10）+ design.md 提交 f0ea230
- [x] 用户确认三项技术决策（枚举谓词过滤 / 厂商源语言级 / C++20），指令"继续"
- [x] 创建规划文件（task_plan / findings / progress）；无历史会话需恢复
- [ ] Phase 1 环境探测 → 进行中
- [x] Phase 1 探测：g++ 13.2 / cmake 4.4 / ninja ✓；HIP SDK ✓（gfx103X）；MSVC 备选；SQLite 头缺失；ze_loader 缺
- [ ] Phase 2 MVP 骨架 → 进行中
- [x] Phase 2 骨架：CMake/g++13.2 构建通过；CLI 5 子命令；三后端源生成 stub 可用
- [x] 修复 4 个真实缺陷：双 libwinpthread 段错误（静态链接）、Ledger 键分隔符、
      Ledger 浮点精度、NDEBUG 断言失效
- [x] 冒烟测试 100% 通过（确定性/规范化/HCS 拒绝/账本往返+键隔离）
- [ ] Phase 3 核心域 → 进行中

## Session 2026-10-26（续）
- [x] Phase 3 核心域：KML 增加 op/dtype/dims[6]/tile/layout；canonical_form 支持
      尾零归一 + 子节点排序（交换律）；workload_of 输出 roofline 输入（GEMM/Conv/Reduce）
- [x] HCS schema v2：tiers[] 三层级（vram/unified/ram/nvme，各自带宽+置信度），
      兼容 v1 扁平；拒绝时列出全部缺失字段
- [x] Phase 4 求解核心：5×5 tile 网格枚举（|X|≤25）、容量谓词、分层 roofline 代价
      （算力/带宽/占用惩罚/场景 B 拷贝项）、字典序 argmin
- [x] 三厂商求解验证：AMD 12.54ms / Apple 5.29ms（零拷贝）/ Intel 12.02ms
- [x] 冒烟测试全绿（确定性/规范化/HCS/账本/场景 A·B）
- [ ] Phase 5 后端生成 + Runner → 进行中
