/*
 * ws_lb_test — structured tests for Work Stealing / Load Balancing
 * using Schedstats API V2.
 *
 * Scenarios:
 *   1) free: unrestricted workers (observe WS/LB activity)
 *   2) pin:  all workers pinned to CPU 0 (expect low/no cross-CPU steal of pinned tasks)
 *   3) imbalance: create load on one CPU then free workers (encourage LB)
 *
 * Usage: ws_lb_test [free|pin|imbalance|all]
 */
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
#include "user/schedstats.h"

#define WORKERS 4
#define ITERS   3000000

static void
print_delta(const char *name, struct sched_stats_v2 *d, int elapsed)
{
  printf("[%s] elapsed=%d ticks\n", name, elapsed);
  printf("  WS successes=%d searches=%d attempts=%d failed=%d\n",
         d->ws_successes, d->ws_searches, d->ws_attempts,
         d->ws_failed_searches);
  printf("  LB migrations=%d checks=%d below=%d no_elig=%d\n", d->lb_migrations,
         d->lb_checks, d->lb_below_threshold, d->lb_no_eligible);
}

static void
delta_stats(struct sched_stats_v2 *a, struct sched_stats_v2 *b,
            struct sched_stats_v2 *d)
{
  memset(d, 0, sizeof(*d));
  d->ws_successes = b->ws_successes - a->ws_successes;
  d->ws_searches = b->ws_searches - a->ws_searches;
  d->ws_attempts = b->ws_attempts - a->ws_attempts;
  d->ws_failed_searches = b->ws_failed_searches - a->ws_failed_searches;
  d->lb_migrations = b->lb_migrations - a->lb_migrations;
  d->lb_checks = b->lb_checks - a->lb_checks;
  d->lb_below_threshold = b->lb_below_threshold - a->lb_below_threshold;
  d->lb_no_eligible = b->lb_no_eligible - a->lb_no_eligible;
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

static int
run_workers(int n, unsigned affinity_mask, int iters)
{
  int i, done = 0;

  for (i = 0; i < n; i++) {
    int pid = fork();
    if (pid < 0)
      return -1;
    if (pid == 0) {
      if (affinity_mask != 0)
        sched_setaffinity(getpid(), affinity_mask);
      cpu_burn(iters);
      exit(0);
    }
  }
  for (i = 0; i < n; i++) {
    int st;
    if (wait(&st) > 0 && st == 0)
      done++;
  }
  return done;
}

static int
scenario_free(void)
{
  struct sched_stats_v2 a, b, d;
  int t0, t1, done;

  printf("\n== scenario free (unrestricted workers) ==\n");
  sched_resetstats();
  sched_getstats(&a);
  t0 = uptime();
  done = run_workers(WORKERS, 0, ITERS);
  t1 = uptime();
  sched_getstats(&b);
  delta_stats(&a, &b, &d);
  print_delta("free", &d, t1 - t0);
  printf("  workers_ok=%d/%d\n", done, WORKERS);
  /* Soft expectation: system remains correct; WS/LB may or may not fire. */
  return done == WORKERS ? 0 : 1;
}

static int
scenario_pin(void)
{
  struct sched_stats_v2 a, b, d;
  int t0, t1, done;
  unsigned mask = 1U << 0; /* CPU 0 only */

  printf("\n== scenario pin (all on CPU 0) ==\n");
  sched_resetstats();
  sched_getstats(&a);
  t0 = uptime();
  done = run_workers(WORKERS, mask, ITERS);
  t1 = uptime();
  sched_getstats(&b);
  delta_stats(&a, &b, &d);
  print_delta("pin", &d, t1 - t0);
  printf("  workers_ok=%d/%d\n", done, WORKERS);
  /* Pinned tasks must not be stolen to other CPUs; successes may still
   * occur for non-pinned system tasks — do not hard-fail on WS count. */
  return done == WORKERS ? 0 : 1;
}

static int
scenario_imbalance(void)
{
  struct sched_stats_v2 a, b, d;
  int t0, t1, done;
  int pid;

  printf("\n== scenario imbalance (seed CPU0 then free workers) ==\n");
  sched_resetstats();

  /* Seed heavy load on CPU 0 */
  pid = fork();
  if (pid == 0) {
    sched_setaffinity(getpid(), 1U << 0);
    cpu_burn(ITERS * 2);
    exit(0);
  }

  /* Let seed run a bit */
  pause(2);

  sched_getstats(&a);
  t0 = uptime();
  done = run_workers(WORKERS, 0, ITERS);
  t1 = uptime();
  wait(0); /* seed */
  sched_getstats(&b);
  delta_stats(&a, &b, &d);
  print_delta("imbalance", &d, t1 - t0);
  printf("  workers_ok=%d/%d\n", done, WORKERS);
  if (d.lb_migrations > 0 || d.ws_successes > 0)
    printf("  NOTE: migration activity observed (good for LB/WS path)\n");
  else
    printf(
      "  NOTE: no migration counters; may need longer/heavier load or more CPUs\n");
  return done == WORKERS ? 0 : 1;
}

int
main(int argc, char *argv[])
{
  int fail = 0;
  const char *mode = argc > 1 ? argv[1] : "all";

  printf("ws_lb_test: mode=%s workers=%d iters=%d\n", mode, WORKERS, ITERS);

  if (strcmp(mode, "free") == 0 || strcmp(mode, "all") == 0)
    fail |= scenario_free();
  if (strcmp(mode, "pin") == 0 || strcmp(mode, "all") == 0)
    fail |= scenario_pin();
  if (strcmp(mode, "imbalance") == 0 || strcmp(mode, "all") == 0)
    fail |= scenario_imbalance();

  printf("\nws_lb_test: %s\n", fail ? "FAIL" : "PASS");
  exit(fail ? 1 : 0);
}
