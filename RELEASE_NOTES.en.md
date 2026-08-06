# v2.14.5 Release Notes

`v2.14.5` adds identifier-delimiter state to the query graph and fixes a View
consistency issue after assignment patches pass through Oracle-compatible
fragment preprocessing.

## Identifier Delimiter State

- `sqlparser_graph_relation_t.quoted_identifier` reports whether the relation
  object-name token used an explicit identifier delimiter.
- `sqlparser_graph_field_t.quoted_identifier` reports whether the field
  column-name token used an explicit identifier delimiter.
- View JSON conditionally emits `quoted_identifier: true` on the applicable
  `relations[]` and `fields[]` entry. A session value whose `kind` is
  `identifier` can emit the same key; the C API exposes it through
  `sqlparser_graph_session_value_t.literal.quoted_identifier`.
- The flag recognizes `"..."`, MySQL backticks, and SQL Server `[...]`. It
  reports delimiter presence only and does not classify the delimiter kind.
  Single-quoted strings are not identifier delimiters.
- A relation flag applies only to its object name, not its database, schema, or
  alias. A field flag applies only to its column name.

## Exact Tokens and Patch Consistency

- A positive flag requires an exact token from the original SQL or patch
  fragment. Quote styles generated internally for dialect compatibility are
  not reported as source delimiters.
- Oracle and Vastbase-Oracle fragment preprocessing now replays identifier
  origins. An explicitly delimited assignment target retains its source token
  even when a right-hand bind such as `:1` is converted to an internal form.
- Views produced directly after `replace_assignment` and similar patches use
  the same rule as a fresh parse of the deparsed SQL. A delimiter flag no
  longer appears only after reparsing.
- Oracle, Dameng, and Vastbase-Oracle database-link relations use original
  object spelling retained by dialect state. MySQL-compatible session
  identifiers derive delimiter state from their original token.

## API and Ownership

- This release adds the public
  `sqlparser_graph_relation_t.quoted_identifier` and
  `sqlparser_graph_field_t.quoted_identifier` fields.
- No public functions or enums are added. Query-graph results remain borrowed
  views owned by the handle; no new release function or caller ownership is
  introduced.
- The new members are integer booleans. They are `0` without an explicit
  delimiter, and View JSON omits the corresponding key.

## Validation

- The nine executable dialect fixtures still contain 2,758 cases with
  `status = "final"` and 8,945 independent patches.
- The fixtures add 1,800 exact assertions: 910 relations, 876 fields, and 14
  session identifier values.
- A remote `make test-unit` completed successfully. All nine fixtures passed
  original deparse, View, patched deparse, and patched/fresh View comparisons.
- Ownership and complexity review confirmed that the handle releases the
  origin cache, Oracle fragment replay is a single linear scan, and no
  long-lived cache or quadratic path was introduced.

Vendored `libpg_query` tag: `17-6.2.2`.
