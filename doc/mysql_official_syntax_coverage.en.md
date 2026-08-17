# MySQL Official Syntax Coverage

This file records MySQL dialect coverage against the MySQL 8.4 Reference
Manual. The complete checklist is
[mysql_official_syntax_coverage.csv](mysql_official_syntax_coverage.csv).

## Sources

- [MySQL 8.4 Reference Manual: SQL Statements](https://dev.mysql.com/doc/refman/8.4/en/sql-statements.html)
- [MySQL 8.4 Reference Manual: Language Structure](https://dev.mysql.com/doc/refman/8.4/en/language-structure.html)
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

The `CURRENT` boundary for `UPDATE_JOIN` includes multi-target assignments
across JOIN chains and comma-separated relation lists. Each assignment target
field identifies its write relation. The multi-table form rejects `ORDER BY`
and `LIMIT`.

## Results

| Status | Syntax Groups | Share of 48 Groups |
| --- | ---: | ---: |
| `CURRENT` | 41 | 85.42% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 4.17% |
| `MODEL_REQUIRED` | 5 | 10.42% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 48 implementable syntax groups.
The current implementation covers 41 groups and leaves 7 groups uncovered.

| Uncovered Class | Syntax Groups | Share of 7 Uncovered Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 28.57% |
| `MODEL_REQUIRED` | 5 | 71.43% |

## Conclusion

The remaining MySQL gaps are concentrated in complete multi-table DELETE
semantics, REPLACE partition variants, program objects, and administrative
statements. Multi-target multi-table UPDATE, INSERT/UPDATE/DELETE modifiers,
basic public REPLACE forms, and `CREATE TABLE` column attributes, table options,
and partition tails without query expressions are covered. Two groups have
basic coverage but need a dedicated model for full official semantics; five
groups require a MySQL-specific model.
