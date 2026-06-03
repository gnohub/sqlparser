# v2.5.0 Release Notes

`v2.5.0` adds structured support for `LIKE ... ESCAPE ...`. Callers can now
read the explicit escape clause attached to the pattern value through Query
Graph and View JSON, while public deparse keeps the dialect-level SQL form.

## Highlights

- Updated the public version to `2.5.0`.
- Added `like_escape` to `sqlparser_graph_value_t`.
- Added `sqlparser_graph_like_escape_kind_t` for no explicit `ESCAPE`, literal,
  bind, and expression escape forms.
- View JSON emits `like_escape` on the pattern value instead of exposing the
  escape clause as a separate business value.
- PostgreSQL, MySQL, Oracle, SQL Server, and Dameng deparse no longer expose
  `pg_catalog.like_escape(...)`; output keeps `LIKE pattern ESCAPE escape`.
- Structured recognition only accepts the `pg_catalog.like_escape` node emitted
  by libpg_query, so an unqualified user-defined `like_escape(...)` function is
  not misclassified as explicit `ESCAPE`.
- Updated the Chinese and English API reference, View JSON guide, and test
  matrices.

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
