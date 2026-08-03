# SQL 用例矩阵

本文件记录 `tests/cases/sql_batch_input.json` 覆盖的回归用例。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。

## 矩阵统计与 session 回归

夹具包含 213 条 `status = "final"` 用例。32 条用例的期望 View 包含 statement 级 `query_graph.session`：5 条 schema/session 用例和 `PG-001` 至 `PG-027`；这 32 条用例均至少包含一个非空 session 投影。

View 校验采用 JSON 结构相等比较，对象键顺序和格式空白不参与比较；session 投影的 action、item scope、target kind、name 及 value 字段均属于比较范围。

## 可执行入口

- API 烟测：`tests/unit/test_api_smoke.c`
- API 矩阵测试：`tests/unit/test_api_case_matrix.c`
- CLI 批量夹具：`tests/cases/sql_batch_input.json`

## 已验证语句形态

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| P001 | `select-basic` | `SELECT 1` | parse、View JSON、deparse |
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
| P070 | `postgresql-view-direct-column` | `SELECT name FROM ...` | SELECT 直接输出列、`query_graph` target 和空 `target_path` |
| P071 | `postgresql-view-star-qualified-star` | `SELECT *, alias.* FROM ...` | 未限定星号、限定星号和输出项归属 |
| P072 | `postgresql-view-functions-and-args` | `SELECT function(column, ...) FROM ...` | 函数输出 `target_path`、函数名和参数序号 |
| P073 | `postgresql-view-expressions-and-case` | `SELECT expression, CASE ... FROM ...` | 表达式输出 `target_path`、操作符和 `CASE` 输出归属 |
| P074 | `postgresql-view-group-having-order` | `GROUP BY ... HAVING ... ORDER BY ...` | 非输出子句字段的 `query_graph` clause 和空 `target_path` |
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
| P092 | `postgresql-dollar-quoted-string-global-bind-position` | dollar-quoted 字符串 + `$n` 参数 | dollar-quoted 字符串内部占位符样式文本不参与 bind 全局计数 |
| P093 | `postgresql-multi-statement-global-bind-position` | 多语句 `$n` 参数 | 多语句输入中位置参数 `bind_position` 按整条 SQL 全局递增 |
| P094 | `postgresql-select-nested-derived-query-graph` | 嵌套派生表 + 输出别名 | 派生表字段向内层真实表字段的 `query_graph` 来源链路 映射和 `output_name` |
| P095 | `postgresql-select-reference-001` | SELECT 参考用例 001 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P096 | `postgresql-select-reference-004` | SELECT 参考用例 004 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P097 | `postgresql-select-reference-005` | SELECT 参考用例 005 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P098 | `postgresql-select-reference-007` | SELECT 参考用例 007 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P099 | `postgresql-select-reference-009` | SELECT 参考用例 009 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P100 | `postgresql-select-reference-011` | SELECT 参考用例 011 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P101 | `postgresql-select-reference-013` | SELECT 参考用例 013 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P102 | `postgresql-select-reference-015` | SELECT 参考用例 015 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P103 | `postgresql-select-reference-017` | SELECT 参考用例 017 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P104 | `postgresql-select-reference-018` | SELECT 参考用例 018 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P105 | `postgresql-select-reference-019` | SELECT 参考用例 019 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P106 | `postgresql-select-reference-020` | SELECT 参考用例 020 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P107 | `postgresql-select-reference-021` | SELECT 参考用例 021 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P108 | `postgresql-select-reference-030` | SELECT 参考用例 030 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P109 | `postgresql-select-reference-031` | SELECT 参考用例 031 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P110 | `postgresql-select-reference-032` | SELECT 参考用例 032 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P111 | `postgresql-select-reference-034` | SELECT 参考用例 034 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P112 | `postgresql-select-reference-035` | SELECT 参考用例 035 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P113 | `postgresql-select-reference-036` | SELECT 参考用例 036 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P114 | `postgresql-select-reference-037` | SELECT 参考用例 037 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P115 | `postgresql-select-reference-038` | SELECT 参考用例 038 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P116 | `postgresql-select-reference-039` | SELECT 参考用例 039 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P117 | `postgresql-select-reference-040` | SELECT 参考用例 040 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P118 | `postgresql-select-reference-041` | SELECT 参考用例 041 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P119 | `postgresql-select-reference-042` | SELECT 参考用例 042 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P120 | `postgresql-select-reference-043` | SELECT 参考用例 043 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P121 | `postgresql-select-reference-046` | SELECT 参考用例 046 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P122 | `postgresql-select-reference-047` | SELECT 参考用例 047 | 文档示例的标准 SELECT/子查询/JOIN/集合查询解析和 View JSON 结构 |
| P123 | `postgresql-select-nested-join-derived-query-graph` | 嵌套 JOIN 中的派生表 | 复杂 FROM/JOIN 下派生表对象枚举、`query_graph` 来源链路 和 `output_name` |
| P124 | `postgresql-select-unqualified-multi-table-scope` | 多表作用域中的未限定字段 | 未限定字段只输出一次，并归属到 `statement` 对象，避免多个表下出现相同 selector |
| P125 | `postgresql-select-union-derived-scope` | UNION 两侧派生表 + `SELECT *` | 派生表字段 occurrence 唯一输出，`query_graph` 来源链路 分别指向对应内层 `*` 来源 |
| P126 | `postgresql-field-match-kind-direct-and-expression` | 直接字段条件 + 函数包裹字段条件 | `query_graph.values[].field_match_kind` 区分 `direct_field` 和 `expression_field` |
| P127 | `postgresql-expression-field-case-expression-value` | CASE 返回字段再与参数比较 | CASE 表达式字段输出 `expression_field` value 关系 |
| P128 | `postgresql-expression-field-multi-field-expression-value` | 多字段表达式与参数比较 | 表达式内字段分别保留 `expression_field` value 关系 |
| P129 | `postgresql-expression-field-value-side-expression` | 字段与值侧表达式比较 | 值侧函数、运算、CAST 输出 `kind=expression`，不暴露 direct bind |
| P130 | `postgresql-expression-field-dml-expression-values` | INSERT/UPDATE 表达式赋值 | DML cell/assignment 输出 `kind=expression` |
| P131 | `postgresql-update-bind-rhs-crypto-source` | `UPDATE ... SET protected = $n` | UPDATE SET 右值为 bind 的保护字段来源表达，可用于后续结构化备份列插入和 literal 改写 |
| P132 | `postgresql-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = $n, protected2 = $n` | 多个保护字段的 SET bind、字段归属和全局 bind 序号 |
| P133 | `postgresql-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | 显式字面量 ESCAPE 输出到 `values[].like_escape` |
| P134 | `postgresql-not-like-escape-bind` | `NOT LIKE $1 ESCAPE $2` | pattern bind 与 escape bind 独立输出，escape bind 保留全局序号 |
| P135 | `postgresql-ilike-escape-bind` | `ILIKE $1 ESCAPE $2` | PostgreSQL `ILIKE` 的 ESCAPE 结构化输出 |
| P136 | `postgresql-like-without-explicit-escape` | `LIKE $1` | 无显式 ESCAPE 时不输出 `like_escape` |
| P138 | `postgresql-update-from-source-field-graph` | `UPDATE ... SET target = source.column FROM ...` | UPDATE FROM 赋值右侧真实表来源字段输出 `source_field`，WHERE 字段对比输出 `right_field` |
| P139 | `postgresql-insert-select-source-block-graph` | `INSERT ... SELECT ... FROM ...` | INSERT SELECT 的目标列、来源块和来源字段链路 |
| P140 | `postgresql-merge-source-target-graph` | `MERGE INTO ... USING ...` | MERGE 的 source/target 字段链路和来源字段表达 |
| P141 | `postgresql-listen-notify-unlisten` | `LISTEN` / `NOTIFY` / `UNLISTEN` | PostgreSQL 通知语句解析、反解析和空 query graph |
| P142 | `postgresql-create-drop-extension` | `CREATE EXTENSION` / `DROP EXTENSION` | 扩展对象 DDL 解析、反解析和 utility 语句输出 |
| P143 | `postgresql-regexp-like-function-predicate` | `regexp_like(name, $1)` | 函数谓词复用 `fields/values/predicates` 输出字段、bind 和 expression predicate |
| P144 | `postgresql-select-alias-order-by-lineage` | `SELECT u.email AS e ... ORDER BY u.email` | SELECT 输出 alias 保留 base field lineage，ORDER BY 字段独立归属 |
| P145 | `postgresql-select-or-predicate-order-by-lineage` | `WHERE field = $n OR field = $n ORDER BY ...` | OR 谓词树保留两个比较子节点、bind 和独立 ORDER BY 字段归属 |
| P146 | `postgresql-national-string-literal` | `SELECT ..., N'...' ... WHERE ... = n'...'` | PostgreSQL national 字符串公开输出保留 `N` 前缀，普通字符串不受影响 |
| P147 | `postgresql-national-string-duplicate-literal` | `'same'` 与 `N'same'` 同时出现 | 同文本普通字符串和 national 字符串按 literal 序号分别恢复 |
| P148 | `postgresql-merge-multiple-conditional-insert-branches` | 两个带条件的 `WHEN NOT MATCHED ... INSERT` | MERGE 分支顺序、条件原文、分支列/行坐标、字段/bind/表达式 cell 和全局 bind 序号 |
| P149 | `postgresql-merge-by-source-and-omitted-insert-columns` | BY TARGET INSERT、带条件的 MATCHED UPDATE、BY SOURCE DELETE | 三种 MERGE action/match、绝对分支序号、省略目标列列表的 INSERT 行及各分支内容 |
| P150 | `postgresql-direct-field-coalesce-expression-value` | 字段与 `COALESCE($1, 'x')` 比较 | 谓词通过单个 `direct_field` expression value 关联值侧表达式，内部 bind 与 literal 不提升 |
| P151 | `postgresql-direct-field-case-expression-value` | 字段与参数化 CASE 表达式比较 | CASE 整体作为单个 `direct_field` expression value 与谓词关联，内部 bind 与 literal 不提升 |
| P152 | `postgresql-direct-field-array-expression-value` | 字段与 `ARRAY[$1, $2]` 比较 | ARRAY 构造整体作为单个 `direct_field` expression value 与谓词关联，内部 bind 不提升 |
| P153 | `postgresql-having-or-count-expression-predicates` | `HAVING COUNT(*) > $1 OR COUNT(id) >= 2` | HAVING 保留 OR 子节点顺序；`COUNT(*)` 关联独立 bind 且不生成字段，`COUNT(id)` 关联表达式字段与 literal；3 个 patch 精确反解析 |
| P154 | `postgresql-not-in-subquery-membership` | `field NOT IN (SELECT ... WHERE ... = $1)` | 外层保留 `NOT` 布尔节点及 `IN` membership 子谓词，内层 block、字段、bind 和 predicate 独立归属；6 个 patch 覆盖两层字段、关系、target、插入 target 与 bind |
| P155 | `postgresql-having-function-null-test` | `HAVING SUM(amount) IS NOT NULL AND COUNT(*) > $1` | HAVING 保留 AND 子节点顺序；复合函数 NULL 测试输出 operator-only expression predicate，不任意绑定内部字段或生成 NULL value；COUNT bind 独立归属，4 个 patch 精确验证 |
| P156 | `postgresql-select-target-fragment-splice-first` | 三输出项 SELECT 的首项替换为两个双引号输出项 | 多输出项 replacement 在原位置展开，后续 target 顺序及 WHERE 字段、bind 归属保持不变；独立 insert patch 验证列表位置 |
| P157 | `postgresql-relation-patch-qualified-shadowed-correlation` | 无别名 schema-qualified 外层关系与内层同名 alias | View 将相关外层三段字段归属到外层关系；关系 patch 同步更新外层限定星号、直接字段和相关字段，内层 alias 及其字段保持不变 |
| P158 | `postgresql-relation-patch-two-part-window-qualifiers` | 同一关系字段覆盖 SELECT、窗口、GROUP BY、HAVING 和 ORDER BY | 单段关系替换为双引号两段路径时，所有已绑定限定符按原深度使用新路径尾段；字段及插入 target patch 独立验证 |
| P159 | `postgresql-data-modifying-cte-delete-multi-reference` | DELETE CTE 的 `RETURNING` 结果被外层 JOIN 重复引用 | 单个 DELETE DML 根、唯一结果块及两处 CTE relation 共享 `source_block`；2 个独立 patch 覆盖结果项替换与插入 |
| P160 | `postgresql-data-modifying-cte-update-delete-root` | UPDATE CTE + 顶层 DELETE | 顶层 DELETE 为 D0、UPDATE CTE 为 D1 子节点；UPDATE `RETURNING` 结果块供 DELETE `USING` relation 引用，2 个独立 patch 精确验证 D1 结果列表 |
| P161 | `postgresql-data-modifying-cte-two-deletes-side-effect` | 无 `RETURNING` 的 DELETE CTE + 有 `RETURNING` 的同级 DELETE CTE | 两个 SELECT-root DML 按声明顺序保持独立根；无结果的 D0 仍保留副作用语义，D1 单独提供结果块；2 个独立 patch 验证 D1 结果列表 |
| P162 | `postgresql-data-modifying-cte-sibling-lineage` | INSERT CTE 的 `RETURNING` 驱动同级 UPDATE CTE | D0 INSERT 与 D1 UPDATE 保持独立根和独立结果块，UPDATE 赋值通过 `source_field`/`source_target` 指向 INSERT 的 `payload`；3 个独立 patch 覆盖两个 DML ordinal 及结果项插入 |
| P163 | `postgresql-data-modifying-cte-merge-returning` | 带 UPDATE、INSERT 分支及 `RETURNING` 的 MERGE CTE | MERGE D0 的 target/source relation、ON 谓词、分支赋值与 INSERT 行、结果块、`RETURNING t.*` 的 `target_after` 来源及外层 CTE `source_block`；2 个独立 patch 覆盖结果项替换与插入 |
| P164 | `postgresql-data-modifying-cte-update-compound-rhs` | UPDATE CTE 中无别名关系参与的复合赋值右值 | assignment 通过 `rhs_fields` 和 `rhs_values` 归属来源字段、bind 与 literal；source/target relation patch 同步更新 RHS、WHERE、RETURNING 限定符，外层 target 插入保持独立 |
| P165 | `postgresql-on-conflict-compound-rhs` | `ON CONFLICT DO UPDATE` 中的复合赋值右值 | `EXCLUDED` 字段、目标表字段、bind 与 literal 均归属同一 assignment；目标关系 alias 在 relation replacement 后保持稳定，并精确验证 RETURNING target 插入 |

## INSERT VALUES 回归：bind 与表达式混合

`PG-BM001` 至 `PG-BM010` 是 PostgreSQL prepared statement、扩展查询协议或驱动模板语境下的 INSERT 输入；这里的 `$n` 表示外部提供的位置参数。这些位置参数必须通过支持的 prepare/bind 流程提供，不能直接通过 simple Query 协议执行。

每条用例逐个校验 VALUES 单元格的 `row`、`column`、`kind` 和 `selector`。直接 bind 单元格还校验 `bind_key`、`bind_kind`、`bind_sql`、`bind_position`；表达式内部的 bind 按当前公开契约不直接挂到单元格上，而由其后的直接 bind 全局 `bind_position` 验证已计入扫描顺序。时间函数名不得出现在 `query_graph.fields[].column` 中。

| 用例 ID | VALUES 形态 | 验证重点 |
| --- | --- | --- |
| `PG-BM001` | 三个直接 bind + 末尾 `CURRENT_TIMESTAMP` | 连续直接 bind 与尾部时间表达式 |
| `PG-BM002` | `now()` 在首位 + 三个直接 bind | 表达式位于首列 |
| `PG-BM003` | `$1`、`CAST($2 AS text)`、`$3`、`clock_timestamp()` | bind 与表达式交错，嵌套 bind 参与全局计数 |
| `PG-BM004` | 直接 bind、`NULL`、`now()`、直接 bind | literal 与时间表达式混合 |
| `PG-BM005` | 直接 bind、独立 `DEFAULT`、`CURRENT_TIMESTAMP`、直接 bind | `DEFAULT` 仅作为独立 VALUES 单元格 |
| `PG-BM006` | 直接 bind、字符串 literal、`clock_timestamp()`、直接 bind | literal、表达式与 bind 混合 |
| `PG-BM007` | `$1`、`COALESCE($2, 'fallback')`、`CURRENT_TIMESTAMP`、`$3` | COALESCE 内嵌 bind 与后续全局位置 |
| `PG-BM008` | `$1`、包含 `$2` 的 `CASE` 表达式、`now()`、`$3` | CASE 表达式内 bind 与后续全局位置 |
| `PG-BM009` | 三行 VALUES，bind 与表达式位置逐行变化 | 跨行 cell 坐标及连续全局 bind 位置 |
| `PG-BM010` | schema-qualified quoted identifiers、非常规空白、三个直接 bind + 时间表达式 | 引号标识符、原始空白及末尾表达式 |

## 方言 CLI 补充用例

| 用例 ID | 用例名称 | 输入 | 验证重点 |
| --- | --- | --- | --- |
| VCLI001 | `vastbase-oracle-cli-current-schema` | `ALTER SESSION SET CURRENT_SCHEMA=KDES` | `vastbase-oracle` CLI 方言名称、`ALTER SESSION` 结构化输出和反解析 |
| VCLI002 | `vastbase-mysql-cli-limit-binds` | ``SELECT `id` FROM `users` ORDER BY `id` LIMIT ?, ?`` | `vastbase-mysql` CLI 方言名称、反引号标识符和 comma LIMIT bind |
| VCLI003 | `vastbase-postgresql-cli-positional-binds` | `SELECT id FROM public.users WHERE id = $1` | `vastbase-postgresql` CLI 方言名称和 PostgreSQL positional bind |
| VCLI004 | `vastbase-sqlserver-cli-top-bind` | `SELECT TOP (5) [id] FROM [dbo].[users] WHERE [id] = @id` | `vastbase-sqlserver` CLI 方言名称、方括号标识符、`TOP` 和 named bind |

## 覆盖边界

本矩阵只列出可成功解析并具有最终 View 与 patch 期望的用例。解析失败路径由独立单元测试维护，不在该 fixture 中登记。

新增回归用例必须同步更新 `tests/cases/sql_batch_input.json` 和本矩阵。
