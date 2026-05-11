# SQL View JSON Guide

SQL View JSON is the structured SQL view exported by `sqlparser`. It reports statements, objects, columns, and value fragments that appear in the SQL text. It does not keep a copy of the input SQL and does not expose the underlying parser JSON.

## Export API

```c
sqlparser_status_t sqlparser_export_view_json(
    const sqlparser_handle_t *handle,
    int pretty,
    char **out_json,
    sqlparser_error_t *out_error);
```

- `handle` must come from a successful parse.
- `pretty != 0` emits formatted JSON; `pretty == 0` emits compact JSON.
- `out_json` is allocated by the library and must be released with `sqlparser_string_free()`.

## C View API

Callers can traverse the SQL View directly without exporting JSON:

```c
sqlparser_view_t view;
sqlparser_statement_view_t statement;
sqlparser_object_view_t object;
sqlparser_column_view_t column;

sqlparser_get_view(handle, &view, &err);
sqlparser_view_statement_at(&view, 0, &statement, &err);
sqlparser_statement_object_at(&statement, 0, &object, &err);
sqlparser_object_column_at(&object, 0, &column, &err);
```

These view structs do not copy the AST and do not keep SQL string copies.
When a field value or `INSERT` cell needs SQL text, call
`sqlparser_value_sql()` or `sqlparser_cell_sql()` on demand, then release the
returned string with `sqlparser_string_free()`.

## Top-Level Layout

The top-level object contains only `statements`:

```json
{
  "statements": [
    {
      "index": 0,
      "keyword": "insert",
      "keywords": ["insert", "into", "values"],
      "clauses": [],
      "objects": []
    }
  ]
}
```

Fields:

| Field | Description |
| --- | --- |
| `index` | zero-based statement index |
| `keyword` | main statement keyword |
| `keywords` | recognized keyword set for the statement |
| `clauses` | writable statement-level clause array; `sql` is `null` when a slot is empty |
| `objects` | tables, views, or other attributable objects in the SQL |

## Clause Layout

`clauses` describes statement-level rewrite slots, not column attribution. The currently writable clause kinds are `select_list`, `where`, and `order_by`.

```json
{
  "kind": "where",
  "selector": "stmt[0].clause[1]",
  "sql": "status = 'active'"
}
```

Fields:

| Field | Description |
| --- | --- |
| `kind` | clause kind, such as `select_list`, `where`, or `order_by` |
| `selector` | clause selector for patching |
| `sql` | clause content, or `null` when the statement can add the clause but it is not present yet |

Use `clauses` for structural rewrites such as replacing the SELECT output list, adding WHERE, appending WHERE conditions, or adding ORDER BY. Use `objects[].columns[]` for table, column, and value attribution.

## Object Layout

Each item in `objects` represents an attributable SQL object:

```json
{
  "database": null,
  "schema": "public",
  "table": "users",
  "alias": "u",
  "selector": "stmt[0].relation[0]",
  "columns": [],
  "rows": []
}
```

Fields:

| Field | Description |
| --- | --- |
| `database` | database name, or `null` when absent |
| `schema` | schema name, or `null` when absent |
| `table` | table or view name, or `null` when absent |
| `alias` | alias, or `null` when absent |
| `selector` | object selector for patching, or `null` when not writable |
| `columns` | columns attributed to the object |
| `rows` | row data for `INSERT ... VALUES` |

## Column Layout

Only columns that appear in SQL are reported. An unqualified column is attached to all matching objects in the statement; attribution is not a uniqueness check.

```json
{
  "name": "status",
  "keyword": "where",
  "operator": "=",
  "selector": "stmt[0].name[3]",
  "target_list_selector": null,
  "target_selector": null,
  "value": {
    "sql": "'active'",
    "selector": "stmt[0].value[7]"
  }
}
```

Fields:

| Field | Description |
| --- | --- |
| `name` | column name; `SELECT *` is reported as `"*"` |
| `keyword` | clause where the column appears, such as `select`, `where`, `set`, or `on` |
| `operator` | related operator, or `null` |
| `selector` | column-name selector, or `null` when not writable |
| `target_list_selector` | whole target-list selector when the column appears in a SELECT output list, otherwise `null` |
| `target_selector` | single-output-target selector when the column appears in a SELECT output list, otherwise `null` |
| `value` | directly related SQL value fragment, or `null` |

`value.sql` is an editable SQL fragment. The model does not infer a database type for the value. Complex expressions that cannot be rendered safely as standalone SQL fragments keep `value: null`; the column entry is still reported.
`value.selector` addresses the whole value expression and can replace a literal,
bind parameter, or expression with a new SQL fragment.

## INSERT Rows

`INSERT ... VALUES` exports rows and cells on the target object:

```json
{
  "index": 0,
  "cells": [
    {
      "column": "id",
      "column_index": 0,
      "sql": "1",
      "selector": "stmt[0].insert_cell[0][0]"
    },
    {
      "column": "name",
      "column_index": 1,
      "sql": "'bob'",
      "selector": "stmt[0].insert_cell[0][1]"
    }
  ]
}
```

When the `INSERT` statement omits explicit column names, `column` is `null` and `column_index` still records the cell position.

## Structured Patch

Structured patches write changes back into the same `handle`.

```c
sqlparser_status_t sqlparser_apply_patch(
    sqlparser_handle_t *handle,
    const sqlparser_patch_list_t *patches,
    sqlparser_error_t *out_error);
```

After obtaining a selector from SQL View JSON or the C view API, fill an array
of `sqlparser_patch_t` and pass it to `sqlparser_apply_patch()`.

Supported operations:

| `op` | Description |
| --- | --- |
| `replace` | replaces a relation, name, value, assignment, literal, where_literal, clause, insert_cell, select_target, or select_targets |
| `insert_column` | adds a column to `INSERT ... VALUES`, or inserts one SELECT output target into `select_targets` |
| `delete_column` | deletes one column from `INSERT ... VALUES`, or deletes one SELECT output target from `select_targets` |
| `delete_row` | deletes one row from `INSERT ... VALUES` |
| `append_condition` | appends a condition to a `where` clause with `AND` or `OR` |

`delete_column` never produces an empty `VALUES` row; deleting the last cell
returns `SQLPARSER_STATUS_UNSUPPORTED`. `delete_row` does not delete the last row.

Replace-value example:

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].insert_cell[1][1]";
patch.sql = "'lisi'";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

Add-column example:

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_INSERT_COLUMN;
patch.selector = "stmt[0].insert_columns";
patch.index = 2;
patch.name = "age";
patch.default_sql = "18";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

Statement-clause example:

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].clause[2]";
patch.sql = "name DESC, id ASC";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

Delete-row example:

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_DELETE_ROW;
patch.selector = "stmt[0].insert_row[1]";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

Replace `SELECT *` example:

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].select_targets[0]";
patch.sql = "id, name, upper(name) AS normalized_name";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```

## Example

SQL:

```sql
INSERT INTO users (id, name) VALUES (1, 'xiaohong'), (2, 'xiaoming')
```

Output:

```json
{
  "statements": [
    {
      "index": 0,
      "keyword": "insert",
      "keywords": ["insert", "into", "values"],
      "clauses": [],
      "objects": [
        {
          "database": null,
          "schema": null,
          "table": "users",
          "alias": null,
          "selector": "stmt[0].relation[0]",
          "columns": [
            {
              "name": "id",
              "keyword": "insert",
              "operator": null,
              "selector": "stmt[0].name[0]",
              "value": null
            },
            {
              "name": "name",
              "keyword": "insert",
              "operator": null,
              "selector": "stmt[0].name[1]",
              "value": null
            }
          ],
          "rows": [
            {
              "index": 0,
              "cells": [
                {
                  "column": "id",
                  "column_index": 0,
                  "sql": "1",
                  "selector": "stmt[0].insert_cell[0][0]"
                },
                {
                  "column": "name",
                  "column_index": 1,
                  "sql": "'xiaohong'",
                  "selector": "stmt[0].insert_cell[0][1]"
                }
              ]
            },
            {
              "index": 1,
              "cells": [
                {
                  "column": "id",
                  "column_index": 0,
                  "sql": "2",
                  "selector": "stmt[0].insert_cell[1][0]"
                },
                {
                  "column": "name",
                  "column_index": 1,
                  "sql": "'xiaoming'",
                  "selector": "stmt[0].insert_cell[1][1]"
                }
              ]
            }
          ]
        }
      ]
    }
  ]
}
```

Replace the second-row `name` value:

```c
sqlparser_patch_t patch;
sqlparser_patch_list_t patches;

memset(&patch, 0, sizeof(patch));
patch.op = SQLPARSER_PATCH_REPLACE;
patch.selector = "stmt[0].insert_cell[1][1]";
patch.sql = "'lisi'";

patches.items = &patch;
patches.count = 1;
sqlparser_apply_patch(handle, &patches, &err);
```
