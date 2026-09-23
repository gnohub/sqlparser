# v2.16.16 发布说明

优化批量 patch 的重复整句解析与原文扫描开销，覆盖 INSERT 值复制、Oracle 系及达梦 `INSERT ALL/FIRST` 值替换、函数表达式及参数修改，以及 UPDATE 赋值与参数替换的混合批次。

合并可连续处理的修改，需要最新解析状态时按需同步。保持修改顺序、`source_selector` 取值语义、整批失败回滚和原有资源上限；公开 API、公开结构体布局、View JSON 字段和所有权规则不变。

与 2.16.15 同机对比，27,530 字节、50 分支的 Oracle `INSERT ALL` 样例一次提交 250 项字符串替换，`sqlparser_apply_patch()` 耗时由约 1.41 秒降至 15 毫秒。实际收益取决于 SQL 和 patch 组合。

批量 patch 回归覆盖 13 个方言入口。完整 `make test` 和 ABI 检查通过；批量 patch 回归的 Valgrind 检查未报内存错误或泄漏。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
