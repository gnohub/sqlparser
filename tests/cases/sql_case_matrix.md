# SQL 用例矩阵

本文件记录 `tests/cases/sql_batch_input.json` 覆盖的回归用例。JSON 夹具是可执行测试源，本文档用于说明当前已验证的语句形态和验证重点。

## 可执行入口

- API 烟测：`tests/unit/test_api_smoke.c`
- API 矩阵测试：`tests/unit/test_api_case_matrix.c`
- CLI 批量夹具：`tests/cases/sql_batch_input.json`

## 已验证语句形态

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| P001 | `select-basic` | `SELECT 1` | parse、parse-tree JSON、summary JSON、deparse |
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

## 负向用例

| 用例 ID | 用例名称 | 输入 | 验证重点 |
| --- | --- | --- | --- |
| P049 | `parse-error` | `SELECT FROM` | 结构化解析错误、错误码、错误消息 |

新增回归用例必须同步更新 `tests/cases/sql_batch_input.json` 和本矩阵。
