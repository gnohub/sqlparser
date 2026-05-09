# 方言覆盖统计

本文件汇总当前可执行回归矩阵中的方言覆盖情况。逐条用例来源见各 `tests/cases/*_input.json` 文件；官方语法覆盖统计见各方言的 `*_official_syntax_coverage.md` 文件。

## 汇总

| 方言 | 统计来源 | 支持用例 | 明确不支持用例 | 合计 | 支持占比 |
| --- | --- | ---: | ---: | ---: | ---: |
| PostgreSQL | `tests/cases/sql_batch_input.json` | 53 | 1 | 54 | 98.15% |
| MySQL | `tests/cases/mysql_dialect_input.json` | 17 | 15 | 32 | 53.13% |
| Oracle | `tests/cases/oracle_dialect_input.json` | 46 | 19 | 65 | 70.77% |
| SQL Server | `tests/cases/sqlserver_dialect_input.json` | 46 | 15 | 61 | 75.41% |

## 口径

- `支持用例` 表示当前方言已经通过解析、SQL View JSON、deparse 或错误路径的可执行回归验证。
- `明确不支持用例` 表示当前实现主动返回 `SQLPARSER_STATUS_UNSUPPORTED` 或解析错误，不返回可用 handle。
- PostgreSQL 默认方言的负向用例为故意构造的非法 SQL，不计为功能缺口。
- MySQL、Oracle、SQL Server 的明确不支持用例主要来自数据库专属语义，无法在不扩展共享 AST 的前提下安全表达。

## 维护要求

- 新增或删除方言用例时同步更新本文件和 [dialect_coverage.csv](./dialect_coverage.csv)。
- 官方语法覆盖清单按方言维护：[PostgreSQL](./postgresql_official_syntax_coverage.md)、[MySQL](./mysql_official_syntax_coverage.md)、[Oracle](./oracle_official_syntax_coverage.md)、[SQL Server](./sqlserver_official_syntax_coverage.md)。
