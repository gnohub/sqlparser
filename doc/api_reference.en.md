# API Reference

This document describes the main public C API types, lifecycle rules, error
handling model, and function groups exposed by `sqlparser`.

## Overview

`sqlparser` is centered around `sqlparser_handle_t`. The standard usage flow is:

1. Call `sqlparser_parse()` to parse SQL and create a handle.
2. Read structure through statement-oriented APIs, generic atomic APIs, or
   selector APIs.
3. Apply rewrites to relation names, name atoms, literal values, or right-hand
   expressions.
4. Export parse-tree JSON, summary JSON, or model JSON if needed.
5. Call `sqlparser_deparse()` to generate the rewritten SQL.
6. Call `sqlparser_handle_destroy()` to release the handle.

## Header and Linking

Public header:

```c
#include "sqlparser/sqlparser.h"
```

Public libraries:

- `lib/libsqlparser.a`
- `lib/libsqlparser.so`

## Quick Example

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

    if (sqlparser_update_set_assignment_literal(handle, 0, 0, &value, &err)
        != SQLPARSER_STATUS_OK) {
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

## Status Codes and Error Object

Most APIs return `sqlparser_status_t`.

| Status Code | Meaning |
| --- | --- |
| `SQLPARSER_STATUS_OK` | Success |
| `SQLPARSER_STATUS_INVALID_ARGUMENT` | Invalid argument |
| `SQLPARSER_STATUS_NO_MEMORY` | Memory allocation failure |
| `SQLPARSER_STATUS_PARSE_ERROR` | SQL parse failure |
| `SQLPARSER_STATUS_INTERNAL_ERROR` | Internal processing failure |
| `SQLPARSER_STATUS_UNSUPPORTED` | The current statement shape is not supported by the requested operation |

Error details are returned through `sqlparser_error_t`:

- `code`: status code
- `cursor`: character offset
- `line`: line number
- `column`: column number
- `message`: error message

## Core Data Types

### Handle

- `sqlparser_handle_t`

`sqlparser_handle_t` is a long-lived object created by one parse operation. It
stores the original SQL, the current syntax tree, and lazily derived results.

### Common Enums

| Enum Type | Meaning |
| --- | --- |
| `sqlparser_statement_kind_t` | statement kind |
| `sqlparser_insert_source_kind_t` | `INSERT` source kind |
| `sqlparser_value_kind_t` | value kind |
| `sqlparser_literal_kind_t` | literal kind |
| `sqlparser_selector_kind_t` | selector kind |

### View Structures

| Structure | Meaning |
| --- | --- |
| `sqlparser_relation_view_t` | relation, schema, and alias view |
| `sqlparser_name_view_t` | name-atom view |
| `sqlparser_literal_view_t` | read-only literal view |
| `sqlparser_literal_value_t` | writable literal value |
| `sqlparser_assignment_view_t` | `UPDATE SET` assignment view |
| `sqlparser_where_literal_view_t` | `WHERE` literal view |
| `sqlparser_selector_t` | stable selector structure |

## Lifecycle and Thread Model

### Memory Ownership

- Handles returned by `sqlparser_parse()` are released by
  `sqlparser_handle_destroy()`.
- Strings returned by `sqlparser_deparse()` and JSON export functions are
  released by `sqlparser_string_free()`.
- Strings inside view structures are borrowed pointers.
- Borrowed pointers must not be freed by the caller.

### Index Rules

All indexes are zero-based:

- `statement_index`
- `relation_index`
- `name_index`
- `literal_index`
- `assignment_index`
- `row_index`
- `column_index`

### Rules After Mutation

After a successful rewrite, callers should reacquire any view data or exported
result they still need. Borrowed pointers obtained before the mutation should
not be reused.

### Thread Behavior

- A single `handle` does not support concurrent read/write access.
- A single `handle` is not guaranteed to be safe for concurrent read-only use.
- The recommended usage model is one handle per owning thread.

## Version and Name Helpers

| Function | Summary |
| --- | --- |
| `sqlparser_version_string()` | returns the library version string |
| `sqlparser_libpg_query_tag()` | returns the pinned `libpg_query` tag |
| `sqlparser_model_schema_string()` | returns the current model-JSON schema marker |
| `sqlparser_statement_kind_name()` | returns the statement-kind name |
| `sqlparser_insert_source_kind_name()` | returns the `INSERT` source-kind name |
| `sqlparser_value_kind_name()` | returns the value-kind name |
| `sqlparser_literal_kind_name()` | returns the literal-kind name |
| `sqlparser_selector_kind_name()` | returns the selector-kind name |

## Parse and Handle Management

### `sqlparser_parse`

Prototype:

```c
sqlparser_status_t sqlparser_parse(
    const char *sql,
    sqlparser_handle_t **out_handle,
    sqlparser_error_t *out_error);
```

Parameters:

- `sql`
  SQL string to parse.
- `out_handle`
  Output parameter. On success, receives a newly created handle.
- `out_error`
  Output parameter. On failure, receives error details.

Return Value:

- `SQLPARSER_STATUS_OK` on success
- `SQLPARSER_STATUS_PARSE_ERROR` when parsing fails
- `SQLPARSER_STATUS_INVALID_ARGUMENT` when arguments are invalid

Notes:

- The input SQL must not be an empty string.
- The caller owns the returned handle and must release it.

### `sqlparser_handle_destroy`

Prototype:

```c
void sqlparser_handle_destroy(sqlparser_handle_t *handle);
```

Notes:

- Releases the handle and all derived caches.
- Accepts `NULL`.

### Related Functions

| Function | Summary |
| --- | --- |
| `sqlparser_original_sql()` | returns the original input SQL |
| `sqlparser_statement_count()` | returns the number of statements |

## Statement-Level Access

| Function | Summary |
| --- | --- |
| `sqlparser_statement_kind()` | returns the logical kind of a statement |
| `sqlparser_statement_node_name()` | returns the underlying node name |
| `sqlparser_statement_target_relation()` | returns the primary target relation |

Notes:

- `statement_kind` is intended for statement classification.
- `node_name` is useful for finer-grained node inspection.
- `statement_target_relation` is useful when the statement has a clear primary
  target object.

## Relation APIs

| Function | Summary |
| --- | --- |
| `sqlparser_statement_relation_count()` | returns the number of relations |
| `sqlparser_statement_relation()` | reads a relation by index |
| `sqlparser_statement_set_relation_name()` | rewrites the schema name or table name of a relation |

Notes:

- Relation APIs are suitable for target relations, joined relations, and DDL
  object names.
- After rewriting a relation, call `sqlparser_deparse()` to generate the new
  SQL text.

## Name APIs

| Function | Summary |
| --- | --- |
| `sqlparser_statement_name_count()` | returns the number of name atoms |
| `sqlparser_statement_name()` | reads a name atom by index |
| `sqlparser_statement_set_name()` | rewrites a name atom |

Notes:

- A `name` is a non-literal string atom in the AST.
- Callers usually filter by `owner_type`, `field_name`, and `value`.
- This API can cover table names, column names, aliases, and DDL object names.

## Literal APIs

| Function | Summary |
| --- | --- |
| `sqlparser_statement_literal_count()` | returns the number of literals |
| `sqlparser_statement_literal()` | reads a literal by index |
| `sqlparser_statement_set_literal()` | rewrites a literal |

Notes:

- Literal APIs are suited for generic literal traversal and replacement.
- Use `sqlparser_literal_value_t` to describe the target type and value.

## INSERT APIs

| Function | Summary |
| --- | --- |
| `sqlparser_insert_source_kind()` | returns the `INSERT` source kind |
| `sqlparser_insert_column_count()` | returns the target column count |
| `sqlparser_insert_column_name()` | reads a target column name |
| `sqlparser_insert_row_count()` | returns the number of `VALUES` rows |
| `sqlparser_insert_cell_literal()` | reads the literal at one cell |
| `sqlparser_insert_set_cell_literal()` | rewrites the literal at one cell |
| `sqlparser_insert_cell_sql()` | reads the right-hand SQL at one cell |
| `sqlparser_insert_set_cell_sql()` | rewrites the right-hand SQL at one cell |

Notes:

- `INSERT ... VALUES` is addressed by row and column coordinates.
- `INSERT ... SELECT` does not expose a fixed cell model, and `row_count`
  is typically `0`.
- If column-name lookup is needed, first traverse the target columns and build
  a caller-side map from column name to `column_index`.
- `sqlparser_insert_cell_sql()` can be used for `DEFAULT`, function calls, and
  other expression-shaped cells.
- `sqlparser_insert_set_cell_sql()` is suitable when the target cell position
  stays the same but the right-hand expression must change.

## UPDATE and WHERE APIs

### UPDATE Assignments

| Function | Summary |
| --- | --- |
| `sqlparser_update_assignment_count()` | returns the number of `SET` assignments |
| `sqlparser_update_assignment()` | reads one assignment |
| `sqlparser_update_set_assignment_literal()` | rewrites the literal on the right side of an assignment |
| `sqlparser_update_assignment_sql()` | reads the right-hand SQL of an assignment |
| `sqlparser_update_set_assignment_sql()` | rewrites the right-hand SQL of an assignment |

### WHERE Literals

| Function | Summary |
| --- | --- |
| `sqlparser_statement_where_literal_count()` | returns the number of `WHERE` literals |
| `sqlparser_statement_where_literal()` | reads one `WHERE` literal |
| `sqlparser_statement_where_set_literal()` | rewrites one `WHERE` literal |

Notes:

- `sqlparser_assignment_view_t` is mainly used to read the column name, value
  kind, and right-hand literal of an assignment.
- `sqlparser_assignment_view_t.value_kind` distinguishes `literal`, `default`,
  and `expression`.
- `sqlparser_update_assignment_sql()` is suitable for reading `DEFAULT` or any
  expression-valued assignment.
- `sqlparser_update_set_assignment_sql()` is suitable for replacing an
  assignment with a non-literal expression.
- `sqlparser_where_literal_view_t` is mainly used to read the column name,
  operator, and condition literal.
- If column-name lookup is needed, first traverse and record the target index,
  then perform the rewrite.

## Selector APIs

A selector represents a readable or writable target as a stable text path or a
structured selector object.

Supported selector forms include:

```text
stmt[0].relation[0]
stmt[0].name[3]
stmt[0].literal[1]
stmt[0].where_literal[0]
stmt[0].assignment[0]
stmt[0].insert_cell[1][2]
```

### Selector Parse and Format

| Function | Summary |
| --- | --- |
| `sqlparser_selector_parse()` | parses text into `sqlparser_selector_t` |
| `sqlparser_selector_format()` | formats `sqlparser_selector_t` as text |

### Selector Read

| Function | Summary |
| --- | --- |
| `sqlparser_selector_relation()` | reads a relation through a selector |
| `sqlparser_selector_name()` | reads a name atom through a selector |
| `sqlparser_selector_literal()` | reads a literal through a selector |
| `sqlparser_selector_where_literal()` | reads a `WHERE` literal through a selector |
| `sqlparser_selector_update_assignment()` | reads an assignment through a selector |
| `sqlparser_selector_insert_cell_literal()` | reads an `INSERT` cell literal through a selector |
| `sqlparser_selector_update_assignment_sql()` | reads assignment right-hand SQL through a selector |
| `sqlparser_selector_insert_cell_sql()` | reads `INSERT` cell right-hand SQL through a selector |

### Selector Rewrite

| Function | Summary |
| --- | --- |
| `sqlparser_selector_set_relation_name()` | rewrites a relation name through a selector |
| `sqlparser_selector_set_name()` | rewrites a name atom through a selector |
| `sqlparser_selector_set_literal()` | rewrites a generic literal through a selector |
| `sqlparser_selector_set_where_literal()` | rewrites a `WHERE` literal through a selector |
| `sqlparser_selector_set_update_assignment_literal()` | rewrites an assignment right-hand literal through a selector |
| `sqlparser_selector_set_insert_cell_literal()` | rewrites an `INSERT` cell through a selector |
| `sqlparser_selector_set_update_assignment_sql()` | rewrites assignment right-hand SQL through a selector |
| `sqlparser_selector_set_insert_cell_sql()` | rewrites `INSERT` cell right-hand SQL through a selector |

Notes:

- Selectors are suited for external rule addressing and JSON patch replay.
- A caller can persist selector text and parse it again in a later request.

## JSON Export and Model Import

| Function | Summary |
| --- | --- |
| `sqlparser_export_parse_tree_json()` | exports parse-tree JSON |
| `sqlparser_export_summary_json()` | exports summary JSON |
| `sqlparser_export_model_json()` | exports stable model JSON |
| `sqlparser_apply_model_json()` | imports a full model or a patch JSON |

### `pretty` Parameter

The following functions accept a `pretty` parameter:

- `sqlparser_export_parse_tree_json()`
- `sqlparser_export_summary_json()`
- `sqlparser_export_model_json()`

Meaning:

- `0`: compact JSON
- non-zero: formatted JSON

### JSON Types

| JSON Type | Purpose |
| --- | --- |
| parse-tree JSON | low-level syntax-tree debugging |
| summary JSON | extraction of tables, columns, keywords, and statement kinds |
| model JSON | stable working model, selector patch, and controlled editing |

### `sqlparser_apply_model_json`

`sqlparser_apply_model_json()` accepts two forms of input:

- a full model exported by `sqlparser_export_model_json()`
- a patch JSON containing a `changes` array

Example:

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

Notes:

- Use `literal` for generic literals, `WHERE` literals, and literal-valued
  assignments or insert cells.
- Use `sql` for `assignment` or `insert_cell` rewrites that involve `DEFAULT`
  or arbitrary expressions.
- It is best to use one rewrite form per change entry.

## Deparse and String Free

### `sqlparser_deparse`

Prototype:

```c
sqlparser_status_t sqlparser_deparse(
    const sqlparser_handle_t *handle,
    char **out_sql,
    sqlparser_error_t *out_error);
```

Parameters:

- `handle`
  Handle to deparse.
- `out_sql`
  Output parameter. On success, receives the generated SQL string.
- `out_error`
  Output parameter. On failure, receives error details.

Return Value:

- `SQLPARSER_STATUS_OK` on success
- the corresponding error code on failure

### `sqlparser_string_free`

Prototype:

```c
void sqlparser_string_free(char *text);
```

Notes:

- Releases strings returned by `sqlparser_deparse()` and the JSON export
  functions.

## Common Usage Patterns

### Statement Classification

1. Call `sqlparser_parse()`.
2. Call `sqlparser_statement_kind()`.
3. Read `sqlparser_statement_node_name()` or the target relation if needed.

### Generic Traversal

1. Traverse relations through the relation APIs.
2. Traverse name atoms through the name APIs.
3. Traverse literals through the literal APIs.

### Rewrite INSERT by Column

1. Traverse target columns and locate the `column_index`.
2. Determine the target `row_index`.
3. Call `sqlparser_insert_set_cell_literal()`.
4. Call `sqlparser_deparse()`.

### Rewrite UPDATE SET and WHERE Together

1. Traverse assignments and locate the target column.
2. Call `sqlparser_update_set_assignment_literal()`.
3. Traverse `WHERE` literals and locate the target predicate.
4. Call `sqlparser_statement_where_set_literal()`.
5. Call `sqlparser_deparse()`.

### Expression-Level Rewrite

1. Call `sqlparser_update_assignment_sql()` or `sqlparser_insert_cell_sql()` to
   read the current right-hand SQL.
2. Call `sqlparser_update_set_assignment_sql()` or
   `sqlparser_insert_set_cell_sql()` to write the new expression.
3. Call `sqlparser_deparse()`.

### Selector-Driven Rewrite

1. Export model JSON.
2. Persist selector text.
3. Generate patch JSON.
4. Call `sqlparser_apply_model_json()`.
5. Call `sqlparser_deparse()`.

## Related Examples

| Example | Description |
| --- | --- |
| `examples/01_select_inspect.c` | `SELECT` inspection and multi-relation extraction |
| `examples/02_insert_values_replace_literal.c` | literal replacement for `INSERT ... VALUES` |
| `examples/03_insert_select_inspect.c` | inspection of `INSERT ... SELECT` |
| `examples/04_update_replace_assignment.c` | simultaneous rewrite of `UPDATE` assignments and `WHERE` |
| `examples/05_delete_inspect.c` | `DELETE` condition inspection and rewrite |
| `examples/06_ddl_inspect.c` | DDL name-atom inspection and rewrite |
| `examples/07_multi_statement_walk.c` | multi-statement traversal |
| `examples/08_model_roundtrip.c` | model JSON export, patch replay, and SQL regeneration |
| `examples/09_expression_rewrite.c` | expression-level rewrite for assignments and insert cells |
