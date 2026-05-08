# Oracle Dialect Case Matrix

This file records regression cases for the Oracle dialect conversion layer. The executable fixture is `tests/cases/oracle_dialect_input.json`; `tests/unit/test_oracle_dialect_case_matrix.c` verifies parsing, SQL View JSON, deparse output, and error codes.

## Supported Cases

| ID | Case | Coverage |
| --- | --- | --- |
| O001 | `SELECT` + `NVL` + named bind | Oracle `:name` bind conversion and restoration |
| O002 | q-quoted string | `q'[...]'` conversion to a safe string literal |
| O004 | `MINUS` | bidirectional Oracle `MINUS` and core `EXCEPT` conversion |
| O005 | `OFFSET ... FETCH` | Oracle pagination syntax |
| O006 | `ROWNUM` predicate | pseudo-column in a predicate expression |
| O007 | multi-table JOIN + bind | table, selected-column, join-column, and predicate-column extraction |
| O008 | `INSERT ... VALUES` + bind | inserted-column extraction and bind restoration |
| O009 | multi-row `INSERT ... VALUES` | multi-row value lists |
| O010 | `INSERT ... SELECT` | target table, source table, and inserted-column extraction |
| O011 | `UPDATE` + multiple assignments + bind | updated-column, predicate-column, and bind restoration |
| O012 | `DELETE` + predicate | conditional delete |
| O013 | repeated named bind | one internal parameter number for the same bind name |
| O014 | positional bind | `:1` and `:2` conversion and restoration |
| O015 | `DATE` literal | date literal |
| O016 | `CASE` expression | conditional expression |
| O017 | `EXISTS` subquery | subquery table and predicate-column extraction |
| O018 | `GROUP BY` + `HAVING` | aggregate query |
| O019 | `UNION ALL` | set query |
| O020 | `INTERSECT` | set query |
| O021 | `MERGE` | basic merge statement |
| O022 | `CREATE TABLE` | table creation with common Oracle type names |
| O023 | `CREATE SEQUENCE` | sequence creation |
| O024 | `CREATE OR REPLACE VIEW` | view creation |
| O025 | `DROP TABLE` | table drop |
| O026 | `TRUNCATE TABLE` | table truncation |
| O027 | transaction control | `SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, and `COMMIT` |
| O028 | privilege statements | `GRANT` and `REVOKE` |
| O029 | comment statement | `COMMENT ON TABLE` |
| O030 | `FOR UPDATE NOWAIT` | row-locking query |
| O031 | `DECODE` + `SYSDATE` | common Oracle function and pseudo-column |
| O032 | `ROW_NUMBER() OVER` | analytic function |
| O033 | `TIMESTAMP` literal | timestamp literal |
| O034 | quoted identifiers | case-sensitive object and column names |
| O035 | `ALTER TABLE ... ADD` | add column |
| O036 | `CREATE INDEX` | create index |
| O037 | `DROP INDEX` | drop index |
| O038 | `IN` + multiple binds | multiple binds in a predicate list |
| O039 | `DELETE` + `DATE` literal | conditional delete and date literal |
| O040 | materialized view | compatible materialized-view syntax |
| O041 | unsupported keywords in string | `RETURNING`, `@`, and `(+)` inside strings do not trigger unsupported |
| O042 | unsupported keywords in comment | `CONNECT BY` inside comments does not trigger unsupported |

## Explicitly Unsupported Cases

The following constructs have Oracle-specific semantics. The conversion layer returns `SQLPARSER_STATUS_UNSUPPORTED` instead of producing SQL with unreliable semantics.

| ID | Case | Reason |
| --- | --- | --- |
| O003 | national q-quoted string | national character-set semantics are not silently downgraded |
| OU001 | `CONNECT BY` | non-equivalent hierarchical query semantics |
| OU002 | `(+)` | non-equivalent legacy outer join semantics |
| OU003 | `INSERT ALL` | non-equivalent multi-table insert semantics |
| OU004 | `RETURNING ... INTO` | non-equivalent host-variable return target |
| OU005 | PL/SQL block | outside SQL statement conversion scope |
| OU006 | `CREATE PROCEDURE` | PL/SQL unit |
| OU007 | `CREATE PACKAGE` | PL/SQL unit |
| OU008 | `PIVOT` | Oracle table transformation |
| OU009 | `UNPIVOT` | Oracle table transformation |
| OU010 | `MODEL` | Oracle model clause |
| OU011 | flashback query | Oracle flashback query semantics |
| OU012 | `MATCH_RECOGNIZE` | row pattern recognition semantics |
| OU013 | `ALTER SESSION` | session-level semantics |
| OU014 | `CREATE SYNONYM` | Oracle synonym object |
| OU015 | database link | remote object reference semantics |
| OU016 | `EXPLAIN PLAN FOR` | Oracle explain plan output semantics |
| OU017 | `CONNECT_BY_ROOT` | hierarchical-query expression |
| OU018 | `INSERT FIRST` | conditional multi-table insert semantics |

## Maintenance

- New Oracle support must update `tests/cases/oracle_dialect_input.json`, this matrix, and executable regression tests.
- Oracle-only syntax that cannot be mapped with equivalent semantics must return `SQLPARSER_STATUS_UNSUPPORTED`.
