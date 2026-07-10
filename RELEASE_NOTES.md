# v2.9.0 发布说明

`v2.9.0` 扩展 MySQL 与 Vastbase-MySQL 方言覆盖，并增强 CTE 与 JOIN 字段的 Query Graph 表达。

## 主要变化

- 支持 `INSERT ... SET`、`ON DUPLICATE KEY UPDATE` 行别名和别名删除目标。
- 支持单表 `UPDATE` / `DELETE ... ORDER BY ... LIMIT`。
- 支持 `STRAIGHT_JOIN`、`JOIN ... USING`、`NATURAL JOIN`、锁定读、索引提示和查询表 `PARTITION(...)`。
- `JOIN ... USING` 字段通过 `fields[]` 和 `candidate_relations` 输出。
- 同一 CTE 的多次引用共享来源 `source_block`，未引用 CTE 保留在 Query Graph 中，递归 CTE 引用指向已注册的来源块。
- 新增 `SQLPARSER_GRAPH_INSERT_MODE_SET` 枚举值。
- MySQL 与 Vastbase-MySQL 方言用例矩阵分别包含 156 条支持用例。

## 兼容性

- 公共 C 结构体布局保持稳定。
- 动态库 ABI 主版本保持为 `libsqlparser.so.0`。
- ABI 导出符号数保持为 135。
- 使用旧版本公共头文件编译的客户端已通过当前动态库加载验证。

## 发布验证

- GCC 8.3 严格编译与全量测试
- ASan 与 UBSan
- Valgrind 全量目标内存检查
- ABI 导出检查
- 旧版本客户端动态库兼容测试

vendored `libpg_query` tag：`17-6.2.2`。
