

#include "kernel/types.h"
#include "user/user.h"

#define TEST_ROUNDS 100
#define TEST_CPUS   4

// Check whether the observed CPU is allowed by the mask.
static int
check_cpu(unsigned int mask, int cpu)
{
  if (cpu < 0 || cpu >= TEST_CPUS)
    return 0;

  return (mask & (1U << cpu)) != 0;
}

// Run an affinity test in a child process.
static int
run_test(unsigned int mask)
{
  int cpu;

  // Set the affinity mask for the current process.
  if (sched_setaffinity(0, mask) != 0) {
    printf("FAIL: cannot set mask=%x\n", mask);
    return 0;
  }

  // Repeatedly observe the CPU after rescheduling.
  for (int i = 0; i < TEST_ROUNDS; i++) {
    pause(1);

    cpu = getcpu();

    if (!check_cpu(mask, cpu)) {
      printf("FAIL: mask=%x, observed CPU=%d, round=%d\n", mask, cpu, i);
      return 0;
    }
  }

  printf("PASS: mask=%x, %d observations valid\n", mask, TEST_ROUNDS);

  return 1;
}

int
main(void)
{
  unsigned int masks[] = {
    0x1, // CPU 0
    0x2, // CPU 1
    0x4, // CPU 2
    0x8, // CPU 3
    0x3, // CPU 0 and CPU 1
    0xF  // All four CPUs
  };

  int total = sizeof(masks) / sizeof(masks[0]);
  int passed = 0;

  printf("=== CPU Affinity Runtime Test ===\n");

  for (int i = 0; i < total; i++) {
    printf("Test %d: mask=%x\n", i + 1, masks[i]);

    int pid = fork();

    if (pid < 0) {
      printf("FAIL: fork failed for mask=%x\n", masks[i]);
      continue;
    }

    if (pid == 0) {
      // Each child tests one affinity mask.
      int result = run_test(masks[i]);
      exit(result ? 0 : 1);
    }

    // Parent collects the child's result.
    int status = -1;

    if (wait(&status) < 0) {
      printf("FAIL: wait failed for mask=%x\n", masks[i]);
      continue;
    }

    if (status == 0)
      passed++;
  }

  printf("Runtime tests: %d/%d passed\n", passed, total);

  exit(passed == total ? 0 : 1);
}
