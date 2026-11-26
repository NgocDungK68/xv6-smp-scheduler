// Test waking idle CPUs with supervisor software interrupts.
// Usage: ipiwakeup <cpus>

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/schedstats.h"
#include "user/user.h"

#define ROUNDS          20
#define BURST           8
#define IDLE_WAIT_TICKS 30

static int
snapshot(struct sched_stats *s)
{
  return sched_getstats(s, sizeof(*s));
}

// Wait until the target CPU enters the idle path.
static int
wait_idle(int cpu)
{
  for (int i = 0; i < IDLE_WAIT_TICKS; i++) {
    struct sched_stats s;

    if (snapshot(&s) < 0)
      return -1;

    if (s.idle_mask & (1U << cpu))
      return 0;

    pause(1);
  }

  return -1;
}

// Wake one blocked process on a designated CPU.
static int
wake_one(int cpu, int *saw_ipi)
{
  int gate[2];
  int ready[2];

  if (pipe(gate) < 0)
    return -1;

  if (pipe(ready) < 0) {
    close(gate[0]);
    close(gate[1]);
    return -1;
  }

  int pid = fork();

  if (pid < 0) {
    close(gate[0]);
    close(gate[1]);
    close(ready[0]);
    close(ready[1]);
    return -1;
  }

  if (pid == 0) {
    char c = 'R';

    close(gate[1]);
    close(ready[0]);

    if (write(ready[1], &c, 1) != 1)
      exit(1);

    close(ready[1]);

    if (read(gate[0], &c, 1) != 1)
      exit(1);

    close(gate[0]);

    exit(getcpu() == cpu ? 0 : 1);
  }

  close(gate[0]);
  close(ready[1]);

  char c;
  int ok = read(ready[0], &c, 1) == 1;
  close(ready[0]);

  // Allow the child to block in read() before changing affinity.
  if (ok && pause(2) < 0)
    ok = 0;

  if (ok && sched_setaffinity(pid, 1U << cpu) < 0)
    ok = 0;

  if (ok && wait_idle(cpu) < 0)
    ok = 0;

  struct sched_stats before, after;

  if (ok && snapshot(&before) < 0)
    ok = 0;

  // Always release the child, including when setup fails.
  c = 'G';

  if (write(gate[1], &c, 1) != 1)
    ok = 0;

  close(gate[1]);

  int status = -1;

  if (wait(&status) != pid || status != 0)
    ok = 0;

  if (ok && snapshot(&after) < 0)
    ok = 0;

  if (ok && after.ipi_sent[cpu] > before.ipi_sent[cpu] &&
      after.ipi_received[cpu] > before.ipi_received[cpu]) {
    *saw_ipi = 1;
  }

  return ok ? 0 : -1;
}

// Wake several blocked children on the same CPU.
static int
wake_burst(int cpu)
{
  int gate[2];
  int ready[2];
  int pids[BURST];
  int started = 0;
  int ok = 1;

  if (pipe(gate) < 0)
    return -1;

  if (pipe(ready) < 0) {
    close(gate[0]);
    close(gate[1]);
    return -1;
  }

  for (int i = 0; i < BURST; i++) {
    int pid = fork();

    if (pid < 0) {
      ok = 0;
      break;
    }

    if (pid == 0) {
      char c = 'R';

      close(gate[1]);
      close(ready[0]);

      if (write(ready[1], &c, 1) != 1)
        exit(1);

      close(ready[1]);

      if (read(gate[0], &c, 1) != 1)
        exit(1);

      close(gate[0]);

      exit(getcpu() == cpu ? 0 : 1);
    }

    pids[started++] = pid;
  }

  close(gate[0]);
  close(ready[1]);

  for (int i = 0; i < started; i++) {
    char c;

    if (read(ready[0], &c, 1) != 1)
      ok = 0;
  }

  close(ready[0]);

  if (pause(2) < 0)
    ok = 0;

  for (int i = 0; i < started; i++) {
    if (sched_setaffinity(pids[i], 1U << cpu) < 0)
      ok = 0;
  }

  if (ok && wait_idle(cpu) < 0)
    ok = 0;

  char tokens[BURST];

  for (int i = 0; i < BURST; i++)
    tokens[i] = 'G';

  if (write(gate[1], tokens, started) != started)
    ok = 0;

  close(gate[1]);

  for (int i = 0; i < started; i++) {
    int status = -1;

    if (wait(&status) < 0 || status != 0)
      ok = 0;
  }

  return ok && started == BURST ? 0 : -1;
}

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    printf("usage: ipiwakeup <cpus>\n");
    exit(1);
  }

  int ncpu = atoi(argv[1]);

  if (ncpu < 1 || ncpu > NCPU) {
    printf("ipiwakeup: invalid CPU count\n");
    exit(1);
  }

  struct sched_stats s;
  uint expected = (1U << ncpu) - 1U;

  // Wait until all requested CPUs are online.
  for (int i = 0; i < IDLE_WAIT_TICKS; i++) {
    if (snapshot(&s) < 0) {
      printf("ipiwakeup: FAIL (stats API)\n");
      exit(1);
    }

    if (s.online_mask == expected)
      break;

    pause(1);
  }

  if (s.version != SCHED_STATS_VERSION || s.size != sizeof(s) ||
      s.online_mask != expected) {
    printf("ipiwakeup: FAIL (stats or CPU configuration)\n");
    exit(1);
  }

  if ((s.features & SCHED_FEAT_IPI) == 0) {
    printf("ipiwakeup: SKIP (IPI disabled)\n");
    exit(0);
  }

  printf("=== WFI + IPI Wakeup Test ===\n");
  printf("[T1] Stats API and online CPUs: PASS\n");

  // Keep the parent on CPU 0.
  if (sched_setaffinity(0, 1U) < 0 || pause(2) < 0 || getcpu() != 0) {
    printf("ipiwakeup: FAIL (pin parent)\n");
    exit(1);
  }

  for (int cpu = 1; cpu < ncpu; cpu++) {
    int saw_ipi = 0;

    for (int round = 0; round < ROUNDS; round++) {
      if (wake_one(cpu, &saw_ipi) < 0) {
        printf("[T2] CPU %d round %d: FAIL\n", cpu, round + 1);
        exit(1);
      }
    }

    if (!saw_ipi) {
      printf("[T2] CPU %d: FAIL (no IPI observed)\n", cpu);
      exit(1);
    }

    if (snapshot(&s) < 0 || s.wfi_waits[cpu] == 0) {
      printf("[T2] CPU %d: FAIL (no WFI observed)\n", cpu);
      exit(1);
    }

    printf("[T2] CPU %d: %d/%d wakeups, IPI observed: PASS\n", cpu, ROUNDS,
           ROUNDS);

    if (wake_burst(cpu) < 0) {
      printf("[T3] CPU %d burst: FAIL\n", cpu);
      exit(1);
    }

    printf("[T3] CPU %d burst %d/%d: PASS\n", cpu, BURST, BURST);
  }

  if (ncpu == 1)
    printf("[T2-T3] Remote IPI: SKIP (one CPU)\n");

  printf("ipiwakeup: PASS\n");
  exit(0);
}