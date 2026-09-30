# v2.16.19 Release Notes

Optimized node lookup for batched UPDATE and WHERE string replacements, INSERT value-type reads, and validation of library-rendered string fragments, reducing repeated lookups and temporary parse-tree construction. Optimized preprocessing scans in MySQL, Vastbase MySQL, and KingbaseES MySQL, skipping inapplicable statement-processing passes.

Patch order, source-read semantics, and atomic rollback remain unchanged. Public APIs, public structure layouts, and resource limits are unchanged, with no persistent AST cache added.

In a same-machine comparison with 2.16.18, a single `sqlparser_apply_patch()` call for 5,000 string replacements across 5,000 MySQL INSERT rows decreased from approximately 270 milliseconds to 105 milliseconds. Actual gains depend on the SQL and patch mix.

Related regressions passed. Valgrind reported no memory errors or leaks in the 5,000-row INSERT and MySQL statement-dispatch cases.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
