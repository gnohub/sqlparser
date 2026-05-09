# Oracle Dialect Support

`SQLPARSER_DIALECT_ORACLE` provides a conversion layer from Oracle SQL to the
current `sqlparser` AST model. Callers select it explicitly through
`sqlparser_parse_with_options()`; when no dialect is specified, parsing uses the
PostgreSQL grammar.

## Supported Scope

The Oracle dialect supports common SQL forms that can be safely mapped to the
current AST. The executable case matrix defines the support boundary:

- `SELECT`, aliases, subqueries, joins, `WHERE`, `GROUP BY`, and `HAVING`
- Oracle bind placeholders such as `:id` and `:name`
- `q'[...]'` strings
- `MINUS` set operator
- `OFFSET ... FETCH`
- `ROWNUM` filters
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`
- `UPDATE` and `DELETE`
- `DATE` and `TIMESTAMP` literals
- `CASE`, `EXISTS`, `UNION ALL`, and `INTERSECT`
- mappable `MERGE`
- common DDL: `CREATE TABLE`, `CREATE SEQUENCE`, `CREATE VIEW`, `DROP TABLE`,
  and `TRUNCATE TABLE`
- transaction control, `GRANT / REVOKE`, and `COMMENT ON`
- `FOR UPDATE NOWAIT`
- common functions and analytic functions such as `DECODE`, `SYSDATE`, and
  `ROW_NUMBER() OVER (...)`
- quoted identifiers, `ALTER TABLE ADD`, `CREATE INDEX`, and `DROP INDEX`
- compatible materialized-view creation forms
- session context switching: `ALTER SESSION SET CURRENT_SCHEMA = ...`,
  `ALTER SESSION SET CONTAINER = ...`, and
  `ALTER SESSION SET CONTAINER = ... SERVICE = ...`

## Explicitly Unsupported Scope

The following Oracle-specific constructs are not silently downgraded. They
return `SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable handle:

- `CONNECT BY` and `CONNECT_BY_ROOT`
- legacy outer join `(+)`
- `INSERT ALL` and `INSERT FIRST`
- `RETURNING ... INTO`
- PL/SQL blocks, procedures, and packages
- `PIVOT` and `UNPIVOT`
- `MODEL` clause
- flashback query
- `MATCH_RECOGNIZE`
- `ALTER SESSION` parameters other than `CURRENT_SCHEMA` and
  `CONTAINER/SERVICE`
- synonyms
- database links
- `EXPLAIN PLAN FOR`
- national q-quoted strings such as `nq'[...]'`

## Public Output Rules

- `sqlparser_deparse()` emits the public Oracle form and does not expose
  internal conversion details.
- Oracle binds remain in `:name` or `:1` form; internal `$1` / `$2` names are
  not emitted.
- `MINUS` remains visible as the Oracle semantic keyword in SQL View JSON and
  deparse output.
- Attributable expression fragments in SQL View JSON use the public Oracle
  form.
- Failed expression-fragment rewrites are not committed to the handle; the
  previous AST, bind mapping, and deparse output remain usable.

## Regression Cases

The Oracle support boundary is defined by:

- `tests/cases/oracle_dialect_input.json`
- `tests/cases/oracle_dialect_matrix.en.md`
- `tests/unit/test_oracle_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

The current Oracle matrix contains 65 cases: 46 supported paths and 19 explicit
unsupported paths.
