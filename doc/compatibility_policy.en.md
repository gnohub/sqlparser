# Compatibility Policy

This document defines the compatibility boundaries for the public API, ABI,
selectors, and model JSON exposed by `sqlparser`.

## Version Source

- The canonical project version is the `VERSION` file at the repository root.
- Runtime code can read the library version through
  `sqlparser_version_string()`.
- The pinned parser-kernel tag is available through
  `sqlparser_libpg_query_tag()`.
- The model schema marker is available through
  `sqlparser_model_schema_string()`.

## Public API

- The public header is `include/sqlparser/sqlparser.h`.
- Symbols exported through that header are treated as the public C API.
- Within the same major version, published functions, enum values, and
  structure fields remain compatible.
- Breaking API changes are introduced only with a new major version.

## v0.2 Public Surface

The `v0.2.0-dev` public surface includes:

- `sqlparser_parse()`, `sqlparser_parse_with_limits()`, and
  `sqlparser_parse_with_options()`
- `sqlparser_handle_t` lifecycle management
- statement, relation, name-atom, literal, `WHERE` literal, `UPDATE assignment`,
  and `INSERT cell` access APIs
- selector parsing, formatting, read, and rewrite APIs
- parse-tree JSON, summary JSON, model JSON, and deparse output APIs
- resource limits, version string, model schema, and dialect-name helper APIs

`SQLPARSER_DIALECT_SQLSERVER` is a reserved enum and currently returns
`SQLPARSER_STATUS_UNSUPPORTED`. MySQL and Oracle support boundaries are defined
by their case matrices.

## ABI and Shared Library

- The shared library is published as `libsqlparser.so.<major>`.
- A `SONAME` major change indicates an ABI break.
- Within the same `SONAME` major version, the binary interface remains
  compatible.

## Selector Semantics

- Selector text is a stable addressing path.
- Published selector kinds and selector syntax remain compatible within the
  same major version.
- New selector kinds are added only in backward-compatible form and do not
  change the meaning of existing selector text.

## Model JSON

- The current model schema is fixed to `sqlparser.model/v1`.
- Field semantics remain compatible within the same schema.
- Breaking model changes are published under a new schema marker.
- Callers should preserve existing `selector` values and must not rewrite
  selector text arbitrarily.

## SQL Output

- `sqlparser_deparse()` preserves SQL semantics, not exact lexical formatting.
- Output may normalize keyword casing, alias spelling, optional keywords, and
  whitespace.
- Text validation should therefore rely on semantic checks or stable fields
  instead of exact source formatting.

## Dependency Boundary

- The vendored `libpg_query` copy is pinned to a fixed version.
- Kernel upgrades are explicit maintenance events and are not introduced
  implicitly in patch-level releases.
