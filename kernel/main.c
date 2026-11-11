#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "runqueue.h"
#include "defs.h"
#include "worksteal.h"
#include "loadbalancing.h"

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
    trapinit();         // trap vectors
    trapinithart();     // install kernel trap vector
    plicinit();         // set up interrupt controller
    plicinithart();     // ask PLIC for device interrupts
    binit();            // buffer cache
    iinit();            // inode table
    fileinit();         // file table
    virtio_disk_init(); // emulated hard disk

    // CPU 0 must be online before the first process is enqueued.
    sched_cpu_online();

    userinit(); // first user process

    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);

  } else {

    // Wait for CPU 0 to finish global initialization.
    while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
      ;

    printk("hart %d starting\n", cpuid());
    kvminithart();  // turn on paging
    trapinithart(); // install kernel trap vector
    plicinithart(); // ask PLIC for device interrupts
    sched_cpu_online();
  }

  scheduler();
}
