# v2.16.16 Release Notes

Reduced repeated whole-statement parsing and source scanning in patch batches, covering INSERT value copying, Oracle-family and Dameng `INSERT ALL/FIRST` value replacements, function-expression and argument edits, and mixed batches of UPDATE assignment and argument replacements.

Compatible consecutive edits are combined, with synchronization when current parsed state is required. Patch order, `source_selector` read semantics, atomic rollback, and existing resource limits remain unchanged, as do public APIs, public structure layouts, View JSON fields, and ownership rules.

In a same-machine comparison with 2.16.15, one batch of 250 string replacements on a 27,530-byte Oracle `INSERT ALL` sample with 50 branches reduced `sqlparser_apply_patch()` time from approximately 1.41 seconds to 15 milliseconds. Actual gains depend on the SQL and patch mix.

Patch batch regressions cover all 13 dialect entries. The full `make test` suite and ABI check passed. Valgrind reported no memory errors or leaks in the patch batch regressions.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
