#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/schedstats.h"
#include "user/user.h"

// Collect global scheduler counters around an unmodified user command.
static int
snapshot(struct sched_stats *s)
{
  if (sched_getstats(s, sizeof(*s)) < 0)
    return -1;
  if (s->version != SCHED_STATS_VERSION || s->size != sizeof(*s))
    return -1;
  return 0;
}

static void
report(const struct sched_stats *before, const struct sched_stats *after)
{
  uint64 sent = 0;
  uint64 received = 0;
  uint64 waits = 0;
  uint64 dispatches = 0;

  for (int cpu = 0; cpu < NCPU; cpu++) {
    sent += after->ipi_sent[cpu] - before->ipi_sent[cpu];
    received += after->ipi_received[cpu] - before->ipi_received[cpu];
    waits += after->wfi_waits[cpu] - before->wfi_waits[cpu];
    dispatches += after->dispatches[cpu] - before->dispatches[cpu];
  }

  printf("SCHEDSTAT dispatches=%lu\n", dispatches);
  printf("SCHEDSTAT ws_searches=%lu ws_attempts=%lu ws_successes=%lu\n",
         after->ws_searches - before->ws_searches,
         after->ws_attempts - before->ws_attempts,
         after->ws_successes - before->ws_successes);
  printf("SCHEDSTAT lb_checks=%lu lb_migrations=%lu\n",
         after->lb_checks - before->lb_checks,
         after->lb_migrations - before->lb_migrations);
  printf("SCHEDSTAT ipi_sent=%lu ipi_received=%lu wfi_waits=%lu\n", sent,
         received, waits);
}

int
main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("usage: schedmeasure command [arguments...]\n");
    exit(1);
  }

  struct sched_stats before;
  struct sched_stats after;
  if (snapshot(&before) < 0) {
    printf("schedmeasure: initial snapshot failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    printf("schedmeasure: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    exec(argv[1], &argv[1]);
    printf("schedmeasure: exec %s failed\n", argv[1]);
    exit(1);
  }

  int status = -1;
  if (wait(&status) != pid) {
    printf("schedmeasure: wait failed\n");
    exit(1);
  }
  if (snapshot(&after) < 0) {
    printf("schedmeasure: final snapshot failed\n");
    exit(1);
  }

  report(&before, &after);
  printf("schedmeasure: child_status=%d\n", status);
  exit(status == 0 ? 0 : 1);
}
