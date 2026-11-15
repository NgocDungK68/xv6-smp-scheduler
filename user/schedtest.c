// schedtest: end-to-end check of run queues / work stealing / load balancing.
//
//   schedtest [nchildren] [work]        (defaults: 4 children, work = 50)
//
// Forks N CPU-bound children that each do the same fixed amount of work, then
// prints the wall-clock time in ticks. Run it with `make qemu CPUS=1` and with
// `make qemu CPUS=4`: with a working multi-CPU scheduler the time drops to
// roughly 1/min(N, CPUS). Adjust `work` if it finishes too fast or too slow.
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static volatile unsigned sink;

// One unit = 1M iterations of a cheap LCG.
static void burn(int units)
{
  unsigned x = 1;

  for (int u = 0; u < units; u++)
    for (int i = 0; i < 1000000; i++)
      x = x * 1664525u + 1013904223u;
  sink = x;
}

int main(int argc, char *argv[])
{
  int n = argc > 1 ? atoi(argv[1]) : 4;
  int work = argc > 2 ? atoi(argv[2]) : 50;
  int started = 0;
  int done = 0;
  int t0 = uptime();

  for (int i = 0; i < n; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("schedtest: fork failed\n");
      break;
    }
    if (pid == 0) {
      burn(work);
      exit(0);
    }
    started++;
  }

  while (wait(0) > 0)
    done++;

  printf("schedtest: %d/%d children finished, work=%d, elapsed=%d ticks\n",
         done, started, work, uptime() - t0);
  exit(0);
}
