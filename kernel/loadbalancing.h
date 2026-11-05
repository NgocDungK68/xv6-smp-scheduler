#ifndef XV6_LOADBALANCING_H
#define XV6_LOADBALANCING_H

#include "types.h"

#define LB_INTERVAL_TICKS       10U
#define LB_IMBALANCE_THRESHOLD   2
#define LB_MAX_MOVES_PER_RUN     1

struct loadbalance_stats {
  uint checks;
  uint migrations;
  uint below_threshold;
  uint no_eligible_pair;
  uint affinity_skips;
  uint stale_candidates;
  uint rate_limited;
};

void loadbalance_init(void);

int loadbalance_run(int coordinator_cpu, uint online_mask, uint now_ticks);

int loadbalance_get_stats(struct loadbalance_stats *out);
void loadbalance_reset_stats(void);

#endif
