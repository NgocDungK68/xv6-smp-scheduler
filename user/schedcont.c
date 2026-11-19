// High Scheduling Contention benchmark for xv6-riscv.
//
// Modes:
//   schedcontention fork [workers] [rounds]
//     Run multiple workers concurrently. Each worker repeatedly fork/waits
//     for a short-lived child, stressing process creation, exit, wait and
//     scheduler transitions.
//
//   schedcontention pingpong [workers] [rounds]
//     Run multiple independent parent/child pairs. Each pair exchanges one
//     byte through two pipes per round, stressing pipe sleep/wakeup and
//     repeated context switches.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_WORKERS 8
#define DEFAULT_WORKERS 4
#define DEFAULT_ROUNDS 100
#define MAX_ROUNDS 2000
#define CHILD_BURN_ITERATIONS 3000

static void
short_cpu_work(void)
{
  volatile uint x = 1;
  for (int i = 0; i < CHILD_BURN_ITERATIONS; i++)
    x = x * 1664525U + 1013904223U;
  // Read the volatile result so the work remains observable.
  if (x == 0xdeadbeefU)
    printf("schedcontention: checksum marker\n");
}

// One worker repeatedly creates a short-lived child and waits for it.
// Returns 0 on success and -1 if fork/wait fails.
static int
run_fork_worker(int rounds)
{
  for (int i = 0; i < rounds; i++) {
    int pid = fork();
    if (pid < 0)
      return -1;

    if (pid == 0) {
      short_cpu_work();
      exit(0);
    }

    if (wait(0) < 0)
      return -1;
  }
  return 0;
}

// One worker creates a ping-pong pair using two unidirectional pipes.
// Each round performs parent->child and child->parent handoffs.
static int
run_pingpong_worker(int rounds)
{
  int to_child[2];
  int to_parent[2];
  int pid;
  int failed = 0;

  if (pipe(to_child) < 0)
    return -1;
  if (pipe(to_parent) < 0) {
    close(to_child[0]);
    close(to_child[1]);
    return -1;
  }

  pid = fork();
  if (pid < 0) {
    close(to_child[0]);
    close(to_child[1]);
    close(to_parent[0]);
    close(to_parent[1]);
    return -1;
  }

  if (pid == 0) {
    char token;
    close(to_child[1]);
    close(to_parent[0]);

    for (int i = 0; i < rounds; i++) {
      if (read(to_child[0], &token, 1) != 1)
        exit(1);
      if (write(to_parent[1], &token, 1) != 1)
        exit(1);
    }

    close(to_child[0]);
    close(to_parent[1]);
    exit(0);
  }

  close(to_child[0]);
  close(to_parent[1]);

  for (int i = 0; i < rounds; i++) {
    char token = (char)(i & 0x7f);
    char reply = 0;

    if (write(to_child[1], &token, 1) != 1) {
      failed = 1;
      break;
    }
    if (read(to_parent[0], &reply, 1) != 1 || reply != token) {
      failed = 1;
      break;
    }
  }

  close(to_child[1]);
  close(to_parent[0]);

  {
    int status = 0;
    if (wait(&status) < 0 || status != 0)
      failed = 1;
  }

  return failed ? -1 : 0;
}

static void
usage(void)
{
  printf("Usage:\n");
  printf("  schedcontention fork [workers] [rounds]\n");
  printf("  schedcontention pingpong [workers] [rounds]\n");
  printf("workers: 1..%d, rounds: 1..%d\n", MAX_WORKERS, MAX_ROUNDS);
}

int
main(int argc, char *argv[])
{
  int mode_pingpong = 0;
  int workers = DEFAULT_WORKERS;
  int rounds = DEFAULT_ROUNDS;
  int started = 0;
  int completed = 0;
  int failed_workers = 0;
  int start_ticks;
  int elapsed;

  if (argc > 1) {
    if (strcmp(argv[1], "fork") == 0)
      mode_pingpong = 0;
    else if (strcmp(argv[1], "pingpong") == 0)
      mode_pingpong = 1;
    else {
      usage();
      exit(1);
    }
  }
  if (argc > 2)
    workers = atoi(argv[2]);
  if (argc > 3)
    rounds = atoi(argv[3]);

  if (workers < 1 || workers > MAX_WORKERS ||
      rounds < 1 || rounds > MAX_ROUNDS) {
    usage();
    exit(1);
  }

  printf("HSC: mode=%s workers=%d rounds=%d\n",
         mode_pingpong ? "pingpong" : "fork", workers, rounds);
  printf("HSC: one operation = one fork/wait pair or one ping-pong round-trip\n");

  start_ticks = uptime();

  // Launch independent workers first so their workloads overlap.
  for (int i = 0; i < workers; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("HSC: worker fork failed at index %d\n", i);
      break;
    }
    if (pid == 0) {
      int rc = mode_pingpong ? run_pingpong_worker(rounds)
                             : run_fork_worker(rounds);
      exit(rc == 0 ? 0 : 1);
    }
    started++;
  }

  for (int i = 0; i < started; i++) {
    int status = 0;
    if (wait(&status) < 0) {
      failed_workers++;
      continue;
    }
    if (status == 0)
      completed++;
    else
      failed_workers++;
  }

  elapsed = uptime() - start_ticks;

  printf("HSC result: workers_started=%d workers_completed=%d failed_workers=%d\n",
         started, completed, failed_workers);
  printf("HSC result: planned_operations=%d elapsed_ticks=%d\n",
         started * rounds, elapsed);
  if (elapsed > 0)
    printf("HSC result: operations_per_tick=%d\n",
           (started * rounds) / elapsed);
  else
    printf("HSC result: elapsed time is below one timer tick; increase rounds\n");

  if (started != workers || completed != started || failed_workers != 0)
    exit(1);
  exit(0);
}
