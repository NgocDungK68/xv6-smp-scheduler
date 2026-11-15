#ifndef XV6_SCHEDSTATS_H
#define XV6_SCHEDSTATS_H

#include "types.h"
#include "param.h"

#define SCHED_STATS_VERSION 1

#define SCHED_FEAT_IPI 1U
#define SCHED_FEAT_WS  2U
#define SCHED_FEAT_LB  4U

struct sched_stats {
  uint version;
  uint size;
  uint features;
  uint online_mask;
  uint idle_mask;
  uint reserved;

  uint64 ipi_sent[NCPU];
  uint64 ipi_received[NCPU];
  uint64 wfi_waits[NCPU];

  uint queued[NCPU];
  uint running[NCPU];

  uint64 ws_searches;
  uint64 ws_attempts;
  uint64 ws_successes;
  uint64 lb_checks;
  uint64 lb_migrations;
};

#endif