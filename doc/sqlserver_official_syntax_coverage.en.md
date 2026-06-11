# SQL Server Official Syntax Coverage

This file records SQL Server dialect coverage against the Microsoft Transact-SQL Reference. The complete item-by-item list is available in [sqlserver_official_syntax_coverage.csv](sqlserver_official_syntax_coverage.csv).

## Sources

- [Microsoft Learn: Transact-SQL Reference](https://learn.microsoft.com/en-us/sql/t-sql/language-reference)
- [MicrosoftDocs/sql-docs: `docs/t-sql`](https://github.com/MicrosoftDocs/sql-docs/tree/live/docs/t-sql)
- Audit date: 2026-06-11

The audit uses these official documentation directories:

| Directory | Items |
| --- | ---: |
| `docs/t-sql/statements` | 368 |
| `docs/t-sql/queries` | 41 |
| `docs/t-sql/language-elements` | 115 |
| `docs/t-sql/functions` | 361 |
| `docs/t-sql/data-types` | 44 |
| `docs/t-sql/system-stored-procedures` | 5 |
| Total | 934 |

## Classification

| Status | Meaning |
| --- | --- |
| `CURRENT` | Covered by the current SQL Server dialect, or directly representable by the existing core AST. |
| `HOOK_ONLY` | Not yet covered by executable regression cases, but implementable through dialect hooks, preprocessing, postprocessing, or type/function mapping without adding SQL Server-specific AST nodes. |
| `MIXED_MODEL` | Basic forms can use the existing AST and hooks, but full official syntax requires a SQL Server-specific model. |
| `MODEL_REQUIRED` | Requires a SQL Server-specific AST/model, typically for batches, variables, control flow, procedure bodies, administration, security, Service Broker, backup/restore, hints, dedicated table sources, or proprietary DDL semantics. |
| `REFERENCE_ONLY` | Official index, category, or explanatory page; excluded from implementation coverage rates. |

## Results

| Status | Items | Share of all 934 items |
| --- | ---: | ---: |
| `CURRENT` | 440 | 47.11% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 116 | 12.42% |
| `MODEL_REQUIRED` | 339 | 36.30% |
| `REFERENCE_ONLY` | 39 | 4.18% |

Excluding `REFERENCE_ONLY`, there are 895 implementation items. The current implementation covers 440 items and leaves 455 items uncovered.

| Uncovered class | Items | Share of 455 uncovered items |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 116 | 25.49% |
| `MODEL_REQUIRED` | 339 | 74.51% |

All items that can be represented by the existing AST plus dialect hooks are now covered by executable regression cases. The remaining uncovered items require a SQL Server-specific model or belong to mixed entries where a basic form is covered but full official syntax still requires model work.

Within `MIXED_MODEL`, 94 basic cases now have executable regression coverage, including database, schema, role, application role, user, synonym, type, index, sequence, view, statistics, `SELECT INTO`, basic full-text predicates, CTAS, aliases, subqueries, basic `ALTER DATABASE`, basic `ALTER TABLE`, `DROP TYPE`, public `DROP USER` restoration, `CREATE USER` SQL Server-specific options, common `ALTER USER` options, `CREATE ROLE AUTHORIZATION`, `ALTER ROLE` membership/rename, `ALTER SCHEMA TRANSFER`, basic `ALTER AUTHORIZATION`, `DROP SCHEMA IF EXISTS`, basic table and query hints, and basic session and execution-environment `SET` statements. Full official syntax for those entries remains counted as `MIXED_MODEL`.

## By Directory

| Directory | `CURRENT` | `HOOK_ONLY` | `MIXED_MODEL` | `MODEL_REQUIRED` | `REFERENCE_ONLY` | Total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `statements` | 17 | 0 | 102 | 248 | 1 | 368 |
| `queries` | 17 | 0 | 12 | 9 | 3 | 41 |
| `language-elements` | 63 | 0 | 2 | 47 | 3 | 115 |
| `functions` | 321 | 0 | 0 | 16 | 24 | 361 |
| `data-types` | 17 | 0 | 0 | 19 | 8 | 44 |
| `system-stored-procedures` | 5 | 0 | 0 | 0 | 0 | 5 |

## Conclusion

The SQL Server dialect now covers all official items that can be represented by the existing AST and dialect hooks. Of the remaining 455 uncovered items, 339 require a SQL Server-specific model and 116 are mixed entries where basic forms can be covered but full official syntax still requires model work.
