# MySQL 方言支持

`SQLPARSER_DIALECT_MYSQL` 提供 MySQL SQL 到 `sqlparser` 当前 AST 模型的转换层。调用方需要通过 `sqlparser_parse_with_options()` 显式指定 MySQL 方言；未指定方言时仍按 PostgreSQL 语法解析。

## 支持范围

MySQL 方言支持可安全映射到当前 AST 的常用 SQL 形态，覆盖范围由可执行用例矩阵定义：

- `SELECT`、别名、子查询、连接、`WHERE`
- 反引号标识符
- MySQL `#` 行注释
- 双引号字符串兼容处理
- `N'...'` national 字符串字面量，公共输出保留 `N` 前缀
- JDBC 风格 `?` 位置参数
- `LIMIT offset,count`
- `WITH` 公用表表达式
- 窗口函数
- 普通标量函数表达式
- `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`
- `INSERT ... SET` 和 `VALUES/SET` row alias
- `INSERT IGNORE`、`INSERT DELAYED`、`INSERT LOW_PRIORITY`、`INSERT HIGH_PRIORITY` 修饰符保留
- `REPLACE VALUES`、`REPLACE SET`、`REPLACE SELECT`、`REPLACE TABLE` 基础形态
- `UPDATE LOW_PRIORITY/IGNORE` 和 `DELETE LOW_PRIORITY/QUICK/IGNORE` 修饰符保留
- `INSERT ... ON DUPLICATE KEY UPDATE`
- `UPDATE`、`DELETE`
- 单表 `UPDATE`、`DELETE` 的 `ORDER BY ... LIMIT`，以及别名删除目标
- 多表 `UPDATE` 的 JOIN 链、逗号 relation 列表及跨多个 relation 的 assignment；每个 assignment 目标字段独立关联其写入 relation
- 带 `ON` 条件的多表 `DELETE u FROM ... JOIN ...` 基础形态
- `STRAIGHT_JOIN`、`JOIN ... USING`、`NATURAL JOIN`
- `USE/FORCE/IGNORE INDEX|KEY` 及 `FOR JOIN|ORDER BY|GROUP BY` scope
- `LOCK IN SHARE MODE`、`FOR UPDATE/SHARE`、`NOWAIT`、`SKIP LOCKED`
- 查询表的 `PARTITION(...)` 选择子句
- `CREATE TABLE` 基础形态、列属性、表选项和无查询表达式的分区尾部
- `ALTER TABLE ADD COLUMN`
- `CREATE VIEW`
- `DROP TABLE`
- `START TRANSACTION`、`COMMIT`、`ROLLBACK`
- `USE db_name`
- `PREPARE`、`EXECUTE`、`DEALLOCATE PREPARE`、`DROP PREPARE`

## 明确不支持范围

当前可执行 MySQL 方言夹具只登记成功用例；失败路径由独立单元测试维护。官方语法覆盖边界见 `mysql_official_syntax_coverage.csv`。

多表 `UPDATE` 不接受 `ORDER BY` 或 `LIMIT`。

## 对外输出规则

- `sqlparser_deparse()` 输出 MySQL 公共形态，不暴露内部转换细节。
- 反引号标识符和 MySQL 字符串兼容规则由方言层处理。
- View JSON 使用统一的 `query_graph` 结构；其中的标识符和值按 MySQL 公开形态输出。
- Query Graph 以 `alias_quoted_identifier` 标记反引号 relation alias，以 `output_quoted_identifier` 标记反引号显式 output alias 或无显式别名时继承的反引号字段名；View JSON 仅输出值为 `true` 的键。
- 多目标 `UPDATE` 不输出单一 `dml.target_relation`；各 assignment 通过 `target_field` 对应字段的 relation 表达写入目标。
- 无法安全表达的 MySQL 专属语义不会降级为 PostgreSQL 语义。

## 回归用例

MySQL 支持范围以以下文件为准：

- `tests/cases/mysql_dialect_input.json`
- `tests/cases/mysql_dialect_matrix.md`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

当前 MySQL 方言矩阵包含 261 条 `status = "final"` 用例和 878 个独立 patch。
