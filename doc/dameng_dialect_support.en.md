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
- `SET SCHEMA <schema>` and `ALTER SESSION SET CURRENT_SCHEMA = ...`
- quoted schema identifiers in `CURRENT_SCHEMA` are marked as quoted
  identifiers in the public literal view
- `MINUS` set operator
- `LIMIT n`, `LIMIT offset,n`, and `LIMIT n OFFSET offset`
- `SELECT TOP n ...`
- `ROWNUM` predicates
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`
- `UPDATE` and `DELETE`
- mappable `MERGE`
- `DATE` and `TIMESTAMP` literals
- common DDL: `CREATE TABLE`, `CREATE VIEW`, `CREATE SEQUENCE`,
  `ALTER TABLE ADD`, `CREATE INDEX`, `DROP TABLE`, and `TRUNCATE TABLE`
- transaction control and `GRANT / REVOKE`
- `FOR UPDATE NOWAIT`
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
- `RETURNING ... INTO`
- DMSQL blocks, procedures, and packages
- `TOP ... PERCENT` and `TOP ... WITH TIES`
- multi-table insert forms such as `INSERT ALL`
- database links
- national q-quoted strings such as `nq'[...]'`
- `ALTER SESSION` parameters other than current-schema switching
- container switching such as `ALTER SESSION SET CONTAINER = ...`

## Public Output Rules

- `sqlparser_deparse()` emits the public Dameng form and does not expose
  internal conversion details.
- Binds remain in `:name`, `:1`, or `?` form; internal `$1` / `$2` names are
  not emitted.
- `MINUS` remains visible as the Dameng semantic keyword in View JSON and
  deparse output.
- `SET SCHEMA` is exposed as session context; View JSON uses the
  `CURRENT_SCHEMA` field name.
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

The current Dameng matrix contains 87 cases: 75 supported paths and 12 explicit
unsupported paths.
