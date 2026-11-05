#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "runqueue.h"
#include "defs.h"
#include "proc.h"

// One FIFO queue and one lock per CPU.
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

  if (rq->count == NPROC) {
    release(&rq->lock);
    return -1;
  }
  // Reject duplicates within this queue.
  for (int i = 0; i < rq->count; i++) {
    int index = (rq->head + i) % NPROC;
    if (rq->queue[index] == p) {
      release(&rq->lock);
      return -1;
    }
  }

  rq->queue[rq->tail] = p;
  rq->tail = (rq->tail + 1) % NPROC;
  rq->count++;

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
  acquire(&rq->lock);
  for (int i = 0; i < rq->count; i++) {
    int index = (rq->head + i) % NPROC;
    if (rq->queue[index] != p)
      continue;

    // Close the gap while preserving FIFO order.
    for (int j = i; j < rq->count - 1; j++) {
      int dst = (rq->head + j) % NPROC;
      int src = (dst + 1) % NPROC;
      rq->queue[dst] = rq->queue[src];
    }

    rq->tail = (rq->head + rq->count - 1) % NPROC;
    rq->queue[rq->tail] = 0;
    rq->count--;

    release(&rq->lock);
    return 0;
  }
  release(&rq->lock);
  return -1;
}

// Atomically move one RUNNABLE process from src_cpu to an EMPTY dst_cpu queue.
// Caller must hold p->lock. Locks are always acquired in ascending CPU-id order.
int
rq_steal_runnable(int src_cpu, int dst_cpu, struct proc *p)
{
  struct runqueue *src;
  struct runqueue *dst;
  struct runqueue *first;
  struct runqueue *second;
  int found = -1;

  if (!valid_cpu(src_cpu) || !valid_cpu(dst_cpu) ||
      src_cpu == dst_cpu || p == 0)
    return -1;

  // State and affinity are protected by p->lock.
  if (!holding(&p->lock))
    return -1;
  if (p->state != RUNNABLE ||
      (p->affinity_mask & (1U << dst_cpu)) == 0)
    return -1;

  src = &runqueues[src_cpu];
  dst = &runqueues[dst_cpu];

  // A fixed ordering prevents opposite-direction migration deadlocks.
  if (src_cpu < dst_cpu) {
    first = src;
    second = dst;
  } else {
    first = dst;
    second = src;
  }
  acquire(&first->lock);
  acquire(&second->lock);

// Destination must be empty.
// Source must retain at least one runnable process.
if (dst->count != 0 || src->count <= 1)
    goto out;

  // Find p in the source queue.
  for (int i = 0; i < src->count; i++) {
    int index = (src->head + i) % NPROC;
    if (src->queue[index] == p) {
      found = i;
      break;
    }
  }
  if (found < 0)
    goto out;

  // Remove p while preserving the order of remaining source entries.
  for (int i = found; i < src->count - 1; i++) {
    int to = (src->head + i) % NPROC;
    int from = (to + 1) % NPROC;
    src->queue[to] = src->queue[from];
  }
  src->tail = (src->head + src->count - 1) % NPROC;
  src->queue[src->tail] = 0;
  src->count--;

  // Append p to the destination while both queue locks are still held.
  dst->queue[dst->tail] = p;
  dst->tail = (dst->tail + 1) % NPROC;
  dst->count++;
  found = 0;

out:
  release(&second->lock);
  release(&first->lock);
  return found == 0 ? 0 : -1;
}
// Transfer a process from source CPU to destination CPU.
int
rq_transfer(int src_cpu, int dst_cpu, struct proc *p)
{
  struct runqueue *src;
  struct runqueue *dst;
  struct runqueue *first;
  struct runqueue *second;

  int acquired_p_lock = 0;
  int found = -1;
  int ret = -1;

  if (!valid_cpu(src_cpu) || !valid_cpu(dst_cpu) ||
      src_cpu == dst_cpu || p == 0)
    return -1;

  // Acquire process lock if caller does not hold it.
  if (!holding(&p->lock)) {
    acquire(&p->lock);
    acquired_p_lock = 1;
  }

  // Validate process state and CPU affinity.
  if (p->state != RUNNABLE ||
      (p->affinity_mask & (1U << dst_cpu)) == 0)
    goto done;

  src = &runqueues[src_cpu];
  dst = &runqueues[dst_cpu];

  // Acquire queue locks in a fixed order to avoid deadlock.
  if (src_cpu < dst_cpu) {
    first = src;
    second = dst;
  } else {
    first = dst;
    second = src;
  }

  acquire(&first->lock);
  acquire(&second->lock);

  // Destination may already contain processes.
  // Only reject a completely full queue.
  if (dst->count >= NPROC || src->count == 0)
    goto unlock;

  // Prevent duplicate membership in destination.
  for (int i = 0; i < dst->count; i++) {
    int index = (dst->head + i) % NPROC;
    if (dst->queue[index] == p)
      goto unlock;
  }

  // Find process in source queue.
  for (int i = 0; i < src->count; i++) {
    int index = (src->head + i) % NPROC;

    if (src->queue[index] == p) {
      found = i;
      break;
    }
  }

  if (found < 0)
    goto unlock;

  // Remove process from source while preserving FIFO order.
  for (int i = found; i < src->count - 1; i++) {
    int to = (src->head + i) % NPROC;
    int from = (to + 1) % NPROC;
    src->queue[to] = src->queue[from];
  }

  src->tail = (src->head + src->count - 1) % NPROC;
  src->queue[src->tail] = 0;
  src->count--;

  // Insert process into destination queue.
  dst->queue[dst->tail] = p;
  dst->tail = (dst->tail + 1) % NPROC;
  dst->count++;

  ret = 0;

unlock:
  release(&second->lock);
  release(&first->lock);

done:
  if (acquired_p_lock)
    release(&p->lock);

  return ret;
}
