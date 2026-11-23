#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "worksteal.h"
#include "loadbalancing.h"
#include "runqueue.h"
#include "schedstats.h"

void
schedstats_init(void)
{
  /* WS and LB already init from main; this is a no-op placeholder. */
}

int
schedstats_collect(struct sched_stats_v2 *out)
{
  struct worksteal_stats ws;
  struct loadbalance_stats lb;
  int cpu;

  if (out == 0)
    return -1;

  memset(out, 0, sizeof(*out));
  out->abi = SCHEDSTATS_ABI_V2;

  for (cpu = 0; cpu < NCPU; cpu++) {
    if (worksteal_get_stats(cpu, &ws) == 0) {
      out->ws_searches += ws.searches;
      out->ws_attempts += ws.attempts;
      out->ws_successes += ws.successes;
      out->ws_failed_searches += ws.failed_searches;
      out->ws_affinity_skips += ws.affinity_skips;
      out->ws_stale += ws.stale_candidates;
    }
    out->rq_len[cpu] = (uint)rq_length(cpu);
  }

  if (loadbalance_get_stats(&lb) == 0) {
    out->lb_checks = lb.checks;
    out->lb_migrations = lb.migrations;
    out->lb_below_threshold = lb.below_threshold;
    out->lb_no_eligible = lb.no_eligible_pair;
    out->lb_affinity_skips = lb.affinity_skips;
    out->lb_stale = lb.stale_candidates;
    out->lb_rate_limited = lb.rate_limited;
  }

  out->online_mask = sched_online_mask();
  return 0;
}

void
schedstats_reset(void)
{
  worksteal_reset_stats();
  loadbalance_reset_stats();
}
