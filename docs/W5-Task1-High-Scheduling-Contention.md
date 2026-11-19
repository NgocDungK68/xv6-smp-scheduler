# W5 – Task 1: High Scheduling Contention Benchmark

## Mục tiêu

Khảo sát tiến trình đồng thời tạo/thoát tiến trình hoặc liên tục đánh thức nhau. Bài thử nghiệm dùng hai workload:

- **Fork contention:** nhiều worker chạy song song; mỗi worker lặp lại `fork()` → child thực hiện một lượng công việc CPU nhỏ → `exit()` → parent `wait()`. Workload này tạo áp lực lên Process Table, Process Lock, Run Queue và các chuyển trạng thái tiến trình.
- **Ping-pong contention:** nhiều cặp parent/child chạy song song, trao đổi một byte qua hai pipe trong mỗi vòng. Workload này tạo các lần `sleep()`/`wakeup()` và context switch liên tục.


## Cách tích hợp

1. Copy `user/schedcontention.c` vào thư mục `user/`.
2. Áp dụng `patches/makefile-uprogs.patch` hoặc thêm `$U/_schedcontention\` vào danh sách `UPROGS` trong Makefile.
3. Build xv6.

## Cách chạy

Chạy từng cấu hình QEMU riêng, không thay đổi workload giữa các lần so sánh.

### Fork contention

```text
$ schedcontention fork 4 100
$ schedcontention fork 8 100
```

### Ping-pong contention

```text
$ schedcontention pingpong 4 500
$ schedcontention pingpong 8 500
```

Lặp lại các lệnh trên với `make qemu CPUS=1`, `CPUS=2`, `CPUS=4` và `CPUS=8`.

## Input / Output

| Mục | Ý nghĩa |
|---|---|
| `mode` | `fork` hoặc `pingpong` |
| `workers` | Số workload độc lập chạy đồng thời, từ 1 đến 8 |
| `rounds` | Số fork/wait cycle hoặc ping-pong round-trip mỗi worker |
| `workers_started` | Số worker được tạo thành công |
| `workers_completed` | Số worker hoàn thành bình thường |
| `failed_workers` | Số worker thất bại |
| `planned_operations` | `workers_started × rounds` |
| `elapsed_ticks` | Thời gian theo timer tick của xv6 |
| `operations_per_tick` | Throughput thô tính theo operation/tick nếu elapsed_ticks > 0 |

## Cách diễn giải

- Fork mode nhấn mạnh quá trình tạo tiến trình, chuyển trạng thái RUNNABLE/RUNNING/ZOMBIE và thu hồi bằng `wait()`.
- Ping-pong mode nhấn mạnh pipe lock, sleep/wakeup và luân phiên context switch giữa các tiến trình.
- Khi tăng `workers`, mức độ đồng thời tăng và có thể làm lộ contention trên các khóa hoặc Run Queue.
- `uptime()` có độ phân giải theo timer tick; nếu kết quả là 0 tick, tăng `rounds`. Không diễn giải 0 tick là thời gian chạy bằng 0.
- Không dùng số đo từ bản có verbose kernel logging để so sánh hiệu năng. Tắt log chẩn đoán khi đo chính thức.

## Phạm vi và giới hạn

Đây là thử nghiệm ban đầu về scheduling contention, không phải benchmark khóa thuần túy. Thời gian đo còn bao gồm chi phí fork/exit/wait hoặc pipe I/O. Để kết luận scheduler tốt hơn, cần kết hợp thời gian thực thi với các thống kê scheduler (context switch, enqueue/dequeue, steal/migration nếu kernel cung cấp).
