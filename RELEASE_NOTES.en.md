# v0.6.0 Release Notes

`v0.6.0` is a SQL View structured-output release for `sqlparser`. It makes the public C structures the stable data access surface and keeps View JSON as an on-demand visualization format.

## Highlights

- SQL View JSON is now serialized on demand from SQL View C structures; parse and structured traversal paths do not generate JSON by default.
- `sqlparser_column_view_t` and `sqlparser_cell_view_t` now expose bind text, bind kind, original bind SQL, bind selector, clause id, and SELECT target path fields.
- Added the public `sqlparser_bind_kind_t`, `sqlparser_bind_kind_name()`, `sqlparser_statement_clause_at()`, and `sqlparser_clause_sql()` APIs.
- Extended `sqlparser_clause_kind_t` with `on`, `group_by`, and `having` clause kinds.
- Replaced `target_kind`, `target_name`, and `target_arg_index` in View JSON with ordered `target_path` entries for functions, expressions, CASE, and nested SELECT output hierarchy.
- Bind placeholders are no longer duplicated as ordinary `value` payloads, so callers do not confuse `?`, `:1`, `:name`, `$1`, or `@name` with literal values.
- `NOT IN`, `NOT LIKE`, `NOT ILIKE`, and `NOT SIMILAR TO` preserve complete public SQL operator text.

## Test Coverage

Current executable case matrices:

- PostgreSQL generic batch fixture: 85 statements.
- MySQL: 57 cases, 42 supported paths, and 15 explicit unsupported paths.
- Oracle: 89 cases, 70 supported paths, and 19 explicit unsupported paths.
- SQL Server: 85 base cases, 70 supported paths, and 15 explicit unsupported paths; the official `HOOK_ONLY` coverage matrix contains 235 cases.
- Dameng: 65 cases, 53 supported paths, and 12 explicit unsupported paths.

## Release Validation

This release validation includes:

- `git diff --check`
- Sensitive-information and legacy-interface scans
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan`
- Linux `make verify-ubsan`
- Linux `make verify-valgrind`
- Windows MSVC `nmake /F Makefile.msvc test`

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- SQL View C structured traversal: read on demand through `sqlparser_get_view()` and related view APIs
- SQL View JSON: exported on demand through `sqlparser_export_view_json()`
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`
