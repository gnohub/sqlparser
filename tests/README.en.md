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
- `make test-model-json`
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

## Representative Files

- `tests/unit/test_api_smoke.c`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_oracle_dialect_case_matrix.c`
- `tests/install/install_smoke.c`
- `tests/cases/sql_batch_input.json`
- `tests/cases/mysql_dialect_input.json`
- `tests/cases/oracle_dialect_input.json`
- `tests/verify_cli_batch.py`

## Coverage

The test coverage includes:

- parse and deparse baseline flow
- resource limits for SQL input, model JSON input, generated output, and
  statement count
- statement kind and node recognition
- `SELECT / INSERT / UPDATE / DELETE / MERGE`
- multi-statement input
- `ON CONFLICT`, `RETURNING`, `UPDATE ... FROM`, and `DELETE ... USING`
- common DDL, transaction control, `GRANT / REVOKE`, and maintenance statements
- JSON export
- selector replay and model JSON replay
- MySQL dialect conversion, deparse output, and explicit unsupported-syntax return codes
- Oracle dialect conversion, deparse output, and explicit unsupported-syntax return codes

## Case Matrix

- [SQL Case Matrix](./cases/sql_case_matrix.en.md)
- [MySQL Dialect Case Matrix](./cases/mysql_dialect_matrix.en.md)
- [Oracle Dialect Case Matrix](./cases/oracle_dialect_matrix.en.md)
