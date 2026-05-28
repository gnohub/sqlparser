# MySQL 方言用例矩阵

本文件记录 MySQL 方言转换层的回归用例。`tests/cases/mysql_dialect_input.json` 是可执行测试源，`tests/unit/test_mysql_dialect_case_matrix.c` 会逐条读取该文件并验证解析、View JSON、deparse 和错误码。

## 已验证支持语句

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| M001 | `mysql-select-limit-comma` | `SELECT ... FROM ... WHERE ... LIMIT offset,count` | 反引号标识符、双引号字符串、表名、查询列、WHERE literal、MySQL comma limit deparse |
| M002 | `mysql-select-join` | `SELECT ... JOIN ... ON ... WHERE ...` | 多表 JOIN、查询列、关联列、条件列 |
| M003 | `mysql-hash-comment` | `SELECT ... # comment` | MySQL `#` 行注释预处理 |
| M004 | `mysql-insert-values-multi-row` | `INSERT ... VALUES (...), (...)` | 多行插入、插入列、双引号字符串归一化 |
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
| MU005 | `mysql-on-duplicate-key` | `INSERT ... ON DUPLICATE KEY UPDATE ...` | MySQL upsert 映射到 DML 插入值和更新赋值 |
| MU009 | `mysql-update-join` | `UPDATE ... JOIN ... SET ...` | 带 `ON` 条件的普通/INNER/CROSS 多表 UPDATE 的目标表、来源表、赋值和条件参数映射 |
| MU010 | `mysql-delete-join` | `DELETE u FROM ... JOIN ...` | 带 `ON` 条件的普通/INNER/CROSS 多表 DELETE 的目标表、来源表和条件参数映射 |
| MU010A | `mysql-update-join-on-bind` | `UPDATE ... JOIN ... ON ... ? SET ... WHERE ...` | 多表 UPDATE 中 JOIN `ON` 参数归属为 `on`，后续 `WHERE` 参数仍归属为 `where` |
| MU010B | `mysql-delete-join-on-bind` | `DELETE u FROM ... JOIN ... ON ... ? WHERE ...` | 多表 DELETE 中 JOIN `ON` 参数归属为 `on`，后续 `WHERE` 参数仍归属为 `where` |

## 明确不支持语句

以下语法具有 MySQL 专有语义或当前 AST 无法安全表达的结构，解析时返回 `SQLPARSER_STATUS_UNSUPPORTED`。

| 用例 ID | 用例名称 | 语句形态 | 原因 |
| --- | --- | --- | --- |
| MU001 | `mysql-insert-ignore` | `INSERT IGNORE ...` | 忽略错误的语义不能安全降级为普通 `INSERT` |
| MU002 | `mysql-insert-delayed` | `INSERT DELAYED ...` | 延迟插入语义需要 MySQL 专用执行语义 |
| MU003 | `mysql-insert-low-priority` | `INSERT LOW_PRIORITY ...` | 优先级语义无法映射到通用 AST |
| MU004 | `mysql-insert-high-priority` | `INSERT HIGH_PRIORITY ...` | 优先级语义无法映射到通用 AST |
| MU006 | `mysql-replace-into` | `REPLACE INTO ...` | 删除再插入的语义不同于普通 `INSERT` |
| MU007 | `mysql-update-ignore` | `UPDATE IGNORE ...` | 忽略错误语义需要 MySQL 专用执行语义 |
| MU008 | `mysql-delete-ignore` | `DELETE IGNORE ...` | 忽略错误语义需要 MySQL 专用执行语义 |
| MU011 | `mysql-auto-increment` | `AUTO_INCREMENT` | 列属性需要 MySQL DDL 语义扩展 |
| MU012 | `mysql-unsigned` | `UNSIGNED` | 类型属性需要 MySQL 类型系统扩展 |
| MU013 | `mysql-zerofill` | `ZEROFILL` | 类型属性需要 MySQL 类型系统扩展 |
| MU014 | `mysql-table-engine` | `ENGINE=...` | 表选项需要 MySQL DDL 语义扩展 |
| MU015 | `mysql-table-charset` | `DEFAULT CHARSET=...` | 表字符集选项需要 MySQL DDL 语义扩展 |
| MU016 | `mysql-table-character-set` | `CHARACTER SET=...` | 表字符集选项需要 MySQL DDL 语义扩展 |
| MU017 | `mysql-table-collate` | `COLLATE=...` | 表排序规则选项需要 MySQL DDL 语义扩展 |
| MU018 | `mysql-update-left-join` | `UPDATE ... LEFT JOIN ... SET ...` | 外连接 UPDATE 的受影响行语义不能降级为普通 `UPDATE FROM` |
| MU019 | `mysql-delete-left-join` | `DELETE u FROM ... LEFT JOIN ...` | 外连接 DELETE 的受影响行语义不能降级为普通 `DELETE USING` |
| MU020 | `mysql-update-join-source-assignment` | `UPDATE ... JOIN ... SET source_alias.column = ...` | 多表 UPDATE 修改 JOIN 来源表时不能降级为单目标表 `UPDATE FROM` |
| MU021 | `mysql-delete-join-source-target` | `DELETE source_alias FROM target JOIN source_alias ...` | 多表 DELETE 删除 JOIN 来源表时不能降级为单目标表 `DELETE USING` |

## 处理规则

- 默认方言是 `SQLPARSER_DIALECT_POSTGRESQL`。
- MySQL 语句必须通过 `sqlparser_parse_with_options` 显式传入 `SQLPARSER_DIALECT_MYSQL`。
- 可安全映射的语法在 dialect preprocess / postprocess 层处理。
- 不能安全映射的 MySQL 专有语义返回 `SQLPARSER_STATUS_UNSUPPORTED`。
- 新增 MySQL 支持项必须同步更新 `tests/cases/mysql_dialect_input.json`、本矩阵和可执行回归测试。
