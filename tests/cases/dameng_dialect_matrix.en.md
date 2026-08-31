# Dameng Dialect Case Matrix

This file records regression cases for the Dameng dialect conversion layer. The executable fixture is `tests/cases/dameng_dialect_input.json`. For every final case, the runner requires unchanged SQL to deparse byte for byte, compares the actual View with the expected JSON structure, and executes each patch independently. Patched SQL must match `patch.deparse` byte for byte, remain identical after a fresh parse and second deparse, and produce the same View from the patched and freshly parsed handles. When a case provides `bind_occurrences`, the runner also compares `position`, `kind`, `key`, and original `sql` item by item for the source SQL and every patched SQL; repeated keys remain separate and `position` is continuous across the full SQL text.

## Matrix Counts and Session Regression

The fixture contains 217 cases with `status = "final"` and 694 independent
patches. Eight cases and their 23 patches contain complete bind-occurrence
assertions.
Statement-level `query_graph.session` appears in 34 cases, covering `D002`,
`D003`, `D003Q`, `D026`, `D089` through `D095`, and the `DM-*` session cases.
All 34 contain at least one non-empty session item.

When `query_graph.session` is present, the matrix test checks its action, item
scope, target kind, name, and value fields as part of the exact View JSON
comparison. Every case also deparses the unmodified handle and compares the
result with the input SQL byte for byte.

## Supported Cases

| ID | Case | Coverage |
| --- | --- | --- |
| D001 | `SELECT` + `NVL` + named bind | Dameng-compatible `:name` bind conversion and restoration |
| D002 | `SET SCHEMA` | current-schema session context switching |
| D003 | `ALTER SESSION SET CURRENT_SCHEMA` | schema session switching |
| D003Q | `ALTER SESSION SET CURRENT_SCHEMA="..."` | quoted schema identifier with public literal-view quoted-identifier semantics |
| D089 | `ALTER SESSION SET NLS_DATE_FORMAT` | session-level DATE string format |
| D090 | `ALTER SESSION SET NLS_TIMESTAMP_FORMAT` | session-level TIMESTAMP string format |
| D091 | `ALTER SESSION SET NLS_TIMESTAMP_TZ_FORMAT` | session-level TIMESTAMP_TZ string format |
| D092 | `ALTER SESSION SET NLS_TIME_FORMAT` | session-level TIME string format |
| D093 | `ALTER SESSION SET NLS_TIME_TZ_FORMAT` | session-level TIME_TZ string format |
| D094 | `ALTER SESSION SET NLS_SORT` | session-level natural-language sort mode |
| D095 | `ALTER SESSION SET CASE_SENSITIVE` | session-level case-sensitivity mode |
| D004 | `MINUS` | bidirectional Dameng `MINUS` and core set-operator conversion |
| D005 | `LIMIT n OFFSET n` | basic Dameng pagination |
| D006 | `LIMIT offset,n` | comma pagination conversion to the core pagination structure |
| D007 | `SELECT TOP n` | basic `TOP` conversion with public deparse restoration |
| D008 | multi-table JOIN + bind | table, selected-column, join-column, and predicate-column extraction |
| D009 | `INSERT ... VALUES` + bind | inserted-column extraction and bind restoration |
| D010 | multi-row `INSERT ... VALUES` | multi-row value lists |
| D011 | `INSERT ... SELECT` | target table, source table, and inserted-column extraction |
| D012 | `UPDATE` + multiple assignments + bind | updated-column, predicate-column, and bind restoration |
| D013 | `DELETE` + predicate | conditional delete |
| D014 | `MERGE` | basic merge statement |
| D015 | `CREATE TABLE` | table creation |
| D016 | `CREATE OR REPLACE VIEW` | view creation |
| D017 | `CREATE SEQUENCE` | sequence creation |
| D018 | `ALTER TABLE ... ADD` | add column |
| D019 | `CREATE INDEX` | create index |
| D020 | `DROP TABLE` + `TRUNCATE TABLE` | table drop and truncation |
| D021 | transaction control | `BEGIN`, `COMMIT`, and `ROLLBACK` |
| D022 | privilege statements | `GRANT` and `REVOKE` |
| D023 | `ROWNUM` predicate | pseudo-column in a predicate expression |
| D024 | `FOR UPDATE NOWAIT` | row-locking query |
| D025 | q-quoted string | `q'[...]'` string compatibility handling |
| D026 | `SET SCHEMA; SELECT` | schema switching and query remain separate in multi-statement input |
| D027 | `DATE` + `TIMESTAMP` literal | date and timestamp literals |
| D028 | `GROUP BY` + `HAVING` + window function | aggregate query and analytic function |
| D029 | `SELECT TOP offset,count` | `TOP` offset/count conversion with public deparse restoration |
| D030 | `INSERT ... VALUES (?, ?, ?)` | JDBC-style positional parameter conversion, inserted-column extraction, and public-form restoration |
| D031 | `UPDATE ... SET ... WHERE ... = ?` | positional parameter conversion and public-form restoration in SET/WHERE clauses |
| D032 | `EXEC SQL PREPARE ... FROM ...` | Dameng embedded-SQL prepare statement with SQL text and `?` placeholders restored in public form |
| D033 | `EXEC SQL EXECUTE ... USING ...` | Dameng embedded-SQL execute statement and arguments restored in public form |
| D034 | `EXEC SQL DEALLOCATE PREPARE ...` | Dameng embedded-SQL prepared statement deallocation |
| D035 | query with multiple named binds | multiple `:name` binds in query predicates |
| D036 | `IN` + multiple named binds | multiple named binds in predicate lists |
| D037 | `INSERT ... VALUES` + multiple named binds | insert columns and named-bind value lists |
| D038 | multi-row `INSERT ... VALUES` + `?` | multi-row JDBC-style parameterized insert |
| D039 | `UPDATE ... SET ... WHERE ... = ?` | updated columns, predicate columns, and positional parameters |
| D040 | `DELETE ... WHERE ... = ?` | conditional delete and positional parameters |
| D041 | `EXEC SQL PREPARE` + INSERT | embedded-SQL prepared insert text |
| D042 | `EXEC SQL EXECUTE` + named binds | prepared statement execution with named bind arguments |
| D043 | `EXEC SQL EXECUTE` + `?` parameters | prepared statement execution with positional parameters |
| D044 | `TOP` + direct column + named bind | TOP query, direct output field, WHERE bind, and ORDER BY attribution |
| D045 | `CASE` expression output | `target_path` attribution for fields inside `CASE WHEN` |
| D046 | `GROUP BY` + `HAVING` + `ORDER BY` | aggregate output and non-output clause attribution |
| D047 | `UPDATE` + multiple named binds | update/where clauses, bind fields, and null values |
| D048 | `JOIN ... ON` + bind | JOIN/ON fields, WHERE bind, and table-column attribution |
| D049 | `NVL` function output | function `target_path`, argument index, and WHERE bind |
| D050 | `BETWEEN` + multiple named binds | multiple named binds and field-value attribution in `BETWEEN` predicates |
| D051 | `NOT IN` + multiple named binds | multiple named binds and field-value attribution in negated `IN` predicates |
| D052 | `NOT BETWEEN` + multiple named binds | multiple named binds and field-value attribution in negated `BETWEEN` predicates |
| D053 | `NOT LIKE` + named bind | named bind, field-level operator, and keyword attribution in negated `LIKE` predicates |
| D054 | `DISTINCT` + `LIKE` bind | DISTINCT projection, LIKE named bind, and field attribution |
| D055 | nested function projection | ordered `target_path` for `LOWER(UPPER(...))` |
| D056 | `DELETE ... IN` + named bind | conditional delete, collection parameters, and field operator |
| D057 | `UPDATE ... EXISTS` | subquery predicate, correlated fields, and SET bind |
| D058 | columnless `INSERT` | columnless insert, row cells, named binds, and null column names |
| D059 | `CREATE OR REPLACE VIEW` + aggregate JOIN | view creation, JOIN predicates, and GROUP BY aggregation |
| D060 | realistic nested ROWNUM pagination field set | multi-field projection, `a.*`, ROWNUM predicates, and pagination binds |
| D061 | `LEFT JOIN` + `alias.*` | qualified star, JOIN/ON fields, and WHERE bind |
| D062 | `LIMIT/OFFSET` + `?` parameters | positional parameters in pagination clauses |
| D063 | `SELECT :bind FROM dual` | DUAL query and named bind in the SELECT list |
| D064 | anonymous and named binds across multiple `UPDATE` statements and `MERGE` | source-order occurrences retain duplicate `:merge_value` keys and exclude comment text `:comment`/`?`; a complex patch covers a subquery, CAST, CASE, `LIMIT/OFFSET`, named/numeric/anonymous binds, protected regions, and exact renumbering after deletion/insertion |
| D065 | `dameng-select-derived-query-graph` | derived table with output alias and named binds | `query_graph` lineage mapping from derived-table fields to inner base-table fields and `output_name` |
| D066 | `dameng-select-reference-024` | SELECT reference case 024 | Dameng/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| D067 | `dameng-select-reference-026` | SELECT reference case 026 | Dameng/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| D068 | `dameng-select-reference-028` | SELECT reference case 028 | Dameng/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| D069 | `dameng-select-reference-033` | SELECT reference case 033 | Dameng/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| D070 | `dameng-select-reference-044` | SELECT reference case 044 | Dameng/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| D071 | `dameng-select-reference-045` | SELECT reference case 045 | Dameng/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| D072 | `dameng-select-reference-048` | SELECT reference case 048 | Dameng/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| D073 | `dameng-select-reference-049` | SELECT reference case 049 | `query_graph` coverage for Dameng/ROWNUM complex derived-table `*` chains and UNION branches |
| D074 | `dameng-select-reference-046` | SELECT reference case 046 | Dameng complex derived-table and multi-JOIN subquery parsing and View JSON shape |
| D075 | `dameng-select-reference-047` | SELECT reference case 047 | Dameng UNION plus complex derived-table subquery parsing and View JSON shape |
| D076 | `dameng-field-match-kind-direct-and-expression` | direct-field predicate plus function-wrapped field predicate | `query_graph.values[].field_match_kind` distinguishes `direct_field` from `expression_field` |
| D077 | `dameng-expression-field-case-expression-value` | CASE returns a field and compares with a bind | CASE expression fields emit `expression_field` value relations |
| D078 | `dameng-expression-field-multi-field-expression-value` | `NVL(secret, id)` and `secret || id` compared with binds | Fields inside the expression keep separate `expression_field` value relations |
| D079 | `dameng-expression-field-value-side-expression` | field compared with function, concatenation, and CAST value-side expressions | value-side expressions emit `kind=expression` instead of direct binds |
| D080 | `dameng-expression-field-dml-expression-values` | INSERT/UPDATE expression assignments | DML cells and assignments emit `kind=expression` |
| D081 | `dameng-update-positional-bind-rhs-crypto-source` | `UPDATE ... SET protected = :1` | protected-field UPDATE SET right-hand positional bind |
| D082 | `dameng-update-named-bind-rhs-crypto-source` | `UPDATE ... SET protected = :name` | protected-field UPDATE SET right-hand named bind |
| D083 | `dameng-update-question-bind-rhs-crypto-source` | `UPDATE ... SET protected = ?` | protected-field UPDATE SET right-hand JDBC positional bind |
| D084 | `dameng-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = :1, protected2 = :2` | multiple protected-field SET binds, field attribution, and global bind positions |
| D085 | `dameng-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | Dameng literal ESCAPE is emitted in `values[].like_escape` |
| D086 | `dameng-not-like-escape-named-bind` | `NOT LIKE :pattern ESCAPE :escape_char` | named pattern and escape binds keep public SQL and global positions |
| D087 | `dameng-like-escape-question-bind` | `LIKE ? ESCAPE ?` | structured output for JDBC-style positional pattern and escape parameters |
| D088 | `dameng-like-without-explicit-escape` | `LIKE :pattern` | `like_escape` is omitted when ESCAPE is not explicit |
| D134 | `dameng-top-percent` | `SELECT TOP 10 PERCENT ...` | parses TOP percentage form and preserves `PERCENT` in deparse output |
| D135 | `dameng-top-with-ties` | `SELECT TOP 2 WITH TIES ...` | parses TOP tie-preserving form and preserves `WITH TIES` in deparse output |
| D136 | `dameng-top-percent-with-ties` | `SELECT TOP 70 PERCENT WITH TIES ...` | parses the official combined TOP form and preserves public output |
| D137 | `dameng-limit-before-top-restoration` | `LIMIT` statement followed by a `TOP` statement | TOP restoration matches the generated LIMIT ordinal and does not rewrite the preceding ordinary LIMIT |

## Multi-Table INSERT

The Dameng SQL manual supports `<multi_insert_stmt>`, including `INSERT ALL`, `INSERT FIRST`, `WHEN ... THEN`, `ELSE`, multiple `INTO` branches, and a trailing query expression. The implementation maps this syntax to the common DML graph structures without adding dialect-specific JSON fields.

| ID | Case | SQL shape | Coverage |
| --- | --- | --- | --- |
| DU006 | `dameng-insert-all` | `INSERT ALL INTO ... VALUES ... SELECT ...` | `insert_mode=all`, multiple branches, target table, and deparse |
| D120 | `dameng-insert-all-bind-branches` | bind cells in multiple branches | branch-cell `bind_key`, `bind_sql`, and global `bind_position` |
| D121 | `dameng-insert-first-direct-source-fields` | `INSERT FIRST WHEN ... ELSE ... SELECT ...` | `insert_mode=first`, condition selector, source target/field linkage |
| D122 | `dameng-insert-all-conditional` | multiple `WHEN ... THEN` branches | multiple condition selectors, bind positions, and source-target links |
| D123 | `dameng-insert-all-multiple-into-per-when` | multiple `INTO` branches under one `WHEN` | stable branch and bind ordering |
| D124 | `dameng-insert-all-source-field-and-expression-cells` | mixed source-field and expression cells | direct fields use `source_target`; expressions are not misclassified |
| D125 | `dameng-insert-all-schema-qualified-targets` | schema-qualified multiple targets | branch target relation keeps schema/table and bind data |
| D126 | `dameng-insert-first-grouped-when-else-branches` | multiple `INTO` branches under one `WHEN/ELSE` in `INSERT FIRST` | `branch_kind=when/else`; deparse preserves grouped `INTO` branches so `INSERT FIRST` semantics are not split |
| D127 | `dameng-insert-select-source-block-graph` | `INSERT ... SELECT ... FROM ...` | target columns, source block, source fields, and bind attribution for INSERT SELECT |
| D128 | `dameng-merge-update-source-target-lineage` | MERGE matched UPDATE | `s.email` assignment links to source field and source target |
| D129 | `dameng-merge-insert-source-target-lineage` | MERGE not matched INSERT | INSERT cell `s.email` links to source field and source target |
| D130 | `dameng-regexp-like-function-predicate` | `REGEXP_LIKE(name, :pat)` | function predicates reuse `fields/values/predicates` for fields, binds, and expression predicates |
| D131 | `dameng-select-alias-order-by-lineage` | `SELECT u.email AS e ... ORDER BY u.email` | SELECT output aliases keep base-field lineage, while ORDER BY fields stay independently attributed |
| D132 | `dameng-select-or-predicate-order-by-lineage` | `WHERE field = :bind OR field = :bind ORDER BY ...` | OR predicate trees keep both comparison children, binds, and independent ORDER BY field attribution |
| D133 | `dameng-unsupported-keywords-in-quoted-identifiers` | `SELECT "RETURNING", "CONNECT" ...` | unsupported prefilter does not reject protected identifiers |
| DU007 | `dameng-database-link` | `table@database_link` | basic remote object references, with `link` in View JSON |
| D138 | `dameng-database-link-schema-alias-bind` | `schema.table@link alias` plus bind | schema/table/alias/link and bind attribution |
| D139 | `dameng-database-link-update-target` | `UPDATE table@link ...` | database link preserved on a DML target |
| D140 | `dameng-database-link-insert-target` | `INSERT INTO table@link ...` | database link preserved on an INSERT target |
| D141 | `dameng-database-link-delete-target` | `DELETE FROM table@link ...` | database link preserved on a DELETE target |
| D142 | `dameng-database-link-quoted-identifiers` | `"TABLE"@"LINK"` | database link with quoted identifiers |
| DU008 | national q-quoted string | `nq'[...]'` conversion while preserving national string semantics |
| DU008A | duplicate national q-quoted string | only the original national item is restored when ordinary and national strings share the same text |
| DU008B | national string literal | `N'...'` input preserves national string semantics |
| DU008C | duplicate national string literal | restore the national prefix only for original `N'...'` strings when ordinary and national strings share the same text |

## INSERT VALUES Regression: Mixed Binds and Expressions

`DM-BM001` through `DM-BM010` cover combinations of JDBC parameter markers, time functions, compound expressions, literals, `DEFAULT`, and multi-row values in Dameng INSERT VALUES statements.

In these cases, `?` is accepted only as a JDBC prepared-statement parameter marker; execution requires the corresponding prepare/bind flow. The fixture checks parsing, View cell mapping, global bind positions, and byte-for-byte deparse without connecting to a database. `DEFAULT` is used only as a standalone value cell. `DM-BM009` verifies multi-row `VALUES` parsing, View mapping, and deparse; it does not verify execution semantics for column-store tables.

| ID | SQL combination | Regression target |
| --- | --- | --- |
| `DM-BM001` | three `?` parameters followed by `SYSDATE()` | the trailing time expression must not duplicate the preceding bind or appear in `query_graph.fields[].column` |
| `DM-BM002` | `SYSDATE()` followed by three `?` parameters | preserve cell coordinates and bind order when an expression precedes binds |
| `DM-BM003` | interleaved `?` parameters and two current-time functions | preserve column order across multiple expressions and binds |
| `DM-BM004` | mixed `?`, `NULL`, and `SYSDATE()` | keep bind, null literal, and expression kinds independent |
| `DM-BM005` | mixed `?`, `DEFAULT`, and `CURRENT_TIMESTAMP()` | classify a standalone `DEFAULT` cell and a time expression correctly |
| `DM-BM006` | mixed `?`, string literal, and `SYSDATE()` | a literal must not shift adjacent bind or expression cells |
| `DM-BM007` | direct `?`, `COALESCE(?, 'fallback')`, `SYSDATE()`, and a following direct `?` | count the nested parameter globally so the following direct bind has the correct position |
| `DM-BM008` | direct `?`, a `CASE` expression containing `?`, `CAST(? AS INTEGER)`, `SYSDATE()`, and a following direct `?` | preserve the global position of a direct bind after multiple parameterized expressions |
| `DM-BM009` | binds and time functions change positions across three `VALUES` rows | keep multi-row cell coordinates and cross-row bind order stable |
| `DM-BM010` | schema-qualified quoted identifiers and irregular spacing with `?` and `SYSDATE` | preserve identifiers, whitespace, cell mapping, and byte-for-byte deparse |

## ROWNUM Predicate Semantics Regression

This group covers ROWNUM combined with an ordinary predicate, ordered Top-N, reversed equality, a greater-than boundary, and a DELETE batch condition. In View JSON, a ROWNUM condition is an expression predicate that does not reference `fields[]` and instead points to the opposite literal or bind value.

| ID | Case | SQL shape | Coverage |
| --- | --- | --- | --- |
| D-RN001 | `dameng-rownum-and-named-bind` | ordinary comparison `AND ROWNUM <= :limit` | the AND root retains both the ordinary comparison and the fieldless ROWNUM expression; the named bind has position 1 |
| D-RN002 | `dameng-rownum-ordered-top-n` | inner `ORDER BY`, outer `ROWNUM < 11` | the ordering field belongs to the inner block, while the ROWNUM predicate and literal belong to the outer block |
| D-RN003 | `dameng-rownum-reversed-equality` | `1 = ROWNUM` | reversed operand order remains unchanged and the fieldless equality expression references the literal |
| D-RN004 | `dameng-rownum-greater-than-boundary` | `ROWNUM > 1` | the greater-than boundary retains its predicate and literal in View JSON without semantic folding |
| D-RN005 | `dameng-delete-rownum-batch-limit` | DELETE ordinary comparison `AND ROWNUM <= :batch_size` | DELETE DML target, AND tree, and the ROWNUM bind keep the correct block, position, and attribution |

## RETURN/RETURNING INTO Regression

Current coverage includes `RETURNING <target, ...> INTO <:bind, ...>` for
`INSERT` and `DELETE`, and `RETURN <target, ...> INTO <:bind, ...>` for
`UPDATE`. Each list must contain `N >= 1` items; the lists must have strictly
equal lengths and pair by ordinal, and every `INTO` item must be a
colon-prefixed host bind. View represents the return channel as a sink channel
in `dml.result_channels`; every return target's `sink_value` refers to the
same-ordinal host bind in `query_graph.values[]`. This boundary excludes
`BULK COLLECT`, receivers that are not colon-prefixed host binds, and unequal
list lengths.

| ID | Case | SQL shape | Coverage |
| --- | --- | --- | --- |
| D143 | `dameng-insert-values-returning-rowid-into-bind` | `INSERT ... VALUES ... RETURNING ROWID INTO :NAV_ROWID` | INSERT `target_after` reference, ROWID pseudo target, sink bind, replace patches, and byte-for-byte deparse |
| D144 | `dameng-update-return-rowid-into-bind` | `UPDATE ... RETURN ROWID INTO :NAV_ROWID` | UPDATE `target_after` reference, Dameng `RETURN` keyword, sink bind, replace patches, and byte-for-byte deparse |
| D145 | `dameng-delete-returning-rowid-into-bind` | `DELETE ... RETURNING ROWID INTO :NAV_ROWID` | DELETE `target_before` reference, ROWID pseudo target, sink bind, replace patches, and byte-for-byte deparse |
| D146 | `dameng-merge-update-delete-where` | a matched UPDATE has both an action `WHERE` and an attached `DELETE WHERE` | the UPDATE branch exposes both `condition_selector` and `delete_condition_selector`, with no independent DELETE action; 3 independent patches cover assignment, action-predicate value, and delete-predicate value replacement |
| D151 | `dameng-insert-returning-eight-target-bind-pairs` | INSERT `RETURNING` with 8 targets ↔ 8 binds | strict ordinal pairing; one atomic pair insertion at the head produces 9↔9 |
| D152 | `dameng-update-return-eight-target-bind-pairs` | UPDATE `RETURN` with 8 targets ↔ 8 binds | complete source-order enumeration covers SET, WHERE, and all eight `INTO` binds for 11 root occurrences; paired middle insertion of `last_login_at` / `:out_last_login_at` produces 12 occurrences while preserving 9↔9 order |
| D153 | `dameng-delete-returning-eight-target-bind-pairs` | DELETE `RETURNING` with 8 targets ↔ 8 binds | strict ordinal pairing; one atomic pair insertion at the tail produces 9↔9 |

## Hierarchical Query Regression

These four final cases define the Dameng hierarchical-query boundary. They
cover both `START WITH ... CONNECT BY` and `CONNECT BY ... START WITH` source
orders, two parent-child field orientations for `PRIOR`, `LEVEL`, `NOCYCLE`, and
`CONNECT_BY_ROOT`. Each case contains five independent patches, for 20 patches
in total: 16 `replace` and 4 `insert_column` actions.

| ID | Case | SQL Shape | Coverage |
| --- | --- | --- | --- |
| D147 | `dameng-hierarchical-start-connect-prior-level` | `START WITH` followed by `CONNECT BY PRIOR` and `LEVEL` | `start_with` / `connect_by` clauses, a relationless `LEVEL` pseudo target, and the left-side `PRIOR` field occurrence |
| D148 | `dameng-hierarchical-connect-start-source-order` | `CONNECT BY` followed by `START WITH` | byte-preserved reversed source order while View remains in START WITH then CONNECT BY semantic order |
| D149 | `dameng-hierarchical-prior-reverse-direction` | `PRIOR manager_id = employee_id` | `PRIOR` remains on the left comparison operand while the field direction is reversed from the basic case |
| D150 | `dameng-hierarchical-connect-by-root-nocycle` | `CONNECT_BY_ROOT`, `LEVEL`, and `CONNECT BY NOCYCLE` | operator `target_path` on the expression target, a relationless pseudo target, and `nocycle` on the CONNECT BY root predicate |

## Multi-Table Single-Target UPDATE Regression

Dameng multi-table `UPDATE` accepts JOIN chains, comma-separated relation lists, and mixed forms, but every SET assignment must resolve to the same table object. That unique object is the statement and DML target relation.

| ID | Case | SQL Shape | Coverage |
| --- | --- | --- | --- |
| D154 | `dameng-multitable-update-two-table-join-first-target-contract` | two-table JOIN with the first relation as target | unique target, ON/WHERE attribution, and assignment and relation patches |
| D155 | `dameng-multitable-update-three-table-middle-target-contract` | three comma-separated relations with the middle relation as target | non-leading target resolution, bind order, and assignment patches |
| D156 | `dameng-multitable-update-four-table-last-target-contract` | four comma-separated relations with the last relation as target | quoted objects, final-relation target, and patches |
| D157 | `dameng-multitable-update-same-table-distinct-alias-contract` | the same table object under distinct aliases | unique write-object resolution by alias and compound-right-side patches |
| D158 | `dameng-multitable-update-four-table-join-chain-contract` | LEFT/INNER/RIGHT JOIN chain | JOIN conditions, one middle target, and assignment patches |
| D159 | `dameng-multitable-update-join-comma-mixed-contract` | mixed JOIN and comma-separated relations | mixed relation list, unique target, and deparse |

## Query Graph Quoted-Alias Contract

`relations[].alias_quoted_identifier` is `true` only when the relation alias is double-quote delimited. `targets[].output_quoted_identifier` is `true` when the output name comes from an explicit double-quoted alias, or inherits a double-quoted field name without an explicit alias; View JSON omits either key when its value is `false`.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| D160 | `dameng-quoted-relation-alias-and-target-output-contract` | double-quoted relation/derived aliases and output names | both quoted flags, field-name inheritance, and two output-alias patches |

## Independent MERGE INSERT Column and Value Mutation

| Case ID | Case Name | Form | Verification Focus |
| --- | --- | --- | --- |
| D161 | `dameng-merge-omitted-insert-column-value-independent` | `WHEN NOT MATCHED THEN INSERT VALUES (...)` with no target-column list | an omitted list still emits `target_list_selector`; three independent patches verify column-only list materialization, value-only cell insertion, and replacement of an existing `merge_insert_cell`; together with the existing paired mode, this covers the three-state `insert_column` contract |

## Query Graph Segmented Quoted-Identifier Contract

Relation qualification records double-quote delimiter state per segment through
`database_quoted_identifier`, `schema_quoted_identifier`, the existing object
`quoted_identifier`, and `link_quoted_identifier`. DML target columns use
`dml_column.quoted_identifier`. Each flag describes only its corresponding name
segment; View JSON omits the key for an unquoted or absent segment, so case
cannot be inferred from identifier spelling. The following 18 cases and 19
independent patches cover the five ordinary DML forms, quoted/unquoted
database-link contrasts, multi-branch `INSERT ALL/FIRST`, and a database-link
target. Public C-structure lifecycle coverage is maintained in
`tests/unit/test_identifier_spelling.c`.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| D162 | `dameng-relation-dml-quoted-identifier-ordinary-select` | three-part SELECT relation with quoted alias and field | database/object segment flags and per-segment recomputation after a relation patch |
| D163 | `dameng-relation-dml-quoted-identifier-ordinary-insert` | three-part INSERT target with quoted and unquoted target columns | schema/object and `dml_column.quoted_identifier`; a relation patch preserves column state |
| D164 | `dameng-relation-dml-quoted-identifier-ordinary-update` | three-part UPDATE target, alias, and SET/WHERE fields | database/object/alias/field flags and relation-patch recomputation |
| D165 | `dameng-relation-dml-quoted-identifier-ordinary-delete` | three-part DELETE target and field | schema/object/alias/field flags and relation-patch recomputation |
| D166 | `dameng-relation-dml-quoted-identifier-ordinary-merge` | three-part MERGE target/source with UPDATE and INSERT branches | both relations' segment flags and MERGE INSERT `dml_column.quoted_identifier`; two patches independently recompute relation and column flags |
| D167 | `dameng-relation-dml-quoted-identifier-database-link-select-unquoted` | SELECT from `APP.T@REMOTE` | unquoted contrast omits all four new true flags; a field patch leaves relation state unchanged |
| D168 | `dameng-relation-dml-quoted-identifier-database-link-select-quoted` | SELECT from `"APP"."T"@"REMOTE"` | schema/object/link plus existing alias/field/output flags remain present together |
| D169 | `dameng-relation-dml-quoted-identifier-database-link-insert-unquoted` | unquoted database-link INSERT | unquoted relation and target-column contrast; a value patch does not change flags |
| D170 | `dameng-relation-dml-quoted-identifier-database-link-insert-quoted` | quoted database-link INSERT | schema/object/link and target-column flags remain after a value patch |
| D171 | `dameng-relation-dml-quoted-identifier-database-link-update-unquoted` | unquoted database-link UPDATE | unquoted relation contrast and independent field-patch boundary |
| D172 | `dameng-relation-dml-quoted-identifier-database-link-update-quoted` | quoted database-link UPDATE | schema/object/link/alias and field flags remain exact after a patch |
| D173 | `dameng-relation-dml-quoted-identifier-database-link-delete-unquoted` | unquoted database-link DELETE | unquoted relation contrast and field-patch boundary |
| D174 | `dameng-relation-dml-quoted-identifier-database-link-delete-quoted` | quoted database-link DELETE | schema/object/link/alias and field flags remain exact after a patch |
| D175 | `dameng-relation-dml-quoted-identifier-database-link-merge-unquoted` | MERGE with unquoted database-link target and source | false/omitted contrast on both relations; quoting a patched target column adds its flag |
| D176 | `dameng-relation-dml-quoted-identifier-database-link-merge-quoted` | MERGE with quoted database-link target and source | schema/object/link/alias flags on both relations; unquoting a patched target column removes its flag |
| D177 | `dameng-relation-dml-quoted-identifier-insert-all-multi-branch` | three `INSERT ALL` targets independently quoting database/schema/object segments | per-branch relation and target-column flags remain after a branch-value patch |
| D178 | `dameng-relation-dml-quoted-identifier-insert-first-multi-branch` | three `INSERT FIRST` targets across WHEN/ELSE branches | per-branch relation and target-column flags remain after an ELSE-value patch |
| D179 | `dameng-relation-dml-quoted-identifier-insert-all-database-link-projection-gap` | `INSERT ALL INTO APP."T"@"REMOTE" ("ID")` | the multi-table-insert target projection retains object/link and target-column flags after a value patch |

## DDL Query Graph Relation Contract

Every SQL form below is converged on Dameng's official syntax and verified by byte-exact deparse in the current entry. DDL relations live in a root block with `kind = "ddl"`; changed objects and FK references use `ddl_role = "target"` and `"reference"`. A query-backed target points through `source_block` to a SELECT block. DROP targets retain complete segmented quoted flags but currently have no relation selector.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| D180 | `dameng-ddl-relation-create-table-foreign-key` | CREATE TABLE with FK REFERENCES | target/reference roles, segmented quoted flags, and two relation patches |
| D181 | `dameng-ddl-relation-alter-table-foreign-key` | ALTER TABLE ADD FK | target/reference roles and two relation patches |
| D182 | `dameng-ddl-relation-create-index-on-table` | CREATE INDEX ON table | ON-table as DDL target and one relation patch |
| D183 | `dameng-ddl-relation-drop-table-target` | DROP TABLE | DDL target and the no-selector DROP boundary |
| D184 | `dameng-ddl-relation-drop-materialized-view-target` | DROP MATERIALIZED VIEW | DDL target and the no-selector DROP boundary |
| D185 | `dameng-ddl-relation-truncate-table-target` | TRUNCATE TABLE | DDL target, segmented quoted flags, and one relation patch |
| D186 | `dameng-ddl-relation-alter-table-rename-target` | ALTER TABLE RENAME TO | old table as the sole DDL target without representing the new name as a relation; one patch |
| D187 | `dameng-ddl-relation-create-view-target-and-source` | CREATE VIEW AS SELECT | DDL target, SELECT source, `source_block`, and two relation patches |
| D188 | `dameng-ddl-relation-create-table-as-target-and-source` | CTAS | separate DDL-target and SELECT-source blocks with two relation patches |
| D189 | `dameng-ddl-relation-create-materialized-view-target-and-source` | CREATE MATERIALIZED VIEW AS SELECT | separate DDL-target and SELECT-source blocks with two relation patches |

## Explicit CTE Column-Name Ordinal Contract

The following final cases verify explicit CTE column-name ordinal mapping and its defined boundaries.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| D190 | `dameng-cte-explicit-column-user-ordinal` | ordinary three-column CTE user example | the source block's three enumerable targets take `did`, `masked_title`, and `plain_content` by ordinal; one relation patch |
| D191 | `dameng-cte-explicit-column-quoted-expression-ordinal` | two duplicate underlying `ID` fields plus one `UPPER(TITLE)` expression, with fully quoted explicit names and reordered outer references | ordinal mapping is independent of underlying name and target kind; all three source targets take the explicit names and quoted flags; one relation patch |
| D192 | `dameng-cte-explicit-column-repeated-reference` | one CTE referenced twice as aliases `a` and `b` | both CTE relations share one source block; source targets are overlaid once by ordinal; one relation patch |
| D193 | `dameng-cte-explicit-column-set-boundary` | a `UNION ALL` CTE | the SET result block has no directly enumerable targets; both branches retain underlying target names without fabricated result targets or cross-branch overlays; one relation patch |

## Coverage Boundary

This matrix lists only cases that parse successfully and have final View and patch expectations. Syntax boundaries outside this executable fixture are maintained in `doc/dameng_official_syntax_coverage.csv`.

## Maintenance

- New Dameng support must update `tests/cases/dameng_dialect_input.json`, this matrix, and executable regression tests.
- Syntax outside the executable fixture must not be listed here as a validated case.
