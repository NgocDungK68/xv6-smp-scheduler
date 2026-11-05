# MODULE 3 – WORK STEALING & LOAD BALANCING

## 1. To-do List

| No. | Task | Description | Status |
|---|---|---|---|
| 1 | Work Stealing Design | Define the conditions that trigger Work Stealing, victim CPU selection, and the process migration procedure. | Completed |
| 2 | Work Stealing Implementation | Implement the Work Stealing mechanism to allow an idle CPU to obtain a runnable process from another CPU's Run Queue. | Completed |
| 3 | Load Balancing Design and Implementation | Implement periodic load checking and process migration when the difference between CPU Run Queues exceeds a threshold. | Implemented |
| 4 | Scheduler Integration | Integrate Work Stealing and Load Balancing into the xv6 scheduler. | Implemented |
| 5 | Testing | Test Work Stealing, Load Balancing, CPU affinity, and scheduler behavior under different workloads. | Partially completed |
| 6 | Performance Evaluation | Run benchmark workloads and evaluate scheduler behavior. | Initial benchmark completed |

## 2. Completed Work

### 2.1. Work Stealing

Work Stealing allows an idle CPU to search other CPUs' Run Queues and take a runnable process when its own Run Queue is empty.

The implementation focuses on the following operations:

- Check whether the local CPU's Run Queue is empty.
- Search other online CPUs for runnable processes.
- Select a suitable victim CPU based on the number of eligible runnable processes.
- Check whether the selected process can run on the destination CPU, considering CPU affinity.
- Steal one process from the victim CPU and place it in the destination CPU's Run Queue.

**Main implementation files:**

| File | Detailed modifications |
|---|---|
| `kernel/worksteal.h` | Define the Work Stealing interface, statistics structure, and function declarations such as `worksteal_init()`, `worksteal_try()`, and statistics functions. |
| `kernel/worksteal.c` | Implement the main Work Stealing algorithm, including checking the local queue, selecting victim CPUs, checking process eligibility, performing stealing, and updating statistics. |
| `kernel/runqueue.h` | Use the Run Queue interface provided by Module 1, including the declaration for `rq_steal_runnable()`. |
| `kernel/runqueue.c` | Provide the actual process migration operation through `rq_steal_runnable()`. |

**Important functions in `kernel/worksteal.c`:**

- `proc_can_run_on()`: Checks whether a process is runnable and allowed to execute on the destination CPU.
- `count_eligible()`: Counts the processes that are eligible to run on a particular CPU.
- `worksteal_init()`: Initializes the Work Stealing mechanism and its statistics.
- `worksteal_try()`: Executes the stealing procedure when a CPU has no local runnable process.
- `worksteal_get_stats()`: Retrieves Work Stealing statistics.
- `worksteal_reset_stats()`: Resets the statistics.

The implementation currently performs one successful stealing operation per invocation of `worksteal_try()`.

### 2.2. Load Balancing

Load Balancing periodically checks the workloads of online CPUs and migrates processes when the workload difference is sufficiently large.

Unlike Work Stealing, which is triggered when a CPU becomes idle, Load Balancing proactively checks the distribution of runnable processes.

**Main implementation files:**

| File | Detailed modifications |
|---|---|
| `kernel/loadbalancing.h` | Define Load Balancing configuration parameters, statistics, and public function declarations. |
| `kernel/loadbalancing.c` | Implement periodic load checking, CPU workload comparison, process selection, affinity checking, and migration. |
| `kernel/runqueue.h` | Declare and expose the Run Queue transfer interface `rq_transfer()`. |
| `kernel/runqueue.c` | Implement `rq_transfer()` to move a process between two Run Queues. Unlike the stealing operation, this transfer supports a destination queue that already contains processes. |

**Configuration parameters in `kernel/loadbalancing.h`:**

| Parameter | Value | Purpose |
|---|---:|---|
| `LB_INTERVAL_TICKS` | 10 | Minimum interval between load-balancing checks. |
| `LB_IMBALANCE_THRESHOLD` | 2 | Minimum queue-length difference required to consider migration. |
| `LB_MAX_MOVES_PER_RUN` | 1 | Maximum number of processes migrated in one execution. |

**Important functions in `kernel/loadbalancing.c`:**

- `loadbalance_init()`: Initializes the Load Balancing mechanism.
- `loadbalance_run()`: Checks CPU workloads and performs migration when necessary.
- `loadbalance_get_stats()`: Retrieves Load Balancing statistics.
- `loadbalance_reset_stats()`: Resets the statistics.

The implementation also considers CPU affinity to ensure that a process is not migrated to a CPU where it is not allowed to execute.

### 2.3. Integration with Module 1 – Per-CPU Run Queues

Module 3 depends on the Per-CPU Run Queue implementation developed by **Trần Ngọc Dũng (Module 1)**.

The Run Queue provides the basic data structures and operations required by Work Stealing and Load Balancing.

**Files and contributions from Module 1:**

| File | Contribution |
|---|---|
| `kernel/runqueue.h` | Defines the Run Queue interface and function declarations. |
| `kernel/runqueue.c` | Implements queue initialization, enqueue, dequeue, queue length checking, process selection, removal, and migration operations. |
| `kernel/proc.c` | Integrates process management and scheduler operations with the Run Queue mechanism. |

**How Module 3 uses Module 1:**

- Work Stealing uses `rq_steal_runnable()` to obtain a process from another CPU.
- Load Balancing uses `rq_transfer()` to migrate a process between CPU queues.
- Both mechanisms use the Run Queue APIs instead of directly manipulating the internal queue structure.

This separation allows Module 3 to focus on scheduling decisions while Module 1 handles the underlying queue operations.

### 2.4. Integration with Module 2 – CPU Affinity

Module 3 also integrates with the CPU Affinity mechanism developed by **Vũ Phú Anh Tuấn (Module 2)**.

CPU Affinity determines which CPUs are allowed to execute a particular process.

**Files and contributions from Module 2:**

| File | Contribution |
|---|---|
| `kernel/proc.h` | Adds the `affinity_mask` field to `struct proc` to represent the CPUs allowed to execute a process. |
| `kernel/proc.c` | Initializes and maintains the affinity information during process creation and management. |

**How Module 3 uses CPU Affinity:**

- Before stealing a process, Work Stealing checks whether the destination CPU is permitted by the process's affinity mask.
- Before migrating a process, Load Balancing performs the same affinity check.
- Processes are not moved to CPUs that are excluded by their affinity configuration.

This prevents Work Stealing and Load Balancing from violating the CPU Affinity restrictions.

### 2.5. Scheduler Integration

The Work Stealing and Load Balancing mechanisms are integrated into the scheduler so that they can operate during normal process scheduling.

**Main files modified:**

| File | Detailed modifications |
|---|---|
| `kernel/main.c` | Initializes the Run Queue, Work Stealing, and Load Balancing mechanisms during kernel startup. |
| `kernel/proc.c` | Integrates Run Queue operations into process scheduling, process state transitions, and the scheduler loop. |

**Changes in `kernel/main.c`:**

The initialization sequence includes:

```c
procinit();
rq_init();
worksteal_init();
loadbalance_init();
```

**Changes in `kernel/proc.c`:**

The scheduler loop is modified to:

1. Obtain the online CPU mask.
2. Execute periodic Load Balancing checks on the coordinating CPU.
3. Attempt to dequeue a runnable process from the local Run Queue.
4. If the local queue is empty, invoke `worksteal_try()`.
5. Attempt to dequeue again after Work Stealing.
6. Execute the selected process if it is runnable and allowed on the current CPU.
7. Enter the idle state when no runnable process is available.

Process management operations such as `userinit()`, `kfork()`, `yield()`, and `wakeup()` are also integrated with the Run Queue mechanism so that runnable processes can be scheduled through per-CPU queues.

## 3. Testing Results

The following tests have been performed during implementation:

| Test | Purpose | Observed result |
|---|---|---|
| `testws` | Run multiple worker processes to create scheduling workload. | Four workers completed successfully. |
| `testmockws` | Test Work Stealing behavior using simulated queues. | Used to test mock scenarios, including empty queues and workload imbalance. |
| `forkforkfork` | Create a large number of processes to generate scheduler workload. | Test reached `OK`; one Load Balancing migration was observed. |
| `usertests` | Check general xv6 functionality after scheduler modifications. | Multiple tests reported `OK`; a complete successful run has not yet been confirmed. |

**Observed Work Stealing output:**

```text
WORK STEAL: CPU 1 stole a process
WORK STEAL: CPU 0 stole a process
```

This confirms that the Work Stealing migration path was triggered during the observed run.

**Observed Load Balancing output:**

```text
LOAD BALANCE: migrated 1 process(es)
```

This confirms that at least one Load Balancing migration occurred during testing.

## 4. Initial Benchmark

An initial benchmark workload was created to observe how multiple processes execute under the modified scheduler.

**Observed `testws` output:**

```text
testws: workers=4 iterations=1000000
testws: completed=4 elapsed=1 ticks
```

A later run reported:

```text
testws: workers=4 iterations=1000000
testws: completed=4 elapsed=2 ticks
```

These results show that all four worker processes completed their workloads.

However, these measurements are only initial observations. They are not sufficient to conclude that the new scheduler outperforms the original scheduler. A more reliable performance evaluation requires repeated runs under the same workload and a comparison with the original scheduler using metrics such as execution time, CPU utilization, and migration counts.
