# Dialect Coverage

This file summarizes the executable regression coverage for each dialect.
Case-level sources are the `tests/cases/*_input.json` files. Official syntax
coverage is tracked in each dialect's `*_official_syntax_coverage.en.md` file.

## Summary

| Dialect | Source | Supported Cases | Explicitly Unsupported Cases | Total | Supported Ratio |
| --- | --- | ---: | ---: | ---: | ---: |
| PostgreSQL | `tests/cases/sql_batch_input.json` | 48 | 1 | 49 | 97.96% |
| MySQL | `tests/cases/mysql_dialect_input.json` | 12 | 15 | 27 | 44.44% |
| Oracle | `tests/cases/oracle_dialect_input.json` | 39 | 19 | 58 | 67.24% |
| SQL Server | `tests/cases/sqlserver_dialect_input.json` | 41 | 15 | 56 | 73.21% |

## Counting Rules

- `Supported Cases` means the dialect is covered by executable regression checks for parsing, SQL View JSON, deparse, or expected error behavior.
- `Explicitly Unsupported Cases` means the implementation deliberately returns `SQLPARSER_STATUS_UNSUPPORTED` or a parse error and does not return a usable handle.
- The PostgreSQL negative case is an intentionally invalid SQL input and is not counted as a feature gap.
- MySQL, Oracle, and SQL Server unsupported cases are database-specific semantics that cannot be safely represented without extending the shared AST.

## Maintenance

- Update this file and [dialect_coverage.csv](./dialect_coverage.csv) when dialect cases are added or removed.
- Official syntax checklists are maintained per dialect: [PostgreSQL](./postgresql_official_syntax_coverage.en.md), [MySQL](./mysql_official_syntax_coverage.en.md), [Oracle](./oracle_official_syntax_coverage.en.md), and [SQL Server](./sqlserver_official_syntax_coverage.en.md).
