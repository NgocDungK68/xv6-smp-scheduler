#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int
main(void)
{
  int passed = 0;
  int total = 3;
  int cpu;
  int valid = 1;
  int pid;
  int status;

  cpu = getcpu();

  if (cpu >= 0 && cpu < NCPU) {
    printf("Test 1 - Valid CPU ID: PASS\n");
    passed++;
  } else {
    printf("Test 1 - Valid CPU ID: FAIL\n");
  }

  for (int i = 0; i < 100; i++) {
    cpu = getcpu();

    if (cpu < 0 || cpu >= NCPU) {
      valid = 0;
      break;
    }
  }

  if (valid) {
    printf("Test 2 - Repeated getcpu calls: PASS\n");
    passed++;
  } else {
    printf("Test 2 - Repeated getcpu calls: FAIL\n");
  }

  pid = fork();

  if (pid < 0) {
    printf("Test 3 - Child process: FAIL\n");
  } else if (pid == 0) {
    cpu = getcpu();

    if (cpu >= 0 && cpu < NCPU)
      exit(0);

    exit(1);
  } else {
    status = -1;

    if (wait(&status) == pid && status == 0) {
      printf("Test 3 - Child process: PASS\n");
      passed++;
    } else {
      printf("Test 3 - Child process: FAIL\n");
    }
  }

  printf("getcpu tests: %d/%d passed\n", passed, total);

  exit(passed == total ? 0 : 1);
}