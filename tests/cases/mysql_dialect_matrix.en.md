# MySQL Dialect Case Matrix

This file records regression cases for the MySQL dialect conversion layer. `tests/cases/mysql_dialect_input.json` is the executable test source; `tests/unit/test_mysql_dialect_case_matrix.c` reads it and verifies parsing, SQL View JSON, deparse output, and error codes.

## Validated Supported Statements

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| M001 | `mysql-select-limit-comma` | `SELECT ... FROM ... WHERE ... LIMIT offset,count` | backtick identifiers, double-quoted strings, table extraction, selected columns, WHERE literal, MySQL comma-limit deparse |
| M002 | `mysql-select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | multi-table join, selected columns, join columns, where columns |
| M003 | `mysql-hash-comment` | `SELECT ... # comment` | MySQL `#` line-comment preprocessing |
| M004 | `mysql-insert-values-multi-row` | `INSERT ... VALUES (...), (...)` | multi-row insert, insert columns, double-quoted string normalization |
| M005 | `mysql-insert-select` | `INSERT ... SELECT ... FROM ... WHERE ...` | insert columns, inner selected columns, WHERE columns |
| M006 | `mysql-update-basic` | `UPDATE ... SET ... WHERE ...` | updated columns, where columns, backtick identifiers |
| M007 | `mysql-delete-conditional` | `DELETE FROM ... WHERE ... AND ...` | conditional delete and multi-condition column extraction |
| M008 | `mysql-create-table-basic` | `CREATE TABLE ... (...)` | basic create-table statement and column-definition parsing |
| M009 | `mysql-alter-table-add-column` | `ALTER TABLE ... ADD COLUMN ...` | alter-table parsing and column-definition deparse |
| M010 | `mysql-create-view` | `CREATE VIEW ... AS SELECT ...` | view definition and inner SELECT extraction |
| M011 | `mysql-drop-table` | `DROP TABLE ...` | drop-table parsing and table extraction |
| M012 | `mysql-start-transaction` | `START TRANSACTION; COMMIT` | MySQL transaction start and multi-statement counting |
| M013 | `mysql-unsupported-keywords-in-string` | `SELECT 'INSERT IGNORE' ...` | unsupported prefilter does not reject string content |
| M014 | `mysql-unsupported-keywords-in-comment` | `SELECT ... /* ON DUPLICATE KEY UPDATE */ ...` | unsupported prefilter does not reject comment content |
| M015 | `mysql-use-database` | `USE analytics` | default database switching and SQL View value selector |
| M016 | `mysql-use-quoted-database` | `USE \`analytics-prod\`` | backtick-delimited database name and public value fragment |
| M017 | `mysql-use-database-in-multi-statement` | `USE ...; SELECT ...` | database switching and following query remain separate in multi-statement input |
| M018 | `mysql-insert-question-params` | `INSERT ... VALUES (?, ?, ?)` | JDBC-style positional parameter conversion, inserted-column extraction, and public-form restoration |
| M019 | `mysql-update-question-params` | `UPDATE ... SET ... WHERE ... = ?` | positional parameter conversion and public-form restoration in SET/WHERE clauses |
| M020 | `mysql-prepare-from-literal` | `PREPARE stmt FROM 'SELECT ... ?'` | MySQL SQL-level prepared statement, `?` placeholder, and public-form restoration |
| M021 | `mysql-execute-using` | `EXECUTE stmt USING @var` | prepared statement execution with user-variable arguments |
| M022 | `mysql-deallocate-prepare` | `DEALLOCATE PREPARE stmt` | prepared statement deallocation |
| M023 | `mysql-drop-prepare` | `DROP PREPARE stmt` | MySQL `DROP PREPARE` deallocation alias |
| M024 | `mysql-select-question-params` | `SELECT ... WHERE ... = ?` | JDBC-style positional parameters in query predicates |
| M025 | `mysql-select-in-question-params` | `SELECT ... IN (?, ?, ?)` | multiple positional parameters in `IN` predicates |
| M026 | `mysql-select-limit-question-params` | `LIMIT ? OFFSET ?` | positional parameters in pagination clauses |
| M027 | `mysql-insert-named-columns-question-params` | `INSERT ... VALUES (?, ?, ?)` | insert columns and positional parameter value lists |
| M028 | `mysql-insert-multi-row-question-params` | multi-row `INSERT ... VALUES` + `?` | multi-row parameterized insert |
| M029 | `mysql-update-multi-question-params` | `UPDATE ... SET ... WHERE ... = ?` | updated columns, predicate columns, and positional parameters |
| M030 | `mysql-delete-question-params` | `DELETE ... WHERE ... = ?` | conditional delete and positional parameters |
| M031 | `mysql-prepare-insert-literal` | `PREPARE stmt FROM 'INSERT ... ?'` | prepared insert SQL text and `?` placeholders |
| M032 | `mysql-prepare-from-user-variable` | `PREPARE stmt FROM @var` | prepared SQL text from a user variable |
| M033 | `mysql-execute-using-multiple-vars` | `EXECUTE stmt USING @id, @name` | multiple user-variable bind arguments |
| M034 | `mysql-view-concat-function` | `SELECT CONCAT(UPPER(...), ...) ...` | function `target_path`, nested function, argument index, and WHERE bind |
| M035 | `mysql-view-case-expression` | `SELECT CASE WHEN ... THEN ... END ...` | output-field attribution inside `CASE` expressions |
| M036 | `mysql-view-group-having-order` | `GROUP BY ... HAVING ... ORDER BY ...` | aggregate output and non-output clause attribution |
| M037 | `mysql-view-update-question-binds` | `UPDATE ... SET ... WHERE ... = ?` | positional bind, null value, and update/where clause attribution |
| M038 | `mysql-view-join-on` | `JOIN ... ON ... WHERE ... = ?` | JOIN/ON fields, WHERE bind, and table-column attribution |
| M039 | `mysql-select-between-question-params` | `BETWEEN ? AND ?` | multiple positional parameters and field-value attribution in `BETWEEN` predicates |
| M040 | `mysql-select-not-in-question-params` | `NOT IN (?, ?)` | multiple positional parameters and field-value attribution in negated `IN` predicates |
| M041 | `mysql-select-not-between-question-params` | `NOT BETWEEN ? AND ?` | multiple positional parameters and field-value attribution in negated `BETWEEN` predicates |
| M042 | `mysql-select-not-like-question-param` | `NOT LIKE ?` | positional parameter, field-level operator, and keyword attribution in negated `LIKE` predicates |
| M043 | `mysql-select-distinct-like-param` | `SELECT DISTINCT ... WHERE ... LIKE ?` | DISTINCT projection, LIKE positional parameter, and field attribution |
| M044 | `mysql-select-left-join-alias-star` | `LEFT JOIN` + `alias.*` | qualified star, JOIN/ON fields, and WHERE bind |
| M045 | `mysql-delete-in-question-params` | `DELETE ... WHERE ... IN (?, ?)` | conditional delete, collection parameters, and field operator |
| M046 | `mysql-update-in-question-params` | `UPDATE ... SET ? WHERE ... IN (?, ?)` | SET bind, WHERE collection predicate, and parameter order |
| M047 | `mysql-select-derived-table-filter` | derived table + outer filter | inner/outer WHERE clauses, derived-table alias, and bind attribution |
| M048 | `mysql-select-json-extract` | `JSON_EXTRACT(...)` | dialect function projection and WHERE bind |
| M049 | `mysql-create-table-if-not-exists` | `CREATE TABLE IF NOT EXISTS ...` | conditional table creation and common column types |
| M050 | `mysql-drop-view-if-exists` | `DROP VIEW IF EXISTS ...` | view drop and object-name extraction |
| M051 | `mysql-select-order-by-ordinal` | `ORDER BY 1` | ordinal sort item and projection-order related syntax |
| M052 | `mysql-limit-comma-question-params` | `LIMIT ?, ?` | positional parameters in MySQL comma-limit syntax, with public SQL preserved in comma-limit form |

## Explicitly Unsupported Statements

The following syntax has MySQL-specific semantics or structures that cannot be safely represented by the current AST. Parsing returns `SQLPARSER_STATUS_UNSUPPORTED`.

| Case ID | Case Name | Statement Shape | Reason |
| --- | --- | --- | --- |
| MU001 | `mysql-insert-ignore` | `INSERT IGNORE ...` | error-ignoring semantics cannot be safely downgraded to ordinary `INSERT` |
| MU002 | `mysql-insert-delayed` | `INSERT DELAYED ...` | delayed-insert semantics require MySQL-specific execution semantics |
| MU003 | `mysql-insert-low-priority` | `INSERT LOW_PRIORITY ...` | priority semantics cannot be mapped to the generic AST |
| MU004 | `mysql-insert-high-priority` | `INSERT HIGH_PRIORITY ...` | priority semantics cannot be mapped to the generic AST |
| MU005 | `mysql-on-duplicate-key` | `ON DUPLICATE KEY UPDATE ...` | conflict-handling semantics cannot be safely represented by the current AST |
| MU006 | `mysql-replace-into` | `REPLACE INTO ...` | delete-then-insert semantics differ from ordinary `INSERT` |
| MU007 | `mysql-update-ignore` | `UPDATE IGNORE ...` | error-ignoring semantics require MySQL-specific execution semantics |
| MU008 | `mysql-delete-ignore` | `DELETE IGNORE ...` | error-ignoring semantics require MySQL-specific execution semantics |
| MU009 | `mysql-auto-increment` | `AUTO_INCREMENT` | column attributes require MySQL DDL semantic extensions |
| MU010 | `mysql-unsigned` | `UNSIGNED` | type attributes require MySQL type-system extensions |
| MU011 | `mysql-zerofill` | `ZEROFILL` | type attributes require MySQL type-system extensions |
| MU012 | `mysql-table-engine` | `ENGINE=...` | table options require MySQL DDL semantic extensions |
| MU013 | `mysql-table-charset` | `DEFAULT CHARSET=...` | table charset options require MySQL DDL semantic extensions |
| MU014 | `mysql-table-character-set` | `CHARACTER SET=...` | table charset options require MySQL DDL semantic extensions |
| MU015 | `mysql-table-collate` | `COLLATE=...` | table collation options require MySQL DDL semantic extensions |

## Rules

- The default dialect is `SQLPARSER_DIALECT_POSTGRESQL`.
- MySQL statements must be parsed through `sqlparser_parse_with_options` with `SQLPARSER_DIALECT_MYSQL`.
- Safely mappable syntax is handled in dialect preprocess / postprocess.
- MySQL-specific semantics that cannot be safely mapped return `SQLPARSER_STATUS_UNSUPPORTED`.
- New MySQL support must update `tests/cases/mysql_dialect_input.json`, this matrix, and executable regression tests.
