# MySQL 方言支持

`SQLPARSER_DIALECT_MYSQL` 提供 MySQL SQL 到 `sqlparser` 当前 AST 模型的转换层。调用方需要通过 `sqlparser_parse_with_options()` 显式指定 MySQL 方言；未指定方言时仍按 PostgreSQL 语法解析。

## 支持范围

MySQL 方言支持可安全映射到当前 AST 的常用 SQL 形态，覆盖范围由可执行用例矩阵定义：

- `SELECT`、别名、子查询、连接、`WHERE`
- 反引号标识符
- MySQL `#` 行注释
- 双引号字符串兼容处理
- JDBC 风格 `?` 位置参数
- `LIMIT offset,count`
- `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`
- `UPDATE`、`DELETE`
- 基础 `CREATE TABLE`
- `ALTER TABLE ADD COLUMN`
- `CREATE VIEW`
- `DROP TABLE`
- `START TRANSACTION`、`COMMIT`
- `USE db_name` 默认数据库切换
- `PREPARE`、`EXECUTE`、`DEALLOCATE PREPARE`、`DROP PREPARE`

## 明确不支持范围

以下 MySQL 专属语义当前不做隐式降级。遇到这些语法时返回 `SQLPARSER_STATUS_UNSUPPORTED`，不会返回可用 handle：

- `INSERT IGNORE`
- `INSERT DELAYED`
- `INSERT LOW_PRIORITY`、`INSERT HIGH_PRIORITY`
- `ON DUPLICATE KEY UPDATE`
- `REPLACE INTO`
- `UPDATE IGNORE`
- `DELETE IGNORE`
- `AUTO_INCREMENT`
- `UNSIGNED`
- `ZEROFILL`
- 表选项，例如 `ENGINE=...`
- 字符集和排序规则表选项，例如 `DEFAULT CHARSET=...`、`CHARACTER SET=...`、`COLLATE=...`

## 对外输出规则

- `sqlparser_deparse()` 输出 MySQL 公共形态，不暴露内部转换细节。
- 反引号标识符和 MySQL 字符串兼容规则由方言层处理。
- SQL View JSON 中可归属的表达式片段使用公共 MySQL 形态。
- 无法安全表达的 MySQL 专属语义不会降级为 PostgreSQL 语义。

## 回归用例

MySQL 支持范围以以下文件为准：

- `tests/cases/mysql_dialect_input.json`
- `tests/cases/mysql_dialect_matrix.md`
- `tests/unit/test_mysql_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

当前 MySQL 方言矩阵包含 48 条用例：33 条支持路径，15 条明确不支持路径。
