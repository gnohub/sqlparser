# sqlparser 项目概览与架构

`sqlparser` 是一个通用 SQL 解析、改写和反解析库。

项目使用仓库内固定版本的 `libpg_query` 作为解析内核，并通过 `sqlparser` 公共 C API 对外提供能力。

## 1. 项目定位

`sqlparser` 的目标能力包括：

1. 把 SQL 文本解析成可复用 `handle`
2. 识别语句类型和基础结构
3. 提取表、列、别名、关键字和字面量等信息
4. 提供精确的改写入口
5. 把改写结果重新生成 SQL
6. 以稳定 JSON 形式导出工作模型

当前对外交付产物为：

- 公共头文件 `include/sqlparser/sqlparser.h`
- 静态库 `lib/libsqlparser.a`
- 动态库 `lib/libsqlparser.so`
- 调试和批量验证工具 `bin/sqlparser_cli`

## 2. 典型工作流

`sqlparser` 的标准工作流如下：

1. 输入完整 SQL 文本
2. 调用 `sqlparser_parse()` 得到 `sqlparser_handle_t`
3. 通过语句级 API、通用原子级 API 或语义级 API 读取结构信息
4. 按索引、selector 或 model patch 执行精确改写
5. 调用 `sqlparser_deparse()` 输出最终 SQL

这个链路覆盖了 SQL 处理中常见的两类场景：

- 快速识别语句类型、关键字、表和列
- 在不改变语义类别的前提下对表名、列名或字面量做受控改写

## 3. 架构分层

### 3.1 公共 API 层

公共 API 层定义在 `include/sqlparser/sqlparser.h` 中，所有外部调用都围绕 `sqlparser_handle_t` 展开。

当前 API 分为三组：

- 语句级接口：语句类型、节点名称、目标表、`INSERT`、`UPDATE`、`WHERE`
- 通用原子接口：`relation`、`name`、`literal`
- 外部化改写接口：`selector`、模型 JSON

### 3.2 规范语法树层

`sqlparser` 内部使用 `libpg_query` 的 protobuf AST 作为统一语法树表示。

这一层负责：

- 持有当前语句树
- 作为所有改写的唯一真源
- 驱动后续摘要提取、扫描、JSON 导出和反解析

这样可以保证改写链路始终围绕同一份结构数据进行，不需要把 JSON 作为主表示反复解析。

### 3.3 语义分析层

语义分析层在统一语法树之上提供更接近 SQL 语义的信息。

当前来源包括：

- `libpg_query` 摘要信息
- `libpg_query` 扫描结果
- `sqlparser` 自身的语法树遍历

这一层输出的典型信息有：

- `statement_types`
- `keywords`
- `tables`
- `aliases`
- `selected_columns`
- `join_columns`
- `where_columns`
- `insert_columns`
- `update_columns`
- `all_referenced_columns`

### 3.4 稳定模型层

稳定模型层用于把当前语句树导出为可供外部程序消费的工作模型。

当前提供：

- `sqlparser_export_model_json()`
- `sqlparser_apply_model_json()`
- `sqlparser_selector_parse()`
- `sqlparser_selector_format()`

这一层适合：

- 规则系统保存定位路径
- 外部程序执行补丁回放
- 以 JSON 作为审计和调试载体

### 3.5 反解析层

反解析层负责从当前语法树状态输出 SQL。

`sqlparser_deparse()` 对外提供统一入口，底层仍基于 `libpg_query` 的反解析能力完成 round-trip。

## 4. 数据模型与缓存

一个 `sqlparser_handle_t` 当前持有以下几类数据：

- `source_sql`：最初输入 SQL
- `current_sql`：在发生改写后按需生成的当前 SQL
- `parse_tree`：protobuf AST
- `parse_tree_json`：懒加载 parse tree JSON
- `summary`：懒加载摘要 protobuf
- `scan`：懒加载词法扫描 protobuf
- `model_json`：懒加载稳定模型 JSON

缓存行为如下：

- 初次解析时只建立必要的统一语法树
- JSON、summary、scan 等派生数据按需生成
- 成功改写后，派生缓存会统一失效
- 再次访问时，会基于最新 AST 重新生成

## 5. 公共接口组织方式

### 5.1 语句级接口

语句级接口提供最直接的业务访问路径，适合高频 DML 场景：

- 语句类型识别
- 目标对象读取
- `INSERT` 行列读取与改写
- `UPDATE` assignment 读取与改写
- `WHERE` literal 读取与改写

### 5.2 通用原子接口

通用原子接口适合覆盖更广的 SQL 语法面。

公共原子包括：

- `relation`
- `name`
- `literal`

这组接口既可用于 DML，也可用于 DDL 和多语句输入。

### 5.3 selector 与模型接口

selector 和模型接口用于把改写目标稳定地外部化。

典型用途包括：

- 把修改点保存为文本路径
- 把完整工作模型保存为 JSON
- 在后续请求中用 patch 回放改写

## 6. 内存与线程模型

`sqlparser` 采用清晰的使用模型：

- `handle` 由 `sqlparser_handle_destroy()` 统一释放
- 导出字符串由 `sqlparser_string_free()` 释放
- 视图结构中的字符串均为 borrowed pointer
- 同一个 `handle` 不支持并发读写
- 推荐使用方式是一个线程独占一个 `handle`

这一模型优先保证可预测性和稳定性，适合代理、中间件和数据处理组件。

## 7. 健壮性、可扩展性与性能

### 7.1 健壮性

公共接口具有以下特点：

- 使用结构化状态码和错误对象
- 不要求调用方感知内置解析内核的内部结构
- 修改后统一执行缓存失效
- 只从当前语法树状态生成 SQL

### 7.2 可扩展性

公共 ABI 不暴露 `PgQuery*` 或 PostgreSQL 节点结构，这为后续扩展留出了空间：

- 维护内置解析器补丁
- 引入 SQL 方言适配层
- 调整内部 AST 访问逻辑而不破坏外部 ABI

### 7.3 性能

当前实现优先减少重复解析和重复序列化：

- 解析一次得到长生命周期 `handle`
- 派生结果按需生成
- `summary`、`scan`、模型 JSON 懒加载
- 改写直接作用于 AST，而不是把 JSON 作为主修改路径

## 8. 当前范围与后续扩展

本版本提供的能力包括：

- `SELECT`
- `INSERT`
- `UPDATE`
- `DELETE`
- 多语句输入
- 常见 DDL 分类与名称改写
- `selector` 和模型 JSON 驱动的精确改写

方言支持属于后续扩展方向，当前架构已为以下扩展位预留空间：

- SQL 预处理与后处理适配
- 解析器补丁
- 新节点类型的 protobuf 和反解析器维护

## 9. 文档与代码入口

- API 手册见 [api_reference.md](./api_reference.md)
- `libpg_query` 集成说明见 [libpg_query_analysis.md](./libpg_query_analysis.md)
- 示例程序见 `examples/*.c`
- 公共头文件见 `include/sqlparser/sqlparser.h`
