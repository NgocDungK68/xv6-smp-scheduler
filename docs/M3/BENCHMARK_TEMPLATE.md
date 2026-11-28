# Benchmark result template – Module 3

Fill after integrating API V2 and running tests.

## Environment

| Item | Value |
|------|-------|
| Date | |
| Commit / branch | |
| QEMU CPUS | |
| Host notes | |
| WS/LB verbose | on / off |

## Build & run commands

```bash
make clean
make CPUS=4 qemu
# inside xv6:
schedmeasure
ws_lb_test all
testws 4 1000000
testws 8 2000000
benchmark
scheduler_test 4
```

## Functional checks

| Test | Result | Notes |
|------|--------|-------|
| ws_lb_test free | PASS/FAIL | |
| ws_lb_test pin | PASS/FAIL | |
| ws_lb_test imbalance | PASS/FAIL | WS/LB counters? |
| testws 4 | PASS/FAIL | elapsed |
| scheduler_test | x/7 | |
| usertests -q | PASS/FAIL | |

## Quantitative runs (repeat ≥ 5)

### Scenario A – unrestricted workers (CPUS=4)

| Run | Workers | Iters | Elapsed | WS succ | LB mig | Notes |
|-----|---------|-------|---------|---------|--------|-------|
| 1 | 4 | 1e6 | | | | |
| 2 | | | | | | |
| 3 | | | | | | |
| 4 | | | | | | |
| 5 | | | | | | |
| **mean** | | | | | | |

### Scenario B – pinned CPU0

| Run | Workers | Elapsed | WS succ | LB mig | Notes |
|-----|---------|---------|---------|--------|-------|
| 1 | 4 | | | | expect limited cross-CPU for pinned |
| ... | | | | | |

### Scenario C – imbalance seed

| Run | Elapsed | WS succ | LB mig | LB checks | Notes |
|-----|---------|---------|--------|-----------|-------|
| 1 | | | | | |
| ... | | | | | |

### Throughput (`benchmark`)

| Processes | Time ticks | KOps/sec |
|-----------|------------|----------|
| 1 | | |
| 2 | | |
| 4 | | |
| 8 | | |

## Observations

1. Did WS fire when expected? Evidence (`ws_successes`):
2. Did LB fire when expected? Evidence (`lb_migrations`):
3. Affinity violations observed? (should be none)
4. Comparison vs baseline / earlier week:
5. Limitations of this measurement:

## Conclusion (1 short paragraph)

...
