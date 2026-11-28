# Schedstats API V2

**Module:** 3 – Work Stealing & Load Balancing  
**Status:** Specification for unified measurement API  
**ABI version:** 2

## 1. Mục tiêu

Cung cấp một interface thống nhất để user-space đo:

- Work Stealing (WS)
- Load Balancing (LB)
- (tuỳ chọn) độ dài Run Queue theo CPU

Tất cả chương trình benchmark / test WS-LB / scheduling contention **phải** dùng API này thay vì chỉ dựa vào `getcpu()` sample hoặc `printf` kernel log.

## 2. Data structure (ABI V2)

Layout cố định, copyout sang user. Field là counter tăng dần (monotonic) cho đến khi `sched_resetstats()`.

```c
#define SCHEDSTATS_ABI_V2  2
#define SCHEDSTATS_NCPU    8   /* must match NCPU used at build time */

struct sched_stats_v2 {
  uint abi;                 /* always SCHEDSTATS_ABI_V2 */

  /* ---- Work Stealing (aggregated over all CPUs) ---- */
  uint ws_searches;         /* worksteal_try entered with empty local RQ */
  uint ws_attempts;         /* actual steal attempts under p->lock */
  uint ws_successes;        /* RQ_STEAL_OK */
  uint ws_failed_searches;  /* searches that stole nothing */
  uint ws_affinity_skips;   /* candidates rejected by affinity */
  uint ws_stale;            /* stale / ineligible during steal */

  /* ---- Load Balancing (global coordinator) ---- */
  uint lb_checks;           /* loadbalance_run passed rate limit */
  uint lb_migrations;       /* successful rq_transfer count */
  uint lb_below_threshold;  /* no pair with diff >= threshold */
  uint lb_no_eligible;      /* imbalance seen but no eligible process */
  uint lb_affinity_skips;
  uint lb_stale;
  uint lb_rate_limited;     /* skipped because interval not elapsed */

  /* ---- Optional snapshot (best-effort, not locked vs migration) ---- */
  uint rq_len[SCHEDSTATS_NCPU];  /* rq_length(cpu) at sample time */
  uint online_mask;              /* sched_online_mask() snapshot */
};
```

**Ghi chú kích thước:** Với `NCPU=8`, struct khoảng `14*4 + 8*4 + 4 = 92` bytes. User phải allocate đủ và zero trước khi gọi nếu muốn detect field mới sau này.

## 3. System calls

| Number (đề xuất) | Name | Prototype | Return |
|------------------|------|-----------|--------|
| 25 | `sched_getstats` | `int sched_getstats(struct sched_stats_v2 *out)` | 0 OK, -1 error |
| 26 | `sched_resetstats` | `int sched_resetstats(void)` | 0 OK, -1 error |

### 3.1 `sched_getstats`

1. Validate user pointer.
2. Aggregate per-CPU `worksteal_stats` → WS fields.
3. Copy `loadbalance_stats` → LB fields.
4. Fill `rq_len[]` và `online_mask` (best-effort).
5. Set `abi = SCHEDSTATS_ABI_V2`.
6. `copyout` toàn struct.

Không reset counter.

### 3.2 `sched_resetstats`

1. `worksteal_reset_stats()`
2. `loadbalance_reset_stats()`
3. Return 0.

Idempotent, an toàn gọi nhiều lần.

## 4. Semantics chi tiết

### Work Stealing

| Field | Khi tăng |
|-------|----------|
| `ws_searches` | Mỗi lần `worksteal_try` chạy với local RQ rỗng và input hợp lệ |
| `ws_attempts` | Mỗi lần thử `rq_steal_runnable` (đã acquire `p->lock`) |
| `ws_successes` | `RQ_STEAL_OK` – process đã chuyển sang RQ của thief |
| `ws_failed_searches` | Kết thúc `worksteal_try` mà không steal được process nào |
| `ws_affinity_skips` | Candidate bị loại vì affinity |
| `ws_stale` | Candidate stale / ineligible sau khi lock |

### Load Balancing

| Field | Khi tăng |
|-------|----------|
| `lb_checks` | Coordinator chạy đủ interval và thực hiện scan |
| `lb_migrations` | `rq_transfer` thành công (thường +1 theo `LB_MAX_MOVES_PER_RUN`) |
| `lb_below_threshold` | Không pair nào `diff >= LB_IMBALANCE_THRESHOLD` |
| `lb_no_eligible` | Có imbalance nhưng không process nào migrate được |
| `lb_rate_limited` | Bỏ qua vì chưa đủ `LB_INTERVAL_TICKS` |

## 5. User-space usage pattern (bắt buộc cho benchmark)

```c
struct sched_stats_v2 before, after;

sched_resetstats();
sched_getstats(&before);   /* optional baseline after reset */

/* ... run workload ... */

sched_getstats(&after);

printf("ws_successes=%d lb_migrations=%d elapsed=...\n",
       after.ws_successes - before.ws_successes,
       after.lb_migrations - before.lb_migrations);
```

**Quy tắc:**

- Luôn `reset` trước workload đo lường.
- So sánh delta, không tin vào giá trị tuyệt đối giữa các boot.
- `getcpu()` sample chỉ là quan sát phụ; **migration proof** = `ws_successes` / `lb_migrations`.

## 6. Tích hợp kernel hiện có

| Module | Hàm sẵn có | Dùng trong V2 |
|--------|------------|---------------|
| WS | `worksteal_get_stats(cpu, &s)` | Sum over `cpu = 0..NCPU-1` |
| WS | `worksteal_reset_stats()` | Gọi từ `sched_resetstats` |
| LB | `loadbalance_get_stats(&s)` | Map 1-1 field |
| LB | `loadbalance_reset_stats()` | Gọi từ `sched_resetstats` |
| RQ | `rq_length(cpu)` | `rq_len[cpu]` |
| Sched | `sched_online_mask()` | `online_mask` |

Không thay đổi thuật toán WS/LB; chỉ expose stats.

## 7. Compatibility / versioning

- `abi != 2` ở user → coi là lỗi hoặc fallback.
- Thêm field mới chỉ ở **cuối** struct và tăng ABI lên 3.
- User program compile với header `kernel/schedstats.h` (shared layout).

## 8. Checklist kiểm thử API

- [ ] `sched_resetstats` → mọi counter = 0 sau `getstats`
- [ ] Chạy `testws` không pin → có thể `ws_successes >= 0` (không bắt buộc >0)
- [ ] Scenario idle CPU + victim load > 1 → `ws_successes > 0`
- [ ] Scenario imbalance ≥ 2 → `lb_migrations > 0` (sau đủ ticks)
- [ ] Process pinned 1 CPU → steal/migrate không đưa sang CPU ngoài mask
- [ ] `usertests` / `scheduler_test` vẫn PASS

## 9. Liên quan

- Implementation: `kernel/schedstats.c`, `sys_sched_getstats`, `sys_sched_resetstats`
- User tool: `user/schedmeasure.c`
- Tests: `user/testws.c` (enhanced), `user/ws_lb_test.c`
- Design: `docs/M3-w3-worksteal-loadbalancing.md`
