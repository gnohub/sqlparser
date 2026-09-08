# Dialect Coverage

This file summarizes the executable regression coverage for each dialect.
Case-level sources are the `tests/cases/*_input.json` files. Official references
are tracked in the dialect-specific `*_official_syntax_coverage.en.md` files and
the corresponding KingbaseES case matrices.

## Summary

| Dialect | Source | Successful Cases | Expected-Failure Cases | Total Cases | Fixture Success Rate |
| --- | --- | ---: | ---: | ---: | ---: |
| PostgreSQL | `tests/cases/sql_batch_input.json` | 234 | 0 | 234 | 100.00% |
| MySQL | `tests/cases/mysql_dialect_input.json` | 276 | 0 | 276 | 100.00% |
| Oracle | `tests/cases/oracle_dialect_input.json` | 291 | 0 | 291 | 100.00% |
| SQL Server | `tests/cases/sqlserver_dialect_input.json` | 651 | 0 | 651 | 100.00% |
| Dameng | `tests/cases/dameng_dialect_input.json` | 223 | 0 | 223 | 100.00% |
| Vastbase PostgreSQL mode | `tests/cases/vastbase_postgresql_dialect_input.json` | 219 | 0 | 219 | 100.00% |
| Vastbase MySQL mode | `tests/cases/vastbase_mysql_dialect_input.json` | 277 | 0 | 277 | 100.00% |
| Vastbase Oracle mode | `tests/cases/vastbase_oracle_dialect_input.json` | 260 | 0 | 260 | 100.00% |
| Vastbase SQL Server mode | `tests/cases/vastbase_sqlserver_dialect_input.json` | 631 | 0 | 631 | 100.00% |
| KingbaseES PostgreSQL entry | `tests/cases/kingbase_postgresql_dialect_input.json` | 45 | 0 | 45 | 100.00% |
| KingbaseES MySQL entry | `tests/cases/kingbase_mysql_dialect_input.json` | 42 | 0 | 42 | 100.00% |
| KingbaseES Oracle entry | `tests/cases/kingbase_oracle_dialect_input.json` | 43 | 0 | 43 | 100.00% |
| KingbaseES SQL Server entry | `tests/cases/kingbase_sqlserver_dialect_input.json` | 45 | 0 | 45 | 100.00% |

The thirteen fixtures contain 3237 final cases and 10249 independent patches
in total.

## Counting Rules

- `Successful Cases` create a handle and pass parsing, View JSON, byte-for-byte
  input preservation when deparsing the unmodified handle, and all applicable
  structural assertions.
- `Expected-Failure Cases` explicitly expect `SQLPARSER_STATUS_UNSUPPORTED`,
  a parse error, or another failure status and do not return a usable handle.
- `Total Cases` is the sum of successful and expected-failure cases, and
  `Fixture Success Rate` is the successful share. Draft cases are excluded.
- Expected-failure cases include invalid SQL and cases whose required semantics
  are not represented by the current dialect-to-AST mapping. These fixture
  counts do not measure official syntax coverage.
- The DDL Query Graph contracts added to the five base entries are proven only
  by those fixtures. Their counts do not imply the same syntax or patch surface
  for Vastbase or KingbaseES compatibility entries; each compatibility fixture
  remains authoritative.
- All thirteen entries verify ordinal overlay of explicit CTE column names onto
  directly enumerable source-block targets only for forms valid in their own
  fixtures. PostgreSQL, Vastbase PostgreSQL, and KingbaseES PostgreSQL
  additionally verify that a shorter list overrides only the target prefix.
  The applicable entries verify SET-result, recursive-SET, or star boundaries
  according to their own fixtures, without fabricating result targets, crossing
  branches, or expanding stars. Compatibility-entry counts prove only the
  project fixture contract, not official server syntax.
- Each KingbaseES compatibility mode has one unified entry. Its V8/V9 syntax
  baseline is merged in that entry without accepting or detecting a server
  version and without version dispatch. The SQLServer entry follows the current
  official V9 compatibility baseline.

## Maintenance

- Update this file and [dialect_coverage.csv](./dialect_coverage.csv) when dialect cases are added or removed.
- Official syntax checklists are maintained per dialect: [PostgreSQL](./postgresql_official_syntax_coverage.en.md), [MySQL](./mysql_official_syntax_coverage.en.md), [Oracle](./oracle_official_syntax_coverage.en.md), [SQL Server](./sqlserver_official_syntax_coverage.en.md), [Dameng](./dameng_official_syntax_coverage.en.md), and [Vastbase](./vastbase_official_syntax_coverage.en.md).
