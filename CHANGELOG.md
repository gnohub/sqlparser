# 变更记录

## 2.13.0

### MERGE 分支结构与改写

- Query Graph 按源码顺序投影 MERGE 的每个 `WHEN` 分支，并提供 action、match 类型、INSERT 目标列与行以及 UPDATE assignment 范围；有条件的分支同时提供对应的 condition selector。
- 新增 `SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION`、MERGE action/match 枚举、两个枚举名称函数和一个 MERGE 分支详情访问函数；顶层与嵌套 MERGE 的分支条件和赋值项均可通过 selector 定位。
- MERGE UPDATE assignment 保留目标字段、来源字段、source target 和 bind 的关联，可通过既有 assignment selector 与 patch 操作读取、插入、替换和删除。
- Oracle、达梦和 Vastbase-Oracle 支持 MERGE UPDATE/INSERT action 级 `WHERE`；其他方言拒绝该语法。

### INSERT VALUES 与 View 语义

- 九种方言模式的 `INSERT ... VALUES` 支持 bind、literal、独立 `DEFAULT`、函数和复合表达式混合排列，并保持逐单元格的行列坐标、selector 与全局 bind 序号。
- 表达式内部的 bind 参与全局序号计算；`SYSDATE`、`CURRENT_TIMESTAMP`、`NOW()`、`GETDATE()` 等时间表达式作为 expression cell 输出，不会被投影为字段名称。
- 新增九组共 90 条混合 VALUES 回归用例，覆盖单行、多行、表达式首尾与交错、嵌套 bind、带定界符的标识符和非常规空白。

### Patch 后的标识符与反解析

- SQL 片段 patch 解析为 AST 后，新增标识符保留片段中的大小写、定界符和转义形式；双引号、MySQL 反引号及 SQL Server 方括号不会被替换或重复添加。
- generation 为 `0` 的未改写 handle 继续逐字节返回原始 SQL。generation 大于 `0` 时根据当前 AST 生成 SQL；标识符的大小写、定界符和转义形式予以保留，节点之间的空白可能由反解析器规范化。
- 标识符拼写状态在成功改写、失败改写后的回滚、handle 克隆和销毁路径中均按既定生命周期管理，并且不会因重复失败的 patch 操作而累积。

### 性能、兼容性与验证

- MERGE 语法验证通过 O(AST) 的主 protobuf 深度优先遍历完成，并执行分支局部检查；Query Graph 在单次构建中复用 statement 级定位数据、表达式源码扫描游标和当前 SQL 缓存，减少重复树遍历、反解析和源码扫描。
- 公共 API 采用追加式扩展，既有函数签名和公共结构体布局保持不变。动态库 ABI 主版本保持为 `libsqlparser.so.0`，ABI 导出检查包含 152 个公共符号。
- 九套方言矩阵的 2535 条预期成功用例均执行 generation-`0` 逐字节反解析检查、AST 标识符拼写审计和 View 构建，并对声明的结构化期望逐字段断言。
- 发布验证覆盖 GCC 8.3 严格发布/调试构建、全量测试、install smoke、ABI、ASan、UBSan、Valgrind 和 benchmark smoke。

## 2.12.0

### 反解析与 AST 标识符

- handle generation 为 `0` 时，`sqlparser_deparse()` 在成功返回时逐字节复制原始输入 SQL，保留标识符定界符和大小写、关键字、空白、换行、注释、分号及多语句边界。
- AST 中源自 SQL identifier token 的名称值保留源 token 的字母大小写；带引号标识符的 AST 值仍为解码后的名称内容，其定界符和转义形式由 generation-`0` deparse 契约保留。
- generation 大于 `0` 时，反解析从当前 handle 状态重新生成 SQL，整个输出不适用逐字节一致性保证。

### 会话状态 Query Graph

- 支持的数据库、schema、角色、身份、事务特征和会话参数语句会投影为结构化 session action、scope、target 和 value。
- 新增 session action、scope、target kind、value kind 枚举，`sqlparser_graph_session_t`、`sqlparser_graph_session_item_t`、`sqlparser_graph_session_value_t` 以及三个 Query Graph 访问函数。
- View JSON 为具有可用 session projection 的语句输出可选 `query_graph.session`，覆盖 identifier、keyword、literal、bind 和 expression value。

### MERGE matched UPDATE 改写

- 新增 `SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT` 和 `stmt[S].merge_assignment[W][A]` selector；`W` 是 MERGE 中所有 `WHEN` 子句的绝对 0 基序号，`A` 是对应 matched UPDATE 分支内赋值项的 0 基序号。
- `update_assignment` selector API 支持 MERGE matched UPDATE 赋值项的读取、右值改写和同一 statement 内的右值克隆；`SQLPARSER_PATCH_INSERT_ASSIGNMENT`、`SQLPARSER_PATCH_DELETE_ASSIGNMENT` 与 `SQLPARSER_PATCH_REPLACE_ASSIGNMENT` 支持赋值项的插入、删除和整项替换。
- Query Graph 为可改写的 MERGE matched UPDATE 赋值项输出对应 selector。

### 兼容性与验证

- 公共 API 采用追加式扩展；既有函数签名和公共结构体布局保持不变。动态库 ABI 主版本保持为 `libsqlparser.so.0`，ABI 导出检查包含 149 个公共符号。
- PostgreSQL、MySQL、Oracle、SQL Server、达梦及 Vastbase 四种兼容模式的九套用例矩阵对所有预期成功用例执行 generation-`0` 逐字节反解析检查和 AST 标识符拼写检查。
- 九套方言矩阵均包含 session 投影期望；支持 MERGE 的方言还包含 matched UPDATE assignment selector 与改写回归。
- 发布验证覆盖 GCC 8.3 严格发布/调试构建、全量测试、install smoke、ABI、ASan、UBSan、Valgrind，以及 Windows VS 2022 x64/MSVC 19.39 清理后构建和全量测试。

## 2.11.0

### 反解析

- 未被改写的数据库、schema、表名、列名、别名、函数名、索引名、约束名和 CTE 名称按输入 SQL 保留标识符拼写，包括未加引号标识符的原始大小写。
- 方言转换涉及注释、字符串、bind 和引号标识符时，反解析不会将其中的文本误识别为标识符。
- patch 新生成的节点使用自身标识符，不会继承输入 SQL 中其他同名节点的拼写。

### 兼容性与验证

- 公共 API、公共结构体、View JSON、Query Graph 和 ABI 保持不变；ABI 导出仍为 146 个公共符号。
- PostgreSQL、MySQL、Oracle、SQL Server、达梦及 Vastbase 兼容模式均增加标识符拼写回归用例。
- 发布验证覆盖 GCC 8.3 严格编译、全量测试、ASan、UBSan、Valgrind、ABI 检查和单次调用性能测试。

## 2.10.1

### MySQL 方言

- 修正 `USE INDEX`、`IGNORE INDEX`、`FORCE INDEX` 及对应 `KEY` 形式的反解析位置，索引提示保持在表名或别名之后、后续查询子句之前。
- 修正索引提示与 `GROUP BY`、`HAVING`、`WINDOW`、集合运算、锁定子句、`NATURAL JOIN`、`STRAIGHT_JOIN` 和 `JOIN ... USING` 组合时的反解析顺序。
- 修正 `STRAIGHT_JOIN` 右侧关系的索引提示恢复。
- Vastbase-MySQL 同步应用上述修正。

### 兼容性与验证

- 公共 API、公共结构体、View JSON 和 ABI 保持不变；ABI 导出仍为 146 个公共符号。
- MySQL 与 Vastbase-MySQL 方言矩阵分别包含 173 条支持用例。
- 发布验证覆盖 GCC 8.3 严格编译、全量测试、ASan、UBSan、Valgrind、ABI 检查以及 Windows VS 2022 x64/MSVC 19.39 全量测试。

## 2.10.0

### SQL Server 方言

- 支持显式或省略 `INTO` 的 `INSERT`，覆盖 `VALUES`、多行 `VALUES`、`SELECT`、集合查询、CTE 和 `DEFAULT VALUES`。
- 支持 `INSERT`、`UPDATE`、`DELETE` 和 `MERGE` 的 `OUTPUT` 结果通道，覆盖 `INSERTED`、`DELETED`、来源字段、`$action`、表达式、别名和 bind。
- 支持 `OUTPUT ... INTO`、sink/client 双通道、目标列列表以及外层 `INSERT` 消费内层 DML `OUTPUT` 的嵌套 DML。
- 支持 `IF...ELSE` 单语句分支、`BEGIN...END` 多语句分支、`ELSE IF` 和嵌套控制流。
- Vastbase-SQLServer 同步支持上述兼容语法。

### 结构化遍历与改写

- 增加控制流只读结构和访问函数，按源码顺序遍历 roots、nodes、branches、items 及可寻址条件 statement。
- Query Graph 支持同一 statement 内的多个 DML、嵌套 DML 父子关系、DML 结果通道和结果字段来源。
- 增加 DML 结果 target、sink relation 和 sink column selector，可继续通过 `sqlparser_apply_patch()` 执行统一改写。
- 公共 API 采用追加式扩展，既有函数签名和公共结构体布局保持不变。

### 性能与验证

- 普通非控制 SQL 不构造控制流状态；控制流状态使用单次连续分配。
- SQL Server 与 Vastbase-SQLServer 方言用例矩阵分别包含 546 条用例，其中 517 条为支持路径，29 条为错误或明确不支持路径。
- 发布验证覆盖 GCC 8.3 严格编译、全量测试、ASan、UBSan、Valgrind、ABI 检查以及 Windows VS 2022 x64/MSVC 19.39 全量测试。
- ABI 导出检查包含 146 个公共符号。

## 2.9.0

### MySQL 方言

- 增加 `INSERT ... SET`、`ON DUPLICATE KEY UPDATE` 行别名、别名删除目标以及单表 `UPDATE` / `DELETE ... ORDER BY ... LIMIT` 支持。
- 增加 `STRAIGHT_JOIN`、`JOIN ... USING`、`NATURAL JOIN`、锁定读、索引提示和查询表 `PARTITION(...)` 支持。
- Vastbase-MySQL 同步支持上述兼容语法。

### Query Graph

- `JOIN ... USING` 中出现的字段通过现有 `fields[]` 和 `candidate_relations` 输出。
- 同一 CTE 的多次引用共享来源 `source_block`，未引用 CTE 保留在 Query Graph 中，递归 CTE 引用指向已注册的来源块。
- 增加 `SQLPARSER_GRAPH_INSERT_MODE_SET`，用于标识 `INSERT ... SET` 写入形态。
- 公共结构体布局保持稳定，ABI 导出符号数保持为 135。

### 性能与验证

- MySQL 扩展语法采用单次特征分类，仅在对应语法出现时执行转换流程。
- MySQL 与 Vastbase-MySQL 方言用例矩阵分别包含 156 条支持用例。
- 发布验证覆盖 GCC 8.3 严格编译、全量测试、ASan、UBSan、Valgrind、ABI 检查及旧版本客户端动态库兼容测试。

## 2.8.1

### Query Graph 性能

- `sqlparser_statement_query_graph()` 构建期新增 statement 级 selector 缓存，一次遍历记录 value、name、relation 和 select target list 的 selector 索引。
- 大型 / 多层 `query_graph` 场景不再为每个 value、field、relation 或 SELECT target list 反复执行全 statement protobuf 树搜索，尤其改善 `INSERT ... SELECT`、集合查询和嵌套 SELECT。
- 保持公共 API、ABI、selector 输出格式和 `query_graph` 结构不变；同一 handle 的 query graph cache 行为不变。

### 测试与验证

- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.8.0

### Query Graph 与 DML 结构化输出

- `query_graph` 增加 predicate 数组，用于表达比较、布尔组合、`EXISTS` 和表达式谓词。
- 新增 `sqlparser_graph_predicate_t`、predicate 类型枚举、布尔枚举和对应名称函数。
- DML branch、assignment 和 cell 增加 source field / source target 关联，用于表达字段搬运、`MERGE` source lineage 和多分支 insert 来源。
- relation view 和 graph relation 增加 database link 名称输出。
- `ROWID` 等伪列可作为独立 target 输出，避免污染星号 lineage。

### 方言覆盖

- Oracle 增强 DML / SELECT 结构化输出，覆盖 alias-qualified `UPDATE`、`INSERT ALL` / `INSERT FIRST`、`MERGE` source lineage、`DISTINCT`、`ORDER BY` 和 qualified star + `ROWID`。
- MySQL 和 Vastbase-MySQL 增强 multi-table `UPDATE` / `DELETE`、`REPLACE`、insert modifier、CREATE TABLE option 和常见 HOOK_ONLY 语法覆盖。
- SQL Server 和 Vastbase-SQLServer 补齐多类基础 DDL、表达式、table/query hint、`FOR JSON`、nested `TOP`、全文谓词和官方语法覆盖统计。
- 达梦补齐 `ALTER SESSION` 参数、`TOP` 公开反解析、database link、national 字符串和多表 insert 相关结构化输出。
- PostgreSQL 和 Vastbase-PostgreSQL 增加通知、extension、national 字符串和常见可执行矩阵覆盖。

### 字符串和兼容语法

- MySQL、Oracle、PostgreSQL、达梦及 Vastbase 对应兼容模式支持 `N'...'` / `n'...'` national 字符串公开形态保真。
- Oracle、达梦及 Vastbase-Oracle 保留 `nq'...'` national q-quoted 字符串语义。
- SQL Server 和 Vastbase-SQLServer 保留 `N'...'` Unicode 字符串前缀和相关 prepared statement 形态。

### 测试与验证

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server、达梦和 Vastbase 四种兼容模式的现有 case matrix。
- 更新中英文方言支持文档、官方语法覆盖统计和 View JSON/API 文档。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.7.0

### Query Graph 操作符分类

- `sqlparser_graph_value_t` 新增 `operator_kind`，用于对 `LIKE`、`NOT LIKE`、`ILIKE`、`NOT ILIKE` 进行结构化分类。
- 新增 `sqlparser_graph_operator_kind_t`、`sqlparser_graph_operator_kind_name()`、`sqlparser_graph_operator_is_like_pattern()` 和 `sqlparser_graph_value_is_like_pattern()`。
- View JSON 的 `query_graph.values[]` 在存在 `operator` 时输出 `operator_kind`，pattern-match 操作为 `like`、`not_like`、`ilike` 或 `not_ilike`，其他操作符为 `unknown`。
- `LIKE ... ESCAPE ...` 识别复用结构化操作符分类，不再依赖调用方比较操作符字符串。

### 测试与验证

- PostgreSQL、Oracle、MySQL、SQL Server、达梦和 Vastbase 四个兼容模式的现有 case matrix 增加 pattern-match 操作符分类覆盖。
- 核心 API 回归测试覆盖 public enum、辅助函数、View JSON 输出、非 pattern 操作符边界和显式 `ESCAPE` 保留。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.6.0

### Vastbase 方言

- 新增四个显式 Vastbase 兼容模式：`vastbase-oracle`、`vastbase-mysql`、`vastbase-postgresql` 和 `vastbase-sqlserver`。
- 公共 C 枚举新增 `SQLPARSER_DIALECT_VASTBASE_ORACLE`、`SQLPARSER_DIALECT_VASTBASE_MYSQL`、`SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` 和 `SQLPARSER_DIALECT_VASTBASE_SQLSERVER`。
- CLI 支持四个 Vastbase 方言名称；`vastbase` 作为确定性别名映射到 `vastbase-oracle`，不会根据 SQL 文本自动猜测兼容模式。
- Vastbase 四个模式分别通过独立 hook 接入对应兼容模式，保留各自公开 SQL 形态、bind 规则和结构化输出规则。

### 测试与验证

- 新增四套 Vastbase case matrix，覆盖 Oracle、MySQL、PostgreSQL 和 SQL Server 兼容模式的解析、View JSON、反解析和明确不支持语法返回码。
- 新增 Vastbase CLI batch 用例和 `examples/dialect/20_vastbase_dialect.c`。
- 更新 Linux 与 Windows/MSVC 构建清单，确保新增源文件、单元测试和示例参与构建。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.5.0

### LIKE ESCAPE 结构化输出

- `sqlparser_graph_value_t` 新增 `like_escape`，用于表达 `LIKE`、`NOT LIKE`、`ILIKE`、`NOT ILIKE` 的显式 `ESCAPE` 子句。
- 新增 `sqlparser_graph_like_escape_kind_t`，区分无显式 `ESCAPE`、字面量、预编译占位符和表达式 escape。
- View JSON 的 `values[]` 在 pattern 主值上输出 `like_escape`，不把 escape 子句拆成并列业务值。
- 各方言 public deparse 保持 `LIKE pattern ESCAPE escape` 形态，不暴露 libpg_query 内部 `pg_catalog.like_escape(...)`。
- 结构化识别只接受 libpg_query 生成的 `pg_catalog.like_escape`，不会把用户 SQL 中未限定的同名函数误判为显式 `ESCAPE`。

### 测试与验证

- PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix 增加 `LIKE ESCAPE` 用例，覆盖字面量 escape、命名 bind、位置 bind、JDBC `?` bind、表达式 escape、无显式 escape 和派生表场景。
- 核心 API 回归测试同步覆盖 C 结构字段、View JSON、public deparse，以及用户自定义 `like_escape(...)` 函数边界。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.4.0

### UPDATE SET 改写

- `sqlparser_update_set_assignment_literal()` 和 selector 版本现在支持把 `UPDATE SET` 赋值右值从 bind 参数改写为 literal。
- 保持目标列、赋值项顺序和当前方言输出规则不变，仅替换赋值右值 AST 节点。
- 函数、运算表达式、字段引用、`DEFAULT` 和子查询右值继续返回 `SQLPARSER_STATUS_UNSUPPORTED`，避免生成语义不明确的 SQL。

### 测试与验证

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix，覆盖单字段、多字段、命名 bind、位置 bind 和 JDBC `?` bind 的 `UPDATE SET` 来源表达。
- 扩充核心 API 回归测试，覆盖 bind 右值改写、备份赋值插入、反解析后二次解析，以及复杂右值拒绝路径。
- 发布验证覆盖 Linux 单元测试、ASan、UBSan、Valgrind 内存检查和 ABI 导出检查。

## 2.2.0

### 结构化 SQL 片段改写

- 增加 `sqlparser_identifier_path_view_t`，用于向公共改写接口传入方言无关的标识符路径。
- 增加结构化 `UPDATE SET` 赋值构造接口，支持通过列路径和右值 SQL 生成、追加或替换赋值项。
- 增加结构化 `SELECT` 输出项替换接口，支持把单个输出项替换为一组列路径。
- 新增 `examples/convenience/18_structured_fragment_rewrite.c`，展示不拼接完整 SQL 文本的结构化改写流程。

### 测试与发布验证

- 扩充核心 API 和健壮性测试，覆盖结构化标识符路径、非法参数、追加赋值、替换输出项和反解析校验。
- 更新 Linux 与 Windows/MSVC 构建清单，确保新增源文件和示例参与构建。
- 发布验证覆盖 Linux 单元测试、内存泄漏检查、ASan、UBSan、ABI 导出检查以及 Windows/MSVC 测试。

## 2.0.2

### View JSON

- `query_graph.values[]` 新增 `field_match_kind`，用于区分条件值绑定的是直接字段还是函数、类型转换、表达式或 `CASE` 包裹字段
- 公共 C 结构 `sqlparser_graph_value_t` 同步暴露 `field_match_kind`，并新增 `sqlparser_graph_field_match_kind_name()` 名称辅助函数
- PostgreSQL、MySQL、Oracle、SQL Server 和达梦用例矩阵新增直接字段与表达式字段匹配回归

## 2.0.0

### View JSON

- `query_graph` 作为结构化输出主数据源，View JSON 仅在调用导出接口时按需生成
- 将预编译占位符输出从 `bind` 收敛为 `bind_key`，并新增 `bind_position` 表示整条输入 SQL 中的 bind 出现序号
- `sqlparser_graph_value_t`、`sqlparser_graph_dml_cell_t` 和 `sqlparser_graph_dml_assignment_t` 同步暴露 `bind_key`、`bind_kind`、`bind_position` 与 `bind_sql`
- View JSON、C 结构遍历和 case matrix 统一校验 `bind_key`、`bind_kind`、`bind_position`、`bind_sql` 与 `selector`
- 多语句 SQL 中 `bind_position` 按整条输入 SQL 全局递增，`bind_key` 保留方言预处理后的占位符 key
- PostgreSQL dollar-quoted 字符串内部的占位符样式文本不会参与 bind 全局计数
- `IN`、`NOT IN`、`BETWEEN` 和单字段函数包裹条件会输出字段关联的 `values[]`
- SELECT 投影内部的条件表达式会输出字段关联的 `values[]`，例如 `CASE WHEN phone = ? THEN ...`
- View JSON 省略 `query_graph` 与 DML 结构中的空数组，公共 C 结构仍通过 `count` 或 `has_*` 字段表达空集合
- MySQL `INSERT ... ON DUPLICATE KEY UPDATE`、带 ON 条件的普通/INNER/CROSS 多表 `UPDATE ... JOIN` 和 `DELETE ... JOIN` 纳入支持矩阵，View JSON 输出 DML 目标、来源、赋值和值参数映射

### 工具与基线

- CLI batch 输入收敛为顶层数组或 `items` 数组，不再保留旧 `sqls` 别名
- `libpg_query` baseline 收敛为单线程成功解析和线程首次解析，不再生成错误路径或并发路径基线

### 健壮性

- 修复 View JSON 序列化过程中 Jansson `_new` 接口失败路径的所有权处理，避免低内存场景下重复释放中间 JSON 节点

## 0.8.0

### 方言能力

- 支持 Oracle 普通 `ALTER SESSION SET <parameter> = <value>` 会话参数赋值，覆盖字符串、标识符、数字和布尔/枚举值
- 保持 Oracle `ALTER SESSION` 公开输出为原始参数名和值，不暴露内部适配前缀
- 修复 MySQL `LIMIT ?, ?` 参数化分页语句，公开反解析保持 MySQL 逗号分页形态

### 测试与覆盖

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix，覆盖更多 DDL、DML、JOIN、函数、表达式、bind、分页以及 `USE`、`SET SCHEMA` 和 `ALTER SESSION SET` 场景
- 新增 Oracle `ALTER SESSION SET NLS_DATE_FORMAT`、`NLS_DATE_LANGUAGE`、`NLS_NUMERIC_CHARACTERS`、`INSTANCE`、`ERROR_ON_OVERLAP_TIME` 回归用例
- 同步更新中英文 case matrix、方言覆盖统计和 Oracle 官方语法覆盖统计

## 0.7.0

### UPDATE SET 改写

- 增加 `UPDATE SET` 赋值项级 patch 能力，支持通过 `stmt[n].assignment[i]` 追加、删除和整项替换赋值项
- 新增 `SQLPARSER_PATCH_INSERT_ASSIGNMENT`、`SQLPARSER_PATCH_DELETE_ASSIGNMENT` 和 `SQLPARSER_PATCH_REPLACE_ASSIGNMENT`
- 新增 `sqlparser_update_insert_assignment_sql()`、`sqlparser_update_delete_assignment()`、`sqlparser_update_set_assignment_full_sql()` 及对应 selector API
- 保持既有 `SQLPARSER_PATCH_REPLACE` 对 assignment 的右值改写语义不变

### 测试与文档

- 增加 `examples/patch/17_update_set_patch.c`，展示通过 `sqlparser_apply_patch()` 追加、删除和整项替换 `UPDATE SET` 赋值项
- 扩充核心 API 和健壮性回归测试，覆盖 Oracle bind 片段、非法 selector、越界索引、空 `SET` 保护和失败后 handle 可用性
- 更新中英文 API 手册、View JSON 手册、示例说明和 MSVC 示例构建清单

## 0.6.0

### View JSON 结构

- 将 `query_graph` C 结构作为结构化输出的主数据源，`sqlparser_export_view_json()` 改为按需从 `query_graph` 序列化 JSON
- 扩展 `sqlparser_graph_field_t` 和 `sqlparser_graph_dml_cell_t`，输出 bind 名称、bind 类型、原始 bind SQL、bind selector、子句编号和 SELECT 输出路径
- 增加 `sqlparser_bind_kind_t`、`sqlparser_bind_kind_name()`、`sqlparser_statement_clause()` 和 `sqlparser_clause_sql()` 公共接口
- 扩展 `sqlparser_clause_kind_t`，增加 `on`、`group_by` 和 `having` 子句类型
- View JSON 移除旧的 `target_kind`、`target_name`、`target_arg_index` 字段，统一使用有序 `target_path` 表达 SELECT 输出层级

### 语义与方言

- bind 输出区分 positional 和 named 两类，保留 `bind_sql` 用于区分 `?`、`:1`、`:name`、`$1`、`@name` 等原始 SQL 形态
- bind 右值不再重复暴露为普通 `value`，避免调用方把占位符误判为字面量值
- SELECT 输出表达式列不再暴露条件运算符和值，输出形态通过 `target_path` 表示
- `NOT IN`、`NOT LIKE`、`NOT ILIKE` 和 `NOT SIMILAR TO` 运算符保持完整公共 SQL 语义

### 测试与文档

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦用例矩阵，覆盖更多 SELECT、INSERT、UPDATE、DELETE、JOIN、函数、表达式和 bind 场景
- 增加 View JSON 公共 C 结构语义测试，验证结构体字段和 View JSON 输出一致
- 增加 bind 字段、cell bind、`clause_id` 和 `target_path` 的通用断言
- 更新中英文 API 手册和 View JSON 手册

## 0.5.0

### View JSON 语义

- 增加 SELECT 输出项语义路径，使用 `target_path` 表达函数、表达式、CASE 和嵌套输出层级
- 为 View JSON 增加字段归属子句编号，便于区分 SELECT、WHERE、JOIN/ON、ORDER BY 等位置的字段引用
- 扩充各方言的 View JSON 语义用例，覆盖函数输出、表达式输出、星号输出、多层 SELECT 和 bind 条件

## 0.4.0

### 方言能力

- 增加达梦 `SQLPARSER_DIALECT_DAMENG` 方言转换层，覆盖 `SET SCHEMA`、`MINUS`、`LIMIT`、`TOP`、bind、常见 DML/DDL、事务和权限语句
- 增加达梦公共输出规则，反解析和 View JSON 不暴露内部参数名或内部转换 SQL
- 增加 PostgreSQL、MySQL、Oracle、SQL Server 和达梦的预编译 / 参数化 SQL 语句覆盖，包含 SQL Server `sp_executesql` 与达梦 `EXEC SQL PREPARE`

### View JSON 与改写

- 增加通用 `SELECT` 输出列表读取、替换、插入和删除接口
- 增加通用 `WHERE` 条件读取、设置和 `AND` / `OR` 追加接口
- 增加通用 statement 级 `clause` selector，支持通过 `stmt[n].clause[m]` 改写 `select_list`、`where` 和 `order_by`
- View JSON 增加 `query_graph`，用于暴露可写语句级子句槽位

### 测试与文档

- 增加 `SELECT` 输出列表和 `WHERE` 条件改写示例
- 增加通用 `clause` patch 示例，覆盖 SELECT 输出列表、WHERE 条件和 ORDER BY 新增
- 示例目录按 `patch`、`convenience`、`inspect`、`dialect` 分类，推荐接入方优先使用 `patch` 示例
- 增加 PostgreSQL、MySQL、Oracle 和 SQL Server 的 WHERE 改写回归用例，覆盖全部已暴露 `where_clause` 的 PostgreSQL AST 类型
- 增加达梦方言用例矩阵、官方语法覆盖统计、CLI 批量夹具和方言示例
- 同步更新 prepared / bind 相关用例矩阵、方言覆盖统计和官方语法覆盖统计

## 0.3.0

### 方言能力

- 增加 PostgreSQL `SET search_path`、`SET LOCAL search_path` 和 `SET SCHEMA` 结构化输出
- 增加 MySQL `USE db_name` 支持
- 增加 SQL Server `USE database_name` 支持
- 增加 Oracle `ALTER SESSION SET CURRENT_SCHEMA`、`ALTER SESSION SET CONTAINER` 和 `ALTER SESSION SET CONTAINER ... SERVICE ...` 支持
- 修复多语句输入中的 `USE`、`SET SCHEMA` 和 `ALTER SESSION SET` 语句处理，确保 parse、View JSON 和 deparse 均保持公开方言形态

### View JSON 与改写

- 这些语句复用现有 `query_graph values` 结构，不新增独立 JSON 格式
- 支持通过 `stmt[n].value[m]` selector 改写对应值并还原为相应方言 SQL
- 修复 SQL Server、MySQL 和 Oracle deparse 中内部 `sqlparser_current_*` 哨兵泄露的边界问题

### 测试与文档

- 增加 MySQL、Oracle 和 SQL Server 多语句 `USE` / `ALTER SESSION SET` 回归用例
- 同步更新方言支持文档、官方语法覆盖清单和可执行用例覆盖统计

## 0.2.0

### 核心能力

- 提供稳定的 `sql -> handle -> rewrite -> deparse` 公共 C API
- 支持 `SELECT / INSERT / UPDATE / DELETE / MERGE / TRANSACTION / 常见 DDL` 的解析与结构读取
- 支持关系名、名称原子、字面量、`WHERE` 字面量、`UPDATE assignment` 和 `INSERT cell` 的精确改写
- 支持右值表达式级改写，包括 `DEFAULT` 与任意表达式 SQL
- 支持 View JSON、`query_graph` C 结构化遍历和 structured patch 写回
- 提供 View JSON 作为按需结构化导出
- 支持可配置资源限制，覆盖 SQL 输入、表达式 SQL 片段、生成输出和语句数量
- 增加方言公共框架，默认 PostgreSQL，并提供 MySQL、Oracle 与 SQL Server 方言转换层
- 收敛默认输出上限到 4MB，并减少 parse/deparse 路径中的常驻 AST 和字符串拷贝

### 发布与构建

- 固定 vendored `libpg_query` 版本并纳入仓库
- 统一发布公共头文件、静态库、动态库与 `pkg-config` 文件
- 新增严格构建、安装烟测、`valgrind` 泄漏校验、循环回归、benchmark smoke 与一键 `verify` 入口
- 构建系统按编译选项签名自动失效并重建本库对象和 vendor 产物
- 新增 `make abi-check`，校验动态库导出符号与公共头文件一致
- 新增 Linux/GCC GitHub Actions CI 门禁
- CI 增加 JSON fixture 校验和源码包 smoke
- 新增 `make dist` 源码发布包目标
- 新增 Windows/MSVC NMake 构建入口，支持生成静态库、CLI、单元测试与示例程序
- Windows/MSVC 构建使用仓库内 vendored Jansson，避免依赖外部包管理器

### 测试与性能

- 扩充通用 SQL 批量夹具，覆盖子查询、`CASE`、窗口、`ON CONFLICT`、`RETURNING`、`UPDATE ... FROM`、`DELETE ... USING`、`MERGE`、事务控制、常见 DDL、`GRANT/REVOKE` 与维护语句
- 增加 MySQL 方言用例矩阵，覆盖已支持语句形态和明确不支持语法
- 增加 Oracle 方言用例矩阵，覆盖已支持语句形态、公共输出规则和明确不支持语法
- 增加 SQL Server 方言用例矩阵，覆盖已支持 T-SQL 语句形态、公共输出规则和明确不支持语法
- 增加安装态 API 烟测、`valgrind` 泄漏校验与表达式改写回归
- 增加稳定性回归，覆盖畸形 SQL、参数校验、资源限制和失败改写回滚
- benchmark 增加读取链路、改写链路与 `rewrite + deparse` 单次调用统计
- 增加按能力分类的测试入口，覆盖 parse、inspect、rewrite、deparse、View JSON、CLI、install smoke 和 ABI

### 文档

- 提供中英文快速开始、API 手册、View JSON 手册、CLI 手册与架构文档
- 增加 Oracle、SQL Server 方言支持说明和 `v0.2.0` 发布说明
- 增加公开变更记录
