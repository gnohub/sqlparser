# Vastbase Official Syntax Coverage

This file maps Vastbase compatibility modes to the current executable
regression matrices. Vastbase publishes Oracle, MySQL, PostgreSQL, and SQL
Server compatibility areas; `sqlparser` exposes each area through an explicit
dialect entry.

## Coverage Matrix

| Mode | Official Reference | Fixture | Successful Cases | Expected-Failure Cases | Total Cases |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_oracle_dialect_input.json` | 220 | 0 | 220 |
| `vastbase-mysql` | [Backticks as identifiers](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html) | `tests/cases/vastbase_mysql_dialect_input.json` | 254 | 0 | 254 |
| `vastbase-postgresql` | [PostgreSQL compatibility](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281) | `tests/cases/vastbase_postgresql_dialect_input.json` | 199 | 0 | 199 |
| `vastbase-sqlserver` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_sqlserver_dialect_input.json` | 604 | 0 | 604 |

## Notes

- The `vastbase` CLI alias is fixed to `vastbase-oracle`.
- The library does not infer compatibility mode from SQL text.
- All 220 `vastbase-oracle` cases have `status = "final"`, with 791 independent
  patches. The project compatibility-entry contract includes
  `RETURNING ... INTO` on `INSERT ... VALUES`, `UPDATE`, and `DELETE` with
  `N >= 1` result targets paired by ordinal with exactly N colon-prefixed host
  binds. It rejects `BULK COLLECT`, receivers other than colon-prefixed binds,
  and unequal list lengths; the same `insert_column` patch inserts both sides.
  This boundary comes from the project's executable matrix and does not claim
  that the Vastbase server documentation defines the same syntax scope.
- All 604 `vastbase-sqlserver` cases have `status = "final"`, with 1850
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
