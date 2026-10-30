# AGENTS.md — UNIKERN

三厂商 GPU 内核生成器（AMD HIP / Apple Metal / Intel OpenCL）：KML 声明 → 求解 → 生成 →
真机差分验证 → SQLite Ledger。C++20 CLI + 静态库，规格见 `.monkeycode/specs/unikern-mvp/`。

## 三条命令

```bash
cmake -G Ninja -B build && cmake --build build      # 配置 + 构建
ctest --test-dir build --output-on-failure          # 全部 CPU 可测的验收臂
./build/unikern.exe backends                        # 这台机器实际能跑哪个厂商
```

判 `ctest` 绿之前必须确认链接真的发生过：构建失败时 `ctest` 会照常报告上一版二进制的 2/2 绿
（本项目已因此误判三次，task_plan.md E20）。

## 这台机器与"本机"这个词

三厂商真机验证分布在**三台机器**上，仓库文档里的"本机"曾经同时指代它们。写任何结论前先跑
`./build/unikern.exe backends`，并**在句子里点明主机名**。截至 2026-10-28 这台 Windows 机器：

- `amd=yes`（RX 6600M / gfx1032，ROCm 7.1，hipcc 在位）— 唯一能跑真机的厂商路径
- `apple=no`、`intel=no` — Apple M4 证据来自 macOS 机器（`docs/evidence/phase10/`），
  Intel Arc B390 证据来自另一台 Windows 机器（`docs/evidence/phase9/`）
- 因此 `run --vendor intel|apple` 在这里必然 rc=1 且零落库，那是**诚实拒绝**，不是回归
- 这只卡的设备自报值（Phase 25 用独立探针实测，别抄别处的数）：`sharedMemPerBlock =
  sharedMemPerMultiprocessor = maxSharedMemoryPerBlockOptin = 65536`、`warpSize=32`、
  `multiProcessorCount=14`、`maxThreadsPerMultiProcessor=2048`；
  **48 KB 动态共享内存默认上限在本机不成立**（65536 B 请求不做 `hipFuncSetAttribute` 也能 launch）

## 改内核生成器时必须守的硬约束（三条 → 九条；4/5 由 Phase 24 实测逼出，6 由 Phase 26 缺口逼出，7 由 Phase 30 的宽度-精确性耦合逼出，8 由 Phase 45 的容差表与本机被 `#ifdef` 掉的 TU 逼出，9 由 Phase 47 换差分基准逼出）

1. **生成源码里零反斜杠**（E10）；路径一律前斜杠；源码只用 Write/Edit 改，不用 heredoc。
2. **hipcc 必须带 `--offload-arch`**（E12）：缺省时它默认 gfx906，在 gfx1032 上所有 launch
   静默失败而对拍全错。runner 已经探测架构并注入旗标，别绕开它自己拼命令行。
3. **改算术顺序=改结论**。conv/reduce 的分块只允许改取数来源（全局内存 ↔ LDS）与复用度，
   每个输出/通道的累加次序（`c→kh→kw` 升序）一动，`max_rel` 就不再恒 0，已入库的逐位对拍
   证据全部失效。`[R20]` 臂按字节钉住了那几段嵌套，改动前先读它。**非 HIP 两侧也有自己的次序钉**
   （`[R20-nonhip]`，Phase 55：conv `c→kh→kw`、reduce `X 累加→P 写→P 折叠→Y 写`、gemm `k0→k→r/c`，
   OpenCL 与 Metal 各一份，按**单调序列 + 累加语句次数=1** 核）。三条字节腿只说"有东西动了"，
   这条臂说"动的是折叠次序还是注释"——前者让 `docs/evidence/phase9|10` 的读数不再可比，必须单独可见。
4. **改共享内存字节数＝改选档，不是只改布局**。`uk_conv_plan` 把 `cand.smem` 交给
   `hipOccupancyMaxActiveBlocksPerMultiprocessor`，接受条件是
   `threads >= per_cu * dp.multiProcessorCount`；smem 变大 → `per_cu` 变小 → 更容易被判成
   "填满设备"。Phase 24 实测：仅把 `uk_sX` 的每像素步长从 `C` 改成 `C+1`，32² 的选档就从
   `tile=0` 跳到 `tile=8` 且慢 1.5×。**做布局 A/B 前先用 `UK_CONV_TILE` / `UK_CONV_OCB`
   把方案钉住**，否则读数是"布局 + 选档"的混合效应。
   reduce 从 Phase 49 起有同形通道 `UK_REDUCE_WIDTH`，**三后端共用同一个旋钮**（Phase 51：HIP 程序、`UK_REDUCE_WIDTH` 读取在 `src/runner_ocl.cpp:190`、`plan_reduce_width`（`src/runner_metal.mm:136`）；生效打印 `source=env width=`，值不在 `[256, n]`
   时 `exit(2)` 拒绝而不是静默回落）。**处理组必须自报身份**：Phase 44 没有这个通道，改用 `sed` 手改
   已发射源码造 baseline 臂，变换静默没生效、两臂同为 16384，于是把真实的 1.685×–2.787× 提速判成
   「不存在」并写进记录（E49，Phase 49 已更正）。**两臂差进入噪声区间时，先怀疑两臂同配置。**
   gemm 从 Phase 62 起有同形通道 `UK_GEMM_STREAM_PATH`（pinned/mmap 数据路径臂，横幅与
   结果行自报 `stream_path=`/`source=`，unfit 值退 2）；env 钉臂的测量**不落解的账本键**
   （路径 ≠ 规划路径时 `LEDGER SKIP` 零落库，E70）——自报身份必须接到落库层，
   否则只装饰不设防。
5. **性能实验动手前先存基线**（`solve --emit` 出产物 + `sha256` 入库到 `docs/evidence/phaseNN/`）。
   Phase 23 的回退只能说"行为一致"（发射程序 506→508，两行诊断事后才定位）；Phase 24 因为存了基线，
   回退的举证就只是一条 `diff` 空输出。**"我回退了"不是证据，`diff` 才是。**
6. **差分校验必须发射被计时的那个实例化**。生成的程序里方案只允许推导一次：`uk_plan` 来自声明形状，
   verify / warm / timed / kernel-only 全都用它。Phase 26 实测到的旧缺口是真实的——verify 段按自己的
   小实例（C=8）推导、计时段按真实形状（C=32）推导，conv 256²×32 上两者选到**不同**的 `(tile, OCB)`，
   于是 `max_rel=0` 证明的是后备内核 `uk_conv_g<1>`，而被计时的是 LDS 分块内核 `uk_conv<8>`。
   `[R20]` 的计数腿（`uk_conv_plan(` == 2、`uk_conv_launch(uk_plan,` == 4）钉的就是这一条。
   **"单一发射函数"不等于"单一方案"，别把前者当后者写进注释。**
7. **reduce 的精确性既不由宽度保证、也看不见；宽度也不再是发射期的常量**。差分验证与计时段
   现在共用**同一个**运行时规划的宽度（D9 之前 verify 写死 4096、计时用别的值——那是 rule 6
   在 reduce 侧的同形缺口，Phase 44 补上并有真机读数）；2²⁰ 验证实例发射点
   `Differential`（`src/codegen_hip.cpp:883`），宽度推导 `uk_reduce_width_plan`（`src/codegen_hip.cpp:841`）。
   Phase 33 的 32 格网格（**只测 2 的幂宽度**）显示：`n > 1.678×10⁷` 之后精确性由 `n` 与宽度的
   组合决定，2 的幂**不保证** —— n=5×10⁷ 时四档宽度全偏（含现役 4096，`rel=+5.44e−05`），
   而 3×10⁷ 时反而是 16384 偏 `+2.41e−04`、4096 精确。所有偏差都远小于门限 2⁻⁵ ⇒
    **准确说法（我一度写成"账本会安静收下带误差的结果"，那是过宽，Phase 34 已改）**：计时段的 `Y`
   拷回主机后**没有任何代码使用它**（发射点是 `"d2h tY"` 那句 `hipMemcpyAsync`，`src/codegen_hip.cpp:946`；
   Phase 34 原文写的 887 是当时的位置，现已被 D9 的插入推走），`max_rel` 只来自 2²⁰ 验证段
   ⇒ 这是**覆盖缺口**（该规模的结果不在验证范围内），不是"已入库证据错了"。
   Phase 35 把这条推进到**机制**：`width` 条线程各自把 `fill` 连加 `k = ceil(n/width)` 次，
   当 `fill` 是 2 的幂且 `k <= 2^24` 时逐位等于 `k*fill`；越过 `2^24*fill` 后 `+fill` 撞上
   round-half-to-even 平局，**部分和冻结**、答案短少任意多的量（实测 `width=1, L=33554433`：
   `result=262144.0`、`rel=-5.000e-01`，超门限 16 倍 ⇒ 是判 FAIL 而不是"误差大一点"）。
   自 D9（Phase 44）起 `width` 是**运行时**由设备自报量推导的（`uk_reduce_width_plan`，发射点 `src/codegen_hip.cpp:841`），地板 256 在发射的规划器里，由臂 `[R15c]` `tests/acceptance.cpp:2185` 一带
   按**算术文本**钉住（不再是某个常量值）。触发上面那个冻结仍需要 `L > 4.3e9` 元素 = 17.2 GB fp32
   = 本仓最大 vram 档的两倍，且 `L` 本身已被 D10 的输入校验限制在 `INT32_MAX` 内 ⇒ **边界而非现役 bug**。因此模型必须写成**连加**，
   不能写成一次乘法——乘法版在它被训练的那批格里逐位相同，于是 41 格"全中"里零信息。
   因此：任何"reduce 精确"的说法必须点名 `(工作负载规模, 验证实例规模, 宽度)` 三件事；
   想改宽度（D9）之前先重跑那张网格，不要用旧结论外推。
   **本条最初写成"非 2 的幂宽度才会破坏精确性"，Phase 33 把它否证并就地改掉（原先的说法留在
   `docs/evidence/phase31/README.md` 与 v24 里，按"转录/补遗不改写、只标演进"的约定处理）。**

8. **容差与能力区间只有一个表，且要覆盖每条 CLI 路径**。R6.2 的三档（整型位级 / FP32 2⁻⁵ / FP16 2⁻⁴）
   只由 `tolerance_for()`（`src/runtime.cpp:182`）给出：产物里的 `if (max_rel > …)` 由
   `dtype_tol_literal()` 从它生成（`src/codegen_hip.cpp:81`，三个算子各 1 处），两个进程内 runner 也读同一张表：
   `tolerance_for`（`src/runner_ocl.cpp:683`）与 `tolerance_for`（`src/runner_metal.mm:623`）。
   **别把数值再写成字面量**——写死一个恰好等于表值
   的字面量今天没有任何腿能区分（Phase 45 的控制 C5 就是这条盲区，如实记着）。同理 `dtype` 是能力轴：
   非 fp32 由 `dtype_gap()`（`src/codegen_hip.cpp:71`）在 `run`/`verify`/`solve`/`--emit` **每一条**路径上
   点名 `dtype=<x>` 拒绝。Phase 45 实测到的旧洞正是"只在 `--emit` 上拒绝"，于是
   `solve --kml <int8 树>` 照常把 `c_ms` 落进账本（为不存在的内核定价）。**写「某条路径不会留下 X」的断言
   之前，把该命令面的每条路径各测一次；文件型 ledger 的「不创建文件」不能推广到 SQLite ledger。**
   本机不编译的 TU 由 ctest 第 6 条 `runner_tu` 做 `-fsyntax-only`（`third_party/CL` 的镜像头够用）；
   `runner_metal.mm` 连这一步都做不到，只能是源码文本腿 + 明说的缺口。
9. **差分基准的形状不能等于被测内核的形状**。规格的 CPU 参考是**两档**（`requirements.md:10`：标量 + 分块）；
   reduce 自 Phase 47 起以分块档为基准，其分片数 `kRefReduceWidth`（`include/unikern/core.hpp:124`）
   **与内核的 pass-1 宽度无关**。两者对齐 ⇒ `max_rel` 恒 0，门永远绿而什么都不测（E32 的同形失败）。
   `[R6.9-basis]` 里的 `basis != kern` 与控制 B1 钉的就是这条。**换基准的前置条件是中性证明**（旧档与新档
   在 shipped 验证输入上逐位相同），不是换完再看有没有红。gemm/conv **不在可换范围**：它们的 CPU 参考与
   `[R20]` 按字节钉住的嵌套累加次序同形，换档 = 改折叠次序 = 让全部已入库 `max_rel=0` 改含义（第 3 条）。

## 探针会就地改写受版本控制的源码

`probe/*_arm_probes.py` 与 `probe/phase1[89]_*.py`、`probe/phase2[01]_*.py` 通过临时改
`src/` 来证明某条测试腿承重。这条保证现在是**机械的**，不靠自觉：

- 写 `src/` 的探针必须走 `probe/arm_probe_guard.py`（`GUARD.write` + `guard_main(main)`）——它在进入时
  按字节快照，退出时**重读比对**、全 `src/` 扫残留 `ARM-PROBE`，并把 `git status --porcelain src/`
  与**进入时**比较。判据是"无漂移"而不是"干净"：本仓长期带正当未提交工作，要求干净会让守卫一进来就红。
- `ctest` 现在是 **7 条门**：`smoke` / `acceptance` / `anchors` / `probe_hygiene`（第 4 条，守卫化写路径 + 守卫下入口 + ROOT 由文件位置推导）/ `record_audit`（第 5 条）/ `runner_tu`（第 6 条）/ `generated_programs`（第 7 条，Phase 50 落地时名为 `hip_programs`，Phase 52 扩到多后端后改名）：CLI 现场发射 gemm/conv/reduce 后，**每个后端三段**——可用性探针 → 该后端自己的 broken-source 控制 → 真产物。HIP 走 `hipcc -fsyntax-only -O3 --offload-arch=<设备自报>`；OpenCL C 走 ROCm 自带 clang 的 `-x cl -Xclang -finclude-default-header`（**不需要 Intel SDK、不需要 Intel 设备**，Phase 52 实测，原先记在 `docs/evidence/phase51/README.md` 的"要各自主机"半句已被否证）；MSL 那一半**由探针决定、不由名字决定**（`clang_list()` 逐个问本机可及的 clang：先问认不认识
`-x metal`，再问有没有 `<metal_stdlib>`）——本机两个候选都停在第一步（`error: language not recognized: 'metal'`，2026-10-29 实测，读数在 `docs/evidence/phase54/msl-probe.txt`），于是打 `PROG gap msl` 并附前端原话；换成认 Metal 的主机，同一段代码会自动继续跑控制与真产物；退 77（Skipped 而非 Passed）的条件是**一段都没跑**。文本臂看不见「这份产物编不编得过」，只有这条门看得见（E51）。
- `probe_hygiene` 的分类器按**写入点**判定而不是按「提到 src/」判定（E44），并自带 `MUST_BE_CLASSIFIED` + `self_test()` 两个方向的控制：任何收紧让已知真写手消失，报的是「门被解除武装」而不是绿。
- **Apple 专属步骤**（clang++ `-fobjc-arc`、`.mm` 探针、`build-apple`）在非 darwin 主机上由
  `has_apple_toolchain()` 拦住并打印 `SKIPPED`。phase11-14 曾把 `ROOT` 写死成 macOS 路径，
  因此在本机**从未运行过**——"探针存在"不等于"腿被跑过"。
- **控制要回答「哪一条款被它弄红」，不是「树红了」**（Phase 49）：先执行的腿会吞掉后面腿的归属 ——
  `[R5.4]` 的字节比较排在最前，任何改生成器的控制都先在它身上红，于是契约腿承不承重无人知道。办法是让更早的
  腿**构造性为真**再重跑：`probe/phase49_widthpin_removal_probe.py` 用影子 `SOURCE_DIR`（复制 `samples/`、
  `workloads/`、`src/runner_*`，把产物重新发射进去）做归属。**装置必须覆盖全部字节腿**：本仓现在有三条
  （HIP / OpenCL / Metal，见下条），只种一份基线会让另两条在每个影子控制上因"基线不存在"假红
  （Phase 53 实测、Phase 54 复验）。这一步当场抓出两个缺陷：一条**替换串没匹配上、
  因此不改变任何字节的惰性控制**（现在先断言字节真的变了，否则报 `INERT control`），以及一条只比较两个字符串
  **相邻位置**的弱顺序断言 —— 把声明移到使用它的块之后它照样绿；顺序必须按**单调锚点序列**钉
  （`pin < use < refuse < decl < query`，E50）。
- **三条字节腿，一份清单**（Phase 48/53/54 各补一条）：`[R5.4-artifact-bytes]` 钉 `uk_amd.cpp`、
  `[R5.4-artifact-bytes-cl]` 钉 `uk_intel.cl`、`[R5.4-artifact-bytes-msl]` 钉 `uk_apple.metal`；
  基线在 `docs/evidence/phase49|52|54/emit/`。**字节比较不需要编译器**——所以"本机没有 Metal 前端"
  绝不能顺带放过 MSL 的字节漂移（Phase 54 就是这样把最后一条后端也接进来的）。
  三条腿都**先逐 op 打印 `identical`/`DRIFT`、再断言计数**：同一个二进制里先 assert 的那条会把后面的
  归属吞掉（M2 实测：CL 腿红时 MSL 腿一行都不打）。
  **非 HIP 侧现在有两条臂、管两条不同的轴**（Phase 55/57）：`[R20-nonhip]` 管**次序**（单调锚点序列），
  `[R20-nonhip-expr]` 管**算式形状**（conv 的 xv/wv/FMA、reduce 的 pass-1/pass-2、gemm 的 As/Bs/内积
  逐字节片段 + 次数=1）。只有次序臂会漏"次序不变、表达式换形"，只有字节腿会对两者都只说"字节动了"——
  所以第 3 条的两半各需一条臂，且**两条控制才证得开两根轴**（E1 换式子 / E3 换次序）。
- **每条新腿必须跑它自己的摘除控制**。不跑控制的腿等于没有腿：E38 那条恒真的 `migrations=`
  存在性判据绿了 26 个相，直到有人真去跑对应的探针腿。
  **期望要具体到能区分失效模式的字段**（哪个 op、哪一半、哪条腿），只写到 rc 一层的话，控制脚本
  自己的解析 bug 会安静地跟着绿（E55：`split()[2]` 应为 `[1]`，"漂移到哪个 op"变成字面量 `DRIFT`）。
- **控制之间必须重建**。Phase 45 的探针第一版对「只改源码文本」的控制跳过 `cmake --build`，于是后两条
  控制跑在**仍携带前一条补丁**的二进制上，报出来的红断言属于别的控制（E42）。这与 E20 同族，但这次
  是**成功**构建造成的陈旧：还原了源码不等于还原了二进制。

## 记录约定

- `task_plan.md` — 阶段表 + 决策（D 系列）+ 错误台账（E 系列，防重复）；`findings.md` — 实测事实；
  `progress.md` — 会话流水。三者是本项目的工作记忆，不是可选项。
- 真机读数一律转录入库到 `docs/evidence/phaseNN/`，README 里写清可逐字复现的命令。
  **注意 runner 是「现场重新生成再编译」**：存盘的 `emit/uk_amd.cpp` 不等于被运行过——两半都要有：
  `[R5.4-artifact-bytes]` 核「文件字节 == 当次生成器输出」（改证据文件会让门红），
  `probe/phase48_artifact_build_run.py` 用 `HIP_PATH` 的 hipcc + 显式 `--offload-arch` 把**存盘字节**
  编译上机。缺前者则「跑的就是这份证据」无人可证；缺后者则存的字节可能根本编不出来。
  **历史转录里的数字不改写**，口径变了就标注时点演进（md5 锚、emit 行数锚都是这么处理的）。
- 写代码行号锚点（`file.cpp:123`）必须在写下的那一刻 grep 现取，不许事后回忆（E23）；
  写"源里必须有 X"的断言之前先数 X 出现几次，同一符号出现在两处时存在性判据恒真（E21/E24）。
- **改这些 Markdown 记录时，Edit 的锚点必须取整行（含结尾竖线），不许取长行的前缀**——
  前缀锚点会把单行表格行撕成两行，渲染错列的表就是错的索引（E27，一天内三次）。
  表格与代码段里的竖线一律转义；`ctest` 的 `anchors` 门现在同时核 `path:line` 与表格列数。
  **引用要写成门能看见的形状**（Phase 56 把这条instrument拆成三层，就是因为旧形状看不见）：
  可核的两种形状是 ①符号紧贴在被引 `path:line` **前面**（`tolerance_for`（`src/runner_ocl.cpp:683`））、
  ②引用臂时把**臂标签**放在前面（`[R20-nonhip]`（`tests/acceptance.cpp:2818`）——门会把标签映射到
  `  // -------- R` 臂头段与该臂打印的 banner，行号落在段外就报"标签主人/行主人"）。
  符号在**上一行**（Markdown 换行）算 weak、什么都没有算 unverifiable，两者各有一条**只许下降的上限**
  （Phase 58 收口：weak 61 / unverifiable 59；落地那天是 109/91；Phase 59/60 逐条还债后降到 **30/39**——
  降下来的原因是**读**，不是放松），所以"这条引用其实没被核过"
  是一个数字而不是一片沉默。上限不是拿来迁就的：它每次响都抓到本相自己新写的记录（E59/E62），修的是写法。
  **一条组引用 `path:A/B/C` 的每个数字都被核**（Phase 58）：组前的符号或臂标签对组内每个数都要成立，
  写下的那一刻就得确认它们属于同一个主人。**描述"过去错在哪"的句子不许构成活引用**：
  写成"main.cpp 的 557 行"这种纯文本，别把路径与行号拼成一条今天并不主张的引用。
  符号是按**整行配对 + 按位置过滤**取的（切片配反引号会把 "`）…、`" 配成一对，让一行第二个引用的
  符号隐形）；`path:NNN` 这类占位符不算符号。门自己也有 `self_test()`，两个方向都验（拒占位符、留
  `` `std::cout` ``）。
  行号仍然必须 grep 现取（E23）——层的存在不代表可以记数字；**控制脚本里写死的行号同样会漂**（E61：
  Phase 56 的控制把臂首行写死，Phase 57 插了新臂，它于是把一条真的错位引用当成"门应该绿"的案例）。
  **控制也不许假定常数**：同一条控制在 Phase 59 又失效一次，因为它假定"cap 恒等于实测"——债一降就有
  余量，它转而指控正常的门（E63）。现在它从门的汇总行**现读**上限再决定期望红还是绿。
  **引用之后的主语也算主语**（Phase 59）：「（发射点 `path:line` 那句 `X`）」这种写法现在会被核，
  但截断在子句分隔符（`、,;；。）|`）、下一条引用、40 字之内——旧规则因为怕串味而完全不读引用之后的文本，
  代价是本仓 20+ 处**行号已漂**的引用一直没人看得见。
  **停用词不算符号**（E64）：`double`/`static`/`return` 在这仓每个 C++ 文件都有，把它们配到引用前
  只会让那条引用**永久绿而什么都不核**。想给引用配名字时，先问"这个词出现在没有 X 的文件里也会吗"。
  **还债的真正产出是错行号**：Phase 59 逐条读 120 条计数债，其中一大半"没符号"其实是"号漂了"
  （`:887`→`:946`、`:840`→`:883`、`:587`→`:610`、`:739`→`:762`、`:805/:562/:530`→`:807/:660/:593`）；
  Phase 60 又清出八组（`:412`→`:435`、`:844`→`:946`、`:311-312`→`:552`、`:108/:113`→`:127`、
  `:110`→`:112/:213` 等），并且其中四条的**结论本身也已过期**——仪器管"号码指什么"，
  句子对不对只能靠重读 ⇒ 计数债不是装饰：它把"没人看过"变成一份可以逐条关闭的清单。
  **给引用配符号要同时过两道，顺序是先长度后窗口**（≥5 字，且出现在被引号 ±25 内）：
  Phase 59 给一行配了 `smem`——4 个字母，规则不认，于是"改了但没降"这种假完工。
  **臂名可以带 `/`**（`tests/acceptance.cpp` 真有一条臂标题是 `R6.2/R6.4`，一条臂覆盖两条需求行）：
  Phase 60 之前门的标签正则不含 `/`，把 6 处完全可核的 `[R6.2/R6.4]` 引用当成沉默。
  ⇒ 判据的词法必须等于文档实际写的形状，否则"绿"意味着"没读到"而不是"通过"。
  **同类两个洞（Phase 61）**：① code span 的正则不许要求 ≥2 字符——`` `Y` `` 无法配对会让**同一行
  后面每一对反引号错位**，主语凭空消失（9 条引用被计成盲）；② `^[A-Z]+\d`（排除 `R5.4`/`G10` 这类臂名）
  必须**大小写敏感**，否则连 `fp16_rel` 这种带数字的真标识符一起吞掉。两处都由 `self_test()` 双向锁住，
  且**真的跑过摘除控制**（还原后 `self_test()` 报 `GATE IS MUTE OR LOOSE` 并点名案例）。
  工具改文档时**插入点不许落在被改引用的反引号之内**（E68：20 处破 span，门不报红，只有读 dry 输出才看得见）；
  一次性批处理一律落成 `probe/*.py` 的显式列表（E69：heredoc 重定向让 `cat` 读自己的输出，写出 100 MB 垃圾）。

## 待决事项（不要替用户决定）

- **D8 已由用户决定并落地（Phase 47，口径 A）**：reduce 的差分基准从"fp32 索引序标量档"换成规格的
  **第二档（分块档）**，参考分片数 `kRefReduceWidth=1024` 与内核 pass-1 宽度解耦（见硬约束第 9 条）。
  决定前的关键实测（Phase 46）：规格的 CPU 参考本来就是**两档**（`requirements.md:10`），而出厂只有
  标量档；在 shipped 验证输入（常量 `2^-6`，n=2²⁰/2²⁴）上**两档逐位相同且等于闭式精确值** ⇒ 换档对
  已入库 `max_rel=0` 中性（真机复证：五档全 PASS，gemm/conv 产物空 diff）。换档后交替 `1e6/1e-6` @2²⁴
  不再把更准的一方判 FAIL（旧基准差 `4.160e-02` > 2⁻⁵，新基准 `2.413e-04`），**容差一字未改**。
  B（容差随 n 绑定）/ C（维持现状）作废。**不要再把第二档写成"绝对真值"**：它与内核同属部分和顺序
  折叠家族，宽输入下谁更准会反转（`docs/evidence/phase46/README.md`、`docs/evidence/phase47/README.md`）。
- R8.5 的"缺页迁移率"在本机是**可测代理**（显式驻留探针率），gfx1032 的 Windows 应用运行时
  路径没有可读的缺页计数器；文档与代码注释都点名这一层，别把它写成硬件计数。
