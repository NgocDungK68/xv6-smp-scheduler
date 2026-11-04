
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
