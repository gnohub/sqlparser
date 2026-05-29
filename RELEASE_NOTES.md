# v2.2.0 发布说明

`v2.2.0` 增加结构化 SQL 片段改写接口，面向需要修改字段列表或 `UPDATE SET` 赋值项的 C 调用方。调用方可以传入结构化标识符路径和右值 SQL，由库按当前 handle 的方言规则构造 AST 片段并完成反解析。

## 主要变化

- 公共版本号更新为 `2.2.0`。
- 新增 `sqlparser_identifier_path_view_t`，用于表达单段列名、限定列名或更长标识符路径。
- 新增结构化 `UPDATE SET` 赋值构造能力，支持通过列路径和右值 SQL 生成、追加或替换赋值项。
- 新增结构化 `SELECT` 输出项替换能力，支持把单个输出项替换为多个列路径。
- 新增结构化改写示例 `examples/convenience/18_structured_fragment_rewrite.c`。
- 中英文 API 手册、示例说明和测试说明同步更新。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`
- Windows/MSVC `nmake /F Makefile.msvc test`

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- 动态库 ABI 主版本：`libsqlparser.so.0`
- 当前 ABI 导出符号数：`124`
- vendored `libpg_query` tag：`17-6.2.2`
