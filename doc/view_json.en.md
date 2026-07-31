# View JSON Guide

View JSON is the on-demand JSON serialization of statement query graphs and control-flow topology. It is intended for regression tests, integration checks, and language-neutral inspection. Production code should prefer the public C structs and does not need to generate JSON before rewriting SQL.

## Export API

```c
sqlparser_status_t sqlparser_export_view_json(
    const sqlparser_handle_t *handle,
    int pretty,
    char **out_json,
    sqlparser_error_t *out_error);
```

- `handle` must come from a successful parse.
- `pretty != 0` produces formatted JSON; `pretty == 0` produces compact JSON.
- `out_json` is allocated by the library and must be released with `sqlparser_string_free()`.
- JSON is generated only when this function is called. Parsing does not build a JSON string by default.

## Top-Level Shape

```json
{
  "statements": [
    {
      "index": 0,
      "keyword": "select",
      "query_graph": {
        "root": 0,
        "blocks": [
          {
            "kind": "select",
            "relations": [0],
            "targets": [0]
          }
        ],
        "relations": [
          {
            "block": 0,
            "kind": "base",
            "table": "users"
          }
        ],
        "targets": [
          {
            "block": 0,
            "ordinal": 0,
            "kind": "field",
            "name": "id",
            "field": 0
          }
        ],
        "fields": [
          {
            "block": 0,
            "clause": "select_list",
            "relation": 0,
            "column": "id",
            "target": 0
          }
        ]
      }
    }
  ]
}
```

Each statement contains:

| Field | Description |
| --- | --- |
| `index` | Zero-based statement index |
| `keyword` | Main statement keyword |
| `query_graph` | Structured graph for this statement |

The query graph represents query blocks, relations, output targets, field references, values, set operations, and DML write structures found in the SQL.

The top-level `control_flow` member is present only for control statements. It
describes branch and nesting relationships between addressable statement units.

JSON only emits meaningful optional fields. Public C structs represent absence through `has_*` flags or zero counts; the JSON view omits those fields instead of emitting `null` or empty arrays.

## control_flow

Example:

```sql
IF @enabled = 1 SELECT id FROM users ELSE SELECT id FROM archived_users
```

Control-flow shape:

```json
{
  "control_flow": {
    "roots": [{"kind": "node", "index": 0}],
    "nodes": [{"kind": "if", "branches": [0, 1]}],
    "branches": [
      {
        "condition_statement": 0,
        "items": [{"kind": "statement", "index": 1}]
      },
      {
        "items": [{"kind": "statement", "index": 2}]
      }
    ]
  }
}
```

| Field | Description |
| --- | --- |
| `roots` | Top-level statement or control-node references in source order |
| `nodes` | Control nodes; an `if` node stores ordered branch indexes |
| `branches` | Branches; a conditional branch has `condition_statement`, while an unconditional `ELSE` branch omits it |
| `items` | Statement or nested-control-node references in source order |

For `kind = "statement"`, `index` addresses a statement unit in `statements[]`.
For `kind = "node"`, it addresses `nodes[]`. A condition statement has keyword
`condition`; its query-graph root block and its field/value clause kind are also
`condition`. A nested `IF` is an item that references another node rather than a
copied subtree.

## query_graph

| Field | Description |
| --- | --- |
| `root` | Root query block index; omitted for statements without a query block |
| `blocks` | Query blocks, including SELECT blocks, derived tables, CTEs, set operations, and scalar subqueries; present when non-empty |
| `relations` | Base tables, derived tables, and CTE references visible in the SQL; present when non-empty |
| `targets` | SELECT output items, star targets, and DML output sources; present when non-empty |
| `fields` | Field-reference occurrences visible in the SQL text; present when non-empty |
| `values` | Literals, binds, and DEFAULT values associated with fields; pagination and pseudo-column binds are excluded; present when non-empty |
| `sets` | `UNION`, `UNION ALL`, `INTERSECT`, and `EXCEPT/MINUS` operations; present when non-empty |
| `predicates` | Predicate-tree nodes from `WHERE`, `ON`, `HAVING`, and similar clauses; present when non-empty |
| `session` | Database, schema, role, identity, transaction-characteristic, or session-parameter action; present only for statements with session-state semantics |
| `dml` | `INSERT`, `UPDATE`, `DELETE`, and `MERGE` write shape, including nested DML; present only for DML statements |

All indexes are zero-based within the current statement. `relations[].source_block`, `targets[].source_block`, `targets[].star_relations`, and `sets[].branches` together describe derived-table, star, and set-operation lineage.

## Derived Tables And Stars

Example:

```sql
SELECT *
FROM (
  SELECT ROWNUM, *
  FROM (
    SELECT *
    FROM (
      SELECT o.*, ROWNUM AS rnum
      FROM (
        SELECT x.id FROM users x
        UNION
        SELECT y.id FROM archived_users y
      ) o
    )
  ) b
) d
```

The graph expresses the lineage as:

- the relation with `alias = "d"` points to its inner block through `source_block`;
- the relation with `alias = "b"` points to the next inner block;
- the relation with `alias = "o"` points to the set-operation result block;
- the outer `SELECT *` is a `targets[]` entry with `kind = "star"` and `star_relations` pointing at `d`;
- the `b` layer star points at `b` and continues through `source_block`;
- `o.*` is represented as `kind = "qualified_star"` and points to the `UNION` result block.

Each SQL occurrence is emitted once. Its source path can be followed through `relation -> source_block -> target -> set branch`.

## relation

```json
{
  "block": 0,
  "kind": "base",
  "schema": "public",
  "table": "users",
  "alias": "u",
  "selector": "stmt[0].relation[0]"
}
```

| Field | Description |
| --- | --- |
| `block` | Query block that owns the relation |
| `kind` | `base`, `derived`, `cte`, or another relation kind |
| `database` | Database name if present in SQL; omitted otherwise |
| `schema` | Schema name if present in SQL; omitted otherwise |
| `table` | Table name if present in SQL; omitted for derived relations without a table name |
| `alias` | Alias if present in SQL; omitted otherwise |
| `link` | Database link name for remote object references; omitted otherwise |
| `source_block` | Source query block for derived tables or CTEs; omitted otherwise |
| `selector` | Relation selector for patching; omitted when no writable node exists |

A CTE definition creates one source block. Multiple references share that
`source_block`, and an unreferenced CTE definition remains in `blocks[]`.

## target

```json
{
  "block": 0,
  "ordinal": 0,
  "kind": "field",
  "name": "id",
  "field": 2,
  "selector": "stmt[0].select_target[0][0]",
  "target_list_selector": "stmt[0].select_targets[0]"
}
```

| Field | Description |
| --- | --- |
| `block` | Query block that owns the target |
| `ordinal` | Target ordinal in the SELECT list |
| `kind` | `field`, `star`, `qualified_star`, `literal`, `bind`, `subquery`, `pseudo`, or `expression` |
| `name` | Output name or alias; omitted when absent |
| `field` | Related `fields[]` index for direct field output; omitted otherwise |
| `value` | Related `values[]` index for literal or bind output targets; omitted otherwise |
| `star_relations` | Relation indexes covered by `*` or `alias.*`; omitted for non-star targets |
| `source_block` | Source query block for derived output; omitted otherwise |
| `selector` | Single target selector; omitted when no writable node exists |
| `target_list_selector` | SELECT target-list selector; omitted when no writable node exists |

## field

```json
{
  "block": 0,
  "clause": "where",
  "relation": 0,
  "column": "status",
  "selector": "stmt[0].name[5]"
}
```

| Field | Description |
| --- | --- |
| `block` | Query block containing the field occurrence |
| `clause` | Clause name, such as `select_list`, `where`, `on`, or `order_by` |
| `relation` | Stable relation index; omitted when not uniquely attributable |
| `candidate_relations` | Candidate relation indexes for unqualified fields in multi-relation scopes; omitted when empty |
| `column` | Column name; `*` is represented by `targets[]` instead |
| `target` | Related SELECT target index; omitted outside output targets |
| `selector` | Name selector; omitted when no writable node exists |
| `target_path` | Ordered output-expression path; omitted for direct fields and non-output fields |

`target_path` is ordered from outer to inner. For `LOWER(UPPER(name))`, the path for `name` is `LOWER -> UPPER`. For `CONCAT(a, b)`, `a` and `b` have distinct `arg_index` values.

Function calls are not emitted as a separate target kind. For `SELECT UPPER(name)`, the target kind is `expression`, and the function nesting for `name` is represented by `fields[].target_path`.

## value

```json
{
  "block": 0,
  "clause": "where",
  "operator": "=",
  "operator_kind": "unknown",
  "field": 1,
  "field_match_kind": "direct_field",
  "kind": "bind",
  "bind_key": "id",
  "bind_kind": 2,
  "bind_sql": ":id",
  "bind_position": 1,
  "selector": "stmt[0].value[6]"
}
```

| Field | Description |
| --- | --- |
| `block` | Query block containing the value |
| `clause` | Clause containing the value |
| `operator` | Associated operator; omitted when absent |
| `operator_kind` | Structured operator classification; emitted when `operator` exists. Pattern-match values are `like`, `not_like`, `ilike`, or `not_ilike`; other operators use `unknown` |
| `field` | Related field index; pagination or pseudo-column values without a related field are not emitted in `values[]` |
| `source_field` | Source field index when the value is a field reference; omitted otherwise |
| `field_match_kind` | Field-match shape; `direct_field` for a direct field and `expression_field` when the field is inside a function, cast, expression, or `CASE` |
| `kind` | `literal`, `bind`, `default`, `expression`, or `field` |
| `bind_key` | Bind key; omitted when no bind exists |
| `bind_kind` | `0` none, `1` positional, `2` named |
| `bind_sql` | Original placeholder text as written in SQL; omitted when no bind exists |
| `bind_position` | One-based bind occurrence position across the full input SQL; omitted when no bind exists |
| `selector` | Value selector; omitted when no writable node exists |
| `literal` | Literal object; omitted for non-literals |
| `like_escape` | Explicit ESCAPE shape for `LIKE` / `NOT LIKE` / `ILIKE` / `NOT ILIKE`; omitted when ESCAPE is not explicit |

When a string literal comes from a quoted-identifier token, the `literal` object emits `quoted_identifier: true`. Ordinary string literals and unquoted identifiers omit this field.

For multi-statement input, `bind_position` is global across the whole SQL text and does not reset per statement.

For `WHERE`, `JOIN ... ON`, `HAVING`, and predicate expressions inside SELECT projections, field-bound values are emitted for `IN`, `NOT IN`, `BETWEEN`, and ordinary comparisons. `field_match_kind` distinguishes direct-field predicates such as `secret = ?` from expression-field predicates such as `UPPER(secret) = ?`, `CAST(secret AS ...) = ?`, `secret || id = ?`, or `CASE ... THEN secret END = ?`. If the field side contains multiple attributable fields, each field gets a separate `expression_field` relation.

For `LIKE ... ESCAPE ...`, the main `values[]` item still represents the right-hand pattern value. `operator_kind` identifies the pattern-match operator classification, and `like_escape` represents only the explicit escape clause. Its `kind` is `literal`, `bind`, or `expression`; bind escapes also include `bind_key`, `bind_kind`, `bind_sql`, and `bind_position`. Deparse output keeps the `LIKE pattern ESCAPE escape` form.

```json
{
  "operator": "LIKE",
  "operator_kind": "like",
  "kind": "bind",
  "bind_key": "pattern",
  "bind_kind": 2,
  "bind_sql": ":pattern",
  "bind_position": 1,
  "like_escape": {
    "kind": "bind",
    "bind_key": "escape_char",
    "bind_kind": 2,
    "bind_sql": ":escape_char",
    "bind_position": 2
  }
}
```

If the value side is a function, cast, operator, array, row, or CASE expression, such as `secret = UPPER(?)`, `secret = ? || 'x'`, or `secret = CAST(? AS CHAR)`, `values[]` emits `kind=expression` attached to `secret` and does not expose inner binds or literals as direct values.

## predicate

```json
{
  "block": 0,
  "clause": "where",
  "kind": "comparison",
  "bool_operator": "none",
  "operator": "=",
  "operator_kind": "unknown",
  "left_field": 1,
  "value": 0
}
```

| Field | Description |
| --- | --- |
| `block` | Query block containing the predicate |
| `clause` | Clause containing the predicate, such as `where`, `on`, or `having` |
| `kind` | `comparison`, `bool`, `exists`, `expression`, or `unknown` |
| `bool_operator` | Boolean operator for `bool` predicates: `and`, `or`, or `not`; other predicate kinds use `none` |
| `operator` | Comparison operator; omitted for non-comparison predicates |
| `operator_kind` | Structured operator classification; omitted for non-comparison predicates |
| `left_field` | Left-side field index; omitted when no stable field side exists |
| `right_field` | Right-side field index for field-to-field comparisons; omitted otherwise |
| `value` | Related `values[]` index for literal, bind, DEFAULT, field, or expression right-side values; omitted otherwise |
| `children` | Child predicate indexes for `AND`, `OR`, and `NOT`; omitted for non-boolean predicates |

`field = literal/bind` is represented by `left_field + value`. `field = field` is represented by `left_field + right_field`, with a `values[]` entry whose `kind` is `field`. Conditions that cannot be split safely into field and value sides are emitted as `kind = "expression"` instead of being reported as direct field movement.

## session

`query_graph.session` describes the statement's session-state action.

```json
{
  "action": "set",
  "items": [
    {
      "scope": "session",
      "target_kind": "parameter",
      "name": "NLS_DATE_LANGUAGE",
      "values": [
        {
          "kind": "identifier",
          "text": "ENGLISH"
        }
      ]
    }
  ]
}
```

| Field | Description |
| --- | --- |
| `action` | `set`, `reset`, `switch`, `discard`, `enable`, `disable`, `force`, `advise`, `close`, `sync`, `assume`, or `revert` |
| `items` | Session-state targets affected by the action; contains at least one item |

Item fields:

| Field | Description |
| --- | --- |
| `scope` | `session`, `local`, or `transaction` |
| `target_kind` | `parameter`, `variable`, `database`, `schema`, `container`, `role`, `authorization`, `login`, `user`, `transaction`, `session_context`, `database_link`, `object`, `constraint`, or `all` |
| `name` | Explicit or canonical semantic name of a parameter, variable, or object; omitted when unavailable, and a canonical name need not appear verbatim in the SQL |
| `values` | Target values; omitted when empty |

A value `kind` is `identifier`, `keyword`, `literal`, `bind`, or `expression`.
Identifiers, keywords, and expressions use `text`; literals use `literal`;
binds use `bind_key`, `bind_kind`, `bind_sql`, and a one-based `bind_position`
assigned by SQL occurrence order across the statements in the handle. A value
of any kind can also include an optional `name` that distinguishes a value
with separate semantics inside the same item. For example, the collation value
in `SET NAMES ... COLLATE ...` uses
`"name": "collation"`. The field is omitted when no distinct semantic label
is available.

## DML

`query_graph.dml` describes write targets, target columns, row values,
assignments, source queries, and result channels.

Common fields:

| Field | Description |
| --- | --- |
| `kind` | `insert`, `update`, `delete`, or `merge` |
| `insert_mode` | INSERT shape: `values`, `select`, `all`, `first`, `set`, `replace_values`, `replace_select`, or `replace_set` |
| `target_relation` | Target relation index; omitted when no stable target exists |
| `target_columns` | Explicit INSERT target-column indexes; omitted when no column list exists |
| `rows` | Cell indexes for `INSERT ... VALUES` or an Oracle/Dameng multi-table INSERT branch |
| `source_block` | Source query block for `INSERT ... SELECT` or Oracle/Dameng multi-table INSERT source query |
| `branches` | INTO branches for Oracle/Dameng `INSERT ALL/FIRST`, or ordered `WHEN` branches for MERGE; omitted when empty |
| `result_channels` | DML result channels; omitted when the DML has no result output |
| `children` | Nested DML nodes owned by this DML; omitted when empty |

Result-channel fields:

| Field | Description |
| --- | --- |
| `kind` | `client` for returned rows or `sink` for rows written to a relation |
| `block` | `dml_result` block containing the channel output targets |
| `sink_relation` | Sink relation index; present only for a `sink` channel |
| `sink_columns` | Sink-column objects; omitted without an explicit column list |
| `references` | Result-target references to target-row or source-relation fields; present when non-empty |

Each `references[]` item contains a result `target` index, an optional `field`
index, a `relation` index, and a `kind`. The kind is `target_before`,
`target_after`, or `source`. SQL Server `DELETED.id`, `INSERTED.id`, and source
table fields use these three kinds, respectively.

Result-target, sink-relation, and sink-column selectors can be passed directly
to `sqlparser_apply_patch()`:

```text
stmt[0].dml_result_target[0][0][0]
stmt[0].dml_result_sink[0][0]
stmt[0].dml_result_sink_column[0][0][0]
```

Each Oracle/Dameng multi-table INSERT branch owns its `target_relation`,
`target_columns`, `rows`, and `branch_kind`. `branch_kind` is
`unconditional`, `when`, or `else`. `WHEN` branch predicates are addressable
through `condition_selector`; pass that selector to
`sqlparser_selector_clause_sql()` to read the original predicate SQL.

Branch cell `kind` can be `literal`, `bind`, `default`, `expression`, or
`field`. For a cell such as `VALUES (id)` that directly references an output
field from the trailing source query, `kind` is `field` and `source_target`
points to the related source-query entry in `targets[]`. If that target is a
direct field, callers can follow `targets[].field` to the corresponding
`fields[]` entry.

For a successfully parsed MERGE, `branches[]` follows the order in which the
`WHEN` clauses appear in the source SQL. Each branch's `ordinal` is its
zero-based absolute ordinal `W` across all `WHEN` clauses in that MERGE. A branch contains
`merge_action_kind` (`insert`, `update`, `delete`, or `nothing`) and
`merge_match_kind` (`matched`, `not_matched_by_target`, or
`not_matched_by_source`). An INSERT branch exposes `target_columns` and `rows`.
When the target column list is omitted, `target_columns` is absent but `rows`
remains present. Every cell uses the absolute `W` as `row`, with contiguous
zero-based `column` values. An UPDATE branch exposes `assignments` that
reference the parent DML assignments. DELETE and NOTHING branches omit
`target_columns`, `rows`, and `assignments`. A conditional branch has `condition_selector`; an unconditional
branch omits it.

`UPDATE` and `MERGE` assignments use `target_field` for the written field. When
the right-hand side is a direct field reference, `kind` is `field` and
`source_field` points to the source field. If that source field comes from a
derived relation and uniquely matches a source-query output target,
`source_target` is emitted as well.

A top-level `UPDATE` assignment has a selector of the form
`stmt[S].assignment[A]`. A matched UPDATE action in a root MERGE uses
`stmt[S].merge_assignment[W][A]`; a nested MERGE uses
`stmt[S].merge_assignment[D][W][A]`. `D` is the DML index within the current
statement, `W` is the absolute zero-based ordinal across all `WHEN` clauses in
the selected MERGE, and `A` is the zero-based assignment ordinal in the target
UPDATE branch. MERGE conditions similarly use
`stmt[S].merge_branch_condition[W]` or the nested form
`stmt[S].merge_branch_condition[D][W]`; callers can read the original
condition text with `sqlparser_selector_clause_sql()`. Assignment selectors
are accepted by the assignment selector APIs and also work with
`SQLPARSER_PATCH_INSERT_ASSIGNMENT`, `SQLPARSER_PATCH_DELETE_ASSIGNMENT`,
`SQLPARSER_PATCH_REPLACE_ASSIGNMENT`, and a patch `source_selector`.

## Rewriting

Selectors from View JSON can be used to populate `sqlparser_patch_t` and passed
to `sqlparser_apply_patch()`. After a rewrite, call `sqlparser_deparse()` to
generate SQL.
