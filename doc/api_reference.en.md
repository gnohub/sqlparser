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
4. Export SQL View JSON or traverse the C view if needed.
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
| `SQLPARSER_STATUS_RESOURCE_LIMIT` | Input, output, or statement count exceeds configured limits |

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
| `sqlparser_bind_kind_t` | prepared-statement placeholder kind |
| `sqlparser_literal_kind_t` | literal kind |
| `sqlparser_clause_kind_t` | SQL View clause kind |
| `sqlparser_selector_kind_t` | selector kind |
| `sqlparser_dialect_t` | SQL dialect |

`sqlparser_bind_kind_t` is used by the SQL View JSON `bind_kind` field:

| Enum | Value | Meaning |
| --- | --- | --- |
| `SQLPARSER_BIND_KIND_NONE` | `0` | the field has no bind |
| `SQLPARSER_BIND_KIND_POSITIONAL` | `1` | positional bind, such as `?`, `:1`, or `$1` |
| `SQLPARSER_BIND_KIND_NAMED` | `2` | named bind, such as `:name` or `@name` |

`sqlparser_clause_kind_t` is used by SQL View `clauses[]` entries and column
`clause_id` attribution:

| Enum | Meaning |
| --- | --- |
| `SQLPARSER_CLAUSE_KIND_SELECT_LIST` | SELECT output list |
| `SQLPARSER_CLAUSE_KIND_WHERE` | WHERE condition |
| `SQLPARSER_CLAUSE_KIND_ORDER_BY` | ORDER BY list |
| `SQLPARSER_CLAUSE_KIND_SET_LIST` | UPDATE SET list |
| `SQLPARSER_CLAUSE_KIND_ON` | JOIN or MERGE ON condition |
| `SQLPARSER_CLAUSE_KIND_GROUP_BY` | GROUP BY list |
| `SQLPARSER_CLAUSE_KIND_HAVING` | HAVING condition |

### Resource Limits

- `sqlparser_limits_t`

`sqlparser_limits_t` constrains the resource scale allowed by one parse and
generated output:

The default limits are: 4 MB SQL input, 4 MB generated output, and 64
statements per parse call.

| Field | Meaning |
| --- | --- |
| `struct_size` | structure size filled by `sqlparser_limits_default()` |
| `max_sql_bytes` | maximum bytes for SQL input and expression SQL fragments |
| `max_output_bytes` | maximum bytes for generated SQL or JSON output |
| `max_statement_count` | maximum number of statements accepted by one parse |

Call `sqlparser_limits_default()` to obtain the default limits. Callers only
need to override fields they want to change; a field set to `0` uses the default.

### Parse Options

- `sqlparser_parse_options_t`

`sqlparser_parse_options_t` configures parse behavior:

| Field | Meaning |
| --- | --- |
| `struct_size` | structure size filled by `sqlparser_parse_options_default()` |
| `dialect` | SQL dialect; defaults to `SQLPARSER_DIALECT_POSTGRESQL` |
| `limits` | resource limits |
| `flags` | reserved field; keep as `0` |

Defined dialects:

| Dialect | Meaning |
| --- | --- |
| `SQLPARSER_DIALECT_POSTGRESQL` | default dialect, preserving existing behavior |
| `SQLPARSER_DIALECT_MYSQL` | MySQL dialect conversion layer for syntax that can be safely mapped to the current AST |
| `SQLPARSER_DIALECT_ORACLE` | Oracle dialect conversion layer for common SQL syntax that can be safely mapped to the current AST |
| `SQLPARSER_DIALECT_SQLSERVER` | SQL Server dialect conversion layer for common T-SQL syntax that can be safely mapped to the current AST |
| `SQLPARSER_DIALECT_DAMENG` | Dameng dialect conversion layer for DM_SQL syntax that can be safely mapped to the current AST |

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

After a successful rewrite, reacquire any view data or exported result that is
still needed. Borrowed pointers obtained before the mutation must
not be reused.

### Thread Behavior

- A single `handle` does not support concurrent read/write access.
- A single `handle` is not guaranteed to be safe for concurrent read-only use.
- The usage model is one handle per owning thread.

## Version and Name Helpers

| Function | Summary |
| --- | --- |
| `sqlparser_version_string()` | returns the library version string |
| `sqlparser_libpg_query_tag()` | returns the pinned `libpg_query` tag |
| `sqlparser_statement_kind_name()` | returns the statement-kind name |
| `sqlparser_insert_source_kind_name()` | returns the `INSERT` source-kind name |
| `sqlparser_value_kind_name()` | returns the value-kind name |
| `sqlparser_bind_kind_name()` | returns the prepared-statement placeholder-kind name |
| `sqlparser_literal_kind_name()` | returns the literal-kind name |
| `sqlparser_selector_kind_name()` | returns the selector-kind name |
| `sqlparser_dialect_name()` | returns the dialect name |
| `sqlparser_bool_operator_name()` | returns the boolean-operator name |
| `sqlparser_clause_kind_name()` | returns the SQL View clause-kind name |

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
- `sqlparser_parse()` uses the default resource limits.

### `sqlparser_parse_with_limits`

Prototype:

```c
sqlparser_status_t sqlparser_parse_with_limits(
    const char *sql,
    const sqlparser_limits_t *limits,
    sqlparser_handle_t **out_handle,
    sqlparser_error_t *out_error);
```

Notes:

- Equivalent to `sqlparser_parse()`, but accepts caller-provided resource limits.
- Passing `NULL` for `limits` uses the defaults.
- On success, the limits are stored on the handle and apply to rewrites,
  exports, and deparse operations.
- Returns `SQLPARSER_STATUS_RESOURCE_LIMIT` when a configured limit is exceeded.

### `sqlparser_parse_with_options`

Prototype:

```c
sqlparser_status_t sqlparser_parse_with_options(
    const char *sql,
    const sqlparser_parse_options_t *options,
    sqlparser_handle_t **out_handle,
    sqlparser_error_t *out_error);
```

Notes:

- Behaves like `sqlparser_parse()`, with dialect and resource-limit configuration.
- Passing `NULL` for `options` uses PostgreSQL dialect and default resource limits.
- MySQL, Oracle, SQL Server, and Dameng dialect input is first converted into parser-compatible SQL, then processed through the unified AST path.
- Dialect syntax that cannot be safely mapped to the current AST returns `SQLPARSER_STATUS_UNSUPPORTED`.

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
| `sqlparser_handle_dialect()` | returns the dialect used by the handle |
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

## SELECT Target-List APIs

A SELECT target list is the output list after `SELECT`. These APIs operate on
the generic AST `SelectStmt.target_list` and can replace `*`, add an output
target, delete an output target, or replace one output expression.

| Function | Summary |
| --- | --- |
| `sqlparser_select_target_list_count()` | returns the number of SELECT target lists in a statement |
| `sqlparser_select_target_count()` | returns the number of output targets in one target list |
| `sqlparser_select_target_sql()` | reads one output target as SQL |
| `sqlparser_select_set_target_sql()` | replaces one output target SQL |
| `sqlparser_select_set_targets_sql()` | replaces the whole SELECT output list |
| `sqlparser_select_insert_target_sql()` | inserts one output target into a SELECT output list |
| `sqlparser_select_delete_target()` | deletes one output target from a SELECT output list |

Notes:

- `target_list_index` distinguishes multiple `SelectStmt` nodes in one
  statement, such as subqueries, CTEs, or set-operation branches.
- `target_index` is the zero-based output-target index inside one target list.
- `sqlparser_select_set_targets_sql()` accepts a comma-separated output list,
  for example `"id, name, upper(name) AS label"`.
- In dialect mode, input fragments are first processed by the dialect hook
  before they are written into the AST.
- After rewriting, call `sqlparser_deparse()` to generate SQL. To validate the
  generated SQL, parse it again with the same dialect using
  `sqlparser_parse_with_options()`.

## UPDATE and WHERE APIs

### UPDATE Assignments

| Function | Summary |
| --- | --- |
| `sqlparser_update_assignment_count()` | returns the number of `SET` assignments |
| `sqlparser_update_assignment()` | reads one assignment |
| `sqlparser_update_set_assignment_literal()` | rewrites the literal on the right side of an assignment |
| `sqlparser_update_assignment_sql()` | reads the right-hand SQL of an assignment |
| `sqlparser_update_set_assignment_sql()` | rewrites the right-hand SQL of an assignment |
| `sqlparser_update_insert_assignment_sql()` | inserts a full `SET` assignment, for example `secret_orig = 'abc'` |
| `sqlparser_update_delete_assignment()` | deletes one `SET` assignment |
| `sqlparser_update_set_assignment_full_sql()` | replaces a full `SET` assignment, including the left-hand column and right-hand expression |

### WHERE Literals

| Function | Summary |
| --- | --- |
| `sqlparser_statement_where_literal_count()` | returns the number of `WHERE` literals |
| `sqlparser_statement_where_literal()` | reads one `WHERE` literal |
| `sqlparser_statement_where_set_literal()` | rewrites one `WHERE` literal |

### WHERE Conditions

| Function | Summary |
| --- | --- |
| `sqlparser_statement_where_count()` | returns the number of writable `WHERE` slots in a statement |
| `sqlparser_statement_where_sql()` | reads one `WHERE` condition SQL; returns `NULL` for an empty slot |
| `sqlparser_statement_set_where_sql()` | sets or replaces one `WHERE` condition SQL |
| `sqlparser_statement_append_where_sql()` | appends a condition to a `WHERE` slot with `AND` or `OR` |

Notes:

- `sqlparser_assignment_view_t` is mainly used to read the column name, value
  kind, and right-hand literal of an assignment.
- `sqlparser_assignment_view_t.value_kind` distinguishes `literal`, `default`,
  and `expression`.
- `sqlparser_update_assignment_sql()` is suitable for reading `DEFAULT` or any
  expression-valued assignment.
- `sqlparser_update_set_assignment_sql()` is suitable for replacing the
  right-hand side of an assignment with a non-literal expression.
- `sqlparser_update_insert_assignment_sql()` inserts before `assignment_index`;
  passing the current assignment count appends to the `SET` list.
- `sqlparser_update_delete_assignment()` rejects deletion of the last assignment
  to avoid producing invalid `UPDATE SET` SQL.
- `sqlparser_update_set_assignment_full_sql()` accepts a complete assignment
  such as `name = 'alice'`; use `sqlparser_update_set_assignment_sql()` when only
  the right-hand expression changes.
- `sqlparser_where_literal_view_t` is mainly used to read the column name,
  operator, and condition literal.
- If column-name lookup is needed, first traverse and record the target index,
  then perform the rewrite.
- `where_index` addresses real AST `where_clause` slots. `INSERT ... VALUES`
  does not expose a synthetic `WHERE` slot.
- `sqlparser_statement_set_where_sql()` can add a missing `WHERE` to `SELECT`,
  `UPDATE`, `DELETE`, `INSERT ... SELECT`, and DDL or utility statements that
  support `WHERE`, including `CREATE VIEW AS SELECT`, partial indexes,
  `COPY FROM`, `CREATE RULE`, `CREATE PUBLICATION`, and exclusion constraints.
- `sqlparser_statement_append_where_sql()` adds a condition when the slot is
  empty, or preserves the existing condition and appends a new one when it is
  present.

### Generic Clause APIs

| Function | Summary |
| --- | --- |
| `sqlparser_statement_clause_count()` | returns the number of writable statement-level clauses |
| `sqlparser_statement_clause()` | reads one clause view |
| `sqlparser_statement_clause_sql()` | reads one clause SQL; returns `NULL` for an empty slot |
| `sqlparser_statement_set_clause_sql()` | sets or replaces one clause SQL |
| `sqlparser_statement_append_clause_condition()` | appends a condition to a `where` clause with `AND` or `OR` |

Supported `sqlparser_clause_kind_t` values:

| Enum | JSON name | Description |
| --- | --- | --- |
| `SQLPARSER_CLAUSE_KIND_SELECT_LIST` | `select_list` | SELECT output list |
| `SQLPARSER_CLAUSE_KIND_WHERE` | `where` | WHERE condition expression |
| `SQLPARSER_CLAUSE_KIND_ORDER_BY` | `order_by` | ORDER BY expression list |
| `SQLPARSER_CLAUSE_KIND_SET_LIST` | `set_list` | UPDATE SET assignment list |

Generic clause APIs are for structural rewrites. Table, column, and value
attribution remains available through SQL View `objects[].columns[]`.

## Selector APIs

A selector represents a readable or writable target as a stable text path or a
structured selector object.

Supported selector forms include:

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
| `sqlparser_selector_where_sql()` | reads `WHERE` condition SQL through a selector |
| `sqlparser_selector_clause()` | reads a generic clause view through a selector |
| `sqlparser_selector_clause_sql()` | reads generic clause SQL through a selector |
| `sqlparser_selector_update_assignment()` | reads an assignment through a selector |
| `sqlparser_selector_insert_cell_literal()` | reads an `INSERT` cell literal through a selector |
| `sqlparser_selector_update_assignment_sql()` | reads assignment right-hand SQL through a selector |
| `sqlparser_selector_insert_cell_sql()` | reads `INSERT` cell right-hand SQL through a selector |
| `sqlparser_selector_select_target_sql()` | reads a SELECT output target SQL through a selector |

### Selector Rewrite

| Function | Summary |
| --- | --- |
| `sqlparser_selector_set_relation_name()` | rewrites a relation name through a selector |
| `sqlparser_selector_set_name()` | rewrites a name atom through a selector |
| `sqlparser_selector_set_literal()` | rewrites a generic literal through a selector |
| `sqlparser_selector_set_where_literal()` | rewrites a `WHERE` literal through a selector |
| `sqlparser_selector_set_where_sql()` | sets or replaces `WHERE` condition SQL through a selector |
| `sqlparser_selector_append_where_sql()` | appends a condition to `WHERE` through a selector |
| `sqlparser_selector_set_clause_sql()` | sets or replaces generic clause SQL through a selector |
| `sqlparser_selector_append_clause_condition()` | appends a condition to a `where` clause through a selector |
| `sqlparser_selector_set_update_assignment_literal()` | rewrites an assignment right-hand literal through a selector |
| `sqlparser_selector_set_insert_cell_literal()` | rewrites an `INSERT` cell through a selector |
| `sqlparser_selector_set_update_assignment_sql()` | rewrites assignment right-hand SQL through a selector |
| `sqlparser_selector_insert_update_assignment_sql()` | inserts a full `SET` assignment through an assignment selector |
| `sqlparser_selector_delete_update_assignment()` | deletes a `SET` assignment through an assignment selector |
| `sqlparser_selector_set_update_assignment_full_sql()` | replaces a full `SET` assignment through an assignment selector |
| `sqlparser_selector_set_insert_cell_sql()` | rewrites `INSERT` cell right-hand SQL through a selector |
| `sqlparser_selector_set_select_target_sql()` | rewrites one SELECT output target SQL through a selector |
| `sqlparser_selector_set_select_targets_sql()` | rewrites the whole SELECT output list through a selector |

Notes:

- Selectors are suited for external rule addressing and structured patch replay.
- A caller can persist selector text and parse it again in a separate request.

## SQL View Structured Traversal

The SQL View can also be traversed through C views without exporting JSON
first. Strings returned through the views are borrowed from the `handle` and
become invalid after the handle is destroyed or rewritten.

| Function | Summary |
| --- | --- |
| `sqlparser_get_view()` | obtains a read-only statement view |
| `sqlparser_view_statement_at()` | reads one statement |
| `sqlparser_statement_keyword_at()` | reads a statement keyword |
| `sqlparser_statement_clause_at()` | reads one clause view |
| `sqlparser_clause_sql()` | renders a clause view as SQL |
| `sqlparser_statement_object_at()` | reads a table, view, or attributable object |
| `sqlparser_object_column_at()` | reads a column attributed to an object |
| `sqlparser_column_value_at()` | reads a value fragment related to a column |
| `sqlparser_object_row_at()` | reads an `INSERT ... VALUES` row |
| `sqlparser_row_cell_at()` | reads an `INSERT ... VALUES` cell |
| `sqlparser_value_sql()` | renders a column value fragment as SQL |
| `sqlparser_cell_sql()` | renders an `INSERT` cell as SQL |

Notes:

- `sqlparser_view_t`, `sqlparser_statement_view_t`,
  `sqlparser_object_view_t`, and related structs do not own memory.
- `sqlparser_clause_sql()`, `sqlparser_value_sql()`, and
  `sqlparser_cell_sql()` return new strings that must be released with
  `sqlparser_string_free()`.
- Column attribution uses only qualified names, aliases, and objects present
  in the SQL statement. It does not read database metadata and does not infer
  unique ownership.
- Selectors can be used for later patches. If no writable node is available,
  `has_selector` is `0`.

## JSON Export and Patch

| Function | Summary |
| --- | --- |
| `sqlparser_export_view_json()` | exports SQL View JSON |
| `sqlparser_apply_patch()` | applies structured patches |

Integration code can usually use `sqlparser_apply_patch()` as the unified
rewrite entry point. The fine-grained statement and selector rewrite functions
are available when the caller already holds exact indexes or structured
selectors.

### `pretty` Parameter

`sqlparser_export_view_json()` accepts a `pretty` parameter:

Meaning:

- `0`: compact JSON
- non-zero: formatted JSON

### `sqlparser_apply_patch`

`sqlparser_apply_patch()` accepts `sqlparser_patch_list_t`. Each patch uses a
selector to address a writable node.

Example:

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

Notes:

- `replace` can rewrite a relation, name, value, assignment, literal,
  where_literal, clause, insert_cell, select_target, or select_targets.
- `insert_column` can add a column to `INSERT ... VALUES` or insert an output
  target into `select_targets`.
- `delete_column` can delete a column from `INSERT ... VALUES` or delete an
  output target from `select_targets`.
- `delete_row` deletes an `INSERT ... VALUES` row.
- `append_condition` appends a condition to a `where` clause with `AND` or
  `OR`; when `bool_operator` is unset, it defaults to `AND`.
- `insert_assignment`, `delete_assignment`, and `replace_assignment` insert,
  delete, and replace full `UPDATE SET` assignments through
  `stmt[n].assignment[i]` selectors.
- `delete_column` does not delete the last cell, and `delete_row` does not delete the last row.
- Patches run in array order and return an error code plus message on failure.

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

### SELECT Target-List Rewrite

1. Call `sqlparser_select_target_count()` or read `target_list_selector` from
   SQL View.
2. Call `sqlparser_select_set_targets_sql()` to replace the whole output list,
   or call `sqlparser_select_insert_target_sql()` /
   `sqlparser_select_delete_target()` to add or remove output targets.
3. Call `sqlparser_deparse()`.
4. To validate generated SQL, parse it again with the same dialect through
   `sqlparser_parse_with_options()`.

### Selector-Driven Rewrite

1. Export SQL View JSON or traverse the C view.
2. Persist selector text.
3. Build `sqlparser_patch_t`.
4. Call `sqlparser_apply_patch()`.
5. Call `sqlparser_deparse()`.

## Related Examples

| Example | Description |
| --- | --- |
| `examples/patch/08_view_patch.c` | SQL View JSON export, patch replay, and SQL regeneration |
| `examples/patch/13_select_target_patch.c` | `SELECT *` expansion, output-target insertion, and output-target deletion through patches |
| `examples/patch/14_where_patch.c` | WHERE insertion and condition append through patches |
| `examples/patch/15_insert_columns_patch.c` | `INSERT ... VALUES` column insertion and deletion through patches |
| `examples/patch/16_clause_patch.c` | SELECT output-list, WHERE, and ORDER BY rewrite through generic clause patches |
| `examples/patch/17_update_set_patch.c` | `UPDATE SET` assignment append, delete, and full-assignment replacement through patches |
| `examples/convenience/02_insert_values_replace_literal.c` | fine-grained literal replacement for `INSERT ... VALUES` |
| `examples/convenience/04_update_replace_assignment.c` | fine-grained rewrite of UPDATE assignments and WHERE literals |
| `examples/convenience/05_delete_inspect.c` | DELETE target inspection and conditional literal rewrite |
| `examples/convenience/06_ddl_inspect.c` | DDL node inspection, name traversal, and object-name rewrite |
| `examples/convenience/09_expression_rewrite.c` | expression-level rewrite for assignments and insert cells |
| `examples/convenience/13_select_target_rewrite.c` | fine-grained SELECT output-list rewrite APIs |
| `examples/convenience/14_where_convenience.c` | fine-grained WHERE condition rewrite APIs |
| `examples/inspect/01_select_inspect.c` | `SELECT` inspection and multi-relation extraction |
| `examples/inspect/03_insert_select_inspect.c` | structural inspection for `INSERT ... SELECT` |
| `examples/inspect/07_multi_statement_walk.c` | traversal of multi-statement input |
| `examples/dialect/10_mysql_dialect.c` | MySQL dialect parsing and patch-based rewrite |
| `examples/dialect/11_oracle_dialect.c` | Oracle dialect parsing and rewrite |
| `examples/dialect/12_sqlserver_dialect.c` | SQL Server dialect parsing and deparse |
| `examples/dialect/17_dameng_dialect.c` | Dameng dialect parsing and deparse |
