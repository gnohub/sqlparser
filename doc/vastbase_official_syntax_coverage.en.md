# Vastbase Official Syntax Coverage

This file maps Vastbase compatibility modes to the current executable
regression matrices. Vastbase publishes Oracle, MySQL, PostgreSQL, and SQL
Server compatibility areas; `sqlparser` exposes each area through an explicit
dialect entry.

## Coverage Matrix

| Mode | Official reference | Fixture | Supported cases | Explicitly unsupported cases | Total |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_oracle_dialect_input.json` | 131 | 16 | 147 |
| `vastbase-mysql` | [Backticks as identifiers](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html) | `tests/cases/vastbase_mysql_dialect_input.json` | 85 | 18 | 103 |
| `vastbase-postgresql` | [PostgreSQL compatibility](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281) | `tests/cases/vastbase_postgresql_dialect_input.json` | 136 | 1 | 137 |
| `vastbase-sqlserver` | [V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d) | `tests/cases/vastbase_sqlserver_dialect_input.json` | 330 | 15 | 345 |

## Notes

- The `vastbase` CLI alias is fixed to `vastbase-oracle`.
- The library does not infer compatibility mode from SQL text.
- Explicitly unsupported cases follow the corresponding base dialect and are
  mostly database-specific semantics that are not safely representable in the
  shared AST yet.
