# KingbaseES SQLServer Compatibility Entry Case Matrix

The executable fixture is `tests/cases/kingbase_sqlserver_dialect_input.json`, verified by the dedicated KingbaseES SQLServer entry and case runner.

## Audit Baseline

- SQLServer-compatible syntax is sourced from the official KingbaseES V9R4C12 material: [V9R4 SQLServer-compatible edition](https://help.kingbase.com.cn/v9.4.12/index.html), [SQLServer compatibility overview](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/kes-vs-sqlserver/overview.html), and [SQL Reference](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html).
- The official V9R4 release notes identify the product as the SQLServer-compatible edition and enumerate SQLServer data types, functions, system variables, and DML capabilities: [V9R4 release notes](https://help.kingbase.com.cn/v9.4.12/intro/releasenotes-external-v9/V9.4.10.html).
- V8 exposes only `pg`, `oracle`, and `mysql` dbmodes; it has no `sqlserver` dbmode. `initdb -m sqlserver` belongs to the V9R4 SQLServer-compatible edition: [V8 initdb](https://help.kingbase.com.cn/v8/admin/reference/ref-server/initdb.html) and [SQLServer migration best practice](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/data_migration/best_practice/sqlserver_best_pratice.html).
- The `kingbase-sqlserver` parser entry takes no server-version parameter. It parses the syntax union defined by this matrix and performs no version detection.

## Fixture State

- All 45 cases have `status = "final"`.
- The 174 independent patches are retained from the source SQLServer cases.
- This initial set contains no `bind_occurrences` assertions.
- Every entry is selected from `tests/cases/sqlserver_dialect_input.json`. Apart from adding the `kingbase-sqlserver-` name prefix, SQL, status, expected View, and patches are unchanged.
- `KBSS001` through `KBSS045` map one-based to the fixture's `items` array order.
- Runner: `tests/unit/test_kingbase_sqlserver_dialect_case_matrix.c`.

## Official Evidence and Coverage

| Category | Covered cases | Official evidence | Boundary |
| --- | --- | --- | --- |
| SQLServer lexing, constants, and variables | `KBSS001`, `KBSS011`-`KBSS014`, `KBSS028`-`KBSS029`, `KBSS031`-`KBSS035` | [constants](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/basic_element/constant.html), [user variables](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/language_elements/local_variable/overview.html), [system configuration](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/system_config.html), [keywords](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/other/keyword.html) | `N'...'`, `0x...`, `@name`, `@@ROWCOUNT`, `?` client placeholders, `#temp`, and bracket-delimited tokens |
| SELECT, joins, CTEs, sets, and pagination | `KBSS001`-`KBSS005`, `KBSS015`-`KBSS019`, `KBSS028`-`KBSS032`, `KBSS037`-`KBSS039`, `KBSS041`, `KBSS044`-`KBSS045` | [SQLServer query contents](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html) | TOP, OFFSET/FETCH, JOIN, WHERE, GROUP BY, HAVING, windows, derived tables, UNION ALL/EXCEPT/INTERSECT, SELECT INTO, and ordinary or explicit-column CTEs |
| TOP | `KBSS002`, `KBSS031`-`KBSS032` | [TOP](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html), [INSERT TOP](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/statement/general/insert.html) | constant, `@row_count`, and `PERCENT WITH TIES`; TOP is not normalized into LIMIT |
| INSERT, UPDATE, and DELETE | `KBSS006`-`KBSS010`, `KBSS040`, `KBSS044` | [INSERT](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/statement/general/insert.html), [DML contents](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html) | single- and multi-row VALUES, INSERT SELECT, multiple UPDATE assignments, DELETE, `UPDATE ... FROM ... JOIN`, three-part relations, and `@` binds |
| MERGE | `KBSS030`, `KBSS044` | [MERGE](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html) | the base `MERGE INTO ... USING ... WHEN MATCHED ... WHEN NOT MATCHED BY TARGET ...` form and three-part relations |
| Types, casts, functions, and expressions | `KBSS011`, `KBSS014`-`KBSS016`, `KBSS020`, `KBSS027`, `KBSS033`-`KBSS035`, `KBSS037`-`KBSS038` | [SQLServer types and functions](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html), [V9R4 release notes](https://help.kingbase.com.cn/v9.4.12/intro/releasenotes-external-v9/V9.4.10.html) | INT/BIGINT/DECIMAL/NVARCHAR/BIT/DATETIME2, IDENTITY, CONVERT styles, ISNULL/GETDATE/NEWID, ROW_NUMBER, CASE, string `+`, and COUNT |
| Core relation DDL and SELECT INTO | `KBSS020`-`KBSS024`, `KBSS027`, `KBSS041` | [CREATE/ALTER/DROP and SELECT INTO contents](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/index.html) | CREATE TABLE/VIEW/INDEX, ALTER TABLE ADD, DROP TABLE IF EXISTS, IDENTITY, SQLServer types, and the SELECT INTO target |
| Batches, transactions, and session state | `KBSS025`-`KBSS026`, `KBSS036`, `KBSS042`-`KBSS043` | [SQLServer compatibility table](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/kes-vs-sqlserver/kes-vs-sqlserver-4.html), [BEGIN TRANSACTION](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/language_elements/transaction/begin_transaction.html), [SET DATEFIRST](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/statement/set/set_datefirst.html), [SET TRANSACTION](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/reference/sqlserver/statement/set/set_transaction.html) | BEGIN/COMMIT TRANSACTION, the ksql GO batch boundary, USE, SET DATEFIRST, and transaction isolation |
| Three-part relations | `KBSS044` | [SQLServer compatibility notes](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/kes-vs-sqlserver/kes-vs-sqlserver-4.html) | `database.schema.object` across SELECT/INSERT/UPDATE/DELETE/MERGE, with per-segment bracket state and post-patch recomputation |
| Client placeholders | `KBSS012` | [Go prepared statements and binding](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/application_development/client-interfaces/go/go-3.html), [JDBC PreparedStatement](https://help.kingbase.com.cn/v9.4.12/development/application-develop-guide/application_development/client-interfaces/jdbc/jdbc-2.html) | `?` is ordered as an interface-SQL placeholder and is not merged with `@name` |

## Explicit Exclusions

- DML `OUTPUT` is not in the initial set. The published V9R4 DML grammar uses `RETURNING` as its result clause; the appearance of `OUTPUT` in the keyword table alone does not establish a complete OUTPUT contract.
- GO is covered, while IF/ELSE, TRY/CATCH, WHILE, stored-procedure bodies, and cursors are deferred to a later PL/MSSQL control-flow phase.
- The official V9R4 contents list APPLY, PIVOT, UNPIVOT, and SELECT FOR families, but the existing final SQLServer fixture has no APPLY/PIVOT/UNPIVOT/FOR XML case that can be reused exactly. No new case is invented in this phase; those families remain a documented follow-up gap.
- Every joined DELETE case in the existing fixture is coupled to OUTPUT, so no independent joined DELETE form can be reused while OUTPUT remains excluded. The initial set covers joined UPDATE only.
- `WITH (NOLOCK)` and `OPTION (RECOMPILE)` are excluded because only keyword or contents-page evidence was found, not a complete V9R4 syntax contract.
- Native SQL Server statements not listed in the official V9R4 KingbaseES compatibility material are excluded.
- Bracket-identifier escaping, per-segment quoted flags, and recomputation after patches are verified by the KingbaseES SQLServer matrix.
- GO is a ksql client batch boundary, not a server SQL statement node.
- No V8 SQLServer claim is made because the official V8 material has no such dbmode.

## View and Patch Contract

Official documentation establishes only the SQL forms. The fixture's `query_graph`, selectors, patches, and byte-preserving source behavior are sqlparser project contracts and must not be interpreted as KingbaseES server APIs.
