# KingbaseES PostgreSQL Compatibility Entry Case Matrix

The executable fixture is `tests/cases/kingbase_postgresql_dialect_input.json`, verified by the dedicated KingbaseES PostgreSQL entry and case runner.

## Audit Baseline

- The syntax baseline is the union of the official V8R6 and V9R1 material: `V008R006C009B0014` and `V009R001C002B0014`. See the [V8 release summary](https://help.kingbase.com.cn/v8/intro/releasenotes-external/summary.html), [V8 manual downloads](https://help.kingbase.com.cn/v8/download.html), [V9 manual downloads](https://help.kingbase.com.cn/v9/download.html), and [V9 SQL Reference](https://help.kingbase.com.cn/v9/development/sql-plsql/sql/index.html).
- The `kingbase-postgresql` entry takes no server-version parameter and does not decide whether an input originated from V8 or V9.
- PostgreSQL compatibility mode is initialized with `initdb -m pg` or `initdb --dbmode=pg`. The current V8 manual lists `pg`, `oracle`, and `mysql`, with numeric aliases `0`, `1`, and `2`: [initdb](https://help.kingbase.com.cn/v8/admin/reference/ref-server/initdb.html).
- Standard Edition is a licensed product edition, not a separate SQL compatibility mode: [KingbaseES License Manual](https://help.kingbase.com.cn/v8/PDF/KingbaseES_License%E4%BF%A1%E6%81%AF%E6%89%8B%E5%86%8C.pdf). The suite therefore uses `kingbase-postgresql` and does not create a duplicate `kingbase-standard` entry.
- V8 has no `sqlserver` dbmode. The SQLServer compatibility entry is based separately on official V9R4C12 material and is outside this fixture: [SQLServer compatibility overview](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/kes-vs-sqlserver/overview.html).

## Fixture State

- All 45 cases have `status = "final"`.
- The 185 independent patches are retained from the source PostgreSQL cases.
- Two cases retain complete `bind_occurrences` assertions.
- Every entry is selected from `tests/cases/sql_batch_input.json`. Apart from adding the `kingbase-postgresql-` name prefix and removing the source case's explicit `postgresql` dialect override, SQL, status, expected View, patches, and bind occurrences are unchanged.
- `KBPG001` through `KBPG045` map one-based to the fixture's `items` array order.
- Runner: `tests/unit/test_kingbase_postgresql_dialect_case_matrix.c`.

## Official Evidence and Coverage

The official V8/V9 SQL manuals label Oracle-only and MySQL-only capabilities separately. This suite uses only common syntax and syntax with explicit PostgreSQL-mode evidence; it does not infer capabilities from other database products. The V9R1 placeholder-expression section further confirms the bind-expression boundary for interface SQL. The existing 45-case minimum already covers the corresponding expressions, DML, and CTE structures, so no duplicate case was added: [V9 expressions](https://help.kingbase.com.cn/v9/development/sql-plsql/sql/Expressions.html).

| Category | Covered cases | Official evidence | Boundary |
| --- | --- | --- | --- |
| Lexing and delimited identifiers | `KBPG014`-`KBPG015`, `KBPG037`-`KBPG038` | [SQL lexical conventions](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/changes.html), [SQL basic elements](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/datatype.html) | single-quoted strings, a semicolon inside a string, dollar-quoted strings, and double-quoted schema/table/column tokens; no backtick or bracket identifiers |
| Types, casts, arrays, and expressions | `KBPG017`, `KBPG030`-`KBPG033`, `KBPG039`-`KBPG041`, `KBPG045` | [SQL basic elements](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/datatype.html), [expressions](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Expressions.html), [operators](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Operators.html), [conditions](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Conditions.html), [functions](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Function.html) | `BIGINT/VARCHAR/TEXT/JSONB/TIMESTAMP`, `ARRAY`, `ROW`, `CASE`, `||`, `ILIKE ... ESCAPE`, and `UPPER/LOWER/COALESCE/SUM/COUNT` |
| SELECT, joins, subqueries, sets, and pagination | `KBPG001`-`KBPG004`, `KBPG016`-`KBPG018`, `KBPG030`-`KBPG032`, `KBPG034`-`KBPG038`, `KBPG043`-`KBPG045` | [queries and subqueries](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Queries_and_Subqueries.html), [SELECT](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | JOIN/ON, WHERE, GROUP BY, HAVING, WINDOW, derived tables, scalar and EXISTS subqueries, `UNION ALL`, `INTERSECT`, ORDER BY, LIMIT/OFFSET, explicit-column and recursive CTEs |
| INSERT and conflict handling | `KBPG005`-`KBPG007`, `KBPG019`-`KBPG020`, `KBPG027`, `KBPG039`, `KBPG042` | [SQL Quick Reference](https://help.kingbase.com.cn/v8/PDF/KingbaseES_SQL%E8%AF%AD%E8%A8%80%E5%BF%AB%E9%80%9F%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C.pdf), [INSERT/ON CONFLICT/RETURNING](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_9.html) | single- and multi-row VALUES, INSERT SELECT, `ON CONFLICT DO UPDATE`, RETURNING, expression values, and data-modifying CTEs |
| UPDATE and DELETE | `KBPG008`, `KBPG021`-`KBPG022`, `KBPG028`-`KBPG029`, `KBPG039`, `KBPG042` | [UPDATE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html), [DELETE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_6.html) | multiple assignments, UPDATE FROM, DELETE USING, RETURNING, binds, and expression RHS values |
| MERGE | `KBPG023` | [MERGE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | the V8-documented base form `MERGE INTO ... USING ... ON ... WHEN MATCHED THEN UPDATE ... WHEN NOT MATCHED THEN INSERT ... VALUES ...` |
| Core relation DDL | `KBPG009`-`KBPG012`, `KBPG033` | [ALTER TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_3.html), [CREATE INDEX](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_4.html), [CREATE VIEW](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_5.html), [CREATE TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_6.html), [DROP TABLE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_9.html) | CREATE TABLE, DROP TABLE, CREATE VIEW, ALTER TABLE ADD COLUMN, and CREATE INDEX |
| Transactions and prepared statements | `KBPG013`, `KBPG024` | [PREPARE and transaction statements](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html) | BEGIN/COMMIT and PREPARE with `$1` |
| `$n` binds | `KBPG020`, `KBPG024`-`KBPG029`, `KBPG034`-`KBPG035`, `KBPG037`-`KBPG041`, `KBPG045` | [PREPARE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html), [Go prepared statements and binding](https://help.kingbase.com.cn/v8/development/client-interfaces/go/go-3.html) | ordered `$1`, `$2`, and later positional parameters in SELECT/INSERT/UPDATE/DELETE, pagination, expressions, and prepared statements; dollar-quoted contents are not binds |

## Explicit Exclusions

- V8/V9 have no separate standard SQL mode, so there is no duplicate `kingbase-standard` case set.
- SQLServer `TOP`, `@variable`, bracket identifiers, and PL/MSSQL control statements are outside this fixture and belong to the separate `kingbase-sqlserver` entry.
- Oracle-only `INSERT ALL/FIRST`, Oracle outer-join, and hierarchical-query extensions are excluded.
- MySQL-only backticks, `INSERT ... SET`, `ON DUPLICATE KEY UPDATE`, `REPLACE`, and multi-table DML are excluded.
- Client placeholders `?` and `:name` are excluded. The baseline uses only `$n`, which is explicitly documented by both server PREPARE and official client material.
- Three-part `database.schema.table` names are excluded. The V8 object-reference baseline is `[schema.]object[.part]`.
- Forms absent from the selected official KingbaseES PostgreSQL MERGE grammar, including `WHEN NOT MATCHED BY SOURCE` and MERGE RETURNING, are excluded.
- `CONCAT` variants listed only under Oracle/MySQL mode entries are excluded until PostgreSQL-mode behavior is verified on a real server.

## View and Patch Contract

Official documentation establishes only the SQL forms. The fixture's `query_graph`, selectors, patches, bind occurrences, and byte-preserving source behavior are sqlparser project contracts and must not be interpreted as KingbaseES server APIs.
