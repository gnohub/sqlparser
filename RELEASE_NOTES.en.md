# v0.2.0 Release Notes

`v0.2.0` is a stable release of `sqlparser` for SQL parsing,
structured inspection, controlled rewrite, and deparse workflows.

## Highlights

- Added an Oracle dialect conversion layer for common Oracle SQL that can be
  safely mapped to the current AST.
- Added a SQL Server dialect conversion layer for common T-SQL that can be
  safely mapped to the current AST.
- Added Oracle and SQL Server examples, CLI dialect option, batch JSON dialect
  field, and full regression matrices.
- Kept dialect public output stable: deparse and SQL View JSON do not expose
  internal `$N`, `EXCEPT`, or other conversion details.
- SQL View can be consumed as JSON or through C structured traversal APIs;
  structured patch accepts only an explicit `patches` array.
- Extended expression-fragment rewrite support for Oracle binds and preserved
  handle state on failed rewrites.
- Reduced the default generated-output limit to 4 MB and removed avoidable
  resident AST and string copies from parse/deparse paths.
- Added stability tests for malformed SQL, argument validation, resource
  limits, failed-rewrite rollback, and multi-dialect public-output stability.
- Extended CI release gates with JSON fixture validation, Linux GCC
  verification, ABI checking, benchmark smoke, and source-package smoke.
- Fixed incremental rebuild invalidation for version-file changes so runtime
  version output stays aligned with `VERSION`.

## Dialect Support Boundary

See [Oracle Dialect Support](./doc/oracle_dialect_support.en.md).

The current Oracle matrix contains 58 cases: 39 supported paths and 19 explicit
unsupported paths. Explicitly unsupported Oracle-specific constructs return
`SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable handle.

See [SQL Server Dialect Support](./doc/sqlserver_dialect_support.en.md).

The current SQL Server matrix contains 56 cases: 41 supported paths and 15
explicit unsupported paths. Explicitly unsupported SQL Server-specific
constructs return `SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable
handle.

## Release Validation

This release gate includes:

- `git diff --check`
- `jq empty tests/cases/*.json`
- `make verify LOOP=5`
- `make install-smoke`
- `make abi-check`
- `make dist`
- Windows MSVC: `nmake /F Makefile.msvc test`

Linux release validation covers release/debug builds, sanitizers, valgrind,
loop regression, CLI batch, installed-library API smoke, and benchmark smoke.

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- SQL View JSON: exported on demand through `sqlparser_export_view_json()`
- SQL View C structured traversal: read on demand through `sqlparser_get_view()`
  and related view APIs
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`
