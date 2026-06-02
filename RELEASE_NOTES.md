# v2.4.0 发布说明

`v2.4.0` 增强 `UPDATE SET` 结构化改写能力。调用方现在可以把赋值右值中的 bind 参数改写为 literal，同时保持目标列、赋值项顺序和当前方言输出规则不变。

## 主要变化

- 公共版本号更新为 `2.4.0`。
- `sqlparser_update_set_assignment_literal()` 支持把 literal 或 bind 右值改写为 literal。
- selector 版本 `sqlparser_selector_set_update_assignment_literal()` 同步支持 bind 右值改写。
- 函数、运算表达式、字段引用、`DEFAULT` 和子查询右值返回 `SQLPARSER_STATUS_UNSUPPORTED`。
- PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix 新增对应回归用例。
- 中英文 API 手册和测试矩阵同步更新。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- Linux `make verify-asan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-ubsan SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make verify-valgrind SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- 动态库 ABI 主版本：`libsqlparser.so.0`
- 当前 ABI 导出符号数：`128`
- vendored `libpg_query` tag：`17-6.2.2`
