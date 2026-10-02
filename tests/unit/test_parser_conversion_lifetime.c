/* Conversion-only PostgreSQL allocations must not escape synchronous packing. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"
#include "src/pg_query_observer.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

/* Keep PostgreSQL's private structures opaque. These are the declarations in
 * utils/palloc.h and utils/memutils.h; no backend include-path or layout is
 * needed to check which context the live observer executes in. */
typedef struct MemoryContextData *TestMemoryContext;
#if defined(_MSC_VER)
#define TEST_THREAD_LOCAL __declspec(thread)
#else
#define TEST_THREAD_LOCAL __thread
#endif
extern TEST_THREAD_LOCAL TestMemoryContext CurrentMemoryContext;
extern TEST_THREAD_LOCAL TestMemoryContext TopMemoryContext;
extern TestMemoryContext GetMemoryChunkContext(void *pointer);
extern void *palloc(size_t size);
extern void pfree(void *pointer);

/* Target the common packed-output allocation used by both converter paths. */
#ifdef SQLPARSER_CONVERSION_OUTPUT_OOM_WRAPPERS
static size_t output_failures;
static int fail_output;
void *__real_pg_query_protobuf_alloc_output(size_t size);
void *__wrap_pg_query_protobuf_alloc_output(size_t size)
{
    if (fail_output) { output_failures++; return NULL; }
    return __real_pg_query_protobuf_alloc_output(size);
}
#endif

enum { ROWS = 384, COLUMNS = 7, LONG_TEXT = 70000, SQL_CAPACITY = 160000 };
static sqlparser_error_t error;

static void append(char *sql, size_t *length, const char *format, ...)
{
    va_list args;
    int count;
    CHECK(*length < SQL_CAPACITY);
    va_start(args, format);
    count = vsnprintf(sql + *length, SQL_CAPACITY - *length, format, args);
    va_end(args);
    CHECK(count >= 0 && (size_t)count < SQL_CAPACITY - *length);
    *length += (size_t)count;
}

static char *make_sql(void)
{
    char *sql = malloc(SQL_CAPACITY);
    size_t i, length = 0U;
    CHECK(sql != NULL);
    append(sql, &length, "INSERT INTO \"Schema Name\".\"MiXeD\" "
        "(\"Id\",\"Label\",\"Amount\",\"Bits\",\"Flag\",\"Missing\",\"Hex\") VALUES ");
    for (i = 0U; i < ROWS; i++)
        append(sql, &length, "%s(%zu,'row-''%zu',-123.45,B'0101',%s,NULL,X'deadbeef')",
            i == 0U ? "" : ",", i, i, i % 2U == 0U ? "TRUE" : "FALSE");
    append(sql, &length, "; SELECT \"Label\" AS \"Display\", "
        "999999999999999999999, '");
    CHECK(length + LONG_TEXT + 1U < SQL_CAPACITY);
    memset(sql + length, 'x', LONG_TEXT);
    length += LONG_TEXT;
    sql[length] = '\0';
    append(sql, &length, "' FROM \"Schema Name\".\"MiXeD\" "
        "WHERE \"Id\" IN (1,2,3) ORDER BY \"Label\" DESC LIMIT 7");
    return sql;
}

static const PgQuery__AConst *constant(const PgQuery__Node *node,
    PgQuery__AConst__ValCase kind)
{
    CHECK(node != NULL && node->node_case == PG_QUERY__NODE__NODE_A_CONST);
    CHECK(node->a_const != NULL && node->a_const->val_case == kind);
    return node->a_const;
}

static void check_tree(const PgQuery__ParseResult *tree)
{
    static const char *columns[COLUMNS] = {
        "Id", "Label", "Amount", "Bits", "Flag", "Missing", "Hex"
    };
    const PgQuery__InsertStmt *insert;
    const PgQuery__SelectStmt *values, *select;
    const PgQuery__ResTarget *target;
    const PgQuery__ColumnRef *column;
    const PgQuery__AConst *value;
    size_t i;
    CHECK(tree != NULL && tree->n_stmts == 2U && tree->stmts != NULL);
    CHECK(tree->stmts[0] != NULL && tree->stmts[0]->stmt != NULL);
    CHECK(tree->stmts[0]->stmt->node_case == PG_QUERY__NODE__NODE_INSERT_STMT);
    insert = tree->stmts[0]->stmt->insert_stmt;
    CHECK(insert != NULL && insert->relation != NULL);
    CHECK(strcmp(insert->relation->schemaname, "Schema Name") == 0);
    CHECK(strcmp(insert->relation->relname, "MiXeD") == 0);
    CHECK(insert->n_cols == COLUMNS && insert->cols != NULL);
    for (i = 0U; i < COLUMNS; i++) {
        CHECK(insert->cols[i] != NULL);
        CHECK(insert->cols[i]->node_case == PG_QUERY__NODE__NODE_RES_TARGET);
        CHECK(insert->cols[i]->res_target != NULL);
        CHECK(strcmp(insert->cols[i]->res_target->name, columns[i]) == 0);
    }
    CHECK(insert->select_stmt != NULL);
    CHECK(insert->select_stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT);
    values = insert->select_stmt->select_stmt;
    CHECK(values != NULL && values->n_values_lists == ROWS);
    for (i = 0U; i < ROWS; i++) {
        const PgQuery__List *row;
        char expected[40];
        CHECK(values->values_lists[i] != NULL);
        CHECK(values->values_lists[i]->node_case == PG_QUERY__NODE__NODE_LIST);
        row = values->values_lists[i]->list;
        CHECK(row != NULL && row->n_items == COLUMNS && row->items != NULL);
        value = constant(row->items[0], PG_QUERY__A__CONST__VAL_IVAL);
        CHECK(value->ival != NULL && value->ival->ival == (int)i);
        snprintf(expected, sizeof(expected), "row-'%zu", i);
        value = constant(row->items[1], PG_QUERY__A__CONST__VAL_SVAL);
        CHECK(value->sval != NULL && strcmp(value->sval->sval, expected) == 0);
        value = constant(row->items[2], PG_QUERY__A__CONST__VAL_FVAL);
        CHECK(value->fval != NULL && strcmp(value->fval->fval, "-123.45") == 0);
        value = constant(row->items[3], PG_QUERY__A__CONST__VAL_BSVAL);
        CHECK(value->bsval != NULL && strcmp(value->bsval->bsval, "b0101") == 0);
        value = constant(row->items[4], PG_QUERY__A__CONST__VAL_BOOLVAL);
        CHECK(value->boolval != NULL && value->boolval->boolval == (i % 2U == 0U));
        CHECK(constant(row->items[5], PG_QUERY__A__CONST__VAL__NOT_SET)->isnull);
        value = constant(row->items[6], PG_QUERY__A__CONST__VAL_BSVAL);
        CHECK(value->bsval != NULL && strcmp(value->bsval->bsval, "xdeadbeef") == 0);
    }
    CHECK(tree->stmts[1] != NULL && tree->stmts[1]->stmt != NULL);
    CHECK(tree->stmts[1]->stmt->node_case == PG_QUERY__NODE__NODE_SELECT_STMT);
    select = tree->stmts[1]->stmt->select_stmt;
    CHECK(select != NULL && select->n_target_list == 3U);
    CHECK(select->target_list[0]->node_case == PG_QUERY__NODE__NODE_RES_TARGET);
    target = select->target_list[0]->res_target;
    CHECK(target != NULL && strcmp(target->name, "Display") == 0);
    CHECK(target->val != NULL && target->val->node_case == PG_QUERY__NODE__NODE_COLUMN_REF);
    column = target->val->column_ref;
    CHECK(column != NULL && column->n_fields == 1U);
    CHECK(column->fields[0]->node_case == PG_QUERY__NODE__NODE_STRING);
    CHECK(strcmp(column->fields[0]->string->sval, "Label") == 0);
    value = constant(select->target_list[1]->res_target->val, PG_QUERY__A__CONST__VAL_FVAL);
    CHECK(value->fval != NULL && strcmp(value->fval->fval, "999999999999999999999") == 0);
    value = constant(select->target_list[2]->res_target->val, PG_QUERY__A__CONST__VAL_SVAL);
    CHECK(value->sval != NULL && strlen(value->sval->sval) == LONG_TEXT);
    for (i = 0U; i < LONG_TEXT; i++) CHECK(value->sval->sval[i] == 'x');
    CHECK(select->n_from_clause == 1U && select->where_clause != NULL);
    CHECK(select->n_sort_clause == 1U && select->limit_count != NULL);
}

typedef struct {
    size_t calls, length;
    uint8_t *bytes;
    int empty;
} observation_t;

static void observe(const PgQuery__ParseResult *tree, void *context)
{
    observation_t *observation = context;
    /* Conversion may use a private allocation context, but observers retain
     * the caller's ordinary raw-tree context and palloc/pfree semantics. */
    CHECK(CurrentMemoryContext != NULL && CurrentMemoryContext != TopMemoryContext);
    observation->calls++;
    CHECK(observation->calls == 1U);
    if (observation->empty) CHECK(tree != NULL && tree->n_stmts == 0U);
    else {
        const PgQuery__SelectStmt *select;
        const PgQuery__AConst *value;
        void *scratch;
        check_tree(tree);
        select = tree->stmts[1]->stmt->select_stmt;
        value = constant(select->target_list[2]->res_target->val, PG_QUERY__A__CONST__VAL_SVAL);
        /* Scalar text is borrowed from the raw tree, whose palloc allocation
         * gives an exact context identity without inspecting its layout. */
        CHECK(GetMemoryChunkContext(value->sval->sval) == CurrentMemoryContext);
        scratch = palloc(128U);
        CHECK(scratch != NULL && GetMemoryChunkContext(scratch) == CurrentMemoryContext);
        memset(scratch, 0xa5, 128U);
        pfree(scratch); /* An observer still has ordinary palloc/pfree semantics. */
    }
    observation->length = pg_query__parse_result__get_packed_size(tree);
    observation->bytes = malloc(observation->length);
    CHECK(observation->bytes != NULL);
    CHECK(pg_query__parse_result__pack(tree, observation->bytes) == observation->length);
    /* The only retained tree representation is independently malloc-owned. */
}

static void vendor_lifetime(const char *sql)
{
    observation_t observation = {0}, empty = {0}, invalid = {0};
    PgQueryProtobufParseResult actual, reference, churn, bad, none;
    PgQuery__ParseResult *tree;
    size_t i;
    sqlparser_pg_query_prepare();
    actual = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        sql, PG_QUERY_PARSE_DEFAULT, observe, &observation);
    reference = pg_query_parse_protobuf_opts_preserving_identifier_spelling(
        sql, PG_QUERY_PARSE_DEFAULT);
    CHECK(actual.error == NULL && reference.error == NULL);
    CHECK(actual.parse_tree.data != NULL && reference.parse_tree.data != NULL);
    CHECK(actual.parse_tree.len == reference.parse_tree.len);
    CHECK(memcmp(actual.parse_tree.data, reference.parse_tree.data, actual.parse_tree.len) == 0);
    CHECK(observation.calls <= 1U); /* The C++ backend has no live C observer. */
    if (observation.calls != 0U) {
        CHECK(observation.length == actual.parse_tree.len);
        CHECK(memcmp(observation.bytes, actual.parse_tree.data, observation.length) == 0);
    }
    CHECK(CurrentMemoryContext == TopMemoryContext);
    pg_query_exit();
    CHECK(CurrentMemoryContext == NULL && TopMemoryContext == NULL);
    for (i = 0U; i < 8U; i++) {
        churn = pg_query_parse_protobuf(sql);
        CHECK(churn.error == NULL && churn.parse_tree.data != NULL);
        pg_query_free_protobuf_parse_result(churn);
        pg_query_exit();
    }
    CHECK(memcmp(actual.parse_tree.data, reference.parse_tree.data, actual.parse_tree.len) == 0);
    if (observation.calls != 0U)
        CHECK(memcmp(observation.bytes, actual.parse_tree.data, observation.length) == 0);
    tree = pg_query__parse_result__unpack(NULL, actual.parse_tree.len,
        (const uint8_t *)actual.parse_tree.data);
    CHECK(tree != NULL);
    pg_query_free_protobuf_parse_result(actual);
    pg_query_free_protobuf_parse_result(reference);
    free(observation.bytes);
    check_tree(tree);
    pg_query__parse_result__free_unpacked(tree, NULL);
    empty.empty = 1;
    none = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        "-- empty input\n", PG_QUERY_PARSE_DEFAULT, observe, &empty);
    CHECK(none.error == NULL && none.parse_tree.data != NULL);
    CHECK(empty.calls <= 1U);
    pg_query_free_protobuf_parse_result(none);
    free(empty.bytes);
    bad = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        "SELECT 1; SELECT )", PG_QUERY_PARSE_DEFAULT, observe, &invalid);
    CHECK(bad.error != NULL && invalid.calls == 0U && invalid.bytes == NULL);
    pg_query_exit();
    CHECK(bad.error->message != NULL && bad.error->message[0] != '\0');
    pg_query_free_protobuf_parse_result(bad);
}

static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_handle_t *handle = NULL;
    CHECK(sqlparser_parse(sql, &handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(handle != NULL && sqlparser_statement_count(handle) == 2U);
    return handle;
}

static void public_lifetime(const char *sql)
{
    size_t round;
    for (round = 0U; round < 4U; round++) {
        sqlparser_handle_t *handle = parse(sql), *other = parse(sql), *reparsed;
        sqlparser_handle_t *bad = NULL;
        sqlparser_query_graph_view_t graph, fresh;
        sqlparser_graph_dml_t dml;
        sqlparser_literal_view_t literal;
        sqlparser_patch_t patch = {0};
        sqlparser_patch_list_t patches = {&patch, 1U};
        char *unchanged = NULL, *output = NULL, *second = NULL, *cell = NULL;
        size_t count;
        CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
        CHECK(dml.target_columns.count == COLUMNS && dml.rows.count == ROWS * COLUMNS);
        CHECK(sqlparser_statement_query_graph(handle, 1U, &fresh, &error) == SQLPARSER_STATUS_OK);
        /* Direct columns/literals do not require function/opaque expression
         * entries. Check actual graph content rather than that sparse store. */
        CHECK(fresh.has_root_block && fresh.block_count > 0U);
        CHECK(fresh.relation_count == 1U && fresh.target_count == 3U);
        CHECK(fresh.field_count > 0U && fresh.value_count > 0U && fresh.predicate_count > 0U);
        pg_query_exit();
        CHECK(sqlparser_deparse(handle, &unchanged, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(unchanged, sql) == 0);
        patch.op = SQLPARSER_PATCH_REPLACE;
        patch.selector = "stmt[0].insert_cell[0][1]";
        patch.sql = "'arena ''replacement'''";
        CHECK(sqlparser_apply_patch(handle, &patches, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(sqlparser_statement_query_graph(handle, 0U, &fresh, &error) == SQLPARSER_STATUS_OK);
        CHECK(fresh.generation == graph.generation + 1UL);
        CHECK(sqlparser_insert_cell_literal(handle, 0U, 0U, 1U, &literal, &error) == SQLPARSER_STATUS_OK);
        CHECK(literal.kind == SQLPARSER_LITERAL_KIND_STRING);
        CHECK(strcmp(literal.string_value, "arena 'replacement'") == 0);
        CHECK(sqlparser_insert_cell_sql(handle, 0U, 0U, 1U, &cell, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(cell, patch.sql) == 0);
        CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(unchanged, sql) == 0);
        CHECK(sqlparser_deparse(other, &second, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(second, sql) == 0);
        sqlparser_string_free(second); second = NULL;
        sqlparser_handle_destroy(handle);
        sqlparser_handle_destroy(other);
        pg_query_exit();
        /* The deparsed string is caller-owned and can create a fresh handle. */
        reparsed = parse(output);
        CHECK(sqlparser_insert_row_count(reparsed, 0U, &count, &error) == SQLPARSER_STATUS_OK);
        CHECK(count == ROWS);
        CHECK(sqlparser_insert_cell_literal(reparsed, 0U, 0U, 1U, &literal, &error) == SQLPARSER_STATUS_OK);
        CHECK(literal.kind == SQLPARSER_LITERAL_KIND_STRING);
        CHECK(strcmp(literal.string_value, "arena 'replacement'") == 0);
        CHECK(sqlparser_deparse(reparsed, &second, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(second, output) == 0);
        sqlparser_handle_destroy(reparsed);
        CHECK(sqlparser_parse("SELECT 1; SELECT )", &bad, &error) == SQLPARSER_STATUS_PARSE_ERROR);
        CHECK(bad == NULL);
        pg_query_exit();
        CHECK(strcmp(unchanged, sql) == 0 && strcmp(second, output) == 0);
        sqlparser_string_free(unchanged);
        sqlparser_string_free(output);
        sqlparser_string_free(second);
        sqlparser_string_free(cell);
    }
}

#ifdef SQLPARSER_CONVERSION_OUTPUT_OOM_WRAPPERS
static void output_oom(const char *sql)
{
    sqlparser_handle_t *handle;
    size_t round;
    for (round = 0U; round < 3U; round++) {
        handle = NULL;
        output_failures = 0U;
        fail_output = 1;
        CHECK(sqlparser_parse(sql, &handle, &error) == SQLPARSER_STATUS_NO_MEMORY);
        CHECK(handle == NULL && output_failures == 1U);
        CHECK(CurrentMemoryContext == TopMemoryContext);
        fail_output = 0;
        handle = parse(sql);
        sqlparser_handle_destroy(handle);
        pg_query_exit();
    }
    handle = NULL;
    output_failures = 0U;
    fail_output = 1;
    CHECK(sqlparser_parse("SELECT 1; SELECT )", &handle, &error) == SQLPARSER_STATUS_PARSE_ERROR);
    CHECK(handle == NULL && output_failures == 1U);
    fail_output = 0;
    handle = parse(sql);
    sqlparser_handle_destroy(handle);
    pg_query_exit();
}
#endif

int main(void)
{
    char *sql = make_sql();
    vendor_lifetime(sql);
    public_lifetime(sql);
#ifdef SQLPARSER_CONVERSION_OUTPUT_OOM_WRAPPERS
    output_oom(sql);
#endif
    free(sql);
    pg_query_exit();
    puts("parser conversion lifetime: observer context, mixed scalars/lists, packed lifetime, graph/patch/deparse and recovery passed");
    return 0;
}
