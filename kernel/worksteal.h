#ifndef WORKSTEAL_H
#define WORKSTEAL_H

#include "types.h"

struct worksteal_stats {
  uint searches;
  uint attempts;
  uint successes;
  uint failed_searches;
  uint affinity_skips;
  uint stale_candidates;
};

void worksteal_init(void);

int worksteal_try(int thief_cpu, uint online_mask);

int worksteal_get_stats(int thief_cpu, struct worksteal_stats *out);

void worksteal_reset_stats(void);

#endif
