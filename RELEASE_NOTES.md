# v0.7.0 发布说明

`v0.7.0` 增加 `UPDATE SET` 赋值项级 patch 能力，让调用方可以通过统一的 `sqlparser_apply_patch()` 完成赋值项追加、删除和整项替换，同时保持既有右值改写接口语义不变。

## 主要变化

- 新增 `SQLPARSER_PATCH_INSERT_ASSIGNMENT`、`SQLPARSER_PATCH_DELETE_ASSIGNMENT` 和 `SQLPARSER_PATCH_REPLACE_ASSIGNMENT`。
- 新增 `sqlparser_update_insert_assignment_sql()`、`sqlparser_update_delete_assignment()`、`sqlparser_update_set_assignment_full_sql()` 及对应 selector API。
- `stmt[n].assignment[i]` 可定位 `UPDATE SET` 赋值项；插入时 `i` 等于当前赋值项数量表示追加。
- `delete_assignment` 不允许删除最后一个赋值项，避免生成非法 `UPDATE SET`。
- 既有 `SQLPARSER_PATCH_REPLACE` 对 assignment 的语义保持不变，仍用于改写右值 SQL。
- 新增 `examples/patch/17_update_set_patch.c`，展示完整 patch 工作流。
- MSVC NMake 示例清单同步包含新增 example。

## 测试覆盖

- 核心 API 覆盖 assignment 插入、删除、整项替换和反解析后重解析。
- patch API 覆盖 Oracle bind 片段，验证不会暴露内部参数。
- 健壮性测试覆盖非法 selector、越界索引、多赋值项 full replacement、空 `SET` 保护和失败后 handle 可用性。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- 敏感信息和旧接口残留扫描
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux `make abi-check DEBUG=0 SHOW_WARNING=0 SHOW_VENDOR_WARNING=0`
- Linux `make verify-asan`
- Linux `make verify-ubsan`
- Linux `make verify-valgrind`
- Windows MSVC `nmake /F Makefile.msvc test`

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- 动态库 ABI 主版本：`libsqlparser.so.0`
- vendored `libpg_query` tag：`17-6.2.2`
