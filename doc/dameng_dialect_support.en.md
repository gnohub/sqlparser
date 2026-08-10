# Dameng Dialect Support

`SQLPARSER_DIALECT_DAMENG` provides a conversion layer from Dameng DM_SQL to the
current `sqlparser` AST model. Callers select it explicitly through
`sqlparser_parse_with_options()`; when no dialect is specified, parsing uses the
PostgreSQL grammar.

## Supported Scope

The Dameng dialect supports common SQL forms that can be safely mapped to the
current AST. The executable case matrix defines the support boundary:

- `SELECT`, aliases, subqueries, joins, `WHERE`, `GROUP BY`, and `HAVING`
- Dameng-compatible bind placeholders such as `:id` and `:name`, plus
  JDBC-style `?` positional parameters
- `q'[...]'` strings, `N'...'` national strings, and `nq'[...]'` national q-quoted strings
- `SET SCHEMA <schema>` and `ALTER SESSION SET CURRENT_SCHEMA = ...`
- session parameters: `NLS_DATE_FORMAT`, `NLS_TIMESTAMP_FORMAT`,
  `NLS_TIMESTAMP_TZ_FORMAT`, `NLS_TIME_FORMAT`, `NLS_TIME_TZ_FORMAT`,
  `NLS_SORT`, and `CASE_SENSITIVE`
- quoted schema identifiers in `CURRENT_SCHEMA` are marked as quoted
  identifiers in the public literal view
- `MINUS` set operator
- `LIMIT n`, `LIMIT offset,n`, and `LIMIT n OFFSET offset`
- `SELECT TOP n ...`, `SELECT TOP n,m ...`, `SELECT TOP n PERCENT ...`,
  `SELECT TOP n WITH TIES ...`, and `SELECT TOP n PERCENT WITH TIES ...`
- `ROWNUM` predicates
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`
- multi-table insert: `INSERT ALL` and `INSERT FIRST`, including
  `WHEN ... THEN`, `ELSE`, and multiple `INTO` branches under one condition
- `UPDATE` and `DELETE`
- DML host-variable returns: `RETURNING <single expression> INTO <single
  colon-prefixed host bind>` for `INSERT` and `DELETE`, and `RETURN <single
  expression> INTO <single colon-prefixed host bind>` for `UPDATE`
- mappable `MERGE`
- `DATE` and `TIMESTAMP` literals
- common DDL: `CREATE TABLE`, `CREATE VIEW`, `CREATE SEQUENCE`,
  `ALTER TABLE ADD`, `CREATE INDEX`, `DROP TABLE`, and `TRUNCATE TABLE`
- transaction control and `GRANT / REVOKE`
- `FOR UPDATE NOWAIT`
- remote object references such as `schema.table@link`
- common functions and analytic functions such as `NVL` and
  `ROW_NUMBER() OVER (...)`
- embedded SQL prepared statements: `EXEC SQL PREPARE`, `EXEC SQL EXECUTE`,
  and `EXEC SQL DEALLOCATE PREPARE`

## Explicitly Unsupported Scope

The following constructs are not silently downgraded. They return
`SQLPARSER_STATUS_UNSUPPORTED` or a parse error and do not return a usable
handle:

- `CONNECT BY`
- `PIVOT` and `UNPIVOT`
- `RETURN` / `RETURNING ... INTO` forms with multiple return targets, multiple
  `INTO` binds, or `BULK COLLECT`
- DMSQL blocks, procedures, and packages
- other `ALTER SESSION` parameters outside the supported list
- `ALTER SESSION SET CONTAINER = ...`

## Public Output Rules

- `sqlparser_deparse()` emits the public Dameng form and does not expose
  internal conversion details.
- Binds remain in `:name`, `:1`, or `?` form; internal `$1` / `$2` names are
  not emitted.
- `MINUS` remains visible as the Dameng semantic keyword in View JSON and
  deparse output.
- `SET SCHEMA` uses the `CURRENT_SCHEMA` field name in View JSON.
- DML return channels use a sink channel in `dml.result_channels`; the return
  target's `sink_value` refers to the host bind in `query_graph.values[]`.
- Attributable expression fragments in View JSON use the public Dameng
  form.
- Failed expression-fragment rewrites are not committed to the handle; the
  previous AST, bind mapping, and deparse output remain usable.

## Regression Cases

The Dameng support boundary is defined by:

- `tests/cases/dameng_dialect_input.json`
- `tests/cases/dameng_dialect_matrix.en.md`
- `tests/unit/test_dameng_dialect_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_stability.c`

The current Dameng matrix contains 169 cases, all with `status = "final"`.
