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

## Run

```bash
make test
```

`make test` runs:

- unit test binaries
- CLI batch fixture validation
- smoke execution for programs under `examples/`

## Representative Files

- `tests/unit/test_api_smoke.c`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/cases/sql_batch_input.json`
- `tests/cases/phase1_cases.md`

## Coverage

The current test coverage includes:

- parse and deparse baseline flow
- statement kind and node recognition
- `SELECT / INSERT / UPDATE / DELETE`
- multi-statement input
- common DDL statements
- JSON export
- selector replay and model JSON replay
