# W3 - M1: Per-CPU Scheduler

## Task 1 - Scheduler Refactoring

### 1. Mục tiêu

Tách `scheduler()`, `sched()`, `yield()` từ `proc.c` sang `scheduler.c` mà không thay đổi thuật toán lập lịch ban đầu.

Phân chia module:
- `scheduler.c`: Chọn CPU, lập lịch và Context Switch.
- `proc.c`: Quản lý tiến trình, Fork, Sleep/Wakeup và Affinity.
- `runqueue.c/h`: Quản lý FIFO Queue riêng cho từng CPU.
- `defs.h`: Khai báo các hàm Scheduler dùng giữa các file.

### 2. Locking Contract

- `p->lock`: Bảo vệ trạng thái, Affinity và Queue Membership.
- `rq->lock`: Bảo vệ dữ liệu FIFO của từng CPU.
- Thứ tự khóa: `p->lock → rq->lock`.
- Không lấy `p->lock` khi đang giữ `rq->lock`.
- Không giữ `rq->lock` qua `sched()` hoặc `swtch()`.
- `sched()` yêu cầu giữ `p->lock`, `noff == 1`, Interrupt đã tắt và `p->state != RUNNING`.

**Kết quả:** Scheduler được tách thành module riêng, chuẩn bị tích hợp Per-CPU Run Queues.

---

## Task 2 - Run Queue Enqueue Integration

### 1. Mục tiêu

Đưa các tiến trình `RUNNABLE` vào Run Queue và bảo đảm Queue Membership nhất quán.

### 2. API

**`sched_enqueue(struct proc *p)`**
- Input: Tiến trình `p`; caller giữ `p->lock`.
- Output: `void`; Panic nếu vi phạm bất biến.
- Chức năng: Chọn CPU, Enqueue và cập nhật `p->rq_cpu`, `p->state = RUNNABLE`.
- Trạng thái hợp lệ: `USED`, `RUNNING`, `SLEEPING`, `RUNNABLE` đã rời Queue.

**`sched_select_cpu(struct proc *p)` — static**
- Input: Tiến trình `p`; caller giữ `p->lock`.
- Output: CPU ID; `-1` nếu không có CPU hợp lệ.
- Task 2: Tạm chọn CPU hiện tại bằng `cpuid()`.

### 3. Queue Membership

- `rq_cpu = -1`: Không thuộc Run Queue nào.
- `rq_cpu >= 0`: Thuộc Queue của CPU tương ứng.
- Mỗi tiến trình chỉ được nằm trong tối đa một Queue.
- Tiến trình trong Queue phải có trạng thái `RUNNABLE`.

### 4. Integration Points

- `userinit()`, `kfork()`: Enqueue tiến trình mới.
- `wakeup()`, `kkill()`: Enqueue tiến trình được đánh thức.
- `yield()`: Enqueue lại tiến trình đang chạy.
- `allocproc()`, `freeproc()`: Khởi tạo hoặc đặt lại `rq_cpu`.

**Kết quả:** Hoàn thành Enqueue và quản lý Membership. Ở Task 2, Scheduler vẫn tạm quét `proc[]`.

---

## Task 3 - Per-CPU Scheduling & Initial Placement

### 1. Mục tiêu

- Thay vòng quét `proc[NPROC]` bằng Run Queue riêng của từng CPU.
- Chọn CPU theo Online Mask, Affinity và tải hiện tại.
- Bảo đảm Dequeue an toàn khi nhiều CPU hoạt động.

### 2. API

**`sched_cpu_online(void)`**
- Input: Không.
- Output: `void`.
- Chức năng: Đánh dấu CPU hiện tại đã khởi tạo xong và Online.
- Được gọi trong `main.c` trước khi CPU tham gia Scheduling.

**`sched_online_mask(void)`**
- Input: Không.
- Output: Bitmask các CPU Online (`uint`).
- Chức năng: Cung cấp danh sách CPU được phép nhận tiến trình.

**`sched_select_cpu(struct proc *p)` — static**
- Input: Tiến trình `p`; caller giữ `p->lock`.
- Output: CPU ID hoặc `-1`.
- Thuật toán:
  1. Lọc CPU hợp lệ: `affinity_mask & online_mask`.
  2. Ưu tiên CPU hiện tại nếu tiến trình đang `RUNNING` và Affinity cho phép.
  3. Tính tải: `load = rq_length(cpu) + cpu_busy[cpu]`.
  4. Chọn CPU tải thấp nhất; đồng tải thì luân phiên bằng `placement_ticket`.

**`rq_dequeue(int cpu_id, struct proc *expected)`**
- Input: CPU ID và tiến trình dự kiến đứng đầu Queue; Scheduler giữ `expected->lock`.
- Output: `0` thành công; `-1` nếu Queue rỗng, sai Head hoặc tham số không hợp lệ.
- Chức năng: Chỉ xóa Head nếu đúng `expected`, tránh Dequeue nhầm tiến trình.

**`scheduler(void)`**
- Input: Không; chạy trên CPU hiện tại.
- Output: Không trả về.
- Chức năng:
  1. Peek Head từ Queue của CPU hiện tại.
  2. Giữ `p->lock`, gọi Conditional Dequeue.
  3. Kiểm tra `RUNNABLE`, `rq_cpu` và Affinity.
  4. Nếu Affinity không phù hợp, Enqueue lại vào CPU hợp lệ.
  5. Chuyển `RUNNING` và gọi `swtch()`.
  6. Nếu Queue rỗng, dùng `wfi` để chờ ngắt.

**`sched(void)` và `yield(void)`**
- Input: Không.
- Output: `void`; tiếp tục khi tiến trình được chạy trở lại.
- `sched()`: Chuyển Context từ tiến trình về Scheduler.
- `yield()`: Enqueue lại tiến trình rồi nhường CPU.

### 3. Đồng bộ và an toàn

- Thứ tự khóa: `p->lock → rq->lock`.
- Peek chỉ trả về con trỏ tạm thời; phải xác nhận lại bằng Conditional Dequeue.
- Dequeue thành công phải thỏa mãn `state == RUNNABLE` và `rq_cpu == cpu_id`.
- Sau Dequeue, đặt `rq_cpu = -1`.
- Kiểm tra lại Affinity trước khi chuyển sang `RUNNING`.
- Không giữ `rq->lock` qua Context Switch.
- `ksetaffinity()` phải từ chối mask không chứa CPU Online nào.

### 4. Kiểm thử

**Kernel Self-test (`runqueue_test.c`)**
- Empty/Invalid Queue.
- FIFO, Peek, Duplicate và Conditional Dequeue.
- Full Queue, Wraparound và Remove.
- Independent Per-CPU Queues.

**User Test (`scheduler_test.c`)**

| Test | Nội dung |
|---|---|
| T1 | Invalid/Offline Affinity |
| T2 | Fork Inheritance & CPU Pinning |
| T3 | Repeated Sleep/Wakeup |
| T4 | External Affinity Update |
| T5 | Concurrent Pinned Workers |
| T6 | Concurrent Unrestricted Workers |
| T7 | Repeated SMP Contention |

Chạy kiểm thử:

```bash
make clean
make CPUS=4 RQ_TEST=1 qemu
```

Kết quả mong muốn: `runqueue: selftest PASS`.

Build bình thường:

```bash
make clean
make CPUS=4 qemu
```

Trong xv6 Shell:

```text
scheduler_test 4
getcpu_test
affinity_test
forktest
testws 8 1000000
usertests -q
```

Kết quả mong muốn: `SMP Scheduler: 7/7 tests passed`, các bài kiểm thử hồi quy PASS.

Kiểm thử bổ sung với `CPUS=1, 2, 8`.

### 5. Kết quả và giới hạn

**Đã triển khai:**
- Per-CPU Scheduling thay thế Global Process Table Scanning.
- Affinity-aware Initial Placement theo tải.
- CPU Online Tracking và CPU Locality khi Yield.
- Conditional Dequeue và kiểm tra Queue Membership.
- Bộ kiểm thử chức năng Scheduler đa CPU.

**Chưa thuộc Task 3:**
- Work Stealing và Dynamic Load Balancing (M3).
- Migration tức thì khi Affinity thay đổi (tích hợp M2).
- IPI Wakeup cho CPU đang Idle.

**Kết luận:** Hoàn thành phần triển khai Task 3. Kết quả kiểm thử thực tế được ghi nhận riêng trong báo cáo W3.