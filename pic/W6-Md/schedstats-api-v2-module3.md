# Module 3 – Scheduler Statistics API V2 Integration

## Purpose
This document describes how Module 3 should use the shared Scheduler Statistics API V2 when evaluating Work Stealing, Load Balancing, and Scheduling Contention.

## Shared interface
Module 3 uses the system calls already standardized by the project:

- `getcpu()` (system call 23): identify the CPU on which the calling process is executing.
- `sched_setaffinity(pid, mask)` (system call 24): set the CPU affinity mask of a process.
- `sched_getstats()` (system call 25): obtain a snapshot of scheduler-related counters.

The exact statistics structure and field names must follow `docs/schedstats-api.md` in the repository. Do not create a second statistics structure or duplicate the system-call interface in Module 3.

## Measurement procedure
For each workload:

1. Take a statistics snapshot immediately before the workload.
2. Run the workload with fixed arguments and a documented CPU configuration.
3. Take a second snapshot after all child processes have completed.
4. Calculate counter deltas as `after - before`.
5. Record the elapsed time and workload parameters alongside the counter deltas.
6. Repeat the same workload under the same configuration when collecting comparison data.

Counters are cumulative, so the benchmark must compare two snapshots rather than interpret the final absolute counter as the workload's contribution.

## Module 3 metrics to review
Use only metrics actually exposed by API V2. At minimum, inspect the API documentation for:

- Work Stealing search / attempt / success counters.
- Load Balancing activity and migration counters.
- Per-CPU dispatch counters.
- Any Run Queue counters that help explain queue movement.

Do not infer a successful migration solely from a process being observed on a different CPU. Correlate process CPU observations with the relevant migration counters.

## Workload integration points
- `testbalance`: use snapshots around each test mode (Idle Work Stealing, Periodic Load Balancing, and affinity-constrained execution).
- `schedcont`: use snapshots around Fork and Ping-Pong modes.
- `benchmark`: use snapshots around each fixed benchmark workload and CPU-count configuration.

## Output format
Each run should preserve:

- workload name and mode;
- worker count and iteration / round count;
- online CPU count;
- elapsed time;
- before and after snapshots or their counter deltas;
- PASS / FAIL information from the workload.

A CSV or tab-separated output is suitable for later plotting. Keep the raw run output so the summary can be reproduced.

## Validation checklist
- [ ] The programs call the existing API V2 wrappers from `user/user.h`.
- [ ] No duplicate system-call numbers or duplicate syscall wrappers are introduced.
- [ ] Counter deltas are non-negative for cumulative counters.
- [ ] Work Stealing and Load Balancing counters are interpreted separately.
- [ ] The same workload arguments are used for Stock and modified xv6 comparisons.
- [ ] Benchmark results include the CPU configuration and repeat number.
