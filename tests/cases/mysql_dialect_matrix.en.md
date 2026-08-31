# MySQL Dialect Case Matrix

This file records regression cases for the MySQL dialect conversion layer. The executable fixture is `tests/cases/mysql_dialect_input.json`. For every final case, the runner requires unchanged SQL to deparse byte for byte, compares the actual View with the expected JSON structure, and executes each patch independently. Patched SQL must match `patch.deparse` byte for byte, remain identical after a fresh parse and second deparse, and produce the same View from the patched and freshly parsed handles. When a case provides `bind_occurrences`, the runner also compares `position`, `kind`, `key`, and original `sql` item by item for the source SQL and every patched SQL; repeated keys remain separate and `position` is continuous across the full SQL text.

## Matrix Counts and Session Regression

The fixture contains 270 cases with `status = "final"` and 898 independent
patches. Three cases and their 11 patches contain complete bind-occurrence
assertions. Expected View JSON contains
statement-level `query_graph.session` output in 37 cases, covering `M015`
through `M017`, `MY-001` through `MY-029`, and 5 `USE` boundaries interleaved
with comments or empty statements. All 37 contain at least one non-empty
session projection.

View validation compares JSON structures; object-key order and formatting
whitespace do not participate. Session action, item scope, target kind, name,
and value fields are all part of that comparison.

## Validated Supported Statements

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| M001 | `mysql-select-limit-comma` | `SELECT ... FROM ... WHERE ... LIMIT offset,count` | backtick identifiers, double-quoted strings, table extraction, selected columns, WHERE literal, MySQL comma-limit deparse |
| M002 | `mysql-select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | multi-table join, selected columns, join columns, where columns |
| M003 | `mysql-hash-comment` | `SELECT ... # comment` | MySQL `#` line-comment preprocessing |
| M004 | `mysql-insert-values-multi-row` | `INSERT ... VALUES (...), (...)` | multi-row insert and insert columns; deparsing the unmodified handle reproduces the input byte for byte, including the original double-quoted string |
| M004N | `mysql-national-string-literal` | `SELECT "..." ... N'...' ... n'...'` | the AST preserves national-string semantics; deparsing the unmodified handle reproduces the input `N`/`n` spelling byte for byte |
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
| M053 | `mysql-multi-statement-global-bind-position` | versioned executable `UPDATE` plus an ordinary `UPDATE` | `?` inside the whole-statement executable comment counts, while pseudo-`?` text in ordinary and inline versioned comments does not; a complex patch covers a subquery, CAST, CASE, `LIMIT/OFFSET`, protected regions, and continuous renumbering after deletion/insertion |
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
| MU005A | `mysql-on-duplicate-key-assignment-patch` | two-assignment `ON DUPLICATE KEY UPDATE` | ordered root `assignment[A]` selectors; insertion, full replacement, and deletion retain MySQL syntax |
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
| M106A | `mysql-insert-set-paired-column-patch` | paired columns and values in `INSERT ... SET` | `insert_columns` atomically inserts and deletes a same-position column/value pair |
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
| M226-M229 | UPDATE/DELETE tail structure regression | `ORDER BY` only, `LIMIT` only, DELETE bind isolation, and alias-qualified multiple sort keys | independently attributed and patchable ORDER fields; LIMIT does not affect WHERE/assignment binds; byte-exact original and patched SQL |
| M230 | `mysql-cte-update-nested-order-limit-bind-isolation` | CTE, `USE INDEX FOR ORDER BY`, correlated scalar-subquery ordering, and mixed assignment/WHERE/ORDER/LIMIT binds | the index-hint `FOR ORDER BY` does not become a DML tail; CTE source blocks and correlated fields are attributed precisely; the first three semantic binds belong to the assignment, outer WHERE, and nested predicate; the LIMIT bind stays outside View; all 8 patches deparse exactly |
| M231 | `mysql-update-join-compound-on-where-or-bind-order` | compound `ON AND`, `WHERE OR`, and mixed ON/SET/WHERE binds | ON and WHERE have separate boolean roots; bind positions are ON 1, SET 2, and WHERE 3/4; field, relation, value, and assignment patches deparse exactly |
| M232 | `mysql-delete-left-join-where-three-and` | `LEFT JOIN` plus a three-term `WHERE AND` | the ON comparison is independent of the WHERE AND root; all three WHERE children stay in the WHERE clause; field, value, and relation patches deparse exactly |
| M233 | `mysql-delete-right-join-compound-on-no-where` | `RIGHT JOIN` with a compound ON and no WHERE | the delete target, reversed relation order, ON AND root, and bind attribution remain correct without a synthetic WHERE |
| M234 | `mysql-update-join-where-or-and-precedence` | unparenthesized `WHERE a OR b AND c` | WHERE retains an OR root with a right-side AND subtree and remains separate from ON; assignment, field, value, and relation patches preserve precedence |
| M235 | `mysql-update-join-user-wrapper-name-where` | an ordinary WHERE function whose name matches the internal marker | only the selected internal wrapper is attributed to ON; the user function remains a WHERE expression predicate through field and assignment patches |
| M236 | `mysql-named-window-partition-order-independent-selectors` | a named window containing both `PARTITION BY` and `ORDER BY` | definition fields are classified as `window_partition` and `order_by`; the same-named SELECT and window-partition fields retain independent selectors verified by individual patches |
| M237 | `mysql-named-window-reused-multiple-order-fields` | two window functions reuse one named window with two ordering fields | the physical definition is traversed once, and both ordering fields enter Query Graph with independently patchable selectors |
| M238 | `mysql-named-window-inheritance-partition-order` | a base window defines partitioning and an inherited window adds ordering | both physical definitions are traversed in order, with separate attribution and selectors for the partition and inherited ordering fields |
| M239 | `mysql-named-window-frame-and-query-order` | named-window partitioning, ordering, a ROWS frame, and query-level `ORDER BY` | window and query ordering remain independently addressable; frame spelling, View semantics, and every patch result are verified exactly |
| M240 | `mysql-join-on-field-cast-expression-value` | a direct field compared with a `CAST(? AS SIGNED)` value-side expression in JOIN ON | the ON comparison `predicate.value` references the sole expression value, `field_match_kind=direct_field`, and the bind inside CAST is not promoted to a separate value |
| M241 | `mysql-join-on-function-and-field-comparison` | a function-to-bind comparison and a field-to-field comparison combined by AND in JOIN ON | ON preserves AND-child order; `UPPER(u.role_code) = ?` emits an expression predicate, while `u.role_id = r.id` emits a two-sided field comparison; all 3 patches deparse exactly |
| M242 | `mysql-row-in-subquery-membership` | `(tenant_id, id) IN (SELECT tenant_id, user_id ...)` | retains both row-value fields without arbitrarily attaching one to the membership predicate; the inner two targets, filter bind, and block attribution remain explicit, with 6 cross-level selector patches |
| M243 | `mysql-left-join-on-null-test` | `LEFT JOIN ... ON p.user_id = u.id AND p.deleted_at IS NULL` | preserves ON-child order for the field-to-field comparison and `IS NULL` comparison; the null test references only `p.deleted_at` and creates no NULL value, while the WHERE bind remains independent; all 4 patches are verified exactly |
| M244 | `mysql-relation-patch-cte-outer-qualifiers` | a CTE definition, its base relation, and an outer CTE reference | View distinguishes the CTE source block from the outer relation; the outer relation patch updates its qualified star, direct field, and WHERE field while preserving the CTE declaration and inner base relation |
| M245 | `mysql-update-join-compound-rhs` | a compound assignment RHS in `UPDATE ... JOIN` | `rhs_fields` and `rhs_values` attribute the source field, positional bind, and literal to one assignment; the source alias remains stable, and assignment replacement and insertion are verified exactly |
| M247 | `mysql-derived-mixed-limit-surfaces` | a derived query uses `LIMIT offset, count` while the outer query uses `LIMIT count OFFSET offset` | each query level preserves its original LIMIT surface; relation, inner and outer target, and outer target-list insertion patches all deparse exactly |
| M248 | `mysql-union-result-limit-offset` | a `UNION ALL` result uses parameterized `LIMIT count OFFSET offset` | View preserves the set root and both branch attributions; relation and branch-target patches retain the OFFSET form byte for byte |
| M249 | `mysql-limit-comma-comment-trivia` | ordinary block comments occupy all three token gaps in `LIMIT offset, count` | the original SQL parses and is preserved byte for byte at generation 0; ordinary-comment replay after the target patch retains the correct expectation for RG016 closure |
| M250 | `mysql-string-quote-escape-surfaces` | single- and double-quoted strings using backslash and doubled-quote escapes | View records all four equivalent string values and selectors precisely; original deparse and relation, target, value, and insertion patches preserve every untouched literal byte for byte |
| M251 | `mysql-string-common-backslash-escapes` | common newline, tab, and backslash string escapes | View retains decoded string semantics; original escape spellings and the quote, national prefix, and backslash spelling of patch fragments are preserved byte for byte |
| M252 | `mysql-string-equal-value-surfaces` | plain, lowercase-`n`, and uppercase-`N` strings with the same value | View exposes three independently addressable values; AST ownership preserves each surface spelling without reusing another equal-valued literal after replacement or insertion |
| M253 | `mysql-string-nested-surface-owners` | an outer double-quoted string, an inner lowercase-`n` string, and an escaped WHERE string | nested block, relation, field, target, and value attribution is complete; cross-level patches preserve every untouched string byte for byte |
| MU006 | `mysql-replace-into` | `REPLACE INTO ... VALUES ...` | MySQL `REPLACE` reuses the INSERT graph shape and preserves replace semantics with `insert_mode=replace_values` |
| MU006A | `mysql-replace-low-priority-multi-row` | `REPLACE LOW_PRIORITY INTO ... VALUES (...), (...)` | multi-row `REPLACE VALUES`, positional parameters, and `LOW_PRIORITY` modifier |
| MU006B | `mysql-replace-delayed-select` | `REPLACE DELAYED INTO ... SELECT ...` | `REPLACE SELECT` target table, source table, positional parameters, and `insert_mode=replace_select` |
| MU006C | `mysql-replace-set` | `REPLACE INTO ... SET ...` | emits `insert_mode=replace_set`; deparsing the unmodified handle reproduces the original `SET` form byte for byte |
| MU006D | `mysql-replace-without-into` | `REPLACE table ... VALUES ...` | deparsing the unmodified handle reproduces the original form without `INTO` byte for byte |
| MU006E | `mysql-replace-table-source` | `REPLACE INTO ... TABLE source` | preserves the source table; deparsing the unmodified handle reproduces the original `TABLE` form byte for byte |

## Multi-Target Multi-Table UPDATE Regression

MySQL multi-table `UPDATE` accepts multiple write targets across JOIN chains and comma-separated relation lists. Each assignment target field identifies its own relation. `ORDER BY` and `LIMIT` are rejected for this multi-table form.

| Case ID | Case | SQL Shape | Validation Focus |
| --- | --- | --- | --- |
| M254 | `mysql-update-multiple-target-inner-join` | two-table `INNER JOIN` with interleaved assignments to both targets | assignment relation attribution and insert, replace, and delete patches |
| M255 | `mysql-update-multiple-target-three-table-bind-order` | three joined tables, three assignment targets, and 12 `?` occurrences | ON/SET/WHERE occurrence order and renumbering after patches |
| M256 | `mysql-update-multiple-target-four-relation-comma-list` | four comma-separated relations with three assignment targets | comma-list relations, target-field attribution, and assignment patches |
| M257 | `mysql-update-multiple-target-four-table-mixed-join` | four-table INNER/LEFT JOIN chain with four assignment targets | JOIN-chain restoration, per-assignment relations, and tail replacement |
| M258 | `mysql-update-multiple-target-quoted-identifiers` | schema-qualified backtick objects with two assignment targets | quoted relation/field attribution and relation and assignment patches |

## Query Graph Quoted-Alias Contract

`relations[].alias_quoted_identifier` is `true` only when the relation alias is backtick delimited. `targets[].output_quoted_identifier` is `true` when the output name comes from an explicit backtick-delimited alias, or inherits a backtick-delimited field name without an explicit alias; View JSON omits either key when its value is `false`.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| M259 | `mysql-quoted-alias-output-flags` | backtick-delimited relation/derived aliases and output names | both quoted flags, field-name inheritance, and two output-alias patches |

## Independent MERGE INSERT Column and Value Mutation

This is a patch contract of the project's current MySQL compatibility entry; it
does not claim official MySQL server support for `MERGE` syntax.

| Case ID | Case Name | Form | Verification Focus |
| --- | --- | --- | --- |
| M260 | `mysql-merge-omitted-insert-column-value-independent` | `WHEN NOT MATCHED THEN INSERT VALUES (...)` with no target-column list | an omitted list still emits `target_list_selector`; three independent patches verify column-only list materialization, value-only cell insertion, and replacement of an existing `merge_insert_cell`; together with the existing paired mode, this covers the three-state `insert_column` contract |

## Query Graph Segmented Quoted-Identifier Contract

Relation qualification records backtick delimiter state per segment through
`database_quoted_identifier`, `schema_quoted_identifier`, the existing object
`quoted_identifier`, and `link_quoted_identifier` when a database link exists.
DML target columns use `dml_column.quoted_identifier`. Each flag describes only
its corresponding name segment; View JSON omits the key for an unquoted or
absent segment, so case cannot be inferred from identifier spelling. The MySQL
entry has no database-link form, so `link_quoted_identifier` is not applicable.
The `MERGE` form in this section validates only the project's compatibility
entry and is not an official MySQL server syntax claim. Public C-structure
lifecycle coverage is maintained in `tests/unit/test_identifier_spelling.c`.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| M261 | `mysql-quoted-identifier-segment-and-dml-column-inventory` | seven statements covering three-part relations, ordinary INSERT, UPDATE, DELETE, compatibility-entry MERGE INSERT, MySQL `INSERT ... SET`, and `REPLACE ... SET` | quoted/unquoted same-name contrasts for `database_quoted_identifier`, `schema_quoted_identifier`, and ordinary/branch/SET `dml_column.quoted_identifier`; five independent patches verify recomputation after relation replacement, MERGE-column replacement, paired insertion, and INSERT SET column insertion |

## DDL Query Graph Relation Contract

This section records only forms defined by MySQL's official syntax and verified by the current entry. DDL targets and references enter a root block with `kind = "ddl"`; a query-backed DDL target points through `source_block` to a SELECT block. Multi-object DROP targets have no relation selector. Behavior not listed here must not be inferred for compatibility entries; their own fixtures are authoritative.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| M262 | `mysql-ddl-relation-direct-inventory` | CREATE/ALTER TABLE, INDEX, DROP/TRUNCATE, and RENAME, including FK and multi-object DROP | target/reference roles, segmented backtick flags, the no-selector DROP boundary, and six relation patches |
| M263 | `mysql-ddl-relation-query-backed-inventory` | CREATE VIEW and CTAS | separate DDL-target and SELECT-source blocks, target `source_block`, and two target/source relation patches |
| M264 | `mysql-ddl-drop-table-quoted-same-spelling` | multi-object DROP TABLE with identically spelled quoted/unquoted names, including a statement-level executable comment | selector-free DROP targets whose schema/object backtick states come from their exact source tokens |

## Explicit CTE Column Ordinal Mapping

The following four final cases verify Query Graph ordinal mapping for explicit CTE column names. A list whose name count differs from the CTE result width is not a positive contract.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| M265 | `mysql-cte-explicit-columns-ordinal` | `cte(a,b)` over inner targets `x,y` | exposed CTE targets are renamed by ordinal to `a,b`; two source-target patches do not change the explicit names |
| M266 | `mysql-cte-explicit-columns-quoted-repeated` | a backtick-delimited column name and two CTE references | `output_quoted_identifier`, shared `source_block`, ON/output field ownership, and ordinal mapping after a patch |
| M267 | `mysql-cte-explicit-columns-dml-lineage` | a CTE field drives a MySQL UPDATE assignment | assignment `source_field` remains the outer CTE field while `source_target` resolves explicit column ordinal 1; one patch verifies recomputation |
| M268 | `mysql-cte-explicit-columns-recursive-star-boundary` | recursive `UNION ALL` plus a `SELECT *` source | SET/recursive branch targets retain underlying names rather than impersonating CTE aliases; an unexpanded star invents no source targets |

## INSERT VALUES Regression: Mixed Binds and Expressions

These ten cases cover SQL templates used with prepared statements or generated by drivers. An unquoted `?` is a data-value parameter marker only; it cannot replace a table name, column name, or keyword. Execution of SQL containing these markers requires the prepare/bind flow.

`DEFAULT` appears only as a standalone VALUES cell. An expression cell remains `kind=expression` and is not classified as `kind=bind`. A nested `?` still consumes its global bind ordinal, which is verified by the `bind_position` of following direct-bind cells. Direct-bind cells assert `bind_key`, `bind_kind`, `bind_sql`, global `bind_position`, and `selector`; time-function and UUID function names must not appear in `query_graph.fields[].column`.

| Case ID | Case Name | VALUES Shape | Validation Focus |
| --- | --- | --- | --- |
| `MY-BM001` | `mysql-insert-bind-mixed-bare-time` | three direct `?` values plus `NOW()` | direct-bind key, kind, SQL, position, selector, and the trailing time expression |
| `MY-BM002` | `mysql-insert-bind-mixed-expression-first` | `CAST(? AS SIGNED)`, `?`, `CURRENT_TIMESTAMP`, `?` | leading expression, nested-bind global ordinal, and following direct binds |
| `MY-BM003` | `mysql-insert-bind-mixed-interleaved-functions` | `?`, `UUID()`, `?`, `COALESCE(?, 'fallback')`, `?` | interleaved binds and expressions, `UUID` absent from `query_graph.fields[].column`, and nested-bind counting |
| `MY-BM004` | `mysql-insert-bind-mixed-null` | `?`, `NULL`, `CAST(? AS SIGNED)`, `?` | independent NULL literal, expression, and direct-bind cell kinds |
| `MY-BM005` | `mysql-insert-bind-mixed-default` | `?`, `DEFAULT`, `CAST(? AS SIGNED)`, `?` | standalone DEFAULT cell and nested-bind global ordinal |
| `MY-BM006` | `mysql-insert-bind-mixed-literal` | `?`, string literal, a `CASE` expression containing `?`, `?` | string literal, CASE expression, and following bind position |
| `MY-BM007` | `mysql-insert-bind-mixed-coalesce-time` | `?`, `COALESCE(?, 'fallback')`, `CURRENT_TIMESTAMP`, `?` | COALESCE bind, independent time expression, and direct bind |
| `MY-BM008` | `mysql-insert-bind-mixed-case-time` | `?`, a `CASE` expression containing `?`, `NOW()`, `?` | bind within a CASE expression, independent time expression, and direct bind |
| `MY-BM009` | `mysql-insert-bind-mixed-three-rows` | three rows mixing binds, CAST/COALESCE/CASE expressions, and time expressions | all nine anonymous occurrences remain separate; every cell selector, continuous cross-row positions, and the eight-item renumbering after deleting the head or tail bind are exact |
| `MY-BM010` | `mysql-insert-bind-mixed-quoted-irregular-whitespace` | schema-qualified backtick identifiers, irregular whitespace, three direct `?` values, and a time expression | quoted identifiers, byte-exact source preservation, and field-for-field equality with expected cell objects |

## Coverage Boundary

This matrix lists only cases that parse successfully and have final View and
patch expectations. Syntax boundaries outside this executable fixture are
maintained in `doc/mysql_official_syntax_coverage.csv`.

## Rules

- The default dialect is `SQLPARSER_DIALECT_POSTGRESQL`.
- MySQL statements must be parsed through `sqlparser_parse_with_options` with `SQLPARSER_DIALECT_MYSQL`.
- Safely mappable syntax is handled in dialect preprocess / postprocess.
- New MySQL support must update `tests/cases/mysql_dialect_input.json`, this matrix, and executable regression tests.
- Syntax outside the executable fixture must not be listed here as a validated case.
