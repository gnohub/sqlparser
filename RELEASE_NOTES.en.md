# v2.15.2 Release Notes

`v2.15.2` adds structured parsing, View, and patch support for an attached
`DELETE WHERE` in Oracle and Dameng MERGE matched UPDATE actions while
preserving the existing independent DELETE-action semantics in PostgreSQL and
SQL Server.

## Supported Boundary

- Oracle and Dameng support
  `WHEN MATCHED THEN UPDATE SET ... [WHERE ...] DELETE WHERE ...`.
- `DELETE WHERE` is attached to the matched UPDATE action and evaluates the
  updated target row. The Query Graph does not create an independent DELETE
  branch for it.
- PostgreSQL and SQL Server `WHEN MATCHED ... THEN DELETE` actions continue to
  use independent MERGE branches.
- This release does not add MERGE support to MySQL or declare this syntax for
  any Vastbase compatibility mode.
- The attached DELETE form requires both `WHERE` and a condition expression;
  a bare `DELETE` remains unsupported.

## Query Graph and Selectors

- A matched UPDATE branch can expose both `condition_selector` and
  `delete_condition_selector`, addressing the action `WHERE` and attached
  `DELETE WHERE` predicates independently.
- A root MERGE uses `stmt[S].merge_delete_condition[W]`; a nested MERGE uses
  `stmt[S].merge_delete_condition[D][W]`.
- `sqlparser_selector_clause_sql()` returns the condition expression without
  the `DELETE WHERE` keywords.
- `sqlparser_selector_set_clause_sql()` and `SQLPARSER_PATCH_REPLACE` can
  replace either an ordinary branch condition or an attached delete condition.

## Patch and Deparse

- A MERGE assignment bounded by a comma, action `WHERE`, attached
  `DELETE WHERE`, or a following `WHEN` uses a local source edit.
- Assignment, ordinary branch-condition, and attached-delete-condition patches
  replace only the selected source interval. Other branches, line breaks,
  whitespace, keyword case, and identifier delimiters remain byte-preserved.
- Patched SQL is reparsed and checked against the expected View; the attached
  delete predicate remains associated with its original UPDATE branch.

## API and Compatibility

- `sqlparser_selector_kind_t` appends
  `SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION = 25`.
- `sqlparser_graph_dml_branch_t` appends `delete_condition_selector` and
  `has_delete_condition_selector`.
- This release adds no public functions or resource-ownership rules.
- The public structure layout is extended. C applications should be rebuilt
  against the 2.15.2 headers.
- The shared-library ABI major remains `libsqlparser.so.0`.

## Cases and Documentation

- Oracle adds two final cases covering an action `WHERE`, attached
  `DELETE WHERE`, and conditional INSERT in one MERGE, plus a matched UPDATE
  with only an attached delete predicate.
- Dameng adds one final case covering coexisting action and attached-delete
  predicates.
- PostgreSQL and SQL Server each add one final case covering independent
  matched DELETE and UPDATE actions followed by a not-matched INSERT.
- This release adds five final cases and 17 independent patches. The nine
  current fixtures contain 2,772 final cases and 8,989 patches.
- English and Chinese View JSON, API, dialect-support, official-syntax, and
  case-matrix documentation are synchronized.

## Validation

- All nine case matrices completed 2,772 cases and 8,989 patches with zero
  failures.
- Original deparse, View JSON, patch deparse, and runner error counts were all
  zero.
- Core API tests passed, covering root and nested selectors, condition reads
  and replacements, dialect restrictions, and bare-DELETE rejection.
- A targeted Valgrind run completed 1,040,125 allocations and 1,040,125 frees;
  it exited with `0 bytes in 0 blocks` and zero errors.

Vendored `libpg_query` tag: `17-6.2.2`.
Vendored Jansson version: `2.15`.
