# Vastbase 方言支持

`sqlparser` 为 Vastbase 提供四个显式兼容模式：

| CLI 名称 | C 枚举 | 兼容入口 |
| --- | --- | --- |
| `vastbase-oracle` | `SQLPARSER_DIALECT_VASTBASE_ORACLE` | Oracle 兼容模式 |
| `vastbase-mysql` | `SQLPARSER_DIALECT_VASTBASE_MYSQL` | MySQL 兼容模式 |
| `vastbase-postgresql` | `SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` | PostgreSQL 兼容模式 |
| `vastbase-sqlserver` | `SQLPARSER_DIALECT_VASTBASE_SQLSERVER` | SQL Server 兼容模式 |

CLI 中的 `vastbase` 是 `vastbase-oracle` 的确定性别名。库不会根据 SQL 文本自动猜测兼容模式。

## 使用方式

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

## 覆盖范围

Vastbase 四个模式分别通过以下可执行矩阵验证：

| 模式 | 回归夹具 | 单元测试 | 成功用例 | 预期失败用例 | 用例总数 |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | `tests/cases/vastbase_oracle_dialect_input.json` | `tests/unit/test_vastbase_oracle_dialect_case_matrix.c` | 188 | 21 | 209 |
| `vastbase-mysql` | `tests/cases/vastbase_mysql_dialect_input.json` | `tests/unit/test_vastbase_mysql_dialect_case_matrix.c` | 210 | 9 | 219 |
| `vastbase-postgresql` | `tests/cases/vastbase_postgresql_dialect_input.json` | `tests/unit/test_vastbase_postgresql_dialect_case_matrix.c` | 174 | 10 | 184 |
| `vastbase-sqlserver` | `tests/cases/vastbase_sqlserver_dialect_input.json` | `tests/unit/test_vastbase_sqlserver_dialect_case_matrix.c` | 547 | 38 | 585 |

`vastbase-sqlserver` 兼容模式包含 SQL Server DML `OUTPUT` 结果通道和 `IF...ELSE` 控制流能力。

## 官方资料

- [Vastbase G100 产品定位](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/aabafe2193fb4584b290dc9cdcc5c035)
- [Vastbase G100 V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d)
- [PostgreSQL 兼容性](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281)
- [MySQL 兼容性：反引号解释为标识符](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html)
