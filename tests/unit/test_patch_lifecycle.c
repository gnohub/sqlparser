/* One parse, many in-place apply/deparse rounds; failure is terminal. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "sqlparser_test_failure.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s (dialect=%d stage=%d fault=%zu): %s\n", __FILE__, __LINE__, #x, dialect, stage, fault_index, error.message); abort(); } } while (0)
static int dialect, stage;
static size_t fault_index;
static sqlparser_error_t error;

#ifdef SQLPARSER_LIFECYCLE_ALLOC_WRAPPERS
static int allocation_armed;
static size_t allocation_calls, allocation_fail_at, allocation_failures;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static int reject_allocation(void)
{
    if (!allocation_armed) return 0;
    allocation_calls++;
    if (allocation_calls != allocation_fail_at) return 0;
    allocation_failures++;
    return 1;
}
void *__wrap_malloc(size_t n) { return reject_allocation() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t s) { return reject_allocation() ? NULL : __real_calloc(n, s); }
void *__wrap_realloc(void *p, size_t n) { return reject_allocation() ? NULL : __real_realloc(p, n); }
static void arm(size_t failure)
{
    allocation_calls = allocation_failures = 0U;
    allocation_fail_at = failure;
    allocation_armed = 1;
}
static void disarm(void) { allocation_armed = 0; }
#endif

static const char original[] = "/*head*/ INSERT INTO t(a,b) VALUES ('old-a','old-b'); /*tail*/";
static sqlparser_handle_t *parse_input(char *input)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *handle = NULL;
    sqlparser_parse_options_default(&options);
    options.dialect = (sqlparser_dialect_t)dialect;
    CHECK(sqlparser_parse_with_options(input, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(handle != NULL);
    return handle;
}
static void verify_values(sqlparser_handle_t *handle, const char *value)
{
    sqlparser_literal_view_t literal;
    size_t col;
    for (col = 0U; col < 2U; col++) {
        CHECK(sqlparser_insert_cell_literal(handle, 0U, 0U, col, &literal, &error) == SQLPARSER_STATUS_OK);
        CHECK(literal.kind == SQLPARSER_LITERAL_KIND_STRING);
        CHECK(literal.string_value != NULL && strcmp(literal.string_value, value) == 0);
    }
}
static void many_rounds(void)
{
    enum { ROUNDS = 8 };
    char input[sizeof(original)], values[ROUNDS][40];
    char *outputs[ROUNDS] = {0}, *snapshots[ROUNDS] = {0}, *second = NULL;
    sqlparser_handle_t *handle, *reference = NULL;
    sqlparser_query_graph_view_t old_graph, graph;
    sqlparser_graph_dml_t dml;
    sqlparser_patch_t patches[3] = {{0}};
    sqlparser_patch_list_t list = {patches, 3U}, empty = {NULL, 0U};
    sqlparser_literal_value_t literal = {0};
    unsigned long generation;
    size_t round, prior;
    strcpy(input, original);
    handle = parse_input(input);
    for (round = 0U; round < ROUNDS; round++) {
        char *view = NULL, *reference_view = NULL;
        stage = (int)round;
        snprintf(values[round], sizeof(values[round]), "round-%zu-'quoted'", round);
        CHECK(sqlparser_statement_query_graph(handle, 0U, &old_graph, &error) == SQLPARSER_STATUS_OK);
        generation = handle->generation;
        literal.kind = SQLPARSER_LITERAL_KIND_STRING;
        literal.string_value = values[round];
        patches[0].op = patches[1].op = patches[2].op = SQLPARSER_PATCH_REPLACE;
        patches[0].selector = "stmt[0].insert_cell[0][0]"; patches[0].literal = &literal;
        patches[1].selector = "stmt[0].insert_cell[0][1]"; patches[1].source_selector = patches[0].selector;
        patches[2] = patches[0]; /* An ordered redundant final edit is still an attempt. */
        CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(handle->generation == generation + 1UL);
        CHECK(sqlparser_query_graph_dml(&old_graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(strcmp(input, original) == 0);
        verify_values(handle, values[round]);
        CHECK(sqlparser_deparse(handle, &outputs[round], &error) == SQLPARSER_STATUS_OK);
        CHECK(outputs[round] != input && outputs[round] != sqlparser_original_sql(handle));
        snapshots[round] = malloc(strlen(outputs[round]) + 1U);
        CHECK(snapshots[round] != NULL); strcpy(snapshots[round], outputs[round]);
        CHECK(sqlparser_export_view_json(handle, 0, &view, &error) == SQLPARSER_STATUS_OK);
        reference = parse_input(outputs[round]);
        verify_values(reference, values[round]);
        CHECK(sqlparser_export_view_json(reference, 0, &reference_view, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(view, reference_view) == 0);
        sqlparser_string_free(view); sqlparser_string_free(reference_view);
        sqlparser_handle_destroy(reference); reference = NULL;
        for (prior = 0U; prior <= round; prior++) CHECK(strcmp(outputs[prior], snapshots[prior]) == 0);
        CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        generation = handle->generation;
        CHECK(sqlparser_apply_patch(handle, &empty, &error) == SQLPARSER_STATUS_OK);
        CHECK(handle->generation == generation);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
        /* Every nonempty attempt invalidates views, even an exact no-op. */
        list.count = 1U;
        CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(handle->generation == generation + 1UL);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        verify_values(handle, values[round]);
        list.count = 3U;
    }
    CHECK(sqlparser_deparse(handle, &second, &error) == SQLPARSER_STATUS_OK);
    CHECK(second != outputs[ROUNDS - 1U]);
    second[0] = '!'; /* An output is caller-owned, not a borrowed handle buffer. */
    verify_values(handle, values[ROUNDS - 1U]);
    CHECK(strcmp(outputs[ROUNDS - 1U], snapshots[ROUNDS - 1U]) == 0);
    patches[0].selector = "stmt[0].insert_cell[0][999]"; list.count = 1U;
    CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    CHECK(sqlparser_test_failed_handle(handle));
    CHECK(strcmp(input, original) == 0);
    sqlparser_handle_destroy(handle);
    for (round = 0U; round < ROUNDS; round++) {
        CHECK(strcmp(outputs[round], snapshots[round]) == 0);
        sqlparser_string_free(outputs[round]); free(snapshots[round]);
    }
    CHECK(second[0] == '!'); sqlparser_string_free(second);
}
/* Patch values can themselves be borrowed from the original handle. They
 * must be copied before an earlier patch invalidates that backing storage. */
static void borrowed_patch_inputs(void)
{
    char input[sizeof(original)];
    sqlparser_handle_t *handle;
    sqlparser_literal_view_t borrowed, actual;
    sqlparser_literal_value_t value = {0};
    sqlparser_patch_t patches[2] = {{0}};
    sqlparser_patch_list_t list = {patches, 2U};
    char *output = NULL;
    strcpy(input, original); handle = parse_input(input);
    CHECK(sqlparser_insert_cell_literal(handle, 0U, 0U, 0U, &borrowed, &error) == SQLPARSER_STATUS_OK);
    value.kind = SQLPARSER_LITERAL_KIND_STRING; value.string_value = borrowed.string_value;
    patches[0].op = patches[1].op = SQLPARSER_PATCH_REPLACE;
    patches[0].selector = "stmt[0].insert_cell[0][0]"; patches[0].sql = "UPPER('changed')";
    patches[1].selector = "stmt[0].insert_cell[0][1]"; patches[1].literal = &value;
    CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_insert_cell_literal(handle, 0U, 0U, 1U, &actual, &error) == SQLPARSER_STATUS_OK);
    CHECK(actual.string_value && strcmp(actual.string_value, "old-a") == 0);
    CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
    CHECK(strstr(output, "UPPER('changed')") || strstr(output, "upper('changed')"));
    CHECK(strcmp(input, original) == 0);
    sqlparser_handle_destroy(handle); sqlparser_string_free(output);
}

static void failure_boundaries(void)
{
    int kind, position;
    for (kind = 0; kind < 3; kind++) for (position = 0; position < 3; position++) {
        char input[sizeof(original)];
        sqlparser_handle_t *handle;
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_dml_t dml;
        sqlparser_patch_t patches[3] = {{0}};
        sqlparser_patch_list_t list = {patches, 3U};
        sqlparser_status_t expected = kind == 0 ? SQLPARSER_STATUS_INVALID_ARGUMENT :
            kind == 1 ? SQLPARSER_STATUS_PARSE_ERROR : SQLPARSER_STATUS_RESOURCE_LIMIT;
        char large[200];
        int i;
        stage = 20 + 3 * kind + position;
        strcpy(input, original); handle = parse_input(input);
        CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        for (i = 0; i < 3; i++) {
            patches[i].op = SQLPARSER_PATCH_REPLACE;
            patches[i].selector = "stmt[0].insert_cell[0][0]";
            patches[i].sql = "'valid'";
        }
        if (kind == 0) patches[position].selector = "stmt[999].insert_cell[0][0]";
        if (kind == 1) patches[position].sql = "'unterminated";
        if (kind == 2) {
            large[0] = '\''; memset(large + 1U, 'x', sizeof(large) - 3U);
            large[sizeof(large) - 2U] = '\''; large[sizeof(large) - 1U] = '\0';
            handle->limits.max_sql_bytes = 128U; patches[position].sql = large;
        }
        CHECK(sqlparser_apply_patch(handle, &list, &error) == expected);
        CHECK(error.code == expected);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(strcmp(input, original) == 0);
        CHECK(sqlparser_test_failed_handle(handle));
        sqlparser_handle_destroy(handle);
    }
    for (kind = 0; kind < 3; kind++) {
        char input[sizeof(original)];
        char *owned = NULL, *output = NULL;
        sqlparser_handle_t *handle;
        strcpy(input, original); handle = parse_input(input);
        CHECK(sqlparser_deparse(handle, &owned, &error) == SQLPARSER_STATUS_OK);
        if (kind == 0) {
            handle->limits.max_output_bytes = 1U;
            CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_RESOURCE_LIMIT);
        } else if (kind == 1) {
            CHECK(sqlparser_deparse(handle, NULL, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        } else {
            CHECK(sqlparser_apply_patch(handle, NULL, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        }
        CHECK(output == NULL && strcmp(input, original) == 0);
        CHECK(sqlparser_test_failed_handle(handle));
        sqlparser_handle_destroy(handle);
        CHECK(strcmp(owned, original) == 0); sqlparser_string_free(owned);
    }
}
#ifdef SQLPARSER_LIFECYCLE_ALLOC_WRAPPERS
static void exercise_allocation_failures(void)
{
    static const char *names[] = {"generic-apply", "deparse", "fast-apply", "reused-fast-apply"};
    int operation;
    for (operation = 0; operation < 4; operation++) {
        size_t failure, allocation_count = 0U;
        char *expected = NULL;
        for (failure = 0U; failure <= allocation_count; failure++) {
            char input[sizeof(original)];
            sqlparser_handle_t *handle;
            sqlparser_patch_t patch[2] = {{0}};
            sqlparser_patch_list_t list = {patch, operation >= 2 ? 2U : 1U};
            sqlparser_status_t status;
            char *output = NULL;
            stage = 40 + operation; fault_index = failure;
            strcpy(input, original); handle = parse_input(input);
            patch[0].op = patch[1].op = SQLPARSER_PATCH_REPLACE;
            patch[0].selector = "stmt[0].insert_cell[0][0]"; patch[0].sql = "'changed'";
            patch[1].selector = "stmt[0].insert_cell[0][1]"; patch[1].sql = "'changed-b'";
            if (operation == 3) {
                patch[0].sql = "'previous-a'"; patch[1].sql = "'previous-b'";
                CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
                CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
                sqlparser_string_free(output); output = NULL;
                patch[0].sql = "'changed'"; patch[1].sql = "'changed-b'";
            }
            arm(failure);
            status = operation == 1 ? sqlparser_deparse(handle, &output, &error) : sqlparser_apply_patch(handle, &list, &error);
            disarm();
            if (failure == 0U) {
                CHECK(status == SQLPARSER_STATUS_OK);
                allocation_count = allocation_calls; CHECK(allocation_count > 0U);
            } else CHECK(allocation_failures == 1U);
            if (status != SQLPARSER_STATUS_OK) {
                /* Some historic generic unpack sites classify OOM as an
                 * internal error. This sweep checks terminal cleanup; the
                 * dedicated serializer test locks its exact NO_MEMORY code. */
                if (operation == 1) CHECK(status == SQLPARSER_STATUS_NO_MEMORY);
                if (!sqlparser_test_failed_handle(handle)) {
                    fprintf(stderr, "lifecycle OOM operation=%s fail_at=%zu calls=%zu\n", names[operation], failure, allocation_calls);
                    CHECK(0);
                }
            } else {
                if (operation != 1) CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
                CHECK(output != NULL);
                if (failure == 0U) {
                    expected = malloc(strlen(output) + 1U); CHECK(expected != NULL); strcpy(expected, output);
                } else if (strcmp(output, expected) != 0) {
                    fprintf(stderr, "lifecycle fallback changed SQL operation=%s fail_at=%zu\nexpected=%s\nactual=%s\n", names[operation], failure, expected, output);
                    CHECK(0);
                }
            }
            CHECK(strcmp(input, original) == 0);
            sqlparser_handle_destroy(handle); sqlparser_string_free(output);
        }
        free(expected);
        printf("patch lifecycle allocation sweep dialect=%d operation=%s allocations=%zu\n", dialect, names[operation], allocation_count);
    }
}

/* A large eligible batch may inspect all descriptors before applying edits,
 * but must stop execution at its second, invalid selector. Compare the actual
 * allocation work with that same two-item prefix on an identical fresh graph.
 * Every descriptor and value remains valid/readable during eligibility scans. */
static void early_failure_stops_large_batch(void)
{
    enum { ROWS = 5000, SELECTOR_CAPACITY = 80 };
    const size_t capacity = (size_t)ROWS * 40U + 128U;
    char *input = malloc(capacity), *snapshot;
    char (*selectors)[SELECTOR_CAPACITY] = calloc(ROWS, sizeof(*selectors));
    sqlparser_patch_t *patches = calloc(ROWS, sizeof(*patches));
    sqlparser_literal_value_t literal = {0};
    size_t used = 0U, row;
    int typed, pass, written;

    dialect = SQLPARSER_DIALECT_MYSQL; stage = 60; fault_index = 0U;
    CHECK(input != NULL && selectors != NULL && patches != NULL);
    written = snprintf(input, capacity, "INSERT INTO t(id,value) VALUES ");
    CHECK(written > 0 && (size_t)written < capacity); used = (size_t)written;
    for (row = 0U; row < ROWS; row++) {
        written = snprintf(input + used, capacity - used, "%s(%zu,'old-value')",
            row == 0U ? "" : ",", row);
        CHECK(written > 0 && (size_t)written < capacity - used);
        used += (size_t)written;
        written = snprintf(selectors[row], sizeof(selectors[row]),
            "stmt[0].insert_cell[%zu][1]", row);
        CHECK(written > 0 && (size_t)written < sizeof(selectors[row]));
        patches[row].op = SQLPARSER_PATCH_REPLACE;
        patches[row].selector = selectors[row];
    }
    CHECK(used + 2U <= capacity); input[used++] = ';'; input[used] = '\0';
    snapshot = malloc(used + 1U); CHECK(snapshot != NULL);
    memcpy(snapshot, input, used + 1U);
    /* Only the second patch is invalid; the other 4,999 selectors and
     * replacement values are ordinary valid edits of the 5,000-row input. */
    written = snprintf(selectors[1], sizeof(selectors[1]),
        "stmt[0].insert_cell[%u][1]", (unsigned)ROWS);
    CHECK(written > 0 && (size_t)written < sizeof(selectors[1]));
    literal.kind = SQLPARSER_LITERAL_KIND_STRING;
    literal.string_value = "new-value";
    for (typed = 0; typed < 2; typed++) {
        size_t prefix_allocations = 0U;
        sqlparser_error_t prefix_error = {0};
        for (row = 0U; row < ROWS; row++) {
            patches[row].sql = typed ? NULL : "'new-value'";
            patches[row].literal = typed ? &literal : NULL;
        }
        for (pass = 0; pass < 2; pass++) {
            sqlparser_handle_t *handle;
            sqlparser_query_graph_view_t graph;
            sqlparser_graph_dml_t dml;
            sqlparser_patch_list_t list = {patches, pass == 0 ? 2U : ROWS};
            sqlparser_status_t status;
            sqlparser_error_t first_error;
            size_t observed_allocations;
            stage = 60 + typed * 2 + pass;
            handle = parse_input(input);
            CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
            CHECK(dml.rows.count == (size_t)ROWS * 2U);
            arm(0U);
            status = sqlparser_apply_patch(handle, &list, &error);
            disarm();
            observed_allocations = allocation_calls; first_error = error;
            CHECK(allocation_failures == 0U);
            CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT);
            CHECK(first_error.code == SQLPARSER_STATUS_INVALID_ARGUMENT);
            CHECK(memcmp(input, snapshot, used + 1U) == 0);
            CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
            CHECK(sqlparser_test_failed_handle(handle));
            sqlparser_handle_destroy(handle);
            if (pass == 0) {
                prefix_allocations = observed_allocations; prefix_error = first_error;
                CHECK(prefix_allocations > 0U);
            } else {
                if (observed_allocations != prefix_allocations)
                    fprintf(stderr, "early batch failure executed excess allocation work mode=%s prefix=%zu full=%zu\n",
                        typed ? "typed" : "raw", prefix_allocations, observed_allocations);
                CHECK(observed_allocations == prefix_allocations);
                CHECK(first_error.code == prefix_error.code && first_error.cursor == prefix_error.cursor &&
                    first_error.line == prefix_error.line && first_error.column == prefix_error.column &&
                    strcmp(first_error.message, prefix_error.message) == 0);
            }
        }
        printf("patch lifecycle early stop mode=%s rows=%u prefix=2 full=%u allocations=%zu first_error=INVALID_ARGUMENT check=ok\n",
            typed ? "typed" : "raw", (unsigned)ROWS, (unsigned)ROWS, prefix_allocations);
    }
    free(snapshot); free(input); free(selectors); free(patches);
}

#endif
int main(void)
{
    for (dialect = SQLPARSER_DIALECT_POSTGRESQL; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        many_rounds(); borrowed_patch_inputs(); failure_boundaries();
    }
#ifdef SQLPARSER_LIFECYCLE_ALLOC_WRAPPERS
    dialect = SQLPARSER_DIALECT_MYSQL; stage = 40; exercise_allocation_failures();
    early_failure_stops_large_batch();
#else
    puts("SKIP: lifecycle allocation sweep requires GNU linker wrappers");
#endif
    puts("patch lifecycle passed: repeated same-handle rounds, input/output ownership, no-op invalidation and terminal failures");
    return 0;
}
