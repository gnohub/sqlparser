# 基准测试汇总

本轮 benchmark 统计的是单次 API 调用本身的延迟和内存损耗，不再使用进程 RSS 作为结论指标。
所有测试样本均为可成功解析的 `insert-values` 语句。

## 指标说明

- 平均分配：单次调用期间累计申请的堆内存。
- 平均峰值活跃：单次调用期间同时存活的堆内存峰值。
- 平均返回残留：API 返回时仍由调用方持有、尚未释放的堆内存。

## parse 全长扫表

| 模式 | SQL 长度（字节） | 平均耗时（ms） | P95（ms） | 平均分配 | 平均峰值活跃 | 平均返回残留 | 吞吐（ops/s） |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| native-parse | 64 | 0.017 | 0.017 | 0.10 KiB | 0.10 KiB | 0.10 KiB | 57970.2795 |
| native-parse | 128 | 0.046 | 0.051 | 8.26 KiB | 8.26 KiB | 0.26 KiB | 21564.1236 |
| native-parse | 256 | 0.097 | 0.102 | 8.56 KiB | 8.56 KiB | 0.56 KiB | 10313.6603 |
| native-parse | 512 | 0.193 | 0.198 | 25.12 KiB | 25.12 KiB | 1.12 KiB | 5181.6173 |
| native-parse | 1024 | 0.382 | 0.392 | 58.25 KiB | 58.25 KiB | 2.25 KiB | 2618.0791 |
| native-parse | 2048 | 0.736 | 0.751 | 124.41 KiB | 124.41 KiB | 4.41 KiB | 1359.0858 |
| native-parse | 4096 | 1.408 | 1.441 | 264.66 KiB | 264.66 KiB | 8.66 KiB | 707.1807 |
| native-parse | 8192 | 2.790 | 2.844 | 529.19 KiB | 521.13 KiB | 17.13 KiB | 356.2585 |
| native-parse | 16384 | 5.541 | 5.599 | 1.041 MiB | 1.025 MiB | 34.08 KiB | 179.2814 |
| native-parse | 32768 | 10.511 | 10.609 | 2.119 MiB | 2.087 MiB | 67.00 KiB | 94.4514 |
| native-parse | 65536 | 20.177 | 20.299 | 4.258 MiB | 4.180 MiB | 132.67 KiB | 49.2412 |
| sqlparser-parse | 64 | 0.021 | 0.021 | 0.39 KiB | 0.38 KiB | 0.28 KiB | 47569.1064 |
| sqlparser-parse | 128 | 0.053 | 0.058 | 8.78 KiB | 8.26 KiB | 0.50 KiB | 18722.8777 |
| sqlparser-parse | 256 | 0.110 | 0.116 | 9.49 KiB | 8.56 KiB | 0.92 KiB | 9031.3466 |
| sqlparser-parse | 512 | 0.218 | 0.228 | 34.87 KiB | 25.12 KiB | 1.73 KiB | 4574.6439 |
| sqlparser-parse | 1024 | 0.432 | 0.443 | 85.63 KiB | 58.25 KiB | 3.36 KiB | 2314.4630 |
| sqlparser-parse | 2048 | 0.829 | 0.844 | 154.95 KiB | 124.41 KiB | 6.52 KiB | 1206.5002 |
| sqlparser-parse | 4096 | 1.596 | 1.643 | 341.44 KiB | 264.66 KiB | 12.77 KiB | 626.2904 |
| sqlparser-parse | 8192 | 3.153 | 3.208 | 682.50 KiB | 521.13 KiB | 25.25 KiB | 317.0949 |
| sqlparser-parse | 16384 | 6.260 | 6.328 | 1.348 MiB | 1.025 MiB | 50.19 KiB | 159.7223 |
| sqlparser-parse | 32768 | 11.866 | 11.957 | 2.755 MiB | 2.087 MiB | 99.11 KiB | 84.2688 |
| sqlparser-parse | 65536 | 22.858 | 23.001 | 5.552 MiB | 4.180 MiB | 196.78 KiB | 43.7477 |

## 真实接口链路抽样

| 模式 | SQL 长度（字节） | 平均耗时（ms） | P95（ms） | 平均分配 | 平均峰值活跃 | 平均返回残留 | 吞吐（ops/s） |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| native-summary | 1024 | 0.067 | 0.072 | 24.06 KiB | 24.06 KiB | 0.06 KiB | 14872.7259 |
| native-summary | 8192 | 0.467 | 0.479 | 128.11 KiB | 128.05 KiB | 0.06 KiB | 2141.2190 |
| native-summary | 65536 | 3.323 | 3.352 | 1.102 MiB | 1.086 MiB | 0.06 KiB | 300.8688 |
| native-deparse | 1024 | 0.287 | 0.296 | 153.08 KiB | 120.00 KiB | 0.00 KiB | 1496.5328 |
| native-deparse | 8192 | 2.202 | 2.229 | 1.236 MiB | 1.008 MiB | 0.00 KiB | 198.6703 |
| native-deparse | 65536 | 17.439 | 17.581 | 9.987 MiB | 8.149 MiB | 0.00 KiB | 26.3040 |
| sqlparser-parse-tree-json | 1024 | 0.147 | 0.152 | 88.49 KiB | 72.05 KiB | 16.45 KiB | 1727.9053 |
| sqlparser-parse-tree-json | 8192 | 1.110 | 1.206 | 361.33 KiB | 184.05 KiB | 121.14 KiB | 235.3492 |
| sqlparser-parse-tree-json | 65536 | 8.266 | 8.324 | 2.964 MiB | 1.524 MiB | 898.50 KiB | 32.8424 |
| sqlparser-summary-json | 1024 | 1.392 | 1.414 | 173.80 KiB | 76.12 KiB | 12.30 KiB | 538.8354 |
| sqlparser-summary-json | 8192 | 10.146 | 10.220 | 933.67 KiB | 356.58 KiB | 90.43 KiB | 73.5588 |
| sqlparser-summary-json | 65536 | 74.139 | 75.244 | 7.307 MiB | 2.657 MiB | 695.40 KiB | 10.0793 |
| sqlparser-deparse | 1024 | 0.305 | 0.315 | 154.08 KiB | 120.00 KiB | 1.00 KiB | 1347.4753 |
| sqlparser-deparse | 8192 | 2.241 | 2.269 | 1.244 MiB | 1.008 MiB | 8.00 KiB | 185.3139 |
| sqlparser-deparse | 65536 | 17.795 | 17.940 | 10.050 MiB | 8.149 MiB | 64.00 KiB | 24.9338 |
