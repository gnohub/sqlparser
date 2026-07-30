# Dameng Dialect Case Matrix

This file records regression cases for the Dameng dialect conversion layer. The executable fixture is `tests/cases/dameng_dialect_input.json`; `tests/unit/test_dameng_dialect_case_matrix.c` verifies parsing, View JSON, deparse output, and error codes.

## Matrix Counts and Session Regression

The fixture contains 162 cases: 150 expect success and 12 expect failure.
Statement-level `expect.session` appears in 34 cases, covering `D002`,
`D003`, `D003Q`, `D026`, `D089` through `D095`, and the `DM-*` session cases.
All 34 contain at least one non-null session expectation.

When an `expect.session` array is present, the matrix test requires one entry
per statement. A non-null entry is matched against the corresponding session
projection, including its action, item scope, target kind, name, and value
fields; `null` asserts that no session projection is emitted. For fixture
cases that expect success, the test also deparses the unmodified handle and
compares the result with the input SQL byte for byte.

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
| D064 | multi-statement `?` parameters | positional `bind_position` increases globally across the full input SQL |
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

## Explicitly Unsupported Cases

The following constructs have Dameng-specific or compatibility-mode semantics. The conversion layer returns `SQLPARSER_STATUS_UNSUPPORTED` or a parse error instead of producing SQL with unreliable semantics.

| ID | Case | Reason |
| --- | --- | --- |
| DU001 | `CONNECT BY` | hierarchical query semantics need a dedicated query model |
| DU002 | `PIVOT` | table transformation semantics need a dedicated query model |
| DU003 | `RETURNING ... INTO` | non-equivalent host-variable return target |
| DU004 | DMSQL block | outside SQL statement conversion scope |
| DU010 | `CREATE PROCEDURE` | DMSQL program unit |
| DU012 | `ALTER SESSION SET CONTAINER` | Dameng does not support container session semantics |

## Maintenance

- New Dameng support must update `tests/cases/dameng_dialect_input.json`, this matrix, and executable regression tests.
- Dameng-only syntax that cannot be mapped with equivalent semantics must return `SQLPARSER_STATUS_UNSUPPORTED` or a parse error.
