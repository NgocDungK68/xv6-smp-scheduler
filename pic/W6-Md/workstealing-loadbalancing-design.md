# Work Stealing and Load Balancing – Module 3

## 1. Idle Work Stealing

Idle Work Stealing is intended to give useful work to a CPU whose local Run Queue is empty.

### Control flow
1. A CPU reaches the scheduler with no locally runnable process.
2. The Work Stealing routine checks whether the feature is initialized and whether the destination CPU is online.
3. It searches eligible source CPUs (victims), excluding itself.
4. It estimates the number of runnable processes that can legally execute on the destination CPU.
5. It selects a suitable victim, respecting CPU affinity.
6. It attempts a safe transfer of a runnable process to the destination Run Queue.
7. The destination scheduler can then dispatch the transferred process.

### Correctness conditions
- Never steal a process that is not runnable.
- Respect the process affinity mask at the destination CPU.
- Use the Run Queue migration API and its documented lock order.
- Do not run the same process concurrently on two CPUs.
- Treat a stale candidate or failed transfer as a failed attempt, not as a successful migration.

## 2. Periodic Load Balancing

Periodic Load Balancing checks whether runnable work is unevenly distributed across CPUs.

### Control flow
1. A balancing check is triggered according to the configured policy.
2. The algorithm reads the current load of eligible CPUs.
3. It determines whether the imbalance is large enough to justify migration.
4. It chooses a source CPU and a destination CPU.
5. It selects a runnable process that is allowed to run on the destination.
6. It performs a safe Run Queue transfer, subject to the migration limit for the balancing cycle.

### Correctness and performance considerations
- Avoid migrations when the imbalance is below the configured threshold.
- Respect CPU affinity for every candidate.
- Bound the number of migrations in one balancing cycle.
- Avoid repeatedly moving the same workload between CPUs (ping-pong migration).
- Keep diagnostic logging disabled during performance measurement unless explicitly needed.

## 3. Relationship with CPU Affinity

CPU Affinity constrains the CPUs on which a process may execute. Both Work Stealing and Load Balancing must check the destination CPU against the process affinity mask before migration. An otherwise idle or lightly loaded CPU is not a valid destination if the process is not permitted to run there.

## 4. How to validate the mechanisms

Use the existing `testbalance` modes to distinguish the scenarios:

- Idle Work Stealing mode: create runnable work concentrated on a source CPU and observe whether an idle eligible CPU receives work.
- Periodic Load Balancing mode: create an uneven distribution and observe whether the balancing mechanism reports migrations.
- Affinity mode: constrain workers to a CPU mask and verify that sampled execution CPUs remain within that mask.

Use Scheduler Statistics API V2 snapshots before and after each mode. A process observed on another CPU is evidence of CPU execution spread, but migration counters are needed to attribute the mechanism responsible.

## 5. Previously observed test evidence

The following results were previously reported for QEMU with four CPUs:

- Workload-spread mode: 8/8 workers completed; 8/8 were observed on a CPU different from their first observed CPU; zero affinity violations and zero anchor mismatches.
- Periodic balancing mode: 8/8 workers completed; three successful Load Balancing migration log events were observed; zero affinity violations and zero anchor placement mismatches.
- Affinity mode: 8/8 workers completed; all workers reported CPU mask `0x1`; zero affinity violations.

These observations should be rerun after API V2 integration before being presented as final-week regression results.
