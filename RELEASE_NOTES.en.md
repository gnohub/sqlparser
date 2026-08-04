# v2.14.2 Release Notes

`v2.14.2` is a patch release for `v2.14.1`. It corrects source-surface
regressions after local patches to `INSERT ... VALUES` typed literals and SQL
Server `INSERT ... OUTPUT` statements.

## INSERT VALUES Surface Preservation

- For an ordinary `INSERT ... VALUES` cell whose source interval can be resolved
  safely, dialect-valid string, typed-literal, function, and compound-expression
  patches use a local replacement. After dialect parsing, only the target
  interval is replaced and unchanged source text is preserved.
- Oracle, Dameng, and Vastbase-Oracle `DATE '...'` and `TIMESTAMP '...'`
  expressions retain typed-literal syntax when either the literal itself or a
  different cell is replaced, rather than being rewritten as `CAST(...)`.
- Expression cells in a patched handle read from current surface SQL. On the
  local-source path, a `source_selector` in a multi-patch request materializes
  prior local edits before reading, ensuring that cloning uses current SQL
  text.
- Reparsing patched SQL produces a byte-identical second deparse, and patched
  and freshly parsed handles continue to export equivalent Views.

## SQL Server INSERT OUTPUT

- Simple SQL Server and Vastbase-SQLServer `INSERT ... OUTPUT ... VALUES`
  statements with verifiable boundaries use local source edits for client,
  `OUTPUT INTO`, and dual result channels.
- OUTPUT targets, sink relations, and sink columns are resolved from actual
  source intervals, avoiding whole-statement AST serialization for supported
  result-channel combinations.
- No whitespace is actively inserted between a target relation and its column
  list. Forms such as `t(a)` and `audit(id)`, bracket identifiers, original
  case, and irregular whitespace remain unchanged.

## Compatibility

- This release adds no public APIs, enums, or structure fields.
- Existing function signatures and public structure layouts are unchanged.
  The shared-library ABI major remains `libsqlparser.so.0`.

## Release Validation

- The nine executable dialect fixtures contain 2,758 cases with
  `status = "final"` and 8,936 independent patches.
- A strict remote build and all nine runners passed. Original deparse, View,
  patched deparse, a second deparse after reparsing, and patched/fresh View
  checks all reported zero failures.
- One targeted Valgrind run covered typed literals, current surface SQL, and
  the multi-patch `source_selector` path. It exited with `0 bytes in 0 blocks`
  and zero errors.

Vendored `libpg_query` tag: `17-6.2.2`.
