// testws: CPU-bound workload with per-worker CPU observations.
// Usage: testws [workers] [iterations]
// CPU changes are samples, not proof of Work Stealing migration.

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
  int invalid_samples;
};

static void
sample_cpu(struct worker_stats *stats)
{
  int cpu = getcpu();

  if (cpu < 0 || cpu >= NCPU) {
    stats->invalid_samples++;
    return;
  }

  if (stats->first_cpu < 0)
    stats->first_cpu = cpu;

  if (stats->last_cpu >= 0 && stats->last_cpu != cpu)
    stats->sampled_changes++;

  stats->last_cpu = cpu;
  stats->cpu_mask |= 1U << cpu;
}

static void
burn_cpu(uint iterations, struct worker_stats *stats)
{
  volatile uint x = (uint)getpid();

  stats->pid = getpid();
  stats->cpu_mask = 0;
  stats->first_cpu = -1;
  stats->last_cpu = -1;
  stats->sampled_changes = 0;
  stats->invalid_samples = 0;

  for (uint i = 0; i < iterations; i++) {
    x = x * 1664525U + 1013904223U;
    x ^= x >> 13;

    if (i % CPU_SAMPLE_INTERVAL == 0)
      sample_cpu(stats);
  }

  sample_cpu(stats);
  (void)x;
}

// A pipe read may return fewer bytes than requested.
static int
read_full(int fd, void *buffer, int size)
{
  int total = 0;
  char *p = (char *)buffer;

  while (total < size) {
    int n = read(fd, p + total, size - total);
    if (n <= 0)
      return -1;
    total += n;
  }

  return total;
}

int
main(int argc, char *argv[])
{
  if (argc > 3) {
    printf("usage: testws [workers: 1-%d] [iterations: positive]\n",
           MAX_WORKERS);
    exit(1);
  }

  int workers = argc > 1 ? atoi(argv[1]) : DEFAULT_WORKERS;
  int iterations = argc > 2 ? atoi(argv[2]) : DEFAULT_ITERATIONS;

  if (workers < 1 || workers > MAX_WORKERS || iterations < 1) {
    printf("usage: testws [workers: 1-%d] [iterations: positive]\n",
           MAX_WORKERS);
    exit(1);
  }

  int launched = 0;
  int valid_reports = 0;
  int completed = 0;
  int reaped = 0;
  int pipes[MAX_WORKERS][2];
  int child_pids[MAX_WORKERS];
  int start_ticks = uptime();

  printf("testws: workers=%d iterations=%d\n", workers, iterations);

  for (int i = 0; i < workers; i++) {
    if (pipe(pipes[i]) < 0) {
      printf("testws: pipe failed at worker %d\n", i);
      break;
    }

    int pid = fork();
    if (pid < 0) {
      close(pipes[i][0]);
      close(pipes[i][1]);
      printf("testws: fork failed at worker %d\n", i);
      break;
    }

    if (pid == 0) {
      struct worker_stats stats;

      // Close inherited read ends from earlier workers.
      for (int j = 0; j < i; j++)
        close(pipes[j][0]);

      close(pipes[i][0]);
      burn_cpu((uint)iterations, &stats);

      if (write(pipes[i][1], &stats, sizeof(stats)) != sizeof(stats))
        exit(1);

      close(pipes[i][1]);
      exit(0);
    }

    child_pids[i] = pid;
    close(pipes[i][1]);
    launched++;
  }

  printf("\nWorker CPU Statistics:\n");

  for (int i = 0; i < launched; i++) {
    struct worker_stats stats;
    int n = read_full(pipes[i][0], &stats, sizeof(stats));
    close(pipes[i][0]);

    if (n != sizeof(stats)) {
      printf("Worker %d: failed to read statistics\n", i);
      continue;
    }

    if (stats.pid != child_pids[i] || stats.invalid_samples != 0 ||
        stats.first_cpu < 0 || stats.first_cpu >= NCPU || stats.last_cpu < 0 ||
        stats.last_cpu >= NCPU || stats.cpu_mask == 0 ||
        (stats.cpu_mask & ~((1U << NCPU) - 1U)) != 0 ||
        stats.sampled_changes < 0) {
      printf("Worker %d: invalid statistics\n", i);
      continue;
    }

    valid_reports++;
    printf("PID=%d first_cpu=%d last_cpu=%d "
           "cpu_mask=0x%x changes=%d\n",
           stats.pid, stats.first_cpu, stats.last_cpu, stats.cpu_mask,
           stats.sampled_changes);
  }

  int status;
  while (wait(&status) > 0) {
    reaped++;
    if (status == 0)
      completed++;
  }

  int elapsed = uptime() - start_ticks;
  int passed = launched == workers && valid_reports == workers &&
               reaped == workers && completed == workers;

  printf("\nWork Stealing Workload Summary:\n");
  printf("Workers launched: %d\n", launched);
  printf("Valid reports: %d\n", valid_reports);
  printf("Workers completed: %d\n", completed);
  printf("Elapsed time: %d ticks\n", elapsed);
  printf("testws: %s\n", passed ? "PASS" : "FAIL");

  exit(passed ? 0 : 1);
}
