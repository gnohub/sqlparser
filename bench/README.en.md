# Benchmark Guide

The `bench/` directory contains the single-thread benchmark program, the batch runner, and generated result files.

## Key Files

- `tools/sqlparser_bench.c`
  Benchmark binary for single API-call measurements.
- `bench/run_benchmarks.py`
  Batch runner that produces CSV and Markdown reports.

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
- fixed SQL-length buckets
- latency measured per API call
- memory measured as per-call allocation, peak live bytes, and retained bytes

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
