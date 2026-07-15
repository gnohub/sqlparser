# SQL Server Dialect Case Matrix

This file records regression cases for the SQL Server dialect conversion layer. The executable fixture is `tests/cases/sqlserver_dialect_input.json`; `tests/unit/test_sqlserver_dialect_case_matrix.c` verifies parsing, View JSON, deparse output, and error codes. The current fixture contains 546 cases: 517 supported paths and 29 error or explicitly unsupported paths.

## Supported Cases

| ID | Case | Coverage |
| --- | --- | --- |
| S001 | bracket identifiers + `@` parameter | `[schema].[table]`, column names, predicate parameter conversion and restoration |
| S002 | `SELECT TOP (n)` | bidirectional `TOP` and core `LIMIT` conversion |
| S003 | `OFFSET ... FETCH` | SQL Server pagination syntax |
| S004 | CTE | `WITH` query, CTE names, and inner predicate-column extraction |
| S005 | `JOIN` + parameter | multi-table join, join columns, predicate columns, and parameter restoration |
| S006 | `LEFT JOIN` | outer join, aliases, and join-column extraction |
| S007 | `INSERT ... VALUES` + parameter | inserted-column extraction and `@` parameter restoration |
| S008 | multi-row `INSERT ... VALUES` | multi-row value lists |
| S009 | `INSERT ... SELECT` | target table, source table, inserted columns, and selected columns |
| S010 | `UPDATE` + multiple assignments | updated columns, predicate columns, and parameter restoration |
| S011 | `DELETE` + predicate | conditional delete |
| S012 | `N'...'` Unicode string | Unicode string prefix preservation |
| S013 | JDBC `?` parameter | positional parameter conversion and restoration |
| S014 | temporary table | `#temp` table-name conversion |
| S015 | common functions | `ISNULL`, `GETDATE`, and `NEWID` |
| S016 | `ROW_NUMBER() OVER` | window function |
| S017 | `CASE` expression | conditional expression |
| S018 | `UNION ALL` | set query |
| S019 | `EXCEPT` | set query |
| S020 | `INTERSECT` | set query |
| S021 | `CREATE TABLE` + `IDENTITY` | table creation, common types, and identity column property |
| S022 | `CREATE VIEW` | view definition |
| S023 | `ALTER TABLE ... ADD` | add column |
| S024 | `CREATE INDEX` | create index |
| S025 | `DROP TABLE IF EXISTS` | table drop |
| S026 | `TRUNCATE TABLE` | table truncation |
| S027 | transaction control | `BEGIN TRANSACTION` and `COMMIT TRANSACTION` |
| S028 | `SAVE TRANSACTION` | savepoint-compatible mapping |
| S029 | `GRANT` / `REVOKE` | privilege statements |
| S030 | `GO` separator | batch separator converted to multiple statements |
| S031 | numeric and temporal types | `BIGINT`, `DECIMAL`, and `DATETIME2` |
| S032 | `IN` + multiple parameters | multiple `@` parameters in a predicate list |
| S033 | `CAST(... AS DATE)` | cast expression |
| S034 | identifiers with spaces | spaces inside bracket-delimited identifiers |
| S035 | `MERGE` | basic merge statement |
| S036 | `TOP (@param)` | parameter conversion and restoration inside `TOP` expressions |
| S037 | CTE + main-query `TOP` | `TOP` restoration on the main `SELECT` |
| S038 | repeated Unicode literal | restore the Unicode prefix only for original `N'...'` strings |
| S039 | `@@` system variable | public-form preservation for system variables |
| S040 | `0x...` binary literal | public-form preservation for binary literals |
| S041 | `CONVERT(..., style)` | bidirectional mapping for SQL Server conversion functions |
| S042 | unsupported keywords in string | `OUTPUT`, `@table`, and `EXEC` inside strings do not trigger unsupported |
| S043 | unsupported keywords in comment | `OUTPUT` inside comments does not trigger unsupported |
| S043Q | unsupported keywords in protected identifiers | `OUTPUT`, `EXEC`, and `PIVOT` inside bracket-delimited identifiers do not trigger unsupported |
| S044 | `USE [database]` | database context switching, bracket-delimited database name, and public value fragment |
| S045 | `USE database` | official basic `USE database_name` form |
| S046 | `USE ...; SELECT ...` | database switching and following query remain separate in multi-statement input |
| S047 | `EXEC sp_prepare` | parameterized statement preparation with handle, parameter definition, and SQL text preserved |
| S048 | `EXEC sp_execute` | prepared-handle execution with bound arguments preserved |
| S049 | `EXEC sp_prepexec` | combined prepare + execute call with SQL text and bound arguments preserved |
| S050 | `EXEC sp_unprepare` | prepared-handle release |
| S051 | `EXEC sp_executesql` | parameterized dynamic SQL execution with SQL text, parameter definition, and values preserved |
| S052 | query with multiple `@` parameters | multiple named parameters in query predicates |
| S053 | `IN` + multiple `@` parameters | multiple named parameters in predicate lists |
| S054 | `TOP (@param)` + `LIKE @param` | parameter restoration in TOP and predicate expressions |
| S055 | `INSERT ... VALUES` + multiple `@` parameters | insert columns and named-parameter value lists |
| S056 | multi-row `INSERT ... VALUES` + `?` | multi-row JDBC-style parameterized insert |
| S057 | `UPDATE ... SET ... WHERE ... = ?` | updated columns, predicate columns, and positional parameters |
| S058 | `DELETE ... WHERE ... = ?` | conditional delete and positional parameters |
| S059 | `EXEC sp_prepare` + INSERT | prepared insert SQL text and parameter definition |
| S060 | `EXEC sp_execute` + named values | prepared handle execution and named argument values |
| S061 | `EXEC sp_executesql` + UPDATE | parameterized dynamic UPDATE SQL text and argument values |
| S062 | `TOP` + direct column + `@` parameter | TOP query, direct output field, WHERE bind, and ORDER BY attribution |
| S063 | `+` expression output | ordinary expression `target_path`, operator name, and output-item attribution |
| S064 | `CASE` expression output | `target_path` attribution for fields inside `CASE WHEN` |
| S065 | `GROUP BY` + `HAVING` + `ORDER BY` | aggregate output and non-output clause attribution |
| S066 | `UPDATE` + multiple `@` parameters | update/where clauses, bind fields, and null values |
| S067 | `BETWEEN` + multiple `@` parameters | multiple named parameters and field-value attribution in `BETWEEN` predicates |
| S068 | `NOT IN` + multiple `@` parameters | multiple named parameters and field-value attribution in negated `IN` predicates |
| S069 | `NOT BETWEEN` + multiple `@` parameters | multiple named parameters and field-value attribution in negated `BETWEEN` predicates |
| S070 | `NOT LIKE` + multiple `@` parameters | named parameter, field-level operator, and keyword attribution in negated `LIKE` predicates |
| S071 | `DISTINCT` + `LIKE` parameter | DISTINCT projection, LIKE named parameter, and field attribution |
| S072 | `DELETE ... IN` + multiple `@` parameters | conditional delete, collection parameters, and field operator |
| S073 | `UPDATE ... EXISTS` | subquery predicate, correlated fields, and SET parameter |
| S074 | columnless `INSERT` | columnless insert, row cells, positional parameters, and null column names |
| S075 | derived-table filter | inner/outer WHERE clauses, derived-table alias, and named parameters |
| S076 | `JSON_VALUE` projection | SQL Server JSON function and WHERE parameter |
| S077 | `ORDER BY 1` | ordinal sort item and projection-order related syntax |
| S078 | `OFFSET/FETCH` + `@` parameters | named parameters in pagination clauses |
| S079 | `LEFT JOIN` + `alias.*` | qualified star, JOIN/ON fields, and WHERE parameter |
| S080 | `CREATE VIEW` + aggregate JOIN | view creation, JOIN predicates, and GROUP BY aggregation |
| S081 | `TOP (?)` + WHERE `?` | positional parameter in `TOP` participates in the global bind sequence |
| S082 | multi-statement `?` parameters | positional `bind_position` increases globally across the full input SQL |
| S083 | `sqlserver-select-derived-query-graph` | `query_graph` lineage mapping from derived-table fields to inner base-table fields and `output_name` |
| S084 | `MERGE` + `?` parameters | MERGE target/source relations, UPDATE assignment, INSERT values, and bind mapping |
| S085 | `sqlserver-field-match-kind-direct-and-expression` | direct-field predicate plus function-wrapped field predicate | `query_graph.values[].field_match_kind` distinguishes `direct_field` from `expression_field` |
| S086 | `sqlserver-expression-field-case-expression-value` | CASE returns a field and compares with a bind | CASE expression fields emit `expression_field` value relations |
| S087 | `sqlserver-expression-field-multi-field-expression-value` | `CONCAT(secret, id)` and `secret + id` compared with binds | Fields inside the expression keep separate `expression_field` value relations |
| S088 | `sqlserver-expression-field-value-side-expression` | field compared with function, concatenation, and CAST value-side expressions | value-side expressions emit `kind=expression` instead of direct binds |
| S089 | `sqlserver-expression-field-dml-expression-values` | INSERT/UPDATE expression assignments | DML cells and assignments emit `kind=expression` |
| S090 | `sqlserver-update-named-bind-rhs-crypto-source` | `UPDATE ... SET protected = @name` | protected-field UPDATE SET right-hand named parameter |
| S091 | `sqlserver-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = @name1, protected2 = @name2` | multiple protected-field SET binds, field attribution, and global bind positions |
| S092 | `sqlserver-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | SQL Server literal ESCAPE is emitted in `values[].like_escape` |
| S093 | `sqlserver-not-like-escape-named-bind` | `NOT LIKE @pattern ESCAPE @escape_char` | named pattern and escape parameters keep public SQL and global positions |
| S094 | `sqlserver-like-escape-question-bind` | `LIKE ? ESCAPE ?` | structured output for JDBC-style positional pattern and escape parameters |
| S095 | `sqlserver-like-without-explicit-escape` | `LIKE @pattern` | `like_escape` is omitted when ESCAPE is not explicit |
| S096 | `sqlserver-bitwise-binary-operators` | `&`, `|`, `^` | bitwise expressions, output `target_path`, and WHERE bind attribution |
| S097 | `sqlserver-bitwise-not-operator` | `~column` | unary bitwise-not output expression |
| S098 | `sqlserver-not-greater-less-comparison` | `!>`, `!<` | field-value relationships for SQL Server negated comparison operators |
| S099 | `sqlserver-string-concat-pipes` | `first_name || last_name` | pipe concatenation expression output and field attribution |
| S100 | `sqlserver-like-bracket-wildcards` | `LIKE 'A[^b]_%'` | SQL Server LIKE bracket wildcard retained as the public pattern literal |
| S101 | `sqlserver-at-time-zone-expression` | `AT TIME ZONE` | time-zone expression compared as an expression-side value |
| S102 | `sqlserver-is-distinct-from-bind` | `IS DISTINCT FROM @bind` | `IS DISTINCT FROM` is not misclassified as a table variable and keeps bind attribution |
| S103 | `TOP ... PERCENT` | `SELECT TOP (10) PERCENT ...` | parses TOP percentage form and preserves public deparse output |
| S104 | `TOP ... WITH TIES` | `SELECT TOP (10) WITH TIES ...` | parses tie-preserving TOP form and preserves public deparse output |
| S105 | `TOP ... PERCENT WITH TIES` | `SELECT TOP (10) PERCENT WITH TIES ...` | parses the official combined TOP form and preserves public output |
| S106 | `WITH (NOLOCK)` table hint | `SELECT ... FROM table WITH (NOLOCK)` | table-hint source text is kept in dialect-private state and restored in public deparse SQL |
| S107 | `OPTION (RECOMPILE)` query hint | `SELECT ... OPTION (RECOMPILE)` | query-hint source text is kept in dialect-private state and restored in public deparse SQL |
| S108 | `FOR JSON PATH` | `SELECT ... FOR JSON PATH` | JSON output suffix text is kept in dialect-private state while core View JSON still exposes table and field attribution |
| S109 | nested `TOP` | `SELECT ... FROM (SELECT TOP (...) ...)` | `TOP` in a subquery scope is converted and restored independently |
| S110 | `FOR JSON` + `OPTION` | `SELECT ... FOR JSON PATH ... OPTION (...)` | multiple query suffixes are restored in SQL Server public order |
| S111 | outer and inner `TOP` | `SELECT TOP (...) ... FROM (SELECT TOP (...) ...)` | multiple `TOP` scopes are converted and restored together |
| S112 | second statement with `FOR JSON` | `SELECT ...; SELECT ... FOR JSON AUTO` | `FOR JSON` suffixes are restored by original statement ordinal, not attached to previous statements |

## INSERT And OUTPUT Cases

| ID | Case | Coverage |
| --- | --- | --- |
| SU003 | INSERT client `OUTPUT` | `INSERTED` field, client channel, and public SQL restoration |
| SH336 | INSERT set query with omitted `INTO` | original `UNION ALL` input, aliases, and Unicode literals |
| SH337 | INSERT SELECT with explicit `INTO` | source query and client channel |
| SH338 | single-row VALUES with omitted `INTO` | INSERT target and result field |
| SH339 | multi-row VALUES with omitted `INTO` | multiple rows and result field |
| SH340 | `DEFAULT VALUES` | result field without an explicit target-column list |
| SH341 | CTE + INSERT SELECT | CTE source block and result channel |
| SH342 | basic INSERT `OUTPUT` | original `SU003` statement shape |
| SH343 | `INSERTED.*` | target-after star reference |
| SH344 | expression and alias | OUTPUT target expression patch entry point |
| SH345 | multiple targets and bind | target order, field references, and bind |
| SH346 | `OUTPUT ... INTO table` | sink channel and sink relation |
| SH347 | explicit sink columns | sink-column selectors |
| SH348 | table-variable sink | `OUTPUT INTO @table_variable` |
| SH349 | sink/client dual channels | ordered channels with separate target lists |
| SH350 | UPDATE before/after | `DELETED` and `INSERTED` references |
| SH351 | UPDATE source field | target-after and source references |
| SH352 | DELETE before | `DELETED` reference |
| SH353 | DELETE JOIN source field | DELETE target and source references |
| SH354 | MERGE `$action` | action target and public SQL restoration |
| SH355 | all MERGE reference kinds | `$action`, before, after, and source |
| SH356 | nested DML table source | parent/child DML and inner result channel |
| SH357 | UPDATE TOP + OUTPUT | DML TOP combined with a result channel |
| SH358 | INSERT target hint + OUTPUT | hint and result-channel restoration |
| SH359 | OUTPUT keywords in a string | protected text does not trigger syntax recognition |
| SH360 | OUTPUT keywords in comments | comment boundaries and trailing-comment restoration |
| SH361 | OUTPUT keywords in identifiers | bracket-identifier boundaries |
| SH362 | OUTPUT text in a source subquery | nested scope and protected text |
| SH363 | source-table hint in INSERT SELECT | the source hint is not restored on the INSERT target |
| SH364 | target and source hints with OUTPUT | both hints are restored at their recorded anchors |
| SH365 | delimited reserved-word column in OUTPUT | `INSERTED.[select]` is parsed as a column reference |
| SH366 | equivalent delimited DELETE alias | differently delimited and escaped forms resolve to one target relation |
| SH367 | multi-statement OUTPUT channels | INSERT, UPDATE, and DELETE result channels remain associated with their statements |
| SH368 | Official IF EXISTS shape | Query condition, table hint, Unicode literal, and two branches |
| SH369 | IF without ELSE | One conditional branch |
| SH370 | Literal Boolean condition | Values and predicate output without a field |
| SH371 | Semicolon-delimited BEGIN blocks | Ordered multi-statement branches |
| SH372 | Newline-delimited BEGIN blocks | Statement boundaries without semicolons |
| SH373 | Dangling ELSE | ELSE binds to the nearest unmatched IF |
| SH374 | ELSE IF chain | Nested IF in the else branch |
| SH375 | NOT EXISTS | Negated query condition and bind |
| SH376 | Scalar-subquery condition | Parenthesized SELECT, aggregate, and bind |
| SH377 | Nested Boolean condition | AND, OR, NOT predicate tree and three binds |
| SH378 | Function condition | COALESCE arguments and values |
| SH379 | CASE condition | CASE expression and values |
| SH380 | UPDATE/INSERT OUTPUT branches | OUTPUT state remains local to each control unit |
| SH381 | DELETE/MERGE OUTPUT branches | DELETE row image, MERGE `$action`, and both row images |
| SH382 | DDL branches | DROP IF EXISTS and CREATE TABLE |
| SH383 | Transaction branches | System variable, COMMIT, and ROLLBACK |
| SH384 | Semicolon-delimited root statements | Root order before and after IF |
| SH385 | Newline-delimited root statements | Root boundaries without semicolons |
| SH386 | Protected text | Control keywords in strings, comments, and delimited identifiers |
| SH387 | Newline UNION ALL | A set query is not split into multiple branch items |
| SH388 | Newline table hint | `WITH (NOLOCK)` is not treated as a new statement |
| SH389 | Three nested IF levels | Multi-level control nodes and branch order |
| SH390 | IF after DROP USER IF EXISTS | Disambiguation between DDL IF and control IF |
| SH391 | NULL/BETWEEN/IN condition | Common condition operators and binds |
| SH392 | Right-side scalar subquery | Official parenthesized query condition and cross-block binds |
| SH393 | CTE branch | Semicolon-prefixed CTE inside a BEGIN block |
| SH394 | Optional BEGIN/END semicolons | Official block semicolon shape |
| SH395 | IF after DROP TABLE IF EXISTS | DDL-to-control boundary without a semicolon |
| SH396 | IF after DROP TABLE | DDL-to-control boundary without `IF EXISTS` or a semicolon |
| SH397 | IF EXISTS control after DROP TABLE | Disambiguation between a complete DROP target and a control condition |
| SH398 | IF after multiline DROP TABLE IF EXISTS | Disambiguation between a DROP condition clause and following control flow |
| SH399 | CTE UPDATE branch | Keeps CTE and multiline UPDATE/SET in one leaf statement |
| SH400 | CTE DELETE branch | Keeps CTE and multiline DELETE in one leaf statement |
| SH401 | CTE INSERT branch | Keeps CTE, INSERT, and its source SELECT in one leaf statement |
| SH402 | CTE MERGE branch | Keeps CTE and complete MERGE actions in one leaf statement |
| SH403 | CREATE VIEW CTE branch | Keeps a view definition and its multiline CTE in one leaf statement |

## Error And Explicitly Unsupported Cases

These cases verify parse errors or explicit unsupported results and do not return a usable handle.

| ID | Case | Reason |
| --- | --- | --- |
| SU001 | `TOP ... WITH TIES` without `ORDER BY` | SQL Server requires `WITH TIES` to be used with `ORDER BY` |
| SU002 | `TOP ... PERCENT WITH TIES` without `ORDER BY` | SQL Server requires `WITH TIES` to be used with `ORDER BY` |
| SU005 | `CROSS APPLY` | APPLY semantics differ from ordinary JOIN |
| SU006 | `PIVOT` | table transformation requires a dedicated AST |
| SU009 | `DECLARE` | variable declarations belong to T-SQL batch semantics |
| SU010 | ordinary procedure `EXEC` | non-prepared/dynamic-SQL system procedure execution is outside SQL structure rewrite scope |
| SU011 | `CREATE PROCEDURE` | procedure definitions require a T-SQL program-unit model |
| SU015 | table variable | table-variable scope belongs to T-SQL batch semantics |
| SU016 | `MERGE ... BY SOURCE` | SQL Server-specific merge branch semantics |
| SU017 | `TOP` + `OFFSET/FETCH` | SQL Server does not allow this combination in the same query scope |
| SU018 | empty OUTPUT target | returns a parse error |
| SU019 | trailing comma in OUTPUT targets | returns a parse error |
| SU020 | `OUTPUT INTO` without a sink | returns a parse error |
| SU021 | invalid dual-channel order | a sink channel cannot follow a client channel |
| SU022 | `DELETED` in INSERT | returns unsupported |
| SU023 | `INSERTED` in DELETE | returns unsupported |
| SU024 | `$action` outside MERGE | returns unsupported |
| SU025 | aggregate in OUTPUT | returns unsupported |
| SU026 | subquery in OUTPUT | returns unsupported |
| SU027 | INSERT EXEC + OUTPUT | returns unsupported |
| SU028 | IF without a condition | returns a syntax error |
| SU029 | IF without a branch statement | returns a syntax error |
| SU030 | Orphan ELSE | returns a syntax error |
| SU031 | Empty BEGIN/END | returns a syntax error |
| SU032 | Unterminated BEGIN/END | returns a syntax error |
| SU033 | Unparenthesized condition SELECT | returns a syntax error |
| SU034 | ELSE without a branch statement | returns a syntax error |
| SU035 | GO inside control flow | returns unsupported without silently rewriting the batch boundary |
| SU036 | Unsupported branch leaf | returns the leaf syntax unsupported status |

## DML Source-Field Lineage Cases

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| SH236 | `sqlserver-update-from-source-field-graph` | `UPDATE ... SET target = source.column FROM ...` | `source_field` for base-table source fields on the right side of UPDATE FROM assignments, plus `right_field` for JOIN/WHERE field comparisons |
| SH237 | `sqlserver-insert-select-source-block-graph` | `INSERT ... SELECT ... FROM ...` | target columns, source block, and source-field lineage for INSERT SELECT |
| SH238 | `sqlserver-merge-source-target-graph` | `MERGE INTO ... USING ...` | source/target field lineage and source-field expressions for MERGE |
| SH254 | `sqlserver-select-or-predicate-order-by-lineage` | `WHERE field = @bind OR field = @bind ORDER BY ...` | OR predicate trees keep both comparison children, binds, and independent ORDER BY field attribution |

## Mixed-Model Basic-Form Cases

The following cases cover basic forms of `MIXED_MODEL` entries. Full official
syntax remains marked as requiring a SQL Server-specific model in
`doc/sqlserver_official_syntax_coverage.csv`.

| Case ID | Case Name | Statement Shape | Validation Focus |
| --- | --- | --- | --- |
| SH239 | `sqlserver-mixed-create-database-basic` | `CREATE DATABASE [appdb]` | basic database creation |
| SH240 | `sqlserver-mixed-drop-database-basic` | `DROP DATABASE [appdb]` | basic database drop |
| SH241 | `sqlserver-mixed-create-schema-basic` | `CREATE SCHEMA [audit]` | basic schema creation |
| SH242 | `sqlserver-mixed-drop-schema-basic` | `DROP SCHEMA [audit]` | basic schema drop |
| SH243 | `sqlserver-mixed-create-role-basic` | `CREATE ROLE [app_role]` | basic role creation |
| SH244 | `sqlserver-mixed-drop-role-basic` | `DROP ROLE [app_role]` | basic role drop |
| SH245 | `sqlserver-mixed-create-sequence-basic` | `CREATE SEQUENCE ... START WITH ...` | basic sequence creation |
| SH246 | `sqlserver-mixed-alter-sequence-basic` | `ALTER SEQUENCE ... RESTART WITH ...` | basic sequence alteration |
| SH247 | `sqlserver-mixed-drop-sequence-basic` | `DROP SEQUENCE ...` | basic sequence drop |
| SH248 | `sqlserver-mixed-drop-view-basic` | `DROP VIEW [dbo].[v_users]` | basic view drop |
| SH249 | `sqlserver-mixed-drop-statistics-basic` | `DROP STATISTICS ...` | basic statistics drop |
| SH250 | `sqlserver-mixed-select-into-basic` | `SELECT ... INTO ... FROM ...` | basic `SELECT INTO` target table |
| SH251 | `sqlserver-mixed-contains-basic` | `CONTAINS(column, @term)` | basic full-text predicate and bind output |
| SH252 | `sqlserver-mixed-freetext-basic` | `FREETEXT(column, @term)` | basic full-text predicate and bind output |
| SH253 | `sqlserver-regexp-like-function-predicate` | `REGEXP_LIKE(column, @pat)` | function predicates reuse `fields/values/predicates` for fields, binds, and expression predicates |
| SH255 | `sqlserver-mixed-create-table-as-select-basic` | `CREATE TABLE ... AS SELECT ...` | basic CTAS parsing, source query, and deparse |
| SH256 | `sqlserver-mixed-aliasing-basic` | `SELECT expression AS alias, expression alias FROM ...` | basic SELECT column aliases and table aliases |
| SH257 | `sqlserver-mixed-subquery-basic` | `WHERE column IN (SELECT ... FROM ...)` | subquery predicate fields, binds, and source table attribution |
| SH258 | `sqlserver-mixed-alter-table-add-column-basic` | `ALTER TABLE ... ADD column type` | basic add-column DDL |
| SH259 | `sqlserver-mixed-alter-table-add-constraint-basic` | `ALTER TABLE ... ADD CONSTRAINT ... PRIMARY KEY` | basic add-table-constraint DDL |
| SH260 | `sqlserver-mixed-drop-type-basic` | `DROP TYPE [schema].[type]` | basic type-drop DDL |
| SH261 | `sqlserver-mixed-create-user-basic` | `CREATE USER [user]` | basic user creation DDL |
| SH262 | `sqlserver-mixed-drop-user-if-exists-basic` | `DROP USER IF EXISTS [user]` | basic user drop DDL and public `DROP USER` restoration |
| SH263 | `sqlserver-mixed-drop-role-drop-user-ordinal` | `DROP ROLE ...; DROP USER ...` | independent occurrence-based restoration for `DROP ROLE` and `DROP USER` in multi-statement input |
| SH264 | `sqlserver-mixed-create-user-without-login` | `CREATE USER [user] WITHOUT LOGIN` | SQL Server user without login mapping |
| SH265 | `sqlserver-mixed-create-user-for-login` | `CREATE USER [user] FOR LOGIN [login]` | SQL Server user mapped to a login |
| SH266 | `sqlserver-mixed-create-user-from-external-provider` | `CREATE USER [user] FROM EXTERNAL PROVIDER` | SQL Server Entra provider user form |
| SH267 | `sqlserver-mixed-create-user-with-options` | `CREATE USER ... WITH DEFAULT_SCHEMA ...` | SQL Server user option tail preservation |
| SH268 | `sqlserver-mixed-create-user-for-certificate` | `CREATE USER ... FOR CERTIFICATE ...` | SQL Server certificate user form |
| SH269 | `sqlserver-mixed-create-user-for-asymmetric-key` | `CREATE USER ... FOR ASYMMETRIC KEY ...` | SQL Server asymmetric key user form |
| SH270 | `sqlserver-mixed-alter-role-add-member` | `ALTER ROLE ... ADD MEMBER ...` | SQL Server role membership add |
| SH271 | `sqlserver-mixed-alter-role-drop-member` | `ALTER ROLE ... DROP MEMBER ...` | SQL Server role membership drop |
| SH272 | `sqlserver-mixed-alter-role-with-name` | `ALTER ROLE ... WITH NAME = ...` | SQL Server role rename |
| SH273 | `sqlserver-mixed-alter-schema-transfer-object` | `ALTER SCHEMA ... TRANSFER schema.object` | SQL Server schema object transfer |
| SH274 | `sqlserver-mixed-alter-schema-transfer-type` | `ALTER SCHEMA ... TRANSFER TYPE::schema.type` | SQL Server schema type transfer |
| SH275 | `sqlserver-mixed-create-role-authorization` | `CREATE ROLE ... AUTHORIZATION ...` | SQL Server role owner assignment |
| SH276 | `sqlserver-mixed-alter-user-name` | `ALTER USER ... WITH NAME = ...` | SQL Server user rename |
| SH277 | `sqlserver-mixed-alter-user-options` | `ALTER USER ... WITH DEFAULT_SCHEMA ...` | SQL Server user option update |
| SH278 | `sqlserver-mixed-alter-user-login` | `ALTER USER ... WITH LOGIN = ...` | SQL Server user login remapping |
| SH279 | `sqlserver-mixed-alter-user-external-provider` | `ALTER USER ... FROM EXTERNAL PROVIDER ...` | SQL Server external provider user form |
| SH280 | `sqlserver-mixed-alter-authorization-object` | `ALTER AUTHORIZATION ON OBJECT::... TO ...` | SQL Server object owner transfer |
| SH281 | `sqlserver-mixed-alter-authorization-schema-owner` | `ALTER AUTHORIZATION ... TO SCHEMA OWNER` | SQL Server schema-owner target |
| SH282 | `sqlserver-mixed-alter-authorization-schema` | `ALTER AUTHORIZATION ON SCHEMA::... TO ...` | SQL Server schema owner transfer |
| SH283 | `sqlserver-mixed-create-schema-authorization` | `CREATE SCHEMA ... AUTHORIZATION ...` | SQL Server schema owner assignment |
| SH284 | `sqlserver-mixed-drop-schema-if-exists` | `DROP SCHEMA IF EXISTS ...` | SQL Server conditional schema drop |
| SH285 | `sqlserver-mixed-create-application-role` | `CREATE APPLICATION ROLE ... WITH PASSWORD ...` | SQL Server application role creation |
| SH286 | `sqlserver-mixed-alter-application-role-name` | `ALTER APPLICATION ROLE ... WITH NAME = ...` | SQL Server application role rename |
| SH287 | `sqlserver-mixed-alter-application-role-options` | `ALTER APPLICATION ROLE ... WITH PASSWORD ...` | SQL Server application role option update |
| SH288 | `sqlserver-mixed-drop-application-role` | `DROP APPLICATION ROLE ...` | SQL Server application role drop |
| SH289 | `sqlserver-mixed-create-synonym` | `CREATE SYNONYM ... FOR ...` | SQL Server synonym creation |
| SH290 | `sqlserver-mixed-drop-synonym-if-exists` | `DROP SYNONYM IF EXISTS ...` | SQL Server conditional synonym drop |
| SH291 | `sqlserver-mixed-create-type-alias` | `CREATE TYPE ... FROM ...` | SQL Server alias type creation |
| SH292 | `sqlserver-mixed-alter-database-compatibility-level` | `ALTER DATABASE ... SET COMPATIBILITY_LEVEL = ...` | SQL Server database compatibility level update |
| SH293 | `sqlserver-mixed-drop-index-if-exists-on-object` | `DROP INDEX IF EXISTS ... ON ...` | SQL Server conditional index drop |
| SH294 | `sqlserver-mixed-update-statistics-fullscan` | `UPDATE STATISTICS ... WITH FULLSCAN` | SQL Server statistics update |
| SH295-SH333 | `sqlserver-set-*` | `SET ...` | basic SQL Server session/execution-environment `SET` statements, public SQL restoration, and internal sentinel hiding |
| SH334 | `sqlserver-table-hints-join-alias` | multi-table JOIN with `WITH (NOLOCK)` / `WITH (FORCESEEK)` | restores table hints by table-source order while keeping field and JOIN attribution unchanged |
| SH335 | `sqlserver-query-hints-multiple` | `OPTION (RECOMPILE, USE HINT(...))` | restores multi-argument query hints from the original SQL fragment while keeping bind attribution unchanged |

## Official Hook Coverage Cases

`tests/cases/sqlserver_dialect_input.json` includes 241 cases generated from
official `CURRENT` entries. These cases cover official items that can be
represented by the existing AST and dialect hooks, including functions,
types/constants, collations, official `TOP` forms, and simple `RENAME OBJECT`
forms. Table hints and query hints are covered as `MIXED_MODEL` basic forms;
fully structured hint semantics remain a dedicated-model boundary.

## Maintenance

- New SQL Server support must update `tests/cases/sqlserver_dialect_input.json`, this matrix, and executable regression tests.
- SQL Server-only syntax that cannot be mapped with equivalent semantics must return `SQLPARSER_STATUS_UNSUPPORTED`.
