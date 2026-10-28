
# Per-CPU Run Queue — Design & API

## 1. Mục tiêu

Mỗi CPU có một Run Queue (hàng đợi tiến trình sẵn sàng chạy) riêng, thay cho việc Scheduler phải quét toàn bộ `proc[NPROC]` mỗi lần chọn tiến trình. Queue chỉ lưu **con trỏ `struct proc *`**, không sao chép dữ liệu tiến trình.

Tuần này chỉ xây dựng **Run Queue prototype**. Việc thay `scheduler()`, tích hợp CPU Affinity và Work Stealing sẽ thực hiện sau.

## 2. Cấu trúc dữ liệu

```c
// Private to kernel/runqueue.c.
struct runqueue {
  struct spinlock lock;
  struct proc *queue[NPROC];
  int head;
  int tail;
  int count;
};

static struct runqueue runqueues[NCPU];
```

| Trường | Ý nghĩa |
|---|---|
| `lock` | Khóa bảo vệ dữ liệu của Queue. |
| `queue[NPROC]` | Mảng vòng chứa các con trỏ tiến trình. |
| `head` | Vị trí lấy phần tử tiếp theo. |
| `tail` | Vị trí thêm phần tử tiếp theo. |
| `count` | Số tiến trình đang chờ trong Queue (`0..NPROC`). |

Sử dụng **Circular FIFO**: thêm ở cuối, lấy ở đầu; `head` và `tail` quay vòng theo `NPROC`. Với `NPROC = 64`, mảng có kích thước nhỏ, không cần cấp phát node động hay dùng Doubly Linked List.

`runqueues[cpu_id]` ứng với CPU ID đó (bắt đầu từ **0** trong cấu hình QEMU của nhóm). `NCPU = 8` là số CPU tối đa lúc biên dịch, không phải số CPU thực sự hoạt động. `struct runqueue` và `runqueues[]` được giữ private trong `runqueue.c`.

## 3. API dùng chung (`kernel/runqueue.h`)

| Hàm | Input | Output / chức năng |
|---|---|---|
| `rq_init()` | Không có | Khởi tạo mọi Queue và Spinlock; không trả giá trị. |
| `rq_enqueue(cpu_id, p)` | CPU ID, con trỏ tiến trình | Thêm `p` ở cuối; `0` nếu thành công, `-1` nếu tham số sai, Queue đầy hoặc `p` đã có trong **cùng Queue**. |
| `rq_dequeue(cpu_id)` | CPU ID | Xóa và trả con trỏ đầu; trả `0` nếu Queue rỗng hoặc ID không hợp lệ. |
| `rq_length(cpu_id)` | CPU ID | Trả số phần tử; `-1` nếu ID không hợp lệ. |
| `rq_peek_at(cpu_id, offset)` | CPU ID, vị trí tính từ đầu (`0` là đầu Queue) | Trả con trỏ để xem, **không xóa**; trả `0` nếu vị trí/ID không hợp lệ. |
| `rq_remove(cpu_id, p)` | CPU ID, con trỏ tiến trình | Xóa đúng `p`, giữ thứ tự các phần tử còn lại; `0` nếu thành công, `-1` nếu không tìm thấy/tham số sai. |

Ví dụ Queue CPU 0 chứa `P1 → P2 → P3`: `rq_peek_at(0, 1)` xem P2 mà không thay đổi Queue; `rq_remove(0, P2)` làm Queue còn `P1 → P3`.

## 4. Đồng bộ và phạm vi

- Các thao tác Enqueue/Dequeue/Length/Peek/Remove tự lấy/nhả `rq->lock` để bảo vệ `queue[]`, `head`, `tail`, `count`; `rq_init()` khởi tạo khóa trước khi Scheduler chạy.
- API Queue **không** sửa `p->state`, CPU Affinity hay metadata Queue membership trong `struct proc`. Phần này do Scheduler quản lý.
- `rq_length()` và `rq_peek_at()` chỉ là kết quả tại thời điểm đọc; khi dùng con trỏ để lập lịch hoặc di chuyển phải giữ `p->lock` và kiểm tra lại trạng thái/Queue.
- `rq_enqueue()` chỉ phát hiện trùng **trong cùng Queue**. Prototype chưa bảo đảm một tiến trình không xuất hiện ở nhiều Queue khác nhau.
- Không tự ghép `rq_remove()` và `rq_enqueue()` để di chuyển tiến trình thật giữa các Queue; migration cần giao thức đồng bộ của Scheduler.
