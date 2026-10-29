
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "runqueue.h"
#include "defs.h"

extern struct proc proc[NPROC];

// Test queue operations with process pointers; do not change process state.
static void
test_empty_and_invalid(void)
{
  for (int i = 0; i < NCPU; i++)
    if (rq_length(i) != 0)
      panic("rq test: init");

  if (rq_dequeue(0) != 0 || rq_peek_at(0, 0) != 0 ||
      rq_remove(0, &proc[0]) != -1)
    panic("rq test: empty");

  if (rq_enqueue(-1, &proc[0]) != -1 ||
      rq_enqueue(NCPU, &proc[0]) != -1 ||
      rq_enqueue(0, 0) != -1 ||
      rq_length(-1) != -1 || rq_length(NCPU) != -1 ||
      rq_dequeue(-1) != 0 || rq_dequeue(NCPU) != 0 ||
      rq_peek_at(0, -1) != 0 || rq_peek_at(NCPU, 0) != 0 ||
      rq_remove(0, 0) != -1 || rq_remove(NCPU, &proc[0]) != -1)
    panic("rq test: invalid args");
}

static void
test_fifo_and_peek(void)
{
  if (rq_enqueue(0, &proc[0]) != 0 ||
      rq_enqueue(0, &proc[1]) != 0 ||
      rq_enqueue(0, &proc[2]) != 0 ||
      rq_enqueue(0, &proc[1]) != -1 ||
      rq_length(0) != 3)
    panic("rq test: enqueue");

  if (rq_peek_at(0, 0) != &proc[0] ||
      rq_peek_at(0, 1) != &proc[1] ||
      rq_peek_at(0, 2) != &proc[2] ||
      rq_peek_at(0, 3) != 0 || rq_length(0) != 3)
    panic("rq test: peek");

  if (rq_remove(0, &proc[1]) != 0 ||
      rq_remove(0, &proc[1]) != -1 ||
      rq_dequeue(0) != &proc[0] ||
      rq_dequeue(0) != &proc[2] ||
      rq_dequeue(0) != 0 || rq_length(0) != 0)
    panic("rq test: remove/FIFO");
}

static void
test_full_and_wraparound(void)
{
  for (int i = 0; i < NPROC; i++)
    if (rq_enqueue(0, &proc[i]) != 0)
      panic("rq test: fill");

  if (rq_length(0) != NPROC || rq_enqueue(0, &proc[0]) != -1)
    panic("rq test: full");

  // Leave four entries spanning the end of the circular array.
  for (int i = 0; i < NPROC - 4; i++)
    if (rq_dequeue(0) != &proc[i])
      panic("rq test: drain");

  for (int i = 0; i < 3; i++)
    if (rq_enqueue(0, &proc[i]) != 0)
      panic("rq test: wrap enqueue");

  if (rq_remove(0, &proc[1]) != 0 ||
      rq_length(0) != 6 ||
      rq_peek_at(0, 4) != &proc[0] ||
      rq_peek_at(0, 5) != &proc[2])
    panic("rq test: wrap remove");

  for (int i = NPROC - 4; i < NPROC; i++)
    if (rq_dequeue(0) != &proc[i])
      panic("rq test: wrap FIFO");

  if (rq_dequeue(0) != &proc[0] ||
      rq_dequeue(0) != &proc[2] || rq_length(0) != 0)
    panic("rq test: wrap empty");
}

static void
test_remove_edges(void)
{
  if (rq_enqueue(0, &proc[3]) != 0 ||
      rq_enqueue(0, &proc[4]) != 0 ||
      rq_enqueue(0, &proc[5]) != 0 ||
      rq_remove(0, &proc[3]) != 0 ||
      rq_remove(0, &proc[5]) != 0 ||
      rq_length(0) != 1 ||
      rq_peek_at(0, 0) != &proc[4] ||
      rq_remove(0, &proc[4]) != 0 ||
      rq_remove(0, &proc[4]) != -1 ||
      rq_dequeue(0) != 0)
    panic("rq test: remove edges");
}

static void
test_independent_queues(void)
{
  if (rq_enqueue(1, &proc[6]) != 0 ||
      rq_enqueue(NCPU - 1, &proc[7]) != 0 ||
      rq_length(0) != 0 || rq_length(1) != 1 ||
      rq_length(NCPU - 1) != 1 ||
      rq_dequeue(1) != &proc[6] ||
      rq_dequeue(NCPU - 1) != &proc[7])
    panic("rq test: independent");

  for (int i = 0; i < NCPU; i++)
    if (rq_length(i) != 0)
      panic("rq test: cleanup");
}

void
rq_selftest(void)
{
  test_empty_and_invalid();
  test_fifo_and_peek();
  test_full_and_wraparound();
  test_remove_edges();
  test_independent_queues();

  printk("runqueue: selftest PASS\n");
}
