# v0.6.0 发布说明

`v0.6.0` 是 `sqlparser` 的 SQL View 结构化输出更新，重点是让公共 C 结构成为稳定的数据访问入口，并让 View JSON 只作为按需导出的可视化结果。

## 主要变化

- SQL View JSON 现在从 SQL View C 结构按需序列化，解析和结构遍历路径不默认生成 JSON。
- `sqlparser_column_view_t` 和 `sqlparser_cell_view_t` 增加 bind、bind 类型、原始 bind SQL、bind selector、子句编号和 SELECT 输出路径字段。
- 新增 `sqlparser_bind_kind_t`、`sqlparser_bind_kind_name()`、`sqlparser_statement_clause_at()` 和 `sqlparser_clause_sql()` 公共接口。
- `sqlparser_clause_kind_t` 增加 `on`、`group_by` 和 `having` 子句类型。
- View JSON 移除 `target_kind`、`target_name`、`target_arg_index`，统一使用有序 `target_path` 表达函数、表达式、CASE 和嵌套 SELECT 输出层级。
- bind 占位符不再重复暴露为普通 `value`，避免调用方把 `?`、`:1`、`:name`、`$1`、`@name` 等占位符误判为字面量值。
- `NOT IN`、`NOT LIKE`、`NOT ILIKE` 和 `NOT SIMILAR TO` 运算符保持完整公共 SQL 语义。

## 测试覆盖

当前可执行用例矩阵：

- PostgreSQL 通用批量夹具：85 条语句。
- MySQL：57 条用例，42 条支持路径，15 条明确不支持路径。
- Oracle：89 条用例，70 条支持路径，19 条明确不支持路径。
- SQL Server：85 条基础用例，70 条支持路径，15 条明确不支持路径；官方 `HOOK_ONLY` 覆盖矩阵包含 235 条用例。
- 达梦：65 条用例，53 条支持路径，12 条明确不支持路径。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- 敏感信息和旧接口残留扫描
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan`
- Linux `make verify-ubsan`
- Linux `make verify-valgrind`
- Windows MSVC `nmake /F Makefile.msvc test`

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- SQL View C 结构化遍历：通过 `sqlparser_get_view()` 和相关 view API 按需读取
- SQL View JSON：通过 `sqlparser_export_view_json()` 按需导出
- 动态库 ABI 主版本：`libsqlparser.so.0`
- vendored `libpg_query` tag：`17-6.2.2`
