# Model JSON Guide

`sqlparser` model JSON is the stable external work model used to:

- export the current SQL structure in a readable form
- identify rewrite targets through selectors
- replay a full model or a patch into the same `handle`

Model JSON is exported by `sqlparser_export_model_json()` and imported by
`sqlparser_apply_model_json()`.

## 1. Top-Level Layout

A typical top-level object looks like this:

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

Top-level fields:

| Field | Meaning |
| --- | --- |
| `schema` | model version marker, currently `sqlparser.model/v1` |
| `source_sql` | original input SQL |
| `current_sql` | SQL generated from the current AST state |
| `statement_count` | number of statements |
| `statements` | structured model entries for each statement |

## 2. Statement Objects

Each statement object contains at least:

| Field | Meaning |
| --- | --- |
| `statement_index` | zero-based statement index |
| `kind` | logical statement kind |
| `node_name` | underlying node name |

Depending on the statement shape, a statement object can also contain:

- `relations`
- `names`
- `literals`
- `where_literals`
- `update_assignments`
- `insert`

## 3. Common Object Shapes

### 3.1 Relation

```json
{
  "selector": "stmt[0].relation[0]",
  "schema_name": "public",
  "table_name": "users"
}
```

### 3.2 Name

```json
{
  "selector": "stmt[0].name[5]",
  "owner_type": "ColumnRef",
  "field_name": "fields",
  "value": "id"
}
```

### 3.3 Literal

```json
{
  "selector": "stmt[0].literal[1]",
  "literal": {
    "kind": "integer",
    "integer_value": 1
  }
}
```

Supported `literal.kind` values are:

- `null`
- `string`
- `integer`
- `float`
- `boolean`

### 3.4 Update Assignment

```json
{
  "selector": "stmt[0].assignment[0]",
  "column_name": "name",
  "value_kind": "expression",
  "sql": "upper(name)"
}
```

When the right-hand side is a literal, the entry also carries `literal`:

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

Supported `value_kind` values are:

- `literal`
- `default`
- `expression`
- `unknown`

### 3.5 Insert Rows and Cells

`INSERT ... VALUES` exports an `insert` object:

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

For `INSERT ... SELECT`, `source_kind` is typically `query`, and no fixed cell
matrix is exported.

## 4. Patch Form

`sqlparser_apply_model_json()` accepts two inputs:

- a full model JSON document
- a patch JSON document containing a `changes` array

Import behavior:

- Patch and full-model imports are atomic; if any change fails, the original
  `handle` is left unchanged.
- Full-model import applies only selectors that differ from the current model,
  avoiding duplicate replay across multiple views of the same AST node.
- SQL expression replacements are committed after ordinary field and literal
  changes so later selectors remain anchored to the original structure as much
  as possible.

The patch top-level object looks like this:

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

## 5. Patch Fields by Target Type

### 5.1 Relation Patch

```json
{
  "selector": "stmt[0].relation[0]",
  "schema_name": "archive",
  "table_name": "users_hist"
}
```

### 5.2 Name Patch

```json
{
  "selector": "stmt[0].name[3]",
  "value": "user_id"
}
```

### 5.3 Literal Patch

This applies to:

- `stmt[x].literal[y]`
- `stmt[x].where_literal[y]`
- literal-valued `assignment`
- literal-valued `insert_cell`

```json
{
  "selector": "stmt[0].where_literal[0]",
  "literal": {
    "kind": "integer",
    "integer_value": 2
  }
}
```

### 5.4 SQL Expression Patch

This applies to:

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

This form is suitable for:

- `DEFAULT`
- function calls
- arithmetic expressions
- JSON constructor expressions
- other valid right-hand expressions accepted by the parser

## 6. Usage Notes

- Prefer `literal` for plain literal replacement.
- Use `sql` for `DEFAULT` or arbitrary expressions.
- It is best to use only one rewrite form per patch entry.
- When editing a full model, keep `selector` values unchanged.
- Call `sqlparser_deparse()` after applying the model to obtain the updated SQL.

## 7. Related APIs

- `sqlparser_export_model_json()`
- `sqlparser_apply_model_json()`
- `sqlparser_selector_parse()`
- `sqlparser_selector_format()`
- `sqlparser_update_assignment_sql()`
- `sqlparser_update_set_assignment_sql()`
- `sqlparser_insert_cell_sql()`
- `sqlparser_insert_set_cell_sql()`

## 8. Related Examples

- `examples/08_model_roundtrip.c`
- `examples/09_expression_rewrite.c`
