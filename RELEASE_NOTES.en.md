# v2.14.1 Release Notes

`v2.14.1` is a patch release for `v2.14.0`. It completes structured MERGE
INSERT addressing and rewrites, and corrects cases where a local patch in a
SQL Server-compatible mode normalized unchanged SQL surface text.

## Structured MERGE INSERT Rewrites

- Every explicit MERGE INSERT target column exposes a `merge_insert_column`
  selector, every complete VALUES cell exposes a `merge_insert_cell` selector,
  and an explicit target-column list exposes an `insert_branch_columns`
  selector.
- A root MERGE uses the WHEN-branch and column ordinals. A nested MERGE in the
  same statement also includes its DML index, preventing selector collisions
  between multiple MERGE statements.
- One target column or complete cell can be replaced independently. The target
  list supports atomic insertion or deletion of the target column and value at
  the same index, preventing count or ordering mismatches.
- A new cell can come from SQL, a source selector, a literal, or a bind. Field,
  bind, and expression semantics, including `source_field` and `source_target`
  lineage, remain available.

## Patch and Deparse Surface Preservation

- SELECT, INSERT, UPDATE, DELETE, and MERGE units in SQL Server and
  Vastbase-SQLServer control flow support local source edits. Unchanged
  branches retain their original line breaks, whitespace, parentheses,
  identifier delimiters, and case.
- CTE DML, `UNION ALL`, table hints, and multiline `DROP ... IF EXISTS`
  boundaries are no longer folded or reordered after a patch.
- Replacing a complete SELECT target wrapped in an ODBC `{fn ...}` scalar
  escape consumes the wrapper and does not leave a `{fn ` prefix behind.
- UPDATE assignments validate the `OUTPUT` boundary against an actual OUTPUT
  target. UPDATE OUTPUT target lists use a verifiable `FROM` or `WHERE`
  boundary for local edits and retain the safe fallback when no boundary can
  be proven.

## Compatibility

- `sqlparser_selector_kind_t` only appends
  `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN` and
  `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL`.
- Existing enum values, public function signatures, and public structure
  layouts remain unchanged. The shared-library ABI major remains
  `libsqlparser.so.0`.
- MERGE INSERT View objects add target-column selectors, cell selectors, and a
  `target_list_selector` when available. Consumers may read these additive
  fields as needed; existing field semantics are unchanged.

## Release Validation

- The nine executable dialect fixtures contain 2,755 cases with
  `status = "final"` and 8,918 independent patches.
- Every case checks byte-exact original deparse and expected View JSON. Every
  patch checks expected SQL, a second deparse after reparsing, and
  patched/fresh View equivalence.
- A complete remote `make test` run exited with status 0. All nine fixtures
  reported zero case, patch, original-deparse, View, patch-deparse, and runner
  failures.

Vendored `libpg_query` tag: `17-6.2.2`.
