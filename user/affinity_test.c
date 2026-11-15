#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int result;
  int passed = 0;
  int total = 7;

  result = sched_setaffinity(0, 1);
  printf("Test 1 - Valid mask: %s\n", result == 0 ? "PASS" : "FAIL");
  if (result == 0)
    passed++;

  result = sched_setaffinity(0, 0);
  printf("Test 2 - Zero mask: %s\n", result == -1 ? "PASS" : "FAIL");
  if (result == -1)
    passed++;

  result = sched_setaffinity(-1, 1);
  printf("Test 3 - Negative PID: %s\n", result == -1 ? "PASS" : "FAIL");
  if (result == -1)
    passed++;

  result = sched_setaffinity(9999, 1);
  printf("Test 4 - Nonexistent PID: %s\n", result == -1 ? "PASS" : "FAIL");
  if (result == -1)
    passed++;

  result = sched_setaffinity(0, 256);
  printf("Test 5 - Invalid CPU mask: %s\n", result == -1 ? "PASS" : "FAIL");
  if (result == -1)
    passed++;

  result = sched_setaffinity(getpid(), 3);
  printf("Test 6 - Existing PID: %s\n", result == 0 ? "PASS" : "FAIL");
  if (result == 0)
    passed++;

  int valid = 1;
  int masks[] = {1, 3, 7, 15};

  for (int i = 0; i < 4; i++) {
    result = sched_setaffinity(0, masks[i]);

    if (result != 0) {
      valid = 0;
      break;
    }
  }

  printf("Test 7 - Multiple mask updates: %s\n", valid ? "PASS" : "FAIL");
  if (valid)
    passed++;

  printf("Affinity tests: %d/%d passed\n", passed, total);

  exit(passed == total ? 0 : 1);
}