
# W3 - M1 - Task 1: Scheduler Design

## 1. Mục tiêu

Tách các hàm scheduler(), sched(), yield() từ proc.c
sang scheduler.c/h để quản lý code rõ ràng và chuẩn bị
tích hợp Per-CPU Run Queues.

Task 1 chỉ tái cấu trúc code, chưa thay đổi thuật toán
lập lịch hiện tại.

## 2. Phân chia chức năng

- proc.c: Quản lý tiến trình, fork, exit, wait, sleep,
  wakeup và lưu CPU Affinity Mask.
- scheduler.c: Lựa chọn tiến trình, Context Switch,
  xử lý nhường CPU.
- scheduler.h: Khai báo giao diện Scheduler.
- runqueue.c/h: Quản lý FIFO Queue riêng cho từng CPU.

Scheduler hiện vẫn quét proc[NPROC].
Task 2 sẽ tích hợp Run Queue vào Scheduler.

## 3. Quy tắc đồng bộ

### Process Lock (p->lock)

Bảo vệ trạng thái và thông tin tiến trình.

Quy trình Context Switch:
1. Scheduler giữ p->lock.
2. Kiểm tra trạng thái RUNNABLE.
3. Chuyển sang RUNNING.
4. Gọi swtch().
5. Khi quay lại Scheduler, giải phóng p->lock.

sched() yêu cầu:
- Đang giữ đúng p->lock.
- mycpu()->noff == 1.
- Interrupt đã tắt.
- Trạng thái tiến trình khác RUNNING.

### Run Queue Lock (rq->lock)

Mỗi CPU có một Run Queue và Spinlock riêng.

- rq->lock chỉ bảo vệ dữ liệu FIFO.
- Các hàm rq_* tự lấy và nhả khóa.
- Không giữ rq->lock khi gọi swtch() hoặc sched().
- Khi cần cả hai khóa, dự kiến thống nhất thứ tự:
  p->lock -> rq->lock.
- Không lấy p->lock trong khi đang giữ rq->lock.

### Wait Lock (wait_lock)

Bảo vệ quan hệ cha-con và quá trình wait/exit.

Giữ thứ tự khóa hiện tại:
wait_lock -> p->lock.

## 4. Quy tắc quản lý Queue dự kiến cho Task 2

- Mỗi tiến trình RUNNABLE chỉ thuộc tối đa một Queue.
- Queue Membership phải được theo dõi nhất quán.
- Không đưa RUNNING, SLEEPING hoặc ZOMBIE vào Queue.
- Không chạy tiến trình trên CPU không phù hợp Affinity.
- Chỉ phân phối tiến trình đến CPU đang hoạt động.
- Work Stealing cần cơ chế Migration an toàn,
  không được gây mất hoặc trùng tiến trình.

Các quy tắc trên chưa được hiện thực trong Task 1.

## 5. Kiểm thử

- Build thành công với CPUS=4.
- Kernel khởi động tới xv6 Shell.
- Run Queue Self-test PASS với RQ_TEST=1.
- Các kiểm thử cơ bản fork, wait, yield và sleep
  không phát sinh lỗi mới.
- Không thay đổi logic Scheduler ban đầu.

## 6. Kết luận

Đã tách Scheduler thành module độc lập và xác định
nguyên tắc đồng bộ cơ bản.

Task 2 sẽ tích hợp Per-CPU Run Queues và xử lý
Enqueue/Dequeue cùng các trạng thái tiến trình.

# W3 - M1 - Task 2: Run Queue Enqueue Integration

## 1. Mục tiêu

Đưa tiến trình RUNNABLE vào Per-CPU Run Queue, bảo đảm trạng thái tiến trình và Queue Membership nhất quán.

## 2. API

**`sched_enqueue(struct proc *p)` — scheduler.c**

- **Input:** Con trỏ `p`; caller phải giữ `p->lock`.
- **Output:** `void`; panic nếu trạng thái, membership hoặc Enqueue không hợp lệ.
- **Chức năng:**
  1. Kiểm tra trạng thái tiến trình và `rq_cpu == -1`.
  2. Gọi `sched_select_cpu(p)` để chọn CPU.
  3. Gọi `rq_enqueue(cpu_id, p)`.
  4. Cập nhật `p->rq_cpu` và `p->state = RUNNABLE`.

**`sched_select_cpu(struct proc *p)` — static helper**

- **Input:** Con trỏ tiến trình `p`.
- **Output:** CPU ID được chọn.
- **Task 2:** Trả về `cpuid()` (CPU hiện tại).
- **Task 3:** Thay bằng Initial Placement Algorithm.

## 3. Queue Membership

- `rq_cpu = -1`: Tiến trình không nằm trong Run Queue.
- `rq_cpu >= 0`: Tiến trình đang nằm trong Queue của CPU tương ứng.
- Một tiến trình chỉ được nằm trong tối đa một Queue.
- `RUNNING`, `SLEEPING`, `ZOMBIE` không nằm trong Queue.

## 4. Locking Contract

- Caller giữ `p->lock` khi gọi `sched_enqueue()`.
- `rq_enqueue()` tự lấy và nhả `rq->lock`.
- Thứ tự khóa: `p->lock -> rq->lock`.
- Không giữ `rq->lock` qua `sched()` hoặc `swtch()`.
- Trạng thái và Queue Membership được cập nhật khi vẫn giữ `p->lock`.

## 5. Integration Points

- `userinit()`, `kfork()`: Enqueue tiến trình mới.
- `wakeup()`, `kkill()`: Enqueue khi đánh thức tiến trình SLEEPING.
- `yield()`: Đưa tiến trình đang chạy trở lại Queue.
- `procinit()`, `allocproc()`, `freeproc()`: Khởi tạo hoặc đặt lại `rq_cpu`.

## 6. Trạng thái triển khai

Scheduler tạm thời vẫn quét `proc[]` để chọn tiến trình và gọi `rq_remove()` trước khi chạy tiến trình đó.

Bước tiếp theo: thay vòng quét `proc[]` bằng cơ chế chọn tiến trình từ Run Queue của CPU hiện tại.
