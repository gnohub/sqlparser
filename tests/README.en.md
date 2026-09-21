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
