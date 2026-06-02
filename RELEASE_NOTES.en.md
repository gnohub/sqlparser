# v2.4.0 Release Notes

`v2.4.0` enhances structured `UPDATE SET` rewrites. Callers can now rewrite an
assignment right-hand-side bind parameter to a literal while keeping the target
column, assignment order, and active dialect output rules unchanged.

## Highlights

- Updated the public version to `2.4.0`.
- `sqlparser_update_set_assignment_literal()` can rewrite literal or bind
  right-hand sides to literals.
- `sqlparser_selector_set_update_assignment_literal()` supports the same bind
  right-hand-side rewrite path.
- Function calls, operator expressions, field references, `DEFAULT`, and
  subquery right-hand sides return `SQLPARSER_STATUS_UNSUPPORTED`.
- Added regression cases to the existing PostgreSQL, MySQL, Oracle, SQL Server,
  and Dameng case matrices.
- Updated the Chinese and English API reference and test matrices.

## Release Validation

This release validation includes:

- `git diff --check`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- Shared-library ABI major: `libsqlparser.so.0`
- Current ABI exported symbols: `128`
- Vendored `libpg_query` tag: `17-6.2.2`
