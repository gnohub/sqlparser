# 方言覆盖统计

本文件汇总当前可执行回归矩阵中的方言覆盖情况。逐条用例来源见各 `tests/cases/*_input.json` 文件；官方语法覆盖统计见各方言的 `*_official_syntax_coverage.md` 文件。

## 汇总

| 方言 | 统计来源 | 成功用例 | 预期失败用例 | 用例总数 | 夹具成功率 |
| --- | --- | ---: | ---: | ---: | ---: |
| PostgreSQL | `tests/cases/sql_batch_input.json` | 214 | 0 | 214 | 100.00% |
| MySQL | `tests/cases/mysql_dialect_input.json` | 253 | 0 | 253 | 100.00% |
| Oracle | `tests/cases/oracle_dialect_input.json` | 244 | 0 | 244 | 100.00% |
| SQL Server | `tests/cases/sqlserver_dialect_input.json` | 621 | 0 | 621 | 100.00% |
| 达梦 | `tests/cases/dameng_dialect_input.json` | 170 | 0 | 170 | 100.00% |
| Vastbase PostgreSQL 兼容模式 | `tests/cases/vastbase_postgresql_dialect_input.json` | 199 | 0 | 199 | 100.00% |
| Vastbase MySQL 兼容模式 | `tests/cases/vastbase_mysql_dialect_input.json` | 254 | 0 | 254 | 100.00% |
| Vastbase Oracle 兼容模式 | `tests/cases/vastbase_oracle_dialect_input.json` | 217 | 0 | 217 | 100.00% |
| Vastbase SQL Server 兼容模式 | `tests/cases/vastbase_sqlserver_dialect_input.json` | 600 | 0 | 600 | 100.00% |

九个夹具合计 2772 条 final 用例和 8989 个独立 patch。

## 口径

- `成功用例` 表示输入成功生成 handle，并通过解析、View JSON、未修改 handle 的 deparse 原文逐字节一致性和适用的结构断言。
- `预期失败用例` 表示 fixture 明确期望 `SQLPARSER_STATUS_UNSUPPORTED`、解析错误或其他失败状态，不返回可用 handle。
- 预期失败用例包括非法 SQL，以及当前方言到 AST 的映射尚未表示其必要语义的用例。该夹具统计不代表官方语法覆盖率。

## 维护要求

- 新增或删除方言用例时同步更新本文件和 [dialect_coverage.csv](./dialect_coverage.csv)。
- 官方语法覆盖清单按方言维护：[PostgreSQL](./postgresql_official_syntax_coverage.md)、[MySQL](./mysql_official_syntax_coverage.md)、[Oracle](./oracle_official_syntax_coverage.md)、[SQL Server](./sqlserver_official_syntax_coverage.md)、[达梦](./dameng_official_syntax_coverage.md)、[Vastbase](./vastbase_official_syntax_coverage.md)。
