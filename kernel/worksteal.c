#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "runqueue.h"
#include "defs.h"
#include "worksteal.h"

static struct spinlock ws_lock;
static int ws_initialized;
static uint next_victim[NCPU];
static struct worksteal_stats ws_stats[NCPU];

// Count runnable processes in src_cpu that are eligible to run on dst_cpu.
static int count_eligible(int src_cpu, int dst_cpu, uint *affinity_skips)
{
  int length;
  int count = 0;

  length = rq_length(src_cpu);

  if (length <= 0)
    return 0;

  for (int offset = 0; offset < length; offset++) {
    struct proc *p = rq_peek_at(src_cpu, offset);
    int affinity_rejected = 0;

    if (p == 0)
      continue;

    if (rq_proc_can_run_on(p, dst_cpu, &affinity_rejected)) {
      count++;
    } else if (affinity_rejected && affinity_skips) {
      (*affinity_skips)++;
    }
  }

  return count;
}

// Initialize Work Stealing module. Safe to call more than once.
void worksteal_init(void)
{
  if (ws_initialized)
    return;

  initlock(&ws_lock, "worksteal");

  for (int cpu = 0; cpu < NCPU; cpu++) {
    next_victim[cpu] = (uint)(cpu + 1) % NCPU;

    ws_stats[cpu].searches = 0;
    ws_stats[cpu].attempts = 0;
    ws_stats[cpu].successes = 0;
    ws_stats[cpu].failed_searches = 0;
    ws_stats[cpu].affinity_skips = 0;
    ws_stats[cpu].stale_candidates = 0;
  }

  ws_initialized = 1;
}

// Try to steal one runnable process for an idle CPU.
//
// Return values:
//   1  : Successfully stole one process.
//   0  : No process was stolen.
//  -1  : Invalid input or module not initialized.
int worksteal_try(int thief_cpu, uint online_mask)
{
  int order[NCPU];
  int eligible[NCPU];

  int norder = 0;
  int start;
  int moved = 0;
  int done = 0; // set when the whole search must stop (dst got work)

  uint skipped = 0;

  if (!ws_initialized || thief_cpu < 0 || thief_cpu >= NCPU)
    return -1;

  if ((online_mask & ~rq_valid_cpu_mask()) != 0 ||
      !rq_cpu_online(thief_cpu, online_mask))
    return -1;

  // Work Stealing is only useful when the local queue is empty.
  if (rq_length(thief_cpu) != 0)
    return 0;

  // Read the next victim starting position.
  acquire(&ws_lock);
  ws_stats[thief_cpu].searches++;
  start = (int)next_victim[thief_cpu];
  release(&ws_lock);

  // --------------------------------------------------
  // STEP 1: Find and rank eligible victim CPUs.
  // --------------------------------------------------
  //
  // A victim is worth visiting only if it has work to spare
  // (load = queued + running > 1). Victims are ranked by the number of
  // queued processes that may legally run on the thief; ties keep the
  // round-robin order.

  for (int step = 0; step < NCPU; step++) {
    int victim = (start + step) % NCPU;

    int score;
    int insert_at;

    if (victim == thief_cpu)
      continue;

    if (!rq_cpu_online(victim, online_mask))
      continue;

    if (rq_load(victim) <= 1)
      continue;

    score = count_eligible(victim, thief_cpu, &skipped);

    if (score == 0)
      continue;

    // Insert victim into descending score order.
    insert_at = norder;

    while (insert_at > 0 && eligible[insert_at - 1] < score) {
      order[insert_at] = order[insert_at - 1];
      eligible[insert_at] = eligible[insert_at - 1];
      insert_at--;
    }

    order[insert_at] = victim;
    eligible[insert_at] = score;

    norder++;
  }

  // Update affinity statistics and rotate victim selection.
  acquire(&ws_lock);
  ws_stats[thief_cpu].affinity_skips += skipped;
  next_victim[thief_cpu] = (uint)(start + 1) % NCPU;
  release(&ws_lock);

  // --------------------------------------------------
  // STEP 2: Try stealing from ranked victim CPUs.
  // --------------------------------------------------

  for (int i = 0; i < norder && !moved && !done; i++) {
    int victim = order[i];
    int length = rq_length(victim);

    for (int offset = 0; offset < length; offset++) {
      struct proc *p = rq_peek_at(victim, offset);

      int attempted = 0;
      int result = RQ_STEAL_INELIGIBLE;

      if (p == 0)
        continue;

      // State and affinity are checked under p->lock, and the migration is
      // done under the same lock hold: rq_steal_runnable() needs p->lock.
      acquire(&p->lock);
      if (p->state == RUNNABLE && (p->affinity_mask & (1U << thief_cpu)) != 0) {
        attempted = 1;
        result = rq_steal_runnable(victim, thief_cpu, p);
      }
      release(&p->lock);

      // Not a candidate any more (cheap filter, not a migration attempt).
      if (!attempted)
        continue;

      acquire(&ws_lock);
      ws_stats[thief_cpu].attempts++;
      if (result == RQ_STEAL_OK) {
        ws_stats[thief_cpu].successes++;
        // Next search starts from the CPU after the victim that gave work.
        next_victim[thief_cpu] = (uint)(victim + 1) % NCPU;
      } else if (result == RQ_STEAL_INELIGIBLE || result == RQ_STEAL_STALE) {
        ws_stats[thief_cpu].stale_candidates++;
      }
      release(&ws_lock);

      if (result == RQ_STEAL_OK) {
        // Only steal one process per invocation.
        moved = 1;
        break;
      }
      if (result == RQ_STEAL_DST_BUSY || result == RQ_STEAL_INVALID) {
        // Our own queue got work (or a bug): stop the whole search.
        done = 1;
        break;
      }
      if (result == RQ_STEAL_SRC_LOW)
        break; // nothing to spare on this victim: go to the next one

      // INELIGIBLE / STALE: try the next candidate on this victim.
    }
  }

  // --------------------------------------------------
  // STEP 3: Update failed search statistics.
  // --------------------------------------------------

  if (!moved) {
    acquire(&ws_lock);
    ws_stats[thief_cpu].failed_searches++;
    release(&ws_lock);
  }

  return moved;
}

// Get Work Stealing statistics for one CPU.
int worksteal_get_stats(int thief_cpu, struct worksteal_stats *out)
{
  if (!ws_initialized || out == 0 || thief_cpu < 0 || thief_cpu >= NCPU)
    return -1;

  acquire(&ws_lock);
  *out = ws_stats[thief_cpu];
  release(&ws_lock);

  return 0;
}

// Reset statistics for all CPUs.
void worksteal_reset_stats(void)
{
  if (!ws_initialized)
    return;

  acquire(&ws_lock);

  for (int cpu = 0; cpu < NCPU; cpu++) {
    ws_stats[cpu].searches = 0;
    ws_stats[cpu].attempts = 0;
    ws_stats[cpu].successes = 0;
    ws_stats[cpu].failed_searches = 0;
    ws_stats[cpu].affinity_skips = 0;
    ws_stats[cpu].stale_candidates = 0;
  }

  release(&ws_lock);
}
