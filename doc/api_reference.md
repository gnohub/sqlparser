# API 手册

本文档说明 `sqlparser` 公共 C API 的主要类型、生命周期规则、错误处理方式和函数分组。

## 概述

`sqlparser` 以 `sqlparser_handle_t` 为中心提供能力。标准调用流程如下：

1. 调用 `sqlparser_parse()` 解析 SQL，创建 `handle`
2. 通过语句级接口、通用原子接口或 selector 接口读取结构信息
3. 调用对应的改写接口修改表名、名称原子、字面量或右值表达式
4. 根据需要导出 SQL View JSON 或遍历 C 结构视图
5. 调用 `sqlparser_deparse()` 生成改写后的 SQL
6. 调用 `sqlparser_handle_destroy()` 释放 `handle`

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

错误信息通过 `sqlparser_error_t` 返回：

- `code`：状态码
- `cursor`：字符位置
- `line`：行号
- `column`：列号
- `message`：错误消息

## 核心数据类型

### 句柄

- `sqlparser_handle_t`

`sqlparser_handle_t` 表示一次解析后的长生命周期对象，内部保存原始 SQL、当前语法树以及按需生成的派生结果。

### 常用枚举

| 枚举类型 | 说明 |
| --- | --- |
| `sqlparser_statement_kind_t` | 语句类型 |
| `sqlparser_insert_source_kind_t` | `INSERT` 数据来源 |
| `sqlparser_value_kind_t` | 值类型 |
| `sqlparser_literal_kind_t` | 字面量类型 |
| `sqlparser_selector_kind_t` | selector 类型 |
| `sqlparser_dialect_t` | SQL 方言类型 |

### 资源限制

- `sqlparser_limits_t`

`sqlparser_limits_t` 用于限制单次解析和输出所允许的资源规模：

默认限制为：SQL 输入 4MB、生成输出 4MB、单次解析 64 条语句。

| 字段 | 说明 |
| --- | --- |
| `struct_size` | 结构体大小，由 `sqlparser_limits_default()` 填充 |
| `max_sql_bytes` | SQL 输入和表达式 SQL 片段最大字节数 |
| `max_output_bytes` | 生成 SQL 或 JSON 输出最大字节数 |
| `max_statement_count` | 单次解析允许的最大语句数量 |

使用 `sqlparser_limits_default()` 可获得默认限制。调用方只需要覆盖需要调整的字段，字段为 `0` 时按默认值处理。

### 解析选项

- `sqlparser_parse_options_t`

`sqlparser_parse_options_t` 用于配置解析行为：

| 字段 | 说明 |
| --- | --- |
| `struct_size` | 结构体大小，由 `sqlparser_parse_options_default()` 填充 |
| `dialect` | SQL 方言，不设置时默认为 `SQLPARSER_DIALECT_POSTGRESQL` |
| `limits` | 资源限制 |
| `flags` | 保留字段，当前应保持为 `0` |

已定义方言：

| 方言 | 说明 |
| --- | --- |
| `SQLPARSER_DIALECT_POSTGRESQL` | 默认方言，保持原有解析行为 |
| `SQLPARSER_DIALECT_MYSQL` | MySQL 方言转换层，支持可安全映射到现有 AST 的语法 |
| `SQLPARSER_DIALECT_ORACLE` | Oracle 方言转换层，支持可安全映射到现有 AST 的常用 SQL 子集 |
| `SQLPARSER_DIALECT_SQLSERVER` | SQL Server 方言转换层，支持可安全映射到现有 AST 的常用 T-SQL 子集 |
| `SQLPARSER_DIALECT_DAMENG` | 达梦方言转换层，支持可安全映射到现有 AST 的 DM_SQL 子集 |

### 视图结构

| 结构体 | 说明 |
| --- | --- |
| `sqlparser_relation_view_t` | 表、schema、别名视图 |
| `sqlparser_name_view_t` | 名称原子视图 |
| `sqlparser_literal_view_t` | 字面量只读视图 |
| `sqlparser_literal_value_t` | 写入用字面量值 |
| `sqlparser_assignment_view_t` | `UPDATE SET` 赋值项视图 |
| `sqlparser_where_literal_view_t` | `WHERE` 字面量视图 |
| `sqlparser_selector_t` | 稳定 selector 结构体 |

## 生命周期与线程模型

### 内存所有权

- `sqlparser_parse()` 返回的 `handle` 由 `sqlparser_handle_destroy()` 释放
- `sqlparser_deparse()` 和 JSON 导出函数返回的字符串由 `sqlparser_string_free()` 释放
- 各类 `view` 中的字符串均为 borrowed pointer
- borrowed pointer 不得由调用方释放

### 索引规则

所有索引均为 0 基：

- `statement_index`
- `relation_index`
- `name_index`
- `literal_index`
- `assignment_index`
- `row_index`
- `column_index`

### 改写后的使用规则

成功改写后，重新获取仍需使用的视图或导出结果。调用方不应继续使用改写前取得的 borrowed pointer。

### 线程行为

- 同一个 `handle` 不支持并发读写
- 同一个 `handle` 不保证多线程只读并发安全
- 使用方式为一个线程独占一个 `handle`

## 版本与名称辅助函数

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_version_string()` | 返回库版本字符串 |
| `sqlparser_libpg_query_tag()` | 返回仓库内固定 `libpg_query` 版本 tag |
| `sqlparser_statement_kind_name()` | 返回语句类型名称 |
| `sqlparser_insert_source_kind_name()` | 返回 `INSERT` 数据来源名称 |
| `sqlparser_value_kind_name()` | 返回值类型名称 |
| `sqlparser_literal_kind_name()` | 返回字面量类型名称 |
| `sqlparser_selector_kind_name()` | 返回 selector 类型名称 |
| `sqlparser_dialect_name()` | 返回方言名称 |
| `sqlparser_bool_operator_name()` | 返回布尔连接符名称 |

## 解析与句柄管理

### `sqlparser_parse`

函数原型：

```c
sqlparser_status_t sqlparser_parse(
    const char *sql,
    sqlparser_handle_t **out_handle,
    sqlparser_error_t *out_error);
```

参数：

- `sql`
  要解析的 SQL 字符串。
- `out_handle`
  输出参数。成功时返回新创建的句柄。
- `out_error`
  输出参数。失败时写入错误信息。

返回值：

- 成功时返回 `SQLPARSER_STATUS_OK`
- 解析失败时返回 `SQLPARSER_STATUS_PARSE_ERROR`
- 参数错误时返回 `SQLPARSER_STATUS_INVALID_ARGUMENT`

说明：

- 输入 SQL 不能为空字符串。
- 调用成功后，调用方负责释放 `handle`。
- `sqlparser_parse()` 使用默认资源限制。

### `sqlparser_parse_with_limits`

函数原型：

```c
sqlparser_status_t sqlparser_parse_with_limits(
    const char *sql,
    const sqlparser_limits_t *limits,
    sqlparser_handle_t **out_handle,
    sqlparser_error_t *out_error);
```

说明：

- 与 `sqlparser_parse()` 相同，但允许调用方传入资源限制。
- `limits` 为 `NULL` 时使用默认限制。
- 解析成功后，限制配置会随 `handle` 保存，并影响改写、导出和反解析。
- 超出限制时返回 `SQLPARSER_STATUS_RESOURCE_LIMIT`。

### `sqlparser_parse_with_options`

函数原型：

```c
sqlparser_status_t sqlparser_parse_with_options(
    const char *sql,
    const sqlparser_parse_options_t *options,
    sqlparser_handle_t **out_handle,
    sqlparser_error_t *out_error);
```

说明：

- 与 `sqlparser_parse()` 相同，但允许传入方言和资源限制。
- `options` 为 `NULL` 时使用默认选项，即 PostgreSQL 方言和默认资源限制。
- MySQL、Oracle、SQL Server、达梦方言先转换为解析内核可接受的 SQL，再进入统一 AST 链路。
- 方言语法无法安全映射到当前 AST 时返回 `SQLPARSER_STATUS_UNSUPPORTED`。

### `sqlparser_handle_destroy`

函数原型：

```c
void sqlparser_handle_destroy(sqlparser_handle_t *handle);
```

说明：

- 释放句柄及其派生缓存。
- 允许传入 `NULL`。

### 相关函数

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_original_sql()` | 返回原始输入 SQL |
| `sqlparser_handle_dialect()` | 返回 `handle` 使用的方言 |
| `sqlparser_statement_count()` | 返回语句数量 |

## 语句级访问

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_kind()` | 返回指定语句的逻辑类型 |
| `sqlparser_statement_node_name()` | 返回底层节点名称 |
| `sqlparser_statement_target_relation()` | 返回语句主目标对象 |

说明：

- `statement_kind` 适合做语句分类。
- `node_name` 适合做更细粒度的节点判断。
- `statement_target_relation` 适合获取具有明确主目标对象的语句。

## Relation 接口

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_relation_count()` | 返回 relation 数量 |
| `sqlparser_statement_relation()` | 读取指定 relation |
| `sqlparser_statement_set_relation_name()` | 改写指定 relation 的 schema 或 table 名称 |

说明：

- relation 接口适用于目标表、关联表和 DDL 对象名读取。
- 改写 relation 后，可调用 `sqlparser_deparse()` 生成新 SQL。

## Name 接口

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_name_count()` | 返回名称原子数量 |
| `sqlparser_statement_name()` | 读取指定名称原子 |
| `sqlparser_statement_set_name()` | 改写指定名称原子 |

说明：

- `name` 表示 AST 中的非 literal 字符串原子。
- 调用方通常需要结合 `owner_type`、`field_name` 和 `value` 做过滤。
- 该接口可覆盖表名、列名、别名和 DDL 对象名等元素。

## Literal 接口

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_literal_count()` | 返回 literal 数量 |
| `sqlparser_statement_literal()` | 读取指定 literal |
| `sqlparser_statement_set_literal()` | 改写指定 literal |

说明：

- literal 接口适合做通用字面量遍历和快速替换。
- 写入时使用 `sqlparser_literal_value_t` 指定目标类型和值。

## INSERT 接口

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

说明：

- `INSERT ... VALUES` 可通过行列坐标访问。
- `INSERT ... SELECT` 不提供固定单元格模型，`row_count` 通常为 `0`。
- 按列名定位时，先遍历目标列，再由调用方建立列名到 `column_index` 的映射。
- `sqlparser_insert_cell_sql()` 可用于读取 `DEFAULT`、函数调用和其他表达式形态。
- `sqlparser_insert_set_cell_sql()` 适用于需要保留单元格语义位置、但要替换为任意右值表达式的场景。

## SELECT 列表接口

SELECT target list 表示 `SELECT` 后面的输出列表。该接口直接作用在通用 AST 的 `SelectStmt.target_list` 上，可用于替换 `*`、增加输出列、删除输出列或替换单个输出表达式。

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_select_target_list_count()` | 返回语句中的 SELECT target list 数量 |
| `sqlparser_select_target_count()` | 返回指定 target list 的输出项数量 |
| `sqlparser_select_target_sql()` | 读取指定输出项 SQL |
| `sqlparser_select_set_target_sql()` | 替换指定输出项 SQL |
| `sqlparser_select_set_targets_sql()` | 替换整个 SELECT 输出列表 |
| `sqlparser_select_insert_target_sql()` | 在 SELECT 输出列表中插入一个输出项 |
| `sqlparser_select_delete_target()` | 删除 SELECT 输出列表中的一个输出项 |

说明：

- `target_list_index` 用于区分同一语句中的多个 `SelectStmt`，例如子查询、CTE 或集合运算分支。
- `target_index` 是指定 target list 内部的 0 基输出项索引。
- `sqlparser_select_set_targets_sql()` 接收逗号分隔的输出列表，例如 `"id, name, upper(name) AS label"`。
- 方言模式下，输入 SQL 片段会先经过对应方言 hook，再写入 AST。
- 改写后可通过 `sqlparser_deparse()` 输出 SQL；需要验证生成 SQL 时，使用同一方言再次调用 `sqlparser_parse_with_options()`。

## UPDATE 与 WHERE 接口

### UPDATE assignment

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_update_assignment_count()` | 返回 `SET` 赋值项数量 |
| `sqlparser_update_assignment()` | 读取指定赋值项 |
| `sqlparser_update_set_assignment_literal()` | 改写赋值项右值 literal |
| `sqlparser_update_assignment_sql()` | 读取赋值项右值 SQL |
| `sqlparser_update_set_assignment_sql()` | 改写赋值项右值 SQL |

### WHERE literal

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_where_literal_count()` | 返回 WHERE literal 数量 |
| `sqlparser_statement_where_literal()` | 读取指定 WHERE literal |
| `sqlparser_statement_where_set_literal()` | 改写指定 WHERE literal |

### WHERE 条件表达式

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_where_count()` | 返回语句中的可写 `WHERE` 槽位数量 |
| `sqlparser_statement_where_sql()` | 读取指定 `WHERE` 条件 SQL；槽位为空时返回 `NULL` |
| `sqlparser_statement_set_where_sql()` | 设置或替换指定 `WHERE` 条件 SQL |
| `sqlparser_statement_append_where_sql()` | 按 `AND` 或 `OR` 向指定 `WHERE` 追加条件 |

说明：

- `sqlparser_assignment_view_t` 主要用于读取列名、值类型和右值 literal。
- `sqlparser_assignment_view_t.value_kind` 可区分 `literal`、`default` 和 `expression`。
- `sqlparser_update_assignment_sql()` 适用于读取 `DEFAULT` 或任意表达式右值。
- `sqlparser_update_set_assignment_sql()` 适用于把赋值项替换为字面量之外的表达式。
- `sqlparser_where_literal_view_t` 主要用于读取列名、运算符和条件 literal。
- 按列名定位时，先遍历并记录目标索引，再执行改写。
- `where_index` 定位 AST 中真实存在的 `where_clause` 槽位；`INSERT ... VALUES` 不会生成虚拟 WHERE。
- `sqlparser_statement_set_where_sql()` 可用于给 `SELECT`、`UPDATE`、`DELETE`、`INSERT ... SELECT` 以及支持 `WHERE` 的 DDL 或工具语句新增缺失的 WHERE，例如 `CREATE VIEW AS SELECT`、partial index、`COPY FROM`、`CREATE RULE`、`CREATE PUBLICATION` 和排他约束。
- `sqlparser_statement_append_where_sql()` 在目标槽位为空时等价于新增 WHERE；已有 WHERE 时会保留原条件并追加新条件。

### 通用子句接口

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_statement_clause_count()` | 返回语句中的可写 statement 级子句数量 |
| `sqlparser_statement_clause()` | 读取指定子句视图 |
| `sqlparser_statement_clause_sql()` | 读取指定子句 SQL；槽位为空时返回 `NULL` |
| `sqlparser_statement_set_clause_sql()` | 设置或替换指定子句 SQL |
| `sqlparser_statement_append_clause_condition()` | 按 `AND` 或 `OR` 向 `where` 子句追加条件 |

当前支持的 `sqlparser_clause_kind_t`：

| 枚举 | JSON 名称 | 说明 |
| --- | --- | --- |
| `SQLPARSER_CLAUSE_KIND_SELECT_LIST` | `select_list` | SELECT 输出列表 |
| `SQLPARSER_CLAUSE_KIND_WHERE` | `where` | WHERE 条件表达式 |
| `SQLPARSER_CLAUSE_KIND_ORDER_BY` | `order_by` | ORDER BY 排序表达式列表 |
| `SQLPARSER_CLAUSE_KIND_SET_LIST` | `set_list` | UPDATE SET 赋值列表 |

通用子句接口用于结构级改写。字段和值归属仍通过 SQL View 的 `objects[].columns[]` 读取。

## Selector 接口

selector 用于把可读取或可改写对象表示为稳定文本路径或结构体。

支持的 selector 形态包括：

```text
stmt[0].relation[0]
stmt[0].name[3]
stmt[0].value[4]
stmt[0].literal[1]
stmt[0].where_literal[0]
stmt[0].clause[0]
stmt[0].assignment[0]
stmt[0].insert_cell[1][2]
stmt[0].select_targets[0]
stmt[0].select_target[0][1]
```

### selector 解析与格式化

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_selector_parse()` | 文本转 `sqlparser_selector_t` |
| `sqlparser_selector_format()` | `sqlparser_selector_t` 转文本 |

### selector 读取

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_selector_relation()` | 通过 selector 读取 relation |
| `sqlparser_selector_name()` | 通过 selector 读取 name |
| `sqlparser_selector_literal()` | 通过 selector 读取 literal |
| `sqlparser_selector_where_literal()` | 通过 selector 读取 WHERE literal |
| `sqlparser_selector_where_sql()` | 通过 selector 读取 WHERE 条件 SQL |
| `sqlparser_selector_clause()` | 通过 selector 读取通用子句视图 |
| `sqlparser_selector_clause_sql()` | 通过 selector 读取通用子句 SQL |
| `sqlparser_selector_update_assignment()` | 通过 selector 读取 assignment |
| `sqlparser_selector_insert_cell_literal()` | 通过 selector 读取 INSERT cell literal |
| `sqlparser_selector_update_assignment_sql()` | 通过 selector 读取 assignment 右值 SQL |
| `sqlparser_selector_insert_cell_sql()` | 通过 selector 读取 INSERT 单元格右值 SQL |
| `sqlparser_selector_select_target_sql()` | 通过 selector 读取 SELECT 输出项 SQL |

### selector 改写

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_selector_set_relation_name()` | 通过 selector 改表名 |
| `sqlparser_selector_set_name()` | 通过 selector 改名称原子 |
| `sqlparser_selector_set_literal()` | 通过 selector 改通用 literal |
| `sqlparser_selector_set_where_literal()` | 通过 selector 改 WHERE literal |
| `sqlparser_selector_set_where_sql()` | 通过 selector 设置或替换 WHERE 条件 SQL |
| `sqlparser_selector_append_where_sql()` | 通过 selector 向 WHERE 追加条件 |
| `sqlparser_selector_set_clause_sql()` | 通过 selector 设置或替换通用子句 SQL |
| `sqlparser_selector_append_clause_condition()` | 通过 selector 向 `where` 类型子句追加条件 |
| `sqlparser_selector_set_update_assignment_literal()` | 通过 selector 改 assignment 右值 |
| `sqlparser_selector_set_insert_cell_literal()` | 通过 selector 改 INSERT 单元格 |
| `sqlparser_selector_set_update_assignment_sql()` | 通过 selector 改 assignment 右值 SQL |
| `sqlparser_selector_set_insert_cell_sql()` | 通过 selector 改 INSERT 单元格右值 SQL |
| `sqlparser_selector_set_select_target_sql()` | 通过 selector 改 SELECT 单个输出项 SQL |
| `sqlparser_selector_set_select_targets_sql()` | 通过 selector 改 SELECT 整个输出列表 |

说明：

- selector 适合做外部规则定位和结构体 patch 回放。
- 调用方可以保存 selector 文本，并在新的请求中重新解析和执行。

## SQL View 结构化遍历

SQL View 可以通过 C 结构直接遍历，不需要先导出 JSON。视图中的字符串归属于 `handle`，在 `handle` 销毁或发生改写后失效。

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_get_view()` | 获取语句级只读视图 |
| `sqlparser_view_statement_at()` | 读取指定语句 |
| `sqlparser_statement_keyword_at()` | 读取语句关键字 |
| `sqlparser_statement_object_at()` | 读取语句中的表、视图或可归属对象 |
| `sqlparser_object_column_at()` | 读取归属到对象的字段 |
| `sqlparser_column_value_at()` | 读取字段关联的值片段 |
| `sqlparser_object_row_at()` | 读取 `INSERT ... VALUES` 行 |
| `sqlparser_row_cell_at()` | 读取 `INSERT ... VALUES` 单元格 |
| `sqlparser_value_sql()` | 把字段值片段渲染成 SQL |
| `sqlparser_cell_sql()` | 把 `INSERT` 单元格渲染成 SQL |

说明：

- `sqlparser_view_t`、`sqlparser_statement_view_t`、`sqlparser_object_view_t` 等结构不拥有内存。
- `sqlparser_value_sql()` 和 `sqlparser_cell_sql()` 返回新字符串，调用方使用 `sqlparser_string_free()` 释放。
- 字段归属只基于 SQL 中出现的限定名、别名和当前语句对象；不访问数据库元数据，也不做唯一性推断。
- `selector` 可用于后续 patch；没有可写节点时 `has_selector` 为 0。

## JSON 导出与 Patch

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_export_view_json()` | 导出 SQL View JSON |
| `sqlparser_apply_patch()` | 应用结构体 patch |

接入层通常可以把改写流程统一收敛到 `sqlparser_apply_patch()`。更细粒度的 statement / selector 改写函数适合在调用方已经持有具体索引或 selector 结构体时直接调用。

### `pretty` 参数

`sqlparser_export_view_json()` 带 `pretty` 参数：

取值说明：

- `0`：紧凑 JSON
- 非 `0`：格式化 JSON

### `sqlparser_apply_patch`

`sqlparser_apply_patch()` 接收 `sqlparser_patch_list_t`。每个 patch 使用 selector 定位可写节点。

示例：

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

说明：

- `replace` 可替换 relation、name、value、assignment、literal、where_literal、clause、insert_cell、select_target 或 select_targets。
- `insert_column` 可用于 `INSERT ... VALUES` 增加列，也可用于 `select_targets` 插入 SELECT 输出项。
- `delete_column` 可用于 `INSERT ... VALUES` 删除列，也可用于 `select_targets` 删除 SELECT 输出项。
- `delete_row` 用于 `INSERT ... VALUES` 的行删除。
- `append_condition` 可按 `AND` 或 `OR` 向 `where` 类型的 `clause` 追加条件；未设置 `bool_operator` 时默认使用 `AND`。
- `delete_column` 不会删除最后一个单元格，`delete_row` 不会删除最后一行。
- patch 按数组顺序执行；失败时返回错误码和错误信息。

## Deparse 与字符串释放

### `sqlparser_deparse`

函数原型：

```c
sqlparser_status_t sqlparser_deparse(
    const sqlparser_handle_t *handle,
    char **out_sql,
    sqlparser_error_t *out_error);
```

参数：

- `handle`
  要反解析的句柄。
- `out_sql`
  输出参数。成功时返回新生成的 SQL 字符串。
- `out_error`
  输出参数。失败时写入错误信息。

返回值：

- 成功时返回 `SQLPARSER_STATUS_OK`
- 失败时返回相应错误码

### `sqlparser_string_free`

函数原型：

```c
void sqlparser_string_free(char *text);
```

说明：

- 用于释放 `sqlparser_deparse()` 和各类 JSON 导出函数返回的字符串。

## 常见使用模式

### 语句分类

1. 调用 `sqlparser_parse()`
2. 调用 `sqlparser_statement_kind()`
3. 根据需要读取 `sqlparser_statement_node_name()` 或目标 relation

### 通用遍历

1. 调用 relation 接口遍历表对象
2. 调用 name 接口遍历名称原子
3. 调用 literal 接口遍历字面量

### INSERT 按列改值

1. 遍历目标列，定位 `column_index`
2. 确定目标 `row_index`
3. 调用 `sqlparser_insert_set_cell_literal()`
4. 调用 `sqlparser_deparse()`

### UPDATE 同时改写 SET 和 WHERE

1. 遍历 assignment，定位目标列
2. 调用 `sqlparser_update_set_assignment_literal()`
3. 遍历 WHERE literal，定位目标条件
4. 调用 `sqlparser_statement_where_set_literal()`
5. 调用 `sqlparser_deparse()`

### 表达式级改写

1. 调用 `sqlparser_update_assignment_sql()` 或 `sqlparser_insert_cell_sql()` 读取当前右值
2. 调用 `sqlparser_update_set_assignment_sql()` 或 `sqlparser_insert_set_cell_sql()` 写入新表达式
3. 调用 `sqlparser_deparse()`

### SELECT 列表改写

1. 调用 `sqlparser_select_target_count()` 或读取 SQL View 中的 `target_list_selector`
2. 调用 `sqlparser_select_set_targets_sql()` 替换整个输出列表，或调用 `sqlparser_select_insert_target_sql()` / `sqlparser_select_delete_target()` 增删输出项
3. 调用 `sqlparser_deparse()`
4. 需要验证生成 SQL 时，使用同一方言再次调用 `sqlparser_parse_with_options()`

### selector 驱动改写

1. 导出 SQL View JSON 或遍历 C 结构视图
2. 保存 selector 文本
3. 构造 `sqlparser_patch_t`
4. 调用 `sqlparser_apply_patch()`
5. 调用 `sqlparser_deparse()`

## 相关示例

| 示例 | 说明 |
| --- | --- |
| `examples/patch/08_view_patch.c` | SQL View JSON 导出、patch、回放 |
| `examples/patch/13_select_target_patch.c` | 通过 patch 展开 `SELECT *`、插入输出列和删除输出列 |
| `examples/patch/14_where_patch.c` | 通过 patch 新增 WHERE 并追加条件 |
| `examples/patch/15_insert_columns_patch.c` | 通过 patch 增加和删除 `INSERT ... VALUES` 字段 |
| `examples/patch/16_clause_patch.c` | 通过通用 clause patch 改写 SELECT 输出列表、WHERE 和 ORDER BY |
| `examples/convenience/02_insert_values_replace_literal.c` | `INSERT ... VALUES` 字面量替换便捷接口 |
| `examples/convenience/04_update_replace_assignment.c` | UPDATE 赋值与 WHERE 便捷接口 |
| `examples/convenience/05_delete_inspect.c` | DELETE 目标表读取和条件字面量改写 |
| `examples/convenience/06_ddl_inspect.c` | DDL 节点识别、名称遍历和对象名改写 |
| `examples/convenience/09_expression_rewrite.c` | assignment 和 insert cell 表达式级改写 |
| `examples/convenience/13_select_target_rewrite.c` | SELECT 输出列表便捷接口 |
| `examples/convenience/14_where_convenience.c` | WHERE 条件便捷接口 |
| `examples/inspect/01_select_inspect.c` | SELECT 读取与多表关联信息 |
| `examples/inspect/03_insert_select_inspect.c` | `INSERT ... SELECT` 结构读取 |
| `examples/inspect/07_multi_statement_walk.c` | 多语句输入遍历 |
| `examples/dialect/10_mysql_dialect.c` | MySQL 方言解析与 patch 改写 |
| `examples/dialect/11_oracle_dialect.c` | Oracle 方言解析与改写 |
| `examples/dialect/12_sqlserver_dialect.c` | SQL Server 方言解析与反解析 |
| `examples/dialect/17_dameng_dialect.c` | 达梦方言解析与反解析 |
