#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "runqueue.h"

// Existing global process table from proc.c.
extern struct proc proc[NPROC];

// Temporary placement policy for Task 2.
// Task 3 will choose among eligible online CPUs.
static int
sched_select_cpu(struct proc *p)
{
  (void)p;
  return cpuid();
}

// Enqueue a process and transition it to RUNNABLE.
// Caller must hold p->lock.
// Accepted states: USED, RUNNING, SLEEPING.
void
sched_enqueue(struct proc *p)
{
  if (p == 0)
    panic("sched_enqueue: null");

  if (!holding(&p->lock))
    panic("sched_enqueue: lock");

  if (p->rq_cpu != -1)
    panic("sched_enqueue: queued");

  if (p->state != USED && p->state != RUNNING && p->state != SLEEPING)
    panic("sched_enqueue: state");

  int cpu_id = sched_select_cpu(p);

  if (cpu_id < 0 || cpu_id >= NCPU)
    panic("sched_enqueue: cpu");

  if (rq_enqueue(cpu_id, p) != 0)
    panic("sched_enqueue: failed");

  p->rq_cpu = cpu_id;
  p->state = RUNNABLE;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        // Temporary bridge: the old scheduler still scans proc[].
        // Remove the process from its run queue before running it.
        if (p->rq_cpu < 0 || p->rq_cpu >= NCPU)
          panic("scheduler: not queued");

        if (rq_remove(p->rq_cpu, p) != 0)
          panic("scheduler: queue mismatch");

        p->rq_cpu = -1;

        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Don't re-enable interrupts on release.
        mycpu()->intena = 0;

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if (found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  sched_enqueue(p);
  sched();
  release(&p->lock);
}
