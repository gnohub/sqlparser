# SQL Case Matrix

This file records the regression cases covered by `tests/cases/sql_batch_input.json`. The JSON fixture is the executable test source; this document describes the validated statement shapes and validation focus.

## Executable Entry Points

- API smoke test: `tests/unit/test_api_smoke.c`
- API matrix test: `tests/unit/test_api_case_matrix.c`
- CLI batch fixture: `tests/cases/sql_batch_input.json`

## Validated Statement Shapes

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| P001 | `select-basic` | `SELECT 1` | parse, parse-tree JSON, summary JSON, deparse |
| P002 | `select-filter` | `SELECT ... FROM ... WHERE ...` | selected columns, filter columns, table extraction |
| P003 | `select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | multi-table join, selected columns, join columns, where columns |
| P004 | `select-cte` | `WITH ... SELECT ...` | CTE name, outer selected columns, upstream filter columns |
| P005 | `insert-single-row` | `INSERT ... VALUES (...)` | insert columns, single-row insert, deparse |
| P006 | `insert-multi-row` | `INSERT ... VALUES (...), (...)` | multi-row insert, insert columns, deparse |
| P007 | `insert-from-select` | `INSERT ... SELECT ... FROM ... WHERE ...` | insert columns, inner SELECT, WHERE extraction |
| P008 | `update-basic` | `UPDATE ... SET ... WHERE ...` | updated columns, where columns, table extraction |
| P009 | `delete-conditional` | `DELETE ... WHERE ... AND ...` | conditional delete and multi-condition column extraction |
| P010 | `delete-in-list` | `DELETE ... WHERE ... IN (...)` | delete predicate extraction with `IN` |
| P011 | `drop-table` | `DROP TABLE ...` | DDL classification, table extraction, deparse |
| P012 | `drop-view` | `DROP VIEW ...` | view DDL classification and object extraction |
| P013 | `create-view` | `CREATE VIEW ... AS SELECT ...` | view definition and inner SELECT extraction |
| P014 | `truncate-table` | `TRUNCATE TABLE ...` | truncate node recognition and deparse |
| P015 | `comment-table` | `COMMENT ON TABLE ... IS ...` | comment node recognition and deparse |
| P016 | `rename-table` | `ALTER TABLE ... RENAME TO ...` | rename node recognition and object-name rewrite basis |
| P017 | `alter-table-add-column` | `ALTER TABLE ... ADD COLUMN ...` | alter-table node recognition and column-definition deparse |
| P018 | `create-index` | `CREATE INDEX ... ON ... (...)` | index node recognition and deparse |
| P019 | `drop-index` | `DROP INDEX ...` | drop-index node recognition and deparse |
| P020 | `explain-select` | `EXPLAIN SELECT ...` | parsing and deparsing an explained query |
| P021 | `copy-table` | `COPY ... FROM STDIN` | copy node recognition and column-list deparse |
| P022 | `lock-table` | `LOCK TABLE ... IN ... MODE` | lock node recognition and deparse |
| P023 | `call-procedure` | `CALL ...()` | call node recognition and deparse |
| P024 | `do-block` | `DO $$ ... $$` | DO block parse and deparse |
| P025 | `create-table-as` | `CREATE TABLE ... AS SELECT ...` | CTAS node recognition and inner query parsing |
| P026 | `transaction-begin-commit` | `BEGIN; COMMIT;` | multi-statement transaction counting and keyword extraction |
| P027 | `transaction-begin-insert-rollback` | `BEGIN; INSERT ...; ROLLBACK;` | mixed transaction and DML parsing |
| P028 | `multi-statement-mixed` | `SELECT ...; INSERT ...` | multi-statement counting and mixed-statement deparse |
| P029 | `quoted-identifiers` | `SELECT "..."."..." FROM "..."` | quoted identifier preservation and name extraction |
| P030 | `literal-semicolon` | `SELECT ';' AS ...` | semicolon handling inside string literals |
| P031 | `select-subquery-exists` | `SELECT ..., EXISTS (SELECT ...) FROM ...` | subquery, `EXISTS`, and multi-table extraction |
| P032 | `select-case-window` | `SELECT CASE ... OVER (...) FROM ...` | `CASE`, window functions, sort/partition column extraction |
| P033 | `select-union-order-limit` | `SELECT ... UNION ALL SELECT ... ORDER BY ... LIMIT ...` | `UNION ALL`, ordering, and limit deparse |
| P034 | `insert-on-conflict-update` | `INSERT ... ON CONFLICT ... DO UPDATE ... RETURNING ...` | conflict handling, returning columns, insert columns |
| P035 | `insert-returning` | `INSERT ... RETURNING ...` | returning columns and insert columns |
| P036 | `update-from-returning` | `UPDATE ... SET ... FROM ... WHERE ... RETURNING ...` | `UPDATE ... FROM`, returning columns, where columns |
| P037 | `delete-using-returning` | `DELETE ... USING ... WHERE ... RETURNING ...` | `DELETE ... USING`, returning columns, multi-table extraction |
| P038 | `merge-basic` | `MERGE INTO ... USING ... WHEN ...` | merge node recognition and keyword coverage |
| P039 | `savepoint-release` | `BEGIN; SAVEPOINT ...; RELEASE ...; COMMIT;` | savepoint transaction parsing |
| P040 | `rollback-to-savepoint` | `BEGIN; SAVEPOINT ...; INSERT ...; ROLLBACK TO ...; COMMIT;` | savepoint and DML mixed parsing |
| P041 | `create-materialized-view` | `CREATE MATERIALIZED VIEW ... AS SELECT ...` | materialized view and inner SELECT extraction |
| P042 | `alter-table-drop-column` | `ALTER TABLE ... DROP COLUMN ...` | drop-column node recognition and deparse |
| P043 | `create-schema` | `CREATE SCHEMA ...` | schema DDL classification and deparse |
| P044 | `drop-schema` | `DROP SCHEMA ...` | schema drop classification and deparse |
| P045 | `grant-select` | `GRANT SELECT ON TABLE ... TO ...` | grant node recognition and object extraction |
| P046 | `revoke-select` | `REVOKE SELECT ON TABLE ... FROM ...` | revoke node recognition and object extraction |
| P047 | `analyze-table` | `ANALYZE ...` | analyze node recognition and table extraction |
| P048 | `vacuum-analyze-table` | `VACUUM ANALYZE ...` | combined vacuum/analyze node recognition |

## Negative Case

| Case ID | Case Name | Input | Validation Focus |
| --- | --- | --- | --- |
| P049 | `parse-error` | `SELECT FROM` | structured parse error, error code, error message |

New regression cases must update both `tests/cases/sql_batch_input.json` and this matrix.
