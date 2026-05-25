# v0.8.0 Release Notes

`v0.8.0` expands executable dialect coverage and adds Oracle ordinary `ALTER SESSION SET` session parameter assignments plus MySQL parameterized `LIMIT ?, ?` pagination.

## Highlights

- Oracle now supports ordinary `ALTER SESSION SET <parameter> = <value>` session parameter assignments.
- Oracle coverage now includes `NLS_DATE_FORMAT`, `NLS_DATE_LANGUAGE`, `NLS_NUMERIC_CHARACTERS`, `INSTANCE`, and `ERROR_ON_OVERLAP_TIME`.
- Oracle public deparse and SQL View output preserve the original parameter/value form and do not expose internal conversion prefixes.
- MySQL now supports parameterized comma pagination through `LIMIT ?, ?` while preserving the MySQL public deparse form.
- The existing PostgreSQL, MySQL, Oracle, SQL Server, and Dameng case matrices were expanded.

## Test Coverage

- PostgreSQL executable cases: 95.
- MySQL executable cases: 67, with 52 supported.
- Oracle executable cases: 103, with 85 supported.
- SQL Server executable cases: 95, with 80 supported.
- Dameng executable cases: 75, with 63 supported.
- Oracle official syntax coverage now reports 32 `CURRENT` groups and 14 `MODEL_REQUIRED` groups out of 46 syntax groups.

## Release Validation

This release validation includes:

- `git diff --check`
- JSON case file validation
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux Oracle targeted case matrix and CLI deparse / view validation
- Linux `make verify-asan`
- Linux `make verify-ubsan`
- Linux `make verify-valgrind`

## Release Boundary

- Public header: `include/sqlparser/sqlparser.h`
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`
