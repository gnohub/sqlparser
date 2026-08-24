# MySQL 方言用例矩阵

本文件记录 MySQL 方言转换层的回归用例。可执行夹具为 `tests/cases/mysql_dialect_input.json`。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。用例提供 `bind_occurrences` 时，runner 还会对原 SQL 及每个 patch 后 SQL 的 `position`、`kind`、`key` 和原始 `sql` 逐项精确校验，重复 key 不合并，`position` 按整段 SQL 连续编号。

## 矩阵统计与 session 回归

夹具包含 263 条 `status = "final"` 用例和 886 个独立 patch；其中 3 条用例及其 11 个 patch 含完整 bind occurrence 断言。37 条用例的期望 View 包含 statement 级 `query_graph.session`，覆盖 `M015` 至 `M017`、`MY-001` 至 `MY-029` 以及 5 条注释或空语句穿插的 `USE` 边界；这 37 条用例均至少包含一个非空 session 投影。

View 校验采用 JSON 结构相等比较，对象键顺序和格式空白不参与比较；session 投影的 action、item scope、target kind、name 及 value 字段均属于比较范围。

## 已验证支持语句

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| M001 | `mysql-select-limit-comma` | `SELECT ... FROM ... WHERE ... LIMIT offset,count` | 反引号标识符、双引号字符串、表名、查询列、WHERE literal、MySQL comma limit deparse |
| M002 | `mysql-select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | 多表 JOIN、查询列、关联列、条件列 |
| M003 | `mysql-hash-comment` | `SELECT ... # comment` | MySQL `#` 行注释预处理 |
| M004 | `mysql-insert-values-multi-row` | `INSERT ... VALUES (...), (...)` | 多行插入、插入列；对未修改的 handle 执行 deparse 时，结果与输入逐字节一致，包括原始双引号字符串 |
| M004N | `mysql-national-string-literal` | `SELECT "..." ... N'...' ... n'...'` | AST 保留 national 字符串语义；对未修改的 handle 执行 deparse 时，结果逐字节保留输入中的 `N`/`n` 拼写 |
| M004NA | `mysql-national-string-duplicate-literal` | 普通字符串和 `N'...'` 同文本 | 只为原始 national 字符串恢复 `N` 前缀 |
| M005 | `mysql-insert-select` | `INSERT ... SELECT ... FROM ... WHERE ...` | 插入列、内层查询列、WHERE 条件列 |
| M006 | `mysql-update-basic` | `UPDATE ... SET ... WHERE ...` | 更新列、条件列、反引号标识符 |
| M007 | `mysql-delete-conditional` | `DELETE FROM ... WHERE ... AND ...` | 条件删除、多条件列提取 |
| M008 | `mysql-create-table-basic` | `CREATE TABLE ... (...)` | 基础建表语句、列定义解析 |
| M009 | `mysql-alter-table-add-column` | `ALTER TABLE ... ADD COLUMN ...` | alter table 解析、列定义 deparse |
| M010 | `mysql-create-view` | `CREATE VIEW ... AS SELECT ...` | view 定义、内层 SELECT 提取 |
| M011 | `mysql-drop-table` | `DROP TABLE ...` | drop table 解析、表名提取 |
| M012 | `mysql-start-transaction` | `START TRANSACTION; COMMIT` | MySQL 事务起始语句、多语句计数 |
| M013 | `mysql-unsupported-keywords-in-string` | `SELECT 'INSERT IGNORE' ...` | unsupported 预筛选不会误伤字符串内容 |
| M014 | `mysql-unsupported-keywords-in-comment` | `SELECT ... /* ON DUPLICATE KEY UPDATE */ ...` | unsupported 预筛选不会误伤注释内容 |
| M014Q | `mysql-unsupported-keywords-in-quoted-identifiers` | ``SELECT `unsigned`, `auto_increment`, `engine` ...`` | unsupported 预筛选不会误伤受保护标识符 |
| M015 | `mysql-use-database` | `USE analytics` | 默认数据库切换语句、View JSON value selector |
| M016 | `mysql-use-quoted-database` | `USE \`analytics-prod\`` | 反引号数据库名和公开 value 片段 |
| M017 | `mysql-use-database-in-multi-statement` | `USE ...; SELECT ...` | 多语句中的数据库切换和后续查询保持独立输出 |
| M018 | `mysql-insert-question-params` | `INSERT ... VALUES (?, ?, ?)` | JDBC 风格位置参数转换、插入列识别和公开形态还原 |
| M019 | `mysql-update-question-params` | `UPDATE ... SET ... WHERE ... = ?` | SET/WHERE 中的位置参数转换和公开形态还原 |
| M020 | `mysql-prepare-from-literal` | `PREPARE stmt FROM 'SELECT ... ?'` | MySQL SQL 级 prepared statement、`?` 占位符和公开 SQL 还原 |
| M021 | `mysql-execute-using` | `EXECUTE stmt USING @var` | prepared statement 执行和用户变量参数 |
| M022 | `mysql-deallocate-prepare` | `DEALLOCATE PREPARE stmt` | prepared statement 释放语句 |
| M023 | `mysql-drop-prepare` | `DROP PREPARE stmt` | MySQL `DROP PREPARE` alias 释放语句 |
| M024 | `mysql-select-question-params` | `SELECT ... WHERE ... = ?` | 查询条件中的 JDBC 风格位置参数 |
| M025 | `mysql-select-in-question-params` | `SELECT ... IN (?, ?, ?)` | `IN` 条件中的多个位置参数 |
| M026 | `mysql-select-limit-question-params` | `LIMIT ? OFFSET ?` | 分页子句中的位置参数 |
| M027 | `mysql-insert-named-columns-question-params` | `INSERT ... VALUES (?, ?, ?)` | 插入列和位置参数值列表 |
| M028 | `mysql-insert-multi-row-question-params` | 多行 `INSERT ... VALUES` + `?` | 多行参数化插入 |
| M029 | `mysql-update-multi-question-params` | `UPDATE ... SET ... WHERE ... = ?` | 更新列、条件列和位置参数 |
| M030 | `mysql-delete-question-params` | `DELETE ... WHERE ... = ?` | 条件删除和位置参数 |
| M031 | `mysql-prepare-insert-literal` | `PREPARE stmt FROM 'INSERT ... ?'` | prepared insert SQL 文本和 `?` 占位符 |
| M032 | `mysql-prepare-from-user-variable` | `PREPARE stmt FROM @var` | 用户变量来源的 prepared SQL 文本 |
| M033 | `mysql-execute-using-multiple-vars` | `EXECUTE stmt USING @id, @name` | 多个用户变量绑定参数 |
| M034 | `mysql-view-concat-function` | `SELECT CONCAT(UPPER(...), ...) ...` | 函数 `target_path`、嵌套函数、参数序号和 WHERE bind |
| M035 | `mysql-view-case-expression` | `SELECT CASE WHEN ... THEN ... END ...` | `CASE` 表达式中的输出字段归属 |
| M035A | `mysql-view-case-predicate-bind` | `CASE WHEN column = ? THEN ...` | SELECT 投影内条件表达式的字段级 bind 归属 |
| M036 | `mysql-view-group-having-order` | `GROUP BY ... HAVING ... ORDER BY ...` | 聚合输出和非输出子句字段归属 |
| M037 | `mysql-view-update-question-binds` | `UPDATE ... SET ... WHERE ... = ?` | 位置参数 bind、空 value、update/where 子句归属 |
| M038 | `mysql-view-join-on` | `JOIN ... ON ... WHERE ... = ?` | JOIN/ON 字段、WHERE bind 和表字段归属 |
| M039 | `mysql-select-between-question-params` | `BETWEEN ? AND ?` | `BETWEEN` 条件中的多个位置参数和字段值关联 |
| M040 | `mysql-select-not-in-question-params` | `NOT IN (?, ?)` | 否定 `IN` 条件中的多个位置参数和字段值关联 |
| M041 | `mysql-select-not-between-question-params` | `NOT BETWEEN ? AND ?` | 否定 `BETWEEN` 条件中的多个位置参数和字段值关联 |
| M042 | `mysql-select-not-like-question-param` | `NOT LIKE ?` | 否定 `LIKE` 条件中的位置参数、字段级 operator 和关键字归属 |
| M043 | `mysql-select-distinct-like-param` | `SELECT DISTINCT ... WHERE ... LIKE ?` | DISTINCT 投影、LIKE 位置参数和字段归属 |
| M044 | `mysql-select-left-join-alias-star` | `LEFT JOIN` + `alias.*` | 限定星号、JOIN/ON 字段和 WHERE bind |
| M045 | `mysql-delete-in-question-params` | `DELETE ... WHERE ... IN (?, ?)` | 条件删除、集合参数和字段 operator |
| M046 | `mysql-update-in-question-params` | `UPDATE ... SET ? WHERE ... IN (?, ?)` | SET bind、WHERE 集合条件和参数序号 |
| M047 | `mysql-select-derived-table-filter` | 派生表 + 外层过滤 | 内外层 WHERE、派生表 alias 和 bind 归属 |
| M048 | `mysql-select-json-extract` | `JSON_EXTRACT(...)` | 方言函数投影和 WHERE bind |
| M049 | `mysql-create-table-if-not-exists` | `CREATE TABLE IF NOT EXISTS ...` | 条件建表和常见列类型 |
| M050 | `mysql-drop-view-if-exists` | `DROP VIEW IF EXISTS ...` | 视图删除和对象名提取 |
| M051 | `mysql-select-order-by-ordinal` | `ORDER BY 1` | 数字排序项和投影顺序相关语法 |
| M052 | `mysql-limit-comma-question-params` | `LIMIT ?, ?` | MySQL 逗号分页中的位置参数，公开 SQL 保持逗号分页形态 |
| M053 | `mysql-multi-statement-global-bind-position` | versioned executable `UPDATE` + 普通 `UPDATE` | 全语句 executable comment 中的 `?` 计入，普通和行内 versioned comment 中的伪 `?` 排除；复杂 patch 覆盖子查询、CAST、CASE、`LIMIT/OFFSET`、保护区及删除/插入后连续重编号 |
| M054 | `mysql-select-derived-query-graph` | 派生表 + 输出别名 + `?` 参数 | 派生表字段向内层真实表字段的 `query_graph` 来源链路 映射和 `output_name` |
| M055 | `mysql-select-reference-002` | SELECT 参考用例 002 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M056 | `mysql-select-reference-003` | SELECT 参考用例 003 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M057 | `mysql-select-reference-006` | SELECT 参考用例 006 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M058 | `mysql-select-reference-008` | SELECT 参考用例 008 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M059 | `mysql-select-reference-010` | SELECT 参考用例 010 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M060 | `mysql-select-reference-012` | SELECT 参考用例 012 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M061 | `mysql-select-reference-014` | SELECT 参考用例 014 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M062 | `mysql-select-reference-016` | SELECT 参考用例 016 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M063 | `mysql-select-reference-022` | SELECT 参考用例 022 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M064 | `mysql-select-reference-023` | SELECT 参考用例 023 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M065 | `mysql-select-reference-025` | SELECT 参考用例 025 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M066 | `mysql-select-reference-027` | SELECT 参考用例 027 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M067 | `mysql-select-reference-029` | SELECT 参考用例 029 | MySQL 合法 SELECT 示例解析和 View JSON 结构 |
| M068 | `mysql-update-join-target-table-qualified` | `UPDATE users JOIN ... SET users.phone = ?` | 未使用别名时，目标表限定赋值仍映射到目标表字段 |
| MU001 | `mysql-insert-ignore` | `INSERT IGNORE ...` | 保留 `IGNORE` 修饰符，内部复用普通 INSERT AST |
| MU002 | `mysql-insert-delayed` | `INSERT DELAYED ...` | 保留 `DELAYED` 修饰符，MySQL 8.4 执行时识别并忽略该关键字 |
| MU003 | `mysql-insert-low-priority` | `INSERT LOW_PRIORITY ...` | 保留 `LOW_PRIORITY` 修饰符，插入列和值结构复用普通 INSERT |
| MU004 | `mysql-insert-high-priority` | `INSERT HIGH_PRIORITY ...` | 保留 `HIGH_PRIORITY` 修饰符，插入列和值结构复用普通 INSERT |
| MU004A | `mysql-insert-low-priority-ignore` | `INSERT LOW_PRIORITY IGNORE ... VALUES (?, ?)` | 组合修饰符和位置参数全局序号 |
| MU004B | `mysql-insert-high-priority-ignore-select` | `INSERT HIGH_PRIORITY IGNORE ... SELECT ...` | 组合修饰符和 INSERT SELECT 来源图 |
| MU004C | `mysql-insert-ignore-on-duplicate-key` | `INSERT IGNORE ... ON DUPLICATE KEY UPDATE ...` | `IGNORE` 与 upsert 赋值、bind 序号组合 |
| MU005 | `mysql-on-duplicate-key` | `INSERT ... ON DUPLICATE KEY UPDATE ...` | MySQL upsert 映射到 DML 插入值和更新赋值 |
| MU005A | `mysql-on-duplicate-key-assignment-patch` | 双赋值 `ON DUPLICATE KEY UPDATE` | 根 `assignment[A]` 按序定位；插入、整项替换和删除 3 个 patch 保持 MySQL 语法 |
| MU007 | `mysql-update-ignore` | `UPDATE IGNORE ...` | 保留 `IGNORE` 修饰符，赋值和条件复用普通 UPDATE 结构 |
| MU008 | `mysql-delete-ignore` | `DELETE IGNORE ...` | 保留 `IGNORE` 修饰符，条件复用普通 DELETE 结构 |
| MU008A | `mysql-update-low-priority-ignore-join` | `UPDATE LOW_PRIORITY IGNORE ... JOIN ...` | 组合修饰符与多表 UPDATE JOIN 共用现有目标表、来源表和条件归属 |
| MU008B | `mysql-delete-low-priority-quick-ignore-join` | `DELETE LOW_PRIORITY QUICK IGNORE ... JOIN ...` | 组合修饰符与多表 DELETE JOIN 共用现有删除目标和条件归属 |
| MU009 | `mysql-update-join` | `UPDATE ... JOIN ... SET ...` | 带 `ON` 条件的普通/INNER/CROSS 多表 UPDATE 的目标表、来源表、赋值和条件参数映射 |
| MU010 | `mysql-delete-join` | `DELETE u FROM ... JOIN ...` | 带 `ON` 条件的普通/INNER/CROSS 多表 DELETE 的目标表、来源表和条件参数映射 |
| MU010A | `mysql-update-join-on-bind` | `UPDATE ... JOIN ... ON ... ? SET ... WHERE ...` | 多表 UPDATE 中 JOIN `ON` 参数归属为 `on`，后续 `WHERE` 参数仍归属为 `where` |
| MU010B | `mysql-delete-join-on-bind` | `DELETE u FROM ... JOIN ... ON ... ? WHERE ...` | 多表 DELETE 中 JOIN `ON` 参数归属为 `on`，后续 `WHERE` 参数仍归属为 `where` |
| MU011 | `mysql-auto-increment` | `CREATE TABLE ... AUTO_INCREMENT` | MySQL 自增列属性在预处理阶段映射到通用 DDL AST，deparse 恢复公开 MySQL 片段 |
| MU012 | `mysql-unsigned` | `CREATE TABLE ... UNSIGNED` | MySQL 数值类型 `UNSIGNED` 属性解析和公开 SQL 恢复 |
| MU013 | `mysql-zerofill` | `CREATE TABLE ... ZEROFILL` | MySQL 数值类型 `ZEROFILL` 属性解析和公开 SQL 恢复 |
| MU014 | `mysql-table-engine` | `CREATE TABLE ... ENGINE=...` | MySQL 表存储引擎选项解析和公开 SQL 恢复 |
| MU015 | `mysql-table-charset` | `CREATE TABLE ... DEFAULT CHARSET=...` | MySQL 表默认字符集选项解析和公开 SQL 恢复 |
| MU016 | `mysql-table-character-set` | `CREATE TABLE ... CHARACTER SET=...` | MySQL 表字符集选项解析和公开 SQL 恢复 |
| MU017 | `mysql-table-collate` | `CREATE TABLE ... COLLATE=...` | MySQL 表排序规则选项解析和公开 SQL 恢复 |
| MU018 | `mysql-update-left-join` | `UPDATE ... LEFT JOIN ... SET ...` | LEFT JOIN 多表 UPDATE 的 join kind、目标表赋值列和条件参数映射 |
| MU019 | `mysql-delete-left-join` | `DELETE u FROM ... LEFT JOIN ...` | LEFT JOIN 多表 DELETE 的目标表、来源表和条件参数映射 |
| MU020 | `mysql-update-join-source-assignment` | `UPDATE ... JOIN ... SET source_alias.column = ...` | 修改 JOIN 右侧单目标表时，目标表、来源表和赋值参数映射 |
| MU021 | `mysql-delete-join-source-target` | `DELETE source_alias FROM target JOIN source_alias ...` | 删除 JOIN 右侧单目标表时，删除目标和条件参数映射 |
| M069 | `mysql-field-match-kind-direct-and-expression` | 直接字段条件 + 函数包裹字段条件 | `query_graph.values[].field_match_kind` 区分 `direct_field` 和 `expression_field` |
| M070 | `mysql-expression-field-case-expression-value` | CASE 返回字段再与 `?` 比较 | CASE 表达式字段输出 `expression_field` value 关系 |
| M071 | `mysql-expression-field-multi-field-expression-value` | `CONCAT(secret, id)`、`secret + id` 与 `?` 比较 | 表达式内字段分别保留 `expression_field` value 关系 |
| M072 | `mysql-expression-field-value-side-expression` | 字段与值侧函数、CONCAT、CAST 比较 | 值侧表达式输出 `kind=expression`，不暴露 direct bind |
| M073 | `mysql-expression-field-dml-expression-values` | INSERT/UPDATE 表达式赋值 | DML cell/assignment 输出 `kind=expression` |
| M074 | `mysql-update-bind-rhs-crypto-source` | `UPDATE ... SET protected = ?` | UPDATE SET 右值为位置 bind 的保护字段来源表达，可用于后续结构化备份列插入和 literal 改写 |
| M075 | `mysql-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = ?, protected2 = ?` | 多个保护字段的 SET bind、字段归属和全局 bind 序号 |
| M076 | `mysql-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | MySQL 字面量 ESCAPE 输出到 `values[].like_escape` |
| M077 | `mysql-like-escape-question-binds` | `LIKE ? ESCAPE ?` | pattern 与 escape 的 JDBC 位置参数分别保留全局 bind 序号 |
| M078 | `mysql-not-like-escape-literal` | `NOT LIKE ? ESCAPE '!'` | 否定 LIKE 中 pattern bind 与字面量 ESCAPE 的结构化输出 |
| M079 | `mysql-like-without-explicit-escape` | `LIKE ?` | 无显式 ESCAPE 时不输出 `like_escape` |
| M089 | `mysql-update-join-source-field-graph` | `UPDATE ... JOIN ... SET target = source.column` | 多表 UPDATE 中 SET 右侧真实表来源字段输出 `source_field`，JOIN/WHERE 字段对比输出 `right_field` |
| M090 | `mysql-insert-select-source-block-graph` | `INSERT ... SELECT ... FROM ...` | INSERT SELECT 的目标列、来源块和来源字段链路 |
| M091 | `mysql-with-cte-select` | `WITH cte AS (...) SELECT ...` | WITH 公用表表达式、内层表归属和位置参数序号 |
| M092 | `mysql-window-row-number` | `ROW_NUMBER() OVER (PARTITION BY ... ORDER BY ...)` | 窗口函数表达式中的字段路径和输出项归属 |
| M093 | `mysql-common-scalar-functions` | `LOWER(...)`、`COALESCE(...)` | 普通标量函数输出路径和 WHERE 参数归属 |
| M094 | `mysql-common-data-types` | `CREATE TABLE` + 常用类型名 | 通用类型名映射到当前 DDL/type AST |
| M095 | `mysql-rollback` | `ROLLBACK` | 事务回滚语句 |
| M096 | `mysql-json-contains-function-predicate` | `JSON_CONTAINS(tags, ?)` | JSON 函数谓词复用 `fields/values/predicates` 输出字段、bind 和 expression predicate |
| M097 | `mysql-select-alias-order-by-lineage` | `SELECT u.email AS e ... ORDER BY u.email` | SELECT 输出别名保留 base field lineage，ORDER BY 字段独立归属 |
| M098 | `mysql-create-table-column-and-table-options` | `CREATE TABLE` + 列属性 + 表选项 | 同一建表语句内组合验证 `UNSIGNED`、`AUTO_INCREMENT`、`ZEROFILL`、`ENGINE`、`CHARSET` 和 `COLLATE` |
| M099 | `mysql-create-table-official-column-attributes` | `CREATE TABLE` + 官方列属性组合 | 覆盖 `PRIMARY KEY`、`COMMENT`、`CHARACTER SET`、`COLLATE`、`INVISIBLE`、生成列和列级 `REFERENCES` 公开 SQL 恢复 |
| M100 | `mysql-create-table-numeric-table-options` | `CREATE TABLE` + 数值类表选项 | 覆盖 `AUTO_INCREMENT`、`AVG_ROW_LENGTH`、`CHECKSUM`、`DELAY_KEY_WRITE`、`KEY_BLOCK_SIZE`、行数和统计表选项恢复 |
| M101 | `mysql-create-table-string-and-storage-table-options` | `CREATE TABLE` + 字符串/存储类表选项 | 覆盖 `COMMENT`、`COMPRESSION`、`CONNECTION`、目录、加密、engine attribute、`ROW_FORMAT` 和 `TABLESPACE` 恢复 |
| M102 | `mysql-create-table-partition-options` | `CREATE TABLE` + `PARTITION BY` | 无查询表达式建表中的分区尾部公开 SQL 恢复 |
| M103 | `mysql-create-temporary-table-options` | `CREATE TEMPORARY TABLE IF NOT EXISTS` + 列属性 + 表选项 | 临时表、列可见性、列注释、`ENGINE` 和 `DEFAULT CHARACTER SET` 组合恢复 |
| M104-M106 | MySQL INSERT 扩展 | `ON DUPLICATE KEY UPDATE`、row alias、`INSERT ... SET` | 冲突更新来源、row alias 和 SET 写入结构 |
| M106A | `mysql-insert-set-paired-column-patch` | `INSERT ... SET` 成对字段和值 | 通过 `insert_columns` 原子插入和删除同位字段值对 |
| M107-M109 | MySQL DELETE/UPDATE 扩展 | 别名删除目标、`ORDER BY`、`LIMIT` | 删除目标归属和 DML 尾部恢复 |
| M110 | `mysql-select-lock-in-share-mode` | `LOCK IN SHARE MODE` | locking read 解析和公开 SQL 恢复 |
| M111 | `mysql-select-straight-join` | `STRAIGHT_JOIN` | relation、ON 字段和公开 SQL 恢复 |
| M112-M115 | MySQL index hint | `USE/FORCE/IGNORE INDEX` + scope | 提示定位和公开 SQL 恢复 |
| M116 | `mysql-comma-table-index-hint-attribution` | 逗号表列表上的 index hint | 紧邻 relation 的提示恢复位置 |
| M117-M118 | index hint 列表边界 | 多索引列表、空 `USE INDEX ()` | 完整列表和空列表恢复 |
| M119-M121 | JOIN 结构边界 | 嵌套 JOIN、`USING`、`NATURAL` | relation、ON/USING 字段和公开 SQL 恢复 |
| M122-M123 | locking read 等待策略 | `NOWAIT`、`SKIP LOCKED` | locking read 解析和公开 SQL 恢复 |
| M124 | `mysql-update-order-limit-bind-isolation` | bind + `ORDER BY` + `LIMIT` | assignment/WHERE bind 不被 DML 尾部污染 |
| M125 | `mysql-nested-straight-join-order` | 嵌套 `STRAIGHT_JOIN` | relation 顺序和公开 SQL 恢复 |
| M126 | `mysql-on-duplicate-row-column-aliases` | row alias column list | alias 列来源不误归属目标表 |
| M127 | `mysql-cte-index-hint-location-attribution` | CTE 内外 index hint | CTE 来源块、嵌套 relation 和提示恢复位置 |
| M128 | `mysql-table-partition-with-index-hints` | `PARTITION(...)` + index hint | 限定表名、别名和公开 SQL 恢复 |
| M129-M135 | 索引提示与查询尾部边界 | `HAVING`、`WINDOW`、集合运算和锁定子句 | 索引提示保持在表引用之后、查询尾部之前 |
| M136-M140 | 索引提示与 JOIN / 作用域组合 | `NATURAL JOIN`、`STRAIGHT_JOIN`、`USING`、别名和多作用域索引提示 | 左右表引用、别名及多个提示的恢复顺序 |
| M141-M145 | 索引提示的嵌套与标识符边界 | CTE、多语句、分区表、保留字别名和派生表 | 各层表引用的提示位置和反解析结果 |
| M226-M229 | UPDATE/DELETE 尾部结构回归 | 仅 `ORDER BY`、仅 `LIMIT`、DELETE bind 隔离、别名限定的多排序键 | ORDER 字段独立归属且可 patch；LIMIT 不污染 WHERE/assignment bind；原始及 patch 后 SQL 精确保留 |
| M230 | `mysql-cte-update-nested-order-limit-bind-isolation` | CTE、`USE INDEX FOR ORDER BY`、相关标量子查询排序及 assignment/WHERE/ORDER/LIMIT 混合 bind | index hint 的 `FOR ORDER BY` 不形成 DML 尾部；CTE 来源块与相关字段归属明确；前三个语义 bind 分别归属 assignment、外层 WHERE 和嵌套 predicate；LIMIT bind 不进入 View；8 个 patch 精确反解析 |
| M231 | `mysql-update-join-compound-on-where-or-bind-order` | 复合 `ON AND`、`WHERE OR` 及 ON/SET/WHERE 混合 bind | ON 与 WHERE 分别形成独立布尔根；bind 位置依次为 ON 1、SET 2、WHERE 3/4；字段、关系、值及 assignment patch 精确反解析 |
| M232 | `mysql-delete-left-join-where-three-and` | `LEFT JOIN` + 三项 `WHERE AND` | ON 比较独立于 WHERE AND 根；WHERE 根保持三个同 clause 子节点；字段、值和关系 patch 精确反解析 |
| M233 | `mysql-delete-right-join-compound-on-no-where` | `RIGHT JOIN` + 复合 ON，无 WHERE | 删除目标、反向 relation 顺序、ON AND 根及 bind 归属保持正确，不生成虚假 WHERE |
| M234 | `mysql-update-join-where-or-and-precedence` | 未加外层括号的 `WHERE a OR b AND c` | WHERE 保持 OR 根及右侧 AND 子树，不与 ON 合并；assignment、字段、值和关系 patch 保持运算优先级 |
| M235 | `mysql-update-join-user-wrapper-name-where` | WHERE 中调用与内部 marker 同名的普通函数 | 仅内部 wrapper 指针归入 ON；用户函数保持 WHERE expression predicate，字段和 assignment patch 不泄漏或误删函数 |
| M236 | `mysql-named-window-partition-order-independent-selectors` | 命名窗口同时包含 `PARTITION BY` 与 `ORDER BY` | 定义字段分别归类为 `window_partition` 和 `order_by`；同名 SELECT 字段与窗口分区字段具有独立 selector，并通过逐项 patch 验证 |
| M237 | `mysql-named-window-reused-multiple-order-fields` | 两个窗口函数复用同一命名窗口，窗口定义包含两个排序字段 | 命名窗口定义只遍历一次，两个排序字段各自进入 Query Graph 并可独立 patch |
| M238 | `mysql-named-window-inheritance-partition-order` | 基础窗口定义分区，派生窗口继承后补充排序 | 两个窗口定义按物理定义顺序遍历，分区字段与继承窗口排序字段保持独立归属和 selector |
| M239 | `mysql-named-window-frame-and-query-order` | 命名窗口分区、排序、ROWS frame 与查询级 `ORDER BY` 组合 | 窗口排序和查询排序各自定位；frame 原文、View 语义及全部 patch 结果精确验证 |
| M240 | `mysql-join-on-field-cast-expression-value` | JOIN ON 中直接字段与 `CAST(? AS SIGNED)` 值侧表达式比较 | ON comparison 的 `predicate.value` 引用唯一 expression value，`field_match_kind=direct_field`，CAST 内部 bind 不提升为独立 value |
| M241 | `mysql-join-on-function-and-field-comparison` | JOIN ON 中函数表达式与 bind、字段对字段比较通过 AND 组合 | ON 保留 AND 子节点顺序；`UPPER(u.role_code) = ?` 输出 expression predicate，`u.role_id = r.id` 输出双侧字段 comparison；3 个 patch 精确反解析 |
| M242 | `mysql-row-in-subquery-membership` | `(tenant_id, id) IN (SELECT tenant_id, user_id ...)` | 行值左侧两个字段完整保留，membership predicate 不任意绑定单个字段；内层双 target、过滤 bind 和 block 归属明确，6 个 patch 覆盖跨层 selector |
| M243 | `mysql-left-join-on-null-test` | `LEFT JOIN ... ON p.user_id = u.id AND p.deleted_at IS NULL` | ON 布尔树按原顺序包含字段对字段比较与 `IS NULL` comparison；NULL 测试只引用 `p.deleted_at`，不生成 NULL value；WHERE bind 独立归属，4 个 patch 精确验证 |
| M244 | `mysql-relation-patch-cte-outer-qualifiers` | CTE 定义、内部基础关系及外层 CTE 引用 | View 明确区分 CTE 来源块和外层关系；外层关系 patch 同步更新限定星号、直接字段及 WHERE 字段，并保持 CTE 声明和内部基础关系不变 |
| M245 | `mysql-update-join-compound-rhs` | `UPDATE ... JOIN` 中的复合赋值右值 | source field、位置参数和 literal 通过 `rhs_fields` 和 `rhs_values` 归属同一 assignment；source relation alias 保持稳定，并精确验证 assignment 替换与插入 |
| M247 | `mysql-derived-mixed-limit-surfaces` | 派生查询使用 `LIMIT offset, count`，外层查询使用 `LIMIT count OFFSET offset` | 两层 LIMIT 的原始表面形式分别保留；内外层 relation、target 及外层 target 插入 patch 均精确反解析 |
| M248 | `mysql-union-result-limit-offset` | `UNION ALL` 结果使用参数化 `LIMIT count OFFSET offset` | 集合根及两个分支的 View 归属保持正确；relation 与两侧 target patch 后仍逐字节保留 OFFSET 形式 |
| M249 | `mysql-limit-comma-comment-trivia` | `LIMIT offset, count` 三个 token 间隙穿插普通块注释 | 原始 SQL 可解析且 generation-0 逐字节保留；target patch 的普通注释回放保持正确期望，并纳入 RG016 闭环 |
| M250 | `mysql-string-quote-escape-surfaces` | 单引号与双引号字符串混用反斜杠、重复引号转义 | View 中四个等价字符串值及 selector 逐项准确；原始反解析和 relation、target、value、insert patch 后未修改字面量均逐字节保留 |
| M251 | `mysql-string-common-backslash-escapes` | 换行、制表符和反斜杠的常用字符串转义 | View 保存解码后的字符串语义；原始转义拼写及 patch fragment 的引号、national 前缀和反斜杠逐字节保留 |
| M252 | `mysql-string-equal-value-surfaces` | 普通字符串、`n` 与 `N` 前缀字符串具有相同值 | View 中三个 value 独立定位；按 AST owner 保留各自表面拼写，替换或插入单个节点不会串用其他节点的拼写 |
| M253 | `mysql-string-nested-surface-owners` | 外层双引号字符串、内层 `n` 字符串和 WHERE 转义字符串 | 嵌套 block、relation、field、target 和 value 归属完整；跨层 patch 后所有未修改字符串仍逐字节保留 |
| MU006 | `mysql-replace-into` | `REPLACE INTO ... VALUES ...` | MySQL `REPLACE` 复用 INSERT 图结构，并通过 `insert_mode=replace_values` 保留替换插入语义 |
| MU006A | `mysql-replace-low-priority-multi-row` | `REPLACE LOW_PRIORITY INTO ... VALUES (...), (...)` | 多行 `REPLACE VALUES`、位置参数和 `LOW_PRIORITY` 修饰符 |
| MU006B | `mysql-replace-delayed-select` | `REPLACE DELAYED INTO ... SELECT ...` | `REPLACE SELECT` 的目标表、来源表、位置参数和 `insert_mode=replace_select` |
| MU006C | `mysql-replace-set` | `REPLACE INTO ... SET ...` | 输出 `insert_mode=replace_set`；对未修改的 handle 执行 deparse 时，结果逐字节保留原始 `SET` 形态 |
| MU006D | `mysql-replace-without-into` | `REPLACE table ... VALUES ...` | 对未修改的 handle 执行 deparse 时，结果逐字节保留省略 `INTO` 的原始形态 |
| MU006E | `mysql-replace-table-source` | `REPLACE INTO ... TABLE source` | 保留来源表；对未修改的 handle 执行 deparse 时，结果逐字节保留原始 `TABLE` 形态 |

## 多目标多表 UPDATE 回归

MySQL 多表 `UPDATE` 支持 JOIN 链和逗号 relation 列表中的多个写入目标；每个 assignment 的目标字段分别关联其 relation。多表形态不接受 `ORDER BY` 或 `LIMIT`。

| 用例 ID | 用例名称 | SQL 形态 | 验证重点 |
| --- | --- | --- | --- |
| M254 | `mysql-update-multiple-target-inner-join` | 两表 `INNER JOIN`、交错双目标 assignment | assignment relation 归属及插入、替换、删除 patch |
| M255 | `mysql-update-multiple-target-three-table-bind-order` | 三表 JOIN、三目标 assignment、12 个 `?` | ON/SET/WHERE occurrence 顺序及 patch 后重编号 |
| M256 | `mysql-update-multiple-target-four-relation-comma-list` | 四 relation 逗号列表、三目标 assignment | 逗号 relation、目标字段归属及 assignment patch |
| M257 | `mysql-update-multiple-target-four-table-mixed-join` | 四表 INNER/LEFT 混合 JOIN、四目标 assignment | JOIN 链、逐 assignment relation 和末项替换 |
| M258 | `mysql-update-multiple-target-quoted-identifiers` | schema-qualified 反引号对象与双目标 assignment | quoted relation/field 归属及 relation、assignment patch |

## Query Graph 引号别名合同

`relations[].alias_quoted_identifier` 仅在 relation alias 使用反引号定界时为 `true`。`targets[].output_quoted_identifier` 在 output name 来自反引号显式别名，或无显式别名时继承反引号字段名时为 `true`；View JSON 不输出值为 `false` 的键。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| M259 | `mysql-quoted-alias-output-flags` | 反引号 relation/派生表 alias 与 output name | 两个引号标志、字段名继承及 2 个 output alias patch |

## MERGE INSERT 独立列值改写

该项属于项目当前 MySQL 兼容入口的 patch 合同，不表示 MySQL 服务端官方支持 `MERGE` 语法。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| M260 | `mysql-merge-omitted-insert-column-value-independent` | 省略目标列列表的 `WHEN NOT MATCHED THEN INSERT VALUES (...)` | 省略状态仍输出 `target_list_selector`；3 个独立 patch 分别验证 column-only 物化列列表、value-only 追加 cell 和现有 `merge_insert_cell` 替换；与既有 paired 模式共同覆盖 `insert_column` 三态合同 |

## Query Graph 分段引号标识合同

relation 的限定名按段记录反引号状态：`database_quoted_identifier`、`schema_quoted_identifier`、既有的 object `quoted_identifier`，以及存在 database link 时的 `link_quoted_identifier`；DML 目标列使用 `dml_column.quoted_identifier`。每个标志只描述对应名称段，未定界或不存在的段不输出该键，不能由名称大小写推断。MySQL 可执行入口没有 database link，因此 `link_quoted_identifier` 不适用。该节中的 `MERGE` 仅验证项目兼容入口，不表示 MySQL 官方服务端语法；公共 C 结构生命周期由 `tests/unit/test_identifier_spelling.c` 验证。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| M261 | `mysql-quoted-identifier-segment-and-dml-column-inventory` | 7 条语句覆盖三段 relation、普通 INSERT、UPDATE、DELETE、兼容入口 MERGE INSERT、MySQL `INSERT ... SET` 与 `REPLACE ... SET` | `database_quoted_identifier`、`schema_quoted_identifier`、普通/分支/SET `dml_column.quoted_identifier` 的 quoted/unquoted 同名对照；5 个独立 patch 验证 relation 分段重算、MERGE 列替换、paired 列插入及 INSERT SET 列插入后的标志重算 |

## INSERT VALUES 回归：bind 与表达式混合

以下 10 条用例验证 prepared statement 或驱动生成的 SQL 模板。未加引号的 `?` 只表示数据值参数，不能替代表名、列名或关键字；含该参数标记的 SQL 必须经过 prepare/bind 流程执行。

`DEFAULT` 只作为独立的 VALUES cell。表达式 cell 保持 `kind=expression`，不会被分类为 `kind=bind`；表达式内部的 `?` 仍计入全局 bind 序号，后续直接 bind 的 `bind_position` 用于验证表达式内 bind 已纳入全局计数。直接 bind cell 同时校验 `bind_key`、`bind_kind`、`bind_sql`、全局 `bind_position` 和 `selector`；时间函数和 UUID 函数名不得出现在 `query_graph.fields[].column` 中。

| 用例 ID | 用例名称 | VALUES 形态 | 验证重点 |
| --- | --- | --- | --- |
| `MY-BM001` | `mysql-insert-bind-mixed-bare-time` | 三个直接 `?` + `NOW()` | 直接 bind cell 的 key、kind、SQL、位置、selector 及尾部时间表达式 |
| `MY-BM002` | `mysql-insert-bind-mixed-expression-first` | `CAST(? AS SIGNED)`、`?`、`CURRENT_TIMESTAMP`、`?` | 首位表达式、嵌套 bind 全局序号和后续直接 bind |
| `MY-BM003` | `mysql-insert-bind-mixed-interleaved-functions` | `?`、`UUID()`、`?`、`COALESCE(?, 'fallback')`、`?` | bind 与表达式交错、`UUID` 不出现在 `query_graph.fields[].column` 中、嵌套 bind 计数 |
| `MY-BM004` | `mysql-insert-bind-mixed-null` | `?`、`NULL`、`CAST(? AS SIGNED)`、`?` | NULL literal、表达式和直接 bind cell 独立分类 |
| `MY-BM005` | `mysql-insert-bind-mixed-default` | `?`、`DEFAULT`、`CAST(? AS SIGNED)`、`?` | 独立 DEFAULT cell 和嵌套 bind 全局序号 |
| `MY-BM006` | `mysql-insert-bind-mixed-literal` | `?`、字符串 literal、包含 `?` 的 `CASE` 表达式、`?` | 字符串 literal、CASE 表达式和后续 bind 位置 |
| `MY-BM007` | `mysql-insert-bind-mixed-coalesce-time` | `?`、`COALESCE(?, 'fallback')`、`CURRENT_TIMESTAMP`、`?` | COALESCE 内部 bind、独立时间表达式和直接 bind |
| `MY-BM008` | `mysql-insert-bind-mixed-case-time` | `?`、包含 `?` 的 `CASE` 表达式、`NOW()`、`?` | CASE 表达式内 bind、独立时间表达式和直接 bind |
| `MY-BM009` | `mysql-insert-bind-mixed-three-rows` | 三行中交错 bind、CAST/COALESCE/CASE 表达式和时间表达式 | 9 个匿名 occurrence 逐次保留；逐 cell selector、跨行连续序号及头/尾 bind 删除后的 8 项重编号 |
| `MY-BM010` | `mysql-insert-bind-mixed-quoted-irregular-whitespace` | schema-qualified 反引号标识符、不规则空白、三个直接 `?` + 时间表达式 | quoted identifier、逐字节保留输入 SQL，并与期望 cell 对象逐字段一致 |

## 覆盖边界

本矩阵只列出可成功解析并具有最终 View 与 patch 期望的用例。未纳入该可执行夹具的语法边界由 `doc/mysql_official_syntax_coverage.csv` 维护。

## 处理规则

- 默认方言是 `SQLPARSER_DIALECT_POSTGRESQL`。
- MySQL 语句必须通过 `sqlparser_parse_with_options` 显式传入 `SQLPARSER_DIALECT_MYSQL`。
- 可安全映射的语法在 dialect preprocess / postprocess 层处理。
- 新增 MySQL 支持项必须同步更新 `tests/cases/mysql_dialect_input.json`、本矩阵和可执行回归测试。
- 未纳入可执行夹具的语法不得在本矩阵中登记为已验证用例。
