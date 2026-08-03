# Changelog

## 2.14.1

### Structured MERGE INSERT Addressing and Rewrites

- MERGE INSERT target columns and complete VALUES cells expose `merge_insert_column` and `merge_insert_cell` selectors, while an explicit target-column list exposes an `insert_branch_columns` selector. Root and nested MERGE statements use distinct coordinates that uniquely identify the DML, WHEN branch, and column within one statement.
- `SQLPARSER_PATCH_REPLACE` can replace one MERGE INSERT target column or complete cell. `SQLPARSER_PATCH_INSERT_COLUMN` and `SQLPARSER_PATCH_DELETE_COLUMN` use the target-list selector to atomically insert or remove the target column and value at the same position. New values continue to accept SQL, source-selector, literal, and bind inputs.
- Field, bind, literal, and expression semantics in MERGE INSERT cells retain their existing `source_field` and `source_target` lineage. Dialect modes that support MERGE share the same selector and patch rules.

### Patch Surface Preservation and Boundary Corrections

- Local patches in SQL Server and Vastbase-SQLServer control-flow SELECT, INSERT, UPDATE, DELETE, and MERGE units preserve original line breaks, whitespace, parentheses, identifier delimiters, and case in unchanged branches, including CTE DML, set queries, table hints, and multiline DDL boundaries.
- Corrected the SELECT-target replacement span for ODBC `{fn ...}` scalar wrappers so replacing the complete target does not leave a `{fn ` prefix behind.
- UPDATE assignments validate the `OUTPUT` boundary against the source position of the first actual OUTPUT target. UPDATE OUTPUT target lists use their actual `FROM` or `WHERE` boundary, preventing scans into the result list or a following control unit and avoiding whole-statement normalization.

### Compatibility and Validation

- `sqlparser_selector_kind_t` appends `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN` and `SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL`. Existing enum values, public function signatures, and public structure layouts remain unchanged.
- The nine executable dialect fixtures contain 2,755 final cases and 8,918 independent patches. A remote full `make test` run validated original deparse, View JSON, patched deparse, a second deparse after reparsing, and patched/fresh View equivalence, with all checks passing.

## 2.14.0

### Source-Surface Preservation for Patch and Deparse

- Patches apply local edits to source intervals that can be resolved safely. Unchanged text retains its original identifier case and delimiters, keywords, comments, whitespace, parentheses, and semicolons byte for byte. Patch fragments are still parsed in the selected dialect before entering the AST, and explicitly supplied delimiters are not duplicated.
- Renaming a relation without an explicit alias also updates qualified columns, qualified stars, and qualified assignment targets that bind uniquely to that relation. Same-scope ambiguity, inner-scope shadowing, explicit aliases, and SQL Server pseudo-relations such as `INSERTED` and `DELETED` remain unchanged.
- Replacing one SELECT target can splice a multi-target fragment at that position and does not inherit the replaced target's alias.

### Query Graph and View

- Compound UPDATE and MERGE assignments expose right-hand fields, values, and subquery entry blocks through `rhs_fields`, `rhs_values`, and `rhs_blocks`. Direct field, literal, bind, and default assignments continue to use the existing assignment payload.
- A statement can expose multiple peer DML roots. View uses `query_graph.dml` for one root and `query_graph.dmls` for multiple roots, while nested DML remains under `children`. Data-modifying CTEs follow the same rule.
- Added `SQLPARSER_CLAUSE_KIND_WINDOW_PARTITION` so the `PARTITION BY` list of a named window definition is independently addressable.

### Dialects and Structural Boundaries

- Expanded set-operation, multi-table DML, bind, and national-literal surface preservation for Oracle, Dameng, and Vastbase-Oracle. Set-tree traversal no longer depends on a fixed branch-count ceiling.
- Corrected comment boundaries, executable comments, index hints, partitions, and DML-tail restoration during parse and after patching for MySQL and Vastbase-MySQL.
- Corrected post-patch surface restoration for `OUTPUT`, MERGE, dynamic execution, transaction batches, and bracket identifiers in SQL Server and Vastbase-SQLServer.

### Compatibility and Validation

- `sqlparser_graph_dml_assignment_t` adds three public span fields, changing the public structure layout. C consumers must rebuild against the 2.14.0 header. View consumers must handle the mutually exclusive `dml` and `dmls` shapes.
- The nine executable dialect fixtures contain 2,752 final cases and 8,890 independent patches. The runner separately checks original deparse, View JSON structure, patched deparse, a second deparse after a fresh parse, and patched/fresh View equivalence.
- The release-candidate code completed one ASan run, one UBSan run, one Valgrind run, ten full regression loops, and the full benchmark. The benchmark executed 530,100 measured operations with zero error operations; this is a stability result, not a claim of improvement over a historical baseline.

## 2.13.0

### MERGE Branch Structure and Rewrites

- Query Graph projects every MERGE `WHEN` branch in source order, including
  its action, match kind, INSERT target columns and rows, and UPDATE assignment
  span. A conditional branch also includes its condition selector.
- Added `SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION`, MERGE action and
  match enums, two enum-name functions, and a MERGE branch-detail accessor.
  Branch conditions and assignments are addressable through selectors in
  both top-level and nested MERGE statements.
- MERGE UPDATE assignments retain their target-field, source-field,
  source-target, and bind relationships and remain readable, insertable,
  replaceable, and deletable through the existing assignment selectors and
  patch operations.
- Oracle, Dameng, and Vastbase-Oracle support action-level `WHERE` clauses on
  MERGE UPDATE and INSERT actions. Other dialects reject this syntax.

### INSERT VALUES and View Semantics

- Across all nine dialect modes, `INSERT ... VALUES` supports interleaved
  binds, literals, standalone `DEFAULT` values, functions, and compound
  expressions while preserving per-cell row and column coordinates,
  selectors, and global bind positions.
- Binds nested in expressions participate in global bind ordering.
  Time expressions such as `SYSDATE`, `CURRENT_TIMESTAMP`, `NOW()`, and
  `GETDATE()` are emitted as expression cells rather than field names.
- Added 90 mixed-VALUES regression cases across nine matrices, covering
  single and multiple rows, leading, trailing, and interleaved expressions,
  nested binds, delimited identifiers, and irregular whitespace.

### Identifiers and Deparse After Patches

- Identifiers introduced by a parsed SQL-fragment patch retain the fragment's
  case, delimiters, and escape spelling. Double quotes, MySQL backticks, and
  SQL Server brackets are neither replaced nor duplicated.
- An unchanged generation-`0` handle continues to return the original SQL
  byte for byte. For generations greater than `0`, SQL is serialized from the
  current AST. Identifier case, delimiters, and escape spelling are preserved,
  while the deparser may normalize whitespace between nodes.
- Identifier-spelling state is managed across successful rewrites, rollback
  after failed rewrites, handle cloning, and destruction, and does not
  accumulate across repeated failed patch operations.

### Performance, Compatibility, and Validation

- MERGE syntax validation uses an O(AST) primary depth-first protobuf
  traversal with local branch checks. During one build, Query Graph reuses
  statement-level lookup data, expression-source scan cursors, and the
  current-SQL cache to reduce repeated tree traversal, deparse, and source
  scanning.
- Public API changes are append-only; existing function signatures and public
  structure layouts remain unchanged. The shared-library ABI major remains
  `libsqlparser.so.0`, and the ABI export check covers 152 public symbols.
- All 2,535 expected-success cases across the nine dialect matrices run
  generation-`0` byte-exact deparse checks, AST identifier-spelling audits,
  and View construction, with field-level assertions for declared structured
  expectations.
- Release validation covers strict GCC 8.3 release and debug builds, the full
  test suite, install smoke, ABI, ASan, UBSan, Valgrind, and benchmark smoke.

## 2.12.0

### Deparse and AST Identifiers

- When a handle generation is `0`, a successful `sqlparser_deparse()` call
  copies the original input SQL byte for byte, preserving identifier
  delimiters and case, keywords, whitespace, line breaks, comments,
  semicolons, and multi-statement boundaries.
- AST name values originating from SQL identifier tokens retain the source
  token's letter case. A quoted identifier still stores decoded name content
  in the AST; its delimiters and escape spelling are preserved by the
  generation-`0` deparse contract.
- When the generation is greater than `0`, deparse serializes the current
  handle state, and the byte-for-byte guarantee no longer applies to the
  output as a whole.

### Session-State Query Graph

- Supported database, schema, role, identity, transaction-characteristic, and
  session-parameter statements are projected as structured session actions,
  scopes, targets, and values.
- Added session action, scope, target-kind, and value-kind enums;
  `sqlparser_graph_session_t`, `sqlparser_graph_session_item_t`, and
  `sqlparser_graph_session_value_t`; and three Query Graph accessors.
- View JSON emits an optional `query_graph.session` object for statements with
  an available session projection, covering identifier, keyword, literal,
  bind, and expression values.

### MERGE Matched-UPDATE Rewrites

- Added `SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT` and the
  `stmt[S].merge_assignment[W][A]` selector. `W` is the absolute zero-based
  ordinal across all `WHEN` clauses in the MERGE, and `A` is the zero-based
  assignment ordinal within the selected matched-UPDATE branch.
- The `update_assignment` selector APIs support reads, right-hand-side
  rewrites, and same-statement right-hand-value cloning for MERGE
  matched-UPDATE assignments. `SQLPARSER_PATCH_INSERT_ASSIGNMENT`,
  `SQLPARSER_PATCH_DELETE_ASSIGNMENT`, and
  `SQLPARSER_PATCH_REPLACE_ASSIGNMENT` support assignment insertion, deletion,
  and full replacement.
- Query Graph emits the corresponding selector for writable MERGE
  matched-UPDATE assignments.

### Compatibility and Validation

- Public API changes are append-only; existing function signatures and public
  structure layouts remain unchanged. The shared-library ABI major remains
  `libsqlparser.so.0`, and the ABI export check covers 149 public symbols.
- All expected-success cases in the nine PostgreSQL, MySQL, Oracle, SQL
  Server, Dameng, and four Vastbase compatibility matrices run the
  generation-`0` byte-exact deparse check and the AST identifier-spelling
  check.
- All nine dialect matrices include session-projection expectations, and
  MERGE-capable dialects include matched-UPDATE assignment selector and
  mutation regressions.
- Release validation covers strict GCC 8.3 release/debug builds, the full test
  suite, install smoke, ABI, ASan, UBSan, and Valgrind, plus a Windows VS 2022
  x64/MSVC 19.39 clean build and full test suite.

## 2.11.0

### Deparse

- Unchanged database, schema, table, column, alias, function, index,
  constraint, and CTE identifiers retain their source SQL spelling, including
  the original case of unquoted identifiers.
- Dialect conversion no longer mistakes text inside comments, strings, bind
  parameters, or quoted identifiers for source identifiers.
- Nodes created by a patch use their own identifiers instead of inheriting
  spelling from unrelated source nodes with the same name.

### Compatibility and Validation

- The public API, public structures, View JSON, Query Graph, and ABI remain
  unchanged. The ABI export count remains 146 public symbols.
- Identifier-spelling regression cases cover PostgreSQL, MySQL, Oracle, SQL
  Server, Dameng, and Vastbase compatibility modes.
- Release validation covers strict GCC 8.3 builds, the full test suite, ASan,
  UBSan, Valgrind, ABI checks, and per-call performance measurements.

## 2.10.1

### MySQL Dialect

- Fixed deparse placement for `USE INDEX`, `IGNORE INDEX`, `FORCE INDEX`, and
  their `KEY` forms. Index hints now remain after the table name or alias and
  before subsequent query clauses.
- Fixed index-hint ordering with `GROUP BY`, `HAVING`, `WINDOW`, set
  operations, locking clauses, `NATURAL JOIN`, `STRAIGHT_JOIN`, and `JOIN ...
  USING`.
- Fixed index-hint restoration for the right relation of `STRAIGHT_JOIN`.
- Applied the same fixes to Vastbase-MySQL compatibility mode.

### Compatibility and Validation

- The public API, public structures, View JSON, and ABI remain unchanged. The
  ABI export count remains 146 public symbols.
- The MySQL and Vastbase-MySQL dialect matrices each contain 173 supported
  cases.
- Release validation covers strict GCC 8.3 builds, the full test suite, ASan,
  UBSan, Valgrind, ABI checks, and the full Windows VS 2022 x64/MSVC 19.39
  test suite.

## 2.10.0

### SQL Server Dialect

- Added `INSERT` support with explicit or omitted `INTO`, covering `VALUES`,
  multi-row `VALUES`, `SELECT`, set queries, CTEs, and `DEFAULT VALUES`.
- Added `OUTPUT` result channels for `INSERT`, `UPDATE`, `DELETE`, and `MERGE`,
  including `INSERTED`, `DELETED`, source fields, `$action`, expressions,
  aliases, and binds.
- Added `OUTPUT ... INTO`, ordered sink/client channels, sink column lists, and
  nested DML where an outer `INSERT` consumes an inner DML `OUTPUT`.
- Added single-statement and `BEGIN...END` multi-statement `IF...ELSE`
  branches, `ELSE IF`, and nested control flow.
- Added matching compatibility coverage to Vastbase-SQLServer.

### Structured Traversal and Rewrite

- Added read-only control-flow structures and accessors for ordered roots,
  nodes, branches, items, and addressable condition statements.
- Extended Query Graph traversal for multiple DML nodes in one statement,
  nested-DML parentage, DML result channels, and result-field origins.
- Added selectors for DML result targets, sink relations, and sink columns;
  these selectors remain writable through `sqlparser_apply_patch()`.
- Public API additions are append-only. Existing function signatures and
  public structure layouts remain unchanged.

### Performance and Validation

- Ordinary non-control SQL does not build control-flow state. Control-flow
  state uses one contiguous allocation.
- The SQL Server and Vastbase-SQLServer dialect matrices each contain 546
  cases: 517 supported paths and 29 error or explicitly unsupported paths.
- Release validation covers strict GCC 8.3 builds, the full test suite, ASan,
  UBSan, Valgrind, ABI checks, and the full Windows VS 2022 x64/MSVC 19.39
  test suite.
- The ABI export check covers 146 public symbols.

## 2.9.0

### MySQL Dialect

- Added support for `INSERT ... SET`, `ON DUPLICATE KEY UPDATE` row aliases,
  aliased delete targets, and single-table `UPDATE` / `DELETE ... ORDER BY ...
  LIMIT` statements.
- Added support for `STRAIGHT_JOIN`, `JOIN ... USING`, `NATURAL JOIN`, locking
  reads, index hints, and query-table `PARTITION(...)` clauses.
- Added matching compatibility coverage to Vastbase-MySQL.

### Query Graph

- Fields in `JOIN ... USING` are emitted through the existing `fields[]` and
  `candidate_relations` structures.
- Repeated references to one CTE share its source block, unused CTEs remain in
  the graph, and recursive CTE references point to the registered block.
- Added `SQLPARSER_GRAPH_INSERT_MODE_SET` for the `INSERT ... SET` write form.
- Public structure layouts remain stable, and the ABI export count remains
  135.

### Performance and Validation

- MySQL extension syntax uses one feature-classification pass and runs each
  conversion path only when its syntax is present.
- The MySQL and Vastbase-MySQL dialect test matrices each contain 156
  supported cases.
- Release validation covers strict GCC 8.3 builds, the full test suite, ASan,
  UBSan, Valgrind, ABI checks, and an old-client shared-library compatibility
  test.

## 2.8.1

### Query Graph Performance

- Added a statement-level selector cache while building
  `sqlparser_statement_query_graph()`, recording value, name, relation, and
  select target-list selector indexes in one traversal.
- Large and deeply nested `query_graph` workloads no longer run a full
  statement protobuf tree search for each value, field, relation, or SELECT
  target list, especially improving `INSERT ... SELECT`, set queries, and
  nested SELECT statements.
- Kept the public API, ABI, selector output format, `query_graph` shape, and
  same-handle query graph cache behavior unchanged.

### Tests and Validation

- Release validation covers Linux unit tests, ASan, UBSan, Valgrind leak
  checks, and ABI export checks.

## 2.8.0

### Query Graph and DML Structured Output

- Added a predicate array to `query_graph` for comparisons, boolean
  combinations, `EXISTS`, and expression predicates.
- Added `sqlparser_graph_predicate_t`, predicate-kind enums, predicate boolean
  enums, and name helpers.
- Added source field / source target links to DML branches, assignments, and
  cells for field movement, `MERGE` source lineage, and multi-branch insert
  sources.
- Added database-link names to relation views and graph relations.
- Pseudo columns such as `ROWID` can be emitted as separate targets without
  polluting star lineage.

### Dialect Coverage

- Enhanced Oracle DML / SELECT structured output for alias-qualified `UPDATE`,
  `INSERT ALL` / `INSERT FIRST`, `MERGE` source lineage, `DISTINCT`,
  `ORDER BY`, and qualified star plus `ROWID`.
- Enhanced MySQL and Vastbase-MySQL coverage for multi-table `UPDATE` /
  `DELETE`, `REPLACE`, insert modifiers, CREATE TABLE options, and common
  hook-only syntax groups.
- Expanded SQL Server and Vastbase-SQLServer coverage for basic DDL,
  expressions, table/query hints, `FOR JSON`, nested `TOP`, full-text
  predicates, and official syntax coverage tracking.
- Expanded Dameng support for `ALTER SESSION` parameters, public `TOP`
  deparse, database links, national strings, and multi-table insert structured
  output.
- Expanded PostgreSQL and Vastbase-PostgreSQL executable coverage for
  notification statements, extensions, national strings, and related matrix
  cases.

### Strings and Compatibility Syntax

- MySQL, Oracle, PostgreSQL, Dameng, and matching Vastbase compatibility modes
  preserve public `N'...'` / `n'...'` national string forms.
- Oracle, Dameng, and Vastbase-Oracle preserve `nq'...'` national q-quoted
  string semantics.
- SQL Server and Vastbase-SQLServer preserve `N'...'` Unicode string prefixes
  and related prepared-statement forms.

### Tests and Validation

- Expanded the existing PostgreSQL, MySQL, Oracle, SQL Server, Dameng, and four
  Vastbase compatibility case matrices.
- Updated Chinese and English dialect support documents, official syntax
  coverage reports, and View JSON / API documentation.
- Release validation covers Linux unit tests, ASan, UBSan, Valgrind leak
  checks, and ABI export checks.

## 2.7.0

### Query Graph Operator Classification

- Added `operator_kind` to `sqlparser_graph_value_t` for structured
  classification of `LIKE`, `NOT LIKE`, `ILIKE`, and `NOT ILIKE`.
- Added `sqlparser_graph_operator_kind_t`,
  `sqlparser_graph_operator_kind_name()`,
  `sqlparser_graph_operator_is_like_pattern()`, and
  `sqlparser_graph_value_is_like_pattern()`.
- View JSON now emits `operator_kind` in `query_graph.values[]` when an
  `operator` is present. Pattern-match operators use `like`, `not_like`,
  `ilike`, or `not_ilike`; other operators use `unknown`.
- `LIKE ... ESCAPE ...` recognition now reuses the structured operator
  classification instead of requiring callers to compare operator strings.

### Tests and Validation

- Expanded the existing PostgreSQL, Oracle, MySQL, SQL Server, Dameng, and four
  Vastbase compatibility case matrices for pattern-match operator
  classification.
- Expanded core API regression coverage for the public enum, helper functions,
  View JSON output, non-pattern operator boundaries, and explicit `ESCAPE`
  preservation.
- Release validation covers Linux unit tests, ASan, UBSan, Valgrind leak
  checks, and ABI export checks.

## 2.6.0

### Vastbase Dialects

- Added four explicit Vastbase compatibility modes: `vastbase-oracle`,
  `vastbase-mysql`, `vastbase-postgresql`, and `vastbase-sqlserver`.
- Added public C enum values `SQLPARSER_DIALECT_VASTBASE_ORACLE`,
  `SQLPARSER_DIALECT_VASTBASE_MYSQL`,
  `SQLPARSER_DIALECT_VASTBASE_POSTGRESQL`, and
  `SQLPARSER_DIALECT_VASTBASE_SQLSERVER`.
- The CLI accepts all four Vastbase dialect names. `vastbase` is a
  deterministic alias for `vastbase-oracle`; the library does not infer a
  compatibility mode from SQL text.
- The four modes are wired through independent dialect hooks and keep their
  public SQL form, bind rules, and structured output rules.

### Tests and Validation

- Added four Vastbase case matrices covering parse, View JSON, deparse, and
  explicit unsupported-syntax return codes for the Oracle, MySQL, PostgreSQL,
  and SQL Server compatibility modes.
- Added Vastbase CLI batch cases and `examples/dialect/20_vastbase_dialect.c`.
- Updated Linux and Windows/MSVC build manifests so the new source file, unit
  tests, and example are built.
- Release validation covers Linux unit tests, ASan, UBSan, Valgrind leak
  checks, and ABI export checks.

## 2.5.0

### LIKE ESCAPE Structured Output

- Added `like_escape` to `sqlparser_graph_value_t` for explicit `ESCAPE`
  clauses attached to `LIKE`, `NOT LIKE`, `ILIKE`, and `NOT ILIKE` patterns.
- Added `sqlparser_graph_like_escape_kind_t` for no explicit `ESCAPE`, literal,
  prepared-placeholder, and expression escape forms.
- View JSON emits `like_escape` on the pattern value instead of exposing the
  escape clause as a separate business value.
- PostgreSQL, MySQL, Oracle, SQL Server, and Dameng public deparse keep
  `LIKE pattern ESCAPE escape` and do not expose the internal
  `pg_catalog.like_escape(...)` form.
- Structured recognition only accepts the `pg_catalog.like_escape` node emitted
  by libpg_query, so an unqualified user-defined `like_escape(...)` function is
  not misclassified as explicit `ESCAPE`.

### Tests and Validation

- Expanded the existing PostgreSQL, MySQL, Oracle, SQL Server, and Dameng case
  matrices for literal escapes, named binds, positional binds, JDBC `?` binds,
  expression escapes, no explicit escape, and derived-table cases.
- Expanded core API regression coverage for C structure fields, View JSON,
  public deparse, and user-defined `like_escape(...)` function boundaries.
- Release validation covers Linux unit tests, ASan, UBSan, Valgrind leak
  checks, and ABI export checks.

## 2.4.0

### UPDATE SET Rewrite

- `sqlparser_update_set_assignment_literal()` and the selector variant now
  support rewriting an `UPDATE SET` assignment right-hand side from a bind
  parameter to a literal.
- The target column, assignment order, and active dialect output rules are kept
  unchanged; only the assignment value AST node is replaced.
- Function calls, operator expressions, field references, `DEFAULT`, and
  subquery right-hand sides still return `SQLPARSER_STATUS_UNSUPPORTED` so the
  library does not generate ambiguous SQL.

### Tests and Validation

- Expanded the existing PostgreSQL, MySQL, Oracle, SQL Server, and Dameng case
  matrices for single-field, multi-field, named-bind, positional-bind, and JDBC
  `?` bind `UPDATE SET` source expressions.
- Expanded core API regression coverage for bind right-hand-side rewrites,
  backup assignment insertion, deparse-then-parse validation, and complex value
  rejection paths.
- Release validation covers Linux unit tests, ASan, UBSan, Valgrind leak
  checks, and ABI export checks.

## 2.2.0

### Structured SQL Fragment Rewrite

- Added `sqlparser_identifier_path_view_t` for passing dialect-neutral
  identifier paths to public rewrite APIs.
- Added structured `UPDATE SET` assignment builders so callers can generate,
  append, or replace assignments from a column path and value SQL.
- Added structured `SELECT` target replacement APIs so callers can replace one
  output item with a list of column paths.
- Added `examples/convenience/18_structured_fragment_rewrite.c` to demonstrate
  structured rewrites without assembling full SQL text by hand.

### Tests and Release Validation

- Expanded core API and robustness tests for structured identifier paths,
  invalid arguments, assignment append, target replacement, and deparse checks.
- Updated Linux and Windows/MSVC build manifests so the new source file and
  example are built.
- Release validation covers Linux unit tests, leak checks, ASan, UBSan, ABI
  export checks, and Windows/MSVC tests.

## 2.0.2

### View JSON

- Added `field_match_kind` to `query_graph.values[]` to distinguish values
  bound to direct fields from values bound to function-, cast-, expression-, or
  `CASE`-wrapped fields
- Exposed the same field through `sqlparser_graph_value_t` and added the
  `sqlparser_graph_field_match_kind_name()` helper
- Added direct-field and expression-field matching regressions for PostgreSQL,
  MySQL, Oracle, SQL Server, and Dameng

## 2.0.0

### View JSON

- Uses `query_graph` as the canonical structured-output source; View JSON is
  generated on demand only when callers request a JSON export
- Replaced the prepared-statement placeholder output field `bind` with
  `bind_key`, and added `bind_position` for the bind occurrence number inside
  the full input SQL text
- Updated `sqlparser_graph_value_t`, `sqlparser_graph_dml_cell_t`, and
  `sqlparser_graph_dml_assignment_t` to expose `bind_key`, `bind_kind`,
  `bind_position`, and `bind_sql`
- Unified View JSON, C structured traversal, and case matrix checks around
  `bind_key`, `bind_kind`, `bind_position`, `bind_sql`, and `selector`
- `bind_position` now increases globally across the full input SQL text, while
  `bind_key` keeps the placeholder key assigned by dialect preprocessing
- Placeholder-like text inside PostgreSQL dollar-quoted strings is excluded
  from global bind counting
- `IN`, `NOT IN`, `BETWEEN`, and single-column function-wrapped predicates now
  emit field-bound `values[]`
- Predicate expressions inside SELECT projections now emit field-bound
  `values[]`, such as `CASE WHEN phone = ? THEN ...`
- View JSON omits empty arrays from `query_graph` and DML structures, while the
  public C structs still represent empty collections through `count` or `has_*`
  fields
- MySQL `INSERT ... ON DUPLICATE KEY UPDATE` and ordinary/INNER/CROSS
  multi-table `UPDATE ... JOIN` / `DELETE ... JOIN` forms with `ON`
  conditions are covered by the supported matrix, with View JSON exposing DML
  targets, sources, assignments, values, and bind mapping

### Tooling and Baselines

- CLI batch input is limited to a top-level array or an `items` array; the old
  `sqls` alias is no longer accepted
- The `libpg_query` baseline now covers single-thread successful parsing and
  first-parse samples only, without error-path or concurrent-path baselines

### Robustness

- Fixed ownership handling for Jansson `_new` APIs on View JSON serialization
  failure paths, avoiding duplicate releases of intermediate JSON nodes under
  low-memory conditions

## 0.8.0

### Dialect Capabilities

- Added Oracle ordinary `ALTER SESSION SET <parameter> = <value>` session parameter assignments for string, identifier, numeric, and boolean/enumerated values
- Kept Oracle `ALTER SESSION` public output in the original parameter/value form without exposing internal conversion prefixes
- Fixed MySQL parameterized comma pagination for `LIMIT ?, ?` while preserving the public MySQL deparse form

### Tests and Coverage

- Expanded the existing PostgreSQL, MySQL, Oracle, SQL Server, and Dameng case matrices for additional DDL, DML, JOIN, function, expression, bind, pagination, `USE`, `SET SCHEMA`, and `ALTER SESSION SET` scenarios
- Added Oracle regression coverage for `ALTER SESSION SET NLS_DATE_FORMAT`, `NLS_DATE_LANGUAGE`, `NLS_NUMERIC_CHARACTERS`, `INSTANCE`, and `ERROR_ON_OVERLAP_TIME`
- Updated the Chinese and English case matrices, dialect coverage summary, and Oracle official syntax coverage summary

## 0.7.0

### UPDATE SET Rewrite

- Added assignment-level patch support for `UPDATE SET`, allowing callers to append, delete, and replace full assignments through `stmt[n].assignment[i]` selectors
- Added `SQLPARSER_PATCH_INSERT_ASSIGNMENT`, `SQLPARSER_PATCH_DELETE_ASSIGNMENT`, and `SQLPARSER_PATCH_REPLACE_ASSIGNMENT`
- Added `sqlparser_update_insert_assignment_sql()`, `sqlparser_update_delete_assignment()`, `sqlparser_update_set_assignment_full_sql()`, and the corresponding selector APIs
- Preserved the existing RHS-only assignment behavior of `SQLPARSER_PATCH_REPLACE`

### Tests and Documentation

- Added `examples/patch/17_update_set_patch.c` to demonstrate `UPDATE SET` assignment append, delete, and full replacement through `sqlparser_apply_patch()`
- Expanded core API and robustness tests for Oracle bind fragments, invalid selectors, out-of-range indexes, empty-`SET` protection, and handle usability after failures
- Updated the Chinese and English API reference, View JSON guide, examples guide, and MSVC example build list

## 0.6.0

### View JSON Structures

- Made the `query_graph` C structures the canonical structured-output source; `sqlparser_export_view_json()` now serializes JSON on demand from `query_graph`
- Extended `sqlparser_graph_field_t` and `sqlparser_graph_dml_cell_t` with bind name, bind kind, original bind SQL, bind selector, clause id, and SELECT target path fields
- Added the public `sqlparser_bind_kind_t`, `sqlparser_bind_kind_name()`, `sqlparser_statement_clause()`, and `sqlparser_clause_sql()` APIs
- Extended `sqlparser_clause_kind_t` with `on`, `group_by`, and `having` clause kinds
- Removed the old `target_kind`, `target_name`, and `target_arg_index` JSON fields in favor of ordered `target_path` entries for SELECT output hierarchy

### Semantics and Dialects

- Classify binds as positional or named while preserving `bind_sql` for original forms such as `?`, `:1`, `:name`, `$1`, and `@name`
- Stop exposing bind placeholders as ordinary `value` payloads, so callers do not confuse placeholders with literal values
- Stop exposing SELECT output expression operators and values as condition fields; output shape is represented through `target_path`
- Preserve complete public SQL operator text for `NOT IN`, `NOT LIKE`, `NOT ILIKE`, and `NOT SIMILAR TO`

### Tests and Documentation

- Expanded PostgreSQL, MySQL, Oracle, SQL Server, and Dameng case matrices for additional SELECT, INSERT, UPDATE, DELETE, JOIN, function, expression, and bind scenarios
- Added View JSON public C-structure semantic tests to verify consistency between the public structs and View JSON
- Added generic assertions for bind fields, cell binds, `clause_id`, and `target_path`
- Updated the Chinese and English API reference and View JSON guide

## 0.5.0

### View JSON Semantics

- Added SELECT target semantic paths through `target_path` for functions, expressions, CASE, and nested output hierarchy
- Added clause attribution ids to View JSON so field references can be associated with SELECT, WHERE, JOIN/ON, ORDER BY, and related locations
- Expanded dialect View JSON semantic coverage for function outputs, expression outputs, star outputs, nested SELECT, and bind predicates

## 0.4.0

### Dialect Capabilities

- Added the Dameng `SQLPARSER_DIALECT_DAMENG` conversion layer, covering
  `SET SCHEMA`, `MINUS`, `LIMIT`, `TOP`, binds, common DML/DDL, transactions,
  and privilege statements
- Added Dameng public-output rules so deparse and View JSON do not expose
  internal parameter names or internal conversion SQL
- Added prepared / parameterized SQL coverage for PostgreSQL, MySQL, Oracle,
  SQL Server, and Dameng, including SQL Server `sp_executesql` and Dameng
  `EXEC SQL PREPARE`

### View JSON and Rewrite

- Added generic `SELECT` output-list read, replace, insert, and delete APIs
- Added generic `WHERE` condition read, set, and `AND` / `OR` append APIs
- Added generic statement-level `clause` selectors for rewriting `select_list`,
  `where`, and `order_by` through `stmt[n].clause[m]`
- Added `query_graph` to View JSON for writable
  statement-level clause slots

### Tests and Documentation

- Added examples for `SELECT` output-list and `WHERE` condition rewrites
- Added a generic `clause` patch example covering SELECT output lists, WHERE
  conditions, and ORDER BY insertion
- Grouped examples into `patch`, `convenience`, `inspect`, and `dialect`;
  integration code should start with the `patch` examples
- Added WHERE rewrite regression coverage for PostgreSQL, MySQL, Oracle, and
  SQL Server, covering every PostgreSQL AST type that exposes `where_clause`
- Added the Dameng dialect case matrix, official syntax coverage summary, CLI
  batch fixture coverage, and dialect example
- Updated prepared / bind case matrices, dialect coverage summaries, and
  official syntax coverage summaries

## 0.3.0

### Dialect Capabilities

- Added structured output for PostgreSQL `SET search_path`, `SET LOCAL
  search_path`, and `SET SCHEMA`
- Added MySQL `USE db_name` support
- Added SQL Server `USE database_name` support
- Added Oracle `ALTER SESSION SET CURRENT_SCHEMA`, `ALTER SESSION SET
  CONTAINER`, and `ALTER SESSION SET CONTAINER ... SERVICE ...`
- Fixed `USE`, `SET SCHEMA`, and `ALTER SESSION SET` handling in
  multi-statement input so parse, View JSON, and deparse retain the public
  dialect form

### View JSON and Rewrite

- These statements reuse the existing `query_graph values` structure; no
  separate JSON format is introduced
- `stmt[n].value[m]` selectors can rewrite the corresponding values and
  deparse back to the appropriate dialect SQL
- Fixed edge cases where SQL Server, MySQL, and Oracle deparse could expose
  internal `sqlparser_current_*` sentinel names

### Tests and Documentation

- Added multi-statement `USE` / `ALTER SESSION SET` regression cases for
  MySQL, Oracle, and SQL Server
- Updated dialect support docs, official syntax coverage checklists, and
  executable coverage summaries

## 0.2.0

### Core Capabilities

- Stable public C API for `sql -> handle -> rewrite -> deparse`
- Parsing and structural inspection for `SELECT`, `INSERT`, `UPDATE`,
  `DELETE`, `MERGE`, transaction control, and common DDL
- Precise rewrites for relation names, name atoms, literals, `WHERE` literals,
  `UPDATE` assignments, and `INSERT` cells
- Expression-level rewrite support for `DEFAULT` and arbitrary SQL expressions
- View JSON export, `query_graph` C structured traversal, and structured patch
  write-back
- On-demand structured View JSON export
- Configurable resource limits for SQL input, expression SQL fragments,
  generated output, and statement count
- Dialect framework with PostgreSQL as the default and MySQL / Oracle /
  SQL Server dialect conversion layers
- Reduced the default generated-output limit to 4 MB and removed avoidable
  resident AST and string copies from parse/deparse paths

### Packaging and Build

- Pinned vendored `libpg_query` version stored in the repository
- Public release surface for the header, static library, shared library, and
  `pkg-config` file
- Strict-build, install-smoke, `valgrind` leak-check, loop-regression,
  benchmark-smoke, and one-shot `verify` entry points
- Build invalidation based on compiler-option signatures for project objects and
  vendored parser objects
- Added `make abi-check` to verify shared-library exports against the public
  header
- Added Linux/GCC GitHub Actions CI gates
- Extended CI with JSON fixture validation and source-package smoke
- Added `make dist` for source release packages
- Added a Windows/MSVC NMake build entry point for the static library, CLI, unit
  tests, and examples
- The Windows/MSVC build uses the vendored Jansson source and does not require an
  external package manager

### Tests and Performance

- Expanded SQL fixture coverage for subqueries, `CASE`, window functions,
  `ON CONFLICT`, `RETURNING`, `UPDATE ... FROM`, `DELETE ... USING`, `MERGE`,
  transaction control, common DDL, `GRANT/REVOKE`, and maintenance statements
- Added a MySQL dialect case matrix covering supported statement shapes and
  explicitly unsupported syntax
- Added an Oracle dialect case matrix covering supported statement shapes,
  public output rules, and explicitly unsupported syntax
- Added a SQL Server dialect case matrix covering supported T-SQL statement
  shapes, public output rules, and explicitly unsupported syntax
- Added installed-library API smoke coverage, `valgrind` leak checks, and
  expression-rewrite regression
- Added stability regression for malformed SQL, argument validation, resource
  limits, and failed-rewrite rollback
- Extended benchmarks for read paths, rewrite paths, and `rewrite + deparse`
  single-call measurements
- Added capability-grouped test entry points for parse, inspect, rewrite,
  deparse, View JSON, CLI, install smoke, and ABI

### Documentation

- Chinese and English quick-start guides, API reference, View JSON guide, CLI
  guide, and architecture guide
- Added Oracle and SQL Server dialect support notes and `v0.2.0` release notes
- Added public changelog
