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
| `root` | Root graph-block index; omitted for statements without a graph block |
| `blocks` | Graph blocks, including DDL, SELECT blocks, derived tables, CTEs, set operations, and scalar subqueries; present when non-empty |
| `relations` | Direct DDL targets/references, base tables, derived tables, and CTE references visible in the SQL; present when non-empty |
| `targets` | SELECT output items, star targets, and DML output sources; present when non-empty |
| `fields` | Field-reference occurrences visible in the SQL text; present when non-empty |
| `values` | Values associated with fields or SELECT targets, plus literal, bind, and DEFAULT occurrences within compound DML assignment right-hand expressions; pagination and pseudo-column binds are excluded; present when non-empty |
| `expressions` | Addressable RHS function or opaque expressions from `WHERE`, `ON`, and `HAVING`; present when non-empty |
| `sets` | `UNION`, `UNION ALL`, `INTERSECT`, and `EXCEPT/MINUS` operations; present when non-empty |
| `predicates` | Predicate-tree nodes from `WHERE`, `ON`, `HAVING`, `START WITH`, `CONNECT BY`, and similar clauses; present when non-empty |
| `session` | Database, schema, role, identity, transaction-characteristic, or session-parameter action; present only for statements with session-state semantics |
| `dml` | The only root DML and its nested DML nodes; present when the statement has exactly one DML root |
| `dmls` | Root DML array, with nested DML under each element; present when the statement has multiple DML roots |

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
| `block` | Graph block that owns the relation |
| `kind` | `base`, `derived`, `cte`, or another relation kind |
| `ddl_role` | Direct DDL relation role: `target` or `reference`; omitted for other relations |
| `database` | Database name if present in SQL; omitted otherwise |
| `database_quoted_identifier` | `true` when the exact token for `database` explicitly uses `"..."`, MySQL backticks, or SQL Server `[...]`; omitted otherwise |
| `schema` | Schema name if present in SQL; omitted otherwise |
| `schema_quoted_identifier` | `true` when the exact token for `schema` explicitly uses one of the three supported delimiter forms; omitted otherwise |
| `table` | Table name if present in SQL; omitted for derived relations without a table name |
| `quoted_identifier` | `true` when the object-name token for `table` explicitly uses `"..."`, MySQL backticks, or SQL Server `[...]`; omitted otherwise |
| `alias` | Alias if present in SQL; omitted otherwise |
| `alias_quoted_identifier` | `true` when the exact token for `alias` explicitly uses `"..."`, MySQL backticks, or SQL Server `[...]`; omitted otherwise |
| `link` | Database link name for remote object references; omitted otherwise |
| `link_quoted_identifier` | `true` when the exact token for `link` explicitly uses one of the three supported delimiter forms; omitted otherwise |
| `source_block` | Source query block for derived tables, CTEs, or a query-backed DDL target; omitted otherwise |
| `selector` | Relation selector for patching; omitted when no writable node exists |

A CTE definition creates one source block. Multiple references share that
`source_block`, and an unreferenced CTE definition remains in `blocks[]`.

Explicit CTE column names override `targets[].name` and
`output_quoted_identifier` by ordinal only when the `source_block` itself owns
directly enumerable targets. A shorter PostgreSQL list overrides only its
matching prefix. Repeated references share the same source block, and DML
`source_target` resolution uses the overlaid names. SET/recursive branch targets
retain underlying names, while stars are neither expanded nor assigned guessed
columns. Selector, field, and ownership contracts are unchanged.

## DDL Relations

The canonical direct-DDL projection uses block `0` as its root with block
`kind = "ddl"`. A direct DDL relation emits only `target` or `reference` as its
`ddl_role`. Targets precede references, and peers retain source order. Callers
should still use `ddl_role` instead of inferring a role from an array position.
Only syntax successfully parsed and normalized into a supported node for the
selected dialect enters this contract; it does not imply that every dialect
accepts every SQL surface listed below.

```json
{
  "root": 0,
  "blocks": [
    {"kind": "ddl", "relations": [0, 1]}
  ],
  "relations": [
    {
      "block": 0,
      "kind": "base",
      "ddl_role": "target",
      "schema": "APP",
      "schema_quoted_identifier": true,
      "table": "CHILD",
      "selector": "stmt[0].relation[0]"
    },
    {
      "block": 0,
      "kind": "base",
      "ddl_role": "reference",
      "schema": "REF",
      "table": "PARENT",
      "quoted_identifier": true,
      "selector": "stmt[0].relation[1]"
    }
  ]
}
```

Direct DDL relations currently cover:

- the created target of `CREATE TABLE` / `CREATE FOREIGN TABLE`, plus
  column-level or table-level foreign-key, `LIKE`, and `INHERITS` references;
- the altered target of `ALTER TABLE`/`ALTER FOREIGN TABLE`, plus foreign-key
  and supported `ATTACH/DETACH PARTITION` references;
- the relation target in `CREATE INDEX ... ON relation`; the index name is not
  a relation;
- every `TRUNCATE` target;
- the old target of a relation-kind `RENAME`; the new name is not emitted as a
  second relation; and
- every target in `DROP TABLE`, `DROP VIEW`, `DROP MATERIALIZED VIEW`, and
  `DROP FOREIGN TABLE`.

The Drop AST represents objects as name lists rather than writable relation
nodes, so Drop relations do not emit `selector`. Quoted flags for
database/schema/table are still derived from each exact source token.

`CREATE VIEW`, `CREATE TABLE AS`, `CREATE MATERIALIZED VIEW`, and
`SELECT ... INTO` for PostgreSQL/Vastbase-PostgreSQL and SQL
Server/Vastbase-SQL Server use a query-backed DDL shape. The DDL root is block
`0`; its target relation emits `ddl_role = "target"` and `source_block = 1`,
with source query entry block `1`. Source SELECT relations retain ordinary
query semantics and omit `ddl_role`, corresponding to `UNKNOWN` in the C
struct.

Non-relation DDL such as `CREATE SCHEMA`, `CREATE SEQUENCE`, `CREATE SYNONYM`,
and `DROP INDEX` does not create a DDL block/relation. A dialect raw surface
enters this projection only after normalization to one of the supported nodes
above. Oracle, Dameng, and Vastbase-Oracle `SELECT ... INTO` remains an ordinary
SELECT; `INTO` does not create a target relation.

A DDL target/reference that has a `selector` continues to support relation
`REPLACE` patches. A View exported after a successful patch recomputes name
segments, quoted flags, `ddl_role`, and `source_block` for the new generation.
Old C graph views become stale under the existing generation rule, and a clone
remains independent of its source handle. This feature adds no selector, patch
kind, or ownership rule.

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
| `output_quoted_identifier` | With an explicit output alias, reports its delimiter state; without one, reports the field token only when `name` is inherited from a direct field. Emitted only when `true` |
| `field` | Related `fields[]` index for direct field or hierarchical pseudo-column output; omitted otherwise |
| `value` | Related `values[]` index for literal or bind output targets; omitted otherwise |
| `sink_value` | `values[]` output-bind index that receives this DML result target in a host-bind sink; omitted otherwise |
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
| `clause` | Clause name, such as `select_list`, `where`, `start_with`, `connect_by`, `on`, or `order_by` |
| `relation` | Stable relation index; omitted when not uniquely attributable |
| `candidate_relations` | Candidate relation indexes for unqualified fields in multi-relation scopes; omitted when empty |
| `column` | Column name; `*` is represented by `targets[]` instead |
| `quoted_identifier` | `true` when the `column` token explicitly uses `"..."`, MySQL backticks, or SQL Server `[...]`; omitted otherwise |
| `pseudo` | `true` for an unquoted pseudo-column occurrence in the current hierarchical query block; omitted otherwise |
| `prior` | `true` when the field occurrence is inside the `PRIOR` operand of `CONNECT BY`; omitted otherwise |
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
| `field` | Related field index; omitted when no field attribution exists. Values in a compound DML assignment right-hand expression are owned through `rhs_values` and need not contain `field`; pagination and pseudo-column values remain excluded from `values[]` |
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

For predicates whose value side is a function, cast, operator, array, row, or CASE expression, such as `secret = UPPER(?)`, `secret = ? || 'x'`, or `secret = CAST(? AS CHAR)`, `values[]` retains the existing field-associated `kind=expression` entry. Direct literal/bind arguments of a structured function are appended after existing values and use the matching `expression_arg` selector; an opaque expression does not promote inner values. Compound DML assignment right-hand expressions remain owned through `rhs_values`.

## expression

```json
{
  "block": 0,
  "clause": "where",
  "kind": "function",
  "sql": "UPPER($1)",
  "name": "UPPER",
  "arguments": [
    {
      "ordinal": 0,
      "kind": "bind",
      "value": 1,
      "selector": "stmt[0].expression_arg[0][0]"
    }
  ],
  "selector": "stmt[0].expression[0]",
  "argument_list_selector": "stmt[0].expression_args[0]"
}
```

| Field | Description |
| --- | --- |
| `block` | Query block containing the expression |
| `clause` | `where`, `on`, or `having` |
| `kind` | `function` or `opaque` |
| `sql` | Exact public SQL for the expression |
| `name` | Function name; omitted for an opaque expression |
| `arguments` | Ordered function arguments; omitted for an opaque expression. Argument `kind` is `literal`, `bind`, `field`, or `expression`, with the matching `value`, `field`, or nested `expression` index |
| `selector` | Whole-expression replacement selector |
| `argument_list_selector` | Argument insertion/deletion selector; omitted for an opaque expression |

Nested expressions are numbered parent-first in left-to-right depth-first argument order. Functions are treated as variadic; argument insertion and deletion validate selectors, indices, and parseability of the resulting SQL, but not function signatures, arity, or argument types. Opaque expressions support whole-expression replacement only.

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
| `clause` | Clause containing the predicate, such as `where`, `on`, `having`, `start_with`, or `connect_by` |
| `kind` | `comparison`, `bool`, `exists`, `expression`, or `unknown` |
| `bool_operator` | Boolean operator for `bool` predicates: `and`, `or`, or `not`; other predicate kinds use `none` |
| `nocycle` | `true` on the root predicate of `CONNECT BY NOCYCLE`; omitted otherwise |
| `operator` | Comparison operator; omitted for non-comparison predicates |
| `operator_kind` | Structured operator classification; omitted for non-comparison predicates |
| `left_field` | Left-side field index; omitted when no stable field side exists |
| `right_field` | Right-side field index for field-to-field comparisons; omitted otherwise |
| `value` | Related `values[]` index for literal, bind, DEFAULT, field, or expression right-side values; omitted otherwise |
| `right_expression` | Related `expressions[]` index for an RHS function/opaque expression; omitted otherwise |
| `children` | Child predicate indexes for `AND`, `OR`, and `NOT`; omitted for non-boolean predicates |

`field = literal/bind` is represented by `left_field + value`. `field = field` is represented by `left_field + right_field`, with a `values[]` entry whose `kind` is `field`. Conditions that cannot be split safely into field and value sides are emitted as `kind = "expression"` instead of being reported as direct field movement.

### Hierarchical Query Representation

- `START WITH` and `CONNECT BY` do not create a separate object. Their fields,
  values, and predicates use the existing `fields[]`, `values[]`, and
  `predicates[]` arrays with `clause = "start_with"` or
  `clause = "connect_by"`.
- When the current query block contains `CONNECT BY`, unquoted `LEVEL`,
  `CONNECT_BY_ISLEAF`, and `CONNECT_BY_ISCYCLE` are relationless fields with
  `pseudo: true`. In the SELECT list, their target has `kind = "pseudo"` and
  `targets[].field` points back to the unique field occurrence. Delimited
  `"LEVEL"` and blocks without `CONNECT BY` retain ordinary field semantics;
  nested SELECT blocks do not inherit the outer hierarchy context.
- `PRIOR` applies transparently to its complete operand. Every field occurrence
  in that operand emits `prior: true`, and the flag does not propagate into a
  nested SELECT.
- `CONNECT_BY_ROOT` adds no target kind. Its SELECT target remains
  `kind = "expression"`, and the underlying field emits
  `{ "kind": "operator", "name": "CONNECT_BY_ROOT", "arg_index": 0 }` in
  `target_path`.
- `NOCYCLE` emits `nocycle: true` only on the CONNECT BY root predicate.
- Condition-related occurrences in one SELECT enter the View arrays in the
  semantic traversal order `where`, `start_with`, `connect_by`, `group_by` /
  `having`, window clauses, then `order_by`. Selectors still come from generic
  AST descriptor traversal and must be treated as opaque locator paths; their
  indexes cannot be derived from source clause order.

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
assigned by SQL occurrence order across the statements in the handle. An
`identifier` emits `quoted_identifier: true` when its original token explicitly
uses `"..."`, MySQL backticks, or SQL Server `[...]`. A value of any kind can
also include an optional `name` that distinguishes a value with separate
semantics inside the same item. For example, the collation value in
`SET NAMES ... COLLATE ...` uses `"name": "collation"`. The field is omitted
when no distinct semantic label is available.

On a relation, `database_quoted_identifier`, `schema_quoted_identifier`,
`quoted_identifier`, `alias_quoted_identifier`, and `link_quoted_identifier`
apply only to `database`, `schema`, `table`, `alias`, and `link`, respectively.
On a field, `quoted_identifier` still applies only to the column name; the
attribution rule for a target's `output_quoted_identifier` is unchanged. On a
DML target-column object, `quoted_identifier` applies only to that object's
`column`.

`output_quoted_identifier` describes an explicit output alias when present.
Without an explicit alias, it describes the field token only when `name` is
inherited from a direct field. Every flag in this family reports only whether
its exact source token uses `"..."`, MySQL backticks, or SQL Server `[...]`;
the flags do not classify the delimiter kind and are emitted only when `true`.
PostgreSQL `U&"..."`, ordinary single-quoted strings, and quote styles generated
internally by the parser do not emit these fields.

## DML

Each `query_graph.dml` or `query_graph.dmls[]` element describes write targets,
target columns, row values, assignments, source queries, and result channels.
A single DML root uses `dml`; multiple peer roots use `dmls`. Data-modifying
CTEs follow this rule even when they belong to a SELECT statement. Nested DML
nodes are represented recursively through each root element's `children`.

Common fields:

| Field | Description |
| --- | --- |
| `kind` | `insert`, `update`, `delete`, or `merge` |
| `insert_mode` | INSERT shape: `values`, `select`, `all`, `first`, `set`, `replace_values`, `replace_select`, or `replace_set` |
| `target_relation` | Target relation index; omitted when no stable target exists |
| `target_columns` | Explicit INSERT target-column objects; omitted when no column list exists |
| `rows` | Cell indexes for `INSERT ... VALUES` or an Oracle/Dameng multi-table INSERT branch |
| `source_block` | Source query block for `INSERT ... SELECT` or Oracle/Dameng multi-table INSERT source query |
| `branches` | INTO branches for Oracle/Dameng `INSERT ALL/FIRST`, or ordered `WHEN` branches for MERGE; omitted when empty |
| `result_channels` | DML result channels; omitted when the DML has no result output |
| `children` | Nested DML nodes owned by this DML; omitted when empty |

Target-column objects in `target_columns`, `branches[].target_columns`, and
`result_channels[].sink_columns` share one shape:

| Field | Description |
| --- | --- |
| `ordinal` | Zero-based target-column ordinal within the current column list |
| `column` | Target-column name |
| `quoted_identifier` | `true` when the exact source token for `column` uses `"..."`, MySQL backticks, or SQL Server `[...]`; omitted otherwise |
| `selector` | Single-column selector; omitted when no writable node exists |

This `quoted_identifier` covers regular INSERT, MERGE INSERT branches, Oracle,
Dameng, and Vastbase-Oracle `INSERT ALL/FIRST` branches, and relation-backed SQL
Server `OUTPUT ... INTO` sinks. It follows the same true-only rule and excludes
`U&"..."` as described above.

A MySQL or Vastbase-MySQL multi-target UPDATE omits `dml.target_relation`;
each assignment's `target_field` identifies a target field in `fields[]` with
its own relation. The corresponding `sqlparser_statement_target_relation()`
call returns `SQLPARSER_STATUS_UNSUPPORTED`. A Dameng multi-table UPDATE
requires every SET assignment to reference the same table object and therefore
always emits one `dml.target_relation`.

Result-channel fields:

| Field | Description |
| --- | --- |
| `kind` | `client` for returned rows or `sink` for results received by a relation or host bind |
| `block` | `dml_result` block containing the channel output targets |
| `sink_relation` | Sink relation index; present only for a relation-backed sink |
| `sink_columns` | Target-column objects for a relation-backed sink; omitted without an explicit column list |
| `references` | Result-target references to target-row or source-relation fields; present when non-empty |

A relation-backed sink identifies its destination through `sink_relation` and
optional `sink_columns`. A host-bind sink omits both fields. Each result target
uses `sink_value` to reference its output bind in `query_graph.values[]`. That
value retains the existing value `selector`, which can be targeted by
`SQLPARSER_PATCH_REPLACE`; no new selector kind is introduced. This
representation is used by `RETURNING ... INTO` in Oracle and Vastbase-Oracle
compatibility mode, and by `RETURN` / `RETURNING ... INTO` in Dameng. N result
targets correspond ordinally to N host binds: the i-th target's `sink_value`
references the i-th output bind.

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
`sqlparser_selector_clause_sql()` to read the original predicate SQL. For
Oracle, Dameng, and Vastbase-Oracle, an `INSERT ALL ... INTO ...@link` branch
target relation restores complete projection: `table`, `link`, and the
corresponding `quoted_identifier` and `link_quoted_identifier` are emitted from
their exact source tokens.

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
When an action has VALUES and its target column list is omitted,
`target_columns` is absent but `rows` remains present. Every cell uses the
absolute `W` as `row`, with contiguous zero-based `column` values. An
omitted-list DEFAULT VALUES action omits both `target_columns` and `rows`. An
UPDATE branch exposes `assignments` that
reference the parent DML assignments. DELETE and NOTHING branches omit
`target_columns`, `rows`, and `assignments`. A conditional branch has `condition_selector`; an unconditional
branch omits it. An attached `DELETE WHERE` predicate on an Oracle/Dameng
matched UPDATE uses `delete_condition_selector` on that same branch; its
`merge_action_kind` remains `update`, and no additional DELETE branch is
created. A PostgreSQL or SQL Server `WHEN MATCHED ... THEN DELETE` action is an
independent branch with `merge_action_kind = delete` and has no
`delete_condition_selector`.

Each `target_columns[]` object in a MERGE INSERT action has a single-column
`selector`, and each `rows[]` cell has a complete-expression `selector`. A root
MERGE uses `stmt[S].merge_insert_column[W][C]` and
`stmt[S].merge_insert_cell[W][C]`. A nested MERGE adds its statement-local DML
index `D` before `W`. A branch with VALUES also exposes
`target_list_selector` as `stmt[S].insert_branch_columns[W]`, or
`stmt[S].insert_branch_columns[D][W]` for a nested MERGE. The selector remains
present when the target-column list is omitted so that the list can be
materialized or a cell can be inserted independently. An omitted-list `INSERT
DEFAULT VALUES` branch has zero columns and zero rows and does not expose this
selector. An explicit-list DEFAULT VALUES branch may still expose its existing
individual-column and target-list selectors.

Single-column and complete-cell selectors independently support
`SQLPARSER_PATCH_REPLACE`. The target-list selector accepts three
`SQLPARSER_PATCH_INSERT_COLUMN` payload shapes: name-only inserts a target
column at `index`, value-only inserts a VALUES cell at `index`, and name plus
value inserts both at the same position. Intermediate counts may differ within
a batch. Before commit, every touched branch that ends with an explicit target
column list must have equal column and value counts; otherwise the whole batch
rolls back atomically. Value-only insertion is valid when the list remains
omitted. `SQLPARSER_PATCH_DELETE_COLUMN` remains paired and requires matching
explicit lists before deletion; an omitted list does not support that deletion.
DEFAULT VALUES has no VALUES list, so neither the three insertion shapes nor
paired deletion are supported; selectors from an explicit list do not make
either operation available.

This is the View and patch contract for successfully parsed MERGE statements
through all nine project dialect entry points; it does not claim that every
corresponding database server provides the syntax natively.

`UPDATE`, `INSERT` conflict-update, and `MERGE` assignments use `target_field`
for the written field. When
the right-hand side is a direct field reference, `kind` is `field` and
`source_field` points to the source field. If that source field comes from a
derived relation and uniquely matches a source-query output target,
`source_target` is emitted as well.

When an assignment has `kind = "expression"`, `rhs_fields` and `rhs_values`
list the related `fields[]` and `values[]` indexes from the right-hand
expression in the current assignment block. `rhs_blocks` lists entry indexes
for subqueries reachable from that expression without crossing another
subquery boundary. Empty lists are omitted. `rhs_blocks` does not repeat blocks
internal to those subqueries; traverse each entry block to reach its relations,
targets, fields, values, predicates, and set operations. A direct `field`,
`literal`, `bind`, or `default` right-hand side continues to use the existing
assignment payload and emits none of the three `rhs_*` lists.

A root `UPDATE` or root `INSERT` conflict-update assignment has a selector of
the form `stmt[S].assignment[A]`. A nested `UPDATE` uses
`stmt[S].assignment[D][A]`, where `D` is the
zero-based DML ordinal within the statement and `A` is the zero-based ordinal
within the target assignment list. A matched UPDATE action in a root MERGE uses
`stmt[S].merge_assignment[W][A]`; a nested MERGE uses
`stmt[S].merge_assignment[D][W][A]`. `W` is the absolute zero-based ordinal
across all `WHEN` clauses in the selected MERGE. MERGE conditions similarly use
`stmt[S].merge_branch_condition[W]` or the nested form
`stmt[S].merge_branch_condition[D][W]`. An Oracle/Dameng attached-delete
predicate uses `stmt[S].merge_delete_condition[W]` or
`stmt[S].merge_delete_condition[D][W]`, with selector kind
`SQLPARSER_SELECTOR_KIND_MERGE_DELETE_CONDITION = 25`. Callers can read either
condition with `sqlparser_selector_clause_sql()` and replace it with
`sqlparser_selector_set_clause_sql()` or `SQLPARSER_PATCH_REPLACE`. Assignment selectors
are accepted by the assignment selector APIs and also work with
`SQLPARSER_PATCH_INSERT_ASSIGNMENT`, `SQLPARSER_PATCH_DELETE_ASSIGNMENT`,
`SQLPARSER_PATCH_REPLACE_ASSIGNMENT`, and a patch `source_selector`.

## Rewriting

Selectors from View JSON can be used to populate `sqlparser_patch_t` and passed
to `sqlparser_apply_patch()`. After a rewrite, call `sqlparser_deparse()` to
generate SQL.
