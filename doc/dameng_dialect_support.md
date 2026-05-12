# 达梦方言支持

`SQLPARSER_DIALECT_DAMENG` 提供达梦 DM_SQL 到 `sqlparser` 当前 AST 模型的转换层。调用方需要通过 `sqlparser_parse_with_options()` 显式指定达梦方言；未指定方言时仍按 PostgreSQL 语法解析。

## 支持范围

达梦方言支持可安全映射到当前 AST 的常用 SQL 形态，覆盖范围由可执行用例矩阵定义：

- `SELECT`、别名、子查询、连接、`WHERE`、`GROUP BY`、`HAVING`
- 达梦兼容 bind 占位符，例如 `:id`、`:name`，以及 JDBC 风格 `?` 位置参数
- `SET SCHEMA <模式名>` 和 `ALTER SESSION SET CURRENT_SCHEMA = ...`
- `MINUS` 集合运算
- `LIMIT n`、`LIMIT offset,n`、`LIMIT n OFFSET offset`
- `SELECT TOP n ...`
- `ROWNUM` 条件
- `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`
- `UPDATE`、`DELETE`
- 可映射的 `MERGE`
- `DATE`、`TIMESTAMP` 字面量
- 常见 DDL：`CREATE TABLE`、`CREATE VIEW`、`CREATE SEQUENCE`、`ALTER TABLE ADD`、`CREATE INDEX`、`DROP TABLE`、`TRUNCATE TABLE`
- 事务控制、`GRANT / REVOKE`
- `FOR UPDATE NOWAIT`
- 常见函数与分析函数，例如 `NVL`、`ROW_NUMBER() OVER (...)`
- `EXEC SQL PREPARE`、`EXEC SQL EXECUTE`、`EXEC SQL DEALLOCATE PREPARE`

## 明确不支持范围

以下语法当前不做隐式降级。遇到这些语法时返回 `SQLPARSER_STATUS_UNSUPPORTED` 或解析错误，不会返回可用 handle：

- `CONNECT BY`
- `PIVOT`、`UNPIVOT`
- `RETURNING ... INTO`
- DMSQL block、procedure、package
- `TOP ... PERCENT`、`TOP ... WITH TIES`
- 多表插入，例如 `INSERT ALL`
- database link
- national q-quoted string，例如 `nq'[...]'`
- 除当前 schema 切换外的其他 `ALTER SESSION` 参数
- container 会话切换，例如 `ALTER SESSION SET CONTAINER = ...`

## 对外输出规则

- `sqlparser_deparse()` 输出达梦公共形态，不暴露内部转换细节。
- bind 保持 `:name`、`:1` 或 `?` 形态，不输出内部 `$1`、`$2`。
- `MINUS` 在 SQL View JSON 和 deparse 输出中保持达梦语义名称。
- `SET SCHEMA` 会以会话上下文结构输出，SQL View JSON 中字段名为 `CURRENT_SCHEMA`。
- SQL View JSON 中可归属的表达式片段使用达梦公共形态。
- 失败的表达式片段改写不会提交到 handle；原有 AST、bind 映射和 deparse 输出保持可用。

## 回归用例

达梦支持范围以以下文件为准：

- `tests/cases/dameng_dialect_input.json`
- `tests/cases/dameng_dialect_matrix.md`
- `tests/unit/test_dameng_dialect_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_stability.c`

当前达梦方言矩阵包含 55 条用例：43 条支持路径，12 条明确不支持路径。
