# Dameng Official Syntax Coverage

This file records Dameng dialect coverage against the official DM_SQL
documentation. The complete checklist is
[dameng_official_syntax_coverage.csv](dameng_official_syntax_coverage.csv).

## Sources

- [Dameng DM_SQL Query Statements](https://eco.dameng.com/document/dm/zh-cn/pm/check-phrases.html)
- [Dameng DM_SQL Insert, Delete, and Update](https://eco.dameng.com/document/dm/zh-cn/pm/insertion-deletion-modification)
- [Dameng DM_SQL Definition Statements](https://eco.dameng.com/document/dm/zh-cn/pm/definition-statement.html)
- [SQL Statements in Dameng DMSQL Programs](https://eco.dameng.com/document/dm/zh-cn/pm/dm8_sql-sql-statement)
- Counting date: 2026-06-10

The scope is the set of official syntax groups touched by the current Dameng
dialect layer: queries, DML, common DDL, transaction statements, expressions,
privilege statements, and Dameng-specific semantics.

## Status Definitions

| Status | Meaning |
| --- | --- |
| `CURRENT` | The Dameng dialect has representative executable coverage, or the current AST can safely represent the syntax. |
| `HOOK_ONLY` | Not covered yet, but implementable through dialect hooks, preprocessing, postprocessing, or type/function mapping. |
| `MIXED_MODEL` | The basic form can use the current AST and hooks, but full official syntax needs a Dameng-specific model. |
| `MODEL_REQUIRED` | Requires a Dameng-specific model, usually for hierarchical queries, table transformations, DMSQL program units, host-variable return targets, or flashback. |
| `REFERENCE_ONLY` | An official index, category, or explanatory page that is not counted as an implementation unit. |

## Results

| Status | Syntax Groups | Share of 38 Groups |
| --- | ---: | ---: |
| `CURRENT` | 31 | 81.58% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 7 | 18.42% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 38 implementable syntax groups.
The current implementation covers 31 groups and leaves 7 groups uncovered.

| Uncovered Class | Syntax Groups | Share of 7 Uncovered Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 7 | 100.00% |

## Conclusion

The current Dameng dialect covers common query, DML, DDL, transaction,
privilege, current-schema statements, representative session-parameter
statements, and basic remote object references. The remaining seven syntax
groups depend on Dameng-specific query-model or program-unit semantics and are
not handled by PostgreSQL-compatible conversion.
