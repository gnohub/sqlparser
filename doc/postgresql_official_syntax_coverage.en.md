# PostgreSQL Official Syntax Coverage

This file records PostgreSQL default-dialect coverage against the official
PostgreSQL SQL Commands documentation. The complete checklist is
[postgresql_official_syntax_coverage.csv](postgresql_official_syntax_coverage.csv).

## Sources

- [PostgreSQL 17: SQL Commands](https://www.postgresql.org/docs/17/sql-commands.html)
- [PostgreSQL 17: The SQL Language](https://www.postgresql.org/docs/17/sql.html)
- [PostgreSQL: Supported Features](https://www.postgresql.org/docs/current/features-sql-standard.html)
- [PostgreSQL pgsql-docs: document `N'...'` national character string literal syntax](https://www.postgresql.org/message-id/om3g7p7u3ztlrdp4tfswgulavljgn2fe6u2agk34mrr65dffuu%40cpzlzuv6flko)
The scope is the set of official SQL command groups that are directly relevant
to the public API, View JSON, deparse, and executable regression tests.

## Status Definitions

| Status | Meaning |
| --- | --- |
| `CURRENT` | The default dialect has representative executable coverage, or the pinned PostgreSQL parser kernel directly represents the syntax. |
| `HOOK_ONLY` | The parser kernel can represent the syntax, but the public query graph or regression matrix does not yet provide dedicated coverage. |
| `MIXED_MODEL` | The basic statement can be parsed, but complete object attribution, options, or structured editing requires public model extensions. |
| `MODEL_REQUIRED` | Full support requires a new public model or dedicated structure. |
| `REFERENCE_ONLY` | An official index, category, or explanatory page that is not counted as an implementation unit. |

## Results

| Status | Syntax Groups | Share of 42 Groups |
| --- | ---: | ---: |
| `CURRENT` | 41 | 97.62% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 1 | 2.38% |
| `MODEL_REQUIRED` | 0 | 0.00% |
| `REFERENCE_ONLY` | 0 | 0.00% |

After excluding `REFERENCE_ONLY`, there are 42 implementable syntax groups.
The current implementation covers 41 groups and leaves 1 group uncovered.

| Uncovered Class | Syntax Groups | Share of 1 Uncovered Group |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 1 | 100.00% |
| `MODEL_REQUIRED` | 0 | 0.00% |

## Conclusion

Explicit `WITH`/CTE column names are projected by ordinal onto directly enumerable, contiguous targets in the CTE `source_block` when no `*` or `alias.*` target is present, including delimiter state. A legal short PostgreSQL list overrides only the prefix. Direct `RETURNING` targets of a data-modifying CTE use the same rule. Set/recursive branches keep their own outputs, stars are not expanded, and a list longer than the direct target set produces no overlay. The base executable fixture now contains 228 `final` cases and 760 independent patches.

PostgreSQL `MERGE` supports an independent
`WHEN MATCHED ... THEN DELETE` action. `insert_column` supports column-only,
value-only, and paired modes. A not-matched INSERT with an omitted target-column
list still emits its list selector and can materialize the list, append a VALUES
cell while keeping the list omitted, or replace an existing cell; explicit
lists retain paired insertion. Two executable cases and 6 independent patches
verify these boundaries; the three patches in the omitted-list case run
independently. Core API unit tests verify final equal-width validation and
whole-batch rollback.

Query Graph preserves delimiter state independently for the database, schema,
and object segments of a relation and for each DML target column; no true flag
is emitted for an unquoted or absent segment. One executable case and four
independent patches cover ordinary DML, MERGE INSERT, `DEFAULT VALUES`, and
per-segment recomputation after patching. The PostgreSQL entry currently has no
database-link relation.

The `CURRENT` relation-DDL contract uses a root block with `kind = "ddl"` and
`ddl_role = "target"|"reference"`. It covers FK, LIKE, INHERITS, and partition
references, multi-object DROP, TRUNCATE, query-backed VIEW/CTAS/materialized
views, and `SELECT INTO`. DROP targets have no relation selector; same-name
quoted/unquoted segments, an `if` identifier, and U&/`UESCAPE` boundaries use
exact source state. Five new final cases and 17 independent patches verify
these boundaries. This evidence belongs only to the base PostgreSQL entry and
does not automatically cover compatibility entries.

PostgreSQL is the default parser-kernel dialect. No hook-only coverage gap
remains. The remaining gap is complete object attribution and option modeling
for role, user, and database-management statements, which requires public model
extension.
