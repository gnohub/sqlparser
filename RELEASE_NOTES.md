# v0.8.0 发布说明

`v0.8.0` 扩充方言可执行用例覆盖，并补齐 Oracle 普通 `ALTER SESSION SET` 会话参数赋值与 MySQL `LIMIT ?, ?` 参数化分页语句。

## 主要变化

- Oracle 支持 `ALTER SESSION SET <parameter> = <value>` 普通会话参数赋值。
- Oracle 新增覆盖 `NLS_DATE_FORMAT`、`NLS_DATE_LANGUAGE`、`NLS_NUMERIC_CHARACTERS`、`INSTANCE`、`ERROR_ON_OVERLAP_TIME`。
- Oracle 公开 deparse 和 SQL View 输出保持原始参数名和值，不暴露内部适配前缀。
- MySQL 支持 `LIMIT ?, ?` 参数化分页，公开 deparse 保持 MySQL 逗号分页形态。
- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix。

## 测试覆盖

- PostgreSQL 可执行用例：95 条。
- MySQL 可执行用例：67 条，支持 52 条。
- Oracle 可执行用例：103 条，支持 85 条。
- SQL Server 可执行用例：95 条，支持 80 条。
- 达梦可执行用例：75 条，支持 63 条。
- Oracle 官方语法覆盖统计更新为 46 个语法组中 32 个 `CURRENT`，14 个 `MODEL_REQUIRED`。

## 发布验证

本版本的发布验证包括：

- `git diff --check`
- JSON case 文件合法性校验
- Linux `make test SHOW_WARNING=1 STRICT=1 SHOW_VENDOR_WARNING=0`
- Linux Oracle 定向 case matrix 和 CLI deparse / view 验证
- Linux `make verify-asan`
- Linux `make verify-ubsan`
- Linux `make verify-valgrind`

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- 动态库 ABI 主版本：`libsqlparser.so.0`
- vendored `libpg_query` tag：`17-6.2.2`
