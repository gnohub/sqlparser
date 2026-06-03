# v2.6.0 发布说明

`v2.6.0` 新增 Vastbase 四个显式兼容模式。调用方可以按实际兼容入口选择 Oracle、MySQL、PostgreSQL 或 SQL Server 模式，库不会根据 SQL 文本自动猜测兼容模式。

## 主要变化

- 公共版本号更新为 `2.6.0`。
- 新增 `SQLPARSER_DIALECT_VASTBASE_ORACLE`、`SQLPARSER_DIALECT_VASTBASE_MYSQL`、`SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` 和 `SQLPARSER_DIALECT_VASTBASE_SQLSERVER`。
- CLI 支持 `vastbase-oracle`、`vastbase-mysql`、`vastbase-postgresql` 和 `vastbase-sqlserver`。
- CLI 中的 `vastbase` 是 `vastbase-oracle` 的确定性别名。
- 四个 Vastbase 模式分别保留对应兼容模式的 public deparse、bind 规则和 Query Graph/View JSON 输出。
- 新增 Vastbase 专项文档、官方语法覆盖统计、四套 case matrix 和方言示例。

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
