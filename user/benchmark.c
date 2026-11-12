// user/bench.c - benchmark kha nang mo rong (scalability) cho xv6-riscv.
//
// Cach dung (trong shell cua xv6):
//     bench <kernel> <workload> <ncpu> <runs> [total]
//
//   kernel   : nhan ghi vao CSV (vd: stock, mine)
//   workload : cpu | alloc | fork | file | pingpong | burst
//   ncpu     : so CPU cua QEMU (CPUS=N). Bench se fork dung ncpu tien trinh.
//   runs     : so lan do (chua tinh 1 lan warmup khong ghi)
//   total    : TONG khoi luong cong viec, chia deu cho ncpu tien trinh
//              (strong scaling). Nen la boi so cua 840 de chia het cho 1..8.
//
// Moi lan do in ra 1 dong:   CSV,<kernel>,<workload>,<ncpu>,<run>,<ticks>
// Ket thuc in:               BENCH_DONE
// Cac dong bat dau bang '#' la ghi chu, script Python se bo qua.
//
// Don vi thoi gian la tick cua xv6 (mac dinh 1 tick = 100 ms tren QEMU virt).
// Hay chon `total` du lon de T(ncpu lon nhat) >= ~10 ticks, neu khong do phan
// giai thoi gian se lam speedup nhay so.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MAXPROCS       16
#define FILE_BLKS      8    // moi file 8 block
#define BLK            1024 // BSIZE cua xv6
#define PAGES_PER_ITER 4
#define BURST_TASKS    32 // so tac vu fork cung luc; trong so = 1+(i%4), tong = 80

enum { W_CPU, W_ALLOC, W_FORK, W_FILE, W_PINGPONG, W_BURST, W_NUM };

static const char *wnames[W_NUM] = {"cpu",  "alloc",    "fork",
                                    "file", "pingpong", "burst"};

// Khoi luong mac dinh (boi so cua 840), chon sao cho T(1 CPU) ~ 100 tick tren
// may thu nghiem. May cua ban nhanh/cham hon -> hay chay `bench x <w> 1 1` va
// chinh `total` (tham so thu 5) de T(1 CPU) >= ~100 tick.
static const int default_total[W_NUM] = {
  840 * 2500000, // cpu  : so vong lap so hoc (~2.1e9, gan can tren cua int)
  840 * 150,     // alloc: so lan sbrk(+) / sbrk(-)
  840 * 20,      // fork : so lan fork+exit+wait
  840 * 7,       // file : so lan open+read(8 block)+close
  840 * 40,      // pingpong: so vong gui-nhan qua pipe (tinh tren moi cap)
  840 * 2500000, // burst: tong so vong lap, chia cho 80 don vi trong so
};

static char buf[BLK];

static void fname(char *name, int id)
{
  name[0] = 'b';
  name[1] = '0' + id / 10;
  name[2] = '0' + id % 10;
  name[3] = 0;
}

// ---- cac loai cong viec (chay trong tien trinh con) ----

// Thuan CPU, khong cham kernel: dung de kiem tra may host co du core that.
static void job_cpu(int id, int iters)
{
  volatile uint x = id + 1;
  for (int i = 0; i < iters; i++)
    x = x * 1664525u + 1013904223u;
}

// Gay tranh chap bo cap phat trang (kalloc/kfree, kmem.lock).
static void job_alloc(int id, int iters)
{
  for (int i = 0; i < iters; i++) {
    if (sbrk(PAGES_PER_ITER * 4096) == (char *)-1) {
      printf("# alloc: sbrk failed\n");
      exit(1);
    }
    sbrk(-PAGES_PER_ITER * 4096);
  }
}

// Gay tranh chap bang tien trinh va cap phat (allocproc, wait, kalloc).
static void job_fork(int id, int iters)
{
  for (int i = 0; i < iters; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("# fork: failed\n");
      exit(1);
    }
    if (pid == 0)
      exit(0);
    wait(0);
  }
}

// Gay tranh chap buffer cache / file table / icache (bcache.lock, ftable.lock).
static void job_file(int id, int iters)
{
  char name[4];
  fname(name, id);
  for (int i = 0; i < iters; i++) {
    int fd = open(name, O_RDONLY);
    if (fd < 0) {
      printf("# file: open %s failed\n", name);
      exit(1);
    }
    for (int b = 0; b < FILE_BLKS; b++)
      if (read(fd, buf, BLK) != BLK) {
        printf("# file: short read\n");
        exit(1);
      }
    close(fd);
  }
}

// Moi worker gom 1 CAP tien trinh trao doi 1 byte qua 2 pipe. Moi vong = 2 lan
// sleep/wakeup + 2 lan chuyen ngu canh => gay ap luc len scheduler (chon viec,
// danh thuc, tranh chap lock cua tien trinh).
static void job_pingpong(int id, int iters)
{
  int ab[2], ba[2];
  char c = 'x';

  if (pipe(ab) < 0 || pipe(ba) < 0) {
    printf("# pingpong: pipe failed\n");
    exit(1);
  }
  int pid = fork();
  if (pid < 0) {
    printf("# pingpong: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    for (int i = 0; i < iters; i++) {
      if (read(ab[0], &c, 1) != 1 || write(ba[1], &c, 1) != 1)
        exit(1);
    }
    exit(0);
  }
  for (int i = 0; i < iters; i++) {
    if (write(ab[1], &c, 1) != 1 || read(ba[0], &c, 1) != 1)
      exit(1);
  }
  int st = 0;
  wait(&st);
  close(ab[0]);
  close(ab[1]);
  close(ba[0]);
  close(ba[1]);
  if (st != 0)
    exit(1);
}

static void job(int w, int id, int iters)
{
  switch (w) {
  case W_CPU:
    job_cpu(id, iters);
    break;
  case W_ALLOC:
    job_alloc(id, iters);
    break;
  case W_FORK:
    job_fork(id, iters);
    break;
  case W_FILE:
    job_file(id, iters);
    break;
  case W_PINGPONG:
    job_pingpong(id, iters);
    break;
  }
}

// ---- chuan bi / don dep (khong tinh vao thoi gian do) ----

static int make_files(int n)
{
  char name[4];
  for (int i = 0; i < BLK; i++)
    buf[i] = 'a' + i % 26;
  for (int id = 0; id < n; id++) {
    fname(name, id);
    int fd = open(name, O_CREATE | O_WRONLY);
    if (fd < 0) {
      printf("# cannot create %s (fs full?)\n", name);
      return -1;
    }
    for (int b = 0; b < FILE_BLKS; b++)
      if (write(fd, buf, BLK) != BLK) {
        printf("# write failed (fs full? tang FSSIZE trong kernel/param.h)\n");
        close(fd);
        return -1;
      }
    close(fd);
  }
  return 0;
}

static void remove_files(int n)
{
  char name[4];
  for (int id = 0; id < n; id++) {
    fname(name, id);
    unlink(name);
  }
}

// ---- burst: 1 tien trinh cha fork BURST_TASKS tac vu co do dai khac nhau ----
// Tong cong viec co dinh, KHONG phu thuoc ncpu. Voi run queue rieng cho tung CPU,
// cac tac vu moi sinh ra nam o CPU cua tien trinh cha, nen CPU khac phai
// "lay trom" (work stealing) thi moi co speedup.
static int run_burst(int total)
{
  int unit =
    total / 80; // trong so 1..4 lap lai tren 32 tac vu: tong = 8*(1+2+3+4) = 80
  int forked = 0;
  int ok = 1;

  int t0 = uptime();
  for (int i = 0; i < BURST_TASKS; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("# burst: fork failed at task %d\n", i);
      ok = 0;
      break;
    }
    if (pid == 0) {
      job_cpu(i, unit * (1 + i % 4));
      exit(0);
    }
    forked++;
  }
  for (int i = 0; i < forked; i++) {
    int st = 0;
    wait(&st);
    if (st != 0)
      ok = 0;
  }
  int t1 = uptime();
  return ok ? t1 - t0 : -1;
}

// ---- mot lan do: tra ve so tick, hoac -1 neu co loi ----

static int run_once(int w, int ncpu, int total)
{
  if (w == W_BURST)
    return run_burst(total);

  int per = total / ncpu;
  int ok = 1;
  int forked = 0;

  int t0 = uptime();
  for (int id = 0; id < ncpu; id++) {
    int pid = fork();
    if (pid < 0) {
      printf("# fork failed for worker %d\n", id);
      ok = 0;
      break;
    }
    if (pid == 0) {
      job(w, id, per);
      exit(0);
    }
    forked++;
  }
  for (int i = 0; i < forked; i++) {
    int st = 0;
    wait(&st);
    if (st != 0)
      ok = 0;
  }
  int t1 = uptime();
  return ok ? t1 - t0 : -1;
}

static void usage(void)
{
  printf(
    "usage: bench <kernel> <cpu|alloc|fork|file|pingpong|burst> <ncpu> <runs> [total]\n");
  exit(1);
}

int main(int argc, char *argv[])
{
  if (argc < 5)
    usage();

  char *kernel = argv[1];
  int w = -1;
  for (int i = 0; i < W_NUM; i++)
    if (strcmp(argv[2], wnames[i]) == 0)
      w = i;
  if (w < 0)
    usage();

  int ncpu = atoi(argv[3]);
  int runs = atoi(argv[4]);
  int total = argc >= 6 ? atoi(argv[5]) : default_total[w];

  if (ncpu < 1 || ncpu > MAXPROCS || runs < 1 || total < ncpu)
    usage();
  if (w == W_BURST && total < 80)
    usage();

  printf("# bench kernel=%s workload=%s cpus=%d runs=%d total=%d\n", kernel,
         wnames[w], ncpu, runs, total);

  if (w == W_FILE && make_files(ncpu) < 0) {
    remove_files(ncpu);
    exit(1);
  }

  run_once(w, ncpu, total); // warmup, khong ghi

  int status = 0;
  int last = 0;
  for (int r = 1; r <= runs; r++) {
    int ticks = run_once(w, ncpu, total);
    if (ticks < 0) {
      printf("# run %d failed\n", r);
      status = 1;
      continue;
    }
    last = ticks;
    printf("CSV,%s,%s,%d,%d,%d\n", kernel, wnames[w], ncpu, r, ticks);
  }
  if (status == 0 && last < 10)
    printf("# WARN: chi %d tick (<10). Tang `total` de chinh xac hon.\n", last);

  if (w == W_FILE)
    remove_files(ncpu);

  printf("BENCH_DONE\n");
  exit(status);
}
