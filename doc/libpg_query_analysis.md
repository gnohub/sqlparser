# libpg_query 集成说明

本文档说明 `sqlparser` 如何集成仓库内固定版本的 `libpg_query`，以及后续维护和方言扩展需要关注的边界。

## 1. 依赖角色

`libpg_query` 是 `sqlparser` 的解析后端，负责提供以下基础能力：

- SQL 解析
- 词法扫描
- 语句摘要提取
- protobuf AST 与 PostgreSQL 节点之间的转换
- SQL 反解析

`sqlparser` 在此基础上完成：

- 公共 ABI 封装
- 语义提取
- 稳定 selector
- 稳定模型 JSON
- 统一错误与生命周期管理

## 2. 固定版本

项目固定使用以下版本：

- tag: `17-6.2.2`
- commit: `7be1aed1f1f968a36cf541319f71e845850f0381`

使用策略如下：

- `libpg_query` 以仓库内固定源码的方式保存
- 版本升级由项目单独评估
- 解析器、反解析器和方言相关维护由项目负责

## 3. 当前复用的核心能力

### 3.1 解析

`sqlparser` 主要复用：

- `pg_query_parse_protobuf()`

这条路径直接返回 protobuf AST，适合作为内部统一语法树表示。

### 3.2 扫描

`sqlparser` 复用：

- `pg_query_scan()`

这条路径主要用于：

- token 级信息
- 关键字辅助分析
- 与语句文本相关的补充提取

### 3.3 摘要

`sqlparser` 复用：

- `pg_query_summary()`

这条路径主要用于快速提取：

- `statement_types`
- `tables`
- `aliases`
- `functions`
- `filter_columns`

### 3.4 反解析

`sqlparser` 复用：

- `pg_query_deparse_protobuf()`

`sqlparser` 对 AST 的所有改写最终都通过这条路径重新生成 SQL。

### 3.5 分段

`sqlparser` 复用：

- `pg_query_split_with_parser()`

这条路径用于多语句 SQL 的语句数识别和基础分段能力。

## 4. protobuf AST 的角色

`sqlparser` 内部统一以 protobuf AST 作为语法树真源，主要因为：

- 解析结果可以稳定保存到 `handle`
- 改写可以直接作用于 AST
- 反解析路径天然消费 protobuf AST
- JSON 可以按需导出，不承担主表示职责

当前链路如下：

```text
SQL -> libpg_query protobuf AST -> sqlparser handle -> rewrite -> deparse -> SQL
```

## 5. 仓库中的关键目录

`libpg_query` 相关源码主要位于：

- `vendor/libpg_query/src/`
- `vendor/libpg_query/src/postgres/`
- `vendor/libpg_query/srcdata/`
- `vendor/libpg_query/scripts/`

这些目录分别承担以下职责：

- `src/`：公开入口和封装代码
- `src/postgres/`：抽取后的 PostgreSQL 相关源码
- `srcdata/`：protobuf 和节点生成所需的数据定义
- `scripts/`：抽取和生成脚本

## 6. sqlparser 对 libpg_query 的封装边界

`sqlparser` 保持以下边界：

- 公共头文件不暴露 `PgQuery*` 类型
- 公共 ABI 不暴露 PostgreSQL 节点结构
- 对外 JSON 不直接承诺 `libpg_query` 原生 JSON 布局
- 对外改写入口统一由 `sqlparser` 管理

这一层封装的作用是把解析内核的内部实现与公共 API 解耦。

## 7. 维护重点

维护重点集中在以下几个方向：

- 解析、改写、反解析主链路稳定
- 统一 JSON 依赖和构建方式
- 保持公共头文件与 ABI 简洁可控
- 用 `examples`、单测和 benchmark 持续验证行为

## 8. 方言扩展预留

方言支持是后续扩展方向，当前架构已经为以下改动预留位置：

- SQL 预处理和后处理适配
- 解析器和扫描器补丁
- 新节点类型的 protobuf 描述
- 读写函数和反解析器的同步维护

如果后续需要新增方言语法，通常需要同步检查以下部分：

- 语法定义和扫描器
- `srcdata/*` 中的节点和枚举描述
- protobuf 生成结果
- read/write 转换代码
- 反解析器对应分支
- 回归测试与 benchmark

## 9. 使用建议

- 把 `libpg_query` 视为固定版本内核，而不是对外公共接口
- 对外接入统一走 `sqlparser` 头文件和库
- 调试底层语法树时可导出 parse tree JSON
- 做业务改写时优先使用 `sqlparser` 的原子级 API、selector 和模型 JSON

## 10. 相关文档

- 项目概览见 [sqlparser_architecture.md](./sqlparser_architecture.md)
- API 使用说明见 [api_reference.md](./api_reference.md)
