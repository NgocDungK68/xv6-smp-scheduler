# Scheduler Statistics API (V2)

**Maintainer:** M1 (Core Scheduler)  
**Source of truth:** `kernel/schedstats.h` on `feature/m1-wfi`  
**Consumers:** M2 tests; M3 Work Stealing/Load Balancing tests and benchmarks.

## 1. Public contract

User-space programs use **only** the following scheduling syscalls:

| Syscall | Number | Purpose |
| --- | ---: | --- |
| `getcpu()` | 23 | CPU running the calling process at the instant of the syscall |
| `sched_setaffinity(int pid, unsigned int mask)` | 24 | Set allowed CPUs; `pid=0` refers to the calling process |
| `sched_getstats(struct sched_stats *out, int size)` | 25 | Copy scheduler statistics to user memory |

Do not add `setaffinity()` as a second syscall or reuse syscall number 25. `getcpu()` is an observation, **not** proof that a migration came from Work Stealing or Load Balancing.

To read statistics:

```c
#include "kernel/types.h"
#include "kernel/schedstats.h"
#include "user/user.h"

struct sched_stats s;
if (sched_getstats(&s, sizeof(s)) < 0 ||
    s.version != SCHED_STATS_VERSION || s.size != sizeof(s)) {
  printf("scheduler stats unavailable\n");
  exit(1);
}
```

**Return values:** 0 on success, -1 on invalid buffer/size or failed `copyout`. **V2 requires the exact size** (`sizeof(struct sched_stats)`). All user programs in this repository are built against the same header. V1 binaries must be rebuilt when using V2. When changing the struct in a future revision, explicitly design compatibility and increment `SCHED_STATS_VERSION`; do not silently change the meaning of existing fields.

## 2. Meaning and units (do not change within V2)

| Field | Meaning | Unit/type |
| --- | --- | --- |
| `version`, `size` | API version and returned struct size | integer / bytes |
| `features` | Supported mechanisms: `SCHED_FEAT_IPI`, `SCHED_FEAT_WS`, `SCHED_FEAT_LB` | bitmask |
| `online_mask`, `idle_mask` | CPUs online / in the IPI idle-wait protocol when sampled | bitmask |
| `queued[cpu]` | Processes waiting in this CPU's Run Queue, excluding the running process | instantaneous count |
| `running[cpu]` | Whether this CPU is currently executing a process | 0 or 1, snapshot |
| `dispatches[cpu]` | Scheduler successfully assigned a process to CPU, immediately before the switch from scheduler to process | cumulative count |
| `ws_searches` | Idle-WS victim-search invocations | cumulative count |
| `ws_attempts` | Actual WS migration attempts | cumulative count |
| `ws_successes` | Successful process transfers performed by WS | cumulative count |
| `lb_checks` | Actual balancing evaluations after interval gating | cumulative count |
| `lb_migrations` | Successful process transfers by periodic LB | cumulative count |
| `ipi_sent[cpu]` | Requests sent to the target CPU's SSWI | cumulative count |
| `ipi_received[cpu]` | Supervisor software interrupt handler invocations on that CPU | cumulative count |
| `wfi_waits[cpu]` | Number of executions of the WFI instruction | cumulative count |

**Counter scope:** system-wide, since boot; never reset as part of a benchmark. Take snapshots before/after and subtract. Kernel WS/LB internal counters are currently 32-bit, converted to 64-bit in this public struct; very long measurements could wrap the underlying 32-bit counters.

**Snapshot consistency:** `sched_getstats()` is best-effort, *not an atomic system-wide snapshot*. CPU state/queue sizes may change while collecting each CPU; do not assert `sum(queued)+sum(running)` is constant across a snapshot.

**Interrupt semantics:** multiple IPI requests can coalesce; `ipi_sent` need not equal `ipi_received`. `wfi_waits` does not measure CPU energy savings or guaranteed hardware sleep. When `IPI=0`, `SCHED_FEAT_IPI` is not present.

**Dispatch vs context switch:** `dispatches` counts only assignments of a process by Scheduler. It does not claim to count all context switches or spinlock contention. Measuring actual lock contention needs separate instrumentation (for example MIT xv6 `kalloctest`-style failed lock acquisitions), which is not part of V2.

**Algorithm independence:** V2 defines events, **not** victim-selection, load-balancing thresholds, Run Queue internals, or locking methods. New WS/LB algorithms must preserve the event semantics above if they reuse V2.

## 3. Recommended usage for M2 (Tuấn)

`lostwake_test.c` and `lwstress.c` need **no source change**. They use standard xv6 `fork()`, `pipe()`, `read()`, `write()`, `wait()`, `uptime()`.

- Run the functional and 10-minute stress tests normally. Their PASS criteria are correct data, child exit statuses, progress, and no hang/panic.
- On the integrated kernel, optionally run `schedmeasure lostwake_test` to print system-level Scheduler counters during one test. `schedmeasure lwstress` is also possible but has extra instrumentation output, so use the normal run for a strict stress verdict.
- Use `getcpu()` and `sched_setaffinity()` when extending Affinity tests. Do not access `struct proc`, Run Queue internals, or CPU idle flags directly from user code.

## 4. Recommended usage for M3 (Duy)

### 4.1. Existing workloads

`testbalance.c` already uses `sched_setaffinity()` and `getcpu()`. No functional API change is needed.

`testws.c` currently calls `setaffinity(getpid(), expected_mask)`. **Replace this with `sched_setaffinity(getpid(), expected_mask)`**. Remove the additional `setaffinity` syscall declaration/number/wrapper from the M3 branch when integrating. Keep syscall numbers 23/24/25 as specified above.

`benchmark.c` has six workloads: `cpu`, `alloc`, `fork`, `file`, `pingpong`, `burst`. The speedup CSV remains independent of the new metrics syscall; **Stock xv6 does not support `sched_getstats()`**.

### 4.2. Confirm WS versus LB

On the new kernel, without changing `testbalance.c`:

```text
schedmeasure testbalance 8 30000000 0 4
schedmeasure testbalance 8 30000000 1 4
schedmeasure testbalance 8 30000000 2 4
```

`mode=0` is the idle-WS scenario, `mode=1` the periodic-LB scenario, and `mode=2` the Affinity scenario. Compare **deltas** of `ws_successes` and `lb_migrations`. A worker changing CPU by itself does not identify which mechanism moved it.

`schedmeasure` collects global counters during an entire command, including process startup, shell/kernel activity and any warmup performed by the command. Treat it as diagnostic evidence, not an exact per-worker attribution or precision timing instrument.

### 4.3. Scheduling Contention (`fork` and `pingpong`)

First benchmark using the existing `uptime()` + CSV; record elapsed ticks and throughput under the same workload and CPU configuration. On the new kernel only, optionally diagnose via:

```text
schedmeasure benchmark mine fork 4 5
schedmeasure benchmark mine pingpong 4 5
```

The wrapper prints **one cumulative counter delta** covering warmup plus all five measured runs. To record *per-run* metrics later, take two `sched_getstats()` snapshots around one `run_once()` call and write separate diagnostic rows; do not change the existing `CSV,kernel,workload,cpus,run,time` format consumed by `plot_speedup.py`.

Use `dispatches` to describe how often processes were scheduled, **not** how long the Scheduler spent choosing a process or how often its locks contended. Actual lock contention requires separate measurements.

### 4.4. Stable output formats

Time/scalability on Stock and New:

```text
CSV,stock,pingpong,4,1,123
CSV,mine,pingpong,4,1,98
```

The integers are illustrative only. The existing `results.csv` schema remains:

```text
kernel,workload,cpus,run,time
```

Optional diagnostics on New:

```text
SCHEDSTAT dispatches=...
SCHEDSTAT ws_searches=... ws_attempts=... ws_successes=...
SCHEDSTAT lb_checks=... lb_migrations=...
SCHEDSTAT ipi_sent=... ipi_received=... wfi_waits=...
```

Keep diagnostics separate from the speedup comparison; always verify the actual `CPUS=N`, workload total, QEMU/toolchain, and number of repetitions.

## 5. Owner and maintenance rules

- **M1** owns `kernel/schedstats.h`, `schedstats.c`, Scheduler counter hooks and syscall registration.
- **M2/M3** own their user test/benchmark applications and scripts. They can use only public syscalls; internal module getter functions (`worksteal_get_stats`, `loadbalance_get_stats`, `rq_length`, etc.) are kernel-only.
- **No new syscall numbers** for individual scheduling algorithms. Add new metrics only with an agreed API revision.
- **No counter resets** before/after benchmarking. Snapshots + differences are required so measurement does not change scheduling policy state.
- **No printing in the Scheduler hot path.** Prefer relaxed per-CPU counters and disabling verbose LB logging for performance runs.

## 6. Test and build checklist

On `feature/m1-wfi`, after adding the V2 files:

```bash
make fmt
make CPUS=4 IPI=1
make qemu CPUS=4 IPI=1
```

Inside xv6:

```text
schedstatstest
ipiwakeup 4
schedmeasure ipiwakeup 4
```

When the branches are integrated, also run:

```text
lostwake_test
getcpu_test
affinity_test
affinity_rt
testbalance 8 30000000 0 4
testbalance 8 30000000 1 4
```

Only claim PASS for tests actually run. The M2 long-running `lwstress` should be run after integration and recorded separately.

## 7. References

- Linux kernel: https://docs.kernel.org/scheduler/sched-stats.html — per-CPU Scheduler statistics and versioned reports.
- MIT xv6 Locks Lab: https://pdos.csail.mit.edu/6.S081/2026/labs/lock.html — benchmark workloads with explicit lock contention instrumentation.
- Linux perf stat: https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/perf-stat.txt — elapsed time and counters are distinct measurements.
