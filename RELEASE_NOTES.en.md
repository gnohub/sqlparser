# v2.16.5 Release Notes

MySQL and the Vastbase-MySQL compatibility entry support multiple write targets across JOIN chains and comma-separated table lists. Each assignment expresses write ownership through its existing `target_field` and the field's `relation`; mixed-target updates do not expose a single `dml.target_relation`.

Dameng supports multi-table UPDATE statements with JOIN or comma-separated table lists, but every assignment must target the same table object. Cross-target, unknown, or ambiguous qualifiers return `SQLPARSER_STATUS_UNSUPPORTED`. Existing assignment and relation patches and transaction rollback rules remain unchanged.

This release adds no public C declarations, View JSON fields, or resource-ownership rules. With 16 final cases and 41 patches added, the nine fixtures now contain 2,822 final cases and 9,118 patches. The full remote `make test` suite passed; targeted Valgrind checks covering the core API and the three affected dialect matrices each reported `0 bytes in 0 blocks` and zero errors.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
