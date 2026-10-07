# Test Guide

## Pipeline Regression Coverage

The public-API benchmark in `tools/sqlparser_pipeline_bench.c` measures a fresh
explicit-MySQL parse, query graph, real selector traversal and patch construction,
one batch application, and allocated SQL retrieval. It checks every output byte
outside the timer and reports cleanup separately. See
[`bench/README.en.md`](../bench/README.en.md) for the exact
5,000-row workload and reproducible comparison commands.

Key regression programs cover:

- `test_protobuf_node` and `test_protobuf_fastpath`: exact descriptor invariants,
  generic/fast byte parity, unknown and duplicate wire fields, malformed input,
  buffer packing, allocation failures, and deterministic wire fuzzing
- `test_mysql_validation_observer`: live-tree/fallback parity, parser-context
  lifetime, parse/validation error precedence, statement limits, and optional
  error outputs, including malformed SQL with a NULL error pointer
- `test_mysql_scanner_differential`: masks, quote/comment handling, origin maps,
  statement boundaries, absent-keyword gates, and available locale behavior
- `test_surface_scanner_differential`: every-index source span parity across
  all dialects, quote/comment variants, malformed input, random bytes and locales
- `test_insert_graph_fast_paths`: native literal and fallback graph construction,
  selector boundaries, graph growth, relation binding, rewrites and terminal failure cleanup
- `test_insert_string_batch` and `test_ascii_string_validation`: raw/typed string
  equivalence, exact source preservation, fragment limits and terminal-failure cleanup
- `test_validation_arena`: allocation-failure cleanup for the serialized fallback
- `test_mysql_identity_preprocess`, `test_sqlserver_identity_preprocess`, and `test_sqlserver_raw_prefilter`: preprocessing, dialect-state and fallback parity
- `test_sqlserver_insert_batch` and `test_sqlserver_validation_proof`: SQL Server-family batch string replacement, validation-evidence reuse, error ordering, ownership and allocation-failure handling
- `test_simple_insert_native`, `test_scalar_insert_native`, and `test_native_wire_provenance`: native INSERT construction, wire parity and source validation
- `test_wire_insert_primary`, `test_wire_insert_graph`, `test_scalar_wire_codec`, `test_scalar_wire_pipeline`, `test_family_scalar_pipeline`, and `test_sqlserver_wire_pipeline`: batch INSERT graphs, string replacement, source locations, ownership and fallback
- `test_oracle_origin_replay`, `test_oracle_graph_classification`, and `test_oracle_owned_commit`: Oracle multi-insert origins, value classification, state commits and literal lifetimes
- `test_insert_source_proof`, `test_batch_selector_fastpath`, and `test_ascii_string_recognizer`: source-span reuse, selector recognition and string boundaries
- `test_distinct_handle_concurrency`: independent handles across all 13 dialects;
  `./bin/test_distinct_handle_concurrency 20` exercises 1,040 small and 80 bulk
  MySQL lifecycles. It does not establish shared-handle mutation safety

The GNU-linker build enables observer and allocation wrappers. The Windows
concurrency test explicitly skips where pthreads are unavailable. Performance
measurements are diagnostics, not machine-dependent unit-test thresholds.


The `tests/` directory records functional coverage for `sqlparser`.

## Layout

- `tests/unit/`
  Unit tests and API regression tests.
- `tests/cases/`
  Named SQL cases, batch fixtures, and supporting notes.

## Test Components

The test suite contains:

- unit tests
- batch SQL fixture verification
- example smoke tests
- installed-library API smoke tests
- strict-build and sanitizer gates
- `valgrind` leak checks
- long-running loop regression
- stability and malformed-input regression

## Run

```bash
make test
```

`make test` runs:

- unit test binaries
- CLI batch fixture validation
- smoke execution for programs under `examples/`

Common quality-gate entry points:

- `make test-parse`
- `make test-inspect`
- `make test-rewrite`
- `make test-deparse`
- `make test-view-json`
- `make test-cli`
- `make test-install`
- `make test-abi`
- `make verify-release`
- `make verify-debug`
- `make verify-asan`
- `make verify-ubsan`
- `make verify-valgrind`
- `make test-loop LOOP=50`
- `make verify`

## Memory Checks

Memory checks related to parsing and batch rewriting include:

- Lifecycle and borrowed inputs: `test_patch_lifecycle`, `test_direct_wire_lifecycle`, `test_patch_graph_borrowed`.
- Dialect state and structural changes: `test_mutation_dialect_state`, `test_patch_structural_rows`, `test_patch_batch`.
- Encoding, conversion and allocation failure: `test_protobuf_output_oom`, `test_protobuf_scalar_lifetime`, `test_parser_conversion_lifetime`, `test_validation_arena`, `test_mysql_validation_observer`, `test_protobuf_fastpath`.
- Source validation and batch INSERTs: `test_mysql_identity_preprocess`, `test_sqlserver_identity_preprocess`, `test_scalar_wire_pipeline`, `test_family_scalar_pipeline`, `test_sqlserver_wire_pipeline`.
- Oracle multi-table inserts: `test_oracle_origin_replay`, `test_oracle_graph_classification`, and `test_oracle_owned_commit`; `test_oracle_owned_commit --alloc` also covers commit allocation failures.
- Threads and independent handles: `test_pg_query_thread_lifecycle`, `test_distinct_handle_concurrency` (the default two waves).
- Full batch pipelines: `sqlparser_pipeline_bench 5000 1 0 literal mysql` and `sqlparser_pipeline_bench 5000 1 0 replace mysql`.

GNU linker wrappers provide fault injection for lifetimes, encoding, output allocation and fallback paths. Keep leak checks separate from normal performance timing. Run individual programs through `scripts/run_valgrind.sh`; `make verify-valgrind` is the full-suite entry point.

```bash
make bin/test_oracle_owned_commit
./scripts/run_valgrind.sh --log-dir build/valgrind -- ./bin/test_oracle_owned_commit
```

## In-place Patch Lifecycle

`test_patch_lifecycle` parses one handle and performs eight successive batch
apply/deparse rounds on each of all 13 dialect entries. Every successful
nonempty batch increments generation once and invalidates borrowed Views,
including identical replacements and modify-then-restore batches. Generation-positive
deparse may canonicalize otherwise unchanged SQL: the COPY no-op regression
now expects `FORMAT CSV, HEADER true` and retains exact relation/option checks,
rather than requiring the original keyword/boolean letter case. An empty
patch list leaves generation and borrowed Views unchanged.

Apply and deparse failures poison the handle: subsequent reads or patches are
rejected, and the caller destroys it. Tests preserve specific selector,
malformed-fragment, and resource-limit error checks, including invalid
intermediate edits that later replacements must not hide. The caller's input
buffer remains unchanged on success and failure; independently allocated SQL
outputs remain valid across later edits, failures and handle destruction.
Borrowed literal strings supplied as later patch inputs are snapshotted before
an earlier edit invalidates their storage.

The GNU-linker target sweeps allocation failure through generic apply, direct
deparse, fast MySQL string batches, and a second fast batch on the same handle.
Each rejected operation must leave a safely destructible terminal handle;
optional allocation fallbacks that succeed must preserve the complete expected
SQL. Dedicated serializer/deparse allocation boundaries retain their exact
`NO_MEMORY` assertions. Generic historical protobuf-unpack failures can report
`INTERNAL_ERROR`. The counter build of `test_patch_batch` also asserts zero
whole-handle clones during application across its existing scenario matrix.

```bash
make -j4 bin/test_patch_lifecycle bin/test_patch_batch_counts SHOW_WARNING=0
./bin/test_patch_lifecycle
./bin/test_patch_batch_counts
```

## String Literal Dialect Output and Rewrite Regression

`tests/unit/test_string_literal_surface.c` exercises library APIs for string values, complete SQL, expression fragments, and source copies. It is discovered automatically by `make test`. Its 2,367 combinations cover all 13 dialect entries. Failures return a nonzero exit status.

```bash
make bin/test_string_literal_surface
./bin/test_string_literal_surface
./bin/test_string_literal_surface sqlserver set-target
./bin/test_string_literal_surface mysql patch-literal
./bin/test_string_literal_surface oracle copy-target
./bin/test_string_literal_surface postgresql
```

- Ten string fixtures cover plain text, single/consecutive/trailing backslashes, paths, both orders of quotes adjacent to backslashes, Unicode, literal backslash sequences such as `\n`/`\t`/`\r`, and embedded `E'…'` text.
- SELECT paths cover individual targets, selectors, target lists, patch `sql`/`literal` inputs, direct literal setters, read-only fragments, and `source_selector` copies. Additional checks cover INSERT cells, UPDATE assignments, WHERE literals, function arguments, and protected comments/delimited aliases.
- Non-PostgreSQL entries also cover reading, replacing, and copying `N'…'` strings. Batch cases verify ordered reads through replace/copy/replace/copy. Failure checks require the final selector's out-of-range error code, rejected access to the poisoned handle, and safe destruction.
- The 91 `typed-string-boundary` combinations check control bytes, Unicode, non-UTF-8 bytes, and combinations with quotes/backslashes, preserving existing byte behavior. They also check that later replacements cannot hide a null string pointer or an invalid SQL fragment.
- The regular string matrix lists expected literal spellings independently, not generated by the renderer under test. Each matrix case parses its expected SQL and checks its string values before rewriting. Results verify exact statement/fragment text, semantic values, generation, complete Views, and values/Views after reparsing the output.

Output expectations follow each entry's syntax family: MySQL-family backslash escaping, and ordinary or national strings for Oracle, Dameng, and SQL Server families. Vastbase/Kingbase compatibility entries check the corresponding family's library output contract, not server support for additional syntax. PostgreSQL-family entries accept both ordinary strings and semantically equivalent `E'…'` strings.

Syntax references: [PostgreSQL string constants](https://www.postgresql.org/docs/16/sql-syntax-lexical.html#SQL-SYNTAX-STRINGS-ESCAPE), [MySQL string literals](https://dev.mysql.com/doc/refman/8.0/en/string-literals.html), [Oracle literals](https://docs.oracle.com/en/database/oracle/oracle-database/19/sqlrf/Literals.html), and [SQL Server constants](https://learn.microsoft.com/en-us/sql/t-sql/data-types/constants-transact-sql).

## Patch Batch Regression and Benchmarks

`test_patch_batch` checks ordered source reads, repeated edits, insertion/deletion indices, mixed operations, binds, comments, resource limits, and terminal failure cleanup. Benchmark mode puts every edit in one patch list and calls `sqlparser_apply_patch()` once. It measures parse, apply, and deparse separately and verifies the resulting values. Timings are not machine-dependent test thresholds.

```bash
make bin/test_patch_batch
./bin/test_patch_batch
./bin/test_patch_batch --bench oracle 50
./bin/test_patch_batch --bench update 500
./bin/test_patch_batch --bench update-copy 250
./bin/test_patch_batch --bench expression 500
./bin/test_patch_batch --bench update 500 50
./bin/test_patch_batch --bench expression 500 50
```

`oracle 50` uses an `INSERT ALL` with 50 branches and 16 columns per row. Its 250 source-copy insertions and 250 simulated ciphertext replacements are checked for 21 columns and values in every output branch. `update-copy 250` appends a backup and replaces each of 250 assignments, for 500 patches. The optional final argument for `update` and `expression` varies only the patch count while keeping the input SQL fixed.

### Batch Path Baseline

The default run includes 267 positive path checks with 267 terminal-failure checks, 83 positive dependency checks with 86 terminal-failure checks, and 85 positive native-AST batch boundary checks with 36 terminal-failure checks. These cover all 13 dialect entries; MERGE runs only on the 10 supporting entries, and INSERT ALL/FIRST only on Oracle, Vastbase Oracle, KingbaseES Oracle, and Dameng. Checks cover complete Views, Views reparsed from output, generation, ordered source reads, bind lifetimes, expression/argument index shifts, comments, and terminal failure cleanup when an intermediate edit exceeds a resource limit. Repeated replacements also verify that introduced comments survive and that later edits cannot hide intermediate fragment errors. Pseudo columns check ordinal shifts caused by newly introduced expressions.

Another 39 positive node-lookup checks and 39 terminal-failure checks can run separately with `--lookup-boundaries`. They cover WHERE literal numbering in parentheses, CASTs, nested functions and subqueries, mixed assignment edits, out-of-order and repeated replacements, statement-local numbering, and terminal failure cleanup.

The fixed `patch_batch_oracle_insert_all.sql` input is 27,530 bytes after removing its final newline, with 50 branches and 16 columns per branch. `--fixture` reads string values in columns 2–6 from the query graph and constructs 250 replacements using `sqlparser_selector_format()`. It submits the selected prefix in one API call, verifies all 800 values and column names, and compares the patched View with the View reparsed from output. The default test applies all 250 replacements.

```bash
make -j4 bin/test_patch_batch bin/test_patch_batch_counts SHOW_WARNING=0
./bin/test_patch_batch
./bin/test_patch_batch_counts --profile-all
./bin/test_patch_batch --fixture tests/cases/patch_batch_oracle_insert_all.sql 250
./bin/test_patch_batch --profile insert_copy postgresql 250 50
./bin/test_patch_batch --profile expr_raw postgresql 100 100
./bin/test_patch_batch --profile mixed_update_expr oracle 200 200
/usr/bin/time -f max_rss_kib=%M ./bin/test_patch_batch --profile multi_comment_all dameng 50 50
```

For `--profile scenario dialect size [patch_count]`, `size` fixes the candidate edit locations and input size; `patch_count` selects only the submitted prefix and defaults to `size`. Mixed scenarios require an even `size`. Available scenarios:

| Group | Scenarios |
| --- | --- |
| Controls | `insert_cell`, `update_literal`, `where_literal`, `select_target`, `merge_cell`, `expr_literal` |
| Ordinary multi-row inserts | `insert_rows`, `insert_rows_quoted`: two columns per row, replacing only the second column |
| MERGE UPDATE | `merge_assignment`: literal replacements in matched-branch assignments |
| Source copying | `insert_copy`: repeated reads from an unchanged source value |
| Functions and expressions | `expr_raw`, `expr_float`, `expr_bind`, `expr_field`, `expr_whole`, `expr_insert`, `expr_delete`, `expr_repeat` |
| Mixed edits | `mixed_update_expr`: alternating UPDATE assignment and function-argument literal replacements |
| Multi-branch inserts | `multi_all`, `multi_first`, `multi_raw`, `multi_float`, `multi_bind`, `multi_copy`, `multi_comment_all`, `multi_comment_first` |

The baseline uses the unmodified production code from `v2.16.15` (`05590bb5994200efb6f12bfb72a3e35c0f338f75`), captured on 2026-09-23: Linux x86_64 / KVM, Intel Xeon Platinum 8168, GCC 4.8.5, compiled with `-std=gnu11 -fPIC -O2 -w -pthread` and statically linked to `libsqlparser.a`:

- [Timing baseline](../bench/baselines/patch_batch_v2.16.15.csv): 45 argument combinations, each run in three independent processes, yielding 135 raw records. The `arguments` column supports direct replay.
- [Call-count baseline](../bench/baselines/patch_batch_v2.16.15_counts.csv): 237 small path checks across 13 dialect entries, each using one API call with six patches.

The normal test binary measures timings. The optional `_counts` binary uses linker wrapping to count full-statement parses, handle clones, deparses, both AST commit entry points, and shared surface visitors during `sqlparser_apply_patch()`, without production-source instrumentation. Full-statement parse counts exclude initial parsing, result verification, and local expression-fragment parsing. Clone/deparse counts do not represent all memory copying or AST serialization. `ast_commits` and `state_commits` record cross-object calls to the two commit entry points, not actual full-tree validation counts. `surface_visits` and `surface_root_visits` exclude SQL Server's independent walker. CSV call counts come from separate instrumented runs; their timings are not used.

Times are in seconds. The fixed fixture also reports query-graph access and patch construction time. Generated scenarios prepare SQL and patches outside the timed stages, leaving `construct_seconds` empty. GNU time's `max_rss_kib` is peak RSS for the whole sample process, including verification, not apply-only memory or a leak metric. Functional checks hard-code neither elapsed-time thresholds nor the old reparse counts. Performance acceptance should compare identical arguments against this baseline while retaining synchronization required by source dependencies, index changes, or overlapping edits.

The 2.16.16 optimized implementation passed the full `make test` suite, the ABI check, and targeted tests for both normal and counter builds. Valgrind on the patch batch regressions reported zero errors and zero bytes in zero blocks at exit.

Batch source edits reuse the existing edit list, synchronizing when earlier results must be read, expression/argument indices change, or source boundaries require it. Library-rendered typed STRINGs retain input-limit checks without building a temporary AST for fragment validation; raw SQL fragments and other types retain their existing validation paths. Existing input and output limits remain in force. Multi-insert source scans use batch-local stack cursors, cleared whenever SQL or dialect state changes, with no persistent SQL/AST cache added.

### Native-AST Batch Regression

At size 500, `insert_rows` generates exactly 500 rows with `ID` and `SECRET_VALUE`, replacing `small-secret-NNNN` with `other-secret-NNNN`. `insert_rows_quoted` uses the dialect's table/column delimiters. Both check complete Views and byte-for-byte deparse output. The optional final argument edits only a row prefix and verifies that other rows remain unchanged.

Multi-row scenarios cover PostgreSQL, MySQL, SQL Server, Dameng, and their corresponding compatibility entries, totaling 10 entries. Oracle, Vastbase Oracle, and Kingbase Oracle use the existing single-row, multi-column `insert_cell` scenario. `merge_assignment` covers the 10 non-MySQL entries. Default path matrices use six edits; replay 500-edit scenarios separately:

```bash
./bin/test_patch_batch --profile insert_rows mysql 500 500
./bin/test_patch_batch --profile insert_rows mysql 5000 5000
./bin/test_patch_batch_counts --profile insert_rows mysql 500 500
./bin/test_patch_batch --profile insert_rows_quoted mysql 500 125
./bin/test_patch_batch --profile insert_cell oracle 500 500
./bin/test_patch_batch --profile update_literal mysql 500 500
./bin/test_patch_batch --profile where_literal mysql 500 500
./bin/test_patch_batch --profile select_target mysql 500 500
./bin/test_patch_batch --profile merge_cell oracle 500 500
./bin/test_patch_batch --profile merge_assignment oracle 500 500
```

Native batch boundary cases cover delimiters, quotes/backslashes, untouched binds and national strings, repeated edits and ordered source reads, cross-row copies, terminal invalid-selector failures, and invalid intermediate hierarchy expressions that must not be hidden by later overwrites. Existing mixed-operation, resource-limit, INSERT ALL/FIRST, and function-argument cases remain regression controls.

Additional boundaries cover national strings across type changes, copying an overwritten cell, literal numbering after a function argument becomes a bind, and restoring oversized intermediate values permitted by native replacement. The multi-row generator supports up to 5,000 rows without changing library resource limits.

Compatible string replacements reuse batch source edits and rebuild parsed state once at the end, synchronizing when source dependencies, type changes, or structural changes require it. Ordinary VALUES reuse stack cursors for statement boundaries, and ordered edits no longer scan all earlier edits. No public fields or persistent AST cache are added.

On the 2.16.17 baseline, one native timing run of the 500-row MySQL scenario measured approximately 1.77 seconds for apply. A separate counter run recorded 500 AST commits, 2,000 shared surface visits, and zero full-statement reparses during the batch. These figures support subsequent same-machine, same-argument comparisons, not fixed timing or internal-call-count pass criteria.

On the same machine, 2.16.18 apply measured approximately 25 milliseconds for 500 rows and 269 milliseconds for 5,000 rows. Each batch used one patch API call and passed complete View and byte-for-byte SQL checks. The 5,000-row batch recorded one full-statement reparse and zero per-edit AST commits or shared surface visits. Timings exclude initial parsing, View export, and deparse.

### 2.16.19 Performance Regression

UPDATE and WHERE string-replacement batches reuse located nodes instead of repeatedly resolving global ordinals. Nodes are used only within the current parsed state and located again after rebuilding it. No public fields or persistent cache are added.

INSERT string-replacement batches read existing node types directly instead of constructing literal Views solely to classify values. Library-rendered string fragments retain input-limit checks without building a temporary AST for fragment validation. Public literal reads, quoted flags, and final whole-statement parsing are unchanged.

Keyword prechecks in MySQL, Vastbase MySQL, and KingbaseES MySQL use one raw-text scan while preserving syntax checks and error order. CREATE and DML preprocessing skip inapplicable passes for known statement types. Conservatively identified single statements can skip an entire pass before statement-boundary scanning. WITH, unknown prefixes, and complex boundaries retain the existing path; ON DUPLICATE checks are unchanged.

Single native apply measurements on the same machine with identical arguments are below. Complete View and SQL result checks passed. Timings exclude initial parsing, View export, and deparse and are not fixed performance thresholds.

| MySQL scenario | Patches | 2.16.18 | 2.16.19 |
| --- | ---: | ---: | ---: |
| UPDATE assignment replacement | 500 | 283.439 ms | 8.106 ms |
| WHERE literal replacement | 500 | 1,020.311 ms | 22.150 ms |
| INSERT row-by-row replacement | 5,000 | 269.921 ms | 105.435 ms |

`test_dialect_surface_state --mysql-guard` covers all 3 MySQL entries with 9 positive and 24 rejection checks for protected keywords, token boundaries, letter case, later statements, and nested statements. The 21 `--mysql-dispatch` checks cover empty segments in multi-statement input, ordinary and executable comments, trailing whitespace, REPLACE SET, WITH UPDATE/DELETE, later CREATE extensions, and semicolons inside strings, verifying exact SQL, complete Views, relation delimiter flags, and selector numbering. Both groups passed before and after optimization.

Related patch-batch, string-output, surface-state, and all 3 MySQL case-matrix regressions passed. Valgrind reported zero errors and zero bytes in zero blocks at exit for the 5,000-row INSERT and MySQL statement-dispatch cases.

## Representative Files

- `tests/unit/test_api_smoke.c`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_oracle_dialect_case_matrix.c`
- `tests/unit/test_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_dameng_dialect_case_matrix.c`
- `tests/unit/test_vastbase_oracle_dialect_case_matrix.c`
- `tests/unit/test_vastbase_mysql_dialect_case_matrix.c`
- `tests/unit/test_vastbase_postgresql_dialect_case_matrix.c`
- `tests/unit/test_vastbase_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_kingbase_oracle_dialect_case_matrix.c`
- `tests/unit/test_kingbase_mysql_dialect_case_matrix.c`
- `tests/unit/test_kingbase_postgresql_dialect_case_matrix.c`
- `tests/unit/test_kingbase_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_robustness.c`
- `tests/unit/test_stability.c`
- `tests/install/install_smoke.c`
- `tests/cases/sql_batch_input.json`
- `tests/cases/mysql_dialect_input.json`
- `tests/cases/oracle_dialect_input.json`
- `tests/cases/sqlserver_dialect_input.json`
- `tests/cases/dameng_dialect_input.json`
- `tests/cases/vastbase_oracle_dialect_input.json`
- `tests/cases/vastbase_mysql_dialect_input.json`
- `tests/cases/vastbase_postgresql_dialect_input.json`
- `tests/cases/vastbase_sqlserver_dialect_input.json`
- `tests/cases/kingbase_oracle_dialect_input.json`
- `tests/cases/kingbase_mysql_dialect_input.json`
- `tests/cases/kingbase_postgresql_dialect_input.json`
- `tests/cases/kingbase_sqlserver_dialect_input.json`
- `tests/verify_cli_batch.py`

## Coverage

The test coverage includes:

- parse and deparse baseline flow
- resource limits for SQL input, generated output, and statement count
- statement kind and node recognition
- `SELECT / INSERT / UPDATE / DELETE / MERGE`
- multi-statement input
- `ON CONFLICT`, `RETURNING`, `UPDATE ... FROM`, and `DELETE ... USING`
- common DDL, transaction control, `GRANT / REVOKE`, and maintenance statements
- JSON export
- selector replay and structured patch replay
- `SELECT` output-list replacement, insertion, deletion, and post-rewrite reparse validation
- structured SQL fragment rewrites, including cloning an UPDATE assignment value into a new assignment and replacing a SELECT target with structured column targets
- `WHERE` condition insertion, replacement, AND/OR append, and post-rewrite reparse validation for `SELECT`, `UPDATE`, `DELETE`, `INSERT ... SELECT`, `ON CONFLICT`, `VIEW`, `INDEX`, `COPY FROM`, `CREATE RULE`, `CREATE PUBLICATION`, and exclusion constraints
- MySQL dialect conversion, deparse output, and explicit unsupported-syntax return codes
- Oracle dialect conversion, deparse output, and explicit unsupported-syntax return codes
- SQL Server dialect conversion, deparse output, and explicit unsupported-syntax return codes
- Dameng dialect conversion, deparse output, and explicit unsupported-syntax return codes
- Vastbase explicit compatibility-mode conversion, deparse output, and explicit unsupported-syntax return codes
- KingbaseES conversion, deparse output, and patch replay through four explicit compatibility entries; each mode uses one unified entry without V8/V9 version dispatch
- crash-resistance regression for public API NULL arguments, out-of-range access,
  invalid selectors, invalid patches, malformed input, and repeated parsing
- argument validation, resource limits, malformed SQL, terminal rewrite failure cleanup,
  and dialect public-output stability

## Case Matrix

- [SQL Case Matrix](./cases/sql_case_matrix.en.md)
- [MySQL Dialect Case Matrix](./cases/mysql_dialect_matrix.en.md)
- [Oracle Dialect Case Matrix](./cases/oracle_dialect_matrix.en.md)
- [SQL Server Dialect Case Matrix](./cases/sqlserver_dialect_matrix.en.md)
- [Dameng Dialect Case Matrix](./cases/dameng_dialect_matrix.en.md)
- [Vastbase Oracle Compatibility Case Matrix](./cases/vastbase_oracle_dialect_matrix.en.md)
- [Vastbase MySQL Compatibility Case Matrix](./cases/vastbase_mysql_dialect_matrix.en.md)
- [Vastbase PostgreSQL Compatibility Case Matrix](./cases/vastbase_postgresql_dialect_matrix.en.md)
- [Vastbase SQL Server Compatibility Case Matrix](./cases/vastbase_sqlserver_dialect_matrix.en.md)
- [KingbaseES Oracle Compatibility Case Matrix](./cases/kingbase_oracle_dialect_matrix.en.md)
- [KingbaseES MySQL Compatibility Case Matrix](./cases/kingbase_mysql_dialect_matrix.en.md)
- [KingbaseES PostgreSQL Compatibility Case Matrix](./cases/kingbase_postgresql_dialect_matrix.en.md)
- [KingbaseES SQL Server Compatibility Case Matrix](./cases/kingbase_sqlserver_dialect_matrix.en.md)
