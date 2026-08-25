# Vastbase SQL Server 兼容模式用例矩阵

可执行夹具为 `tests/cases/vastbase_sqlserver_dialect_input.json`。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。

## 事务特征规范语义值回归

以下 4 条 final 用例覆盖 Vastbase SQL Server 兼容模式的常用事务隔离级别和访问模式。原始 SQL 反解析必须逐字节保持；View 必须按输入顺序输出去除 trivia 后的规范关键字值。session 语义值不提供 selector，因此这些用例不设置 patch。

| ID | 用例 | SQL | 验证重点 |
| --- | --- | --- | --- |
| `VS-TX001` | `vastbase-sqlserver-session-transaction-commented-read-uncommitted` | ALTER/*command*/SESSION SET TRANSACTION ISOLATION/*name*/LEVEL READ/*value*/UNCOMMITTED; | `READ UNCOMMITTED` 规范值及单事务特征 |
| `VS-TX002` | `vastbase-sqlserver-session-characteristics-commented-repeatable-read-write` | ALTER SESSION SET SESSION/*scope*/CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE/*value*/READ, READ/*mode*/WRITE; | `REPEATABLE READ`、`READ WRITE` 及 session characteristics 入口 |
| `VS-TX003` | `vastbase-sqlserver-session-transaction-commented-serializable-read-only` | ALTER SESSION SET TRANSACTION ISOLATION LEVEL SERIALIZABLE/*tail*/, READ/*mode*/ONLY; | `SERIALIZABLE`、`READ ONLY` 及逗号前注释 |
| `VS-TX004` | `vastbase-sqlserver-session-transaction-commented-option-order` | ALTER SESSION SET TRANSACTION read/*mode*/write, ISOLATION/*name*/LEVEL read/*value*/committed; | 输入选项顺序、小写原文保留及 `READ COMMITTED` 规范值 |

## 矩阵统计与 session 回归

夹具包含 619 条 `status = "final"` 用例和 1884 个独立 patch，其中 75 条用例的期望 View 包含非空 session 投影。

View 校验采用 JSON 结构相等比较，对象键顺序和格式空白不参与比较；session action、item scope、target kind、name、value 类型、规范文本及顺序均属于比较范围。

## MERGE INSERT 独立列值改写回归

以下 final 用例定义项目 `vastbase-sqlserver` 兼容入口合同，不声称 Vastbase 服务端官网定义了相同语法范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VSH607` | `vastbase-sqlserver-merge-omitted-insert-column-value-independent` | final | 3 | 省略目标列清单时输出 `target_list_selector`；分别验证仅增加目标列并物化清单、仅增加 VALUES cell 且保持清单省略，以及替换已有 cell |

## 定界别名状态回归

`vastbase-sqlserver-graph-quoted-relation-alias-and-target-output` 及其 2 个 output alias patch 验证 Query Graph 字段合同：relation alias 的精确来源 token 使用方括号时输出 `alias_quoted_identifier: true`；target 的 `output_name` 来源于带方括号的显式 alias，或无显式 alias 时来源于带方括号的直接字段 token，则输出 `output_quoted_identifier: true`。未定界来源不输出对应字段。该合同属于项目兼容入口，不代表 Vastbase 服务端官方语法范围。

## 定界关系分段与 DML 列状态回归

以下 2 条 final 用例以同名定界/未定界标识符对照验证三段 relation 与 DML 列状态；14 个独立 patch 覆盖普通 DML、MERGE INSERT、`OUTPUT ... INTO` sink relation 和 sink column，且要求 patch handle 与重新解析 handle 的标志一致。该组用例定义项目 `vastbase-sqlserver` 兼容入口合同，不声称 Vastbase 服务端官网定义了相同语法范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VSH608` | `vastbase-sqlserver-quoted-identifier-three-part-dml-matrix` | final | 7 | SELECT、INSERT、UPDATE FROM、DELETE 与 MERGE target/source 覆盖 `database_quoted_identifier`、`schema_quoted_identifier`、relation `quoted_identifier` 和 DML column `quoted_identifier`；relation 与 MERGE 列改写重新计算状态 |
| `VSH609` | `vastbase-sqlserver-output-into-quoted-identifier-sink-matrix` | final | 7 | INSERT、UPDATE、DELETE、MERGE 的 `OUTPUT ... INTO` sink relation 三段状态及 `sink_columns[].quoted_identifier`；逐个 sink relation/column patch 与普通 DML relation patch 保持 fresh View 一致 |

## DDL relation 投影回归

以下 10 条 final 用例定义项目 `vastbase-sqlserver` 兼容入口的 DDL Query Graph 合同：DDL 根块使用 `kind = "ddl"`，relation 以 `ddl_role = "target"` 或 `"reference"` 区分操作对象与外键引用对象，并保留 database/schema/object 来源分段的方括号定界状态。CREATE VIEW 和 `SELECT INTO` 将 DDL target 的 `source_block` 指向独立 SELECT 块。语法形态以夹具中已验证的 SQL Server 兼容语法为边界；该合同属于项目兼容入口，不声称 Vastbase 服务端官网定义了相同范围。

| ID | 用例 | 状态 | 独立 patch | 验证重点 |
| --- | --- | --- | ---: | --- |
| `VSH610` | `vastbase-sqlserver-ddl-graph-create-table-fk-quoted-segments` | final | 2 | 三段 CREATE TABLE target 与 FK reference，方括号定界分段和 selector 独立 |
| `VSH611` | `vastbase-sqlserver-ddl-graph-alter-table-fk-quoted-segments` | final | 2 | ALTER TABLE target 与 FK reference 的三段投影及独立改写 |
| `VSH612` | `vastbase-sqlserver-ddl-graph-create-index-three-part-target` | final | 1 | `CREATE INDEX ... ON` 的三段表作为 DDL target，index 名不伪装为 relation |
| `VSH613` | `vastbase-sqlserver-ddl-graph-truncate-three-part-target` | final | 1 | TRUNCATE TABLE 三段 target 的定界状态及 relation patch |
| `VSH614` | `vastbase-sqlserver-ddl-graph-drop-table-multi-target` | final | 0 | DROP TABLE 多目标按源码顺序投影，不伪造 selector |
| `VSH615` | `vastbase-sqlserver-ddl-graph-drop-view-multi-target` | final | 0 | DROP VIEW 多目标按源码顺序投影，完整保留 schema/object 定界状态 |
| `VSH616` | `vastbase-sqlserver-ddl-graph-create-view-target-source-block` | final | 2 | CREATE VIEW target、SELECT 来源与 `source_block` 关联 |
| `VSH617` | `vastbase-sqlserver-ddl-graph-select-into-target-source-block` | final | 2 | `SELECT INTO` 目标作为 DDL target，FROM relation 保留在独立 SELECT 块，两侧 selector 均可改写 |
| `VSH618` | `vastbase-sqlserver-ddl-multistatement-surface-relation-patch` | final | 2 | CREATE INDEX/TRUNCATE TABLE 普通双语句 batch 的 relation patch 不增加 `USING btree`，不丢失 `TABLE` |
| `VSH619` | `vastbase-sqlserver-ddl-drop-table-quoted-same-spelling` | final | 0 | quoted/unquoted 同名 DROP target 无 selector，schema/object 方括号状态按精确来源 token 分别输出 |

## 完整绑定占位符 occurrence 回归

以下 2 条 final 用例定义项目 `vastbase-sqlserver` 兼容入口的 handle 级 occurrence 合同，不作为 Vastbase 服务端官方能力声明。runner 对输入及每个 patch 后的公开 SQL 逐项断言 `position`、`kind`、`key` 和 `sql`；重复 bind 不合并，跨 `GO` 编号不重置，字符串、注释、方括号标识符、`@@` 系统变量和 OUTPUT sink relation 不计入。

| 用例 | 根 occurrence | Patch | 基础入口关系 | 验证重点 |
| --- | ---: | ---: | --- | --- |
| `vastbase-sqlserver-multi-statement-global-bind-position` | 7 | 5 | 除用例名外，逐字段镜像 `sqlserver-multi-statement-global-bind-position` | 跨 `GO` 的 UPDATE 与 MERGE，命名/匿名 bind 和重复 key；复杂改写覆盖子查询、OFFSET/FETCH、CAST、CASE 及保护区排除 |
| `vastbase-sqlserver-update-output-into-eight-target-sink-column-pairs` | 3 | 1 | 除用例名外，逐字段镜像 `sqlserver-update-output-into-eight-target-sink-column-pairs` | `@profile_audit` sink relation 不计入；OUTPUT target 成对插入后表达式中的重复 `@audit_tag` 与匿名 `?` 按源码顺序进入列表 |

## SQL/JSON 语义输入回归

以下用例验证 Vastbase-SQLServer 兼容入口中 SQL/JSON 专用 AST 节点的输入字段遍历。字段顺序、relation 与 target 归属、原始 name selector 以及嵌套 `target_path` 均为精确 View 合同；每条用例还覆盖字段或关系替换、target 替换、列插入及 patch 后精确反解析。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| VSH404 | `vastbase-sqlserver-json-array-field-inputs` | `JSON_ARRAY([id], [name])` | 多字段按参数顺序进入 `fields`，分别归属 `JSON_ARRAY` arg 0 和 arg 1 |
| VSH405 | `vastbase-sqlserver-json-object-multi-pair-nested-value` | 多 pair `JSON_OBJECT` 嵌套 `JSON_VALUE` | value 字段按扁平 key/value 槽位归属，嵌套输入保留两层 `target_path` |
| VSH406 | `vastbase-sqlserver-json-arrayagg-expression-input` | `JSON_ARRAYAGG(COALESCE(...))` | 聚合输入内字段按表达式顺序输出并保留两层函数路径 |
| VSH407 | `vastbase-sqlserver-json-value-qualified-multi-target` | qualified `JSON_VALUE` 与普通 target 并列 | relation alias、target 顺序、限定字段 selector 和 JSON 输入字段归属 |

## Vastbase SQL Server 兼容入口函数参数角色回归

以下用例验证兼容入口中函数参数的语法角色与数据字段角色严格分离。`IDENTITY` 的数据类型参数以及日期函数的 datepart 参数不进入 `query_graph.fields`；其余表达式参数按原始顺序保留 relation、target、selector 和 `target_path.arg_index`。既有 `VSH047`、`VSH053` 分别覆盖三参数 `DATE_BUCKET` 和 `DATETRUNC`，本节补充可选 origin、表达式参数和同族函数组合。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| VS113 | `vastbase-sqlserver-identity-function-default-seed-increment` | `IDENTITY(int)` + `SELECT INTO` | 数据类型参数不作为字段输出 |
| VS114 | `vastbase-sqlserver-identity-function-explicit-seed-increment` | `IDENTITY(int, 1, 1)` + `SELECT INTO` | 数据类型参数不作为字段输出，seed 与 increment 保持常量角色 |
| VSH408 | `vastbase-sqlserver-date-bucket-optional-origin-inputs` | 四参数 `DATE_BUCKET` | 日期输入为 arg 2，可选 origin 为 arg 3，datepart 不作为字段输出 |
| VSH409 | `vastbase-sqlserver-dateadd-expression-inputs` | `DATEADD` + 表达式参数 | number 与 date 输入分别保留 arg 1、arg 2，datepart 不作为字段输出 |
| VSH410 | `vastbase-sqlserver-datediff-family-expression-inputs` | `DATEDIFF` 与 `DATEDIFF_BIG` 并列 | 两个 target 的四个日期输入保持各自归属，datepart 不作为字段输出 |
| VSH411 | `vastbase-sqlserver-datename-datepart-role` | `DATENAME` | date 输入保留 arg 1，datepart 不作为字段输出 |
| VSH412 | `vastbase-sqlserver-datepart-datepart-role` | `DATEPART` | date 输入保留 arg 1，datepart 不作为字段输出 |

## Vastbase SQL Server 兼容入口 WITHIN GROUP 聚合排序回归

以下用例验证兼容入口中聚合函数直接参数、`WITHIN GROUP` 排序表达式和窗口分区字段按各自语义位置进入 View。排序字段的 `target_path.arg_index` 从函数直接参数数量开始连续编号；窗口字段继续使用独立的 `window_partition` 路径。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| VSH413 | `vastbase-sqlserver-string-agg-within-group-multi-sort` | `STRING_AGG` + 多排序字段 | 直接输入与两个排序字段按顺序输出，排序字段归属 arg 2、arg 3 |
| VSH414 | `vastbase-sqlserver-string-agg-within-group-order-expression` | 排序加法表达式 + 第二排序项 | 表达式字段保持嵌套路径，第二排序项归属 arg 3 |
| VSH415 | `vastbase-sqlserver-approx-percentile-multi-target` | 两个 approximate percentile target | 相同排序列分别归属 target 0、target 1 和对应函数 arg 1 |
| VSH416 | `vastbase-sqlserver-percentile-cont-window-partition-boundary` | `WITHIN GROUP` + `OVER (PARTITION BY ...)` | 聚合排序字段与窗口分区字段独立输出且不重复 |

## Vastbase SQL Server 兼容入口 OUTPUT 末尾边界回归

以下用例要求兼容入口的 `OUTPUT` 恢复器仅在存在后继子句时保留必要边界；语句末尾不得增加空格，末尾分号必须直接跟随最后一个 OUTPUT target。每条用例均独立执行 target replace 与 target insert patch，并精确比较反解析结果。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| VSH420 | `vastbase-sqlserver-merge-output-action-terminal-semicolon` | `MERGE ... OUTPUT $action;` | MERGE 末尾 OUTPUT 无尾空格，分号位置不变 |
| VSH421 | `vastbase-sqlserver-update-output-terminal` | `UPDATE ... OUTPUT INSERTED.a` | 无后继 WHERE/FROM 时不生成右边界 |
| VSH422 | `vastbase-sqlserver-delete-output-terminal` | `DELETE ... OUTPUT DELETED.id` | 无后继 WHERE/FROM 时不生成右边界 |

## Vastbase SQL Server 兼容入口基础 CONNECT BY 回归

该兼容入口只覆盖基础 `CONNECT BY` 条件，不包含 `START WITH`、`PRIOR`、`NOCYCLE` 或 `CONNECT_BY_ROOT`。以下 final 用例包含 5 个独立 patch，其中 4 个 `replace`、1 个 `insert_column`。
在该查询块内，无显式 `AS` 的 `CONNECT_BY_ROOT expr` 形态按边界外层次操作符拒绝；显式 `AS` 别名或定界标识符保留同名普通字段语义。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| VSH423 | `vastbase-sqlserver-connect-by-official` | `WHERE` + 基础 `CONNECT BY` + `ORDER BY` | `connect_by` 字段和值归属、WHERE 与 ORDER BY 独立语义、relation/field/value/target 替换及 SELECT 列插入 |

## Vastbase SQL Server 兼容入口 OUTPUT target/sink column 成对插入回归

成对 `insert_column` 只适用于 sink `OUTPUT ... INTO` 通道包含显式、非空 sink column list，且改写前 OUTPUT target 数与 sink column 数严格相等的场景。操作按同一序号原子插入一个 OUTPUT target 和一个 sink column。原本合法的不等长 `OUTPUT` 仍可解析和反解析，但不支持这个成对插入操作；client `OUTPUT` 和未显式列出 sink column 的 `OUTPUT ... INTO` 也不纳入该成对改写边界。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| VSH424 | `vastbase-sqlserver-insert-output-into-eight-target-sink-column-pairs` | INSERT 8 OUTPUT target ↔ 8 显式 sink column | 头部原子插入 1 组后为 9↔9，View 与反解析保持按序配对 |
| VSH425 | `vastbase-sqlserver-update-output-into-eight-target-sink-column-pairs` | UPDATE 8 OUTPUT target ↔ 8 显式 sink column | 中部原子插入 1 组后为 9↔9，View 与反解析保持按序配对 |
| VSH426 | `vastbase-sqlserver-delete-output-into-eight-target-sink-column-pairs` | DELETE 8 OUTPUT target ↔ 8 显式 sink column | 尾部原子插入 1 组后为 9↔9，View 与反解析保持按序配对 |

## 嵌套 UPDATE 赋值列表回归

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| VSH427 | `vastbase-sqlserver-nested-update-output-assignment-contract` | `INSERT ... SELECT` 内嵌带 `OUTPUT` 的 `UPDATE` | `assignment[D][A]` 定位 D1 双赋值；插入、整项替换和删除 3 个 patch 保持嵌套与 `OUTPUT` |

## INSERT VALUES 回归：bind 与表达式混合

`VS-BM001` 至 `VS-BM010` 覆盖 Vastbase SQL Server 兼容模式下 VALUES cell 中 `DEFAULT`、`NULL`、bind、literal 和 expression 的组合，以及两行 VALUES 输入。数据库侧执行 `@name` 参数语法时须具备对应变量或参数作用域，并按部署要求启用 MSSQL 变量格式；`VS-BM010` 的方括号标识符还要求启用 MSSQL 方括号兼容。

`DEFAULT` 始终是独立 cell。每条用例逐 cell 断言 `row`、`column`、`kind` 和 `selector`；逐个直接 bind 断言 `bind_key`、`bind_kind`、`bind_sql`、`bind_position` 和 `selector`，并通过表达式后的直接 bind 位置验证嵌套 bind 已计入全局序号；`GETDATE()`、`CURRENT_TIMESTAMP` 断言为 `kind=expression`，且不得出现在 `query_graph.fields[].column` 中。

| ID | SQL 形态 | 边界 |
| --- | --- | --- |
| `VS-BM001` | 三个 `@` bind + 尾部 `GETDATE()` | `@` 引用要求已有变量/参数作用域 |
| `VS-BM002` | 首位 `GETDATE()` + 三个 `@` bind | 首列 expression 不得造成 bind 错位 |
| `VS-BM003` | `@` bind、`CURRENT_TIMESTAMP`、`@` bind、`GETDATE()` 交错 | 两种时间 expression 均不得出现在 `query_graph.fields[].column` 中 |
| `VS-BM004` | bind + `NULL` + `GETDATE()` + bind | `NULL` 为 literal，`GETDATE()` 为 expression |
| `VS-BM005` | bind + 独立 `DEFAULT` + `CURRENT_TIMESTAMP` + bind | `DEFAULT` 不嵌入其他表达式 |
| `VS-BM006` | bind + Unicode literal + `GETDATE()` + bind | literal 与 expression 不改变后续 bind 位置 |
| `VS-BM007` | 直接 bind + `COALESCE(@retry_count, 0)` + `GETDATE()` + 直接 bind | 尾部直接 bind 为 position 3，验证嵌套 bind 已计数 |
| `VS-BM008` | 直接 bind + 包含 `@enabled` 的 `CASE` 表达式 + `CAST(@amount AS int)` + `CURRENT_TIMESTAMP` + 直接 bind | 尾部直接 bind 为 position 4，验证两个嵌套 bind 均已计数 |
| `VS-BM009` | 两个 `@` bind + `NULL` + `CURRENT_TIMESTAMP` | 直接 bind、literal 和时间 expression 的相邻映射 |
| `VS-BM010` | schema-qualified 方括号标识符 + 不规则空白的两行 VALUES | 两行混合 `@` bind、独立 `DEFAULT`、literal、`GETDATE()` 和 `CURRENT_TIMESTAMP` |

## 支持用例

| ID | 用例 | SQL | 状态 |
| --- | --- | --- | --- |
| `VS001` | `vastbase-sqlserver-select-bracket-param` | SELECT [u].[id], [u].[name] FROM [dbo].[users] AS [u] WHERE [u].[id] = @id | 已覆盖 |
| `VS002` | `vastbase-sqlserver-select-top` | SELECT TOP (5) [id], [name] FROM [dbo].[users] ORDER BY [id] | 已覆盖 |
| `VS003` | `vastbase-sqlserver-offset-fetch` | SELECT [id] FROM [dbo].[users] ORDER BY [id] OFFSET 10 ROWS FETCH NEXT 5 ROWS ONLY | 已覆盖 |
| `VS004` | `vastbase-sqlserver-cte` | WITH active_users AS (SELECT [id], [name] FROM [dbo].[users] WHERE [status] = 'active') SELECT [id], [name] FROM active_users | 已覆盖 |
| `VS005` | `vastbase-sqlserver-join` | SELECT [u].[id], [o].[order_no] FROM [dbo].[users] [u] JOIN [dbo].[orders] [o] ON [u].[id] = [o].[user_id] WHERE [o].[status] = @status | 已覆盖 |
| `VS006` | `vastbase-sqlserver-left-join` | SELECT [u].[id], [p].[phone] FROM [dbo].[users] AS [u] LEFT JOIN [dbo].[phones] AS [p] ON [u].[id] = [p].[user_id] | 已覆盖 |
| `VS007` | `vastbase-sqlserver-insert-values-param` | INSERT INTO [dbo].[users] ([id], [name]) VALUES (@id, @name) | 已覆盖 |
| `VS008` | `vastbase-sqlserver-insert-values-multi-row` | INSERT INTO [dbo].[users] ([id], [name]) VALUES (1, 'bob'), (2, 'alice') | 已覆盖 |
| `VS009` | `vastbase-sqlserver-insert-select` | INSERT INTO [dbo].[archive_users] ([id], [name]) SELECT [id], [name] FROM [dbo].[users] WHERE [status] = 'inactive' | 已覆盖 |
| `VS010` | `vastbase-sqlserver-update-basic` | UPDATE [dbo].[users] SET [name] = @name, [status] = 'active' WHERE [id] = @id | 已覆盖 |
| `VS011` | `vastbase-sqlserver-delete-conditional` | DELETE FROM [dbo].[users] WHERE [id] = @id AND [status] = 'inactive' | 已覆盖 |
| `VS012` | `vastbase-sqlserver-unicode-string` | SELECT N'Bob''s order' AS label FROM [dbo].[orders] WHERE [customer_name] = N'Alice' | 已覆盖 |
| `VS013` | `vastbase-sqlserver-jdbc-question-params` | SELECT [id] FROM [dbo].[users] WHERE [id] = ? AND [status] = ? | 已覆盖 |
| `VS014` | `vastbase-sqlserver-temp-table` | SELECT [id] FROM #active_users WHERE [status] = 'active' | 已覆盖 |
| `VS015` | `vastbase-sqlserver-functions` | SELECT ISNULL([name], 'unknown') AS [name], GETDATE() AS [now_value], NEWID() AS [row_id] FROM [dbo].[users] | 已覆盖 |
| `VS016` | `vastbase-sqlserver-row-number` | SELECT ROW_NUMBER() OVER (PARTITION BY [status] ORDER BY [id]) AS rn, [id] FROM [dbo].[users] | 已覆盖 |
| `VS017` | `vastbase-sqlserver-case-expression` | SELECT CASE WHEN [status] = 'active' THEN 1 ELSE 0 END AS active_flag FROM [dbo].[users] | 已覆盖 |
| `VS018` | `vastbase-sqlserver-union-all` | SELECT [id] FROM [dbo].[users] UNION ALL SELECT [id] FROM [dbo].[archive_users] | 已覆盖 |
| `VS019` | `vastbase-sqlserver-except` | SELECT [id] FROM [dbo].[users] EXCEPT SELECT [id] FROM [dbo].[archive_users] | 已覆盖 |
| `VS020` | `vastbase-sqlserver-intersect` | SELECT [id] FROM [dbo].[users] INTERSECT SELECT [id] FROM [dbo].[active_users] | 已覆盖 |
| `VS021` | `vastbase-sqlserver-create-table-identity` | CREATE TABLE [dbo].[users] ([id] INT IDENTITY(1,1) PRIMARY KEY, [name] NVARCHAR(64), [active] BIT) | 已覆盖 |
| `VS022` | `vastbase-sqlserver-create-view` | CREATE VIEW [dbo].[v_users] AS SELECT [id], [name] FROM [dbo].[users] | 已覆盖 |
| `VS023` | `vastbase-sqlserver-alter-table-add` | ALTER TABLE [dbo].[users] ADD [age] INT | 已覆盖 |
| `VS024` | `vastbase-sqlserver-create-index` | CREATE INDEX [ix_users_name] ON [dbo].[users] ([name]) | 已覆盖 |
| `VS025` | `vastbase-sqlserver-drop-table` | DROP TABLE IF EXISTS [dbo].[users] | 已覆盖 |
| `VS026` | `vastbase-sqlserver-truncate-table` | TRUNCATE TABLE [dbo].[users] | 已覆盖 |
| `VS027` | `vastbase-sqlserver-transaction` | BEGIN TRANSACTION; UPDATE [dbo].[users] SET [status] = 'active' WHERE [id] = @id; COMMIT TRANSACTION | 已覆盖 |
| `VS028` | `vastbase-sqlserver-save-transaction` | SAVE TRANSACTION sp1 | 已覆盖 |
| `VS029` | `vastbase-sqlserver-grant-revoke` | GRANT SELECT ON [dbo].[users] TO [app_role]; REVOKE SELECT ON [dbo].[users] FROM [app_role] | 已覆盖 |
| `VS030` | `vastbase-sqlserver-go-separator` | SELECT [id] FROM [dbo].[users]<br>GO<br>SELECT [id] FROM [dbo].[orders] | 已覆盖 |
| `VS031` | `vastbase-sqlserver-numeric-types` | CREATE TABLE [dbo].[payments] ([id] BIGINT IDENTITY, [amount] DECIMAL(18,2), [created_at] DATETIME2) | 已覆盖 |
| `VS032` | `vastbase-sqlserver-in-list-params` | SELECT [id] FROM [dbo].[users] WHERE [id] IN (@id1, @id2, @id3) | 已覆盖 |
| `VS033` | `vastbase-sqlserver-cast-date` | SELECT CAST('2024-01-01' AS DATE) AS created_date FROM [dbo].[users] | 已覆盖 |
| `VS034` | `vastbase-sqlserver-quoted-identifier` | SELECT [Order Detail].[Order ID] FROM [dbo].[Order Detail] WHERE [Order Detail].[Order ID] = @order_id | 已覆盖 |
| `VS035` | `vastbase-sqlserver-merge-basic` | MERGE INTO [dbo].[users] AS [t] USING [dbo].[staging_users] AS [s] ON [t].[id] = [s].[id] WHEN MATCHED THEN UPDATE SET [name] = [s].[name] WHEN NOT MATCHED BY TARGET THEN INSERT ([id], [name]) VALUES ([s].[id], [s].[name]); | 已覆盖 |
| `VS036` | `vastbase-sqlserver-top-parameter` | SELECT TOP (@row_count) [id] FROM [dbo].[users] ORDER BY [id] | 已覆盖 |
| `VS037` | `vastbase-sqlserver-cte-top-main-select` | WITH latest_users AS (SELECT [id] FROM [dbo].[users] WHERE [status] = 'active') SELECT TOP (3) [id] FROM latest_users ORDER BY [id] | 已覆盖 |
| `VS038` | `vastbase-sqlserver-unicode-duplicate-literal` | SELECT 'same' AS ascii_value, N'same' AS unicode_value FROM [dbo].[users] | 已覆盖 |
| `VS042` | `vastbase-sqlserver-unsupported-keywords-in-string` | SELECT 'OUTPUT @table EXEC' AS label FROM [dbo].[users] | 已覆盖 |
| `VS043` | `vastbase-sqlserver-unsupported-keywords-in-comment` | SELECT [id] FROM [dbo].[users] /* OUTPUT inserted.id */ WHERE [id] = @id | 已覆盖 |
| `VS043Q` | `vastbase-sqlserver-unsupported-keywords-in-quoted-identifiers` | SELECT [OUTPUT], [EXEC], [PIVOT] FROM [dbo].[users] | 已覆盖 |
| `VS103` | `vastbase-sqlserver-top-percent` | SELECT TOP (10) PERCENT [id] FROM [dbo].[users] | 已覆盖 |
| `VS104` | `vastbase-sqlserver-top-with-ties` | SELECT TOP (10) WITH TIES [id] FROM [dbo].[users] ORDER BY [score] | 已覆盖 |
| `VS105` | `vastbase-sqlserver-top-percent-with-ties` | SELECT TOP (10) PERCENT WITH TIES [id] FROM [dbo].[users] ORDER BY [score] | 已覆盖 |
| `VS106` | `vastbase-sqlserver-table-hint-nolock` | SELECT [id] FROM [dbo].[users] WITH (NOLOCK) | 已覆盖 |
| `VS107` | `vastbase-sqlserver-query-hint-recompile` | SELECT [id] FROM [dbo].[users] OPTION (RECOMPILE) | 已覆盖 |
| `VS108` | `vastbase-sqlserver-for-json-path` | SELECT [id] FROM [dbo].[users] FOR JSON PATH | 已覆盖 |
| `VS109` | `vastbase-sqlserver-nested-top` | SELECT [id] FROM (SELECT TOP (2) [id] FROM [dbo].[users]) AS [u] | 已覆盖 |
| `VS110` | `vastbase-sqlserver-for-json-path-with-option` | SELECT [id] FROM [dbo].[users] WHERE [status] = @status FOR JSON PATH, INCLUDE_NULL_VALUES OPTION (RECOMPILE) | 已覆盖 |
| `VS111` | `vastbase-sqlserver-nested-top-with-outer-top` | SELECT TOP (5) [id] FROM (SELECT TOP (2) [id] FROM [dbo].[users]) AS [u] | 已覆盖 |
| `VS112` | `vastbase-sqlserver-for-json-second-statement` | SELECT [id] FROM [dbo].[users]; SELECT [id] FROM [dbo].[orders] FOR JSON AUTO | 已覆盖 |
| `VSU003` | `vastbase-sqlserver-output-insert-client` | INSERT INTO [dbo].[users] ([id]) OUTPUT inserted.[id] VALUES (1) | 已覆盖 |
| `VS039` | `vastbase-sqlserver-system-variable` | SELECT @@ROWCOUNT | 已覆盖 |
| `VS040` | `vastbase-sqlserver-binary-literal` | SELECT 0xDEADBEEF AS payload | 已覆盖 |
| `VS041` | `vastbase-sqlserver-convert-style` | SELECT CONVERT(VARCHAR(10), [created_at], 120) FROM [dbo].[users] | 已覆盖 |
| `VS044` | `vastbase-sqlserver-use-database` | USE [AdventureWorks2022] | 已覆盖 |
| `VS045` | `vastbase-sqlserver-use-database-official-example` | USE AdventureWorks2022 | 已覆盖 |
| `VS046` | `vastbase-sqlserver-use-database-in-multi-statement` | USE [AdventureWorks2022]; SELECT * FROM [dbo].[users] | 已覆盖 |
| `VS047` | `vastbase-sqlserver-sp-prepare` | EXEC sp_prepare @handle OUTPUT, N'@p1 int', N'SELECT * FROM users WHERE id=@p1' | 已覆盖 |
| `VS048` | `vastbase-sqlserver-sp-execute` | EXEC sp_execute @handle, 42 | 已覆盖 |
| `VS049` | `vastbase-sqlserver-sp-prepexec` | EXEC sp_prepexec @handle OUTPUT, N'@p1 int', N'SELECT * FROM users WHERE id=@p1', 42 | 已覆盖 |
| `VS050` | `vastbase-sqlserver-sp-unprepare` | EXEC sp_unprepare @handle | 已覆盖 |
| `VS051` | `vastbase-sqlserver-sp-executesql` | EXEC sp_executesql N'SELECT * FROM users WHERE id=@p1', N'@p1 int', @p1=42 | 已覆盖 |
| `VS052` | `vastbase-sqlserver-select-multiple-named-params` | SELECT [id], [name] FROM [dbo].[users] WHERE [id] = @id AND [status] = @status | 已覆盖 |
| `VS053` | `vastbase-sqlserver-select-in-named-params` | SELECT [id] FROM [dbo].[users] WHERE [status] IN (@s1, @s2, @s3) | 已覆盖 |
| `VS054` | `vastbase-sqlserver-top-named-param-like` | SELECT TOP (@limit) [id] FROM [dbo].[users] WHERE [name] LIKE @pattern ORDER BY [id] | 已覆盖 |
| `VS055` | `vastbase-sqlserver-insert-multiple-named-params` | INSERT INTO [dbo].[users] ([id], [name], [status]) VALUES (@id, @name, @status) | 已覆盖 |
| `VS056` | `vastbase-sqlserver-insert-multi-row-question-params` | INSERT INTO [dbo].[users] ([id], [name]) VALUES (?, ?), (?, ?) | 已覆盖 |
| `VS057` | `vastbase-sqlserver-update-question-params-expanded` | UPDATE [dbo].[users] SET [name] = ?, [status] = ? WHERE [id] = ? | 已覆盖 |
| `VS058` | `vastbase-sqlserver-delete-question-params` | DELETE FROM [dbo].[users] WHERE [id] = ? AND [status] = ? | 已覆盖 |
| `VS059` | `vastbase-sqlserver-sp-prepare-insert` | EXEC sp_prepare @handle OUTPUT, N'@id int, @name nvarchar(50)', N'INSERT INTO users(id, name) VALUES(@id, @name)' | 已覆盖 |
| `VS060` | `vastbase-sqlserver-sp-execute-named-values` | EXEC sp_execute @handle, @id = 1, @name = N'bob' | 已覆盖 |
| `VS061` | `vastbase-sqlserver-sp-executesql-update` | EXEC sp_executesql N'UPDATE users SET name=@name WHERE id=@id', N'@name nvarchar(50), @id int', @name=N'bob', @id=1 | 已覆盖 |
| `VS062` | `vastbase-sqlserver-view-top-direct-and-bind` | SELECT TOP (@limit) [id], [name] FROM [dbo].[users] WHERE [id] = @id ORDER BY [id] | 已覆盖 |
| `VS063` | `vastbase-sqlserver-view-plus-expression` | SELECT [first_name] + [last_name] FROM [dbo].[users] WHERE [id] = @id | 已覆盖 |
| `VS064` | `vastbase-sqlserver-view-case-expression` | SELECT CASE WHEN [state] = 1 THEN [name] ELSE [fallback_name] END FROM [dbo].[users] | 已覆盖 |
| `VS065` | `vastbase-sqlserver-view-group-having-order` | SELECT [dept], COUNT([id]) FROM [dbo].[users] GROUP BY [dept] HAVING COUNT([id]) > 1 ORDER BY [dept] | 已覆盖 |
| `VS066` | `vastbase-sqlserver-view-update-at-binds` | UPDATE [dbo].[servers] SET [ip] = @aaa WHERE [id] = @id | 已覆盖 |
| `VS067` | `vastbase-sqlserver-select-between-named-params` | SELECT [id] FROM [dbo].[users] WHERE [age] BETWEEN @min_age AND @max_age | 已覆盖 |
| `VS068` | `vastbase-sqlserver-select-not-in-named-params` | SELECT [id] FROM [dbo].[users] WHERE [status] NOT IN (@s1, @s2) | 已覆盖 |
| `VS069` | `vastbase-sqlserver-select-not-between-named-params` | SELECT [id] FROM [dbo].[users] WHERE [age] NOT BETWEEN @min_age AND @max_age | 已覆盖 |
| `VS070` | `vastbase-sqlserver-select-not-like-named-param` | SELECT [id] FROM [dbo].[users] WHERE [name] NOT LIKE @name_pattern | 已覆盖 |
| `VS071` | `vastbase-sqlserver-select-distinct-like-param` | SELECT DISTINCT [name] FROM [dbo].[table1] WHERE [name] LIKE @name | 已覆盖 |
| `VS072` | `vastbase-sqlserver-delete-in-named-params` | DELETE FROM [dbo].[users] WHERE [email] IN (@email1, @email2) | 已覆盖 |
| `VS073` | `vastbase-sqlserver-update-exists-subquery` | UPDATE [dbo].[users] SET [status] = @status WHERE EXISTS (SELECT 1 FROM [dbo].[orders] o WHERE o.[user_id] = [users].[id] AND o.[phone] = @phone) | 已覆盖 |
| `VS074` | `vastbase-sqlserver-insert-without-column-list` | INSERT INTO [dbo].[users] VALUES (?, ?, ?) | 已覆盖 |
| `VS075` | `vastbase-sqlserver-select-derived-table-filter` | SELECT t.[id], t.[phone] FROM (SELECT [id], [phone] FROM [dbo].[users] WHERE [status] = @status) t WHERE t.[phone] = @phone | 已覆盖 |
| `VS076` | `vastbase-sqlserver-select-json-value` | SELECT JSON_VALUE([extra], '$.phone') AS [phone_json] FROM [dbo].[users] WHERE [id] = @id | 已覆盖 |
| `VS077` | `vastbase-sqlserver-select-order-by-ordinal` | SELECT [id], [phone] FROM [dbo].[users] ORDER BY 1 | 已覆盖 |
| `VS078` | `vastbase-sqlserver-offset-fetch-named-params` | SELECT [id] FROM [dbo].[users] ORDER BY [id] OFFSET @offset ROWS FETCH NEXT @limit ROWS ONLY | 已覆盖 |
| `VS079` | `vastbase-sqlserver-select-left-join-alias-star` | SELECT u.*, o.[order_no] FROM [dbo].[users] u LEFT JOIN [dbo].[orders] o ON u.[id] = o.[user_id] WHERE o.[status] = @status | 已覆盖 |
| `VS080` | `vastbase-sqlserver-create-view-join-aggregate` | CREATE VIEW [dbo].[v_user_orders] AS SELECT u.[id], COUNT(o.[id]) AS [order_count] FROM [dbo].[users] u JOIN [dbo].[orders] o ON u.[id] = o.[user_id] GROUP BY u.[id] | 已覆盖 |
| `VS081` | `vastbase-sqlserver-top-question-bind-order` | SELECT TOP (?) [id] FROM [dbo].[users] WHERE [name] LIKE ? ORDER BY [id] | 已覆盖 |
| `VS082` | `vastbase-sqlserver-multi-statement-global-bind-position` | UPDATE [dbo].[users] SET [a] = ? WHERE [b] = ?; UPDATE [dbo].[users] SET [c] = ? WHERE [d] = ? | 已覆盖 |
| `VS083` | `vastbase-sqlserver-select-derived-query-graph` | SELECT s.[name] AS [outer_name] FROM (SELECT [id], [name] FROM [dbo].[users] WHERE [age] <= @age) s WHERE s.[name] LIKE @name | 已覆盖 |
| `VS084` | `vastbase-sqlserver-merge-bind-query-graph` | MERGE INTO target t USING source s ON t.id=s.id WHEN MATCHED THEN UPDATE SET phone = ? WHEN NOT MATCHED THEN INSERT (id, phone) VALUES (?, ?) | 已覆盖 |
| `VS085` | `vastbase-sqlserver-field-match-kind-direct-and-expression` | SELECT [id] FROM [dbo].[users] WHERE [secret] = @plain_secret AND UPPER([secret]) = @upper_secret | 已覆盖 |
| `VS086` | `vastbase-sqlserver-expression-field-case-expression-value` | SELECT [id] FROM [dbo].[users] WHERE CASE WHEN [id] = 1 THEN [secret] ELSE [backup_secret] END = @v | 已覆盖 |
| `VS087` | `vastbase-sqlserver-expression-field-multi-field-expression-value` | SELECT [id] FROM [dbo].[users] WHERE CONCAT([secret], [id]) = @v1 AND [secret] + [id] = @v2 | 已覆盖 |
| `VS088` | `vastbase-sqlserver-expression-field-value-side-expression` | SELECT [id] FROM [dbo].[users] WHERE [secret] = UPPER(@v1) AND [secret] = @v2 + 'x' AND [secret] = CAST(@v3 AS VARCHAR(32)) | 已覆盖 |
| `VS089` | `vastbase-sqlserver-expression-field-dml-expression-values` | INSERT INTO [dbo].[users] ([id], [secret]) VALUES (1, UPPER(@v1)); UPDATE [dbo].[users] SET [secret] = @v2 + 'x' WHERE [id] = 1 | 已覆盖 |
| `VS090` | `vastbase-sqlserver-update-named-bind-rhs-crypto-source` | UPDATE [dbo].[dbp_crypto_test] SET [secret] = @secret_value WHERE [id] = @id | 已覆盖 |
| `VS091` | `vastbase-sqlserver-update-multiple-bind-rhs-crypto-source` | UPDATE [dbo].[dbp_crypto_test] SET [phone] = @phone_value, [secret] = @secret_value WHERE [id] = @id | 已覆盖 |
| `VS092` | `vastbase-sqlserver-like-escape-literal` | SELECT [id] FROM [dbo].[users] WHERE [name] LIKE 'A!_%' ESCAPE '!' | 已覆盖 |
| `VS093` | `vastbase-sqlserver-not-like-escape-named-bind` | SELECT [id] FROM [dbo].[users] WHERE [name] NOT LIKE @pattern ESCAPE @escape_char | 已覆盖 |
| `VS094` | `vastbase-sqlserver-like-escape-question-bind` | SELECT [id] FROM [dbo].[users] WHERE [name] LIKE ? ESCAPE ? | 已覆盖 |
| `VS095` | `vastbase-sqlserver-like-without-explicit-escape` | SELECT [id] FROM [dbo].[users] WHERE [name] LIKE @pattern | 已覆盖 |
| `VS096` | `vastbase-sqlserver-bitwise-binary-operators` | SELECT [flags] & 4 AS [and_value], [flags] \| 2 AS [or_value], [flags] ^ 1 AS [xor_value] FROM [dbo].[users] WHERE ([flags] & @mask) = @mask | 已覆盖 |
| `VS097` | `vastbase-sqlserver-bitwise-not-operator` | SELECT ~[flags] AS [not_value] FROM [dbo].[users] | 已覆盖 |
| `VS098` | `vastbase-sqlserver-not-greater-less-comparison` | SELECT [id] FROM [dbo].[users] WHERE [score] !> @max_score AND [score] !< @min_score | 已覆盖 |
| `VS099` | `vastbase-sqlserver-string-concat-pipes` | SELECT [first_name] \|\| [last_name] AS [full_name] FROM [dbo].[users] | 已覆盖 |
| `VS100` | `vastbase-sqlserver-like-bracket-wildcards` | SELECT [name] FROM [dbo].[users] WHERE [name] LIKE 'A[^b]_%' | 已覆盖 |
| `VS101` | `vastbase-sqlserver-at-time-zone-expression` | SELECT [id] FROM [dbo].[users] WHERE [created_at] AT TIME ZONE 'UTC' = @ts | 已覆盖 |
| `VS102` | `vastbase-sqlserver-is-distinct-from-bind` | SELECT [id] FROM [dbo].[users] WHERE [deleted_at] IS DISTINCT FROM @deleted_at | 已覆盖 |
| `VSH001` | `vastbase-sqlserver-hook-constants-transact-sql` | INSERT INTO [dbo].[binlog] ([payload]) VALUES (0xDEADBEEF) | 已覆盖 |
| `VSH002` | `vastbase-sqlserver-hook-datetimeoffset-transact-sql` | CREATE TABLE [dbo].[events] ([created_at] DATETIMEOFFSET(7)) | 已覆盖 |
| `VSH003` | `vastbase-sqlserver-hook-nondeterministic-convert-date-literals` | SELECT CONVERT(DATETIME, '01-02-2024', 101) AS [converted_at] FROM [dbo].[users] | 已覆盖 |
| `VSH004` | `vastbase-sqlserver-hook-ai-functions-transact-sql` | SELECT AI_FUNCTIONS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH005` | `vastbase-sqlserver-hook-any-value-transact-sql` | SELECT ANY_VALUE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH006` | `vastbase-sqlserver-hook-app-name-transact-sql` | SELECT APP_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH007` | `vastbase-sqlserver-hook-applock-mode-transact-sql` | SELECT APPLOCK_MODE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH008` | `vastbase-sqlserver-hook-applock-test-transact-sql` | SELECT APPLOCK_TEST(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH009` | `vastbase-sqlserver-hook-approx-percentile-cont-transact-sql` | SELECT APPROX_PERCENTILE_CONT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH010` | `vastbase-sqlserver-hook-approx-percentile-disc-transact-sql` | SELECT APPROX_PERCENTILE_DISC(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH011` | `vastbase-sqlserver-hook-assemblyproperty-transact-sql` | SELECT ASSEMBLYPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH012` | `vastbase-sqlserver-hook-asymkey-id-transact-sql` | SELECT ASYMKEY_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH013` | `vastbase-sqlserver-hook-asymkeyproperty-transact-sql` | SELECT ASYMKEYPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH014` | `vastbase-sqlserver-hook-base64-decode-transact-sql` | SELECT BASE64_DECODE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH015` | `vastbase-sqlserver-hook-base64-encode-transact-sql` | SELECT BASE64_ENCODE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH016` | `vastbase-sqlserver-hook-binary-checksum-transact-sql` | SELECT BINARY_CHECKSUM(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH017` | `vastbase-sqlserver-hook-bit-count-transact-sql` | SELECT BIT_COUNT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH018` | `vastbase-sqlserver-hook-cast-and-convert-transact-sql` | SELECT CONVERT(INT, '42', 0) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH019` | `vastbase-sqlserver-hook-cert-id-transact-sql` | SELECT CERT_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH020` | `vastbase-sqlserver-hook-certencoded-transact-sql` | SELECT CERTENCODED(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH021` | `vastbase-sqlserver-hook-certprivatekey-transact-sql` | SELECT CERTPRIVATEKEY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH022` | `vastbase-sqlserver-hook-certproperty-transact-sql` | SELECT CERTPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH023` | `vastbase-sqlserver-hook-checksum-transact-sql` | SELECT CHECKSUM(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH024` | `vastbase-sqlserver-hook-col-length-transact-sql` | SELECT COL_LENGTH(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH025` | `vastbase-sqlserver-hook-col-name-transact-sql` | SELECT COL_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH026` | `vastbase-sqlserver-hook-collation-functions-collationproperty-transact-sql` | SELECT COLLATIONPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH027` | `vastbase-sqlserver-hook-collation-functions-tertiary-weights-transact-sql` | SELECT TERTIARY_WEIGHTS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH028` | `vastbase-sqlserver-hook-columnproperty-transact-sql` | SELECT COLUMNPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH029` | `vastbase-sqlserver-hook-columns-updated-transact-sql` | SELECT COLUMNS_UPDATED(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH030` | `vastbase-sqlserver-hook-compress-transact-sql` | SELECT COMPRESS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH031` | `vastbase-sqlserver-hook-connectionproperty-transact-sql` | SELECT CONNECTIONPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH032` | `vastbase-sqlserver-hook-connections-transact-sql` | SELECT @@CONNECTIONS AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH033` | `vastbase-sqlserver-hook-context-info-transact-sql` | SELECT CONTEXT_INFO(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH034` | `vastbase-sqlserver-hook-cpu-busy-transact-sql` | SELECT @@CPU_BUSY AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH035` | `vastbase-sqlserver-hook-crypt-gen-random-transact-sql` | SELECT CRYPT_GEN_RANDOM(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH036` | `vastbase-sqlserver-hook-current-date-transact-sql` | SELECT CURRENT_DATE AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH037` | `vastbase-sqlserver-hook-current-request-id-transact-sql` | SELECT CURRENT_REQUEST_ID() AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH038` | `vastbase-sqlserver-hook-current-timestamp-transact-sql` | SELECT CURRENT_TIMESTAMP AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH039` | `vastbase-sqlserver-hook-current-timezone-id-transact-sql` | SELECT CURRENT_TIMEZONE_ID() AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH040` | `vastbase-sqlserver-hook-current-timezone-transact-sql` | SELECT CURRENT_TIMEZONE() AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH041` | `vastbase-sqlserver-hook-current-transaction-id-transact-sql` | SELECT CURRENT_TRANSACTION_ID() AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH042` | `vastbase-sqlserver-hook-current-user-transact-sql` | SELECT CURRENT_USER AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH043` | `vastbase-sqlserver-hook-cursor-rows-transact-sql` | SELECT CURSOR_ROWS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH044` | `vastbase-sqlserver-hook-cursor-status-transact-sql` | SELECT CURSOR_STATUS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH045` | `vastbase-sqlserver-hook-database-principal-id-transact-sql` | SELECT DATABASE_PRINCIPAL_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH046` | `vastbase-sqlserver-hook-databasepropertyex-transact-sql` | SELECT DATABASEPROPERTYEX(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH047` | `vastbase-sqlserver-hook-date-bucket-transact-sql` | SELECT DATE_BUCKET(day, 1, [created_at]) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH048` | `vastbase-sqlserver-hook-datefirst-transact-sql` | SELECT @@DATEFIRST AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH049` | `vastbase-sqlserver-hook-datefromparts-transact-sql` | SELECT DATEFROMPARTS(2024, 1, 2) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH050` | `vastbase-sqlserver-hook-datetime2fromparts-transact-sql` | SELECT DATETIME2FROMPARTS(2024, 1, 2, 3, 4, 5, 0, 7) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH051` | `vastbase-sqlserver-hook-datetimefromparts-transact-sql` | SELECT DATETIMEFROMPARTS(2024, 1, 2, 3, 4, 5, 0) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH052` | `vastbase-sqlserver-hook-datetimeoffsetfromparts-transact-sql` | SELECT DATETIMEOFFSETFROMPARTS(2024, 1, 2, 3, 4, 5, 0, -8, 0, 7) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH053` | `vastbase-sqlserver-hook-datetrunc-transact-sql` | SELECT DATETRUNC(day, [created_at]) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH054` | `vastbase-sqlserver-hook-db-id-transact-sql` | SELECT DB_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH055` | `vastbase-sqlserver-hook-db-name-transact-sql` | SELECT DB_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH056` | `vastbase-sqlserver-hook-dbts-transact-sql` | SELECT @@DBTS AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH057` | `vastbase-sqlserver-hook-decompress-transact-sql` | SELECT DECOMPRESS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH058` | `vastbase-sqlserver-hook-decryptbyasymkey-transact-sql` | SELECT DECRYPTBYASYMKEY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH059` | `vastbase-sqlserver-hook-decryptbycert-transact-sql` | SELECT DECRYPTBYCERT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH060` | `vastbase-sqlserver-hook-decryptbykey-transact-sql` | SELECT DECRYPTBYKEY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH061` | `vastbase-sqlserver-hook-decryptbykeyautoasymkey-transact-sql` | SELECT DECRYPTBYKEYAUTOASYMKEY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH062` | `vastbase-sqlserver-hook-decryptbykeyautocert-transact-sql` | SELECT DECRYPTBYKEYAUTOCERT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH063` | `vastbase-sqlserver-hook-decryptbypassphrase-transact-sql` | SELECT DECRYPTBYPASSPHRASE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH064` | `vastbase-sqlserver-hook-edge-id-from-parts-transact-sql` | SELECT EDGE_ID_FROM_PARTS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH065` | `vastbase-sqlserver-hook-edit-distance-similarity-transact-sql` | SELECT EDIT_DISTANCE_SIMILARITY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH066` | `vastbase-sqlserver-hook-edit-distance-transact-sql` | SELECT EDIT_DISTANCE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH067` | `vastbase-sqlserver-hook-encryptbyasymkey-transact-sql` | SELECT ENCRYPTBYASYMKEY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH068` | `vastbase-sqlserver-hook-encryptbycert-transact-sql` | SELECT ENCRYPTBYCERT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH069` | `vastbase-sqlserver-hook-encryptbykey-transact-sql` | SELECT ENCRYPTBYKEY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH070` | `vastbase-sqlserver-hook-encryptbypassphrase-transact-sql` | SELECT ENCRYPTBYPASSPHRASE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH071` | `vastbase-sqlserver-hook-eomonth-transact-sql` | SELECT EOMONTH(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH072` | `vastbase-sqlserver-hook-error-line-transact-sql` | SELECT ERROR_LINE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH073` | `vastbase-sqlserver-hook-error-message-transact-sql` | SELECT ERROR_MESSAGE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH074` | `vastbase-sqlserver-hook-error-number-transact-sql` | SELECT ERROR_NUMBER(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH075` | `vastbase-sqlserver-hook-error-procedure-transact-sql` | SELECT ERROR_PROCEDURE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH076` | `vastbase-sqlserver-hook-error-severity-transact-sql` | SELECT ERROR_SEVERITY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH077` | `vastbase-sqlserver-hook-error-state-transact-sql` | SELECT ERROR_STATE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH078` | `vastbase-sqlserver-hook-eventdata-transact-sql` | SELECT EVENTDATA(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH079` | `vastbase-sqlserver-hook-fetch-status-transact-sql` | SELECT @@FETCH_STATUS AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH080` | `vastbase-sqlserver-hook-file-id-transact-sql` | SELECT FILE_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH081` | `vastbase-sqlserver-hook-file-idex-transact-sql` | SELECT FILE_IDEX(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH082` | `vastbase-sqlserver-hook-file-name-transact-sql` | SELECT FILE_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH083` | `vastbase-sqlserver-hook-filegroup-id-transact-sql` | SELECT FILEGROUP_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH084` | `vastbase-sqlserver-hook-filegroup-name-transact-sql` | SELECT FILEGROUP_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH085` | `vastbase-sqlserver-hook-filegroupproperty-transact-sql` | SELECT FILEGROUPPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH086` | `vastbase-sqlserver-hook-fileproperty-transact-sql` | SELECT FILEPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH087` | `vastbase-sqlserver-hook-filepropertyex-transact-sql` | SELECT FILEPROPERTYEX(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH088` | `vastbase-sqlserver-hook-format-transact-sql` | SELECT FORMAT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH089` | `vastbase-sqlserver-hook-formatmessage-transact-sql` | SELECT FORMATMESSAGE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH090` | `vastbase-sqlserver-hook-fulltextcatalogproperty-transact-sql` | SELECT FULLTEXTCATALOGPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH091` | `vastbase-sqlserver-hook-fulltextserviceproperty-transact-sql` | SELECT FULLTEXTSERVICEPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH092` | `vastbase-sqlserver-hook-get-bit-transact-sql` | SELECT GET_BIT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH093` | `vastbase-sqlserver-hook-get-filestream-transaction-context-transact-sql` | SELECT GET_FILESTREAM_TRANSACTION_CONTEXT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH094` | `vastbase-sqlserver-hook-getansinull-transact-sql` | SELECT GETANSINULL(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH095` | `vastbase-sqlserver-hook-graph-id-from-edge-id-transact-sql` | SELECT GRAPH_ID_FROM_EDGE_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH096` | `vastbase-sqlserver-hook-graph-id-from-node-id-transact-sql` | SELECT GRAPH_ID_FROM_NODE_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH097` | `vastbase-sqlserver-hook-has-dbaccess-transact-sql` | SELECT HAS_DBACCESS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH098` | `vastbase-sqlserver-hook-has-perms-by-name-transact-sql` | SELECT HAS_PERMS_BY_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH099` | `vastbase-sqlserver-hook-hashbytes-transact-sql` | SELECT HASHBYTES(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH100` | `vastbase-sqlserver-hook-host-id-transact-sql` | SELECT HOST_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH101` | `vastbase-sqlserver-hook-host-name-transact-sql` | SELECT HOST_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH102` | `vastbase-sqlserver-hook-ident-current-transact-sql` | SELECT IDENT_CURRENT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH103` | `vastbase-sqlserver-hook-ident-incr-transact-sql` | SELECT IDENT_INCR(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH104` | `vastbase-sqlserver-hook-ident-seed-transact-sql` | SELECT IDENT_SEED(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH105` | `vastbase-sqlserver-hook-idle-transact-sql` | SELECT @@IDLE AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH106` | `vastbase-sqlserver-hook-index-col-transact-sql` | SELECT INDEX_COL(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH107` | `vastbase-sqlserver-hook-indexkey-property-transact-sql` | SELECT INDEXKEY_PROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH108` | `vastbase-sqlserver-hook-indexproperty-transact-sql` | SELECT INDEXPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH109` | `vastbase-sqlserver-hook-io-busy-transact-sql` | SELECT @@IO_BUSY AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH110` | `vastbase-sqlserver-hook-is-member-transact-sql` | SELECT IS_MEMBER(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH111` | `vastbase-sqlserver-hook-is-objectsigned-transact-sql` | SELECT IS_OBJECTSIGNED(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH112` | `vastbase-sqlserver-hook-is-rolemember-transact-sql` | SELECT IS_ROLEMEMBER(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH113` | `vastbase-sqlserver-hook-is-srvrolemember-transact-sql` | SELECT IS_SRVROLEMEMBER(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH114` | `vastbase-sqlserver-hook-isdate-transact-sql` | SELECT ISDATE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH115` | `vastbase-sqlserver-hook-jaro-winkler-distance-transact-sql` | SELECT JARO_WINKLER_DISTANCE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH116` | `vastbase-sqlserver-hook-jaro-winkler-similarity-transact-sql` | SELECT JARO_WINKLER_SIMILARITY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH117` | `vastbase-sqlserver-hook-json-array-transact-sql` | SELECT JSON_ARRAY(1, 2) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH118` | `vastbase-sqlserver-hook-json-arrayagg-transact-sql` | SELECT JSON_ARRAYAGG([id]) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH119` | `vastbase-sqlserver-hook-json-contains-transact-sql` | SELECT JSON_CONTAINS('{"a":1}', '1', '$.a') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH120` | `vastbase-sqlserver-hook-json-modify-transact-sql` | SELECT JSON_MODIFY('{"a":1}', '$.a', 2) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH121` | `vastbase-sqlserver-hook-json-object-transact-sql` | SELECT JSON_OBJECT('id': [id]) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH122` | `vastbase-sqlserver-hook-json-objectagg-transact-sql` | SELECT JSON_OBJECTAGG([name]: [id]) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH123` | `vastbase-sqlserver-hook-json-path-exists-transact-sql` | SELECT JSON_PATH_EXISTS('{"a":1}', '$.a') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH124` | `vastbase-sqlserver-hook-json-query-transact-sql` | SELECT JSON_QUERY('{"a":1}', '$') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH125` | `vastbase-sqlserver-hook-json-value-transact-sql` | SELECT JSON_VALUE('{"a":1}', '$.a') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH126` | `vastbase-sqlserver-hook-key-guid-transact-sql` | SELECT KEY_GUID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH127` | `vastbase-sqlserver-hook-key-id-transact-sql` | SELECT KEY_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH128` | `vastbase-sqlserver-hook-key-name-transact-sql` | SELECT KEY_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH129` | `vastbase-sqlserver-hook-langid-transact-sql` | SELECT LANGID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH130` | `vastbase-sqlserver-hook-language-transact-sql` | SELECT LANGUAGE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH131` | `vastbase-sqlserver-hook-left-shift-transact-sql` | SELECT LEFT_SHIFT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH132` | `vastbase-sqlserver-hook-lock-timeout-transact-sql` | SELECT @@LOCK_TIMEOUT AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH133` | `vastbase-sqlserver-hook-logical-functions-choose-transact-sql` | SELECT CHOOSE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH134` | `vastbase-sqlserver-hook-logical-functions-greatest-transact-sql` | SELECT GREATEST(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH135` | `vastbase-sqlserver-hook-logical-functions-iif-transact-sql` | SELECT IIF(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH136` | `vastbase-sqlserver-hook-logical-functions-least-transact-sql` | SELECT LEAST(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH137` | `vastbase-sqlserver-hook-loginproperty-transact-sql` | SELECT LOGINPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH138` | `vastbase-sqlserver-hook-max-connections-transact-sql` | SELECT @@MAX_CONNECTIONS AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH139` | `vastbase-sqlserver-hook-max-precision-transact-sql` | SELECT @@MAX_PRECISION AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH140` | `vastbase-sqlserver-hook-min-active-rowversion-transact-sql` | SELECT MIN_ACTIVE_ROWVERSION(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH141` | `vastbase-sqlserver-hook-nestlevel-transact-sql` | SELECT NESTLEVEL(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH142` | `vastbase-sqlserver-hook-newsequentialid-transact-sql` | SELECT NEWSEQUENTIALID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH143` | `vastbase-sqlserver-hook-node-id-from-parts-transact-sql` | SELECT NODE_ID_FROM_PARTS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH144` | `vastbase-sqlserver-hook-object-definition-transact-sql` | SELECT OBJECT_DEFINITION(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH145` | `vastbase-sqlserver-hook-object-id-from-edge-id-transact-sql` | SELECT OBJECT_ID_FROM_EDGE_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH146` | `vastbase-sqlserver-hook-object-id-from-node-id-transact-sql` | SELECT OBJECT_ID_FROM_NODE_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH147` | `vastbase-sqlserver-hook-object-id-transact-sql` | SELECT OBJECT_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH148` | `vastbase-sqlserver-hook-object-name-transact-sql` | SELECT OBJECT_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH149` | `vastbase-sqlserver-hook-object-schema-name-transact-sql` | SELECT OBJECT_SCHEMA_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH150` | `vastbase-sqlserver-hook-objectproperty-transact-sql` | SELECT OBJECTPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH151` | `vastbase-sqlserver-hook-objectpropertyex-transact-sql` | SELECT OBJECTPROPERTYEX(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH152` | `vastbase-sqlserver-hook-odbc-scalar-functions-transact-sql` | SELECT {fn NOW()} AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH153` | `vastbase-sqlserver-hook-options-transact-sql` | SELECT @@OPTIONS AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH154` | `vastbase-sqlserver-hook-original-db-name-transact-sql` | SELECT ORIGINAL_DB_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH155` | `vastbase-sqlserver-hook-original-login-transact-sql` | SELECT ORIGINAL_LOGIN(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH156` | `vastbase-sqlserver-hook-pack-received-transact-sql` | SELECT @@PACK_RECEIVED AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH157` | `vastbase-sqlserver-hook-pack-sent-transact-sql` | SELECT @@PACK_SENT AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH158` | `vastbase-sqlserver-hook-packet-errors-transact-sql` | SELECT @@PACKET_ERRORS AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH159` | `vastbase-sqlserver-hook-parse-transact-sql` | SELECT PARSE('42' AS INT USING 'en-US') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH160` | `vastbase-sqlserver-hook-parsename-transact-sql` | SELECT PARSENAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH161` | `vastbase-sqlserver-hook-partition-transact-sql` | SELECT PARTITION(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH162` | `vastbase-sqlserver-hook-permissions-transact-sql` | SELECT PERMISSIONS(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH163` | `vastbase-sqlserver-hook-procid-transact-sql` | SELECT @@PROCID AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH164` | `vastbase-sqlserver-hook-product-aggregate-transact-sql` | SELECT PRODUCT_AGGREGATE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH165` | `vastbase-sqlserver-hook-pwdcompare-transact-sql` | SELECT PWDCOMPARE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH166` | `vastbase-sqlserver-hook-pwdencrypt-transact-sql` | SELECT PWDENCRYPT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH167` | `vastbase-sqlserver-hook-regexp-count-transact-sql` | SELECT REGEXP_COUNT([name], 'a') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH168` | `vastbase-sqlserver-hook-regexp-instr-transact-sql` | SELECT REGEXP_INSTR([name], 'a') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH169` | `vastbase-sqlserver-hook-regexp-like-transact-sql` | SELECT REGEXP_LIKE([name], 'a') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH170` | `vastbase-sqlserver-hook-regexp-matches-transact-sql` | SELECT REGEXP_MATCHES([name], 'a') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH171` | `vastbase-sqlserver-hook-regexp-replace-transact-sql` | SELECT REGEXP_REPLACE([name], 'a', 'b') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH172` | `vastbase-sqlserver-hook-regexp-substr-transact-sql` | SELECT REGEXP_SUBSTR([name], 'a') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH173` | `vastbase-sqlserver-hook-remserver-transact-sql` | SELECT @@REMSERVER AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH174` | `vastbase-sqlserver-hook-replication-functions-publishingservername` | SELECT PUBLISHINGSERVERNAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH175` | `vastbase-sqlserver-hook-right-shift-transact-sql` | SELECT RIGHT_SHIFT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH176` | `vastbase-sqlserver-hook-rowcount-big-transact-sql` | SELECT ROWCOUNT_BIG(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH177` | `vastbase-sqlserver-hook-rowcount-transact-sql` | SELECT @@ROWCOUNT AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH178` | `vastbase-sqlserver-hook-schema-id-transact-sql` | SELECT SCHEMA_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH179` | `vastbase-sqlserver-hook-schema-name-transact-sql` | SELECT SCHEMA_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH180` | `vastbase-sqlserver-hook-servername-transact-sql` | SELECT SERVERNAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH181` | `vastbase-sqlserver-hook-serverproperty-transact-sql` | SELECT SERVERPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH182` | `vastbase-sqlserver-hook-servicename-transact-sql` | SELECT SERVICENAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH183` | `vastbase-sqlserver-hook-session-context-transact-sql` | SELECT SESSION_CONTEXT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH184` | `vastbase-sqlserver-hook-session-id-transact-sql` | SELECT SESSION_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH185` | `vastbase-sqlserver-hook-session-user-transact-sql` | SELECT SESSION_USER AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH186` | `vastbase-sqlserver-hook-sessionproperty-transact-sql` | SELECT SESSIONPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH187` | `vastbase-sqlserver-hook-set-bit-transact-sql` | SELECT SET_BIT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH188` | `vastbase-sqlserver-hook-signbyasymkey-transact-sql` | SELECT SIGNBYASYMKEY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH189` | `vastbase-sqlserver-hook-signbycert-transact-sql` | SELECT SIGNBYCERT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH190` | `vastbase-sqlserver-hook-smalldatetimefromparts-transact-sql` | SELECT SMALLDATETIMEFROMPARTS(2024, 1, 2, 3, 4) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH191` | `vastbase-sqlserver-hook-spid-transact-sql` | SELECT @@SPID AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH192` | `vastbase-sqlserver-hook-sql-variant-property-transact-sql` | SELECT SQL_VARIANT_PROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH193` | `vastbase-sqlserver-hook-stats-date-transact-sql` | SELECT STATS_DATE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH194` | `vastbase-sqlserver-hook-string-escape-transact-sql` | SELECT STRING_ESCAPE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH195` | `vastbase-sqlserver-hook-suser-id-transact-sql` | SELECT SUSER_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH196` | `vastbase-sqlserver-hook-suser-name-transact-sql` | SELECT SUSER_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH197` | `vastbase-sqlserver-hook-suser-sid-transact-sql` | SELECT SUSER_SID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH198` | `vastbase-sqlserver-hook-suser-sname-transact-sql` | SELECT SUSER_SNAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH199` | `vastbase-sqlserver-hook-switchoffset-transact-sql` | SELECT SWITCHOFFSET(SYSDATETIMEOFFSET(), '-08:00') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH200` | `vastbase-sqlserver-hook-symkeyproperty-transact-sql` | SELECT SYMKEYPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH201` | `vastbase-sqlserver-hook-system-user-transact-sql` | SELECT SYSTEM_USER AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH202` | `vastbase-sqlserver-hook-textsize-transact-sql` | SELECT @@TEXTSIZE AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH203` | `vastbase-sqlserver-hook-timefromparts-transact-sql` | SELECT TIMEFROMPARTS(3, 4, 5, 0, 7) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH204` | `vastbase-sqlserver-hook-timeticks-transact-sql` | SELECT @@TIMETICKS AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH205` | `vastbase-sqlserver-hook-todatetimeoffset-transact-sql` | SELECT TODATETIMEOFFSET(GETDATE(), '-08:00') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH206` | `vastbase-sqlserver-hook-total-errors-transact-sql` | SELECT @@TOTAL_ERRORS AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH207` | `vastbase-sqlserver-hook-total-read-transact-sql` | SELECT @@TOTAL_READ AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH208` | `vastbase-sqlserver-hook-total-write-transact-sql` | SELECT @@TOTAL_WRITE AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH209` | `vastbase-sqlserver-hook-trancount-transact-sql` | SELECT @@TRANCOUNT AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH210` | `vastbase-sqlserver-hook-translate-transact-sql` | SELECT TRANSLATE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH211` | `vastbase-sqlserver-hook-trigger-nestlevel-transact-sql` | SELECT TRIGGER_NESTLEVEL(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH212` | `vastbase-sqlserver-hook-try-cast-transact-sql` | SELECT TRY_CAST('42' AS INT) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH213` | `vastbase-sqlserver-hook-try-convert-transact-sql` | SELECT TRY_CONVERT(INT, '42', 0) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH214` | `vastbase-sqlserver-hook-try-parse-transact-sql` | SELECT TRY_PARSE('42' AS INT USING 'en-US') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH215` | `vastbase-sqlserver-hook-type-id-transact-sql` | SELECT TYPE_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH216` | `vastbase-sqlserver-hook-type-name-transact-sql` | SELECT TYPE_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH217` | `vastbase-sqlserver-hook-typeproperty-transact-sql` | SELECT TYPEPROPERTY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH218` | `vastbase-sqlserver-hook-unistr-transact-sql` | SELECT UNISTR('abc') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH219` | `vastbase-sqlserver-hook-user-id-transact-sql` | SELECT USER_ID(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH220` | `vastbase-sqlserver-hook-user-name-transact-sql` | SELECT USER_NAME(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH221` | `vastbase-sqlserver-hook-user-transact-sql` | SELECT USER AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH222` | `vastbase-sqlserver-hook-vector-distance-transact-sql` | SELECT VECTOR_DISTANCE('[1,2]', '[2,3]') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH223` | `vastbase-sqlserver-hook-vector-norm-transact-sql` | SELECT VECTOR_NORM('[1,2]') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH224` | `vastbase-sqlserver-hook-vector-normalize-transact-sql` | SELECT VECTOR_NORMALIZE('[1,2]') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH225` | `vastbase-sqlserver-hook-vectorproperty-transact-sql` | SELECT VECTORPROPERTY('[1,2]', 'dimensions') AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH226` | `vastbase-sqlserver-hook-verifysignedbyasymkey-transact-sql` | SELECT VERIFYSIGNEDBYASYMKEY(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH227` | `vastbase-sqlserver-hook-verifysignedbycert-transact-sql` | SELECT VERIFYSIGNEDBYCERT(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH228` | `vastbase-sqlserver-hook-version-transact-sql-configuration-functions` | SELECT @@VERSION AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH229` | `vastbase-sqlserver-hook-version-transact-sql-metadata-functions` | SELECT VERSION_TRANSACT_SQL(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH230` | `vastbase-sqlserver-hook-xact-state-transact-sql` | SELECT XACT_STATE(1) AS [v] FROM [dbo].[users] | 已覆盖 |
| `VSH232` | `vastbase-sqlserver-hook-collations` | SELECT [name] COLLATE Latin1_General_CI_AS AS [name] FROM [dbo].[users] | 已覆盖 |
| `VSH233` | `vastbase-sqlserver-hook-rename-transact-sql` | RENAME OBJECT [dbo].[old_users] TO [new_users] | 已覆盖 |
| `VSH236` | `vastbase-sqlserver-update-from-source-field-graph` | UPDATE t SET name = s.name FROM dbo.t AS t JOIN dbo.src AS s ON t.id = s.id WHERE s.active = @active | 已覆盖 |
| `VSH237` | `vastbase-sqlserver-insert-select-source-block-graph` | INSERT INTO dbo.t (id, email) SELECT s.id, s.email FROM dbo.src AS s WHERE s.active = @active | 已覆盖 |
| `VSH238` | `vastbase-sqlserver-merge-source-target-graph` | MERGE INTO dbo.t AS t USING (SELECT @id AS id, @email AS email) AS s ON t.id=s.id WHEN MATCHED THEN UPDATE SET email=s.email WHEN NOT MATCHED THEN INSERT(id,email) VALUES(s.id,s.email); | 已覆盖 |
| `VSH239` | `vastbase-sqlserver-mixed-create-database-basic` | CREATE DATABASE [appdb] | 已覆盖基础形态 |
| `VSH240` | `vastbase-sqlserver-mixed-drop-database-basic` | DROP DATABASE [appdb] | 已覆盖基础形态 |
| `VSH241` | `vastbase-sqlserver-mixed-create-schema-basic` | CREATE SCHEMA [audit] | 已覆盖基础形态 |
| `VSH242` | `vastbase-sqlserver-mixed-drop-schema-basic` | DROP SCHEMA [audit] | 已覆盖基础形态 |
| `VSH243` | `vastbase-sqlserver-mixed-create-role-basic` | CREATE ROLE [app_role] | 已覆盖基础形态 |
| `VSH244` | `vastbase-sqlserver-mixed-drop-role-basic` | DROP ROLE [app_role] | 已覆盖基础形态 |
| `VSH245` | `vastbase-sqlserver-mixed-create-sequence-basic` | CREATE SEQUENCE [dbo].[seq_users] START WITH 1 INCREMENT BY 1 | 已覆盖基础形态 |
| `VSH246` | `vastbase-sqlserver-mixed-alter-sequence-basic` | ALTER SEQUENCE [dbo].[seq_users] RESTART WITH 10 | 已覆盖基础形态 |
| `VSH247` | `vastbase-sqlserver-mixed-drop-sequence-basic` | DROP SEQUENCE [dbo].[seq_users] | 已覆盖基础形态 |
| `VSH248` | `vastbase-sqlserver-mixed-drop-view-basic` | DROP VIEW [dbo].[v_users] | 已覆盖基础形态 |
| `VSH249` | `vastbase-sqlserver-mixed-drop-statistics-basic` | DROP STATISTICS [dbo].[users].[st_users_id] | 已覆盖基础形态 |
| `VSH250` | `vastbase-sqlserver-mixed-select-into-basic` | SELECT [id] INTO [dbo].[users_copy] FROM [dbo].[users] | 已覆盖基础形态 |
| `VSH251` | `vastbase-sqlserver-mixed-contains-basic` | SELECT * FROM [dbo].[users] WHERE CONTAINS([name], @term) | 已覆盖基础形态 |
| `VSH252` | `vastbase-sqlserver-mixed-freetext-basic` | SELECT * FROM [dbo].[users] WHERE FREETEXT([name], @term) | 已覆盖基础形态 |
| `VSH253` | `vastbase-sqlserver-regexp-like-function-predicate` | SELECT * FROM [dbo].[users] WHERE REGEXP_LIKE([name], @pat) | 已覆盖 |
| `VSH254` | `vastbase-sqlserver-select-or-predicate-order-by-lineage` | SELECT [u].[id], [u].[email], [u].[bank_card] FROM [dbo].[users] AS [u] WHERE [u].[email] = @email OR [u].[bank_card] = @card ORDER BY [u].[id] | 已覆盖 |
| `VSH255` | `vastbase-sqlserver-mixed-create-table-as-select-basic` | CREATE TABLE [dbo].[users_copy] AS SELECT [id] FROM [dbo].[users] | 已覆盖基础形态 |
| `VSH256` | `vastbase-sqlserver-mixed-aliasing-basic` | SELECT [u].[id] AS [user_id], [u].[name] [user_name] FROM [dbo].[users] AS [u] | 已覆盖基础形态 |
| `VSH257` | `vastbase-sqlserver-mixed-subquery-basic` | SELECT [id] FROM [dbo].[users] WHERE [id] IN (SELECT ...) | 外层 membership 字段与 `IN` predicate、内层 target/过滤字段、bind 和来源表分别归属，7 个 patch 覆盖两层 selector |
| `VSH259` | `vastbase-sqlserver-mixed-alter-table-add-constraint-basic` | ALTER TABLE [dbo].[users] ADD CONSTRAINT [pk_users] PRIMARY KEY ([id]) | 已覆盖基础形态 |
| `VSH260` | `vastbase-sqlserver-mixed-drop-type-basic` | DROP TYPE [dbo].[phone] | 已覆盖基础形态 |
| `VSH261` | `vastbase-sqlserver-mixed-create-user-basic` | CREATE USER [app_user] | 已覆盖基础形态 |
| `VSH262` | `vastbase-sqlserver-mixed-drop-user-if-exists-basic` | DROP USER IF EXISTS [app_user] | 已覆盖基础形态 |
| `VSH263` | `vastbase-sqlserver-mixed-drop-role-drop-user-ordinal` | DROP ROLE [app_role]; DROP USER [app_user] | 已覆盖 |
| `VSH264` | `vastbase-sqlserver-mixed-create-user-without-login` | CREATE USER [app_user] WITHOUT LOGIN | 已覆盖 |
| `VSH265` | `vastbase-sqlserver-mixed-create-user-for-login` | CREATE USER [app_user] FOR LOGIN [app_login] | 已覆盖 |
| `VSH266` | `vastbase-sqlserver-mixed-create-user-from-external-provider` | CREATE USER [app_user] FROM EXTERNAL PROVIDER | 已覆盖 |
| `VSH267` | `vastbase-sqlserver-mixed-create-user-with-options` | CREATE USER ... WITH DEFAULT_SCHEMA ... | 已覆盖 |
| `VSH268` | `vastbase-sqlserver-mixed-create-user-for-certificate` | CREATE USER ... FOR CERTIFICATE ... | 已覆盖 |
| `VSH269` | `vastbase-sqlserver-mixed-create-user-for-asymmetric-key` | CREATE USER ... FOR ASYMMETRIC KEY ... | 已覆盖 |
| `VSH270` | `vastbase-sqlserver-mixed-alter-role-add-member` | ALTER ROLE ... ADD MEMBER ... | 已覆盖 |
| `VSH271` | `vastbase-sqlserver-mixed-alter-role-drop-member` | ALTER ROLE ... DROP MEMBER ... | 已覆盖 |
| `VSH272` | `vastbase-sqlserver-mixed-alter-role-with-name` | ALTER ROLE ... WITH NAME = ... | 已覆盖 |
| `VSH273` | `vastbase-sqlserver-mixed-alter-schema-transfer-object` | ALTER SCHEMA ... TRANSFER schema.object | 已覆盖 |
| `VSH274` | `vastbase-sqlserver-mixed-alter-schema-transfer-type` | ALTER SCHEMA ... TRANSFER TYPE::schema.type | 已覆盖 |
| `VSH275` | `vastbase-sqlserver-mixed-create-role-authorization` | CREATE ROLE ... AUTHORIZATION ... | 已覆盖 |
| `VSH276` | `vastbase-sqlserver-mixed-alter-user-name` | ALTER USER ... WITH NAME = ... | 已覆盖 |
| `VSH277` | `vastbase-sqlserver-mixed-alter-user-options` | ALTER USER ... WITH DEFAULT_SCHEMA ... | 已覆盖 |
| `VSH278` | `vastbase-sqlserver-mixed-alter-user-login` | ALTER USER ... WITH LOGIN = ... | 已覆盖 |
| `VSH279` | `vastbase-sqlserver-mixed-alter-user-external-provider` | ALTER USER ... FROM EXTERNAL PROVIDER ... | 已覆盖 |
| `VSH280` | `vastbase-sqlserver-mixed-alter-authorization-object` | ALTER AUTHORIZATION ON OBJECT::... TO ... | 已覆盖 |
| `VSH281` | `vastbase-sqlserver-mixed-alter-authorization-schema-owner` | ALTER AUTHORIZATION ... TO SCHEMA OWNER | 已覆盖 |
| `VSH282` | `vastbase-sqlserver-mixed-alter-authorization-schema` | ALTER AUTHORIZATION ON SCHEMA::... TO ... | 已覆盖 |
| `VSH283` | `vastbase-sqlserver-mixed-create-schema-authorization` | CREATE SCHEMA ... AUTHORIZATION ... | 已覆盖 |
| `VSH284` | `vastbase-sqlserver-mixed-drop-schema-if-exists` | DROP SCHEMA IF EXISTS ... | 已覆盖 |
| `VSH285` | `vastbase-sqlserver-mixed-create-application-role` | CREATE APPLICATION ROLE ... WITH PASSWORD ... | 已覆盖 |
| `VSH286` | `vastbase-sqlserver-mixed-alter-application-role-name` | ALTER APPLICATION ROLE ... WITH NAME = ... | 已覆盖 |
| `VSH287` | `vastbase-sqlserver-mixed-alter-application-role-options` | ALTER APPLICATION ROLE ... WITH PASSWORD ... | 已覆盖 |
| `VSH288` | `vastbase-sqlserver-mixed-drop-application-role` | DROP APPLICATION ROLE ... | 已覆盖 |
| `VSH289` | `vastbase-sqlserver-mixed-create-synonym` | CREATE SYNONYM ... FOR ... | 已覆盖 |
| `VSH290` | `vastbase-sqlserver-mixed-drop-synonym-if-exists` | DROP SYNONYM IF EXISTS ... | 已覆盖 |
| `VSH291` | `vastbase-sqlserver-mixed-create-type-alias` | CREATE TYPE ... FROM ... | 已覆盖 |
| `VSH292` | `vastbase-sqlserver-mixed-alter-database-compatibility-level` | ALTER DATABASE ... SET COMPATIBILITY_LEVEL = ... | 已覆盖 |
| `VSH293` | `vastbase-sqlserver-mixed-drop-index-if-exists-on-object` | DROP INDEX IF EXISTS ... ON ... | 已覆盖 |
| `VSH294` | `vastbase-sqlserver-mixed-update-statistics-fullscan` | UPDATE STATISTICS ... WITH FULLSCAN | 已覆盖 |
| `VSH295-VSH333` | `vastbase-sqlserver-set-*` | SET ... | 已覆盖基础会话/执行环境形态 |
| `VSH334` | `vastbase-sqlserver-table-hints-join-alias` | JOIN + `WITH (NOLOCK)` / `WITH (FORCESEEK)` | 已覆盖基础表提示公开 SQL 恢复 |
| `VSH335` | `vastbase-sqlserver-query-hints-multiple` | `OPTION (RECOMPILE, USE HINT(...))` | 已覆盖基础查询提示公开 SQL 恢复 |
| `VSH336` | `vastbase-sqlserver-insert-output-select-union-all-omitted-into` | INSERT target (...) OUTPUT ... SELECT ... UNION ALL SELECT ... | 已覆盖 |
| `VSH337` | `vastbase-sqlserver-insert-output-select-explicit-into` | INSERT INTO ... OUTPUT ... SELECT ... | 已覆盖 |
| `VSH338` | `vastbase-sqlserver-insert-output-values-omitted-into` | INSERT target (...) OUTPUT ... VALUES (...) | 已覆盖 |
| `VSH339` | `vastbase-sqlserver-insert-output-multi-values-omitted-into` | INSERT target (...) OUTPUT ... VALUES (...), (...) | 已覆盖 |
| `VSH340` | `vastbase-sqlserver-insert-output-default-values` | INSERT target OUTPUT ... DEFAULT VALUES | 已覆盖 |
| `VSH341` | `vastbase-sqlserver-cte-insert-output-select` | WITH ... INSERT ... OUTPUT ... SELECT ... | 已覆盖 |
| `VSH342` | `vastbase-sqlserver-insert-output-existing-case` | INSERT INTO ... OUTPUT INSERTED.id VALUES (...) | 已覆盖 |
| `VSH343` | `vastbase-sqlserver-insert-output-inserted-star` | INSERT ... OUTPUT INSERTED.* VALUES (...) | 已覆盖 |
| `VSH344` | `vastbase-sqlserver-insert-output-expression-alias` | INSERT ... OUTPUT expression AS alias VALUES (...) | 已覆盖 |
| `VSH345` | `vastbase-sqlserver-insert-output-multiple-targets-bind` | INSERT ... OUTPUT target, target, bind VALUES (...) | 已覆盖 |
| `VSH346` | `vastbase-sqlserver-insert-output-into-table` | INSERT ... OUTPUT ... INTO table VALUES (...) | 已覆盖 |
| `VSH347` | `vastbase-sqlserver-insert-output-into-table-columns` | INSERT ... OUTPUT ... INTO table(columns) VALUES (...) | 已覆盖 |
| `VSH348` | `vastbase-sqlserver-insert-output-into-table-variable` | INSERT ... OUTPUT ... INTO @table(columns) VALUES (...) | 已覆盖 |
| `VSH349` | `vastbase-sqlserver-insert-output-dual-channel` | INSERT ... OUTPUT ... INTO ... OUTPUT ... VALUES (...) | 已覆盖 |
| `VSH350` | `vastbase-sqlserver-update-output-before-after` | UPDATE ... OUTPUT DELETED..., INSERTED... WHERE ... | 已覆盖 |
| `VSH351` | `vastbase-sqlserver-update-output-from-source` | UPDATE ... OUTPUT INSERTED..., source... FROM ... | 已覆盖 |
| `VSH352` | `vastbase-sqlserver-delete-output-before` | DELETE ... OUTPUT DELETED... WHERE ... | 已覆盖 |
| `VSH353` | `vastbase-sqlserver-delete-output-from-source` | DELETE alias OUTPUT DELETED..., source... FROM ... | 已覆盖 |
| `VSH354` | `vastbase-sqlserver-merge-output-action` | MERGE ... OUTPUT $action | 已覆盖 |
| `VSH355` | `vastbase-sqlserver-merge-output-all-references` | MERGE ... OUTPUT $action, DELETED..., INSERTED..., source... | 已覆盖 |
| `VSH356` | `vastbase-sqlserver-nested-delete-output-table-source` | INSERT ... SELECT ... FROM (DELETE ... OUTPUT ...) | 已覆盖 |
| `VSH357` | `vastbase-sqlserver-update-top-output` | UPDATE TOP (...) ... OUTPUT ... | 已覆盖 |
| `VSH358` | `vastbase-sqlserver-insert-target-hint-output` | INSERT target WITH (...) ... OUTPUT ... | 已覆盖 |
| `VSH359` | `vastbase-sqlserver-output-keywords-in-string` | OUTPUT 关键字出现在字符串中 | 已覆盖 |
| `VSH360` | `vastbase-sqlserver-output-keywords-in-comments` | OUTPUT 关键字出现在注释中 | 已覆盖 |
| `VSH361` | `vastbase-sqlserver-output-keywords-as-identifiers` | OUTPUT 关键字出现在方括号标识符中 | 已覆盖 |
| `VSH362` | `vastbase-sqlserver-output-keywords-in-source-subquery` | OUTPUT 文本出现在来源子查询中 | 已覆盖 |
| `VSH363` | `vastbase-sqlserver-insert-select-source-table-hint-anchor` | INSERT SELECT 来源表 hint | 已覆盖 |
| `VSH364` | `vastbase-sqlserver-insert-output-target-and-source-table-hints` | INSERT 目标/来源 hint 与 OUTPUT | 已覆盖 |
| `VSH365` | `vastbase-sqlserver-output-delimited-select-column` | OUTPUT 中的定界保留字列 | 已覆盖 |
| `VSH366` | `vastbase-sqlserver-delete-output-equivalent-delimited-alias` | DELETE 等价定界别名 | 已覆盖 |
| `VSH367` | `vastbase-sqlserver-multi-statement-output-channels` | 多语句 OUTPUT 通道 | 已覆盖 |
| `VSH368` | `vastbase-sqlserver-if-exists-official-shape` | 官方 IF EXISTS 双分支 | 已覆盖 |
| `VSH369` | `vastbase-sqlserver-if-without-else` | 无 ELSE 的 IF | 已覆盖 |
| `VSH370` | `vastbase-sqlserver-if-boolean-literal` | 常量布尔条件 | 已覆盖 |
| `VSH371` | `vastbase-sqlserver-if-block-semicolon-statements` | 分号分隔 BEGIN 块 | 已覆盖 |
| `VSH372` | `vastbase-sqlserver-if-block-newline-statements` | 换行分隔 BEGIN 块 | 已覆盖 |
| `VSH373` | `vastbase-sqlserver-if-dangling-else` | dangling ELSE | 已覆盖 |
| `VSH374` | `vastbase-sqlserver-else-if-chain` | ELSE IF 链 | 已覆盖 |
| `VSH375` | `vastbase-sqlserver-if-not-exists` | NOT EXISTS 条件 | 已覆盖 |
| `VSH376` | `vastbase-sqlserver-if-scalar-subquery-condition` | 标量子查询条件 | 已覆盖 |
| `VSH377` | `vastbase-sqlserver-if-nested-boolean-condition` | AND/OR/NOT 嵌套条件 | 已覆盖 |
| `VSH378` | `vastbase-sqlserver-if-function-condition` | COALESCE 条件 | 已覆盖 |
| `VSH379` | `vastbase-sqlserver-if-case-condition` | CASE 条件 | 已覆盖 |
| `VSH380` | `vastbase-sqlserver-if-update-insert-output-branches` | UPDATE/INSERT OUTPUT 分支 | 已覆盖 |
| `VSH381` | `vastbase-sqlserver-if-delete-merge-output-branches` | DELETE/MERGE OUTPUT 分支 | 已覆盖 |
| `VSH382` | `vastbase-sqlserver-if-ddl-branches` | DDL 分支 | 已覆盖 |
| `VSH383` | `vastbase-sqlserver-if-transaction-branches` | 事务分支 | 已覆盖 |
| `VSH384` | `vastbase-sqlserver-if-root-statements-semicolon` | 分号分隔根语句 | 已覆盖 |
| `VSH385` | `vastbase-sqlserver-if-root-statements-newline` | 换行分隔根语句 | 已覆盖 |
| `VSH386` | `vastbase-sqlserver-if-protected-keyword-text` | 受保护文本中的关键字 | 已覆盖 |
| `VSH387` | `vastbase-sqlserver-if-union-line-boundary` | UNION ALL 换行边界 | 已覆盖 |
| `VSH388` | `vastbase-sqlserver-if-table-hint-line-boundary` | 表提示换行边界 | 已覆盖 |
| `VSH389` | `vastbase-sqlserver-if-three-level-nesting` | 三层嵌套 IF | 已覆盖 |
| `VSH390` | `vastbase-sqlserver-drop-user-if-exists-before-control` | DROP USER IF EXISTS 后接 IF | 已覆盖 |
| `VSH391` | `vastbase-sqlserver-if-null-between-in-condition` | NULL/BETWEEN/IN 条件 | 已覆盖 |
| `VSH392` | `vastbase-sqlserver-if-query-right-operand` | 右侧标量子查询 | 已覆盖 |
| `VSH393` | `vastbase-sqlserver-if-cte-branch` | CTE 分支 | 已覆盖 |
| `VSH394` | `vastbase-sqlserver-if-begin-end-optional-semicolons` | BEGIN/END 可选分号 | 已覆盖 |
| `VSH395` | `vastbase-sqlserver-drop-table-if-exists-before-control` | DROP TABLE IF EXISTS 后接 IF | 已覆盖 |
| `VSH396` | `vastbase-sqlserver-drop-table-before-control` | DROP TABLE 后接 IF | 已覆盖 |
| `VSH397` | `vastbase-sqlserver-drop-table-before-if-exists-control` | DROP TABLE 后接 IF EXISTS 控制语句 | 已覆盖 |
| `VSH398` | `vastbase-sqlserver-multiline-drop-if-exists-before-control` | 换行 DROP TABLE IF EXISTS 后接 IF | 已覆盖 |
| `VSH399` | `vastbase-sqlserver-if-cte-update-branch` | CTE UPDATE 分支 | 已覆盖 |
| `VSH400` | `vastbase-sqlserver-if-cte-delete-branch` | CTE DELETE 分支 | 已覆盖 |
| `VSH401` | `vastbase-sqlserver-if-cte-insert-branch` | CTE INSERT 分支 | 已覆盖 |
| `VSH402` | `vastbase-sqlserver-if-cte-merge-branch` | CTE MERGE 分支 | 已覆盖 |
| `VSH403` | `vastbase-sqlserver-if-exec-create-view-cte-branch` | EXEC 包装的 CREATE VIEW CTE 分支 | 已覆盖 |
| `VSH404` | `vastbase-sqlserver-merge-cte-target-relation-binding` | MERGE 的 CTE 目标及底层 relation patch 传播 | 已覆盖 |

## 覆盖边界

本矩阵只列出可成功解析并具有最终 View 与 patch 期望的用例。未纳入该可执行夹具的语法不得在本矩阵中登记为已验证用例。
