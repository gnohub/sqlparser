# Dialect Coverage

This file summarizes the executable regression coverage for each dialect.
Case-level sources are the `tests/cases/*_input.json` files. Official syntax
coverage is tracked in each dialect's `*_official_syntax_coverage.en.md` file.

## Summary

| Dialect | Source | Successful Cases | Expected-Failure Cases | Total Cases | Fixture Success Rate |
| --- | --- | ---: | ---: | ---: | ---: |
| PostgreSQL | `tests/cases/sql_batch_input.json` | 217 | 0 | 217 | 100.00% |
| MySQL | `tests/cases/mysql_dialect_input.json` | 261 | 0 | 261 | 100.00% |
| Oracle | `tests/cases/oracle_dialect_input.json` | 252 | 0 | 252 | 100.00% |
| SQL Server | `tests/cases/sqlserver_dialect_input.json` | 626 | 0 | 626 | 100.00% |
| Dameng | `tests/cases/dameng_dialect_input.json` | 184 | 0 | 184 | 100.00% |
| Vastbase PostgreSQL mode | `tests/cases/vastbase_postgresql_dialect_input.json` | 202 | 0 | 202 | 100.00% |
| Vastbase MySQL mode | `tests/cases/vastbase_mysql_dialect_input.json` | 262 | 0 | 262 | 100.00% |
| Vastbase Oracle mode | `tests/cases/vastbase_oracle_dialect_input.json` | 221 | 0 | 221 | 100.00% |
| Vastbase SQL Server mode | `tests/cases/vastbase_sqlserver_dialect_input.json` | 606 | 0 | 606 | 100.00% |

The nine fixtures contain 2831 final cases and 9136 independent patches in
total.

## Counting Rules

- `Successful Cases` create a handle and pass parsing, View JSON, byte-for-byte
  input preservation when deparsing the unmodified handle, and all applicable
  structural assertions.
- `Expected-Failure Cases` explicitly expect `SQLPARSER_STATUS_UNSUPPORTED`,
  a parse error, or another failure status and do not return a usable handle.
- Expected-failure cases include invalid SQL and cases whose required semantics
  are not represented by the current dialect-to-AST mapping. These fixture
  counts do not measure official syntax coverage.

## Maintenance

- Update this file and [dialect_coverage.csv](./dialect_coverage.csv) when dialect cases are added or removed.
- Official syntax checklists are maintained per dialect: [PostgreSQL](./postgresql_official_syntax_coverage.en.md), [MySQL](./mysql_official_syntax_coverage.en.md), [Oracle](./oracle_official_syntax_coverage.en.md), [SQL Server](./sqlserver_official_syntax_coverage.en.md), [Dameng](./dameng_official_syntax_coverage.en.md), and [Vastbase](./vastbase_official_syntax_coverage.en.md).
