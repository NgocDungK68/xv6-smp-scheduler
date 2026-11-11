
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

static struct proc test_proc;

void
affinity_selftest(void)
{
  test_proc.affinity_mask = (1U << 0) | (1U << 2);

  if (cpu_allowed(&test_proc, 0) != 1 || cpu_allowed(&test_proc, 1) != 0 ||
      cpu_allowed(&test_proc, 2) != 1 || cpu_allowed(&test_proc, 3) != 0)
    panic("affinity test: mask");

  if (cpu_allowed(&test_proc, -1) != 0 || cpu_allowed(&test_proc, NCPU) != 0 ||
      cpu_allowed(0, 0) != 0)
    panic("affinity test: invalid args");

  printk("affinity: selftest PASS\n");
}
