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
- `LIMIT offset,count`
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`
- `UPDATE` and `DELETE`
- basic `CREATE TABLE`
- `ALTER TABLE ADD COLUMN`
- `CREATE VIEW`
- `DROP TABLE`
- `START TRANSACTION` and `COMMIT`

## Explicitly Unsupported Scope

The following MySQL-specific constructs are not silently downgraded. They return
`SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable handle:

- `INSERT IGNORE`
- `INSERT DELAYED`
- `INSERT LOW_PRIORITY` and `INSERT HIGH_PRIORITY`
- `ON DUPLICATE KEY UPDATE`
- `REPLACE INTO`
- `UPDATE IGNORE`
- `DELETE IGNORE`
- `AUTO_INCREMENT`
- `UNSIGNED`
- `ZEROFILL`
- table options such as `ENGINE=...`
- charset and collation table options such as `DEFAULT CHARSET=...`,
  `CHARACTER SET=...`, and `COLLATE=...`

## Public Output Rules

- `sqlparser_deparse()` emits the public MySQL form and does not expose internal
  conversion details.
- Backtick-delimited identifiers and MySQL string compatibility rules are
  handled by the dialect layer.
- Attributable expression fragments in SQL View JSON use the public MySQL
  form.
- MySQL-specific semantics that cannot be represented safely are not downgraded
  to PostgreSQL semantics.

## Regression Cases

The MySQL support boundary is defined by:

- `tests/cases/mysql_dialect_input.json`
- `tests/cases/mysql_dialect_matrix.en.md`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

The current MySQL matrix contains 27 cases: 12 supported paths and 15 explicit
unsupported paths.
