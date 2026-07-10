# v2.9.0 Release Notes

`v2.9.0` expands MySQL and Vastbase-MySQL dialect coverage and improves Query
Graph representation for CTEs and JOIN fields.

## Highlights

- Supports `INSERT ... SET`, `ON DUPLICATE KEY UPDATE` row aliases, and aliased
  delete targets.
- Supports single-table `UPDATE` / `DELETE ... ORDER BY ... LIMIT` statements.
- Supports `STRAIGHT_JOIN`, `JOIN ... USING`, `NATURAL JOIN`, locking reads,
  index hints, and query-table `PARTITION(...)` clauses.
- Emits `JOIN ... USING` fields through `fields[]` and `candidate_relations`.
- Repeated references to one CTE share its source block, unused CTEs remain in
  the graph, and recursive CTE references point to the registered block.
- Adds the `SQLPARSER_GRAPH_INSERT_MODE_SET` enum value.
- The MySQL and Vastbase-MySQL dialect test matrices each contain 156
  supported cases.

## Compatibility

- Public C structure layouts remain stable.
- The shared-library ABI major remains `libsqlparser.so.0`.
- The ABI export count remains 135.
- A client compiled with the previous public header passes against the current
  shared library.

## Release Validation

- Strict GCC 8.3 build and full test suite
- ASan and UBSan
- Valgrind checks for all configured targets
- ABI export check
- Old-client shared-library compatibility test

Vendored `libpg_query` tag: `17-6.2.2`.
