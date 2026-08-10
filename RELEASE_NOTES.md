# v2.15.2 发布说明

`v2.15.2` 为 Oracle 与 Dameng 增加 MERGE matched UPDATE 附属 `DELETE WHERE` 的结构化解析、View 与 patch 能力，并保持 PostgreSQL、SQL Server 独立 DELETE action 的既有语义。

## 支持范围

- Oracle 与 Dameng 支持 `WHEN MATCHED THEN UPDATE SET ... [WHERE ...] DELETE WHERE ...`。
- `DELETE WHERE` 是 matched UPDATE action 的附属操作，条件针对已更新的目标行求值；Query Graph 不为其生成独立 DELETE branch。
- PostgreSQL 与 SQL Server 的 `WHEN MATCHED ... THEN DELETE` 继续使用独立 MERGE branch。
- MySQL 不增加 MERGE 能力；Vastbase 各兼容模式不声明本语法支持。
- 附属 DELETE 必须包含 `WHERE` 和条件表达式，裸 `DELETE` 不在支持范围内。

## Query Graph 与 Selector

- matched UPDATE branch 可同时提供 `condition_selector` 与 `delete_condition_selector`，分别定位 action `WHERE` 和附属 `DELETE WHERE` 条件。
- 根 MERGE 使用 `stmt[S].merge_delete_condition[W]`；嵌套 MERGE 使用 `stmt[S].merge_delete_condition[D][W]`。
- `sqlparser_selector_clause_sql()` 返回不含 `DELETE WHERE` 关键字的条件表达式。
- `sqlparser_selector_set_clause_sql()` 与 `SQLPARSER_PATCH_REPLACE` 可独立替换普通分支条件或附属删除条件。

## Patch 与反解析

- MERGE assignment 由逗号、action `WHERE`、附属 `DELETE WHERE` 或后续 `WHEN` 明确限定时执行局部源码改写。
- assignment、普通分支条件和附属删除条件的 patch 仅替换目标源码区间；其他分支、换行、空白、关键字大小写及标识符定界符保持原文。
- patch 后的 SQL 会重新解析并与预期 View 对账，附属删除条件仍归属原 UPDATE branch。

## API 与兼容性

- `sqlparser_selector_kind_t` 追加 `SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION = 25`。
- `sqlparser_graph_dml_branch_t` 追加 `delete_condition_selector` 和 `has_delete_condition_selector`。
- 本版本不新增公开函数或资源所有权规则。
- 公开结构体布局发生追加式变化。C 调用方应使用 2.15.2 头文件重新编译。
- 动态库 ABI 主版本保持 `libsqlparser.so.0`。

## 用例与文档

- Oracle 新增 2 条 final case，覆盖同时存在 action `WHERE`、附属 `DELETE WHERE` 与条件 INSERT，以及仅含附属删除条件的 matched UPDATE。
- Dameng 新增 1 条 final case，覆盖 action `WHERE` 与附属 `DELETE WHERE` 共存。
- PostgreSQL、SQL Server 各新增 1 条 final case，覆盖独立 matched DELETE、matched UPDATE 与 not-matched INSERT 的分支顺序和 patch。
- 本版本共新增 5 条 final case 和 17 个独立 patch。当前九套 fixture 共包含 2,772 条 final case 和 8,989 个 patch。
- View JSON、API、方言支持范围、官方语法覆盖和 case matrix 的中英文文档已同步。

## 验证

- 九套 case matrix 共完成 2,772 条 case 和 8,989 个 patch，失败数为 0。
- 原始反解析、View JSON、patch 反解析和 runner 内部错误数均为 0。
- 核心 API 测试通过，覆盖根级与嵌套 selector、条件读取与替换、方言限制及裸 DELETE 拒绝。
- 定向 Valgrind 检查执行 1,040,125 次分配和 1,040,125 次释放；退出时为 `0 bytes in 0 blocks`，错误数为 0。

内置 `libpg_query` 标签：`17-6.2.2`。
内置 Jansson 版本：`2.15`。
