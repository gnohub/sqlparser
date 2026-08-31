# Vastbase Official Syntax Coverage

This file maps Vastbase compatibility modes to the current executable
regression matrices. Vastbase publishes Oracle, MySQL, PostgreSQL, and SQL
Server compatibility areas; `sqlparser` exposes each area through an explicit
dialect entry.

## Coverage Matrix

| Mode | Official Reference | Fixture | Successful Cases | Expected-Failure Cases | Total Cases |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_oracle_dialect_input.json` | 254 | 0 | 254 |
| `vastbase-mysql` | [Backticks as identifiers](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html) | `tests/cases/vastbase_mysql_dialect_input.json` | 271 | 0 | 271 |
| `vastbase-postgresql` | [PostgreSQL compatibility](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281) | `tests/cases/vastbase_postgresql_dialect_input.json` | 213 | 0 | 213 |
| `vastbase-sqlserver` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_sqlserver_dialect_input.json` | 625 | 0 | 625 |

## Notes

- The `vastbase` CLI alias is fixed to `vastbase-oracle`.
- The library does not infer compatibility mode from SQL text.
- All 271 `vastbase-mysql` cases have `status = "final"`, with 858 independent
  patches. The project compatibility-entry contract supports MySQL multi-target
  multi-table `UPDATE` across JOIN chains and comma-separated relation lists,
  with each assignment target field identifying its write relation. `ORDER BY`
  and `LIMIT` are rejected for this multi-table form. This boundary comes from
  the project's executable matrix and does not claim that the Vastbase server
  documentation defines the same syntax scope.
- All 254 `vastbase-oracle` cases have `status = "final"`, with 832 independent
  patches. The project compatibility-entry contract includes
  `RETURNING ... INTO` on `INSERT ... VALUES`, `UPDATE`, and `DELETE` with
  `N >= 1` result targets paired by ordinal with exactly N colon-prefixed host
  binds. It rejects `BULK COLLECT`, receivers other than colon-prefixed binds,
  and unequal list lengths; the same `insert_column` patch inserts both sides.
  This boundary comes from the project's executable matrix and does not claim
  that the Vastbase server documentation defines the same syntax scope.
- All 213 `vastbase-postgresql` cases have `status = "final"`, with 689
  independent patches.
- All 625 `vastbase-sqlserver` cases have `status = "final"`, with 1892
  independent patches. The project compatibility-entry paired `insert_column`
  applies only to a sink `OUTPUT ... INTO` with an explicit non-empty
  sink-column list when the OUTPUT-target and sink-column counts are strictly
  equal before the rewrite, and atomically inserts both sides at the same
  ordinal. Legally unequal OUTPUT lists still parse and deparse but do not
  support paired insertion; client `OUTPUT` and `OUTPUT ... INTO` without an
  explicit sink-column list are outside this mutation boundary. Three cases
  cover 8↔8 pairs for INSERT, UPDATE, and DELETE and head, middle, and tail
  insertions that produce 9↔9 pairs. This is executable project-contract
  evidence, not a claim that Vastbase server documentation defines the same
  syntax scope.
- The project compatibility-entry contract for all four modes uses the existing
  `insert_column` operation to insert a MERGE INSERT target column, a VALUES
  cell, or both. Existing columns and cells remain independently replaceable.
  An omitted target list with VALUES exposes `target_list_selector` and may be
  materialized or remain omitted. The two sides may be temporarily unequal
  within a patch batch; if an explicit target list exists at batch completion,
  its width must match the VALUES width or the whole batch rolls back
  atomically. Paired deletion is unchanged, and `MERGE INSERT DEFAULT VALUES`
  is outside this mutation boundary. This is executable project-contract
  evidence, not a claim that Vastbase server documentation defines the same
  syntax scope.
- Twenty-two final cases and 42 independent patches across the four entries
  verify delimited state for relation database/schema/object segments and DML
  columns. `vastbase-oracle` also covers database links and `INSERT ALL/FIRST`
  branches; `vastbase-sqlserver` also covers `OUTPUT ... INTO` sink relations
  and sink columns. `database_quoted_identifier`, `schema_quoted_identifier`,
  relation `quoted_identifier`, `link_quoted_identifier`, DML-column
  `quoted_identifier`, existing `alias_quoted_identifier`, field
  `quoted_identifier`, and `output_quoted_identifier` each apply only to their
  exact source token. An undelimited source leaves the C flag at `0` and omits
  the corresponding View JSON key. Relation and DML-column patches recompute
  flags from the new source, and patched and reparsed handles have identical
  Views. Each entry recognizes only its existing delimiter: double quotes for
  Oracle/PostgreSQL, backticks for MySQL, and brackets for SQL Server.
  `U&"..."` is outside this contract. This is a project compatibility-entry
  contract, not a claim that Vastbase server documentation defines the same
  scope.
- Twenty-eight final cases and 50 independent patches across the four entries
  verify relation-bearing DDL Query Graphs. A `kind = "ddl"` root block uses
  relation `ddl_role = "target"` / `"reference"` to distinguish operated and
  referenced objects while preserving delimiter state for each
  database/schema/object source segment. CREATE VIEW, CTAS, CREATE MATERIALIZED
  VIEW, and `SELECT INTO` at verified entries link the DDL target to a separate
  SELECT block through `source_block`; foreign keys and verified PostgreSQL
  LIKE/INHERITS/partition objects are references. The PostgreSQL-compatible
  entry also verifies the foreign-table target lifecycle; MySQL/PostgreSQL/SQL
  Server-compatible entries verify exact source state for same-name
  quoted/unquoted DROP segments, and the SQL Server-compatible entry also
  verifies public-surface retention in an ordinary multi-statement batch. Only
  dialect forms reconciled by the fixtures belong to this project contract.
  This does not claim that Vastbase server documentation defines the same
  syntax scope.
- Each entry verifies ordinal overlay of explicit CTE column names onto
  directly enumerable source-block targets only for forms valid in its own
  fixture, with exact delimiter state. Vastbase PostgreSQL additionally
  verifies that shorter lists override only the target prefix. Explicit names
  participate in DML `source_target` lineage, and repeated references share the
  overlaid source block. The applicable entries verify SET-result,
  recursive-SET, or star boundaries according to their own fixtures, without
  fabricating result targets, crossing branches, or expanding stars. This is
  an executable project contract, not an official Vastbase server syntax claim.
- The `vastbase-sqlserver` hierarchical-query boundary contains only a basic
  `CONNECT BY` condition. `START WITH`, `PRIOR`, `NOCYCLE`, and
  `CONNECT_BY_ROOT` are outside this mode's current boundary.
- In that basic hierarchy block, only the ambiguous `CONNECT_BY_ROOT expr`
  form without an explicit `AS` is rejected. A standalone
  `CONNECT_BY_ROOT` target remains an ordinary field; an explicit `AS` alias
  or a delimited identifier can be used when an alias is needed.
- All four fixtures currently contain only `status = "final"` cases and have
  zero expected-failure cases. Fixture counts do not measure official syntax
  coverage.
