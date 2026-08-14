# PostgreSQL Dialect Support

`SQLPARSER_DIALECT_POSTGRESQL` is the default dialect. The parser kernel is the
pinned in-tree `libpg_query 17-6.2.2`, which uses the PostgreSQL 17 parser
baseline.

## Supported Scope

The PostgreSQL dialect supports PostgreSQL syntax forms represented by the
current parser kernel. The executable case matrix defines the support boundary:

- `SELECT`, `WITH`, subqueries, joins, `WHERE`, `GROUP BY`, `HAVING`,
  `ORDER BY`, and `LIMIT`
- `UNION ALL`, `EXCEPT`, and `INTERSECT`
- `CASE`, window functions, function calls, and casts
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`
- `ON CONFLICT DO UPDATE` and `RETURNING`
- `UPDATE`, `UPDATE FROM`, `DELETE`, and `DELETE USING`
- `MERGE`, including an independent `WHEN MATCHED ... THEN DELETE` action
- common DDL: `CREATE TABLE`, `CREATE TABLE AS`, `CREATE VIEW`, and
  `CREATE MATERIALIZED VIEW`
- `ALTER TABLE RENAME`, `ALTER TABLE ADD COLUMN`, and
  `ALTER TABLE DROP COLUMN`
- `CREATE INDEX`, `DROP INDEX`, `DROP TABLE`, and `DROP VIEW`
- `CREATE SCHEMA` and `DROP SCHEMA`
- `COMMENT ON`, `GRANT`, and `REVOKE`
- `EXPLAIN`, `COPY`, `LOCK`, `ANALYZE`, and `VACUUM`
- `LISTEN`, `NOTIFY`, and `UNLISTEN`
- `CREATE EXTENSION` and `DROP EXTENSION`
- transaction control, `SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, and
  `RELEASE SAVEPOINT`
- `CALL` and `DO`
- multi-statement parsing and deparsing
- `SET search_path`, `SET LOCAL search_path`, and `SET SCHEMA`
- PostgreSQL `$n` parameter placeholders
- `PREPARE`, `EXECUTE`, and `DEALLOCATE`
- national string literals: `N'...'`

## Explicitly Unsupported Scope

The PostgreSQL default dialect does not maintain a separate feature-level
negative list. Parse failures normally come from invalid SQL, PostgreSQL version
differences outside the pinned parser kernel, or specialized structures not yet
exposed by the public query graph.

## Public Output Rules

- `sqlparser_deparse()` emits PostgreSQL-compatible SQL.
- View JSON emits structured results through the common `query_graph`.

## Regression Cases

The PostgreSQL support boundary is defined by:

- `tests/cases/sql_batch_input.json`
- `tests/cases/sql_case_matrix.en.md`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_stability.c`

The current PostgreSQL matrix contains 216 cases, all with `status = "final"`.
