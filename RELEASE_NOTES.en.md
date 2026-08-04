# v2.14.4 Release Notes

`v2.14.4` is a patch release for `v2.14.3`. It corrects stale handle-level
source-surface state after a structural patch enters the AST fallback path,
which could cause a later consecutive patch to lose a newly inserted node.

## Consecutive Structural Patches

- When a structural patch cannot use a local source edit, the fallback path now
  clears both the per-call and handle-level source-surface completeness flags,
  keeping the AST and current source state consistent.
- Before this fix, adding a column and value to Oracle-compatible `INSERT ALL`
  through `source_selector` could be followed by an internal deparse and reparse
  that restored source from before the patch. The first call appeared to
  succeed while the new cell was lost, and replacing that cell later reported
  `cell index is out of range`.
- The same handle can now apply consecutive insert and new-cell replacement
  patches without an intervening View, deparse, or fresh parse. Final deparse
  reflects only the requested changes and preserves unchanged branches, bind
  parameters, and other original SQL text.

## Regression Coverage

- Oracle, Dameng, and Vastbase-Oracle share one table-driven unit regression.
- Each regression statement has two `INSERT ALL` branches with 32 target
  columns and values per branch.
- Starting from a pristine handle, the test executes four `insert_column`
  operations and four replacements of the newly inserted cells as eight
  independent `apply_patch` calls.
- The test checks every call status and generation, exact final SQL, and stable
  deparse after a fresh parse.

## Compatibility

- This release adds no public APIs, enums, or structure fields.
- Existing function signatures and public structure layouts are unchanged.
  The shared-library ABI major remains `libsqlparser.so.0`.

## Release Validation

- The nine executable dialect fixtures still contain 2,758 cases with
  `status = "final"` and 8,945 independent patches.
- The final code completed a remote full `make test` with exit code 0.
- A targeted Valgrind run matched all 1,018,764 allocations with frees, exited
  with `0 bytes in 0 blocks`, and reported zero errors.

Vendored `libpg_query` tag: `17-6.2.2`.
