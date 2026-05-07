# v0.2.0-dev Release Notes

`v0.2.0-dev` is a development preview of `sqlparser` for SQL parsing,
structured inspection, controlled rewrite, and deparse workflows.

## Highlights

- Added an Oracle dialect conversion layer for common Oracle SQL that can be
  safely mapped to the current AST.
- Added an Oracle example, CLI dialect option, batch JSON dialect field, and a
  full regression matrix.
- Kept Oracle public output stable: deparse, summary, and model JSON do not
  expose internal `$N`, `EXCEPT`, or other conversion details.
- Extended expression-fragment rewrite support for Oracle binds and preserved
  handle state on failed rewrites.
- Added stability tests for malformed SQL, argument validation, resource
  limits, failed-rewrite rollback, and multi-dialect public-output stability.
- Extended CI release gates with JSON fixture validation, Linux GCC
  verification, ABI checking, benchmark smoke, and source-package smoke.
- Fixed incremental rebuild invalidation for version-file changes so runtime
  version output stays aligned with `VERSION`.

## Oracle Support Boundary

See [Oracle Dialect Support](./doc/oracle_dialect_support.en.md).

The current Oracle matrix contains 58 cases: 39 supported paths and 19 explicit
unsupported paths. Explicitly unsupported Oracle-specific constructs return
`SQLPARSER_STATUS_UNSUPPORTED` and do not return a usable handle.

## Release Validation

This release gate includes:

- `git diff --check`
- `jq empty tests/cases/*.json`
- `make verify LOOP=5`
- `make install-smoke`
- `make abi-check`
- `make dist`
- Windows MSVC: `nmake /F Makefile.msvc test`

Linux release validation covers release/debug builds, sanitizers, valgrind,
loop regression, CLI batch, installed-library API smoke, and benchmark smoke.

## Compatibility

- Public header: `include/sqlparser/sqlparser.h`
- Model JSON schema: `sqlparser.model/v1`
- Shared-library ABI major: `libsqlparser.so.0`
- Vendored `libpg_query` tag: `17-6.2.2`

See [Compatibility Policy](./doc/compatibility_policy.en.md).
