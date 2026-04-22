# 模型 JSON 手册

`sqlparser` 的模型 JSON 是面向外部程序的稳定工作模型，用于：

- 导出当前 SQL 的可读结构
- 通过 selector 精确定位修改点
- 把完整模型或 patch 回放到同一个 `handle`

模型 JSON 由 `sqlparser_export_model_json()` 导出，由 `sqlparser_apply_model_json()` 导入。

## 1. 顶层结构

典型顶层结构如下：

```json
{
  "schema": "sqlparser.model/v1",
  "source_sql": "UPDATE public.users SET name = 'bob' WHERE id = 1",
  "current_sql": "UPDATE public.users SET name = 'bob' WHERE id = 1",
  "statement_count": 1,
  "statements": [
    {
      "statement_index": 0,
      "kind": "update",
      "node_name": "UpdateStmt"
    }
  ]
}
```

顶层字段说明：

| 字段 | 说明 |
| --- | --- |
| `schema` | 当前模型版本标识，固定为 `sqlparser.model/v1` |
| `source_sql` | 初始输入 SQL |
| `current_sql` | 当前 AST 状态对应的 SQL |
| `statement_count` | 语句数量 |
| `statements` | 逐条语句的结构化模型 |

## 2. 语句对象

每个 `statement` 对象至少包含：

| 字段 | 说明 |
| --- | --- |
| `statement_index` | 语句索引，0 基 |
| `kind` | 逻辑语句类型 |
| `node_name` | 底层节点名称 |

根据语句类型不同，还可能包含以下数组或对象：

- `relations`
- `names`
- `literals`
- `where_literals`
- `update_assignments`
- `insert`

## 3. 常见对象形态

### 3.1 relation

```json
{
  "selector": "stmt[0].relation[0]",
  "schema_name": "public",
  "table_name": "users"
}
```

### 3.2 name

```json
{
  "selector": "stmt[0].name[5]",
  "owner_type": "ColumnRef",
  "field_name": "fields",
  "value": "id"
}
```

### 3.3 literal

```json
{
  "selector": "stmt[0].literal[1]",
  "literal": {
    "kind": "integer",
    "integer_value": 1
  }
}
```

`literal.kind` 当前支持：

- `null`
- `string`
- `integer`
- `float`
- `boolean`

### 3.4 update assignment

```json
{
  "selector": "stmt[0].assignment[0]",
  "column_name": "name",
  "value_kind": "expression",
  "sql": "upper(name)"
}
```

如果右值是字面量，还会同时导出 `literal`：

```json
{
  "selector": "stmt[0].assignment[0]",
  "column_name": "name",
  "value_kind": "literal",
  "sql": "'bob'",
  "literal": {
    "kind": "string",
    "string_value": "bob"
  }
}
```

`value_kind` 取值包括：

- `literal`
- `default`
- `expression`
- `unknown`

### 3.5 insert rows / cells

`INSERT ... VALUES` 会导出 `insert` 对象：

```json
{
  "source_kind": "values",
  "columns": [
    {
      "column_index": 0,
      "name": "id"
    },
    {
      "column_index": 1,
      "name": "name"
    }
  ],
  "rows": [
    {
      "row_index": 0,
      "cells": [
        {
          "selector": "stmt[0].insert_cell[0][0]",
          "value_kind": "literal",
          "sql": "1",
          "literal": {
            "kind": "integer",
            "integer_value": 1
          }
        },
        {
          "selector": "stmt[0].insert_cell[0][1]",
          "value_kind": "expression",
          "sql": "upper('bob')"
        }
      ]
    }
  ]
}
```

对于 `INSERT ... SELECT`，`source_kind` 通常为 `query`，不会导出固定单元格矩阵。

## 4. Patch 形式

`sqlparser_apply_model_json()` 接受两种输入：

- 完整模型 JSON
- 仅包含 `changes` 数组的 patch JSON

patch 顶层结构如下：

```json
{
  "changes": [
    {
      "selector": "stmt[0].assignment[0]",
      "literal": {
        "kind": "string",
        "string_value": "carol"
      }
    }
  ]
}
```

## 5. Patch 支持的字段

### 5.1 relation patch

```json
{
  "selector": "stmt[0].relation[0]",
  "schema_name": "archive",
  "table_name": "users_hist"
}
```

### 5.2 name patch

```json
{
  "selector": "stmt[0].name[3]",
  "value": "user_id"
}
```

### 5.3 literal patch

适用于：

- `stmt[x].literal[y]`
- `stmt[x].where_literal[y]`
- 字面量形态的 `assignment`
- 字面量形态的 `insert_cell`

```json
{
  "selector": "stmt[0].where_literal[0]",
  "literal": {
    "kind": "integer",
    "integer_value": 2
  }
}
```

### 5.4 SQL 表达式 patch

适用于：

- `stmt[x].assignment[y]`
- `stmt[x].insert_cell[row][column]`

```json
{
  "selector": "stmt[0].assignment[0]",
  "sql": "lower(name)"
}
```

```json
{
  "selector": "stmt[1].insert_cell[0][2]",
  "sql": "clock_timestamp()"
}
```

这类 patch 适用于：

- `DEFAULT`
- 函数调用
- 算术表达式
- JSON 构造表达式
- 其他可被解析器接受的右值表达式

## 6. 使用建议

- 对字面量替换，优先使用 `literal`
- 对 `DEFAULT` 或任意表达式替换，使用 `sql`
- 单个 patch 条目建议只使用一种改写形式
- 完整模型编辑后重新导入时，建议保持 `selector` 不变
- 改写完成后使用 `sqlparser_deparse()` 获取最新 SQL

## 7. 相关接口

- `sqlparser_export_model_json()`
- `sqlparser_apply_model_json()`
- `sqlparser_selector_parse()`
- `sqlparser_selector_format()`
- `sqlparser_update_assignment_sql()`
- `sqlparser_update_set_assignment_sql()`
- `sqlparser_insert_cell_sql()`
- `sqlparser_insert_set_cell_sql()`

## 8. 相关示例

- `examples/08_model_roundtrip.c`
- `examples/09_expression_rewrite.c`
