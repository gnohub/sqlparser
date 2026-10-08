/* Per-call live-tree validation, byte parity, lifetime and backend fallback. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"
#include "src/pg_query_observer.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

#ifdef SQLPARSER_OBSERVER_TEST_WRAPPERS
static int disable_observer, fail_unpack, fail_handle_alloc;
static size_t observed_parse_calls, unpack_calls;
void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t size)
{
    if (fail_handle_alloc && count == 1U && size == sizeof(sqlparser_handle_t)) return NULL;
    return __real_calloc(count, size);
}
PgQueryProtobufParseResult
__real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
    const char *, int, PgQueryProtobufObserver, void *);
PgQueryProtobufParseResult
__wrap_pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
    const char *sql, int options, PgQueryProtobufObserver observer, void *context)
{
    observed_parse_calls++;
    return __real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        sql, options, disable_observer ? NULL : observer, context);
}
PgQueryProtobufParseResult
__real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
    const char *, int, PgQueryProtobufObserver, void *, size_t *, int *, PgQueryNativeScalarInsertProof *);
PgQueryProtobufParseResult
__wrap_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
    const char *sql, int options, PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified, PgQueryNativeScalarInsertProof *native_proof)
{
    observed_parse_calls++;
    if (disable_observer) {
        *statement_count = 0U;
        *certified = 0;
        if (native_proof != NULL) memset(native_proof, 0, sizeof(*native_proof));
        return __real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
            sql, options, NULL, context);
    }
    return __real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
        sql, options, observer, context, statement_count, certified, native_proof);
}
PgQueryProtobufParseResult
__real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan(const char *, size_t, int, PgQueryProtobufObserver,
    void *, size_t *, int *, PgQueryNativeScalarInsertProof *, PgQueryMysqlOwnedScalarInsertPlan *);
PgQueryProtobufParseResult
__wrap_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan(
    const char *sql, size_t length, int options, PgQueryProtobufObserver observer, void *context,
    size_t *count, int *certified, PgQueryNativeScalarInsertProof *proof,
    PgQueryMysqlOwnedScalarInsertPlan *plan)
{
    if (disable_observer) {
        if (plan) memset(plan, 0, sizeof(*plan));
        return __wrap_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
            sql, options, observer, context, count, certified, proof);
    }
    observed_parse_calls++;
    return __real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan(
        sql, length, options, observer, context, count, certified, proof, plan);
}

PgQuery__ParseResult *__real_pg_query__parse_result__unpack(
    ProtobufCAllocator *, size_t, const uint8_t *);
PgQuery__ParseResult *__wrap_pg_query__parse_result__unpack(
    ProtobufCAllocator *allocator, size_t size, const uint8_t *bytes)
{
    unpack_calls++;
    if (fail_unpack) return NULL;
    return __real_pg_query__parse_result__unpack(allocator, size, bytes);
}
#endif

static int has_live_observer;

typedef struct {
    size_t calls, count, size;
    uint8_t *bytes;
} observation_t;

static void observe(const PgQuery__ParseResult *tree, void *context)
{
    observation_t *result = context;
    result->calls++;
    CHECK(tree->base.descriptor == &pg_query__parse_result__descriptor);
    result->count = tree->n_stmts;
    result->size = pg_query__parse_result__get_packed_size(tree);
    result->bytes = malloc(result->size != 0U ? result->size : 1U);
    CHECK(result->bytes != NULL);
    CHECK(pg_query__parse_result__pack(tree, result->bytes) == result->size);
    /* Keep serialized bytes only. All borrowed tree pointers die here. */
}

static void check_observed_bytes(const char *sql, size_t expected_count, int valid)
{
    observation_t observation = {0};
    PgQueryProtobufParseResult normal, observed;
    sqlparser_pg_query_prepare();
    normal = pg_query_parse_protobuf_opts_preserving_identifier_spelling(
        sql, PG_QUERY_PARSE_DEFAULT);
    observed = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        sql, PG_QUERY_PARSE_DEFAULT, observe, &observation);
    CHECK((normal.error == NULL) == valid);
    CHECK((observed.error == NULL) == valid);
    CHECK(normal.parse_tree.len == observed.parse_tree.len);
    CHECK(memcmp(normal.parse_tree.data, observed.parse_tree.data, normal.parse_tree.len) == 0);
    if (valid) {
        CHECK(observation.calls <= 1U);
        has_live_observer = observation.calls != 0U;
        if (has_live_observer) {
            CHECK(observation.count == expected_count);
            CHECK(observation.size == observed.parse_tree.len);
            CHECK(memcmp(observation.bytes, observed.parse_tree.data, observation.size) == 0);
        }
    } else {
        CHECK(observation.calls == 0U);
        CHECK(normal.error->cursorpos == observed.error->cursorpos);
        CHECK(strcmp(normal.error->message, observed.error->message) == 0);
    }
    free(observation.bytes);
    pg_query_free_protobuf_parse_result(normal);
    pg_query_free_protobuf_parse_result(observed);
}

static void check_parse_parity(const char *sql, size_t statement_limit)
{
    sqlparser_parse_options_t options;
    sqlparser_error_t observed_error, fallback_error;
    sqlparser_status_t observed_status, fallback_status;
    sqlparser_handle_t *observed = NULL, *fallback = NULL, *churn = NULL;
    char *observed_sql = NULL, *fallback_sql = NULL;
    size_t i;
    sqlparser_parse_options_default(&options);
    options.dialect = SQLPARSER_DIALECT_MYSQL;
    if (statement_limit != 0U) options.limits.max_statement_count = statement_limit;
#ifdef SQLPARSER_OBSERVER_TEST_WRAPPERS
    disable_observer = 0;
    unpack_calls = 0;
    fail_unpack = has_live_observer; /* A live validation must never unpack. */
#endif
    observed_status = sqlparser_parse_with_options(sql, &options, &observed, &observed_error);
#ifdef SQLPARSER_OBSERVER_TEST_WRAPPERS
    if (has_live_observer) CHECK(unpack_calls == 0U);
    fail_unpack = 0;
    disable_observer = 1;
    unpack_calls = 0U;
#endif
    fallback_status = sqlparser_parse_with_options(sql, &options, &fallback, &fallback_error);
#ifdef SQLPARSER_OBSERVER_TEST_WRAPPERS
    if (observed_status == SQLPARSER_STATUS_OK) CHECK(unpack_calls == 1U);
    disable_observer = 0;
#endif
    CHECK(observed_status == fallback_status);
    CHECK(memcmp(&observed_error, &fallback_error, sizeof(observed_error)) == 0);
    if (observed_status == SQLPARSER_STATUS_OK) {
        CHECK(observed != NULL && fallback != NULL);
        CHECK(observed->ast == NULL && fallback->ast == NULL);
        CHECK(observed->statement_count == fallback->statement_count);
        CHECK(observed->parse_tree.len == fallback->parse_tree.len);
        CHECK(memcmp(observed->parse_tree.data, fallback->parse_tree.data,
            observed->parse_tree.len) == 0);
        /* Reuse parser contexts before any lazy unpack/graph/deparse work. */
        for (i = 0; i < 20U; i++) {
            CHECK(sqlparser_parse_with_options("SELECT 'context-churn', 123", &options,
                &churn, NULL) == SQLPARSER_STATUS_OK);
            sqlparser_handle_destroy(churn);
        }
        CHECK(sqlparser_deparse(observed, &observed_sql, NULL) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(fallback, &fallback_sql, NULL) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(observed_sql, fallback_sql) == 0);
        sqlparser_string_free(observed_sql);
        sqlparser_string_free(fallback_sql);
        if (observed->statement_count != 0U) {
            sqlparser_query_graph_view_t graph;
            CHECK(sqlparser_statement_query_graph(observed, 0U, &graph, NULL) == SQLPARSER_STATUS_OK);
        }
    } else {
        CHECK(observed == NULL && fallback == NULL);
    }
    sqlparser_handle_destroy(observed);
    sqlparser_handle_destroy(fallback);
    observed = NULL;
    CHECK(sqlparser_parse_with_options(sql, &options, &observed, NULL) == observed_status);
    sqlparser_handle_destroy(observed);
}

int main(void)
{
    static const char *const queries[] = {
        "SELECT 1", "SELECT 1; SELECT 'Two'", "/* comments only */",
        "INSERT INTO t(id,v) VALUES (1,'one'),(2,'two')",
        "INSERT IGNORE INTO t(id) VALUES (1)",
        "INSERT INTO t(id,v) VALUES(1,'one') ON DUPLICATE KEY UPDATE v=VALUES(v)",
        "SELECT ? AS `Case`, N'National', 'Quoted'", "SELECT )", "SELECT 'unterminated",
        "SELECT x FROM t CONNECT BY PRIOR id = parent_id", "SELECT PRIOR x FROM t",
        "MERGE INTO t USING s ON t.id=s.id WHEN MATCHED THEN UPDATE SET v=s.v",
        "UPDATE t SET v=1 ORDER BY id LIMIT 1", "SELECT 1 LIMIT 0, 1"
    };
    size_t i;
    char *large = malloc(71000U);
    CHECK(large != NULL);
    check_observed_bytes("SELECT 1; SELECT 'Case'", 2U, 1);
    check_observed_bytes("/* no statements */", 0U, 1);
    check_observed_bytes("SELECT )", 0U, 0);
    for (i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) check_parse_parity(queries[i], 0U);
    check_parse_parity("SELECT 1; SELECT 2", 1U);
    strcpy(large, "SELECT '");
    memset(large + 8U, 'x', 70000U);
    strcpy(large + 70008U, "'");
    check_parse_parity(large, 0U);
    free(large);
#ifdef SQLPARSER_OBSERVER_TEST_WRAPPERS
    {
        sqlparser_parse_options_t options;
        sqlparser_handle_t *handle = NULL;
        sqlparser_error_t error;
        sqlparser_parse_options_default(&options);
        options.dialect = SQLPARSER_DIALECT_MYSQL;
        disable_observer = 1;
        fail_unpack = 1;
        CHECK(sqlparser_parse_with_options("SELECT 1", &options, &handle, &error) ==
            SQLPARSER_STATUS_INTERNAL_ERROR);
        CHECK(handle == NULL && strcmp(error.message, "failed to unpack parse tree protobuf") == 0);
        disable_observer = 0;
        fail_unpack = 0;
        CHECK(observed_parse_calls > 0U);
        /* A subsequent handle allocation failure takes precedence over any
         * earlier live validation error, exactly as on the fallback path. */
        fail_handle_alloc = 1;
        for (disable_observer = 0; disable_observer <= 1; disable_observer++) {
            CHECK(sqlparser_parse_with_options("SELECT PRIOR x FROM t", &options,
                &handle, &error) == SQLPARSER_STATUS_NO_MEMORY);
            CHECK(handle == NULL && error.code == SQLPARSER_STATUS_NO_MEMORY);
            CHECK(strcmp(error.message, "out of memory") == 0);
        }
        fail_handle_alloc = 0;
        disable_observer = 0;
    }
#endif
    puts("MySQL live observer, error parity, lifetime and fallback checks passed");
    return 0;
}
