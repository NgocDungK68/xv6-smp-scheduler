#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// Stress xv6 logging system by having several processes writing
// concurrently to their own file (e.g., logstress f1 f2 f3 f4)
//
// test-xv6.py kills QEMU 2 seconds after starting logstress, then expects
// (1) a commit to have been in flight and (2) every file to exist.
// So: create all files up front (so they exist no matter how the
// children get scheduled), and when a file/disk is full, truncate and
// keep writing so the log keeps committing.

#define BUFSZ 500

char buf[BUFSZ];

int
main(int argc, char **argv)
{
  int fd, n;
  enum { N = 1500, SZ = 2000 };

  for (int i = 1; i < argc; i++) {
    fd = open(argv[i], O_CREATE | O_RDWR);
    if (fd < 0) {
      printf("%s: create %s failed\n", argv[0], argv[i]);
      exit(1);
    }
    close(fd);
  }

  for (int i = 1; i < argc; i++) {
    int pid1 = fork();
    if (pid1 < 0) {
      printf("%s: fork failed\n", argv[0]);
      exit(1);
    }
    if (pid1 == 0) {
      char *name = argv[i];
      fd = open(name, O_CREATE | O_RDWR);
      if (fd < 0) {
        printf("%s: open %s failed\n", argv[0], name);
        exit(1);
      }
      memset(buf, '0' + i, SZ);
      for (int j = 0; j < N; j++) {
        if ((n = write(fd, buf, SZ)) != SZ) {
          // file or disk full: start over from an empty file
          close(fd);
          fd = open(name, O_CREATE | O_RDWR | O_TRUNC);
          if (fd < 0) {
            printf("%s: truncate %s failed\n", argv[0], name);
            exit(1);
          }
        }
      }
      exit(0);
    }
  }
  int xstatus;
  for (int i = 1; i < argc; i++) {
    wait(&xstatus);
    if (xstatus != 0)
      exit(xstatus);
  }
  return 0;
}
