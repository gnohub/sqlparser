/* SQLValueFunction is a scalar-only native node. Admission to the internal
 * validation certificate must leave public/native wire and fallback unchanged. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "src/pg_query_observer.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

typedef struct { size_t calls, statements; } observation;
static void observe(const PgQuery__ParseResult *tree, void *context)
{
    observation *o = context;
    o->calls++;
    o->statements = tree->n_stmts;
}

static void same_native_wire(const char *sql, int expected_certificate)
{
    observation actual_observed = {0}, reference_observed = {0};
    size_t statements = 0U;
    int certified = 0;
    PgQueryProtobufParseResult actual =
        pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(
            sql, PG_QUERY_PARSE_DEFAULT, observe, &actual_observed,
            &statements, &certified);
    PgQueryProtobufParseResult reference =
        pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
            sql, PG_QUERY_PARSE_DEFAULT, observe, &reference_observed);
    CHECK(actual.error == NULL && reference.error == NULL);
    if (certified != expected_certificate || reference_observed.calls != 1U)
        fprintf(stderr, "SQL=%s certified=%d expected=%d observed=%zu\n",
            sql, certified, expected_certificate, reference_observed.calls);
    CHECK(certified == expected_certificate && reference_observed.calls == 1U);
    CHECK(actual.parse_tree.data != NULL && reference.parse_tree.data != NULL);
    CHECK(actual.parse_tree.len == reference.parse_tree.len);
    CHECK(memcmp(actual.parse_tree.data, reference.parse_tree.data,
                 actual.parse_tree.len) == 0);
    if (certified) {
        CHECK(actual_observed.calls == 0U);
        CHECK(statements == reference_observed.statements);
    } else {
        CHECK(actual_observed.calls == 1U);
        CHECK(actual_observed.statements == reference_observed.statements);
    }
    pg_query_exit();
    CHECK(memcmp(actual.parse_tree.data, reference.parse_tree.data,
                 actual.parse_tree.len) == 0);
    pg_query_free_protobuf_parse_result(actual);
    pg_query_free_protobuf_parse_result(reference);
}

static void keyword_and_fallback_cases(void)
{
    static const char *const values[] = {
        "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIME(0)",
        "CURRENT_TIME(6)", "CURRENT_TIMESTAMP", "CURRENT_TIMESTAMP(0)",
        "CURRENT_TIMESTAMP(6)", "LOCALTIME", "LOCALTIME(0)", "LOCALTIME(6)",
        "LOCALTIMESTAMP", "LOCALTIMESTAMP(0)", "LOCALTIMESTAMP(6)",
        "CURRENT_ROLE", "CURRENT_USER", "USER", "SESSION_USER",
        "CURRENT_CATALOG", "CURRENT_SCHEMA"
    };
    char sql[512];
    for (size_t i = 0U; i < sizeof(values) / sizeof(values[0]); i++) {
        CHECK(snprintf(sql, sizeof(sql), "SELECT %s", values[i]) > 0);
        same_native_wire(sql, 1);
        CHECK(snprintf(sql, sizeof(sql),
            "INSERT INTO t(a,b,c) VALUES(1,%s,'x'),(2,%s,NULL)",
            values[i], values[i]) > 0);
        same_native_wire(sql, 1);
    }
    /* A newly admitted scalar must never hide a different rejected visitor. */
    same_native_wire("SELECT CURRENT_TIMESTAMP, 1+2", 0);
    same_native_wire("WITH x AS (SELECT CURRENT_TIMESTAMP) SELECT * FROM x", 0);
    same_native_wire("SELECT CURRENT_TIMESTAMP FROM t AS renamed", 0);
    same_native_wire("SELECT CURRENT_TIMESTAMP; UPDATE t SET a=1+2", 0);
    same_native_wire("SELECT CURRENT_TIMESTAMP; SELECT CURRENT_DATE", 1);
}

static void nine_column_lifecycle(void)
{
    enum { ROWS = 64 };
    const size_t capacity = 160U * ROWS + 256U;
    char *sql = malloc(capacity), *out = NULL;
    size_t used = 0U;
    sqlparser_parse_options_t options;
    sqlparser_handle_t *handle = NULL;
    sqlparser_query_graph_view_t graph;
    sqlparser_graph_dml_t dml;
    sqlparser_error_t error;
    CHECK(sql != NULL);
    used += (size_t)snprintf(sql + used, capacity - used,
        "INSERT INTO TEST_LIB.TEACHER_STATISTICS("
        "STAT_DATE,TEACHER_ID,TEACHER_NAME_ENCRYPT,PHONE_ENCRYPT,"
        "TOTAL_TEACHING,TOTAL_HOURS,CHECK_STATUS,CREATE_TIME,UPDATE_TIME) VALUES ");
    for (size_t i = 0U; i < ROWS; i++) {
        int n = snprintf(sql + used, capacity - used,
            "%s('202505','T%zu','张三李四','13800138000',20,100.50,0,"
            "CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)", i ? "," : "", i + 1001U);
        CHECK(n > 0 && (size_t)n < capacity - used);
        used += (size_t)n;
    }
    CHECK(used >= 4096U);
    same_native_wire(sql, 1);
    sqlparser_parse_options_default(&options);
    options.dialect = SQLPARSER_DIALECT_MYSQL;
    CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    pg_query_exit();
    CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
    CHECK(dml.rows.count == ROWS * 9U);
    CHECK(sqlparser_deparse(handle, &out, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(out, sql) == 0);
    sqlparser_handle_destroy(handle);
    pg_query_exit();
    CHECK(strcmp(out, sql) == 0);
    sqlparser_string_free(out);
    free(sql);
}

int main(void)
{
    keyword_and_fallback_cases();
    nine_column_lifecycle();
    puts("direct-wire SQL value certificates: keywords, precision, native byte parity, rejected siblings and nine-column lifetime passed");
    return 0;
}
