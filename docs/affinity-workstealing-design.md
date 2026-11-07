# CPU Affinity & Work Stealing — Integration Design

## 1. Mục tiêu

Thống nhất cơ chế phối hợp giữa Module 2 (CPU Affinity) và Module 3 (Work Stealing), đảm bảo tiến trình chỉ được di chuyển đến CPU nằm trong `affinity_mask`.

## 2. Ý tưởng thiết kế

- Khi CPU rảnh thực hiện Work Stealing, chỉ lựa chọn tiến trình ở trạng thái `RUNNABLE`.
- Sử dụng `cpu_allowed(p, dst_cpu)` để kiểm tra CPU đích có thuộc `affinity_mask` hay không.
- Giữ `p->lock` khi kiểm tra trạng thái và Affinity Mask để tránh race condition.
- Khi chuyển tiến trình, khóa hai Run Queues theo thứ tự CPU ID tăng dần.
- Nếu CPU đích không hợp lệ, bỏ qua tiến trình và tiếp tục tìm ứng viên khác.
- Đảm bảo Safe Migration: không làm mất hoặc nhân đôi tiến trình khi chuyển queue.

## 3. Phối hợp triển khai

- **Module 2:** Cung cấp `affinity_mask`, `cpu_allowed()` và kiểm thử CPU Affinity.
- **Module 3:** Tích hợp kiểm tra Affinity vào Work Stealing và thực hiện Safe Migration.
- **Module 1:** Phối hợp xử lý khi Affinity Mask thay đổi khiến Run Queue hiện tại không còn phù hợp.

