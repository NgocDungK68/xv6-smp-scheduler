
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "runqueue.h"
#include "defs.h"

extern struct proc proc[NPROC];

// Dequeue with the expected process lock held.
// Tests only queue storage, not process state.
static int
test_dequeue(int cpu_id, struct proc *expected)
{
  if (expected == 0)
    return rq_dequeue(cpu_id, expected);

  acquire(&expected->lock);
  int result = rq_dequeue(cpu_id, expected);
  release(&expected->lock);

  return result;
}

// Test empty queues and invalid arguments.
static void
test_empty_and_invalid(void)
{
  for (int i = 0; i < NCPU; i++) {
    if (rq_length(i) != 0)
      panic("rq test: init");
  }

  if (test_dequeue(0, &proc[0]) != -1 || rq_peek_at(0, 0) != 0 ||
      rq_remove(0, &proc[0]) != -1)
    panic("rq test: empty");

  if (rq_enqueue(-1, &proc[0]) != -1 || rq_enqueue(NCPU, &proc[0]) != -1 ||
      rq_enqueue(0, 0) != -1 || rq_length(-1) != -1 || rq_length(NCPU) != -1 ||
      test_dequeue(-1, &proc[0]) != -1 || test_dequeue(NCPU, &proc[0]) != -1 ||
      test_dequeue(0, 0) != -1 || rq_peek_at(0, -1) != 0 ||
      rq_peek_at(NCPU, 0) != 0 || rq_remove(0, 0) != -1 ||
      rq_remove(NCPU, &proc[0]) != -1)
    panic("rq test: invalid args");
}

// Test FIFO order, peek, and conditional dequeue.
static void
test_fifo_and_peek(void)
{
  if (rq_enqueue(0, &proc[0]) != 0 || rq_enqueue(0, &proc[1]) != 0 ||
      rq_enqueue(0, &proc[2]) != 0 || rq_enqueue(0, &proc[1]) != -1 ||
      rq_length(0) != 3)
    panic("rq test: enqueue");

  if (rq_peek_at(0, 0) != &proc[0] || rq_peek_at(0, 1) != &proc[1] ||
      rq_peek_at(0, 2) != &proc[2] || rq_peek_at(0, 3) != 0)
    panic("rq test: peek");

  // P1 is not the head. Dequeue must fail without changes.
  if (test_dequeue(0, &proc[1]) != -1 || rq_length(0) != 3 ||
      rq_peek_at(0, 0) != &proc[0])
    panic("rq test: head mismatch");

  // Remove P1 from the middle, then dequeue in FIFO order.
  if (rq_remove(0, &proc[1]) != 0 || rq_remove(0, &proc[1]) != -1 ||
      test_dequeue(0, &proc[0]) != 0 || test_dequeue(0, &proc[2]) != 0 ||
      test_dequeue(0, &proc[2]) != -1 || rq_length(0) != 0)
    panic("rq test: remove/FIFO");
}

// Test a full circular queue and head/tail wraparound.
static void
test_full_and_wraparound(void)
{
  for (int i = 0; i < NPROC; i++) {
    if (rq_enqueue(0, &proc[i]) != 0)
      panic("rq test: fill");
  }

  if (rq_length(0) != NPROC || rq_enqueue(0, &proc[0]) != -1)
    panic("rq test: full");

  // Keep the last four processes in the queue.
  for (int i = 0; i < NPROC - 4; i++) {
    if (test_dequeue(0, &proc[i]) != 0)
      panic("rq test: drain");
  }

  // Append three processes after the circular wraparound.
  for (int i = 0; i < 3; i++) {
    if (rq_enqueue(0, &proc[i]) != 0)
      panic("rq test: wrap enqueue");
  }

  // Remove an element while preserving the FIFO order.
  if (rq_remove(0, &proc[1]) != 0 || rq_length(0) != 6 ||
      rq_peek_at(0, 4) != &proc[0] || rq_peek_at(0, 5) != &proc[2])
    panic("rq test: wrap remove");

  for (int i = NPROC - 4; i < NPROC; i++) {
    if (test_dequeue(0, &proc[i]) != 0)
      panic("rq test: wrap FIFO");
  }

  if (test_dequeue(0, &proc[0]) != 0 || test_dequeue(0, &proc[2]) != 0 ||
      rq_length(0) != 0)
    panic("rq test: wrap empty");
}

// Test removing the head, tail, and last remaining process.
static void
test_remove_edges(void)
{
  if (rq_enqueue(0, &proc[3]) != 0 || rq_enqueue(0, &proc[4]) != 0 ||
      rq_enqueue(0, &proc[5]) != 0 || rq_remove(0, &proc[3]) != 0 ||
      rq_remove(0, &proc[5]) != 0 || rq_length(0) != 1 ||
      rq_peek_at(0, 0) != &proc[4] || rq_remove(0, &proc[4]) != 0 ||
      rq_remove(0, &proc[4]) != -1 || test_dequeue(0, &proc[4]) != -1)
    panic("rq test: remove edges");
}

// Verify that each CPU has an independent FIFO queue.
static void
test_independent_queues(void)
{
  if (rq_enqueue(1, &proc[6]) != 0 || rq_enqueue(NCPU - 1, &proc[7]) != 0 ||
      rq_length(0) != 0 || rq_length(1) != 1 || rq_length(NCPU - 1) != 1 ||
      test_dequeue(1, &proc[6]) != 0 || test_dequeue(NCPU - 1, &proc[7]) != 0)
    panic("rq test: independent");

  for (int i = 0; i < NCPU; i++) {
    if (rq_length(i) != 0)
      panic("rq test: cleanup");
  }
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
