# v0.4.0 Release Notes

`v0.4.0` is a minor dialect-capability and structured-rewrite release for
`sqlparser`. It adds Dameng dialect support, expanded prepared / parameterized
SQL coverage, and generic clause-level rewrite support.

## Highlights

- Added the Dameng `SQLPARSER_DIALECT_DAMENG` conversion layer for `SET
  SCHEMA`, `MINUS`, `LIMIT`, `TOP`, binds, common DML/DDL, transactions, and
  privilege statements.
- Expanded prepared / parameterized SQL coverage for PostgreSQL, MySQL, Oracle,
  SQL Server, and Dameng, including PostgreSQL `$n`, JDBC `?`, Oracle `:name` /
  `:1`, SQL Server `@name`, and Dameng binds.
- Added generic `SELECT` output-list read, replace, insert, and delete support.
- Added generic `WHERE` condition read, set, and `AND` / `OR` append support.
- Added statement-level `clause` selectors for rewriting `select_list`,
  `where`, and `order_by` through `stmt[n].clause[m]`.
- Added Dameng CLI dialect support and fixed CLI argument-order handling so
  options such as `--dialect` and `--mode` can appear before or after inline
  SQL.

## Dialect Support Boundary

Current executable case matrices:

- PostgreSQL: 70 cases, 69 supported paths, and 1 invalid-SQL negative path.
- MySQL: 48 cases, 33 supported paths, and 15 explicit unsupported paths.
- Oracle: 78 cases, 59 supported paths, and 19 explicit unsupported paths.
- SQL Server: 76 base cases, 61 supported paths, and 15 explicit unsupported
  paths; the official `HOOK_ONLY` coverage matrix contains 235 cases.
- Dameng: 55 cases, 43 supported paths, and 12 explicit unsupported paths.

## Release Validation

This release validation includes:

- `git diff --check`
- JSON fixture validation
- Linux `make test`
- Windows MSVC `nmake /F Makefile.msvc test`
- Consistency checks between coverage summaries and executable fixtures

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- SQL View JSON: exported on demand through `sqlparser_export_view_json()`
- SQL View C structured traversal: read on demand through `sqlparser_get_view()`
  and related view APIs
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`
