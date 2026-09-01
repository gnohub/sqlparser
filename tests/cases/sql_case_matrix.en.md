# SQL Case Matrix

This file records the regression cases covered by `tests/cases/sql_batch_input.json`. For every final case, the runner requires unchanged SQL to deparse byte for byte, compares the actual View with the expected JSON structure, and executes each patch independently. Patched SQL must match `patch.deparse` byte for byte, remain identical after a fresh parse and second deparse, and produce the same View from the patched and freshly parsed handles. When a case provides `bind_occurrences`, the runner also compares `position`, `kind`, `key`, and original `sql` item by item for the source SQL and every patched SQL; repeated keys remain separate and `position` is continuous across the full SQL text.

## Matrix Counts and Session Regression

The fixture contains 228 cases with `status = "final"` and 760 independent
patches. Two cases and their 10 patches contain complete bind-occurrence
assertions. Expected View JSON contains
statement-level `query_graph.session` output in 32 cases: 5 schema/session
cases and `PG-001` through `PG-027`. All 32 contain at least one non-empty
session projection.

View validation compares JSON structures; object-key order and formatting
whitespace do not participate. Session action, item scope, target kind, name,
and value fields are all part of that comparison.

## Executable Entry Points

- API smoke test: `tests/unit/test_api_smoke.c`
- API matrix test: `tests/unit/test_api_case_matrix.c`
- CLI batch fixture: `tests/cases/sql_batch_input.json`

## Validated Statement Shapes

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| P001 | `select-basic` | `SELECT 1` | parse, View JSON, deparse |
| P002 | `select-filter` | `SELECT ... FROM ... WHERE ...` | selected columns, filter columns, table extraction |
| P003 | `select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | multi-table join, selected columns, join columns, where columns |
| P004 | `select-cte` | `WITH ... SELECT ...` | CTE name, outer selected columns, upstream filter columns |
| P005 | `insert-single-row` | `INSERT ... VALUES (...)` | insert columns, single-row insert, deparse |
| P006 | `insert-multi-row` | `INSERT ... VALUES (...), (...)` | multi-row insert, insert columns, deparse |
| P007 | `insert-from-select` | `INSERT ... SELECT ... FROM ... WHERE ...` | insert columns, inner SELECT, WHERE extraction |
| P008 | `update-basic` | `UPDATE ... SET ... WHERE ...` | updated columns, where columns, table extraction |
| P009 | `delete-conditional` | `DELETE ... WHERE ... AND ...` | conditional delete and multi-condition column extraction |
| P010 | `delete-in-list` | `DELETE ... WHERE ... IN (...)` | delete predicate extraction with `IN` |
| P011 | `drop-table` | `DROP TABLE ...` | DDL classification, table extraction, deparse |
| P012 | `drop-view` | `DROP VIEW ...` | view DDL classification and object extraction |
| P013 | `create-view` | `CREATE VIEW ... AS SELECT ...` | view definition and inner SELECT extraction |
| P014 | `truncate-table` | `TRUNCATE TABLE ...` | truncate node recognition and deparse |
| P015 | `comment-table` | `COMMENT ON TABLE ... IS ...` | comment node recognition and deparse |
| P016 | `rename-table` | `ALTER TABLE ... RENAME TO ...` | rename node recognition and object-name rewrite basis |
| P017 | `alter-table-add-column` | `ALTER TABLE ... ADD COLUMN ...` | alter-table node recognition and column-definition deparse |
| P018 | `create-index` | `CREATE INDEX ... ON ... (...)` | index node recognition and deparse |
| P019 | `drop-index` | `DROP INDEX ...` | drop-index node recognition and deparse |
| P020 | `explain-select` | `EXPLAIN SELECT ...` | parsing and deparsing an explained query |
| P021 | `copy-table` | `COPY ... FROM STDIN` | copy node recognition and column-list deparse |
| P022 | `lock-table` | `LOCK TABLE ... IN ... MODE` | lock node recognition and deparse |
| P023 | `call-procedure` | `CALL ...()` | call node recognition and deparse |
| P024 | `do-block` | `DO $$ ... $$` | DO block parse and deparse |
| P025 | `create-table-as` | `CREATE TABLE ... AS SELECT ...` | CTAS node recognition and inner query parsing |
| P026 | `transaction-begin-commit` | `BEGIN; COMMIT;` | multi-statement transaction counting and keyword extraction |
| P027 | `transaction-begin-insert-rollback` | `BEGIN; INSERT ...; ROLLBACK;` | mixed transaction and DML parsing |
| P028 | `multi-statement-mixed` | `SELECT ...; INSERT ...` | multi-statement counting and mixed-statement deparse |
| P029 | `quoted-identifiers` | `SELECT "..."."..." FROM "..."` | quoted identifier preservation and name extraction |
| P030 | `literal-semicolon` | `SELECT ';' AS ...` | semicolon handling inside string literals |
| P031 | `select-subquery-exists` | `SELECT ..., EXISTS (SELECT ...) FROM ...` | subquery, `EXISTS`, and multi-table extraction |
| P032 | `select-case-window` | `SELECT CASE ... OVER (...) FROM ...` | `CASE`, window functions, sort/partition column extraction |
| P033 | `select-union-order-limit` | `SELECT ... UNION ALL SELECT ... ORDER BY ... LIMIT ...` | `UNION ALL`, ordering, and limit deparse |
| P034 | `insert-on-conflict-update` | `INSERT ... ON CONFLICT ... DO UPDATE ... RETURNING ...` | conflict handling, returning columns, insert columns |
| P035 | `insert-returning` | `INSERT ... RETURNING ...` | returning columns and insert columns; replacing a returning target with `$1 AS echoed` changes the exact occurrence count from zero to one |
| P036 | `update-from-returning` | `UPDATE ... SET ... FROM ... WHERE ... RETURNING ...` | `UPDATE ... FROM`, returning columns, where columns |
| P037 | `delete-using-returning` | `DELETE ... USING ... WHERE ... RETURNING ...` | `DELETE ... USING`, returning columns, multi-table extraction |
| P038 | `merge-basic` | `MERGE INTO ... USING ... WHEN ...` | merge node recognition and keyword coverage |
| P039 | `savepoint-release` | `BEGIN; SAVEPOINT ...; RELEASE ...; COMMIT;` | savepoint transaction parsing |
| P040 | `rollback-to-savepoint` | `BEGIN; SAVEPOINT ...; INSERT ...; ROLLBACK TO ...; COMMIT;` | savepoint and DML mixed parsing |
| P041 | `create-materialized-view` | `CREATE MATERIALIZED VIEW ... AS SELECT ...` | materialized view and inner SELECT extraction |
| P042 | `alter-table-drop-column` | `ALTER TABLE ... DROP COLUMN ...` | drop-column node recognition and deparse |
| P043 | `create-schema` | `CREATE SCHEMA ...` | schema DDL classification and deparse |
| P044 | `drop-schema` | `DROP SCHEMA ...` | schema drop classification and deparse |
| P045 | `grant-select` | `GRANT SELECT ON TABLE ... TO ...` | grant node recognition and object extraction |
| P046 | `revoke-select` | `REVOKE SELECT ON TABLE ... FROM ...` | revoke node recognition and object extraction |
| P047 | `analyze-table` | `ANALYZE ...` | analyze node recognition and table extraction |
| P048 | `vacuum-analyze-table` | `VACUUM ANALYZE ...` | combined vacuum/analyze node recognition |
| P049 | `postgresql-set-search-path` | `SET search_path TO ...` | session schema search-path output and value selector |
| P050 | `postgresql-set-schema` | `SET SCHEMA ...` | `SET SCHEMA` alias deparses as `search_path` |
| P051 | `postgresql-set-local-search-path` | `SET LOCAL search_path = ...` | local transaction-level schema search path |
| P052 | `postgresql-prepare-select` | `PREPARE ... AS SELECT ... $1` | PostgreSQL SQL-level prepared statement, parameters, and query-object extraction |
| P053 | `postgresql-execute-prepared` | `EXECUTE ...(...)` | PostgreSQL prepared statement execution |
| P054 | `postgresql-deallocate-prepare` | `DEALLOCATE PREPARE ...` | PostgreSQL prepared statement deallocation |
| P055 | `oracle-cli-dialect-q-quote` | Oracle `q'[...]'` | CLI `dialect` field and Oracle q-quoted string handling |
| P056 | `sqlserver-cli-dialect-top-param` | SQL Server `TOP` + `@` parameter | CLI `dialect` field and SQL Server dialect output handling |
| P057 | `dameng-cli-dialect-set-schema-top` | Dameng `SET SCHEMA` + `TOP` + bind | CLI `dialect` field and Dameng dialect output handling |
| P058 | `postgresql-select-dollar-params` | `SELECT ... WHERE ... = $1` | PostgreSQL `$n` parameters in query predicates, View output, and deparse |
| P059 | `postgresql-select-in-dollar-params` | `SELECT ... IN ($1, $2, $3)` | multiple `$n` parameters in `IN` predicates |
| P059A | `postgresql-select-between-dollar-params` | `BETWEEN $1 AND $2` | multiple `$n` parameters and field-value attribution in `BETWEEN` predicates |
| P059B | `postgresql-select-not-in-dollar-params` | `NOT IN ($1, $2)` | multiple `$n` parameters and field-value attribution in negated `IN` predicates |
| P059C | `postgresql-select-not-between-dollar-params` | `NOT BETWEEN $1 AND $2` | multiple `$n` parameters and field-value attribution in negated `BETWEEN` predicates |
| P060 | `postgresql-select-limit-dollar-params` | `LIMIT $2 OFFSET $3` | `$n` parameters in pagination clauses |
| P061 | `postgresql-insert-dollar-params` | `INSERT ... VALUES ($1, $2, $3)` | insert columns and `$n` parameter value lists |
| P062 | `postgresql-insert-multi-row-dollar-params` | multi-row `INSERT ... VALUES` + `$n` | multi-row parameterized insert |
| P063 | `postgresql-update-dollar-params` | `UPDATE ... SET ... WHERE ... = $n` | updated columns, predicate columns, and `$n` parameters |
| P064 | `postgresql-delete-dollar-params` | `DELETE ... WHERE ... = $n` | conditional delete and `$n` parameters |
| P065 | `postgresql-prepare-insert` | `PREPARE ... AS INSERT ...` | prepared insert statement and parameterized value list |
| P066 | `postgresql-prepare-update` | `PREPARE ... AS UPDATE ...` | prepared update statement and predicate parameters |
| P067 | `postgresql-prepare-delete` | `PREPARE ... AS DELETE ...` | prepared delete statement and predicate parameters |
| P068 | `postgresql-execute-prepared-with-args` | `EXECUTE ...(...)` | prepared statement execution arguments |
| P069 | `postgresql-deallocate-all` | `DEALLOCATE ALL` | deallocating all prepared statements |
| P070 | `postgresql-view-direct-column` | `SELECT name FROM ...` | direct SELECT output column, `query_graph` target, and empty `target_path` |
| P071 | `postgresql-view-star-qualified-star` | `SELECT *, alias.* FROM ...` | unqualified star, qualified star, and output-item ownership |
| P072 | `postgresql-view-functions-and-args` | `SELECT function(column, ...) FROM ...` | function `target_path`, function name, and argument index |
| P073 | `postgresql-view-expressions-and-case` | `SELECT expression, CASE ... FROM ...` | expression `target_path`, operator name, and `CASE` ownership |
| P074 | `postgresql-view-group-having-order` | `GROUP BY ... HAVING ... ORDER BY ...` | non-output clause fields with `query_graph` clause and empty `target_path` |
| P075 | `postgresql-view-distinct-nested-functions` | `SELECT DISTINCT LOW(UPPER(...)) FROM ...` | `DISTINCT` keyword and outer-to-inner nested function `target_path` |
| P076 | `postgresql-view-join-on` | `JOIN ... ON ... WHERE ...` | JOIN/ON fields, WHERE binds, and table-column attribution |
| P077 | `postgresql-view-window-array-row-tests` | window, array, ROW, boolean/NULL expressions | `target_path` for window functions, compound expressions, and read-only clauses |
| P078 | `postgresql-view-bind-values` | `UPDATE ... SET ... WHERE ... = $n` | PostgreSQL bind fields, null values, and update/where clause ownership |
| P079 | `postgresql-view-not-like-bind` | `NOT LIKE $n` | field-level operator, keyword, and bind attribution for negated LIKE |
| P080 | `postgresql-view-not-ilike-bind` | `NOT ILIKE $n` | field-level operator, keyword, and bind attribution for negated ILIKE |
| P081 | `postgresql-view-not-similar-bind` | `NOT SIMILAR TO $n` | field-level operator, keyword, and bind attribution for negated SIMILAR TO |
| P082 | `postgresql-create-table-if-not-exists-types` | `CREATE TABLE IF NOT EXISTS ...` | conditional table creation, common data types, and table extraction |
| P083 | `postgresql-insert-without-column-list` | `INSERT INTO ... VALUES ($1, $2, $3)` | columnless insert, row cells, positional binds, and null column names |
| P084 | `postgresql-update-in-not-in-conditions` | `UPDATE ... SET ... WHERE ... IN ... NOT IN ...` | SET bind, collection predicates, and negated collection predicates |
| P085 | `postgresql-select-rich-where` | `IS NOT NULL` + `BETWEEN` + `LIKE` | complex WHERE predicates, range parameters, and pattern-match parameters |
| P086 | `postgresql-select-derived-table-filter` | derived table + outer filter | derived-table fields, inner/outer WHERE clauses, and bind attribution |
| P087 | `postgresql-select-scalar-subquery` | scalar subquery in SELECT output | projection subquery, correlated fields, and outer WHERE bind |
| P088 | `postgresql-select-intersect` | `INTERSECT` | set operation, both input tables, and output columns |
| P089 | `postgresql-create-view-join-aggregate` | aggregate JOIN view | view definition, JOIN predicates, and GROUP BY aggregation |
| P090 | `postgresql-select-order-by-ordinal` | `ORDER BY 1` | ordinal sort item and projection-order related syntax |
| P091 | `postgresql-select-quoted-mixed-identifiers` | quoted mixed-case / spaced identifiers | special identifiers, selected columns, and WHERE bind |
| P092 | `postgresql-dollar-quoted-string-global-bind-position` | dollar-quoted string plus `$n` parameters | placeholder-like text inside dollar-quoted strings is excluded from global bind counting |
| P093 | `postgresql-multi-statement-global-bind-position` | `$n` across multiple `UPDATE` statements, `MERGE`, and `CALL` | source-order occurrences retain duplicate `$4`/`$7` keys and exclude comment text `$90`; a complex patch covers a subquery, CAST, CASE, `LIMIT/OFFSET`, JSONB operators, protected regions, and exact renumbering after deletion/insertion |
| P094 | `postgresql-select-nested-derived-query-graph` | nested derived tables with output alias | `query_graph` lineage mapping from derived-table fields to inner base-table fields and `output_name` |
| P095 | `postgresql-select-reference-001` | SELECT reference case 001 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P096 | `postgresql-select-reference-004` | SELECT reference case 004 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P097 | `postgresql-select-reference-005` | SELECT reference case 005 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P098 | `postgresql-select-reference-007` | SELECT reference case 007 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P099 | `postgresql-select-reference-009` | SELECT reference case 009 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P100 | `postgresql-select-reference-011` | SELECT reference case 011 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P101 | `postgresql-select-reference-013` | SELECT reference case 013 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P102 | `postgresql-select-reference-015` | SELECT reference case 015 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P103 | `postgresql-select-reference-017` | SELECT reference case 017 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P104 | `postgresql-select-reference-018` | SELECT reference case 018 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P105 | `postgresql-select-reference-019` | SELECT reference case 019 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P106 | `postgresql-select-reference-020` | SELECT reference case 020 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P107 | `postgresql-select-reference-021` | SELECT reference case 021 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P108 | `postgresql-select-reference-030` | SELECT reference case 030 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P109 | `postgresql-select-reference-031` | SELECT reference case 031 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P110 | `postgresql-select-reference-032` | SELECT reference case 032 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P111 | `postgresql-select-reference-034` | SELECT reference case 034 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P112 | `postgresql-select-reference-035` | SELECT reference case 035 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P113 | `postgresql-select-reference-036` | SELECT reference case 036 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P114 | `postgresql-select-reference-037` | SELECT reference case 037 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P115 | `postgresql-select-reference-038` | SELECT reference case 038 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P116 | `postgresql-select-reference-039` | SELECT reference case 039 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P117 | `postgresql-select-reference-040` | SELECT reference case 040 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P118 | `postgresql-select-reference-041` | SELECT reference case 041 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P119 | `postgresql-select-reference-042` | SELECT reference case 042 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P120 | `postgresql-select-reference-043` | SELECT reference case 043 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P121 | `postgresql-select-reference-046` | SELECT reference case 046 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P122 | `postgresql-select-reference-047` | SELECT reference case 047 | Standard SELECT/subquery/JOIN/set-query parsing and View JSON shape from the reference document |
| P123 | `postgresql-select-nested-join-derived-query-graph` | derived table inside nested JOIN | Derived-object enumeration, `query_graph` lineage, and `output_name` under complex FROM/JOIN nesting |
| P124 | `postgresql-select-unqualified-multi-table-scope` | unqualified fields in a multi-table scope | Unqualified fields are reported once under a `statement` object, avoiding the same selector under multiple tables |
| P125 | `postgresql-select-union-derived-scope` | derived tables on both sides of UNION with `SELECT *` | Unique occurrence output for derived fields, with `query_graph` lineage pointing to the matching inner `*` source |
| P126 | `postgresql-field-match-kind-direct-and-expression` | direct-field predicate plus function-wrapped field predicate | `query_graph.values[].field_match_kind` distinguishes `direct_field` from `expression_field` |
| P127 | `postgresql-expression-field-case-expression-value` | CASE returns a field and compares with a bind | CASE expression fields emit `expression_field` value relations |
| P128 | `postgresql-expression-field-multi-field-expression-value` | multi-field expression compared with binds | Fields inside the expression keep separate `expression_field` value relations |
| P129 | `postgresql-expression-field-value-side-expression` | field compared with value-side expressions | function, operator, and CAST value-side expressions emit `kind=expression` instead of direct binds |
| P130 | `postgresql-expression-field-dml-expression-values` | INSERT/UPDATE expression assignments | DML cells and assignments emit `kind=expression` |
| P131 | `postgresql-update-bind-rhs-crypto-source` | `UPDATE ... SET protected = $n` | protected-field UPDATE SET right-hand binds for later structured backup assignment insertion and literal rewrite |
| P132 | `postgresql-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = $n, protected2 = $n` | multiple protected-field SET binds, field attribution, and global bind positions |
| P133 | `postgresql-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | explicit literal ESCAPE is emitted in `values[].like_escape` |
| P134 | `postgresql-not-like-escape-bind` | `NOT LIKE $1 ESCAPE $2` | pattern bind and escape bind remain separate; escape bind keeps its global position |
| P135 | `postgresql-ilike-escape-bind` | `ILIKE $1 ESCAPE $2` | structured ESCAPE output for PostgreSQL `ILIKE` |
| P136 | `postgresql-like-without-explicit-escape` | `LIKE $1` | `like_escape` is omitted when ESCAPE is not explicit |
| P138 | `postgresql-update-from-source-field-graph` | `UPDATE ... SET target = source.column FROM ...` | `source_field` for base-table source fields on the right side of UPDATE FROM assignments, plus `right_field` for WHERE field comparisons |
| P139 | `postgresql-insert-select-source-block-graph` | `INSERT ... SELECT ... FROM ...` | target columns, source block, and source-field lineage for INSERT SELECT |
| P140 | `postgresql-merge-source-target-graph` | `MERGE INTO ... USING ...` | source/target field lineage and source-field expressions for MERGE |
| P141 | `postgresql-listen-notify-unlisten` | `LISTEN` / `NOTIFY` / `UNLISTEN` | PostgreSQL notification statements parse, deparse, and emit an empty query graph |
| P142 | `postgresql-create-drop-extension` | `CREATE EXTENSION` / `DROP EXTENSION` | extension-object DDL parsing, deparsing, and utility-statement output |
| P143 | `postgresql-regexp-like-function-predicate` | `regexp_like(name, $1)` | function predicates reuse `fields/values/predicates` for fields, binds, and expression predicates |
| P144 | `postgresql-select-alias-order-by-lineage` | `SELECT u.email AS e ... ORDER BY u.email` | SELECT output aliases keep base-field lineage, while ORDER BY fields stay independently attributed |
| P145 | `postgresql-select-or-predicate-order-by-lineage` | `WHERE field = $n OR field = $n ORDER BY ...` | OR predicate trees keep both comparison children, binds, and independent ORDER BY field attribution |
| P146 | `postgresql-national-string-literal` | `SELECT ..., N'...' ... WHERE ... = n'...'` | PostgreSQL national string literals keep the public `N` prefix while ordinary strings remain unchanged |
| P147 | `postgresql-national-string-duplicate-literal` | ordinary `'same'` and `N'same'` together | same-text ordinary and national strings are restored independently by literal ordinal |
| P148 | `postgresql-merge-multiple-conditional-insert-branches` | two conditional `WHEN NOT MATCHED ... INSERT` actions | MERGE branch order, exact condition text, branch columns/row coordinates, field/bind/expression cells, and global bind positions |
| P149 | `postgresql-merge-by-source-and-omitted-insert-columns` | BY TARGET INSERT, conditional MATCHED UPDATE, and BY SOURCE DELETE | all three MERGE action/match pairs, absolute branch ordinals, INSERT rows without a target column list, and exact branch payloads |
| P150 | `postgresql-direct-field-coalesce-expression-value` | field compared with `COALESCE($1, 'x')` | the predicate references one `direct_field` expression value while nested binds and literals remain unpromoted |
| P151 | `postgresql-direct-field-case-expression-value` | field compared with a parameterized CASE expression | the whole CASE is one `direct_field` expression value referenced by the predicate while nested binds and literals remain unpromoted |
| P152 | `postgresql-direct-field-array-expression-value` | field compared with `ARRAY[$1, $2]` | the ARRAY constructor is one `direct_field` expression value referenced by the predicate while nested binds remain unpromoted |
| P153 | `postgresql-having-or-count-expression-predicates` | `HAVING COUNT(*) > $1 OR COUNT(id) >= 2` | HAVING preserves OR-child order; `COUNT(*)` references a standalone bind without producing a field, while `COUNT(id)` references its expression field and literal; all 3 patches deparse exactly |
| P154 | `postgresql-not-in-subquery-membership` | `field NOT IN (SELECT ... WHERE ... = $1)` | preserves the outer `NOT` node and its `IN` membership child while keeping the inner block, fields, bind, and predicate independently attributed; 6 patches cover both field levels, the relation, target replacement/insertion, and bind |
| P155 | `postgresql-having-function-null-test` | `HAVING SUM(amount) IS NOT NULL AND COUNT(*) > $1` | preserves HAVING AND-child order; the compound-function null test emits an operator-only expression predicate without selecting an internal field or creating a NULL value, while the COUNT bind remains independent; all 4 patches are verified exactly |
| P156 | `postgresql-select-target-fragment-splice-first` | replaces the first item of a three-target SELECT with two quoted targets | splices the multi-target replacement at the selected position while preserving following-target order and WHERE field/bind attribution; an independent insert patch validates list positioning |
| P157 | `postgresql-relation-patch-qualified-shadowed-correlation` | an unaliased schema-qualified outer relation with a same-named inner alias | View attributes the correlated three-part field to the outer relation; the relation patch updates the outer qualified star, direct field, and correlated field while preserving the inner alias and its field |
| P158 | `postgresql-relation-patch-two-part-window-qualifiers` | one relation referenced across SELECT, window, GROUP BY, HAVING, and ORDER BY | replacing a one-part relation with a quoted two-part path makes every bound qualifier use the new path tail at its original depth; independent field and target-insertion patches are also verified |
| P159 | `postgresql-data-modifying-cte-delete-multi-reference` | a DELETE CTE whose `RETURNING` result is referenced twice by an outer JOIN | one DELETE DML root, one result block, and two CTE relations sharing its `source_block`; 2 independent patches cover result-target replacement and insertion |
| P160 | `postgresql-data-modifying-cte-update-delete-root` | an UPDATE CTE with a top-level DELETE | the top-level DELETE is D0 and the UPDATE CTE is its D1 child; the UPDATE result block feeds the DELETE `USING` relation, and 2 independent patches verify the D1 result list exactly |
| P161 | `postgresql-data-modifying-cte-two-deletes-side-effect` | a DELETE CTE without `RETURNING` beside a DELETE CTE with `RETURNING` | the two SELECT-root DMLs remain independent roots in declaration order; side-effect-only D0 has no result while D1 owns one result block; 2 independent patches verify the D1 result list |
| P162 | `postgresql-data-modifying-cte-sibling-lineage` | an INSERT CTE whose `RETURNING` output feeds a sibling UPDATE CTE | D0 INSERT and D1 UPDATE remain independent roots with distinct result blocks; the UPDATE assignment points through `source_field`/`source_target` to the INSERT `payload`; 3 independent patches cover both DML ordinals and result insertion |
| P163 | `postgresql-data-modifying-cte-merge-returning` | a MERGE CTE with UPDATE and INSERT branches plus `RETURNING` | MERGE D0 target/source relations, ON predicate, branch assignment and INSERT row, result block, `target_after` origin for `RETURNING t.*`, and outer CTE `source_block`; 2 independent patches cover result-target replacement and insertion |
| P164 | `postgresql-data-modifying-cte-update-compound-rhs` | a compound assignment RHS in an UPDATE CTE with unaliased relations | `rhs_fields` and `rhs_values` attribute the source field, bind, and literal to the assignment; source and target relation patches propagate through RHS, WHERE, and RETURNING qualifiers while outer-target insertion remains independent |
| P165 | `postgresql-on-conflict-compound-rhs` | a compound assignment RHS in `ON CONFLICT DO UPDATE` | the `EXCLUDED` field, target-table field, bind, and literal belong to one assignment; the target alias remains stable after relation replacement, and RETURNING target insertion is verified exactly |
| P166 | `postgresql-merge-matched-delete-action` | conditional matched DELETE and matched UPDATE actions followed by a not-matched INSERT | DELETE remains an independent `WHEN MATCHED ... THEN DELETE` branch; all three branches retain their absolute order and selectors; 3 independent patches cover the DELETE branch condition, UPDATE assignment, and INSERT cell |
| P167 | `postgresql-on-conflict-assignment-list-contract` | root `INSERT ... ON CONFLICT DO UPDATE SET` with two assignments | ordered `assignment[A]` selectors address conflict-update items; insertion, full replacement, and deletion deparse exactly |
| P168 | `postgresql-data-modifying-cte-update-assignment-list-contract` | nested two-assignment `UPDATE` in a data-modifying CTE | `assignment[D][A]` uses the zero-based DML ordinal; all three assignment patches preserve `RETURNING` and the outer query |

## Query Graph Quoted-Alias Contract

`relations[].alias_quoted_identifier` is `true` only when the relation alias is double-quote delimited. `targets[].output_quoted_identifier` is `true` when the output name comes from an explicit double-quoted alias, or inherits a double-quoted field name without an explicit alias; View JSON omits either key when its value is `false`.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| P169 | `postgresql-quoted-relation-alias-and-target-output-contract` | double-quoted relation/derived aliases and output names | both quoted flags, field-name inheritance, and two output-alias patches |

## Independent MERGE INSERT Column and Value Mutation

| Case ID | Case Name | Form | Verification Focus |
| --- | --- | --- | --- |
| P170 | `postgresql-merge-omitted-insert-column-value-independent` | `WHEN NOT MATCHED THEN INSERT VALUES (...)` with no target-column list | an omitted list still emits `target_list_selector`; three independent patches verify column-only list materialization, value-only cell insertion, and replacement of an existing `merge_insert_cell`; together with the existing paired mode, this covers the three-state `insert_column` contract |

## Query Graph Segmented Quoted-Identifier Contract

Relation qualification records delimiter state per segment through
`database_quoted_identifier`, `schema_quoted_identifier`, the existing object
`quoted_identifier`, and `link_quoted_identifier` when a database link exists.
DML target columns use `dml_column.quoted_identifier`. Each flag describes only
its corresponding name segment; View JSON omits the key for an unquoted or
absent segment, so case cannot be inferred from identifier spelling.
PostgreSQL has no database-link form in this fixture, so
`link_quoted_identifier` is not applicable. Batch, clone, patch-to-fresh-View,
and public C-structure lifecycle coverage is maintained in
`tests/unit/test_identifier_spelling.c`.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| P171 | `postgresql-quoted-identifier-segment-and-dml-column-inventory` | six statements covering three-part relations, ordinary INSERT, UPDATE, DELETE, MERGE INSERT, and `DEFAULT VALUES` | quoted/unquoted same-name contrasts for `database_quoted_identifier`, `schema_quoted_identifier`, and ordinary/branch `dml_column.quoted_identifier`; four independent patches verify per-segment recomputation after relation replacement, MERGE-column replacement, and paired column insertion |

## DDL Query Graph Relation Contract

DDL relations enter a root block with `kind = "ddl"`; `ddl_role = "target"|"reference"` distinguishes the object being changed from constraint, inheritance, template, or partition references. A query-backed DDL target points through `source_block` to a separate SELECT block. Multi-object DROP targets retain segmented quoted flags but currently have no relation selector. Consumers must use the role rather than relation-array order.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| P172 | `postgresql-ddl-relation-direct-inventory` | CREATE/ALTER TABLE, INDEX, DROP/TRUNCATE, and RENAME, including FK, LIKE, INHERITS, and multi-object DROP | DDL block, target/reference roles, complete segmented quoted flags, the no-selector DROP boundary, and eight relation patches |
| P173 | `postgresql-ddl-relation-query-backed-inventory` | CREATE VIEW, CTAS, and CREATE MATERIALIZED VIEW | separate DDL-target and SELECT-source blocks, target `source_block` linkage, and three target/source relation patches |
| P174 | `postgresql-ddl-relation-partition-operations` | ATTACH / DETACH PARTITION | changed table as target, partition table as reference, and three relation patches |
| P175 | `postgresql-ddl-relation-select-into` | `SELECT ... INTO target FROM source` | DDL target block, SELECT source block, `source_block`, and two relation patches |
| P176 | `postgresql-ddl-relation-foreign-table-and-exact-drop-spelling` | CREATE/ALTER/RENAME/DROP FOREIGN TABLE and identically spelled quoted/unquoted DROP targets | foreign-table target lifecycle, one relation patch, the `if` identifier boundary, and exact segment state after a U& identifier with `UESCAPE` |

## Explicit CTE Column-Name Ordinal Contract

The following final cases verify explicit CTE column-name ordinal mapping and its defined boundaries.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| P177 | `postgresql-cte-explicit-column-ordinal-inventory` | ordinary SELECT CTEs covering a full list, a shorter list, quoted/unquoted names, and repeated CTE references | enumerable source targets take explicit CTE names by ordinal; a shorter list overrides only its prefix; quoted state comes from the CTE column-name token; repeated references share one `source_block`; one relation patch |
| P178 | `postgresql-cte-explicit-column-dml-source-target` | a SELECT CTE and a data-modifying UPDATE CTE each feed an outer UPDATE | CTE names override SELECT/RETURNING source targets by ordinal, and both outer assignments use `source_field` plus `source_target = 1` to identify `masked_title`; two relation patches |
| P179 | `postgresql-cte-explicit-column-set-recursive-boundary` | a `UNION ALL` CTE and a recursive `UNION ALL` CTE | a SET result block currently has no directly enumerable result targets; branch targets, set structure, and the CTE `source_block` remain intact without fabricated ordinal targets; one relation patch |
| P180 | `postgresql-cte-explicit-column-star-boundary` | two explicit names wrap one `SELECT *` target | `*` is not expanded and two names are not forced onto one star target; outer fields remain associated with the CTE relation; one relation patch |

## INSERT VALUES Regression: Mixed Binds and Expressions

`PG-BM001` through `PG-BM010` are INSERT inputs in PostgreSQL prepared-statement, extended-query-protocol, or driver-template contexts. Here `$n` denotes an externally supplied positional parameter. The simple Query protocol does not supply these parameters; execution requires a supported prepare/bind flow.

Every case checks `row`, `column`, `kind`, and `selector` for every VALUES cell. Direct-bind cells also check `bind_key`, `bind_kind`, `bind_sql`, and `bind_position`. Under the current public contract a bind nested inside an expression is not attached directly to the cell; the global `bind_position` of a following direct bind verifies that the nested occurrence was counted. Time-function names must not appear in `query_graph.fields[].column`.

| Case ID | VALUES shape | Validation focus |
| --- | --- | --- |
| `PG-BM001` | three direct binds + trailing `CURRENT_TIMESTAMP` | consecutive direct binds and a trailing time expression |
| `PG-BM002` | leading `now()` + three direct binds | expression in the first column |
| `PG-BM003` | `$1`, `CAST($2 AS text)`, `$3`, `clock_timestamp()` | interleaving binds and expressions; nested bind participates in global counting |
| `PG-BM004` | direct bind, `NULL`, `now()`, direct bind | literal and time expression mixed with binds |
| `PG-BM005` | direct bind, standalone `DEFAULT`, `CURRENT_TIMESTAMP`, direct bind | `DEFAULT` only as a standalone VALUES cell |
| `PG-BM006` | direct bind, string literal, `clock_timestamp()`, direct bind | literal, expression, and binds together |
| `PG-BM007` | `$1`, `COALESCE($2, 'fallback')`, `CURRENT_TIMESTAMP`, `$3` | nested bind and the following global position |
| `PG-BM008` | `$1`, a `CASE` expression containing `$2`, `now()`, `$3` | CASE-nested bind and the following global position |
| `PG-BM009` | three VALUES rows with changing bind/expression positions | cross-row cell coordinates and continuous global bind positions |
| `PG-BM010` | schema-qualified quoted identifiers, irregular whitespace, three direct binds + time expression | quoted identifiers, original whitespace, and the trailing expression |

## Dialect CLI Cases

| Case ID | Case Name | Input | Validation Focus |
| --- | --- | --- | --- |
| VCLI001 | `vastbase-oracle-cli-current-schema` | `ALTER SESSION SET CURRENT_SCHEMA=APP` | `vastbase-oracle` CLI dialect name, `ALTER SESSION` View JSON, and deparse |
| VCLI002 | `vastbase-mysql-cli-limit-binds` | ``SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ?`` | `vastbase-mysql` CLI dialect name, backtick identifiers, and comma LIMIT binds |
| VCLI003 | `vastbase-postgresql-cli-positional-binds` | `SELECT id FROM public.users WHERE id = $1` | `vastbase-postgresql` CLI dialect name and PostgreSQL positional bind |
| VCLI004 | `vastbase-sqlserver-cli-top-bind` | `SELECT TOP (5) [id] FROM [dbo].[users] WHERE [id] = @id` | `vastbase-sqlserver` CLI dialect name, bracket identifiers, `TOP`, and named bind |

## Coverage Boundary

This matrix lists only cases that parse successfully and have final View and
patch expectations. Parse-failure paths are maintained by separate unit tests
and are not listed in this fixture.

New regression cases must update both `tests/cases/sql_batch_input.json` and this matrix.

## Predicate RHS expressions

These cases cover `query_graph.expressions[]` and argument-level patching. Predicates link the RHS through `right_expression`; `values[]` retains existing entries but does not duplicate an expression index, and no placeholder value is synthesized when the RHS had no existing value.

| Case ID | Case name | Verification focus |
| --- | --- | --- |
| `P181` | `postgresql-predicate-expression-like-concat-mixed-args` | LIKE RHS root with literal, bind, field, and nested-function arguments |
| `P182` | `postgresql-predicate-expression-on-having-functions` | Ordered ON/HAVING roots, independent replacement, and ownership |
| `P183` | `postgresql-predicate-expression-nested-and-opaque-args` | Nested function and operator/ARRAY/CASE opaque boundaries |
| `P184` | `postgresql-predicate-expression-variadic-argument-mutations` | Zero/one/two arguments, replacement, head/middle/tail insertion, and deletion to zero |
| `P185` | `postgresql-predicate-expression-comment-surface-bind-renumber` | Comment surface, bind renumbering, and fresh View after patch |
| `P186` | `postgresql-predicate-expression-reverse-rhs-bind-field` | left bind value coexists with a right field/function; the predicate retains `value`/`right_field` and adds `right_expression` |
