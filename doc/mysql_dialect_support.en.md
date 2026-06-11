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
- preserved `INSERT IGNORE`, `INSERT DELAYED`, `INSERT LOW_PRIORITY`, and `INSERT HIGH_PRIORITY` modifiers
- basic `REPLACE VALUES`, `REPLACE SET`, `REPLACE SELECT`, and `REPLACE TABLE` forms
- preserved `UPDATE LOW_PRIORITY/IGNORE` and `DELETE LOW_PRIORITY/QUICK/IGNORE` modifiers
- `INSERT ... ON DUPLICATE KEY UPDATE`
- `UPDATE` and `DELETE`
- basic multi-table `UPDATE ... JOIN ... SET ...` forms with `ON` conditions
- basic multi-table `DELETE u FROM ... JOIN ...` forms with `ON` conditions
- basic `CREATE TABLE`, column attributes, table options, and partition tails without query expressions
- `ALTER TABLE ADD COLUMN`
- `CREATE VIEW`
- `DROP TABLE`
- `START TRANSACTION`, `COMMIT`, and `ROLLBACK`
- `USE db_name` default database switching
- `PREPARE`, `EXECUTE`, `DEALLOCATE PREPARE`, and `DROP PREPARE`

## Explicitly Unsupported Scope

The executable MySQL dialect matrix currently has no explicit unsupported
cases. Official syntax coverage boundaries are tracked in
`mysql_official_syntax_coverage.csv`.

## Public Output Rules

- `sqlparser_deparse()` emits the public MySQL form and does not expose internal
  conversion details.
- Backtick-delimited identifiers and MySQL string compatibility rules are
  handled by the dialect layer.
- Attributable expression fragments in View JSON use the public MySQL
  form.
- MySQL-specific semantics that cannot be represented safely are not downgraded
  to PostgreSQL semantics.

## Regression Cases

The MySQL support boundary is defined by:

- `tests/cases/mysql_dialect_input.json`
- `tests/cases/mysql_dialect_matrix.en.md`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

The current MySQL matrix contains 131 cases: 131 supported paths and 0 explicit
unsupported paths.
