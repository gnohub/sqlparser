# Test Guide

The `tests/` directory records functional coverage for `sqlparser`.

## Layout

- `tests/unit/`
  Unit tests and API regression tests.
- `tests/cases/`
  Named SQL cases, batch fixtures, and supporting notes.

## Test Components

The current test suite contains:

- unit tests
- batch SQL fixture verification
- example smoke tests
- installed-library API smoke tests
- strict-build and sanitizer gates
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

- `make verify-release`
- `make verify-debug`
- `make verify-asan`
- `make verify-ubsan`
- `make test-loop LOOP=50`
- `make verify`

## Representative Files

- `tests/unit/test_api_smoke.c`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/install/install_smoke.c`
- `tests/cases/sql_batch_input.json`
- `tests/verify_cli_batch.py`

## Coverage

The current test coverage includes:

- parse and deparse baseline flow
- statement kind and node recognition
- `SELECT / INSERT / UPDATE / DELETE / MERGE`
- multi-statement input
- `ON CONFLICT`, `RETURNING`, `UPDATE ... FROM`, and `DELETE ... USING`
- common DDL, transaction control, `GRANT / REVOKE`, and maintenance statements
- JSON export
- selector replay and model JSON replay
