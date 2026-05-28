# SQL Server 方言支持

`SQLPARSER_DIALECT_SQLSERVER` 提供 SQL Server T-SQL 到 `sqlparser` 当前 AST 模型的转换层。调用方需要通过 `sqlparser_parse_with_options()` 显式指定 SQL Server 方言；未指定方言时仍按 PostgreSQL 语法解析。

## 支持范围

SQL Server 方言支持可安全映射到当前 AST 的常用 SQL 形态，覆盖范围由可执行用例矩阵定义：

- `SELECT`、别名、子查询、连接、`WHERE`、`ORDER BY`
- 方括号标识符，例如 `[dbo].[users]`
- SQL Server 参数占位符，例如 `@id`、`@name`
- JDBC `?` 参数占位符
- `TOP (n)`、`TOP (@param)` 查询限制
- `OFFSET ... FETCH NEXT ... ROWS ONLY`
- `N'...'` Unicode 字符串字面量
- 临时表名，例如 `#active_users`
- `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`
- `UPDATE`、`DELETE`
- `CASE`、窗口函数、`UNION ALL`、`EXCEPT`、`INTERSECT`
- 可映射的 `MERGE`
- 常见 DDL：`CREATE TABLE`、`ALTER TABLE ADD`、`CREATE VIEW`、`CREATE INDEX`、`DROP TABLE`、`TRUNCATE TABLE`
- `IDENTITY` 列属性的兼容映射
- 事务控制、`SAVE TRANSACTION`、`GRANT / REVOKE`
- `GO` 批处理分隔符
- `@@` 系统变量和 `0x...` 二进制字面量的公共形态保留
- `TRY_CAST`、`TRY_CONVERT`、`CONVERT(..., style)`、`PARSE`、`TRY_PARSE`
- ODBC `{fn ...}` 标量函数包装
- 简单 `RENAME OBJECT ... TO ...`
- 常见类型名和函数，例如 `NVARCHAR`、`BIT`、`DATETIME2`、`ISNULL`、`GETDATE`、`NEWID`
- `USE database_name` 数据库上下文切换
- `sp_prepare`、`sp_execute`、`sp_prepexec`、`sp_unprepare`、`sp_executesql` 参数化动态 SQL

## 明确不支持范围

以下 T-SQL 专属语义当前不做隐式降级。遇到这些语法时返回 `SQLPARSER_STATUS_UNSUPPORTED`，不会返回可用 handle：

- `TOP ... PERCENT` 与 `TOP ... WITH TIES`
- 同一查询作用域内同时使用 `TOP` 与 `OFFSET ... FETCH`
- 嵌套 `SELECT TOP`
- DML `TOP`，例如 `UPDATE TOP (...)`
- `OUTPUT`
- 表提示和查询提示，例如 `WITH (NOLOCK)`、`OPTION (...)`
- `CROSS APPLY`、`OUTER APPLY`
- `PIVOT`、`UNPIVOT`
- `FOR XML`、`FOR JSON`
- `DECLARE` 和普通 `EXEC` / `EXECUTE` 过程调用
- procedure、function、trigger 定义
- `BEGIN TRY` / `BEGIN CATCH`
- `OPENQUERY`、`OPENROWSET`、`OPENDATASOURCE`、`OPENJSON`、`OPENXML`
- 表变量，例如 `@table`
- `MERGE ... WHEN NOT MATCHED BY SOURCE`

## 对外输出规则

- `sqlparser_deparse()` 输出 SQL Server 公共形态，不暴露内部转换细节。
- `@name` 和 `?` 参数在 deparse 和 View JSON 中保持公共形态，不输出内部 `$1`、`$2`。
- `@@` 系统变量和 `0x...` 二进制字面量在 deparse 中保持 SQL Server 公共形态。
- SQL Server 风格转换函数在 deparse 输出中保持 `TRY_CAST`、`TRY_CONVERT`、`CONVERT`、`PARSE` 或 `TRY_PARSE`。
- `TOP (n)` 和 `OFFSET ... FETCH` 在 deparse 输出中保持 SQL Server 语法。
- `N'...'` Unicode 字符串在可保留语义的场景中输出 `N` 前缀。
- View JSON 中可归属的表达式片段使用公共 SQL Server 形态。
- 失败的表达式片段改写不会提交到 handle；原有 AST、参数映射和 deparse 输出保持可用。

## 回归用例

SQL Server 支持范围以以下文件为准：

- `tests/cases/sqlserver_dialect_input.json`
- `tests/cases/sqlserver_dialect_matrix.md`
- `tests/unit/test_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

当前 SQL Server 矩阵包含 335 条用例：320 条支持路径，15 条明确不支持路径。其中 235 条来自官方 `HOOK_ONLY` 覆盖项。
