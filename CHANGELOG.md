# 变更记录

## 0.1.0-dev

### 核心能力

- 提供稳定的 `sql -> handle -> rewrite -> deparse` 公共 C API
- 支持 `SELECT / INSERT / UPDATE / DELETE / MERGE / TRANSACTION / 常见 DDL` 的解析与结构读取
- 支持关系名、名称原子、字面量、`WHERE` 字面量、`UPDATE assignment` 和 `INSERT cell` 的精确改写
- 支持右值表达式级改写，包括 `DEFAULT` 与任意表达式 SQL
- 支持 `parse tree JSON`、`summary JSON` 与稳定模型 JSON 的导出与导入

### 发布与构建

- 固定 vendored `libpg_query` 版本并纳入仓库
- 统一发布公共头文件、静态库、动态库与 `pkg-config` 元数据
- 新增严格构建、安装烟测、循环回归、benchmark smoke 与一键 `verify` 入口
- 构建系统按编译选项签名自动失效并重建本库对象和 vendor 产物

### 测试与性能

- 扩充通用 SQL 批量夹具，覆盖子查询、`CASE`、窗口、`ON CONFLICT`、`RETURNING`、`UPDATE ... FROM`、`DELETE ... USING`、`MERGE`、事务控制、常见 DDL、`GRANT/REVOKE` 与维护语句
- 增加安装态 API 烟测与表达式改写回归
- benchmark 增加读取链路、改写链路与 `rewrite + deparse` 单次调用统计

### 文档

- 提供中英文快速开始、API 手册、模型 JSON 手册、CLI 手册与架构文档
- 增加兼容性策略与公开变更记录
