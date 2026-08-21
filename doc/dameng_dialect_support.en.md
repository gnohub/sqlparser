# Dameng Dialect Support

`SQLPARSER_DIALECT_DAMENG` provides a conversion layer from Dameng DM_SQL to the
current `sqlparser` AST model. Callers select it explicitly through
`sqlparser_parse_with_options()`; when no dialect is specified, parsing uses the
PostgreSQL grammar.

## Supported Scope

The Dameng dialect supports common SQL forms that can be safely mapped to the
current AST. The executable case matrix defines the support boundary:

- `SELECT`, aliases, subqueries, joins, `WHERE`, `GROUP BY`, and `HAVING`
- Dameng hierarchical queries through `START WITH ... CONNECT BY [NOCYCLE]`,
  `PRIOR`, `LEVEL`, and `CONNECT_BY_ROOT` in the SELECT list, preserving both
  `START WITH ... CONNECT BY` and `CONNECT BY ... START WITH` source clause
  orders
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
- multi-table single-target `UPDATE` with JOIN chains, comma-separated relation
  lists, and mixed forms; every SET assignment must target the same table object
- DML host-variable returns: `RETURNING <target, ...> INTO <:bind, ...>` for
  `INSERT` and `DELETE`, and `RETURN <target, ...> INTO <:bind, ...>` for
  `UPDATE`; both lists contain `N >= 1` items, have strictly equal lengths,
  and pair by ordinal
- mappable `MERGE`, including a post-assignment `WHERE` and an attached
  `DELETE WHERE` on the same matched UPDATE branch; `insert_column` on a
  not-matched INSERT supports column-only, value-only, and paired modes, and
  existing VALUES cells can be replaced independently
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

- `PIVOT` and `UNPIVOT`
- `RETURN` / `RETURNING ... INTO` forms with `BULK COLLECT`, receivers that are
  not colon-prefixed host binds, or unequal target/bind list lengths
- multi-table `UPDATE` statements whose SET assignments target multiple table
  objects
- DMSQL blocks, procedures, and packages
- other `ALTER SESSION` parameters outside the supported list
- `ALTER SESSION SET CONTAINER = ...`

## Public Output Rules

- `sqlparser_deparse()` emits the public Dameng form and does not expose
  internal conversion details.
- Generation-`0` output or a local source edit can preserve
  `LIMIT offset,count`; a full-AST fallback may canonicalize it to the
  semantically equivalent `LIMIT count OFFSET offset`.
- Binds remain in `:name`, `:1`, or `?` form; internal `$1` / `$2` names are
  not emitted.
- `MINUS` remains visible as the Dameng semantic keyword in View JSON and
  deparse output.
- Hierarchical fields, values, and predicates use the existing Query Graph
  arrays with `start_with` / `connect_by` clauses, field `pseudo` / `prior`
  flags, `nocycle` on the CONNECT BY root predicate, and an operator
  `target_path` for `CONNECT_BY_ROOT`; no separate hierarchy object is added.
- `SET SCHEMA` uses the `CURRENT_SCHEMA` field name in View JSON.
- DML return channels use a sink channel in `dml.result_channels`; every
  return target's `sink_value` refers to the same-ordinal host bind in
  `query_graph.values[]`.
- A multi-table `UPDATE` always has one `dml.target_relation`; every
  assignment's `target_field` resolves to that relation.
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

The current Dameng matrix contains 185 cases, all with `status = "final"`, and
658 independent patches. Six cases cover multi-table single-target `UPDATE`.
Three multi-return cases respectively verify INSERT
`RETURNING`, UPDATE `RETURN`, and DELETE `RETURNING` with 8↔8 pairs and atomic
head, middle, and tail insertions that produce 9↔9 pairs.
