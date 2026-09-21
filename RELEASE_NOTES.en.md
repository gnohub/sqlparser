# v2.16.14 Release Notes

Improved SQL comment boundaries: Oracle, Vastbase Oracle, KingbaseES Oracle, and Dameng recognize leading and inter-keyword comments in `INSERT ALL/FIRST`. SQL Server-family entries recognize comments between `SELECT` and `TOP` and on the same line as a `GO` batch separator. The MySQL entry preserves ordinary comments around a whole-statement executable comment and handles trailing `USE` comments. PostgreSQL-family entries preserve a nested comment before the first SELECT target after a target patch.

These capabilities cover SQL parsing, deparsing, and patches; they do not assert database-side execution of optimizer hints or executable comments. No public API, structure field, View JSON field, or ownership rule was added.

The 13 fixtures add 32 final cases and 56 patches, for 3,269 final cases and 10,305 patches in total. The full remote `make test` suite, all 13 case matrices, and targeted tests passed. Valgrind reported no memory errors or leaks for the 32 new cases.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
