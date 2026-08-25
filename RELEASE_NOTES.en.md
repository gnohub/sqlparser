# v2.16.10 Release Notes

Query Graph adds a DDL root block and relation roles that represent supported DDL objects and references as `TARGET` and `REFERENCE`. Query-backed DDL targets point through `source_block` to a separate SELECT source block.

Relation projection covers common table, index, view, TRUNCATE, RENAME, and DROP forms successfully parsed by each dialect entry. PostgreSQL-compatible entries also cover FOREIGN TABLE and partition references. DROP segment delimiter state is derived from each exact source token, while U& identifiers continue not to set the ordinary delimiter flags. CREATE INDEX/TRUNCATE relation patches in ordinary SQL Server multi-statement input preserve the public SQL surface.

The public API adds the DDL block kind, relation-role enum, `ddl_role` field, and `sqlparser_graph_ddl_relation_role_name()`. On x86_64 and AArch64 64-bit layouts, the relation structure retains its size and every old member offset. With 66 final cases and 113 patches added, the nine fixtures now contain 2,968 final cases and 9,379 patches. The full `make test` suite, ABI export check, and targeted identifier Valgrind check passed.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
