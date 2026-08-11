# v2.15.3 Release Notes

`v2.15.3` adds structured parsing, Query Graph, and patch support for Oracle
and Dameng hierarchical queries, together with the verified basic
`CONNECT BY` form in Vastbase-SQLServer.

## Supported Boundary

- Oracle supports `START WITH ... CONNECT BY [NOCYCLE] ...`, together with
  `PRIOR`, `LEVEL`, `CONNECT_BY_ROOT`, `CONNECT_BY_ISLEAF`, and
  `CONNECT_BY_ISCYCLE`. Oracle mode does not accept the reversed
  `CONNECT BY ... START WITH ...` clause order.
- Dameng supports `START WITH`, `CONNECT BY [NOCYCLE]`, `PRIOR`, `LEVEL`, and
  `CONNECT_BY_ROOT`, and preserves both `START WITH ... CONNECT BY ...` and
  `CONNECT BY ... START WITH ...` source orders.
- Vastbase-SQLServer supports only a basic `CONNECT BY` condition.
  `START WITH`, `PRIOR`, `NOCYCLE`, and `CONNECT_BY_ROOT` are outside this
  release's supported boundary for that mode.
- Hierarchical context is local to the current SELECT query block. A nested
  SELECT does not inherit pseudo-column or `PRIOR` state from its parent, and
  the delimited identifier `"LEVEL"` retains ordinary field semantics.

## Query Graph

- `sqlparser_clause_kind_t` appends
  `SQLPARSER_CLAUSE_KIND_START_WITH = 11` and
  `SQLPARSER_CLAUSE_KIND_CONNECT_BY = 12`.
- `START WITH` and `CONNECT BY` do not create a dedicated hierarchy object.
  Their fields, values, and predicates remain in `fields[]`, `values[]`, and
  `predicates[]` with a `start_with` or `connect_by` clause.
- Hierarchical pseudo-columns use `pseudo: true` in `fields[]`; field
  occurrences inside the `PRIOR` operand use `prior: true`; and
  `CONNECT BY NOCYCLE` marks only the clause's root predicate with
  `nocycle: true`.
- A `CONNECT_BY_ROOT` target remains an expression. Its underlying field uses
  the existing `target_path` to record the operator path. No target kind,
  selector, or dedicated patch type is added.
- Condition-related occurrences are built in the semantic order `WHERE`,
  `START WITH`, `CONNECT BY`, `GROUP BY` / `HAVING`, window clauses, and
  `ORDER BY`. Selectors remain generic AST-descriptor locations and should be
  treated as opaque paths.

## Patch and Deparse

- Relation, field, value, and SELECT-target changes reuse existing selectors
  and patch types.
- Replacements with an exact source interval use local source edits and change
  only the selected interval. Unchanged hierarchical-clause order, line
  breaks, whitespace, keyword case, and identifier delimiters remain
  byte-preserved.
- Oracle, Dameng, and Vastbase-SQLServer preprocessing preserves source
  mappings for internal hierarchy keywords. Bind-state rebuilding after a
  patch traverses both `START WITH` and `CONNECT BY` expressions.

## API and Compatibility

- `sqlparser_graph_field_t` appends `pseudo` and `prior`, while
  `sqlparser_graph_predicate_t` appends `nocycle`.
- This release adds no public functions or resource-ownership rules.
- The public enum gains values and public structures gain fields. C
  applications should be rebuilt against the 2.15.3 headers.
- The shared-library ABI major remains `libsqlparser.so.0`.

## Cases and Documentation

- Oracle adds four final cases and 20 patches covering basic hierarchy,
  compound conditions, `CONNECT_BY_ROOT`, `NOCYCLE`, and the
  `CONNECT_BY_ISLEAF` / `CONNECT_BY_ISCYCLE` pseudo-columns.
- Dameng adds four final cases and 20 patches covering both clause orders,
  both parent-child field orientations, `CONNECT_BY_ROOT`, and `NOCYCLE`.
- Vastbase-SQLServer adds one final case and five patches covering basic
  `CONNECT BY` while keeping ordinary `WHERE` / `ORDER BY` and hierarchy
  conditions distinct.
- This release adds nine final cases and 45 independent patches. The nine
  current fixtures contain 2,781 final cases and 9,034 patches.

## Validation

- Oracle regression completed 248 cases and 849 patches, Dameng completed
  174 cases and 633 patches, and Vastbase-SQLServer completed 601 cases and
  1,847 patches, all with zero failures.
- Original deparse, View JSON, patch deparse, and runner error counts were all
  zero.
- Core API tests passed, covering query-block context isolation, dialect
  boundaries, exact source deparse, post-patch reparse, and failed-patch
  rollback.
- Targeted Valgrind checks for the relevant dialect targets exited with
  `0 bytes in 0 blocks` and zero errors.

Vendored `libpg_query` tag: `17-6.2.2`.
Vendored Jansson version: `2.15`.
