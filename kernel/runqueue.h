
#ifndef XV6_RUNQUEUE_H
#define XV6_RUNQUEUE_H

struct proc;

// Per-CPU FIFO queues store process pointers only.
// Process state, affinity, and queue ownership are managed by the scheduler.

// Initialize all run queues.
void rq_init(void);

// Append p; return 0 on success, -1 on failure.
int rq_enqueue(int cpu_id, struct proc *p);

// Remove the head; return 0 if empty or cpu_id is invalid.
struct proc *rq_dequeue(int cpu_id);

// Return the number of queued processes, or -1 for invalid cpu_id.
int rq_length(int cpu_id);

// Inspect the entry at offset from the head (0-based), without removing it.
// The returned pointer is a snapshot; revalidate under p->lock before use.
struct proc *rq_peek_at(int cpu_id, int offset);

// Remove p while preserving FIFO order; return 0 on success, -1 on failure.
int rq_remove(int cpu_id, struct proc *p);

#ifdef RUNQUEUE_TEST
void         rq_selftest(void);
#endif

#endif
