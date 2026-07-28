# API 手册

本文档说明 `sqlparser` 公共 C API 的主要类型、生命周期规则、结构化读取接口和改写接口。

## 概述

`sqlparser` 以 `sqlparser_handle_t` 为中心提供能力。标准流程如下：

1. 调用 `sqlparser_parse()` 或 `sqlparser_parse_with_options()` 解析 SQL。
2. 通过语句接口、selector 接口或 `query_graph` 接口读取结构信息。
3. 使用 selector、细粒度改写函数或 `sqlparser_apply_patch()` 修改 AST。
4. 调用 `sqlparser_deparse()` 生成改写后的 SQL。
5. 使用 `sqlparser_handle_destroy()` 释放 handle。

View JSON 是 `query_graph` 的按需 JSON 序列化，主要用于回归测试、集成验证和跨语言查看结果。业务代码优先使用 C 结构接口，不需要为了改写 SQL 先生成 JSON。

## 头文件与链接

公共头文件：

```c
#include "sqlparser/sqlparser.h"
```

公共库文件：

- `lib/libsqlparser.a`
- `lib/libsqlparser.so`

## 快速示例

```c
#include <stdio.h>
#include "sqlparser/sqlparser.h"

int main(void)
{
    const char *sql = "UPDATE public.users SET name = 'bob' WHERE id = 1";
    sqlparser_handle_t *handle = NULL;
    sqlparser_error_t err;
    sqlparser_literal_value_t value;
    char *out_sql = NULL;

    if (sqlparser_parse(sql, &handle, &err) != SQLPARSER_STATUS_OK) {
        printf("parse failed: %s\n", err.message);
        return 1;
    }

    value.kind = SQLPARSER_LITERAL_KIND_STRING;
    value.string_value = "carol";
    value.float_value = NULL;
    value.integer_value = 0;
    value.boolean_value = 0;

    if (sqlparser_update_set_assignment_literal(handle, 0, 0, &value, &err) != SQLPARSER_STATUS_OK) {
        printf("rewrite failed: %s\n", err.message);
        sqlparser_handle_destroy(handle);
        return 1;
    }

    if (sqlparser_deparse(handle, &out_sql, &err) != SQLPARSER_STATUS_OK) {
        printf("deparse failed: %s\n", err.message);
        sqlparser_handle_destroy(handle);
        return 1;
    }

    printf("%s\n", out_sql);

    sqlparser_string_free(out_sql);
    sqlparser_handle_destroy(handle);
    return 0;
}
```

## 返回状态与错误对象

大多数 API 返回 `sqlparser_status_t`。

| 状态码 | 说明 |
| --- | --- |
| `SQLPARSER_STATUS_OK` | 操作成功 |
| `SQLPARSER_STATUS_INVALID_ARGUMENT` | 参数非法 |
| `SQLPARSER_STATUS_NO_MEMORY` | 内存分配失败 |
| `SQLPARSER_STATUS_PARSE_ERROR` | SQL 解析失败 |
| `SQLPARSER_STATUS_INTERNAL_ERROR` | 内部处理失败 |
| `SQLPARSER_STATUS_UNSUPPORTED` | 当前语句形态不支持该操作 |
| `SQLPARSER_STATUS_RESOURCE_LIMIT` | 输入、输出或语句数量超过资源限制 |

`sqlparser_error_t` 字段：

| 字段 | 说明 |
| --- | --- |
| `code` | 状态码 |
| `cursor` | 字符位置 |
| `line` | 行号 |
| `column` | 列号 |
| `message` | 错误消息 |

## 核心类型

### handle

`sqlparser_handle_t` 表示一次解析后的长期对象。内部保存原始 SQL、当前语法树、方言状态和按需生成的派生缓存。

### 常用枚举

| 枚举类型 | 说明 |
| --- | --- |
| `sqlparser_statement_kind_t` | 语句类型 |
| `sqlparser_insert_source_kind_t` | `INSERT` 数据来源 |
| `sqlparser_value_kind_t` | 精细值读取接口中的值类型 |
| `sqlparser_bind_kind_t` | 预编译占位符类型 |
| `sqlparser_graph_field_match_kind_t` | query graph 条件值的字段匹配形态 |
| `sqlparser_graph_operator_kind_t` | query graph value 的结构化操作符分类 |
| `sqlparser_graph_like_escape_kind_t` | query graph 中显式 `LIKE ... ESCAPE ...` 的 escape 形态 |
| `sqlparser_graph_predicate_kind_t` | query graph predicate 节点类型 |
| `sqlparser_graph_predicate_bool_t` | query graph boolean predicate 连接类型 |
| `sqlparser_control_node_kind_t` | 控制流节点类型 |
| `sqlparser_control_item_kind_t` | 控制流条目引用类型 |
| `sqlparser_graph_dml_result_kind_t` | DML 结果通道类型 |
| `sqlparser_graph_dml_reference_kind_t` | DML 结果字段来源类型 |
| `sqlparser_literal_kind_t` | 字面量类型 |
| `sqlparser_selector_kind_t` | selector 类型 |
| `sqlparser_clause_kind_t` | query graph 与 clause patch 使用的子句类型 |
| `sqlparser_dialect_t` | SQL 方言类型 |

`sqlparser_identifier_path_view_t` 用于向结构化改写接口传入标识符路径。

| 字段 | 说明 |
| --- | --- |
| `parts` | 标识符分段数组，由调用方持有，库只在调用期间读取 |
| `part_count` | 标识符分段数量，必须大于 `0` |

`part_count = 1` 表示单列名，例如 `phone_backup`；`part_count = 2` 表示限定列名，例如 `u.phone`；更长路径按当前 handle 的方言规则反解析。每个分段必须非空，调用方不需要传入引号字符。

`sqlparser_bind_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_BIND_KIND_NONE` | `0` | 没有 bind |
| `SQLPARSER_BIND_KIND_POSITIONAL` | `1` | 位置 bind，例如 `?`、`:1`、`$1` |
| `SQLPARSER_BIND_KIND_NAMED` | `2` | 命名 bind，例如 `:name`、`@name` |

bind 字段规则：

- `bind_key` 按 `bind_kind` 解释；命名 bind 为名称，匿名 `?` 为全局序号字符串，显式编号 bind 保留 SQL 中的编号字符串。
- `bind_position` 是整条输入 SQL 中的 bind 出现序号，从 1 开始；多语句输入不按 statement 重置。
- `bind_sql` 保留 SQL 中的原始占位符文本。

`sqlparser_graph_value_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_VALUE_LITERAL` | `1` | 字面量值 |
| `SQLPARSER_GRAPH_VALUE_BIND` | `2` | 预编译占位符 |
| `SQLPARSER_GRAPH_VALUE_DEFAULT` | `3` | `DEFAULT` |
| `SQLPARSER_GRAPH_VALUE_EXPRESSION` | `4` | 函数、运算符、`CASE` 等表达式 |
| `SQLPARSER_GRAPH_VALUE_FIELD` | `5` | 值侧直接引用 SQL 中的字段，例如 field-to-field predicate、DML assignment RHS 或 source query 输出字段 |

`sqlparser_graph_like_escape_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_NONE` | `0` | 没有显式 `ESCAPE` 子句 |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL` | `1` | `ESCAPE` 是字面量 |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_BIND` | `2` | `ESCAPE` 是预编译占位符 |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION` | `3` | `ESCAPE` 是函数、运算符或其他表达式 |

`sqlparser_graph_field_match_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN` | `0` | 未关联字段或无法稳定判断 |
| `SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD` | `1` | 条件左侧是直接字段，例如 `secret = ?` |
| `SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD` | `2` | 条件左侧字段位于函数、类型转换、表达式或 `CASE` 中，例如 `UPPER(secret) = ?` |

`sqlparser_graph_operator_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_OPERATOR_UNKNOWN` | `0` | 未分类或不是 pattern-match 操作符 |
| `SQLPARSER_GRAPH_OPERATOR_LIKE` | `1` | `LIKE` |
| `SQLPARSER_GRAPH_OPERATOR_NOT_LIKE` | `2` | `NOT LIKE` |
| `SQLPARSER_GRAPH_OPERATOR_ILIKE` | `3` | `ILIKE` |
| `SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE` | `4` | `NOT ILIKE` |

`sqlparser_graph_predicate_kind_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_PREDICATE_UNKNOWN` | `0` | 未分类谓词 |
| `SQLPARSER_GRAPH_PREDICATE_COMPARISON` | `1` | 字段和值、字段和字段之间的比较 |
| `SQLPARSER_GRAPH_PREDICATE_BOOL` | `2` | `AND`、`OR`、`NOT` 组合谓词 |
| `SQLPARSER_GRAPH_PREDICATE_EXISTS` | `3` | `EXISTS` 谓词 |
| `SQLPARSER_GRAPH_PREDICATE_EXPRESSION` | `4` | 无法安全拆分为字段和值侧的表达式谓词 |

`sqlparser_graph_predicate_bool_t`：

| 枚举 | 数值 | 说明 |
| --- | --- | --- |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_NONE` | `0` | 非 boolean 谓词 |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_AND` | `1` | `AND` |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_OR` | `2` | `OR` |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_NOT` | `3` | `NOT` |

`sqlparser_clause_kind_t`：

| 枚举 | JSON 名称 | 说明 |
| --- | --- | --- |
| `SQLPARSER_CLAUSE_KIND_SELECT_LIST` | `select_list` | SELECT 输出列表 |
| `SQLPARSER_CLAUSE_KIND_WHERE` | `where` | WHERE 条件 |
| `SQLPARSER_CLAUSE_KIND_ORDER_BY` | `order_by` | ORDER BY 排序 |
| `SQLPARSER_CLAUSE_KIND_SET_LIST` | `set_list` | UPDATE SET 列表 |
| `SQLPARSER_CLAUSE_KIND_ON` | `on` | JOIN 或 MERGE ON 条件 |
| `SQLPARSER_CLAUSE_KIND_GROUP_BY` | `group_by` | GROUP BY 分组 |
| `SQLPARSER_CLAUSE_KIND_HAVING` | `having` | HAVING 条件 |
| `SQLPARSER_CLAUSE_KIND_DML_RESULT` | `dml_result` | DML 结果输出列表 |
| `SQLPARSER_CLAUSE_KIND_CONDITION` | `condition` | 控制流条件表达式 |

`sqlparser_graph_dml_result_kind_t`：

| 枚举 | 说明 |
| --- | --- |
| `SQLPARSER_GRAPH_DML_RESULT_CLIENT` | 返回给调用端的结果通道 |
| `SQLPARSER_GRAPH_DML_RESULT_SINK` | 写入目标 relation 的结果通道 |

`sqlparser_graph_dml_reference_kind_t`：

| 枚举 | 说明 |
| --- | --- |
| `SQLPARSER_GRAPH_DML_REFERENCE_TARGET_BEFORE` | 目标行修改前的字段，例如 SQL Server `DELETED.id` |
| `SQLPARSER_GRAPH_DML_REFERENCE_TARGET_AFTER` | 目标行修改后的字段，例如 SQL Server `INSERTED.id` |
| `SQLPARSER_GRAPH_DML_REFERENCE_SOURCE` | DML 来源 relation 的字段 |

## 资源限制与解析选项

`sqlparser_limits_t` 默认限制为：SQL 输入 4MB、生成输出 4MB、单次解析 64 条语句。

| 字段 | 说明 |
| --- | --- |
| `struct_size` | 结构体大小，由 `sqlparser_limits_default()` 填充 |
| `max_sql_bytes` | SQL 输入和表达式 SQL 片段最大字节数 |
| `max_output_bytes` | 生成 SQL 或 JSON 输出最大字节数 |
| `max_statement_count` | 单次解析允许的最大语句数量 |

`sqlparser_parse_options_t`：

| 字段 | 说明 |
| --- | --- |
| `struct_size` | 结构体大小，由 `sqlparser_parse_options_default()` 填充 |
| `dialect` | SQL 方言；默认是 `SQLPARSER_DIALECT_POSTGRESQL` |
| `limits` | 资源限制 |
| `flags` | 保留字段，当前保持为 `0` |

已定义方言：

| 方言 | 说明 |
| --- | --- |
| `SQLPARSER_DIALECT_POSTGRESQL` | 默认方言 |
| `SQLPARSER_DIALECT_MYSQL` | MySQL 方言转换层 |
| `SQLPARSER_DIALECT_ORACLE` | Oracle 方言转换层 |
| `SQLPARSER_DIALECT_SQLSERVER` | SQL Server 方言转换层 |
| `SQLPARSER_DIALECT_DAMENG` | 达梦方言转换层 |
| `SQLPARSER_DIALECT_VASTBASE_ORACLE` | Vastbase Oracle 兼容模式 |
| `SQLPARSER_DIALECT_VASTBASE_MYSQL` | Vastbase MySQL 兼容模式 |
| `SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` | Vastbase PostgreSQL 兼容模式 |
| `SQLPARSER_DIALECT_VASTBASE_SQLSERVER` | Vastbase SQL Server 兼容模式 |

## 生命周期与线程模型

- `sqlparser_parse()` 返回的 handle 由 `sqlparser_handle_destroy()` 释放。
- `sqlparser_deparse()`、`sqlparser_export_view_json()` 和渲染类函数返回的字符串由 `sqlparser_string_free()` 释放。
- C 结构视图中的字符串均为 borrowed pointer，归属 handle，不允许调用方释放。
- 成功 patch 或任何 AST 修改后，旧的 borrowed pointer、selector 读取结果和 query graph view 均失效。
- 同一个 handle 不支持并发读写，也不保证多线程只读并发安全；推荐一个线程独占一个 handle。

## 版本与名称辅助函数

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_version_string()` | 返回库版本字符串 |
| `sqlparser_libpg_query_tag()` | 返回仓库内固定 `libpg_query` 版本 tag |
| `sqlparser_statement_kind_name()` | 返回语句类型名称 |
| `sqlparser_insert_source_kind_name()` | 返回 `INSERT` 数据来源名称 |
| `sqlparser_value_kind_name()` | 返回值类型名称 |
| `sqlparser_bind_kind_name()` | 返回 bind 类型名称 |
| `sqlparser_literal_kind_name()` | 返回字面量类型名称 |
| `sqlparser_selector_kind_name()` | 返回 selector 类型名称 |
| `sqlparser_clause_kind_name()` | 返回子句类型名称 |
| `sqlparser_graph_block_kind_name()` | 返回 query graph block 类型名称 |
| `sqlparser_graph_relation_kind_name()` | 返回 query graph relation 类型名称 |
| `sqlparser_graph_target_kind_name()` | 返回 query graph target 类型名称 |
| `sqlparser_graph_value_kind_name()` | 返回 query graph value 类型名称 |
| `sqlparser_graph_field_match_kind_name()` | 返回 query graph 字段匹配形态名称 |
| `sqlparser_graph_operator_kind_name()` | 返回 query graph 操作符分类名称 |
| `sqlparser_graph_set_kind_name()` | 返回 query graph set 类型名称 |
| `sqlparser_graph_dml_kind_name()` | 返回 query graph DML 类型名称 |
| `sqlparser_graph_predicate_kind_name()` | 返回 query graph predicate 类型名称 |
| `sqlparser_graph_predicate_bool_name()` | 返回 query graph boolean predicate 连接类型名称 |
| `sqlparser_graph_operator_is_like_pattern()` | 判断操作符分类是否为 `LIKE`、`NOT LIKE`、`ILIKE` 或 `NOT ILIKE` |
| `sqlparser_graph_value_is_like_pattern()` | 判断 query graph value 是否为 pattern-match 的 pattern 值 |
| `sqlparser_dialect_name()` | 返回方言名称 |
| `sqlparser_bool_operator_name()` | 返回布尔连接符名称 |

## 解析与句柄管理

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_limits_default()` | 填充默认资源限制 |
| `sqlparser_parse_options_default()` | 填充默认解析选项 |
| `sqlparser_parse()` | 使用默认选项解析 SQL |
| `sqlparser_parse_with_limits()` | 使用指定资源限制解析 SQL |
| `sqlparser_parse_with_options()` | 使用指定方言和资源限制解析 SQL |
| `sqlparser_handle_destroy()` | 释放 handle |
| `sqlparser_original_sql()` | 返回原始输入 SQL |
| `sqlparser_handle_dialect()` | 返回 handle 使用的方言 |
| `sqlparser_statement_count()` | 返回语句数量 |

## 语句级访问

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_kind()` | 返回指定语句的逻辑类型 |
| `sqlparser_statement_node_name()` | 返回底层节点名称 |
| `sqlparser_statement_target_relation()` | 返回语句主目标对象 |

控制流中的条件表达式和分支 SQL 都是可寻址 statement unit。条件 unit 的类型为 `SQLPARSER_STATEMENT_KIND_CONDITION`，节点名称为 `ConditionExpr`；分支 SQL 保持自身语句类型。现有 `stmt[n]...` selector 可直接读取和修改这些 unit。

## 控制流只读遍历

`sqlparser_handle_control_flow()` 返回控制流的只读拓扑。普通 SQL 返回成功和空 view；包含控制语句的 handle 通过 roots、nodes、branches 和 items 表达有序结构。

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_handle_control_flow()` | 获取 handle 的控制流 view |
| `sqlparser_control_span_index_at()` | 从控制流 span 读取第 N 个索引 |
| `sqlparser_control_node_at()` | 读取控制节点 |
| `sqlparser_control_branch_at()` | 读取有条件或无条件分支 |
| `sqlparser_control_item_at()` | 读取分支中的 statement 或嵌套节点引用 |

| 结构 | 说明 |
| --- | --- |
| `sqlparser_control_flow_view_t` | roots span、节点/分支/条目数量及 generation |
| `sqlparser_control_node_t` | 节点类型和有序 branch span；当前节点类型为 `SQLPARSER_CONTROL_NODE_IF` |
| `sqlparser_control_branch_t` | 可选条件 statement 索引和有序 item span |
| `sqlparser_control_item_t` | `SQLPARSER_CONTROL_ITEM_STATEMENT` 或 `SQLPARSER_CONTROL_ITEM_NODE` 引用 |

roots、`node.branches` 和 `branch.items` 都是索引池 span，必须通过 `sqlparser_control_span_index_at()` 读取，不能把 `offset` 直接当作对象索引。view 借用 handle 内存；handle 修改后 generation 变化，旧 view 失效。

```c
sqlparser_control_flow_view_t flow;
sqlparser_control_item_t root;
size_t root_item_index;

sqlparser_handle_control_flow(handle, &flow, &err);
sqlparser_control_span_index_at(
    &flow, flow.roots, 0, &root_item_index, &err);
sqlparser_control_item_at(&flow, root_item_index, &root, &err);
```

## 通用读取与细粒度改写

### Relation

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_relation_count()` | 返回 relation 数量 |
| `sqlparser_statement_relation()` | 读取指定 relation |
| `sqlparser_statement_set_relation_name()` | 改写指定 relation 的 schema 或 table 名称 |

### Name

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_name_count()` | 返回名称原子数量 |
| `sqlparser_statement_name()` | 读取指定名称原子 |
| `sqlparser_statement_set_name()` | 改写指定名称原子 |

### Literal

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_literal_count()` | 返回 literal 数量 |
| `sqlparser_statement_literal()` | 读取指定 literal |
| `sqlparser_statement_set_literal()` | 改写指定 literal |

`sqlparser_literal_view_t.quoted_identifier` 为 `1` 时，表示字符串 literal 来源于带引号标识符 token，例如 `ALTER SESSION SET CURRENT_SCHEMA="KdesMixed"` 中的 schema 值。普通字符串字面量和未加引号标识符该字段为 `0`。

### INSERT

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_insert_source_kind()` | 返回 `INSERT` 数据来源 |
| `sqlparser_insert_column_count()` | 返回目标列数量 |
| `sqlparser_insert_column_name()` | 读取目标列名称 |
| `sqlparser_insert_row_count()` | 返回 `VALUES` 行数 |
| `sqlparser_insert_cell_literal()` | 读取指定单元格字面量 |
| `sqlparser_insert_set_cell_literal()` | 改写指定单元格字面量 |
| `sqlparser_insert_cell_sql()` | 读取指定单元格右值 SQL |
| `sqlparser_insert_set_cell_sql()` | 改写指定单元格右值 SQL |

### SELECT 输出列表

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_select_target_list_count()` | 返回语句中的 SELECT target list 数量 |
| `sqlparser_select_target_count()` | 返回指定 target list 的输出项数量 |
| `sqlparser_select_target_sql()` | 读取指定输出项 SQL |
| `sqlparser_select_set_target_sql()` | 替换指定输出项 SQL |
| `sqlparser_select_set_targets_sql()` | 替换整个 SELECT 输出列表 |
| `sqlparser_select_insert_target_sql()` | 在 SELECT 输出列表中插入输出项 |
| `sqlparser_select_delete_target()` | 删除 SELECT 输出项 |

`target_list_index` 用于区分同一语句中的多个 `SelectStmt`，例如子查询、CTE 或集合运算分支。

### UPDATE 与 WHERE

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_update_assignment_count()` | 返回 `SET` 赋值项数量 |
| `sqlparser_update_assignment()` | 读取指定赋值项 |
| `sqlparser_update_set_assignment_literal()` | 将赋值项右值 literal 或 bind 改写为 literal |
| `sqlparser_update_assignment_sql()` | 读取赋值项右值 SQL |
| `sqlparser_update_set_assignment_sql()` | 改写赋值项右值 SQL |
| `sqlparser_update_insert_assignment_sql()` | 插入完整 `SET` 赋值项 |
| `sqlparser_update_delete_assignment()` | 删除指定 `SET` 赋值项 |
| `sqlparser_update_set_assignment_full_sql()` | 整项替换指定 `SET` 赋值项 |
| `sqlparser_statement_where_literal_count()` | 返回 WHERE literal 数量 |
| `sqlparser_statement_where_literal()` | 读取指定 WHERE literal |
| `sqlparser_statement_where_set_literal()` | 改写指定 WHERE literal |
| `sqlparser_statement_where_count()` | 返回可写 WHERE 槽位数量 |
| `sqlparser_statement_where_sql()` | 读取 WHERE 条件 SQL |
| `sqlparser_statement_set_where_sql()` | 设置或替换 WHERE 条件 SQL |
| `sqlparser_statement_append_where_sql()` | 按 `AND` 或 `OR` 追加 WHERE 条件 |

### 通用子句

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_clause_count()` | 返回可写 statement 级子句数量 |
| `sqlparser_statement_clause()` | 读取指定子句视图 |
| `sqlparser_statement_clause_sql()` | 读取指定子句 SQL |
| `sqlparser_statement_set_clause_sql()` | 设置或替换指定子句 SQL |
| `sqlparser_statement_append_clause_condition()` | 按 `AND` 或 `OR` 向 `where` 子句追加条件 |
| `sqlparser_clause_sql()` | 通过 `sqlparser_clause_view_t` 渲染子句 SQL |

通用子句接口只负责结构级改写；字段、值和来源关系通过 `query_graph` 读取。

## Selector 接口

selector 用于把可读或可写对象表示为稳定文本路径或结构体。

常见 selector：

```text
stmt[0].relation[0]
stmt[0].name[3]
stmt[0].value[4]
stmt[0].literal[1]
stmt[0].where_literal[0]
stmt[0].clause[0]
stmt[0].assignment[0]
stmt[0].insert_cell[1][2]
stmt[0].insert_branch_columns[0]
stmt[0].insert_branch_condition[0]
stmt[0].select_targets[0]
stmt[0].select_target[0][1]
stmt[0].dml_result_targets[0][0]
stmt[0].dml_result_target[0][0][1]
stmt[0].dml_result_sink[0][0]
stmt[0].dml_result_sink_columns[0][0]
stmt[0].dml_result_sink_column[0][0][1]
```

### selector 解析与读取

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_selector_parse()` | 文本转 `sqlparser_selector_t` |
| `sqlparser_selector_format()` | `sqlparser_selector_t` 转文本 |
| `sqlparser_selector_relation()` | 通过 selector 读取 relation |
| `sqlparser_selector_name()` | 通过 selector 读取 name |
| `sqlparser_selector_literal()` | 通过 selector 读取 literal |
| `sqlparser_selector_where_literal()` | 通过 selector 读取 WHERE literal |
| `sqlparser_selector_where_sql()` | 通过 selector 读取 WHERE 条件 SQL |
| `sqlparser_selector_clause()` | 通过 selector 读取通用子句视图 |
| `sqlparser_selector_clause_sql()` | 通过 selector 读取通用子句 SQL；也可读取 Oracle/Dameng `INSERT ALL/FIRST` branch condition SQL |
| `sqlparser_selector_update_assignment()` | 通过 selector 读取 assignment |
| `sqlparser_selector_update_assignment_sql()` | 通过 selector 读取 assignment 右值 SQL |
| `sqlparser_selector_insert_cell_literal()` | 通过 selector 读取 INSERT cell literal |
| `sqlparser_selector_insert_cell_sql()` | 通过 selector 读取 INSERT 单元格右值 SQL |
| `sqlparser_selector_select_target_sql()` | 通过 selector 读取 SELECT 输出项 SQL |

### selector 改写

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_selector_set_relation_name()` | 改写 relation 名称 |
| `sqlparser_selector_set_name()` | 改写名称原子 |
| `sqlparser_selector_set_literal()` | 改写 literal |
| `sqlparser_selector_set_where_literal()` | 改写 WHERE literal |
| `sqlparser_selector_set_where_sql()` | 设置或替换 WHERE 条件 |
| `sqlparser_selector_append_where_sql()` | 向 WHERE 追加条件 |
| `sqlparser_selector_set_clause_sql()` | 设置或替换通用子句 |
| `sqlparser_selector_append_clause_condition()` | 向 `where` 类型子句追加条件 |
| `sqlparser_selector_set_update_assignment_literal()` | 将 assignment 右值 literal 或 bind 改写为 literal |
| `sqlparser_selector_set_update_assignment_sql()` | 改写 assignment 右值 SQL |
| `sqlparser_selector_insert_update_assignment_sql()` | 插入完整 `SET` 赋值项 |
| `sqlparser_selector_insert_update_assignment_from_assignment_value()` | 用结构化目标列和已有 assignment 右值克隆插入 `SET` 赋值项 |
| `sqlparser_selector_delete_update_assignment()` | 删除 `SET` 赋值项 |
| `sqlparser_selector_set_update_assignment_full_sql()` | 整项替换 `SET` 赋值项 |
| `sqlparser_selector_set_insert_cell_literal()` | 改写 INSERT 单元格 literal |
| `sqlparser_selector_set_insert_cell_sql()` | 改写 INSERT 单元格右值 SQL |
| `sqlparser_selector_set_select_target_sql()` | 改写 SELECT 单个输出项 |
| `sqlparser_selector_set_select_targets_sql()` | 改写 SELECT 整个输出列表 |
| `sqlparser_selector_replace_select_target_with_columns()` | 用结构化列列表替换一个 SELECT 输出项 |

### 结构化 SQL 片段改写

结构化改写接口接收 selector 和 `sqlparser_identifier_path_view_t`，直接构造或克隆 AST 节点。调用方只提供标识符分段和源 selector，不需要拼接 SQL 片段，也不需要传入 quote 字符。方言由 `sqlparser_handle_t` 决定。

`sqlparser_selector_insert_update_assignment_from_assignment_value()` 用于插入新的 `UPDATE SET` 赋值项。函数会克隆 `source_assignment_selector` 指向的 assignment 右值，并以 `target` 作为新 assignment 左侧：

```c
const char *backup_parts[] = {"phone_backup"};
sqlparser_identifier_path_view_t target;
sqlparser_selector_t insert_selector;
sqlparser_selector_t source_selector;

target.parts = backup_parts;
target.part_count = 1;

sqlparser_selector_parse("stmt[0].assignment[0]", &insert_selector, &err);
sqlparser_selector_parse("stmt[0].assignment[0]", &source_selector, &err);
sqlparser_selector_insert_update_assignment_from_assignment_value(
    handle,
    &insert_selector,
    &target,
    &source_selector,
    &err);
```

`sqlparser_update_set_assignment_literal()` 和 `sqlparser_selector_set_update_assignment_literal()` 只替换 assignment 右值并保留左侧目标列。原右值为 literal 或 bind 时可替换为 `sqlparser_literal_value_t`；原右值为函数、运算表达式、字段引用、`DEFAULT` 或子查询时返回 `SQLPARSER_STATUS_UNSUPPORTED`。

`sqlparser_selector_replace_select_target_with_columns()` 用于把一个 SELECT 输出项替换为多个结构化列 target，常用于将 `*` 或 `alias.*` 展开为调用方已经计算好的列列表：

```c
const char *id_parts[] = {"u", "id"};
const char *name_parts[] = {"u", "name"};
sqlparser_identifier_path_view_t columns[2];
sqlparser_selector_t target_selector;

columns[0].parts = id_parts;
columns[0].part_count = 2;
columns[1].parts = name_parts;
columns[1].part_count = 2;

sqlparser_selector_parse("stmt[0].select_target[0][0]", &target_selector, &err);
sqlparser_selector_replace_select_target_with_columns(
    handle,
    &target_selector,
    columns,
    2,
    &err);
```

两个接口的输入数组均为 borrowed view，库不会在 handle 中保存调用方指针。失败时返回错误状态，并保持原 handle 不变。

## query_graph C 结构化遍历

`query_graph` 提供查询块、关系、输出项、字段引用、条件、DML 写入和值绑定的结构化访问。

### 获取入口

```c
sqlparser_status_t sqlparser_statement_query_graph(
    const sqlparser_handle_t *handle,
    size_t statement_index,
    sqlparser_query_graph_view_t *out_graph,
    sqlparser_error_t *out_error);
```

`sqlparser_query_graph_view_t` 包含当前 statement 的计数和根 block 信息。view 不拥有内存，生命周期与 handle 和当前 generation 一致。

### 读取函数

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_query_graph_span_index_at()` | 从 span 中读取第 N 个全局索引 |
| `sqlparser_query_graph_block_at()` | 读取 query block |
| `sqlparser_query_graph_relation_at()` | 读取 relation |
| `sqlparser_query_graph_target_at()` | 读取 SELECT target |
| `sqlparser_query_graph_field_at()` | 读取字段 occurrence |
| `sqlparser_query_graph_value_at()` | 读取字段关联值 |
| `sqlparser_query_graph_set_at()` | 读取集合运算节点 |
| `sqlparser_query_graph_predicate_at()` | 读取 WHERE/ON/HAVING 谓词节点 |
| `sqlparser_query_graph_dml()` | 读取根 DML 写入结构 |
| `sqlparser_query_graph_dml_count()` | 读取当前 statement 的 DML 数量 |
| `sqlparser_query_graph_dml_at()` | 按 0 基索引读取 DML |
| `sqlparser_query_graph_dml_parent()` | 读取嵌套 DML 的父 DML 索引 |
| `sqlparser_query_graph_dml_result_count()` | 读取指定 DML 的结果通道数量 |
| `sqlparser_query_graph_dml_result_at()` | 读取指定 DML 的结果通道 |
| `sqlparser_query_graph_dml_reference_at()` | 读取结果通道中的字段来源关系 |
| `sqlparser_query_graph_dml_branch_at()` | 读取 Oracle/Dameng multi-table INSERT 分支 |
| `sqlparser_query_graph_dml_column_at()` | 读取 INSERT 目标列 |
| `sqlparser_query_graph_dml_cell_at()` | 读取 INSERT VALUES 单元格 |
| `sqlparser_query_graph_dml_assignment_at()` | 读取 UPDATE/MERGE 赋值项 |

### 主要结构

| 结构体 | 说明 |
| --- | --- |
| `sqlparser_graph_block_t` | 查询块，持有 relation、target 和 predicate span |
| `sqlparser_graph_relation_t` | SQL 中出现的 base、derived、cte 或 dual relation |
| `sqlparser_graph_target_t` | SELECT 输出项，包含输出顺序、`*` 来源和 selector |
| `sqlparser_graph_field_t` | SQL 中出现的字段 occurrence |
| `sqlparser_graph_value_t` | 与字段关联的 literal、bind、default 或 expression 值 |
| `sqlparser_graph_set_t` | `UNION`、`UNION ALL`、`INTERSECT`、`EXCEPT/MINUS` 分支关系 |
| `sqlparser_graph_predicate_t` | WHERE、ON、HAVING 中的比较、组合、EXISTS 或表达式谓词 |
| `sqlparser_graph_dml_t` | INSERT、UPDATE、DELETE、MERGE 写入结构 |
| `sqlparser_graph_dml_result_t` | DML 结果通道、输出 block、可选 sink relation、sink columns 和字段来源 span |
| `sqlparser_graph_dml_reference_t` | 一个结果 target 对目标行或来源 relation 的字段引用 |
| `sqlparser_graph_dml_branch_t` | Oracle/Dameng multi-table INSERT 的单个 INTO 分支 |
| `sqlparser_graph_dml_column_t` | INSERT 显式目标列 |
| `sqlparser_graph_dml_cell_t` | INSERT VALUES 单元格；Oracle/Dameng multi-table INSERT 中可通过 `source_target_index` 关联末尾 source query 输出项 |
| `sqlparser_graph_dml_assignment_t` | UPDATE/MERGE 赋值项 |

### 归属规则

- `sqlparser_graph_relation_t.link_name` 表达远程对象引用中的 database link；SQL 未出现时为 `NULL`。
- `relations[].source_block_index` 表达派生表或 CTE 来源。
- 同一个 CTE 定义只构建一个来源 block；多次引用共享该 `source_block_index`，未被引用的 CTE 定义仍保留在 graph 中。
- `targets[].star_relations` 表达 `*` 或 `alias.*` 覆盖的 relation。
- `targets[].source_block_index` 表达星号或子查询 target 的来源 block。
- `sets[].branch_blocks` 表达集合运算左右分支。
- `predicates[]` 表达 `WHERE`、`ON`、`HAVING` 中的谓词树；`children` span 表达 `AND`、`OR`、`NOT` 的子谓词。
- `field = literal/bind` 谓词通过 `left_field_index + value_index` 表达；`field = field` 谓词通过 `left_field_index + right_field_index` 表达，并在 `values[]` 中以 `SQLPARSER_GRAPH_VALUE_FIELD` 记录右侧来源字段。
- 未限定字段如果不能仅凭 SQL 唯一归属，`has_relation` 为 0，`candidate_relations` 给出当前 scope 候选 relation。
- `sqlparser_graph_dml_t.insert_mode` 区分 `VALUES`、`SELECT`、`INSERT ALL`、`INSERT FIRST`、MySQL `INSERT ... SET` 以及 `REPLACE` 的 `VALUES`、`SELECT`、`SET` 形态。
- `sqlparser_query_graph_dml_count()` 和 `sqlparser_query_graph_dml_at()` 用于遍历同一 statement 内的多个 DML；`sqlparser_query_graph_dml()` 继续读取根 DML。
- `sqlparser_query_graph_dml_parent()` 表达嵌套 DML 的父子关系；没有父 DML 时 `out_has_parent` 为 0。
- `sqlparser_graph_dml_result_t.kind` 区分 client 和 sink 通道；sink 通道通过 `has_sink_relation`、`sink_relation_index` 和 `sink_columns` 指向写入目标。
- `sqlparser_graph_dml_result_t.references` 中的索引通过 `sqlparser_query_graph_span_index_at()` 读取，再传给 `sqlparser_query_graph_dml_reference_at()`。每个 reference 关联一个结果 target，并标明 `target_before`、`target_after` 或 `source` 来源。
- DML 结果 target 使用 `stmt[S].dml_result_target[D][C][T]` selector；通道 target 列表、sink relation、sink columns 列表和单列分别使用 `dml_result_targets`、`dml_result_sink`、`dml_result_sink_columns` 和 `dml_result_sink_column` selector。
- `sqlparser_graph_dml_t.branches` 仅用于 Oracle/Dameng multi-table INSERT；每个 branch 持有独立 target relation、target columns、rows、branch kind 和可选 condition selector。condition selector 可通过 `sqlparser_selector_clause_sql()` 读取原始条件 SQL。
- Oracle/Dameng multi-table INSERT branch cell 如果直接引用末尾 source query 输出字段，`sqlparser_graph_dml_cell_t.kind` 为 `SQLPARSER_GRAPH_VALUE_FIELD`，并通过 `has_source_target/source_target_index` 指向对应 `targets[]` 项。
- `UPDATE` 和 `MERGE` assignment 的右侧如果是直接字段引用，`sqlparser_graph_dml_assignment_t.value_kind` 为 `SQLPARSER_GRAPH_VALUE_FIELD`，并通过 `has_source_field/source_field_index` 指向来源字段；来源字段可唯一匹配派生 source query 输出项时，同时提供 `has_source_target/source_target_index`。
- `values[]` 只记录与字段或 SELECT target 关联的应用侧值；`LIMIT/OFFSET`、`ROWNUM` 等分页或伪列 bind 不进入 `values[]`。
- `sqlparser_graph_value_t.field_match_kind` 仅在 `has_field` 为真时有效，用于区分 `secret = ?` 这类直接字段匹配和 `UPPER(secret) = ?` 这类表达式字段匹配。
- `sqlparser_graph_value_t.operator_kind` 是基于已归一操作符的结构化分类；调用方判断 pattern-match 语义时应使用 `sqlparser_graph_value_is_like_pattern()` 或枚举值，不需要比较 `operator_name` 字符串。
- 字段侧表达式包含多个字段时，每个可定位字段各输出一条 `expression_field` value 关系。
- 值侧是函数、CAST、运算符、数组、ROW 或 CASE 表达式时，关联字段的 value 使用 `SQLPARSER_GRAPH_VALUE_EXPRESSION`，不会把内部 bind/literal 当作 direct value 暴露。
- `LIKE`、`NOT LIKE`、`ILIKE`、`NOT ILIKE` 带显式 `ESCAPE` 时，pattern 对应的 `sqlparser_graph_value_t.like_escape` 保存 escape 结构；没有显式 `ESCAPE` 时 `kind` 为 `SQLPARSER_GRAPH_LIKE_ESCAPE_NONE`。反解析输出保持公开 SQL 形态，例如 `LIKE pattern ESCAPE escape`。

## JSON 导出与 Patch

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_export_view_json()` | 按需导出 View JSON |
| `sqlparser_apply_patch()` | 应用结构体 patch |

`sqlparser_export_view_json()` 的 `pretty` 参数：

- `0`：紧凑 JSON
- 非 `0`：格式化 JSON

`sqlparser_apply_patch()` 接收 `sqlparser_patch_list_t`。每个 patch 使用 selector 定位可写节点。

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].assignment[0]";
patch.sql = "'carol'";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

支持的 patch 操作：

| 操作 | 说明 |
| --- | --- |
| `SQLPARSER_PATCH_REPLACE` | 替换 relation、name、value、assignment、literal、where_literal、clause、insert_cell、select_target 或 select_targets |
| `SQLPARSER_PATCH_INSERT_COLUMN` | 给 `INSERT ... VALUES` 增加列、给 `INSERT ... SELECT` 增加目标列、给 Oracle/Dameng `INSERT ALL/FIRST` branch 增加目标列，或向 `select_targets` 插入 SELECT 输出项 |
| `SQLPARSER_PATCH_DELETE_COLUMN` | 删除 `INSERT ... VALUES` 列、删除 `INSERT ... SELECT` 目标列，或删除 SELECT 输出项 |
| `SQLPARSER_PATCH_DELETE_ROW` | 删除 `INSERT ... VALUES` 行 |
| `SQLPARSER_PATCH_APPEND_CONDITION` | 按 `AND` 或 `OR` 向 `where` 子句追加条件 |
| `SQLPARSER_PATCH_INSERT_ASSIGNMENT` | 插入 `UPDATE SET` 赋值项 |
| `SQLPARSER_PATCH_DELETE_ASSIGNMENT` | 删除 `UPDATE SET` 赋值项 |
| `SQLPARSER_PATCH_REPLACE_ASSIGNMENT` | 整项替换 `UPDATE SET` 赋值项 |

patch 成功后 handle generation 递增，旧 query graph view 失效。

`sqlparser_patch_t` 的值来源字段互斥：`sql`、`default_sql`、`source_selector`、`literal`、`bind` 中同一位置只能提供一种。`source_selector` 支持克隆已有 `insert_cell`、`select_target` 或 `assignment` 的 SQL 片段；`literal` 和 `bind` 由库按当前方言渲染，调用方不需要拼接占位符文本。

## Deparse 与字符串释放

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_deparse()` | 反解析当前 AST，生成 SQL 字符串 |
| `sqlparser_string_free()` | 释放库返回的字符串 |

未被改写的 schema、表名、列名和别名按输入 SQL 中的标识符拼写输出，包括未加引号标识符的原始大小写。关键字、空白和可选语法形式仍由反解析器规范化。

## 常见使用模式

### 字段和值归属

1. 调用 `sqlparser_statement_query_graph()`。
2. 遍历 `relations`、`fields`、`values` 和 DML 结构。
3. 根据结构化字段执行调用方规则。

### selector 驱动改写

1. 通过 query graph 或 View JSON 获取 selector。
2. 构造 `sqlparser_patch_t`。
3. 调用 `sqlparser_apply_patch()`。
4. 调用 `sqlparser_deparse()`。
5. 使用同一方言重新解析生成 SQL，验证语法有效。

### SELECT 输出列表改写

1. 使用 `query_graph.targets[]` 找到目标输出项或 `target_list_selector`。
2. 使用 `sqlparser_apply_patch()` 或 SELECT target 接口增删改输出项。
3. 反解析并重新解析验证。

## 相关示例

| 示例 | 说明 |
| --- | --- |
| `examples/patch/08_view_patch.c` | View JSON 导出、patch、回放 |
| `examples/patch/13_select_target_patch.c` | 通过 patch 展开 `SELECT *`、插入输出列和删除输出列 |
| `examples/patch/14_where_patch.c` | 通过 patch 新增 WHERE 并追加条件 |
| `examples/patch/15_insert_columns_patch.c` | 通过 patch 增加和删除 `INSERT ... VALUES` 字段 |
| `examples/patch/16_clause_patch.c` | 通过通用 clause patch 改写 SELECT 输出列表、WHERE 和 ORDER BY |
| `examples/patch/17_update_set_patch.c` | 通过 patch 追加、删除和整项替换 `UPDATE SET` 赋值项 |
| `examples/patch/19_oracle_multi_insert_patch.c` | 通过 patch 改写 Oracle `INSERT ALL` 分支列和值 |
| `examples/convenience/18_structured_fragment_rewrite.c` | 结构化 UPDATE assignment 插入和 SELECT `*` 展开 |
| `examples/inspect/01_select_inspect.c` | SELECT 读取与多表关联信息 |
| `examples/inspect/03_insert_select_inspect.c` | `INSERT ... SELECT` 结构读取 |
| `examples/inspect/07_multi_statement_walk.c` | 多语句输入遍历 |
| `examples/dialect/10_mysql_dialect.c` | MySQL 方言解析与 patch 改写 |
| `examples/dialect/11_oracle_dialect.c` | Oracle 方言解析与改写 |
| `examples/dialect/12_sqlserver_dialect.c` | SQL Server `IF ... ELSE` 控制流、DML 返回通道与反解析 |
| `examples/dialect/17_dameng_dialect.c` | 达梦方言解析与反解析 |
| `examples/dialect/20_vastbase_dialect.c` | Vastbase 兼容模式解析与反解析 |
