# 变更记录

## 未发布

## 0.8.0

### 方言能力

- 支持 Oracle 普通 `ALTER SESSION SET <parameter> = <value>` 会话参数赋值，覆盖字符串、标识符、数字和布尔/枚举值
- 保持 Oracle `ALTER SESSION` 公开输出为原始参数名和值，不暴露内部适配前缀
- 修复 MySQL `LIMIT ?, ?` 参数化分页语句，公开反解析保持 MySQL 逗号分页形态

### 测试与覆盖

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦现有 case matrix，覆盖更多 DDL、DML、JOIN、函数、表达式、bind、分页和上下文切换场景
- 新增 Oracle `ALTER SESSION SET NLS_DATE_FORMAT`、`NLS_DATE_LANGUAGE`、`NLS_NUMERIC_CHARACTERS`、`INSTANCE`、`ERROR_ON_OVERLAP_TIME` 回归用例
- 同步更新中英文 case matrix、方言覆盖统计和 Oracle 官方语法覆盖统计

## 0.7.0

### UPDATE SET 改写

- 增加 `UPDATE SET` 赋值项级 patch 能力，支持通过 `stmt[n].assignment[i]` 追加、删除和整项替换赋值项
- 新增 `SQLPARSER_PATCH_INSERT_ASSIGNMENT`、`SQLPARSER_PATCH_DELETE_ASSIGNMENT` 和 `SQLPARSER_PATCH_REPLACE_ASSIGNMENT`
- 新增 `sqlparser_update_insert_assignment_sql()`、`sqlparser_update_delete_assignment()`、`sqlparser_update_set_assignment_full_sql()` 及对应 selector API
- 保持既有 `SQLPARSER_PATCH_REPLACE` 对 assignment 的右值改写语义不变

### 测试与文档

- 增加 `examples/patch/17_update_set_patch.c`，展示通过 `sqlparser_apply_patch()` 追加、删除和整项替换 `UPDATE SET` 赋值项
- 扩充核心 API 和健壮性回归测试，覆盖 Oracle bind 片段、非法 selector、越界索引、空 `SET` 保护和失败后 handle 可用性
- 更新中英文 API 手册、SQL View JSON 手册、示例说明和 MSVC 示例构建清单

## 0.6.0

### SQL View 结构

- 将 SQL View C 结构作为结构化输出的主数据源，`sqlparser_export_view_json()` 改为按需从 SQL View C 结构序列化 JSON
- 扩展 `sqlparser_column_view_t` 和 `sqlparser_cell_view_t`，输出 bind 名称、bind 类型、原始 bind SQL、bind selector、子句编号和 SELECT 输出路径
- 增加 `sqlparser_bind_kind_t`、`sqlparser_bind_kind_name()`、`sqlparser_statement_clause_at()` 和 `sqlparser_clause_sql()` 公共接口
- 扩展 `sqlparser_clause_kind_t`，增加 `on`、`group_by` 和 `having` 子句类型
- SQL View JSON 移除旧的 `target_kind`、`target_name`、`target_arg_index` 字段，统一使用有序 `target_path` 表达 SELECT 输出层级

### 语义与方言

- bind 输出区分 positional 和 named 两类，保留 `bind_sql` 用于区分 `?`、`:1`、`:name`、`$1`、`@name` 等原始 SQL 形态
- bind 右值不再重复暴露为普通 `value`，避免调用方把占位符误判为字面量值
- SELECT 输出表达式列不再暴露条件运算符和值，输出形态通过 `target_path` 表示
- `NOT IN`、`NOT LIKE`、`NOT ILIKE` 和 `NOT SIMILAR TO` 运算符保持完整公共 SQL 语义

### 测试与文档

- 扩充 PostgreSQL、MySQL、Oracle、SQL Server 和达梦用例矩阵，覆盖更多 SELECT、INSERT、UPDATE、DELETE、JOIN、函数、表达式和 bind 场景
- 增加 SQL View 公共 C 结构语义测试，验证结构体字段和 View JSON 输出一致
- 增加 bind 字段、cell bind、`clause_id` 和 `target_path` 的通用断言
- 更新中英文 API 手册和 SQL View JSON 手册

## 0.5.0

### SQL View 语义

- 增加 SELECT 输出项语义路径，使用 `target_path` 表达函数、表达式、CASE 和嵌套输出层级
- 为 SQL View JSON 增加字段归属子句编号，便于区分 SELECT、WHERE、JOIN/ON、ORDER BY 等位置的字段引用
- 扩充各方言的 View JSON 语义用例，覆盖函数输出、表达式输出、星号输出、多层 SELECT 和 bind 条件

## 0.4.0

### 方言能力

- 增加达梦 `SQLPARSER_DIALECT_DAMENG` 方言转换层，覆盖 `SET SCHEMA`、`MINUS`、`LIMIT`、`TOP`、bind、常见 DML/DDL、事务和权限语句
- 增加达梦公共输出规则，反解析和 SQL View JSON 不暴露内部参数名或内部转换 SQL
- 增加 PostgreSQL、MySQL、Oracle、SQL Server 和达梦的预编译 / 参数化 SQL 语句覆盖，包含 SQL Server `sp_executesql` 与达梦 `EXEC SQL PREPARE`

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
- 增加达梦方言用例矩阵、官方语法覆盖统计、CLI 批量夹具和方言示例
- 同步更新 prepared / bind 相关用例矩阵、方言覆盖统计和官方语法覆盖统计

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
