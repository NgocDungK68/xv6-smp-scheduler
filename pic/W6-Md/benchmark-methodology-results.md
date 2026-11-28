# Module 3 – Benchmark Methodology and Results

## 1. Workloads

### High Scheduling Contention
The `schedcont` program provides two modes:

- `fork`: creates concurrent workers using the Fork workload.
- `pingpong`: exercises repeated process coordination / Ping-Pong activity.

The program accepts a worker count and a workload-round parameter. Keep these arguments identical when comparing Stock xv6 with the modified SMP Scheduler.

### Work Stealing / Load Balancing
Use `testbalance` to exercise:
- Idle Work Stealing;
- Periodic Load Balancing;
- CPU Affinity constraints.

### Scalability
Use the existing benchmark workload across the supported CPU counts. Record the actual CPU count used for each run; do not assume the host or emulator supports every requested configuration.

## 2. Measurement protocol

For every workload and CPU configuration:

1. Start from the same build variant and clean run state.
2. Record the exact command, CPU count, worker count, and iteration count.
3. Capture elapsed time.
4. Capture Scheduler Statistics API V2 before and after the workload.
5. Compute counter deltas for cumulative counters.
6. Repeat the run several times and report the median, while retaining individual samples.
7. Use the same workload and parameters for Stock xv6 and modified xv6.

## 3. Metrics

- Execution Time: elapsed ticks reported by the workload.
- Throughput: completed operations divided by elapsed ticks, if the operation count is well-defined.
- Speedup at a fixed CPU count N: `T_stock(N) / T_modified(N)`.
- Scheduler activity: API V2 counter deltas for Work Stealing, Load Balancing, Run Queue activity, and per-CPU Dispatch, using the exact fields documented in `schedstats-api.md`.

Do not compare a Stock result at one CPU count with a modified result at a different CPU count when calculating same-N speedup.

## 4. Previously recorded Scheduling Contention run

QEMU `CPUS=4`:

| Mode | Command | Workers completed | Failed workers | Planned operations | Elapsed ticks | Operations/tick |
|---|---|---:|---:|---:|---:|---:|
| Fork | `schedcont fork 4 100` | 4/4 | 0 | 400 | 16 | 25 |
| Ping-Pong | `schedcont pingpong 4 500` | 4/4 | 0 | 2000 | 14 | 142 |

These are previously observed modified-scheduler results. They are not a Stock-vs-modified comparison because a matching Stock run has not been provided here.

## 5. Required result table

| Scheduler | Workload | CPUs | Workers | Iterations / rounds | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 | Median |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Stock | Fork | | | | | | | | | |
| Modified | Fork | | | | | | | | | |
| Stock | Ping-Pong | | | | | | | | | |
| Modified | Ping-Pong | | | | | | | | | |

## 6. Recommended plots

1. Execution Time vs CPU Count – Stock and Modified, separate series.
2. Speedup vs CPU Count – calculate at matching CPU counts.
3. Work Stealing / Load Balancing counter deltas by workload.
4. Per-CPU Dispatch distribution, when it helps explain load distribution.

## 7. Interpretation checklist

- [ ] Confirm all workers completed before accepting a run.
- [ ] Record failures and affinity violations.
- [ ] Compare the same workload arguments and CPU count.
- [ ] Use median values for the summary and retain raw samples.
- [ ] Do not claim a mechanism caused a migration without supporting counters.
- [ ] Distinguish observed results from expected behavior or planned tests.
