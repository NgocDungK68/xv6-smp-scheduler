#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "ipi.h"

static uint64 sent[NCPU];
static uint64 received[NCPU];

// Enable supervisor software interrupts on this hart.
void
ipiinithart(void)
{
#ifdef IPI_WAKEUP
  c_sip(SIP_SSIP);
  w_sie(r_sie() | SIE_SSIE);
#endif
}

// Send a supervisor software interrupt to another hart.
void
ipisend(int cpu_id)
{
#ifdef IPI_WAKEUP
  if (cpu_id < 0 || cpu_id >= NCPU)
    panic("ipisend: cpu");

  // Make earlier queue updates visible before notifying the target.
  io_fence();
  *(volatile uint *)(ACLINT_SSWI + 4UL * cpu_id) = 1;

  __atomic_fetch_add(&sent[cpu_id], 1, __ATOMIC_RELAXED);
#else
  (void)cpu_id;
#endif
}

// Acknowledge a supervisor software interrupt on this hart.
void
ipiintr(void)
{
#ifdef IPI_WAKEUP
  c_sip(SIP_SSIP);

  int id = cpuid();
  __atomic_fetch_add(&received[id], 1, __ATOMIC_RELAXED);
#else
  panic("ipiintr: disabled");
#endif
}

uint64
ipisent(int cpu_id)
{
  if (cpu_id < 0 || cpu_id >= NCPU)
    return 0;

  return __atomic_load_n(&sent[cpu_id], __ATOMIC_RELAXED);
}

uint64
ipireceived(int cpu_id)
{
  if (cpu_id < 0 || cpu_id >= NCPU)
    return 0;

  return __atomic_load_n(&received[cpu_id], __ATOMIC_RELAXED);
}