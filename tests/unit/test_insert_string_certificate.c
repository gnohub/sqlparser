/* Native wire/location parity is stronger than comparing deparsed SQL alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"

static sqlparser_error_t error;
static size_t case_number, commits;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d case=%zu %s: %s\n", __FILE__, __LINE__, case_number, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_CERTIFICATE_WRAPPERS
sqlparser_status_t __real_sqlparser_handle_commit_certified_insert_strings(sqlparser_handle_t *, char **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_handle_commit_certified_insert_strings(sqlparser_handle_t *h, char **sql, sqlparser_error_t *e)
{
    commits++;
    return __real_sqlparser_handle_commit_certified_insert_strings(h, sql, e);
}
#define CHECK_COMMITS(n) CHECK(commits == (n))
#else
#define CHECK_COMMITS(n) ((void)(n))
#endif

static sqlparser_handle_t *parse(const char *sql, int dialect)
{
    sqlparser_handle_t *handle = NULL;
    sqlparser_parse_options_t options;
    sqlparser_parse_options_default(&options); options.dialect = (sqlparser_dialect_t)dialect;
    CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    return handle;
}

static void compare(sqlparser_handle_t *actual, sqlparser_handle_t *expected, size_t rows, size_t columns)
{
    char *a = NULL, *b = NULL;
    sqlparser_bind_occurrence_view_t ab, bb;
    sqlparser_query_graph_view_t ag, bg;
    size_t row, column, ac, bc;
    CHECK(actual->parse_tree.len == expected->parse_tree.len);
    CHECK(memcmp(actual->parse_tree.data, expected->parse_tree.data, actual->parse_tree.len) == 0);
    CHECK(sqlparser_deparse(actual, &a, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(expected, &b, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(a, b) == 0); sqlparser_string_free(a); sqlparser_string_free(b);
    CHECK(sqlparser_export_view_json(actual, 0U, &a, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_export_view_json(expected, 0U, &b, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(a, b) == 0); sqlparser_string_free(a); sqlparser_string_free(b);
    CHECK(sqlparser_handle_bind_occurrences(actual, &ab, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_bind_occurrences(expected, &bb, &error) == SQLPARSER_STATUS_OK);
    CHECK(ab.count == bb.count && ab.count == 0U);
    CHECK(sqlparser_statement_query_graph(actual, 0U, &ag, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(expected, 0U, &bg, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_count(&ag, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_count(&bg, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc);
    for (row = 0U; row < rows; row++) for (column = 0U; column < columns; column++) {
        sqlparser_selector_t selector;
        sqlparser_literal_view_t al, bl;
        char text[80];
        snprintf(text, sizeof(text), "stmt[0].insert_cell[%zu][%zu]", row, column);
        CHECK(sqlparser_selector_parse(text, &selector, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_selector_insert_cell_literal(actual, &selector, &al, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_selector_insert_cell_literal(expected, &selector, &bl, &error) == SQLPARSER_STATUS_OK);
        CHECK(al.kind == bl.kind);
        if (al.kind == SQLPARSER_LITERAL_KIND_STRING) CHECK(strcmp(al.string_value, bl.string_value) == 0);
        else CHECK(al.integer_value == bl.integer_value);
        CHECK(sqlparser_selector_insert_cell_sql(actual, &selector, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_selector_insert_cell_sql(expected, &selector, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(a, b) == 0); sqlparser_string_free(a); sqlparser_string_free(b);
    }
}

#define ROWS 17U
#define COLS 7U
static char *make_sql(char contents[ROWS][COLS][80], size_t rows, size_t columns, int envelope)
{
    char *sql = malloc(32768U), *p = sql;
    size_t r, c;
    CHECK(sql != NULL);
    p += sprintf(p, "%sINSERT INTO t(", envelope ? " \n/* head */ -- header\n" : "");
    for (c = 0U; c < columns; c++) p += sprintf(p, "%sc%zu", c ? "," : "", c);
    p += sprintf(p, ") VALUES ");
    for (r = 0U; r < rows; r++) {
        p += sprintf(p, "%s(", r ? ", /* row */ " : "");
        for (c = 0U; c < columns; c++) {
            if (c == 0U) p += sprintf(p, "%zu", r);
            else p += sprintf(p, ",'%s'", contents[r][c]);
        }
        p += sprintf(p, ")");
    }
    if (envelope) p += sprintf(p, " ; /* tail */ \n");
    return sql;
}

static void native_cases(void)
{
    size_t sizes[][2] = {{1,3}, {3,4}, {17,7}};
    size_t size, order, typed, sparse, envelope;
    for (size = 0U; size < 3U; size++) for (order = 0U; order < 3U; order++)
    for (typed = 0U; typed < 2U; typed++) for (sparse = 0U; sparse < 2U; sparse++)
    for (envelope = 0U; envelope < 2U; envelope++) {
        char contents[ROWS][COLS][80] = {{{0}}};
        size_t rows = sizes[size][0], columns = sizes[size][1], r, c, step, before;
        sqlparser_handle_t *handle;
        char *input;
        case_number++;
        for (r = 0U; r < rows; r++) for (c = 1U; c < columns; c++)
            snprintf(contents[r][c], 80U, "original-%zu-%zu", r, c);
        input = make_sql(contents, rows, columns, (int)envelope); handle = parse(input, SQLPARSER_DIALECT_MYSQL); free(input);
        for (step = 0U; step < 3U; step++) {
            sqlparser_patch_t patches[ROWS * COLS + 2U] = {{0}};
            sqlparser_literal_value_t values[ROWS * COLS + 2U] = {{0}};
            sqlparser_patch_list_t list = {patches, 0U};
            sqlparser_query_graph_view_t stale;
            sqlparser_graph_dml_t dml;
            sqlparser_handle_t *reference;
            size_t sequence[ROWS * COLS], count = 0U, index;
            unsigned long generation = handle->generation;
            char *expected;
            CHECK(sqlparser_statement_query_graph(handle, 0U, &stale, &error) == SQLPARSER_STATUS_OK);
            for (r = 0U; r < rows; r++) for (c = 1U; c < columns; c++) {
                if (sparse && rows > 1U && (r + c) % 2U) continue;
                sequence[count++] = r * columns + c;
            }
            if (order == 1U) for (index = 0U; index < count / 2U; index++) {
                size_t t = sequence[index]; sequence[index] = sequence[count - index - 1U]; sequence[count - index - 1U] = t;
            }
            if (order == 2U) for (index = count; index > 1U; index--) {
                size_t target = (index * 17U + 3U) % index, t = sequence[index - 1U];
                sequence[index - 1U] = sequence[target]; sequence[target] = t;
            }
            for (index = 0U; index < count; index++) {
                char selector[80], sql[90];
                r = sequence[index] / columns; c = sequence[index] % columns;
                if (step != 2U) {
                    if ((r + c + step) % 3U == 0U) contents[r][c][0] = '\0';
                    else snprintf(contents[r][c], 80U, step == 0U ? "longer-?-$1-:name-%zu-%zu" : "x%zu%zu", r, c);
                }
                snprintf(selector, sizeof(selector), "stmt[0].insert_cell[%zu][%zu]", r, c);
                patches[index].op = SQLPARSER_PATCH_REPLACE;
                patches[index].selector = sqlparser_strdup(selector);
                if (typed) {
                    values[index].kind = SQLPARSER_LITERAL_KIND_STRING;
                    values[index].string_value = sqlparser_strdup(contents[r][c]); patches[index].literal = &values[index];
                } else {
                    snprintf(sql, sizeof(sql), "'%s'", contents[r][c]); patches[index].sql = sqlparser_strdup(sql);
                }
            }
            list.count = count;
            before = commits;
            memset(&error, 0x5a, sizeof(error));
            CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
            CHECK_COMMITS(before + 1U);
            CHECK(error.code == SQLPARSER_STATUS_OK && error.message[0] == '\0');
            CHECK(handle->ast == NULL && handle->patch_batch_flags == 0U && handle->surface_source_complete);
            CHECK(handle->generation == generation + 1UL && handle->sql == handle->parser_sql);
            for (index = 0U; index < count; index++) {
                free((void *)patches[index].selector); free((void *)patches[index].sql); free((void *)values[index].string_value);
            }
            CHECK(sqlparser_query_graph_dml(&stale, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
            expected = make_sql(contents, rows, columns, (int)envelope);
            CHECK(strcmp(sqlparser_original_sql(handle), expected) == 0);
            reference = parse(expected, SQLPARSER_DIALECT_MYSQL); free(expected);
            compare(handle, reference, rows, columns);
            sqlparser_handle_destroy(reference);
        }
        sqlparser_handle_destroy(handle);
    }
}

static void fallback_cases(void)
{
    /* Mixed identity-source trees now certify unchanged nested/UTF-8 cells.
     * Dialect rewrites, extra statements and joined literal tokens still use
     * the generic path. Preserve exact wire parity for both decisions. */
    static const struct { const char *sql; int certified; } inputs[] = {
        {"INSERT INTO `t`(a,b,c) VALUES ('a','b',1)", 0},
        {"INSERT INTO t(a,b,c) VALUES ('a','b','Ω')", 1},
        {"INSERT INTO t(a,b,c) VALUES ('a','b','line\nline')", 1},
        {"INSERT INTO t(a,b,c) VALUES ('a','b','can''t')", 1},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',N'national')", 0},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',('nested'))", 1},
        {"INSERT INTO t(a,b,c) VALUES ('a','b','one'\n 'two')", 0},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',?)", 0},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',1+2)", 1},
        {"INSERT IGNORE INTO t(a,b,c) VALUES ('a','b',1)", 0},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',1) ON DUPLICATE KEY UPDATE c=2", 0},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',-1)", 1},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',2147483648)", 1},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',TRUE)", 1},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',NULL)", 1},
        {"INSERT INTO s.t(a,b,c) VALUES ('a','b',1)", 1},
        {"INSERT INTO t(a,b,c) VALUES ('a','b',1); INSERT INTO u(a) VALUES ('z')", 0},
    };
    size_t index;
    for (index = 0U; index < sizeof(inputs) / sizeof(inputs[0]); index++) {
        sqlparser_handle_t *h = parse(inputs[index].sql, SQLPARSER_DIALECT_MYSQL), *reference;
        sqlparser_query_graph_view_t graph;
        sqlparser_patch_t p[2] = {{0}};
        sqlparser_patch_list_t list = {p, 2U};
        size_t before = commits;
        char *output = NULL;
        case_number++;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        p[0].op = p[1].op = SQLPARSER_PATCH_REPLACE;
        p[0].selector = "stmt[0].insert_cell[0][0]"; p[0].sql = "'changed-long'";
        p[1].selector = "stmt[0].insert_cell[0][1]"; p[1].sql = "''";
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK_COMMITS(before + (size_t)inputs[index].certified);
        CHECK(sqlparser_deparse(h, &output, &error) == SQLPARSER_STATUS_OK);
        reference = parse(output, SQLPARSER_DIALECT_MYSQL);
        CHECK(h->parse_tree.len == reference->parse_tree.len);
        CHECK(memcmp(h->parse_tree.data, reference->parse_tree.data, h->parse_tree.len) == 0);
        sqlparser_string_free(output); sqlparser_handle_destroy(h); sqlparser_handle_destroy(reference);
    }
}


static void borrowed_and_following_edits(int typed)
{
    const char *input = "/*h*/ INSERT INTO t(a,b,c,d) VALUES ('left','right',"
        "'stmt[0].insert_cell[0][0]','stmt[0].insert_cell[0][1]'),"
        "('keep-a','keep-b','keep-c','keep-d'); /*t*/";
    const char *expected = "/*h*/ INSERT INTO t(a,b,c,d) VALUES ('right','left',"
        "'stmt[0].insert_cell[0][0]','stmt[0].insert_cell[0][1]'),"
        "('keep-a','keep-b','keep-c','keep-d'); /*t*/";
    sqlparser_handle_t *h = parse(input, SQLPARSER_DIALECT_MYSQL), *reference;
    sqlparser_query_graph_view_t graph;
    sqlparser_graph_dml_cell_t cells[4];
    sqlparser_graph_dml_t dml;
    sqlparser_literal_value_t literal[2] = {{0}};
    sqlparser_patch_t p[2] = {{0}};
    sqlparser_patch_list_t list = {p, 2U};
    size_t i, index, before = commits;
    char *raw[2] = {NULL, NULL};
    case_number++;
    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
    for (i = 0U; i < 4U; i++) {
        CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, i, &index, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_cell_at(&graph, index, &cells[i], &error) == SQLPARSER_STATUS_OK);
    }
    for (i = 0U; i < 2U; i++) {
        p[i].op = SQLPARSER_PATCH_REPLACE;
        p[i].selector = cells[i + 2U].literal.string_value;
        if (typed) {
            literal[i].kind = SQLPARSER_LITERAL_KIND_STRING;
            literal[i].string_value = cells[1U - i].literal.string_value;
            p[i].literal = &literal[i];
        } else {
            char text[32];
            snprintf(text, sizeof(text), "'%s'", cells[1U - i].literal.string_value);
            raw[i] = sqlparser_strdup(text); CHECK(raw[i] != NULL);
            p[i].sql = raw[i];
        }
    }
    CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
    CHECK_COMMITS(before + 1U);
    CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    for (i = 0U; !typed && i < 2U; i++) {
        CHECK(strcmp(raw[i], i == 0U ? "'right'" : "'left'") == 0);
        memset(raw[i], 0xa7, strlen(raw[i])); free(raw[i]);
    }
    reference = parse(expected, SQLPARSER_DIALECT_MYSQL);
    CHECK(h->parse_tree.len == reference->parse_tree.len);
    CHECK(memcmp(h->parse_tree.data, reference->parse_tree.data, h->parse_tree.len) == 0);
    /* Apply again immediately, without a graph/deparse intervening: it must
     * still use the existing non-resident-AST route and have identical state. */
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'again'"};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .sql="'again-b'"};
    CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_apply_patch(reference, &list, &error) == SQLPARSER_STATUS_OK);
    compare(h, reference, 2U, 4U);
    for (i = 0U; i < 7U; i++) {
        char *a = NULL, *b = NULL;
        sqlparser_patch_t follow = {0};
        sqlparser_patch_list_t one = {&follow, 1U};
        follow.op = SQLPARSER_PATCH_REPLACE;
        follow.selector = "stmt[0].insert_cell[0][0]";
        if (i == 0U) follow.source_selector = "stmt[0].insert_cell[1][1]";
        if (i == 1U) follow.sql = "'can''t'";
        if (i == 2U) follow.sql = "123";
        if (i == 3U) follow = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN,
            .selector="stmt[0].insert_columns", .index=2U, .name="new_col", .default_sql="'new'"};
        if (i == 4U) follow = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ROW,
            .selector="stmt[0].insert_row[1]"};
        if (i == 5U) follow = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_COLUMN,
            .selector="stmt[0].insert_columns", .index=2U};
        if (i == 6U) follow = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].relation[0]", .name="RenamedTable"};
        CHECK(sqlparser_apply_patch(h, &one, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_apply_patch(reference, &one, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(h, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(reference, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(a, b) == 0); sqlparser_string_free(a); sqlparser_string_free(b);
        CHECK(sqlparser_export_view_json(h, 0U, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_export_view_json(reference, 0U, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(a, b) == 0); sqlparser_string_free(a); sqlparser_string_free(b);
    }
    sqlparser_handle_destroy(h); sqlparser_handle_destroy(reference);
}

static void replacement_fallbacks(void)
{
    const char *contents[] = {"can't", "Ω", "back\\slash", "line\nline"};
    size_t i;
    for (i = 0U; i < sizeof(contents) / sizeof(contents[0]); i++) {
        sqlparser_handle_t *h = parse("INSERT INTO t(a,b) VALUES ('a','b')", SQLPARSER_DIALECT_MYSQL), *ref;
        sqlparser_query_graph_view_t graph;
        sqlparser_literal_value_t literal = {0};
        sqlparser_patch_t p[2] = {{0}};
        sqlparser_patch_list_t list = {p, 2U};
        char *output = NULL;
        size_t before = commits;
        case_number++;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        literal.kind = SQLPARSER_LITERAL_KIND_STRING; literal.string_value = contents[i];
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&literal};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .sql="'plain'"};
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK_COMMITS(before);
        CHECK(sqlparser_deparse(h, &output, &error) == SQLPARSER_STATUS_OK);
        ref = parse(output, SQLPARSER_DIALECT_MYSQL);
        compare(h, ref, 1U, 2U);
        sqlparser_string_free(output); sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
    }
}


static void planner_boundaries(void)
{
    const char *source = "INSERT INTO t(a,b) VALUES ('one','two')";
    size_t mode;
    for (mode = 0U; mode < 6U; mode++) {
        sqlparser_handle_t *h = parse(source, SQLPARSER_DIALECT_MYSQL);
        sqlparser_query_graph_view_t graph;
        sqlparser_patch_t p[2] = {{0}};
        sqlparser_patch_list_t list = {p, 2U};
        size_t before = commits;
        case_number++;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'expanded'"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .sql="''"};
        if (mode < 4U) {
            if (mode < 2U) h->limits.max_sql_bytes = strlen(source) + 1U;
            else h->limits.max_output_bytes = strlen(source) + 1U;
            /* The readonly planner must fall back, preserving generic-path
             * size/error precedence (it does not impose a new per-edit limit).
             * A fresh non-resident-AST handle is an independent generic oracle. */
            sqlparser_handle_t *reference = parse(source, SQLPARSER_DIALECT_MYSQL);
            sqlparser_status_t actual, expected;
            sqlparser_error_t actual_error;
            reference->limits = h->limits;
            if (mode & 1U) p[0].selector = "stmt[99].insert_cell[0][0]";
            actual = sqlparser_apply_patch(h, &list, &error); actual_error = error;
            expected = sqlparser_apply_patch(reference, &list, &error);
            CHECK(actual == expected && actual_error.code == error.code);
            CHECK(strcmp(actual_error.message, error.message) == 0 && h->failed == reference->failed);
            CHECK_COMMITS(before);
            sqlparser_handle_destroy(reference);
        } else {
            /* Sorted source restoration checks a grow before a shrink even if
             * the caller requests them in reverse order. Preserve that rule. */
            p[0].sql = "'sixsix'"; p[1].sql = "''";
            h->limits.max_sql_bytes = h->limits.max_output_bytes = strlen(source) + 3U;
            if (mode == 5U) { sqlparser_patch_t swap = p[0]; p[0] = p[1]; p[1] = swap; }
            CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
            CHECK_COMMITS(before + 1U);
        }
        sqlparser_handle_destroy(h);
    }
}

/* A fresh non-resident-AST handle takes the generic route. Compare complete
 * failure diagnostics and later observable state, not only accept/reject. */
static void limit_parity(const sqlparser_patch_list_t *list, size_t sql_limit, size_t output_limit)
{
    const char *source = "INSERT INTO t(a,b) VALUES ('one','two')";
    sqlparser_handle_t *actual = parse(source, SQLPARSER_DIALECT_MYSQL);
    sqlparser_handle_t *expected = parse(source, SQLPARSER_DIALECT_MYSQL);
    sqlparser_query_graph_view_t stale;
    sqlparser_graph_dml_t dml;
    sqlparser_status_t a, b;
    sqlparser_error_t ae;
    char *asql = NULL, *bsql = NULL;
    case_number++;
    CHECK(sqlparser_statement_query_graph(actual, 0U, &stale, &error) == SQLPARSER_STATUS_OK);
    actual->limits.max_sql_bytes = expected->limits.max_sql_bytes = sql_limit;
    actual->limits.max_output_bytes = expected->limits.max_output_bytes = output_limit;
    a = sqlparser_apply_patch(actual, list, &error); ae = error;
    b = sqlparser_apply_patch(expected, list, &error);
    CHECK(a == b && ae.code == error.code && ae.cursor == error.cursor &&
        ae.line == error.line && ae.column == error.column && strcmp(ae.message, error.message) == 0);
    CHECK(actual->failed == expected->failed);
    CHECK(sqlparser_query_graph_dml(&stale, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    a = sqlparser_deparse(actual, &asql, &error); ae = error;
    b = sqlparser_deparse(expected, &bsql, &error);
    CHECK(a == b && ae.code == error.code && ae.cursor == error.cursor &&
        ae.line == error.line && ae.column == error.column && strcmp(ae.message, error.message) == 0);
    CHECK(actual->failed == expected->failed);
    if (a == SQLPARSER_STATUS_OK) CHECK(strcmp(asql, bsql) == 0);
    else CHECK(asql == NULL && bsql == NULL);
    sqlparser_string_free(asql); sqlparser_string_free(bsql);
    sqlparser_handle_destroy(actual); sqlparser_handle_destroy(expected);
}

static void all_raw_boundaries(void)
{
    const size_t source_length = strlen("INSERT INTO t(a,b) VALUES ('one','two')");
    sqlparser_patch_t p[3] = {{0}};
    sqlparser_patch_list_t list = {p, 2U};
    char text[160];
    size_t order, mode, offset;
    for (mode = 0U; mode < 2U; mode++) for (order = 0U; order < 2U; order++) {
        /* The earlier invalid selector wins over a later overlong plain
         * payload; swapping them preserves the existing fragment-limit error. */
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector=mode ? "invalid-selector" : "stmt[99].insert_cell[0][0]", .sql="'plain'"};
        text[0] = '\''; memset(text + 1U, 'x', 128U); text[129] = '\''; text[130] = '\0';
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][1]", .sql=text};
        if (order) { sqlparser_patch_t t = p[0]; p[0] = p[1]; p[1] = t; }
        limit_parity(&list, source_length, 1024U);
    }
    for (mode = 0U; mode < 2U; mode++) for (offset = 0U; offset < 3U; offset++)
    for (order = 0U; order < 2U; order++) {
        size_t length = source_length + offset - 1U;
        /* Exact fragment length is checked even when source growth or the
         * output limit makes the read-only plan fall back afterwards. */
        text[0] = '\''; memset(text + 1U, 'x', length - 2U);
        text[length - 1U] = '\''; text[length] = '\0';
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][0]", .sql=text};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][1]", .sql="''"};
        if (order) { sqlparser_patch_t t = p[0]; p[0] = p[1]; p[1] = t; }
        limit_parity(&list, mode ? 1024U : source_length, mode ? source_length : 1024U);
    }
    for (mode = 0U; mode < 2U; mode++) for (offset = 0U; offset < 3U; offset++)
    for (order = 0U; order < 2U; order++) {
        size_t limit = source_length + offset - 1U;
        /* Final output has the original length, but the grow-first ordering
         * temporarily exceeds it. Duplicate grow/shrink keeps length paired
         * with the retained payload as well. */
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][0]", .sql="''"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][1]", .sql="'sixsix'"};
        if (order) { sqlparser_patch_t t = p[0]; p[0] = p[1]; p[1] = t; }
        limit_parity(&list, mode ? 1024U : limit, mode ? limit : 1024U);
        p[2] = p[1]; p[2].sql = "''"; list.count = 3U;
        limit_parity(&list, mode ? 1024U : limit, mode ? limit : 1024U);
        list.count = 2U;
    }
}

int main(void)
{
    native_cases(); fallback_cases(); borrowed_and_following_edits(0); borrowed_and_following_edits(1);
    replacement_fallbacks(); planner_boundaries(); all_raw_boundaries();
    printf("INSERT string certificate: %zu cases, %zu certified commits; native wire, source, view, selectors and fallback parity passed\n", case_number, commits);
    return 0;
}
