# Phase 18 证据 — R8.5 后半（告警率 → 违规记录 → 重新求解）

本机：Windows 11 + MinGW g++ 13.2.0（C++20）+ AMD **RX 6600M（gfx1032）**，ROCm 7.1（hipcc）。
日期 2026-10-28。基线 HEAD `7720608`（Phase 17 Harness 复核之后）。

## 复现命令

| 文件 | 命令 | 证什么 |
|------|------|--------|
| `phase18-streamed-round1.txt` | `./build/unikern.exe run --hcs samples/hcs_amd.json --dims 32768 32768 32768 --workdir uk_run/phase18 --ledger uk_run/phase18/long1.sqlite3` | 场景 B 流式在 Phase 16 守卫落地后**首次**真机重跑：`mode=streamed shards=64 shard_rows=512 prefetch_d=2`、`verify=PASS max_rel=0`、`migrations=0`、rc=0、LEDGER COMMIT ε_rel=5.05902。pinned 分片走 `hipHostMalloc` 不经守卫，合法流式未被误判成超订 |
| `phase18-resident-tokens.txt` | `./build/unikern.exe run --hcs samples/hcs_amd.json --op reduce --dims 1048576 0 0 --workdir uk_run/phase18r --ledger uk_run/phase18r/red2.sqlite3` | 干净路径的新 token 实测：`migrations=0 checks=6`（verify 3 + timed 3）、`vram_free=8407547904 vram_total=8573157376`（`hipMemGetInfo` 实测，声明 8589934592）、COMMIT ε_rel=0.419355 |
| `alarm-firing-control.txt` | `python probe/phase18_alarm_probe.py` | **开火控制**：把 `uk_mmalloc` 的 `hipMalloc` 注入成 `hipHostMalloc` 后，整条链实测——守卫开火（`oversubscription ... migrations=1 checks=1 vram_free=8425177088`）→ `UNIKERN_VIOLATION rate=1 threshold=0 committed=no` → `RESOLVE re-solve device_capacity=8589934592->8425177088 (measured by hipMemGetInfo)` → 重试再违例 → `RESOLVE STOP`，rc=5、solution 零落库、账本只有两条 `#VIOLATION` 事件行；探针自带 try/finally 复原 + 残标断言，复原后 ctest 2/2 绿 |
| `removal-controls.txt` | `python probe/phase18_leg_removal_probe.py docs/evidence/phase18/removal-controls.txt` | **摘除即红四条腿全承重**：守卫拒绝行的 `checks=` 发射、以及 conv/reduce/gemm 各自 result 行的 `checks=` 发射，逐一删除都让 acceptance 变红（exit 3221226505 = `0xC0000409` assert abort），复原 byte-identical 后 ctest 2/2 绿。这批控制还抓到首版臂其实**不承重**（见下"臂的教训"） |
| `longtest-series.txt` | 驻留：`./build/unikern.exe run --hcs samples/hcs_amd.json --op reduce --dims 1048576 0 0 --workdir uk_run/phase18s --ledger uk_run/phase18s/series.sqlite3`（×15 轮）；流式：`./build/unikern.exe run --hcs samples/hcs_amd.json --dims 32768 32768 32768 --workdir uk_run/phase18s2 --ledger uk_run/phase18s2/stream.sqlite3`（×2 轮） | R8.5 长测本体：逐轮的 `migrations/checks`、`vram_free/vram_total` 与实测耗时序列 |

## 口径与边界（必读）

- 率 = `migrations / checks`（D7）。`checks` 是生成程序内 `uk_resident` 的探针次数，
  `migrations` 是其中非设备驻留的次数。阈值 `--migration-threshold`（比率），默认 **0.0**
  等于 Phase 16 的"任一非驻留即退 5"语义；`nan`/负数在 parse 即拒（rc=2）且不建账本。
- **这不是缺页计数器**：gfx1032 的 Windows 应用运行时路径没有可读的缺页/迁移硬件计数器
  （HSA perf counters 不在此路径），所以该率是 R8.5"运行时缺页迁移率"的**可测代理**
  （显式驻留探针率）。程序自身显式发起的 pinned 分片搬运不计入迁移。
- 重求解只用实测量：拿不到 `vram_free` 即 `RESOLVE STOP` 退 5，绝不用臆造常数重解（E13）。
- 重试键取 `ledger_key(tightened, tree)`，即按重解实际使用的 HCS 落账，不把另一容量上
  测得的行挂在旧指纹下（R7.1）。
- **读数演进**：32768³ 流式 Phase 8 记 `measured_ms=55354.1 / kernel_ms=71354.8`
  （kernel>e2e，即当时每分片事件对不可靠），本轮序列两轮流式分别 `21789.8/20627.0` 与 `23162.3/31288.3`——一轮 kernel<e2e、
  一轮 kernel>e2e，所以**不能用单轮判定"拷贝与计算真重叠"**：`kernel_ms` 在本机流式路径不是
  稳定量。Phase 8 转录保持原样不改写，本轮读数只作新时点样本。
- 生成程序行数锚点随之演进：gemm `425→447`、conv `253→275`、reduce `217→239`（各 +22 行）。
  v12 里记的 253/217/425 是**历史时点**读数。

## 臂的教训（同一目录内可复核）

首版 `[R8.5-long]` 的 token 断言写成 `src->find("\" checks=\"") != npos`。用摘除即红检验时，
删掉 conv 程序 result 行的 `checks=` 发射后 **acceptance 仍然绿**——因为守卫的拒绝行自己带同一
个 token，存在性判据被它满足。这不是实现的问题，是**断言没有点名它要区分的那一层**。改成出现
次数判据后（每份程序 `" checks="` / `" vram_free="` / `" vram_total="` 各恰好 **2 次**：拒绝行 +
result 行；`++UK_RESIDENT_CHECKS` 恰好 1 次），四条腿逐一摘除全部变红。

另一次同形坑：序列第一次跑出来的转录是**零读数**——`--ledger` 指向的目录不存在，
`ERROR: sqlite ledger open failed` 走 stderr，而我把输出用 grep 白名单过滤后写盘，失败行被吞。
现在转录头部保留了这条注记，且每轮显式打印 `rc=`。

## 仍如实开放

- 长测序列**已跑**：15 轮驻留 + 2 轮流式，全 rc=0，每轮 `migrations=0 checks=6`，率恒 0。
  诚实结论：本机正常路径不会产生非驻留设备分配，所以率的分布只有 0；超阈行为由注入开火控制证明。
- 流式 `kernel_ms` 在本机不是稳定量：同一二进制相邻两轮给出 `20627<21790` 与 `31288>23162`。
- 场景 B 在 Apple/Intel 侧无对应实现（MVP 里它们是 resident-only），该率只在 HIP 路径上有意义。
