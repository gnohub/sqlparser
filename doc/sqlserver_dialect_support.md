# SQL Server 方言支持

`SQLPARSER_DIALECT_SQLSERVER` 提供 SQL Server T-SQL 的解析、结构化遍历、改写和反解析能力。调用方需要通过 `sqlparser_parse_with_options()` 显式指定 SQL Server 方言；未指定方言时仍按 PostgreSQL 语法解析。

## 支持范围

SQL Server 方言的覆盖范围由可执行用例矩阵定义：

- `SELECT`、别名、子查询、连接、`WHERE`、`ORDER BY`
- 方括号标识符，例如 `[dbo].[users]`
- SQL Server 参数占位符，例如 `@id`、`@name`
- JDBC `?` 参数占位符
- `TOP (n)`、`TOP (@param)`、`TOP ... PERCENT`、`TOP ... WITH TIES` 查询限制；支持子查询中的 `TOP`；`WITH TIES` 形态要求同一查询作用域包含 `ORDER BY`
- `OFFSET ... FETCH NEXT ... ROWS ONLY`
- 基础表提示和查询提示公开形态保留，例如 `WITH (NOLOCK)`、`WITH (FORCESEEK)`、`OPTION (RECOMPILE)`
- `FOR JSON PATH/AUTO` 查询后缀公开形态保留
- `N'...'` Unicode 字符串字面量
- 临时表名，例如 `#active_users`
- 显式或省略 `INTO` 的 `INSERT VALUES`、多行 `INSERT`、`INSERT SELECT`、集合查询和 `DEFAULT VALUES`
- `UPDATE`、`DELETE`
- `INSERT`、`UPDATE`、`DELETE`、`MERGE` 的 `OUTPUT`；支持 `INSERTED`、`DELETED`、来源字段、`$action`、表达式、别名和 bind
- `OUTPUT ... INTO relation [(column, ...)]`、`OUTPUT ... INTO @table_variable` 及 sink/client 双通道
- 外层 `INSERT` 消费内层 DML `OUTPUT` 的嵌套 DML 形态
- `UPDATE TOP (...) ... OUTPUT` 和 INSERT 目标表 hint 与 `OUTPUT` 的组合
- `IF...ELSE` 单语句分支、`BEGIN...END` 多语句分支、`ELSE IF` 和嵌套控制流；条件支持布尔表达式、bind、`EXISTS` 和括号子查询
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
- `USE database_name`
- 基础 `SET` 会话/执行环境语句，例如 `SET NOCOUNT ON`、`SET DATEFORMAT dmy`、`SET IDENTITY_INSERT dbo.Tool ON`
- `sp_prepare`、`sp_execute`、`sp_prepexec`、`sp_unprepare`、`sp_executesql` 参数化动态 SQL

## 明确不支持范围

以下 T-SQL 专属语义当前不做隐式降级。遇到这些语法时返回 `SQLPARSER_STATUS_UNSUPPORTED`，不会返回可用 handle：

- 同一查询作用域内同时使用 `TOP` 与 `OFFSET ... FETCH`
- `TOP ... WITH TIES` 但同一查询作用域没有 `ORDER BY`
- 未列入可执行矩阵的 DML `TOP` 形态
- `OUTPUT` 中的聚合、子查询，以及 `INSERT EXEC` 与 `OUTPUT` 的组合
- `CROSS APPLY`、`OUTER APPLY`
- `PIVOT`、`UNPIVOT`
- `FOR XML`
- `DECLARE` 和普通 `EXEC` / `EXECUTE` 过程调用
- procedure、function、trigger 定义
- `BEGIN TRY` / `BEGIN CATCH`
- 不属于 `IF...ELSE` 分支的独立 `BEGIN...END` 语句块
- `OPENQUERY`、`OPENROWSET`、`OPENDATASOURCE`、`OPENJSON`、`OPENXML`
- 普通表变量引用；`OUTPUT INTO @table_variable` sink 除外
- `MERGE ... WHEN NOT MATCHED BY SOURCE`

## 对外输出规则

- `sqlparser_deparse()` 输出 SQL Server 公共形态，不暴露内部转换细节。
- `@name` 和 `?` 参数在 deparse 和 View JSON 中保持公共形态，不输出内部 `$1`、`$2`。
- `@@` 系统变量和 `0x...` 二进制字面量在 deparse 中保持 SQL Server 公共形态。
- SQL Server 风格转换函数在 deparse 输出中保持 `TRY_CAST`、`TRY_CONVERT`、`CONVERT`、`PARSE` 或 `TRY_PARSE`。
- `TOP`、`TOP ... PERCENT`、`TOP ... WITH TIES` 和 `OFFSET ... FETCH` 在 deparse 输出中保持 SQL Server 语法。
- 表提示、`FOR JSON` 后缀和查询提示以原始公开片段恢复到 deparse 输出；View JSON 不定义独立的结构化 hint 或 JSON 后缀字段。
- `N'...'` Unicode 字符串在可保留语义的场景中输出 `N` 前缀。
- View JSON 中可归属的表达式片段使用公共 SQL Server 形态。
- 控制流条件和分支 SQL 作为有序 statement unit 输出；View JSON 的 `control_flow` 与公共控制流只读结构一致。
- 失败的表达式片段改写不会提交到 handle；原有 AST、参数映射和 deparse 输出保持可用。

## 回归用例

SQL Server 支持范围以以下文件为准：

- `tests/cases/sqlserver_dialect_input.json`
- `tests/cases/sqlserver_dialect_matrix.md`
- `tests/unit/test_sqlserver_dialect_case_matrix.c`
- `tests/unit/test_stability.c`

当前 SQL Server 矩阵包含 546 条用例：517 条支持路径，29 条明确错误或不支持路径。
