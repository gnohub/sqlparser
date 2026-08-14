# v2.16.4 Release Notes

Root `INSERT` conflict-update lists use `stmt[S].assignment[A]`, while nested `UPDATE` assignment lists use `stmt[S].assignment[D][A]`. Existing assignment insertion, full-replacement, and deletion patches now apply to MySQL and Vastbase-MySQL `ON DUPLICATE KEY UPDATE`, PostgreSQL and Vastbase-PostgreSQL `ON CONFLICT DO UPDATE` and nested `UPDATE` statements in data-modifying CTEs, and SQL Server and Vastbase-SQLServer nested `UPDATE` statements with `OUTPUT`.

The View JSON schema, public C declarations, and resource-ownership rules are unchanged; assignment-selector output is expanded. Some MySQL conflict-update items now use assignment selectors instead of their previous value selectors. Paired column/value mutation for `INSERT ... SET` is an existing capability covered by additional regression cases in this release.

The full remote `make test` suite passed. The six affected dialect matrices cover 2,158 final cases and 6,798 patches. Targeted Valgrind checks for the PostgreSQL, MySQL, and SQL Server base-dialect matrices each reported `0 bytes in 0 blocks` and zero errors. With 10 final cases and 28 patches added, the nine fixtures now contain 2,806 final cases and 9,077 patches.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
