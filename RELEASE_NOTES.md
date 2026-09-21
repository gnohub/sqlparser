# v2.16.15 发布说明

优化批量 patch 的重复序列化与解析开销，覆盖共享 AST 路径、Oracle 系及达梦 `INSERT ALL/FIRST` 列和值修改，以及可合并的函数参数字面量替换。

保持修改顺序、`source_selector` 取值语义和整批失败回滚。公开 API、公开结构体布局、View JSON 字段和所有权规则不变。

与 2.16.14 同机对比，50 分支、500 个 patch 的 Oracle `INSERT ALL` 样例中，单次 `sqlparser_apply_patch()` 耗时由约 12.53 秒降至 0.21 秒。实际收益取决于 SQL 和 patch 组合。

新增批量 patch 回归覆盖 13 个方言入口。远端完整 `make test` 和 ABI 检查通过；新增回归的 Valgrind 检查未报内存错误或泄漏。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
