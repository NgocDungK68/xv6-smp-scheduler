#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define DEFAULT_WORKERS     4
#define DEFAULT_ITERATIONS  10000000
#define MAX_WORKERS         16
#define CPU_SAMPLE_INTERVAL 100000

struct worker_stats {
  int pid;
  uint cpu_mask;
  int first_cpu;
  int last_cpu;
  int sampled_changes;
};

static void sample_cpu(struct worker_stats *stats)
{
  int cpu = getcpu();

  if (cpu < 0 || cpu >= NCPU)
    return;

  if (stats->first_cpu < 0)
    stats->first_cpu = cpu;

  if (stats->last_cpu >= 0 && stats->last_cpu != cpu)
    stats->sampled_changes++;

  stats->last_cpu = cpu;
  stats->cpu_mask |= (1U << cpu);
}

static void burn_cpu(uint iterations, struct worker_stats *stats)
{
  volatile uint x = (uint)getpid();

  stats->pid = getpid();
  stats->cpu_mask = 0;
  stats->first_cpu = -1;
  stats->last_cpu = -1;
  stats->sampled_changes = 0;

  for (uint i = 0; i < iterations; i++) {
    x = x * 1664525U + 1013904223U;
    x ^= x >> 13;

    if (i % CPU_SAMPLE_INTERVAL == 0)
      sample_cpu(stats);
  }

  sample_cpu(stats);

  if (x == 0x12345678U)
    stats->sampled_changes++;
}

int main(int argc, char *argv[])
{
  int workers = DEFAULT_WORKERS;
  int iterations = DEFAULT_ITERATIONS;
  int launched = 0;
  int pipes[MAX_WORKERS][2];
  int start_ticks;
  int end_ticks;

  if (argc > 1)
    workers = atoi(argv[1]);

  if (argc > 2)
    iterations = atoi(argv[2]);

  if (workers < 1 || workers > MAX_WORKERS || iterations < 1) {
    printf("usage: testws [workers: 1-%d] [iterations: positive]\n",
           MAX_WORKERS);
    exit(1);
  }

  printf("testws: workers=%d iterations=%d\n", workers, iterations);

  start_ticks = uptime();

  // Create workers.
  for (int i = 0; i < workers; i++) {
    int pid;

    if (pipe(pipes[i]) < 0) {
      printf("testws: pipe failed\n");
      break;
    }

    pid = fork();

    if (pid < 0) {
      close(pipes[i][0]);
      close(pipes[i][1]);
      printf("testws: fork failed\n");
      break;
    }

    if (pid == 0) {
      struct worker_stats stats;

      // Close inherited read ends from previous workers.
      for (int j = 0; j < i; j++)
        close(pipes[j][0]);

      close(pipes[i][0]);

      burn_cpu((uint)iterations, &stats);

      write(pipes[i][1], &stats, sizeof(stats));

      close(pipes[i][1]);
      exit(0);
    }

    // Parent keeps only the read end.
    close(pipes[i][1]);
    launched++;
  }

  // Collect results and print them in the parent only.
  printf("\nWorker CPU Statistics:\n");

  for (int i = 0; i < launched; i++) {
    struct worker_stats stats;
    int n;

    n = read(pipes[i][0], &stats, sizeof(stats));
    close(pipes[i][0]);

    if (n != sizeof(stats)) {
      printf("Worker %d: failed to read statistics\n", i);
      continue;
    }

    printf("PID=%d first_cpu=%d last_cpu=%d "
           "cpu_mask=0x%x changes=%d\n",
           stats.pid, stats.first_cpu, stats.last_cpu, stats.cpu_mask,
           stats.sampled_changes);
  }

  // Reap all children.
  for (int i = 0; i < launched; i++)
    wait(0);

  end_ticks = uptime();

  printf("\nWork Stealing Workload Summary:\n");
  printf("Workers launched: %d\n", launched);
  printf("Workers completed: %d\n", launched);
  printf("Elapsed time: %d ticks\n", end_ticks - start_ticks);

  printf("testws: finished\n");

  exit(0);
}
