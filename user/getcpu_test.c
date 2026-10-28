#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int cpu = getcpu();
  int pid = getpid();

  printf("Process PID = %d\n", pid);
  printf("Current CPU ID = %d\n", cpu);

  if (cpu >= 0) {
    printf("getcpu() test: PASS\n");
  } else {
    printf("getcpu() test: FAIL\n");
  }

  exit(0);
}