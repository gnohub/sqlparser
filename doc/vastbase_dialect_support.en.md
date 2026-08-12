# Vastbase Dialect Support

`sqlparser` provides four explicit Vastbase compatibility modes:

| CLI name | C enum | Compatibility entry |
| --- | --- | --- |
| `vastbase-oracle` | `SQLPARSER_DIALECT_VASTBASE_ORACLE` | Oracle compatibility mode |
| `vastbase-mysql` | `SQLPARSER_DIALECT_VASTBASE_MYSQL` | MySQL compatibility mode |
| `vastbase-postgresql` | `SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` | PostgreSQL compatibility mode |
| `vastbase-sqlserver` | `SQLPARSER_DIALECT_VASTBASE_SQLSERVER` | SQL Server compatibility mode |

In the CLI, `vastbase` is a deterministic alias for `vastbase-oracle`. The
library does not infer a compatibility mode from SQL text.

## Usage

```c
sqlparser_parse_options_t options;

sqlparser_parse_options_default(&options);
options.dialect = SQLPARSER_DIALECT_VASTBASE_ORACLE;
```

```bash
./bin/sqlparser_cli --dialect vastbase-oracle --mode view \
  "ALTER SESSION SET CURRENT_SCHEMA=APP"
```

```bash
./bin/sqlparser_cli --dialect vastbase-mysql --mode view \
  "SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ?"
```

## Coverage

The four Vastbase modes are verified by executable regression matrices:

| Mode | Fixture | Unit Test | Successful Cases | Expected-Failure Cases | Total Cases |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | `tests/cases/vastbase_oracle_dialect_input.json` | `tests/unit/test_vastbase_oracle_dialect_case_matrix.c` | 220 | 0 | 220 |
| `vastbase-mysql` | `tests/cases/vastbase_mysql_dialect_input.json` | `tests/unit/test_vastbase_mysql_dialect_case_matrix.c` | 254 | 0 | 254 |
| `vastbase-postgresql` | `tests/cases/vastbase_postgresql_dialect_input.json` | `tests/unit/test_vastbase_postgresql_dialect_case_matrix.c` | 199 | 0 | 199 |
| `vastbase-sqlserver` | `tests/cases/vastbase_sqlserver_dialect_input.json` | `tests/unit/test_vastbase_sqlserver_dialect_case_matrix.c` | 604 | 0 | 604 |

The `vastbase-sqlserver` mode includes SQL Server DML `OUTPUT` result channels
and `IF...ELSE` control flow.

The `vastbase-sqlserver` mode supports a basic `CONNECT BY` condition.
`START WITH`, `PRIOR`, `NOCYCLE`, and `CONNECT_BY_ROOT` are outside this
compatibility entry's support boundary.
Within a basic `CONNECT BY` query block, `CONNECT_BY_ROOT expr` without an
explicit `AS` is rejected as an out-of-bound hierarchy operator. An ordinary
field with the same name remains available through an explicit `AS` alias or a
delimited identifier.

The `vastbase-sqlserver` fixture contains 604 `final` cases and 1850
independent patches. As a project compatibility-entry contract, paired
`insert_column` applies only to a sink `OUTPUT ... INTO` channel with an
explicit non-empty sink-column list when the OUTPUT-target and sink-column
counts are strictly equal before the rewrite; it atomically inserts both sides
at the same ordinal. Legally unequal OUTPUT lists still parse and deparse, but
do not support paired insertion. Client `OUTPUT` and `OUTPUT ... INTO` without
an explicit sink-column list are also outside this mutation boundary. Three
cases cover 8↔8 pairs for INSERT, UPDATE, and DELETE, plus atomic head, middle,
and tail insertions that produce 9↔9 pairs. This contract and its executable
evidence do not claim the same official Vastbase server syntax support.

The `vastbase-oracle` fixture contains 220 `final` cases and 791 independent
patches. As a project compatibility-entry contract, `RETURNING ... INTO` on
`INSERT ... VALUES`, `UPDATE`, and `DELETE` supports `N >= 1` result targets
with exactly N colon-prefixed host binds, paired by ordinal. It rejects
`BULK COLLECT`, receivers other than colon-prefixed binds, and unequal list
lengths. The same `insert_column` patch inserts both the target and receiver;
one-sided insertion is not exposed. This contract and its executable evidence
do not claim the same official Vastbase server syntax support.

## Official References

- [Vastbase G100 positioning](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/aabafe2193fb4584b290dc9cdcc5c035)
- [Vastbase G100 V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d)
- [PostgreSQL compatibility](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281)
- [MySQL compatibility: backticks as identifiers](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html)
