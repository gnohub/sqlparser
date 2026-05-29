# v2.2.0 Release Notes

`v2.2.0` adds structured SQL fragment rewrite APIs for C callers that need to
modify output column lists or `UPDATE SET` assignments. Callers can pass
structured identifier paths and value SQL, and the library builds AST fragments
according to the current handle dialect before deparsing.

## Highlights

- Updated the public version to `2.2.0`.
- Added `sqlparser_identifier_path_view_t` to represent single-part column
  names, qualified column names, and longer identifier paths.
- Added structured `UPDATE SET` assignment construction for generating,
  appending, or replacing assignments from a column path and value SQL.
- Added structured `SELECT` target replacement for replacing one output item
  with multiple column paths.
- Added the structured rewrite example
  `examples/convenience/18_structured_fragment_rewrite.c`.
- Updated the Chinese and English API reference, examples guide, and test guide.

## Release Validation

This release validation includes:

- `git diff --check`
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`
- Windows/MSVC `nmake /F Makefile.msvc test`

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- Shared-library ABI major: `libsqlparser.so.0`
- Current ABI exported symbols: `124`
- Vendored `libpg_query` tag: `17-6.2.2`
