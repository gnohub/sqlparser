# v0.9.0 发布说明

`v0.9.0` 收敛 SQL View 的预编译占位符输出，公共 C 结构和 JSON View 统一使用 `bind_key`、`bind_kind`、`bind_position`、`bind_sql` 和 `bind_selector`。

## 主要变化

- 移除旧 `bind` 输出字段，改为更明确的 `bind_key`。
- `sqlparser_column_view_t` 和 `sqlparser_cell_view_t` 直接暴露 bind 结构化字段，JSON 仅作为按需 view 输出。
- `bind_position` 表示整条输入 SQL 中第几个 bind occurrence，从 1 开始，多语句 SQL 中不会按 statement 重置。
- 匿名 `?`、显式编号位置 bind（如 `:1`、`$1`）和命名 bind（如 `:name`、`@name`）统一输出 `bind_kind`、`bind_key` 和 `bind_sql`。
- PostgreSQL dollar-quoted 字符串内部的占位符样式文本不会参与 bind 全局计数。

## 测试覆盖

- PostgreSQL 可执行用例：97 条，支持 96 条。
- MySQL 可执行用例：68 条，支持 53 条。
- Oracle 可执行用例：104 条，支持 86 条。
- SQL Server 可执行用例：97 条，支持 82 条。
- 达梦可执行用例：76 条，支持 64 条。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- JSON case 文件合法性校验
- 旧 `bind` 字段残留扫描
- Linux `make clean && make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux 定向 CLI 验证多语句 bind 全局序号和 dollar-quoted 字符串计数
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`
- Windows VS 2022 x64 + MSVC `nmake /F Makefile.msvc clean && nmake /F Makefile.msvc test`

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- 动态库 ABI 主版本：`libsqlparser.so.0`
- vendored `libpg_query` tag：`17-6.2.2`
