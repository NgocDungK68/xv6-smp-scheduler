
#include "kernel/types.h"
#include "user/user.h"

#define WORKERS   3
#define ROUNDS    40
#define NMASKS    7
#define TEST_CPUS 4

static uint masks[NMASKS] = {0x1, 0x2, 0x4, 0x8, 0x3, 0xC, 0xF};

struct result {
  int round;
  int cpu;
  int passed;
};

static int
allowed(uint mask, int cpu)
{
  return cpu >= 0 && cpu < TEST_CPUS && (mask & (1U << cpu)) != 0;
}

static void
worker(int startfd, int ackfd)
{
  uint mask;

  for (int round = 0; round < ROUNDS; round++) {
    if (read(startfd, &mask, sizeof(mask)) != sizeof(mask))
      exit(1);

    struct result r;
    r.round = round;
    r.cpu = -1;
    r.passed = 1;

    if (pause(1) < 0)
      r.passed = 0;

    if (r.passed) {
      r.cpu = getcpu();
      if (!allowed(mask, r.cpu))
        r.passed = 0;
    }

    // Sample the CPU during CPU-bound execution.
    volatile uint x = (uint)getpid();

    for (uint j = 0; j < 300000U && r.passed; j++) {
      x = x * 1664525U + 1013904223U;

      if ((j % 10000U) == 0) {
        int cpu = getcpu();

        if (!allowed(mask, cpu)) {
          r.cpu = cpu;
          r.passed = 0;
        }
      }
    }

    (void)x;

    if (write(ackfd, &r, sizeof(r)) != sizeof(r))
      exit(1);

    if (!r.passed)
      exit(1);
  }

  close(startfd);
  close(ackfd);
  exit(0);
}

int
main(void)
{
  int start[WORKERS][2];
  int ack[WORKERS][2];
  int pids[WORKERS];

  int passed = 0;
  int failed = 0;
  int stopped = 0;
  int cpu_changes = 0;
  int last_cpu[WORKERS];

  printf("=== AFFINITY MIGRATION TEST ===\n");

  for (int i = 0; i < WORKERS; i++)
    last_cpu[i] = -1;

  for (int i = 0; i < WORKERS; i++) {
    if (pipe(start[i]) < 0 || pipe(ack[i]) < 0) {
      printf("ERROR: pipe creation failed\n");
      exit(1);
    }

    int pid = fork();

    if (pid < 0) {
      printf("ERROR: fork failed\n");
      exit(1);
    }

    if (pid == 0) {
      close(start[i][1]);
      close(ack[i][0]);
      worker(start[i][0], ack[i][1]);
    }

    close(start[i][0]);
    close(ack[i][1]);
    pids[i] = pid;
  }

  for (int round = 0; round < ROUNDS && !stopped; round++) {

    // Update every worker's affinity.
    for (int i = 0; i < WORKERS; i++) {
      uint mask = masks[(round + i) % NMASKS];

      if (sched_setaffinity(pids[i], mask) < 0) {
        printf("SET FAIL: worker=%d round=%d\n", i, round);
        failed++;
        stopped = 1;
        break;
      }

      if (write(start[i][1], &mask, sizeof(mask)) != sizeof(mask)) {
        printf("START FAIL: worker=%d round=%d\n", i, round);
        failed++;
        stopped = 1;
        break;
      }
    }

    if (stopped)
      break;

    // Wait until all workers finish this round.
    for (int i = 0; i < WORKERS; i++) {
      struct result r;

      if (read(ack[i][0], &r, sizeof(r)) != sizeof(r)) {
        printf("ACK FAIL: worker=%d round=%d\n", i, round);
        failed++;
        stopped = 1;
        continue;
      }

      if (r.round != round || !r.passed) {
        printf("AFFINITY FAIL: worker=%d round=%d cpu=%d\n", i, round, r.cpu);
        failed++;
        stopped = 1;
      } else {
        passed++;

        if (last_cpu[i] >= 0 && last_cpu[i] != r.cpu)
          cpu_changes++;

        last_cpu[i] = r.cpu;
      }
    }

    if ((round + 1) % 10 == 0) {
      printf("Progress: %d/%d rounds, checks=%d\n", round + 1, ROUNDS, passed);
    }
  }

  for (int i = 0; i < WORKERS; i++) {
    close(start[i][1]);
    close(ack[i][0]);
  }

  int completed = 0;

  for (int i = 0; i < WORKERS; i++) {
    int status = -1;

    if (wait(&status) > 0 && status == 0)
      completed++;
  }

  printf("\n=== FINAL SUMMARY ===\n");
  printf("Affinity checks: %d/%d\n", passed, WORKERS * ROUNDS);
  printf("Failures: %d\n", failed);
  printf("Workers completed: %d/%d\n", completed, WORKERS);
  printf("Observed CPU changes: %d\n", cpu_changes);

  int ok = !stopped && failed == 0 && passed == WORKERS * ROUNDS &&
           completed == WORKERS;

  printf("Affinity: %s\n", ok ? "PASS" : "FAIL");

  if (cpu_changes > 0)
    printf("CPU changes: OBSERVED\n");
  else
    printf("CPU changes: NOT OBSERVED\n");

  exit(ok ? 0 : 1);
}
