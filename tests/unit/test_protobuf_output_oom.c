/* Fail only the malloc for serialized parser output, not other allocations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "sqlparser_test_failure.h"
#include "src/pg_query_observer.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s (case=%d failure=%zu): %s\n",__FILE__,__LINE__,#x,test_case,fail_at,error.message); abort(); } } while (0)
static int test_case;
static size_t fail_at;
static sqlparser_error_t error;
#ifdef SQLPARSER_PROTOBUF_OUTPUT_OOM_WRAPPERS
static size_t attempts, failures;
static int armed, persistent_failure;
void *__real_malloc(size_t);
void *__real_pg_query_protobuf_alloc_output(size_t);
void *__wrap_malloc(size_t size) {
    if (persistent_failure && failures != 0U) return NULL;
    return __real_malloc(size);
}
void *__wrap_pg_query_protobuf_alloc_output(size_t size) {
    if (armed && ++attempts == fail_at) { failures++; return NULL; }
    return __real_pg_query_protobuf_alloc_output(size);
}
static void arm(size_t failure, int persistent) {
    attempts = failures = 0; fail_at = failure;
    persistent_failure = persistent; armed = 1;
    memset(&error, 0, sizeof(error));
}
static void disarm(void) { armed = persistent_failure = 0; }

static void initial_parse(void) {
    for (int dialect = SQLPARSER_DIALECT_POSTGRESQL; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        sqlparser_parse_options_t options;
        sqlparser_parse_options_default(&options);
        options.dialect = (sqlparser_dialect_t)dialect;
        for (int persistent = 0; persistent < 2; persistent++) {
            for (int optional_error = 0; optional_error < 2; optional_error++) {
                sqlparser_handle_t *handle = NULL;
                arm(1, persistent);
                CHECK(sqlparser_parse_with_options("SELECT 1", &options, &handle,
                    optional_error ? NULL : &error) == SQLPARSER_STATUS_NO_MEMORY);
                CHECK(handle == NULL && failures == 1);
                if (!optional_error) CHECK(error.code == SQLPARSER_STATUS_NO_MEMORY);
                disarm();
                /* Existing syntax errors still take precedence over the
                 * subsequent attempted serialization of their empty tree. */
                arm(1, persistent);
                CHECK(sqlparser_parse_with_options("SELECT )", &options, &handle,
                    optional_error ? NULL : &error) == SQLPARSER_STATUS_PARSE_ERROR);
                CHECK(handle == NULL && failures == 1);
                if (!optional_error) CHECK(error.code == SQLPARSER_STATUS_PARSE_ERROR);
                disarm();
                CHECK(sqlparser_parse_with_options("SELECT 1", &options, &handle, &error) == SQLPARSER_STATUS_OK);
                sqlparser_handle_destroy(handle);
            }
        }
    }
}
static sqlparser_status_t operation(sqlparser_handle_t *handle) {
    if (test_case == 0) return sqlparser_statement_set_where_sql(handle, 0, 0, "id = 3", &error);
    if (test_case == 1) {
        sqlparser_patch_t patches[2] = {{0}};
        sqlparser_patch_list_t list = {patches, 2};
        patches[0].op = patches[1].op = SQLPARSER_PATCH_REPLACE;
        patches[0].selector = "stmt[0].insert_cell[0][1]"; patches[0].sql = "'new-a'";
        patches[1].selector = "stmt[0].insert_cell[1][1]"; patches[1].sql = "'new-b'";
        return sqlparser_apply_patch(handle, &list, &error);
    }
    if (test_case == 2) return sqlparser_validate_ast_identifier_spelling(handle, &error);
    return sqlparser_select_insert_target_sql(handle, 0, 0, 1, "[extra]", &error);
}
static void followup_paths(void) {
    const char *queries[] = {
        "SELECT id FROM t WHERE id = 1",
        /* MySQL identifier quoting forces final full reparse: this fixture must
         * exercise the native parser-output allocator, which certified plain
         * INSERT commits bypass. Their OOM coverage lives in test_patch_lifecycle. */
        "INSERT INTO `s`.`t`(id, v) VALUES (1,'old-a'),(2,'old-b')",
        "SELECT id FROM t WHERE id = 1",
        "SELECT({fn ABS(-1)}) AS [odbc]"
    };
    for (test_case = 0; test_case < 4; test_case++) {
        size_t count = 0;
        for (size_t failure = 0; failure <= count; failure++) {
            sqlparser_parse_options_t options;
            sqlparser_handle_t *handle = NULL;
            sqlparser_query_graph_view_t graph;
            sqlparser_graph_block_t block;
            char *output = NULL;
            sqlparser_status_t status;
            sqlparser_parse_options_default(&options);
            options.dialect = test_case == 1 ? SQLPARSER_DIALECT_MYSQL :
                (test_case == 3 ? SQLPARSER_DIALECT_SQLSERVER : SQLPARSER_DIALECT_POSTGRESQL);
            CHECK(sqlparser_parse_with_options(queries[test_case], &options, &handle, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_statement_query_graph(handle, 0, &graph, &error) == SQLPARSER_STATUS_OK);
            arm(failure, 0);
            status = operation(handle);
            disarm();
            if (failure == 0) {
                CHECK(status == SQLPARSER_STATUS_OK);
                count = attempts;
                CHECK(count > 0);
            } else {
                CHECK(failures == 1);
                if (status != SQLPARSER_STATUS_NO_MEMORY)
                    fprintf(stderr, "unexpected status=%d serializers=%zu\n", status, attempts);
                CHECK(status == SQLPARSER_STATUS_NO_MEMORY && error.code == SQLPARSER_STATUS_NO_MEMORY);
                if (test_case == 0 || test_case == 1 || test_case == 3) {
                    CHECK(sqlparser_test_failed_handle(handle));
                    sqlparser_handle_destroy(handle); handle = NULL;
                    CHECK(sqlparser_parse_with_options(queries[test_case], &options, &handle, &error) == SQLPARSER_STATUS_OK);
                    CHECK(sqlparser_statement_query_graph(handle, 0, &graph, &error) == SQLPARSER_STATUS_OK);
                }
                CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
                CHECK(strcmp(output, queries[test_case]) == 0);
                if (test_case != 1) CHECK(sqlparser_query_graph_block_at(&graph, 0, &block, &error) == SQLPARSER_STATUS_OK);
                else {
                    sqlparser_graph_dml_t dml;
                    CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
                }
                sqlparser_string_free(output);
                CHECK(operation(handle) == SQLPARSER_STATUS_OK);
            }
            sqlparser_handle_destroy(handle);
        }
        printf("serializer OOM followup case=%d covered_allocations=%zu\n", test_case, count);
    }
}
#endif
int main(void) {
#ifdef SQLPARSER_PROTOBUF_OUTPUT_OOM_WRAPPERS
    initial_parse(); followup_paths();
    pg_query_exit();
    puts("protobuf serialization OOM returns NO_MEMORY; syntax precedence, fresh-handle retry, terminal patch failures and borrowed views passed");
#else
    (void)test_case; (void)fail_at; (void)error;
    puts("SKIP: protobuf output allocation injection requires GNU linker wrappers");
#endif
    return 0;
}
