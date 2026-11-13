# W4 – Task 7: Work Stealing và Periodic Load Balancing

Tài liệu mô tả các kịch bản kiểm thử `testbalance` trên xv6 SMP, tập trung vào Idle Work Stealing, Periodic Load Balancing và CPU Affinity.

## Test 1 – Idle Work Stealing (Mode 0)

**Mục tiêu:** Tạo tình huống một CPU có nhiều tiến trình chờ, trong khi các CPU còn lại ban đầu không có tiến trình.

**Cách thực hiện:**

1. Lấy CPU hiện tại bằng `getcpu()`.
2. Pin tiến trình cha vào CPU đó.
3. Fork 8 tiến trình con.
4. Các tiến trình con ban đầu kế thừa Affinity Mask, nên được đưa về CPU nguồn.
5. Sau khi tạo workload, mở rộng Affinity Mask của các tiến trình con sang tất cả CPU.
6. Các CPU đang rảnh có thể thực hiện Work Stealing.

Đây là tình huống phù hợp với điều kiện kích hoạt trong thuật toán hiện tại: Local Run Queue rỗng thì mới tìm CPU nguồn.

**Lệnh chạy:**

```sh
$ testbalance 8 30000000 0 4
```

**Kết quả cần quan sát:**

- Các worker được tạo thành công và hoàn thành.
- Work Stealing được kích hoạt khi CPU đích có Local Run Queue rỗng.
- Có ghi nhận Migration thành công nếu một CPU lấy được tiến trình phù hợp.
- Không có tiến trình bị mất, chạy trùng hoặc chạy trên CPU không được phép.

## Test 2 – Periodic Load Balancing (Mode 1)

**Mục tiêu:** Kiểm tra khả năng cân bằng tải khi các CPU đều có công việc nhưng mức tải khác nhau.

**Cách thực hiện:**

1. Tạo 2 tiến trình anchor trên mỗi CPU không phải CPU nguồn.
2. Pin các anchor vào CPU tương ứng.
3. Tạo 8 tiến trình workload trên CPU nguồn.
4. Mở rộng Affinity Mask của 8 tiến trình workload.
5. CPU 0 thực hiện kiểm tra Load Balancing theo chu kỳ.
6. Nếu độ chênh lệch tải vượt ngưỡng, hệ thống có thể chuyển một tiến trình sang CPU khác.

Hai anchor trên mỗi CPU đích giúp duy trì Queue không rỗng trong phần lớn thời gian, giảm cơ hội kích hoạt Idle Work Stealing và tạo điều kiện kiểm tra Periodic Load Balancing.

**Lệnh chạy:**

```sh
$ testbalance 8 30000000 1 4
```


**Kết quả mong đợi:**

- Load Balancing được gọi theo chu kỳ.
- Migration chỉ xảy ra khi độ chênh lệch tải đạt ngưỡng cấu hình.
- Các tiến trình được chuyển vẫn tuân thủ CPU Affinity.
- Không xảy ra Migration liên tục không cần thiết.

## Test 3 – CPU Affinity (Mode 2)

**Mục tiêu:** Kiểm tra Work Stealing không đưa tiến trình sang CPU bị cấm bởi Affinity Mask.

**Cách thực hiện:**

1. Tạo 8 tiến trình con.
2. Giữ tất cả tiến trình chỉ được phép chạy trên CPU nguồn.
3. Các CPU còn lại có thể thử Work Stealing nhưng không được lấy những tiến trình này.

**Lệnh chạy:**

```sh
$ testbalance 8 30000000 2 4
```

**Kết quả mong đợi:**

- `cpu_mask` của mỗi worker chỉ chứa bit CPU nguồn.
- Không có worker nào chạy trên CPU không được cấp quyền.
- Work Stealing bỏ qua các tiến trình không tương thích với CPU đích.

## Tổng hợp kết quả Task 7
Tiêu chí	                                Kết quả	Đánh giá
Idle Work Stealing	                        8/8 worker chạy trên CPU khác CPU ban đầu	PASS một phần
Periodic Load Balancing                 	Có 3 log Migration thành công	            PASS
CPU Affinity	                            0 vi phạm	                                PASS
Worker completion	                        8/8	                                        PASS
Load Balancing completion	                14/14	                                    PASS
Anchor placement	                        0 sai lệch	                                PASS
Process Integrity	                        Không thấy tiến trình bị bỏ sót	            PASS trong các workload đã thử


Hệ thống không còn bị deadlock và các tiến trình được phân bổ chéo thành công giữa 4 lõi.



##