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
  "ALTER SESSION SET CURRENT_SCHEMA=KDES"
```

```bash
./bin/sqlparser_cli --dialect vastbase-mysql --mode view \
  "SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ?"
```

## Coverage

The four Vastbase modes are verified by executable regression matrices:

| Mode | Fixture | Unit Test | Successful Cases | Expected-Failure Cases | Total Cases |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | `tests/cases/vastbase_oracle_dialect_input.json` | `tests/unit/test_vastbase_oracle_dialect_case_matrix.c` | 188 | 21 | 209 |
| `vastbase-mysql` | `tests/cases/vastbase_mysql_dialect_input.json` | `tests/unit/test_vastbase_mysql_dialect_case_matrix.c` | 210 | 9 | 219 |
| `vastbase-postgresql` | `tests/cases/vastbase_postgresql_dialect_input.json` | `tests/unit/test_vastbase_postgresql_dialect_case_matrix.c` | 174 | 10 | 184 |
| `vastbase-sqlserver` | `tests/cases/vastbase_sqlserver_dialect_input.json` | `tests/unit/test_vastbase_sqlserver_dialect_case_matrix.c` | 547 | 38 | 585 |

The `vastbase-sqlserver` mode includes SQL Server DML `OUTPUT` result channels
and `IF...ELSE` control flow.

## Official References

- [Vastbase G100 positioning](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/aabafe2193fb4584b290dc9cdcc5c035)
- [Vastbase G100 V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d)
- [PostgreSQL compatibility](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281)
- [MySQL compatibility: backticks as identifiers](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html)
