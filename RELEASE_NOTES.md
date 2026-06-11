# v2.8.0 发布说明

`v2.8.0` 扩展 Query Graph、DML 结构化输出和多方言覆盖，重点增强 Oracle P3 结构化输出、MySQL / SQL Server / 达梦 / Vastbase 方言兼容能力，以及 national / Unicode 字符串公开形态保真。

## 主要变化

- 公共版本号更新为 `2.8.0`。
- `query_graph` 增加 predicate 结构，用于表达比较、布尔组合、`EXISTS` 和表达式谓词。
- DML assignment、cell 和 branch 增加 source field / source target 关联，用于表达字段搬运、`MERGE` source lineage 和多分支 insert 来源。
- Oracle 覆盖 alias-qualified `UPDATE`、`INSERT ALL` / `INSERT FIRST`、`MERGE` source lineage、`DISTINCT`、`ORDER BY` 和 qualified star + `ROWID`。
- MySQL、SQL Server、达梦和 Vastbase 兼容模式补齐多类可由现有结构安全表达的方言语法。
- MySQL、Oracle、PostgreSQL、达梦及 Vastbase 对应兼容模式支持 `N'...'` / `n'...'` national 字符串公开形态保真。
- Oracle、达梦及 Vastbase-Oracle 保留 `nq'...'` national q-quoted 字符串语义。
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
- 当前 ABI 导出符号数：`135`
- vendored `libpg_query` tag：`17-6.2.2`
