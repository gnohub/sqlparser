# SQL Server 方言用例矩阵

本文件记录 SQL Server 方言转换层的回归用例。可执行夹具为 `tests/cases/sqlserver_dialect_input.json`，单元测试 `tests/unit/test_sqlserver_dialect_case_matrix.c` 会逐条验证解析结果、SQL View JSON、反解析输出和错误码。

## 支持用例

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| S001 | 方括号标识符 + `@` 参数 | `[schema].[table]`、列名、条件参数转换与还原 |
| S002 | `SELECT TOP (n)` | `TOP` 到核心 `LIMIT` 的双向转换 |
| S003 | `OFFSET ... FETCH` | SQL Server 分页语法 |
| S004 | CTE | `WITH` 查询、CTE 名称和内层条件列识别 |
| S005 | `JOIN` + 参数 | 多表 JOIN、关联列、条件列和参数还原 |
| S006 | `LEFT JOIN` | 外连接、别名和关联列识别 |
| S007 | `INSERT ... VALUES` + 参数 | 插入列识别和 `@` 参数还原 |
| S008 | 多行 `INSERT ... VALUES` | 多行值列表 |
| S009 | `INSERT ... SELECT` | 目标表、来源表、插入列和查询列识别 |
| S010 | `UPDATE` + 多赋值 | 更新列、条件列和参数还原 |
| S011 | `DELETE` + 条件 | 条件删除 |
| S012 | `N'...'` Unicode 字符串 | Unicode 字符串前缀保留 |
| S013 | JDBC `?` 参数 | 位置参数转换与还原 |
| S014 | 临时表 | `#temp` 表名转换 |
| S015 | 常见函数 | `ISNULL`、`GETDATE`、`NEWID` |
| S016 | `ROW_NUMBER() OVER` | 窗口函数 |
| S017 | `CASE` 表达式 | 条件表达式 |
| S018 | `UNION ALL` | 集合查询 |
| S019 | `EXCEPT` | 集合查询 |
| S020 | `INTERSECT` | 集合查询 |
| S021 | `CREATE TABLE` + `IDENTITY` | 建表语句、常见类型和自增列属性 |
| S022 | `CREATE VIEW` | 视图定义 |
| S023 | `ALTER TABLE ... ADD` | 添加列 |
| S024 | `CREATE INDEX` | 创建索引 |
| S025 | `DROP TABLE IF EXISTS` | 删除表 |
| S026 | `TRUNCATE TABLE` | 清空表 |
| S027 | 事务控制 | `BEGIN TRANSACTION`、`COMMIT TRANSACTION` |
| S028 | `SAVE TRANSACTION` | savepoint 兼容映射 |
| S029 | `GRANT` / `REVOKE` | 授权语句 |
| S030 | `GO` 分隔符 | 批处理分隔符转换为多语句 |
| S031 | 数值与时间类型 | `BIGINT`、`DECIMAL`、`DATETIME2` |
| S032 | `IN` + 多参数 | 条件列表中的多个 `@` 参数 |
| S033 | `CAST(... AS DATE)` | 类型转换表达式 |
| S034 | 带空格的标识符 | 方括号标识符中的空格 |
| S035 | `MERGE` | 基础合并语句 |
| S036 | `TOP (@param)` | `TOP` 表达式中的参数转换与还原 |
| S037 | CTE + 主查询 `TOP` | `TOP` 回写到主查询 `SELECT` |
| S038 | 重复 Unicode 字面量 | 只为原始 `N'...'` 字符串恢复 Unicode 前缀 |
| S039 | `@@` 系统变量 | 系统变量公共形态保留 |
| S040 | `0x...` 二进制字面量 | 二进制字面量公共形态保留 |
| S041 | `CONVERT(..., style)` | SQL Server 风格转换函数双向映射 |
| S042 | unsupported 关键字字符串 | 字符串中的 `OUTPUT`、`@table`、`EXEC` 不触发 unsupported |
| S043 | unsupported 关键字注释 | 注释中的 `OUTPUT` 不触发 unsupported |
| S044 | `USE [database]` | 数据库上下文切换、方括号数据库名和公开 value 片段 |
| S045 | `USE database` | 官方 `USE database_name` 基础形态 |
| S046 | `USE ...; SELECT ...` | 多语句中的数据库切换和后续查询保持独立输出 |
| S047 | `EXEC sp_prepare` | 预编译参数化语句，保留 handle、参数定义和 SQL 文本 |
| S048 | `EXEC sp_execute` | 通过 prepared handle 执行并保留绑定参数 |
| S049 | `EXEC sp_prepexec` | prepare + execute 合并调用，保留 SQL 文本和绑定参数 |
| S050 | `EXEC sp_unprepare` | prepared handle 释放语句 |
| S051 | `EXEC sp_executesql` | 参数化动态 SQL 执行，保留 SQL 文本、参数定义和参数值 |
| S052 | 多 `@` 参数查询 | 查询条件中的多个命名参数 |
| S053 | `IN` + 多 `@` 参数 | 条件列表中的多个命名参数 |
| S054 | `TOP (@param)` + `LIKE @param` | TOP 和条件表达式中的参数还原 |
| S055 | `INSERT ... VALUES` + 多 `@` 参数 | 插入列和命名参数值列表 |
| S056 | 多行 `INSERT ... VALUES` + `?` | 多行 JDBC 风格参数化插入 |
| S057 | `UPDATE ... SET ... WHERE ... = ?` | 更新列、条件列和位置参数 |
| S058 | `DELETE ... WHERE ... = ?` | 条件删除和位置参数 |
| S059 | `EXEC sp_prepare` + INSERT | prepared insert SQL 文本和参数定义 |
| S060 | `EXEC sp_execute` + 命名值 | prepared handle 执行和命名参数值 |
| S061 | `EXEC sp_executesql` + UPDATE | 参数化动态 UPDATE SQL 文本和参数值 |
| S062 | `TOP` + 直接字段 + `@` 参数 | TOP 查询、直接输出字段、WHERE bind 和 ORDER BY 归属 |
| S063 | `+` 表达式输出 | 普通表达式 `target_path`、操作符和输出项归属 |
| S064 | `CASE` 表达式输出 | `CASE WHEN` 中字段的 `target_path` 归属 |
| S065 | `GROUP BY` + `HAVING` + `ORDER BY` | 聚合输出和非输出子句字段归属 |
| S066 | `UPDATE` + 多 `@` 参数 | update/where 子句、bind 字段和空 value |
| S067 | `BETWEEN` + 多 `@` 参数 | `BETWEEN` 条件中的多个命名参数和字段值关联 |
| S068 | `NOT IN` + 多 `@` 参数 | 否定 `IN` 条件中的多个命名参数和字段值关联 |
| S069 | `NOT BETWEEN` + 多 `@` 参数 | 否定 `BETWEEN` 条件中的多个命名参数和字段值关联 |
| S070 | `NOT LIKE` + 多 `@` 参数 | 否定 `LIKE` 条件中的命名参数、字段级 operator 和关键字归属 |
| S071 | `DISTINCT` + `LIKE` 参数 | DISTINCT 投影、LIKE 命名参数和字段归属 |
| S072 | `DELETE ... IN` + 多 `@` 参数 | 条件删除、集合参数和字段 operator |
| S073 | `UPDATE ... EXISTS` | 子查询条件、相关字段和 SET 参数 |
| S074 | 无列名 `INSERT` | 无列名插入、行 cell、位置参数和空列名输出 |
| S075 | 派生表过滤 | 内外层 WHERE、派生表 alias 和命名参数 |
| S076 | `JSON_VALUE` 投影 | SQL Server JSON 函数和 WHERE 参数 |
| S077 | `ORDER BY 1` | 数字排序项和投影顺序相关语法 |
| S078 | `OFFSET/FETCH` + `@` 参数 | 分页子句中的命名参数 |
| S079 | `LEFT JOIN` + `alias.*` | 限定星号、JOIN/ON 字段和 WHERE 参数 |
| S080 | `CREATE VIEW` + JOIN 聚合 | 视图创建、JOIN 条件和 GROUP BY 聚合 |

## 明确不支持用例

以下语法具有 SQL Server 专属语义，当前不会尝试映射为 PostgreSQL AST。转换层返回 `SQLPARSER_STATUS_UNSUPPORTED`，避免生成语义不可靠的 SQL。

| ID | 用例 | 原因 |
| --- | --- | --- |
| SU001 | `TOP ... PERCENT` | 百分比限制语义不能等价映射 |
| SU002 | `TOP ... WITH TIES` | 同分保留语义不能等价映射 |
| SU003 | `OUTPUT` | DML 输出流语义需要 SQL Server 专用模型 |
| SU004 | 表提示 | 表访问提示不能安全降级 |
| SU005 | `CROSS APPLY` | APPLY 语义不同于普通 JOIN |
| SU006 | `PIVOT` | 表变换语义需要专用 AST |
| SU007 | `FOR JSON` | 结果格式化语义不属于当前结构模型 |
| SU008 | `OPTION (...)` | 查询提示不能安全降级 |
| SU009 | `DECLARE` | 变量声明属于 T-SQL 批处理语义 |
| SU010 | 普通过程 `EXEC` | 非 prepared/dynamic SQL 系统过程调用超出 SQL 结构改写范围 |
| SU011 | `CREATE PROCEDURE` | 过程定义需要 T-SQL 程序单元模型 |
| SU015 | 表变量 | 表变量作用域属于 T-SQL 批处理语义 |
| SU016 | `MERGE ... BY SOURCE` | SQL Server 专属 merge 分支语义 |
| SU017 | `TOP` + `OFFSET/FETCH` | SQL Server 不允许在同一查询作用域组合使用 |
| SU018 | 嵌套 `TOP` | 当前只支持顶层 `SELECT TOP` 的安全双向映射 |

## 官方 `HOOK_ONLY` 覆盖用例

`tests/cases/sqlserver_hook_coverage_input.json` 按 `doc/sqlserver_official_syntax_coverage.csv` 中的 `HOOK_ONLY` 条目生成，当前包含 235 条用例。该夹具覆盖函数、类型/常量、排序规则和简单 `RENAME OBJECT` 等可通过现有 AST 与方言 hook 承载的官方条目。

## 维护要求

- 新增 SQL Server 支持项必须同步更新 `tests/cases/sqlserver_dialect_input.json`、`tests/cases/sqlserver_hook_coverage_input.json`、本矩阵和可执行回归测试。
- 无法保证语义等价的 SQL Server 专有语法必须返回 `SQLPARSER_STATUS_UNSUPPORTED`。
