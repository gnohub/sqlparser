# Dialect Coverage

This file summarizes the executable regression coverage for each dialect.
Case-level sources are the `tests/cases/*_input.json` files. Official syntax
coverage is tracked in each dialect's `*_official_syntax_coverage.en.md` file.

## Summary

| Dialect | Source | Supported Cases | Explicitly Unsupported Cases | Total | Supported Ratio |
| --- | --- | ---: | ---: | ---: | ---: |
| PostgreSQL | `tests/cases/sql_batch_input.json` | 153 | 1 | 154 | 99.35% |
| MySQL | `tests/cases/mysql_dialect_input.json` | 156 | 0 | 156 | 100.00% |
| Oracle | `tests/cases/oracle_dialect_input.json` | 163 | 12 | 175 | 93.14% |
| SQL Server | `tests/cases/sqlserver_dialect_input.json` | 448 | 11 | 459 | 97.60% |
| Dameng | `tests/cases/dameng_dialect_input.json` | 125 | 6 | 131 | 95.42% |
| Vastbase PostgreSQL mode | `tests/cases/vastbase_postgresql_dialect_input.json` | 144 | 1 | 145 | 99.31% |
| Vastbase MySQL mode | `tests/cases/vastbase_mysql_dialect_input.json` | 156 | 0 | 156 | 100.00% |
| Vastbase Oracle mode | `tests/cases/vastbase_oracle_dialect_input.json` | 162 | 12 | 174 | 93.10% |
| Vastbase SQL Server mode | `tests/cases/vastbase_sqlserver_dialect_input.json` | 448 | 11 | 459 | 97.60% |

## Counting Rules

- `Supported Cases` means the dialect is covered by executable regression checks for parsing, View JSON, deparse, or expected error behavior.
- `Explicitly Unsupported Cases` means the implementation deliberately returns `SQLPARSER_STATUS_UNSUPPORTED` or a parse error and does not return a usable handle.
- The PostgreSQL negative case is an intentionally invalid SQL input and is not counted as a feature gap.
- Oracle, SQL Server, Dameng, and some Vastbase compatibility-mode unsupported cases are database-specific semantics that cannot be safely represented without extending the shared AST.

## Maintenance

- Update this file and [dialect_coverage.csv](./dialect_coverage.csv) when dialect cases are added or removed.
- Official syntax checklists are maintained per dialect: [PostgreSQL](./postgresql_official_syntax_coverage.en.md), [MySQL](./mysql_official_syntax_coverage.en.md), [Oracle](./oracle_official_syntax_coverage.en.md), [SQL Server](./sqlserver_official_syntax_coverage.en.md), [Dameng](./dameng_official_syntax_coverage.en.md), and [Vastbase](./vastbase_official_syntax_coverage.en.md).
