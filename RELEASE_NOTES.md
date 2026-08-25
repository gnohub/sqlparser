# v2.16.10 发布说明

Query Graph 新增 DDL 根 block 与 relation role，将受支持 DDL 中的操作对象和引用对象分别表达为 `TARGET` 与 `REFERENCE`。查询支撑型 DDL target 通过 `source_block` 连接独立 SELECT 来源块。

relation 投影覆盖各方言入口成功解析的常用表、索引、视图、TRUNCATE、RENAME 与 DROP 形态；PostgreSQL-compatible 入口同时覆盖 FOREIGN TABLE 和分区引用。DROP 分段定界状态依据语句内精确来源输出，U& identifier 仍不设置普通定界标志；SQL Server 普通多语句中的 CREATE INDEX/TRUNCATE relation patch 保持公开 SQL surface。

公开 API 新增 DDL block 类型、relation role 枚举、`ddl_role` 字段和 `sqlparser_graph_ddl_relation_role_name()`；在 x86_64 与 AArch64 的 64 位布局中，relation 结构体的 `sizeof` 和全部旧字段 offset 保持不变。新增 66 条 final case 和 113 个 patch 后，九套 fixture 合计 2,968 条 final case 和 9,379 个 patch。完整 `make test`、ABI 导出检查和 identifier 定向 Valgrind 均通过。

内置 `libpg_query` 标签：`17-6.2.2`；内置 Jansson 版本：`2.15`。
