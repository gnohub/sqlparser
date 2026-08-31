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
- 多表单目标 `UPDATE`，支持 JOIN 链、逗号 relation 列表及混合形态；全部 SET assignment 必须指向同一个 table object
- DML 返回宿主绑定变量：`INSERT`、`DELETE` 的 `RETURNING <target, ...> INTO <:bind, ...>`，以及 `UPDATE` 的 `RETURN <target, ...> INTO <:bind, ...>`；每个列表均为 `N >= 1` 项，严格等长并按序号一一配对
- 可映射的 `MERGE`；matched UPDATE action 支持赋值后 `WHERE` 和归属同一 UPDATE 分支的 `DELETE WHERE`，not-matched INSERT 的 `insert_column` 支持 column-only、value-only 和 paired 三态，现有 VALUES cell 可独立替换
- `DATE`、`TIMESTAMP` 字面量
- 常见 DDL：`CREATE TABLE`、CTAS、`CREATE VIEW`、`CREATE MATERIALIZED VIEW`、`CREATE SEQUENCE`、`ALTER TABLE ADD/RENAME`、`CREATE INDEX`、`DROP TABLE`、`DROP MATERIALIZED VIEW`、`TRUNCATE TABLE`
- 事务控制、`GRANT / REVOKE`
- `FOR UPDATE NOWAIT`
- 远程对象引用，例如 `schema.table@link`
- 常见函数与分析函数，例如 `NVL`、`ROW_NUMBER() OVER (...)`
- `EXEC SQL PREPARE`、`EXEC SQL EXECUTE`、`EXEC SQL DEALLOCATE PREPARE`

## 明确不支持范围

以下语法当前不做隐式降级。遇到这些语法时返回 `SQLPARSER_STATUS_UNSUPPORTED` 或解析错误，不会返回可用 handle：

- `PIVOT`、`UNPIVOT`
- `RETURN`/`RETURNING ... INTO` 的 `BULK COLLECT`、非冒号 bind 接收项或 target/bind 不等长形态
- SET assignment 指向多个 table object 的多表 `UPDATE`
- DMSQL block、procedure、package
- 未列入支持范围的其他 `ALTER SESSION` 参数
- `ALTER SESSION SET CONTAINER = ...`

## 对外输出规则

- `sqlparser_deparse()` 输出达梦公共形态，不暴露内部转换细节。
- generation 为 `0` 或局部源码 edit 可用时可保留 `LIMIT offset,count`；完整 AST fallback 可以规范为语义等价的 `LIMIT count OFFSET offset`。
- bind 保持 `:name`、`:1` 或 `?` 形态，不输出内部 `$1`、`$2`。
- `MINUS` 在 View JSON 和 deparse 输出中保持达梦语义名称。
- 层次查询字段、值和谓词进入既有 Query Graph 数组，分别使用 `start_with`、`connect_by` clause、`pseudo` / `prior` 字段标记、CONNECT BY 根谓词的 `nocycle` 标记和 `CONNECT_BY_ROOT` operator `target_path`；不增加独立 hierarchy 对象。
- `SET SCHEMA` 在 View JSON 中输出字段名 `CURRENT_SCHEMA`。
- DML 返回通道在 `dml.result_channels` 中使用 sink channel；每个返回 target 的 `sink_value` 指向 `query_graph.values[]` 中同序号的宿主 bind。
- 多表 `UPDATE` 始终具有唯一 `dml.target_relation`；每个 assignment 的 `target_field` 关联该 relation。
- 省略 MERGE INSERT 目标列列表时仍输出 `target_list_selector`；column-only patch 可物化列列表，value-only patch 可在保持列表省略时追加 VALUES cell，显式列表继续支持 paired patch 同时追加两侧。patch batch 结束时若存在显式列表，则校验列值等长；失败时由核心 patch API 整批回滚。
- Query Graph 以 `alias_quoted_identifier` 标记双引号 relation alias，以 `output_quoted_identifier` 标记双引号显式 output alias 或无显式别名时继承的双引号字段名；View JSON 仅输出值为 `true` 的键。
- relation 限定名的定界状态按段输出：`database_quoted_identifier`、`schema_quoted_identifier`、既有的 object `quoted_identifier` 和 `link_quoted_identifier`；DML 目标列使用 `dml_column.quoted_identifier`，覆盖普通 INSERT、MERGE INSERT 及 `INSERT ALL/FIRST` 的每个分支。每个标志仅描述对应段，未定界或不存在的段不输出该键，不能由名称大小写推断；database-link target 同样保留 schema/object/link 的独立状态。
- relation DDL 输出 `kind = "ddl"` 根 block，并以 `ddl_role = "target"|"reference"` 区分操作目标和 FK 引用；VIEW、CTAS、物化视图 target 通过 `source_block` 指向 SELECT block。DROP target 没有 relation selector，新名称也不作为 RENAME 的第二个 relation。该合同仅由当前达梦入口 fixture 证明，不自动外推到兼容入口。
- View JSON 中可归属的表达式片段使用达梦公共形态。
- 失败的表达式片段改写不会提交到 handle；原有 AST、bind 映射和 deparse 输出保持可用。

- CTE 显式列名在来源 block 直接可枚举 targets 时按 ordinal 覆盖输出名与双引号状态；重复引用只覆盖一次，SET branch 保留底层输出。

## 回归用例

达梦支持范围以以下文件为准：

- `tests/cases/dameng_dialect_input.json`
- `tests/cases/dameng_dialect_matrix.md`
- `tests/unit/test_dameng_dialect_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_stability.c`

当前达梦方言矩阵包含 217 条用例，全部为 `status = "final"`，共包含 694 个独立 patch。其中 6 条用例覆盖多表单目标 `UPDATE`，3 条多返回项用例分别验证 INSERT `RETURNING`、UPDATE `RETURN` 和 DELETE `RETURNING` 的 8↔8 配对，以及头、中、尾原子插入后的 9↔9 配对。
