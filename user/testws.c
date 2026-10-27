
    
  
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define DEFAULT_WORKERS 4
#define DEFAULT_ITERATIONS 1000000
#define MAX_WORKERS 16

static void
burn_cpu(uint iterations)
{
  // Volatile keeps the repeated computation observable to the compiler.
  volatile uint x = (uint)getpid();
  for (uint i = 0; i < iterations; i++) {
    x = x * 1664525U + 1013904223U;
    x ^= x >> 13;
  }
  // Read the final value so the workload has an observable result.
  if (x == 0x12345678U)
    printf("testws: checksum marker reached\n");
}

int
main(int argc, char *argv[])
{
  int workers = DEFAULT_WORKERS;
  int iterations = DEFAULT_ITERATIONS;
  int launched = 0;
  int start_ticks;
  int end_ticks;

  if (argc > 1)
    workers = atoi(argv[1]);
  if (argc > 2)
    iterations = atoi(argv[2]);

  if (workers < 1 || workers > MAX_WORKERS) {
    printf("usage: testws [workers: 1-%d] [iterations: positive]\n", MAX_WORKERS);
    exit(1);
  }
  if (iterations < 1) {
    printf("testws: iterations must be positive\n");
    exit(1);
  }

  printf("testws: workers=%d iterations=%d\n", workers, iterations);
  start_ticks = uptime();

  for (int i = 0; i < workers; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("testws: fork failed after %d workers\n", launched);
      break;
    }
    if (pid == 0) {
      burn_cpu((uint)iterations);
      exit(0);
    }
    launched++;
  }

  for (int i = 0; i < launched; i++)
    wait(0);

  end_ticks = uptime();
  printf("testws: completed=%d elapsed=%d ticks\n",
         launched, end_ticks - start_ticks);
  exit(0);
}


