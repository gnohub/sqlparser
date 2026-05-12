# MySQL 方言用例矩阵

本文件记录 MySQL 方言转换层的回归用例。`tests/cases/mysql_dialect_input.json` 是可执行测试源，`tests/unit/test_mysql_dialect_case_matrix.c` 会逐条读取该文件并验证解析、SQL View JSON、deparse 和错误码。

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
| M015 | `mysql-use-database` | `USE analytics` | 默认数据库切换语句、SQL View value selector |
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

## 明确不支持语句

以下语法具有 MySQL 专有语义或当前 AST 无法安全表达的结构，解析时返回 `SQLPARSER_STATUS_UNSUPPORTED`。

| 用例 ID | 用例名称 | 语句形态 | 原因 |
| --- | --- | --- | --- |
| MU001 | `mysql-insert-ignore` | `INSERT IGNORE ...` | 忽略错误的语义不能安全降级为普通 `INSERT` |
| MU002 | `mysql-insert-delayed` | `INSERT DELAYED ...` | 延迟插入语义需要 MySQL 专用执行语义 |
| MU003 | `mysql-insert-low-priority` | `INSERT LOW_PRIORITY ...` | 优先级语义无法映射到通用 AST |
| MU004 | `mysql-insert-high-priority` | `INSERT HIGH_PRIORITY ...` | 优先级语义无法映射到通用 AST |
| MU005 | `mysql-on-duplicate-key` | `ON DUPLICATE KEY UPDATE ...` | 冲突处理语义不能安全映射到现有 AST |
| MU006 | `mysql-replace-into` | `REPLACE INTO ...` | 删除再插入的语义不同于普通 `INSERT` |
| MU007 | `mysql-update-ignore` | `UPDATE IGNORE ...` | 忽略错误语义需要 MySQL 专用执行语义 |
| MU008 | `mysql-delete-ignore` | `DELETE IGNORE ...` | 忽略错误语义需要 MySQL 专用执行语义 |
| MU009 | `mysql-auto-increment` | `AUTO_INCREMENT` | 列属性需要 MySQL DDL 语义扩展 |
| MU010 | `mysql-unsigned` | `UNSIGNED` | 类型属性需要 MySQL 类型系统扩展 |
| MU011 | `mysql-zerofill` | `ZEROFILL` | 类型属性需要 MySQL 类型系统扩展 |
| MU012 | `mysql-table-engine` | `ENGINE=...` | 表选项需要 MySQL DDL 语义扩展 |
| MU013 | `mysql-table-charset` | `DEFAULT CHARSET=...` | 表字符集选项需要 MySQL DDL 语义扩展 |
| MU014 | `mysql-table-character-set` | `CHARACTER SET=...` | 表字符集选项需要 MySQL DDL 语义扩展 |
| MU015 | `mysql-table-collate` | `COLLATE=...` | 表排序规则选项需要 MySQL DDL 语义扩展 |

## 处理规则

- 默认方言是 `SQLPARSER_DIALECT_POSTGRESQL`。
- MySQL 语句必须通过 `sqlparser_parse_with_options` 显式传入 `SQLPARSER_DIALECT_MYSQL`。
- 可安全映射的语法在 dialect preprocess / postprocess 层处理。
- 不能安全映射的 MySQL 专有语义返回 `SQLPARSER_STATUS_UNSUPPORTED`。
- 新增 MySQL 支持项必须同步更新 `tests/cases/mysql_dialect_input.json`、本矩阵和可执行回归测试。
