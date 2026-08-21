# Oracle 方言支持

`SQLPARSER_DIALECT_ORACLE` 提供 Oracle SQL 到 `sqlparser` 当前 AST 模型的转换层。调用方需要通过 `sqlparser_parse_with_options()` 显式指定 Oracle 方言；未指定方言时仍按 PostgreSQL 语法解析。

## 支持范围

Oracle 方言支持可安全映射到当前 AST 的常用 SQL 形态，覆盖范围由可执行用例矩阵定义：

- `SELECT`、别名、子查询、连接、`WHERE`、`GROUP BY`、`HAVING`
- Oracle 层次查询：`START WITH ... CONNECT BY [NOCYCLE]`、`PRIOR`、`LEVEL`、`CONNECT_BY_ISLEAF`、`CONNECT_BY_ISCYCLE`，以及 SELECT 输出中的 `CONNECT_BY_ROOT`；子句顺序限定为 `START WITH` 在 `CONNECT BY` 之前，不接受 `CONNECT BY ... START WITH`
- Oracle bind 占位符，例如 `:id`、`:name`，以及 JDBC 风格 `?` 位置参数
- `q'[...]'` 字符串、`N'...'` national 字符串和 `nq'[...]'` national q-quoted 字符串
- `MINUS` 集合运算
- `OFFSET ... ROWS` 以及行数型 `FETCH FIRST|NEXT ... ROWS ONLY`；本边界不包含 `FETCH ... PERCENT`
- `ROWNUM` 过滤
- `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`，包括 `UNION`、`UNION ALL`、`INTERSECT`、`MINUS` 来源查询
- Oracle 多表插入：`INSERT ALL`、`INSERT FIRST`，包括 `WHEN ... THEN` 条件分支
- `UPDATE`、`DELETE`
- `INSERT`、`UPDATE`、`DELETE` 的 `RETURNING ... INTO`：支持 `N >= 1` 个返回 target 与严格等长的 N 个冒号宿主绑定变量，并按 ordinal 配对
- `DATE`、`TIMESTAMP` 字面量
- `CASE`、`EXISTS`、`UNION ALL`、`INTERSECT`
- 可映射的 `MERGE`；matched UPDATE action 支持赋值后 `WHERE` 和归属同一 UPDATE 分支的 `DELETE WHERE`，not-matched INSERT 支持分支条件以及 column-only、value-only、paired 三态 `insert_column`，现有 VALUES cell 可独立替换
- 常见 DDL：`CREATE TABLE`、`CREATE SEQUENCE`、`CREATE VIEW`、`DROP TABLE`、`TRUNCATE TABLE`
- 事务控制、`GRANT / REVOKE`、`COMMENT ON`
- `FOR UPDATE NOWAIT`
- 远程对象引用，例如 `schema.table@link`
- 常见函数与分析函数，例如 `DECODE`、`SYSDATE`、`ROW_NUMBER() OVER (...)`
- 引号标识符、`ALTER TABLE ADD`、`CREATE INDEX`、`DROP INDEX`
- 兼容形态的物化视图创建语句
- `CREATE SYNONYM`、`DROP SYNONYM`
- 会话语句：`ALTER SESSION SET CURRENT_SCHEMA = ...`、`ALTER SESSION SET CONTAINER = ...`、`ALTER SESSION SET CONTAINER = ... SERVICE = ...`，以及普通参数赋值，例如 `NLS_DATE_FORMAT`、`NLS_DATE_LANGUAGE`、`NLS_NUMERIC_CHARACTERS`、`INSTANCE`、`ERROR_ON_OVERLAP_TIME`
- `CURRENT_SCHEMA` 的带引号 schema 标识符会在公共 literal view 中标记为 quoted identifier
- `EXPLAIN PLAN FOR ...`，包括 `SET STATEMENT_ID` 和 `INTO` 基础形态
- `EXECUTE IMMEDIATE ... USING ...` 动态 SQL 执行语句

## 明确不支持范围

以下 Oracle 专属语义当前不做隐式降级。遇到这些语法时返回 `SQLPARSER_STATUS_UNSUPPORTED`，不会返回可用 handle：

- 旧式外连接 `(+)`
- `RETURNING ... INTO` 的 `BULK COLLECT`、非冒号 bind receiver，以及 target/receiver 数量不等的形态
- PL/SQL block、procedure、package
- `PIVOT`、`UNPIVOT`
- `MODEL` clause
- flashback query
- `MATCH_RECOGNIZE`

## 对外输出规则

- `sqlparser_deparse()` 输出 Oracle 公共形态，不暴露内部转换细节。
- 完整 AST 反解析继续输出 Oracle `OFFSET ... FETCH` 分页，不会降级为 `LIMIT`；局部源码 edit 可用时仍保留未修改分页文本。
- Oracle bind 保持 `:name`、`:1` 或 `?` 形态，不输出内部 `$1`、`$2`。
- `MINUS` 在 View JSON 和 deparse 输出中保持 Oracle 语义名称。
- 层次查询字段、值和谓词进入既有 Query Graph 数组，分别使用 `start_with`、`connect_by` clause、`pseudo` / `prior` 字段标记、CONNECT BY 根谓词的 `nocycle` 标记和 `CONNECT_BY_ROOT` operator `target_path`；不增加独立 hierarchy 对象。
- `RETURNING ... INTO` 在 View 中使用一个 `kind = "sink"` 通道，每个 target 的 `sink_value` 指向对应 ordinal 的输出 bind；`insert_column` 使用同一个 patch 原子地插入 target/receiver 对，不支持拆分为单侧插入。
- 省略 MERGE INSERT 目标列列表时仍输出 `target_list_selector`；column-only patch 可物化列列表，value-only patch 可在保持列表省略时追加 VALUES cell，显式列表继续支持 paired patch 同时追加两侧。patch batch 结束时若存在显式列表，则校验列值等长；失败时由核心 patch API 整批回滚。
- Query Graph 以 `alias_quoted_identifier` 标记双引号 relation alias，以 `output_quoted_identifier` 标记双引号显式 output alias 或无显式别名时继承的双引号字段名；View JSON 仅输出值为 `true` 的键。
- View JSON 中可归属的表达式片段使用公共 Oracle 形态。
- 失败的表达式片段改写不会提交到 handle；原有 AST、bind 映射和 deparse 输出保持可用。

## 回归用例

Oracle 支持范围以以下文件为准：

- `tests/cases/oracle_dialect_input.json`
- `tests/cases/oracle_dialect_matrix.md`
- `tests/unit/test_oracle_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

当前 Oracle 方言矩阵包含 253 条用例和 857 个独立 patch，均为 `status = "final"`。其中 4 条层次查询用例包含 20 个独立 patch；O198 至 O200 分别验证 `INSERT`、`UPDATE`、`DELETE` 的 8 对 `RETURNING ... INTO` 结果及头部、中部、尾部成对插入。
