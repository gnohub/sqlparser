# SQL View JSON Guide

SQL View JSON is the structured SQL view exported by `sqlparser`. It is serialized on demand from the SQL View C structs and reports statements, objects, columns, and value fragments that appear in the SQL text. It does not keep a copy of the input SQL and does not expose the underlying parser JSON.

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
| `clauses` | statement-level clause array; writable clauses have selectors, read-only attribution clauses use `null` selectors |
| `objects` | tables, views, or other attributable objects in the SQL |

## Clause Layout

`clauses` describes statement-level structure. The currently writable clause kinds are `select_list`, `set_list`, `where`, and `order_by`. Read-only attribution clauses such as `on`, `group_by`, and `having` use `selector: null` and are not direct patch targets.

```json
{
  "id": 2,
  "kind": "where",
  "selector": "stmt[0].clause[1]",
  "sql": "status = 'active'"
}
```

Fields:

| Field | Description |
| --- | --- |
| `id` | one-based clause id; `objects[].columns[].clause_id` references this id |
| `kind` | clause kind, such as `select_list`, `set_list`, `where`, `order_by`, or `on` |
| `selector` | clause selector for patching; `null` for read-only clauses |
| `sql` | clause content, or `null` when the statement can add the clause but it is not present yet |

Use writable `clauses` for structural rewrites such as replacing the SELECT output list, replacing the UPDATE SET list, adding WHERE, appending WHERE conditions, or adding ORDER BY. `objects[].columns[]` links each attributed field to `clauses[].id` through `clause_id`.

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
  "bind": null,
  "bind_kind": 0,
  "bind_sql": null,
  "bind_selector": null,
  "selector": "stmt[0].name[3]",
  "target_list_selector": null,
  "target_selector": null,
  "clause_id": 2,
  "target_path": [],
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
| `bind` | normalized bind name when the field value is a prepared-statement placeholder, otherwise `null` |
| `bind_kind` | bind type enum: `0` means no bind, `1` means positional bind, and `2` means named bind |
| `bind_sql` | original placeholder text in SQL, such as `"?"`, `":1"`, `":id"`, `"@id"`, or `"$1"`; `null` when there is no bind |
| `bind_selector` | selector for the bind value expression, otherwise `null` |
| `selector` | column-name selector, or `null` when not writable |
| `target_list_selector` | whole target-list selector when the column appears in a SELECT output list, otherwise `null` |
| `target_selector` | single-output-target selector when the column appears in a SELECT output list, otherwise `null` |
| `clause_id` | one-based `clauses[]` id for the clause containing the field, or `null` when not stable |
| `target_path` | ordered function or expression wrapper path when the field appears in a SELECT output item; non-output fields use `[]` |
| `value` | directly related SQL value fragment, or `null` |

`value.sql` is an editable SQL fragment. The model does not infer a database type for the value. Complex expressions that cannot be rendered safely as standalone SQL fragments keep `value: null`; the column entry is still reported.
`value.selector` addresses the whole value expression and can replace a literal,
bind parameter, or expression with a new SQL fragment.

Prepared-statement placeholders export structured bind fields and keep
`value: null`. `bind` is the normalized name or position, `bind_kind`
identifies named versus positional binding, and `bind_sql` preserves the
placeholder text as it appeared in SQL. Oracle `:1` and JDBC-style `?` are both
positional binds, but `bind_sql` preserves them as `":1"` and `"?"`, so callers
can distinguish the source form. Use `bind_selector` to rewrite the bind
expression.

When one field is associated with multiple condition values, the view exports
one column entry per value. For example, `status IN (:s1, :s2, :s3)` produces
three `name: "status"` and `operator: "IN"` entries, each carrying the bind
information for one placeholder. `NOT IN`, `NOT BETWEEN`, `NOT LIKE`,
`NOT ILIKE`, and `NOT SIMILAR TO` preserve the negated operator text in
`operator`.

`target_path` is an ordered array from the outermost wrapper to the innermost
wrapper. The array order is the hierarchy; no additional ordering field is
needed.

Each entry contains:

| Field | Description |
| --- | --- |
| `kind` | `function` or `expression` |
| `name` | function name, operator, or expression category; `null` when not stable |
| `arg_index` | zero-based argument or operand index where the next inner expression appears |

Examples:

| SQL fragment | Field | `target_path` |
| --- | --- | --- |
| `SELECT name FROM users` | `name` | `[]` |
| `SELECT * FROM users` | `*` | `[]` |
| `SELECT UPPER(name) FROM users` | `name` | `[{"kind":"function","name":"UPPER","arg_index":0}]` |
| `SELECT LOW(UPPER(name)) FROM users` | `name` | `[{"kind":"function","name":"LOW","arg_index":0},{"kind":"function","name":"UPPER","arg_index":0}]` |
| `SELECT CONCAT(UPPER(first_name), last_name) FROM users` | `first_name` | `[{"kind":"function","name":"CONCAT","arg_index":0},{"kind":"function","name":"UPPER","arg_index":0}]` |
| `SELECT CONCAT(UPPER(first_name), last_name) FROM users` | `last_name` | `[{"kind":"function","name":"CONCAT","arg_index":1}]` |
| `SELECT first_name || last_name FROM users` | `first_name` | `[{"kind":"expression","name":"||","arg_index":0}]` |
| `SELECT first_name || last_name FROM users` | `last_name` | `[{"kind":"expression","name":"||","arg_index":1}]` |
| `SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users` | `amount` | `[{"kind":"function","name":"SUM","arg_index":0}]` |
| `SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users` | `dept` | `[{"kind":"expression","name":"window_partition","arg_index":0}]` |
| `SELECT SUM(amount) OVER (PARTITION BY dept ORDER BY created_at) FROM users` | `created_at` | `[{"kind":"expression","name":"window_order","arg_index":0}]` |

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
      "bind": null,
      "bind_kind": 0,
      "bind_sql": null,
      "bind_selector": null,
      "selector": "stmt[0].insert_cell[0][0]"
    },
    {
      "column": "name",
      "column_index": 1,
      "sql": "'bob'",
      "bind": null,
      "bind_kind": 0,
      "bind_sql": null,
      "bind_selector": null,
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
| `insert_assignment` | inserts a full `UPDATE SET` assignment through a `stmt[n].assignment[i]` selector |
| `delete_assignment` | deletes one `UPDATE SET` assignment through a `stmt[n].assignment[i]` selector |
| `replace_assignment` | replaces a full `UPDATE SET` assignment through a `stmt[n].assignment[i]` selector |

`delete_column` never produces an empty `VALUES` row; deleting the last cell
returns `SQLPARSER_STATUS_UNSUPPORTED`. `delete_row` does not delete the last
row. `delete_assignment` does not delete the last `UPDATE SET` assignment.

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

Rewrite `UPDATE SET` example:

```c
sqlparser_patch_t patches[2];
sqlparser_patch_list_t patch_list;

memset(patches, 0, sizeof(patches));
patches[0].op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
patches[0].selector = "stmt[0].assignment[1]";
patches[0].sql = "secret_orig = 'abc'";
patches[1].op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
patches[1].selector = "stmt[0].assignment[0]";
patches[1].sql = "secret = 'masked'";

patch_list.items = patches;
patch_list.count = 2;
sqlparser_apply_patch(handle, &patch_list, &err);
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
              "bind": null,
              "bind_kind": 0,
              "bind_sql": null,
              "bind_selector": null,
              "selector": "stmt[0].name[0]",
              "target_list_selector": null,
              "target_selector": null,
              "clause_id": null,
              "target_path": [],
              "value": null
            },
            {
              "name": "name",
              "keyword": "insert",
              "operator": null,
              "bind": null,
              "bind_kind": 0,
              "bind_sql": null,
              "bind_selector": null,
              "selector": "stmt[0].name[1]",
              "target_list_selector": null,
              "target_selector": null,
              "clause_id": null,
              "target_path": [],
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
                  "bind": null,
                  "bind_kind": 0,
                  "bind_sql": null,
                  "bind_selector": null,
                  "selector": "stmt[0].insert_cell[0][0]"
                },
                {
                  "column": "name",
                  "column_index": 1,
                  "sql": "'xiaohong'",
                  "bind": null,
                  "bind_kind": 0,
                  "bind_sql": null,
                  "bind_selector": null,
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
                  "bind": null,
                  "bind_kind": 0,
                  "bind_sql": null,
                  "bind_selector": null,
                  "selector": "stmt[0].insert_cell[1][0]"
                },
                {
                  "column": "name",
                  "column_index": 1,
                  "sql": "'xiaoming'",
                  "bind": null,
                  "bind_kind": 0,
                  "bind_sql": null,
                  "bind_selector": null,
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
