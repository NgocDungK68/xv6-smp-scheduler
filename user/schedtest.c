// schedtest: CPU-bound scheduler workload and completion check.
// Usage: schedtest [nchildren] [work]
// Defaults: 4 children, 50 work units (1M iterations per unit).
// Compare elapsed ticks across QEMU CPU counts, but do not assume
// a fixed speedup or use elapsed time alone to prove migration.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define DEFAULT_CHILDREN 4
#define DEFAULT_WORK     50
#define MAX_CHILDREN     16

static volatile uint sink;

static void
burn(int units)
{
  volatile uint x = 1;

  for (int u = 0; u < units; u++) {
    for (int i = 0; i < 1000000; i++)
      x = x * 1664525U + 1013904223U;
  }

  sink = x;
}

int
main(int argc, char *argv[])
{
  if (argc > 3) {
    printf("usage: schedtest [nchildren: 1-%d] [work: positive]\n",
           MAX_CHILDREN);
    exit(1);
  }

  int n = argc > 1 ? atoi(argv[1]) : DEFAULT_CHILDREN;
  int work = argc > 2 ? atoi(argv[2]) : DEFAULT_WORK;

  if (n < 1 || n > MAX_CHILDREN || work < 1) {
    printf("usage: schedtest [nchildren: 1-%d] [work: positive]\n",
           MAX_CHILDREN);
    exit(1);
  }

  int started = 0;
  int completed = 0;
  int reaped = 0;
  int start_ticks = uptime();

  for (int i = 0; i < n; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("schedtest: fork failed at child %d\n", i);
      break;
    }

    if (pid == 0) {
      burn(work);
      exit(0);
    }

    started++;
  }

  int status;
  while (wait(&status) > 0) {
    reaped++;
    if (status == 0)
      completed++;
  }

  int elapsed = uptime() - start_ticks;
  int passed = started == n && reaped == n && completed == n;

  printf("schedtest: %d/%d children completed, work=%d, elapsed=%d ticks\n",
         completed, n, work, elapsed);
  printf("schedtest: %s\n", passed ? "PASS" : "FAIL");

  exit(passed ? 0 : 1);
}
