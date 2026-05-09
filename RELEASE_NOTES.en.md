# v0.3.0 Release Notes

`v0.3.0` is a minor dialect-capability release for `sqlparser`. It adds
database, schema, and session-context switching support across parsing,
structured inspection, rewrite, and deparse workflows.

## Highlights

- PostgreSQL exposes SQL View output for `SET search_path`,
  `SET LOCAL search_path`, and `SET SCHEMA`.
- MySQL supports `USE db_name` default-database switching, including
  backtick-delimited database names.
- SQL Server supports `USE database_name` database-context switching and
  preserves bracket-delimited public output in deparse.
- Oracle supports `ALTER SESSION SET CURRENT_SCHEMA`,
  `ALTER SESSION SET CONTAINER`, and
  `ALTER SESSION SET CONTAINER ... SERVICE ...`.
- Context-switching statements reuse the existing SQL View JSON structure; no
  separate JSON format is introduced.
- `stmt[n].value[m]` selectors can rewrite context-switch targets and deparse
  back to the corresponding dialect SQL.
- Fixed parse/deparse boundaries for context switching in multi-statement
  inputs, avoiding exposure of internal `sqlparser_current_*` sentinel names.
- Updated dialect support docs, official syntax coverage checklists, and
  executable coverage summaries.

## Dialect Support Boundary

Current executable case matrices:

- PostgreSQL: 54 cases, 53 supported paths, and 1 invalid-SQL negative path.
- MySQL: 32 cases, 17 supported paths, and 15 explicit unsupported paths.
- Oracle: 65 cases, 46 supported paths, and 19 explicit unsupported paths.
- SQL Server: 61 base cases, 46 supported paths, and 15 explicit unsupported
  paths; the official `HOOK_ONLY` coverage matrix contains 235 cases.

## Release Validation

This release gate includes:

- `git diff --check`
- JSON fixture validation
- Linux GCC 8.3: `make test`
- Linux GCC 8.3: `make verify-ci`
- ABI export-symbol check
- CLI parse/deparse smoke coverage for MySQL, Oracle, and SQL Server
  multi-statement context switching

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- SQL View JSON: exported on demand through `sqlparser_export_view_json()`
- SQL View C structured traversal: read on demand through `sqlparser_get_view()`
  and related view APIs
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`
