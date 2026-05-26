# v0.9.0 Release Notes

`v0.9.0` refines prepared-statement placeholder output in SQL View. Public C structures and JSON View now consistently expose `bind_key`, `bind_kind`, `bind_position`, `bind_sql`, and `bind_selector`.

## Highlights

- Removed the old `bind` output field in favor of the clearer `bind_key`.
- `sqlparser_column_view_t` and `sqlparser_cell_view_t` expose structured bind fields directly; JSON is only the on-demand view output.
- `bind_position` is the one-based bind occurrence in the full input SQL text and does not restart for each statement in multi-statement SQL.
- Anonymous `?`, explicitly numbered positional binds such as `:1` and `$1`, and named binds such as `:name` and `@name` all expose `bind_kind`, `bind_key`, and `bind_sql`.
- Placeholder-like text inside PostgreSQL dollar-quoted strings is excluded from global bind counting.

## Test Coverage

- PostgreSQL executable cases: 97, with 96 supported.
- MySQL executable cases: 68, with 53 supported.
- Oracle executable cases: 104, with 86 supported.
- SQL Server executable cases: 97, with 82 supported.
- Dameng executable cases: 76, with 64 supported.

## Release Validation

This release validation includes:

- `git diff --check`
- JSON case file validation
- removed `bind` field residue scan
- Linux `make clean && make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux targeted CLI validation for multi-statement global bind positions and dollar-quoted string counting
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`
- Windows VS 2022 x64 + MSVC `nmake /F Makefile.msvc clean && nmake /F Makefile.msvc test`

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`
