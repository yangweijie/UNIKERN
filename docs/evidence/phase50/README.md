# Phase 50 — ctest 第 7 条门 `hip_programs`：生成的程序第一次被编译器检查

主机：Windows 11 + AMD RX 6600M（gfx1032），`hipcc` 取 `HIP_PATH`。全套 `ctest` 由 6 条门变 7 条，
总时长 12 s（本门 7.8 s）。

## 补的是 Phase 49 自己记下的洞

Phase 49 的 README 里写着"P3 造出的产物其实编不过，而 ctest 里没有腿能看见"。原因很结构性：
acceptance 的臂把生成源码当**文本**读，门 #6 只编译 runner 的 TU，从来没有人让 hipcc 解析过
一份**发射出来的程序**。本相关掉这个洞：`probe/generated_program_check.py`（Phase 50 落地时名为
hip_program_check.py，Phase 52 改名并把 OpenCL 产物一起纳入） 用 CLI 现场发射
gemm / conv / reduce，然后 `hipcc -fsyntax-only -O3 --offload-arch=<设备自报>` 逐份解析。

`--offload-arch` 不写死：门自己编一个 host-only 的架构探针（与 `src/runner.cpp` 里
`kArchProbeSrc` 同形，零 `__global__`），读 `gcnArchName`。理由与 E12 同源——目标三元组错了，
检查的就不是真机上的那份代码。

## 三条控制，期望各不相同（这是关键）

`controls.txt`（`probe/phase50_hipprogram_control_probe.py`）：

| 控制 | 破坏方式 | 归属后的 acceptance | `hip_programs` | 含义 |
| --- | --- | --- | --- | --- |
| K1 | gemm 程序里 `e0` 改成 `ze0`（没有任何臂提到 `e0`） | **GREEN** | **RED** | 门看见了全部文本臂看不见的东西 |
| K2 | Phase 49 的 P3（声明移到使用之后） | RED（序列断言） | RED（`use of undeclared identifier 'pin'`） | 与文本臂重叠，不是唯一防线 |
| K3 | 只改一行注释 | GREEN | **GREEN** | 门不是"任何字节变化都红"，它检查的是语法 |

K3 是必要的：没有它，"反应灵敏"和"只反应坏代码"无法区分。
编译器原话（K1）：`build/uk_hip_programs/gemm/uk_amd.cpp:225:35: error: use of undeclared
identifier 'e0'`。

## 两种跳过模式都实测过（不是"设计上会跳过"）

`gate-output.txt` 里三种输出都有：PASS（本机）、无 hipcc → `SKIPPED reason=no hipcc` 且 **exit 77**、
有 hipcc 但看不见 ROCm 设备 → `SKIPPED reason=device reported no gcnArchName: UK_ARCH_FAIL
no ROCm-capable device is detected` 也退 77。ctest 因此报 **Skipped 而不是 Passed**
（`SKIP_RETURN_CODE 77`）——Mac/Intel 机器上看到的是"这台没查"，不是一片绿。
第二类跳过有文档化的旁路：`--arch <name>`（会显式标注这是 override，不是设备事实）。

## 边界

- **语法检查 ≠ 跑通**。本门不声称 `verify=PASS`；上机执行仍只在 `docs/evidence/**`，
  由 `probe/phase48_artifact_build_run.py`（存盘字节自己编译运行）与 `run --vendor amd` 覆盖。
- 只覆盖 HIP 一份产物；MSL/OpenCL 的生成源码在本机没有可用的编译器，仍是文本腿 + 明说缺口。
  **这一行写于 Phase 50，第二半已被 Phase 52 否证**：ROCm 自带的 clang 21 有 OpenCL C 内建头，
  `-fsyntax-only -x cl -Xclang -finclude-default-header` 在 AMD 主机上就能解析 Intel 目标产物，
  所以 OpenCL 半边现在也在门里（`docs/evidence/phase52/`）；仍然缺的只有 MSL。
- 架构探针需要一个可见设备；CI 类无卡主机要走 `--arch`。

## 复现

```bash
cmake -G Ninja -B build && cmake --build build
ctest --test-dir build --output-on-failure                 # 7 条门
python probe/generated_program_check.py                     # 本机：RESULT PASS（Phase 52 起的名字）
python probe/generated_program_check.py --arch gfx1032      # 无卡主机的旁路（会标注 override）
python probe/phase50_hipprogram_control_probe.py docs/evidence/phase50/controls.txt
```
