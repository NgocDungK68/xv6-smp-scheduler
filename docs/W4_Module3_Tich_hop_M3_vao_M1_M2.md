# W4 – Module 3: 

## 1. Tích hợp WS LB vào M1 và M2

Tích hợp Work-Stealing và Load Balancing với hai thành phần đã được phát triển trước đó

## 1.1. Tích hợp với M1 – Per-CPU Runqueue


- Sử dụng cấu trúc và thao tác quản lý queue do M1 cung cấp.
- Kiểm tra trạng thái runqueue để nhận biết CPU đang rảnh hoặc CPU có nhiều tiến trình chờ xử lý.
- Chọn runqueue nguồn (victim CPU) khi cần lấy tiến trình sang CPU đích.
- Thực hiện việc chuyển tiến trình thông qua các thao tác queue phù hợp, đồng thời phải bảo đảm đồng bộ khi truy cập runqueue.

**Thành phần liên quan của M1:** `kernel/runqueue.c`, `kernel/runqueue.h` và phần tích hợp scheduler trong `kernel/proc.c`.

### 1.2. Tích hợp với M2 – CPU Affinity

Work-stealing không được phép làm cho tiến trình chạy trên CPU nằm ngoài phạm vi affinity đã thiết lập. Vì vậy, trước khi chuyển một tiến trình từ queue nguồn sang queue đích, cần kiểm tra bit tương ứng với CPU đích trong `affinity_mask`.

Ví dụ:

- `mask = 0x2` (`000...0010`): tiến trình chỉ được phép chạy trên CPU 1.
- Nếu CPU đích không nằm trong affinity mask, tiến trình đó không hợp lệ để được steal sang CPU đích.
- Các tiến trình bị giới hạn CPU phải tiếp tục được giữ hoặc chuyển đến một CPU hợp lệ theo chính sách scheduler; không được bỏ qua ràng buộc affinity chỉ để cân bằng số lượng queue.

Trong tuần này, tầng user-space và kernel-space của system call `setaffinity` được bổ sung/đồng bộ để chương trình kiểm thử có thể thiết lập affinity cho worker.

Các vị trí thay đổi:

- `user/user.h`: khai báo `int setaffinity(int pid, int mask);`.
- `user/usys.pl`: thêm `entry("setaffinity");` để sinh wrapper system call.
- `kernel/syscall.h`: khai báo mã system call `SYS_setaffinity`.
- `kernel/syscall.c`: khai báo hàm xử lý và đăng ký `sys_setaffinity` trong bảng `syscalls[]`.
- `kernel/sysproc.c`: triển khai `sys_setaffinity`, đọc PID và affinity mask, tìm tiến trình tương ứng và cập nhật `p->affinity_mask` khi đang giữ khóa tiến trình.

**Lưu ý về tên system call:** cần thống nhất tên được sử dụng trong toàn bộ M2 và M3. Nếu nhánh M2 đang dùng `sched_setaffinity(pid, mask)` thay vì `setaffinity(pid, mask)`, phải đồng bộ tên hàm ở khai báo user-space, wrapper, mã syscall và chương trình kiểm thử. Không nên để hai tên khác nhau cùng đại diện cho một chức năng.

### 1.3. Tích hợp W-S và L-B

- **Idle Work-Stealing:** khi một CPU không có tiến trình phù hợp để chạy, cơ chế work-stealing tìm CPU nguồn có queue chứa tiến trình có thể di chuyển.
- **Load Balancing:** kiểm tra sự chênh lệch tải giữa các CPU và hỗ trợ điều phối khi mức chênh lệch đạt điều kiện đã thiết kế.
- **Victim Selection:** lựa chọn CPU nguồn dựa trên tình trạng queue, tránh lấy tiến trình một cách tùy ý.
- **Affinity Validation:** chỉ xem xét những tiến trình được phép chạy trên CPU đích.
- **Synchronization:** bảo vệ thao tác lấy tiến trình khỏi queue nguồn và đưa vào queue đích để tránh race condition khi nhiều CPU cùng truy cập queue.

Các file M3 liên quan:

- `kernel/worksteal.c`, `kernel/worksteal.h`: xử lý và khai báo cơ chế work-stealing.
- `kernel/loadbalancing.c`, `kernel/loadbalancing.h`: xử lý và khai báo logic cân bằng tải.
- `kernel/main.c`: gọi các hàm khởi tạo của M3 trong quá trình khởi động kernel, nếu các module được tách thành hai phần độc lập.

## 2. Luồng phối hợp giữa M1, M2 và M3

Luồng xử lý tổng quát sau khi tích hợp:

1. Tiến trình được đưa vào local runqueue theo cơ chế quản lý của M1.
2. Scheduler trên mỗi CPU ưu tiên lấy tiến trình từ runqueue cục bộ.
3. Nếu CPU không có tiến trình phù hợp, M3 kiểm tra các runqueue khác để tìm nguồn có thể cung cấp tiến trình.
4. Trước khi chuyển tiến trình, M3 kiểm tra affinity mask do M2 quản lý.
5. Nếu CPU đích được phép chạy tiến trình, tiến trình có thể được chuyển sang queue đích theo cơ chế đồng bộ của runqueue.
6. CPU đích tiếp tục lấy tiến trình từ local runqueue để thực thi.
7. Nếu không tìm được tiến trình hợp lệ, CPU tiếp tục chờ hoặc thực hiện lại việc kiểm tra theo chính sách scheduler.

Như vậy, **M1 quản lý hàng đợi**, **M2 xác định CPU hợp lệ**, còn **M3 quyết định khi nào và từ đâu cần lấy thêm tiến trình**.

## 3. Cập nhật chương trình kiểm thử `user/testws.c`

`user/testws.c` được mở rộng để kiểm tra sự phối hợp của ba thành phần thay vì chỉ kiểm tra riêng thuật toán work-stealing.

### 3.1. Kịch bản kiểm thử

- Tạo nhóm tiến trình worker để tạo tải trên nhiều CPU.
- Thiết lập affinity cho một nửa số worker vào CPU 1 bằng mask `0x2`.
- Cho phép các worker còn lại chạy trên các CPU được phép theo cấu hình affinity tương ứng.
- Quan sát CPU thực thi của worker và kiểm tra tiến trình có tuân thủ affinity hay không.
- Tạo tình huống tải không đồng đều để kiểm tra khả năng hỗ trợ phân phối lại công việc của work-stealing/load balancing.

### 3.2. Kiểm tra kết quả

Bổ sung assertion/đối chiếu kết quả để phát hiện trường hợp tiến trình chạy trên CPU không nằm trong affinity mask. Khi phát hiện vi phạm, chương trình in thông báo lỗi, ví dụ:

```text
>>> ERROR: Affinity violation
```

Ngoài kiểm tra affinity, cần quan sát hoạt động của các worker trên nhiều CPU để đánh giá M1 và M3. Một lần chạy test chỉ được xem là thành công khi:

- Các worker bị pin vào CPU 1 không chạy trên CPU khác.
- Các worker không bị pin vẫn có thể được scheduler phân phối trên những CPU hợp lệ.
- Work-stealing không chuyển tiến trình đến CPU trái với affinity mask.
- Kernel tiếp tục hoạt động ổn định trong suốt bài kiểm thử, không xuất hiện deadlock, kernel panic hoặc lỗi đồng bộ.

"/pic/W4_test with M1 M2/testws_lb_.png"

## 5. Các lỗi khi keets hợp

Trong quá trình tích hợp M1, M2 và M3, đã chú ý xử lý các vấn đề:

- **Syscall:** Đăng ký đầy đủ số hiệu, hàm xử lý và entry trong `syscalls[]`.
- **Tương thích `argint`:** Gọi trực tiếp `argint()` do phiên bản xv6 sử dụng kiểu trả về `void`.
- **CPU Affinity:** Sử dụng khóa tiến trình khi cập nhật `affinity_mask`.
- **Runqueue:** Tuân thủ cơ chế khóa của M1 khi lấy và chuyển tiến trình.
- **Header và Makefile:** Kiểm tra thứ tự include, khai báo prototype và thêm các module M3 vào danh sách build.

## 6. Kết quả và đánh giá tạm thời

- M3 sử dụng local runqueue của M1 làm cơ sở đánh giá tải và lựa chọn CPU nguồn.
- M3 phối hợp với affinity mask của M2 để tránh di chuyển tiến trình đến CPU không hợp lệ.
- `setaffinity` được đưa qua các tầng user-space và kernel-space để phục vụ việc cấu hình affinity trong bài test.
- `user/testws.c` được mở rộng thành kịch bản kiểm thử kết hợp Local Runqueue, CPU Affinity và Work-Stealing Load Balancing.

**Trạng thái test:** PASS sau khi build thành công và chạy `testws` trên cấu hình SMP dự kiến `CPUS=4`.
