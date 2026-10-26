# 需求文档 — UNIKERN MVP（三厂商 GPU 内核统一抽象与最优求解平台 · 第一阶段）

## Introduction

UNIKERN 把物理 GPU 抽象为统一数学表示（KML），把"内核定制"归约为确定性的数学优化问题。
本需求覆盖第一阶段（MVP），范围按评审决策修订（v1.1，2026-10-26）：

- **MVP 直接覆盖三厂商后端**：AMD（RDNA，ROCm 工具链）、Apple（Apple Silicon GPU，Metal 工具链）、Intel（Arc B 系列，Level Zero / SPIR-V 工具链）。
- **测试硬件 = 用户现役设备**：AMD 离散卡、Apple Silicon 整机、Intel Arc B 系列卡；**不含 NVIDIA**，全部验收不依赖 NVIDIA SDK 或硬件。
- **参考实现对拍**：正确性验证以 **CPU 参考实现**为基准（标量 + 分块两档），不依赖任何 GPU 厂商库。
- 打通完整闭环：KML 原语树 → 可行域 F → 确定性代价 C → 唯一解求解 → 三后端生成 → 差分验证 → 解账本 Ledger → 实测校准。
- 多级存储：Apple 统一内存平台走**场景 A（零拷贝全驻留）**；AMD / Intel 离散卡走**场景 B（显存 + RAM + 盘的显式流式）**。

后续阶段（本文档只标边界，不展开验收标准）：
- **V1**：NVIDIA 后端补全（PTX 路线、GPUDirect Storage 直读通路）；LLM 提名器
- **V2**：情报流自动化与 AOT 零日预开发
- **V3**：CPU+GPU 异构任务图分区与 CXL 远内存层

## Glossary

- **系统**：指 UNIKERN-MVP，本文档 SHALL 的主语。
- **KML**：Kernel Math Language，以 tile 为原子数学对象的内核表示（MLIR Dialect）；内核 = 可组合原语树。
- **算子签名**：算子种类 + 输入形状域 + 数据类型的三元组。
- **HCS**：硬件能力规格，L0 情报的归一化产物；字段带置信度标记。
- **HCS 指纹**：HCS 内容的哈希，用于账本键与工具链绑定。
- **F（可行域）**：多面体整数不等式组（容量 ≤ HCS 上限、tile 整除、依赖无冲突、原语能力覆盖）定义的候选集合。
- **C（代价模型）**：确定性解析代价函数，分层 roofline 展开（盘 / RAM / 互联 / 显存层级）；常量来自 HCS 与实测校准，无随机性、无学习参数。
- **x\***（唯一最优解）：argmin over X of ( C(x), canonical_form(x) )，字典序。
- **canonical_form**：KML 原语树的规范化序列化形式，用作字典序次键。
- **Ledger**：Kernel Solution Ledger 解账本（本地 SQLite）；键 = (算子签名, HCS 指纹, KML 原语树哈希)，值 = (解, 实测时间, 误差记录)。
- **流式分片**：显存不足时以 tile / layer / 权重分片为粒度的分层驻留单元。
- **场景 A（物理统一内存平台）**：Apple Silicon 等 CPU 与 GPU 共享统一物理内存的平台，零迁移语义。
- **场景 B（传统离散 GPU）**：显存 + CPU RAM + NVMe 盘分层，经 PCIe 互联，需显式拷贝与流式调度。
- **CPU 参考实现**：宿主机上的标量与分块两档参考内核，作为正确性对拍基准。
- **UCM**：统一计算模型，厂商中立的硬件抽象面（SIMD 形状、存储层级、张量原语族、同步原语）。

## Requirements

### R1 — KML 前端与原语树

**User Story:** AS 内核开发者, I WANT 以 tile 原语树声明 GEMM / 卷积 / 归约算子, SO THAT 同一声明可在三个厂商后端生成实现。

1. THE 系统 SHALL 提供 KML 前端，支持以 tile、layout、原语树声明 GEMM、卷积、归约三类算子。
2. WHEN 用户提交非法原语树（类型不匹配或结构不完整），THE 系统 SHALL 返回含违规节点的结构化错误，并拒绝进入求解流程。
3. THE 系统 SHALL 为每个合法原语树生成唯一的 canonical_form 规范化表示。

### R2 — 多面体可行域判定（F）

**User Story:** AS 求解器, I WANT 以多面体约束筛选候选内核, SO THAT 不可行候选在枚举前被淘汰。

1. THE 系统 SHALL 将显存/共享内存/寄存器容量、tile 整除性、依赖冲突、原语能力覆盖表达为整数不等式组 F(x)。
2. WHEN 候选 x 的 F(x) 判定可行，THE 系统 SHALL 将 x 加入候选集 X。
3. IF 候选 x 的 F(x) 判定不可行，THE 系统 SHALL 输出对应的约束违反记录，并从候选集淘汰 x。
4. THE 系统 SHALL 报告可行域规模 |X| 供上层决策。

### R3 — 确定性解析代价模型（C）

**User Story:** AS 求解器, I WANT 确定性的解析代价函数, SO THAT 解的选择可复现且可审计。

1. THE 系统 SHALL 以分层 roofline（算力项、带宽项、延迟项）计算 C(x)，常量取自 HCS 与实测校准。
2. THE 系统 SHALL 保证 C 对同一线程内的同一 x 返回相同值（无随机性、无学习参数、无可变全局状态）。
3. WHEN HCS 层级常量更新，THE 系统 SHALL 在下次求解采用新常量并记录常量版本号。

### R4 — 唯一最优解求解器

**User Story:** AS 平台用户, I WANT 同一输入得到同一最优解, SO THAT 结果可复现、可审计。

1. THE 系统 SHALL 按 ( C(x), canonical_form(x) ) 字典序在 X 上求 argmin 得 x\*。
2. THE 系统 SHALL 对固定 (F, C, 字典序) 的同一线程输入产生逐字节相同的输出 x\*。
3. WHEN 枚举与搜索组件产出候选，THE 系统 SHALL 仅将其作为提名，最终排序与选择由 C 决定。
4. IF 可行域 X 为空，THE 系统 SHALL 返回不可行结论与首个约束违反记录，并终止求解。

### R5 — 三厂商后端代码生成

**User Story:** AS 平台用户, I WANT 从同一 KML 原语树生成 AMD / Apple / Intel 三种内核, SO THAT 内核跨厂商复用且生成路径独立。

1. THE 系统 SHALL 从 x\* 的 KML 原语树独立生成三厂商后端源码：AMD 生成 HIP C++（经 ROCm comgr 编译）；Apple 生成 Metal Shading Language（经 metal 工具链产出 metallib）；Intel 生成 OpenCL C / SYCL（经 ocloc / Level Zero 工具链产出 SPIR-V 构件）；生成输入仅限 KML 与 HCS（独立生成路线）。
2. THE 系统 SHALL 为每个后端产出可加载执行的可执行构件，内核输入输出通过显式参数传递。
3. THE 系统 SHALL 在每个生成产物中记录对应 HCS 指纹与工具链版本指纹。

### R6 — 正确性差分验证

**User Story:** AS 平台用户, I WANT 以 CPU 参考实现自动验证三后端输出, SO THAT 只有正确的解进入账本。

1. THE 系统 SHALL 将每个后端生成内核的输出与 CPU 参考实现输出按算子容差策略比对。
2. THE 系统 SHALL 采用分级容差：整型与索引类结果要求位级一致；FP32 结果相对 ULP 容差默认 ≤ 2^-5，FP16 结果默认 ≤ 2^-4，两者均可配置。
3. IF 比对失败，THE 系统 SHALL 拒绝将对应解写入 Ledger，并记录失败的后端、KML 原语树与差异摘要。

### R7 — Kernel Solution Ledger（本地 SQLite）

**User Story:** AS 平台用户, I WANT 解账本, SO THAT 见过的组合直接命中、跳过搜索。

1. THE 系统 SHALL 以本地 SQLite 数据库实现 Ledger，以 (算子签名, HCS 指纹, KML 原语树哈希) 为键写入与查询。
2. WHEN 查询键命中，THE 系统 SHALL 返回记录的解，并跳过枚举、可行域构建与代价认证。
3. WHEN 实测完成，THE 系统 SHALL 将 (x, 实测时间) 回写 Ledger，并用于校准 C 的层级常量。
4. THE 系统 SHALL 以 SQLite 事务方式写入 Ledger，保证进程中断后账本不含半条记录。

### R8 — 多级存储与显式流式调度（场景 A / 场景 B）

**User Story:** AS 小显存 GPU 用户, I WANT 工作负载超出显存时自动流式调度、统一内存平台零拷贝, SO THAT 大模型与大数据在三厂商设备上可运行且性能可预期。

1. WHILE 目标设备属于场景 A（统一内存平台），THE 系统 SHALL 采用零拷贝全驻留策略执行，并在放置记录中标记"统一内存"。
2. WHEN 目标设备属于场景 B 且工作负载驻留需求超过显存容量，THE 系统 SHALL 按 C 求解的分片粒度与预取距离执行显式流式调度。
3. WHILE 场景 B 流式执行进行中，THE 系统 SHALL 保持数据搬运与 SM/compute unit 计算重叠（双缓冲）。
4. THE 系统 SHALL 在场景 B 的候选数据通路（pinned 显式拷贝 / mmap 流式加载）中由 C 择优。
5. THE 系统 SHALL 采用显式流式路径作为场景 B 显存不足的唯一处理方式；IF 运行时缺页迁移率超过告警阈值，THE 系统 SHALL 记录违规事件并触发重新求解。
6. THE 系统 SHALL 在求解结果中输出数据放置层（统一内存 / 显存 / RAM / 盘）的显式决策记录。

### R9 — HCS 导入（MVP 半自动）

**User Story:** AS 平台运维者, I WANT 导入三厂商结构化 HCS, SO THAT 代价模型常量有据可依。

1. THE 系统 SHALL 接受符合 HCS Schema 的结构化输入（JSON / YAML），字段含置信度标记，覆盖 AMD、Apple、Intel 三厂商设备。
2. THE 系统 SHALL 校验 HCS 必填字段（显存或统一内存容量、显存带宽、计算单元数、互联带宽、存储层级表）；IF 必填字段缺失，THE 系统 SHALL 拒绝导入并列出全部缺失字段。
3. THE 系统 SHALL 在求解报告中输出所用 HCS 的版本与置信度摘要。

### R10 — UCM 能力覆盖与分层退化

**User Story:** AS 平台用户, I WANT 三厂商原语能力差异被显式管理, SO THAT 不出现静默性能降级。

1. THE 系统 SHALL 为 UCM 每个抽象原语定义能力区间，并映射三厂商后端的实现方式。
2. IF 某后端无法覆盖某原语能力区间，THE 系统 SHALL 将该区间对 KML 标记为该后端不支持，并将标记纳入 F 约束。
3. THE 系统 SHALL 在求解报告中输出每个解在各后端的原语覆盖情况。

## INCOSE 合规说明

- **主动语态**：SHALL 主语均为"系统"或明确角色。
- **无模糊词 / 逃逸条款 / 绝对词**：以可测条件替代（位级一致、ULP 阈值默认值已确认、告警阈值可配置、结构化错误）。
- **无否定句**：法律红线改写为正向生成方式约束（R5.1：生成输入仅限 KML 与 HCS）；R8.5 将"禁止隐式超订"表达为正向架构行为 + 运行时告警（Unwanted-Behavior EARS 模式）；R2.3 用"约束违反记录"表达证书语义。
- **单一思想 / 显式条件 / 术语一致 / 无代词**：术语一律引自 Glossary；场景 A / 场景 B 为已定义术语。
- **Solution-free 边界说明**：R2（多面体）、R3（roofline）、R4（字典序）属于平台核心命题"把定制变成数学优化"的 what 级约束，非实现细节。
