# API Reference

This document describes the public C API types, lifecycle rules, structured
read APIs, and rewrite APIs exposed by `sqlparser`.

## Overview

`sqlparser` is centered around `sqlparser_handle_t`. The standard flow is:

1. Parse SQL with `sqlparser_parse()` or `sqlparser_parse_with_options()`.
2. Read structure through statement APIs, selector APIs, or the `query_graph`
   APIs.
3. Rewrite the AST through selectors, fine-grained rewrite functions, or
   `sqlparser_apply_patch()`.
4. Generate SQL with `sqlparser_deparse()`.
5. Release the handle with `sqlparser_handle_destroy()`.

View JSON is the on-demand JSON serialization of `query_graph`. It is intended
for regression tests, integration checks, and language-neutral inspection.
Production code should prefer the C query graph structs and does not need to
generate JSON before rewriting SQL.

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
| `SQLPARSER_STATUS_UNSUPPORTED` | The requested operation is unsupported for the current statement shape |
| `SQLPARSER_STATUS_RESOURCE_LIMIT` | Input, output, or statement count exceeds configured limits |

`sqlparser_error_t` fields:

| Field | Meaning |
| --- | --- |
| `code` | status code |
| `cursor` | character offset |
| `line` | line number |
| `column` | column number |
| `message` | error message |

## Core Types

### Handle

`sqlparser_handle_t` represents a long-lived parsed object. It stores the
original SQL, the current syntax tree, dialect state, and lazily derived caches.

### Common Enums

| Enum Type | Meaning |
| --- | --- |
| `sqlparser_statement_kind_t` | statement kind |
| `sqlparser_insert_source_kind_t` | `INSERT` source kind |
| `sqlparser_value_kind_t` | value kind used by fine-grained value APIs |
| `sqlparser_bind_kind_t` | prepared-statement placeholder kind |
| `sqlparser_literal_kind_t` | literal kind |
| `sqlparser_selector_kind_t` | selector kind |
| `sqlparser_clause_kind_t` | clause kind used by query graph and clause patches |
| `sqlparser_dialect_t` | SQL dialect |

`sqlparser_bind_kind_t`:

| Enum | Value | Meaning |
| --- | --- | --- |
| `SQLPARSER_BIND_KIND_NONE` | `0` | no bind |
| `SQLPARSER_BIND_KIND_POSITIONAL` | `1` | positional bind, such as `?`, `:1`, or `$1` |
| `SQLPARSER_BIND_KIND_NAMED` | `2` | named bind, such as `:name` or `@name` |

Bind-field rules:

- `bind_key` is interpreted by `bind_kind`; named binds use the name,
  anonymous `?` uses the global sequence string, and explicitly numbered binds
  keep the number written in SQL.
- `bind_position` is the one-based bind occurrence across the full input SQL;
  it does not restart per statement.
- `bind_sql` preserves the original placeholder text as written in SQL.

`sqlparser_clause_kind_t`:

| Enum | JSON Name | Meaning |
| --- | --- | --- |
| `SQLPARSER_CLAUSE_KIND_SELECT_LIST` | `select_list` | SELECT output list |
| `SQLPARSER_CLAUSE_KIND_WHERE` | `where` | WHERE condition |
| `SQLPARSER_CLAUSE_KIND_ORDER_BY` | `order_by` | ORDER BY list |
| `SQLPARSER_CLAUSE_KIND_SET_LIST` | `set_list` | UPDATE SET list |
| `SQLPARSER_CLAUSE_KIND_ON` | `on` | JOIN or MERGE ON condition |
| `SQLPARSER_CLAUSE_KIND_GROUP_BY` | `group_by` | GROUP BY list |
| `SQLPARSER_CLAUSE_KIND_HAVING` | `having` | HAVING condition |

## Resource Limits and Parse Options

The default `sqlparser_limits_t` values are: 4 MB SQL input, 4 MB generated
output, and 64 statements per parse call.

| Field | Meaning |
| --- | --- |
| `struct_size` | structure size filled by `sqlparser_limits_default()` |
| `max_sql_bytes` | maximum bytes for SQL input and expression fragments |
| `max_output_bytes` | maximum bytes for generated SQL or JSON output |
| `max_statement_count` | maximum number of statements accepted by one parse |

`sqlparser_parse_options_t`:

| Field | Meaning |
| --- | --- |
| `struct_size` | structure size filled by `sqlparser_parse_options_default()` |
| `dialect` | SQL dialect; defaults to `SQLPARSER_DIALECT_POSTGRESQL` |
| `limits` | resource limits |
| `flags` | reserved; keep as `0` |

Defined dialects:

| Dialect | Meaning |
| --- | --- |
| `SQLPARSER_DIALECT_POSTGRESQL` | default dialect |
| `SQLPARSER_DIALECT_MYSQL` | MySQL dialect conversion layer |
| `SQLPARSER_DIALECT_ORACLE` | Oracle dialect conversion layer |
| `SQLPARSER_DIALECT_SQLSERVER` | SQL Server dialect conversion layer |
| `SQLPARSER_DIALECT_DAMENG` | Dameng dialect conversion layer |

## Lifecycle and Thread Model

- Handles returned by `sqlparser_parse()` are released by
  `sqlparser_handle_destroy()`.
- Strings returned by `sqlparser_deparse()`, `sqlparser_export_view_json()`,
  and rendering APIs are released by `sqlparser_string_free()`.
- Strings inside C view structs are borrowed from the handle and must not be
  freed by the caller.
- After a successful patch or any AST mutation, previous borrowed pointers,
  selector read results, and query graph views are invalid.
- A single handle does not support concurrent read/write access and is not
  guaranteed to be safe for concurrent read-only access. Use one owning thread
  per handle.

## Version and Name Helpers

| Function | Summary |
| --- | --- |
| `sqlparser_version_string()` | returns the library version string |
| `sqlparser_libpg_query_tag()` | returns the pinned `libpg_query` tag |
| `sqlparser_statement_kind_name()` | returns the statement-kind name |
| `sqlparser_insert_source_kind_name()` | returns the `INSERT` source-kind name |
| `sqlparser_value_kind_name()` | returns the value-kind name |
| `sqlparser_bind_kind_name()` | returns the bind-kind name |
| `sqlparser_literal_kind_name()` | returns the literal-kind name |
| `sqlparser_selector_kind_name()` | returns the selector-kind name |
| `sqlparser_clause_kind_name()` | returns the clause-kind name |
| `sqlparser_graph_block_kind_name()` | returns the query graph block-kind name |
| `sqlparser_graph_relation_kind_name()` | returns the query graph relation-kind name |
| `sqlparser_graph_target_kind_name()` | returns the query graph target-kind name |
| `sqlparser_graph_value_kind_name()` | returns the query graph value-kind name |
| `sqlparser_graph_set_kind_name()` | returns the query graph set-kind name |
| `sqlparser_graph_dml_kind_name()` | returns the query graph DML-kind name |
| `sqlparser_dialect_name()` | returns the dialect name |
| `sqlparser_bool_operator_name()` | returns the boolean-operator name |

## Parse and Handle Management

| Function | Summary |
| --- | --- |
| `sqlparser_limits_default()` | fills default resource limits |
| `sqlparser_parse_options_default()` | fills default parse options |
| `sqlparser_parse()` | parses SQL with default options |
| `sqlparser_parse_with_limits()` | parses SQL with caller-provided limits |
| `sqlparser_parse_with_options()` | parses SQL with caller-provided dialect and limits |
| `sqlparser_handle_destroy()` | releases a handle |
| `sqlparser_original_sql()` | returns the original input SQL |
| `sqlparser_handle_dialect()` | returns the handle dialect |
| `sqlparser_statement_count()` | returns the number of statements |

## Statement-Level Access

| Function | Summary |
| --- | --- |
| `sqlparser_statement_kind()` | returns the logical statement kind |
| `sqlparser_statement_node_name()` | returns the underlying node name |
| `sqlparser_statement_target_relation()` | returns the primary target relation |

## Generic Reads and Fine-Grained Rewrites

### Relation

| Function | Summary |
| --- | --- |
| `sqlparser_statement_relation_count()` | returns the number of relations |
| `sqlparser_statement_relation()` | reads one relation |
| `sqlparser_statement_set_relation_name()` | rewrites the schema or table name of one relation |

### Name

| Function | Summary |
| --- | --- |
| `sqlparser_statement_name_count()` | returns the number of name atoms |
| `sqlparser_statement_name()` | reads one name atom |
| `sqlparser_statement_set_name()` | rewrites one name atom |

### Literal

| Function | Summary |
| --- | --- |
| `sqlparser_statement_literal_count()` | returns the number of literals |
| `sqlparser_statement_literal()` | reads one literal |
| `sqlparser_statement_set_literal()` | rewrites one literal |

### INSERT

| Function | Summary |
| --- | --- |
| `sqlparser_insert_source_kind()` | returns the `INSERT` source kind |
| `sqlparser_insert_column_count()` | returns the target-column count |
| `sqlparser_insert_column_name()` | reads one target column |
| `sqlparser_insert_row_count()` | returns the number of `VALUES` rows |
| `sqlparser_insert_cell_literal()` | reads one cell literal |
| `sqlparser_insert_set_cell_literal()` | rewrites one cell literal |
| `sqlparser_insert_cell_sql()` | reads one cell right-hand SQL |
| `sqlparser_insert_set_cell_sql()` | rewrites one cell right-hand SQL |

### SELECT Target Lists

| Function | Summary |
| --- | --- |
| `sqlparser_select_target_list_count()` | returns the number of SELECT target lists in a statement |
| `sqlparser_select_target_count()` | returns the number of targets in one target list |
| `sqlparser_select_target_sql()` | reads one output target as SQL |
| `sqlparser_select_set_target_sql()` | replaces one output target SQL |
| `sqlparser_select_set_targets_sql()` | replaces the full SELECT output list |
| `sqlparser_select_insert_target_sql()` | inserts one output target |
| `sqlparser_select_delete_target()` | deletes one output target |

`target_list_index` distinguishes multiple `SelectStmt` nodes in one statement,
such as subqueries, CTEs, or set-operation branches.

### UPDATE and WHERE

| Function | Summary |
| --- | --- |
| `sqlparser_update_assignment_count()` | returns the number of `SET` assignments |
| `sqlparser_update_assignment()` | reads one assignment |
| `sqlparser_update_set_assignment_literal()` | rewrites an assignment right-hand literal |
| `sqlparser_update_assignment_sql()` | reads an assignment right-hand SQL |
| `sqlparser_update_set_assignment_sql()` | rewrites an assignment right-hand SQL |
| `sqlparser_update_insert_assignment_sql()` | inserts a full `SET` assignment |
| `sqlparser_update_delete_assignment()` | deletes one `SET` assignment |
| `sqlparser_update_set_assignment_full_sql()` | replaces a full `SET` assignment |
| `sqlparser_statement_where_literal_count()` | returns the number of WHERE literals |
| `sqlparser_statement_where_literal()` | reads one WHERE literal |
| `sqlparser_statement_where_set_literal()` | rewrites one WHERE literal |
| `sqlparser_statement_where_count()` | returns writable WHERE slot count |
| `sqlparser_statement_where_sql()` | reads one WHERE condition SQL |
| `sqlparser_statement_set_where_sql()` | sets or replaces one WHERE condition SQL |
| `sqlparser_statement_append_where_sql()` | appends one WHERE condition with `AND` or `OR` |

### Generic Clauses

| Function | Summary |
| --- | --- |
| `sqlparser_statement_clause_count()` | returns writable statement-level clause count |
| `sqlparser_statement_clause()` | reads one clause view |
| `sqlparser_statement_clause_sql()` | reads one clause SQL |
| `sqlparser_statement_set_clause_sql()` | sets or replaces one clause SQL |
| `sqlparser_statement_append_clause_condition()` | appends a condition to a `where` clause |
| `sqlparser_clause_sql()` | renders SQL from `sqlparser_clause_view_t` |

Generic clause APIs are for structural rewrites. Field, value, and lineage
attribution is read through `query_graph`.

## Selector APIs

A selector represents a readable or writable target as a stable text path or a
structured selector object.

Common selector forms:

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

### Selector Parse and Read

| Function | Summary |
| --- | --- |
| `sqlparser_selector_parse()` | parses text into `sqlparser_selector_t` |
| `sqlparser_selector_format()` | formats `sqlparser_selector_t` as text |
| `sqlparser_selector_relation()` | reads a relation |
| `sqlparser_selector_name()` | reads a name atom |
| `sqlparser_selector_literal()` | reads a literal |
| `sqlparser_selector_where_literal()` | reads a WHERE literal |
| `sqlparser_selector_where_sql()` | reads WHERE condition SQL |
| `sqlparser_selector_clause()` | reads a generic clause view |
| `sqlparser_selector_clause_sql()` | reads generic clause SQL |
| `sqlparser_selector_update_assignment()` | reads an assignment |
| `sqlparser_selector_update_assignment_sql()` | reads assignment right-hand SQL |
| `sqlparser_selector_insert_cell_literal()` | reads INSERT cell literal |
| `sqlparser_selector_insert_cell_sql()` | reads INSERT cell right-hand SQL |
| `sqlparser_selector_select_target_sql()` | reads SELECT output target SQL |

### Selector Rewrite

| Function | Summary |
| --- | --- |
| `sqlparser_selector_set_relation_name()` | rewrites a relation name |
| `sqlparser_selector_set_name()` | rewrites a name atom |
| `sqlparser_selector_set_literal()` | rewrites a literal |
| `sqlparser_selector_set_where_literal()` | rewrites a WHERE literal |
| `sqlparser_selector_set_where_sql()` | sets or replaces WHERE condition SQL |
| `sqlparser_selector_append_where_sql()` | appends a WHERE condition |
| `sqlparser_selector_set_clause_sql()` | sets or replaces a generic clause |
| `sqlparser_selector_append_clause_condition()` | appends a condition to a `where` clause |
| `sqlparser_selector_set_update_assignment_literal()` | rewrites assignment right-hand literal |
| `sqlparser_selector_set_update_assignment_sql()` | rewrites assignment right-hand SQL |
| `sqlparser_selector_insert_update_assignment_sql()` | inserts a full `SET` assignment |
| `sqlparser_selector_delete_update_assignment()` | deletes a `SET` assignment |
| `sqlparser_selector_set_update_assignment_full_sql()` | replaces a full `SET` assignment |
| `sqlparser_selector_set_insert_cell_literal()` | rewrites INSERT cell literal |
| `sqlparser_selector_set_insert_cell_sql()` | rewrites INSERT cell right-hand SQL |
| `sqlparser_selector_set_select_target_sql()` | rewrites one SELECT output target |
| `sqlparser_selector_set_select_targets_sql()` | rewrites a full SELECT output list |

## query_graph C Traversal

`query_graph` is the minimal structure view for field attribution, output order,
DML writes, and value binding. It does not read database metadata, distinguish
tables from views, expand `*`, or store per-node SQL text.

### Entry Point

```c
sqlparser_status_t sqlparser_statement_query_graph(
    const sqlparser_handle_t *handle,
    size_t statement_index,
    sqlparser_query_graph_view_t *out_graph,
    sqlparser_error_t *out_error);
```

`sqlparser_query_graph_view_t` contains statement-local counts and root block
information. It does not own memory and is valid only for the handle generation
from which it was read.

### Read Functions

| Function | Summary |
| --- | --- |
| `sqlparser_query_graph_span_index_at()` | reads the Nth global index from a span |
| `sqlparser_query_graph_block_at()` | reads a query block |
| `sqlparser_query_graph_relation_at()` | reads a relation |
| `sqlparser_query_graph_target_at()` | reads a SELECT target |
| `sqlparser_query_graph_field_at()` | reads a field occurrence |
| `sqlparser_query_graph_value_at()` | reads a field-bound value |
| `sqlparser_query_graph_set_at()` | reads a set-operation node |
| `sqlparser_query_graph_dml()` | reads DML write shape |
| `sqlparser_query_graph_dml_column_at()` | reads one INSERT target column |
| `sqlparser_query_graph_dml_cell_at()` | reads one INSERT VALUES cell |
| `sqlparser_query_graph_dml_assignment_at()` | reads one UPDATE/MERGE assignment |

### Main Structs

| Struct | Meaning |
| --- | --- |
| `sqlparser_graph_block_t` | query block with relation and target spans |
| `sqlparser_graph_relation_t` | base, derived, CTE, or dual relation visible in SQL |
| `sqlparser_graph_target_t` | SELECT output target with output order, star source, and selector |
| `sqlparser_graph_field_t` | field-reference occurrence visible in SQL |
| `sqlparser_graph_value_t` | literal, bind, default, or expression associated with a field |
| `sqlparser_graph_set_t` | `UNION`, `UNION ALL`, `INTERSECT`, or `EXCEPT/MINUS` branches |
| `sqlparser_graph_dml_t` | INSERT, UPDATE, DELETE, or MERGE write shape |
| `sqlparser_graph_dml_column_t` | explicit INSERT target column |
| `sqlparser_graph_dml_cell_t` | INSERT VALUES cell |
| `sqlparser_graph_dml_assignment_t` | UPDATE/MERGE assignment |

### Attribution Rules

- `relations[].source_block_index` links a derived table or CTE to its source
  block.
- `targets[].star_relations` reports the relation indexes covered by `*` or
  `alias.*`.
- `targets[].source_block_index` links star or subquery targets to their source
  block.
- `sets[].branch_blocks` reports set-operation branches.
- If an unqualified field cannot be uniquely attributed from SQL text alone,
  `has_relation` is `0` and `candidate_relations` lists relation candidates in
  the current scope.
- `values[]` contains only application-side values associated with fields.
  `LIMIT/OFFSET`, `ROWNUM`, and other pagination or pseudo-column binds are
  intentionally excluded.

## JSON Export and Patch

| Function | Summary |
| --- | --- |
| `sqlparser_export_view_json()` | exports View JSON on demand |
| `sqlparser_apply_patch()` | applies structured patches |

`sqlparser_export_view_json()` `pretty` values:

- `0`: compact JSON
- non-zero: formatted JSON

`sqlparser_apply_patch()` accepts `sqlparser_patch_list_t`. Each patch uses a
selector to address a writable node.

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

Patch operations:

| Operation | Meaning |
| --- | --- |
| `SQLPARSER_PATCH_REPLACE` | replaces a relation, name, value, assignment, literal, where literal, clause, insert cell, select target, or select target list |
| `SQLPARSER_PATCH_INSERT_COLUMN` | adds an `INSERT ... VALUES` column or inserts a SELECT output target |
| `SQLPARSER_PATCH_DELETE_COLUMN` | deletes an `INSERT ... VALUES` column or deletes a SELECT output target |
| `SQLPARSER_PATCH_DELETE_ROW` | deletes an `INSERT ... VALUES` row |
| `SQLPARSER_PATCH_APPEND_CONDITION` | appends a condition to a `where` clause with `AND` or `OR` |
| `SQLPARSER_PATCH_INSERT_ASSIGNMENT` | inserts an `UPDATE SET` assignment |
| `SQLPARSER_PATCH_DELETE_ASSIGNMENT` | deletes an `UPDATE SET` assignment |
| `SQLPARSER_PATCH_REPLACE_ASSIGNMENT` | replaces a full `UPDATE SET` assignment |

After a patch succeeds, the handle generation increases and any previous query
graph view becomes invalid.

## Deparse and String Free

| Function | Summary |
| --- | --- |
| `sqlparser_deparse()` | deparses the current AST into SQL |
| `sqlparser_string_free()` | releases strings returned by the library |

## Common Usage Patterns

### Field and Value Attribution

1. Call `sqlparser_statement_query_graph()`.
2. Traverse `relations`, `fields`, `values`, and DML structures.
3. Combine the result with caller-owned metadata for field-policy matching.

### Selector-Driven Rewrite

1. Read selectors from query graph or View JSON.
2. Build `sqlparser_patch_t`.
3. Call `sqlparser_apply_patch()`.
4. Call `sqlparser_deparse()`.
5. Parse the generated SQL again with the same dialect to validate syntax.

### SELECT Target-List Rewrite

1. Use `query_graph.targets[]` to locate the target or target-list selector.
2. Use `sqlparser_apply_patch()` or SELECT target APIs to add, delete, or
   replace output targets.
3. Deparse and reparse to validate the result.

## Related Examples

| Example | Description |
| --- | --- |
| `examples/patch/08_view_patch.c` | View JSON export, patch replay, and SQL regeneration |
| `examples/patch/13_select_target_patch.c` | `SELECT *` expansion, output-target insertion, and output-target deletion through patches |
| `examples/patch/14_where_patch.c` | WHERE insertion and condition append through patches |
| `examples/patch/15_insert_columns_patch.c` | `INSERT ... VALUES` column insertion and deletion through patches |
| `examples/patch/16_clause_patch.c` | SELECT output-list, WHERE, and ORDER BY rewrite through generic clause patches |
| `examples/patch/17_update_set_patch.c` | `UPDATE SET` assignment append, delete, and full-assignment replacement through patches |
| `examples/inspect/01_select_inspect.c` | `SELECT` inspection and multi-relation extraction |
| `examples/inspect/03_insert_select_inspect.c` | structural inspection for `INSERT ... SELECT` |
| `examples/inspect/07_multi_statement_walk.c` | traversal of multi-statement input |
| `examples/dialect/10_mysql_dialect.c` | MySQL dialect parsing and patch-based rewrite |
| `examples/dialect/11_oracle_dialect.c` | Oracle dialect parsing and rewrite |
| `examples/dialect/12_sqlserver_dialect.c` | SQL Server dialect parsing and deparse |
| `examples/dialect/17_dameng_dialect.c` | Dameng dialect parsing and deparse |
