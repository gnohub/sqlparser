# KingbaseES SQLServer 兼容入口用例矩阵

可执行夹具为 `tests/cases/kingbase_sqlserver_dialect_input.json`，由独立的 KingbaseES SQLServer 入口和 case runner 验证。

## 审计基线

- SQLServer 兼容语法来源为 KingbaseES V9R4C12 官方资料：[V9R4 SQLServer 兼容版](https://help.kingbase.com.cn/v9.4.12/index.html)、[SQLServer 兼容性总览](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/kes-vs-sqlserver/overview.html)、[SQL 参考](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html)。
- 官方 V9R4 发布说明明确将该产品定义为 SQLServer 兼容版本，并列出 SQLServer 数据类型、函数、系统变量和 DML 能力：[V9R4 发布说明](https://help.kingbase.com.cn/v9.4.12/intro/releasenotes-external-v9/V9.4.10.html)。
- V8 的 `dbmode` 只有 `pg`、`oracle`、`mysql`，没有 `sqlserver`。`initdb -m sqlserver` 属于 V9R4 SQLServer 兼容版：[V8 initdb](https://help.kingbase.com.cn/v8/admin/reference/ref-server/initdb.html)、[SQLServer 迁移最佳实践](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/data_migration/best_practice/sqlserver_best_pratice.html)。
- `kingbase-sqlserver` 解析入口不接收服务端版本参数；入口只按本矩阵定义的语法并集解析，不执行版本判断。

## 夹具状态

- 45 条用例全部为 `status = "final"`。
- 174 个独立 patch 随原 SQLServer 用例保留。
- 当前首批用例不包含 `bind_occurrences` 断言。
- 所有条目均选自 `tests/cases/sqlserver_dialect_input.json`；除增加 `kingbase-sqlserver-` 名称前缀外，SQL、状态、期望 View 和 patch 不变。
- `KBSS001` 至 `KBSS045` 按 fixture 的 `items` 数组顺序进行 1-based 映射。
- Runner：`tests/unit/test_kingbase_sqlserver_dialect_case_matrix.c`。

## 官方依据与覆盖

| 分类 | 覆盖用例 | 官方依据 | 覆盖边界 |
| --- | --- | --- | --- |
| SQLServer 词法、常量与变量 | `KBSS001`、`KBSS011`-`KBSS014`、`KBSS028`-`KBSS029`、`KBSS031`-`KBSS035` | [常量](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/basic_element/constant.html)、[用户变量](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/language_elements/local_variable/overview.html)、[系统配置](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/system_config.html)、[关键字](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/other/keyword.html) | `N'...'`、`0x...`、`@name`、`@@ROWCOUNT`、`?` 客户端占位符、`#temp`、方括号定界 token |
| SELECT、JOIN、CTE、集合与分页 | `KBSS001`-`KBSS005`、`KBSS015`-`KBSS019`、`KBSS028`-`KBSS032`、`KBSS037`-`KBSS039`、`KBSS041`、`KBSS044`-`KBSS045` | [SQLServer 查询目录](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html) | TOP、OFFSET/FETCH、JOIN、WHERE、GROUP BY、HAVING、窗口、派生表、UNION ALL/EXCEPT/INTERSECT、SELECT INTO、普通和显式列名 CTE |
| TOP | `KBSS002`、`KBSS031`-`KBSS032` | [TOP](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html)、[INSERT TOP](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/statement/general/insert.html) | 常量、`@row_count`、`PERCENT WITH TIES`；不把 TOP 当 LIMIT |
| INSERT、UPDATE 与 DELETE | `KBSS006`-`KBSS010`、`KBSS040`、`KBSS044` | [INSERT](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/statement/general/insert.html)、[DML 目录](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html) | 单行/多行 VALUES、INSERT SELECT、UPDATE 多 assignment、DELETE、`UPDATE ... FROM ... JOIN`、三段限定 relation 和 `@` bind |
| MERGE | `KBSS030`、`KBSS044` | [MERGE](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html) | `MERGE INTO ... USING ... WHEN MATCHED ... WHEN NOT MATCHED BY TARGET ...` 基础形态和三段限定 relation |
| 类型、转换、函数与表达式 | `KBSS011`、`KBSS014`-`KBSS016`、`KBSS020`、`KBSS027`、`KBSS033`-`KBSS035`、`KBSS037`-`KBSS038` | [SQLServer 类型与函数目录](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html)、[V9R4 发布说明](https://help.kingbase.com.cn/v9.4.12/intro/releasenotes-external-v9/V9.4.10.html) | INT/BIGINT/DECIMAL/NVARCHAR/BIT/DATETIME2、IDENTITY、CONVERT style、ISNULL/GETDATE/NEWID、ROW_NUMBER、CASE、字符串 `+`、COUNT |
| 核心 relation DDL 与 SELECT INTO | `KBSS020`-`KBSS024`、`KBSS027`、`KBSS041` | [CREATE/ALTER/DROP 与 SELECT INTO 目录](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html) | CREATE TABLE/VIEW/INDEX、ALTER TABLE ADD、DROP TABLE IF EXISTS、IDENTITY、SQLServer 类型和 SELECT INTO 目标表 |
| 批处理、事务与 session | `KBSS025`-`KBSS026`、`KBSS036`、`KBSS042`-`KBSS043` | [SQLServer 兼容性表](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/kes-vs-sqlserver/kes-vs-sqlserver-4.html)、[BEGIN TRANSACTION](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/language_elements/transaction/begin_transaction.html)、[SET DATEFIRST](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/statement/set/set_datefirst.html)、[SET TRANSACTION](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/statement/set/set_transaction.html) | BEGIN/COMMIT TRANSACTION、ksql 的 GO 批次边界、USE、SET DATEFIRST 和事务隔离级别 |
| 三段限定 relation | `KBSS044` | [SQLServer 兼容性说明](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/kes-vs-sqlserver/kes-vs-sqlserver-4.html) | SELECT/INSERT/UPDATE/DELETE/MERGE 的 `database.schema.object`，各段方括号状态与 patch 后重算 |
| 客户端占位符 | `KBSS012` | [Go 准备语句与绑定](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/application_development/client-interfaces/go/go-3.html)、[JDBC PreparedStatement](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/application_development/client-interfaces/jdbc/jdbc-2.html) | `?` 按出现顺序作为接口 SQL 占位符；不与 `@name` 合并 |

## 明确未纳入

- 首批不纳入 DML `OUTPUT`。V9R4 的 DML 章节公开语法以 `RETURNING` 为结果子句，关键字表出现 `OUTPUT` 不能单独证明完整 OUTPUT 语法合同。
- 首批已覆盖 GO，但不纳入 IF/ELSE、TRY/CATCH、WHILE、存储过程体和游标；这些属于后续 PL/MSSQL 控制流阶段。
- V9R4 官方目录列有 APPLY、PIVOT、UNPIVOT 和 SELECT FOR 等语法族，但现有 final SQLServer fixture 没有 APPLY/PIVOT/UNPIVOT/FOR XML 对应 case 可精确复用；本阶段不新造 case，后续需单独补齐。
- 现有 fixture 的 joined DELETE 全部与 OUTPUT 绑定，没有可在排除 OUTPUT 的前提下复用的独立形态；首批只覆盖 joined UPDATE。
- 不纳入仅由关键字或目录名称支撑、但未找到完整 V9R4 语法合同的 `WITH (NOLOCK)` 与 `OPTION (RECOMPILE)`。
- 不纳入 SQL Server 原生产品中存在、但 V9R4 官方资料未列入 KingbaseES SQLServer 兼容范围的语句。
- 方括号标识符的转义、逐段 quoted flags 和 patch 后重算已由 KingbaseES SQLServer 矩阵验证。
- GO 是 ksql 客户端批处理边界，不作为服务端 SQL 语句节点。
- 不纳入 V8 SQLServer 断言；V8 官方资料没有该 dbmode。

## View 与 patch 合同

官方资料只证明 SQL 形态。fixture 中的 `query_graph`、selector、patch 和原文逐字节保持属于 sqlparser 项目合同，不能反向解释为 KingbaseES 服务端公开接口。
