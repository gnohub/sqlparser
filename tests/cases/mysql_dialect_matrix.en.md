# MySQL Dialect Case Matrix

This file records regression cases for the MySQL dialect conversion layer. `tests/cases/mysql_dialect_input.json` is the executable test source; `tests/unit/test_mysql_dialect_case_matrix.c` reads it and verifies parsing, View JSON, deparse output, and error codes.

## Validated Supported Statements

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| M001 | `mysql-select-limit-comma` | `SELECT ... FROM ... WHERE ... LIMIT offset,count` | backtick identifiers, double-quoted strings, table extraction, selected columns, WHERE literal, MySQL comma-limit deparse |
| M002 | `mysql-select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | multi-table join, selected columns, join columns, where columns |
| M003 | `mysql-hash-comment` | `SELECT ... # comment` | MySQL `#` line-comment preprocessing |
| M004 | `mysql-insert-values-multi-row` | `INSERT ... VALUES (...), (...)` | multi-row insert, insert columns, double-quoted string normalization |
| M004N | `mysql-national-string-literal` | `SELECT "..." ... N'...' ... n'...'` | national string prefix preservation; lowercase input is exposed with the canonical `N` prefix |
| M004NA | `mysql-national-string-duplicate-literal` | ordinary string and `N'...'` with the same text | restore the `N` prefix only for the original national string |
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
| M014Q | `mysql-unsupported-keywords-in-quoted-identifiers` | ``SELECT `unsigned`, `auto_increment`, `engine` ...`` | unsupported prefilter does not reject protected identifiers |
| M015 | `mysql-use-database` | `USE analytics` | default database switching and View JSON value selector |
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
| M035A | `mysql-view-case-predicate-bind` | `CASE WHEN column = ? THEN ...` | field-bound bind attribution for predicates inside SELECT projections |
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
| M053 | `mysql-multi-statement-global-bind-position` | multi-statement `UPDATE ... ?` | positional `bind_position` increases globally across the full input SQL |
| M054 | `mysql-select-derived-query-graph` | derived table with output alias and `?` parameters | `query_graph` lineage mapping from derived-table fields to inner base-table fields and `output_name` |
| M055 | `mysql-select-reference-002` | SELECT reference case 002 | MySQL-valid SELECT example parsing and View JSON shape |
| M056 | `mysql-select-reference-003` | SELECT reference case 003 | MySQL-valid SELECT example parsing and View JSON shape |
| M057 | `mysql-select-reference-006` | SELECT reference case 006 | MySQL-valid SELECT example parsing and View JSON shape |
| M058 | `mysql-select-reference-008` | SELECT reference case 008 | MySQL-valid SELECT example parsing and View JSON shape |
| M059 | `mysql-select-reference-010` | SELECT reference case 010 | MySQL-valid SELECT example parsing and View JSON shape |
| M060 | `mysql-select-reference-012` | SELECT reference case 012 | MySQL-valid SELECT example parsing and View JSON shape |
| M061 | `mysql-select-reference-014` | SELECT reference case 014 | MySQL-valid SELECT example parsing and View JSON shape |
| M062 | `mysql-select-reference-016` | SELECT reference case 016 | MySQL-valid SELECT example parsing and View JSON shape |
| M063 | `mysql-select-reference-022` | SELECT reference case 022 | MySQL-valid SELECT example parsing and View JSON shape |
| M064 | `mysql-select-reference-023` | SELECT reference case 023 | MySQL-valid SELECT example parsing and View JSON shape |
| M065 | `mysql-select-reference-025` | SELECT reference case 025 | MySQL-valid SELECT example parsing and View JSON shape |
| M066 | `mysql-select-reference-027` | SELECT reference case 027 | MySQL-valid SELECT example parsing and View JSON shape |
| M067 | `mysql-select-reference-029` | SELECT reference case 029 | MySQL-valid SELECT example parsing and View JSON shape |
| M068 | `mysql-update-join-target-table-qualified` | `UPDATE users JOIN ... SET users.phone = ?` | target-table qualified assignment still maps to the target relation when no alias is used |
| MU001 | `mysql-insert-ignore` | `INSERT IGNORE ...` | preserves the `IGNORE` modifier while reusing the ordinary INSERT AST |
| MU002 | `mysql-insert-delayed` | `INSERT DELAYED ...` | preserves the `DELAYED` modifier; MySQL 8.4 recognizes and ignores it at execution time |
| MU003 | `mysql-insert-low-priority` | `INSERT LOW_PRIORITY ...` | preserves the `LOW_PRIORITY` modifier while columns and values use ordinary INSERT structures |
| MU004 | `mysql-insert-high-priority` | `INSERT HIGH_PRIORITY ...` | preserves the `HIGH_PRIORITY` modifier while columns and values use ordinary INSERT structures |
| MU004A | `mysql-insert-low-priority-ignore` | `INSERT LOW_PRIORITY IGNORE ... VALUES (?, ?)` | combined modifiers and global positional parameters |
| MU004B | `mysql-insert-high-priority-ignore-select` | `INSERT HIGH_PRIORITY IGNORE ... SELECT ...` | combined modifiers and INSERT SELECT source graph |
| MU004C | `mysql-insert-ignore-on-duplicate-key` | `INSERT IGNORE ... ON DUPLICATE KEY UPDATE ...` | `IGNORE` combined with upsert assignments and bind positions |
| MU005 | `mysql-on-duplicate-key` | `INSERT ... ON DUPLICATE KEY UPDATE ...` | MySQL upsert mapped to DML inserted values and update assignments |
| MU007 | `mysql-update-ignore` | `UPDATE IGNORE ...` | preserves the `IGNORE` modifier while assignments and predicates reuse ordinary UPDATE structures |
| MU008 | `mysql-delete-ignore` | `DELETE IGNORE ...` | preserves the `IGNORE` modifier while predicates reuse ordinary DELETE structures |
| MU008A | `mysql-update-low-priority-ignore-join` | `UPDATE LOW_PRIORITY IGNORE ... JOIN ...` | combined modifiers with multi-table UPDATE JOIN using existing target, source, and predicate attribution |
| MU008B | `mysql-delete-low-priority-quick-ignore-join` | `DELETE LOW_PRIORITY QUICK IGNORE ... JOIN ...` | combined modifiers with multi-table DELETE JOIN using existing delete-target and predicate attribution |
| MU009 | `mysql-update-join` | `UPDATE ... JOIN ... SET ...` | ordinary/INNER/CROSS multi-table UPDATE with `ON`, including target relation, source relation, assignments, and predicate parameters |
| MU010 | `mysql-delete-join` | `DELETE u FROM ... JOIN ...` | ordinary/INNER/CROSS multi-table DELETE with `ON`, including target relation, source relation, and predicate parameters |
| MU010A | `mysql-update-join-on-bind` | `UPDATE ... JOIN ... ON ... ? SET ... WHERE ...` | JOIN `ON` parameters in multi-table UPDATE are attributed to `on`; later `WHERE` parameters remain attributed to `where` |
| MU010B | `mysql-delete-join-on-bind` | `DELETE u FROM ... JOIN ... ON ... ? WHERE ...` | JOIN `ON` parameters in multi-table DELETE are attributed to `on`; later `WHERE` parameters remain attributed to `where` |
| MU011 | `mysql-auto-increment` | `CREATE TABLE ... AUTO_INCREMENT` | MySQL auto-increment column attributes are mapped through the generic DDL AST and restored to public MySQL SQL during deparse |
| MU012 | `mysql-unsigned` | `CREATE TABLE ... UNSIGNED` | MySQL numeric `UNSIGNED` attributes are parsed and restored in public SQL |
| MU013 | `mysql-zerofill` | `CREATE TABLE ... ZEROFILL` | MySQL numeric `ZEROFILL` attributes are parsed and restored in public SQL |
| MU014 | `mysql-table-engine` | `CREATE TABLE ... ENGINE=...` | MySQL table engine options are parsed and restored in public SQL |
| MU015 | `mysql-table-charset` | `CREATE TABLE ... DEFAULT CHARSET=...` | MySQL default table charset options are parsed and restored in public SQL |
| MU016 | `mysql-table-character-set` | `CREATE TABLE ... CHARACTER SET=...` | MySQL table character-set options are parsed and restored in public SQL |
| MU017 | `mysql-table-collate` | `CREATE TABLE ... COLLATE=...` | MySQL table collation options are parsed and restored in public SQL |
| MU018 | `mysql-update-left-join` | `UPDATE ... LEFT JOIN ... SET ...` | LEFT JOIN multi-table UPDATE join kind, target assignment column, and condition-parameter mapping |
| MU019 | `mysql-delete-left-join` | `DELETE u FROM ... LEFT JOIN ...` | LEFT JOIN multi-table DELETE target/source relation and condition-parameter mapping |
| MU020 | `mysql-update-join-source-assignment` | `UPDATE ... JOIN ... SET source_alias.column = ...` | single-target update of the joined right-side table, including target/source relation and assignment-parameter mapping |
| MU021 | `mysql-delete-join-source-target` | `DELETE source_alias FROM target JOIN source_alias ...` | single-target delete of the joined right-side table, including delete target and condition-parameter mapping |
| M069 | `mysql-field-match-kind-direct-and-expression` | direct-field predicate plus function-wrapped field predicate | `query_graph.values[].field_match_kind` distinguishes `direct_field` from `expression_field` |
| M070 | `mysql-expression-field-case-expression-value` | CASE returns a field and compares with `?` | CASE expression fields emit `expression_field` value relations |
| M071 | `mysql-expression-field-multi-field-expression-value` | `CONCAT(secret, id)` and `secret + id` compared with `?` | Fields inside the expression keep separate `expression_field` value relations |
| M072 | `mysql-expression-field-value-side-expression` | field compared with function, CONCAT, and CAST value-side expressions | value-side expressions emit `kind=expression` instead of direct binds |
| M073 | `mysql-expression-field-dml-expression-values` | INSERT/UPDATE expression assignments | DML cells and assignments emit `kind=expression` |
| M074 | `mysql-update-bind-rhs-crypto-source` | `UPDATE ... SET protected = ?` | protected-field UPDATE SET right-hand positional bind for later structured backup assignment insertion and literal rewrite |
| M075 | `mysql-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = ?, protected2 = ?` | multiple protected-field SET binds, field attribution, and global bind positions |
| M076 | `mysql-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | MySQL literal ESCAPE is emitted in `values[].like_escape` |
| M077 | `mysql-like-escape-question-binds` | `LIKE ? ESCAPE ?` | pattern and escape JDBC positional parameters keep separate global bind positions |
| M078 | `mysql-not-like-escape-literal` | `NOT LIKE ? ESCAPE '!'` | structured output for pattern bind plus literal ESCAPE in negated LIKE |
| M079 | `mysql-like-without-explicit-escape` | `LIKE ?` | `like_escape` is omitted when ESCAPE is not explicit |
| M089 | `mysql-update-join-source-field-graph` | `UPDATE ... JOIN ... SET target = source.column` | `source_field` for base-table source fields on the right side of multi-table UPDATE assignments, plus `right_field` for JOIN/WHERE field comparisons |
| M090 | `mysql-insert-select-source-block-graph` | `INSERT ... SELECT ... FROM ...` | target columns, source block, and source-field lineage for INSERT SELECT |
| M091 | `mysql-with-cte-select` | `WITH cte AS (...) SELECT ...` | WITH common table expression, inner table attribution, and positional-parameter ordering |
| M092 | `mysql-window-row-number` | `ROW_NUMBER() OVER (PARTITION BY ... ORDER BY ...)` | field paths and output-target attribution inside a window-function expression |
| M093 | `mysql-common-scalar-functions` | `LOWER(...)` and `COALESCE(...)` | scalar-function output paths and WHERE parameter attribution |
| M094 | `mysql-common-data-types` | `CREATE TABLE` with common type names | common type names mapped to the current DDL/type AST |
| M095 | `mysql-rollback` | `ROLLBACK` | transaction rollback statement |
| M096 | `mysql-json-contains-function-predicate` | `JSON_CONTAINS(tags, ?)` | JSON function predicates reuse `fields/values/predicates` for fields, binds, and expression predicates |
| M097 | `mysql-select-alias-order-by-lineage` | `SELECT u.email AS e ... ORDER BY u.email` | SELECT output aliases keep base-field lineage, while ORDER BY fields stay independently attributed |
| M098 | `mysql-create-table-column-and-table-options` | `CREATE TABLE` with column attributes and table options | combined validation for `UNSIGNED`, `AUTO_INCREMENT`, `ZEROFILL`, `ENGINE`, `CHARSET`, and `COLLATE` in one create-table statement |
| M099 | `mysql-create-table-official-column-attributes` | `CREATE TABLE` with official column attributes | public SQL restoration for `PRIMARY KEY`, `COMMENT`, `CHARACTER SET`, `COLLATE`, `INVISIBLE`, generated columns, and column-level `REFERENCES` |
| M100 | `mysql-create-table-numeric-table-options` | `CREATE TABLE` with numeric table options | restores `AUTO_INCREMENT`, `AVG_ROW_LENGTH`, `CHECKSUM`, `DELAY_KEY_WRITE`, `KEY_BLOCK_SIZE`, row-count options, and statistics options |
| M101 | `mysql-create-table-string-and-storage-table-options` | `CREATE TABLE` with string and storage table options | restores `COMMENT`, `COMPRESSION`, `CONNECTION`, directory options, encryption, engine attributes, `ROW_FORMAT`, and `TABLESPACE` |
| M102 | `mysql-create-table-partition-options` | `CREATE TABLE` with `PARTITION BY` | restores partition tails for create-table statements without query expressions |
| M103 | `mysql-create-temporary-table-options` | `CREATE TEMPORARY TABLE IF NOT EXISTS` with column attributes and table options | combined restoration for temporary tables, column visibility, column comments, `ENGINE`, and `DEFAULT CHARACTER SET` |
| M104-M106 | MySQL INSERT extensions | `ON DUPLICATE KEY UPDATE`, row aliases, and `INSERT ... SET` | conflict-update sources, row aliases, and SET write shape |
| M107-M109 | MySQL DELETE/UPDATE extensions | aliased delete targets, `ORDER BY`, and `LIMIT` | delete-target attribution and DML-tail restoration |
| M110 | `mysql-select-lock-in-share-mode` | `LOCK IN SHARE MODE` | locking-read parsing and public SQL restoration |
| M111 | `mysql-select-straight-join` | `STRAIGHT_JOIN` | relations, ON fields, and public SQL restoration |
| M112-M115 | MySQL index hints | `USE/FORCE/IGNORE INDEX` with scopes | hint positioning and public SQL restoration |
| M116 | `mysql-comma-table-index-hint-attribution` | index hint on a comma table list | adjacent-relation restoration position |
| M117-M118 | index-hint list boundaries | multiple names and empty `USE INDEX ()` | complete-list and empty-list restoration |
| M119-M121 | JOIN structure boundaries | nested JOIN, `USING`, and `NATURAL` | relations, ON/USING fields, and public SQL restoration |
| M122-M123 | locking-read wait policies | `NOWAIT` and `SKIP LOCKED` | locking-read parsing and public SQL restoration |
| M124 | `mysql-update-order-limit-bind-isolation` | binds with `ORDER BY` and `LIMIT` | assignment and WHERE binds are isolated from DML tails |
| M125 | `mysql-nested-straight-join-order` | nested `STRAIGHT_JOIN` | relation order and public SQL restoration |
| M126 | `mysql-on-duplicate-row-column-aliases` | row-alias column list | alias columns are not attributed to the target table |
| M127 | `mysql-cte-index-hint-location-attribution` | index hints inside and outside a CTE | CTE source blocks, nested relations, and hint restoration positions |
| M128 | `mysql-table-partition-with-index-hints` | `PARTITION(...)` with index hints | qualified tables, aliases, and public SQL restoration |
| M129-M135 | index hints at query-tail boundaries | `HAVING`, `WINDOW`, set operations, and locking clauses | hints remain after relations and before query tails |
| M136-M140 | index hints with JOIN and scope combinations | `NATURAL JOIN`, `STRAIGHT_JOIN`, `USING`, aliases, and multiple scoped hints | left/right relations, aliases, and multi-hint restoration order |
| M141-M145 | nested and identifier boundaries for index hints | CTEs, multiple statements, partitioned tables, reserved-word aliases, and derived tables | hint positions at each relation depth and public SQL restoration |
| MU006 | `mysql-replace-into` | `REPLACE INTO ... VALUES ...` | MySQL `REPLACE` reuses the INSERT graph shape and preserves replace semantics with `insert_mode=replace_values` |
| MU006A | `mysql-replace-low-priority-multi-row` | `REPLACE LOW_PRIORITY INTO ... VALUES (...), (...)` | multi-row `REPLACE VALUES`, positional parameters, and `LOW_PRIORITY` modifier |
| MU006B | `mysql-replace-delayed-select` | `REPLACE DELAYED INTO ... SELECT ...` | `REPLACE SELECT` target table, source table, positional parameters, and `insert_mode=replace_select` |
| MU006C | `mysql-replace-set` | `REPLACE INTO ... SET ...` | `SET` form is normalized to public MySQL `REPLACE ... VALUES` and emits `insert_mode=replace_set` |
| MU006D | `mysql-replace-without-into` | `REPLACE table ... VALUES ...` | official form without `INTO` is normalized to `REPLACE INTO ...` |
| MU006E | `mysql-replace-table-source` | `REPLACE INTO ... TABLE source` | official `TABLE` form is normalized to `REPLACE ... SELECT * FROM source` and preserves the source table |

## Explicitly Unsupported Statements

The executable MySQL dialect matrix currently has no explicit unsupported cases. Official syntax coverage boundaries are tracked in `doc/mysql_official_syntax_coverage.csv`.

## Rules

- The default dialect is `SQLPARSER_DIALECT_POSTGRESQL`.
- MySQL statements must be parsed through `sqlparser_parse_with_options` with `SQLPARSER_DIALECT_MYSQL`.
- Safely mappable syntax is handled in dialect preprocess / postprocess.
- MySQL-specific semantics that cannot be safely mapped return `SQLPARSER_STATUS_UNSUPPORTED`.
- New MySQL support must update `tests/cases/mysql_dialect_input.json`, this matrix, and executable regression tests.
