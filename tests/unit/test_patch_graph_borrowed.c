/* Public patch inputs may borrow graph, AST, or bind-occurrence storage.
 * The first edit destroys that storage before later edits use their inputs.
 * In particular, graph expression sql/name are graph-owned allocations, not
 * caller-owned strings returned by sqlparser_insert_cell_sql/selector_format.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_test_failure.h"

static sqlparser_dialect_t dialect;
static const char *stage;
static unsigned int round_index;
static sqlparser_error_t error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, \
    "%s:%d: %s dialect=%s stage=%s round=%u error=%s\n", \
    __FILE__, __LINE__, #x, sqlparser_dialect_name(dialect), stage, \
    round_index, error.message); abort(); } } while (0)

static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *handle = NULL;
    sqlparser_parse_options_default(&options);
    options.dialect = dialect;
    CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(handle != NULL);
    return handle;
}

static char *copy_text(const char *text)
{
    char *copy;
    CHECK(text != NULL);
    copy = malloc(strlen(text) + 1U);
    CHECK(copy != NULL);
    strcpy(copy, text);
    return copy;
}

static void apply(sqlparser_handle_t *handle, const sqlparser_patch_t *patches, size_t count)
{
    sqlparser_patch_list_t list = {patches, count};
    CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
}

static sqlparser_graph_dml_cell_t graph_cell(const sqlparser_query_graph_view_t *graph,
    size_t ordinal)
{
    sqlparser_graph_dml_t dml;
    sqlparser_graph_dml_cell_t cell;
    size_t index;
    CHECK(sqlparser_query_graph_dml(graph, &dml, &error) == SQLPARSER_STATUS_OK);
    CHECK(ordinal < dml.rows.count);
    CHECK(sqlparser_query_graph_span_index_at(graph, dml.rows, ordinal, &index,
        &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_cell_at(graph, index, &cell, &error) == SQLPARSER_STATUS_OK);
    CHECK(cell.row_index == 0U && cell.column_ordinal == ordinal);
    return cell;
}

static void stale_graph(const sqlparser_query_graph_view_t *graph)
{
    sqlparser_graph_dml_t dml;
    sqlparser_graph_dml_cell_t cell;
    sqlparser_graph_expression_t expression;
    CHECK(sqlparser_query_graph_dml(graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    CHECK(sqlparser_query_graph_dml_cell_at(graph, 0U, &cell, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    CHECK(sqlparser_query_graph_expression_at(graph, 0U, &expression, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
}

static void string_cell(sqlparser_handle_t *handle, size_t column, const char *expected)
{
    sqlparser_literal_view_t value;
    CHECK(sqlparser_insert_cell_literal(handle, 0U, 0U, column, &value, &error) == SQLPARSER_STATUS_OK);
    CHECK(value.kind == SQLPARSER_LITERAL_KIND_STRING);
    CHECK(value.string_value != NULL && strcmp(value.string_value, expected) == 0);
}

/* The oracle is an independently parsed handle with caller-owned patch data.
 * Compare complete SQL and public semantic JSON, then verify reparsing. */
static char *compare_result(sqlparser_handle_t *handle, sqlparser_handle_t *reference)
{
    sqlparser_handle_t *reparsed;
    char *actual = NULL, *expected = NULL, *view = NULL, *reference_view = NULL;
    CHECK(sqlparser_deparse(handle, &actual, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(reference, &expected, &error) == SQLPARSER_STATUS_OK);
    if (strcmp(actual, expected) != 0)
        fprintf(stderr, "actual=%s\nexpected=%s\n", actual, expected);
    CHECK(strcmp(actual, expected) == 0);
    CHECK(sqlparser_export_view_json(handle, 0, &view, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_export_view_json(reference, 0, &reference_view, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(view, reference_view) == 0);
    sqlparser_string_free(reference_view); reference_view = NULL;
    reparsed = parse(actual);
    CHECK(sqlparser_export_view_json(reparsed, 0, &reference_view, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(view, reference_view) == 0);
    sqlparser_handle_destroy(reparsed);
    sqlparser_string_free(expected);
    sqlparser_string_free(view);
    sqlparser_string_free(reference_view);
    return actual;
}

static const char graph_input[] =
    "/*head*/ INSERT INTO t(a,b,c,d,e,f,g,h,i,j,k) VALUES "
    "(UPPER('graph-source'),0,'stmt[0].insert_cell[0][1]',"
    "'stmt[0].insert_cell[0][0]','''graph-default''',"
    "'graph ''string''',1.25,0,0,0,0); "
    "SELECT a FROM t WHERE a LIKE UPPER('graph-source'); /*tail*/";

static void borrowed_graph_fields(void)
{
    enum { ROUNDS = 3, PATCHES = 6 };
    char input[sizeof(graph_input)];
    sqlparser_handle_t *handle, *reference;
    char *outputs[ROUNDS] = {0}, *snapshots[ROUNDS] = {0};
    size_t prior;
    stage = "graph-fields";
    strcpy(input, graph_input);
    handle = parse(input);
    reference = parse(graph_input);
    for (round_index = 0U; round_index < ROUNDS; round_index++) {
        sqlparser_query_graph_view_t graph, expression_graph, fresh;
        sqlparser_graph_expression_t expression;
        sqlparser_graph_dml_cell_t selector, source, default_sql, string, floating;
        sqlparser_literal_value_t value = {0}, number = {0};
        sqlparser_literal_value_t own_value = {0}, own_number = {0};
        sqlparser_literal_view_t actual_float;
        sqlparser_patch_t patches[PATCHES] = {{0}}, own[PATCHES];
        sqlparser_patch_t reset[2] = {{0}};
        sqlparser_patch_list_t empty = {NULL, 0U};
        sqlparser_graph_dml_t dml;
        char *own_sql, *own_name;
        const char *column_name;
        size_t count;
        CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        /* INSERT cells are DML values, not graph expressions. Borrow the
         * expression-store strings from another statement on this handle;
         * editing either statement invalidates the entire graph cache. */
        CHECK(sqlparser_statement_query_graph(handle, 1U, &expression_graph, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_expression_at(&expression_graph, 0U, &expression, &error) == SQLPARSER_STATUS_OK);
        CHECK(expression.sql != NULL && expression.name != NULL);
        own_sql = copy_text(expression.sql);
        own_name = copy_text(expression.name);
        selector = graph_cell(&graph, 2U);
        source = graph_cell(&graph, 3U);
        default_sql = graph_cell(&graph, 4U);
        string = graph_cell(&graph, 5U);
        floating = graph_cell(&graph, 6U);
        CHECK(selector.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
        CHECK(source.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
        CHECK(default_sql.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
        CHECK(string.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
        CHECK(floating.literal.kind == SQLPARSER_LITERAL_KIND_FLOAT);
        CHECK(strcmp(selector.literal.string_value, "stmt[0].insert_cell[0][1]") == 0);
        CHECK(strcmp(source.literal.string_value, "stmt[0].insert_cell[0][0]") == 0);
        CHECK(strcmp(default_sql.literal.string_value, "'graph-default'") == 0);
        value.kind = SQLPARSER_LITERAL_KIND_STRING;
        value.string_value = string.literal.string_value;
        number.kind = SQLPARSER_LITERAL_KIND_FLOAT;
        number.float_value = floating.literal.float_value;
        patches[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][0]", .sql="LOWER('changed')"};
        patches[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector=selector.literal.string_value, .sql=expression.sql};
        patches[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN,
            .selector="stmt[0].insert_columns", .index=11U,
            .name=expression.name, .default_sql=default_sql.literal.string_value};
        patches[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][10]", .source_selector=source.literal.string_value};
        patches[4] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][8]", .literal=&value};
        patches[5] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][9]", .literal=&number};
        memcpy(own, patches, sizeof(own));
        own[1].selector = "stmt[0].insert_cell[0][1]"; own[1].sql = own_sql;
        own[2].name = own_name; own[2].default_sql = "'graph-default'";
        own[3].source_selector = "stmt[0].insert_cell[0][0]";
        own_value.kind = SQLPARSER_LITERAL_KIND_STRING; own_value.string_value = "graph 'string'";
        own_number.kind = SQLPARSER_LITERAL_KIND_FLOAT; own_number.float_value = "1.25";
        own[4].literal = &own_value; own[5].literal = &own_number;
        /* Empty apply preserves the graph and the borrowed pointers. */
        CHECK(sqlparser_apply_patch(handle, &empty, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
        apply(handle, patches, PATCHES);
        /* Do not inspect any of the borrowed text after the mutation. */
        stale_graph(&graph);
        stale_graph(&expression_graph);
        apply(reference, own, PATCHES);
        CHECK(sqlparser_statement_query_graph(handle, 0U, &fresh, &error) == SQLPARSER_STATUS_OK);
        CHECK(fresh.generation == graph.generation + 1UL);
        CHECK(sqlparser_insert_column_count(handle, 0U, &count, &error) == SQLPARSER_STATUS_OK && count == 12U);
        CHECK(sqlparser_insert_column_name(handle, 0U, 11U, &column_name, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(column_name, own_name) == 0);
        string_cell(handle, 8U, "graph 'string'");
        string_cell(handle, 11U, "graph-default");
        CHECK(sqlparser_insert_cell_literal(handle, 0U, 0U, 9U, &actual_float, &error) == SQLPARSER_STATUS_OK);
        CHECK(actual_float.kind == SQLPARSER_LITERAL_KIND_FLOAT);
        CHECK(actual_float.float_value != NULL && strcmp(actual_float.float_value, "1.25") == 0);
        outputs[round_index] = compare_result(handle, reference);
        CHECK(outputs[round_index] != input && outputs[round_index] != sqlparser_original_sql(handle));
        snapshots[round_index] = copy_text(outputs[round_index]);
        CHECK(strcmp(input, graph_input) == 0);
        free(own_sql); free(own_name);
        for (prior = 0U; prior <= round_index; prior++)
            CHECK(strcmp(outputs[prior], snapshots[prior]) == 0);
        /* Reuse the same handle and obtain fresh borrowed views next round. */
        reset[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_COLUMN,
            .selector="stmt[0].insert_columns", .index=11U};
        reset[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][0]", .sql="UPPER('graph-source')"};
        apply(handle, reset, 2U); apply(reference, reset, 2U);
        stale_graph(&fresh);
    }
    {
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_expression_t expression;
        sqlparser_patch_t bad[2] = {{0}};
        sqlparser_patch_list_t list = {bad, 2U};
        stage = "borrowed-input-terminal-error";
        CHECK(sqlparser_statement_query_graph(handle, 1U, &graph, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_expression_at(&graph, 0U, &expression, &error) == SQLPARSER_STATUS_OK);
        bad[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][1]", .sql=expression.sql};
        bad[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[999].insert_cell[0][0]", .sql="'invalid'"};
        CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        stale_graph(&graph);
        CHECK(sqlparser_test_failed_handle(handle));
    }
    CHECK(strcmp(input, graph_input) == 0);
    sqlparser_handle_destroy(handle); sqlparser_handle_destroy(reference);
    for (prior = 0U; prior < ROUNDS; prior++) {
        CHECK(strcmp(outputs[prior], snapshots[prior]) == 0);
        sqlparser_string_free(outputs[prior]); free(snapshots[prior]);
    }
}

static int postgres_style(void)
{
    return dialect == SQLPARSER_DIALECT_POSTGRESQL ||
        dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL ||
        dialect == SQLPARSER_DIALECT_KINGBASE_POSTGRESQL;
}
static int mysql_style(void)
{
    return dialect == SQLPARSER_DIALECT_MYSQL ||
        dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL ||
        dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL;
}
static int sqlserver_style(void)
{
    return dialect == SQLPARSER_DIALECT_SQLSERVER ||
        dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER ||
        dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER;
}

/* Bind occurrence text is cache-owned. A graph cell's inline bind[] array
 * would be a caller-owned copy and would not exercise this lifetime bug.
 * Numeric positional keys need a snapshot just as named keys do. */
static void borrowed_bind_fields(int numeric)
{
    const char *token = postgres_style() ? "$7" : mysql_style() ? "?" :
        sqlserver_style() ? "@borrowed_key" : numeric ? ":7" : ":borrowed_key";
    char input[256], original[256];
    char *key = NULL, *sql, *output, *snapshot;
    sqlparser_handle_t *handle, *reference;
    sqlparser_query_graph_view_t graph;
    sqlparser_bind_occurrence_view_t occurrences, fresh;
    sqlparser_bind_occurrence_t occurrence, actual;
    sqlparser_bind_value_t borrowed, owned;
    sqlparser_patch_t patches[3] = {{0}}, own[3];
    size_t i;
    int written;
    stage = numeric ? "borrowed-positional-bind-key" : "borrowed-native-bind-key";
    round_index = 0U;
    written = snprintf(input, sizeof(input), "INSERT INTO t(a,b,c) VALUES (%s,0,0)", token);
    CHECK(written > 0 && (size_t)written < sizeof(input));
    strcpy(original, input);
    handle = parse(input); reference = parse(input);
    CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_bind_occurrences(handle, &occurrences, &error) == SQLPARSER_STATUS_OK);
    CHECK(occurrences.count == 1U);
    CHECK(sqlparser_bind_occurrence_at(&occurrences, 0U, &occurrence, &error) == SQLPARSER_STATUS_OK);
    CHECK(occurrence.sql != NULL && strcmp(occurrence.sql, token) == 0);
    CHECK(occurrence.kind == (postgres_style() || mysql_style() || numeric ?
        SQLPARSER_BIND_KIND_POSITIONAL : SQLPARSER_BIND_KIND_NAMED));
    CHECK(mysql_style() ? occurrence.key == NULL :
        occurrence.key != NULL && strcmp(occurrence.key, token + 1U) == 0);
    borrowed.kind = occurrence.kind; borrowed.key = occurrence.key;
    if (occurrence.key != NULL) key = copy_text(occurrence.key);
    sql = copy_text(occurrence.sql);
    owned = borrowed; owned.key = key;
    patches[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
        .selector="stmt[0].insert_cell[0][0]", .sql="UPPER('changed')"};
    patches[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
        .selector="stmt[0].insert_cell[0][1]", .bind=&borrowed};
    patches[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
        .selector="stmt[0].insert_cell[0][2]", .sql=occurrence.sql};
    memcpy(own, patches, sizeof(own)); own[1].bind = &owned; own[2].sql = sql;
    apply(handle, patches, 3U);
    stale_graph(&graph);
    CHECK(sqlparser_bind_occurrence_at(&occurrences, 0U, &actual, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    apply(reference, own, 3U);
    output = compare_result(handle, reference); snapshot = copy_text(output);
    CHECK(sqlparser_handle_bind_occurrences(handle, &fresh, &error) == SQLPARSER_STATUS_OK);
    CHECK(fresh.count == 2U);
    for (i = 0U; i < fresh.count; i++) {
        CHECK(sqlparser_bind_occurrence_at(&fresh, i, &actual, &error) == SQLPARSER_STATUS_OK);
        CHECK(actual.kind == owned.kind && strcmp(actual.sql, sql) == 0);
        CHECK(key == NULL ? actual.key == NULL : actual.key != NULL && strcmp(actual.key, key) == 0);
    }
    CHECK(strcmp(input, original) == 0);
    sqlparser_handle_destroy(handle); sqlparser_handle_destroy(reference);
    CHECK(strcmp(output, snapshot) == 0);
    sqlparser_string_free(output); free(snapshot); free(key); free(sql);
}

int main(void)
{
    int d;
    for (d = SQLPARSER_DIALECT_POSTGRESQL; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++) {
        dialect = (sqlparser_dialect_t)d;
        borrowed_graph_fields();
        borrowed_bind_fields(0);
        if (!postgres_style() && !mysql_style() && !sqlserver_style())
            borrowed_bind_fields(1);
    }
    puts("patch graph-borrowed inputs: all 13 dialects passed");
    return 0;
}
