#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/schedstats.h"
#include "user/user.h"

static int
readstats(struct sched_stats *s)
{
  return sched_getstats(s, sizeof(*s));
}

int
main(void)
{
  struct sched_stats before;
  struct sched_stats after;

  if (readstats(&before) < 0 || before.version != SCHED_STATS_VERSION ||
      before.size != sizeof(before) || (before.online_mask & 1U) == 0) {
    printf("schedstatstest: FAIL (API/version)\n");
    exit(1);
  }

  if (sched_getstats(&before, sizeof(before) - 1) != -1 ||
      sched_getstats(0, sizeof(before)) != -1) {
    printf("schedstatstest: FAIL (bad arguments)\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    printf("schedstatstest: FAIL (fork)\n");
    exit(1);
  }
  if (pid == 0)
    exit(0);

  int status = -1;
  if (wait(&status) != pid || status != 0 || readstats(&after) < 0) {
    printf("schedstatstest: FAIL (wait/snapshot)\n");
    exit(1);
  }

  uint64 new_dispatches = 0;
  for (int cpu = 0; cpu < NCPU; cpu++) {
    if (after.dispatches[cpu] < before.dispatches[cpu] ||
        after.queued[cpu] > NPROC || after.running[cpu] > 1) {
      printf("schedstatstest: FAIL (counter/queue)\n");
      exit(1);
    }
    new_dispatches += after.dispatches[cpu] - before.dispatches[cpu];
  }

  if (new_dispatches == 0) {
    printf("schedstatstest: FAIL (no process dispatch)\n");
    exit(1);
  }

  printf("schedstatstest: PASS\n");
  exit(0);
}
