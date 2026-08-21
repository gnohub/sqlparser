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
- `MERGE`，包括独立的 `WHEN MATCHED ... THEN DELETE` action；not-matched INSERT 的 `insert_column` 支持 column-only、value-only 和 paired 三态，现有 VALUES cell 可独立替换
- 常见 DDL：`CREATE TABLE`、`CREATE TABLE AS`、`CREATE VIEW`、`CREATE MATERIALIZED VIEW`
- `ALTER TABLE RENAME`、`ALTER TABLE ADD COLUMN`、`ALTER TABLE DROP COLUMN`
- `CREATE INDEX`、`DROP INDEX`、`DROP TABLE`、`DROP VIEW`
- `CREATE SCHEMA`、`DROP SCHEMA`
- `COMMENT ON`、`GRANT`、`REVOKE`
- `EXPLAIN`、`COPY`、`LOCK`、`ANALYZE`、`VACUUM`
- `LISTEN`、`NOTIFY`、`UNLISTEN`
- `CREATE EXTENSION`、`DROP EXTENSION`
- 事务控制、`SAVEPOINT`、`ROLLBACK TO SAVEPOINT`、`RELEASE SAVEPOINT`
- `CALL`、`DO`
- 多语句解析和反解析
- `SET search_path`、`SET LOCAL search_path`、`SET SCHEMA`
- PostgreSQL `$n` 参数占位符
- `PREPARE`、`EXECUTE`、`DEALLOCATE`
- national 字符串字面量：`N'...'`

## 明确不支持范围

PostgreSQL 默认方言当前没有单独维护负向功能清单。解析失败通常来自非法 SQL、解析内核不支持的 PostgreSQL 版本差异，或公共 query_graph 尚未暴露的专用结构。

## 对外输出规则

- `sqlparser_deparse()` 输出 PostgreSQL 兼容 SQL。
- View JSON 通过统一的 `query_graph` 输出结构化结果。
- 省略 MERGE INSERT 目标列列表时仍输出 `target_list_selector`；column-only patch 可物化列列表，value-only patch 可在保持列表省略时追加 VALUES cell，显式列表继续支持 paired patch 同时追加两侧。patch batch 结束时若存在显式列表，则校验列值等长；失败时由核心 patch API 整批回滚。
- Query Graph 以 `alias_quoted_identifier` 标记双引号 relation alias，以 `output_quoted_identifier` 标记双引号显式 output alias 或无显式别名时继承的双引号字段名；View JSON 仅输出值为 `true` 的键。

## 回归用例

PostgreSQL 默认方言支持范围以以下文件为准：

- `tests/cases/sql_batch_input.json`
- `tests/cases/sql_case_matrix.md`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_stability.c`

当前 PostgreSQL 矩阵包含 218 条用例和 734 个独立 patch，全部为 `status = "final"`。
