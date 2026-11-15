#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "ipi.h"
#include "runqueue.h"
#include "worksteal.h"
#include "loadbalancing.h"
#include "schedstats.h"

// Collect a best-effort snapshot of scheduler statistics.
void
schedstats_read(struct sched_stats *out)
{
  memset(out, 0, sizeof(*out));

  out->version = SCHED_STATS_VERSION;
  out->size = sizeof(*out);
  out->features = SCHED_FEAT_WS | SCHED_FEAT_LB;

#ifdef IPI_WAKEUP
  out->features |= SCHED_FEAT_IPI;
#endif

  out->online_mask = sched_online_mask();
  out->idle_mask = sched_idle_mask();

  for (int cpu = 0; cpu < NCPU; cpu++) {
    out->ipi_sent[cpu] = ipisent(cpu);
    out->ipi_received[cpu] = ipireceived(cpu);
    out->wfi_waits[cpu] = sched_wfi_waits(cpu);

    int n = rq_length(cpu);
    out->queued[cpu] = n < 0 ? 0 : n;
    out->running[cpu] = sched_cpu_busy(cpu) == 1;

    struct worksteal_stats ws;
    if (worksteal_get_stats(cpu, &ws) == 0) {
      out->ws_searches += ws.searches;
      out->ws_attempts += ws.attempts;
      out->ws_successes += ws.successes;
    }
  }

  struct loadbalance_stats lb;
  if (loadbalance_get_stats(&lb) == 0) {
    out->lb_checks = lb.checks;
    out->lb_migrations = lb.migrations;
  }
}