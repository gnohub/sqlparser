# Changelog

## 2.16.7

### Independent INSERT Column-Name Patches

- For a regular single-table `INSERT ... VALUES`, existing `SQLPARSER_PATCH_INSERT_COLUMN` now adds only a column name when given only `name/index`, without modifying VALUES. Supplying one value source retains the existing paired column/value insertion. One patch list may combine multiple name-only patches, paired insertion, and `REPLACE insert_cell`; every row must match the final column count before commit, or the whole batch rolls back.
- Oracle, Dameng, and the Vastbase-Oracle compatibility entry provide the same branch-scoped capability for the currently modeled explicit single-VALUES branches of `INSERT ALL/FIRST`. Other branches and the source SELECT remain unchanged, and touched branches are validated before commit. MERGE INSERT still requires paired insertion; `DEFAULT VALUES`, MySQL `INSERT ... SET`, branches without `VALUES`, and multiple tuples per branch remain outside this scope.
- The implementation reuses the existing patch operation, selectors, and transaction candidate. It adds no public API, enum value, View JSON schema field, or persistent state.

### Validation

- Direct core-API regressions were added; the nine fixture totals remain 2,831 final cases and 9,136 patches. After the functional changes, the full remote `make test` suite passed; the targeted core-API test passed, and Valgrind reported `0 bytes in 0 blocks` and zero errors.

## 2.16.6

### Query Graph Identifier Delimiter Metadata

- `sqlparser_graph_relation_t` adds `alias_quoted_identifier`, and `sqlparser_graph_target_t` adds `output_quoted_identifier`. They report whether a relation alias or target output name uses an identifier delimiter; View JSON emits the corresponding field only when its value is `true`.
- An explicit output alias determines `output_quoted_identifier`. Without an explicit alias, an output name taken directly from a field inherits that field's delimiter state. Double quotes, backticks, and brackets are recognized; a `U&` prefix is not counted separately.
- No public export symbols, dynamic allocations, or resource-ownership rules were added. On the supported x86_64 and AArch64 layouts, existing member offsets and `sizeof` remain unchanged for the affected structures.

### Cases and Validation

- Nine final cases and 18 patches were added. The nine fixtures now contain 2,831 final cases and 9,136 patches.
- The full remote `make test` suite passed. The ABI/export check remains at 154 public symbols, and the targeted identifier Valgrind check reported `0 bytes in 0 blocks` and zero errors.

## 2.16.5

### Multi-Table UPDATE

- MySQL and the Vastbase-MySQL compatibility entry support multiple write targets across JOIN chains and comma-separated table lists. Each assignment is associated with the relation identified by its qualifier, and mixed-target updates do not expose a single `dml.target_relation`.
- Dameng supports multi-table UPDATE statements with JOIN or comma-separated table lists while requiring every assignment to target the same table object. Cross-target, unknown, or ambiguous qualifiers return `SQLPARSER_STATUS_UNSUPPORTED`.
- Assignment and relation mutations continue to use the existing selectors and transaction candidate. A failure preserves the original handle, SQL, View, and generation.
- This release adds no public C declarations, View JSON fields, or resource-ownership rules. Query Graph continues to express write ownership through each assignment's existing `target_field` and the field's `relation`.

### Cases and Validation

- Sixteen final cases and 41 patches were added. The nine fixtures now contain 2,822 final cases and 9,118 patches.
- The full remote `make test` suite passed. Four targeted Valgrind checks covering the core API, MySQL, Vastbase-MySQL, and Dameng each reported `0 bytes in 0 blocks` and zero errors.

## 2.16.4

### Assignment-List Selection and Rewrite

- Root `INSERT` conflict-update lists use `stmt[S].assignment[A]`, while nested `UPDATE` assignment lists use `stmt[S].assignment[D][A]`. Assignment insertion, full replacement, and deletion support both targets.
- MySQL and Vastbase-MySQL cover `ON DUPLICATE KEY UPDATE`; PostgreSQL and Vastbase-PostgreSQL cover `ON CONFLICT DO UPDATE` and nested `UPDATE` statements in data-modifying CTEs; SQL Server and Vastbase-SQLServer cover nested `UPDATE` statements with `OUTPUT`.
- The View JSON schema, public C declarations, and resource-ownership rules are unchanged. Assignment-selector output is expanded, and some MySQL conflict-update items now use assignment selectors instead of their previous value selectors. Paired column/value mutation for `INSERT ... SET` is an existing capability; this release adds regression coverage only.

### Cases and Validation

- The full remote `make test` suite passed. The six affected dialect matrices cover 2,158 final cases and 6,798 patches. Targeted Valgrind checks for the PostgreSQL, MySQL, and SQL Server base-dialect matrices each reported `0 bytes in 0 blocks` and zero errors.
- Ten final cases and 28 patches were added. The nine fixtures now contain 2,806 final cases and 9,077 patches.

## 2.16.3

### Complete Bind Occurrence Access

- Added handle-level `sqlparser_handle_bind_occurrences()` and `sqlparser_bind_occurrence_at()` APIs that return every real placeholder in actual order across the current SQL. Repeated items remain separate, and positions continue across statements.
- Each occurrence exposes its `position`, `kind`, `key`, and complete `sql`, following the placeholder boundaries of all nine dialect entries. An effective rewrite invalidates the old view; a failed or effective no-op mutation preserves it.
- Existing Query Graph bind fields and call paths remain unchanged for semantic associations. The complete list is exposed independently and is not added to View JSON.

### Validation

- The strict incremental build, two targeted tests, and all nine dialect matrices passed, covering 2,796 final cases and 9,049 patches. The ABI/export check passed with 154 public symbols, and both targeted Valgrind checks reported `0 bytes in 0 blocks` and zero errors.

## 2.16.2

### Multiple DML Result Receivers

- `INSERT`, `UPDATE`, and `DELETE` in Oracle and the Vastbase-Oracle compatibility entry now support `N >= 1` `RETURNING` targets paired ordinally with exactly N colon-prefixed host binds. Dameng provides the same equal-length pairing with `RETURNING` for `INSERT` and `DELETE` and `RETURN` for `UPDATE`.
- Query Graph continues to use the existing sink result channel. Each result target's `sink_value` points to the output bind at the same ordinal. The internal AST sidecar stores one pair count instead of maintaining two redundant equal counts.
- `BULK COLLECT`, non-colon-bind receivers, empty lists, and unequal target/receiver counts in the Oracle-family or Dameng input return `SQLPARSER_STATUS_UNSUPPORTED`.

### Atomic Paired Patches

- Existing `SQLPARSER_PATCH_INSERT_COLUMN` now accepts a `dml_result_targets` list selector for paired DML results: `index` is the common insertion position, `default_sql` supplies the target SQL, and `name` supplies the receiver. Both sides are inserted atomically in one transaction candidate; a parse, index, receiver, or commit failure leaves the original handle unchanged.
- SQL Server and the Vastbase-SQLServer compatibility entry support same-ordinal insertion into `OUTPUT ... INTO sink(columns...)` when the explicit sink column list is nonempty and its count equals the OUTPUT target count before the mutation. Otherwise-valid unequal `OUTPUT` lists remain parseable and deparseable but do not support this paired patch. Client `OUTPUT` and channels without an explicit sink column list retain their existing boundary.
- PostgreSQL and Vastbase-PostgreSQL `RETURNING` are client result lists without a matching SQL receiver list and continue to use the existing target-only mutation. MySQL and Vastbase-MySQL do not gain DML `RETURNING INTO` syntax.

### API, Cases, and Validation

- This release adds no public functions, public enum values, public structure fields, View JSON fields, or resource-ownership rules. Existing selectors, patch operations, and result-channel representations are unchanged.
- Fifteen final cases and 15 independent paired patches were added across Oracle, Dameng, SQL Server, Vastbase-Oracle, and Vastbase-SQLServer. Each applicable entry has `INSERT`, `UPDATE`, and `DELETE` cases with eight original pairs and one head, middle, or tail insertion producing exactly nine pairs. The nine fixtures now contain 2,796 final cases and 9,049 patches.
- The remote strict core API test and all five affected dialect matrices passed. The five matrices covered 1,876/1,876 cases and 5,996/5,996 patches, with zero original-deparse, View JSON, or patch-deparse failures. Six targeted Valgrind checks covering the core API and the five matrices each reported `0 bytes in 0 blocks` and zero errors.

## 2.16.1

### Compact Query Graph DML Cell Cache

- Internal DML-cell records fell from 456 B to 80 B, with no public API or View
  JSON changes.
- Retained Query Graph memory for 20,000 literal cells fell from 14.659 MiB to
  1.685 MiB, an 88.504% reduction.
- Relevant regressions, five targeted Valgrind Memcheck runs, and the ABI check
  passed.

## 2.16.0

### Deparser Memory

- When neither pretty-print nor commas-at-line-start is enabled, the fallback
  deparser no longer creates a `DeparseStatePart` at each comma. It writes the
  same `", "` produced by the previous merge; both formatting paths remain
  unchanged.
- For 16 projections, fallback-deparse requested bytes fell from 30,970 B to
  6,394 B and peak live bytes fell from 24,640 B to 5,310 B. For 256
  projections, the corresponding figures fell from 610,810 B to 152,058 B and
  from 516,160 B to 117,208 B. Both outputs remained byte-identical to their
  pre-change baselines.

### Patch AST Lifecycle

- A successful, non-no-op patch now releases the transaction candidate's
  unpacked AST before transferring the candidate into the original handle.
  Packed-tree, surface-SQL, generation, persisted identifier/dialect semantics,
  and derived-cache rebinding rules are unchanged. Failures and semantic
  no-ops continue to leave the original handle unchanged.
- Control-condition rendering now obtains the current statement node before
  reading AST state. When fallback deparse needs dialect postprocessing and no
  AST is present, it rebuilds and binds the AST for that operation and releases
  it before returning.
- Retained bytes after a single replacement fell from 1,700 B to 364 B. After
  four consecutive apply operations on one handle, final retained bytes fell
  from 1,505 B to 169 B with no per-operation growth.

### Compact Query Graph Cache

- The Query Graph cache now uses private compact target and value records;
  public accessors reconstruct the complete public structures on demand. On
  Linux LP64, target records fell from 224 B to 104 B and value records fell
  from the 800 B public layout to 88 B.
- Unused empty-record construction paths for target and value cache entries
  were removed. An unexpected null target/value source or null target
  identifier now returns `SQLPARSER_STATUS_INTERNAL_ERROR`. These checks cover
  internal consistency failures only; public API layouts and successful-path
  behavior are unchanged.
- Bind text is owned by one contiguous cache text pool. `LIKE ... ESCAPE` uses
  a 56 B sparse record only for values that actually have an escape. Target,
  value, block, and ordinary index arrays start at four elements. After graph
  construction, the implementation makes at most one best-effort attempt to
  shrink each target, value, value-text, LIKE-escape, and index-pool buffer to
  its used size.
- Allocator-payload measurements for the complete Query Graph cache were
  20.66 KiB for 100 projections, 26.30 KiB for 129, 40.19 KiB for 200, and
  51.12 KiB for 256.

### API, Compatibility, and Validation

- The public C API, public enum values, public structure layouts, View JSON
  schema, and resource-ownership rules are unchanged. The shared library still
  exports 152 public symbols and retains the `libsqlparser.so.0` SONAME.
- The strict build and full `make test` suite passed. All 2,781/2,781 cases and
  9,034/9,034 patches in the nine case matrices passed, together with every
  unit, example, and CLI target. Twenty-four complete
  AST/Graph-to-Patch-to-Graph-rebuild-to-fallback-deparse chains finished
  within the configured timeout. The Memcheck run reported 23,129 allocations,
  23,129 frees, `0 bytes in 0 blocks`, and zero errors; Helgrind reported zero
  errors.
- The complete `make verify-valgrind` matrix passed. Unit tests, dialect
  matrices, examples, CLI batches, and install smoke all reported
  `0 bytes in 0 blocks` and zero errors. ABI validation retained 152 public
  symbols, and install smoke confirmed `2.16.0` through both version text and
  the public API.

## 2.15.4

### Unified Mutation Transactions

- All 40 existing statement/index and selector convenience mutators remain
  available and now execute through the transaction candidate used by
  `sqlparser_apply_patch()`. Patch dispatch calls internal in-place primitives
  instead of calling back into public mutators.
- Each convenience mutation commits at most once. A failure or effective no-op
  leaves the original handle and generation unchanged; an actual change
  increments the generation once and invalidates derived caches under the same
  rule.
- Unused direct structured-mutation paths and the second candidate clone for
  multi-table INSERT cells were removed. Failed mutations continue to roll
  back with the transaction candidate.

### Pagination Families

- The private `SelectStmt` and protobuf model now distinguish `LIMIT`,
  `OFFSET ... ROWS`, `FETCH FIRST`, and `FETCH NEXT`. A full-AST deparse no
  longer downgrades Oracle or Vastbase-Oracle `OFFSET ... ROWS` or
  `[OFFSET ... ROWS] FETCH FIRST|NEXT ... ROWS ONLY` to `LIMIT`.
- PostgreSQL / Vastbase-PostgreSQL retain either the `LIMIT` or standard
  `OFFSET ... FETCH` family; MySQL / Vastbase-MySQL retain the `LIMIT` family;
  SQL Server / Vastbase-SQLServer retain `OFFSET ... FETCH` while `TOP`
  remains independently restored; and Dameng continues to distinguish `TOP`,
  `LIMIT`, and standard `OFFSET ... FETCH`.
- Local source edits continue to byte-preserve unchanged regions. A full-AST
  fallback may canonicalize `ROW` to `ROWS` and Dameng `LIMIT offset,count` to
  the semantically equivalent `LIMIT count OFFSET offset`. This release does
  not add `FETCH ... PERCENT` semantics; existing SQL Server and Dameng
  `TOP ... PERCENT [WITH TIES]` support is unchanged.

### API, Compatibility, and Validation

- This release adds or removes no public functions, public enum values, public
  structure fields, or resource-ownership rules. `LimitClauseStyle` belongs
  only to the vendored parser AST/protobuf and is not part of the public View
  or Query Graph.
- No fixture cases were added; the nine fixtures still contain 2,781 final
  cases and 9,034 patches. The full `make test` suite passed. Targeted Valgrind
  checks for the core API, identifier spelling, robustness, and pagination
  dialect state each exited with `0 bytes in 0 blocks` and zero errors.
- The vendored `libpg_query` tag remains `17-6.2.2`. Its protobuf generation
  script explicitly preserves existing field numbers and emits the pagination
  field, enum, and existing `String.location`.

## 2.15.3

### Oracle and Dameng Hierarchical Queries

- Oracle supports `START WITH ... CONNECT BY [NOCYCLE] ...` and recognizes
  `PRIOR`, `LEVEL`, `CONNECT_BY_ROOT`, `CONNECT_BY_ISLEAF`, and
  `CONNECT_BY_ISCYCLE` within hierarchical queries.
- Dameng supports `START WITH`, `CONNECT BY [NOCYCLE]`, `PRIOR`, `LEVEL`, and
  `CONNECT_BY_ROOT`, and preserves both `START WITH ... CONNECT BY ...` and
  `CONNECT BY ... START WITH ...` source orders.
- Vastbase-SQLServer supports only the verified basic `CONNECT BY` form. This
  release does not extend `START WITH`, `PRIOR`, `NOCYCLE`, or
  `CONNECT_BY_ROOT` to that mode.

### Query Graph and Patch

- `sqlparser_clause_kind_t` appends
  `SQLPARSER_CLAUSE_KIND_START_WITH = 11` and
  `SQLPARSER_CLAUSE_KIND_CONNECT_BY = 12`. Hierarchical conditions reuse
  `fields[]`, `values[]`, and `predicates[]` instead of adding a dedicated
  hierarchy object.
- `sqlparser_graph_field_t` appends `pseudo` and `prior`, while
  `sqlparser_graph_predicate_t` appends `nocycle`. `CONNECT_BY_ROOT` is
  represented through the existing `target_path`; no target kind, selector,
  or patch type is added.
- Relation, field, value, and SELECT-target changes reuse existing selectors
  and patch mechanisms. Replacements with an exact source interval use local
  source edits. After a patch, unchanged hierarchical-clause order,
  whitespace, case, and identifier delimiters remain byte-preserved.

### Cases, API, and Validation

- Nine final cases and 45 independent patches were added across Oracle,
  Dameng, and Vastbase-SQLServer. The nine current fixtures contain 2,781
  final cases and 9,034 patches.
- Dialect regressions passed for Oracle (248 cases / 849 patches), Dameng
  (174 / 633), and Vastbase-SQLServer (601 / 1,847), together with the core API
  tests. Original deparse, View JSON, and patch-deparse failure counts were all
  zero. Targeted Valgrind checks for the relevant dialect targets exited with
  `0 bytes in 0 blocks` and zero errors.
- This release adds no public functions or resource-ownership rules. The
  public enum gains values and public structures gain fields, so C
  applications should be rebuilt against the 2.15.3 headers.

## 2.15.2

### Attached Delete Predicates in MERGE Matched Updates

- Oracle and Dameng support `UPDATE SET ... [WHERE ...] DELETE WHERE ...`
  inside a matched UPDATE action. The attached delete predicate remains on the
  same UPDATE branch and does not create an independent DELETE branch.
- The Query Graph UPDATE branch exposes `delete_condition_selector` for the
  attached delete predicate. The ordinary action `WHERE` continues to use
  `condition_selector`; both predicates can coexist and be read or replaced
  independently.
- PostgreSQL and SQL Server `WHEN MATCHED ... THEN DELETE` actions remain
  independent MERGE branches and stay distinct from the Oracle/Dameng
  attached-delete semantics.

### Patch and Source Preservation

- `sqlparser_selector_clause_sql()`, `sqlparser_selector_set_clause_sql()`,
  and `SQLPARSER_PATCH_REPLACE` support MERGE branch conditions and attached
  delete conditions.
- A MERGE assignment bounded by a comma, action `WHERE`, attached
  `DELETE WHERE`, or a following `WHEN` uses a local source edit. Replacing an
  assignment or condition preserves unchanged line breaks, whitespace, case,
  identifier delimiters, and other branches.
- The internal `MergeWhenClause` and protobuf schema append a dedicated
  `delete_condition` field with field number 7. The grammar accepts the
  attached DELETE form only when `WHERE` and a condition expression are
  present.

### API, Cases, and Validation

- `sqlparser_selector_kind_t` appends
  `SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION = 25`.
  `sqlparser_graph_dml_branch_t` appends `delete_condition_selector` and
  `has_delete_condition_selector`. This release adds no public functions or
  resource-ownership rules. C applications should be rebuilt against the
  2.15.2 headers.
- Five final cases and 17 independent patches were added across Oracle,
  Dameng, PostgreSQL, and SQL Server. The nine current fixtures contain 2,772
  final cases and 8,989 patches.
- All nine case matrices and the core API tests passed with zero original
  deparse, View JSON, or patch-deparse failures. A targeted Valgrind run exited
  with `0 bytes in 0 blocks` and zero errors.

## 2.15.1

### DML Results Returned into Host Binds

- Oracle and Vastbase-Oracle support one `RETURNING` expression and one
  colon-prefixed `INTO` host bind for `INSERT`, `UPDATE`, and `DELETE`.
- Dameng supports `RETURNING ... INTO :bind` for `INSERT` and `DELETE`, and
  `RETURN ... INTO :bind` for `UPDATE`.
- Query Graph represents the return flow as a sink result channel. The result
  target's `sink_value` references the output bind in `query_graph.values[]`.
  `INSERT` and `UPDATE` use `target_after` lineage; `DELETE` uses
  `target_before` lineage.

### Patch and Source Preservation

- DML input values, result targets, and output binds reuse existing selectors
  and `SQLPARSER_PATCH_REPLACE`; no dedicated patch type is introduced.
- After a result-target or output-bind patch, unchanged identifier delimiters,
  letter case, whitespace, keywords, and bind spelling remain byte-preserved.
- The supported boundary is one result target and one colon-prefixed host
  bind. Multiple result targets, multiple output binds, and `BULK COLLECT`
  remain unsupported.

### API, Cases, and Validation

- `sqlparser_graph_target_t` adds `sink_value_index` and `has_sink_value`.
  This release adds no public functions, enums, or resource-ownership rules.
  Because the public structure layout changes, C applications should be
  rebuilt against the 2.15.1 headers.
- Nine final cases and 27 independent patches were added across Oracle,
  Dameng, and Vastbase-Oracle. The nine current fixtures contain 2,767 final
  cases and 8,972 patches.
- Repository examples, documentation, and test data use the neutral `APP`
  schema name. This convention does not restrict schema names in caller SQL.
- Targeted strict regression covered 841 cases and 2,939 patches with zero
  failures. Affected core API checks, three examples, and CLI argument-order
  checks passed. Targeted Valgrind checks for the three affected dialects
  reported no memory remaining at exit and zero errors.

## 2.15.0

### Linux AArch64 Builds

- Make configuration accepts `CROSS_COMPILE` and derives `CC`, `AR`, `RANLIB`,
  `NM`, and `READELF` from the prefix. Native Linux builds continue to use the
  standard toolchain when the prefix is empty.
- `scripts/build_linux_aarch64.sh` performs incremental cross builds in the
  isolated `build/linux-aarch64`, `bin/linux-aarch64`, and
  `lib/linux-aarch64` output directories.
- Cross-build validation checks the AArch64 ELF identity of the shared library
  and CLI, every static-archive member, dynamic vendor dependencies, and public
  ABI exports.

### Self-Contained Third-Party Dependencies

- Linux and MSVC Windows builds now use the vendored Jansson 2.15 source.
  Linux no longer requires a system Jansson installation or its `pkg-config`
  metadata.
- Jansson and `libpg_query` objects are incorporated directly into
  `libsqlparser.a` and `libsqlparser.so`. The pkg-config file no longer declares
  an external Jansson dependency.
- `libpg_query` objects, archives, and dependency files are written under the
  top-level build directory. Compiler, archiver, debug mode, flags, source-set,
  and header changes participate in incremental rebuild decisions.

### Validation and Compatibility

- Linux AArch64 cross and native builds completed successfully. Native
  `make test` covered 2,758 cases and 8,945 patches across nine case matrices
  with zero failures.
- Both build paths produce a `sqlparser_cli` executable that runs on Linux
  AArch64 and emits byte-identical View JSON for the same input.
- The shared library continues to export 152 public symbols with SONAME
  `libsqlparser.so.0`. This release adds no public C APIs or resource-ownership
  rules.

## 2.14.5

### Query-Graph Identifier Delimiter State

- `sqlparser_graph_relation_t` and `sqlparser_graph_field_t` add
  `quoted_identifier` to report whether the relation object name or field
  column name used an explicit identifier delimiter in the original SQL or a
  patch fragment.
- View JSON emits `quoted_identifier: true` on the applicable `relations[]`,
  `fields[]`, and session identifier value when the token uses `"..."`, MySQL
  backticks, or SQL Server brackets. The flag reports delimiter presence only;
  it does not classify the delimiter kind.
- Detection requires the exact token from input SQL or a patch fragment.
  Dialect-compatibility quotes generated internally by the parser are not
  reported as source delimiters, and ordinary string literals are excluded.

### Patch Consistency and Validation

- Oracle and Vastbase-Oracle fragment preprocessing now retains exact
  identifier origins. Assignment patches that also rewrite bind syntax produce
  the same View on the current handle and after a fresh parse.
- Oracle, Dameng, and Vastbase-Oracle database-link relations read original
  object spelling from dialect state. MySQL-compatible session identifiers
  likewise derive delimiter state from the original token.
- The nine executable dialect fixtures still contain 2,758 final cases and
  8,945 independent patches, with 1,800 delimiter-state assertions added. A
  `make test-unit` completed successfully.
- This release adds two public structure fields but no public functions,
  enums, or resource ownership. Query-graph results remain owned by the handle.

## 2.14.4

### State Consistency Across Structural Patches

- When a structural patch cannot remain on the local source-edit path and falls
  back to an AST rewrite, both the per-call and handle-level source-surface
  completeness flags are cleared. A later internal deparse and reparse can no
  longer restore stale source from before the patch.
- For Oracle-compatible `INSERT ALL`, a column and value added on a handle now
  remain in its current AST and source state. A subsequent independent
  `replace` patch can address the new cell without silently losing the first
  insertion or reporting `cell index is out of range`.
- Consecutive patches do not require an intervening View, deparse, or fresh
  parse. Final deparse preserves unchanged branches, bind parameters, and other
  original SQL text.

### Regression Coverage and Compatibility

- Oracle, Dameng, and Vastbase-Oracle add a unit regression with two
  `INSERT ALL` branches and 32 columns per branch. Starting from one pristine
  handle, the test executes four insert-and-replace pairs as eight independent
  `apply_patch` calls, then compares the final SQL exactly and verifies stable
  output after a fresh parse.
- This release adds no public APIs, enums, or structure fields. Existing
  function signatures, public structure layouts, and the shared-library ABI
  major are unchanged.
- The nine executable dialect fixtures still contain 2,758 final cases and
  8,945 independent patches. A full `make test` on the final code
  passed; a targeted Valgrind run exited with `0 bytes in 0 blocks` and zero
  errors.

## 2.14.3

### Local Source Edits for INSERT COLUMN

- On an ordinary `INSERT ... VALUES` statement with an explicit target list,
  `insert_column` adds the column name and default value to the target list and
  every VALUES row at their original source intervals instead of serializing
  the complete AST.
- All insertion intervals are checked for valid boundaries and conflicts before
  edits are added. Multi-row VALUES statements use one insertion index and
  remain atomic. Shared source-scan state and ordered tail appends avoid repeated
  scans and edit movement as the row count grows.
- Unchanged typed literals, time functions, identifiers, case, whitespace, and
  other source text remain byte-identical. Expressions such as `DATE '...'` and
  `TIMESTAMP '...'` are not rewritten as `CAST(...)` when another column is
  added.

### Regression Coverage and Compatibility

- Each of the nine executable dialect fixtures adds one `insert_column` patch
  covering typed literals, `NOW()`, `CURRENT_TIMESTAMP`, or `GETDATE()`. The
  case runner accepts a strictly parsed internal INSERT target-list selector;
  existing JSON Pointer patch paths are unchanged.
- This release adds no public APIs, enums, or structure fields. Existing
  function signatures, public structure layouts, and the shared-library ABI
  major are unchanged.
- The nine fixtures contain 2,758 final cases and 8,945 independent patches.
  A full `make test` and one targeted Valgrind run on the final code
  passed; Valgrind reported no memory remaining at exit and zero errors.

## 2.14.2

### INSERT VALUES Surface Preservation and View Consistency

- For an ordinary `INSERT ... VALUES` cell whose source interval can be resolved safely, dialect-valid string, typed-literal, function, and compound-expression patches use a local replacement. After dialect parsing, only the target interval is replaced and unchanged source text is preserved.
- Oracle, Dameng, and Vastbase-Oracle `DATE '...'` and `TIMESTAMP '...'` typed literals no longer degrade to `CAST(...)` when either the literal itself or another cell in the statement is replaced. Views exported from patched and freshly parsed handles remain equivalent.
- A surface-complete patched handle can read cell text from the current statement, VALUES ordinal, row, and column. On the local-source path, prior edits in a multi-patch request are materialized before a `source_selector` is resolved, so cloning reads current SQL rather than normalized AST text.

### Local Rewrites for SQL Server INSERT OUTPUT

- Simple SQL Server and Vastbase-SQLServer `INSERT ... OUTPUT ... VALUES` statements with verifiable boundaries support local rewrites for client, `OUTPUT INTO`, and dual result channels. Result targets, sink relations, and sink columns are resolved to source intervals.
- Removed active insertion of whitespace between the target relation and column list. Forms such as `t(a)` and `audit(id)`, bracket identifiers, original case, and irregular whitespace remain unchanged after a patch.

### Compatibility and Validation

- This release adds no public APIs, enums, or structure fields. Existing function signatures and public structure layouts are unchanged, and the shared-library ABI major remains `libsqlparser.so.0`.
- The nine executable dialect fixtures contain 2,758 final cases and 8,936 independent patches. A strict build and all nine runners validated original deparse, View JSON, patched deparse, a second deparse after reparsing, and patched/fresh View equivalence, with all checks passing.
- One targeted Valgrind run covered typed literals, current surface SQL, and the multi-patch `source_selector` lifecycle. All 2,100 allocations were freed, no memory remained at exit, and the error count was zero.

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
- The nine executable dialect fixtures contain 2,755 final cases and 8,918 independent patches. A full `make test` run validated original deparse, View JSON, patched deparse, a second deparse after reparsing, and patched/fresh View equivalence, with all checks passing.

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
