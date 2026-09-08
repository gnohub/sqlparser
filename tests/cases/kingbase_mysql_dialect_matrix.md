# KingbaseES MySQL 兼容模式用例矩阵

可执行夹具为 `tests/cases/kingbase_mysql_dialect_input.json`。本矩阵以 KingbaseES `V008R006C009B0014` 和 MySQL 兼容版 `V009R003C011` 为资料基线，只纳入人大金仓官方资料能够直接确认、且已存在于 MySQL fixture 的 SQL 形态。所有用例均为 `final`，由 `tests/unit/test_kingbase_mysql_dialect_case_matrix.c` 验证。

## 基线与入口边界

`kingbase-mysql` 是单一方言入口，接受 V8 与 V9 官方 MySQL 语法的并集；不按版本拆分入口，也不增加版本字段、版本检测或版本分支。KingbaseES V8R6 通过 `initdb -m/--dbmode=mysql`（或数值 `2`）初始化 MySQL 模式，V9R3 则作为独立的 MySQL 兼容版发布。

V9 增量只扩展可接受语法：紧邻名称的 `@var` 和 `PREPARE ... FROM` 均有独立语法上下文，不改变 V8 的 relation/dblink 或 `PREPARE ... AS` 解析，也不修改前 36 条 V8 合同。

- 初始化模式：[initdb](https://help.kingbase.com.cn/v8/admin/reference/ref-server/initdb.html)
- 文档版本：[V8 手册下载](https://help.kingbase.com.cn/v8/download.html)
- MySQL 模式索引：[MySQL 模式索引](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/MySQL_schema_index.html)
- 兼容能力总览：[MySQL 兼容性总览](https://help.kingbase.com.cn/v8/development/develop-transfer/kes-vs-mysql/overview.html)
- V9R3C11 产品入口：[KingbaseES（MySQL 兼容版）V9](https://help.kingbase.com.cn/v9.3.11/index.html)
- V9R3C11 SQL 参考：[MySQL SQL 参考](https://help.kingbase.com.cn/v9.3.11/development/application-develop-guide/reference/mysql/index.html)
- V9R3 功能基线：[V009R003C010 版本说明](https://help.kingbase.com.cn/v9.3.11/intro/releasenotes-external-v9/V9.3.10.html)

## 夹具统计

夹具包含 42 条 `status = "final"` 用例和 112 个独立 patch。用例的 SQL、期望 View、patch 及 bind occurrence 断言均从现有 MySQL fixture 中按官方 V8/V9 支持范围筛选；除用例名前缀外，不改变原合同。

## 官方资料与覆盖关系

| 分类 | 官方资料 | 覆盖用例 |
| --- | --- | --- |
| 注释与定界标识符 | [SQL 基本元素](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/datatype.html)、[V8R6C8 版本说明](https://help.kingbase.com.cn/v8/intro/releasenotes-external/V8.6.8.14.html) | `KBM001`、`KBM034` |
| 客户端 bind | [Go 准备语句和绑定](https://help.kingbase.com.cn/v8/development/client-interfaces/go/go-3.html)、[JDBC 接口](https://help.kingbase.com.cn/v8/development/client-interfaces/jdbc/jdbc-2.html) | `KBM004`–`KBM007`、`KBM010`、`KBM013`、`KBM016`、`KBM017`、`KBM020`、`KBM024`、`KBM031`–`KBM033`、`KBM036` |
| SELECT、CTE、JOIN、LIMIT 与索引提示 | [查询和子查询](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Queries_and_Subqueries.html)、[SELECT（MySQL 模式）](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | `KBM004`、`KBM005`、`KBM007`、`KBM021`、`KBM022`、`KBM028`、`KBM029`、`KBM034`、`KBM035` |
| INSERT 与冲突更新 | [INSERT（MySQL 模式）](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_9.html) | `KBM006`、`KBM009`–`KBM011`、`KBM020`、`KBM025`、`KBM026` |
| REPLACE | [REPLACE INTO](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | `KBM012`–`KBM015` |
| UPDATE | [UPDATE（MySQL 模式）](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | `KBM016`、`KBM027`、`KBM032`、`KBM033` |
| DELETE | [MULTIPLE TABLE DELETE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html)、[兼容能力总览](https://help.kingbase.com.cn/v8/development/develop-transfer/kes-vs-mysql/overview.html) | `KBM017`、`KBM030`、`KBM031` |
| DDL 与数据类型 | [CREATE TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_6.html)、[ALTER TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_3.html)、[MySQL 模式索引](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/MySQL_schema_index.html) | `KBM002`、`KBM008`、`KBM018`、`KBM019`、`KBM023` |
| 函数与结构化表达式 | [函数](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Function.html)、[表达式](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Expressions.html) | `KBM024`、`KBM025`、`KBM036` |
| 事务控制 | [SQL 兼容性说明](https://help.kingbase.com.cn/v8/development/develop-transfer/kes-vs-mysql/kes-vs-mysql-3.html) | `KBM003` |
| V9 用户变量 | [V9 用户变量](https://help.kingbase.com.cn/v9.3.11/development/application-develop-guide/reference/mysql/structure_language/user_defined_variables.html) | `KBM039`–`KBM042` |
| V9 预备语句 | [V9 SQL 参考](https://help.kingbase.com.cn/v9.3.11/development/application-develop-guide/reference/mysql/index.html)、[V9R3 版本说明](https://help.kingbase.com.cn/v9.3.11/intro/releasenotes-external-v9/V9.3.10.html) | `KBM037`–`KBM040` |

## 用例清单

| ID | 用例 | 独立 patch | 验证重点 |
| --- | --- | ---: | --- |
| `KBM001` | `kingbase-mysql-hash-comment` | 3 | MySQL 模式 `#` 行注释与逐字节回放 |
| `KBM002` | `kingbase-mysql-alter-table-add-column` | 0 | `ALTER TABLE ... ADD COLUMN` |
| `KBM003` | `kingbase-mysql-start-transaction` | 0 | `START TRANSACTION` 与多语句计数 |
| `KBM004` | `kingbase-mysql-select-question-params` | 6 | SELECT 条件中的 JDBC 风格位置参数 |
| `KBM005` | `kingbase-mysql-select-limit-question-params` | 6 | 官方 `LIMIT count OFFSET start` 形态与 bind |
| `KBM006` | `kingbase-mysql-insert-multi-row-question-params` | 3 | 多行 VALUES 与位置参数 |
| `KBM007` | `kingbase-mysql-view-join-on` | 6 | JOIN/ON/WHERE 字段与 bind 归属 |
| `KBM008` | `kingbase-mysql-create-table-if-not-exists` | 0 | 条件建表与反引号标识符 |
| `KBM009` | `kingbase-mysql-insert-ignore` | 2 | `INSERT IGNORE` |
| `KBM010` | `kingbase-mysql-insert-ignore-on-duplicate-key` | 3 | IGNORE、VALUES 与冲突更新组合 |
| `KBM011` | `kingbase-mysql-on-duplicate-key-assignment-patch` | 3 | 冲突更新 assignment 插入、替换与删除 |
| `KBM012` | `kingbase-mysql-replace-into` | 2 | REPLACE VALUES |
| `KBM013` | `kingbase-mysql-replace-set` | 2 | REPLACE SET 与位置参数 |
| `KBM014` | `kingbase-mysql-replace-without-into` | 2 | REPLACE 省略 INTO |
| `KBM015` | `kingbase-mysql-replace-table-source` | 2 | REPLACE TABLE 来源 |
| `KBM016` | `kingbase-mysql-update-ignore` | 5 | `UPDATE IGNORE` |
| `KBM017` | `kingbase-mysql-delete-join` | 4 | 多表 DELETE JOIN |
| `KBM018` | `kingbase-mysql-auto-increment` | 0 | AUTO_INCREMENT 列属性 |
| `KBM019` | `kingbase-mysql-unsigned` | 0 | UNSIGNED 数值属性 |
| `KBM020` | `kingbase-mysql-insert-select-source-block-graph` | 5 | INSERT SELECT 来源块 |
| `KBM021` | `kingbase-mysql-with-cte-select` | 6 | CTE 与来源关系 |
| `KBM022` | `kingbase-mysql-window-row-number` | 5 | 窗口函数、分区与排序字段 |
| `KBM023` | `kingbase-mysql-common-data-types` | 0 | BIGINT、DECIMAL、DATETIME、BOOLEAN |
| `KBM024` | `kingbase-mysql-json-contains-function-predicate` | 4 | `mysql_json` 插件函数的 predicate 结构 |
| `KBM025` | `kingbase-mysql-on-duplicate-values-function` | 3 | 冲突更新中的 `VALUES(column)` |
| `KBM026` | `kingbase-mysql-insert-set-paired-column-patch` | 2 | INSERT SET 列值结构与成对 patch |
| `KBM027` | `kingbase-mysql-update-order-limit` | 6 | 单表 UPDATE 的 ORDER BY/LIMIT |
| `KBM028` | `kingbase-mysql-select-use-index` | 5 | USE INDEX |
| `KBM029` | `kingbase-mysql-select-force-and-ignore-index` | 5 | FORCE INDEX、IGNORE KEY 与集合查询 |
| `KBM030` | `kingbase-mysql-delete-limit-only` | 3 | 单表 DELETE LIMIT |
| `KBM031` | `kingbase-mysql-delete-left-join-where-three-and` | 4 | LEFT JOIN 多表删除与复合条件 |
| `KBM032` | `kingbase-mysql-update-multiple-target-three-table-bind-order` | 3 | 三表 JOIN、多目标 assignment 与 12 个 bind |
| `KBM033` | `kingbase-mysql-update-multiple-target-four-relation-comma-list` | 3 | 四 relation 逗号列表与多目标更新 |
| `KBM034` | `kingbase-mysql-quoted-alias-output-flags` | 2 | 反引号 relation alias 与 output alias 标志 |
| `KBM035` | `kingbase-mysql-cte-explicit-columns-ordinal` | 2 | CTE 显式列名按序映射 |
| `KBM036` | `kingbase-mysql-predicate-expression-function-argument-kinds` | 5 | 可变参数 CONCAT、嵌套函数与参数 patch |
| `KBM037` | `kingbase-mysql-prepare-from-literal` | 0 | V9 MySQL 形态 `PREPARE ... FROM` 字符串 |
| `KBM038` | `kingbase-mysql-deallocate-prepare` | 0 | V9 `DEALLOCATE PREPARE` |
| `KBM039` | `kingbase-mysql-prepare-from-user-variable` | 0 | V9 用户变量作为 PREPARE 来源 |
| `KBM040` | `kingbase-mysql-execute-using-multiple-vars` | 0 | V9 EXECUTE USING 有序用户变量 |
| `KBM041` | `kingbase-mysql-user-variable-basic` | 0 | V9 单用户变量 SET 赋值 |
| `KBM042` | `kingbase-mysql-user-variable-punctuation` | 0 | V9 用户变量名中的点、下划线和美元符号 |

`KBM024` 的函数属于 V8 官方 `mysql_json` 插件范围；该用例只声明 SQL 语法与解析合同，不声明插件已安装。

## 未纳入的边界

- 官方兼容矩阵明确标记为不支持的 `RLIKE`、`INSERT DELAYED`、`LOGFILE GROUP`、`LOCK/UNLOCK INSTANCE`、`SET PASSWORD FOR`、`CHECK/CHECKSUM TABLE` 不作为正向用例。
- V8/V9 官方完整语法未确认 `LOW_PRIORITY`、`HIGH_PRIORITY`、`REPLACE DELAYED`、`ENGINE`、表级 `DEFAULT CHARSET`、`ZEROFILL`、`USE database` 或 INSERT row alias，因此不纳入。
- V9 文档列出 `/*! specific code */` 注释，但未定义 MySQL version marker 的执行规则；不据此纳入 versioned executable comment。
- V8/V9 可核实的 SELECT 精确文法只确认 `LIMIT count OFFSET start`；未用 MySQL 的 `LIMIT offset,count` 推断 KingbaseES 行为。
- V8 服务端 PREPARE 保留 `PREPARE name(types) AS statement` 与 `$N` 参数形态；V9 专用 MySQL SQL 参考新增用户变量及 MySQL 预备语句组。本入口将两类语法作为并集接受，不以版本字段选择解析路径。`DROP PREPARE` 未在 V9R3C11 参考中形成正向合同。
- V9 迁移最佳实践仍包含“SQL 中不支持用户变量”的旧描述，与 V9R3C11 专用 SQL 参考及 V9R3 版本说明冲突。本矩阵以专用 SQL 参考的明确语法为 V9 正向依据，并仅纳入其直接给出的单变量赋值、变量命名和预备语句形态。
- 本夹具定义可复用的 MySQL 子集，不穷举 KingbaseES 自有扩展；例如 DML `RETURNING` 需要独立的 KingbaseES 用例，不能从现有 MySQL fixture 等价复用。
