#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Giới hạn 10 triệu vòng lặp mỗi tiến trình để không bị tràn số bộ nhớ 32-bit
#define OPS_PER_PROCESS 10000000 

// Hàm giả lập tính toán CPU-bound nặng
void do_work() {
    // Từ khóa volatile ép CPU phải thực sự tính toán, ngăn trình biên dịch tối ưu hóa (bỏ qua) vòng lặp này
    volatile int sum = 0;
    for (int i = 0; i < OPS_PER_PROCESS; i++) {
        sum += (i % 7) * 3;
    }
}

// Hàm chạy kịch bản với số lượng tiến trình cho trước
void run_test(int num_procs) {
    int pids[8];
    int start_ticks, end_ticks, total_ticks;

    // Bấm giờ
    start_ticks = uptime();

    // Sinh ra các tiến trình con chạy song song
    for (int i = 0; i < num_procs; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            printf("Loi: Fork that bai!\n");
            exit(1);
        }
        if (pids[i] == 0) {
            do_work();
            exit(0); // Tính xong thì thoát
        }
    }

    // Tiến trình cha chờ tất cả con làm xong
    for (int i = 0; i < num_procs; i++) {
        wait(0);
    }

    // Chốt giờ
    end_ticks = uptime();
    total_ticks = end_ticks - start_ticks;
    if (total_ticks == 0) total_ticks = 1; // Chống lỗi chia cho 0 nếu QEMU chạy quá nhanh

    // Tính toán Kilo-Ops (1 KOps = 1000 Ops)
    int total_kops = num_procs * (OPS_PER_PROCESS / 1000);
    
    // Trong xv6, phần cứng ngắt nhịp đếm 10 ticks = 1 giây
    int kops_per_sec = (total_kops * 10) / total_ticks;

    // In kết quả căn lề bằng tab
    printf(" %d\t\t| %d\t\t| %d\n", num_procs, total_ticks, kops_per_sec);
}

int main(int argc, char *argv[]) {
    printf("\n=== TUAN 1: BASELINE STOCK XV6 (GLOBAL LOCK) ===\n");
    printf(" Cores (Procs)\t| Time (Ticks)\t| KOps/sec\n");
    printf("------------------------------------------------\n");

    // Tự động quét qua các mức tải: 1, 2, 4, 8
    int configs[] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) {
        run_test(configs[i]);
    }

    printf("================================================\n\n");
    exit(0);
}
