# SQL Server Official Syntax Coverage

This file records SQL Server dialect coverage against the Microsoft Transact-SQL Reference. The complete item-by-item list is available in [sqlserver_official_syntax_coverage.csv](sqlserver_official_syntax_coverage.csv).

## Sources

- [Microsoft Learn: Transact-SQL Reference](https://learn.microsoft.com/en-us/sql/t-sql/language-reference)
- [MicrosoftDocs/sql-docs: `docs/t-sql`](https://github.com/MicrosoftDocs/sql-docs/tree/live/docs/t-sql)
The audit uses these official documentation directories:

| Directory | Items |
| --- | ---: |
| `docs/t-sql/statements` | 368 |
| `docs/t-sql/queries` | 41 |
| `docs/t-sql/language-elements` | 115 |
| `docs/t-sql/functions` | 361 |
| `docs/t-sql/data-types` | 44 |
| `docs/t-sql/system-stored-procedures` | 5 |
| Total | 934 |

## Classification

| Status | Meaning |
| --- | --- |
| `CURRENT` | Covered by the current SQL Server dialect, or directly representable by the existing core AST. |
| `HOOK_ONLY` | Has no executable regression case, but is representable through dialect hooks, preprocessing, postprocessing, or type/function mapping without adding SQL Server-specific AST nodes. |
| `MIXED_MODEL` | Basic forms can use the existing AST and hooks, but full official syntax requires a SQL Server-specific model. |
| `MODEL_REQUIRED` | Requires a SQL Server-specific AST/model, typically for batches, variables, control flow, procedure bodies, administration, security, Service Broker, backup/restore, hints, dedicated table sources, or proprietary DDL semantics. |
| `REFERENCE_ONLY` | Official index, category, or explanatory page; excluded from implementation coverage rates. |

## Results

| Status | Items | Share of all 934 items |
| --- | ---: | ---: |
| `CURRENT` | 442 | 47.32% |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 117 | 12.53% |
| `MODEL_REQUIRED` | 336 | 35.97% |
| `REFERENCE_ONLY` | 39 | 4.18% |

Excluding `REFERENCE_ONLY`, there are 895 implementation items. The current implementation covers 442 items and leaves 453 items uncovered.

| Uncovered class | Items | Share of 453 uncovered items |
| --- | ---: | ---: |
| `HOOK_ONLY` | 0 | 0.00% |
| `MIXED_MODEL` | 117 | 25.83% |
| `MODEL_REQUIRED` | 336 | 74.17% |

All items that can be represented by the existing AST plus dialect hooks are now covered by executable regression cases. The remaining uncovered items require a SQL Server-specific model or belong to mixed entries where a basic form is covered but full official syntax still requires model work.

Within `MIXED_MODEL`, 95 basic cases now have executable regression coverage, including database, schema, role, application role, user, synonym, type, index, sequence, view, statistics, `SELECT INTO`, basic full-text predicates, CTAS, aliases, subqueries, basic `ALTER DATABASE`, basic `ALTER TABLE`, `DROP TYPE`, public `DROP USER` restoration, `CREATE USER` SQL Server-specific options, common `ALTER USER` options, `CREATE ROLE AUTHORIZATION`, `ALTER ROLE` membership/rename, `ALTER SCHEMA TRANSFER`, basic `ALTER AUTHORIZATION`, `DROP SCHEMA IF EXISTS`, basic table and query hints, basic session and execution-environment `SET` statements, and `BEGIN...END` inside `IF...ELSE` branches. Full official syntax for those entries remains counted as `MIXED_MODEL`.

The `OUTPUT` item has 33 successful and 10 error-path cases covering `INSERT`,
`UPDATE`, `DELETE`, `MERGE`, ordered sink/client channels, and nested DML. A
paired `insert_column` can atomically insert both sides at the same ordinal when
the sink column list is explicit and nonempty and the target/column counts are
equal before the mutation. Otherwise-valid unequal `OUTPUT ... INTO` lists
remain parseable and deparseable, but do not support this paired mutation.
Sink relations preserve bracket-delimiter state independently for database,
schema, and object segments, while sink columns carry their own
`quoted_identifier`. One added case and seven independent patches cover all
four DML sinks and post-patch recomputation.

Ordinary relations likewise preserve bracket state per database, schema, and
object segment, and DML target columns preserve delimiter state independently;
no true flag is emitted for an unquoted or absent segment. A second added case
and seven independent patches cover SELECT, INSERT, UPDATE FROM, DELETE, and
MERGE.

The `IF...ELSE` item has 36 successful and 9 error-path cases covering
single-statement branches, multi-statement blocks, `ELSE IF`, nesting,
condition queries, DML, DDL, transactions, and syntax boundaries.

The `MERGE` item includes an independent `WHEN MATCHED ... THEN DELETE`
action. `insert_column` supports column-only, value-only, and paired modes. A
not-matched INSERT with an omitted target-column list still emits its list
selector and can materialize the list, append a VALUES cell while keeping the
list omitted, or replace an existing cell; explicit lists retain paired
insertion. Two executable cases and 6 independent patches verify these
boundaries; the three patches in the omitted-list case run independently. Core
API unit tests verify final equal-width validation and whole-batch rollback.

## By Directory

| Directory | `CURRENT` | `HOOK_ONLY` | `MIXED_MODEL` | `MODEL_REQUIRED` | `REFERENCE_ONLY` | Total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `statements` | 17 | 0 | 102 | 248 | 1 | 368 |
| `queries` | 18 | 0 | 12 | 8 | 3 | 41 |
| `language-elements` | 64 | 0 | 3 | 45 | 3 | 115 |
| `functions` | 321 | 0 | 0 | 16 | 24 | 361 |
| `data-types` | 17 | 0 | 0 | 19 | 8 | 44 |
| `system-stored-procedures` | 5 | 0 | 0 | 0 | 0 | 5 |

## Conclusion

The current base relation-DDL contract uses a root block with `kind = "ddl"`
and `ddl_role = "target"|"reference"`. It covers FK references in CREATE/ALTER
TABLE, CREATE INDEX, TRUNCATE, multi-object DROP, CREATE VIEW, and official
`SELECT INTO`. Query-backed targets point through `source_block` to a SELECT
block. DROP targets have no relation selector, and same-name quoted/unquoted
segments retain exact source state. Ten new final cases and 12 independent
patches also verify that relation patches in single statements and ordinary
multi-statement batches do not add `USING btree` to CREATE INDEX and retain the
public `TRUNCATE TABLE` surface. This base-entry evidence does not automatically
cover compatibility entries.

The SQL Server dialect now covers all official items that can be represented by the existing AST and dialect hooks. Of the remaining 453 uncovered items, 336 require a SQL Server-specific model and 117 are mixed entries where basic forms can be covered but full official syntax still requires model work.
