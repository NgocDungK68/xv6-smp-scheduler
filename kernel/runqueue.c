#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "runqueue.h"
#include "defs.h"
#include "proc.h"

// One FIFO queue and one lock per CPU.
//
// The queue holds only RUNNABLE processes waiting for a CPU. The process a
// CPU is executing is not in the queue; `running` records that so that
// load = count + running can be compared between CPUs.
//
// Lock order: p->lock -> runqueue locks, taken in ascending CPU id.
// No function in this file takes a p->lock while holding a runqueue lock,
// except rq_transfer()/rq_steal_runnable() which take/require p->lock FIRST.
struct runqueue {
  struct spinlock lock;
  struct proc *queue[NPROC];
  int head;
  int tail;
  int count;
  int running; // 1 while this CPU executes a process (use __atomic_*)
};

static struct runqueue runqueues[NCPU];
static int rq_initialized;

static int
valid_cpu(int cpu_id)
{
  return cpu_id >= 0 && cpu_id < NCPU;
}

// ---------------------------------------------------------------------
// Helpers. The *_locked functions require the queue's lock to be held.
// ---------------------------------------------------------------------

// Return the offset (0 = head) of p in rq, or -1.
static int
rq_find_locked(struct runqueue *rq, struct proc *p)
{
  for (int i = 0; i < rq->count; i++) {
    if (rq->queue[(rq->head + i) % NPROC] == p)
      return i;
  }
  return -1;
}

// Remove the entry at `offset`, preserving FIFO order of the others.
static void
rq_remove_at_locked(struct runqueue *rq, int offset)
{
  for (int i = offset; i < rq->count - 1; i++) {
    int to = (rq->head + i) % NPROC;
    int from = (to + 1) % NPROC;
    rq->queue[to] = rq->queue[from];
  }
  rq->tail = (rq->head + rq->count - 1) % NPROC;
  rq->queue[rq->tail] = 0;
  rq->count--;
}

// Append p. Caller must have checked count < NPROC.
static void
rq_push_locked(struct runqueue *rq, struct proc *p)
{
  rq->queue[rq->tail] = p;
  rq->tail = (rq->tail + 1) % NPROC;
  rq->count++;
}

// Lock two different queues in ascending CPU id order.
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
  release(&runqueues[a].lock);
  release(&runqueues[b].lock);
}

// ---------------------------------------------------------------------
// Shared helpers for worksteal.c / loadbalancing.c
// ---------------------------------------------------------------------

uint
rq_valid_cpu_mask(void)
{
  return (1U << NCPU) - 1U;
}

int
rq_cpu_online(int cpu_id, uint online_mask)
{
  return valid_cpu(cpu_id) && (online_mask & (1U << cpu_id)) != 0;
}

int
rq_proc_can_run_on(struct proc *p, int dst_cpu, int *affinity_rejected)
{
  int allowed = 0;

  if (affinity_rejected)
    *affinity_rejected = 0;
  if (p == 0 || !valid_cpu(dst_cpu))
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

// ---------------------------------------------------------------------
// Public queue API
// ---------------------------------------------------------------------

void
rq_init(void)
{
  if (rq_initialized)
    return;

  for (int i = 0; i < NCPU; i++) {
    struct runqueue *rq = &runqueues[i];
    initlock(&rq->lock, "runqueue");
    rq->head = 0;
    rq->tail = 0;
    rq->count = 0;
    rq->running = 0;
    for (int j = 0; j < NPROC; j++)
      rq->queue[j] = 0;
  }
  rq_initialized = 1;
}

int
rq_enqueue(int cpu_id, struct proc *p)
{
  if (!valid_cpu(cpu_id) || p == 0)
    return -1;

  struct runqueue *rq = &runqueues[cpu_id];
  acquire(&rq->lock);

  // Full, or already queued here (duplicates are rejected).
  if (rq->count == NPROC || rq_find_locked(rq, p) >= 0) {
    release(&rq->lock);
    return -1;
  }

  rq_push_locked(rq, p);
  release(&rq->lock);
  return 0;
}

struct proc *
rq_dequeue(int cpu_id)
{
  if (!valid_cpu(cpu_id))
    return 0;

  struct runqueue *rq = &runqueues[cpu_id];
  acquire(&rq->lock);
  if (rq->count == 0) {
    release(&rq->lock);
    return 0;
  }

  struct proc *p = rq->queue[rq->head];
  rq->queue[rq->head] = 0;
  rq->head = (rq->head + 1) % NPROC;
  rq->count--;

  release(&rq->lock);
  return p;
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

int
rq_load(int cpu_id)
{
  if (!valid_cpu(cpu_id))
    return -1;

  struct runqueue *rq = &runqueues[cpu_id];
  acquire(&rq->lock);
  int count = rq->count;
  release(&rq->lock);
  return count + __atomic_load_n(&rq->running, __ATOMIC_ACQUIRE);
}

void
rq_set_running(int cpu_id, int running)
{
  if (!valid_cpu(cpu_id))
    return;
  __atomic_store_n(&runqueues[cpu_id].running, running ? 1 : 0,
                   __ATOMIC_RELEASE);
}

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

int
rq_remove(int cpu_id, struct proc *p)
{
  if (!valid_cpu(cpu_id) || p == 0)
    return -1;

  struct runqueue *rq = &runqueues[cpu_id];
  int ret = -1;

  acquire(&rq->lock);
  int found = rq_find_locked(rq, p);
  if (found >= 0) {
    rq_remove_at_locked(rq, found);
    ret = 0;
  }
  release(&rq->lock);
  return ret;
}

// ---------------------------------------------------------------------
// Migration
// ---------------------------------------------------------------------

// Work stealing: move one RUNNABLE process from src_cpu to an EMPTY dst_cpu.
// Caller must hold p->lock (p->lock is taken BEFORE the queue locks).
//
// src must keep at least one unit of load after the move, where
// load = queued + running. So a victim that is executing one process and has
// one more waiting CAN be stolen from, while a victim whose only work is a
// single queued process (about to be dequeued by its own scheduler) cannot.
int
rq_steal_runnable(int src_cpu, int dst_cpu, struct proc *p)
{
  struct runqueue *src;
  struct runqueue *dst;
  int found;
  int ret;

  if (!valid_cpu(src_cpu) || !valid_cpu(dst_cpu) || src_cpu == dst_cpu ||
      p == 0 || !holding(&p->lock))
    return RQ_STEAL_INVALID;

  // State and affinity are protected by p->lock.
  if (p->state != RUNNABLE || (p->affinity_mask & (1U << dst_cpu)) == 0)
    return RQ_STEAL_INELIGIBLE;

  src = &runqueues[src_cpu];
  dst = &runqueues[dst_cpu];

  rq_lock_pair(src_cpu, dst_cpu);

  if (dst->count != 0) {
    ret = RQ_STEAL_DST_BUSY;
  } else if (src->count + __atomic_load_n(&src->running, __ATOMIC_ACQUIRE) <=
             1) {
    ret = RQ_STEAL_SRC_LOW;
  } else {
    found = rq_find_locked(src, p);
    if (found < 0) {
      ret = RQ_STEAL_STALE;
    } else {
      rq_remove_at_locked(src, found);
      rq_push_locked(dst, p); // dst was empty, so it cannot be full
      ret = RQ_STEAL_OK;
    }
  }

  rq_unlock_pair(src_cpu, dst_cpu);
  return ret;
}

// Transfer a process from source CPU to destination CPU (load balancing).
// The destination may already contain processes; only a full queue or a
// duplicate is rejected.
int
rq_transfer(int src_cpu, int dst_cpu, struct proc *p)
{
  int acquired_p_lock = 0;
  int ret = -1;

  if (!valid_cpu(src_cpu) || !valid_cpu(dst_cpu) || src_cpu == dst_cpu ||
      p == 0)
    return -1;

  // Acquire process lock if caller does not hold it.
  if (!holding(&p->lock)) {
    acquire(&p->lock);
    acquired_p_lock = 1;
  }

  // Validate process state and CPU affinity.
  if (p->state == RUNNABLE && (p->affinity_mask & (1U << dst_cpu)) != 0) {
    struct runqueue *src = &runqueues[src_cpu];
    struct runqueue *dst = &runqueues[dst_cpu];

    rq_lock_pair(src_cpu, dst_cpu);

    if (dst->count < NPROC && rq_find_locked(dst, p) < 0) {
      int found = rq_find_locked(src, p);
      if (found >= 0) {
        rq_remove_at_locked(src, found);
        rq_push_locked(dst, p);
        ret = 0;
      }
    }

    rq_unlock_pair(src_cpu, dst_cpu);
  }

  if (acquired_p_lock)
    release(&p->lock);

  return ret;
}
