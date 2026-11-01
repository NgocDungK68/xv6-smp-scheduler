#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "runqueue.h"
#include "defs.h"

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
