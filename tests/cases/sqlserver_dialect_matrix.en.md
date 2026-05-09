# SQL Server Dialect Case Matrix

This file records regression cases for the SQL Server dialect conversion layer. The executable fixture is `tests/cases/sqlserver_dialect_input.json`; `tests/unit/test_sqlserver_dialect_case_matrix.c` verifies parsing, SQL View JSON, deparse output, and error codes.

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
| S044 | `USE [database]` | database context switching, bracket-delimited database name, and public value fragment |
| S045 | `USE database` | official basic `USE database_name` form |
| S046 | `USE ...; SELECT ...` | database switching and following query remain separate in multi-statement input |

## Explicitly Unsupported Cases

The following constructs have SQL Server-specific semantics. The conversion layer returns `SQLPARSER_STATUS_UNSUPPORTED` instead of producing SQL with unreliable semantics.

| ID | Case | Reason |
| --- | --- | --- |
| SU001 | `TOP ... PERCENT` | percentage limit semantics cannot be mapped equivalently |
| SU002 | `TOP ... WITH TIES` | tie-preserving semantics cannot be mapped equivalently |
| SU003 | `OUTPUT` | DML output streams require a SQL Server-specific model |
| SU004 | table hint | table access hints cannot be safely downgraded |
| SU005 | `CROSS APPLY` | APPLY semantics differ from ordinary JOIN |
| SU006 | `PIVOT` | table transformation requires a dedicated AST |
| SU007 | `FOR JSON` | result formatting is outside the current structural model |
| SU008 | `OPTION (...)` | query hints cannot be safely downgraded |
| SU009 | `DECLARE` | variable declarations belong to T-SQL batch semantics |
| SU010 | `EXEC` | procedure execution is outside SQL structure rewrite scope |
| SU011 | `CREATE PROCEDURE` | procedure definitions require a T-SQL program-unit model |
| SU015 | table variable | table-variable scope belongs to T-SQL batch semantics |
| SU016 | `MERGE ... BY SOURCE` | SQL Server-specific merge branch semantics |
| SU017 | `TOP` + `OFFSET/FETCH` | SQL Server does not allow this combination in the same query scope |
| SU018 | nested `TOP` | only top-level `SELECT TOP` is currently mapped bidirectionally |

## Official `HOOK_ONLY` Coverage Cases

`tests/cases/sqlserver_hook_coverage_input.json` is generated from `HOOK_ONLY`
entries in `doc/sqlserver_official_syntax_coverage.csv` and currently contains
235 cases. It covers official items that can be represented by the existing AST
and dialect hooks, including functions, types/constants, collations, and simple
`RENAME OBJECT` forms.

## Maintenance

- New SQL Server support must update `tests/cases/sqlserver_dialect_input.json`, `tests/cases/sqlserver_hook_coverage_input.json`, this matrix, and executable regression tests.
- SQL Server-only syntax that cannot be mapped with equivalent semantics must return `SQLPARSER_STATUS_UNSUPPORTED`.
