
#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define ALL_CPU_MASK    ((1U << NCPU) - 1U)
#define WORKERS_PER_CPU 2
#define STRESS_ROUNDS   3
#define WORK_ITERATIONS 1500000U

static int
valid_cpu(int cpu, int online_cpus)
{
  return cpu >= 0 && cpu < online_cpus;
}

// T1: Reject invalid or offline CPU affinity masks.
static int
test_invalid_affinity(int online_cpus)
{
  int ok = 1;

  if (sched_setaffinity(0, 0) != -1)
    ok = 0;

  if (sched_setaffinity(0, 1U << NCPU) != -1)
    ok = 0;

  if (sched_setaffinity(-1, 1) != -1)
    ok = 0;

  if (sched_setaffinity(99999, 1) != -1)
    ok = 0;

  if (online_cpus < NCPU && sched_setaffinity(0, 1U << online_cpus) != -1)
    ok = 0;

  if (sched_setaffinity(0, ALL_CPU_MASK) != 0)
    ok = 0;

  return ok;
}

// T2/T3: Check fork inheritance, CPU pinning and sleep/wakeup.
static int
test_pinned_fork(int target, int sleeps)
{
  if (sched_setaffinity(0, 1U << target) != 0)
    return 0;

  int pid = fork();

  if (pid == 0) {
    int ok = getcpu() == target;

    for (int i = 0; i < sleeps; i++) {
      if (pause(1) < 0 || getcpu() != target)
        ok = 0;
    }

    exit(ok ? 0 : 1);
  }

  // Restore parent's affinity even when fork fails.
  int restored = sched_setaffinity(0, ALL_CPU_MASK) == 0;

  if (pid < 0)
    return 0;

  int status = -1;
  int reaped = wait(&status);

  return restored && reaped == pid && status == 0;
}

static int
test_each_cpu(int online_cpus, int sleeps)
{
  int passed = 0;

  for (int cpu = 0; cpu < online_cpus; cpu++) {
    int ok = test_pinned_fork(cpu, sleeps);

    printf("    CPU %d: %s\n", cpu, ok ? "PASS" : "FAIL");

    passed += ok;
  }

  printf("    Result: %d/%d CPUs\n", passed, online_cpus);
  return passed == online_cpus;
}

// T4: Update another process's affinity.
// The child must run on CPU 1 after its next scheduling point.
static int
test_affinity_update(void)
{
  int ready[2];
  int start[2];

  if (pipe(ready) < 0)
    return 0;

  if (pipe(start) < 0) {
    close(ready[0]);
    close(ready[1]);
    return 0;
  }

  if (sched_setaffinity(0, 1U) != 0) {
    close(ready[0]);
    close(ready[1]);
    close(start[0]);
    close(start[1]);
    return 0;
  }

  int pid = fork();

  if (pid == 0) {
    close(ready[0]);
    close(start[1]);

    int ok = getcpu() == 0;
    char signal = ok ? 'R' : 'F';

    if (write(ready[1], &signal, 1) != 1)
      ok = 0;

    close(ready[1]);

    char token;
    if (read(start[0], &token, 1) != 1)
      ok = 0;

    close(start[0]);

    // Apply updated affinity after sleeping and rescheduling.
    if (pause(1) < 0 || getcpu() != 1)
      ok = 0;

    exit(ok ? 0 : 1);
  }

  int restored = sched_setaffinity(0, ALL_CPU_MASK) == 0;

  close(ready[1]);
  close(start[0]);

  if (pid < 0) {
    close(ready[0]);
    close(start[1]);
    return 0;
  }

  char signal = 'F';
  int got_ready = read(ready[0], &signal, 1) == 1;
  close(ready[0]);

  int changed = sched_setaffinity(pid, 1U << 1) == 0;

  char token = 'G';
  int released = write(start[1], &token, 1) == 1;
  close(start[1]);

  int status = -1;
  int reaped = wait(&status);

  return restored && got_ready && signal == 'R' && changed && released &&
         reaped == pid && status == 0;
}

// Worker for T5/T6/T7.
// All workers wait at a pipe barrier before CPU-bound execution.
static void
worker_run(int online_cpus, int target, int pinned, int ready_fd, int start_fd,
           int report_fd, uint iterations)
{
  int ok = pause(1) >= 0;
  int cpu = getcpu();

  if (!valid_cpu(cpu, online_cpus) || (pinned && cpu != target))
    ok = 0;

  char ready = ok ? 'R' : 'F';

  if (write(ready_fd, &ready, 1) != 1)
    ok = 0;

  close(ready_fd);

  // Wait until the parent releases all workers.
  char token;

  if (read(start_fd, &token, 1) != 1)
    ok = 0;

  close(start_fd);

  // CPU-bound workload with periodic CPU verification.
  volatile uint checksum = (uint)getpid();

  for (uint i = 0; ok && i < iterations; i++) {
    checksum = checksum * 1664525U + i + 1013904223U;

    if ((i & 0x3ffffU) == 0) {
      cpu = getcpu();

      if (!valid_cpu(cpu, online_cpus) || (pinned && cpu != target))
        ok = 0;
    }
  }

  cpu = getcpu();

  if (!valid_cpu(cpu, online_cpus) || (pinned && cpu != target))
    ok = 0;

  // Report the CPU where this worker last executed.
  char report = (char)cpu;

  if (write(report_fd, &report, 1) != 1)
    ok = 0;

  close(report_fd);

  exit(ok ? 0 : 1);
}

// T5/T6/T7: Run multiple workers concurrently.
// pinned = 1: Each worker is restricted to a specific CPU.
// pinned = 0: Workers may run on any online CPU.
static int
test_concurrent(int online_cpus, int pinned, uint iterations, int round)
{
  int ready[2];
  int start[2];
  int report[2];

  if (pipe(ready) < 0)
    return 0;

  if (pipe(start) < 0) {
    close(ready[0]);
    close(ready[1]);
    return 0;
  }

  if (pipe(report) < 0) {
    close(ready[0]);
    close(ready[1]);
    close(start[0]);
    close(start[1]);
    return 0;
  }

  int wanted = WORKERS_PER_CPU * online_cpus;
  int launched = 0;
  int setup_ok = sched_setaffinity(0, ALL_CPU_MASK) == 0;

  uint start_ticks = uptime();

  // Create all workers before releasing the start barrier.
  for (int i = 0; i < wanted && setup_ok; i++) {
    int target = i % online_cpus;

    if (pinned && sched_setaffinity(0, 1U << target) != 0) {
      setup_ok = 0;
      break;
    }

    int pid = fork();

    if (pid == 0) {
      close(ready[0]);
      close(start[1]);
      close(report[0]);

      worker_run(online_cpus, target, pinned, ready[1], start[0], report[1],
                 iterations);
    }

    // Restore the parent for the next worker.
    if (sched_setaffinity(0, ALL_CPU_MASK) != 0)
      setup_ok = 0;

    if (pid < 0) {
      setup_ok = 0;
      break;
    }

    launched++;
  }

  close(ready[1]);
  close(start[0]);
  close(report[1]);

  // Wait for every launched worker to reach the barrier.
  int ready_ok = 0;

  for (int i = 0; i < launched; i++) {
    char signal = 'F';

    if (read(ready[0], &signal, 1) == 1 && signal == 'R')
      ready_ok++;
  }

  close(ready[0]);

  // Release workers to start CPU-bound work.
  int released = 0;

  for (int i = 0; i < launched; i++) {
    char token = 'G';

    if (write(start[1], &token, 1) == 1)
      released++;
  }

  close(start[1]);

  // Reap all launched workers and inspect exit status.
  int completed = 0;

  for (int i = 0; i < launched; i++) {
    int status = -1;

    if (wait(&status) > 0 && status == 0)
      completed++;
  }

  // Collect final CPU IDs observed by workers.
  uint observed = 0;
  int reports = 0;
  char cpu = 0;

  while (read(report[0], &cpu, 1) == 1) {
    int id = (unsigned char)cpu;
    reports++;

    if (valid_cpu(id, online_cpus))
      observed |= 1U << id;
    else
      setup_ok = 0;
  }

  close(report[0]);

  uint elapsed = uptime() - start_ticks;

  int ok = setup_ok && launched == wanted && ready_ok == wanted &&
           released == wanted && completed == wanted && reports == wanted;

  // Every CPU must be observed when workers are pinned across all CPUs.
  if (pinned && observed != ((1U << online_cpus) - 1U))
    ok = 0;

  printf("    Round %d: %d/%d completed, "
         "ready %d/%d, CPUs=0x%x, %d ticks: %s\n",
         round, completed, wanted, ready_ok, wanted, observed, (int)elapsed,
         ok ? "PASS" : "FAIL");

  return ok;
}

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("Usage: scheduler_test <online_cpus>\n");
    exit(1);
  }

  int online_cpus = atoi(argv[1]);

  if (online_cpus < 1 || online_cpus > NCPU) {
    printf("scheduler_test: invalid online CPU count\n");
    exit(1);
  }

  int total = online_cpus >= 2 ? 7 : 6;
  int passed = 0;

  printf("\n=== SMP Scheduler Test (CPUS=%d) ===\n", online_cpus);

  // T1
  int ok = test_invalid_affinity(online_cpus);
  printf("[T1] Invalid/offline affinity: %s\n", ok ? "PASS" : "FAIL");
  passed += ok;

  // T2
  printf("[T2] Fork inheritance and CPU pinning:\n");
  ok = test_each_cpu(online_cpus, 0);
  passed += ok;

  // T3
  printf("[T3] Repeated sleep/wakeup (3 rounds per CPU):\n");
  ok = test_each_cpu(online_cpus, 3);
  passed += ok;

  // T4
  if (online_cpus >= 2) {
    ok = test_affinity_update();

    printf("[T4] External affinity update: %s\n", ok ? "PASS" : "FAIL");

    passed += ok;
  } else {
    printf("[T4] External affinity update: "
           "SKIP (need 2 CPUs)\n");
  }

  // T5
  printf("[T5] Concurrent pinned workers:\n");
  ok = test_concurrent(online_cpus, 1, WORK_ITERATIONS, 1);
  passed += ok;

  // T6
  printf("[T6] Concurrent unrestricted workers:\n");
  ok = test_concurrent(online_cpus, 0, WORK_ITERATIONS, 1);
  passed += ok;

  // T7
  printf("[T7] Repeated SMP contention (%d rounds):\n", STRESS_ROUNDS);

  ok = 1;

  for (int round = 1; round <= STRESS_ROUNDS; round++) {
    if (!test_concurrent(online_cpus, 1, WORK_ITERATIONS / 2, round))
      ok = 0;
  }

  passed += ok;

  printf("\nSMP Scheduler: %d/%d tests passed\n", passed, total);

  printf("SMP Scheduler: %s\n", passed == total ? "PASS" : "FAIL");

  exit(passed == total ? 0 : 1);
}
