#!/usr/bin/env python3

import argparse
import csv
import io
import os
import platform
import shutil
import statistics
import subprocess
from collections import defaultdict
from datetime import datetime
from pathlib import Path


PARSE_WORKLOAD = "insert-values"

FULL_PARSE_LENGTHS = [64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536]
SMOKE_PARSE_LENGTHS = [64, 1024, 8192]
PARSE_MODES = [
    "native-parse",
    "sqlparser-parse",
]
FULL_PARSE_REPEAT = 3
SMOKE_PARSE_REPEAT = 1

FULL_API_LENGTHS = [1024, 8192, 65536]
SMOKE_API_LENGTHS = [1024]
FULL_API_REPEAT = 3
SMOKE_API_REPEAT = 1

API_CASES = [
    {"section": "read", "mode": "native-deparse", "workload": "insert-values"},
    {"section": "read", "mode": "sqlparser-view-json", "workload": "insert-values"},
    {"section": "read", "mode": "sqlparser-deparse", "workload": "insert-values"},
    {"section": "rewrite", "mode": "sqlparser-update-assignment-literal", "workload": "update-where"},
    {"section": "rewrite", "mode": "sqlparser-update-assignment-sql", "workload": "update-where"},
    {"section": "rewrite", "mode": "sqlparser-update-rewrite-deparse", "workload": "update-where"},
    {"section": "rewrite", "mode": "sqlparser-insert-cell-literal", "workload": "insert-values"},
    {"section": "rewrite", "mode": "sqlparser-insert-cell-sql", "workload": "insert-values"},
    {"section": "rewrite", "mode": "sqlparser-insert-rewrite-deparse", "workload": "insert-values"},
]

NUMERIC_FIELDS = [
    "target_sql_bytes",
    "actual_sql_bytes",
    "iterations",
    "warmup",
    "total_operations",
    "ok_operations",
    "error_operations",
    "wall_total_s",
    "throughput_ops_s",
    "avg_us",
    "min_us",
    "p50_us",
    "p95_us",
    "p99_us",
    "max_us",
    "avg_alloc_bytes",
    "p50_alloc_bytes",
    "p95_alloc_bytes",
    "max_alloc_bytes",
    "avg_peak_live_bytes",
    "p50_peak_live_bytes",
    "p95_peak_live_bytes",
    "max_peak_live_bytes",
    "avg_retained_bytes",
    "p50_retained_bytes",
    "p95_retained_bytes",
    "max_retained_bytes",
]

INTEGER_FIELDS = {
    "target_sql_bytes",
    "actual_sql_bytes",
    "iterations",
    "warmup",
    "total_operations",
    "ok_operations",
    "error_operations",
    "p50_alloc_bytes",
    "p95_alloc_bytes",
    "max_alloc_bytes",
    "p50_peak_live_bytes",
    "p95_peak_live_bytes",
    "max_peak_live_bytes",
    "p50_retained_bytes",
    "p95_retained_bytes",
    "max_retained_bytes",
}

SUMMARY_NUMERIC_FIELDS = [field for field in NUMERIC_FIELDS if field != "target_sql_bytes"]


def parse_args():
    parser = argparse.ArgumentParser(description="Run sqlparser API-call benchmarks and export CSV results.")
    parser.add_argument("--output-dir", required=True, help="Directory for benchmark outputs.")
    parser.add_argument("--bench-bin", default="./bin/sqlparser_bench", help="Path to benchmark binary.")
    parser.add_argument(
        "--profile",
        choices=("full", "smoke"),
        default="full",
        help="Benchmark profile. 'full' runs the complete matrix, 'smoke' runs a fast regression subset.",
    )
    parser.add_argument(
        "--stages",
        default="all",
        help="Comma-separated stages: parse,api,report or all",
    )
    parser.add_argument(
        "--keep-existing",
        action="store_true",
        help="Reuse an existing output directory instead of deleting it",
    )
    return parser.parse_args()


def run_command(command, cwd=None):
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "command failed: {}\nstdout:\n{}\nstderr:\n{}".format(
                " ".join(command),
                completed.stdout,
                completed.stderr,
            )
        )
    return completed.stdout


def parse_profile(length_bytes):
    if length_bytes <= 2048:
        return 10000, 1000
    if length_bytes <= 8192:
        return 5000, 500
    if length_bytes <= 16384:
        return 2000, 200
    if length_bytes <= 32768:
        return 1000, 100
    return 500, 50


def api_profile(length_bytes):
    if length_bytes <= 1024:
        return 2000, 200
    if length_bytes <= 8192:
        return 1000, 100
    return 300, 30


def smoke_parse_profile(length_bytes):
    if length_bytes <= 1024:
        return 200, 20
    return 100, 10


def smoke_api_profile(length_bytes):
    if length_bytes <= 1024:
        return 100, 10
    return 50, 5


def parse_lengths_for_profile(profile):
    return FULL_PARSE_LENGTHS if profile == "full" else SMOKE_PARSE_LENGTHS


def parse_repeat_for_profile(profile):
    return FULL_PARSE_REPEAT if profile == "full" else SMOKE_PARSE_REPEAT


def api_lengths_for_profile(profile):
    return FULL_API_LENGTHS if profile == "full" else SMOKE_API_LENGTHS


def api_repeat_for_profile(profile):
    return FULL_API_REPEAT if profile == "full" else SMOKE_API_REPEAT


def parse_profile_for_mode(profile, length_bytes):
    if profile == "full":
        return parse_profile(length_bytes)
    return smoke_parse_profile(length_bytes)


def api_profile_for_mode(profile, length_bytes):
    if profile == "full":
        return api_profile(length_bytes)
    return smoke_api_profile(length_bytes)


def parse_csv_row(output_text):
    reader = csv.DictReader(io.StringIO(output_text))
    rows = list(reader)
    if len(rows) != 1:
        raise RuntimeError("expected exactly one CSV row, got {}".format(len(rows)))
    row = rows[0]
    for field in NUMERIC_FIELDS:
        if field in row:
            if field in INTEGER_FIELDS:
                row[field] = int(float(row[field]))
            else:
                row[field] = float(row[field])
    return row


def ensure_empty_dir(path):
    if path.exists():
        shutil.rmtree(str(path))
    path.mkdir(parents=True, exist_ok=True)


def selected_stages(stage_text):
    values = {item.strip() for item in stage_text.split(",") if item.strip()}
    if not values or "all" in values:
        return {"parse", "api", "report"}
    return values


def median(values):
    return statistics.median(values) if values else 0


def write_csv(path, rows, fieldnames):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def read_csv_rows(path):
    rows = []
    if not path.exists():
        return rows
    with path.open("r", newline="") as source:
        reader = csv.DictReader(source)
        for row in reader:
            for field in NUMERIC_FIELDS:
                if field in row and row[field] not in ("", None):
                    if field in INTEGER_FIELDS:
                        row[field] = int(float(row[field]))
                    else:
                        row[field] = float(row[field])
            for field in ("repeat_index", "repeat_count"):
                if field in row and row[field] not in ("", None):
                    row[field] = int(float(row[field]))
            rows.append(row)
    return rows


def aggregate_rows(rows, key_fields):
    grouped = defaultdict(list)
    for row in rows:
        grouped[tuple(row[field] for field in key_fields)].append(row)

    summary_rows = []
    for key, group_rows in sorted(grouped.items()):
        summary = {field: key[index] for index, field in enumerate(key_fields)}
        summary["repeat_count"] = len(group_rows)
        summary["error_message"] = next((row["error_message"] for row in group_rows if row["error_message"]), "")
        for field in NUMERIC_FIELDS:
            values = [row[field] for row in group_rows]
            if field in INTEGER_FIELDS:
                summary[field] = int(round(median(values)))
            else:
                summary[field] = float(median(values))
        summary_rows.append(summary)
    return summary_rows


def benchmark_case(bench_bin, mode, workload, length_bytes, iterations, warmup, sample_sql_path=None):
    command = [
        "taskset",
        "-c",
        "0",
        bench_bin,
        "--csv-header",
        "--mode",
        mode,
        "--workload",
        workload,
        "--length-bytes",
        str(length_bytes),
        "--iterations",
        str(iterations),
        "--warmup",
        str(warmup),
    ]
    if sample_sql_path is not None:
        command.extend(["--dump-sql", str(sample_sql_path)])

    row = parse_csv_row(run_command(command))
    if row["actual_sql_bytes"] != length_bytes:
        raise RuntimeError(
            "generated SQL length mismatch for {} {}B: target={}, actual={}".format(
                mode,
                length_bytes,
                length_bytes,
                row["actual_sql_bytes"],
            )
        )
    if row["error_operations"] != 0:
        raise RuntimeError(
            "benchmark case reported errors for {} {}B: {}".format(
                mode,
                length_bytes,
                row["error_message"],
            )
        )
    return row


def write_system_info(output_dir):
    system_info = []
    system_info.append("timestamp={}".format(datetime.utcnow().isoformat() + "Z"))
    system_info.append("platform={}".format(platform.platform()))
    for command in [
        ["uname", "-a"],
        ["bash", "-lc", "hostname"],
        ["bash", "-lc", "grep -m1 'model name' /proc/cpuinfo"],
        ["bash", "-lc", "gcc --version | head -n 1"],
    ]:
        try:
            output = run_command(command).strip()
        except RuntimeError as exc:
            output = str(exc)
        system_info.append("$ {}".format(" ".join(command)))
        system_info.append(output)
    (output_dir / "system_info.txt").write_text("\n".join(system_info) + "\n")


def write_methodology(output_dir, profile):
    if profile == "full":
        parse_text = "- `parse` 全长扫表：64B 到 64KB，3 次重复"
        parse_scale = "- `parse` 采样规模：<=2KB 为 10000/1000 warmup；<=8KB 为 5000/500；<=16KB 为 2000/200；<=32KB 为 1000/100；64KB 为 500/50"
        api_text = "- 其他单次 API 抽样：1KB 2000/200；8KB 1000/100；64KB 300/30，3 次重复"
    else:
        parse_text = "- `parse` 烟测：64B、1KB、8KB，1 次重复"
        parse_scale = "- `parse` 采样规模：1KB 以内 200/20 warmup；8KB 100/10"
        api_text = "- 其他单次 API 烟测：1KB，100/10，1 次重复"

    text = """基准测试方法
- 构建模式：release（`DEBUG=0 SHOW_WARNING=0`）
- 执行环境：以实际测试环境为准
- 并发模型：单线程、单次 API 调用统计
- 成功样本：仅统计可成功解析的 SQL，不包含解析失败场景；长度扫表使用 `insert-values`，改写抽样补充 `update-where`
- profile：{profile}
{parse_text}
{parse_scale}
{api_text}
- 延迟统计：每次 API 调用用 `CLOCK_MONOTONIC` 单独计时
- 内存统计：benchmark 二进制通过 `malloc/calloc/realloc/free/strdup/strndup` 链接包装，统计单次调用期间的累计分配字节数、峰值活跃字节数、返回时残留字节数
- `avg_alloc_bytes`：单次调用内部累计申请的堆内存
- `avg_peak_live_bytes`：单次调用内部同时存活的堆内存峰值
- `avg_retained_bytes`：API 返回时仍归调用方持有、尚未释放的堆内存
""".format(
        profile=profile,
        parse_text=parse_text,
        parse_scale=parse_scale,
        api_text=api_text,
    )
    (output_dir / "methodology.txt").write_text(text)


def bytes_to_kib(value):
    return value / 1024.0


def us_to_ms(value):
    return value / 1000.0


def format_bytes_human(value):
    kib = value / 1024.0
    if kib >= 1024.0:
        return "{:.3f} MiB".format(kib / 1024.0)
    return "{:.2f} KiB".format(kib)


def build_report(output_dir, parse_summary, api_summary):
    lines = []
    lines.append("# 基准测试汇总")
    lines.append("")
    lines.append("本轮 benchmark 统计的是单次 API 调用本身的延迟和内存损耗，不再使用进程 RSS 作为结论指标。")
    lines.append("所有测试样本均为可成功解析的 SQL；长度扫表使用 `insert-values`，改写抽样补充 `update-where`。")
    lines.append("")
    lines.append("## 指标说明")
    lines.append("")
    lines.append("- 平均分配：单次调用期间累计申请的堆内存。")
    lines.append("- 平均峰值活跃：单次调用期间同时存活的堆内存峰值。")
    lines.append("- 平均返回残留：API 返回时仍由调用方持有、尚未释放的堆内存。")
    lines.append("")

    def find_row(rows, mode, workload, length_bytes):
        for row in rows:
            if row["mode"] == mode and row["workload"] == workload and row["target_sql_bytes"] == length_bytes:
                return row
        return None

    lines.append("## parse 全长扫表")
    lines.append("")
    lines.append("| 模式 | SQL 长度（字节） | 平均耗时（ms） | P95（ms） | 平均分配 | 平均峰值活跃 | 平均返回残留 | 吞吐（ops/s） |")
    lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for mode in PARSE_MODES:
        for length_bytes in FULL_PARSE_LENGTHS:
            row = find_row(parse_summary, mode, PARSE_WORKLOAD, length_bytes)
            if row is None:
                continue
            lines.append(
                "| {} | {} | {:.3f} | {:.3f} | {} | {} | {} | {:.4f} |".format(
                    mode,
                    length_bytes,
                    us_to_ms(row["avg_us"]),
                    us_to_ms(row["p95_us"]),
                    format_bytes_human(row["avg_alloc_bytes"]),
                    format_bytes_human(row["avg_peak_live_bytes"]),
                    format_bytes_human(row["avg_retained_bytes"]),
                    row["throughput_ops_s"],
                )
            )

    lines.append("")
    lines.append("## 真实接口链路抽样")
    lines.append("")
    lines.append("| 模式 | workload | SQL 长度（字节） | 平均耗时（ms） | P95（ms） | 平均分配 | 平均峰值活跃 | 平均返回残留 | 吞吐（ops/s） |")
    lines.append("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for case in [item for item in API_CASES if item["section"] == "read"]:
        for length_bytes in FULL_API_LENGTHS:
            row = find_row(api_summary, case["mode"], case["workload"], length_bytes)
            if row is None:
                continue
            lines.append(
                "| {} | {} | {} | {:.3f} | {:.3f} | {} | {} | {} | {:.4f} |".format(
                    case["mode"],
                    case["workload"],
                    length_bytes,
                    us_to_ms(row["avg_us"]),
                    us_to_ms(row["p95_us"]),
                    format_bytes_human(row["avg_alloc_bytes"]),
                    format_bytes_human(row["avg_peak_live_bytes"]),
                    format_bytes_human(row["avg_retained_bytes"]),
                    row["throughput_ops_s"],
                )
            )

    lines.append("")
    lines.append("## 真实接口改写链路抽样")
    lines.append("")
    lines.append("| 模式 | workload | SQL 长度（字节） | 平均耗时（ms） | P95（ms） | 平均分配 | 平均峰值活跃 | 平均返回残留 | 吞吐（ops/s） |")
    lines.append("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for case in [item for item in API_CASES if item["section"] == "rewrite"]:
        for length_bytes in FULL_API_LENGTHS:
            row = find_row(api_summary, case["mode"], case["workload"], length_bytes)
            if row is None:
                continue
            lines.append(
                "| {} | {} | {} | {:.3f} | {:.3f} | {} | {} | {} | {:.4f} |".format(
                    case["mode"],
                    case["workload"],
                    length_bytes,
                    us_to_ms(row["avg_us"]),
                    us_to_ms(row["p95_us"]),
                    format_bytes_human(row["avg_alloc_bytes"]),
                    format_bytes_human(row["avg_peak_live_bytes"]),
                    format_bytes_human(row["avg_retained_bytes"]),
                    row["throughput_ops_s"],
                )
            )

    (output_dir / "benchmark_summary.md").write_text("\n".join(lines) + "\n")


def main():
    args = parse_args()
    stages = selected_stages(args.stages)
    output_dir = Path(args.output_dir).resolve()
    samples_dir = output_dir / "samples"
    parse_raw_path = output_dir / "single_call_parse_raw.csv"
    parse_summary_path = output_dir / "single_call_parse_median.csv"
    api_raw_path = output_dir / "single_call_api_raw.csv"
    api_summary_path = output_dir / "single_call_api_median.csv"

    if args.keep_existing:
        output_dir.mkdir(parents=True, exist_ok=True)
        samples_dir.mkdir(parents=True, exist_ok=True)
    else:
        ensure_empty_dir(output_dir)
        samples_dir.mkdir(parents=True, exist_ok=True)

    if not (output_dir / "system_info.txt").exists():
        write_system_info(output_dir)
    if not (output_dir / "methodology.txt").exists():
        write_methodology(output_dir, args.profile)

    common_fieldnames = ["series", "repeat_index", "mode", "workload"] + NUMERIC_FIELDS + ["error_message"]
    parse_summary_rows = []
    api_summary_rows = []

    if "parse" in stages:
        parse_raw_rows = []
        for length_bytes in parse_lengths_for_profile(args.profile):
            sample_path = samples_dir / "{}_{}.sql".format(PARSE_WORKLOAD, length_bytes)
            for mode in PARSE_MODES:
                iterations, warmup = parse_profile_for_mode(args.profile, length_bytes)
                for repeat_index in range(1, parse_repeat_for_profile(args.profile) + 1):
                    row = benchmark_case(
                        args.bench_bin,
                        mode,
                        PARSE_WORKLOAD,
                        length_bytes,
                        iterations,
                        warmup,
                        sample_sql_path=sample_path if mode == PARSE_MODES[0] and repeat_index == 1 else None,
                    )
                    row["repeat_index"] = repeat_index
                    row["series"] = "single_call_parse"
                    parse_raw_rows.append(row)
        write_csv(parse_raw_path, parse_raw_rows, common_fieldnames)

        parse_summary_rows = []
        for mode in PARSE_MODES:
            for length_bytes in parse_lengths_for_profile(args.profile):
                group = [
                    row
                    for row in parse_raw_rows
                    if row["mode"] == mode and row["workload"] == PARSE_WORKLOAD and row["target_sql_bytes"] == length_bytes
                ]
                if group:
                    parse_summary_rows.append(aggregate_rows(group, ["mode", "workload", "target_sql_bytes"])[0])
        write_csv(
            parse_summary_path,
            parse_summary_rows,
            ["mode", "workload", "target_sql_bytes", "repeat_count"] + SUMMARY_NUMERIC_FIELDS + ["error_message"],
        )
    elif parse_raw_path.exists():
        parse_raw_rows = read_csv_rows(parse_raw_path)
        for mode in PARSE_MODES:
            for length_bytes in parse_lengths_for_profile(args.profile):
                group = [
                    row
                    for row in parse_raw_rows
                    if row["mode"] == mode and row["workload"] == PARSE_WORKLOAD and row["target_sql_bytes"] == length_bytes
                ]
                if group:
                    parse_summary_rows.append(aggregate_rows(group, ["mode", "workload", "target_sql_bytes"])[0])
        write_csv(
            parse_summary_path,
            parse_summary_rows,
            ["mode", "workload", "target_sql_bytes", "repeat_count"] + SUMMARY_NUMERIC_FIELDS + ["error_message"],
        )
    elif parse_summary_path.exists():
        parse_summary_rows = read_csv_rows(parse_summary_path)

    if "api" in stages:
        api_raw_rows = []
        for case in API_CASES:
            for length_bytes in api_lengths_for_profile(args.profile):
                sample_path = samples_dir / "api_{}_{}_{}.sql".format(case["workload"], case["mode"], length_bytes)
                iterations, warmup = api_profile_for_mode(args.profile, length_bytes)
                for repeat_index in range(1, api_repeat_for_profile(args.profile) + 1):
                    row = benchmark_case(
                        args.bench_bin,
                        case["mode"],
                        case["workload"],
                        length_bytes,
                        iterations,
                        warmup,
                        sample_sql_path=sample_path if repeat_index == 1 else None,
                    )
                    row["repeat_index"] = repeat_index
                    row["series"] = "single_call_api"
                    api_raw_rows.append(row)
        write_csv(api_raw_path, api_raw_rows, common_fieldnames)

        api_summary_rows = []
        for case in API_CASES:
            for length_bytes in api_lengths_for_profile(args.profile):
                group = [
                    row
                    for row in api_raw_rows
                    if row["mode"] == case["mode"] and row["workload"] == case["workload"] and row["target_sql_bytes"] == length_bytes
                ]
                if group:
                    api_summary_rows.append(aggregate_rows(group, ["mode", "workload", "target_sql_bytes"])[0])
        write_csv(
            api_summary_path,
            api_summary_rows,
            ["mode", "workload", "target_sql_bytes", "repeat_count"] + SUMMARY_NUMERIC_FIELDS + ["error_message"],
        )
    elif api_raw_path.exists():
        api_raw_rows = read_csv_rows(api_raw_path)
        for case in API_CASES:
            for length_bytes in api_lengths_for_profile(args.profile):
                group = [
                    row
                    for row in api_raw_rows
                    if row["mode"] == case["mode"] and row["workload"] == case["workload"] and row["target_sql_bytes"] == length_bytes
                ]
                if group:
                    api_summary_rows.append(aggregate_rows(group, ["mode", "workload", "target_sql_bytes"])[0])
        write_csv(
            api_summary_path,
            api_summary_rows,
            ["mode", "workload", "target_sql_bytes", "repeat_count"] + SUMMARY_NUMERIC_FIELDS + ["error_message"],
        )
    elif api_summary_path.exists():
        api_summary_rows = read_csv_rows(api_summary_path)

    if "report" in stages:
        build_report(output_dir, parse_summary_rows, api_summary_rows)


if __name__ == "__main__":
    main()
