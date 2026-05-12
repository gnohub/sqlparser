# v0.4.0 发布说明

`v0.4.0` 是 `sqlparser` 的方言能力与结构化改写小版本更新，重点补齐达梦方言、预编译 / 参数化 SQL 覆盖，以及通用子句级改写能力。

## 主要变化

- 增加达梦 `SQLPARSER_DIALECT_DAMENG` 方言转换层，覆盖 `SET SCHEMA`、`MINUS`、`LIMIT`、`TOP`、bind、常见 DML/DDL、事务和权限语句。
- 增加 PostgreSQL、MySQL、Oracle、SQL Server 和达梦的预编译 / 参数化 SQL 用例覆盖，包含 PostgreSQL `$n`、JDBC `?`、Oracle `:name` / `:1`、SQL Server `@name` 和达梦 bind。
- 增加通用 `SELECT` 输出列表读取、替换、插入和删除能力。
- 增加通用 `WHERE` 条件读取、设置和 `AND` / `OR` 追加能力。
- 增加 statement 级 `clause` selector，支持通过 `stmt[n].clause[m]` 改写 `select_list`、`where` 和 `order_by`。
- CLI 支持达梦方言，并修复参数顺序问题，`--dialect`、`--mode` 等选项可放在 SQL 前后。

## 方言支持边界

当前可执行用例矩阵：

- PostgreSQL：70 条用例，69 条支持路径，1 条非法 SQL 负向路径。
- MySQL：48 条用例，33 条支持路径，15 条明确不支持路径。
- Oracle：78 条用例，59 条支持路径，19 条明确不支持路径。
- SQL Server：76 条基础用例，61 条支持路径，15 条明确不支持路径；官方 `HOOK_ONLY` 覆盖矩阵包含 235 条用例。
- 达梦：55 条用例，43 条支持路径，12 条明确不支持路径。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- JSON fixture 格式校验
- Linux `make test`
- Windows MSVC `nmake /F Makefile.msvc test`
- 覆盖统计与可执行 fixture 数量一致性校验

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- SQL View JSON：通过 `sqlparser_export_view_json()` 按需导出
- SQL View C 结构化遍历：通过 `sqlparser_get_view()` 和相关 view API 按需读取
- 动态库 ABI 主版本：`libsqlparser.so.0`
- vendored `libpg_query` tag：`17-6.2.2`
