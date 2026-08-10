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
| `MODEL_REQUIRED` | Requires an Oracle-specific model, usually for hierarchical queries, PL/SQL, table transformations, or flashback. |
| `REFERENCE_ONLY` | An official index, category, or explanatory page that is not counted as an implementation unit. |

## Results

| Status | Syntax Groups | Share of 47 Groups |
| --- | ---: | ---: |
| `CURRENT` | 37 | 78.72% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 4.26% |
| `MODEL_REQUIRED` | 8 | 17.02% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 47 implementable syntax groups.
Of these, 37 are classified as `CURRENT`, and 10 remain incomplete.

| Incomplete Class | Syntax Groups | Share of 10 Incomplete Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 2 | 20.00% |
| `MODEL_REQUIRED` | 8 | 80.00% |

## Conclusion

`RETURNING ... INTO` covers one result target and one colon-prefixed host bind
in `INSERT`, `UPDATE`, and `DELETE`; multiple targets, multiple binds, and
`BULK COLLECT` remain outside the current boundary. The remaining Oracle gaps
are mainly Oracle-specific semantics that cannot be safely mapped to the shared
AST. `SYNONYM` and `EXPLAIN PLAN FOR` now cover basic statement parsing,
keywords, and deparse output; full object attributes or execution-plan
semantics require an Oracle-specific model.
