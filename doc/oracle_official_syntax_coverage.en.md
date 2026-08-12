# Oracle Official Syntax Coverage

This file records Oracle dialect coverage against the Oracle Database SQL
Language Reference. The complete checklist is
[oracle_official_syntax_coverage.csv](oracle_official_syntax_coverage.csv).

## Sources

- [Oracle Database 23ai SQL Language Reference: Types of SQL Statements](https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/Types-of-SQL-Statements.html)
- [Oracle Database 23ai SQL Language Reference: SELECT](https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/SELECT.html)
- [Oracle Database 23ai SQL Language Reference: ALTER SESSION](https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/ALTER-SESSION.html)
The scope is the set of official syntax groups touched by the current Oracle
dialect layer: queries, DML, common DDL, transaction statements, expressions,
privilege statements, and Oracle-specific semantics.

## Status Definitions

| Status | Meaning |
| --- | --- |
| `CURRENT` | The Oracle dialect has representative executable coverage, or the current AST can safely represent the syntax. |
| `HOOK_ONLY` | Not covered yet, but implementable through dialect hooks, preprocessing, postprocessing, or type/function mapping. |
| `MIXED_MODEL` | The basic form can use the current AST and hooks, but full official syntax needs an Oracle-specific model. |
| `MODEL_REQUIRED` | Requires an Oracle-specific model, usually for PL/SQL, table transformations, or flashback. |
| `REFERENCE_ONLY` | An official index, category, or explanatory page that is not counted as an implementation unit. |

## Results

| Status | Syntax Groups | Share of 47 Groups |
| --- | ---: | ---: |
| `CURRENT` | 38 | 80.85% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 4.26% |
| `MODEL_REQUIRED` | 7 | 14.89% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 47 implementable syntax groups.
Of these, 38 are classified as `CURRENT`, and 9 remain incomplete.

| Incomplete Class | Syntax Groups | Share of 9 Incomplete Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 22.22% |
| `MODEL_REQUIRED` | 7 | 77.78% |

## Conclusion

The Oracle `MERGE` `CURRENT` boundary includes an action `WHERE` on a matched
UPDATE, an attached `DELETE WHERE` on that same UPDATE branch, and a
conditional not-matched INSERT. Two executable cases and 8 independent patches
verify this boundary.

The Oracle hierarchical-query `CURRENT` boundary includes `START WITH`,
`CONNECT BY`, unary `PRIOR`, `LEVEL`, `CONNECT_BY_ROOT`,
`CONNECT_BY_ISLEAF`, `CONNECT_BY_ISCYCLE`, `NOCYCLE`, and compound hierarchy
conditions. Source text must place `START WITH` before `CONNECT BY`. The
executable matrix contains 4 `final` cases and 20 independent patches; the
reverse clause order with `CONNECT BY` before `START WITH` is outside the
current boundary.

`RETURNING ... INTO` on `INSERT`, `UPDATE`, and `DELETE` supports `N >= 1`
result targets with exactly N colon-prefixed host binds, paired by ordinal. It
rejects `BULK COLLECT`, receivers other than colon-prefixed binds, and unequal
list lengths. A paired `insert_column` inserts both the target and receiver in
the same patch rather than exposing one-sided operations. O198 through O200
each verify eight pairs and nine pairs after insertion at the head, middle, or
tail. The complete Oracle executable fixture contains 251 `final` cases and
852 independent patches. The remaining Oracle gaps are mainly Oracle-specific
semantics that
cannot be safely mapped to the shared AST. `SYNONYM` and `EXPLAIN PLAN FOR` now
cover basic statement parsing, keywords, and deparse output; full object
attributes or execution-plan semantics require an Oracle-specific model.
