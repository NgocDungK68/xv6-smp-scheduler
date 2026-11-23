/* User-visible ABI for Schedstats API V2.
 * Keep layout identical to kernel/schedstats.h (NCPU must match build). */
#ifndef USER_SCHEDSTATS_H
#define USER_SCHEDSTATS_H

#define SCHEDSTATS_ABI_V2 2
#define SCHEDSTATS_NCPU   8

struct sched_stats_v2 {
  unsigned int abi;

  unsigned int ws_searches;
  unsigned int ws_attempts;
  unsigned int ws_successes;
  unsigned int ws_failed_searches;
  unsigned int ws_affinity_skips;
  unsigned int ws_stale;

  unsigned int lb_checks;
  unsigned int lb_migrations;
  unsigned int lb_below_threshold;
  unsigned int lb_no_eligible;
  unsigned int lb_affinity_skips;
  unsigned int lb_stale;
  unsigned int lb_rate_limited;

  unsigned int rq_len[SCHEDSTATS_NCPU];
  unsigned int online_mask;
};

#endif
