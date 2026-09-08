# KingbaseES PostgreSQL 兼容入口用例矩阵

可执行夹具为 `tests/cases/kingbase_postgresql_dialect_input.json`，由独立的 KingbaseES PostgreSQL 入口和 case runner 验证。

## 审计基线

- 语法基线是 V8R6 与 V9R1 官方资料的并集：`V008R006C009B0014` 与 `V009R001C002B0014`。[V8 发布版本汇总](https://help.kingbase.com.cn/v8/intro/releasenotes-external/summary.html)、[V8 手册下载](https://help.kingbase.com.cn/v8/download.html)、[V9 手册下载](https://help.kingbase.com.cn/v9/download.html)、[V9 SQL 语言参考](https://help.kingbase.com.cn/v9/development/sql-plsql/sql/index.html)。
- `kingbase-postgresql` 入口不接收服务端版本参数；解析时不判断 SQL 来自 V8 还是 V9。
- PostgreSQL 兼容模式通过 `initdb -m pg` 或 `initdb --dbmode=pg` 初始化。当前 V8 文档列出的模式为 `pg`、`oracle`、`mysql`，并提供 `0`、`1`、`2` 数字别名：[initdb](https://help.kingbase.com.cn/v8/admin/reference/ref-server/initdb.html)。
- “标准版”是 License 产品版本，不是独立 SQL 兼容模式：[KingbaseES License 信息手册](https://help.kingbase.com.cn/v8/PDF/KingbaseES_License%E4%BF%A1%E6%81%AF%E6%89%8B%E5%86%8C.pdf)。因此本套件使用 `kingbase-postgresql`，不另建 `kingbase-standard`。
- V8 的 `dbmode` 没有 `sqlserver`；SQLServer 兼容入口单独以 V9R4C12 官方资料为依据，不属于本 fixture。[SQLServer 兼容性总览](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/kes-vs-sqlserver/overview.html)。

## 夹具状态

- 45 条用例全部为 `status = "final"`。
- 185 个独立 patch 随原 PostgreSQL 用例保留。
- 2 条用例保留完整 `bind_occurrences` 断言。
- 所有条目均选自 `tests/cases/sql_batch_input.json`；除增加 `kingbase-postgresql-` 名称前缀，并移除来源用例中显式固定为 `postgresql` 的 `dialect` override 外，SQL、状态、期望 View、patch 和 bind occurrence 不变。
- `KBPG001` 至 `KBPG045` 按 fixture 的 `items` 数组顺序进行 1-based 映射。
- Runner：`tests/unit/test_kingbase_postgresql_dialect_case_matrix.c`。

## 官方依据与覆盖

官方 V8/V9 SQL 手册将 Oracle/MySQL 专用能力单独标注。本套件只采用公共语法以及具有明确 PostgreSQL 模式依据的语法，不从其他数据库产品反推能力。V9R1 新增的占位符表达式章节进一步确认了接口 SQL 中的 bind 表达式边界；现有 45 条最小集合已经覆盖对应表达式、DML 和 CTE 结构，因此未增加同类重复 case：[V9 表达式](https://help.kingbase.com.cn/v9/development/sql-plsql/sql/Expressions.html)。

| 分类 | 覆盖用例 | 官方依据 | 覆盖边界 |
| --- | --- | --- | --- |
| 词法与定界标识符 | `KBPG014`-`KBPG015`、`KBPG037`-`KBPG038` | [SQL 词汇约定](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/changes.html)、[SQL 基本元素](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/datatype.html) | 单引号字符串、字符串内分号、美元引用字符串、双引号 schema/table/column；不使用反引号或方括号标识符 |
| 数据类型、转换、数组与表达式 | `KBPG017`、`KBPG030`-`KBPG033`、`KBPG039`-`KBPG041`、`KBPG045` | [SQL 基本元素](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/datatype.html)、[表达式](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Expressions.html)、[操作符](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Operators.html)、[条件表达式](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Conditions.html)、[函数](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Function.html) | `BIGINT/VARCHAR/TEXT/JSONB/TIMESTAMP`、`ARRAY`、`ROW`、`CASE`、`||`、`ILIKE ... ESCAPE`、`UPPER/LOWER/COALESCE/SUM/COUNT` |
| SELECT、JOIN、子查询、集合与分页 | `KBPG001`-`KBPG004`、`KBPG016`-`KBPG018`、`KBPG030`-`KBPG032`、`KBPG034`-`KBPG038`、`KBPG043`-`KBPG045` | [查询和子查询](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Queries_and_Subqueries.html)、[SELECT](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | JOIN/ON、WHERE、GROUP BY、HAVING、WINDOW、派生表、标量/EXISTS 子查询、`UNION ALL`、`INTERSECT`、ORDER BY、LIMIT/OFFSET、显式列名和递归 CTE |
| INSERT 与冲突处理 | `KBPG005`-`KBPG007`、`KBPG019`-`KBPG020`、`KBPG027`、`KBPG039`、`KBPG042` | [SQL 语句快速参考](https://help.kingbase.com.cn/v8/PDF/KingbaseES_SQL%E8%AF%AD%E8%A8%80%E5%BF%AB%E9%80%9F%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C.pdf)、[INSERT/ON CONFLICT/RETURNING](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_9.html) | 单行/多行 VALUES、INSERT SELECT、`ON CONFLICT DO UPDATE`、RETURNING、表达式值和 data-modifying CTE |
| UPDATE 与 DELETE | `KBPG008`、`KBPG021`-`KBPG022`、`KBPG028`-`KBPG029`、`KBPG039`、`KBPG042` | [UPDATE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html)、[DELETE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_6.html) | 多 assignment、UPDATE FROM、DELETE USING、RETURNING、bind 和表达式右值 |
| MERGE | `KBPG023` | [MERGE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | V8 文档明确给出的 `MERGE INTO ... USING ... ON ... WHEN MATCHED THEN UPDATE ... WHEN NOT MATCHED THEN INSERT ... VALUES ...` 基础形态 |
| 核心 relation DDL | `KBPG009`-`KBPG012`、`KBPG033` | [ALTER TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_3.html)、[CREATE INDEX](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_4.html)、[CREATE VIEW](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_5.html)、[CREATE TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_6.html)、[DROP TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_9.html) | CREATE TABLE、DROP TABLE、CREATE VIEW、ALTER TABLE ADD COLUMN、CREATE INDEX |
| 事务与预备语句 | `KBPG013`、`KBPG024` | [PREPARE 与事务语句](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | BEGIN/COMMIT 与带 `$1` 的 PREPARE |
| `$n` bind | `KBPG020`、`KBPG024`-`KBPG029`、`KBPG034`-`KBPG035`、`KBPG037`-`KBPG041`、`KBPG045` | [PREPARE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html)、[Go 接口准备语句和绑定](https://help.kingbase.com.cn/v8/development/client-interfaces/go/go-3.html) | `$1`、`$2` 等位置参数在 SELECT/INSERT/UPDATE/DELETE、分页、表达式和预备语句中的顺序与 occurrence；不把美元引用内容识别为 bind |

## 明确未纳入

- V8/V9 不存在独立“标准 SQL 模式”，因此不复制一套与 PG 相同的 `kingbase-standard` case。
- 不在本 fixture 纳入 SQLServer `TOP`、`@variable`、方括号标识符或 PL/MSSQL 控制语句；这些由 `kingbase-sqlserver` 入口单独承载。
- 不纳入 Oracle 专用的 `INSERT ALL/FIRST`、Oracle 外连接和层次查询扩展。
- 不纳入 MySQL 专用的反引号、`INSERT ... SET`、`ON DUPLICATE KEY UPDATE`、`REPLACE` 和多表 DML。
- 不纳入 `?` 或 `:name` 客户端占位符。本套件只以服务端 PREPARE 和官方接口均明确记录的 `$n` 为基线。
- 不纳入 `database.schema.table` 三段限定名；V8 对象引用文档的基础形式是 `[schema.]object[.part]`。
- 不纳入 `WHEN NOT MATCHED BY SOURCE`、MERGE RETURNING 等未在所选 KingbaseES PostgreSQL 官方 MERGE 语法中出现的形态。
- 不纳入仅在 Oracle/MySQL 模式条目下列出的 `CONCAT` 变体；PG 模式适用性需要真实服务端验证后再决定。

## View 与 patch 合同

官方资料只证明 SQL 形态。fixture 中的 `query_graph`、selector、patch、bind occurrence 和原文逐字节保持属于 sqlparser 项目合同，不能反向解释为 KingbaseES 服务端公开接口。
