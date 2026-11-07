#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define OPS_PER_PROCESS 10000000

// Hàm giả lập tính toán CPU-bound nặng
void
do_work()
{
  volatile int sum = 0;
  (void)sum;

  // Vòng lặp giả lập khối lượng công việc nặng
  for (int i = 0; i < 10000000; i++) {
    sum += i;
  }

  // In ra kết quả để trình biên dịch hiểu biến sum có được sử dụng
  printf("Worker finished, sum = %d\n", sum);
}

// Hàm chạy kịch bản với số lượng tiến trình cho trước
void
run_test(int num_procs)
{
  int pids[32];
  uint64 start_time, end_time;

  printf("=== Running benchmark with %d processes ===\n", num_procs);
  start_time = uptime();

  for (int i = 0; i < num_procs; i++) {
    pids[i] = fork();
    if (pids[i] < 0) {
      printf("Fork failed\n");
      exit(1);
    }
    if (pids[i] == 0) {
      // Tiến trình con thực hiện công việc
      do_work();
      exit(0);
    }
  }

  // Tiến trình cha chờ tất cả các con hoàn thành
  for (int i = 0; i < num_procs; i++) {
    wait(0);
  }

  end_time = uptime();
  printf("All %d processes finished in %lu ticks\n", num_procs, end_time - start_time);
}

int
main(int argc, char *argv[])
{
  printf("Starting CPU benchmark suite for xv6 SMP Scheduler...\n");
  
  // Chạy thử nghiệm với các mức số lượng tiến trình khác nhau
  run_test(1);
  run_test(2);
  run_test(4);
  run_test(8);

  printf("Benchmark completed successfully.\n");
  exit(0);
}