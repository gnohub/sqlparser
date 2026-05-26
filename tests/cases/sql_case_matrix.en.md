# SQL Case Matrix

This file records the regression cases covered by `tests/cases/sql_batch_input.json`. The JSON fixture is the executable test source; this document describes the validated statement shapes and validation focus.

## Executable Entry Points

- API smoke test: `tests/unit/test_api_smoke.c`
- API matrix test: `tests/unit/test_api_case_matrix.c`
- CLI batch fixture: `tests/cases/sql_batch_input.json`

## Validated Statement Shapes

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| P001 | `select-basic` | `SELECT 1` | parse, SQL View JSON, deparse |
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
| P049 | `postgresql-set-search-path` | `SET search_path TO ...` | session schema search-path output and value selector |
| P050 | `postgresql-set-schema` | `SET SCHEMA ...` | `SET SCHEMA` alias deparses as `search_path` |
| P051 | `postgresql-set-local-search-path` | `SET LOCAL search_path = ...` | local transaction-level schema search path |
| P052 | `postgresql-prepare-select` | `PREPARE ... AS SELECT ... $1` | PostgreSQL SQL-level prepared statement, parameters, and query-object extraction |
| P053 | `postgresql-execute-prepared` | `EXECUTE ...(...)` | PostgreSQL prepared statement execution |
| P054 | `postgresql-deallocate-prepare` | `DEALLOCATE PREPARE ...` | PostgreSQL prepared statement deallocation |
| P055 | `oracle-cli-dialect-q-quote` | Oracle `q'[...]'` | CLI `dialect` field and Oracle q-quoted string handling |
| P056 | `sqlserver-cli-dialect-top-param` | SQL Server `TOP` + `@` parameter | CLI `dialect` field and SQL Server dialect output handling |
| P057 | `dameng-cli-dialect-set-schema-top` | Dameng `SET SCHEMA` + `TOP` + bind | CLI `dialect` field and Dameng dialect output handling |
| P058 | `postgresql-select-dollar-params` | `SELECT ... WHERE ... = $1` | PostgreSQL `$n` parameters in query predicates, View output, and deparse |
| P059 | `postgresql-select-in-dollar-params` | `SELECT ... IN ($1, $2, $3)` | multiple `$n` parameters in `IN` predicates |
| P059A | `postgresql-select-between-dollar-params` | `BETWEEN $1 AND $2` | multiple `$n` parameters and field-value attribution in `BETWEEN` predicates |
| P059B | `postgresql-select-not-in-dollar-params` | `NOT IN ($1, $2)` | multiple `$n` parameters and field-value attribution in negated `IN` predicates |
| P059C | `postgresql-select-not-between-dollar-params` | `NOT BETWEEN $1 AND $2` | multiple `$n` parameters and field-value attribution in negated `BETWEEN` predicates |
| P060 | `postgresql-select-limit-dollar-params` | `LIMIT $2 OFFSET $3` | `$n` parameters in pagination clauses |
| P061 | `postgresql-insert-dollar-params` | `INSERT ... VALUES ($1, $2, $3)` | insert columns and `$n` parameter value lists |
| P062 | `postgresql-insert-multi-row-dollar-params` | multi-row `INSERT ... VALUES` + `$n` | multi-row parameterized insert |
| P063 | `postgresql-update-dollar-params` | `UPDATE ... SET ... WHERE ... = $n` | updated columns, predicate columns, and `$n` parameters |
| P064 | `postgresql-delete-dollar-params` | `DELETE ... WHERE ... = $n` | conditional delete and `$n` parameters |
| P065 | `postgresql-prepare-insert` | `PREPARE ... AS INSERT ...` | prepared insert statement and parameterized value list |
| P066 | `postgresql-prepare-update` | `PREPARE ... AS UPDATE ...` | prepared update statement and predicate parameters |
| P067 | `postgresql-prepare-delete` | `PREPARE ... AS DELETE ...` | prepared delete statement and predicate parameters |
| P068 | `postgresql-execute-prepared-with-args` | `EXECUTE ...(...)` | prepared statement execution arguments |
| P069 | `postgresql-deallocate-all` | `DEALLOCATE ALL` | deallocating all prepared statements |
| P070 | `postgresql-view-direct-column` | `SELECT name FROM ...` | direct SELECT output column, `clause_id`, and empty `target_path` |
| P071 | `postgresql-view-star-qualified-star` | `SELECT *, alias.* FROM ...` | unqualified star, qualified star, and output-item ownership |
| P072 | `postgresql-view-functions-and-args` | `SELECT function(column, ...) FROM ...` | function `target_path`, function name, and argument index |
| P073 | `postgresql-view-expressions-and-case` | `SELECT expression, CASE ... FROM ...` | expression `target_path`, operator name, and `CASE` ownership |
| P074 | `postgresql-view-group-having-order` | `GROUP BY ... HAVING ... ORDER BY ...` | non-output clause fields with `clause_id` and empty `target_path` |
| P075 | `postgresql-view-distinct-nested-functions` | `SELECT DISTINCT LOW(UPPER(...)) FROM ...` | `DISTINCT` keyword and outer-to-inner nested function `target_path` |
| P076 | `postgresql-view-join-on` | `JOIN ... ON ... WHERE ...` | JOIN/ON fields, WHERE binds, and table-column attribution |
| P077 | `postgresql-view-window-array-row-tests` | window, array, ROW, boolean/NULL expressions | `target_path` for window functions, compound expressions, and read-only clauses |
| P078 | `postgresql-view-bind-values` | `UPDATE ... SET ... WHERE ... = $n` | PostgreSQL bind fields, null values, and update/where clause ownership |
| P079 | `postgresql-view-not-like-bind` | `NOT LIKE $n` | field-level operator, keyword, and bind attribution for negated LIKE |
| P080 | `postgresql-view-not-ilike-bind` | `NOT ILIKE $n` | field-level operator, keyword, and bind attribution for negated ILIKE |
| P081 | `postgresql-view-not-similar-bind` | `NOT SIMILAR TO $n` | field-level operator, keyword, and bind attribution for negated SIMILAR TO |
| P082 | `postgresql-create-table-if-not-exists-types` | `CREATE TABLE IF NOT EXISTS ...` | conditional table creation, common data types, and table extraction |
| P083 | `postgresql-insert-without-column-list` | `INSERT INTO ... VALUES ($1, $2, $3)` | columnless insert, row cells, positional binds, and null column names |
| P084 | `postgresql-update-in-not-in-conditions` | `UPDATE ... SET ... WHERE ... IN ... NOT IN ...` | SET bind, collection predicates, and negated collection predicates |
| P085 | `postgresql-select-rich-where` | `IS NOT NULL` + `BETWEEN` + `LIKE` | complex WHERE predicates, range parameters, and pattern-match parameters |
| P086 | `postgresql-select-derived-table-filter` | derived table + outer filter | derived-table fields, inner/outer WHERE clauses, and bind attribution |
| P087 | `postgresql-select-scalar-subquery` | scalar subquery in SELECT output | projection subquery, correlated fields, and outer WHERE bind |
| P088 | `postgresql-select-intersect` | `INTERSECT` | set operation, both input tables, and output columns |
| P089 | `postgresql-create-view-join-aggregate` | aggregate JOIN view | view definition, JOIN predicates, and GROUP BY aggregation |
| P090 | `postgresql-select-order-by-ordinal` | `ORDER BY 1` | ordinal sort item and projection-order related syntax |
| P091 | `postgresql-select-quoted-mixed-identifiers` | quoted mixed-case / spaced identifiers | special identifiers, selected columns, and WHERE bind |
| P092 | `postgresql-dollar-quoted-string-global-bind-position` | dollar-quoted string plus `$n` parameters | placeholder-like text inside dollar-quoted strings is excluded from global bind counting |
| P093 | `postgresql-multi-statement-global-bind-position` | multi-statement `$n` parameters | positional `bind_position` increases globally across the full input SQL |

## Negative Case

| Case ID | Case Name | Input | Validation Focus |
| --- | --- | --- | --- |
| P094 | `parse-error` | `SELECT FROM` | structured parse error, error code, error message |

New regression cases must update both `tests/cases/sql_batch_input.json` and this matrix.
