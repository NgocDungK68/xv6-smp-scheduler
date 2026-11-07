#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "runqueue.h"
#include "defs.h"
#include "worksteal.h"
#include "loadbalancing.h"

// Bitmask of CPUs that have completed initialization.
volatile uint cpu_online_mask = 0;

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if (cpuid() == 0) {
    consoleinit();
    printkinit();

    printk("\n");
    printk("xv6 kernel is booting\n");
    printk("\n");

    kinit();
    kvminit();
    kvminithart();

    procinit();

    // Initialize scheduling modules.
    rq_init();
    worksteal_init();
    loadbalance_init();

#ifdef RUNQUEUE_TEST
    rq_selftest();
#endif

    trapinit();
    trapinithart();

    plicinit();
    plicinithart();

    binit();
    iinit();
    fileinit();
    virtio_disk_init();

    // Mark CPU 0 online BEFORE creating the first process.
    __atomic_store_n(&cpu_online_mask, 1U, __ATOMIC_RELEASE);

    userinit();

    // Allow secondary CPUs to continue initialization.
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);

  } else {

    // Wait for CPU 0 to finish global initialization.
    while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
      ;

    printk("hart %d starting\n", cpuid());

    kvminithart();
    trapinithart();
    plicinithart();

    // Mark this CPU online after initialization.
    __atomic_fetch_or(&cpu_online_mask, 1U << cpuid(), __ATOMIC_RELEASE);
  }

  scheduler();
}
