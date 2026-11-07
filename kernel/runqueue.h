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

// Initialize all run queues. Safe to call more than once.
void rq_init(void);

// Append p; return 0 on success, -1 on failure (invalid, full, duplicate).
int rq_enqueue(int cpu_id, struct proc *p);

// Remove the head; return 0 if empty or cpu_id is invalid.
struct proc *rq_dequeue(int cpu_id);

// Return the number of queued processes, or -1 for invalid cpu_id.
int rq_length(int cpu_id);

// Return queued processes + running flag (see "load" above), or -1 for an
// invalid cpu_id.
int rq_load(int cpu_id);

// Tell the run queue whether cpu_id is executing a process right now.
// Called by scheduler() around swtch() and by yield().
void rq_set_running(int cpu_id, int running);

// Inspect the entry at offset from the head (0-based), without removing it.
// The returned pointer is a snapshot; revalidate under p->lock before use.
struct proc *rq_peek_at(int cpu_id, int offset);

// Remove p while preserving FIFO order; return 0 on success, -1 on failure.
int rq_remove(int cpu_id, struct proc *p);

// Move p from src_cpu's queue to the tail of dst_cpu's queue.
// Takes p->lock itself if the caller does not hold it.
// Return 0 on success, -1 on failure.
int rq_transfer(int src_cpu, int dst_cpu, struct proc *p);

// Work-stealing migration: move one RUNNABLE process from src_cpu to an
// EMPTY dst_cpu queue, but only if src keeps at least one unit of load.
// Caller must hold p->lock. Returns one of the RQ_STEAL_* codes.
int rq_steal_runnable(int src_cpu, int dst_cpu, struct proc *p);

// ---- helpers shared by worksteal.c and loadbalancing.c ----

// Bit mask with one bit per CPU that exists (NCPU bits).
uint rq_valid_cpu_mask(void);

// Non-zero if cpu_id is valid and its bit is set in online_mask.
int rq_cpu_online(int cpu_id, uint online_mask);

// Snapshot check: takes p->lock (caller must NOT hold it). Returns 1 if p is
// RUNNABLE and its affinity allows dst_cpu. *affinity_rejected (optional) is
// set to 1 if p is RUNNABLE but affinity forbids dst_cpu.
int rq_proc_can_run_on(struct proc *p, int dst_cpu, int *affinity_rejected);

#ifdef RUNQUEUE_TEST
void rq_selftest(void);
#endif

#endif
