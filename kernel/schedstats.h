#ifndef XV6_SCHEDSTATS_H
#define XV6_SCHEDSTATS_H

#include "types.h"
#include "param.h"

#define SCHEDSTATS_ABI_V2 2

struct sched_stats_v2 {
  uint abi;

  uint ws_searches;
  uint ws_attempts;
  uint ws_successes;
  uint ws_failed_searches;
  uint ws_affinity_skips;
  uint ws_stale;

  uint lb_checks;
  uint lb_migrations;
  uint lb_below_threshold;
  uint lb_no_eligible;
  uint lb_affinity_skips;
  uint lb_stale;
  uint lb_rate_limited;

  uint rq_len[NCPU];
  uint online_mask;
};

void schedstats_init(void);
int schedstats_collect(struct sched_stats_v2 *out);
void schedstats_reset(void);

#endif
