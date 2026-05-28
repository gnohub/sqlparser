# MySQL Official Syntax Coverage

This file records MySQL dialect coverage against the MySQL 8.4 Reference
Manual. The complete checklist is
[mysql_official_syntax_coverage.csv](mysql_official_syntax_coverage.csv).

## Sources

- [MySQL 8.4 Reference Manual: SQL Statements](https://dev.mysql.com/doc/refman/8.4/en/sql-statements.html)
- [MySQL 8.4 Reference Manual: Language Structure](https://dev.mysql.com/doc/refman/8.4/en/language-structure.html)
- Counting date: 2026-05-08

The scope is the set of official syntax groups touched by the current MySQL
dialect layer: queries, DML, common DDL, transaction statements, expressions,
type attributes, and MySQL-specific semantics.

## Status Definitions

| Status | Meaning |
| --- | --- |
| `CURRENT` | The MySQL dialect has representative executable coverage, or the current AST can safely represent the syntax. |
| `HOOK_ONLY` | Not covered yet, but implementable through dialect hooks, preprocessing, postprocessing, or type/function mapping. |
| `MIXED_MODEL` | The basic form can use the current AST and hooks, but full official syntax needs a dedicated model. |
| `MODEL_REQUIRED` | Requires a MySQL-specific model, usually for MySQL-specific DML semantics, DDL options, type attributes, or program units. |
| `REFERENCE_ONLY` | An official index, category, or explanatory page that is not counted as an implementation unit. |

## Results

| Status | Syntax Groups | Share of 44 Groups |
| --- | ---: | ---: |
| `CURRENT` | 21 | 47.73% |
| `HOOK_ONLY` | 5 | 11.36% |
| `MIXED_MODEL` | 2 | 4.55% |
| `MODEL_REQUIRED` | 16 | 36.36% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 44 implementable syntax groups.
The current implementation covers 21 groups and leaves 23 groups uncovered.

| Uncovered Class | Syntax Groups | Share of 23 Uncovered Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 5 | 21.74% |
| `MIXED_MODEL` | 2 | 8.70% |
| `MODEL_REQUIRED` | 16 | 69.57% |

## Conclusion

The remaining MySQL gaps are concentrated in outer-join DML, table options,
type attributes, and program objects. Five uncovered groups can be implemented
with the current AST and hooks; two groups have basic coverage but need a
dedicated model for full official semantics; sixteen groups require a
MySQL-specific model.
