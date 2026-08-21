# MySQL Dialect Support

`SQLPARSER_DIALECT_MYSQL` provides a conversion layer from MySQL SQL to the
current `sqlparser` AST model. Callers select it explicitly through
`sqlparser_parse_with_options()`; when no dialect is specified, parsing uses the
PostgreSQL grammar.

## Supported Scope

The MySQL dialect supports common SQL forms that can be safely mapped to the
current AST. The executable case matrix defines the support boundary:

- `SELECT`, aliases, subqueries, joins, and `WHERE`
- backtick-delimited identifiers
- MySQL `#` line comments
- compatible double-quoted string handling
- `N'...'` national string literals with the `N` prefix preserved in public output
- JDBC-style `?` positional parameters
- `LIMIT offset,count`
- `WITH` common table expressions
- window functions
- common scalar-function expressions
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`
- `INSERT ... SET` and `VALUES/SET` row aliases
- preserved `INSERT IGNORE`, `INSERT DELAYED`, `INSERT LOW_PRIORITY`, and `INSERT HIGH_PRIORITY` modifiers
- basic `REPLACE VALUES`, `REPLACE SET`, `REPLACE SELECT`, and `REPLACE TABLE` forms
- preserved `UPDATE LOW_PRIORITY/IGNORE` and `DELETE LOW_PRIORITY/QUICK/IGNORE` modifiers
- `INSERT ... ON DUPLICATE KEY UPDATE`
- mappable `MERGE` through the project's MySQL compatibility entry;
  `insert_column` on a not-matched INSERT supports column-only, value-only, and
  paired modes, and existing VALUES cells can be replaced independently. This
  is a parser compatibility contract, not a claim that the MySQL server
  officially supports `MERGE`
- `UPDATE` and `DELETE`
- single-table `UPDATE` and `DELETE` with `ORDER BY ... LIMIT`, and aliased delete targets
- multi-table `UPDATE` with JOIN chains, comma-separated relations, and assignments that write multiple relations; each assignment target field identifies its write relation
- basic multi-table `DELETE u FROM ... JOIN ...` forms with `ON` conditions
- `STRAIGHT_JOIN`, `JOIN ... USING`, and `NATURAL JOIN`
- `USE/FORCE/IGNORE INDEX|KEY` with `FOR JOIN|ORDER BY|GROUP BY` scopes
- `LOCK IN SHARE MODE`, `FOR UPDATE/SHARE`, `NOWAIT`, and `SKIP LOCKED`
- query-table `PARTITION(...)` selection clauses
- basic `CREATE TABLE`, column attributes, table options, and partition tails without query expressions
- `ALTER TABLE ADD COLUMN`
- `CREATE VIEW`
- `DROP TABLE`
- `START TRANSACTION`, `COMMIT`, and `ROLLBACK`
- `USE db_name`
- `PREPARE`, `EXECUTE`, `DEALLOCATE PREPARE`, and `DROP PREPARE`

## Explicitly Unsupported Scope

The executable MySQL dialect fixture lists successful cases only; failure paths
are maintained by separate unit tests. Official syntax coverage boundaries are
tracked in `mysql_official_syntax_coverage.csv`.

Multi-table `UPDATE` does not accept `ORDER BY` or `LIMIT`.

## Public Output Rules

- `sqlparser_deparse()` emits the public MySQL form and does not expose internal
  conversion details.
- Backtick-delimited identifiers and MySQL string compatibility rules are
  handled by the dialect layer.
- View JSON uses the common `query_graph` structure; identifiers and values in
  that structure use the public MySQL form.
- An omitted MERGE INSERT target-column list still emits
  `target_list_selector`. A column-only patch can materialize that list, a
  value-only patch can append a VALUES cell while keeping the list omitted, and
  an explicit list continues to support paired insertion on both sides. If an
  explicit list exists when the patch batch finishes, the core patch API
  validates equal column/value widths and rolls back the batch on failure.
- Query Graph uses `alias_quoted_identifier` for backtick-delimited relation
  aliases and `output_quoted_identifier` for explicit backtick-delimited output
  aliases or inherited backtick-delimited field names when no explicit alias
  exists. View JSON emits either key only when its value is `true`.
- A multi-target `UPDATE` omits a single `dml.target_relation`; each assignment
  identifies its write relation through the field referenced by `target_field`.
- MySQL-specific semantics that cannot be represented safely are not downgraded
  to PostgreSQL semantics.

## Regression Cases

The MySQL support boundary is defined by:

- `tests/cases/mysql_dialect_input.json`
- `tests/cases/mysql_dialect_matrix.en.md`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

The current MySQL matrix contains 262 cases with `status = "final"` and 881
independent patches.
