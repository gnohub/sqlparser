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
| `vastbase-oracle` | `tests/cases/vastbase_oracle_dialect_input.json` | `tests/unit/test_vastbase_oracle_dialect_case_matrix.c` | 254 | 0 | 254 |
| `vastbase-mysql` | `tests/cases/vastbase_mysql_dialect_input.json` | `tests/unit/test_vastbase_mysql_dialect_case_matrix.c` | 271 | 0 | 271 |
| `vastbase-postgresql` | `tests/cases/vastbase_postgresql_dialect_input.json` | `tests/unit/test_vastbase_postgresql_dialect_case_matrix.c` | 213 | 0 | 213 |
| `vastbase-sqlserver` | `tests/cases/vastbase_sqlserver_dialect_input.json` | `tests/unit/test_vastbase_sqlserver_dialect_case_matrix.c` | 625 | 0 | 625 |

四套夹具均只包含 `final` 用例，独立 patch 数依次为 `vastbase-oracle` 832、`vastbase-mysql` 858、`vastbase-postgresql` 689、`vastbase-sqlserver` 1892。各入口验证 Query Graph 定界标识符的精确来源状态：relation 的 `database_quoted_identifier`、`schema_quoted_identifier`、object `quoted_identifier`、`alias_quoted_identifier`，以及 field `quoted_identifier`、target `output_quoted_identifier` 和 DML column `quoted_identifier`；`vastbase-oracle` 还验证 database link 的 `link_quoted_identifier` 与 `INSERT ALL/FIRST` 分支目标列，`vastbase-sqlserver` 还验证 `OUTPUT ... INTO` sink relation 和 sink column。各字段只对应自身来源 token；未定界来源的 C 标志为 `0`，View JSON 省略对应键。各入口仅识别既有定界符：Oracle/PostgreSQL 为双引号、MySQL 为反引号、SQL Server 为方括号。relation 或 DML column patch 后按新来源重新计算标志，patch handle 与重新解析 handle 的 View 必须一致。`U&"..."` 不在该合同范围内。以上是项目兼容入口合同及可执行证据，不声称 Vastbase 服务端官网定义了相同语法范围。

四个项目兼容入口以 28 条 final 用例和 50 个独立 patch 验证 relation-bearing DDL 投影。Query Graph 以 `kind = "ddl"` 的根块表达 DDL，并以 relation `ddl_role = "target"` / `"reference"` 区分操作对象与引用对象；定界标志仍按 database/schema/object 来源分段独立输出。CREATE VIEW、CTAS、CREATE MATERIALIZED VIEW 以及已验证入口的 `SELECT INTO` 将 DDL target 的 `source_block` 指向独立 SELECT 块；外键、PostgreSQL LIKE/INHERITS/partition 等引用对象按实际语法投影为 `reference`。PostgreSQL 兼容入口还验证 foreign-table target 生命周期；MySQL/PostgreSQL/SQL Server 兼容入口验证 quoted/unquoted 同名 DROP 分段的精确来源状态，SQL Server 兼容入口另验证普通多语句 batch 中的 DDL relation patch 保留公开 surface。仅夹具中已成功解析并完成 View/patch 对账的方言形态属于该合同；这是项目兼容入口证据，不作为 Vastbase 服务端官方语法声明。

四个项目兼容入口均只对各自 fixture 中合法的 CTE 显式列名形态验证按 ordinal 覆盖 source block 中可直接枚举的 target，并保留列名 token 的定界状态；Vastbase PostgreSQL 另验证短列表只覆盖 target 前缀。显式列名可参与 DML `source_target` lineage，重复 CTE 引用共享同一已覆盖 source block。相应入口按各自 fixture 分别验证 SET 结果、递归 SET 或 star 边界，不伪造结果 target、不跨分支覆盖，也不展开 star。该能力是项目 fixture 合同，不代表 Vastbase 服务端官方语法范围。

四个 Vastbase 项目兼容入口的 `MERGE ... WHEN NOT MATCHED THEN INSERT ... VALUES` 改写复用 `insert_column`：仅提供 `name` 时只增加目标列，仅提供一个 value source 时只增加 VALUES cell，同时提供时成对增加。省略目标列清单但存在 VALUES 时仍输出 `target_list_selector`，既可按需物化清单，也可在保持清单省略的情况下仅增加 cell；已有目标列和 cell 可分别替换。同一 patch batch 内允许两侧暂时不等长；批末存在显式目标列清单时必须与 VALUES 等长，否则整批原子回滚。成对删除语义不变，`MERGE INSERT DEFAULT VALUES` 不在该改写范围内。该合同及可执行证据不声称 Vastbase 服务端官网定义了相同语法范围。

`vastbase-sqlserver` 兼容模式包含 SQL Server DML `OUTPUT` 结果通道和 `IF...ELSE` 控制流能力。

`vastbase-mysql` 项目兼容入口合同支持 MySQL 多目标多表 `UPDATE` 的 JOIN 链与逗号 relation 列表，每个 assignment 目标字段分别关联其写入 relation；多表形态不接受 `ORDER BY` 或 `LIMIT`。该合同及可执行证据不声称 Vastbase 服务端官网定义了相同语法范围。

`vastbase-sqlserver` 兼容模式支持基础 `CONNECT BY` 条件。`START WITH`、`PRIOR`、`NOCYCLE` 和 `CONNECT_BY_ROOT` 不在该兼容入口的支持范围内。
在包含基础 `CONNECT BY` 的查询块中，无显式 `AS` 的 `CONNECT_BY_ROOT expr` 形态按边界外层次操作符拒绝；同名普通字段可使用显式 `AS` 别名或定界标识符。

作为 `vastbase-sqlserver` 项目兼容入口合同，成对 `insert_column` 只适用于 sink `OUTPUT ... INTO` 通道具有显式、非空 sink column list，且改写前 OUTPUT target 数与 sink column 数严格相等的场景；操作按同一序号原子插入两侧。原本合法的不等长 `OUTPUT` 仍可解析和反解析，但不支持该成对插入；client `OUTPUT` 和未显式列出 sink column 的 `OUTPUT ... INTO` 也不纳入该改写边界。3 条用例分别覆盖 INSERT、UPDATE、DELETE 的 8↔8 配对及头、中、尾原子插入后的 9↔9 配对。该合同及可执行证据不声称 Vastbase 服务端官网定义了相同语法范围。

作为 `vastbase-oracle` 项目兼容入口合同，`INSERT ... VALUES`、`UPDATE` 和 `DELETE` 的 `RETURNING ... INTO` 支持 `N >= 1` 个返回 target 与严格等长的 N 个冒号宿主 bind，并按 ordinal 配对；不接受 `BULK COLLECT`、非冒号 bind receiver 或数量不等的两侧列表。同一 `insert_column` patch 成对插入 target 和 receiver，不拆分单侧操作。该合同及可执行证据不声称 Vastbase 服务端官方支持同一语法范围。

## 官方资料

- [Vastbase G100 产品定位](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/aabafe2193fb4584b290dc9cdcc5c035)
- [Vastbase G100 V3.0 Build 8](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/5e3842f9085a4fd5b491f3203651ff7d)
- [PostgreSQL 兼容性](https://docs.vastdata.com.cn/zh_CN/VastbaseG100/V3.0.8/1/a9976158894e40398e9268181a597281)
- [MySQL 兼容性：反引号解释为标识符](https://docs.vastdata.com.cn/zh/docs/VastbaseG100Ver2.2.14/doc/%E5%85%BC%E5%AE%B9%E6%80%A7%E6%89%8B%E5%86%8C/MySQL%E5%85%BC%E5%AE%B9%E6%80%A7/%E5%8F%8D%E5%BC%95%E5%8F%B7%E8%A7%A3%E9%87%8A%E4%B8%BA%E6%A0%87%E8%AF%86%E7%AC%A6.html)
