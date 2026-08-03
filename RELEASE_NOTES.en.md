# v2.14.0 Release Notes

`v2.14.0` closes the consistency loop across patching, deparse, and View:
unchanged SQL surface text survives local rewrites, compound DML assignments
expose right-hand lineage, and one statement can represent multiple peer DML
roots.

## Patch and Deparse

- Unchanged SQL spans retain original identifier case and delimiters, keywords,
  comments, whitespace, parentheses, and semicolons byte for byte. A local
  patch replaces only its resolved source interval.
- Patch values are parsed as SQL fragments in the selected dialect before
  entering the AST. Explicit double quotes, MySQL backticks, or SQL Server
  brackets in the fragment are neither replaced nor duplicated.
- Relation renames propagate only to qualified references that bind uniquely
  in scope. Same-scope ambiguity, inner-scope shadowing, explicit aliases, and
  SQL Server pseudo-relations such as `INSERTED` and `DELETED` remain unchanged.
- Replacing one SELECT target splices a multi-target fragment at the original
  position and does not inherit the old target alias.

## Query Graph and View

- `sqlparser_graph_dml_assignment_t` adds `rhs_fields`, `rhs_values`, and
  `rhs_blocks` for traversing fields, values, and subquery entry blocks in a
  compound UPDATE or MERGE assignment right-hand side.
- A single root DML continues to use `query_graph.dml`; multiple peer roots use
  `query_graph.dmls`. Nested DML remains under each root's `children`, and
  data-modifying CTEs follow the same structure.
- Added `SQLPARSER_CLAUSE_KIND_WINDOW_PARTITION`, making the `PARTITION BY`
  list of a named window definition independently addressable.

## Dialect Boundaries

- Oracle, Dameng, and Vastbase-Oracle extend parsing, lineage, and surface
  preservation for set operations, multi-table DML, binds, and national
  literals. Set-tree traversal does not depend on a fixed branch-count ceiling.
- MySQL and Vastbase-MySQL correct restoration of ordinary comments,
  executable comments, index hints, table partitions, and DML tails along
  patch paths.
- SQL Server and Vastbase-SQLServer correct post-patch restoration for
  `OUTPUT`, MERGE, dynamic execution, transaction batches, and bracket
  identifiers.

## Compatibility

- The public layout of `sqlparser_graph_dml_assignment_t` has grown. C
  consumers that use this structure must rebuild against the 2.14.0 header.
- View consumers must recognize the mutually exclusive `query_graph.dml` and
  `query_graph.dmls` shapes and must not assume one DML root per statement.
- Patch fragments remain dialect-parsed SQL and are not concatenated as
  unparsed strings.

## Release Validation

- The nine executable dialect fixtures contain 2,752 cases with
  `status = "final"` and 8,890 independent patches.
- Every case checks byte-exact original deparse, the expected View JSON
  structure, and all independent patches. Each patch also checks expected SQL,
  a second deparse after a fresh parse, and patched/fresh View equivalence.
- The release-candidate code completed one ASan run, one UBSan run, one
  Valgrind run, ten full regression loops, and the full benchmark. The
  benchmark executed 530,100 measured operations with zero error operations;
  this does not claim an improvement over a historical baseline.

Vendored `libpg_query` tag: `17-6.2.2`.
