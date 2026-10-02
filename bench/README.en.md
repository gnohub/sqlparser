# Benchmark Guide

The `bench/` directory retains benchmark runners, adapter sources and fixed historical baselines. Generated test results are not included in source releases.

## Key Files

- `tools/sqlparser_bench.c`
  Benchmark binary for single API-call measurements.
- `bench/run_benchmarks.py`
  Batch runner that produces CSV and Markdown reports.
- `tools/libpg_query_baseline.c`
  Pre-patch baseline binary for the vendored `libpg_query`, including
  single-thread success and first-parse measurements.
- `bench/run_libpg_query_baseline.py`
  Batch runner and report generator for the `libpg_query` baseline.
- `tools/sqlparser_pipeline_bench.c`, `bench/run_pipeline_benchmarks.py`
  Full batch-INSERT comparisons covering parse, graph, patch construction, rewrite and output.
- `tools/sqlparser_common_pipeline_bench.c`, `bench/run_common_pipeline_benchmarks.py`
  Common SELECT, JOIN, UPDATE and DELETE pipeline comparisons.
- `bench/mysql_bench_adapter.c`, `bench/run_related_benchmarks.py`
  Reuse the single-API benchmark with an explicit MySQL dialect.

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

## Full-Pipeline and Memory Checks

`tools/sqlparser_pipeline_bench.c` measures a fresh parse, first QueryGraph, graph-derived selector and patch construction, one apply, and deparse. Patch allocation and replacement construction are timed; input preparation, result verification and cleanup are outside the total. Cleanup is reported separately.

The 5,000-row input is 133,927 bytes. Each replacement holds 49 content bytes (51 with SQL quotes), producing 293,927 output bytes. `literal` uses typed STRING values; `replace` uses SQL fragments. Both validate the complete output. `invalid` checks failure and destroys the handle without deparsing; its timing is not comparable to a successful pipeline.

```bash
make static
gcc -std=gnu11 -O2 -Iinclude tools/sqlparser_pipeline_bench.c \
  lib/libsqlparser.a -pthread -lm -o bin/sqlparser_pipeline_bench
./bin/sqlparser_pipeline_bench 5000 31 5 literal mysql
./bin/sqlparser_pipeline_bench 5000 31 5 replace mysql
```

Arguments are rows, measured runs, warmup runs, mode and optional dialect. Link the same harness source against each library, use identical compiler settings and CPU affinity, and run serially. Memory instrumentation and leak checks run separately; their timings are not performance measurements.

Batch runners are `run_pipeline_benchmarks.py`, `run_common_pipeline_benchmarks.py` and `run_related_benchmarks.py`; use `--help` for options. The common runner covers SELECT/JOIN/UPDATE/DELETE pipelines; related API measurements use a different timing scope.

Benchmark sources and runners support reproducible checks. Per-run results, recheck logs and intermediate experiments are not shipped with source releases. Write new output under `bench/results/` or `build/`, which are excluded from source packages. Existing historical data in `baselines/` supports explicitly versioned comparisons and does not describe the current implementation. Cumulative allocations, peak live requested bytes, process RSS and leaked bytes are distinct metrics.

Current calling rules are in the [Release notes](../RELEASE_NOTES.en.md). Leak-check entry points are in the [Test guide](../tests/README.en.md).
