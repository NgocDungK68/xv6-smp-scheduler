#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define DEFAULT_WORKERS     8
#define DEFAULT_ITERATIONS  30000000
#define DEFAULT_MODE        3
#define DEFAULT_ONLINE_CPUS 4
#define MAX_SOURCE_WORKERS  8
#define ANCHORS_PER_CPU     2
#define MAX_ANCHORS         ((NCPU - 1) * ANCHORS_PER_CPU)
#define MAX_REPORTS         (MAX_SOURCE_WORKERS + MAX_ANCHORS)
#define SAMPLE_INTERVAL     50000U
#define ANCHOR_CAP_FACTOR   8
#define FAST_TICKS          1
#define ANCHOR_SETTLE_TICKS 2
#define SCENARIO_GAP_TICKS  5
#define PIPE_BYTES          512
#define CPU_NONE            0xFF
#define STOP_FILE           "tb_stop"

#define MODE_IDLE_WS     0
#define MODE_PERIODIC_LB 1
#define MODE_AFFINITY    2
#define MODE_COMPARE     3
#define ROLE_SOURCE      0

struct worker_report {
  int pid;
  uint cpu_mask;
  ushort start;
  ushort first_move;
  ushort last_move;
  ushort finish;
  uchar role;
  uchar first_cpu;
  uchar last_cpu;
  uchar migrations;
};

struct ready_report {
  int pid;
  int target_cpu;
  int actual_cpu;
};

struct scenario_result {
  int mode;
  int sources;
  int moved;
  int before_run;
  int fast_moves;
  int lat_count;
  int min_latency;
  int max_latency;
  int sum_latency;
  uint spread_mask;
  int failed;
};

// Hàm tự chế thay thế cho sleep() bị thiếu ở kernel
// Dùng uptime() để chạy vòng lặp chờ, giữ nguyên logic thời gian
static void custom_sleep(int wait_ticks)
{
  int start = uptime();
  while (uptime() - start < wait_ticks) {
    // Chờ chủ động (busy-wait)
  }
}

typedef char report_batch_fits_in_pipe
  [(MAX_REPORTS * sizeof(struct worker_report) <= PIPE_BYTES) ? 1 : -1];

static int read_exact(int fd, void *buffer, int length)
{
  int received = 0;
  char *p = (char *)buffer;

  while (received < length) {
    int n = read(fd, p + received, length - received);
    if (n <= 0)
      return -1;
    received += n;
  }
  return received;
}

static int rel_ticks(ushort tick, int base)
{
  return (int)(ushort)(tick - (ushort)base);
}

static void request_stop(void)
{
  int fd = open(STOP_FILE, O_CREATE | O_WRONLY);

  if (fd >= 0)
    close(fd);
  else
    printf("testbalance: could not create %s; anchors stop at their cap\n",
           STOP_FILE);
}

static int stop_requested(void)
{
  int fd = open(STOP_FILE, O_RDONLY);

  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

static int record_cpu(struct worker_report *report, int *prev_cpu)
{
  int tick = uptime();
  int cpu = getcpu();
  ushort now = (ushort)tick;

  if (cpu < 0 || cpu >= NCPU)
    return tick;

  if (report->first_cpu == CPU_NONE) {
    report->first_cpu = (uchar)cpu;
    report->start = now;
  }
  report->last_cpu = (uchar)cpu;
  report->cpu_mask |= (1U << cpu);

  if (cpu != *prev_cpu) {
    if (report->migrations == 0)
      report->first_move = now;
    report->last_move = now;
    if (report->migrations < 255)
      report->migrations++;
    *prev_cpu = cpu;
  }
  return tick;
}

static void run_worker(int role, int home_cpu, int iterations, int result_fd,
                       int poll_stop)
{
  struct worker_report report;
  volatile uint value = (uint)getpid() + 1U;
  uint next_sample = 0;
  uint limit = (uint)iterations;
  int prev_cpu = home_cpu;
  int last_poll_tick = -1;

  memset(&report, 0, sizeof(report));
  report.pid = getpid();
  report.role = (uchar)role;
  report.first_cpu = CPU_NONE;
  report.last_cpu = CPU_NONE;

  for (uint i = 0; i < limit; i++) {
    value = value * 1664525U + 1013904223U;
    value ^= value >> 13;

    if (i >= next_sample) {
      int tick = record_cpu(&report, &prev_cpu);

      next_sample += SAMPLE_INTERVAL;
      if (poll_stop && tick != last_poll_tick) {
        last_poll_tick = tick;
        if (stop_requested())
          break;
      }
    }
  }

  record_cpu(&report, &prev_cpu);
  report.finish = (ushort)uptime();

  if (write(result_fd, &report, sizeof(report)) != sizeof(report))
    printf("testbalance: worker %d could not write its report\n", report.pid);

  close(result_fd);
  exit(0);
}

static int parse_arg(int argc, char **argv, int index, int fallback)
{
  if (argc <= index)
    return fallback;
  return atoi(argv[index]);
}

static const char *mode_name(int mode)
{
  if (mode == MODE_IDLE_WS)
    return "idle-work-stealing";
  if (mode == MODE_PERIODIC_LB)
    return "periodic-load-balancing";
  if (mode == MODE_AFFINITY)
    return "affinity-pinned";
  return "compare";
}

static void print_latency(const struct scenario_result *r)
{
  if (r->lat_count == 0) {
    printf("n/a");
    return;
  }
  printf("min=%d avg=%d max=%d ticks", r->min_latency,
         r->sum_latency / r->lat_count, r->max_latency);
}

static void account_source(struct scenario_result *res,
                           const struct worker_report *rep, int widen_tick,
                           int source_cpu, uint source_mask)
{
  int before_run = rep->first_cpu != CPU_NONE && rep->first_cpu != source_cpu;

  printf("source pid=%d first_cpu=%d last_cpu=%d moves=%d cpu_mask=0x%x"
         " start=+%d",
         rep->pid, rep->first_cpu, rep->last_cpu, rep->migrations,
         rep->cpu_mask, rel_ticks(rep->start, widen_tick));

  res->spread_mask |= rep->cpu_mask;
  if (before_run)
    res->before_run++;

  if (rep->migrations > 0) {
    int latency = rel_ticks(rep->first_move, widen_tick);

    printf(" first_move=+%d last_move=+%d", latency,
           rel_ticks(rep->last_move, widen_tick));
    res->moved++;
    if (latency <= FAST_TICKS)
      res->fast_moves++;
    if (res->lat_count == 0 || latency < res->min_latency)
      res->min_latency = latency;
    if (res->lat_count == 0 || latency > res->max_latency)
      res->max_latency = latency;
    res->sum_latency += latency;
    res->lat_count++;
  }

  printf(" finish=+%d%s\n", rel_ticks(rep->finish, widen_tick),
         before_run ? " [migrated before first run]" : "");

  (void)source_mask;
}

static int run_scenario(int mode, int workers, int iterations, int online_cpus,
                        int source_cpu, struct scenario_result *res)
{
  uint source_mask = 1U << source_cpu;
  uint all_mask = (1U << online_cpus) - 1U;
  int result_pipe[2];
  int source_start[2];
  int source_pids[MAX_SOURCE_WORKERS];
  int source_launched = 0;
  int anchors_launched = 0;
  int affinity_updates = 0;
  int affinity_update_failures = 0;
  int pin_failures = 0;
  int anchor_placement_failures = 0;
  int affinity_violations = 0;
  int total_children;
  int reports_read = 0;
  int source_reports = 0;
  int children_reaped = 0;
  int stop_sent = 0;
  int expectation_failed = 0;
  int harness_failed;
  int start_ticks;
  int end_ticks;
  int widen_tick;

  memset(res, 0, sizeof(*res));
  res->mode = mode;
  res->min_latency = -1;
  unlink(STOP_FILE);

  if (pipe(result_pipe) < 0) {
    printf("testbalance: result pipe failed\n");
    exit(1);
  }

  printf("\n=== scenario: %s | source_cpu=%d workers=%d iterations=%d"
         " online_cpus=%d ===\n",
         mode_name(mode), source_cpu, workers, iterations, online_cpus);

  start_ticks = uptime();

  if (mode == MODE_PERIODIC_LB) {
    int anchor_start[2];
    int anchor_ready[2];
    int anchor_count = 0;

    if (pipe(anchor_start) < 0 || pipe(anchor_ready) < 0) {
      printf("testbalance: anchor pipe failed\n");
      exit(1);
    }

    for (int cpu = 0; cpu < online_cpus; cpu++) {
      if (cpu == source_cpu)
        continue;

      for (int slot = 0; slot < ANCHORS_PER_CPU; slot++) {
        int pid = fork();

        if (pid < 0) {
          printf("testbalance: anchor fork failed\n");
          continue;
        }

        if (pid == 0) {
          char token;
          struct ready_report ready;

          close(anchor_start[1]);
          close(anchor_ready[0]);
          close(result_pipe[0]);

          if (read(anchor_start[0], &token, 1) != 1)
            exit(1);
          close(anchor_start[0]);

          ready.pid = getpid();
          ready.target_cpu = cpu;
          ready.actual_cpu = getcpu();
          write(anchor_ready[1], &ready, sizeof(ready));
          close(anchor_ready[1]);

          run_worker(cpu + 1, cpu, iterations * ANCHOR_CAP_FACTOR,
                     result_pipe[1], 1);
        }

        anchor_count++;

        if (sched_setaffinity(pid, 1U << cpu) < 0)
          pin_failures++;
      }
    }

    anchors_launched = anchor_count;
    close(anchor_start[0]);
    close(anchor_ready[1]);

    if (anchor_count > 0) {
      char tokens[MAX_ANCHORS];
      for (int i = 0; i < anchor_count; i++)
        tokens[i] = 'A';
      if (write(anchor_start[1], tokens, anchor_count) != anchor_count)
        printf("testbalance: could not release all anchor workers\n");
    }
    close(anchor_start[1]);

    for (int i = 0; i < anchor_count; i++) {
      struct ready_report ready;
      if (read_exact(anchor_ready[0], &ready, sizeof(ready)) != sizeof(ready)) {
        printf("testbalance: missing anchor ready report\n");
        anchor_placement_failures++;
        continue;
      }
      if (ready.actual_cpu != ready.target_cpu)
        anchor_placement_failures++;
    }
    close(anchor_ready[0]);

    // Sử dụng custom_sleep thay cho sleep gốc của Kernel
    custom_sleep(ANCHOR_SETTLE_TICKS);
  }

  if (pipe(source_start) < 0) {
    printf("testbalance: source start pipe failed\n");
    if (mode == MODE_PERIODIC_LB)
      request_stop();
    exit(1);
  }

  for (int i = 0; i < workers; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("testbalance: source worker fork failed\n");
      break;
    }

    if (pid == 0) {
      char token;
      close(source_start[1]);
      close(result_pipe[0]);
      if (read(source_start[0], &token, 1) != 1)
        exit(1);
      close(source_start[0]);
      run_worker(ROLE_SOURCE, source_cpu, iterations, result_pipe[1], 0);
    }

    if (sched_setaffinity(pid, source_mask) < 0)
      pin_failures++;
    source_pids[source_launched++] = pid;
  }

  close(source_start[0]);
  if (source_launched > 0) {
    char tokens[MAX_SOURCE_WORKERS];
    for (int i = 0; i < source_launched; i++)
      tokens[i] = 'S';
    if (write(source_start[1], tokens, source_launched) != source_launched)
      printf("testbalance: could not release all source workers\n");
  }
  close(source_start[1]);

  widen_tick = uptime();

  if (mode != MODE_AFFINITY) {
    for (int i = 0; i < source_launched; i++) {
      if (sched_setaffinity(source_pids[i], all_mask) == 0)
        affinity_updates++;
      else
        affinity_update_failures++;
    }
  }

  close(result_pipe[1]);
  total_children = anchors_launched + source_launched;
  res->sources = source_launched;

  if (mode == MODE_PERIODIC_LB && source_launched == 0) {
    request_stop();
    stop_sent = 1;
  }

  printf("setup: widen_tick=%d (absolute) anchors=%d source_workers=%d"
         " affinity_updates=%d update_failures=%d pin_failures=%d\n",
         widen_tick, anchors_launched, source_launched, affinity_updates,
         affinity_update_failures, pin_failures);
  if (mode == MODE_PERIODIC_LB)
    printf("setup: every non-source CPU holds %d pinned anchors until all"
           " sources finish (no idle CPU)\n",
           ANCHORS_PER_CPU);
  else if (mode == MODE_IDLE_WS)
    printf("setup: all non-source CPUs are idle while the source CPU is"
           " loaded\n");

  printf("\nWorker CPU observations (+N = ticks after widening; cpu_mask"
         " = CPUs sampled by getcpu):\n");
  for (int i = 0; i < total_children; i++) {
    struct worker_report rep;

    if (read_exact(result_pipe[0], &rep, sizeof(rep)) != sizeof(rep)) {
      printf("testbalance: missing worker result %d/%d\n", i + 1,
             total_children);
      break;
    }
    reports_read++;

    if (rep.role == ROLE_SOURCE) {
      account_source(res, &rep, widen_tick, source_cpu, source_mask);
      if (mode == MODE_AFFINITY && rep.cpu_mask != source_mask)
        affinity_violations++;

      source_reports++;
      if (mode == MODE_PERIODIC_LB && !stop_sent &&
          source_reports == source_launched) {
        request_stop();
        stop_sent = 1;
      }
    } else {
      int target_cpu = rep.role - 1;
      printf("anchor target_cpu=%d pid=%d first_cpu=%d last_cpu=%d"
             " cpu_mask=0x%x finish=+%d\n",
             target_cpu, rep.pid, rep.first_cpu, rep.last_cpu, rep.cpu_mask,
             rel_ticks(rep.finish, widen_tick));
      if (rep.cpu_mask != (1U << target_cpu))
        affinity_violations++;
    }
  }
  close(result_pipe[0]);

  if (mode == MODE_PERIODIC_LB && !stop_sent)
    request_stop();

  while (children_reaped < total_children) {
    if (wait(0) < 0)
      break;
    children_reaped++;
  }

  unlink(STOP_FILE);
  end_ticks = uptime();

  printf("\n--- %s summary ---\n", mode_name(mode));
  printf("worker reports received: %d/%d\n", reports_read, total_children);
  printf("children reaped: %d/%d\n", children_reaped, total_children);
  printf("source workers that changed CPU: %d/%d\n", res->moved,
         source_launched);
  printf("  migrated before their first run: %d\n", res->before_run);
  printf("  first move within %d tick(s) of widening: %d\n", FAST_TICKS,
         res->fast_moves);
  printf("  first-move latency: ");
  print_latency(res);
  printf("\n");
  printf("CPUs used by source workers: 0x%x\n", res->spread_mask);
  printf("affinity violations observed: %d\n", affinity_violations);
  if (mode == MODE_PERIODIC_LB)
    printf("anchor placement mismatches: %d\n", anchor_placement_failures);
  printf("elapsed ticks (diagnostic only): %d\n", end_ticks - start_ticks);

  if (mode == MODE_AFFINITY) {
    printf("expected: every source cpu_mask equals 0x%x; idle WS and"
           " periodic LB must both respect affinity\n",
           source_mask);
    printf("RESULT: %s\n", affinity_violations == 0 ? "PASS" : "FAIL");
  } else if (mode == MODE_IDLE_WS) {
    if (res->moved == 0) {
      printf("RESULT: FAIL - nothing migrated although CPUs were idle: idle"
             " WS is off/broken or the widened mask is ignored\n");
      expectation_failed = 1;
    } else if (res->min_latency <= FAST_TICKS) {
      printf("RESULT: PASS - first migration %d tick(s) after widening with"
             " idle CPUs available: event-triggered, consistent with idle"
             " WS\n",
             res->min_latency);
    } else {
      printf("RESULT: WARN - first migration only %d ticks after widening"
             " despite idle CPUs: looks time-triggered (LB); idle WS did not"
             " react promptly\n",
             res->min_latency);
    }
  } else {
    if (res->moved == 0) {
      printf("RESULT: FAIL - no source worker migrated within this run: LB"
             " did not fire (increase iterations) or is not implemented\n");
      expectation_failed = 1;
    } else {
      printf("RESULT: PASS - %d source worker(s) migrated while every"
             " non-source CPU stayed busy, so idle WS was not eligible:"
             " attributable to periodic LB (first move after %d ticks)\n",
             res->moved, res->min_latency);
      if (res->min_latency <= FAST_TICKS)
        printf("NOTE: first move was within %d tick(s); either the LB period"
               " is that short or something other than LB moved work -"
               " check the kernel log\n",
               FAST_TICKS);
    }
  }

  harness_failed =
    !(affinity_violations == 0 && anchor_placement_failures == 0 &&
      pin_failures == 0 && affinity_update_failures == 0 &&
      source_launched == workers &&
      (mode != MODE_PERIODIC_LB ||
       anchors_launched == (online_cpus - 1) * ANCHORS_PER_CPU) &&
      reports_read == total_children && children_reaped == total_children);

  res->failed = harness_failed || expectation_failed;
  return res->failed;
}

static void print_comparison(const struct scenario_result *ws,
                             const struct scenario_result *lb)
{
  printf("\n=== WS vs LB comparison (ticks are after affinity widening)"
         " ===\n");
  printf("idle CPU available during window: WS=yes LB=no\n");
  printf("sources that moved:               WS=%d/%d LB=%d/%d\n", ws->moved,
         ws->sources, lb->moved, lb->sources);
  printf("migrated before first run:        WS=%d LB=%d\n", ws->before_run,
         lb->before_run);
  printf("moves within %d tick(s):           WS=%d LB=%d\n", FAST_TICKS,
         ws->fast_moves, lb->fast_moves);
  printf("first-move latency WS: ");
  print_latency(ws);
  printf("\nfirst-move latency LB: ");
  print_latency(lb);
  printf("\n");

  if (ws->moved == 0 || lb->moved == 0) {
    printf("VERDICT: INCONCLUSIVE - %s showed no migration, so there is"
           " nothing to compare\n",
           ws->moved == 0 ? "idle-ws" : "periodic-lb");
  } else if (ws->min_latency < lb->min_latency) {
    printf("VERDICT: DISTINGUISHED - migration with an idle CPU came %d"
           " tick(s) before migration with no idle CPU: WS is"
           " event-triggered, LB is time-triggered (LB period is about %d..%d"
           " ticks)\n",
           lb->min_latency - ws->min_latency, lb->min_latency, lb->max_latency);
  } else if (ws->min_latency == lb->min_latency) {
    printf("VERDICT: NOT DISTINGUISHED BY TIMING - both reacted after %d"
           " tick(s). The LB period may be <= the tick resolution, or WS"
           " also acts without idleness. Compare kernel logs by pid.\n",
           ws->min_latency);
  } else {
    printf("VERDICT: ANOMALY - migration without idle CPUs (%d ticks) came"
           " BEFORE migration with idle CPUs (%d ticks): idle WS is slower"
           " than LB or WS steals from non-idle CPUs\n",
           lb->min_latency, ws->min_latency);
  }
  printf("Attribution rule: mode 1 moves can only be LB (no idle CPU); mode"
         " 0 moves faster than the LB period can only be WS.\n");
}

int main(int argc, char **argv)
{
  int workers = parse_arg(argc, argv, 1, DEFAULT_WORKERS);
  int iterations = parse_arg(argc, argv, 2, DEFAULT_ITERATIONS);
  int mode = parse_arg(argc, argv, 3, DEFAULT_MODE);
  int online_cpus = parse_arg(argc, argv, 4, DEFAULT_ONLINE_CPUS);
  int source_cpu;
  struct scenario_result ws;
  struct scenario_result lb;
  int status;

  if (workers < 1 || workers > MAX_SOURCE_WORKERS || iterations < 1000000 ||
      iterations > 100000000 || mode < MODE_IDLE_WS || mode > MODE_COMPARE ||
      online_cpus < 1 || online_cpus > NCPU) {
    printf("usage: testbalance [workers:1-%d] [iterations:1000000-100000000]"
           " [mode:0-3] [online_cpus:1-%d]\n",
           MAX_SOURCE_WORKERS, NCPU);
    printf("mode 0=idle-ws, 1=periodic-lb, 2=affinity-pinned, 3=compare"
           " (default)\n");
    printf("use workers >= 2 so at least one worker is queued behind a"
           " running one\n");
    exit(1);
  }

  if ((mode == MODE_PERIODIC_LB || mode == MODE_COMPARE) && online_cpus < 2) {
    printf("testbalance: periodic-LB scenario requires CPUS >= 2\n");
    exit(1);
  }

  source_cpu = getcpu();
  if (source_cpu < 0 || source_cpu >= online_cpus) {
    printf(
      "testbalance: current CPU %d is outside online_cpus=%d; pass the actual CPUS value\n",
      source_cpu, online_cpus);
    exit(1);
  }

  if (sched_setaffinity(0, 1U << source_cpu) < 0) {
    printf("testbalance: cannot pin parent to CPU %d\n", source_cpu);
    exit(1);
  }

  if (mode == MODE_COMPARE) {
    status = run_scenario(MODE_IDLE_WS, workers, iterations, online_cpus,
                          source_cpu, &ws);

    // Sử dụng custom_sleep thay thế
    custom_sleep(SCENARIO_GAP_TICKS);

    status |= run_scenario(MODE_PERIODIC_LB, workers, iterations, online_cpus,
                           source_cpu, &lb);
    print_comparison(&ws, &lb);
    exit(status != 0);
  }

  status =
    run_scenario(mode, workers, iterations, online_cpus, source_cpu, &ws);
  exit(status != 0);
}
