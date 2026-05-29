# Oracle 方言支持

`SQLPARSER_DIALECT_ORACLE` 提供 Oracle SQL 到 `sqlparser` 当前 AST 模型的转换层。调用方需要通过 `sqlparser_parse_with_options()` 显式指定 Oracle 方言；未指定方言时仍按 PostgreSQL 语法解析。

## 支持范围

Oracle 方言支持可安全映射到当前 AST 的常用 SQL 形态，覆盖范围由可执行用例矩阵定义：

- `SELECT`、别名、子查询、连接、`WHERE`、`GROUP BY`、`HAVING`
- Oracle bind 占位符，例如 `:id`、`:name`，以及 JDBC 风格 `?` 位置参数
- `q'[...]'` 字符串
- `MINUS` 集合运算
- `OFFSET ... FETCH`
- `ROWNUM` 过滤
- `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`，包括 `UNION`、`UNION ALL`、`INTERSECT`、`MINUS` 来源查询
- Oracle 多表插入：`INSERT ALL`、`INSERT FIRST`，包括 `WHEN ... THEN` 条件分支
- `UPDATE`、`DELETE`
- `DATE`、`TIMESTAMP` 字面量
- `CASE`、`EXISTS`、`UNION ALL`、`INTERSECT`
- 可映射的 `MERGE`
- 常见 DDL：`CREATE TABLE`、`CREATE SEQUENCE`、`CREATE VIEW`、`DROP TABLE`、`TRUNCATE TABLE`
- 事务控制、`GRANT / REVOKE`、`COMMENT ON`
- `FOR UPDATE NOWAIT`
- 常见函数与分析函数，例如 `DECODE`、`SYSDATE`、`ROW_NUMBER() OVER (...)`
- 引号标识符、`ALTER TABLE ADD`、`CREATE INDEX`、`DROP INDEX`
- 兼容形态的物化视图创建语句
- 会话语句：`ALTER SESSION SET CURRENT_SCHEMA = ...`、`ALTER SESSION SET CONTAINER = ...`、`ALTER SESSION SET CONTAINER = ... SERVICE = ...`，以及普通参数赋值，例如 `NLS_DATE_FORMAT`、`NLS_DATE_LANGUAGE`、`NLS_NUMERIC_CHARACTERS`、`INSTANCE`、`ERROR_ON_OVERLAP_TIME`
- `CURRENT_SCHEMA` 的带引号 schema 标识符会在公共 literal view 中标记为 quoted identifier
- `EXECUTE IMMEDIATE ... USING ...` 动态 SQL 执行语句

## 明确不支持范围

以下 Oracle 专属语义当前不做隐式降级。遇到这些语法时返回 `SQLPARSER_STATUS_UNSUPPORTED`，不会返回可用 handle：

- `CONNECT BY`、`CONNECT_BY_ROOT`
- 旧式外连接 `(+)`
- `RETURNING ... INTO`
- PL/SQL block、procedure、package
- `PIVOT`、`UNPIVOT`
- `MODEL` clause
- flashback query
- `MATCH_RECOGNIZE`
- synonym
- database link
- `EXPLAIN PLAN FOR`
- national q-quoted string，例如 `nq'[...]'`

## 对外输出规则

- `sqlparser_deparse()` 输出 Oracle 公共形态，不暴露内部转换细节。
- Oracle bind 保持 `:name`、`:1` 或 `?` 形态，不输出内部 `$1`、`$2`。
- `MINUS` 在 View JSON 和 deparse 输出中保持 Oracle 语义名称。
- View JSON 中可归属的表达式片段使用公共 Oracle 形态。
- 失败的表达式片段改写不会提交到 handle；原有 AST、bind 映射和 deparse 输出保持可用。

## 回归用例

Oracle 支持范围以以下文件为准：

- `tests/cases/oracle_dialect_input.json`
- `tests/cases/oracle_dialect_matrix.md`
- `tests/unit/test_oracle_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

当前 Oracle 方言矩阵包含 136 条用例：120 条支持路径，16 条明确不支持路径。
