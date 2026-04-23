# API 手册

本文档说明 `sqlparser` 公共 C API 的主要类型、生命周期规则、错误处理方式和函数分组。

## 概述

`sqlparser` 以 `sqlparser_handle_t` 为中心提供能力。标准调用流程如下：

1. 调用 `sqlparser_parse()` 解析 SQL，创建 `handle`
2. 通过语句级接口、通用原子接口或 selector 接口读取结构信息
3. 调用对应的改写接口修改表名、名称原子、字面量或右值表达式
4. 根据需要导出 `parse tree JSON`、`summary JSON` 或模型 JSON
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

成功改写后，建议重新获取后续所需的视图或导出结果。调用方不应继续使用改写前取得的 borrowed pointer。

### 线程行为

- 同一个 `handle` 不支持并发读写
- 同一个 `handle` 不保证多线程只读并发安全
- 推荐使用方式是一个线程独占一个 `handle`

## 版本与名称辅助函数

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_version_string()` | 返回库版本字符串 |
| `sqlparser_libpg_query_tag()` | 返回仓库内固定 `libpg_query` 版本 tag |
| `sqlparser_model_schema_string()` | 返回当前模型 JSON schema 标识 |
| `sqlparser_statement_kind_name()` | 返回语句类型名称 |
| `sqlparser_insert_source_kind_name()` | 返回 `INSERT` 数据来源名称 |
| `sqlparser_value_kind_name()` | 返回值类型名称 |
| `sqlparser_literal_kind_name()` | 返回字面量类型名称 |
| `sqlparser_selector_kind_name()` | 返回 selector 类型名称 |

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
- 如果需要按列名定位，推荐先遍历目标列，再由调用方建立列名到 `column_index` 的映射。
- `sqlparser_insert_cell_sql()` 可用于读取 `DEFAULT`、函数调用和其他表达式形态。
- `sqlparser_insert_set_cell_sql()` 适用于需要保留单元格语义位置、但要替换为任意右值表达式的场景。

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

说明：

- `sqlparser_assignment_view_t` 主要用于读取列名、值类型和右值 literal。
- `sqlparser_assignment_view_t.value_kind` 可区分 `literal`、`default` 和 `expression`。
- `sqlparser_update_assignment_sql()` 适用于读取 `DEFAULT` 或任意表达式右值。
- `sqlparser_update_set_assignment_sql()` 适用于把赋值项替换为字面量之外的表达式。
- `sqlparser_where_literal_view_t` 主要用于读取列名、运算符和条件 literal。
- 如果需要按列名定位，建议先遍历，再记录目标索引后执行改写。

## Selector 接口

selector 用于把可读取或可改写对象表示为稳定文本路径或结构体。

支持的 selector 形态包括：

```text
stmt[0].relation[0]
stmt[0].name[3]
stmt[0].literal[1]
stmt[0].where_literal[0]
stmt[0].assignment[0]
stmt[0].insert_cell[1][2]
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
| `sqlparser_selector_update_assignment()` | 通过 selector 读取 assignment |
| `sqlparser_selector_insert_cell_literal()` | 通过 selector 读取 INSERT cell literal |
| `sqlparser_selector_update_assignment_sql()` | 通过 selector 读取 assignment 右值 SQL |
| `sqlparser_selector_insert_cell_sql()` | 通过 selector 读取 INSERT 单元格右值 SQL |

### selector 改写

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_selector_set_relation_name()` | 通过 selector 改表名 |
| `sqlparser_selector_set_name()` | 通过 selector 改名称原子 |
| `sqlparser_selector_set_literal()` | 通过 selector 改通用 literal |
| `sqlparser_selector_set_where_literal()` | 通过 selector 改 WHERE literal |
| `sqlparser_selector_set_update_assignment_literal()` | 通过 selector 改 assignment 右值 |
| `sqlparser_selector_set_insert_cell_literal()` | 通过 selector 改 INSERT 单元格 |
| `sqlparser_selector_set_update_assignment_sql()` | 通过 selector 改 assignment 右值 SQL |
| `sqlparser_selector_set_insert_cell_sql()` | 通过 selector 改 INSERT 单元格右值 SQL |

说明：

- selector 适合做外部规则定位和 JSON patch 回放。
- 调用方可以先保存 selector 文本，再在后续请求中重新解析并执行。

## JSON 导出与模型导入

| 函数 | 摘要 |
| --- | --- |
| `sqlparser_export_parse_tree_json()` | 导出 parse tree JSON |
| `sqlparser_export_summary_json()` | 导出摘要 JSON |
| `sqlparser_export_model_json()` | 导出稳定模型 JSON |
| `sqlparser_apply_model_json()` | 导入完整模型或 patch JSON |

### `pretty` 参数

下列函数都带 `pretty` 参数：

- `sqlparser_export_parse_tree_json()`
- `sqlparser_export_summary_json()`
- `sqlparser_export_model_json()`

取值说明：

- `0`：紧凑 JSON
- 非 `0`：格式化 JSON

### JSON 类型

| JSON 类型 | 用途 |
| --- | --- |
| parse tree JSON | 底层语法树调试 |
| summary JSON | 表、列、关键字和语句类型提取 |
| 模型 JSON | 稳定工作模型、selector patch 和受控编辑 |

### `sqlparser_apply_model_json`

`sqlparser_apply_model_json()` 支持两种输入形式：

- `sqlparser_export_model_json()` 导出的完整模型
- 带 `changes` 数组的 patch JSON

示例：

```json
{
  "changes": [
    {
      "selector": "stmt[0].assignment[0]",
      "literal": {
        "kind": "string",
        "string_value": "carol"
      }
    },
    {
      "selector": "stmt[0].where_literal[0]",
      "literal": {
        "kind": "integer",
        "integer_value": 2
      }
    },
    {
      "selector": "stmt[0].assignment[0]",
      "sql": "lower(name)"
    }
  ]
}
```

说明：

- 对通用 `literal`、`where_literal` 和字面量形态的赋值项，可使用 `literal`
- 对 `assignment` 或 `insert_cell` 的 `DEFAULT` / 表达式改写，可使用 `sql`
- 单个 change 条目建议只使用一种改写形式

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

### selector 驱动改写

1. 导出模型 JSON
2. 保存 selector 文本
3. 生成 patch JSON
4. 调用 `sqlparser_apply_model_json()`
5. 调用 `sqlparser_deparse()`

## 相关示例

| 示例 | 说明 |
| --- | --- |
| `examples/01_select_inspect.c` | SELECT 读取与多表关联信息 |
| `examples/02_insert_values_replace_literal.c` | `INSERT ... VALUES` 字面量替换 |
| `examples/03_insert_select_inspect.c` | `INSERT ... SELECT` 检查 |
| `examples/04_update_replace_assignment.c` | UPDATE 赋值与 WHERE 同时改写 |
| `examples/05_delete_inspect.c` | DELETE 条件检查与改写 |
| `examples/06_ddl_inspect.c` | DDL 名称原子读取与改写 |
| `examples/07_multi_statement_walk.c` | 多语句遍历 |
| `examples/08_model_roundtrip.c` | 模型 JSON 导出、patch、回放 |
| `examples/09_expression_rewrite.c` | assignment / insert cell 表达式级改写 |
