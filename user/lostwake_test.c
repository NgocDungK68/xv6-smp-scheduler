
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define WORKERS 4
#define ROUNDS  1000
#define BLOCK   128

static void
worker(int id)
{
  int fd[2];

  if (pipe(fd) < 0) {
    printf("worker %d: pipe failed\n", id);
    exit(1);
  }

  int pid = fork();

  if (pid < 0) {
    printf("worker %d: fork failed\n", id);
    close(fd[0]);
    close(fd[1]);
    exit(1);
  }

  // Child: ghi du lieu vao pipe.
  if (pid == 0) {
    close(fd[0]);

    char buf[BLOCK];

    for (int i = 0; i < BLOCK; i++)
      buf[i] = (char)(id + i);

    for (int round = 0; round < ROUNDS; round++) {
      int offset = 0;

      while (offset < BLOCK) {
        int n = write(fd[1], buf + offset, BLOCK - offset);

        if (n <= 0) {
          printf("worker %d: write failed\n", id);
          close(fd[1]);
          exit(1);
        }

        offset += n;
      }
    }

    close(fd[1]);
    exit(0);
  }

  // Parent: doc va kiem tra du lieu.
  close(fd[1]);

  char buf[BLOCK];
  int failures = 0;

  for (int round = 0; round < ROUNDS; round++) {
    int offset = 0;

    while (offset < BLOCK) {
      int n = read(fd[0], buf + offset, BLOCK - offset);

      if (n <= 0) {
        printf("worker %d: unexpected EOF\n", id);
        failures++;
        break;
      }

      offset += n;
    }

    if (failures > 0)
      break;

    for (int i = 0; i < BLOCK; i++) {
      if (buf[i] != (char)(id + i)) {
        printf("worker %d: data mismatch\n", id);
        failures++;
        break;
      }
    }

    if (failures > 0)
      break;
  }

  close(fd[0]);

  int status = -1;
  int done = wait(&status);

  if (done != pid || status != 0)
    failures++;

  exit(failures ? 1 : 0);
}

int
main(void)
{
  printf("=== LOST WAKEUP STRESS TEST ===\n");
  printf("workers=%d rounds=%d block=%d\n", WORKERS, ROUNDS, BLOCK);

  int created = 0;
  int failures = 0;

  // Tao cac worker chay dong thoi.
  for (int i = 0; i < WORKERS; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("fork failed at worker %d\n", i);
      failures++;
      break;
    }

    if (pid == 0)
      worker(i);

    created++;
  }

  // Cho tat ca worker da tao hoan thanh.
  for (int i = 0; i < created; i++) {
    int status = -1;
    int pid = wait(&status);

    if (pid < 0 || status != 0) {
      printf("worker failed: pid=%d status=%d\n", pid, status);
      failures++;
    }
  }

  if (failures == 0) {
    printf("LOST WAKEUP TEST: PASS\n");
  } else {
    printf("LOST WAKEUP TEST: FAIL (%d errors)\n", failures);
  }

  exit(failures ? 1 : 0);
}
