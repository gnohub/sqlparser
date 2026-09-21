# v2.16.15 Release Notes

Reduced repeated serialization and parsing in patch batches, covering the shared AST path, column and value edits in Oracle-family and Dameng `INSERT ALL/FIRST`, and compatible function-argument literal replacements.

Patch order, `source_selector` read semantics, and atomic rollback remain unchanged. No changes to public APIs, public structure layouts, View JSON fields, or ownership rules.

In a same-machine comparison with 2.16.14, a single `sqlparser_apply_patch()` call for the Oracle `INSERT ALL` sample with 50 branches and 500 patches decreased from approximately 12.53 seconds to 0.21 seconds. Actual gains depend on the SQL and patch mix.

New batch regressions cover all 13 dialect entries. The full remote `make test` suite and ABI check passed. Valgrind reported no memory errors or leaks in the new regressions.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
