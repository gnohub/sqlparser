# Oracle Dialect Case Matrix

This file records regression cases for the Oracle dialect conversion layer. The executable fixture is `tests/cases/oracle_dialect_input.json`. For every final case, the runner requires unchanged SQL to deparse byte for byte, compares the actual View with the expected JSON structure, and executes each patch independently. Patched SQL must match `patch.deparse` byte for byte, remain identical after a fresh parse and second deparse, and produce the same View from the patched and freshly parsed handles. When a case provides `bind_occurrences`, the runner also compares `position`, `kind`, `key`, and original `sql` item by item for the source SQL and every patched SQL; repeated keys remain separate and `position` is continuous across the full SQL text.

## Matrix Counts and Session Regression

The fixture contains 252 cases with `status = "final"` and 854 independent
patches. Two cases and their 6 patches contain complete bind-occurrence
assertions.
Statement-level `query_graph.session` appears in 59 cases, covering `O043`,
`O043Q`, `O044` through `O047`, `O082` through `O086`, and the `ORA-*`
session cases. All 59 contain at least one non-empty session item.

View validation compares JSON structures; object-key order and formatting
whitespace do not participate. Session action, item scope, target kind, name,
and value fields are all part of that comparison.

## INSERT VALUES Regression: Mixed Binds and Expressions

`ORA-BM001` through `ORA-BM010` cover Oracle single-row
`INSERT ... VALUES`. `:name` and `:1` are handled as Oracle binds. A `?` is
accepted only as a JDBC prepared-statement template marker; native Oracle bind
variables use colon-prefixed markers.

All ten cases use single-row VALUES statements, and `DEFAULT` is always a
standalone cell. Every case asserts each cell's `row`, `column`, `kind`, and
`selector`, plus each direct bind's `bind_key`, `bind_kind`, `bind_sql`, global
`bind_position`, and `selector`. A later direct-bind position verifies that nested
binds were included in the global sequence. `SYSDATE` and
`CURRENT_TIMESTAMP` must not appear in `query_graph.fields[].column`.

| ID | SQL shape | Boundary |
| --- | --- | --- |
| `ORA-BM001` | three standalone JDBC `?` cells + trailing `SYSDATE` | preserves the original input SQL byte for byte, including irregular spacing and semicolon; `?` is JDBC-only |
| `ORA-BM002` | leading `CURRENT_TIMESTAMP` + three standalone JDBC `?` cells | a leading expression cell must not shift bind positions |
| `ORA-BM003` | `:1`, `:2`, `:3` + trailing `SYSDATE` | numeric colon-prefixed Oracle bind markers |
| `ORA-BM004` | leading `SYSDATE` + three named binds | named colon-prefixed Oracle bind markers |
| `ORA-BM005` | named binds interleaved with `SYSDATE` / `CURRENT_TIMESTAMP` | expression cells must not shift later binds |
| `ORA-BM006` | bind + `NULL` + `SYSDATE` + bind | `NULL` is literal and `SYSDATE` is expression |
| `ORA-BM007` | bind + standalone `DEFAULT` + `CURRENT_TIMESTAMP` + bind | `DEFAULT` is not nested in another expression |
| `ORA-BM008` | direct bind + `COALESCE(:retry_count, 0)` + `SYSDATE` + direct bind | trailing direct bind at position 3 verifies that the nested bind was counted |
| `ORA-BM009` | direct bind + a `CASE` expression containing `:enabled` + `CAST(:amount AS NUMBER)` + `SYSDATE` + direct bind | trailing direct bind at position 4 verifies that both nested binds were counted |
| `ORA-BM010` | schema-qualified quoted identifiers + three colon-prefixed binds + `CURRENT_TIMESTAMP` | preserves mixed-case identifiers, irregular whitespace, and semicolon |

## Supported Cases

| ID | Case | Coverage |
| --- | --- | --- |
| O001 | `SELECT` + `NVL` + named bind | Oracle `:name` bind conversion and restoration |
| O002 | q-quoted string | `q'[...]'` conversion to a safe string literal |
| O003 | national q-quoted string | `nq'[...]'` conversion while preserving national string semantics |
| O003A | duplicate national q-quoted string | only the original national item is restored when ordinary and national strings share the same text |
| O003B | national string literal | `N'...'` input preserves national string semantics |
| O003C | duplicate national string literal | restore the national prefix only for original `N'...'` strings when ordinary and national strings share the same text |
| O004 | `MINUS` | bidirectional Oracle `MINUS` and core `EXCEPT` conversion |
| O005 | `OFFSET ... FETCH` | Oracle pagination syntax |
| O006 | `ROWNUM` predicate | pseudo-column in a predicate expression |
| O007 | multi-table JOIN + bind | table, selected-column, join-column, and predicate-column extraction |
| O008 | `INSERT ... VALUES` + bind | inserted-column extraction and bind restoration |
| O009 | multi-row `INSERT ... VALUES` | multi-row value lists |
| O010 | `INSERT ... SELECT` | target table, source table, and inserted-column extraction |
| O011 | `UPDATE` + multiple assignments + bind | updated-column, predicate-column, and bind restoration |
| O012 | `DELETE` + predicate | conditional delete |
| O013 | repeated named bind | one internal parameter number for the same bind name |
| O014 | positional bind | `:1` and `:2` conversion and restoration |
| O015 | `DATE` literal | date literal |
| O016 | `CASE` expression | conditional expression |
| O017 | `EXISTS` subquery | subquery table and predicate-column extraction |
| O018 | `GROUP BY` + `HAVING` | aggregate query |
| O019 | `UNION ALL` | set query |
| O020 | `INTERSECT` | set query |
| O021 | `MERGE` | basic merge statement |
| O022 | `CREATE TABLE` | table creation with common Oracle type names |
| O023 | `CREATE SEQUENCE` | sequence creation |
| O024 | `CREATE OR REPLACE VIEW` | view creation |
| O025 | `DROP TABLE` | table drop |
| O026 | `TRUNCATE TABLE` | table truncation |
| O027 | transaction control | `SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, and `COMMIT` |
| O028 | privilege statements | `GRANT` and `REVOKE` |
| O029 | comment statement | `COMMENT ON TABLE` |
| O030 | `FOR UPDATE NOWAIT` | row-locking query |
| O031 | `DECODE` + `SYSDATE` | common Oracle function and pseudo-column |
| O032 | `ROW_NUMBER() OVER` | analytic function |
| O033 | `TIMESTAMP` literal | timestamp literal |
| O034 | quoted identifiers | case-sensitive object and column names |
| O035 | `ALTER TABLE ... ADD` | add column |
| O036 | `CREATE INDEX` | create index |
| O037 | `DROP INDEX` | drop index |
| O038 | `IN` + multiple binds | multiple binds in a predicate list |
| O039 | `DELETE` + `DATE` literal | conditional delete and date literal |
| O040 | materialized view | compatible materialized-view syntax |
| O041 | unsupported keywords in string | `RETURNING`, `@`, and `(+)` inside strings do not trigger unsupported |
| O042 | hierarchical-query keywords in a comment | `CONNECT BY` text inside comments does not participate in SQL syntax recognition |
| O042Q | unsupported keywords in protected identifiers | `RETURNING` and `@` inside quoted identifiers do not trigger unsupported |
| O043 | `ALTER SESSION SET CURRENT_SCHEMA` | current-schema session context switching |
| O043Q | `ALTER SESSION SET CURRENT_SCHEMA="..."` | quoted schema identifier with public literal-view quoted-identifier semantics |
| O044 | `ALTER SESSION SET CONTAINER` | current-container session context switching |
| O045 | `ALTER SESSION SET CONTAINER=CDB$ROOT` | official root container name |
| O046 | `ALTER SESSION SET CONTAINER ... SERVICE ...` | container switching with the `SERVICE` clause |
| O047 | `SELECT ...; ALTER SESSION SET CURRENT_SCHEMA` | query and schema switching remain separate in multi-statement input |
| O048 | `INSERT ... VALUES (?, ?, ?)` | JDBC-style positional parameter conversion, inserted-column extraction, and public-form restoration |
| O049 | `UPDATE ... SET ... WHERE ... = ?` | positional parameter conversion and public-form restoration in SET/WHERE clauses |
| O050 | `EXECUTE IMMEDIATE ... USING ...` | Oracle dynamic SQL execution with SQL text and bind arguments restored in public form |
| O051 | multiple named-bind query | multiple `:name` binds in `SELECT` predicates |
| O052 | `IN` + multiple named binds | bind restoration in `IN (:a, :b, :c)` predicates |
| O053 | `FETCH FIRST` + bind | bind restoration in pagination limits |
| O054 | `INSERT ... VALUES` + multiple named binds | insert columns and named-bind value lists |
| O055 | `UPDATE` + multiple named binds | updated columns, predicate columns, and named binds |
| O056 | `DELETE` + multiple named binds | conditional delete and named binds |
| O057 | positional bind pair | `:1` and `:2` predicate parameters |
| O058 | expanded `INSERT ... VALUES (?, ?, ?)` | insert columns and JDBC-style positional parameters |
| O059 | `DELETE ... WHERE ... = ?` | JDBC-style positional parameters in conditional delete |
| O060 | `EXECUTE IMMEDIATE` update statement | dynamic UPDATE SQL text and multiple USING binds |
| O061 | nested ROWNUM pagination with bind | nested query, `a.*`, pseudo-column alias, and named binds |
| O062 | `NVL` + `TO_CHAR` + `UPPER` | function `target_path`, nested function, argument index, and WHERE bind |
| O063 | `CASE` expression output | `target_path` attribution for fields inside `CASE WHEN` |
| O064 | `GROUP BY` + `HAVING` + `ORDER BY` | aggregate output and non-output clause attribution |
| O065 | `UPDATE` + multiple named binds | update/where clauses, bind fields, and null values |
| O066 | ROWNUM pagination attribution | nested query, `a.*`, ROWNUM predicate, and outer predicate attribution |
| O067 | mixed `:1` and `?` positional binds | `bind_kind` and `bind_sql` distinguish Oracle positional binds from JDBC positional markers |
| O068 | `BETWEEN` + multiple named binds | multiple named binds and field-value attribution in `BETWEEN` predicates |
| O069 | `NOT IN` + multiple named binds | multiple named binds and field-value attribution in negated `IN` predicates |
| O070 | `NOT BETWEEN` + multiple named binds | multiple named binds and field-value attribution in negated `BETWEEN` predicates |
| O071 | `NOT LIKE` + named bind | named bind, field-level operator, and keyword attribution in negated `LIKE` predicates |
| O072 | `DISTINCT` + `LIKE` bind | DISTINCT projection, LIKE named bind, and field attribution |
| O073 | nested function projection | ordered `target_path` for `LOWER(UPPER(...))` |
| O074 | `DELETE ... IN` + named bind | conditional delete, collection parameters, and field operator |
| O075 | `UPDATE ... EXISTS` | subquery predicate, correlated fields, and SET bind |
| O076 | columnless `INSERT` | columnless insert, row cells, named binds, and null column names |
| O077 | `CREATE OR REPLACE VIEW` + aggregate JOIN | view creation, JOIN predicates, and GROUP BY aggregation |
| O078 | realistic nested ROWNUM pagination field set | multi-field projection, `a.*`, ROWNUM predicates, and pagination binds |
| O079 | `LEFT JOIN` + `alias.*` | qualified star, JOIN/ON fields, and WHERE bind |
| O080 | `ORDER BY 1` | ordinal sort item and projection-order related syntax |
| O081 | `SELECT :bind FROM dual` | DUAL query and named bind in the SELECT list |
| O082 | `ALTER SESSION SET NLS_DATE_FORMAT` | string-valued ordinary session parameter |
| O083 | `ALTER SESSION SET NLS_DATE_LANGUAGE` | identifier-valued ordinary session parameter |
| O084 | `ALTER SESSION SET INSTANCE` | numeric ordinary session parameter |
| O085 | `ALTER SESSION SET ERROR_ON_OVERLAP_TIME` | boolean/enumerated ordinary session parameter |
| O086 | `ALTER SESSION SET NLS_NUMERIC_CHARACTERS` | punctuation-bearing string session parameter |
| O087 | named binds across multiple `UPDATE` statements and `MERGE` | source-order occurrences retain duplicate `:same`/`:merge_value` keys and exclude comment text `:comment`/`?`; a complex patch covers a subquery, CAST, CASE, `FETCH FIRST`, named/numeric/anonymous binds, protected regions, and exact renumbering after deletion/insertion |
| O088 | `oracle-select-derived-query-graph` | derived table with output alias and named binds | `query_graph` representation for derived-table fields, output alias, and predicate binds |
| O089 | `oracle-select-reference-024` | SELECT reference case 024 | Oracle/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| O090 | `oracle-select-reference-026` | SELECT reference case 026 | Oracle/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| O091 | `oracle-select-reference-028` | SELECT reference case 028 | Oracle/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| O092 | `oracle-select-reference-033` | SELECT reference case 033 | Oracle/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| O093 | `oracle-select-reference-044` | SELECT reference case 044 | Oracle/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| O094 | `oracle-select-reference-045` | SELECT reference case 045 | Oracle/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| O095 | `oracle-select-reference-048` | SELECT reference case 048 | Oracle/ROWNUM/complex derived SELECT example parsing and View JSON shape |
| O096 | `oracle-select-reference-049` | SELECT reference case 049 | Oracle/ROWNUM/complex derived SELECT parsing, `d.* -> b.* -> o.*` lineage, and View JSON shape |
| O097 | `oracle-select-reference-046` | SELECT reference case 046 | Oracle complex derived-table and multi-JOIN subquery parsing and View JSON shape |
| O098 | `oracle-select-reference-047` | SELECT reference case 047 | Oracle UNION plus complex derived-table subquery parsing and View JSON shape |
| O099 | `oracle-select-nested-star-query-graph` | nested derived tables, ROWNUM, and `SELECT *` | `query_graph` represents the derived-table `*` chain and UNION branches |
| O100 | `oracle-field-match-kind-direct-and-expression` | direct-field predicate plus function-wrapped field predicate | `query_graph.values[].field_match_kind` distinguishes `direct_field` from `expression_field` |
| O101 | `oracle-expression-field-case-expression-value` | CASE returns a field and compares with a bind | CASE expression fields emit `expression_field` value relations |
| O102 | `oracle-expression-field-multi-field-expression-value` | `NVL(SECRET, ID)` and `SECRET || ID` compared with binds | Fields inside the expression keep separate `expression_field` value relations |
| O103 | `oracle-expression-field-value-side-expression` | field compared with function, concatenation, and CAST value-side expressions | value-side expressions emit `kind=expression` instead of direct binds |
| O104 | `oracle-expression-field-dml-expression-values` | INSERT/UPDATE expression assignments | DML cells and assignments emit `kind=expression` |
| O105 | `oracle-update-positional-bind-rhs-crypto-source` | `UPDATE ... SET protected = :1` | protected-field UPDATE SET right-hand Oracle positional bind |
| O106 | `oracle-update-named-bind-rhs-crypto-source` | `UPDATE ... SET protected = :name` | protected-field UPDATE SET right-hand Oracle named bind |
| O107 | `oracle-update-question-bind-rhs-crypto-source` | `UPDATE ... SET protected = ?` | protected-field UPDATE SET right-hand JDBC positional bind |
| O108 | `oracle-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = :1, protected2 = :2` | multiple protected-field SET binds, field attribution, and global bind positions |
| O132 | `oracle-insert-all-bind-branches` | `INSERT ALL` bind branches | branch cells expose bind key, bind kind, bind SQL, and global bind position |
| O133 | `oracle-insert-all-multi-target` | `INSERT ALL` multiple target tables | each INTO branch keeps its own target relation, target columns, and rows |
| O134 | `oracle-insert-select-union-literals` | `INSERT ... SELECT ... UNION ALL` literal sources | source targets expose literals through value indexes |
| O135 | `oracle-insert-select-union-positional-binds` | `INSERT ... SELECT ... UNION ALL` positional bind sources | source targets expose positional binds through value indexes |
| O136 | `oracle-insert-select-union-named-binds` | `INSERT ... SELECT ... UNION ALL` named bind sources | source targets expose named binds through value indexes |
| O137 | `INSERT ALL` | Oracle multi-table insert | `insert_mode=all`, branch target relations, target columns, rows, and deparse |
| O138 | `INSERT FIRST` | conditional multi-table insert | `insert_mode=first` and branch condition selectors |
| O139 | `oracle-insert-first-direct-source-fields` | `INSERT FIRST` branch cells reference source-query fields | branch cells emit `kind=field` and point to source-query outputs through `source_target` |
| O141A | `oracle-insert-first-grouped-when-else-branches` | multiple `INTO` branches under one `WHEN/ELSE` in `INSERT FIRST` | `branch_kind=when/else`; deparse preserves grouped `INTO` branches so `INSERT FIRST` semantics are not split |
| O140 | `oracle-insert-all-conditional` | conditional `INSERT ALL WHEN ... THEN` | `insert_mode=all`, branch condition selectors, bind positions, and source-target links |
| O141 | `oracle-insert-all-multiple-into-per-when` | multiple INTO branches under one WHEN | multiple branches under the same WHEN keep independent spans and condition selectors; ELSE branches parse correctly |
| O142 | `oracle-insert-select-source-fields` | direct-field `INSERT ... SELECT` source targets | source-query output fields keep `kind=field` and field attribution |
| O143 | `oracle-insert-select-expression-targets` | expression `INSERT ... SELECT` source targets | source-query expression targets keep `kind=expression`; field target paths remain visible |
| O144 | `oracle-insert-all-source-field-and-expression-cells` | mixed source-field and expression branch cells | direct-field cells use `source_target`; expression cells are not misclassified as field, literal, or bind |
| O145 | `oracle-insert-select-union-distinct-literals` | literal `INSERT ... SELECT ... UNION` sources | set kind, branch targets, literal values, and target ordinals remain stable |
| O146 | `oracle-insert-select-intersect-binds` | positional-bind `INSERT ... SELECT ... INTERSECT` sources | set kind, branch targets, bind key/SQL/global positions remain stable |
| O147 | `oracle-insert-select-minus-named-binds` | named-bind `INSERT ... SELECT ... MINUS` sources | public Oracle `MINUS`, branch targets, bind key/SQL/global positions remain stable |
| O148 | `oracle-insert-all-schema-qualified-targets` | schema-qualified `INSERT ALL` targets | each branch target relation keeps schema/table; bind key/SQL/global positions remain stable |
| O149 | `oracle-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | Oracle literal ESCAPE is emitted in `values[].like_escape` |
| O150 | `oracle-not-like-escape-named-bind` | `NOT LIKE :pattern ESCAPE :escape_char` | named pattern bind and named escape bind keep public SQL and global positions |
| O151 | `oracle-like-escape-question-bind` | `LIKE ? ESCAPE ?` | structured ESCAPE output for Oracle JDBC-style positional parameters |
| O152 | `oracle-like-escape-expression` | `LIKE :pattern ESCAPE UPPER('!')` | expression ESCAPE emits `like_escape.kind=expression` |
| O153 | `oracle-derived-like-escape-literal` | outer derived-table `LIKE ... ESCAPE` | LIKE ESCAPE output stays stable under derived-table field attribution |
| O154 | `oracle-like-without-explicit-escape` | `LIKE :pattern` | `like_escape` is omitted when ESCAPE is not explicit |
| O155 | `oracle-p3-update-alias-qualified-assignment` | `UPDATE ... x SET x.email = :1` | alias-qualified assignment targets expose the real column `email` and keep the RHS bind selector |
| O156 | `oracle-p3-update-multiple-alias-qualified-assignments` | multiple `x.column = :bind` assignments | every assignment emits the real target column, and WHERE binds remain attributed |
| O157 | `oracle-p3-update-from-source-field` | `UPDATE ... SET name = s.name FROM src s` | assignment RHS emits `kind=field`, `source_field`, and field-to-field WHERE predicates |
| O158 | `oracle-p3-update-schema-qualified-alias-target` | schema-qualified UPDATE target | schema/table/alias are preserved, and assignment columns are not reported as aliases |
| O159 | `oracle-p3-update-scalar-subquery-predicate` | UPDATE with scalar subquery | outer fields and inner predicate field/value/operator data enter `query_graph` |
| O160 | `oracle-p3-delete-exists-correlated-predicate` | DELETE with correlated EXISTS | AND predicate tree, field-to-field correlation, and literal selectors stay structured |
| O161 | `oracle-p3-select-or-predicate-and-order-by` | SELECT with OR and ORDER BY | both OR branches stay in predicates, and ORDER BY fields do not alter output-target lineage |
| O162 | `oracle-p3-insert-all-independent-branches` | multi-branch `INSERT ALL` | branch target relation, target columns, rows, and bind cell selectors are emitted independently |
| O163 | `oracle-p3-merge-update-source-target-lineage` | MERGE matched UPDATE | `s.email` assignments link to source field and source target |
| O164 | `oracle-p3-merge-insert-source-target-lineage` | MERGE not matched INSERT | INSERT cell `s.email` links to source field and source target |
| O165 | `oracle-p3-select-distinct-base-field-lineage` | SELECT DISTINCT direct field | DISTINCT does not change base-field pass-through lineage |
| O166 | `oracle-p3-select-alias-order-by-lineage` | SELECT alias and ORDER BY | output alias is preserved, and ORDER BY fields are attributed independently |
| O167 | `oracle-p3-select-star-rowid-lineage` | qualified star plus ROWID | `x.*` and `x.ROWID` are structured separately, and ROWID does not pollute star lineage |
| O168 | `oracle-p3-update-full-alias-qualified-crypto-shape` | alias-qualified UPDATE with scalar subquery | integrated P3 shape for multiple protected-column assignments and subquery predicates |
| O169 | `oracle-regexp-like-function-predicate` | `REGEXP_LIKE(name, :pat)` | function predicates reuse `fields/values/predicates` for fields, binds, and expression predicates |
| OU015 | `oracle-database-link` | `table@database_link` | basic remote object references, with `link` in View JSON |
| O170 | `oracle-database-link-schema-alias-bind` | `schema.table@link alias` plus bind | schema/table/alias/link and bind attribution |
| O171 | `oracle-database-link-update-target` | `UPDATE table@link ...` | database link preserved on a DML target |
| O172 | `oracle-database-link-insert-target` | `INSERT INTO table@link ...` | database link preserved on an INSERT target |
| O173 | `oracle-database-link-delete-target` | `DELETE FROM table@link ...` | database link preserved on a DELETE target |
| O174 | `oracle-database-link-quoted-identifiers` | `"TABLE"@"LINK"` | database link with quoted identifiers |
| OU014 | `oracle-create-synonym` | `CREATE SYNONYM u FOR users` | Oracle synonym creation statement |
| O175 | `oracle-create-public-synonym` | `CREATE OR REPLACE PUBLIC SYNONYM ...` | Oracle public synonym creation statement |
| O176 | `oracle-drop-synonym` | `DROP SYNONYM ... FORCE` | Oracle synonym drop statement |
| OU016 | `oracle-explain-plan` | `EXPLAIN PLAN FOR SELECT ...` | Oracle explain plan statement preservation |
| O177 | `oracle-explain-plan-into` | `EXPLAIN PLAN SET STATEMENT_ID ... INTO ... FOR SELECT ...` | Oracle explain plan form with plan table |
| O178 | `oracle-union-all-three-branch-scope` | explicitly grouped three-branch `UNION ALL` | two-level set topology, branch order, and literal selectors remain stable |
| O179 | `oracle-grouped-union-all-intersect` | explicitly grouped `UNION ALL` and `INTERSECT` | grouping boundaries, operator kinds, and post-patch deparse for selected branches remain stable |
| O180 | `oracle-union-all-root-cte-scope` | root CTE visible across `UNION ALL` branches | both branches resolve to the same CTE source block |
| O181 | `oracle-union-all-qualified-table-bypasses-cte` | schema-qualified and database-link base tables sharing a CTE name | `app.src` and `src@remote_db` remain base relations and are not misclassified as the CTE |
| O182 | `oracle-correlated-union-all-subquery-scope` | `UNION ALL` inside a correlated subquery | each set branch keeps its local relation and resolves the outer `o` relation |
| O183 | `oracle-upper-reverse-bind-expression-predicate` | `:v = UPPER(SECRET)` | expression predicate references `SECRET` through `right_field` and emits only the left-side bind value |
| O184 | `oracle-in-subquery-named-bind-membership` | `ID IN (SELECT USER_ID ... STATUS = :status)` | separates the outer membership field and `IN` predicate from the inner block, target, filter field, and named bind; 6 patches cover both field levels, the relation, target replacement/insertion, and bind |
| O185 | `oracle-direct-bind-null-test` | `:STATUS IS NOT NULL AND DELETED_AT IS NULL` | emits an expression predicate that references only the real named bind for the direct-bind null test; the field null test is a comparison with no NULL value, with AND order and all 4 patches verified exactly |
| O186 | `oracle-nested-select-target-multi-replace-middle` | replaces the middle item of a three-target derived-table SELECT with three quoted targets | expands the replacement only at the selected inner target-list position while preserving inner/outer blocks, relations, and target order; an independent insert patch validates the inner list position |
| O187 | `oracle-merge-update-compound-rhs` | a compound assignment RHS in a MERGE UPDATE branch | branch `rhs_fields` and `rhs_values` attribute the source field, positional bind, and literal to the assignment; the source alias remains stable, and MERGE assignment replacement and insertion are verified exactly |
| O188 | `oracle-merge-insert-structured-pair-rewrite` | structured MERGE INSERT target-column and VALUES-cell rewriting with three-column lineage, independent target-column and complete-cell selectors, atomic column/value insertion and deletion, quoted identifiers, and preservation of untouched SQL bytes |
| O189 | `oracle-insert-returning-rowid-into-bind` | `INSERT ... VALUES ... RETURNING ROWID INTO :NAV_ROWID` | one `ROWID` pseudo result target links to one colon-prefixed host bind through `sink_value`; replace patches for a VALUES cell, the result target, and the output bind all preserve exact deparse output |
| O190 | `oracle-update-returning-rowid-into-bind` | `UPDATE ... RETURNING ROWID INTO :NAV_ROWID` | one `ROWID` pseudo target in `target_after` links to one colon-prefixed host bind; replace patches for the assignment, result target, and output bind all preserve exact deparse output |
| O191 | `oracle-delete-returning-rowid-into-bind` | `DELETE ... RETURNING ROWID INTO :NAV_ROWID` | one `ROWID` pseudo target in `target_before` links to one colon-prefixed host bind; replace patches for the predicate value, result target, and output bind all preserve exact deparse output |
| O192 | `oracle-merge-update-where-delete-where-conditional-insert` | a matched UPDATE has both an action `WHERE` and an attached `DELETE WHERE`, followed by a conditional INSERT | the UPDATE branch exposes both `condition_selector` and `delete_condition_selector`; the delete predicate remains part of that UPDATE branch; 5 independent patches cover assignment replacement/insertion and all three predicate values |
| O193 | `oracle-merge-delete-where-updated-target-value` | a matched UPDATE followed by `DELETE WHERE t.STATUS = 'CLOSED'` | models the delete predicate against the updated target value without creating an independent DELETE action; 3 independent patches cover assignment replacement/insertion and delete-predicate value replacement |

## ROWNUM Predicate Semantics Regression

These five final cases verify that `ROWNUM` participates in predicates only as a pseudocolumn expression and never enters `query_graph.fields`. Each predicate preserves its source operator and points to the literal or bind on the other side; compound conditions also preserve their Boolean tree, block, and DML attribution exactly. These cases do not emit session projections.

| ID | Case | SQL Shape | Coverage |
| --- | --- | --- | --- |
| `O-RN001` | `oracle-rownum-and-named-bind` | ordinary field condition with `ROWNUM <= :limit` | `AND` tree, fieldless ROWNUM expression predicate, and named-bind position |
| `O-RN002` | `oracle-rownum-derived-order-by-limit` | derived-table `ORDER BY` with outer `ROWNUM < 11` | inner and outer blocks, derived relation, star source, and literal-predicate attribution |
| `O-RN003` | `oracle-rownum-right-operand-equality` | `1 = ROWNUM` | source operator and left-literal selector when ROWNUM is the right operand |
| `O-RN004` | `oracle-rownum-greater-than-literal` | `ROWNUM > 1` | greater-than operator and other-side literal in an expression predicate |
| `O-RN005` | `oracle-delete-rownum-batch-limit` | `DELETE ... expired = 1 AND ROWNUM <= :batch_size` | DELETE target, `AND` tree, ordinary-field predicate, and ROWNUM bind-predicate attribution |

## Hierarchical Query Regression

These four final cases define the Oracle hierarchical-query boundary.
`START WITH` must precede `CONNECT BY`. Fields, values, and predicates use the
existing Query Graph arrays, while hierarchical pseudo-columns, `PRIOR`,
`NOCYCLE`, and `CONNECT_BY_ROOT` use public View fields. Each case contains
five independent patches, for 20 patches in total: 16 `replace` and 4
`insert_column` actions.

| ID | Case | SQL Shape | Coverage |
| --- | --- | --- | --- |
| O194 | `oracle-hierarchical-basic-level-bind` | named bind in `START WITH`, `CONNECT BY PRIOR`, and `LEVEL` | `start_with` / `connect_by` clauses, relationless `LEVEL` pseudo target, the `PRIOR` field occurrence, and bind attribution |
| O195 | `oracle-hierarchical-compound-connect-where-depth` | `WHERE`, `START WITH`, and compound `CONNECT BY` | semantic traversal of WHERE, START WITH, and CONNECT BY, plus field comparison, the `LEVEL` depth condition, and named binds |
| O196 | `oracle-hierarchical-connect-by-root` | `CONNECT_BY_ROOT employee_id` with `LEVEL` | expression target preservation and underlying-field attribution through an `operator/CONNECT_BY_ROOT/arg_index=0` target path |
| O197 | `oracle-hierarchical-nocycle-pseudocolumns` | `CONNECT BY NOCYCLE` with `CONNECT_BY_ISLEAF` and `CONNECT_BY_ISCYCLE` | `nocycle` on the CONNECT BY root predicate, two relationless pseudo targets, and their field links |

## Multiple RETURNING INTO Result Pairs

Oracle `RETURNING ... INTO` on `INSERT`, `UPDATE`, and `DELETE` supports
`N >= 1` result targets with exactly N colon-prefixed host binds, paired by
ordinal. O189 through O191 verify the one-pair baseline. Each of the following
three final cases verifies eight pairs plus one paired `insert_column`: the same
patch inserts the target and receiver at the same ordinal, expanding the result
to nine pairs without permitting a one-sided insertion. View uses one
`kind = "sink"` channel, and every target's `sink_value` points to the output
bind at the corresponding ordinal.

| ID | Case | DML | Verification Focus |
| --- | --- | --- | --- |
| O198 | `oracle-insert-returning-eight-target-bind-pairs` | INSERT | eight pairs; paired insertion of `tenant_id` / `:out_tenant_id` at index 0 verifies nine aligned pairs at the head |
| O199 | `oracle-update-returning-eight-target-bind-pairs` | UPDATE | complete source-order enumeration covers SET, WHERE, and all eight `INTO` binds for 11 root occurrences; paired insertion of `postal_code` / `:out_postal_code` at index 4 produces 12 occurrences while keeping all nine ordinals aligned |
| O200 | `oracle-delete-returning-eight-target-bind-pairs` | DELETE | eight pairs; paired insertion of `tenant_id` / `:out_tenant_id` at index 8 verifies nine aligned pairs at the tail |

## Query Graph Quoted-Alias Contract

`relations[].alias_quoted_identifier` is `true` only when the relation alias is double-quote delimited. `targets[].output_quoted_identifier` is `true` when the output name comes from an explicit double-quoted alias, or inherits a double-quoted field name without an explicit alias; View JSON omits either key when its value is `false`.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| O201 | `oracle-quoted-relation-alias-and-target-output-contract` | double-quoted relation/derived aliases and output names | both quoted flags, field-name inheritance, and two output-alias patches |

## Coverage Boundary

This matrix lists only cases that parse successfully and have final View and
patch expectations. Syntax boundaries outside this executable fixture are
maintained in `doc/oracle_official_syntax_coverage.csv`.

`RETURNING ... INTO` rejects `BULK COLLECT`, receivers other than
colon-prefixed binds, and unequal target/receiver counts. A paired
`insert_column` must insert both sides in the same patch.

## Maintenance

- New Oracle support must update `tests/cases/oracle_dialect_input.json`, this matrix, and executable regression tests.
- Syntax outside the executable fixture must not be listed here as a validated case.
