/*
 * schedmeasure — dump Schedstats API V2 and optional short workload.
 * Usage:
 *   schedmeasure              # reset + print snapshot
 *   schedmeasure dump         # print without reset
 *   schedmeasure burn [n]     # reset, burn n workers, dump delta
 */
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
#include "user/schedstats.h"

static void
print_stats(const char *tag, struct sched_stats_v2 *s)
{
  int i;

  printf("=== %s (abi=%d online=0x%x) ===\n", tag, s->abi, s->online_mask);
  printf(
    "WS: searches=%d attempts=%d successes=%d failed=%d aff_skip=%d stale=%d\n",
    s->ws_searches, s->ws_attempts, s->ws_successes, s->ws_failed_searches,
    s->ws_affinity_skips, s->ws_stale);
  printf(
    "LB: checks=%d migrations=%d below=%d no_elig=%d aff_skip=%d stale=%d rate_lim=%d\n",
    s->lb_checks, s->lb_migrations, s->lb_below_threshold, s->lb_no_eligible,
    s->lb_affinity_skips, s->lb_stale, s->lb_rate_limited);
  printf("RQ lens:");
  for (i = 0; i < SCHEDSTATS_NCPU; i++) {
    if (s->online_mask & (1U << i))
      printf(" c%d=%d", i, s->rq_len[i]);
  }
  printf("\n");
}

static void
delta(struct sched_stats_v2 *a, struct sched_stats_v2 *b,
      struct sched_stats_v2 *d)
{
  memset(d, 0, sizeof(*d));
  d->abi = b->abi;
  d->online_mask = b->online_mask;
  d->ws_searches = b->ws_searches - a->ws_searches;
  d->ws_attempts = b->ws_attempts - a->ws_attempts;
  d->ws_successes = b->ws_successes - a->ws_successes;
  d->ws_failed_searches = b->ws_failed_searches - a->ws_failed_searches;
  d->ws_affinity_skips = b->ws_affinity_skips - a->ws_affinity_skips;
  d->ws_stale = b->ws_stale - a->ws_stale;
  d->lb_checks = b->lb_checks - a->lb_checks;
  d->lb_migrations = b->lb_migrations - a->lb_migrations;
  d->lb_below_threshold = b->lb_below_threshold - a->lb_below_threshold;
  d->lb_no_eligible = b->lb_no_eligible - a->lb_no_eligible;
  d->lb_affinity_skips = b->lb_affinity_skips - a->lb_affinity_skips;
  d->lb_stale = b->lb_stale - a->lb_stale;
  d->lb_rate_limited = b->lb_rate_limited - a->lb_rate_limited;
  for (int i = 0; i < SCHEDSTATS_NCPU; i++)
    d->rq_len[i] = b->rq_len[i];
}

static void
burn_workers(int n)
{
  int i;
  volatile int x = 0;

  for (i = 0; i < n; i++) {
    int pid = fork();
    if (pid == 0) {
      for (int k = 0; k < 2000000; k++)
        x += k;
      exit(0);
    }
  }
  for (i = 0; i < n; i++)
    wait(0);
  (void)x;
}

int
main(int argc, char *argv[])
{
  struct sched_stats_v2 st, before, after, d;

  if (argc >= 2 && strcmp(argv[1], "dump") == 0) {
    if (sched_getstats(&st) < 0) {
      printf("schedmeasure: sched_getstats failed\n");
      exit(1);
    }
    print_stats("snapshot", &st);
    exit(0);
  }

  if (argc >= 2 && strcmp(argv[1], "burn") == 0) {
    int n = argc > 2 ? atoi(argv[2]) : 4;
    if (n < 1 || n > 16)
      n = 4;
    sched_resetstats();
    sched_getstats(&before);
    int t0 = uptime();
    burn_workers(n);
    int t1 = uptime();
    sched_getstats(&after);
    delta(&before, &after, &d);
    printf("burn workers=%d elapsed=%d ticks\n", n, t1 - t0);
    print_stats("delta", &d);
    exit(0);
  }

  /* default: reset and show empty-ish snapshot */
  if (sched_resetstats() < 0) {
    printf("schedmeasure: reset failed (syscall missing?)\n");
    exit(1);
  }
  if (sched_getstats(&st) < 0) {
    printf("schedmeasure: getstats failed\n");
    exit(1);
  }
  print_stats("after reset", &st);
  exit(0);
}
