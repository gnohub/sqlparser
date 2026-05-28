# libpg_query Integration

This document describes how `sqlparser` integrates the pinned in-repository
version of `libpg_query`, including parser-kernel maintenance and dialect
extension boundaries.

## 1. Dependency Role

`libpg_query` is the parsing backend used by `sqlparser`. It provides the
following base capabilities:

- SQL parsing
- lexical scanning
- conversion between protobuf AST and PostgreSQL nodes
- SQL deparse

`sqlparser` builds on top of that foundation and provides:

- public ABI packaging
- semantic extraction
- stable selectors
- View JSON
- unified error handling and lifecycle management

## 2. Pinned Version

The project is pinned to:

- tag: `17-6.2.2`
- commit: `7be1aed1f1f968a36cf541319f71e845850f0381`

The policy is:

- `libpg_query` is stored as pinned source code inside the repository
- version upgrades are evaluated explicitly by the project
- parser, deparser, and dialect-related maintenance are owned by the project

## 3. Core Capabilities Reused Today

### 3.1 Parse

`sqlparser` primarily reuses:

- `pg_query_parse_protobuf()`

This path returns a protobuf AST directly, which makes it a good canonical
syntax-tree representation for internal state.

### 3.2 Scan

`sqlparser` reuses:

- `pg_query_scan()`

This path is mainly used for:

- token-level information
- keyword-assisted analysis
- supplemental extraction that depends on the statement text

### 3.3 Deparse

`sqlparser` reuses:

- `pg_query_deparse_protobuf()`

All AST rewrites performed by `sqlparser` are turned back into SQL through this
path.

### 3.4 Split

`sqlparser` reuses:

- `pg_query_split_with_parser()`

This path is used to count statements and provide basic segmentation for
multi-statement SQL input.

## 4. Role of the Protobuf AST

Internally, `sqlparser` treats the protobuf AST as the syntax-tree source of
truth because:

- parse results can be stored stably inside a `handle`
- rewrites can be applied directly to the AST
- the deparse path naturally consumes the protobuf AST
- JSON can be exported on demand without becoming the primary state format

The current processing path is:

```text
SQL -> libpg_query protobuf AST -> sqlparser handle -> rewrite -> deparse -> SQL
```

## 5. Key Repository Directories

The main `libpg_query` sources live under:

- `vendor/libpg_query/src/`
- `vendor/libpg_query/src/postgres/`
- `vendor/libpg_query/srcdata/`
- `vendor/libpg_query/scripts/`

Their roles are:

- `src/`: public entry points and wrapper code
- `src/postgres/`: extracted PostgreSQL-related sources
- `srcdata/`: source data used to generate protobuf and node definitions
- `scripts/`: extraction and generation scripts

## 6. sqlparser Encapsulation Boundary

`sqlparser` keeps the following boundary:

- the public header does not expose `PgQuery*` types
- the public ABI does not expose PostgreSQL node structures
- external JSON does not promise the native `libpg_query` JSON layout
- public mutation entry points are managed by `sqlparser`

This layer decouples the public API from parser-kernel internals.

## 7. Maintenance Focus

The main maintenance points are:

- keeping the parse, rewrite, and deparse pipeline stable
- keeping JSON dependency usage and build integration consistent
- keeping the public header and ABI compact and controlled
- continuously validating behavior through examples, tests, and benchmarks

## 8. Dialect Extension Points

Dialect adaptation can be added through these extension points:

- SQL pre-processing and post-processing adaptation
- parser and scanner patches
- new protobuf node descriptions
- synchronized maintenance for read/write conversion and deparse logic

When adding dialect syntax, inspect:

- grammar and scanner definitions
- node and enum descriptions under `srcdata/*`
- generated protobuf output
- read/write conversion code
- matching deparse branches
- regression tests and benchmarks

## 9. Usage

- Treat `libpg_query` as a pinned parser kernel, not as the public interface.
- Integrate through the `sqlparser` header and libraries.
- Export View JSON when low-level syntax-tree inspection is needed.
- Use `sqlparser` atomic APIs, selectors, and structured patch for business-level
  rewrites.

## 10. Related Documents

- project overview:
  [sqlparser_architecture.en.md](./sqlparser_architecture.en.md)
- API reference: [api_reference.en.md](./api_reference.en.md)
- View JSON guide: [view_json.en.md](./view_json.en.md)
- CLI guide: [cli_guide.en.md](./cli_guide.en.md)
