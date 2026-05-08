# PostgreSQL 方言支持

`SQLPARSER_DIALECT_POSTGRESQL` 是默认方言。解析内核基于仓库内固定版本 `libpg_query 17-6.2.2`，语法基线对应 PostgreSQL 17 解析器。

## 支持范围

PostgreSQL 方言支持当前解析内核可表达的 PostgreSQL 语句形态，覆盖范围由可执行用例矩阵定义：

- `SELECT`、`WITH`、子查询、连接、`WHERE`、`GROUP BY`、`HAVING`、`ORDER BY`、`LIMIT`
- `UNION ALL`、`EXCEPT`、`INTERSECT`
- `CASE`、窗口函数、函数调用、类型转换
- `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`
- `ON CONFLICT DO UPDATE`、`RETURNING`
- `UPDATE`、`UPDATE FROM`、`DELETE`、`DELETE USING`
- `MERGE`
- 常见 DDL：`CREATE TABLE`、`CREATE TABLE AS`、`CREATE VIEW`、`CREATE MATERIALIZED VIEW`
- `ALTER TABLE RENAME`、`ALTER TABLE ADD COLUMN`、`ALTER TABLE DROP COLUMN`
- `CREATE INDEX`、`DROP INDEX`、`DROP TABLE`、`DROP VIEW`
- `CREATE SCHEMA`、`DROP SCHEMA`
- `COMMENT ON`、`GRANT`、`REVOKE`
- `EXPLAIN`、`COPY`、`LOCK`、`ANALYZE`、`VACUUM`
- 事务控制、`SAVEPOINT`、`ROLLBACK TO SAVEPOINT`、`RELEASE SAVEPOINT`
- `CALL`、`DO`
- 多语句解析和反解析

## 明确不支持范围

PostgreSQL 默认方言当前没有单独维护负向功能清单。解析失败通常来自非法 SQL、解析内核不支持的 PostgreSQL 版本差异，或公共 SQL View 尚未暴露的专用结构。

## 对外输出规则

- `sqlparser_deparse()` 输出 PostgreSQL 兼容 SQL。
- SQL View JSON 只输出语句、对象、字段、值片段和 selector，不保存输入 SQL 副本。
- `SQL View JSON` 和 `SQL View JSON` 作为诊断接口保留。

## 回归用例

PostgreSQL 默认方言支持范围以以下文件为准：

- `tests/cases/sql_batch_input.json`
- `tests/cases/sql_case_matrix.md`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_stability.c`

当前 PostgreSQL 矩阵包含 49 条用例：48 条支持路径，1 条非法 SQL 负向路径。
