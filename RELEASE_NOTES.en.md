# v2.16.13 Release Notes

Added four public KingbaseES dialect enums and CLI entries for PostgreSQL, Oracle, MySQL, and SQL Server. The PostgreSQL, Oracle, and MySQL entries merge the V8/V9 syntax baselines, while SQL Server uses the V9R4 compatibility baseline. No entry accepts, detects, or dispatches on a server version.

The entries reuse the existing base dialect capabilities. KingbaseES Oracle adds only a thin preprocess branch for plain `RETURNING`, while Oracle `RETURNING INTO` state and paired-rewrite boundaries remain unchanged. Oracle lexical scanning also protects `?` inside dollar-quoted literals.

The public API adds only four dialect enum values. No public function, structure field, View JSON field, or ownership rule was added, and the exported symbol count remains 162.

Four final fixtures add 175 cases and 628 patches. The thirteen fixtures now contain 3,237 final cases and 10,249 patches. The full remote `make test` suite, all thirteen dialect matrices, and all four KingbaseES CLI entries passed.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
