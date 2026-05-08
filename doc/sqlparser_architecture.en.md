# sqlparser Overview and Architecture

`sqlparser` is a generic SQL parse, rewrite, and deparse library.

The project uses a pinned in-repository version of `libpg_query` as the parser
kernel and exposes its capabilities through a public `sqlparser` C API.

## 1. Project Scope

The target capabilities of `sqlparser` are:

1. Parse SQL text into a reusable handle.
2. Identify statement kinds and core structural properties.
3. Extract relations, columns, aliases, keywords, and literal values.
4. Provide precise rewrite entry points.
5. Deparse the rewritten structure back into SQL text.
6. Export a stable SQL view as JSON.

The public deliverables are:

- public header `include/sqlparser/sqlparser.h`
- static library `lib/libsqlparser.a`
- shared library `lib/libsqlparser.so`
- command-line inspection and batch validation tool `bin/sqlparser_cli`

## 2. Typical Workflow

The standard `sqlparser` workflow is:

1. Accept a full SQL string.
2. Call `sqlparser_parse()` to create a `sqlparser_handle_t`.
3. Read structural data through statement-level APIs, generic atomic APIs, or
   semantic APIs.
4. Apply precise rewrites by index, selector, or structured patch.
5. Call `sqlparser_deparse()` to produce the final SQL text.

This flow covers two common categories of SQL processing:

- quickly classifying a statement and extracting keywords, relations, and
  columns
- applying controlled rewrites to relation names, column names, or literal
  values without changing the statement’s semantic category

## 3. Layered Architecture

### 3.1 Public API Layer

The public API layer is defined in `include/sqlparser/sqlparser.h`. All
external interaction is centered on `sqlparser_handle_t`.

The public API surface is organized into three groups:

- statement-oriented APIs: statement kind, node name, target relation,
  `INSERT`, `UPDATE`, and `WHERE`
- generic atomic APIs: `relation`, `name`, and `literal`
- externally addressable rewrite APIs: `selector`, SQL View JSON, and structured patch

### 3.2 Canonical Syntax Tree Layer

Internally, `sqlparser` uses the `libpg_query` protobuf AST as its canonical
syntax-tree representation.

This layer is responsible for:

- holding the statement tree
- acting as the single source of truth for all rewrites
- driving structured view export and deparse

This keeps the rewrite path centered on a single structural representation
instead of repeatedly parsing JSON as the primary state.

### 3.3 Semantic Analysis Layer

The semantic analysis layer exposes information that is closer to SQL-level
meaning than the raw syntax tree.

Its input is `sqlparser`'s own traversal of the canonical syntax tree.

Typical outputs include:

- `statement_types`
- `keywords`
- `tables`
- `aliases`
- `selected_columns`
- `join_columns`
- `where_columns`
- `insert_columns`
- `update_columns`
- `all_referenced_columns`

### 3.4 SQL View Layer

The SQL View layer exports the current syntax tree on demand as a structured
view for external programs.

This layer provides:

- `sqlparser_export_view_json()`
- `sqlparser_apply_patch()`
- `sqlparser_selector_parse()`
- `sqlparser_selector_format()`

This layer is suitable for:

- storing stable target paths in rule systems
- replaying rewrite patches from external programs
- using JSON as a structured output and debugging representation

### 3.5 Deparse Layer

The deparse layer emits SQL from the current syntax-tree state.

`sqlparser_deparse()` is the single public entry point, while the underlying
round-trip still relies on `libpg_query` deparse functionality.

## 4. Data Model and Caching

A `sqlparser_handle_t` holds the following categories of data:

- original SQL and internal parser SQL
- current SQL generated on demand after rewrites
- `parse_tree`: protobuf AST
- SQL View JSON: structured JSON exported on demand

Caching behavior is:

- only the required canonical syntax tree is created during the initial parse
- SQL View JSON and other derived outputs are generated on demand
- a successful rewrite invalidates the derived caches
- subsequent reads regenerate those derived results from the latest AST

## 5. Public API Organization

### 5.1 Statement-Oriented APIs

Statement-oriented APIs provide the shortest access path for common DML use
cases:

- statement kind classification
- target object lookup
- `INSERT` row/column access and rewrite
- `UPDATE` assignment access and rewrite
- `WHERE` literal access and rewrite

### 5.2 Generic Atomic APIs

Generic atomic APIs are designed to cover a broader SQL surface.

The current atomic objects are:

- `relation`
- `name`
- `literal`

These APIs work for DML, DDL, and multi-statement input.

### 5.3 Selector and View APIs

Selector and view APIs make rewrite targets stable and externally addressable.

Typical use cases include:

- storing a modification point as a text path
- serializing the SQL view as JSON
- replaying a patch in a separate request

## 6. Memory and Thread Model

`sqlparser` uses an explicit ownership model:

- `handle` objects are released with `sqlparser_handle_destroy()`
- exported strings are released with `sqlparser_string_free()`
- strings inside view structures are borrowed pointers
- the same `handle` does not support concurrent read/write access
- the usage model is one thread owning one handle

This model prioritizes predictability and stability for proxy, middleware, and
data-processing scenarios.

## 7. Robustness, Extensibility, and Performance

### 7.1 Robustness

The public interface has the following properties:

- structured status codes and error objects
- no requirement for callers to understand parser-kernel internals
- centralized cache invalidation after mutation
- SQL generation only from the current syntax-tree state

### 7.2 Extensibility

The public ABI does not expose `PgQuery*` types or PostgreSQL node structures.
The extension points include:

- maintaining parser-kernel patches
- introducing SQL dialect adaptation layers
- changing internal AST access logic without breaking the external ABI

### 7.3 Performance

The implementation minimizes repeated parsing and serialization:

- parse once into a long-lived `handle`
- derive secondary results lazily
- generate SQL View JSON only when requested
- rewrite the AST directly instead of using JSON as the primary mutation path

## 8. Feature Scope and Extension Points

This release provides:

- `SELECT`
- `INSERT`
- `UPDATE`
- `DELETE`
- multi-statement input
- common DDL classification and object-name rewrite
- MySQL, Oracle, and SQL Server dialect conversion layers
- selector-driven and structured patch driven precise rewrite

Dialect adaptation can be added through these extension points:

- SQL pre-processing and post-processing adaptation
- parser-kernel patches
- maintenance for new protobuf node types and matching deparse branches

## 9. Documents and Code Entry Points

- API reference: [api_reference.en.md](./api_reference.en.md)
- SQL View JSON guide: [view_json.en.md](./view_json.en.md)
- CLI guide: [cli_guide.en.md](./cli_guide.en.md)
- `libpg_query` integration notes:
  [libpg_query_analysis.en.md](./libpg_query_analysis.en.md)
- examples: `examples/*.c`
- public header: `include/sqlparser/sqlparser.h`
