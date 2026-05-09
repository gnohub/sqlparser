# Benchmark Guide

The `bench/` directory contains the single-thread benchmark program, the batch runner, and generated result files.

## Key Files

- `tools/sqlparser_bench.c`
  Benchmark binary for single API-call measurements.
- `bench/run_benchmarks.py`
  Batch runner that produces CSV and Markdown reports.
- `tools/libpg_query_baseline.c`
  Pre-patch baseline binary for the vendored `libpg_query`, including
  first-parse and concurrent-parse measurements.
- `bench/run_libpg_query_baseline.py`
  Batch runner and report generator for the `libpg_query` baseline.

## Main Outputs

- `bench/results/<timestamp>/single_call_parse_raw.csv`
- `bench/results/<timestamp>/single_call_parse_median.csv`
- `bench/results/<timestamp>/single_call_api_raw.csv`
- `bench/results/<timestamp>/single_call_api_median.csv`
- `bench/results/<timestamp>/benchmark_summary.md`
- `bench/results/<timestamp>/system_info.txt`
- `bench/results/<timestamp>/methodology.txt`

## Methodology

- single-thread execution
- success-only SQL samples
- `insert-values` for length sweeps
- `update-where` added for rewrite-path sampling
- latency measured per API call
- memory measured as per-call allocation, peak live bytes, and retained bytes

## Coverage

- parse length sweep
- native `libpg_query` read-path baseline
- `sqlparser` read-path measurements
- `sqlparser` rewrite-path measurements
- single-call `rewrite + deparse` cost

## Basic Usage

Build the benchmark binary:

```bash
make bench-build
```

Run the batch benchmark:

```bash
python3 ./bench/run_benchmarks.py \
  --output-dir ./bench/results/manual_run \
  --bench-bin ./bin/sqlparser_bench
```

Quick smoke run:

```bash
make bench-smoke
```

Generate the pre-patch `libpg_query` baseline:

```bash
make libpg-query-baseline BENCH_PROFILE=full
```

Available profiles:

- `--profile full`
- `--profile smoke`
