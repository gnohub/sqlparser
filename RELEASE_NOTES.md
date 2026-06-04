# v2.7.0 发布说明

`v2.7.0` 为 Query Graph 增加操作符结构化分类。调用方可以通过公共枚举判断 `LIKE`、`NOT LIKE`、`ILIKE` 和 `NOT ILIKE` 这类 pattern-match 语义，不需要依赖操作符字符串比较。

## 主要变化

- 公共版本号更新为 `2.7.0`。
- `sqlparser_graph_value_t` 新增 `operator_kind`。
- 新增 `sqlparser_graph_operator_kind_t`，枚举值包括 `unknown`、`like`、`not_like`、`ilike` 和 `not_ilike`。
- 新增 `sqlparser_graph_operator_kind_name()`、`sqlparser_graph_operator_is_like_pattern()` 和 `sqlparser_graph_value_is_like_pattern()`。
- View JSON 的 `query_graph.values[]` 在存在 `operator` 时输出 `operator_kind`。
- `LIKE ... ESCAPE ...` 的结构化识别复用 `operator_kind`，显式 `ESCAPE` 仍通过 `like_escape` 表达。
- 公共头文件的结构体布局发生变化，使用本版本头文件重新编译调用方后再链接本版本库。

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
- 当前 ABI 导出符号数：`131`
- vendored `libpg_query` tag：`17-6.2.2`
