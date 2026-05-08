# PostgreSQL Official Syntax Coverage

This file records PostgreSQL default-dialect coverage against the official
PostgreSQL SQL Commands documentation. The complete checklist is
[postgresql_official_syntax_coverage.csv](postgresql_official_syntax_coverage.csv).

## Sources

- [PostgreSQL 17: SQL Commands](https://www.postgresql.org/docs/17/sql-commands.html)
- [PostgreSQL 17: The SQL Language](https://www.postgresql.org/docs/17/sql.html)
- Counting date: 2026-05-08

The scope is the set of official SQL command groups that are directly relevant
to the public API, SQL View JSON, deparse, and executable regression tests.

## Status Definitions

| Status | Meaning |
| --- | --- |
| `CURRENT` | The default dialect has representative executable coverage, or the pinned PostgreSQL parser kernel directly represents the syntax. |
| `HOOK_ONLY` | The parser kernel can represent the syntax, but the public SQL View or regression matrix does not yet provide dedicated coverage. |
| `MIXED_MODEL` | The basic statement can be parsed, but complete object attribution, options, or structured editing requires public model extensions. |
| `MODEL_REQUIRED` | Full support requires a new public model or dedicated structure. |
| `REFERENCE_ONLY` | An official index, category, or explanatory page that is not counted as an implementation unit. |

## Results

| Status | Syntax Groups | Share of 41 Groups |
| --- | ---: | ---: |
| `CURRENT` | 36 | 87.80% |
| `HOOK_ONLY` | 4 | 9.76% |
| `MIXED_MODEL` | 1 | 2.44% |
| `MODEL_REQUIRED` | 0 | 0.00% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 41 implementable syntax groups.
The current implementation covers 36 groups and leaves 5 groups uncovered.

| Uncovered Class | Syntax Groups | Share of 5 Uncovered Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 4 | 80.00% |
| `MIXED_MODEL` | 1 | 20.00% |
| `MODEL_REQUIRED` | 0 | 0.00% |

## Conclusion

PostgreSQL is the default parser-kernel dialect. The remaining gaps are mostly
dedicated public SQL View, selector, or regression coverage rather than basic
parse capability. One syntax group requires public model extension for complete
coverage.
