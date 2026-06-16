# v2.8.1 Release Notes

`v2.8.1` is a `query_graph` performance patch release. It optimizes cold
`sqlparser_statement_query_graph()` calls for large and deeply nested SQL
workloads, especially `INSERT ... SELECT`, set queries, and nested SELECT
statements. This release does not add public APIs or change public structure
layouts or selector semantics.

## Highlights

- Updated the public version to `2.8.1`.
- Added a statement-level selector cache while building `query_graph`,
  recording value, name, relation, and select target-list selector indexes in
  one traversal.
- Avoided repeated full statement protobuf tree searches for each value, field,
  relation, or SELECT target list in large and deeply nested `query_graph`
  builds.
- Kept `sqlparser_statement_query_graph()`, public structure layouts, selector
  output format, and same-handle query graph cache behavior unchanged.
- The public header layout is unchanged; callers do not need adaptation for
  public structure changes.

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
- Current ABI exported symbols: `135`
- Vendored `libpg_query` tag: `17-6.2.2`
