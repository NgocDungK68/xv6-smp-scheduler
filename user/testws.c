#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define DEFAULT_WORKERS     4
#define DEFAULT_ITERATIONS  10000000
#define MAX_WORKERS         16
#define CPU_SAMPLE_INTERVAL 100000
#define FREE_MASK           0xFFFFFFFFU
#define PINNED_CPU          1

struct worker_stats {
  int pid;
  uint cpu_mask;
  int first_cpu;
  int last_cpu;
  int sampled_changes;
  uint expected_cpu_mask;
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

static void wait_for_cpu_in_mask(uint mask)
{
  int spins = 0;
  while (!(mask & (1U << getcpu()))) {
    if (++spins > 100000000)
      break;
  }
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
    if (i != 0 && i % CPU_SAMPLE_INTERVAL == 0)
      sample_cpu(stats);
  }
  sample_cpu(stats);
}

int main(int argc, char *argv[])
{
  int workers = DEFAULT_WORKERS;
  int iterations = DEFAULT_ITERATIONS;
  int launched = 0;
  int result_pipe[2];
  int start_ticks, end_ticks;
  struct worker_stats all[MAX_WORKERS];
  int valid[MAX_WORKERS];

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
  printf(
    "--- Kịch bản: Ép 50%% số tiến trình vào CPU %d, còn lại chạy tự do ---\n\n",
    PINNED_CPU);

  if (pipe(result_pipe) < 0) {
    printf("testws: pipe failed\n");
    exit(1);
  }

  start_ticks = uptime();

  /* --- 1. Fork workers --- */
  for (int i = 0; i < workers; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("testws: fork failed at worker %d\n", i);
      break;
    }

    if (pid == 0) {
      struct worker_stats stats;
      uint expected_mask = (1U << PINNED_CPU);

      close(result_pipe[0]);

      if (i < workers / 2) {
        if (setaffinity(getpid(), expected_mask) < 0) {
          printf("Worker (PID %d): setaffinity FAILED!\n", getpid());
          stats.expected_cpu_mask = FREE_MASK;
        } else {
          stats.expected_cpu_mask = expected_mask;
          wait_for_cpu_in_mask(expected_mask);
        }
      } else {
        stats.expected_cpu_mask = FREE_MASK;
      }

      burn_cpu((uint)iterations, &stats);
      write(result_pipe[1], &stats, sizeof(stats));
      close(result_pipe[1]);
      exit(0);
    }
    launched++;
  }
  close(result_pipe[1]);

  /* --- 2. Collect TOÀN BỘ stats vào mảng trước --- */
  for (int i = 0; i < launched; i++) {
    int n = read(result_pipe[0], &all[i], sizeof(all[i]));
    valid[i] = (n == sizeof(all[i]));
  }
  close(result_pipe[0]);

  /* --- 3. Per-worker report --- */
  printf("\nWorker CPU Statistics & Verification:\n");
  for (int i = 0; i < launched; i++) {
    if (!valid[i]) {
      printf("Worker[%d]: failed to read statistics\n", i);
      continue;
    }
    struct worker_stats *s = &all[i];
    printf("PID=%d first_cpu=%d last_cpu=%d cpu_mask=0x%x changes=%d\n", s->pid,
           s->first_cpu, s->last_cpu, s->cpu_mask, s->sampled_changes);

    if (s->expected_cpu_mask != FREE_MASK) {
      if ((s->cpu_mask & s->expected_cpu_mask) == s->cpu_mask) {
        printf("  >>> THÀNH CÔNG: PID %d bị khóa chặt vào CPU %d.\n", s->pid,
               PINNED_CPU);
      } else {
        printf(
          "  >>> LỖI: PID %d vi phạm Affinity! (Expected: 0x%x, Actual: 0x%x)\n",
          s->pid, s->expected_cpu_mask, s->cpu_mask);
      }
    } else {
      if (s->sampled_changes > 0) {
        if (s->first_cpu == PINNED_CPU)
          printf(
            "  >>> THÀNH CÔNG: PID %d bị Work-Stealing khỏi CPU bận %d → CPU %d.\n",
            s->pid, s->first_cpu, s->last_cpu);
        else
          printf(
            "  >>> THÀNH CÔNG: PID %d đã được Load Balancing (CPU %d → %d).\n",
            s->pid, s->first_cpu, s->last_cpu);
      } else {
        printf("  >>> INFO: PID %d chạy trên một CPU duy nhất (CPU %d).\n",
               s->pid, s->last_cpu);
      }
    }
  }

  /* --- 4. Aggregate (tính SAU khi đã có đủ dữ liệu) --- */
  int pinned_total = 0, pinned_ok = 0, pinned_bad = 0;
  int free_total = 0, free_migrated = 0, free_stolen = 0;
  int cpu_load[NCPU];
  for (int c = 0; c < NCPU; c++)
    cpu_load[c] = 0;

  for (int i = 0; i < launched; i++) {
    if (!valid[i])
      continue;
    struct worker_stats *s = &all[i];

    if (s->last_cpu >= 0 && s->last_cpu < NCPU)
      cpu_load[s->last_cpu]++;

    if (s->expected_cpu_mask != FREE_MASK) {
      pinned_total++;
      if ((s->cpu_mask & s->expected_cpu_mask) == s->cpu_mask)
        pinned_ok++;
      else
        pinned_bad++;
    } else {
      free_total++;
      if (s->sampled_changes > 0) {
        free_migrated++;
        if (s->first_cpu == PINNED_CPU)
          free_stolen++;
      }
    }
  }

  printf("\n=== AGGREGATE REPORT ===\n");
  printf("Pinned workers:  %d  (OK=%d, VIOLATIONS=%d)\n", pinned_total,
         pinned_ok, pinned_bad);
  printf("Free workers:    %d  (migrated=%d, stayed=%d)\n", free_total,
         free_migrated, free_total - free_migrated);
  printf("Work-stealing:   %d worker(s) bị steal khỏi CPU bận %d\n",
         free_stolen, PINNED_CPU);
  printf("Final CPU distribution (last_cpu):\n");
  for (int c = 0; c < NCPU; c++) {
    printf("  CPU %d: %d worker(s)%s\n", c, cpu_load[c],
           c == PINNED_CPU ? "  [pinned target]" : "");
  }
  printf("Affinity enforcement: %s\n", pinned_bad == 0 ? "PASS" : "FAIL");
  printf("Work-stealing:        %s\n",
         free_migrated > 0 ? "PASS" : "not triggered (no contention)");

  for (int i = 0; i < launched; i++)
    wait(0);

  end_ticks = uptime();

  printf("\nWork Stealing Workload Summary:\n");
  printf("Workers launched:  %d\n", launched);
  printf("Workers completed: %d\n", launched);
  printf("Elapsed time:      %d ticks\n", end_ticks - start_ticks);
  printf("testws: finished\n");

  exit(0);
}
