# Test Guide

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

## Patch Batch Regression and Benchmarks

`test_patch_batch` checks ordered source reads, repeated edits, insertion/deletion indices, mixed operations, binds, comments, resource limits, and rollback. Benchmark mode puts every edit in one patch list and calls `sqlparser_apply_patch()` once. It measures parse, apply, and deparse separately and verifies the resulting values. Timings are not machine-dependent test thresholds.

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

The default run also includes 237 positive path checks with 237 rollback checks, plus 83 positive dependency checks and 86 rollback checks. These cover all 13 dialect entries; MERGE runs only on the 10 supporting entries, and INSERT ALL/FIRST only on Oracle, Vastbase Oracle, KingbaseES Oracle, and Dameng. Checks cover complete Views, Views reparsed from output, generation, ordered source reads, bind lifetimes, expression/argument index shifts, comments, and rollback when an intermediate edit exceeds a resource limit. Repeated replacements also verify that introduced comments survive and that later edits cannot hide intermediate fragment errors. Pseudo columns check ordinal shifts caused by newly introduced expressions.

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
| Source copying | `insert_copy`: repeated reads from an unchanged source value |
| Functions and expressions | `expr_raw`, `expr_float`, `expr_bind`, `expr_field`, `expr_whole`, `expr_insert`, `expr_delete`, `expr_repeat` |
| Mixed edits | `mixed_update_expr`: alternating UPDATE assignment and function-argument literal replacements |
| Multi-branch inserts | `multi_all`, `multi_first`, `multi_raw`, `multi_float`, `multi_bind`, `multi_copy`, `multi_comment_all`, `multi_comment_first` |

The baseline uses the unmodified production code from `v2.16.15` (`05590bb5994200efb6f12bfb72a3e35c0f338f75`), captured on 2026-09-23: Linux x86_64 / KVM, Intel Xeon Platinum 8168, GCC 4.8.5, compiled with `-std=gnu11 -fPIC -O2 -w -pthread` and statically linked to `libsqlparser.a`:

- [Timing baseline](../bench/baselines/patch_batch_v2.16.15.csv): 45 argument combinations, each run in three independent processes, yielding 135 raw records. The `arguments` column supports direct replay.
- [Call-count baseline](../bench/baselines/patch_batch_v2.16.15_counts.csv): 237 small path checks across 13 dialect entries, each using one API call with six patches.

The normal test binary measures timings. The optional `_counts` binary uses linker wrapping only to count `sqlparser_parse_with_options()`, handle clone, and `sqlparser_deparse()` calls during `sqlparser_apply_patch()`, without production-source instrumentation. Full-statement parse counts exclude initial parsing, result verification, and local expression-fragment parsing. Clone/deparse counts do not represent all memory copying or AST serialization. CSV call counts come from separate instrumented runs; their timings are not used.

Times are in seconds. The fixed fixture also reports query-graph access and patch construction time. Generated scenarios prepare SQL and patches outside the timed stages, leaving `construct_seconds` empty. GNU time's `max_rss_kib` is peak RSS for the whole sample process, including verification, not apply-only memory or a leak metric. Functional checks hard-code neither elapsed-time thresholds nor the old reparse counts. Performance acceptance should compare identical arguments against this baseline while retaining synchronization required by source dependencies, index changes, or overlapping edits.

The optimized implementation passed the full `make test` suite, the ABI check, and targeted tests for both normal and counter builds. Valgrind on the patch batch regressions reported zero errors and zero bytes in zero blocks at exit.

Batch source edits reuse the existing edit list, synchronizing when earlier results must be read, expression/argument indices change, or source boundaries require it. Standard strings and decimal numbers still undergo fragment parsing, without copying a whole dialect state just for validation; existing input and output limits remain in force. Multi-insert source scans use batch-local stack cursors, cleared whenever SQL or dialect state changes, with no persistent SQL/AST cache added.

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
- argument validation, resource limits, malformed SQL, failed-rewrite rollback,
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
