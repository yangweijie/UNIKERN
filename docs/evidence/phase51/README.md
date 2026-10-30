# Phase 51 — G13 拆成两半：OpenCL 向设备要宽度，Metal 保留**标了名字的常量**

主机：Windows 11 + AMD RX 6600M（gfx1032）。**本机没有 Intel 设备、没有 macOS**，所以这一相的
"验证"手段是：门 #6 真的编译 OpenCL 那具身体、门 #7 编译发射产物、AMD 侧上机复核未回归；
**没有任何一条 Intel/Apple 数值读数**，也不会假装有。

## 改了什么

| 后端 | 宽度从哪来 | 现在 |
| --- | --- | --- |
| AMD（发射的 HIP 程序） | `hipGetDeviceProperties`（resident threads/CU × CUs）→ 2 的幂 → 256 地板 | 不变（Phase 44） |
| Intel（`plan_reduce_width` `src/runner_ocl.cpp:190`） | 原先是写死的 `min(L,4096)` | `CL_DEVICE_MAX_COMPUTE_UNITS × CL_DEVICE_MAX_WORK_GROUP_SIZE` → 同一套 2 的幂 + 256 地板；查询失败退回 4096 且 `source=props_unavailable` |
| Apple（`plan_reduce_width` `src/runner_metal.mm:136`） | 原先同上写死 | **保持常量但改名标注** `source=metal_no_capacity_api`：Metal 公共 API 既不报 CU 数也不报每 CU 线程数，没有可诚实查询的量，于是把它写成"这是常量"而不是伪装成测量 |

三处共用同一个钉值旋钮 **`UK_REDUCE_WIDTH`**（Phase 49 给 HIP 程序加的那个），语义也一致：
生效时打印 `UNIKERN_INFO reduce_width source=env width=<w>`，值不在 `[256, n]` 时打印
`what=reduce_width_pin_unfit` 并**拒绝执行**（不是静默忽略）。这才是 G13 里真正能落地的部分——
**跨后端的宽度 A/B 从此有共同词汇**，而没有设备的相位也能证明"钉值被读到"。

顺带修掉一处 rule 6：两个 runner 的 verify 段原先各自调用 `reduce_width(VL)` 重新算一遍宽度，
现在复用计时那一趟的 `plan.P`（只因 verify 实例更短而封顶，且保住 256 地板）。

**没有越界**：`CL_DEVICE_MAX_WORK_GROUP_SIZE × CUs` 与 HIP 的 `maxThreadsPerMultiProcessor × CUs`
**是同类事实（设备自报容量）但不是同一个量**——代码注释与这里都写明了。等 Intel 机器上第一次
真跑时，要对着实际读数重判这个代理够不够。

## 腿与控制

`[R15d-runner-width]`（`tests/acceptance.cpp:2202`）核：两个 runner 都读同名旋钮、都有拒绝 token、
都打印 `source=`、旧写死函数已删除、verify 复用 `plan.P`、`getenv` 各只出现一次；
OpenCL 侧另有 `CL_DEVICE_MAX_COMPUTE_UNITS` 与 256 地板算术的文本。

三条控制**期望各不相同**（`controls.txt`）——这一步顺带把三台仪器的分工钉出来了：

| 控制 | 破坏 | acceptance | 门 #6 `runner_tu` | 门 #7 `hip_programs` |
| --- | --- | --- | --- | --- |
| R1 | 丢掉 OpenCL 的 256 地板算术 | 红 | 绿 | 绿 |
| R2 | 不合适的钉值只打印不拒绝 | 红（`what=reduce_width_pin_unfit`） | 绿 | 绿 |
| R3 | 在设备查询里打错一个变量名 | **绿** | **红**（`wgg_undeclared was not declared`） | 绿 |

R3 就是门 #6 的存在理由：本机 `runner_ocl.cpp` 的身体不参与构建，文本腿读的字符串又都还在，
只有真编译它的门能看见。**这一相还有个副产品**：我第一次写坏的 OpenCL 代码（C++ 字符串里
被 Python 塞进真换行）就是门 #6 在 0.1 s 内报出来的，前两次同类事故都是靠人肉读编译错误（E52）。

## AMD 侧未回归

`amd-sanity.txt`：reduce 与 conv 各一次真机 `run --vendor amd`，都
`verified=PASS max_rel=0 migrations=0 checks=6`，reduce 仍自报 `source=device width=16384 cap=28672`。

## 复现

```bash
ctest --test-dir build --output-on-failure      # 7/7
python probe/runner_tu_check.py                 # 门 #6：本机唯一能编译 OpenCL 身体的检查
python probe/phase51_g13_removal_probe.py docs/evidence/phase51/controls.txt
UK_REDUCE_WIDTH=100 ./build/unikern.exe run --hcs samples/hcs_amd.json --op reduce \
  --dims 1048576 0 0 --vendor amd --ledger build/p51/pin.sqlite3   # 生成侧同款拒绝语义
```

## 仍未闭合

- Intel/Apple 上没有任何 reduce 数值读数：`width=` 的取值、耗时、`max_rel` 都要等真设备。
- Metal 的容量查询缺失是 API 限制，不是工作项；若某天 Apple 公共 API 给出 CU 数，再换成查询。
- 门 #7 只解析 HIP 产物；MSL/OpenCL **产物**的语法检查要各自主机的工具链（本机没有）。
  **本行的 OpenCL 半句在下一相就被实测否证（Phase 52，2026-10-29）**：ROCm 自带的 clang 有 OpenCL C
  内建头，`clang -fsyntax-only -x cl -Xclang -finclude-default-header` 在这台 AMD 主机上就能解析
  Intel 目标产物，门 #7 因此改名 `generated_program_check.py` 并把 `.cl` 纳入；仍然成立的只有 MSL 半句。
