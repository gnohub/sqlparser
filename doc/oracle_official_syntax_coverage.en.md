# Oracle Official Syntax Coverage

This file records Oracle dialect coverage against the Oracle Database SQL
Language Reference. The complete checklist is
[oracle_official_syntax_coverage.csv](oracle_official_syntax_coverage.csv).

## Sources

- [Oracle Database 23ai SQL Language Reference: Types of SQL Statements](https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/Types-of-SQL-Statements.html)
- [Oracle Database 23ai SQL Language Reference: SELECT](https://docs.oracle.com/en/database/oracle/oracle-database/23/sqlrf/SELECT.html)
- Counting date: 2026-05-08

The scope is the set of official syntax groups touched by the current Oracle
dialect layer: queries, DML, common DDL, transaction statements, expressions,
privilege statements, and Oracle-specific semantics.

## Status Definitions

| Status | Meaning |
| --- | --- |
| `CURRENT` | The Oracle dialect has representative executable coverage, or the current AST can safely represent the syntax. |
| `HOOK_ONLY` | Not covered yet, but implementable through dialect hooks, preprocessing, postprocessing, or type/function mapping. |
| `MIXED_MODEL` | The basic form can use the current AST and hooks, but full official syntax needs an Oracle-specific model. |
| `MODEL_REQUIRED` | Requires an Oracle-specific model, usually for hierarchical queries, multi-table insert, PL/SQL, table transformations, flashback, session semantics, or remote object references. |
| `REFERENCE_ONLY` | An official index, category, or explanatory page that is not counted as an implementation unit. |

## Results

| Status | Syntax Groups | Share of 45 Groups |
| --- | ---: | ---: |
| `CURRENT` | 30 | 66.67% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 15 | 33.33% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 45 implementable syntax groups.
The current implementation covers 30 groups and leaves 15 groups uncovered.

| Uncovered Class | Syntax Groups | Share of 15 Uncovered Groups |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 0 | 0.00% |
| `MODEL_REQUIRED` | 15 | 100.00% |

## Conclusion

The remaining Oracle gaps are all Oracle-specific semantics that cannot be
safely mapped to the shared AST. Future Oracle expansion should use an
Oracle-specific model instead of downgrading those semantics to PostgreSQL
compatible forms.
