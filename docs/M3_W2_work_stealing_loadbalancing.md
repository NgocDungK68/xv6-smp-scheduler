# Module 3 – Work Stealing & Performance Benchmark - W2

## 1. Idle Work Stealing

When a CPU has no runnable processes in its local Run Queue, it searches other CPUs for available work.

Algorithm:
1. Detect that the local Run Queue is empty.
2. Select another CPU as the source (victim).
3. Find a runnable process that can execute on the destination CPU.
4. Check CPU Affinity before stealing.
5. Transfer one eligible process to the local Run Queue.
6. Return to the Scheduler to execute the stolen process.

## 2. CPU Affinity

Each process has an `affinity_mask` specifying which CPUs are allowed to execute it.

A process can be stolen only if:

```c
(p->affinity_mask & (1U << dst_cpu)) != 0
```

Processes that are not compatible with the destination CPU must be skipped.

## 3. Periodic Load Balancing

Periodic Load Balancing proactively distributes processes across CPUs without waiting for a CPU to become completely idle.

Proposed algorithm:
1. Check Run Queue lengths periodically.
2. Identify the most loaded and least loaded CPUs.
3. Calculate the load difference.
4. Migrate an eligible process if the difference exceeds a configured threshold.
5. Avoid unnecessary migrations using periodic intervals and load thresholds.

Initial parameters for experimentation:
- `BALANCE_INTERVAL = 10 ticks`
- `IMBALANCE_THRESHOLD = 2`

These parameters are configurable and require benchmarking.

## 4. Performance Benchmark

### Input
- Number of CPUs.
- Number of worker processes.
- Iterations per process.
- Initial workload distribution.
- CPU Affinity configuration (after integration).

### Output
- Total execution time (ticks).
- Successful and failed Work Stealing attempts.
- Number of process migrations.
- Process distribution across CPUs.
- Run Queue load distribution.

### Comparison
Benchmark the following configurations using identical workloads:
1. Stock xv6 Scheduler.
2. Per-CPU Run Queues.
3. Per-CPU Run Queues + Idle Work Stealing.
4. Per-CPU Run Queues + Idle Work Stealing + Periodic Load Balancing.

## 5. Current Implementation Status

- [x] Initial Work Stealing prototype.
- [x] Mock Queue testing.
- [x] Initial xv6 benchmark workload.
- [ ] Periodic Load Balancing implementation.
