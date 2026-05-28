# v2.0.0 Release Notes

`v2.0.0` is the stable interface release for `sqlparser`. It provides C callers with SQL parsing, structured traversal, selector-based rewriting, and deparsing. This release keeps `query_graph` as the canonical structured-output source, while View JSON is generated only when callers request a JSON export.

## Highlights

- Updated the public version to `2.0.0`.
- View JSON and public C structures consistently expose `bind_key`, `bind_kind`, `bind_position`, `bind_sql`, and `selector` for prepared-statement placeholders.
- `bind_position` is the one-based bind occurrence in the full input SQL text and does not restart for each statement in multi-statement SQL.
- Anonymous `?`, explicitly numbered positional binds such as `:1` and `$1`, and named binds such as `:name` and `@name` expose the same structured bind fields.
- SELECT output hierarchy is represented by ordered `target_path` entries for functions, expressions, CASE forms, and nested output paths.
- View JSON omits empty arrays from `query_graph` and DML structures, while the public C structs still represent empty collections through `count` or `has_*` fields.
- PostgreSQL, MySQL, Oracle, SQL Server, and Dameng dialect matrices continue to cover DDL, DML, JOIN, functions, expressions, binds, pagination, and context-switching scenarios.
- CLI batch input accepts only a top-level array or an `items` array.
- The `libpg_query` baseline keeps single-thread successful parsing and first-parse measurements.

## Robustness Fixes

- Fixed ownership handling for Jansson `_new` APIs on View JSON serialization failure paths, avoiding duplicate releases of intermediate JSON nodes under low-memory conditions.

## Release Validation

This release validation includes:

- `git diff --check`
- JSON case file validation
- Linux `make clean && make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux full View JSON CLI case sweep
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`
