# 基准测试说明

`bench/` 目录保留 `sqlparser` 的基准测试执行器、适配源码和固定历史基线。生成的测试结果不随源码版本发布。

## 相关文件

- `tools/sqlparser_bench.c`
  单次 API 调用 benchmark 二进制。
- `bench/run_benchmarks.py`
  批量执行器与 CSV、Markdown 报告生成脚本。
- `tools/libpg_query_baseline.c`
  vendored `libpg_query` 修改前基线二进制，覆盖单线程成功解析和线程首次解析。
- `bench/run_libpg_query_baseline.py`
  `libpg_query` 基线批量执行器与报告生成脚本。
- `tools/sqlparser_pipeline_bench.c`、`bench/run_pipeline_benchmarks.py`
  批量 INSERT 的解析、建图、patch 构造、改写及输出全流程对比。
- `tools/sqlparser_common_pipeline_bench.c`、`bench/run_common_pipeline_benchmarks.py`
  SELECT、JOIN、UPDATE、DELETE 的常见全流程对比。
- `bench/mysql_bench_adapter.c`、`bench/run_related_benchmarks.py`
  复用单次 API benchmark，显式测试 MySQL 入口。

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

## 全流程与内存检查

`tools/sqlparser_pipeline_bench.c` 的计时包含重新解析、首次 QueryGraph、从图中取得 selector 并构造 patch、一次 apply 和 deparse。patch 分配和替换值构造计入总耗时；输入准备、结果复验与销毁不计入，销毁另列。

5,000 行输入为 133,927 字节，每项替换内容为 49 字节（含 SQL 引号 51 字节），输出为 293,927 字节。`literal` 使用类型化 STRING，`replace` 使用 SQL 片段，两者均核对完整输出。`invalid` 只验证失败并销毁 handle，不再 deparse，其耗时不能与成功流程混比。

```bash
make static
gcc -std=gnu11 -O2 -Iinclude tools/sqlparser_pipeline_bench.c \
  lib/libsqlparser.a -pthread -lm -o bin/sqlparser_pipeline_bench
./bin/sqlparser_pipeline_bench 5000 31 5 literal mysql
./bin/sqlparser_pipeline_bench 5000 31 5 replace mysql
```

参数依次为行数、有效轮数、预热轮数、模式及可选方言。对比时将相同的基准程序分别链接到两份库，使用相同编译参数和 CPU 亲和性，并串行运行。内存插桩与泄漏检查单独执行，不使用其耗时评价性能。

批量脚本为 `run_pipeline_benchmarks.py`、`run_common_pipeline_benchmarks.py` 和 `run_related_benchmarks.py`，参数见各脚本的 `--help`。common 覆盖 SELECT/JOIN/UPDATE/DELETE 全流程；related API 数据采用不同的计时范围。

基准源码和执行器用于复测；逐轮结果、复测日志和阶段实验材料不随源码版本发布。新结果写入 `bench/results/` 或 `build/`，两者不进入源码包。`baselines/` 中既有的历史基线仅用于明确版本间的对照，不代表当前实现。累计分配、峰值活跃请求字节、进程 RSS 和泄漏字节是不同指标。

当前调用规则见[发布说明](../RELEASE_NOTES.md)，泄漏检查入口见[测试说明](../tests/README.md)。
