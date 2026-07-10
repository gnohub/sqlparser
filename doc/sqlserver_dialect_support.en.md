# SQL Server Dialect Support

`SQLPARSER_DIALECT_SQLSERVER` provides a conversion layer from SQL Server
T-SQL to the current `sqlparser` AST model. Callers select it explicitly through
`sqlparser_parse_with_options()`; when no dialect is specified, parsing uses the
PostgreSQL grammar.

## Supported Scope

The SQL Server dialect supports common SQL forms that can be safely mapped to
the current AST. The executable case matrix defines the support boundary:

- `SELECT`, aliases, subqueries, joins, `WHERE`, and `ORDER BY`
- bracket-delimited identifiers such as `[dbo].[users]`
- SQL Server parameter placeholders such as `@id` and `@name`
- JDBC `?` parameter placeholders
- `TOP (n)`, `TOP (@param)`, `TOP ... PERCENT`, and `TOP ... WITH TIES`
  query limits, including `TOP` inside subqueries; `WITH TIES` forms require
  `ORDER BY` in the same query scope
- `OFFSET ... FETCH NEXT ... ROWS ONLY`
- public-form preservation for basic table and query hints, such as
  `WITH (NOLOCK)`, `WITH (FORCESEEK)`, and `OPTION (RECOMPILE)`
- public-form preservation for `FOR JSON PATH/AUTO` query suffixes
- `N'...'` Unicode string literals
- temporary table names such as `#active_users`
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`
- `UPDATE` and `DELETE`
- `CASE`, window functions, `UNION ALL`, `EXCEPT`, and `INTERSECT`
- mappable `MERGE`
- common DDL: `CREATE TABLE`, `ALTER TABLE ADD`, `CREATE VIEW`,
  `CREATE INDEX`, `DROP TABLE`, and `TRUNCATE TABLE`
- compatible mapping for the `IDENTITY` column property
- transaction control, `SAVE TRANSACTION`, and `GRANT / REVOKE`
- `GO` batch separators
- public-form preservation for `@@` system variables and `0x...` binary literals
- `TRY_CAST`, `TRY_CONVERT`, `CONVERT(..., style)`, `PARSE`, and `TRY_PARSE`
- ODBC `{fn ...}` scalar-function wrappers
- simple `RENAME OBJECT ... TO ...`
- common type names and functions such as `NVARCHAR`, `BIT`, `DATETIME2`,
  `ISNULL`, `GETDATE`, and `NEWID`
- `USE database_name`
- basic session and execution-environment `SET` statements such as
  `SET NOCOUNT ON`, `SET DATEFORMAT dmy`, and `SET IDENTITY_INSERT dbo.Tool ON`
- parameterized dynamic SQL through `sp_prepare`, `sp_execute`, `sp_prepexec`,
  `sp_unprepare`, and `sp_executesql`

## Explicitly Unsupported Scope

The following T-SQL-specific constructs are not silently downgraded. They
return `SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable handle:

- `TOP` combined with `OFFSET ... FETCH` in the same query scope
- `TOP ... WITH TIES` without `ORDER BY` in the same query scope
- DML `TOP`, such as `UPDATE TOP (...)`
- `OUTPUT`
- `CROSS APPLY` and `OUTER APPLY`
- `PIVOT` and `UNPIVOT`
- `FOR XML`
- `DECLARE` and ordinary `EXEC` / `EXECUTE` procedure calls
- procedure, function, and trigger definitions
- `BEGIN TRY` / `BEGIN CATCH`
- `OPENQUERY`, `OPENROWSET`, `OPENDATASOURCE`, `OPENJSON`, and `OPENXML`
- table variables such as `@table`
- `MERGE ... WHEN NOT MATCHED BY SOURCE`

## Public Output Rules

- `sqlparser_deparse()` emits the public SQL Server form and does not expose
  internal conversion details.
- `@name` and `?` parameters remain in public form in deparse and View
  JSON; internal `$1` / `$2` names are not emitted.
- `@@` system variables and `0x...` binary literals remain in SQL Server
  public form in deparse.
- SQL Server conversion functions remain visible as `TRY_CAST`,
  `TRY_CONVERT`, `CONVERT`, `PARSE`, or `TRY_PARSE` in deparse output.
- `TOP`, `TOP ... PERCENT`, `TOP ... WITH TIES`, and `OFFSET ... FETCH`
  remain visible as SQL Server syntax in deparse output.
- Table hints, `FOR JSON` suffixes, and query hints are restored as original
  public fragments in deparse output; they are not emitted as structured View
  JSON hint or JSON-suffix fields yet.
- `N'...'` Unicode strings keep the `N` prefix when the semantics can be
  preserved.
- Attributable expression fragments in View JSON use the public SQL Server
  form.
- Failed expression-fragment rewrites are not committed to the handle; the
  previous AST, parameter mapping, and deparse output remain usable.

## Regression Cases

The SQL Server support boundary is defined by:

- `tests/cases/sqlserver_dialect_input.json`
- `tests/cases/sqlserver_dialect_matrix.en.md`
- `tests/unit/test_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

The SQL Server matrix contains 459 cases: 448 supported paths and 11 explicit
unsupported paths. Of these, 241 cases provide executable coverage for official
`CURRENT` entries, and 94 cases cover basic forms of `MIXED_MODEL` official
entries.
