# v2.16.8 Release Notes

Existing `SQLPARSER_PATCH_INSERT_COLUMN` supports three MERGE INSERT payload shapes: name-only, value-only, and name plus value, which insert a target column, a VALUES cell, or both at the same position. Existing target columns and VALUES cells continue to support independent replacement. A branch with VALUES now exposes the existing `target_list_selector` even when its target-column list is omitted, allowing the list to be materialized or a cell to be inserted while the list remains omitted.

Column and value counts may differ temporarily within one patch batch. Every touched branch that ends with an explicit target-column list is validated for equal widths before commit, and a mismatch rolls back the whole batch atomically. Deletion remains paired, and the three insertion shapes do not apply to `MERGE INSERT DEFAULT VALUES`. The contract covers successfully parsed MERGE statements through all nine project dialect entry points; it does not claim native support from every corresponding database server.

This release adds no public API, enum value, structure field, or View JSON field; it only expands the output range of an existing selector. With nine final cases and 27 patches added, the nine fixtures now contain 2,840 final cases and 9,163 patches. The full remote `make test` suite, targeted core-API test, all nine dialect matrices, and Valgrind passed.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
