# Unified KingbaseES Oracle Compatibility Mode Case Matrix

## Baseline and scope

- Product baseline: the syntax union of KingbaseES `V008R006C009B0014` and the V9R2C12 Oracle-compatible edition.
- The public entry is uniformly named `kingbase-oracle`; no version field or version-based dispatch is defined.
- Fixture: `tests/cases/kingbase_oracle_dialect_input.json`.
- Runner: `tests/unit/test_kingbase_oracle_dialect_case_matrix.c`.
- The fixture currently contains 43 cases with `status = "final"` and 157 independent patches.
- Every expected View, selector, patch, and deparse result is reused from an existing Oracle/Base fixture. Syntax without V8/V9 official evidence or an exactly reusable expectation is excluded.

Official baseline:

- [V8R6C9B14 manual downloads](https://help.kingbase.com.cn/v8/download.html)
- [V8 Oracle compatibility specification](https://help.kingbase.com.cn/v8/PDF/KingbaseES%E4%B8%8EOracle%E7%9A%84%E5%85%BC%E5%AE%B9%E6%80%A7%E8%AF%B4%E6%98%8E.pdf)
- [V9 Oracle SQL compatibility specification](https://help.kingbase.com.cn/v9/development/develop-transfer/kes-vs-oracle/kes-vs-oracle-3.html)
- [V9R2 Oracle-compatible INSERT](https://help.kingbase.com.cn/v9.2.10/development/application-develop-guide/reference/oracle/sql/statement/insert.html)
- [V9 Oracle-to-KingbaseES migration guide](https://help.kingbase.com.cn/v9/PDF/Oracle%E8%87%B3KingbaseES%E8%BF%81%E7%A7%BB%E6%9C%80%E4%BD%B3%E5%AE%9E%E8%B7%B5.pdf)

## Lexical forms, identifiers, and binds

Official references:

- [SQL basic elements](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/datatype.html)
- [Expressions and placeholder expressions](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Expressions.html)
- [Go driver `$N`, `?`, and `:NAME` placeholders](https://help.kingbase.com.cn/v8/development/client-interfaces/go/go-3.html)

| ID | Case | Coverage |
| --- | --- | --- |
| KO002 | `kingbase-oracle-select-bind-nvl` | Oracle named bind, function arguments, literal, and field ownership |
| KO003 | `kingbase-oracle-q-quoted-string` | `q'[...]'` delimited literal and source restoration |
| KO004 | `kingbase-oracle-national-q-quoted-string` | `nq'{...}'` national delimited literal |
| KO020 | `kingbase-oracle-quoted-identifiers` | Double-quoted relation/column names and quoted flags |
| KO024 | `kingbase-oracle-insert-question-params` | JDBC `?` positional placeholders |
| KO025 | `kingbase-oracle-select-positional-bind-pair` | Oracle `:1` and `:2` positional binds |
| KO038 | `kingbase-oracle-dollar-quoted-string-global-bind-position` | Placeholder-like text protected by dollar quoting and global `$N` positions |

## SELECT, CTE, JOIN, and pagination

Official references:

- [Queries, subqueries, hierarchical queries, DUAL, and PIVOT/UNPIVOT](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Queries_and_Subqueries.html)
- [SELECT, LIMIT/OFFSET/FETCH, and Oracle-mode SELECT](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html)
- [Operators and set operations](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Operators.html)
- [V8R6 SQL capability scope](https://help.kingbase.com.cn/v8/development/develop-transfer/transplant-r3/transplant-r3-2.html)

| ID | Case | Coverage |
| --- | --- | --- |
| KO005 | `kingbase-oracle-minus-set-operator` | Oracle `MINUS` set operation |
| KO006 | `kingbase-oracle-offset-fetch` | `OFFSET ... FETCH NEXT` pagination |
| KO007 | `kingbase-oracle-rownum-filter` | `ROWNUM` pseudo-column predicate |
| KO008 | `kingbase-oracle-join-bind` | JOIN, ON, WHERE, and named bind ownership |
| KO017 | `kingbase-oracle-for-update-nowait` | `FOR UPDATE NOWAIT` |
| KO019 | `kingbase-oracle-analytic-row-number` | `ROW_NUMBER() OVER` analytic function |
| KO029 | `kingbase-oracle-database-link-schema-alias-bind` | `schema.table@dblink`, alias, and bind |
| KO030 | `kingbase-oracle-union-all-root-cte-scope` | Root CTE scope across `UNION ALL` branches |
| KO034 | `kingbase-oracle-cte-explicit-column-set-boundary` | Positional CTE column mapping across a set boundary |
| KO037 | `kingbase-oracle-select-limit-dollar-params` | KingbaseES `LIMIT/OFFSET` with `$N` binds |
| KO040 | `kingbase-oracle-cte-explicit-column-set-recursive-boundary` | Ordinary CTE, `WITH RECURSIVE`, and explicit columns |

## INSERT, UPSERT, MERGE, UPDATE, and DELETE

Official references:

- [Oracle-mode INSERT, INSERT ALL/FIRST, ON CONFLICT, and RETURNING](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_9.html)
- [MERGE and UPDATE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_10.html)
- [Oracle SQL/PLSQL compatibility scope](https://help.kingbase.com.cn/v8/PDF/KingbaseES%E4%B8%8EOracle%E7%9A%84%E5%85%BC%E5%AE%B9%E6%80%A7%E8%AF%B4%E6%98%8E.pdf)

| ID | Case | Coverage |
| --- | --- | --- |
| KO001 | `kingbase-oracle-merge-insert-structured-pair-rewrite` | Structured MERGE not-matched target/value patches |
| KO009 | `kingbase-oracle-insert-values-bind` | Single-row INSERT, target columns, named bind, and literal |
| KO010 | `kingbase-oracle-insert-returning-rowid-into-bind` | `RETURNING ROWID INTO :bind` |
| KO011 | `kingbase-oracle-insert-values-multi-row` | Multi-row `VALUES` |
| KO012 | `kingbase-oracle-update-bind` | Multiple assignments, WHERE, and named binds |
| KO013 | `kingbase-oracle-delete-conditional` | DELETE target and compound condition |
| KO026 | `kingbase-oracle-insert-all` | Unconditional multi-target `INSERT ALL` |
| KO028 | `kingbase-oracle-insert-first` | Conditional `INSERT FIRST` branch |
| KO036 | `kingbase-oracle-insert-on-conflict-update` | `ON CONFLICT DO UPDATE`, `EXCLUDED`, and `RETURNING` |
| KO039 | `kingbase-oracle-on-conflict-assignment-list-contract` | UPSERT assignment insertion, replacement, and deletion |

## DDL and object references

Official references:

- [CREATE SEQUENCE, SYNONYM, TABLE, VIEW, and DELETE](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_6.html)
- [ALTER TABLE and COMMENT](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_3.html)
- [CREATE DATABASE LINK, DIRECTORY, and INDEX](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_4.html)
- [CREATE MATERIALIZED VIEW](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Statements_5.html)

| ID | Case | Coverage |
| --- | --- | --- |
| KO014 | `kingbase-oracle-create-table` | `NUMBER`, `VARCHAR2`, and `DATE` types |
| KO015 | `kingbase-oracle-create-sequence` | Oracle-style sequence options |
| KO016 | `kingbase-oracle-create-view` | `CREATE OR REPLACE VIEW` target and source relations |
| KO021 | `kingbase-oracle-alter-table-add-column` | `ALTER TABLE ... ADD` |
| KO022 | `kingbase-oracle-create-index` | Index target relation and columns |
| KO023 | `kingbase-oracle-create-materialized-view-compatible-form` | Materialized-view target and source query |
| KO027 | `kingbase-oracle-create-synonym` | Private synonym |
| KO033 | `kingbase-oracle-ddl-relation-create-table-foreign-key` | Quoted schema/table, referenced relation, and DDL graph |

## Functions, expressions, and hierarchical queries

Official references:

- [KingbaseES SQL functions](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Function.html)
- [Hierarchical queries](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/SQL_Queries_and_Subqueries.html)
- [Hierarchical-query operators](https://help.kingbase.com.cn/v8/development/sql-plsql/sql/Operators.html)

| ID | Case | Coverage |
| --- | --- | --- |
| KO018 | `kingbase-oracle-decode-sysdate` | `DECODE`, `SYSDATE`, and literal arguments |
| KO031 | `kingbase-oracle-hierarchical-basic-level-bind` | `START WITH`, `CONNECT BY PRIOR`, `LEVEL`, and bind |
| KO032 | `kingbase-oracle-hierarchical-nocycle-pseudocolumns` | `NOCYCLE`, `CONNECT_BY_ISLEAF`, and `CONNECT_BY_ISCYCLE` |
| KO035 | `kingbase-oracle-predicate-expression-like-concat-mixed-args` | KingbaseES variadic `CONCAT`, nested functions, and RHS argument patches |

## V9R2 incremental coverage

Official references:

- [V9 Oracle SQL compatibility specification](https://help.kingbase.com.cn/v9/development/develop-transfer/kes-vs-oracle/kes-vs-oracle-3.html)
- [V9 Oracle-to-KingbaseES migration guide](https://help.kingbase.com.cn/v9/PDF/Oracle%E8%87%B3KingbaseES%E8%BF%81%E7%A7%BB%E6%9C%80%E4%BD%B3%E5%AE%9E%E8%B7%B5.pdf)

| ID | Case | Coverage |
| --- | --- | --- |
| KO041 | `kingbase-oracle-merge-update-where-delete-where-conditional-insert` | MERGE matched UPDATE, action WHERE, attached DELETE WHERE, and conditional INSERT |
| KO042 | `kingbase-oracle-p3-update-multiple-alias-qualified-assignments` | Multiple alias-qualified UPDATE assignments |
| KO043 | `kingbase-oracle-database-link-update-target` | Remote DBLink UPDATE target, binds, and patches |

## Known boundaries

- `kingbase-oracle` accepts the covered V8/V9 syntax union; cases, Views, and selectors carry no version information.
- `INSERT ALL/FIRST` covers only the officially documented basic capability. Targets using `PARTITION`, `SUBPARTITION`, materialized views, or `@dblink` are excluded.
- `REPLACE INTO`, multi-table DELETE/UPDATE, and `#` comments belong to KingbaseES MySQL compatibility mode and are excluded.
- The V9 compatibility table marks DELETE hints as unsupported, while a migration document differs; no hint case is added from that conflicting evidence.
- FORCE VIEW, SAMPLE, FLASHBACK, DML partitioning, and JSON extensions have official documentation, but the current Oracle/Base fixtures provide no exactly reusable expectations; this batch adds no speculative cases for them.
- PL/SQL anonymous blocks, procedures, functions, packages, and triggers form a separate procedural-language boundary and are not promised by this SQL-structure batch.
- ksql `\set SQLTERM` and `/` are client-side script controls, not server SQL input.
