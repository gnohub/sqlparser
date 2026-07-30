# v2.12.0 Release Notes

`v2.12.0` strengthens lossless round trips for unchanged SQL and adds
structured session-state traversal plus MERGE matched-UPDATE assignment
rewrites.

## Highlights

- When a handle generation is `0`, a successful `sqlparser_deparse()` call
  returns the original input SQL byte for byte, preserving identifier
  delimiters and case, keywords, whitespace, line breaks, comments,
  semicolons, and multi-statement boundaries.
- AST name values originating from SQL identifier tokens retain the source
  token's letter case; quoted identifiers continue to use decoded AST name
  content.
- Query Graph adds read-only session action, item, and value accessors, and
  View JSON emits the optional `query_graph.session` object.
- Session projection covers supported database, schema, role, identity,
  transaction-characteristic, and session-parameter statements, with
  identifier, keyword, literal, bind, and expression values.
- Added the `stmt[S].merge_assignment[W][A]` selector for reading and rewriting
  assignments in `WHEN MATCHED ... THEN UPDATE` actions.
- The `update_assignment` selector APIs support reads, right-hand-side
  rewrites, and same-statement right-hand-value cloning for MERGE
  matched-UPDATE assignments. The three assignment patch operations support
  assignment insertion, deletion, and full replacement.

## Compatibility

- Public API changes are append-only; existing function signatures and public
  structure layouts remain unchanged.
- The shared-library ABI major remains `libsqlparser.so.0`. Three session Query
  Graph functions are newly exported, and the ABI export check covers 149
  public symbols.
- When the generation is greater than `0`, deparse serializes the current
  handle state, and the generation-`0` byte-for-byte guarantee no longer
  applies to the output as a whole.

## Release Validation

- All nine dialect case matrices run generation-`0` byte-exact deparse and AST
  identifier-spelling checks for every expected-success case.
- All nine dialect matrices include session-projection expectations.
- MERGE-capable dialects cover positive matched-UPDATE assignment selector
  parsing, reads, insertion, deletion, replacement, and right-hand-value
  cloning. Core API tests cover failure atomicity for invalid branches,
  out-of-range indexes, invalid assignment fragments, and deletion of the last
  assignment.
- Strict GCC 8.3 release/debug builds, the full test suite, install smoke, the
  149-symbol ABI check, ASan, UBSan, and Valgrind all passed.
- A Windows VS 2022 x64/MSVC 19.39 clean build and full test suite passed.

Vendored `libpg_query` tag: `17-6.2.2`.
