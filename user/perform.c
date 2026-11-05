#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define OPS 30000000   // ~270M tổng giá trị sum, không tràn int 32-bit

static void
do_work(void)
{
  volatile int sum = 0;
  for(int i = 0; i < OPS; i++)
    sum += (i % 7) * 3;
}

static int
run_test(int n)
{
  int t0 = uptime();
  for(int i = 0; i < n; i++){
    int pid = fork();
    if(pid < 0){
      printf("fork failed\n");
      exit(1);
    }
    if(pid == 0){
      do_work();
      exit(0);
    }
  }
  for(int i = 0; i < n; i++)
    wait(0);
  int t = uptime() - t0;
  return t ? t : 1;
}

int
main(int argc, char *argv[])
{
  char *label = argc > 1 ? argv[1] : "run";
  int maxp = argc > 2 ? atoi(argv[2]) : 8;
  int base = 0;

  printf("label,procs,ticks,kops_per_sec,speedup_x100\n");
  for(int n = 1; n <= maxp; n *= 2){
    int t = run_test(n);
    int kops = (n * (OPS / 1000)) * 10 / t;
    if(n == 1) base = kops;
    printf("%s,%d,%d,%d,%d\n", label, n, t, kops, kops * 100 / base);
  }
  exit(0);
}
