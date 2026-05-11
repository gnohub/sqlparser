# 变更记录

## 未发布

### SQL View 与改写

- 增加通用 `SELECT` 输出列表读取、替换、插入和删除接口
- 增加通用 `WHERE` 条件读取、设置和 `AND` / `OR` 追加接口
- 增加通用 statement 级 `clause` selector，支持通过 `stmt[n].clause[m]` 改写 `select_list`、`where` 和 `order_by`
- SQL View JSON 增加 `statements[].clauses[]`，用于暴露可写语句级子句槽位

### 测试与文档

- 增加 `SELECT` 输出列表和 `WHERE` 条件改写示例
- 增加通用 `clause` patch 示例，覆盖 SELECT 输出列表、WHERE 条件和 ORDER BY 新增
- 示例目录按 `patch`、`convenience`、`inspect`、`dialect` 分类，推荐接入方优先使用 `patch` 示例
- 增加 PostgreSQL、MySQL、Oracle 和 SQL Server 的 WHERE 改写回归用例，覆盖全部已暴露 `where_clause` 的 PostgreSQL AST 类型

## 0.3.0

### 方言能力

- 增加 PostgreSQL 会话 schema 上下文输出，覆盖 `SET search_path`、`SET LOCAL search_path` 和 `SET SCHEMA`
- 增加 MySQL `USE db_name` 默认数据库切换支持
- 增加 SQL Server `USE database_name` 数据库上下文切换支持
- 增加 Oracle `ALTER SESSION SET CURRENT_SCHEMA`、`ALTER SESSION SET CONTAINER` 和 `ALTER SESSION SET CONTAINER ... SERVICE ...` 支持
- 修复多语句输入中的上下文切换语句处理，确保 parse、SQL View JSON 和 deparse 均保持公共 SQL 形态

### SQL View 与改写

- 会话上下文切换语句复用现有 `statements[].objects[].columns[].value` 结构，不新增独立 JSON 格式
- 支持通过 `stmt[n].value[m]` selector 改写上下文切换目标并还原为对应方言 SQL
- 修复 SQL Server、MySQL 和 Oracle deparse 中内部 `sqlparser_current_*` 哨兵泄露的边界问题

### 测试与文档

- 增加 MySQL、Oracle 和 SQL Server 多语句上下文切换回归用例
- 同步更新方言支持文档、官方语法覆盖清单和可执行用例覆盖统计

## 0.2.0

### 核心能力

- 提供稳定的 `sql -> handle -> rewrite -> deparse` 公共 C API
- 支持 `SELECT / INSERT / UPDATE / DELETE / MERGE / TRANSACTION / 常见 DDL` 的解析与结构读取
- 支持关系名、名称原子、字面量、`WHERE` 字面量、`UPDATE assignment` 和 `INSERT cell` 的精确改写
- 支持右值表达式级改写，包括 `DEFAULT` 与任意表达式 SQL
- 支持 SQL View JSON、SQL View C 结构化遍历和 structured patch 写回
- 提供 SQL View JSON 作为按需诊断导出
- 支持可配置资源限制，覆盖 SQL 输入、表达式 SQL 片段、生成输出和语句数量
- 增加方言公共框架，默认 PostgreSQL，并提供 MySQL、Oracle 与 SQL Server 方言转换层
- 收敛默认输出上限到 4MB，并减少 parse/deparse 路径中的常驻 AST 和字符串拷贝

### 发布与构建

- 固定 vendored `libpg_query` 版本并纳入仓库
- 统一发布公共头文件、静态库、动态库与 `pkg-config` 元数据
- 新增严格构建、安装烟测、`valgrind` 泄漏校验、循环回归、benchmark smoke 与一键 `verify` 入口
- 构建系统按编译选项签名自动失效并重建本库对象和 vendor 产物
- 新增 `make abi-check`，校验动态库导出符号与公共头文件一致
- 新增 Linux/GCC GitHub Actions CI 门禁
- CI 增加 JSON fixture 校验和源码包 smoke
- 新增 `make dist` 源码发布包目标
- 新增 Windows/MSVC NMake 构建入口，支持生成静态库、CLI、单元测试与示例程序
- Windows/MSVC 构建使用仓库内 vendored Jansson，避免依赖外部包管理器

### 测试与性能

- 扩充通用 SQL 批量夹具，覆盖子查询、`CASE`、窗口、`ON CONFLICT`、`RETURNING`、`UPDATE ... FROM`、`DELETE ... USING`、`MERGE`、事务控制、常见 DDL、`GRANT/REVOKE` 与维护语句
- 增加 MySQL 方言用例矩阵，覆盖已支持语句形态和明确不支持语法
- 增加 Oracle 方言用例矩阵，覆盖已支持语句形态、公共输出规则和明确不支持语法
- 增加 SQL Server 方言用例矩阵，覆盖已支持 T-SQL 语句形态、公共输出规则和明确不支持语法
- 增加安装态 API 烟测、`valgrind` 泄漏校验与表达式改写回归
- 增加稳定性回归，覆盖畸形 SQL、参数校验、资源限制和失败改写回滚
- benchmark 增加读取链路、改写链路与 `rewrite + deparse` 单次调用统计
- 增加按能力分类的测试入口，覆盖 parse、inspect、rewrite、deparse、SQL View JSON、CLI、install smoke 和 ABI

### 文档

- 提供中英文快速开始、API 手册、SQL View JSON 手册、CLI 手册与架构文档
- 增加 Oracle、SQL Server 方言支持说明和 `v0.2.0` 发布说明
- 增加公开变更记录
