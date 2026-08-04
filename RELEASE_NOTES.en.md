# v2.14.3 Release Notes

`v2.14.3` is a patch release for `v2.14.2`. It corrects whole-AST
serialization of unchanged expressions after `insert_column` on an ordinary
`INSERT ... VALUES` statement.

## INSERT COLUMN Surface Preservation

- For an ordinary `INSERT ... VALUES` statement with an explicit target list,
  `insert_column` inserts the column name and default value into the target
  list and every VALUES row at their original source intervals. The operation
  no longer requires serialization of the complete AST.
- All target intervals, boundaries, and conflicts are validated before edits
  are added. Every row uses the same insertion index, and a failure cannot
  leave a partially modified statement.
- Unchanged text is preserved byte for byte. Original expressions such as
  `DATE '...'`, `TIMESTAMP '...'`, `NOW()`, `CURRENT_TIMESTAMP`, and
  `GETDATE()` are not rewritten as `CAST(...)` or another normalized form when
  a different column is added.
- Multi-row planning reuses expression-source scan state, while ordered source
  edits use a tail-append path to avoid repeated scanning or movement as the
  VALUES row count grows.

## Regression Fixtures

- Each of the nine executable dialect fixtures adds one `insert_column`
  regression patch covering typed literals or dialect time functions, with an
  exact-text assertion for patched SQL.
- The case runner accepts a strictly parsed internal selector for an INSERT
  target list. Existing JSON Pointer patch paths are unchanged.

## Compatibility

- This release adds no public APIs, enums, or structure fields.
- Existing function signatures and public structure layouts are unchanged.
  The shared-library ABI major remains `libsqlparser.so.0`.

## Release Validation

- The nine executable dialect fixtures contain 2,758 cases with
  `status = "final"` and 8,945 independent patches.
- The final code completed a remote full `make test`. Original deparse, View,
  patched deparse, a second deparse after reparsing, and patched/fresh View
  checks all passed.
- One targeted Valgrind run on the final code matched all 947,143 allocations
  with frees, exited with `0 bytes in 0 blocks`, and reported zero errors.

Vendored `libpg_query` tag: `17-6.2.2`.
