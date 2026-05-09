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

| Status | Syntax Groups | Share of 40 Groups |
| --- | ---: | ---: |
| `CURRENT` | 16 | 40.00% |
| `HOOK_ONLY` | 7 | 17.50% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 17 | 42.50% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 40 implementable syntax groups.
The current implementation covers 16 groups and leaves 24 groups uncovered.

| Uncovered Class | Syntax Groups | Share of 24 Uncovered Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 7 | 29.17% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 17 | 70.83% |

## Conclusion

The remaining MySQL gaps are concentrated in MySQL-specific DML semantics,
table options, type attributes, and program objects. Seven uncovered groups can
be implemented with the current AST and hooks; seventeen groups require a
MySQL-specific model.
