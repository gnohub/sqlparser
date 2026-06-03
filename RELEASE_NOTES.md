# v2.5.0 发布说明

`v2.5.0` 增强 `LIKE ... ESCAPE ...` 的结构化表达能力。调用方现在可以在 Query Graph 和 View JSON 中稳定读取 pattern 右值对应的显式 escape 子句，同时 public deparse 保持各方言公开 SQL 形态。

## 主要变化

- 公共版本号更新为 `2.5.0`。
- `sqlparser_graph_value_t` 新增 `like_escape` 字段。
- 新增 `sqlparser_graph_like_escape_kind_t`，区分无显式 `ESCAPE`、字面量、预编译占位符和表达式 escape。
- View JSON 在 pattern 主 value 上输出 `like_escape`，避免把 escape 子句拆成独立业务值。
- PostgreSQL、MySQL、Oracle、SQL Server 和达梦反解析不再暴露 `pg_catalog.like_escape(...)`，输出保持 `LIKE pattern ESCAPE escape`。
- 结构化识别只接受 libpg_query 生成的 `pg_catalog.like_escape`，不会误判用户 SQL 中未限定的同名函数。
- 中英文 API 手册、View JSON 手册和测试矩阵同步更新。

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
- 当前 ABI 导出符号数：`128`
- vendored `libpg_query` tag：`17-6.2.2`
