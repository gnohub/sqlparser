# v0.3.0 发布说明

`v0.3.0` 是 `sqlparser` 的方言能力小版本更新，重点补齐数据库、schema 和会话上下文切换语句的解析、结构化读取、改写和反解析链路。

## 主要变化

- PostgreSQL 支持 `SET search_path`、`SET LOCAL search_path` 和 `SET SCHEMA` 的 SQL View 输出。
- MySQL 支持 `USE db_name` 默认数据库切换，包含反引号数据库名。
- SQL Server 支持 `USE database_name` 数据库上下文切换，deparse 保持方括号公共形态。
- Oracle 支持 `ALTER SESSION SET CURRENT_SCHEMA`、`ALTER SESSION SET CONTAINER` 和 `ALTER SESSION SET CONTAINER ... SERVICE ...`。
- 上下文切换语句复用现有 SQL View JSON 结构，不新增独立 JSON 格式。
- 支持通过 `stmt[n].value[m]` selector 改写上下文切换目标并还原为对应方言 SQL。
- 修复多语句输入中上下文切换语句的 parse/deparse 边界，避免输出内部 `sqlparser_current_*` 哨兵名。
- 更新方言支持文档、官方语法覆盖清单和可执行用例覆盖统计。

## 方言支持边界

当前可执行用例矩阵：

- PostgreSQL：54 条用例，53 条支持路径，1 条非法 SQL 负向路径。
- MySQL：32 条用例，17 条支持路径，15 条明确不支持路径。
- Oracle：65 条用例，46 条支持路径，19 条明确不支持路径。
- SQL Server：61 条基础用例，46 条支持路径，15 条明确不支持路径；官方 `HOOK_ONLY` 覆盖矩阵包含 235 条用例。

## 发布验证

本版本的发布门禁包括：

- `git diff --check`
- JSON fixture 格式校验
- Linux GCC 8.3：`make test`
- Linux GCC 8.3：`make verify-ci`
- ABI 导出符号检查
- CLI parse/deparse 抽样，覆盖 MySQL、Oracle、SQL Server 多语句上下文切换

## 发布边界

- 公共头文件：`include/sqlparser/sqlparser.h`
- SQL View JSON：通过 `sqlparser_export_view_json()` 按需导出
- SQL View C 结构化遍历：通过 `sqlparser_get_view()` 和相关 view API 按需读取
- 动态库 ABI 主版本：`libsqlparser.so.0`
- vendored `libpg_query` tag：`17-6.2.2`
