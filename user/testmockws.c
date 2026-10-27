/*
 * testmockws.c - Work Stealing algorithm test with Mock Queues inside xv6 user space.
 *
 * IMPORTANT: This tests the algorithm's control flow only. The queues below are
 * simulated user-space data structures; they are NOT the kernel scheduler's
 * real per-CPU run queues and do not move real xv6 processes between CPUs.
 */
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MOCK_MAX_CPUS 4
#define MOCK_MAX_TASKS 32

struct mock_task {
  int id;
  int work_units;
};

struct mock_queue {
  struct mock_task items[MOCK_MAX_TASKS];
  int count;
};

struct mock_system {
  int ncpu;
  struct mock_queue queues[MOCK_MAX_CPUS];
  int steal_attempts;
  int steals_success;
  int steals_failed;
};

struct worksteal_state {
  int next_victim;
};

/* Static storage avoids using most of the one-page xv6 user stack. */
static struct mock_system system_state;
static struct worksteal_state states[MOCK_MAX_CPUS];

static int
valid_cpu(const struct mock_system *system, int cpu_id)
{
  return system != 0 && cpu_id >= 0 && cpu_id < system->ncpu &&
         cpu_id < MOCK_MAX_CPUS;
}

static void
mock_system_init(struct mock_system *system, int ncpu)
{
  int cpu;
  int i;

  if (system == 0)
    return;
  if (ncpu < 0)
    ncpu = 0;
  if (ncpu > MOCK_MAX_CPUS)
    ncpu = MOCK_MAX_CPUS;

  system->ncpu = ncpu;
  system->steal_attempts = 0;
  system->steals_success = 0;
  system->steals_failed = 0;
  for (cpu = 0; cpu < MOCK_MAX_CPUS; cpu++) {
    system->queues[cpu].count = 0;
    for (i = 0; i < MOCK_MAX_TASKS; i++) {
      system->queues[cpu].items[i].id = 0;
      system->queues[cpu].items[i].work_units = 0;
    }
  }
}

static void
worksteal_init(struct worksteal_state *state, int cpu_id, int ncpu)
{
  if (state == 0)
    return;
  if (ncpu <= 0 || cpu_id < 0 || cpu_id >= ncpu) {
    state->next_victim = 0;
    return;
  }
  state->next_victim = (cpu_id + 1) % ncpu;
}

static void
init_states(struct worksteal_state *all_states, int ncpu)
{
  int cpu;
  for (cpu = 0; cpu < MOCK_MAX_CPUS; cpu++)
    worksteal_init(&all_states[cpu], cpu, ncpu);
}

static int
mock_queue_len(const struct mock_system *system, int cpu_id)
{
  if (!valid_cpu(system, cpu_id))
    return -1;
  return system->queues[cpu_id].count;
}

static int
mock_queue_enqueue(struct mock_system *system, int cpu_id,
                   struct mock_task task)
{
  struct mock_queue *queue;
  if (!valid_cpu(system, cpu_id))
    return 0;
  queue = &system->queues[cpu_id];
  if (queue->count >= MOCK_MAX_TASKS)
    return 0;
  queue->items[queue->count++] = task;
  return 1;
}

static int
mock_queue_dequeue(struct mock_system *system, int cpu_id,
                   struct mock_task *task_out)
{
  struct mock_queue *queue;
  int i;
  if (!valid_cpu(system, cpu_id) || task_out == 0)
    return 0;
  queue = &system->queues[cpu_id];
  if (queue->count == 0)
    return 0;
  *task_out = queue->items[0];
  for (i = 1; i < queue->count; i++)
    queue->items[i - 1] = queue->items[i];
  queue->count--;
  return 1;
}

static int
mock_total_queued(const struct mock_system *system)
{
  int cpu;
  int total = 0;
  if (system == 0)
    return -1;
  for (cpu = 0; cpu < system->ncpu; cpu++)
    total += system->queues[cpu].count;
  return total;
}

/* Simulates the atomic queue transfer; no real concurrency or kernel locks. */
static int
mock_steal_attempt(int thief_cpu, int victim_cpu, void *ctx)
{
  struct mock_system *system = (struct mock_system *)ctx;
  struct mock_task task;

  if (system == 0 || !valid_cpu(system, thief_cpu) ||
      !valid_cpu(system, victim_cpu) || thief_cpu == victim_cpu)
    return 0;

  system->steal_attempts++;
  if (system->queues[victim_cpu].count == 0 ||
      system->queues[thief_cpu].count != 0 ||
      system->queues[thief_cpu].count >= MOCK_MAX_TASKS) {
    system->steals_failed++;
    return 0;
  }

  if (!mock_queue_dequeue(system, victim_cpu, &task) ||
      !mock_queue_enqueue(system, thief_cpu, task)) {
    system->steals_failed++;
    return 0;
  }
  system->steals_success++;
  return 1;
}

static int
mock_worksteal(struct mock_system *system,
               struct worksteal_state *all_states,
               int thief_cpu, int *victim_out)
{
  int start;
  int i;
  int victim;

  if (victim_out != 0)
    *victim_out = -1;
  if (system == 0 || all_states == 0 || !valid_cpu(system, thief_cpu))
    return 0;
  if (system->ncpu < 2 || system->queues[thief_cpu].count != 0)
    return 0;

  start = all_states[thief_cpu].next_victim;
  if (start < 0 || start >= system->ncpu)
    start = (thief_cpu + 1) % system->ncpu;

  for (i = 0; i < system->ncpu; i++) {
    victim = (start + i) % system->ncpu;
    if (victim == thief_cpu)
      continue;
    if (mock_steal_attempt(thief_cpu, victim, system)) {
      all_states[thief_cpu].next_victim = (victim + 1) % system->ncpu;
      if (victim_out != 0)
        *victim_out = victim;
      return 1;
    }
  }

  /* Rotate the search start even when every attempt fails. */
  all_states[thief_cpu].next_victim = (start + 1) % system->ncpu;
  return 0;
}

static int
test_check(int condition, const char *name, const char *reason)
{
  if (!condition) {
    printf("FAIL: %s - %s\n", name, reason);
    return 0;
  }
  return 1;
}

static int
test_empty_queues(void)
{
  int victim = 77;
  mock_system_init(&system_state, MOCK_MAX_CPUS);
  init_states(states, MOCK_MAX_CPUS);
  if (!test_check(mock_worksteal(&system_state, states, 0, &victim) == 0,
                  "empty queues", "stealing should fail")) return 0;
  if (!test_check(victim == -1, "empty queues", "victim should be -1")) return 0;
  if (!test_check(system_state.steal_attempts == 3,
                  "empty queues", "must try each other CPU once")) return 0;
  if (!test_check(mock_total_queued(&system_state) == 0,
                  "empty queues", "queue contents changed")) return 0;
  printf("PASS: empty queues\n");
  return 1;
}

static int
test_imbalanced_queue(void)
{
  struct mock_task task;
  int victim = -1;
  int i;

  mock_system_init(&system_state, MOCK_MAX_CPUS);
  init_states(states, MOCK_MAX_CPUS);
  for (i = 0; i < 3; i++) {
    task.id = 100 + i;
    task.work_units = 5 + i;
    if (!mock_queue_enqueue(&system_state, 2, task))
      return test_check(0, "imbalanced queue", "could not set up queue");
  }
  if (!test_check(mock_worksteal(&system_state, states, 0, &victim) == 1,
                  "imbalanced queue", "CPU 0 should steal a task")) return 0;
  if (!test_check(victim == 2, "imbalanced queue", "CPU 2 should be victim")) return 0;
  if (!test_check(mock_queue_len(&system_state, 0) == 1 &&
                  mock_queue_len(&system_state, 2) == 2,
                  "imbalanced queue", "wrong queue sizes after steal")) return 0;
  if (!test_check(mock_total_queued(&system_state) == 3,
                  "imbalanced queue", "total task count must be conserved")) return 0;
  printf("PASS: imbalanced queue\n");
  return 1;
}

static int
test_invalid_target(void)
{
  int victim = 123;
  mock_system_init(&system_state, MOCK_MAX_CPUS);
  init_states(states, MOCK_MAX_CPUS);
  if (!test_check(mock_worksteal(&system_state, states, -1, &victim) == 0,
                  "invalid target", "negative CPU id accepted")) return 0;
  if (!test_check(mock_worksteal(&system_state, states, MOCK_MAX_CPUS, &victim) == 0,
                  "invalid target", "out-of-range CPU id accepted")) return 0;
  if (!test_check(system_state.steal_attempts == 0,
                  "invalid target", "invalid target started stealing")) return 0;
  printf("PASS: invalid target CPU\n");
  return 1;
}

static int
test_nonempty_target(void)
{
  struct mock_task task;
  int victim = 88;
  mock_system_init(&system_state, MOCK_MAX_CPUS);
  init_states(states, MOCK_MAX_CPUS);
  task.id = 1; task.work_units = 3;
  if (!mock_queue_enqueue(&system_state, 0, task)) return 0;
  task.id = 2;
  if (!mock_queue_enqueue(&system_state, 2, task)) return 0;
  if (!test_check(mock_worksteal(&system_state, states, 0, &victim) == 0,
                  "nonempty target", "should not steal when local queue has work")) return 0;
  if (!test_check(victim == -1 && system_state.steal_attempts == 0,
                  "nonempty target", "unexpected steal attempt")) return 0;
  printf("PASS: non-empty target queue\n");
  return 1;
}

static int
test_invalid_source(void)
{
  mock_system_init(&system_state, MOCK_MAX_CPUS);
  if (!test_check(mock_steal_attempt(0, MOCK_MAX_CPUS, &system_state) == 0,
                  "invalid source", "out-of-range source accepted")) return 0;
  if (!test_check(system_state.steal_attempts == 0,
                  "invalid source", "invalid source counted as attempt")) return 0;
  printf("PASS: invalid source CPU\n");
  return 1;
}

static int
test_search_rotation(void)
{
  int first_start;
  mock_system_init(&system_state, MOCK_MAX_CPUS);
  init_states(states, MOCK_MAX_CPUS);
  first_start = states[0].next_victim;
  if (!test_check(mock_worksteal(&system_state, states, 0, 0) == 0,
                  "search rotation", "empty queues should fail")) return 0;
  if (!test_check(states[0].next_victim == (first_start + 1) % MOCK_MAX_CPUS,
                  "search rotation", "search start did not advance after failures")) return 0;
  printf("PASS: round-robin search rotation\n");
  return 1;
}

int
main(void)
{
  int passed = 0;
  int total = 6;

  printf("Work Stealing Mock Queue tests (inside xv6 user space)\n");
  passed += test_empty_queues();
  passed += test_imbalanced_queue();
  passed += test_invalid_target();
  passed += test_nonempty_target();
  passed += test_invalid_source();
  passed += test_search_rotation();
  printf("Result: %d/%d tests passed\n", passed, total);
  if (passed == total)
    printf("All Mock Queue tests passed.\n");
  exit(passed == total ? 0 : 1);
}
