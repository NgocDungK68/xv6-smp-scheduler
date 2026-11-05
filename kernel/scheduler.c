#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "runqueue.h"

// CPUs set their online bit once, after per-hart initialization.
static uint online_mask;
static uint placement_ticket;
static int cpu_busy[NCPU];

void
sched_cpu_online(void)
{
  push_off();
  int id = cpuid();

  if (id < 0 || id >= NCPU)
    panic("sched_cpu_online: id");

  __atomic_fetch_or(&online_mask, 1U << id, __ATOMIC_RELEASE);
  pop_off();
}

uint
sched_online_mask(void)
{
  return __atomic_load_n(&online_mask, __ATOMIC_ACQUIRE);
}

// Caller holds p->lock. Select among CPUs that are online and allowed.
static int
sched_select_cpu(struct proc *p)
{
  uint eligible = p->affinity_mask & sched_online_mask();
  if (eligible == 0)
    return -1;

  int current = cpuid();

  // A yielding process stays local when its affinity still allows it.
  if (p->state == RUNNING && (eligible & (1U << current)))
    return current;

  int min_load = NPROC + 1;
  uint best_mask = 0;
  int ties = 0;

  for (int id = 0; id < NCPU; id++) {
    if ((eligible & (1U << id)) == 0)
      continue;

    int queued = rq_length(id);
    if (queued < 0)
      panic("sched_select_cpu: queue");

    // RUNNING processes are not stored in their run queue.
    int busy = __atomic_load_n(&cpu_busy[id], __ATOMIC_RELAXED);
    int load = queued + busy;

    if (load < min_load) {
      min_load = load;
      best_mask = 1U << id;
      ties = 1;
    } else if (load == min_load) {
      best_mask |= 1U << id;
      ties++;
    }
  }

  if (ties == 0)
    panic("sched_select_cpu: empty");

  uint choice =
    __atomic_fetch_add(&placement_ticket, 1U, __ATOMIC_RELAXED) % (uint)ties;

  for (int id = 0; id < NCPU; id++) {
    if (best_mask & (1U << id)) {
      if (choice == 0)
        return id;
      choice--;
    }
  }

  panic("sched_select_cpu: tie");
  return -1;
}

// Enqueue a process and transition it to RUNNABLE.
// Caller must hold p->lock.
// Accepted states: USED, RUNNING, SLEEPING, RUNNABLE.
void
sched_enqueue(struct proc *p)
{
  if (p == 0)
    panic("sched_enqueue: null");

  if (!holding(&p->lock))
    panic("sched_enqueue: lock");

  if (p->rq_cpu != -1)
    panic("sched_enqueue: queued");

  if (p->state != USED && p->state != RUNNING && p->state != SLEEPING &&
      p->state != RUNNABLE)
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
  struct cpu *c = mycpu();
  int id = cpuid();
  c->proc = 0;

  for (;;) {
    // xv6 idle protocol: briefly enable interrupts before polling.
    intr_on();
    intr_off();

    // Peek releases rq->lock before acquiring p->lock.
    struct proc *p = rq_peek_at(id, 0);
    if (p == 0) {
      // Periodic timer interrupts wake this CPU;
      // an IPI is a future extension.
      asm volatile("wfi");
      continue;
    }

    acquire(&p->lock);

    // A concurrent migration may have moved the peeked process.
    // Conditional dequeue never removes a different queue head.
    if (rq_dequeue(id, p) != 0) {
      release(&p->lock);
      continue;
    }

    // A successful dequeue requires correct ownership/state.
    if (p->state != RUNNABLE || p->rq_cpu != id)
      panic("scheduler: queue invariant");

    p->rq_cpu = -1;

    // Affinity may have changed while the process was queued.
    if ((p->affinity_mask & (1U << id)) == 0) {
      sched_enqueue(p);
      release(&p->lock);
      continue;
    }

    p->state = RUNNING;
    c->proc = p;
    __atomic_store_n(&cpu_busy[id], 1, __ATOMIC_RELAXED);

    swtch(&c->context, &p->context);

    // p->lock is still held here: sched() transferred it back.
    __atomic_store_n(&cpu_busy[id], 0, __ATOMIC_RELAXED);
    mycpu()->intena = 0;
    c->proc = 0;
    release(&p->lock);
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
