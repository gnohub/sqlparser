# v2.16.11 Release Notes

Query Graph maps explicit CTE column names by ordinal. When the source block directly owns enumerable targets, explicit names and quoted state override target outputs. Repeated references share the same result, DML `source_target` resolution uses the overlaid names, and a legal short PostgreSQL list overrides only its matching prefix.

SET/recursive CTE branches retain their own outputs, stars are not expanded or assigned guessed mappings, and existing boundaries remain when no complete direct correspondence can be established. The implementation adds no public API, structure field, string allocation, or ownership rule.

With 40 final cases and 46 patches added, the nine fixtures now contain 3,008 final cases and 9,425 patches. The full `make test` suite, all nine dialect matrices, targeted core/identifier tests, and the identifier Valgrind check passed.

Vendored `libpg_query` tag: `17-6.2.2`; vendored Jansson version: `2.15`.
