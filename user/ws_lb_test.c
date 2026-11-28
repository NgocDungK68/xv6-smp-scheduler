/*
 * ws_lb_test — structured tests for Work Stealing / Load Balancing
 * using the official Scheduler Statistics API V2 (docs/schedstats-api.md).
 *
 * Counters are cumulative since boot and there is no reset call, so every
 * scenario takes a snapshot before and after the workload and reports the
 * difference.
 *
 * Scenarios:
 *   1) free:      unrestricted workers (observe WS/LB activity, soft check)
 *   2) pin:       all workers pinned to CPU 0 (pinned tasks must not move)
 *   3) imbalance: CPU 0 loaded by a pinned seed, then free workers; the idle
 *                 CPUs must pick work up. PASS needs ws_successes or
 *                 lb_migrations to increase (proved by counters).
 *
 * Usage: ws_lb_test [free|pin|imbalance|all]
 */
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/schedstats.h"
#include "user/user.h"

#define WORKERS 4
#define ITERS   3000000

struct ws_lb_delta {
  int ws_searches;
  int ws_attempts;
  int ws_successes;
  int lb_checks;
  int lb_migrations;
  int dispatches[NCPU];
};

/* Read one snapshot and validate version/size as required by the API doc. */
static int
read_stats(struct sched_stats *s)
{
  if (sched_getstats(s, sizeof(*s)) < 0 || s->version != SCHED_STATS_VERSION ||
      s->size != sizeof(*s)) {
    printf("Failed to get scheduler stats\n");
    return -1;
  }
  return 0;
}

static void
delta_stats(const struct sched_stats *a, const struct sched_stats *b,
            struct ws_lb_delta *d)
{
  memset(d, 0, sizeof(*d));
  d->ws_searches = (int)(b->ws_searches - a->ws_searches);
  d->ws_attempts = (int)(b->ws_attempts - a->ws_attempts);
  d->ws_successes = (int)(b->ws_successes - a->ws_successes);
  d->lb_checks = (int)(b->lb_checks - a->lb_checks);
  d->lb_migrations = (int)(b->lb_migrations - a->lb_migrations);
  for (int i = 0; i < NCPU; i++)
    d->dispatches[i] = (int)(b->dispatches[i] - a->dispatches[i]);
}

static void
print_delta(const char *name, const struct ws_lb_delta *d,
            const struct sched_stats *after, int elapsed)
{
  printf("[%s] elapsed=%d ticks\n", name, elapsed);
  printf("  WS successes=%d searches=%d attempts=%d\n", d->ws_successes,
         d->ws_searches, d->ws_attempts);
  printf("  LB migrations=%d checks=%d\n", d->lb_migrations, d->lb_checks);
  printf("  dispatches:");
  for (int i = 0; i < NCPU; i++)
    if (after->online_mask & (1U << i))
      printf(" cpu%d=%d", i, d->dispatches[i]);
  printf("\n");
}

static void
cpu_burn(int iters)
{
  volatile unsigned x = (unsigned)getpid();
  for (int i = 0; i < iters; i++) {
    x = x * 1664525U + 1013904223U;
    x ^= x >> 13;
  }
  (void)x;
}

/*
 * Fork n workers, wait for exactly those workers and count the ones that
 * exited with status 0. Other children of this process (e.g. the seed in
 * the imbalance scenario) are reaped but not counted.
 */
static int
run_workers(int n, unsigned affinity_mask, int iters)
{
  int pids[WORKERS];
  int launched = 0, remaining, done = 0;

  if (n > WORKERS)
    n = WORKERS;

  for (int i = 0; i < n; i++) {
    int pid = fork();
    if (pid < 0)
      break;
    if (pid == 0) {
      if (affinity_mask != 0 && sched_setaffinity(getpid(), affinity_mask) < 0)
        exit(2);
      cpu_burn(iters);
      exit(0);
    }
    pids[launched++] = pid;
  }

  remaining = launched;
  while (remaining > 0) {
    int st, p = wait(&st);
    if (p < 0)
      break;
    for (int j = 0; j < launched; j++) {
      if (pids[j] == p) {
        remaining--;
        if (st == 0)
          done++;
        break;
      }
    }
  }
  return done;
}

static int
scenario_free(void)
{
  struct sched_stats a, b;
  struct ws_lb_delta d;
  int t0, t1, done;

  printf("\n== scenario free (unrestricted workers) ==\n");
  if (read_stats(&a) < 0)
    return 1;
  t0 = uptime();
  done = run_workers(WORKERS, 0, ITERS);
  t1 = uptime();
  if (read_stats(&b) < 0)
    return 1;
  delta_stats(&a, &b, &d);
  print_delta("free", &d, &b, t1 - t0);
  printf("  workers_ok=%d/%d\n", done, WORKERS);
  /* Soft expectation: system stays correct; WS/LB may or may not fire. */
  return done == WORKERS ? 0 : 1;
}

static int
scenario_pin(void)
{
  struct sched_stats a, b;
  struct ws_lb_delta d;
  int t0, t1, done;
  unsigned mask = 1U << 0; /* CPU 0 only */

  printf("\n== scenario pin (all on CPU 0) ==\n");
  if (read_stats(&a) < 0)
    return 1;
  t0 = uptime();
  done = run_workers(WORKERS, mask, ITERS);
  t1 = uptime();
  if (read_stats(&b) < 0)
    return 1;
  delta_stats(&a, &b, &d);
  print_delta("pin", &d, &b, t1 - t0);
  printf("  workers_ok=%d/%d\n", done, WORKERS);
  /* Pinned workers cannot be moved. Counters may still change because of
   * unrelated system activity, so this is informational, not a hard fail. */
  if (d.ws_successes > 0 || d.lb_migrations > 0)
    printf("  NOTE: ws_successes=%d lb_migrations=%d during pinned run;"
           " expected ~0 (check no other runnable tasks)\n",
           d.ws_successes, d.lb_migrations);
  return done == WORKERS ? 0 : 1;
}

static int
scenario_imbalance(void)
{
  struct sched_stats a, b;
  struct ws_lb_delta d;
  int t0, t1, done;
  int seed;
  int fail = 0;

  printf("\n== scenario imbalance (seed CPU0 then free workers) ==\n");

  /* Seed heavy load on CPU 0 */
  seed = fork();
  if (seed < 0) {
    printf("  fork failed\n");
    return 1;
  }
  if (seed == 0) {
    if (sched_setaffinity(getpid(), 1U << 0) < 0)
      exit(2);
    cpu_burn(ITERS * 2);
    exit(0);
  }

  /* Let seed run a bit */
  pause(2);

  if (read_stats(&a) < 0) {
    wait(0);
    return 1;
  }
  t0 = uptime();
  done = run_workers(WORKERS, 0, ITERS);
  t1 = uptime();
  wait(0); /* seed (may already be reaped inside run_workers: harmless) */
  if (read_stats(&b) < 0)
    return 1;
  delta_stats(&a, &b, &d);
  print_delta("imbalance", &d, &b, t1 - t0);
  printf("  workers_ok=%d/%d\n", done, WORKERS);

  if (done != WORKERS)
    fail = 1;

  if (!(b.features & (SCHED_FEAT_WS | SCHED_FEAT_LB))) {
    printf("  FAIL: both WS and LB are disabled in features=0x%x\n",
           b.features);
    return 1;
  }

  /* Proof comes from counters, not from observing CPU changes. */
  if (d.ws_successes > 0 || d.lb_migrations > 0) {
    printf("  OK: migration proved by counters (ws_successes=+%d,"
           " lb_migrations=+%d)\n",
           d.ws_successes, d.lb_migrations);
    if (d.lb_migrations == 0)
      printf("  NOTE: only WS fired; LB migration is not demonstrated here"
             " (use: schedmeasure testbalance 8 30000000 1 4)\n");
  } else {
    printf("  FAIL: CPU 0 was overloaded while other CPUs were free, but"
           " ws_successes and lb_migrations did not increase\n");
    fail = 1;
  }
  return fail;
}

int
main(int argc, char *argv[])
{
  int fail = 0;
  const char *mode = argc > 1 ? argv[1] : "all";
  struct sched_stats probe;

  printf("ws_lb_test: mode=%s workers=%d iters=%d\n", mode, WORKERS, ITERS);

  if (read_stats(&probe) < 0)
    exit(1);
  printf("ws_lb_test: stats v%d features=0x%x online_mask=0x%x\n",
         probe.version, probe.features, probe.online_mask);

  if (strcmp(mode, "free") == 0 || strcmp(mode, "all") == 0)
    fail |= scenario_free();
  if (strcmp(mode, "pin") == 0 || strcmp(mode, "all") == 0)
    fail |= scenario_pin();
  if (strcmp(mode, "imbalance") == 0 || strcmp(mode, "all") == 0)
    fail |= scenario_imbalance();

  printf("\nws_lb_test: %s\n", fail ? "FAIL" : "PASS");
  exit(fail ? 1 : 0);
}
