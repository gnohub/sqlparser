# v2.8.1 发布说明

`v2.8.1` 是 `query_graph` 性能补丁版本，重点优化大型 / 多层 SQL 场景下 `sqlparser_statement_query_graph()` 的冷调用耗时，尤其改善 `INSERT ... SELECT`、集合查询和嵌套 SELECT。本版本不新增公共 API，不改变公共结构体布局和 selector 输出语义。

## 主要变化

- 公共版本号更新为 `2.8.1`。
- `query_graph` 构建期新增 statement 级 selector 缓存，一次遍历记录 value、name、relation 和 select target list 的 selector 索引。
- 避免大型 / 多层 `query_graph` 构建中为每个 value、field、relation 或 SELECT target list 重复执行全 statement protobuf 树搜索。
- 保持 `sqlparser_statement_query_graph()` 接口、公共结构体布局、selector 输出格式和同一 handle 的 query graph cache 行为不变。
- 公共头文件布局未变化；调用方不需要因为公共结构变更做适配。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- 动态库 ABI 主版本：`libsqlparser.so.0`
- 当前 ABI 导出符号数：`135`
- vendored `libpg_query` tag：`17-6.2.2`
