# v2.15.1 Release Notes

`v2.15.1` adds structured parsing, View, and patch support for single-target,
single-host-bind `RETURN` / `RETURNING ... INTO` flows in Oracle, Dameng, and
Vastbase-Oracle while preserving unchanged SQL source text.

## Supported Boundary

- Oracle and Vastbase-Oracle support one `RETURNING` expression and one
  colon-prefixed `INTO` host bind for `INSERT`, `UPDATE`, and `DELETE`.
- Dameng supports `RETURNING ... INTO :bind` for `INSERT` and `DELETE`, and
  `RETURN ... INTO :bind` for `UPDATE`.
- The colon and bind name form one contiguous token, such as `:NAV_ROWID`.
  A spaced form such as `: NAV_ROWID` is outside this syntax.
- Multiple result targets, multiple output binds, and `BULK COLLECT` remain
  unsupported.

## Query Graph and View

- A DML result returned into a host bind uses a `kind = "sink"` result channel
  without a `sink_relation`.
- The result target's `sink_value` references the output bind in
  `query_graph.values[]`. That value retains its bind key, kind, SQL, global
  position, and existing value selector.
- `ROWID` is emitted as a pseudo target without a field. `INSERT` and `UPDATE`
  use `target_after` references; `DELETE` uses `target_before`.
- Relation-backed sinks continue to use `sink_relation` and optional
  `sink_columns`; their representation remains distinct from host-bind sinks.

## Patch and Deparse

- DML input values, result targets, and output binds reuse existing selectors
  and `SQLPARSER_PATCH_REPLACE`; no dedicated selector or patch type is added.
- Result targets and output binds can be replaced independently. A regenerated
  View retains the `sink_value` association, bind positions, and lineage.
- A patch changes only the selected source interval; all other source intervals
  remain byte-preserved.
- Empty statements and comment-only segments do not consume statement indexes
  in multi-statement SQL. Dameng `RETURN` letter case is restored during
  deparse.

## API and Compatibility

- `sqlparser_graph_target_t` adds `sink_value_index` and `has_sink_value` for
  the optional output-bind association.
- This release adds no public functions, enums, selector kinds, or
  resource-ownership rules.
- The public structure layout is extended. C applications should be rebuilt
  against the 2.15.1 headers.
- The shared-library ABI major remains `libsqlparser.so.0`.

## Cases and Documentation

- Nine final cases and 27 independent patches were added across Oracle,
  Dameng, and Vastbase-Oracle, covering DML input values, result targets, and
  output-bind rewrites for `INSERT`, `UPDATE`, and `DELETE`.
- The nine current fixtures contain 2,767 final cases and 8,972 patches.
- Repository examples, documentation, and test data use the neutral `APP`
  schema name. This convention does not restrict schema names in caller SQL.
- English and Chinese View JSON, API, dialect support, official syntax
  coverage, and case-matrix documentation are synchronized.

## Validation

- The Oracle, Dameng, and Vastbase-Oracle matrices completed 628 cases and
  2,219 patches with zero failures.
- The SQL batch matrix also completed 213 cases and 720 patches. The targeted
  strict regression therefore covered 841 cases and 2,939 patches.
- Affected core API checks, three examples, and CLI argument-order checks
  completed successfully.
- Targeted Valgrind checks for the three affected dialects reported no memory
  remaining at exit and zero errors.

Vendored `libpg_query` tag: `17-6.2.2`.
Vendored Jansson version: `2.15`.
