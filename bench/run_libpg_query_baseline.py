#!/usr/bin/env python3

import argparse
import csv
import io
import os
import platform
import shlex
import shutil
import subprocess
from datetime import datetime
from pathlib import Path


FULL_LENGTHS = [64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536]
SMOKE_LENGTHS = [64, 1024, 8192]
FULL_CONCURRENCY = [1, 2, 4, 8, 16, 32]
SMOKE_CONCURRENCY = [1, 4]


def parse_args():
    parser = argparse.ArgumentParser(description="Run vendored libpg_query baseline measurements.")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--baseline-bin", default="./bin/libpg_query_baseline")
    parser.add_argument("--cc", default="gcc", help="Compiler used to build the baseline binary.")
    parser.add_argument("--profile", choices=("full", "smoke"), default="full")
    parser.add_argument("--keep-existing", action="store_true")
    return parser.parse_args()


def run_command(command):
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "command failed: {}\nstdout:\n{}\nstderr:\n{}".format(
                " ".join(command), completed.stdout, completed.stderr
            )
        )
    return completed.stdout


def parse_rows(output_text):
    reader = csv.DictReader(io.StringIO(output_text))
    return list(reader)


def write_csv(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def as_float(row, field):
    value = row.get(field)
    if value in (None, ""):
        return 0.0
    return float(value)


def as_int(row, field):
    value = row.get(field)
    if value in (None, ""):
        return 0
    return int(float(value))


def fmt_ms(row, field):
    return "{:.4f}".format(as_float(row, field))


def fmt_bytes(value):
    value = float(value)
    if value >= 1024.0 * 1024.0:
        return "{:.2f} MB".format(value / 1024.0 / 1024.0)
    if value >= 1024.0:
        return "{:.2f} KB".format(value / 1024.0)
    return "{:.0f} B".format(value)


def parse_profile(length_bytes, profile):
    if profile == "smoke":
        return (200, 20) if length_bytes <= 1024 else (100, 10)
    if length_bytes <= 2048:
        return 10000, 1000
    if length_bytes <= 8192:
        return 5000, 500
    if length_bytes <= 16384:
        return 2000, 200
    if length_bytes <= 32768:
        return 1000, 100
    return 500, 50


def run_baseline_row(binary, mode, length_bytes, iterations, warmup=0, threads=1):
    command = [
        binary,
        "--mode",
        mode,
        "--length-bytes",
        str(length_bytes),
        "--iterations",
        str(iterations),
        "--threads",
        str(threads),
        "--csv-header",
    ]
    if warmup > 0:
        command.extend(["--warmup", str(warmup)])
    return parse_rows(run_command(command))


def write_system_info(path, cc):
    lines = [
        "timestamp={}".format(datetime.now().isoformat(timespec="seconds")),
        "platform={}".format(platform.platform()),
        "python={}".format(platform.python_version()),
        "machine={}".format(platform.machine()),
        "processor={}".format(platform.processor()),
        "cpu_count={}".format(os.cpu_count()),
    ]
    compiler_command = shlex.split(cc) + ["--version"]
    try:
        output = run_command(["uname", "-srm"]).strip().splitlines()
        if output:
            lines.append("uname={}".format(output[0]))
    except Exception as exc:
        lines.append("uname=<{}>".format(exc))
    try:
        output = run_command(compiler_command).strip().splitlines()
        if output:
            lines.append("compiler_version={}".format(output[0]))
    except Exception:
        lines.append("compiler_version=<unavailable>")
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        for line in cpuinfo.read_text(errors="ignore").splitlines():
            if line.startswith("model name"):
                lines.append("cpu_model={}".format(line.split(":", 1)[1].strip()))
                break
    path.write_text("\n".join(lines) + "\n")


def write_methodology(path, profile):
    path.write_text(
        "\n".join(
            [
                "# libpg_query 基线方法",
                "",
                "- 不修改 vendored `libpg_query` 源码。",
                "- 成功解析样本使用 `INSERT ... VALUES`，按 SQL 字节长度扫表。",
                "- 单线程基线统计单次 `pg_query_parse_protobuf()` 加结果释放的成本。",
                "- 线程首次基线在新线程内连续解析两次，分别记录第一次和第二次调用成本。",
                "- 并发成功基线使用多个线程同时解析同一条只读 SQL。",
                "- 并发错误基线使用多个线程同时解析语法错误 SQL，校验错误路径稳定性。",
                "- 内存统计来自链接器 `--wrap` 包装的 malloc/calloc/realloc/free/strdup/strndup。",
                "- 延迟单位为毫秒；内存 CSV 使用字节，Markdown 汇总转换为 KB/MB。",
                "- profile: `{}`。".format(profile),
                "",
            ]
        )
    )


def write_summary(path, single_rows, thread_rows, concurrent_rows, error_rows):
    lines = [
        "# libpg_query 当前基线汇总",
        "",
        "本报告用于记录修改 vendored `libpg_query` 之前的行为基线，后续 patch 需要和这些数据对比。",
        "",
        "## 单线程成功解析长度扫表",
        "",
        "| SQL 长度 | 平均耗时(ms) | P95(ms) | P99(ms) | 平均分配 | P95 峰值活跃 | P95 返回残留 |",
        "|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in single_rows:
        lines.append(
            "| {} | {} | {} | {} | {} | {} | {} |".format(
                as_int(row, "actual_sql_bytes"),
                fmt_ms(row, "avg_ms"),
                fmt_ms(row, "p95_ms"),
                fmt_ms(row, "p99_ms"),
                fmt_bytes(as_float(row, "avg_alloc_bytes")),
                fmt_bytes(as_float(row, "p95_peak_live_bytes")),
                fmt_bytes(as_float(row, "p95_retained_bytes")),
            )
        )

    lines.extend(
        [
            "",
            "## 线程首次解析基线",
            "",
            "| 阶段 | 平均耗时(ms) | P95(ms) | 平均分配 | P95 返回残留 | 异常次数 |",
            "|---|---:|---:|---:|---:|---:|",
        ]
    )
    for row in thread_rows:
        lines.append(
            "| {} | {} | {} | {} | {} | {} |".format(
                row["mode"],
                fmt_ms(row, "avg_ms"),
                fmt_ms(row, "p95_ms"),
                fmt_bytes(as_float(row, "avg_alloc_bytes")),
                fmt_bytes(as_float(row, "p95_retained_bytes")),
                as_int(row, "unexpected_operations"),
            )
        )

    lines.extend(
        [
            "",
            "## 多线程成功解析基线",
            "",
            "| 线程数 | 吞吐(ops/s) | 平均耗时(ms) | P95(ms) | P99(ms) | 异常次数 |",
            "|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for row in concurrent_rows:
        lines.append(
            "| {} | {:.2f} | {} | {} | {} | {} |".format(
                as_int(row, "threads"),
                as_float(row, "throughput_ops_s"),
                fmt_ms(row, "avg_ms"),
                fmt_ms(row, "p95_ms"),
                fmt_ms(row, "p99_ms"),
                as_int(row, "unexpected_operations"),
            )
        )

    lines.extend(
        [
            "",
            "## 多线程错误路径基线",
            "",
            "| 线程数 | 吞吐(ops/s) | 平均耗时(ms) | P95(ms) | 实际 parse error 次数 | 错误信息异常次数 | 异常次数 |",
            "|---:|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for row in error_rows:
        lines.append(
            "| {} | {:.2f} | {} | {} | {} | {} | {} |".format(
                as_int(row, "threads"),
                as_float(row, "throughput_ops_s"),
                fmt_ms(row, "avg_ms"),
                fmt_ms(row, "p95_ms"),
                as_int(row, "actual_parse_errors"),
                as_int(row, "bad_error_messages"),
                as_int(row, "unexpected_operations"),
            )
        )

    lines.extend(
        [
            "",
            "## 后续对比口径",
            "",
            "- 单线程 P95 不应退化。",
            "- 线程首次解析耗时和返回残留应下降或保持稳定。",
            "- 多线程错误路径不能出现错误信息异常或 unexpected operation。",
            "- 多线程吞吐不应因为修复进程级副作用而明显下降。",
            "",
        ]
    )
    path.write_text("\n".join(lines))


def main():
    args = parse_args()
    output_dir = Path(args.output_dir)
    if output_dir.exists() and not args.keep_existing:
        shutil.rmtree(str(output_dir))
    output_dir.mkdir(parents=True, exist_ok=True)

    lengths = FULL_LENGTHS if args.profile == "full" else SMOKE_LENGTHS
    concurrency = FULL_CONCURRENCY if args.profile == "full" else SMOKE_CONCURRENCY
    thread_samples = 200 if args.profile == "full" else 20
    concurrent_iterations = 1000 if args.profile == "full" else 100
    error_iterations = 200 if args.profile == "full" else 20

    single_rows = []
    for length in lengths:
        iterations, warmup = parse_profile(length, args.profile)
        single_rows.extend(
            run_baseline_row(
                args.baseline_bin,
                "single-success",
                length,
                iterations,
                warmup=warmup,
                threads=1,
            )
        )

    thread_rows = run_baseline_row(
        args.baseline_bin,
        "thread-init",
        1024,
        thread_samples,
        warmup=0,
        threads=1,
    )

    concurrent_rows = []
    error_rows = []
    for threads in concurrency:
        concurrent_rows.extend(
            run_baseline_row(
                args.baseline_bin,
                "concurrent-success",
                1024,
                concurrent_iterations,
                warmup=0,
                threads=threads,
            )
        )
        error_rows.extend(
            run_baseline_row(
                args.baseline_bin,
                "concurrent-error",
                1024,
                error_iterations,
                warmup=0,
                threads=threads,
            )
        )

    write_csv(output_dir / "single_success.csv", single_rows)
    write_csv(output_dir / "thread_init.csv", thread_rows)
    write_csv(output_dir / "concurrent_success.csv", concurrent_rows)
    write_csv(output_dir / "concurrent_error.csv", error_rows)
    write_system_info(output_dir / "system_info.txt", args.cc)
    write_methodology(output_dir / "methodology.md", args.profile)
    write_summary(output_dir / "summary.md", single_rows, thread_rows, concurrent_rows, error_rows)
    print("libpg_query baseline written to {}".format(output_dir))


if __name__ == "__main__":
    main()
