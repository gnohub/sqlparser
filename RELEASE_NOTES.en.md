# v0.7.0 Release Notes

`v0.7.0` adds assignment-level `UPDATE SET` patch support. Callers can now append, delete, and replace full assignments through the unified `sqlparser_apply_patch()` path while preserving the existing RHS-only assignment rewrite behavior.

## Highlights

- Added `SQLPARSER_PATCH_INSERT_ASSIGNMENT`, `SQLPARSER_PATCH_DELETE_ASSIGNMENT`, and `SQLPARSER_PATCH_REPLACE_ASSIGNMENT`.
- Added `sqlparser_update_insert_assignment_sql()`, `sqlparser_update_delete_assignment()`, `sqlparser_update_set_assignment_full_sql()`, and the corresponding selector APIs.
- `stmt[n].assignment[i]` addresses `UPDATE SET` assignments; inserting at `i == assignment_count` appends a new assignment.
- `delete_assignment` rejects deletion of the last assignment to avoid generating invalid `UPDATE SET` SQL.
- The existing `SQLPARSER_PATCH_REPLACE` assignment behavior is unchanged and still rewrites only the RHS SQL.
- Added `examples/patch/17_update_set_patch.c` to demonstrate the complete patch workflow.
- The MSVC NMake example list now includes the new example.

## Test Coverage

- Core API coverage includes assignment insertion, deletion, full replacement, deparse, and reparse.
- Patch API coverage includes Oracle bind fragments and verifies that internal parameters are not exposed.
- Robustness tests cover invalid selectors, out-of-range indexes, multi-assignment full replacement rejection, empty-`SET` protection, and handle usability after failures.

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
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`
