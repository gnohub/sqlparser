# v2.16.7 Release Notes

For a regular single-table `INSERT ... VALUES`, existing `SQLPARSER_PATCH_INSERT_COLUMN` can add only a column name and may be combined with paired insertion and `REPLACE insert_cell` in one patch list. Every row must match the final column count before commit, or the whole batch rolls back. Oracle, Dameng, and the Vastbase-Oracle compatibility entry provide the same branch-scoped capability for the currently modeled explicit single-VALUES branches of `INSERT ALL/FIRST`; other branches and the source SELECT remain unchanged.

MERGE INSERT still requires paired insertion; `DEFAULT VALUES`, MySQL `INSERT ... SET`, branches without `VALUES`, and multiple tuples per branch remain outside this scope. The implementation reuses the existing patch operation, selectors, and transaction candidate, adding no public API, enum value, View JSON schema field, or persistent state. The nine fixture totals remain 2,831 final cases and 9,136 patches. After the functional changes, the full remote `make test` suite, targeted core-API test, and Valgrind check passed; Valgrind reported `0 bytes in 0 blocks` and zero errors.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
