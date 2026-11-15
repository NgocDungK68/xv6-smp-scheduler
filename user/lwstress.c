
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 10 ticks/giay theo timer cua xv6 hien tai
#define DURATION_TICKS  6000
#define REPORT_INTERVAL 10

int
main(void)
{
  uint start = uptime();
  int passed = 0;
  int failed = 0;

  printf("=== LOST WAKEUP STRESS TEST 10 MIN ===\n");
  printf("Start ticks: %d\n", start);

  while ((uint)(uptime() - start) < DURATION_TICKS) {
    int pid = fork();

    if (pid < 0) {
      printf("FAIL: fork\n");
      failed++;
      break;
    }

    if (pid == 0) {
      char *argv[] = {"lostwake_test", 0};
      exec("lostwake_test", argv);
      printf("FAIL: exec\n");
      exit(1);
    }

    int status = -1;
    int done = wait(&status);

    if (done != pid || status != 0) {
      printf("FAIL: round=%d status=%d\n", passed + failed + 1, status);
      failed++;
      break;
    }

    passed++;

    if (passed % REPORT_INTERVAL == 0) {
      printf("PROGRESS: passed=%d failed=%d elapsed_ticks=%d\n", passed, failed,
             (uint)(uptime() - start));
    }
  }

  uint elapsed = (uint)(uptime() - start);

  printf("\n=== FINAL SUMMARY ===\n");
  printf("Passed: %d\n", passed);
  printf("Failed: %d\n", failed);
  printf("Elapsed ticks: %d\n", elapsed);

  if (failed == 0 && elapsed >= DURATION_TICKS) {
    printf("RESULT: PASS\n");
    exit(0);
  }

  printf("RESULT: FAIL / INCOMPLETE\n");
  exit(1);
}
