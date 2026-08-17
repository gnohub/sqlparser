# Dameng Official Syntax Coverage

This file records Dameng dialect coverage against the official DM_SQL
documentation. The complete checklist is
[dameng_official_syntax_coverage.csv](dameng_official_syntax_coverage.csv).

## Sources

- [Dameng DM_SQL Query Statements](https://eco.dameng.com/document/dm/zh-cn/pm/check-phrases.html)
- [Dameng DM_SQL Insert, Delete, and Update](https://eco.dameng.com/document/dm/zh-cn/pm/insertion-deletion-modification)
- [Dameng DM_SQL Definition Statements](https://eco.dameng.com/document/dm/zh-cn/pm/definition-statement.html)
- [SQL Statements in Dameng DMSQL Programs](https://eco.dameng.com/document/dm/zh-cn/pm/dm8_sql-sql-statement)
The scope is the set of official syntax groups touched by the current Dameng
dialect layer: queries, DML, common DDL, transaction statements, expressions,
privilege statements, and Dameng-specific semantics.

## Status Definitions

| Status | Meaning |
| --- | --- |
| `CURRENT` | The Dameng dialect has representative executable coverage, or the current AST can safely represent the syntax. |
| `HOOK_ONLY` | Not covered yet, but implementable through dialect hooks, preprocessing, postprocessing, or type/function mapping. |
| `MIXED_MODEL` | The basic form can use the current AST and hooks, but full official syntax needs a Dameng-specific model. |
| `MODEL_REQUIRED` | Requires a Dameng-specific model, usually for table transformations, DMSQL program units, or flashback. |
| `REFERENCE_ONLY` | An official index, category, or explanatory page that is not counted as an implementation unit. |

The `CURRENT` boundary for `RETURNING_INTO` is
`RETURNING <target, ...> INTO <:bind, ...>` for `INSERT` and `DELETE`, and
`RETURN <target, ...> INTO <:bind, ...>` for `UPDATE`. Each list contains
`N >= 1` items; the lists have strictly equal lengths and pair by ordinal, and
every receiver is a colon-prefixed host bind. This boundary excludes
`BULK COLLECT`, receivers that are not colon-prefixed host binds, and unequal
list lengths.

The `CURRENT` boundary for `UPDATE` includes multi-table single-target forms
using JOIN chains, comma-separated relation lists, or both. Every SET
assignment must target the same table object.

## Results

| Status | Syntax Groups | Share of 38 Groups |
| --- | ---: | ---: |
| `CURRENT` | 33 | 86.84% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 5 | 13.16% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 38 implementable syntax groups.
The current implementation covers 33 groups and leaves 5 groups uncovered.

| Uncovered Class | Syntax Groups | Share of 5 Uncovered Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 5 | 100.00% |

## Conclusion

The Dameng `MERGE` `CURRENT` boundary includes an action `WHERE` on a matched
UPDATE and an attached `DELETE WHERE` on that same UPDATE branch. One
executable case and 3 independent patches verify this boundary.

The Dameng hierarchical-query `CURRENT` boundary accepts both source clause
orders for `START WITH` and `CONNECT BY`, two parent-child field orientations
for unary `PRIOR`, `LEVEL`, `CONNECT_BY_ROOT`, and `NOCYCLE`. The executable matrix
contains 4 `final` cases and 20 independent patches.

The Dameng multi-table `UPDATE` `CURRENT` boundary always has one write target.
Six `final` cases and 17 independent patches cover first, middle, and last
relation targets, distinct aliases of the same table, JOIN chains, and mixed
JOIN/comma forms.

The current Dameng dialect covers common query, DML, DDL, transaction,
privilege, current-schema statements, representative session-parameter
statements, basic remote object references, and the `N >= 1`, strictly
equal-length, ordinally paired `RETURN` / `RETURNING ... INTO` forms defined
above. Three executable cases cover 8↔8 pairs for INSERT, UPDATE, and DELETE,
plus atomic head, middle, and tail insertions that produce 9↔9 pairs. The
remaining five syntax groups depend on Dameng-specific query-model or
program-unit semantics and are not handled by PostgreSQL-compatible conversion.
