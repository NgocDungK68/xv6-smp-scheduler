
#include "kernel/types.h"
#include "user/user.h"

#define TEST_ROUNDS 100
#define TEST_CPUS 4

static int
cpu_allowed(unsigned int mask, int cpu)
{
  if (cpu < 0 || cpu >= TEST_CPUS)
    return 0;

  return (mask & (1U << cpu)) != 0;
}

static int
test_dynamic_affinity(void)
{
  unsigned int masks[] = {
    0x1, 0x2, 0x4, 0x8, 0x3, 0xC, 0xF
  };

  int total = sizeof(masks) / sizeof(masks[0]);
  int passed = 0;

  printf("=== DYNAMIC CPU AFFINITY TEST ===\n");

  for (int i = 0; i < total; i++) {
    unsigned int mask = masks[i];
    int valid = 1;

    if (sched_setaffinity(0, mask) != 0) {
      printf("FAIL: cannot set mask=%x\n", mask);
      continue;
    }

    // Allow the process to be rescheduled.
    for (int j = 0; j < TEST_ROUNDS; j++) {
      pause(1);

      int cpu = getcpu();

      if (!cpu_allowed(mask, cpu)) {
        printf("FAIL: mask=%x cpu=%d round=%d\n",
               mask, cpu, j);
        valid = 0;
        break;
      }
    }

    if (valid) {
      printf("PASS: mask=%x, %d observations\n",
             mask, TEST_ROUNDS);
      passed++;
    }
  }

  printf("Dynamic affinity: %d/%d passed\n",
         passed, total);

  return passed == total;
}

int
main(void)
{
  int result = test_dynamic_affinity();

  exit(result ? 0 : 1);
}
