# v2.0.0 发布说明

`v2.0.0` 是 `sqlparser` 的稳定接口版本，面向 C 语言调用方提供 SQL 解析、结构化遍历、按 selector 改写以及反解析能力。本版本继续以 `query_graph` 作为结构化输出主数据源，View JSON 仅在调用导出接口时按需生成。

## 主要变化

- 公共版本号更新为 `2.0.0`。
- View JSON 和公共 C 结构统一使用 `bind_key`、`bind_kind`、`bind_position`、`bind_sql` 和 `selector` 表达预编译占位符。
- `bind_position` 表示整条输入 SQL 中第几个 bind occurrence，从 1 开始，多语句 SQL 中不会按 statement 重置。
- 匿名 `?`、显式编号位置 bind（如 `:1`、`$1`）和命名 bind（如 `:name`、`@name`）统一输出结构化 bind 字段。
- SELECT 输出层级使用有序 `target_path` 表达函数、表达式、CASE 和嵌套输出路径。
- View JSON 不输出 `query_graph` 与 DML 结构中的空数组，公共 C 结构仍通过 `count` 或 `has_*` 字段表达空集合。
- PostgreSQL、MySQL、Oracle、SQL Server 和达梦方言测试矩阵继续覆盖 DDL、DML、JOIN、函数、表达式、bind、分页和上下文切换场景。
- CLI batch 输入仅支持顶层数组或 `items` 数组。
- `libpg_query` baseline 保留单线程成功解析和线程首次解析口径。

## 健壮性修复

- 修复 View JSON 序列化过程中 Jansson `_new` 接口失败路径的所有权处理，避免低内存场景下重复释放中间 JSON 节点。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- JSON case 文件合法性校验
- Linux `make clean && make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux 全量 View JSON CLI 用例扫描
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- 动态库 ABI 主版本：`libsqlparser.so.0`
- vendored `libpg_query` tag：`17-6.2.2`
