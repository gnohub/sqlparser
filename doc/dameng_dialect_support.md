# 达梦方言支持

`SQLPARSER_DIALECT_DAMENG` 提供达梦 DM_SQL 到 `sqlparser` 当前 AST 模型的转换层。调用方需要通过 `sqlparser_parse_with_options()` 显式指定达梦方言；未指定方言时仍按 PostgreSQL 语法解析。

## 支持范围

达梦方言支持可安全映射到当前 AST 的常用 SQL 形态，覆盖范围由可执行用例矩阵定义：

- `SELECT`、别名、子查询、连接、`WHERE`、`GROUP BY`、`HAVING`
- 达梦层次查询：`START WITH ... CONNECT BY [NOCYCLE]`、`PRIOR`、`LEVEL` 和 SELECT 输出中的 `CONNECT_BY_ROOT`；保留 `START WITH ... CONNECT BY` 与 `CONNECT BY ... START WITH` 两种源文本子句顺序
- 达梦兼容 bind 占位符，例如 `:id`、`:name`，以及 JDBC 风格 `?` 位置参数
- `q'[...]'` 字符串、`N'...'` national 字符串和 `nq'[...]'` national q-quoted 字符串
- `SET SCHEMA <模式名>` 和 `ALTER SESSION SET CURRENT_SCHEMA = ...`
- 会话参数设置：`NLS_DATE_FORMAT`、`NLS_TIMESTAMP_FORMAT`、`NLS_TIMESTAMP_TZ_FORMAT`、`NLS_TIME_FORMAT`、`NLS_TIME_TZ_FORMAT`、`NLS_SORT`、`CASE_SENSITIVE`
- `CURRENT_SCHEMA` 的带引号 schema 标识符会在公共 literal view 中标记为 quoted identifier
- `MINUS` 集合运算
- `LIMIT n`、`LIMIT offset,n`、`LIMIT n OFFSET offset`
- `SELECT TOP n ...`、`SELECT TOP n,m ...`、`SELECT TOP n PERCENT ...`、`SELECT TOP n WITH TIES ...`、`SELECT TOP n PERCENT WITH TIES ...`
- `ROWNUM` 条件
- `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`
- 多表插入：`INSERT ALL`、`INSERT FIRST`，包括 `WHEN ... THEN`、`ELSE` 和单个条件分支下的多个 `INTO`
- `UPDATE`、`DELETE`
- DML 返回宿主绑定变量：`INSERT`、`DELETE` 的 `RETURNING <单个表达式> INTO <单个冒号宿主绑定变量>`，以及 `UPDATE` 的 `RETURN <单个表达式> INTO <单个冒号宿主绑定变量>`
- 可映射的 `MERGE`；matched UPDATE action 支持赋值后 `WHERE` 和归属同一 UPDATE 分支的 `DELETE WHERE`
- `DATE`、`TIMESTAMP` 字面量
- 常见 DDL：`CREATE TABLE`、`CREATE VIEW`、`CREATE SEQUENCE`、`ALTER TABLE ADD`、`CREATE INDEX`、`DROP TABLE`、`TRUNCATE TABLE`
- 事务控制、`GRANT / REVOKE`
- `FOR UPDATE NOWAIT`
- 远程对象引用，例如 `schema.table@link`
- 常见函数与分析函数，例如 `NVL`、`ROW_NUMBER() OVER (...)`
- `EXEC SQL PREPARE`、`EXEC SQL EXECUTE`、`EXEC SQL DEALLOCATE PREPARE`

## 明确不支持范围

以下语法当前不做隐式降级。遇到这些语法时返回 `SQLPARSER_STATUS_UNSUPPORTED` 或解析错误，不会返回可用 handle：

- `PIVOT`、`UNPIVOT`
- `RETURN`/`RETURNING ... INTO` 的多个返回 target、多个 `INTO` 宿主绑定变量或 `BULK COLLECT` 形态
- DMSQL block、procedure、package
- 未列入支持范围的其他 `ALTER SESSION` 参数
- `ALTER SESSION SET CONTAINER = ...`

## 对外输出规则

- `sqlparser_deparse()` 输出达梦公共形态，不暴露内部转换细节。
- bind 保持 `:name`、`:1` 或 `?` 形态，不输出内部 `$1`、`$2`。
- `MINUS` 在 View JSON 和 deparse 输出中保持达梦语义名称。
- 层次查询字段、值和谓词进入既有 Query Graph 数组，分别使用 `start_with`、`connect_by` clause、`pseudo` / `prior` 字段标记、CONNECT BY 根谓词的 `nocycle` 标记和 `CONNECT_BY_ROOT` operator `target_path`；不增加独立 hierarchy 对象。
- `SET SCHEMA` 在 View JSON 中输出字段名 `CURRENT_SCHEMA`。
- DML 返回通道在 `dml.result_channels` 中使用 sink channel；返回 target 的 `sink_value` 指向 `query_graph.values[]` 中的宿主绑定变量。
- View JSON 中可归属的表达式片段使用达梦公共形态。
- 失败的表达式片段改写不会提交到 handle；原有 AST、bind 映射和 deparse 输出保持可用。

## 回归用例

达梦支持范围以以下文件为准：

- `tests/cases/dameng_dialect_input.json`
- `tests/cases/dameng_dialect_matrix.md`
- `tests/unit/test_dameng_dialect_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_stability.c`

当前达梦方言矩阵包含 174 条用例，全部为 `status = "final"`。其中 4 条层次查询用例包含 20 个独立 patch。
