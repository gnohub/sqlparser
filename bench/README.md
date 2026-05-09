# 基准测试说明

`bench/` 目录包含 `sqlparser` 的单线程基准测试程序、批量执行脚本和结果文件。

## 相关文件

- `tools/sqlparser_bench.c`
  单次 API 调用 benchmark 二进制。
- `bench/run_benchmarks.py`
  批量执行器与 CSV、Markdown 报告生成脚本。
- `tools/libpg_query_baseline.c`
  vendored `libpg_query` 修改前基线二进制，覆盖线程首次解析和并发解析。
- `bench/run_libpg_query_baseline.py`
  `libpg_query` 基线批量执行器与报告生成脚本。

## 主要输出

- `bench/results/<timestamp>/single_call_parse_raw.csv`
- `bench/results/<timestamp>/single_call_parse_median.csv`
- `bench/results/<timestamp>/single_call_api_raw.csv`
- `bench/results/<timestamp>/single_call_api_median.csv`
- `bench/results/<timestamp>/benchmark_summary.md`
- `bench/results/<timestamp>/system_info.txt`
- `bench/results/<timestamp>/methodology.txt`

## 统计口径

- 单线程
- 成功解析样本
- 长度扫表使用 `insert-values`
- 改写链路抽样补充 `update-where`
- 延迟以单次 API 调用为单位统计
- 内存指标为单次调用期间的累计分配、峰值活跃和返回残留

## 覆盖范围

- `parse` 长度扫表
- 原生 `libpg_query` 读取链路对照
- `sqlparser` 读取链路
- `sqlparser` 改写链路
- `sqlparser` 的 `rewrite + deparse` 单次调用开销

## 基本用法

构建 benchmark 程序：

```bash
make bench-build
```

然后执行批量测试：

```bash
python3 ./bench/run_benchmarks.py \
  --output-dir ./bench/results/manual_run \
  --bench-bin ./bin/sqlparser_bench
```

快速烟测：

```bash
make bench-smoke
```

生成 `libpg_query` 修改前基线：

```bash
make libpg-query-baseline BENCH_PROFILE=full
```

可选 profile：

- `--profile full`
- `--profile smoke`
