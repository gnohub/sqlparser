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
- `TOP (n)` and `TOP (@param)` query limits
- `OFFSET ... FETCH NEXT ... ROWS ONLY`
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
- `USE database_name` database context switching
- parameterized dynamic SQL through `sp_prepare`, `sp_execute`, `sp_prepexec`,
  `sp_unprepare`, and `sp_executesql`

## Explicitly Unsupported Scope

The following T-SQL-specific constructs are not silently downgraded. They
return `SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable handle:

- `TOP ... PERCENT` and `TOP ... WITH TIES`
- `TOP` combined with `OFFSET ... FETCH` in the same query scope
- nested `SELECT TOP`
- DML `TOP`, such as `UPDATE TOP (...)`
- `OUTPUT`
- table and query hints such as `WITH (NOLOCK)` and `OPTION (...)`
- `CROSS APPLY` and `OUTER APPLY`
- `PIVOT` and `UNPIVOT`
- `FOR XML` and `FOR JSON`
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
- `TOP (n)` and `OFFSET ... FETCH` remain visible as SQL Server syntax in
  deparse output.
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

The SQL Server matrix contains 334 cases: 319 supported paths and 15 explicit
unsupported paths. Of these, 235 cases come from official `HOOK_ONLY` coverage
items.
