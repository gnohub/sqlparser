# v2.16.9 Release Notes

Query Graph relations add delimiter-state fields for database, schema, and database-link segments, while DML columns add a target-column delimiter-state field. Each flag is derived only from its corresponding exact source token, and View JSON emits the matching field only when its value is `true`.

The state covers ordinary SELECT, INSERT, UPDATE, DELETE, and MERGE relations, plus target columns in regular INSERT, MERGE INSERT, `INSERT ALL/FIRST`, MySQL SET forms, and SQL Server `OUTPUT ... INTO` sinks. Multi-table INSERT database-link branches in Oracle, Dameng, and Vastbase-Oracle fully project the object, link, and their delimiter state. Branch link data is owned internally by dialect state and is deep-copied and released through clone/destroy paths; caller ownership rules are unchanged.

This release adds public structure fields but no public export symbol. Existing member offsets and `sizeof` remain unchanged on x86_64 and AArch64 64-bit layouts; no equivalent claim is made for 32-bit layouts. With 62 final cases and 103 patches added, the nine fixtures now contain 2,902 final cases and 9,266 patches. The full `make test` suite, targeted identifier and core-API tests, all nine dialect matrices, and the relevant Valgrind checks passed.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
