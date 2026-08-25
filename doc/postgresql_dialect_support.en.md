# PostgreSQL Dialect Support

`SQLPARSER_DIALECT_POSTGRESQL` is the default dialect. The parser kernel is the
pinned in-tree `libpg_query 17-6.2.2`, which uses the PostgreSQL 17 parser
baseline.

## Supported Scope

The PostgreSQL dialect supports PostgreSQL syntax forms represented by the
current parser kernel. The executable case matrix defines the support boundary:

- `SELECT`, `WITH`, subqueries, joins, `WHERE`, `GROUP BY`, `HAVING`,
  `ORDER BY`, and `LIMIT`
- `UNION ALL`, `EXCEPT`, and `INTERSECT`
- `CASE`, window functions, function calls, and casts
- `INSERT VALUES`, multi-row `INSERT`, and `INSERT SELECT`
- `ON CONFLICT DO UPDATE` and `RETURNING`
- `UPDATE`, `UPDATE FROM`, `DELETE`, and `DELETE USING`
- `MERGE`, including an independent `WHEN MATCHED ... THEN DELETE` action;
  `insert_column` on a not-matched INSERT supports column-only, value-only, and
  paired modes, and existing VALUES cells can be replaced independently
- common relation DDL: `CREATE TABLE`, `CREATE FOREIGN TABLE`,
  `CREATE TABLE AS`, `CREATE VIEW`, `CREATE MATERIALIZED VIEW`, and `SELECT INTO`
- `ALTER TABLE` and `ALTER FOREIGN TABLE` RENAME and ADD/DROP COLUMN, plus FK,
  LIKE, INHERITS, and ATTACH/DETACH PARTITION
- `CREATE INDEX`, `DROP INDEX`, `DROP TABLE`, `DROP VIEW`,
  `DROP MATERIALIZED VIEW`, and `TRUNCATE TABLE`
- `CREATE SCHEMA` and `DROP SCHEMA`
- `COMMENT ON`, `GRANT`, and `REVOKE`
- `EXPLAIN`, `COPY`, `LOCK`, `ANALYZE`, and `VACUUM`
- `LISTEN`, `NOTIFY`, and `UNLISTEN`
- `CREATE EXTENSION` and `DROP EXTENSION`
- transaction control, `SAVEPOINT`, `ROLLBACK TO SAVEPOINT`, and
  `RELEASE SAVEPOINT`
- `CALL` and `DO`
- multi-statement parsing and deparsing
- `SET search_path`, `SET LOCAL search_path`, and `SET SCHEMA`
- PostgreSQL `$n` parameter placeholders
- `PREPARE`, `EXECUTE`, and `DEALLOCATE`
- national string literals: `N'...'`

## Explicitly Unsupported Scope

The PostgreSQL default dialect does not maintain a separate feature-level
negative list. Parse failures normally come from invalid SQL, PostgreSQL version
differences outside the pinned parser kernel, or specialized structures not yet
exposed by the public query graph.

## Public Output Rules

- `sqlparser_deparse()` emits PostgreSQL-compatible SQL.
- View JSON emits structured results through the common `query_graph`.
- An omitted MERGE INSERT target-column list still emits
  `target_list_selector`. A column-only patch can materialize that list, a
  value-only patch can append a VALUES cell while keeping the list omitted, and
  an explicit list continues to support paired insertion on both sides. If an
  explicit list exists when the patch batch finishes, the core patch API
  validates equal column/value widths and rolls back the batch on failure.
- Query Graph uses `alias_quoted_identifier` for double-quoted relation aliases
  and `output_quoted_identifier` for explicit double-quoted output aliases or
  inherited double-quoted field names when no explicit alias exists. View JSON
  emits either key only when its value is `true`.
- Relation qualification reports delimiter state per segment through
  `database_quoted_identifier`, `schema_quoted_identifier`, the existing object
  `quoted_identifier`, and `link_quoted_identifier` when a database link exists.
  DML target columns use `dml_column.quoted_identifier`. Each flag describes
  only its corresponding segment; View JSON omits the key for an unquoted or
  absent segment, so case cannot be inferred from identifier spelling. The
  PostgreSQL entry currently has no database-link relation, so the link flag is
  not applicable.
- Relation DDL emits a root block with `kind = "ddl"` and uses
  `ddl_role = "target"|"reference"` to distinguish changed objects from FK,
  LIKE, INHERITS, and partition references. Query-backed targets point through
  `source_block` to a SELECT block. CREATE/ALTER/RENAME/DROP FOREIGN TABLE uses
  the same target-role contract. Multi-object DROP targets have no relation
  selector; identically spelled quoted/unquoted segments retain exact
  source-token delimiter state. This contract is proven by the current entry's
  fixture and is not automatically inherited by compatibility entries.

## Regression Cases

The PostgreSQL support boundary is defined by:

- `tests/cases/sql_batch_input.json`
- `tests/cases/sql_case_matrix.en.md`
- `tests/unit/test_api_case_matrix.c`
- `tests/unit/test_core_api.c`
- `tests/unit/test_stability.c`

The current PostgreSQL matrix contains 224 cases and 755 independent patches,
all with `status = "final"`.
