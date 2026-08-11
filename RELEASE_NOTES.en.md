# v2.16.0 Release Notes

`v2.16.0` tightens memory lifecycles in the fallback deparser, the successful
Patch path, and the Query Graph cache. It reduces transient allocations and
return-time retained memory for multi-projection SQL without changing parse,
deparse, Patch, or public Query Graph semantics.

## Fallback Deparser

- When neither pretty-print nor commas-at-line-start is enabled, the fallback
  deparser no longer creates a `DeparseStatePart` at each comma. It writes the
  same `", "` produced by the previous merge; both formatting paths remain
  unchanged.

Allocator-payload measurements on Linux x86_64 were:

| Projections | Requested bytes before | Requested bytes after | Peak live bytes before | Peak live bytes after | Retained after |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 30,970 B | 6,394 B | 24,640 B | 5,310 B | 128 B |
| 256 | 610,810 B | 152,058 B | 516,160 B | 117,208 B | 2,112 B |

Non-pretty output for both 16 and 256 projections remained byte-identical to
the pre-change baseline and reparsed successfully.

## Patch AST Lifecycle

- Patch operations still execute on a candidate handle and retain atomic
  rollback for patch lists. After all patch items succeed and the result is
  confirmed to be non-no-op, the unpacked AST is released before the candidate
  is transferred into the original handle.
- Packed-tree, surface-SQL, generation, persisted identifier/dialect semantics,
  and rebinding rules are unchanged. Releasing the AST also unbinds associated
  internal state; a later AST or Query Graph accessor lazily rebuilds and
  rebinds it from the current packed tree as before.
- Control-condition rendering obtains the current statement node before
  reading AST state, ensuring that a lazy AST has been rebuilt after a patch.
  Fallback deparse rebuilds and binds the AST only when dialect postprocessing
  needs it and no AST is present. Any AST rebuilt for this purpose is released
  on every success and failure exit, preserving the reduced retained memory
  after a patch returns.
- Failure and semantic-no-op paths do not clear published AST/Graph views on
  the original handle. SQL, generation, packed tree, and dialect state remain
  unchanged on those paths.

Allocator-payload measurements on Linux x86_64 were:

| Scenario | Peak before | Peak after | Retained before | Retained after |
| --- | ---: | ---: | ---: | ---: |
| Single replacement | 3,284 B | 3,284 B | 1,700 B | 364 B |
| Four consecutive applies on one handle | 4,807 B | 3,471 B | 1,505 B | 169 B |

Retained memory did not grow across the four consecutive operations. Direct
`sqlparser_apply_patch()` calls and convenience mutators routed through the
same transaction path share this lifecycle.

## Compact Query Graph Cache

- Public `sqlparser_graph_target_t` and `sqlparser_graph_value_t` layouts are
  unchanged. The cache uses private compact records, and public accessors
  reconstruct indexes, statement metadata, selectors, and complete values at
  read time.
- On Linux LP64, target records fell from 224 B to 104 B, while value records
  fell from the 800 B public layout to 88 B. Literal and bind data share a
  union payload; derivable index, statement, and selector fields are no longer
  stored repeatedly.
- Unused empty-record construction paths for target and value cache entries
  were removed. An unexpected null target/value source or null target
  identifier now returns `SQLPARSER_STATUS_INTERNAL_ERROR`. These checks cover
  internal consistency failures only; public API layouts and successful-path
  behavior are unchanged.
- Bind text, including escape binds, is stored in one contiguous NUL-terminated
  pool, with relative offsets in internal records. `LIKE ... ESCAPE` uses a
  56 B sparse side array sorted by value index, so values without an escape do
  not carry a fixed escape record.
- Target, value, block, and ordinary index-pool capacities now start at four
  instead of 16; the selector-build cache starts at eight. After all statements
  are built, the implementation makes at most one best-effort attempt to shrink
  each target, value, value-text, LIKE-escape, and index-pool buffer to its used
  size. A failed shrink allocation does not affect the successfully built graph.

Requested Query Graph cache payload for `SELECT 1,2,3,4` fell from 18,400 B to
1,872 B. Under the same measurement method, complete Query Graph cache payload
measurements for selected projection counts were:

| Projections | v2.16.0 payload |
| ---: | ---: |
| 100 | 20.66 KiB |
| 129 | 26.30 KiB |
| 200 | 40.19 KiB |
| 256 | 51.12 KiB |

## API and Compatibility

- This release adds or removes no public functions, public enum values, public
  structure fields, or resource-ownership rules. The View JSON schema,
  selector output, and Patch calling conventions are unchanged.
- The shared library still exports 152 public symbols and retains the
  `libsqlparser.so.0` SONAME. C callers do not need source changes for these
  internal cache adjustments.

## Validation

- The strict build passed. The full `make test` suite completed all
  2,781/2,781 cases and 9,034/9,034 patches across the nine case matrices, and
  every unit, example, and CLI target passed.
- Targeted deparser regressions covered 16/256-projection non-pretty output,
  pretty-print, and commas-at-line-start. Pre- and post-change outputs were
  byte-identical and reparsed successfully.
- Targeted Patch regressions covered consecutive success, forced-failure
  rollback, semantic no-op, convenience mutators, AST/Graph reacquisition, and
  pagination fallback. All SQL, packed-tree, generation, and derived-cache
  assertions passed.
- Targeted Query Graph regressions covered public target/value accessors, all
  reachable value kinds, `LIKE ... ESCAPE`, multi-statement and nested-block
  graphs, long bind text, allocation-failure retry, and shrink failure.
  Affected View JSON remained byte-identical to the pre-change baseline.
- Integration lifecycle validation completed 24 full chains within the
  configured timeout: eight serial chains plus four independent-handle threads
  running four chains each.
  Every chain acquired AST/Graph state, applied a successful Patch, rejected
  the stale Graph generation, rebuilt AST/Graph state, forced fallback deparse,
  reparsed the output, and checked public Graph semantics. The Memcheck run
  reported 23,129 allocations, 23,129 frees, `0 bytes in 0 blocks`, and
  `ERROR SUMMARY: 0`. Helgrind reported zero errors, with no timeout or
  deadlock.
- Core-API Memcheck reported 1,098,789 allocations and 1,098,789
  frees, with `0 bytes in 0 blocks` and zero errors. SQL Server surface
  lifecycle Memcheck reported 130,586 allocations and 130,586 frees, also with
  `0 bytes in 0 blocks` and zero errors.
- The complete `make verify-valgrind` matrix passed. Every unit test, dialect
  matrix, example, CLI batch, and install smoke reported
  `0 bytes in 0 blocks` and `ERROR SUMMARY: 0`. ABI validation retained 152
  public symbols, and install smoke confirmed `2.16.0` through both version
  text and `sqlparser_version_string()`.

Vendored `libpg_query` tag: `17-6.2.2`.

Vendored Jansson version: `2.15`.
