#!/usr/bin/env python3
"""
run_bench.py - Chay `bench` trong xv6 (QEMU) cho nhieu kernel / workload / so CPU
va gom ket qua thanh results.csv de dua cho plot_speedup.py.

Vi du:
    python3 run_bench.py \
        --kernel stock=~/xv6-stock --kernel mine=~/xv6-mine \
        --workload cpu,alloc,fork,file --cpus 1-8 --runs 5 \
        --install -o results.csv

    python3 plot_speedup.py results.csv

Moi thu muc kernel phai la mot ban xv6-riscv (co Makefile, ho tro `make qemu CPUS=N`).
--install: copy chuong trinh (mac dinh bench.c) vao user/ va them $U/_<ten> vao UPROGS.
--source : chon file C khac, vd --source benchmark.c  (ten chuong trinh = ten file, bo .c)
           Khi dung benchmark.c thi --workload mac dinh la "cpu".
"""
import argparse
import csv
import os
import re
import select
import signal
import subprocess
import sys
import time

ROW_RE = re.compile(r"^CSV,([^,\s]+),([^,\s]+),(\d+),(\d+),(\d+)\s*$")
FIELDS = ["kernel", "workload", "cpus", "run", "time"]


def parse_cpus(s):
    out = []
    for part in s.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += range(int(a), int(b) + 1)
        else:
            out.append(int(part))
    return sorted(set(out))


def in_uprogs(text, prog):
    return re.search(r"\$U/_" + re.escape(prog) + r"(?![A-Za-z0-9_])", text) is not None


def install_bench(path, source, prog):
    with open(source) as f:
        code = f.read()
    if "BENCH_DONE" not in code or "CSV," not in code:
        sys.exit(f"'{source}' khong phai ban benchmark moi (khong in dong CSV/BENCH_DONE).\n"
                 f"Kiem tra lai duong dan --source (trinh duyet co the luu file moi thanh 'ten (1).c').")
    with open(os.path.join(path, "user", prog + ".c"), "w") as f:
        f.write(code)
    mk = os.path.join(path, "Makefile")
    with open(mk) as f:
        text = f.read()
    if not in_uprogs(text, prog):
        new = text.replace("UPROGS=\\\n", "UPROGS=\\\n\t$U/_" + prog + "\\\n", 1)
        if new == text:
            sys.exit(f"Khong tim thay 'UPROGS=\\' trong {mk}; hay them $U/_{prog} thu cong.")
        with open(mk, "w") as f:
            f.write(new)
        print(f"[install] da them $U/_{prog} vao {mk}", file=sys.stderr)


def check_installed(path, prog):
    with open(os.path.join(path, "Makefile")) as f:
        ok = in_uprogs(f.read(), prog)
    if not ok or not os.path.exists(os.path.join(path, "user", prog + ".c")):
        sys.exit(f"'{path}' chua co {prog}. Chay lai voi --install.")


def run_qemu(path, ncpu, command, prog, toolprefix, boot_timeout, run_timeout, idle_timeout):
    """Chay 1 phien QEMU. Tra ve (log, loi); loi = None neu thanh cong."""
    cmd = ["make", "qemu", f"CPUS={ncpu}"]
    if toolprefix:
        cmd.append(f"TOOLPREFIX={toolprefix}")
    p = subprocess.Popen(
        cmd, cwd=path, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, start_new_session=True,
    )
    fd = p.stdout.fileno()
    buf = ""
    sent = False
    err = None
    t_start = time.time()
    t_sent = None
    t_last = time.time()          # lan cuoi nhan duoc byte nao tu QEMU
    try:
        while True:
            now = time.time()
            if not sent and now - t_start > boot_timeout:
                err = f"qua {boot_timeout}s ma xv6 chua len shell (build/boot loi?)"
                break
            if sent and now - t_sent > run_timeout:
                err = f"benchmark chay qua {run_timeout}s (--run-timeout)"
                break
            if sent and now - t_last > idle_timeout:
                err = (f"khong co output nao trong {idle_timeout}s sau khi chay benchmark "
                       f"(kernel co the bi treo/deadlock, hoac total qua lon; xem --idle-timeout)")
                break
            ready, _, _ = select.select([fd], [], [], 1.0)
            if ready:
                chunk = os.read(fd, 65536)
                if not chunk:
                    err = "make/QEMU thoat som (loi build? xem log)"
                    break
                buf += chunk.decode(errors="replace")
                t_last = time.time()
            if not sent and "init: starting sh" in buf and buf.rstrip().endswith("$"):
                try:
                    p.stdin.write((command + "\n").encode())
                    p.stdin.flush()
                except OSError:
                    err = "khong ghi duoc lenh vao QEMU"
                    break
                sent = True
                t_sent = time.time()
                t_last = t_sent
            if re.search(r"^(panic|usertrap|kerneltrap)", buf, re.M):
                err = "kernel panic / trap (xem log)"
                break
            if f"exec {prog} failed" in buf:
                err = f"xv6 khong tim thay chuong trinh {prog}"
                break
            if re.search(r"^BENCH_DONE", buf, re.M):
                break
            if sent:
                pos = buf.rfind(command)
                if pos >= 0 and buf[pos + len(command):].rstrip().endswith("$"):
                    err = (f"{prog} da thoat ma khong in BENCH_DONE (sai tham so, hoac file "
                           f"nguon khong phai ban moi? xem dong 'usage' trong log)")
                    break
    finally:
        try:
            os.killpg(p.pid, signal.SIGTERM)
            p.wait(timeout=5)
        except Exception:
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except Exception:
                pass
    return buf, err


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kernel", action="append", required=True, metavar="TEN=DUONG_DAN")
    ap.add_argument("--source", default=None,
                    help="file C cua chuong trinh benchmark (mac dinh: bench.c canh script)")
    ap.add_argument("--workload", default=None,
                    help="mac dinh: cpu,alloc,fork,file,pingpong,burst (hoac cpu neu --source la benchmark.c)")
    ap.add_argument("--cpus", default="1-8")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--total", action="append", default=[], metavar="WORKLOAD=N",
                    help="ghi de khoi luong mac dinh, vd --total cpu=84000000")
    ap.add_argument("--toolprefix", default=None, help="vd riscv64-linux-gnu-")
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--boot-timeout", type=int, default=300)
    ap.add_argument("--run-timeout", type=int, default=900,
                    help="tong thoi gian toi da cho 1 phien benchmark (giay)")
    ap.add_argument("--idle-timeout", type=int, default=180,
                    help="bao treo neu QEMU khong in gi trong ngan nay (giay)")
    ap.add_argument("--log-dir", default=None,
                    help="luu log day du cua tung phien QEMU vao thu muc nay")
    ap.add_argument("--keep-going", action="store_true",
                    help="gap loi thi bo qua cau hinh do va chay tiep (mac dinh: dung lai)")
    ap.add_argument("-o", "--output", default="results.csv")
    args = ap.parse_args()

    kernels = []
    for k in args.kernel:
        name, _, path = k.partition("=")
        if not path:
            sys.exit(f"--kernel can dang TEN=DUONG_DAN, nhan duoc: {k}")
        kernels.append((name, os.path.expanduser(path)))
    here = os.path.dirname(os.path.abspath(__file__))
    source = os.path.expanduser(args.source) if args.source else os.path.join(here, "bench.c")
    prog = os.path.splitext(os.path.basename(source))[0]
    if args.workload is None:
        args.workload = "cpu" if prog == "benchmark" else "cpu,alloc,fork,file,pingpong,burst"
    workloads = args.workload.split(",")
    cpus = parse_cpus(args.cpus)
    totals = dict(t.split("=") for t in args.total)

    host = os.cpu_count() or 1
    if max(cpus) > host:
        print(f"[canh bao] may host chi co {host} core nhung se chay den {max(cpus)} CPU: "
              f"speedup o muc do do se khong phan anh kernel.", file=sys.stderr)

    for _, path in kernels:
        if args.install:
            install_bench(path, source, prog)
        else:
            check_installed(path, prog)

    if args.log_dir:
        os.makedirs(args.log_dir, exist_ok=True)

    failures = []
    with open(args.output, "w", newline="") as out:
        w = csv.writer(out, lineterminator="\n")
        w.writerow(FIELDS)
        for kname, kpath in kernels:
            for wl in workloads:
                for n in cpus:
                    cmd = f"{prog} {kname} {wl} {n} {args.runs}"
                    if wl in totals:
                        cmd += f" {totals[wl]}"
                    tag = f"{kname}/{wl}/cpus={n}"
                    print(f"[{kname}] {wl} cpus={n} ...", file=sys.stderr, flush=True)
                    log, err = run_qemu(kpath, n, cmd, prog, args.toolprefix, args.boot_timeout,
                                        args.run_timeout, args.idle_timeout)
                    if args.log_dir:
                        with open(os.path.join(args.log_dir, f"{kname}_{wl}_{n}.log"), "w") as lf:
                            lf.write(log)
                    lines = [l.strip() for l in log.splitlines()]
                    rows = [m.groups() for m in map(ROW_RE.match, lines) if m]
                    for note in (l for l in lines if l.startswith("# ")):
                        print(f"    {note}", file=sys.stderr)
                    for r in rows:
                        w.writerow(r)
                    out.flush()
                    if err or not rows:
                        err = err or "khong thu duoc dong CSV nao"
                        failures.append((tag, err))
                        print(f"    [LOI] {tag}: {err}", file=sys.stderr)
                        print("    --- 25 dong cuoi cua log ---", file=sys.stderr)
                        for l in lines[-25:]:
                            print(f"    | {l}", file=sys.stderr)
                        print("    ----------------------------", file=sys.stderr)
                        if not args.keep_going:
                            print(f"Dung lai vi loi (them --keep-going de chay tiep). "
                                  f"Ket qua da do duoc luu o {args.output}", file=sys.stderr)
                            sys.exit(1)
    print(f"Da ghi {args.output}", file=sys.stderr)
    if failures:
        print(f"{len(failures)} cau hinh bi loi:", file=sys.stderr)
        for tag, err in failures:
            print(f"  - {tag}: {err}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
