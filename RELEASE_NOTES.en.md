# v2.16.18 Release Notes

Optimized compatible batches of string replacements in INSERT, UPDATE, WHERE, SELECT targets, and MERGE, reducing repeated whole-tree validation, dialect-state reconciliation, and source scanning.

Patch order, source-read semantics, and atomic rollback remain unchanged. Public APIs, public structure layouts, and resource limits are unchanged, with no persistent AST cache added.

In a same-machine comparison with 2.16.17, a single `sqlparser_apply_patch()` call for 500 replacements across 500 MySQL INSERT rows decreased from approximately 1.78 seconds to 25 milliseconds. A batch of 5,000 replacements across 5,000 rows took approximately 269 milliseconds. Actual gains depend on the SQL and patch mix.

The full `make test` suite passed. Valgrind reported no memory errors or leaks in the 5,000-row scenario.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
