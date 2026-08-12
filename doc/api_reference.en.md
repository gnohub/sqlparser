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
| `sqlparser_graph_field_match_kind_t` | query graph field-match kind for condition values |
| `sqlparser_graph_operator_kind_t` | structured operator classification for query graph values |
| `sqlparser_graph_like_escape_kind_t` | escape shape for explicit `LIKE ... ESCAPE ...` in query graph |
| `sqlparser_graph_predicate_kind_t` | query graph predicate node kind |
| `sqlparser_graph_predicate_bool_t` | query graph boolean predicate operator |
| `sqlparser_graph_session_action_t` | session-state action kind |
| `sqlparser_graph_session_scope_t` | session-state scope |
| `sqlparser_graph_session_target_kind_t` | session-state target kind |
| `sqlparser_graph_session_value_kind_t` | session-state value kind |
| `sqlparser_control_node_kind_t` | control-flow node kind |
| `sqlparser_control_item_kind_t` | control-flow item reference kind |
| `sqlparser_graph_dml_result_kind_t` | DML result-channel kind |
| `sqlparser_graph_dml_reference_kind_t` | DML result-field source kind |
| `sqlparser_literal_kind_t` | literal kind |
| `sqlparser_selector_kind_t` | selector kind |
| `sqlparser_clause_kind_t` | clause kind used by query graph and clause patches |
| `sqlparser_dialect_t` | SQL dialect |

`sqlparser_identifier_path_view_t` passes an identifier path into structured
rewrite APIs.

| Field | Meaning |
| --- | --- |
| `parts` | identifier parts owned by the caller and read only during the call |
| `part_count` | number of identifier parts; must be greater than `0` |

`part_count = 1` means a single column name, such as `phone_backup`;
`part_count = 2` means a qualified column name, such as `u.phone`; longer paths
are deparsed according to the current handle dialect. Each part must be
non-empty, and callers do not pass quote characters.

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

`sqlparser_graph_value_kind_t`:

| Enum | Value | Meaning |
| --- | --- | --- |
| `SQLPARSER_GRAPH_VALUE_LITERAL` | `1` | literal value |
| `SQLPARSER_GRAPH_VALUE_BIND` | `2` | prepared-statement placeholder |
| `SQLPARSER_GRAPH_VALUE_DEFAULT` | `3` | `DEFAULT` |
| `SQLPARSER_GRAPH_VALUE_EXPRESSION` | `4` | function, operator, `CASE`, or other expression |
| `SQLPARSER_GRAPH_VALUE_FIELD` | `5` | right-hand field reference visible in SQL, such as a field-to-field predicate, DML assignment RHS, or source-query output field |

`sqlparser_graph_like_escape_kind_t`:

| Enum | Value | Meaning |
| --- | --- | --- |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_NONE` | `0` | no explicit `ESCAPE` clause |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_LITERAL` | `1` | `ESCAPE` is a literal |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_BIND` | `2` | `ESCAPE` is a prepared-statement placeholder |
| `SQLPARSER_GRAPH_LIKE_ESCAPE_EXPRESSION` | `3` | `ESCAPE` is a function, operator, or other expression |

`sqlparser_graph_field_match_kind_t`:

| Enum | Value | Meaning |
| --- | --- | --- |
| `SQLPARSER_GRAPH_FIELD_MATCH_UNKNOWN` | `0` | no field association or no stable classification |
| `SQLPARSER_GRAPH_FIELD_MATCH_DIRECT_FIELD` | `1` | the predicate left side is a direct field, such as `secret = ?` |
| `SQLPARSER_GRAPH_FIELD_MATCH_EXPRESSION_FIELD` | `2` | the field is inside a function, cast, expression, or `CASE`, such as `UPPER(secret) = ?` |

`sqlparser_graph_operator_kind_t`:

| Enum | Value | Meaning |
| --- | --- | --- |
| `SQLPARSER_GRAPH_OPERATOR_UNKNOWN` | `0` | unclassified or not a pattern-match operator |
| `SQLPARSER_GRAPH_OPERATOR_LIKE` | `1` | `LIKE` |
| `SQLPARSER_GRAPH_OPERATOR_NOT_LIKE` | `2` | `NOT LIKE` |
| `SQLPARSER_GRAPH_OPERATOR_ILIKE` | `3` | `ILIKE` |
| `SQLPARSER_GRAPH_OPERATOR_NOT_ILIKE` | `4` | `NOT ILIKE` |

`sqlparser_graph_predicate_kind_t`:

| Enum | Value | Meaning |
| --- | --- | --- |
| `SQLPARSER_GRAPH_PREDICATE_UNKNOWN` | `0` | unclassified predicate |
| `SQLPARSER_GRAPH_PREDICATE_COMPARISON` | `1` | field-to-value or field-to-field comparison |
| `SQLPARSER_GRAPH_PREDICATE_BOOL` | `2` | `AND`, `OR`, or `NOT` boolean predicate |
| `SQLPARSER_GRAPH_PREDICATE_EXISTS` | `3` | `EXISTS` predicate |
| `SQLPARSER_GRAPH_PREDICATE_EXPRESSION` | `4` | expression predicate that cannot be safely split into field and value sides |

`sqlparser_graph_predicate_bool_t`:

| Enum | Value | Meaning |
| --- | --- | --- |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_NONE` | `0` | non-boolean predicate |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_AND` | `1` | `AND` |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_OR` | `2` | `OR` |
| `SQLPARSER_GRAPH_PREDICATE_BOOL_NOT` | `3` | `NOT` |

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
| `SQLPARSER_CLAUSE_KIND_DML_RESULT` | `dml_result` | DML result target list |
| `SQLPARSER_CLAUSE_KIND_CONDITION` | `condition` | control-flow condition expression |
| `SQLPARSER_CLAUSE_KIND_WINDOW_PARTITION` | `window_partition` | PARTITION BY list in a named window definition |
| `SQLPARSER_CLAUSE_KIND_START_WITH` | `start_with` | hierarchical-query start condition |
| `SQLPARSER_CLAUSE_KIND_CONNECT_BY` | `connect_by` | hierarchical-query recursive condition |

The hierarchical-query values append to the existing numbering:
`SQLPARSER_CLAUSE_KIND_START_WITH = 11` and
`SQLPARSER_CLAUSE_KIND_CONNECT_BY = 12`.

`sqlparser_graph_dml_result_kind_t`:

| Enum | Meaning |
| --- | --- |
| `SQLPARSER_GRAPH_DML_RESULT_CLIENT` | result channel returned to the client |
| `SQLPARSER_GRAPH_DML_RESULT_SINK` | result channel received by a sink relation or host bind |

`sqlparser_graph_dml_reference_kind_t`:

| Enum | Meaning |
| --- | --- |
| `SQLPARSER_GRAPH_DML_REFERENCE_TARGET_BEFORE` | target-row field before modification, such as SQL Server `DELETED.id` |
| `SQLPARSER_GRAPH_DML_REFERENCE_TARGET_AFTER` | target-row field after modification, such as SQL Server `INSERTED.id` |
| `SQLPARSER_GRAPH_DML_REFERENCE_SOURCE` | field from a DML source relation |

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
| `SQLPARSER_DIALECT_VASTBASE_ORACLE` | Vastbase Oracle compatibility mode |
| `SQLPARSER_DIALECT_VASTBASE_MYSQL` | Vastbase MySQL compatibility mode |
| `SQLPARSER_DIALECT_VASTBASE_POSTGRESQL` | Vastbase PostgreSQL compatibility mode |
| `SQLPARSER_DIALECT_VASTBASE_SQLSERVER` | Vastbase SQL Server compatibility mode |

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
| `sqlparser_graph_field_match_kind_name()` | returns the query graph field-match kind name |
| `sqlparser_graph_operator_kind_name()` | returns the query graph operator-kind name |
| `sqlparser_graph_set_kind_name()` | returns the query graph set-kind name |
| `sqlparser_graph_dml_kind_name()` | returns the query graph DML-kind name |
| `sqlparser_graph_predicate_kind_name()` | returns the query graph predicate-kind name |
| `sqlparser_graph_predicate_bool_name()` | returns the query graph boolean-predicate operator name |
| `sqlparser_graph_operator_is_like_pattern()` | checks whether an operator kind is `LIKE`, `NOT LIKE`, `ILIKE`, or `NOT ILIKE` |
| `sqlparser_graph_value_is_like_pattern()` | checks whether a query graph value is a pattern-match pattern value |
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

Control conditions and branch SQL are addressable statement units. A condition
unit has kind `SQLPARSER_STATEMENT_KIND_CONDITION` and node name
`ConditionExpr`; branch SQL keeps its own statement kind. Existing `stmt[n]...`
selectors can read and rewrite these units.

## Read-Only Control-Flow Traversal

`sqlparser_handle_control_flow()` returns the read-only control topology. An
ordinary SQL handle returns success with an empty view. A control statement is
represented by ordered roots, nodes, branches, and items.

| Function | Summary |
| --- | --- |
| `sqlparser_handle_control_flow()` | gets the control-flow view for a handle |
| `sqlparser_control_span_index_at()` | reads the Nth index from a control span |
| `sqlparser_control_node_at()` | reads a control node |
| `sqlparser_control_branch_at()` | reads a conditional or unconditional branch |
| `sqlparser_control_item_at()` | reads a statement or nested-node reference |

| Struct | Meaning |
| --- | --- |
| `sqlparser_control_flow_view_t` | root span, node/branch/item counts, and generation |
| `sqlparser_control_node_t` | node kind and ordered branch span; the current node kind is `SQLPARSER_CONTROL_NODE_IF` |
| `sqlparser_control_branch_t` | optional condition-statement index and ordered item span |
| `sqlparser_control_item_t` | `SQLPARSER_CONTROL_ITEM_STATEMENT` or `SQLPARSER_CONTROL_ITEM_NODE` reference |

The roots, `node.branches`, and `branch.items` fields are index-pool spans. Read
them through `sqlparser_control_span_index_at()` rather than treating `offset`
as an object index. The view borrows handle-owned memory and becomes stale when
a successful rewrite changes the handle generation.

```c
sqlparser_control_flow_view_t flow;
sqlparser_control_item_t root;
size_t root_item_index;

sqlparser_handle_control_flow(handle, &flow, &err);
sqlparser_control_span_index_at(
    &flow, flow.roots, 0, &root_item_index, &err);
sqlparser_control_item_at(&flow, root_item_index, &root, &err);
```

## Generic Reads and Fine-Grained Rewrites

### Relation

| Function | Summary |
| --- | --- |
| `sqlparser_statement_relation_count()` | returns the number of relations |
| `sqlparser_statement_relation()` | reads one relation |
| `sqlparser_statement_set_relation_name()` | rewrites the schema or table name of one relation |

For a relation without an explicit alias, a name rewrite also updates qualified column references, qualified stars, and qualified assignment targets that scope resolution binds uniquely to that relation. Explicit aliases, same-scope ambiguity, inner-scope shadowing, and SQL Server pseudo-relations such as `INSERTED` and `DELETED` remain unchanged. Existing qualifier depth may contract with a shorter relation path but does not expand when the new path is longer. A relation-selector replace follows the same rules.

### Name

| Function | Summary |
| --- | --- |
| `sqlparser_statement_name_count()` | returns the number of name atoms |
| `sqlparser_statement_name()` | reads one name atom |
| `sqlparser_statement_set_name()` | rewrites one name atom |

For an unquoted identifier derived from an input identifier token, the AST
name value retains the token text and case, so `abc` and `DDD` remain `abc`
and `DDD`. For a quoted identifier, `value` retains the decoded name text and
case without double-quote, backtick, or bracket delimiters. Delimiter and
escape-byte preservation is guaranteed only for generation-`0` deparse. The
name API exposes identifier atoms explicitly supported for reading and
rewriting; other AST string fields,
including keywords, operators, structural control values, literals, and
payloads, are not exposed as names.

### Literal

| Function | Summary |
| --- | --- |
| `sqlparser_statement_literal_count()` | returns the number of literals |
| `sqlparser_statement_literal()` | reads one literal |
| `sqlparser_statement_set_literal()` | rewrites one literal |

`sqlparser_literal_view_t.quoted_identifier` is `1` when a string literal came from a quoted-identifier token, such as the schema value in `ALTER SESSION SET CURRENT_SCHEMA="AppMixed"`. Ordinary string literals and unquoted identifiers report `0`.

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
| `sqlparser_select_set_target_sql()` | replaces one output position with complete target SQL; the previous target alias is not inherited; a multi-target fragment is spliced at that position |
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
| `sqlparser_update_set_assignment_literal()` | rewrites an assignment right-hand literal or bind to a literal |
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
stmt[0].merge_assignment[1][0]
stmt[0].merge_assignment[2][1][0]
stmt[0].merge_branch_condition[1]
stmt[0].merge_branch_condition[2][1]
stmt[0].merge_delete_condition[1]
stmt[0].merge_delete_condition[2][1]
stmt[0].merge_insert_column[1][2]
stmt[0].merge_insert_column[2][1][2]
stmt[0].merge_insert_cell[1][2]
stmt[0].merge_insert_cell[2][1][2]
stmt[0].insert_cell[1][2]
stmt[0].insert_branch_columns[0]
stmt[0].insert_branch_columns[2][1]
stmt[0].insert_branch_condition[0]
stmt[0].select_targets[0]
stmt[0].select_target[0][1]
stmt[0].dml_result_targets[0][0]
stmt[0].dml_result_target[0][0][1]
stmt[0].dml_result_sink[0][0]
stmt[0].dml_result_sink_columns[0][0]
stmt[0].dml_result_sink_column[0][0][1]
```

`stmt[S].assignment[A]` addresses assignment `A` in the top-level `UPDATE` of
statement `S`. A matched UPDATE action in the root MERGE uses
`stmt[S].merge_assignment[W][A]`; a nested MERGE uses
`stmt[S].merge_assignment[D][W][A]`. `D` is the DML index within the current
statement, `W` is the absolute zero-based ordinal across all `WHEN` clauses in
the target MERGE, not an ordinal renumbered over UPDATE actions, and `A` is the
zero-based assignment ordinal within that UPDATE branch. `W` must identify a
`WHEN MATCHED ... THEN UPDATE` clause. The selector kind is
`SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT`. A root MERGE has `row_index = 0`;
for a nested MERGE, `row_index` stores `D`. `item_index` stores `W`, and
`column_index` stores `A`.

A MERGE branch condition uses `stmt[S].merge_branch_condition[W]`; a nested
MERGE uses `stmt[S].merge_branch_condition[D][W]`. Its kind is
`SQLPARSER_SELECTOR_KIND_MERGE_BRANCH_CONDITION`, and `D` and `W` have the
same meanings as in a MERGE assignment selector. An unconditional branch has
no condition selector.

An attached `DELETE WHERE` predicate on an Oracle/Dameng matched UPDATE branch
uses `stmt[S].merge_delete_condition[W]`; a nested MERGE uses
`stmt[S].merge_delete_condition[D][W]`. Its kind is
`SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION = 25`, with the same `D` and
`W` meanings as a MERGE assignment selector. This selector addresses an
attached delete predicate on the same UPDATE action, not an independent DELETE
branch. PostgreSQL and SQL Server `WHEN MATCHED ... THEN DELETE` actions remain
independent MERGE branches with `merge_action_kind = delete`.

`sqlparser_selector_clause_sql()` returns the predicate expression without
the `DELETE WHERE` keywords. `sqlparser_selector_set_clause_sql()` and
`SQLPARSER_PATCH_REPLACE` likewise accept a predicate expression without a
`WHERE` keyword.

A target column in a MERGE INSERT action uses
`stmt[S].merge_insert_column[W][C]`, and a complete VALUES cell uses
`stmt[S].merge_insert_cell[W][C]`. Their kinds are
`SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN` and
`SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL`, respectively. The nested-MERGE
forms are
`stmt[S].merge_insert_column[D][W][C]` and
`stmt[S].merge_insert_cell[D][W][C]`. An explicit target-column list uses
`SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS` and the text form
`stmt[S].insert_branch_columns[W]`, or
`stmt[S].insert_branch_columns[D][W]` for a nested MERGE. `D` and `W` have the
same meanings as in MERGE assignment selectors, and `C` is the zero-based
column ordinal in the INSERT action. A parsed root-MERGE selector has
`row_index = 0`; for a nested MERGE, `row_index` stores `D`. `item_index`
stores `W`, and `column_index` stores `C` for an individual item. An INSERT
action without an explicit target-column list still exposes cell selectors but
has no target-column or target-list selector.

`sqlparser_selector_update_assignment()`,
`sqlparser_selector_update_assignment_sql()`, the
`sqlparser_selector_set_update_assignment_*()` and
`sqlparser_selector_insert_update_assignment_*()` families, and
`sqlparser_selector_delete_update_assignment()` accept both `assignment` and
`merge_assignment` selectors.

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
| `sqlparser_selector_clause_sql()` | reads generic clause SQL, Oracle/Dameng `INSERT ALL/FIRST` branch-condition SQL, MERGE branch-condition SQL, and MERGE attached-delete predicate SQL |
| `sqlparser_selector_update_assignment()` | reads an assignment |
| `sqlparser_selector_update_assignment_sql()` | reads assignment right-hand SQL |
| `sqlparser_selector_insert_cell_literal()` | reads INSERT cell literal |
| `sqlparser_selector_insert_cell_sql()` | reads INSERT cell right-hand SQL |
| `sqlparser_selector_select_target_sql()` | reads SELECT output target SQL |

### Selector Rewrite

| Function | Summary |
| --- | --- |
| `sqlparser_selector_set_relation_name()` | rewrites a relation name using the generic relation-binding rules |
| `sqlparser_selector_set_name()` | rewrites a name atom |
| `sqlparser_selector_set_literal()` | rewrites a literal |
| `sqlparser_selector_set_where_literal()` | rewrites a WHERE literal |
| `sqlparser_selector_set_where_sql()` | sets or replaces WHERE condition SQL |
| `sqlparser_selector_append_where_sql()` | appends a WHERE condition |
| `sqlparser_selector_set_clause_sql()` | sets or replaces a generic clause, MERGE branch condition, or MERGE attached-delete condition |
| `sqlparser_selector_append_clause_condition()` | appends a condition to a `where` clause |
| `sqlparser_selector_set_update_assignment_literal()` | rewrites assignment right-hand literal or bind to a literal |
| `sqlparser_selector_set_update_assignment_sql()` | rewrites assignment right-hand SQL |
| `sqlparser_selector_insert_update_assignment_sql()` | inserts a full `SET` assignment |
| `sqlparser_selector_insert_update_assignment_from_assignment_value()` | inserts a `SET` assignment from a structured target and a cloned assignment value |
| `sqlparser_selector_delete_update_assignment()` | deletes a `SET` assignment |
| `sqlparser_selector_set_update_assignment_full_sql()` | replaces a full `SET` assignment |
| `sqlparser_selector_set_insert_cell_literal()` | rewrites INSERT cell literal |
| `sqlparser_selector_set_insert_cell_sql()` | rewrites INSERT cell right-hand SQL |
| `sqlparser_selector_set_select_target_sql()` | replaces one SELECT output position with complete target SQL; the previous target alias is not inherited; a multi-target fragment is spliced at that position |
| `sqlparser_selector_set_select_targets_sql()` | rewrites a full SELECT output list |
| `sqlparser_selector_replace_select_target_with_columns()` | replaces one SELECT output target with structured column targets |

### Structured SQL Fragment Rewrite

`sqlparser_apply_patch()` is the recommended mutation gateway. Existing
statement, selector, and structured convenience mutation functions remain
available and retain their public argument validation. After conversion to a
patch, they share atomic rollback, handle-generation updates, and derived-cache
invalidation rules.

Structured rewrite APIs use selectors to locate their targets, render
`sqlparser_identifier_path_view_t` values and other structured inputs for the
handle dialect, then apply them through the same patch transaction. When an
existing assignment value is reused, the corresponding node is cloned on the
transaction candidate. Callers provide identifier parts and source selectors;
they do not build SQL fragments or pass quote characters.

`sqlparser_selector_insert_update_assignment_from_assignment_value()` inserts a
new `SET` assignment into a top-level `UPDATE` or MERGE matched UPDATE action.
It clones the right-hand value of the assignment pointed to by
`source_assignment_selector` and uses `target` as the new assignment left
side. Both the insertion selector and the source selector can use either
`assignment` or `merge_assignment`. Both selectors must address the same
statement; otherwise the function returns `SQLPARSER_STATUS_UNSUPPORTED`:

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

`sqlparser_update_set_assignment_literal()` and
`sqlparser_selector_set_update_assignment_literal()` replace only the
assignment right-hand value and keep the target column unchanged. A literal or
bind right-hand value can be replaced with `sqlparser_literal_value_t`;
function calls, operator expressions, field references, `DEFAULT`, and
subqueries return `SQLPARSER_STATUS_UNSUPPORTED`.

`sqlparser_selector_replace_select_target_with_columns()` replaces one SELECT
output target with multiple structured column targets. It is intended for
replacing `*` or `alias.*` with a caller-provided column list:

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

The input arrays are borrowed views. The library does not store caller pointers
inside the handle. On failure, these APIs return an error status and preserve
the original handle.

## query_graph C Traversal

`query_graph` provides structured access to query blocks, relations, output
targets, field references, predicates, DML writes, session state, and bound
values.

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
| `sqlparser_query_graph_value_at()` | reads a query graph value |
| `sqlparser_query_graph_set_at()` | reads a set-operation node |
| `sqlparser_query_graph_predicate_at()` | reads a WHERE, ON, HAVING, START WITH, or CONNECT BY predicate node |
| `sqlparser_query_graph_session()` | reads the statement session-state action |
| `sqlparser_query_graph_session_item_at()` | reads a session-state target |
| `sqlparser_query_graph_session_value_at()` | reads a session-state value |
| `sqlparser_query_graph_dml()` | reads DML index 0 for the current statement |
| `sqlparser_query_graph_dml_count()` | reads the DML count for the current statement |
| `sqlparser_query_graph_dml_at()` | reads a DML by zero-based index |
| `sqlparser_query_graph_dml_parent()` | reads the parent DML index for a nested DML |
| `sqlparser_query_graph_dml_result_count()` | reads the result-channel count for a DML |
| `sqlparser_query_graph_dml_result_at()` | reads a result channel for a DML |
| `sqlparser_query_graph_dml_reference_at()` | reads a field-source relation from a result channel |
| `sqlparser_query_graph_dml_branch_at()` | reads a DML branch, including an Oracle/Dameng multi-table INSERT branch or a MERGE `WHEN` branch |
| `sqlparser_query_graph_merge_branch_detail()` | reads a MERGE branch action, match kind, and branch-assignment span; returns `SQLPARSER_STATUS_UNSUPPORTED` for a non-MERGE branch |
| `sqlparser_query_graph_dml_column_at()` | reads one INSERT target column |
| `sqlparser_query_graph_dml_cell_at()` | reads one INSERT VALUES cell |
| `sqlparser_query_graph_dml_assignment_at()` | reads one UPDATE/MERGE assignment |

### Main Structs

| Struct | Meaning |
| --- | --- |
| `sqlparser_graph_block_t` | query block with relation, target, and predicate spans |
| `sqlparser_graph_relation_t` | base, derived, CTE, or dual relation visible in SQL; `quoted_identifier` reports an explicit supported delimiter on the object-name token |
| `sqlparser_graph_target_t` | query or DML-result output target with output order, star source, selector, and an optional sink-value association |
| `sqlparser_graph_field_t` | field-reference occurrence visible in SQL; `quoted_identifier` reports an explicit supported delimiter on the column-name token, while `pseudo` / `prior` report hierarchical occurrence semantics |
| `sqlparser_graph_value_t` | literal, bind, default, expression, or field value in the query graph |
| `sqlparser_graph_set_t` | `UNION`, `UNION ALL`, `INTERSECT`, or `EXCEPT/MINUS` branches |
| `sqlparser_graph_predicate_t` | comparison, boolean, EXISTS, or expression predicate from WHERE, ON, HAVING, START WITH, or CONNECT BY; `nocycle` marks the CONNECT BY root predicate |
| `sqlparser_graph_session_t` | statement session-state action and item count |
| `sqlparser_graph_session_item_t` | session-state scope, target, and value span |
| `sqlparser_graph_session_value_t` | session-state identifier, keyword, literal, bind, or expression value |
| `sqlparser_graph_dml_t` | INSERT, UPDATE, DELETE, or MERGE write shape |
| `sqlparser_graph_dml_result_t` | DML result channel, output block, optional relation and columns for a relation-backed sink, and a field-reference span |
| `sqlparser_graph_dml_reference_t` | one result-target reference to a target-row or source-relation field |
| `sqlparser_graph_dml_branch_t` | common DML-branch shape with target relation, target columns, rows, branch condition, optional MERGE attached-delete condition, and branch ordinal |
| `sqlparser_graph_dml_column_t` | explicit INSERT target column |
| `sqlparser_graph_dml_cell_t` | INSERT VALUES cell; Oracle/Dameng multi-table INSERT cells can link to trailing source-query output through `source_target_index` |
| `sqlparser_graph_dml_assignment_t` | UPDATE/MERGE assignment |

### Attribution Rules

- `sqlparser_graph_relation_t.quoted_identifier` applies only to `object_name`,
  and `sqlparser_graph_field_t.quoted_identifier` applies only to
  `column_name`. It is `1` when the exact source token uses `"..."`, MySQL
  backticks, or SQL Server `[...]`, and `0` otherwise. The flag does not
  classify the delimiter kind or describe database, schema, or alias state.
- `sqlparser_graph_relation_t.link_name` reports the database link for remote
  object references. It is `NULL` when the SQL has no database link.
- `relations[].source_block_index` links a derived table or CTE to its source
  block.
- A CTE definition is built once. Multiple references share its
  `source_block_index`, and an unreferenced CTE definition remains present in
  the graph.
- `targets[].star_relations` reports the relation indexes covered by `*` or
  `alias.*`.
- `targets[].source_block_index` links star or subquery targets to their source
  block.
- `sets[].branch_blocks` reports set-operation branches.
- `predicates[]` represents the predicate tree for `WHERE`, `ON`, `HAVING`,
  `START WITH`, and `CONNECT BY`;
  the `children` span reports child predicates for `AND`, `OR`, and `NOT`.
- Hierarchical queries reuse `fields[]`, `values[]`, and `predicates[]`.
  Occurrences in `START WITH` and `CONNECT BY` use
  `SQLPARSER_CLAUSE_KIND_START_WITH` and `SQLPARSER_CLAUSE_KIND_CONNECT_BY`.
  Condition-related occurrences in one SELECT are built in the semantic order
  `WHERE`, `START WITH`, `CONNECT BY`, `GROUP BY` / `HAVING`, window clauses,
  then `ORDER BY`. Selectors still come from generic AST descriptor traversal;
  callers must treat them as opaque locator paths and must not derive their
  indexes from source clause order.
- In a query block that contains `CONNECT BY`, unquoted `LEVEL`,
  `CONNECT_BY_ISLEAF`, and `CONNECT_BY_ISCYCLE` occurrences set
  `sqlparser_graph_field_t.pseudo = 1` and have no relation. A corresponding
  SELECT pseudo target points back through `field_index`. Delimited `"LEVEL"`
  and blocks without `CONNECT BY` retain ordinary field semantics, and nested
  SELECT blocks do not inherit the outer hierarchy context.
- `PRIOR` transparently marks every field occurrence in its operand through
  `sqlparser_graph_field_t.prior = 1` and is valid only in the current
  `CONNECT BY` condition. A `CONNECT_BY_ROOT` SELECT target remains
  `SQLPARSER_GRAPH_TARGET_EXPRESSION`; its underlying field has one
  `target_path` entry with `kind = "operator"`, `name = "CONNECT_BY_ROOT"`,
  and `arg_index = 0`.
- `CONNECT BY NOCYCLE` sets `sqlparser_graph_predicate_t.nocycle = 1` on the
  CONNECT BY root predicate. This capability adds no hierarchy-specific
  object, selector, patch kind, or public function. Field, value, relation,
  and SELECT-target rewrites continue to use existing selectors and patch
  kinds.
- A `field = literal/bind` predicate is represented by
  `left_field_index + value_index`. A `field = field` predicate is represented
  by `left_field_index + right_field_index`, with a `values[]` entry whose kind
  is `SQLPARSER_GRAPH_VALUE_FIELD` for the right-side source field.
- If a field reference cannot be uniquely attributed from SQL text alone,
  `has_relation` is `0` and `candidate_relations` lists relation candidates in
  the current scope.
- `sqlparser_graph_dml_t.insert_mode` distinguishes `VALUES`, `SELECT`,
  `INSERT ALL`, `INSERT FIRST`, MySQL `INSERT ... SET`, and the MySQL `REPLACE`
  `VALUES`, `SELECT`, and `SET` forms.
- `sqlparser_query_graph_dml_count()` and `sqlparser_query_graph_dml_at()`
  traverse every DML node in one statement. `sqlparser_query_graph_dml()` is a
  compatibility shorthand for index 0. Multiple parentless DML nodes can
  coexist; use `sqlparser_query_graph_dml_parent()` to distinguish roots from
  nested nodes.
- `sqlparser_query_graph_dml_parent()` reports nested DML parentage. A DML
  without a parent returns `out_has_parent = 0`.
- `sqlparser_graph_dml_result_t.kind` distinguishes client and sink channels.
  A sink can be received by a relation or a host bind. Only a relation-backed
  sink sets `has_sink_relation = 1` and uses `sink_relation_index` and optional
  `sink_columns` to identify its destination.
- A host-bind sink has no relation association. Its corresponding
  `sqlparser_graph_target_t` sets `has_sink_value = 1`, making
  `sink_value_index` valid for `sqlparser_query_graph_value_at()`. The existing
  `selector` on that `sqlparser_graph_value_t` can be used with
  `SQLPARSER_PATCH_REPLACE`; no new selector kind is introduced.
- Read indexes from `sqlparser_graph_dml_result_t.references` with
  `sqlparser_query_graph_span_index_at()`, then pass each index to
  `sqlparser_query_graph_dml_reference_at()`. Each reference links one result
  target to a `target_before`, `target_after`, or `source` field origin.
- DML result targets use `stmt[S].dml_result_target[D][C][T]`. The target list,
  sink relation, sink-column list, and individual sink columns use
  `dml_result_targets`, `dml_result_sink`, `dml_result_sink_columns`, and
  `dml_result_sink_column`, respectively.
- `sqlparser_graph_dml_t.branches` is used by Oracle/Dameng multi-table INSERT
  and MERGE. Each branch owns its target relation, target columns, rows, branch
  kind, and optional condition selector. The condition selector can be passed
  to `sqlparser_selector_clause_sql()` to read the original predicate SQL. An
  Oracle/Dameng matched UPDATE branch can additionally set
  `has_delete_condition_selector = 1` and expose `delete_condition_selector`
  for an attached `DELETE WHERE` predicate on that same UPDATE action.
- For a successfully parsed MERGE, each `WHEN` clause has one branch whose `ordinal` is the absolute
  zero-based ordinal across all `WHEN` clauses. Read its action, match kind,
  and assignment span with `sqlparser_query_graph_merge_branch_detail()`. An
  INSERT cell uses that absolute `WHEN` ordinal as `row_index` and its
  zero-based VALUES position as `column_ordinal`; when the target column list
  is omitted, `target_columns` can be empty while `rows` is nonempty. An UPDATE
  assignment appears in both the parent DML assignment span and the branch
  detail assignment span, with both spans referencing the same assignment
  index. DELETE and NOTHING actions carry no target columns, rows, or
  assignments.
- For an Oracle/Dameng multi-table INSERT branch cell that directly references an
  output field from the trailing source query, `sqlparser_graph_dml_cell_t.kind`
  is `SQLPARSER_GRAPH_VALUE_FIELD`, and
  `has_source_target/source_target_index` points to the related `targets[]`
  entry.
- If an `UPDATE` or `MERGE` assignment right-hand side is a direct field
  reference, `sqlparser_graph_dml_assignment_t.value_kind` is
  `SQLPARSER_GRAPH_VALUE_FIELD` and `has_source_field/source_field_index`
  points to the source field. If that source field uniquely matches a derived
  source-query output, `has_source_target/source_target_index` is set as well.
- The `rhs_fields`, `rhs_values`, and `rhs_blocks` spans are used only when
  `sqlparser_graph_dml_assignment_t.value_kind` is
  `SQLPARSER_GRAPH_VALUE_EXPRESSION`. `rhs_fields` and `rhs_values` own field
  and value occurrences from the right-hand expression in the current
  assignment block. `rhs_blocks` owns entry blocks for subqueries reachable
  from that expression without crossing another subquery boundary. Read every
  span index with `sqlparser_query_graph_span_index_at()`, then pass it to
  `sqlparser_query_graph_field_at()`, `sqlparser_query_graph_value_at()`, or
  `sqlparser_query_graph_block_at()` as appropriate. Traverse an entry block
  to reach the subquery's internal semantics.
- For `SQLPARSER_GRAPH_VALUE_FIELD`, `SQLPARSER_GRAPH_VALUE_LITERAL`,
  `SQLPARSER_GRAPH_VALUE_BIND`, and `SQLPARSER_GRAPH_VALUE_DEFAULT`, the
  assignment retains its existing payload and all three `rhs_*` spans have a
  `count` of `0`.
- `values[]` contains application-side values associated with fields or SELECT
  targets, together with literal, bind, and default occurrences owned by
  `rhs_values` for a compound DML assignment. A value owned only through
  `rhs_values` need not have an associated field. `LIMIT/OFFSET`, `ROWNUM`, and
  other pagination or pseudo-column binds remain excluded.
- `sqlparser_graph_value_t.field_match_kind` is meaningful only when
  `has_field` is true. It distinguishes direct-field predicates such as
  `secret = ?` from expression-field predicates such as `UPPER(secret) = ?`.
- `sqlparser_graph_value_t.operator_kind` is a structured classification based
  on the normalized operator. Callers can use
  `sqlparser_graph_value_is_like_pattern()` or the enum value to check
  pattern-match semantics without comparing `operator_name` strings.
- If the field side contains multiple attributable fields, each field gets a
  separate `expression_field` value relation.
- For a predicate whose value side is a function, cast, operator, array, row,
  or CASE expression, the related field value uses
  `SQLPARSER_GRAPH_VALUE_EXPRESSION`; inner binds and literals from that
  predicate expression are not exposed as direct values. This restriction does
  not apply to a compound DML assignment right-hand expression, whose inner
  values are owned through `rhs_values`.
- When `LIKE`, `NOT LIKE`, `ILIKE`, or `NOT ILIKE` has an explicit `ESCAPE`,
  the pattern `sqlparser_graph_value_t.like_escape` stores the escape shape.
  Without an explicit `ESCAPE`, the kind is `SQLPARSER_GRAPH_LIKE_ESCAPE_NONE`.
  Deparse output keeps the public SQL form, for example
  `LIKE pattern ESCAPE escape`.

### Session State

Supported database, schema, role, identity, transaction-characteristic, and
session-parameter statements are projected into
`sqlparser_graph_session_t`. When the current statement has no available
session projection, `sqlparser_query_graph_session()` returns success with
action `SQLPARSER_GRAPH_SESSION_ACTION_UNKNOWN` and `item_count = 0`.

When `sqlparser_graph_session_value_t.kind` is
`SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER`, `literal.quoted_identifier` uses
the same exact-token rule to report delimiter presence. Ordinary single-quoted
strings and quote styles generated internally for dialect compatibility do not
set the flag. Query-graph strings and structures remain owned by the handle and
must not be freed by the caller.

```c
sqlparser_query_graph_view_t graph;
sqlparser_graph_session_t session;
sqlparser_graph_session_item_t item;
sqlparser_graph_session_value_t value;
size_t item_index;
size_t value_index;

sqlparser_statement_query_graph(handle, 0, &graph, &err);
sqlparser_query_graph_session(&graph, &session, &err);

for (item_index = 0; item_index < session.item_count; item_index++) {
    sqlparser_query_graph_session_item_at(
        &graph, item_index, &item, &err);

    for (value_index = 0; value_index < item.value_count; value_index++) {
        sqlparser_query_graph_session_value_at(
            &graph, item.value_offset + value_index, &value, &err);
    }
}
```

`action` reports operations such as `set`, `reset`, `switch`, and `discard`.
An item's `scope` is `session`, `local`, or `transaction`; `target_kind`
identifies targets such as a parameter, database, schema, or role. `name` can
be an explicit SQL name or a canonical semantic name such as `timezone` or
`search_path`; it is `NULL` when unavailable, and a canonical name need not
appear verbatim in the SQL.

Each value can also carry an optional `name` that labels a value with distinct
semantics inside the same item. For example, the second value in MySQL
`SET NAMES ... COLLATE ...` uses `name = "collation"`. The field is `NULL`
when no distinct semantic label is available.

Use the remaining value fields according to `kind`:

| `kind` | Valid fields |
| --- | --- |
| `SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER` | `text` |
| `SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD` | `text` |
| `SQLPARSER_GRAPH_SESSION_VALUE_LITERAL` | `literal` |
| `SQLPARSER_GRAPH_SESSION_VALUE_BIND` | `bind_key`, `bind_kind`, `bind_sql`, `bind_position`, `has_bind_position` |
| `SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION` | `text` |

`bind_position` starts at `1` and follows SQL occurrence order across all
statements in the same handle. Borrowed string pointers in the returned
structures, and graph data referenced by an item's value span, remain valid
only while the owning query graph view remains valid.

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
| `SQLPARSER_PATCH_REPLACE` | replaces a relation, name, value, assignment, literal, where literal, clause, MERGE branch condition, MERGE attached-delete condition, insert cell, MERGE INSERT target column or complete cell, select target, or select target list |
| `SQLPARSER_PATCH_INSERT_COLUMN` | adds an `INSERT ... VALUES` column, adds an `INSERT ... SELECT` target column, adds an Oracle/Dameng `INSERT ALL/FIRST` branch target column, atomically adds a MERGE INSERT target/value pair, inserts a SELECT output target, or inserts a target/receiver pair into a paired DML result list |
| `SQLPARSER_PATCH_DELETE_COLUMN` | deletes an `INSERT ... VALUES` column, deletes an `INSERT ... SELECT` target column, atomically deletes a MERGE INSERT target/value pair, or deletes a SELECT output target |
| `SQLPARSER_PATCH_DELETE_ROW` | deletes an `INSERT ... VALUES` row |
| `SQLPARSER_PATCH_APPEND_CONDITION` | appends a condition to a `where` clause with `AND` or `OR` |
| `SQLPARSER_PATCH_INSERT_ASSIGNMENT` | inserts a `SET` assignment into a top-level `UPDATE` or MERGE matched UPDATE action |
| `SQLPARSER_PATCH_DELETE_ASSIGNMENT` | deletes a `SET` assignment from a top-level `UPDATE` or MERGE matched UPDATE action |
| `SQLPARSER_PATCH_REPLACE_ASSIGNMENT` | replaces a full `SET` assignment in a top-level `UPDATE` or MERGE matched UPDATE action |

`sqlparser_apply_patch()` commits and increments the generation once only when
the candidate produces an actual change from the current handle; previous
query graph views then become invalid. An empty patch list or effective no-op
does not increment the generation, and failure of any patch leaves the whole
list uncommitted.

All three assignment patch operations accept either
`stmt[S].assignment[A]` or `stmt[S].merge_assignment[W][A]` as their target
selector.

Use a `merge_insert_column` or `merge_insert_cell` selector with
`SQLPARSER_PATCH_REPLACE` for an individual MERGE INSERT rewrite. A target
column replacement takes its identifier from `sql`; a complete cell takes its
new value from exactly one of `sql`, `source_selector`, `literal`, or `bind`.
To insert a target/value pair, use an `insert_branch_columns` selector with
`SQLPARSER_PATCH_INSERT_COLUMN`, set the position in `index`, set the target
column in `name`, and provide the corresponding value through exactly one of
`default_sql`, `source_selector`, `literal`, or `bind`. Use the same selector
and `index` with `SQLPARSER_PATCH_DELETE_COLUMN` to delete a pair. Both
operations update the target-column and VALUES lists atomically. They fail for
mismatched list lengths, invalid indexes, or an INSERT action without an
explicit target-column list.

For a DML result channel with an explicit paired receiver list, target the
`dml_result_targets` list selector with `SQLPARSER_PATCH_INSERT_COLUMN`.
`index` is the common insertion position in both lists, `default_sql` supplies
the new target SQL, and `name` supplies its receiver. The receiver is a colon
bind for Oracle, Dameng, and Vastbase-Oracle compatibility mode, and an
explicit sink column for SQL Server and Vastbase SQL Server compatibility
mode. `sqlparser_apply_patch()` inserts both sides atomically in one
transaction. Unequal list lengths, an invalid index or receiver, or an invalid
payload-field combination fails without changing the handle.

The value-source fields in `sqlparser_patch_t` are mutually exclusive for one
rewrite position: provide only one of `sql`, `default_sql`, `source_selector`,
`literal`, or `bind`. `source_selector` clones SQL from an existing
`insert_cell`, `merge_insert_cell`, `select_target`, or assignment; assignment
cloning accepts both `assignment` and `merge_assignment` selectors. `literal`
and `bind` are rendered by the library according to the handle dialect.

## Deparse and String Free

| Function | Summary |
| --- | --- |
| `sqlparser_deparse()` | deparses the current AST into SQL |
| `sqlparser_string_free()` | releases strings returned by the library |

When `sqlparser_deparse()` succeeds and the handle generation is `0`, it
returns the input SQL byte for byte, including identifier quoting, case,
keywords, whitespace, line breaks, comments, semicolons, and multi-statement
boundaries. When the generation is greater than `0`, SQL is generated from the
current handle state and no blanket byte-for-byte guarantee applies. If a
full-AST deparse is required, original whitespace, case, and `ROW` / `ROWS`
spelling may be canonicalized, but modeled pagination families remain valid
for the selected dialect.

## Common Usage Patterns

### Field and Value Attribution

1. Call `sqlparser_statement_query_graph()`.
2. Traverse `relations`, `fields`, `values`, and DML structures.
3. Apply caller-defined rules to the structured fields.

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
| `examples/patch/19_oracle_multi_insert_patch.c` | Oracle `INSERT ALL` branch column and value rewrites through patches |
| `examples/convenience/18_structured_fragment_rewrite.c` | structured UPDATE assignment insertion and SELECT `*` expansion |
| `examples/inspect/01_select_inspect.c` | `SELECT` inspection and multi-relation extraction |
| `examples/inspect/03_insert_select_inspect.c` | structural inspection for `INSERT ... SELECT` |
| `examples/inspect/07_multi_statement_walk.c` | traversal of multi-statement input |
| `examples/dialect/10_mysql_dialect.c` | MySQL dialect parsing and patch-based rewrite |
| `examples/dialect/11_oracle_dialect.c` | Oracle dialect parsing and rewrite |
| `examples/dialect/12_sqlserver_dialect.c` | SQL Server `IF ... ELSE` control flow, DML result channels, and deparse |
| `examples/dialect/17_dameng_dialect.c` | Dameng dialect parsing and deparse |
| `examples/dialect/20_vastbase_dialect.c` | Vastbase compatibility-mode parsing and deparse |
