# v2.6.0 Release Notes

`v2.6.0` adds four explicit Vastbase compatibility modes. Callers can select
the Oracle, MySQL, PostgreSQL, or SQL Server compatibility entry directly; the
library does not infer a mode from SQL text.

## Highlights

- Updated the public version to `2.6.0`.
- Added `SQLPARSER_DIALECT_VASTBASE_ORACLE`,
  `SQLPARSER_DIALECT_VASTBASE_MYSQL`,
  `SQLPARSER_DIALECT_VASTBASE_POSTGRESQL`, and
  `SQLPARSER_DIALECT_VASTBASE_SQLSERVER`.
- The CLI accepts `vastbase-oracle`, `vastbase-mysql`,
  `vastbase-postgresql`, and `vastbase-sqlserver`.
- In the CLI, `vastbase` is a deterministic alias for `vastbase-oracle`.
- The four Vastbase modes keep the public deparse, bind rules, and Query
  Graph/View JSON output rules of their compatibility entries.
- Added Vastbase-specific documentation, official syntax coverage, four case
  matrices, and a dialect example.

## Release Validation

This release validation includes:

- `git diff --check`
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- Shared-library ABI major: `libsqlparser.so.0`
- Current ABI exported symbols: `128`
- Vendored `libpg_query` tag: `17-6.2.2`
