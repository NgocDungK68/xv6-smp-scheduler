# MODULE 3 – Work Stealing & Load Balancing (Updated)

> Tài liệu cập nhật cho milestone: **Benchmark + WS/LB test dùng API V2 thống nhất, có dữ liệu đánh giá rõ, sơ đồ Figma và tài liệu kỹ thuật hoàn thiện.**

## 1. To-do / Milestone checklist

| No. | Task | Status |
|-----|------|--------|
| 1 | Work Stealing design + implementation | Completed |
| 2 | Load Balancing design + implementation | Completed |
| 3 | Scheduler integration | Completed |
| 4 | **Schedstats API V2** (`sched_getstats` / `sched_resetstats`) | Spec + code ready (integrate) |
| 5 | **schedmeasure** + WS/LB structured tests | Code ready |
| 6 | Benchmark pipeline + evaluation data | Template ready |
| 7 | Figma flows (WS/LB + Benchmark) | Spec below / mermaid |
| 8 | Update technical docs | This file + `schedstats-api.md` |

## 2. Architecture summary

```
User programs (testws, ws_lb_test, benchmark, schedmeasure)
        │
        │  syscalls: sched_getstats / sched_resetstats / getcpu / setaffinity
        ▼
┌─────────────────────────────────────────┐
│  Schedstats aggregator (schedstats.c)   │
│  sums worksteal_stats[] + lb_stats      │
└─────────────────────────────────────────┘
        ▲                    ▲
        │                    │
  worksteal.c          loadbalancing.c
        │                    │
        └────────┬───────────┘
                 ▼
            runqueue.c  (rq_steal_runnable / rq_transfer)
                 ▲
            scheduler.c (idle → WS; CPU0 periodic → LB)
```

## 3. Work Stealing (recap)

**Trigger:** Local run queue empty in `scheduler()`.

**Steps:**

1. `worksteal_try(thief_cpu, online_mask)`
2. Rank victims by eligible runnable count (affinity-aware), skip load ≤ 1
3. Attempt `rq_steal_runnable` under `p->lock`
4. At most one successful steal per call
5. Update per-CPU `worksteal_stats`

**Files:** `kernel/worksteal.c`, `kernel/worksteal.h`

## 4. Load Balancing (recap)

**Trigger:** CPU 0 coordinator, every ≥ `LB_INTERVAL_TICKS` (10).

**Config:**

| Param | Value | Meaning |
|-------|------:|---------|
| `LB_INTERVAL_TICKS` | 10 | Min ticks between checks |
| `LB_IMBALANCE_THRESHOLD` | 2 | Min load diff to migrate |
| `LB_MAX_MOVES_PER_RUN` | 1 | Max moves per run |

**Steps:**

1. Snapshot `sched_cpu_load` for online CPUs
2. Find src/dst pair with largest diff ≥ threshold and eligible process
3. `rq_transfer` under process lock
4. Update `loadbalance_stats`

**Files:** `kernel/loadbalancing.c`, `kernel/loadbalancing.h`

## 5. Measurement – API V2

Xem chi tiết: **`docs/schedstats-api.md`**

- `sched_getstats(struct sched_stats_v2 *)`
- `sched_resetstats(void)`

**Nguyên tắc đánh giá:**

- Migration proof = `ws_successes` / `lb_migrations` (không chỉ `getcpu` sample)
- Luôn reset trước workload
- Báo cáo delta + elapsed ticks + cấu hình (CPUS, workers, iters, affinity)

## 6. Test matrix

| Program | Mục đích | API V2 |
|---------|----------|--------|
| `schedmeasure` | Dump / burn + delta stats | Yes |
| `ws_lb_test` | free / pin / imbalance scenarios | Yes |
| `testws` | CPU-bound multi-worker + getcpu sample | Nên bổ sung getstats |
| `benchmark` | Throughput KOps/sec baseline | Nên bổ sung getstats |
| `scheduler_test` | Functional SMP / affinity | Unchanged |
| `usertests` | Regression | Must still pass |

### Recommended shell sequence

```text
make clean && make CPUS=4 qemu
$ schedmeasure
$ ws_lb_test all
$ testws 4 1000000
$ testws 8 2000000
$ benchmark
$ scheduler_test 4
$ usertests -q
```

## 7. Benchmark evaluation template

### 7.1 Config

- QEMU `CPUS=2,4,8`
- Workers: 1, 2, 4, 8
- Iterations fixed per comparison
- Runs: ≥ 5, report mean ± range

### 7.2 Metrics

| Metric | Source |
|--------|--------|
| Elapsed ticks | `uptime()` |
| Throughput (KOps/sec) | benchmark formula |
| WS successes / searches | API V2 delta |
| LB migrations / checks | API V2 delta |
| CPU mask changes | testws samples (secondary) |

### 7.3 Result table (fill after runs)

| CPUS | Workers | Elapsed (mean) | KOps/s | WS succ | LB mig | Notes |
|------|---------|----------------|--------|---------|--------|-------|
| 4 | 4 | | | | | free |
| 4 | 4 | | | | | pin CPU0 |
| 4 | 8 | | | | | imbalance |
| ... | | | | | | |

### 7.4 Interpretation guidelines

- **Correctness first:** all workers complete, no panic, usertests OK
- **WS path live:** idle CPU + loaded victim → `ws_successes > 0`
- **LB path live:** sustained imbalance ≥ threshold → `lb_migrations > 0`
- **Affinity:** pinned process never runs outside mask (scheduler + steal checks)
- Do **not** claim “faster than baseline” without paired runs and variance

## 8. Known limits

- QEMU ticks coarseness → short runs may show 1–2 ticks only
- LB rate limit + threshold → may need longer / heavier imbalance to fire
- `rq_len[]` in getstats is best-effort snapshot
- No real hardware cache/NUMA effects

## 9. Figma / diagrams

See `docs/FIGMA_FLOWS.md` (text + mermaid for direct paste into Figma or docs).

## 10. File inventory (deliverables)

| Path | Role |
|------|------|
| `docs/schedstats-api.md` | ABI V2 spec |
| `docs/M3-module3-update.md` | This milestone doc |
| `docs/FIGMA_FLOWS.md` | Diagram specs |
| `docs/BENCHMARK_TEMPLATE.md` | Result sheet |
| `kernel/schedstats.h` / `.c` | Aggregator |
| `patches/SYSCALL_INTEGRATION.md` | How to wire syscalls |
| `user/schedstats.h` | User ABI |
| `user/schedmeasure.c` | Measurement tool |
| `user/ws_lb_test.c` | WS/LB scenarios |
