# v2.13.0 Release Notes

`v2.13.0` adds structured MERGE branch projection and selector-based rewrites.
It also defines mixed `INSERT ... VALUES` and bind-lineage behavior across all
nine dialect modes, together with identifier-preserving deparse behavior after
SQL-fragment patches.

## Highlights

- Query Graph emits every MERGE `WHEN` branch in source order, including its
  action, match kind, INSERT target columns and rows, and UPDATE assignment
  span. A conditional branch also includes its condition selector.
- Added a MERGE branch-condition selector, action and match enums, two
  enum-name functions, and a MERGE branch-detail accessor. Branch conditions
  and assignments are addressable through selectors in both top-level and
  nested MERGE statements.
- MERGE UPDATE assignments expose target fields, source fields, source
  targets, and bind relationships and can be inserted, replaced, and deleted
  through assignment selectors and patch operations.
- Oracle, Dameng, and Vastbase-Oracle support action-level `WHERE` clauses on
  MERGE UPDATE and INSERT actions.
- `INSERT ... VALUES` in all nine dialect modes distinguishes direct binds,
  literals, standalone `DEFAULT` values, and expression cells. Binds nested
  in expressions participate in global ordering, and time expressions are
  not emitted as field names.
- Identifiers in parsed SQL-fragment patches retain the fragment's case and
  delimiter form, including double quotes, MySQL backticks, and SQL Server
  brackets; deparse neither replaces nor duplicates those delimiters.

## Deparse Contract

- For an unchanged generation-`0` handle, successful deparse returns the
  original SQL byte for byte, including keywords, identifiers, whitespace,
  line breaks, comments, semicolons, and multi-statement boundaries.
- For generations greater than `0`, SQL is serialized from the current AST.
  Identifier case, delimiters, and escape spelling are preserved, while the
  deparser may normalize whitespace between nodes.
- Patch SQL fragments are parsed in the selected dialect before entering the
  AST; they are not inserted as unparsed strings.

## Compatibility

- Public API changes are append-only; existing function signatures and public
  structure layouts remain unchanged.
- The shared-library ABI major remains `libsqlparser.so.0`.
- Added `sqlparser_graph_merge_action_kind_name()`,
  `sqlparser_graph_merge_match_kind_name()`, and
  `sqlparser_query_graph_merge_branch_detail()`. The ABI export check covers
  152 public symbols.

## Release Validation

- The nine dialect matrices contain 2,535 expected-success cases. Every case
  runs a generation-`0` byte-exact deparse check, an AST identifier-spelling
  audit, and View construction.
- The nine matrices add 90 mixed-VALUES cases covering single and multiple
  rows, nested binds, functions and compound expressions, delimited
  identifiers, and irregular whitespace.
- MERGE regressions cover branch ordering, condition selectors, INSERT rows,
  UPDATE assignments, DELETE and NOTHING actions, top-level and nested MERGE
  statements, lineage, and action-level `WHERE` clauses.
- Strict GCC 8.3 release and debug builds, the full test suite, install smoke,
  the 152-symbol ABI check, ASan, UBSan, Valgrind, and benchmark smoke all
  passed.

Vendored `libpg_query` tag: `17-6.2.2`.
