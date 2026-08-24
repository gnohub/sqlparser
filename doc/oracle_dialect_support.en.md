# Oracle Dialect Support

`SQLPARSER_DIALECT_ORACLE` provides a conversion layer from Oracle SQL to the
current `sqlparser` AST model. Callers select it explicitly through
`sqlparser_parse_with_options()`; when no dialect is specified, parsing uses the
PostgreSQL grammar.

## Supported Scope

The Oracle dialect supports common SQL forms that can be safely mapped to the
current AST. The executable case matrix defines the support boundary:

- `SELECT`, aliases, subqueries, joins, `WHERE`, `GROUP BY`, and `HAVING`
- Oracle hierarchical queries through `START WITH ... CONNECT BY [NOCYCLE]`,
  `PRIOR`, `LEVEL`, `CONNECT_BY_ISLEAF`, `CONNECT_BY_ISCYCLE`, and
  `CONNECT_BY_ROOT` in the SELECT list. `START WITH` must precede
  `CONNECT BY`; the reversed `CONNECT BY ... START WITH` order is not accepted
- Oracle bind placeholders such as `:id` and `:name`, plus JDBC-style `?`
  positional parameters
- `q'[...]'` strings, `N'...'` national strings, and `nq'[...]'` national q-quoted strings
- `MINUS` set operator
- row-count `OFFSET ... ROWS` and `FETCH FIRST|NEXT ... ROWS ONLY`;
  `FETCH ... PERCENT` is outside this boundary
- `ROWNUM` filters
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`, including `UNION`,
  `UNION ALL`, `INTERSECT`, and `MINUS` source queries
- Oracle multi-table insert: `INSERT ALL` and `INSERT FIRST`, including `WHEN ... THEN` conditional branches
- `UPDATE` and `DELETE`
- `RETURNING ... INTO` on `INSERT`, `UPDATE`, and `DELETE`, with `N >= 1`
  result targets paired by ordinal with exactly N colon-prefixed host binds
- `DATE` and `TIMESTAMP` literals
- `CASE`, `EXISTS`, `UNION ALL`, and `INTERSECT`
- mappable `MERGE`, including a post-assignment `WHERE` and an attached
  `DELETE WHERE` on the same matched UPDATE branch, plus conditional
  not-matched INSERT actions whose `insert_column` patches support column-only,
  value-only, and paired modes; existing VALUES cells can be replaced
  independently
- common DDL: `CREATE TABLE`, `CREATE SEQUENCE`, `CREATE VIEW`, `DROP TABLE`,
  and `TRUNCATE TABLE`
- transaction control, `GRANT / REVOKE`, and `COMMENT ON`
- `FOR UPDATE NOWAIT`
- remote object references such as `schema.table@link`
- common functions and analytic functions such as `DECODE`, `SYSDATE`, and
  `ROW_NUMBER() OVER (...)`
- quoted identifiers, `ALTER TABLE ADD`, `CREATE INDEX`, and `DROP INDEX`
- compatible materialized-view creation forms
- `CREATE SYNONYM` and `DROP SYNONYM`
- session statements: `ALTER SESSION SET CURRENT_SCHEMA = ...`,
  `ALTER SESSION SET CONTAINER = ...`,
  `ALTER SESSION SET CONTAINER = ... SERVICE = ...`, and ordinary parameter
  assignments such as `NLS_DATE_FORMAT`, `NLS_DATE_LANGUAGE`,
  `NLS_NUMERIC_CHARACTERS`, `INSTANCE`, and `ERROR_ON_OVERLAP_TIME`
- quoted schema identifiers in `CURRENT_SCHEMA` are marked as quoted
  identifiers in the public literal view
- `EXPLAIN PLAN FOR ...`, including basic `SET STATEMENT_ID` and `INTO` forms
- dynamic SQL execution through `EXECUTE IMMEDIATE ... USING ...`

## Explicitly Unsupported Scope

The following Oracle-specific constructs are not silently downgraded. They
return `SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable handle:

- legacy outer join `(+)`
- `RETURNING ... INTO` with `BULK COLLECT`, a receiver other than a
  colon-prefixed bind, or unequal target/receiver counts
- PL/SQL blocks, procedures, and packages
- `PIVOT` and `UNPIVOT`
- `MODEL` clause
- flashback query
- `MATCH_RECOGNIZE`

## Public Output Rules

- `sqlparser_deparse()` emits the public Oracle form and does not expose
  internal conversion details.
- A full-AST deparse keeps Oracle `OFFSET ... FETCH` pagination and does not
  downgrade it to `LIMIT`; local source edits still preserve unchanged
  pagination text when available.
- Oracle binds remain in `:name`, `:1`, or `?` form; internal `$1` / `$2`
  names are not emitted.
- `MINUS` remains visible as the Oracle semantic keyword in View JSON and
  deparse output.
- Hierarchical fields, values, and predicates use the existing Query Graph
  arrays with `start_with` / `connect_by` clauses, field `pseudo` / `prior`
  flags, `nocycle` on the CONNECT BY root predicate, and an operator
  `target_path` for `CONNECT_BY_ROOT`; no separate hierarchy object is added.
- View represents `RETURNING ... INTO` with one `kind = "sink"` channel, and
  every target's `sink_value` points to the output bind at the corresponding
  ordinal. `insert_column` atomically inserts the target/receiver pair in the
  same patch; one-sided insertion is not supported.
- An omitted MERGE INSERT target-column list still emits
  `target_list_selector`. A column-only patch can materialize that list, a
  value-only patch can append a VALUES cell while keeping the list omitted, and
  an explicit list continues to support paired insertion on both sides. If an
  explicit list exists when the patch batch finishes, the core patch API
  validates equal column/value widths and rolls back the batch on failure.
- Query Graph uses `alias_quoted_identifier` for double-quoted relation aliases
  and `output_quoted_identifier` for explicit double-quoted output aliases or
  inherited double-quoted field names when no explicit alias exists. View JSON
  emits either key only when its value is `true`.
- Relation qualification reports delimiter state per segment through
  `database_quoted_identifier`, `schema_quoted_identifier`, the existing object
  `quoted_identifier`, and `link_quoted_identifier`. DML target columns use
  `dml_column.quoted_identifier` for ordinary INSERT, MERGE INSERT, and every
  `INSERT ALL/FIRST` branch. Each flag describes only its corresponding
  segment; View JSON omits the key for an unquoted or absent segment, so case
  cannot be inferred from identifier spelling. Database-link targets retain
  independent schema, object, and link state as well.
- Attributable expression fragments in View JSON use the public Oracle
  form.
- Failed expression-fragment rewrites are not committed to the handle; the
  previous AST, bind mapping, and deparse output remain usable.

## Regression Cases

The Oracle support boundary is defined by:

- `tests/cases/oracle_dialect_input.json`
- `tests/cases/oracle_dialect_matrix.en.md`
- `tests/unit/test_oracle_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

The current Oracle matrix contains 271 cases and 876 independent patches, all
with `status = "final"`. Four hierarchical-query cases contain 20 independent
patches. O198 through O200 verify eight `RETURNING ... INTO` pairs on `INSERT`,
`UPDATE`, and `DELETE`, plus paired insertion at the head, middle, and tail.
