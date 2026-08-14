# SQL Server Dialect Support

`SQLPARSER_DIALECT_SQLSERVER` provides parsing, structured traversal, rewrite,
and deparse support for SQL Server T-SQL. Callers select it explicitly through
`sqlparser_parse_with_options()`; when no dialect is specified, parsing uses the
PostgreSQL grammar.

## Supported Scope

The executable case matrix defines the SQL Server dialect support boundary:

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
- `INSERT VALUES`, multi-row `INSERT`, `INSERT SELECT`, set queries, and
  `DEFAULT VALUES`, with explicit or omitted `INTO`
- `UPDATE` and `DELETE`
- `OUTPUT` on `INSERT`, `UPDATE`, `DELETE`, and `MERGE`, including `INSERTED`,
  `DELETED`, source fields, `$action`, expressions, aliases, and binds
- `OUTPUT ... INTO relation [(column, ...)]`, `OUTPUT ... INTO @table_variable`,
  and ordered sink/client dual channels
- atomic insertion of one OUTPUT-target/sink-column pair at the same ordinal
  when an explicit sink-column list is initially equal in length to the OUTPUT
  target list
- nested DML where an outer `INSERT` consumes an inner DML `OUTPUT`
- `UPDATE TOP (...) ... OUTPUT` and INSERT target hints combined with `OUTPUT`
- `IF...ELSE` single-statement branches, `BEGIN...END` multi-statement branches,
  `ELSE IF`, and nested control flow; conditions support boolean expressions,
  binds, `EXISTS`, and parenthesized subqueries
- `CASE`, window functions, `UNION ALL`, `EXCEPT`, and `INTERSECT`
- mappable `MERGE`, including an independent
  `WHEN MATCHED ... THEN DELETE` action
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

## Paired OUTPUT Mutation Boundary

Paired `insert_column` applies only to a sink `OUTPUT ... INTO` channel with an
explicit non-empty sink-column list when the OUTPUT-target and sink-column
counts are strictly equal before the rewrite. The operation atomically inserts
one OUTPUT target and one sink column at the same ordinal.

SQL Server OUTPUT forms whose counts are legally unequal still parse and
deparse, but do not support this paired insertion. Client `OUTPUT` and
`OUTPUT ... INTO` without an explicit sink-column list are also outside the
paired-mutation boundary. This limits the patch operation, not parsing support
for those SQL forms.

## Explicitly Unsupported Scope

The following T-SQL-specific constructs are not silently downgraded. They
return `SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable handle:

- `TOP` combined with `OFFSET ... FETCH` in the same query scope
- `TOP ... WITH TIES` without `ORDER BY` in the same query scope
- DML `TOP` forms not listed in the executable matrix
- aggregates and subqueries in `OUTPUT`, and `INSERT EXEC` combined with
  `OUTPUT`
- `CROSS APPLY` and `OUTER APPLY`
- `PIVOT` and `UNPIVOT`
- `FOR XML`
- `DECLARE` and ordinary `EXEC` / `EXECUTE` procedure calls
- procedure, function, and trigger definitions
- `BEGIN TRY` / `BEGIN CATCH`
- standalone `BEGIN...END` blocks outside an `IF...ELSE` branch
- `OPENQUERY`, `OPENROWSET`, `OPENDATASOURCE`, `OPENJSON`, and `OPENXML`
- ordinary table-variable references, excluding an
  `OUTPUT INTO @table_variable` sink
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
  public fragments in deparse output. View JSON does not define dedicated hint
  or JSON-suffix fields.
- `N'...'` Unicode strings keep the `N` prefix when the semantics can be
  preserved.
- Attributable expression fragments in View JSON use the public SQL Server
  form.
- Control conditions and branch SQL are emitted as ordered statement units;
  View JSON `control_flow` mirrors the public read-only control structures.
- Failed expression-fragment rewrites are not committed to the handle; the
  previous AST, parameter mapping, and deparse output remain usable.

## Regression Cases

The SQL Server support boundary is defined by:

- `tests/cases/sqlserver_dialect_input.json`
- `tests/cases/sqlserver_dialect_matrix.en.md`
- `tests/unit/test_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

The SQL Server matrix contains 625 cases, all with `status = "final"`, and 1870
independent patches. Three cases respectively verify INSERT, UPDATE, and DELETE
with 8↔8 OUTPUT-target/sink-column pairs and atomic head, middle, and tail
insertions that produce 9↔9 pairs.
