# SQL Server Official Syntax Coverage

This file records SQL Server dialect coverage against the Microsoft Transact-SQL Reference. The complete item-by-item list is available in [sqlserver_official_syntax_coverage.csv](sqlserver_official_syntax_coverage.csv).

## Sources

- [Microsoft Learn: Transact-SQL Reference](https://learn.microsoft.com/en-us/sql/t-sql/language-reference)
- [MicrosoftDocs/sql-docs: `docs/t-sql`](https://github.com/MicrosoftDocs/sql-docs/tree/live/docs/t-sql)
- Audit date: 2026-05-08

The audit uses these official documentation directories:

| Directory | Items |
| --- | ---: |
| `docs/t-sql/statements` | 368 |
| `docs/t-sql/queries` | 41 |
| `docs/t-sql/language-elements` | 115 |
| `docs/t-sql/functions` | 361 |
| `docs/t-sql/data-types` | 44 |
| Total | 929 |

## Classification

| Status | Meaning |
| --- | --- |
| `CURRENT` | Covered by the current SQL Server dialect, or directly representable by the existing core AST. |
| `HOOK_ONLY` | Not currently covered, but implementable through dialect hooks, preprocessing, postprocessing, or type/function mapping without adding SQL Server-specific AST nodes. |
| `MIXED_MODEL` | Basic forms can use the existing AST and hooks, but full official syntax requires a SQL Server-specific model. |
| `MODEL_REQUIRED` | Requires a SQL Server-specific AST/model, typically for batches, variables, control flow, procedure bodies, administration, security, Service Broker, backup/restore, hints, dedicated table sources, or proprietary DDL semantics. |
| `REFERENCE_ONLY` | Official index, category, or explanatory page; excluded from implementation coverage rates. |

## Results

| Status | Items | Share of all 929 items |
| --- | ---: | ---: |
| `CURRENT` | 186 | 20.02% |
| `HOOK_ONLY` | 235 | 25.30% |
| `MIXED_MODEL` | 82 | 8.83% |
| `MODEL_REQUIRED` | 387 | 41.66% |
| `REFERENCE_ONLY` | 39 | 4.20% |

Excluding `REFERENCE_ONLY`, there are 890 implementation items. The current implementation covers 186 items and leaves 704 items uncovered.

| Uncovered class | Items | Share of 704 uncovered items |
| --- | ---: | ---: |
| `HOOK_ONLY` | 235 | 33.38% |
| `MIXED_MODEL` | 82 | 11.65% |
| `MODEL_REQUIRED` | 387 | 54.97% |

For full official syntax coverage, `MIXED_MODEL + MODEL_REQUIRED` is 469 items, or 66.62% of uncovered items. Items that can be completed using only the existing AST and dialect hooks account for 235 items, or 33.38%.

## By Directory

| Directory | `CURRENT` | `HOOK_ONLY` | `MIXED_MODEL` | `MODEL_REQUIRED` | `REFERENCE_ONLY` | Total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `statements` | 12 | 5 | 59 | 291 | 1 | 368 |
| `queries` | 15 | 0 | 10 | 13 | 3 | 41 |
| `language-elements` | 51 | 0 | 13 | 48 | 3 | 115 |
| `functions` | 94 | 227 | 0 | 16 | 24 | 361 |
| `data-types` | 14 | 3 | 0 | 19 | 8 | 44 |

## Conclusion

Among the currently uncovered SQL Server items, 387 items cannot be solved by the existing AST alone, which is 54.97%. Another 82 items are partially hookable but require a dedicated SQL Server model for complete official syntax coverage. Under the full-syntax criterion, 469 uncovered items require a SQL Server-specific model, which is 66.62%.
