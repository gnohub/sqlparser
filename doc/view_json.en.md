# View JSON Guide

View JSON is the on-demand JSON serialization of the `sqlparser` query graph. It is intended for regression tests, integration checks, and language-neutral inspection. Production code should prefer the public C query graph structs and does not need to generate JSON before rewriting SQL.

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

The query graph does not expand database metadata and does not infer fields that are not present in the SQL text. It only reports SQL-visible query blocks, relations, targets, field references, values, set operations, and DML write structures.

JSON only emits meaningful optional fields. Public C structs represent absence through `has_*` flags or zero counts; the JSON view omits those fields instead of emitting `null` or empty arrays.

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
| `dml` | `INSERT`, `UPDATE`, and `DELETE` write shape; present only for DML statements |

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

The graph does not expand `*` into real table fields and does not duplicate a single SQL occurrence under multiple objects. Callers that need lineage should follow `relation -> source_block -> target -> set branch`.

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
| `source_block` | Source query block for derived tables or CTEs; omitted otherwise |
| `selector` | Relation selector for patching; omitted when no writable node exists |

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
| `kind` | `field`, `star`, `qualified_star`, `literal`, `subquery`, `pseudo`, or `expression` |
| `name` | Output name or alias; omitted when absent |
| `field` | Related `fields[]` index for direct field output; omitted otherwise |
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
| `field` | Related field index; pagination or pseudo-column values without a related field are not emitted in `values[]` |
| `field_match_kind` | Field-match shape; `direct_field` for a direct field and `expression_field` when the field is inside a function, cast, expression, or `CASE` |
| `kind` | `literal`, `bind`, `default`, or `expression` |
| `bind_key` | Bind key; omitted when no bind exists |
| `bind_kind` | `0` none, `1` positional, `2` named |
| `bind_sql` | Original placeholder text as written in SQL; omitted when no bind exists |
| `bind_position` | One-based bind occurrence position across the full input SQL; omitted when no bind exists |
| `selector` | Value selector; omitted when no writable node exists |
| `literal` | Literal object; omitted for non-literals |

When a string literal comes from a quoted-identifier token, the `literal` object emits `quoted_identifier: true`. Ordinary string literals and unquoted identifiers omit this field.

For multi-statement input, `bind_position` is global across the whole SQL text and does not reset per statement.

For `WHERE`, `JOIN ... ON`, `HAVING`, and predicate expressions inside SELECT projections, field-bound values are emitted for `IN`, `NOT IN`, `BETWEEN`, ordinary comparisons, and single-column function-wrapped predicates when the predicate can be attributed to one field. `field_match_kind` distinguishes direct-field predicates such as `secret = ?` from expression-field predicates such as `UPPER(secret) = ?`, `CAST(secret AS ...) = ?`, `secret || 'x' = ?`, or `CASE ... THEN secret END = ?`. Predicates whose value side also contains field references are not force-attributed.

## Rewriting

Selectors from View JSON can be used to populate `sqlparser_patch_t` and passed to `sqlparser_apply_patch()`. After a rewrite, call `sqlparser_deparse()` and reparse the generated SQL to verify that the result is still syntactically valid.
