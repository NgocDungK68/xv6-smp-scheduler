#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "runqueue.h"
#include "defs.h"

// One circular FIFO run queue per CPU.
// Lock order for migration: p->lock, then rq locks by ascending CPU ID.
struct runqueue {
  struct spinlock lock;
  struct proc *queue[NPROC];
  int head;
  int tail;
  int count;
};

static struct runqueue runqueues[NCPU];

static int
valid_cpu(int cpu_id)
{
  return cpu_id >= 0 && cpu_id < NCPU;
}

// Caller holds rq->lock. Return offset from head, or -1 if absent.
static int
rq_find_locked(struct runqueue *rq, struct proc *p)
{
  for (int i = 0; i < rq->count; i++) {
    int index = (rq->head + i) % NPROC;
    if (rq->queue[index] == p)
      return i;
  }
  return -1;
}

// Caller holds rq->lock and has checked capacity.
static void
rq_push_locked(struct runqueue *rq, struct proc *p)
{
  rq->queue[rq->tail] = p;
  rq->tail = (rq->tail + 1) % NPROC;
  rq->count++;
}

// Caller holds rq->lock and passes a valid offset.
static void
rq_remove_at_locked(struct runqueue *rq, int offset)
{
  for (int i = offset; i < rq->count - 1; i++) {
    int dst = (rq->head + i) % NPROC;
    int src = (dst + 1) % NPROC;
    rq->queue[dst] = rq->queue[src];
  }

  rq->tail = (rq->head + rq->count - 1) % NPROC;
  rq->queue[rq->tail] = 0;
  rq->count--;
}

// Caller guarantees two distinct, valid CPU IDs.
static void
rq_lock_pair(int a, int b)
{
  if (a < b) {
    acquire(&runqueues[a].lock);
    acquire(&runqueues[b].lock);
  } else {
    acquire(&runqueues[b].lock);
    acquire(&runqueues[a].lock);
  }
}

static void
rq_unlock_pair(int a, int b)
{
  if (a < b) {
    release(&runqueues[b].lock);
    release(&runqueues[a].lock);
  } else {
    release(&runqueues[a].lock);
    release(&runqueues[b].lock);
  }
}

// Called exactly once, before any scheduler starts running.
void
rq_init(void)
{
  for (int i = 0; i < NCPU; i++) {
    struct runqueue *rq = &runqueues[i];
    initlock(&rq->lock, "runqueue");
    rq->head = 0;
    rq->tail = 0;
    rq->count = 0;
    for (int j = 0; j < NPROC; j++)
      rq->queue[j] = 0;
  }
}

int
rq_enqueue(int cpu_id, struct proc *p)
{
  if (!valid_cpu(cpu_id) || p == 0)
    return -1;

  struct runqueue *rq = &runqueues[cpu_id];
  acquire(&rq->lock);

  if (rq->count == NPROC || rq_find_locked(rq, p) >= 0) {
    release(&rq->lock);
    return -1;
  }

  rq_push_locked(rq, p);
  release(&rq->lock);
  return 0;
}

// Caller holds expected->lock. Only dequeue the expected head.
int
rq_dequeue(int cpu_id, struct proc *expected)
{
  if (!valid_cpu(cpu_id) || expected == 0)
    return -1;

  struct runqueue *rq = &runqueues[cpu_id];
  acquire(&rq->lock);

  if (rq->count == 0 || rq->queue[rq->head] != expected) {
    release(&rq->lock);
    return -1;
  }

  rq->queue[rq->head] = 0;
  rq->head = (rq->head + 1) % NPROC;
  rq->count--;

  release(&rq->lock);
  return 0;
}

int
rq_length(int cpu_id)
{
  if (!valid_cpu(cpu_id))
    return -1;

  struct runqueue *rq = &runqueues[cpu_id];
  acquire(&rq->lock);
  int count = rq->count;
  release(&rq->lock);
  return count;
}

// Snapshot only; recheck p->state and p->rq_cpu under p->lock.
struct proc *
rq_peek_at(int cpu_id, int offset)
{
  if (!valid_cpu(cpu_id) || offset < 0)
    return 0;

  struct runqueue *rq = &runqueues[cpu_id];
  acquire(&rq->lock);

  struct proc *p = 0;
  if (offset < rq->count)
    p = rq->queue[(rq->head + offset) % NPROC];

  release(&rq->lock);
  return p;
}

// Raw queue removal for M1 self-tests; does not modify p->rq_cpu.
int
rq_remove(int cpu_id, struct proc *p)
{
  if (!valid_cpu(cpu_id) || p == 0)
    return -1;

  struct runqueue *rq = &runqueues[cpu_id];
  acquire(&rq->lock);

  int offset = rq_find_locked(rq, p);
  if (offset < 0) {
    release(&rq->lock);
    return -1;
  }

  rq_remove_at_locked(rq, offset);
  release(&rq->lock);
  return 0;
}

// Shared migration engine. Caller holds p->lock.
// Never call rq_length() or sched_cpu_load() under the rq locks.
static int
rq_migrate_locked(int src_cpu, int dst_cpu, struct proc *p, int stealing,
                  int min_diff)
{
  if (!valid_cpu(src_cpu) || !valid_cpu(dst_cpu) || src_cpu == dst_cpu ||
      p == 0)
    return RQ_STEAL_INVALID;

  if (!holding(&p->lock))
    return RQ_STEAL_INVALID;

  if (p->state != RUNNABLE)
    return RQ_STEAL_INELIGIBLE;

  if (p->rq_cpu != src_cpu)
    return RQ_STEAL_STALE;

  if (!cpu_allowed(p, dst_cpu))
    return RQ_STEAL_INELIGIBLE;

  uint online = sched_online_mask();
  uint required = (1U << src_cpu) | (1U << dst_cpu);
  if ((online & required) != required)
    return RQ_STEAL_INELIGIBLE;

  struct runqueue *src = &runqueues[src_cpu];
  struct runqueue *dst = &runqueues[dst_cpu];
  int result;

  rq_lock_pair(src_cpu, dst_cpu);

  int offset = rq_find_locked(src, p);
  if (offset < 0) {
    result = RQ_STEAL_STALE;
    goto unlock;
  }

  if (rq_find_locked(dst, p) >= 0) {
    result = RQ_STEAL_INVALID;
    goto unlock;
  }

  if (stealing && dst->count != 0) {
    result = RQ_STEAL_DST_BUSY;
    goto unlock;
  }

  if (dst->count >= NPROC) {
    result = stealing ? RQ_STEAL_DST_BUSY : RQ_STEAL_INVALID;
    goto unlock;
  }

  // Read CPU busy flags without taking additional rq locks.
  int src_load = src->count + sched_cpu_busy(src_cpu);
  int dst_load = dst->count + sched_cpu_busy(dst_cpu);

  if (stealing) {
    if (src_load <= 1) {
      result = RQ_STEAL_SRC_LOW;
      goto unlock;
    }
  } else if (src_load - dst_load < min_diff) {
    result = RQ_STEAL_SRC_LOW;
    goto unlock;
  }

  // The queue transfer and ownership change are indivisible to
  // scheduler/migration operations respecting the lock contract.
  rq_remove_at_locked(src, offset);
  rq_push_locked(dst, p);
  p->rq_cpu = dst_cpu;
  result = RQ_STEAL_OK;

unlock:
  rq_unlock_pair(src_cpu, dst_cpu);
  return result;
}

// Idle Work Stealing: only steal into an empty destination queue.
int
rq_steal_runnable(int src_cpu, int dst_cpu, struct proc *p)
{
  return rq_migrate_locked(src_cpu, dst_cpu, p, 1, 0);
}

// Periodic Load Balancing: dst may be non-empty.
// Return 0 for success, -1 otherwise.
int
rq_transfer(int src_cpu, int dst_cpu, struct proc *p, int min_diff)
{
  if (min_diff < 1)
    return -1;

  return rq_migrate_locked(src_cpu, dst_cpu, p, 0, min_diff) == RQ_STEAL_OK
           ? 0
           : -1;
}
