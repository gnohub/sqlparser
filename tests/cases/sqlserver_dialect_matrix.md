# SQL Server 方言用例矩阵

本文件记录 SQL Server 方言转换层的回归用例。可执行夹具为 `tests/cases/sqlserver_dialect_input.json`。对每条 final 用例，runner 验证未修改 SQL 的反解析结果与输入逐字节一致、实际 View 与期望 JSON 结构相等，并独立执行每个 patch；patch 后 SQL 必须与 `patch.deparse` 逐字节一致，重新解析后再次反解析仍须一致，且 patch handle 与重新解析 handle 的 View 输出必须一致。用例提供 `bind_occurrences` 时，runner 还会对原 SQL 及每个 patch 后 SQL 的 `position`、`kind`、`key` 和原始 `sql` 逐项精确校验，重复 key 不合并，`position` 按整段 SQL 连续编号。

## 矩阵统计与 session 回归

夹具包含 629 条 `status = "final"` 用例和 1889 个独立 patch；其中 2 条用例及其 6 个 patch 含完整 bind occurrence 断言。91 条用例的期望 View 包含非空 `query_graph.session` 投影，覆盖 `S044` 至 `S046`、`SH295` 至 `SH333` 和 49 条 `MSSQL-*` session 用例。

View 校验采用 JSON 结构相等比较，对象键顺序和格式空白不参与比较；session action、item scope、target kind、name、value 类型、规范文本及顺序均属于比较范围。

## SQL/JSON 语义输入回归

以下用例验证 SQL/JSON 专用 AST 节点的输入字段遍历。字段顺序、relation 与 target 归属、原始 name selector 以及嵌套 `target_path` 均为精确 View 合同；每条用例还覆盖字段或关系替换、target 替换、列插入及 patch 后精确反解析。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH404 | `sqlserver-json-array-field-inputs` | `JSON_ARRAY([id], [name])` | 多字段按参数顺序进入 `fields`，分别归属 `JSON_ARRAY` arg 0 和 arg 1 |
| SH405 | `sqlserver-json-object-multi-pair-nested-value` | 多 pair `JSON_OBJECT` 嵌套 `JSON_VALUE` | value 字段按扁平 key/value 槽位归属，嵌套输入保留两层 `target_path` |
| SH406 | `sqlserver-json-arrayagg-expression-input` | `JSON_ARRAYAGG(COALESCE(...))` | 聚合输入内字段按表达式顺序输出并保留 `JSON_ARRAYAGG`、`COALESCE` 两层路径 |
| SH407 | `sqlserver-json-value-qualified-multi-target` | qualified `JSON_VALUE` 与普通 target 并列 | relation alias、target 顺序、限定字段 selector 和 JSON 输入字段归属 |

## SQL Server 函数参数角色回归

以下用例验证函数参数的语法角色与数据字段角色严格分离。`IDENTITY` 的数据类型参数以及日期函数的 datepart 参数不进入 `query_graph.fields`；其余表达式参数按原始顺序保留 relation、target、selector 和 `target_path.arg_index`。既有 `SH047`、`SH053` 分别覆盖三参数 `DATE_BUCKET` 和 `DATETRUNC`，本节补充可选 origin、表达式参数和同族函数组合。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| S113 | `sqlserver-identity-function-default-seed-increment` | `IDENTITY(int)` + `SELECT INTO` | 数据类型参数不作为字段输出 |
| S114 | `sqlserver-identity-function-explicit-seed-increment` | `IDENTITY(int, 1, 1)` + `SELECT INTO` | 数据类型参数不作为字段输出，seed 与 increment 保持常量角色 |
| SH408 | `sqlserver-date-bucket-optional-origin-inputs` | 四参数 `DATE_BUCKET` | 日期输入为 arg 2，可选 origin 为 arg 3，datepart 不作为字段输出 |
| SH409 | `sqlserver-dateadd-expression-inputs` | `DATEADD` + 表达式参数 | number 与 date 输入分别保留 arg 1、arg 2，datepart 不作为字段输出 |
| SH410 | `sqlserver-datediff-family-expression-inputs` | `DATEDIFF` 与 `DATEDIFF_BIG` 并列 | 两个 target 的四个日期输入保持各自归属，datepart 不作为字段输出 |
| SH411 | `sqlserver-datename-datepart-role` | `DATENAME` | date 输入保留 arg 1，datepart 不作为字段输出 |
| SH412 | `sqlserver-datepart-datepart-role` | `DATEPART` | date 输入保留 arg 1，datepart 不作为字段输出 |

## WITHIN GROUP 聚合排序回归

以下用例验证聚合函数直接参数、`WITHIN GROUP` 排序表达式和窗口分区字段按各自语义位置进入 View。排序字段的 `target_path.arg_index` 从函数直接参数数量开始连续编号；窗口字段继续使用独立的 `window_partition` 路径。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH413 | `sqlserver-string-agg-within-group-multi-sort` | `STRING_AGG` + 多排序字段 | 直接输入与两个排序字段按顺序输出，排序字段归属 arg 2、arg 3 |
| SH414 | `sqlserver-string-agg-within-group-order-expression` | 排序加法表达式 + 第二排序项 | 表达式字段保持嵌套路径，第二排序项归属 arg 3 |
| SH415 | `sqlserver-approx-percentile-multi-target` | 两个 approximate percentile target | 相同排序列分别归属 target 0、target 1 和对应函数 arg 1 |
| SH416 | `sqlserver-percentile-cont-window-partition-boundary` | `WITHIN GROUP` + `OVER (PARTITION BY ...)` | 聚合排序字段与窗口分区字段独立输出且不重复 |

## HAVING 值侧表达式回归

以下用例验证 `HAVING` 中直接字段与值侧表达式比较的 View 合同。值侧函数整体输出为单个 expression value，predicate 通过 value 索引引用该值，函数内部 bind 不提升为独立 value。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH417 | `sqlserver-having-direct-field-value-expression` | 分组字段与 `UPPER(@status)` 比较 | `HAVING` comparison 引用唯一 expression value，`field_match_kind=direct_field`，内部 bind 不单独输出 |

## 反向表达式谓词回归

以下用例验证标量 bind 位于比较左侧、字段表达式位于比较右侧时的 View 合同。predicate 必须通过 `right_field` 引用表达式中的实际字段，且只记录比较对侧的 bind，不得把 `AT TIME ZONE` 的时区文本提升为独立 value。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH418 | `sqlserver-at-time-zone-reverse-bind-expression-predicate` | `@ts = [created_at] AT TIME ZONE 'UTC'` | expression predicate、`right_field=created_at`、唯一 bind value 及原始方括号标识符保持稳定 |

## SELECT 目标片段展开回归

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH419 | `sqlserver-select-target-multi-replace-trailing-aliases` | 三输出项 SELECT 的尾项替换为两个带显式别名的方括号输出项 | replacement 在尾位置展开，两个别名独立保留且不继承旧 target 状态；独立 insert patch 验证列表顺序与方括号拼写 |

## OUTPUT 末尾边界回归

以下用例要求 `OUTPUT` 恢复器仅在存在后继子句时保留必要边界；语句末尾不得增加空格，末尾分号必须直接跟随最后一个 OUTPUT target。每条用例均独立执行 target replace 与 target insert patch，并精确比较反解析结果。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH420 | `sqlserver-merge-output-action-terminal-semicolon` | `MERGE ... OUTPUT $action;` | MERGE 末尾 OUTPUT 无尾空格，分号位置不变 |
| SH421 | `sqlserver-update-output-terminal` | `UPDATE ... OUTPUT INSERTED.a` | 无后继 WHERE/FROM 时不生成右边界 |
| SH422 | `sqlserver-delete-output-terminal` | `DELETE ... OUTPUT DELETED.id` | 无后继 WHERE/FROM 时不生成右边界 |

## 嵌套 MERGE INSERT 定位回归

以下用例验证同一 statement 中多个嵌套 MERGE 的 INSERT 分支使用包含 DML 索引的独立 selector。目标列、完整 VALUES cell 和列值对 patch 必须只作用于指定 MERGE，其他嵌套 MERGE 及外层 INSERT 原文保持不变。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH423 | `sqlserver-nested-merge-insert-selector-uniqueness` | 外层 `INSERT ... SELECT` 组合两个带 `OUTPUT` 的嵌套 MERGE | 两个非零 DML 的目标列列表、目标列和 cell selector 唯一，独立替换及成对插入互不影响 |
| SH424 | `sqlserver-merge-matched-delete-action` | 带条件的 matched DELETE、matched UPDATE 和 not-matched-by-target INSERT | DELETE 作为独立 `WHEN MATCHED ... THEN DELETE` 分支，三个分支保持绝对顺序与各自 selector；3 个独立 patch 覆盖 DELETE 分支条件、UPDATE assignment 和 INSERT cell 替换 |

## OUTPUT target/sink column 成对插入回归

成对 `insert_column` 只适用于 sink `OUTPUT ... INTO` 通道包含显式、非空 sink column list，且改写前 OUTPUT target 数与 sink column 数严格相等的场景。操作按同一序号原子插入一个 OUTPUT target 和一个 sink column。SQL Server 原本合法的不等长 `OUTPUT` 仍可解析和反解析，但不支持这个成对插入操作；client `OUTPUT` 和未显式列出 sink column 的 `OUTPUT ... INTO` 也不纳入该成对改写边界。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH425 | `sqlserver-insert-output-into-eight-target-sink-column-pairs` | INSERT 8 OUTPUT target ↔ 8 显式 sink column | 头部原子插入 1 组后为 9↔9，View 与反解析保持按序配对 |
| SH426 | `sqlserver-update-output-into-eight-target-sink-column-pairs` | UPDATE 8 OUTPUT target ↔ 8 显式 sink column | 根 SQL 仅枚举 SET/WHERE 的 3 个 bind，不将 `INTO @profile_audit` 误认为 bind；中部成对插入含重复 `@audit_tag` 和匿名 `?` 的 OUTPUT 表达式后为 6 个 occurrence，9↔9 顺序保持 |
| SH427 | `sqlserver-delete-output-into-eight-target-sink-column-pairs` | DELETE 8 OUTPUT target ↔ 8 显式 sink column | 尾部原子插入 1 组后为 9↔9，View 与反解析保持按序配对 |

## 嵌套 UPDATE 赋值列表回归

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH428 | `sqlserver-nested-update-output-assignment-contract` | `INSERT ... SELECT` 内嵌带 `OUTPUT` 的 `UPDATE` | `assignment[D][A]` 定位 D1 双赋值；插入、整项替换和删除 3 个 patch 保持嵌套与 `OUTPUT` |

## Query Graph 引号别名合同

`relations[].alias_quoted_identifier` 仅在 relation alias 使用方括号定界时为 `true`。`targets[].output_quoted_identifier` 在 output name 来自方括号显式别名，或无显式别名时继承方括号字段名时为 `true`；View JSON 不输出值为 `false` 的键。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH429 | `sqlserver-graph-quoted-relation-alias-and-target-output` | 方括号 relation/派生表 alias 与 output name | 两个引号标志、字段名继承及 2 个 output alias patch |

## MERGE INSERT 独立列值改写

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH430 | `sqlserver-merge-omitted-insert-column-value-independent` | 省略目标列列表的 `WHEN NOT MATCHED THEN INSERT VALUES (...)` | 省略状态仍输出 `target_list_selector`；3 个独立 patch 分别验证 column-only 物化列列表、value-only 追加 cell 和现有 `merge_insert_cell` 替换；与既有 paired 模式共同覆盖 `insert_column` 三态合同 |

## INSERT VALUES 回归：bind 与表达式混合

`MSSQL-BM001` 至 `MSSQL-BM010` 覆盖 SQL Server VALUES cell 中 `DEFAULT`、`NULL`、bind、literal 和 expression 的组合，以及两行 VALUES 输入。

T-SQL `@name` 引用要求已有变量或参数作用域；`MSSQL-BM009` 的 `?` 仅作为 JDBC/ODBC 驱动参数标记。`DEFAULT` 始终是独立 cell。每条用例逐 cell 断言 `row`、`column`、`kind` 和 `selector`；逐个直接 bind 断言 `bind_key`、`bind_kind`、`bind_sql`、`bind_position` 和 `selector`，并通过表达式后的直接 bind 位置验证嵌套 bind 已计入全局序号；`GETDATE()`、`CURRENT_TIMESTAMP` 断言为 `kind=expression`，且不得出现在 `query_graph.fields[].column` 中。

| ID | SQL 形态 | 关键边界 |
| --- | --- | --- |
| `MSSQL-BM001` | 三个 `@` bind + 尾部 `GETDATE()` | `@` 引用要求已有变量/参数作用域 |
| `MSSQL-BM002` | 首位 `GETDATE()` + 三个 `@` bind | 首列 expression 不得造成 bind 错位 |
| `MSSQL-BM003` | `@` bind、`CURRENT_TIMESTAMP`、`@` bind、`GETDATE()` 交错 | 两种时间 expression 均不得出现在 `query_graph.fields[].column` 中 |
| `MSSQL-BM004` | bind + `NULL` + `GETDATE()` + bind | `NULL` 为 literal，`GETDATE()` 为 expression |
| `MSSQL-BM005` | bind + 独立 `DEFAULT` + `CURRENT_TIMESTAMP` + bind | `DEFAULT` 不嵌入其他表达式 |
| `MSSQL-BM006` | bind + Unicode literal + `GETDATE()` + bind | literal 与 expression 不改变后续 bind 位置 |
| `MSSQL-BM007` | 直接 bind + `COALESCE(@retry_count, 0)` + `GETDATE()` + 直接 bind | 尾部直接 bind 为 position 3，验证嵌套 bind 已计数 |
| `MSSQL-BM008` | 直接 bind + 包含 `@enabled` 的 `CASE` 表达式 + `CAST(@amount AS int)` + `CURRENT_TIMESTAMP` + 直接 bind | 尾部直接 bind 为 position 4，验证两个嵌套 bind 均已计数 |
| `MSSQL-BM009` | 三个独立 JDBC/ODBC `?` + `GETDATE()` | `?` 仅作为驱动模板参数标记，不嵌入复杂表达式 |
| `MSSQL-BM010` | schema-qualified 方括号标识符 + 不规则空白的两行 VALUES | 两行混合 `@` bind、独立 `DEFAULT`、literal、`GETDATE()`、`CURRENT_TIMESTAMP` |

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
| S043Q | unsupported 关键字受保护标识符 | 方括号标识符中的 `OUTPUT`、`EXEC`、`PIVOT` 不触发 unsupported |
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
| S081 | `TOP (?)` + WHERE `?` | `TOP` 中的位置参数计入全局 bind 序号 |
| S082 | `GO` 分隔的多 `UPDATE` + `MERGE` | 全文按序枚举命名/匿名 bind，保留重复 `@merge_value` 并排除注释伪 bind；复杂 patch 覆盖子查询、CAST、CASE、`OFFSET/FETCH`、`@@ROWCOUNT` 及标识符/字符串保护区，并精确校验删除/插入后重编号 |
| S083 | `sqlserver-select-derived-query-graph` | 派生表字段向内层真实表字段的 `query_graph` 来源链路映射和 `output_name` |
| S084 | `MERGE` + `?` 参数 | MERGE 的 target/source relation、UPDATE assignment、INSERT values 和 bind 映射 |
| S085 | `sqlserver-field-match-kind-direct-and-expression` | 直接字段条件 + 函数包裹字段条件 | `query_graph.values[].field_match_kind` 区分 `direct_field` 和 `expression_field` |
| S086 | `sqlserver-expression-field-case-expression-value` | CASE 返回字段再与 bind 比较 | CASE 表达式字段输出 `expression_field` value 关系 |
| S087 | `sqlserver-expression-field-multi-field-expression-value` | `CONCAT(secret, id)`、`secret + id` 与 bind 比较 | 表达式内字段分别保留 `expression_field` value 关系 |
| S088 | `sqlserver-expression-field-value-side-expression` | 字段与值侧函数、拼接、CAST 比较 | 值侧表达式输出 `kind=expression`，不暴露 direct bind |
| S089 | `sqlserver-expression-field-dml-expression-values` | INSERT/UPDATE 表达式赋值 | DML cell/assignment 输出 `kind=expression` |
| S090 | `sqlserver-update-named-bind-rhs-crypto-source` | `UPDATE ... SET protected = @name` | UPDATE SET 右值为命名参数的保护字段来源表达 |
| S091 | `sqlserver-update-multiple-bind-rhs-crypto-source` | `UPDATE ... SET protected1 = @name1, protected2 = @name2` | 多个保护字段的 SET bind、字段归属和全局 bind 序号 |
| S092 | `sqlserver-like-escape-literal` | `LIKE 'A!_%' ESCAPE '!'` | SQL Server 字面量 ESCAPE 输出到 `values[].like_escape` |
| S093 | `sqlserver-not-like-escape-named-bind` | `NOT LIKE @pattern ESCAPE @escape_char` | 命名参数 pattern 与 escape 分别保留公开 SQL 和全局序号 |
| S094 | `sqlserver-like-escape-question-bind` | `LIKE ? ESCAPE ?` | JDBC 风格位置参数 pattern 和 escape 的结构化输出 |
| S095 | `sqlserver-like-without-explicit-escape` | `LIKE @pattern` | 无显式 ESCAPE 时不输出 `like_escape` |
| S096 | `sqlserver-bitwise-binary-operators` | `&`、`|`、`^` | 位运算表达式、输出项 `target_path` 和 WHERE bind 归属 |
| S097 | `sqlserver-bitwise-not-operator` | `~column` | 一元位取反表达式输出 |
| S098 | `sqlserver-not-greater-less-comparison` | `!>`、`!<` | SQL Server 否定比较运算符的字段和值关系 |
| S099 | `sqlserver-string-concat-pipes` | `first_name || last_name` | 管道拼接表达式输出和字段归属 |
| S100 | `sqlserver-like-bracket-wildcards` | `LIKE 'A[^b]_%'` | SQL Server LIKE bracket wildcard 保持为公开 pattern 字面量 |
| S101 | `sqlserver-at-time-zone-expression` | `AT TIME ZONE` | 时间区域表达式作为值侧表达式参与比较 |
| S102 | `sqlserver-is-distinct-from-bind` | `IS DISTINCT FROM @bind` | `IS DISTINCT FROM` 不误判为表变量，并输出 bind 关系 |
| S103 | `TOP ... PERCENT` | `SELECT TOP (10) PERCENT ...` | 百分比 TOP 子句解析并在 deparse 中保留公开形态 |
| S104 | `TOP ... WITH TIES` | `SELECT TOP (10) WITH TIES ...` | 同分保留 TOP 子句解析并在 deparse 中保留公开形态 |
| S105 | `TOP ... PERCENT WITH TIES` | `SELECT TOP (10) PERCENT WITH TIES ...` | 官方组合 TOP 形态解析并保留公开输出 |
| S106 | `WITH (NOLOCK)` 表提示 | `SELECT ... FROM table WITH (NOLOCK)` | 表提示原片段由方言私有 state 保留，deparse 恢复公开 SQL |
| S107 | `OPTION (RECOMPILE)` 查询提示 | `SELECT ... OPTION (RECOMPILE)` | 查询提示原片段由方言私有 state 保留，deparse 恢复公开 SQL |
| S108 | `FOR JSON PATH` | `SELECT ... FOR JSON PATH` | JSON 输出后缀由方言私有 state 保留，核心 View JSON 仍输出表和字段归属 |
| S109 | 嵌套 `TOP` | `SELECT ... FROM (SELECT TOP (...) ...)` | 子查询作用域中的 `TOP` 独立转换和公开反解析 |
| S110 | `FOR JSON` + `OPTION` | `SELECT ... FOR JSON PATH ... OPTION (...)` | 多个查询后缀按 SQL Server 公开顺序恢复 |
| S111 | 外层和内层 `TOP` | `SELECT TOP (...) ... FROM (SELECT TOP (...) ...)` | 多作用域 `TOP` 同时转换和恢复 |
| S112 | 多语句第二条 `FOR JSON` | `SELECT ...; SELECT ... FOR JSON AUTO` | `FOR JSON` 后缀按原 statement 序号恢复，避免挂到前一条语句 |
| S115 | `sqlserver-update-from-compound-rhs` | `UPDATE ... FROM` 中的复合赋值右值 | source field、命名参数和 literal 通过 `rhs_fields` 和 `rhs_values` 归属同一 assignment；source alias 保持稳定，assignment 替换或插入后的方括号与 `INNER JOIN` 必须逐字节保留 |

## INSERT 与 OUTPUT 用例

| ID | 用例 | 覆盖点 |
| --- | --- | --- |
| SU003 | INSERT client `OUTPUT` | `INSERTED` 字段、client channel、公开 SQL 恢复 |
| SH336 | 省略 `INTO` 的 INSERT 集合查询 | 原始 `UNION ALL` 输入、别名和 Unicode literal |
| SH337 | 显式 `INTO` 的 INSERT SELECT | 来源查询与 client channel |
| SH338 | 省略 `INTO` 的单行 VALUES | INSERT 目标和结果字段 |
| SH339 | 省略 `INTO` 的多行 VALUES | 多行写入和结果字段 |
| SH340 | `DEFAULT VALUES` | 无显式目标列的结果字段 |
| SH341 | CTE + INSERT SELECT | CTE 来源块和结果通道 |
| SH342 | 基础 INSERT `OUTPUT` | 兼容原 `SU003` 形态 |
| SH343 | `INSERTED.*` | target-after 星号引用 |
| SH344 | 表达式和别名 | OUTPUT target 表达式 patch 入口 |
| SH345 | 多 target 与 bind | target 顺序、字段引用和 bind |
| SH346 | `OUTPUT ... INTO table` | sink channel 和 sink relation |
| SH347 | sink 显式列列表 | sink column selector |
| SH348 | table variable sink | `OUTPUT INTO @table_variable` |
| SH349 | sink/client 双通道 | 有序双通道和独立 target 列表 |
| SH350 | UPDATE before/after | `DELETED` 与 `INSERTED` 引用 |
| SH351 | UPDATE 来源字段 | target-after 和 source 引用 |
| SH352 | DELETE before | `DELETED` 引用 |
| SH353 | DELETE JOIN 来源字段 | DELETE target 和 source 引用 |
| SH354 | MERGE `$action` | action target 与公开 SQL 恢复 |
| SH355 | MERGE 全部引用类型 | `$action`、before、after、source |
| SH356 | 嵌套 DML table source | 父子 DML 与内层 result channel |
| SH357 | UPDATE TOP + OUTPUT | DML TOP 与结果通道组合 |
| SH358 | INSERT 目标 hint + OUTPUT | hint 与结果通道组合恢复 |
| SH359 | 字符串中的 OUTPUT 关键字 | 受保护文本不触发语法识别 |
| SH360 | 注释中的 OUTPUT 关键字 | 注释边界和尾注释恢复 |
| SH361 | 标识符中的 OUTPUT 关键字 | 方括号标识符边界 |
| SH362 | 来源子查询中的 OUTPUT 文本 | 嵌套作用域与受保护文本 |
| SH363 | INSERT SELECT 来源表 hint | 来源 hint 不会恢复到 INSERT 目标 |
| SH364 | INSERT 目标与来源表 hint + OUTPUT | 两个 hint 按各自锚点恢复 |
| SH365 | OUTPUT 中的定界保留字列 | `INSERTED.[select]` 按列引用解析 |
| SH366 | DELETE 等价定界别名 | 不同定界形式和转义后的同名别名归一为同一目标关系 |
| SH367 | 多语句 OUTPUT 通道 | INSERT、UPDATE、DELETE 的结果通道按语句独立关联 |
| SH368 | 官方 IF EXISTS 形态 | 查询条件、表提示、Unicode 常量和双分支 |
| SH369 | 无 ELSE 的 IF | 单条件分支 |
| SH370 | 常量布尔条件 | 无字段条件的 values/predicate 输出 |
| SH371 | 分号分隔 BEGIN 块 | 双分支多语句顺序 |
| SH372 | 换行分隔 BEGIN 块 | 无分号语句边界 |
| SH373 | dangling ELSE | ELSE 归属最近未闭合 IF |
| SH374 | ELSE IF 链 | else 分支中的嵌套 IF |
| SH375 | NOT EXISTS | 否定查询条件和 bind |
| SH376 | 标量子查询条件 | 括号 SELECT、聚合和 bind |
| SH377 | 嵌套布尔条件 | AND、OR、NOT 谓词树和三个 bind |
| SH378 | 函数条件 | COALESCE 参数和值输出 |
| SH379 | CASE 条件 | CASE 表达式和值输出 |
| SH380 | UPDATE/INSERT OUTPUT 分支 | 每个控制单元独立维护 OUTPUT 状态 |
| SH381 | DELETE/MERGE OUTPUT 分支 | DELETE row image、MERGE `$action` 和双 row image |
| SH382 | DDL 分支 | DROP IF EXISTS 与 CREATE TABLE |
| SH383 | 事务分支 | 系统变量、COMMIT 和 ROLLBACK |
| SH384 | 分号分隔根语句 | IF 前后普通语句的根顺序 |
| SH385 | 换行分隔根语句 | 无分号根语句边界 |
| SH386 | 受保护文本 | 字符串、注释和定界标识符中的控制关键字 |
| SH387 | UNION ALL 换行 | 集合查询不被拆成多个 branch item |
| SH388 | 表提示换行 | `WITH (NOLOCK)` 不被识别为新语句 |
| SH389 | 三层嵌套 IF | 多级控制节点与分支顺序 |
| SH390 | DROP USER IF EXISTS 后接 IF | DDL 内部 IF 与控制 IF 消歧 |
| SH391 | NULL/BETWEEN/IN 条件 | 常用条件运算与 bind |
| SH392 | 右侧标量子查询 | 官方括号查询条件和跨块 bind |
| SH393 | CTE 分支 | BEGIN 块内的分号前导 CTE |
| SH394 | BEGIN/END 可选分号 | 官方块分号形态 |
| SH395 | DROP TABLE IF EXISTS 后接 IF | 无分号 DDL 与控制流边界 |
| SH396 | DROP TABLE 后接 IF | 不含 `IF EXISTS` 的无分号 DDL 与控制流边界 |
| SH397 | DROP TABLE 后接 IF EXISTS 控制语句 | 完整 DROP 目标与控制条件消歧 |
| SH398 | 换行 DROP TABLE IF EXISTS 后接 IF | DROP 条件子句与后续控制语句消歧 |
| SH399 | CTE UPDATE 分支 | CTE 与 UPDATE/SET 续行保持为同一叶语句 |
| SH400 | CTE DELETE 分支 | CTE 与 DELETE 续行保持为同一叶语句 |
| SH401 | CTE INSERT 分支 | CTE、INSERT 与来源 SELECT 保持为同一叶语句 |
| SH402 | CTE MERGE 分支 | CTE 与完整 MERGE action 保持为同一叶语句 |
| SH403 | CREATE VIEW CTE 分支 | 视图定义与换行 CTE 保持为同一叶语句 |

## 覆盖边界

本矩阵只列出可成功解析并具有最终 View 与 patch 期望的用例。未纳入该可执行夹具的语法边界由 `doc/sqlserver_official_syntax_coverage.csv` 维护。

## DML 来源字段链路补充用例

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH236 | `sqlserver-update-from-source-field-graph` | `UPDATE ... SET target = source.column FROM ...` | UPDATE FROM 赋值右侧真实表来源字段输出 `source_field`，JOIN/WHERE 字段对比输出 `right_field` |
| SH237 | `sqlserver-insert-select-source-block-graph` | `INSERT ... SELECT ... FROM ...` | INSERT SELECT 的目标列、来源块和来源字段链路 |
| SH238 | `sqlserver-merge-source-target-graph` | `MERGE INTO ... USING ...` | MERGE 的 source/target 字段链路和来源字段表达 |
| SH254 | `sqlserver-select-or-predicate-order-by-lineage` | `WHERE field = @bind OR field = @bind ORDER BY ...` | OR 谓词树保留两个比较子节点、bind 和独立 ORDER BY 字段归属 |

## 混合模型基础形态补充用例

以下用例覆盖 `MIXED_MODEL` 条目的基础形态。完整官方语法仍按 `doc/sqlserver_official_syntax_coverage.csv` 标记为需要 SQL Server 专用模型。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH239 | `sqlserver-mixed-create-database-basic` | `CREATE DATABASE [appdb]` | 基础数据库创建语句 |
| SH240 | `sqlserver-mixed-drop-database-basic` | `DROP DATABASE [appdb]` | 基础数据库删除语句 |
| SH241 | `sqlserver-mixed-create-schema-basic` | `CREATE SCHEMA [audit]` | 基础 schema 创建语句 |
| SH242 | `sqlserver-mixed-drop-schema-basic` | `DROP SCHEMA [audit]` | 基础 schema 删除语句 |
| SH243 | `sqlserver-mixed-create-role-basic` | `CREATE ROLE [app_role]` | 基础 role 创建语句 |
| SH244 | `sqlserver-mixed-drop-role-basic` | `DROP ROLE [app_role]` | 基础 role 删除语句 |
| SH245 | `sqlserver-mixed-create-sequence-basic` | `CREATE SEQUENCE ... START WITH ...` | 基础 sequence 创建语句 |
| SH246 | `sqlserver-mixed-alter-sequence-basic` | `ALTER SEQUENCE ... RESTART WITH ...` | 基础 sequence 修改语句 |
| SH247 | `sqlserver-mixed-drop-sequence-basic` | `DROP SEQUENCE ...` | 基础 sequence 删除语句 |
| SH248 | `sqlserver-mixed-drop-view-basic` | `DROP VIEW [dbo].[v_users]` | 基础 view 删除语句 |
| SH249 | `sqlserver-mixed-drop-statistics-basic` | `DROP STATISTICS ...` | 基础 statistics 删除语句 |
| SH250 | `sqlserver-mixed-select-into-basic` | `SELECT ... INTO ... FROM ...` | 基础 `SELECT INTO` 目标表输出 |
| SH251 | `sqlserver-mixed-contains-basic` | `CONTAINS(column, @term)` | 基础全文谓词和 bind 输出 |
| SH252 | `sqlserver-mixed-freetext-basic` | `FREETEXT(column, @term)` | 基础全文谓词和 bind 输出 |
| SH253 | `sqlserver-regexp-like-function-predicate` | `REGEXP_LIKE(column, @pat)` | 函数谓词复用 `fields/values/predicates` 输出字段、bind 和 expression predicate |
| SH255 | `sqlserver-mixed-create-table-as-select-basic` | `CREATE TABLE ... AS SELECT ...` | 基础 CTAS 解析、来源查询和反解析 |
| SH256 | `sqlserver-mixed-aliasing-basic` | `SELECT expression AS alias, expression alias FROM ...` | SELECT 列别名和表别名基础形态 |
| SH257 | `sqlserver-mixed-subquery-basic` | `WHERE column IN (SELECT ... FROM ...)` | 外层 membership 字段与 `IN` predicate、内层 target/过滤字段、bind 和来源表分别归属，7 个 patch 覆盖两层 selector |
| SH259 | `sqlserver-mixed-alter-table-add-constraint-basic` | `ALTER TABLE ... ADD CONSTRAINT ... PRIMARY KEY` | 基础表约束新增 DDL |
| SH260 | `sqlserver-mixed-drop-type-basic` | `DROP TYPE [schema].[type]` | 基础 type 删除 DDL |
| SH261 | `sqlserver-mixed-create-user-basic` | `CREATE USER [user]` | 基础 user 创建 DDL |
| SH262 | `sqlserver-mixed-drop-user-if-exists-basic` | `DROP USER IF EXISTS [user]` | 基础 user 删除 DDL 和 `DROP USER` 公开形态恢复 |
| SH263 | `sqlserver-mixed-drop-role-drop-user-ordinal` | `DROP ROLE ...; DROP USER ...` | 多语句中 `DROP ROLE` 与 `DROP USER` 按 occurrence 独立恢复 |
| SH264 | `sqlserver-mixed-create-user-without-login` | `CREATE USER [user] WITHOUT LOGIN` | SQL Server user 无 login 映射形态 |
| SH265 | `sqlserver-mixed-create-user-for-login` | `CREATE USER [user] FOR LOGIN [login]` | SQL Server user 到 login 映射形态 |
| SH266 | `sqlserver-mixed-create-user-from-external-provider` | `CREATE USER [user] FROM EXTERNAL PROVIDER` | SQL Server Entra provider user 形态 |
| SH267 | `sqlserver-mixed-create-user-with-options` | `CREATE USER ... WITH DEFAULT_SCHEMA ...` | SQL Server user 选项 tail 保留 |
| SH268 | `sqlserver-mixed-create-user-for-certificate` | `CREATE USER ... FOR CERTIFICATE ...` | SQL Server certificate user 形态 |
| SH269 | `sqlserver-mixed-create-user-for-asymmetric-key` | `CREATE USER ... FOR ASYMMETRIC KEY ...` | SQL Server asymmetric key user 形态 |
| SH270 | `sqlserver-mixed-alter-role-add-member` | `ALTER ROLE ... ADD MEMBER ...` | SQL Server role 增加成员 |
| SH271 | `sqlserver-mixed-alter-role-drop-member` | `ALTER ROLE ... DROP MEMBER ...` | SQL Server role 删除成员 |
| SH272 | `sqlserver-mixed-alter-role-with-name` | `ALTER ROLE ... WITH NAME = ...` | SQL Server role 重命名 |
| SH273 | `sqlserver-mixed-alter-schema-transfer-object` | `ALTER SCHEMA ... TRANSFER schema.object` | SQL Server schema 对象迁移 |
| SH274 | `sqlserver-mixed-alter-schema-transfer-type` | `ALTER SCHEMA ... TRANSFER TYPE::schema.type` | SQL Server schema type 迁移 |
| SH275 | `sqlserver-mixed-create-role-authorization` | `CREATE ROLE ... AUTHORIZATION ...` | SQL Server role owner 指定 |
| SH276 | `sqlserver-mixed-alter-user-name` | `ALTER USER ... WITH NAME = ...` | SQL Server user 重命名 |
| SH277 | `sqlserver-mixed-alter-user-options` | `ALTER USER ... WITH DEFAULT_SCHEMA ...` | SQL Server user 多选项修改 |
| SH278 | `sqlserver-mixed-alter-user-login` | `ALTER USER ... WITH LOGIN = ...` | SQL Server user login 重映射 |
| SH279 | `sqlserver-mixed-alter-user-external-provider` | `ALTER USER ... FROM EXTERNAL PROVIDER ...` | SQL Server external provider user 形态 |
| SH280 | `sqlserver-mixed-alter-authorization-object` | `ALTER AUTHORIZATION ON OBJECT::... TO ...` | SQL Server 对象 owner 迁移 |
| SH281 | `sqlserver-mixed-alter-authorization-schema-owner` | `ALTER AUTHORIZATION ... TO SCHEMA OWNER` | SQL Server schema owner 迁移目标 |
| SH282 | `sqlserver-mixed-alter-authorization-schema` | `ALTER AUTHORIZATION ON SCHEMA::... TO ...` | SQL Server schema owner 迁移 |
| SH283 | `sqlserver-mixed-create-schema-authorization` | `CREATE SCHEMA ... AUTHORIZATION ...` | SQL Server schema owner 指定 |
| SH284 | `sqlserver-mixed-drop-schema-if-exists` | `DROP SCHEMA IF EXISTS ...` | SQL Server schema 条件删除 |
| SH285 | `sqlserver-mixed-create-application-role` | `CREATE APPLICATION ROLE ... WITH PASSWORD ...` | SQL Server application role 创建 |
| SH286 | `sqlserver-mixed-alter-application-role-name` | `ALTER APPLICATION ROLE ... WITH NAME = ...` | SQL Server application role 重命名 |
| SH287 | `sqlserver-mixed-alter-application-role-options` | `ALTER APPLICATION ROLE ... WITH PASSWORD ...` | SQL Server application role 多选项修改 |
| SH288 | `sqlserver-mixed-drop-application-role` | `DROP APPLICATION ROLE ...` | SQL Server application role 删除 |
| SH289 | `sqlserver-mixed-create-synonym` | `CREATE SYNONYM ... FOR ...` | SQL Server synonym 创建 |
| SH290 | `sqlserver-mixed-drop-synonym-if-exists` | `DROP SYNONYM IF EXISTS ...` | SQL Server synonym 条件删除 |
| SH291 | `sqlserver-mixed-create-type-alias` | `CREATE TYPE ... FROM ...` | SQL Server alias type 创建 |
| SH292 | `sqlserver-mixed-alter-database-compatibility-level` | `ALTER DATABASE ... SET COMPATIBILITY_LEVEL = ...` | SQL Server database compatibility level 修改 |
| SH293 | `sqlserver-mixed-drop-index-if-exists-on-object` | `DROP INDEX IF EXISTS ... ON ...` | SQL Server index 条件删除 |
| SH294 | `sqlserver-mixed-update-statistics-fullscan` | `UPDATE STATISTICS ... WITH FULLSCAN` | SQL Server statistics 更新 |
| SH295-SH333 | `sqlserver-set-*` | `SET ...` | SQL Server 基础会话/执行环境 `SET` 语句、公开 SQL 恢复和内部 sentinel 隐藏 |
| SH334 | `sqlserver-table-hints-join-alias` | 多表 JOIN + `WITH (NOLOCK)` / `WITH (FORCESEEK)` | 多表表提示按表源顺序恢复，字段和 JOIN 归属保持不变 |
| SH335 | `sqlserver-query-hints-multiple` | `OPTION (RECOMPILE, USE HINT(...))` | 多查询提示参数按原 SQL 片段恢复，bind 归属保持不变 |
| SH336 | `sqlserver-merge-cte-target-relation-binding` | `WITH cte AS (...) MERGE INTO cte ...` | MERGE 目标识别为 CTE 并关联来源块，底层 relation patch 同步更新限定列 |

## Query Graph 分段引号标识合同

relation 的限定名按段记录方括号定界状态：`database_quoted_identifier`、`schema_quoted_identifier`、既有的 object `quoted_identifier`，以及存在 database link 时的 `link_quoted_identifier`；DML 目标列和 `OUTPUT ... INTO` sink column 共用 `dml_column.quoted_identifier`。每个标志只描述对应名称段，未定界或不存在的段不输出该键，不能由名称大小写推断。SQL Server 入口没有 database link，因此 `link_quoted_identifier` 不适用；公共 C 结构生命周期由 `tests/unit/test_identifier_spelling.c` 验证。

| 用例 ID | 用例名称 | 语句形态 | 验证重点 |
| --- | --- | --- | --- |
| SH431 | `sqlserver-quoted-identifier-three-part-dml-matrix` | 5 条语句覆盖 SELECT、INSERT、UPDATE FROM、DELETE 与 MERGE 的三段 relation | database/schema/object 分段标志与普通/MERGE `dml_column.quoted_identifier`；7 个独立 patch 覆盖五类 relation 逐段重算和 MERGE 列 quoted↔unquoted 重算 |
| SH432 | `sqlserver-output-into-quoted-identifier-sink-matrix` | INSERT、UPDATE、DELETE、MERGE 的 `OUTPUT ... INTO` 三段 sink relation 与 sink column | 四类 result channel 的 sink relation 分段标志及 `sink_columns[].quoted_identifier`；7 个独立 patch 覆盖四个 sink relation、两个 sink column 和源 INSERT relation 的 fresh View 重算 |

## 官方 hook 覆盖用例

`tests/cases/sqlserver_dialect_input.json` 已包含 250 条按官方 `CURRENT` 条目生成的用例。该部分覆盖函数、类型/常量、排序规则、`TOP` 官方形态和简单 `RENAME OBJECT` 等可通过现有 AST 与方言 hook 承载的官方条目。表提示和查询提示以 `MIXED_MODEL` 基础形态覆盖，完整结构化 hint 语义仍以专用模型为边界。

## 维护要求

- 新增 SQL Server 支持项必须同步更新 `tests/cases/sqlserver_dialect_input.json`、本矩阵和可执行回归测试。
- 无法保证语义等价的 SQL Server 专有语法必须返回 `SQLPARSER_STATUS_UNSUPPORTED`。
