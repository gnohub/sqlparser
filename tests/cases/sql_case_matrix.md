# SQL 用例矩阵

本文件记录 `tests/cases/sql_batch_input.json` 覆盖的回归用例。JSON 夹具是可执行测试源，本文档用于说明当前已验证的语句形态和验证重点。

## 可执行入口

- API 烟测：`tests/unit/test_api_smoke.c`
- API 矩阵测试：`tests/unit/test_api_case_matrix.c`
- CLI 批量夹具：`tests/cases/sql_batch_input.json`

## 已验证语句形态

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| P001 | `select-basic` | `SELECT 1` | parse、SQL View JSON、deparse |
| P002 | `select-filter` | `SELECT ... FROM ... WHERE ...` | 查询列、过滤列、表名提取 |
| P003 | `select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | 多表 JOIN、查询列、关联列、条件列 |
| P004 | `select-cte` | `WITH ... SELECT ...` | CTE 名称、外层查询列、上游过滤列 |
| P005 | `insert-single-row` | `INSERT ... VALUES (...)` | 插入列、单行插入、deparse |
| P006 | `insert-multi-row` | `INSERT ... VALUES (...), (...)` | 多行插入、插入列、deparse |
| P007 | `insert-from-select` | `INSERT ... SELECT ... FROM ... WHERE ...` | 插入列、内层 SELECT、WHERE 提取 |
| P008 | `update-basic` | `UPDATE ... SET ... WHERE ...` | 更新列、条件列、表名提取 |
| P009 | `delete-conditional` | `DELETE ... WHERE ... AND ...` | 条件删除、多条件列提取 |
| P010 | `delete-in-list` | `DELETE ... WHERE ... IN (...)` | `IN` 条件、delete 谓词提取 |
| P011 | `drop-table` | `DROP TABLE ...` | DDL 分类、表名提取、deparse |
| P012 | `drop-view` | `DROP VIEW ...` | view DDL 分类、对象名提取 |
| P013 | `create-view` | `CREATE VIEW ... AS SELECT ...` | view 定义、内层 SELECT 提取 |
| P014 | `truncate-table` | `TRUNCATE TABLE ...` | truncate 节点识别、deparse |
| P015 | `comment-table` | `COMMENT ON TABLE ... IS ...` | comment 节点识别、deparse |
| P016 | `rename-table` | `ALTER TABLE ... RENAME TO ...` | rename 节点识别、对象名改写基础 |
| P017 | `alter-table-add-column` | `ALTER TABLE ... ADD COLUMN ...` | alter table 节点识别、列定义 deparse |
| P018 | `create-index` | `CREATE INDEX ... ON ... (...)` | index 节点识别、deparse |
| P019 | `drop-index` | `DROP INDEX ...` | drop index 节点识别、deparse |
| P020 | `explain-select` | `EXPLAIN SELECT ...` | explain 包裹查询解析、deparse |
| P021 | `copy-table` | `COPY ... FROM STDIN` | copy 节点识别、列名 deparse |
| P022 | `lock-table` | `LOCK TABLE ... IN ... MODE` | lock 节点识别、deparse |
| P023 | `call-procedure` | `CALL ...()` | call 节点识别、deparse |
| P024 | `do-block` | `DO $$ ... $$` | DO 代码块解析、deparse |
| P025 | `create-table-as` | `CREATE TABLE ... AS SELECT ...` | CTAS 节点识别、内层查询解析 |
| P026 | `transaction-begin-commit` | `BEGIN; COMMIT;` | 多语句事务计数、关键字提取 |
| P027 | `transaction-begin-insert-rollback` | `BEGIN; INSERT ...; ROLLBACK;` | 事务与 DML 混合解析 |
| P028 | `multi-statement-mixed` | `SELECT ...; INSERT ...` | 多语句计数、混合语句 deparse |
| P029 | `quoted-identifiers` | `SELECT "..."."..." FROM "..."` | 引号标识符保留、名称提取 |
| P030 | `literal-semicolon` | `SELECT ';' AS ...` | 字符串字面量中的分号处理 |
| P031 | `select-subquery-exists` | `SELECT ..., EXISTS (SELECT ...) FROM ...` | 子查询、`EXISTS`、多表提取 |
| P032 | `select-case-window` | `SELECT CASE ... OVER (...) FROM ...` | `CASE`、窗口函数、排序/分区列提取 |
| P033 | `select-union-order-limit` | `SELECT ... UNION ALL SELECT ... ORDER BY ... LIMIT ...` | `UNION ALL`、排序、limit deparse |
| P034 | `insert-on-conflict-update` | `INSERT ... ON CONFLICT ... DO UPDATE ... RETURNING ...` | 冲突处理、返回列、插入列提取 |
| P035 | `insert-returning` | `INSERT ... RETURNING ...` | returning 列、插入列提取 |
| P036 | `update-from-returning` | `UPDATE ... SET ... FROM ... WHERE ... RETURNING ...` | `UPDATE ... FROM`、返回列、条件列 |
| P037 | `delete-using-returning` | `DELETE ... USING ... WHERE ... RETURNING ...` | `DELETE ... USING`、返回列、多表提取 |
| P038 | `merge-basic` | `MERGE INTO ... USING ... WHEN ...` | merge 节点识别、关键字覆盖 |
| P039 | `savepoint-release` | `BEGIN; SAVEPOINT ...; RELEASE ...; COMMIT;` | savepoint 多语句事务解析 |
| P040 | `rollback-to-savepoint` | `BEGIN; SAVEPOINT ...; INSERT ...; ROLLBACK TO ...; COMMIT;` | savepoint 与 DML 混合解析 |
| P041 | `create-materialized-view` | `CREATE MATERIALIZED VIEW ... AS SELECT ...` | materialized view、内层 SELECT 提取 |
| P042 | `alter-table-drop-column` | `ALTER TABLE ... DROP COLUMN ...` | drop column 节点识别、deparse |
| P043 | `create-schema` | `CREATE SCHEMA ...` | schema DDL 分类、deparse |
| P044 | `drop-schema` | `DROP SCHEMA ...` | schema drop 分类、deparse |
| P045 | `grant-select` | `GRANT SELECT ON TABLE ... TO ...` | grant 节点识别、对象名提取 |
| P046 | `revoke-select` | `REVOKE SELECT ON TABLE ... FROM ...` | revoke 节点识别、对象名提取 |
| P047 | `analyze-table` | `ANALYZE ...` | analyze 节点识别、表名提取 |
| P048 | `vacuum-analyze-table` | `VACUUM ANALYZE ...` | vacuum/analyze 组合节点识别 |
| P049 | `postgresql-set-search-path` | `SET search_path TO ...` | 会话 schema 搜索路径输出和 value selector |
| P050 | `postgresql-set-schema` | `SET SCHEMA ...` | `SET SCHEMA` alias 反解析为 `search_path` |
| P051 | `postgresql-set-local-search-path` | `SET LOCAL search_path = ...` | 本地事务级 schema 搜索路径 |
| P052 | `postgresql-prepare-select` | `PREPARE ... AS SELECT ... $1` | PostgreSQL SQL 级 prepared statement、参数和查询对象提取 |
| P053 | `postgresql-execute-prepared` | `EXECUTE ...(...)` | PostgreSQL prepared statement 执行语句 |
| P054 | `postgresql-deallocate-prepare` | `DEALLOCATE PREPARE ...` | PostgreSQL prepared statement 释放语句 |
| P055 | `oracle-cli-dialect-q-quote` | Oracle `q'[...]'` | CLI `dialect` 字段和 Oracle q-quoted 字符串处理 |
| P056 | `sqlserver-cli-dialect-top-param` | SQL Server `TOP` + `@` 参数 | CLI `dialect` 字段和 SQL Server 方言输出处理 |
| P057 | `dameng-cli-dialect-set-schema-top` | 达梦 `SET SCHEMA` + `TOP` + bind | CLI `dialect` 字段和达梦方言输出处理 |
| P058 | `postgresql-select-dollar-params` | `SELECT ... WHERE ... = $1` | PostgreSQL `$n` 参数在查询条件中的解析、View 输出和反解析 |
| P059 | `postgresql-select-in-dollar-params` | `SELECT ... IN ($1, $2, $3)` | `IN` 条件中的多个 `$n` 参数 |
| P059A | `postgresql-select-between-dollar-params` | `BETWEEN $1 AND $2` | `BETWEEN` 条件中的多个 `$n` 参数和字段值关联 |
| P059B | `postgresql-select-not-in-dollar-params` | `NOT IN ($1, $2)` | 否定 `IN` 条件中的多个 `$n` 参数和字段值关联 |
| P059C | `postgresql-select-not-between-dollar-params` | `NOT BETWEEN $1 AND $2` | 否定 `BETWEEN` 条件中的多个 `$n` 参数和字段值关联 |
| P060 | `postgresql-select-limit-dollar-params` | `LIMIT $2 OFFSET $3` | 分页子句中的 `$n` 参数 |
| P061 | `postgresql-insert-dollar-params` | `INSERT ... VALUES ($1, $2, $3)` | 插入列和 `$n` 参数值列表 |
| P062 | `postgresql-insert-multi-row-dollar-params` | 多行 `INSERT ... VALUES` + `$n` | 多行参数化插入 |
| P063 | `postgresql-update-dollar-params` | `UPDATE ... SET ... WHERE ... = $n` | 更新列、条件列和 `$n` 参数 |
| P064 | `postgresql-delete-dollar-params` | `DELETE ... WHERE ... = $n` | 条件删除和 `$n` 参数 |
| P065 | `postgresql-prepare-insert` | `PREPARE ... AS INSERT ...` | prepared insert 语句和参数化值列表 |
| P066 | `postgresql-prepare-update` | `PREPARE ... AS UPDATE ...` | prepared update 语句和条件参数 |
| P067 | `postgresql-prepare-delete` | `PREPARE ... AS DELETE ...` | prepared delete 语句和条件参数 |
| P068 | `postgresql-execute-prepared-with-args` | `EXECUTE ...(...)` | prepared statement 执行参数 |
| P069 | `postgresql-deallocate-all` | `DEALLOCATE ALL` | 释放所有 prepared statements |
| P070 | `postgresql-view-direct-column` | `SELECT name FROM ...` | SELECT 直接输出列、`clause_id` 和空 `target_path` |
| P071 | `postgresql-view-star-qualified-star` | `SELECT *, alias.* FROM ...` | 未限定星号、限定星号和输出项归属 |
| P072 | `postgresql-view-functions-and-args` | `SELECT function(column, ...) FROM ...` | 函数输出 `target_path`、函数名和参数序号 |
| P073 | `postgresql-view-expressions-and-case` | `SELECT expression, CASE ... FROM ...` | 表达式输出 `target_path`、操作符和 `CASE` 输出归属 |
| P074 | `postgresql-view-group-having-order` | `GROUP BY ... HAVING ... ORDER BY ...` | 非输出子句字段的 `clause_id` 和空 `target_path` |
| P075 | `postgresql-view-distinct-nested-functions` | `SELECT DISTINCT LOW(UPPER(...)) FROM ...` | `DISTINCT` 关键字和从外到内的嵌套函数 `target_path` |
| P076 | `postgresql-view-join-on` | `JOIN ... ON ... WHERE ...` | JOIN/ON 字段、WHERE bind 和表字段归属 |
| P077 | `postgresql-view-window-array-row-tests` | 窗口、数组、ROW、布尔/NULL 表达式 | 窗口函数、复合表达式和只读子句的 `target_path` |
| P078 | `postgresql-view-bind-values` | `UPDATE ... SET ... WHERE ... = $n` | PostgreSQL bind 字段、空 value 和 update/where 子句归属 |
| P079 | `postgresql-view-not-like-bind` | `NOT LIKE $n` | 否定 LIKE 的字段级 operator、关键字和 bind 归属 |
| P080 | `postgresql-view-not-ilike-bind` | `NOT ILIKE $n` | 否定 ILIKE 的字段级 operator、关键字和 bind 归属 |
| P081 | `postgresql-view-not-similar-bind` | `NOT SIMILAR TO $n` | 否定 SIMILAR TO 的字段级 operator、关键字和 bind 归属 |
| P082 | `postgresql-create-table-if-not-exists-types` | `CREATE TABLE IF NOT EXISTS ...` | 条件建表、常见数据类型和表名提取 |
| P083 | `postgresql-insert-without-column-list` | `INSERT INTO ... VALUES ($1, $2, $3)` | 无列名插入、行 cell、位置 bind 和空列名输出 |
| P084 | `postgresql-update-in-not-in-conditions` | `UPDATE ... SET ... WHERE ... IN ... NOT IN ...` | SET bind、集合条件和否定集合条件 |
| P085 | `postgresql-select-rich-where` | `IS NOT NULL` + `BETWEEN` + `LIKE` | 复杂 WHERE 条件、范围参数和模式匹配参数 |
| P086 | `postgresql-select-derived-table-filter` | 派生表 + 外层过滤 | 派生表字段、内外层 WHERE 和 bind 归属 |
| P087 | `postgresql-select-scalar-subquery` | SELECT 标量子查询 | 投影子查询、相关字段和外层 WHERE bind |
| P088 | `postgresql-select-intersect` | `INTERSECT` | 集合操作、两侧表名和输出列 |
| P089 | `postgresql-create-view-join-aggregate` | JOIN 聚合视图 | 视图定义、JOIN 条件和 GROUP BY 聚合 |
| P090 | `postgresql-select-order-by-ordinal` | `ORDER BY 1` | 数字排序项和投影顺序相关语法 |
| P091 | `postgresql-select-quoted-mixed-identifiers` | 双引号混合大小写 / 空格标识符 | 特殊标识符、查询列和 WHERE bind |

## 负向用例

| 用例 ID | 用例名称 | 输入 | 验证重点 |
| --- | --- | --- | --- |
| P092 | `parse-error` | `SELECT FROM` | 结构化解析错误、错误码、错误消息 |

新增回归用例必须同步更新 `tests/cases/sql_batch_input.json` 和本矩阵。
