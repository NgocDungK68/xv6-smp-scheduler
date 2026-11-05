/*
 * testmockws.c - Standalone Mock Queue test for xv6 user space.
 *
 * Run inside the xv6 shell:
 *   $ testmockws
 *
 * This single file contains:
 *   - Mock per-CPU queues (4 simulated CPUs)
 *   - Idle Work Stealing victim selection
 *   - CPU Affinity filtering
 *   - Mock task transfer
 *   - Functional tests
 *
 * IMPORTANT:
 * This program simulates queues in user memory. It does NOT access kernel
 * Run Queues, execute tasks on selected CPUs, or modify the xv6 scheduler.
 */
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MOCK_MAX_CPUS 4
#define MOCK_MAX_TASKS 32
#define ALL_CPU_MASK ((1U << MOCK_MAX_CPUS) - 1U)

struct mock_task {
  int id;
  int work_units;
  uint affinity_mask;
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

/* Round-robin cursor used to break ties between equally loaded victims. */
struct worksteal_state {
  int next_victim;
};

/* Static storage avoids putting large queue arrays on the small xv6 stack. */
static struct mock_system system_state;
static struct worksteal_state ws_states[MOCK_MAX_CPUS];
static int tests_passed;
static int tests_total;

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
      system->queues[cpu].items[i].affinity_mask = 0;
    }
  }
}

static void
worksteal_init(struct worksteal_state *state, int cpu_id, int ncpu)
{
  if (state == 0)
    return;
  if (ncpu < 1 || ncpu > MOCK_MAX_CPUS ||
      cpu_id < 0 || cpu_id >= ncpu) {
    state->next_victim = 0;
    return;
  }
  state->next_victim = (cpu_id + 1) % ncpu;
}

static void
init_all_worksteal_states(void)
{
  int cpu;
  for (cpu = 0; cpu < MOCK_MAX_CPUS; cpu++)
    worksteal_init(&ws_states[cpu], cpu, MOCK_MAX_CPUS);
}

static void
reset_test(void)
{
  mock_system_init(&system_state, MOCK_MAX_CPUS);
  init_all_worksteal_states();
}

static struct mock_task
make_task(int id, uint affinity_mask)
{
  struct mock_task task;
  task.id = id;
  task.work_units = 5;
  task.affinity_mask = affinity_mask;
  return task;
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

  queue->items[queue->count] = task;
  queue->count++;
  return 1;
}

static int
mock_queue_remove_at(struct mock_system *system, int cpu_id, int index,
                     struct mock_task *task_out)
{
  struct mock_queue *queue;
  int i;

  if (!valid_cpu(system, cpu_id) || task_out == 0)
    return 0;
  queue = &system->queues[cpu_id];
  if (index < 0 || index >= queue->count)
    return 0;

  *task_out = queue->items[index];
  for (i = index + 1; i < queue->count; i++)
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

static int
mock_count_task_id(const struct mock_system *system, int task_id)
{
  int cpu;
  int i;
  int count = 0;

  if (system == 0)
    return -1;
  for (cpu = 0; cpu < system->ncpu; cpu++) {
    for (i = 0; i < system->queues[cpu].count; i++) {
      if (system->queues[cpu].items[i].id == task_id)
        count++;
    }
  }
  return count;
}

/* A task may be stolen only if the destination CPU is in its affinity mask. */
static int
mock_task_allowed_on(const struct mock_task *task, int cpu_id)
{
  if (task == 0 || cpu_id < 0 || cpu_id >= MOCK_MAX_CPUS)
    return 0;
  return (task->affinity_mask & (1U << cpu_id)) != 0;
}

/* Count tasks in a source queue that are eligible for the destination CPU. */
static int
mock_eligible_count(int thief_cpu, int victim_cpu,
                    const struct mock_system *system)
{
  int i;
  int count = 0;

  if (!valid_cpu(system, thief_cpu) || !valid_cpu(system, victim_cpu) ||
      thief_cpu == victim_cpu)
    return 0;

  for (i = 0; i < system->queues[victim_cpu].count; i++) {
    if (mock_task_allowed_on(&system->queues[victim_cpu].items[i], thief_cpu))
      count++;
  }
  return count;
}

/*
 * Move one eligible task. In this mock implementation the operation is
 * sequential; a real kernel implementation must protect source/destination
 * queues and process state with the agreed locking protocol.
 */
static int
mock_steal_one(int thief_cpu, int victim_cpu, struct mock_system *system)
{
  struct mock_task task;
  int i;
  int eligible_index = -1;

  if (!valid_cpu(system, thief_cpu) || !valid_cpu(system, victim_cpu) ||
      thief_cpu == victim_cpu)
    return 0;

  system->steal_attempts++;

  if (system->queues[thief_cpu].count != 0 ||
      system->queues[thief_cpu].count >= MOCK_MAX_TASKS) {
    system->steals_failed++;
    return 0;
  }

  for (i = 0; i < system->queues[victim_cpu].count; i++) {
    if (mock_task_allowed_on(&system->queues[victim_cpu].items[i], thief_cpu)) {
      eligible_index = i;
      break;
    }
  }

  if (eligible_index < 0 ||
      !mock_queue_remove_at(system, victim_cpu, eligible_index, &task)) {
    system->steals_failed++;
    return 0;
  }

  if (!mock_queue_enqueue(system, thief_cpu, task)) {
    /* Restore the task if the destination cannot accept it. */
    (void)mock_queue_enqueue(system, victim_cpu, task);
    system->steals_failed++;
    return 0;
  }

  system->steals_success++;
  return 1;
}

/*
 * Idle Work Stealing:
 * - Run only if the destination queue is empty.
 * - Prefer the source with the most affinity-compatible tasks.
 * - Break equal-load ties using a per-CPU round-robin cursor.
 * - Try each other CPU at most once.
 */
static int
mock_worksteal(struct mock_system *system, int thief_cpu, int *victim_out)
{
  int tried[MOCK_MAX_CPUS];
  int i;
  int offset;
  int start;
  int victim;
  int best_victim;
  int load;
  int best_load;

  if (victim_out != 0)
    *victim_out = -1;
  if (!valid_cpu(system, thief_cpu))
    return 0;
  if (system->ncpu < 2 || system->queues[thief_cpu].count != 0)
    return 0;

  start = ws_states[thief_cpu].next_victim;
  if (start < 0 || start >= system->ncpu)
    start = (thief_cpu + 1) % system->ncpu;

  for (i = 0; i < MOCK_MAX_CPUS; i++)
    tried[i] = 0;

  for (i = 0; i < system->ncpu - 1; i++) {
    best_victim = -1;
    best_load = 0;

    /* Find the remaining CPU with the largest eligible-task count. */
    for (offset = 0; offset < system->ncpu; offset++) {
      victim = (start + offset) % system->ncpu;
      if (victim == thief_cpu || tried[victim])
        continue;

      load = mock_eligible_count(thief_cpu, victim, system);
      if (load > best_load) {
        best_load = load;
        best_victim = victim;
      }
    }

    if (best_victim < 0)
      break;

    tried[best_victim] = 1;
    if (mock_steal_one(thief_cpu, best_victim, system)) {
      ws_states[thief_cpu].next_victim = (best_victim + 1) % system->ncpu;
      if (victim_out != 0)
        *victim_out = best_victim;
      return 1;
    }
  }

  /* Rotate the starting point even when no eligible task was found. */
  ws_states[thief_cpu].next_victim = (start + 1) % system->ncpu;
  return 0;
}

static void
check(int condition, const char *name)
{
  tests_total++;
  if (condition) {
    tests_passed++;
    printf("PASS: %s\n", name);
  } else {
    printf("FAIL: %s\n", name);
  }
}

static void
test_empty_queues(void)
{
  int victim = 99;
  reset_test();
  check(mock_worksteal(&system_state, 0, &victim) == 0 &&
        victim == -1 && mock_total_queued(&system_state) == 0,
        "empty queues");
}

static void
test_choose_most_loaded_eligible_victim(void)
{
  int i;
  int victim = -1;
  reset_test();
  (void)mock_queue_enqueue(&system_state, 1,
                           make_task(10, ALL_CPU_MASK));
  for (i = 0; i < 3; i++)
    (void)mock_queue_enqueue(&system_state, 2,
                             make_task(20 + i, ALL_CPU_MASK));

  check(mock_worksteal(&system_state, 0, &victim) == 1 &&
        victim == 2 && mock_queue_len(&system_state, 0) == 1 &&
        mock_queue_len(&system_state, 2) == 2 &&
        mock_total_queued(&system_state) == 4,
        "choose most-loaded eligible victim");
}

static void
test_affinity_filter_and_task_integrity(void)
{
  int victim = -1;
  struct mock_task cpu2_only;
  struct mock_task cpu0_allowed;

  reset_test();
  cpu2_only = make_task(31, 1U << 2);
  cpu0_allowed = make_task(32, 1U << 0);
  (void)mock_queue_enqueue(&system_state, 2, cpu2_only);
  (void)mock_queue_enqueue(&system_state, 2, cpu0_allowed);

  check(mock_worksteal(&system_state, 0, &victim) == 1 &&
        victim == 2 && mock_queue_len(&system_state, 0) == 1 &&
        mock_queue_len(&system_state, 2) == 1 &&
        system_state.queues[0].items[0].id == 32 &&
        system_state.queues[2].items[0].id == 31 &&
        mock_count_task_id(&system_state, 31) == 1 &&
        mock_count_task_id(&system_state, 32) == 1 &&
        mock_total_queued(&system_state) == 2,
        "affinity filtering and task integrity");
}

static void
test_no_affinity_compatible_task(void)
{
  int victim = 77;
  reset_test();
  (void)mock_queue_enqueue(&system_state, 2,
                           make_task(40, 1U << 2));

  check(mock_worksteal(&system_state, 0, &victim) == 0 &&
        victim == -1 && mock_queue_len(&system_state, 2) == 1 &&
        mock_queue_len(&system_state, 0) == 0 &&
        system_state.steal_attempts == 0 &&
        mock_total_queued(&system_state) == 1,
        "no affinity-compatible task");
}

static void
test_zero_affinity_mask(void)
{
  int victim = -1;
  reset_test();
  (void)mock_queue_enqueue(&system_state, 1, make_task(50, 0));

  check(mock_worksteal(&system_state, 0, &victim) == 0 &&
        mock_queue_len(&system_state, 1) == 1 &&
        mock_queue_len(&system_state, 0) == 0,
        "zero affinity mask");
}

static void
test_invalid_destination_cpu(void)
{
  int victim = 123;
  reset_test();
  check(mock_worksteal(&system_state, -1, &victim) == 0 &&
        victim == -1 &&
        mock_worksteal(&system_state, MOCK_MAX_CPUS, &victim) == 0 &&
        system_state.steal_attempts == 0,
        "invalid destination CPU");
}

static void
test_nonempty_destination(void)
{
  int victim = -1;
  reset_test();
  (void)mock_queue_enqueue(&system_state, 0,
                           make_task(60, ALL_CPU_MASK));
  (void)mock_queue_enqueue(&system_state, 2,
                           make_task(61, ALL_CPU_MASK));

  check(mock_worksteal(&system_state, 0, &victim) == 0 &&
        victim == -1 && system_state.steal_attempts == 0 &&
        mock_total_queued(&system_state) == 2,
        "non-empty destination queue");
}

static void
test_invalid_source_cpu(void)
{
  reset_test();
  check(mock_steal_one(0, MOCK_MAX_CPUS, &system_state) == 0 &&
        system_state.steal_attempts == 0,
        "invalid source CPU");
}

static void
test_round_robin_rotation(void)
{
  int first_start;
  reset_test();
  first_start = ws_states[0].next_victim;
  (void)mock_worksteal(&system_state, 0, 0);
  check(ws_states[0].next_victim == (first_start + 1) % MOCK_MAX_CPUS,
        "round-robin search rotation");
}

int
main(void)
{
  tests_passed = 0;
  tests_total = 0;

  printf("Work Stealing + CPU Affinity Mock Queue tests\n");
  printf("Simulated CPUs: %d\n\n", MOCK_MAX_CPUS);

  test_empty_queues();
  test_choose_most_loaded_eligible_victim();
  test_affinity_filter_and_task_integrity();
  test_no_affinity_compatible_task();
  test_zero_affinity_mask();
  test_invalid_destination_cpu();
  test_nonempty_destination();
  test_invalid_source_cpu();
  test_round_robin_rotation();

  printf("\nResult: %d/%d tests passed\n", tests_passed, tests_total);
  if (tests_passed == tests_total) {
    printf("All Mock Work Stealing tests passed.\n");
    exit(0);
  }

  printf("Some Mock Work Stealing tests failed.\n");
  exit(1);
}
