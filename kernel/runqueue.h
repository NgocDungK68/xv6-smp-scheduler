#ifndef XV6_RUNQUEUE_H
#define XV6_RUNQUEUE_H

#include "types.h"

struct proc;

// Per-CPU FIFO queues store process pointers only.
// Process state, affinity, and queue ownership are managed by the scheduler.
//
// A queue holds only RUNNABLE processes that are WAITING for a CPU. The
// process a CPU is executing right now is not in its queue; the scheduler
// records that with rq_set_running(). The "load" of a CPU is therefore
//     load = queued processes + (1 if the CPU is executing a process)
//
// Lock order (must be respected everywhere):
//     wait_lock -> p->lock -> runqueue lock(s) in ascending CPU id
// Nothing may take a p->lock while holding a runqueue lock.

// Result codes of rq_steal_runnable().
#define RQ_STEAL_OK         0  // p moved from src to dst
#define RQ_STEAL_INVALID    -1 // bad arguments, or caller did not hold p->lock
#define RQ_STEAL_INELIGIBLE -2 // p is no longer RUNNABLE / affinity forbids dst
#define RQ_STEAL_DST_BUSY   -3 // dst queue is no longer empty: stop stealing
#define RQ_STEAL_SRC_LOW    -4 // src has no work to spare: try another victim
#define RQ_STEAL_STALE      -5 // p is no longer in the src queue: try next one

// Initialize all run queues. Call once during boot.
void rq_init(void);

// Append p; return 0 on success, -1 on failure (invalid, full, duplicate).
int rq_enqueue(int cpu_id, struct proc *p);

// Remove the head only if it matches expected.
// Scheduling callers must hold expected->lock.
// Return 0 on success, -1 if no matching head.
int rq_dequeue(int cpu_id, struct proc *expected);

// Return the number of queued processes, or -1 for invalid cpu_id.
int rq_length(int cpu_id);

// Inspect the entry at offset from the head (0-based), without removing it.
// The returned pointer is a snapshot; revalidate under p->lock before use.
struct proc *rq_peek_at(int cpu_id, int offset);

// Remove p while preserving FIFO order; return 0 on success, -1 on failure.
int rq_remove(int cpu_id, struct proc *p);

// Caller MUST hold p->lock.
//
// Stealing requires an empty destination queue,
// and the source must retain at least one unit of load.
int rq_steal_runnable(int src_cpu, int dst_cpu, struct proc *p);

// Caller MUST hold p->lock.
//
// Load balancing allows a non-empty destination.
// The move succeeds only if the current load difference
// is at least min_diff.
int rq_transfer(int src_cpu, int dst_cpu, struct proc *p, int min_diff);

#ifdef RUNQUEUE_TEST
void rq_selftest(void);
#endif

#endif
