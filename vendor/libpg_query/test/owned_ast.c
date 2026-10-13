/* Ordinary semantic/ownership regression. Serialization is used solely here as
 * an independent differential oracle, never by the owned parser itself. */
#include "src/pg_query_owned_ast.h"
#include "src/pg_query_observer.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define tests parse_tests
#define testsLength parse_tests_length
#include "parse_tests.c"
#undef tests
#undef testsLength
#define tests parse_opts_tests
#define testsLength parse_opts_tests_length
#include "parse_opts_tests.c"
#undef tests
#undef testsLength
#define tests deparse_tests
#define testsLength deparse_tests_length
#include "deparse_tests.c"
#undef tests
#undef testsLength

static const char *family_tests[] = {
    "", "-- empty source\n", ";;;",
    "SELECT MiXeD, \"CaseName\", '', 0, -3, 1.25, 1e10, true, false, null, B'101', X'ff' FROM MySchema.MyTable",
    "WITH RECURSIVE X(N) AS (VALUES(1) UNION ALL SELECT N+1 FROM X WHERE N<5) SELECT * FROM X",
    "SELECT a, count(*), grouping(a) FROM T GROUP BY GROUPING SETS ((a), ()) HAVING count(*) > 0",
    "SELECT sum(X) OVER (PARTITION BY A ORDER BY B ROWS BETWEEN 2 PRECEDING AND CURRENT ROW) FROM T",
    "SELECT * FROM T LEFT JOIN LATERAL (SELECT * FROM U WHERE U.id=T.id) S ON true ORDER BY T.id NULLS FIRST LIMIT 5 OFFSET 2",
    "SELECT ARRAY[1,2], ROW(1,'a'), CASE WHEN true THEN 1 ELSE 2 END, CAST('1' AS numeric(2,1))",
    "SELECT JSON_OBJECT('a': 1), JSON_ARRAY(1,2), XMLPARSE(DOCUMENT '<x/>')",
    "INSERT INTO T(A,B) VALUES (1,'x'),(2,'y') ON CONFLICT(A) DO UPDATE SET B=EXCLUDED.B RETURNING *",
    "UPDATE T SET (A,B)=(SELECT X,Y FROM U) FROM U WHERE T.id=U.id RETURNING T.*",
    "DELETE FROM T USING U WHERE T.id=U.id RETURNING T.*",
    "MERGE INTO T USING U ON T.id=U.id WHEN MATCHED THEN UPDATE SET A=U.A WHEN NOT MATCHED THEN INSERT(id,A) VALUES(U.id,U.A)",
    "CREATE TABLE T(id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, value text DEFAULT '', ref int REFERENCES U(id), CHECK(id>0)) PARTITION BY RANGE(id)",
    "CREATE TABLE T_child PARTITION OF T FOR VALUES FROM(0) TO(10)",
    "ALTER TABLE T ADD COLUMN C text, ALTER COLUMN A SET NOT NULL, DROP COLUMN B",
    "CREATE UNIQUE INDEX I ON T USING btree(A DESC NULLS LAST) INCLUDE(B) WHERE A>0",
    "CREATE OR REPLACE VIEW V(A) AS SELECT id FROM T WITH LOCAL CHECK OPTION",
    "CREATE MATERIALIZED VIEW V AS SELECT * FROM T WITH NO DATA",
    "CREATE TYPE Mood AS ENUM('happy','sad')",
    "CREATE TYPE Pair AS (A int, B text)",
    "CREATE DOMAIN Positive AS integer CHECK(VALUE>0)",
    "CREATE FUNCTION F(A int) RETURNS int LANGUAGE sql IMMUTABLE AS $$ SELECT A+1 $$",
    "CREATE PROCEDURE P(IN A int) LANGUAGE sql AS $$ SELECT A $$",
    "CREATE TRIGGER TR BEFORE INSERT OR UPDATE ON T FOR EACH ROW EXECUTE FUNCTION F()",
    "CREATE RULE R AS ON INSERT TO T DO INSTEAD NOTHING",
    "CREATE POLICY P ON T FOR SELECT TO PUBLIC USING(A>0)",
    "CREATE SEQUENCE S INCREMENT 2 START 10 CACHE 5",
    "ALTER SEQUENCE S RESTART WITH 20",
    "CREATE SCHEMA S AUTHORIZATION CURRENT_USER",
    "CREATE ROLE R LOGIN NOSUPERUSER",
    "GRANT SELECT(A), UPDATE ON T TO R WITH GRANT OPTION",
    "REVOKE SELECT ON T FROM R CASCADE",
    "COMMENT ON COLUMN T.A IS 'comment'",
    "SECURITY LABEL FOR provider ON TABLE T IS 'label'",
    "BEGIN ISOLATION LEVEL REPEATABLE READ; SAVEPOINT S; ROLLBACK TO S; COMMIT",
    "PREPARE Q(int) AS SELECT * FROM T WHERE A=$1; EXECUTE Q(1); DEALLOCATE Q",
    "DECLARE C NO SCROLL CURSOR FOR SELECT * FROM T; FETCH FORWARD 2 FROM C; CLOSE C",
    "EXPLAIN(ANALYZE false, BUFFERS true, FORMAT JSON) SELECT * FROM T",
    "COPY T(A,B) TO STDOUT WITH(FORMAT CSV, HEADER true)",
    "VACUUM(VERBOSE, ANALYZE) T(A); ANALYZE T; REINDEX TABLE T",
    "SET LOCAL search_path TO S, public; SHOW search_path; RESET ALL",
    "LISTEN Channel; NOTIFY Channel, 'message'; UNLISTEN *",
    "LOCK TABLE T IN ACCESS SHARE MODE; TRUNCATE T RESTART IDENTITY CASCADE",
    "CREATE EXTENSION IF NOT EXISTS hstore WITH SCHEMA public",
    "CREATE SERVER S FOREIGN DATA WRAPPER F OPTIONS(host 'localhost')",
    "CREATE FOREIGN TABLE F(A int) SERVER S OPTIONS(table_name 'remote')",
    "CREATE PUBLICATION P FOR TABLE T WHERE(A>0) WITH(publish='insert')",
    "CREATE SUBSCRIPTION S CONNECTION 'host=localhost' PUBLICATION P WITH(connect=false)",
    "DROP TABLE IF EXISTS T CASCADE",
    "SELECT * FROM", "CREATE TABLE", "SELECT 'unterminated"
};

typedef struct TestCase
{
    const char *sql;
    int options;
    int expect_error;
    PgQueryOwnedParseResult owned;
} TestCase;

typedef struct TestBatch
{
    TestCase *cases;
    size_t count;
    int failed;
} TestBatch;

static void
force_full_observer(const PgQuery__ParseResult *tree, void *context)
{
    int *seen = context;
    *seen = tree != NULL;
}

static void *
parse_on_thread(void *argument)
{
    TestBatch *batch = argument;
    size_t i;
    for (i = 0; i < batch->count; ++i)
    {
        char *source = strdup(batch->cases[i].sql);
        if (source == NULL)
        {
            batch->failed = 1;
            break;
        }
        batch->cases[i].owned =
            pg_query_parse_owned_opts_preserving_identifier_spelling(source, batch->cases[i].options);
        /* The returned AST may not borrow the caller's SQL, either. */
        memset(source, '!', strlen(source));
        free(source);
    }
    /* Release all producer-thread PostgreSQL state before handing ownership
     * to the consumer. Packing and destruction below run on another thread. */
    pg_query_exit();
    return NULL;
}

static int
compare_case(TestCase *test)
{
    int observer_seen = 0;
    PgQueryProtobufParseResult legacy =
        pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
            test->sql, test->options, force_full_observer, &observer_seen);
    PgQueryOwnedParseResult owned = test->owned;
    int failed = 0;

    if (legacy.error != NULL)
    {
        if (!test->expect_error || owned.error == NULL || owned.parse_tree != NULL ||
            strcmp(legacy.error->message, owned.error->message) != 0 ||
            legacy.error->cursorpos != owned.error->cursorpos)
            failed = 1;
    }
    else if (test->expect_error || !observer_seen || owned.error != NULL || owned.parse_tree == NULL ||
             !protobuf_c_message_check(&owned.parse_tree->base))
        failed = 1;
    else
    {
        size_t size = pg_query__parse_result__get_packed_size(owned.parse_tree);
        unsigned char *bytes = malloc(size ? size : 1U);
        if (bytes == NULL)
            failed = 1;
        else
        {
            size_t actual = pg_query__parse_result__pack(owned.parse_tree, bytes);
            failed = actual != size || size != legacy.parse_tree.len ||
                     memcmp(bytes, legacy.parse_tree.data, size) != 0;
            free(bytes);
        }
    }
    /* Exercise ordinary mutation ownership after a new native parse has
     * reused the consumer's context and the producer's context is gone. */
    if (!failed && owned.parse_tree != NULL && owned.parse_tree->n_stmts == 1U &&
        owned.parse_tree->stmts[0]->stmt != NULL &&
        owned.parse_tree->stmts[0]->stmt->node_case == PG_QUERY__NODE__NODE_INSERT_STMT)
    {
        PgQuery__InsertStmt *insert = owned.parse_tree->stmts[0]->stmt->insert_stmt;
        if (insert->relation != NULL && insert->relation->relname != protobuf_c_empty_string)
        {
            char *replacement = strdup("MutatedRelation");
            if (replacement == NULL)
                failed = 1;
            else
            {
                free(insert->relation->relname);
                insert->relation->relname = replacement;
            }
        }
        if (insert->select_stmt != NULL &&
            insert->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT)
        {
            PgQuery__SelectStmt *select = insert->select_stmt->select_stmt;
            if (select->n_values_lists > 1U)
            {
                pg_query__node__free_unpacked(select->values_lists[--select->n_values_lists], NULL);
                select->values_lists[select->n_values_lists] = NULL;
            }
        }
        if (!protobuf_c_message_check(&owned.parse_tree->base))
            failed = 1;
    }
    if (failed)
        fprintf(stderr, "owned AST differential failed (options %d): %s\n", test->options, test->sql);
    pg_query_free_protobuf_parse_result(legacy);
    pg_query_free_owned_parse_result(owned);
    memset(&test->owned, 0, sizeof(test->owned));
    return failed;
}

int
main(void)
{
    size_t i, index = 0U;
    TestBatch batch = {0};
    pthread_t worker;
    int failed = 0;
    batch.count = sizeof(parse_tests) / sizeof(*parse_tests) / 2U +
        sizeof(parse_opts_tests) / sizeof(*parse_opts_tests) / 3U +
        sizeof(deparse_tests) / sizeof(*deparse_tests) +
        sizeof(family_tests) / sizeof(*family_tests);
    batch.cases = calloc(batch.count, sizeof(*batch.cases));
    if (batch.cases == NULL)
        return 1;
    for (i = 0; i < sizeof(parse_tests) / sizeof(*parse_tests); i += 2U)
        batch.cases[index++].sql = parse_tests[i];
    for (i = 0; i < sizeof(parse_opts_tests) / sizeof(*parse_opts_tests); i += 3U)
    {
        batch.cases[index].sql = parse_opts_tests[i];
        batch.cases[index++].options = atoi(parse_opts_tests[i + 1U]);
    }
    for (i = 0; i < sizeof(deparse_tests) / sizeof(*deparse_tests); ++i)
        batch.cases[index++].sql = deparse_tests[i];
    for (i = 0; i < sizeof(family_tests) / sizeof(*family_tests); ++i)
    {
        batch.cases[index].sql = family_tests[i];
        batch.cases[index++].expect_error = i >= sizeof(family_tests) / sizeof(*family_tests) - 3U;
    }
    if (pthread_create(&worker, NULL, parse_on_thread, &batch) != 0)
    {
        free(batch.cases);
        return 1;
    }
    pthread_join(worker, NULL);
    if (batch.failed)
        failed = 1;
    for (i = 0; i < batch.count; ++i)
        failed |= compare_case(&batch.cases[i]);
    printf("owned AST: %zu ordinary differential cases, producer thread exited before comparison/free: %s\n",
        batch.count, failed ? "FAIL" : "PASS");
    free(batch.cases);
    pg_query_exit();
    return failed;
}
