# v2.7.0 Release Notes

`v2.7.0` adds structured Query Graph operator classification. Callers can use
the public enum to identify `LIKE`, `NOT LIKE`, `ILIKE`, and `NOT ILIKE`
pattern-match semantics without comparing operator strings.

## Highlights

- Updated the public version to `2.7.0`.
- Added `operator_kind` to `sqlparser_graph_value_t`.
- Added `sqlparser_graph_operator_kind_t` with `unknown`, `like`,
  `not_like`, `ilike`, and `not_ilike`.
- Added `sqlparser_graph_operator_kind_name()`,
  `sqlparser_graph_operator_is_like_pattern()`, and
  `sqlparser_graph_value_is_like_pattern()`.
- View JSON now emits `operator_kind` in `query_graph.values[]` when an
  `operator` is present.
- `LIKE ... ESCAPE ...` recognition reuses `operator_kind`; explicit `ESCAPE`
  remains represented by `like_escape`.
- The public header layout changed. Rebuild callers with this version's header
  before linking against this version of the library.

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
- Current ABI exported symbols: `131`
- Vendored `libpg_query` tag: `17-6.2.2`
