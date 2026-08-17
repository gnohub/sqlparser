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
  "ALTER SESSION SET CURRENT_SCHEMA=APP"
```

```bash
./bin/sqlparser_cli --dialect vastbase-mysql --mode view \
  "SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ?"
```

## 覆盖范围

Vastbase 四个模式分别通过以下可执行矩阵验证：

| 模式 | 回归夹具 | 单元测试 | 成功用例 | 预期失败用例 | 用例总数 |
| --- | --- | --- | ---: | ---: | ---: |
| `vastbase-oracle` | `tests/cases/vastbase_oracle_dialect_input.json` | `tests/unit/test_vastbase_oracle_dialect_case_matrix.c` | 220 | 0 | 220 |
| `vastbase-mysql` | `tests/cases/vastbase_mysql_dialect_input.json` | `tests/unit/test_vastbase_mysql_dialect_case_matrix.c` | 261 | 0 | 261 |
| `vastbase-postgresql` | `tests/cases/vastbase_postgresql_dialect_input.json` | `tests/unit/test_vastbase_postgresql_dialect_case_matrix.c` | 201 | 0 | 201 |
| `vastbase-sqlserver` | `tests/cases/vastbase_sqlserver_dialect_input.json` | `tests/unit/test_vastbase_sqlserver_dialect_case_matrix.c` | 605 | 0 | 605 |

`vastbase-sqlserver` 兼容模式包含 SQL Server DML `OUTPUT` 结果通道和 `IF...ELSE` 控制流能力。

`vastbase-mysql` 夹具包含 261 条 `final` 用例和 836 个独立 patch。项目兼容入口合同支持 MySQL 多目标多表 `UPDATE` 的 JOIN 链与逗号 relation 列表，每个 assignment 目标字段分别关联其写入 relation；多表形态不接受 `ORDER BY` 或 `LIMIT`。该合同及可执行证据不声称 Vastbase 服务端官网定义了相同语法范围。

`vastbase-sqlserver` 兼容模式支持基础 `CONNECT BY` 条件。`START WITH`、`PRIOR`、`NOCYCLE` 和 `CONNECT_BY_ROOT` 不在该兼容入口的支持范围内。
在包含基础 `CONNECT BY` 的查询块中，无显式 `AS` 的 `CONNECT_BY_ROOT expr` 形态按边界外层次操作符拒绝；同名普通字段可使用显式 `AS` 别名或定界标识符。

`vastbase-sqlserver` 夹具包含 605 条 `final` 用例和 1853 个独立 patch。作为项目兼容入口合同，成对 `insert_column` 只适用于 sink `OUTPUT ... INTO` 通道具有显式、非空 sink column list，且改写前 OUTPUT target 数与 sink column 数严格相等的场景；操作按同一序号原子插入两侧。原本合法的不等长 `OUTPUT` 仍可解析和反解析，但不支持该成对插入；client `OUTPUT` 和未显式列出 sink column 的 `OUTPUT ... INTO` 也不纳入该改写边界。3 条用例分别覆盖 INSERT、UPDATE、DELETE 的 8↔8 配对及头、中、尾原子插入后的 9↔9 配对。该合同及可执行证据不声称 Vastbase 服务端官网定义了相同语法范围。

`vastbase-oracle` 夹具包含 220 条 `final` 用例和 791 个独立 patch。作为项目兼容入口合同，`INSERT ... VALUES`、`UPDATE` 和 `DELETE` 的 `RETURNING ... INTO` 支持 `N >= 1` 个返回 target 与严格等长的 N 个冒号宿主 bind，并按 ordinal 配对；不接受 `BULK COLLECT`、非冒号 bind receiver 或数量不等的两侧列表。同一 `insert_column` patch 成对插入 target 和 receiver，不拆分单侧操作。该合同及可执行证据不声称 Vastbase 服务端官方支持同一语法范围。

## 官方资料

- [Vastbase G100 产品定位](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/aabafe2193fb4584b290dc9cdcc5c035)
- [Vastbase G100 V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d)
- [PostgreSQL 兼容性](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281)
- [MySQL 兼容性：反引号解释为标识符](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html)
