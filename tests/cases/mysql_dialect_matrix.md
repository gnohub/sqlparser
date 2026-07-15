# MySQL 方言用例矩阵

本文件记录 MySQL 方言转换层的回归用例。`tests/cases/mysql_dialect_input.json` 是可执行测试源，`tests/unit/test_mysql_dialect_case_matrix.c` 会逐条读取该文件并验证解析、View JSON、deparse 和错误码。

## 已验证支持语句

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| M001 | `mysql-select-limit-comma` | `SELECT ... FROM ... WHERE ... LIMIT offset,count` | 反引号标识符、双引号字符串、表名、查询列、WHERE literal、MySQL comma limit deparse |
| M002 | `mysql-select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | 多表 JOIN、查询列、关联列、条件列 |
| M003 | `mysql-hash-comment` | `SELECT ... # comment` | MySQL `#` 行注释预处理 |
| M004 | `mysql-insert-values-multi-row` | `INSERT ... VALUES (...), (...)` | 多行插入、插入列、双引号字符串归一化 |
| M004N | `mysql-national-string-literal` | `SELECT "..." ... N'...' ... n'...'` | national 字符串前缀保留，大小写输入统一公开为 `N` 前缀 |
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
| M053 | `mysql-multi-statement-global-bind-position` | 多语句 `UPDATE ... ?` | 多语句输入中位置参数 `bind_position` 按整条 SQL 全局递增 |
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
| MU006 | `mysql-replace-into` | `REPLACE INTO ... VALUES ...` | MySQL `REPLACE` 复用 INSERT 图结构，并通过 `insert_mode=replace_values` 保留替换插入语义 |
| MU006A | `mysql-replace-low-priority-multi-row` | `REPLACE LOW_PRIORITY INTO ... VALUES (...), (...)` | 多行 `REPLACE VALUES`、位置参数和 `LOW_PRIORITY` 修饰符 |
| MU006B | `mysql-replace-delayed-select` | `REPLACE DELAYED INTO ... SELECT ...` | `REPLACE SELECT` 的目标表、来源表、位置参数和 `insert_mode=replace_select` |
| MU006C | `mysql-replace-set` | `REPLACE INTO ... SET ...` | `SET` 形态规范化为公开 MySQL `REPLACE ... VALUES`，并输出 `insert_mode=replace_set` |
| MU006D | `mysql-replace-without-into` | `REPLACE table ... VALUES ...` | 省略 `INTO` 的官方形态规范化为 `REPLACE INTO ...` |
| MU006E | `mysql-replace-table-source` | `REPLACE INTO ... TABLE source` | 官方 `TABLE` 形态规范化为 `REPLACE ... SELECT * FROM source` 并保留来源表 |

## 明确不支持语句

当前可执行 MySQL 方言矩阵没有明确不支持用例。官方语法覆盖边界见 `doc/mysql_official_syntax_coverage.csv`。

## 处理规则

- 默认方言是 `SQLPARSER_DIALECT_POSTGRESQL`。
- MySQL 语句必须通过 `sqlparser_parse_with_options` 显式传入 `SQLPARSER_DIALECT_MYSQL`。
- 可安全映射的语法在 dialect preprocess / postprocess 层处理。
- 不能安全映射的 MySQL 专有语义返回 `SQLPARSER_STATUS_UNSUPPORTED`。
- 新增 MySQL 支持项必须同步更新 `tests/cases/mysql_dialect_input.json`、本矩阵和可执行回归测试。
