#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "runqueue.h"
#include "defs.h"
#include "loadbalancing.h"

static struct spinlock lb_lock;
static int lb_initialized;
static int lb_has_run;
static uint lb_last_tick;
static uint lb_pair_cursor;
static struct loadbalance_stats lb_stats;

static uint
valid_cpu_mask(void)
{
  return (1U << NCPU) - 1U;
}

static int
cpu_is_online(int cpu_id, uint online_mask)
{
  return cpu_id >= 0 && cpu_id < NCPU &&
         (online_mask & (1U << cpu_id)) != 0;
}

static int
proc_can_run_on(struct proc *p, int dst_cpu, int *affinity_rejected)
{
  int allowed = 0;

  if (affinity_rejected)
    *affinity_rejected = 0;
  if (p == 0 || dst_cpu < 0 || dst_cpu >= NCPU)
    return 0;

  acquire(&p->lock);
  if (p->state == RUNNABLE) {
    if ((p->affinity_mask & (1U << dst_cpu)) != 0)
      allowed = 1;
    else if (affinity_rejected)
      *affinity_rejected = 1;
  }
  release(&p->lock);
  return allowed;
}

// Return the first FIFO-ordered process in src_cpu that is runnable and
// allowed to execute on dst_cpu. rq_transfer() will revalidate before moving.
static struct proc *
find_eligible(int src_cpu, int dst_cpu, uint *affinity_skips)
{
  int length = rq_length(src_cpu);

  if (length <= 0)
    return 0;

  for (int offset = 0; offset < length; offset++) {
    struct proc *p = rq_peek_at(src_cpu, offset);
    int affinity_rejected = 0;

    if (p == 0)
      continue;
    if (proc_can_run_on(p, dst_cpu, &affinity_rejected))
      return p;
    if (affinity_rejected && affinity_skips)
      (*affinity_skips)++;
  }
  return 0;
}

void
loadbalance_init(void)
{
  initlock(&lb_lock, "loadbalance");
  lb_has_run = 0;
  lb_last_tick = 0;
  lb_pair_cursor = 0;
  lb_stats.checks = 0;
  lb_stats.migrations = 0;
  lb_stats.below_threshold = 0;
  lb_stats.no_eligible_pair = 0;
  lb_stats.affinity_skips = 0;
  lb_stats.stale_candidates = 0;
  lb_stats.rate_limited = 0;
  lb_initialized = 1;
}

int
loadbalance_run(int coordinator_cpu, uint online_mask, uint now_ticks)
{
  int lengths[NCPU];
  int best_src = -1;
  int best_dst = -1;
  int best_diff = LB_IMBALANCE_THRESHOLD - 1;
  struct proc *best_proc = 0;
  uint affinity_skips = 0;
  uint start;
  int saw_imbalance = 0;

  if (!lb_initialized || coordinator_cpu != 0)
    return -1;
  if ((online_mask & ~valid_cpu_mask()) != 0 ||
      !cpu_is_online(coordinator_cpu, online_mask))
    return -1;

  // Serialize the periodic coordinator and enforce a single global interval.
  acquire(&lb_lock);
  if (lb_has_run && (uint)(now_ticks - lb_last_tick) < LB_INTERVAL_TICKS) {
    lb_stats.rate_limited++;
    release(&lb_lock);
    return 0;
  }
  lb_has_run = 1;
  lb_last_tick = now_ticks;
  lb_stats.checks++;
  start = lb_pair_cursor;
  release(&lb_lock);

  for (int cpu = 0; cpu < NCPU; cpu++) {
    if (cpu_is_online(cpu, online_mask))
      lengths[cpu] = rq_length(cpu);
    else
      lengths[cpu] = -1;
  }

  // Examine source/destination pairs. A move is considered only when the
  // queue-length difference is at least the threshold and the source has a
  // RUNNABLE process whose affinity permits the destination CPU.
  // Circular scan order breaks equal-difference ties fairly.
  for (int dst_step = 0; dst_step < NCPU; dst_step++) {
    int dst = ((int)start + dst_step) % NCPU;

    if (!cpu_is_online(dst, online_mask))
      continue;

    for (int src_step = 0; src_step < NCPU; src_step++) {
      int src = ((int)start + src_step) % NCPU;
      int diff;
      struct proc *candidate;

      if (src == dst || !cpu_is_online(src, online_mask))
        continue;

      diff = lengths[src] - lengths[dst];
      if (diff < LB_IMBALANCE_THRESHOLD)
        continue;
      saw_imbalance = 1;
      if (diff < best_diff)
        continue;

      candidate = find_eligible(src, dst, &affinity_skips);
      if (candidate == 0)
        continue;

      if (diff > best_diff || best_proc == 0) {
        best_diff = diff;
        best_src = src;
        best_dst = dst;
        best_proc = candidate;
      }
    }
  }

  acquire(&lb_lock);
  lb_stats.affinity_skips += affinity_skips;
  release(&lb_lock);

  if (best_proc == 0) {
    acquire(&lb_lock);
    if (saw_imbalance)
      lb_stats.no_eligible_pair++;
    else
      lb_stats.below_threshold++;
    lb_pair_cursor = (start + 1U) % NCPU;
    release(&lb_lock);
    return 0;
  }

  // Move at most one process per interval. rq_transfer() checks RUNNABLE,
  // affinity, queue membership and capacity under the required locks.
  if (rq_transfer(best_src, best_dst, best_proc) == 0) {
    acquire(&lb_lock);
    lb_stats.migrations += LB_MAX_MOVES_PER_RUN;
    lb_pair_cursor = (uint)(best_dst + 1) % NCPU;
    release(&lb_lock);
    return LB_MAX_MOVES_PER_RUN;
  }

  acquire(&lb_lock);
  lb_stats.stale_candidates++;
  lb_pair_cursor = (start + 1U) % NCPU;
  release(&lb_lock);
  return 0;
}

int
loadbalance_get_stats(struct loadbalance_stats *out)
{
  if (!lb_initialized || out == 0)
    return -1;

  acquire(&lb_lock);
  *out = lb_stats;
  release(&lb_lock);
  return 0;
}

void
loadbalance_reset_stats(void)
{
  if (!lb_initialized)
    return;

  acquire(&lb_lock);
  lb_stats.checks = 0;
  lb_stats.migrations = 0;
  lb_stats.below_threshold = 0;
  lb_stats.no_eligible_pair = 0;
  lb_stats.affinity_skips = 0;
  lb_stats.stale_candidates = 0;
  lb_stats.rate_limited = 0;
  lb_has_run = 0;
  lb_last_tick = 0;
  lb_pair_cursor = 0;
  release(&lb_lock);
}
