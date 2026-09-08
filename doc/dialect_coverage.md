# 方言覆盖统计

本文件汇总当前可执行回归矩阵中的方言覆盖情况。逐条用例来源见各 `tests/cases/*_input.json` 文件；官方依据见各方言的 `*_official_syntax_coverage.md` 文件及 KingbaseES 对应用例矩阵。

## 汇总

| 方言 | 统计来源 | 成功用例 | 预期失败用例 | 用例总数 | 夹具成功率 |
| --- | --- | ---: | ---: | ---: | ---: |
| PostgreSQL | `tests/cases/sql_batch_input.json` | 234 | 0 | 234 | 100.00% |
| MySQL | `tests/cases/mysql_dialect_input.json` | 276 | 0 | 276 | 100.00% |
| Oracle | `tests/cases/oracle_dialect_input.json` | 291 | 0 | 291 | 100.00% |
| SQL Server | `tests/cases/sqlserver_dialect_input.json` | 651 | 0 | 651 | 100.00% |
| 达梦 | `tests/cases/dameng_dialect_input.json` | 223 | 0 | 223 | 100.00% |
| Vastbase PostgreSQL 兼容模式 | `tests/cases/vastbase_postgresql_dialect_input.json` | 219 | 0 | 219 | 100.00% |
| Vastbase MySQL 兼容模式 | `tests/cases/vastbase_mysql_dialect_input.json` | 277 | 0 | 277 | 100.00% |
| Vastbase Oracle 兼容模式 | `tests/cases/vastbase_oracle_dialect_input.json` | 260 | 0 | 260 | 100.00% |
| Vastbase SQL Server 兼容模式 | `tests/cases/vastbase_sqlserver_dialect_input.json` | 631 | 0 | 631 | 100.00% |
| KingbaseES PostgreSQL 兼容入口 | `tests/cases/kingbase_postgresql_dialect_input.json` | 45 | 0 | 45 | 100.00% |
| KingbaseES MySQL 兼容入口 | `tests/cases/kingbase_mysql_dialect_input.json` | 42 | 0 | 42 | 100.00% |
| KingbaseES Oracle 兼容入口 | `tests/cases/kingbase_oracle_dialect_input.json` | 43 | 0 | 43 | 100.00% |
| KingbaseES SQL Server 兼容入口 | `tests/cases/kingbase_sqlserver_dialect_input.json` | 45 | 0 | 45 | 100.00% |

十三个夹具合计 3237 条 final 用例和 10249 个独立 patch。

## 口径

- `成功用例` 表示输入成功生成 handle，并通过解析、View JSON、未修改 handle 的 deparse 原文逐字节一致性和适用的结构断言。
- `预期失败用例` 表示 fixture 明确期望 `SQLPARSER_STATUS_UNSUPPORTED`、解析错误或其他失败状态，不返回可用 handle。
- `用例总数` 为成功与预期失败用例之和，`夹具成功率` 为成功用例占比；draft 用例不计入。
- 预期失败用例包括非法 SQL，以及当前方言到 AST 的映射尚未表示其必要语义的用例。该夹具统计不代表官方语法覆盖率。
- 基础五入口新增的 DDL Query Graph 合同只由各自 fixture 证明；不能由基础入口统计推断 Vastbase 或 KingbaseES 兼容入口具有相同语法范围或 patch 能力，各兼容入口仍以自身 fixture 为准。
- 十三个入口均只对各自 fixture 中合法的 CTE 显式列名形态验证 source block 可直接枚举 target 的 ordinal 覆盖；PostgreSQL、Vastbase-PostgreSQL 与 KingbaseES-PostgreSQL 另验证短列表只覆盖前缀。相应入口按各自 fixture 分别验证 SET 结果、递归 SET 或 star 边界，不伪造结果 target、不跨分支覆盖，也不展开 star。兼容入口统计仅证明项目 fixture 合同，不代表服务端官方语法范围。
- KingbaseES 每种兼容模式使用一个统一入口；V8/V9 语法基线在对应入口内合并，不接收或检测服务端版本，也不做版本分派。SQLServer 入口以现有 V9 官方兼容模式为基线。

## 维护要求

- 新增或删除方言用例时同步更新本文件和 [dialect_coverage.csv](./dialect_coverage.csv)。
- 官方语法覆盖清单按方言维护：[PostgreSQL](./postgresql_official_syntax_coverage.md)、[MySQL](./mysql_official_syntax_coverage.md)、[Oracle](./oracle_official_syntax_coverage.md)、[SQL Server](./sqlserver_official_syntax_coverage.md)、[达梦](./dameng_official_syntax_coverage.md)、[Vastbase](./vastbase_official_syntax_coverage.md)。
