# Phase 12 证据产物（docs/evidence/phase12/）

G1 的声明半边（KML 文本前端）、G8（R10.3 每后端原语覆盖报告）与 G9（账本键的算子 arity
身份）三件事的**命令输出原文转录**。所有命令在 Apple M4 / macOS 26.6.2 arm64 主机实跑，
CLI 产物 md5 随录（`95055c84b431c550b6e8d0ce324d3d49`）；毫秒读数为运行时实测，逐次浮动。

| 文件 | 生成命令 | 证明的事 |
|------|----------|----------|
| phase12-kml-and-device.txt | `probe/phase12_capture.sh` | 三份样例声明解析（含 canonical/op_sig/workload/覆盖表/回写源）；畸形声明 rc=2 且**一次列全 4 条违规**；solve 报告带覆盖三轴；**由 KML 声明驱动的 512³ 真机执行**（verify=PASS max_rel=0 → LEDGER COMMIT ε_rel 88.75 / 同键 HIT 再 COMMIT 84.28）；conv/reduce 声明在 ledger 任何访问前被拒（**账本文件未创建**） |
| phase12-key-identity.txt | `probe/uk_kml_probe.cpp`（库级探针） | G9：conv 三种形状签名互异（`conv:fp32:64x64x32x3x3x64` / `...x128` / `...x5x5x64`），gemm 签名逐位不变（`gemm:fp32:4096x4096x4096`），reduce 只带它的单长度；round-trip（export→import）三算子均 canonical 相同 |
| phase12-arm-probes.txt | `python3 probe/phase12_arm_probes.py` | 六条新腿逐一摘除各自转红的断言行号（见下表），收尾校验残留标记 0 + ctest 全绿 |
| phase12-ctest.txt | `ctest --test-dir build-apple --output-on-failure` | 当前树 2/2 全绿（acceptance 含新增 `[R1.1/2]`、`[R7.1]`、`[R10.3]` 三段） |
| `probe/uk_kml_probe.cpp`、`probe/phase12_capture.sh`、`probe/phase12_arm_probes.py` | — | 探针与转录脚本本体（编译产物不入库） |

## 六条新腿的带弹性证明（摘掉即红，红在哪一条断言）

> 下表行号是本轮复跑时的坐标；测试文件再插入就会漂移，引用请以"摘掉的腿 + 断言文本"为准（Phase 11 的同类漂移见 `docs/evidence/phase11/README.md` 末尾复验块）。

| 摘掉的腿 | 转红的断言（acceptance.cpp 行号） |
|----------|-----------------------------------|
| KML 每算子 arity 校验 | `!r.first.has_value()` @ 323（reduce 带两维本该拒） |
| 未知字段（拼写错误）拒绝 | `find("4 violations")` @ 313（漏一条即计数不符） |
| children 作用域切割（防父借子字段） | `ok.first.has_value()` @ 345（连合法父子都解析错） |
| `op_signature` 按 arity 编码 | `op_signature(c1) == "conv:fp32:64x64x32x3x3x64"` @ 783 |
| `canonical_form` 按 arity 编码 | `canonical_form(r1) == canonical_form(r2)` @ 796 |
| 覆盖表的 execution 轴取自后端 | `c.streaming == (c.backend == "amd:hip")` @ 1133 |

## 关键事实与边界（如实记录）

- **本轮闭合的是 G1 的声明半边，不是执行半边**：三类算子现在都能从文本声明、被求解、
  被规范化、进账本键；conv/reduce 的**可用内核生成**仍缺（Phase 13），执行仍由 Phase 11
  的三层拒绝点名拦着。覆盖报告把这件事变成可查产物，而不是注释里的承诺。
- **R10.2 的"纳入 F 约束"目前的真实形态**：不支持的区间表现为*生成/执行拒绝 + 报告点名*，
  **不是**求解枚举里的一条 F 谓词（solve 仍是 (hcs, tree) 纯函数，不知道后端是谁）。
  这条差异在此明确写出，避免"文档说进了 F 约束"的误读；真要合进 F 需要给解空间加后端轴，属 V1。
- **G9 的影响半径**：`gemm` 签名逐位不变（4096³ 仍写作三维）→ Phase 8/9/10 已入库的账本键
  与指纹不受影响（`[R7.1]` 臂直接断言这一点）。`reduce` 的 canonical_form 由
  `dims=L,0,0` 变为 `dims=L`，树哈希随之不同——但 reduce 从未有过已入库的实测行
  （它在 Phase 11 起就在落库前被拒），故没有历史产物需要迁移。
- **layout 只有一个可实现值**："NT" 是三个后端 emitted kernel 实际按 A[M][K]/B[K][N]/C[M][N]
  行主序索引的那个标签；前端**拒绝**其它 layout，而不是收下后按行主序默默执行——
  那正是 G7 的同类静默。cuTe 风格 layout 代数属 V1。
- **arity 单一权威**：`kml_arity()` 被前端校验、`kml_export`、`op_signature`、
  `canonical_form` 共用。写第二份"每算子几维"的规则迟早与键计算分歧，故禁止。
- **旧阶段的复现性会被本阶段改动波及**：`op_support_gap()` 的函数体在本轮改了形状
  （加了 layout 轴），Phase 11 的 `probe/phase11_arm_probes.py` 锚点因此失配——
  已同步改写锚点，脚本仍会 fail-closed（锚点数≠1 即中止，绝不半改）。
  引用别的阶段的可复现脚本时，改到被引用的函数体就要顺带回验。
- **本轮两次自捉**：① 键身份臂先写成了"reduce 只设 dims[0]"，被自己的断言抓出
  **结构体默认维会漏进签名**（于是才有 arity 单一权威这条设计）；② R10.3 CLI 臂的
  "无账本行"断言最初复用了 solve 用过的路径，而 solve 连只读查找都会创建账本文件 →
  断言变成恒真，改为独立新路径后才真正证明拒绝发生在开账本之前。
- **平台边界**：`--kml` 端到端真机验证只做了 Apple（Metal 运行时 JIT）；
  AMD 需 ROCm 主机、Intel 需 oneAPI 主机复跑；`runner_ocl.cpp` 仍只有 `-fsyntax-only`
  编译检查。
