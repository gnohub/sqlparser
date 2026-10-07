/* Run against both reference and optimized libraries and compare the output.
 * This exercises complete externally observable results and certified-commit
 * decisions, while separately checking that proved spans skip rediscovery. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/core/sqlparser_ast_internal.h"

static size_t case_number, commits, spans;
static int applying;
static sqlparser_error_t error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d case=%zu %s: %s\n", __FILE__, __LINE__, case_number, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_SOURCE_PROOF_WRAPPERS
sqlparser_status_t __real_sqlparser_handle_commit_certified_insert_strings(sqlparser_handle_t *, char **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_handle_commit_certified_insert_strings(sqlparser_handle_t *h, char **sql, sqlparser_error_t *e)
{
    commits++;
    return __real_sqlparser_handle_commit_certified_insert_strings(h, sql, e);
}
int __real_sqlparser_view_insert_cell_source_span(sqlparser_handle_t *, const sqlparser_surface_source_edits_t *,
    sqlparser_view_expression_source_cache_t *, int, size_t, size_t, size_t, size_t *, size_t *, sqlparser_error_t *);
int __wrap_sqlparser_view_insert_cell_source_span(sqlparser_handle_t *h, const sqlparser_surface_source_edits_t *edits,
    sqlparser_view_expression_source_cache_t *cache, int comments, size_t statement, size_t row, size_t column,
    size_t *start, size_t *end, sqlparser_error_t *e)
{
    if (applying) spans++;
    return __real_sqlparser_view_insert_cell_source_span(h, edits, cache, comments, statement, row, column, start, end, e);
}
#endif

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t length)
{
    const unsigned char *p = data;
    size_t i;
    for (i = 0U; i < length; i++) hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}

static void snapshot(sqlparser_handle_t *h, sqlparser_status_t status, sqlparser_error_t e, size_t round)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    sqlparser_query_graph_view_t graph;
    sqlparser_graph_dml_t dml;
    char *sql = NULL, *json = NULL;
    size_t i;
    printf("%zu:%zu status=%d code=%d cursor=%d line=%d column=%d message=%s failed=%d generation=%lu commits=%zu",
        case_number, round, status, e.code, e.cursor, e.line, e.column, e.message, h->failed, h->generation, commits);
    if (status == SQLPARSER_STATUS_OK) {
        sqlparser_status_t deparsed = sqlparser_deparse(h, &sql, &error);
        printf(" deparse=%d/%d/%d/%d/%d:%s", deparsed, error.code, error.cursor, error.line, error.column, error.message);
        if (deparsed != SQLPARSER_STATUS_OK) {
            CHECK(deparsed == SQLPARSER_STATUS_RESOURCE_LIMIT && sql == NULL);
            CHECK(h->failed);
            printf(" terminal=%d\n", h->failed);
            return;
        }
        CHECK(sqlparser_export_view_json(h, 0U, &json, &error) == SQLPARSER_STATUS_OK);
        hash = hash_bytes(hash, h->parse_tree.data, h->parse_tree.len);
        hash = hash_bytes(hash, sql, strlen(sql) + 1U);
        hash = hash_bytes(hash, json, strlen(json) + 1U);
        hash = hash_bytes(hash, h->sql, h->sql_len + 1U);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
        for (i = 0U; i < dml.rows.count; i++) {
            size_t index;
            sqlparser_graph_dml_cell_t cell;
            CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, i, &index, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error) == SQLPARSER_STATUS_OK);
            hash = hash_bytes(hash, &cell.kind, sizeof(cell.kind));
            hash = hash_bytes(hash, &cell.row_index, sizeof(cell.row_index));
            hash = hash_bytes(hash, &cell.column_ordinal, sizeof(cell.column_ordinal));
            if (cell.kind == SQLPARSER_GRAPH_VALUE_LITERAL && cell.literal.kind == SQLPARSER_LITERAL_KIND_STRING)
                hash = hash_bytes(hash, cell.literal.string_value, strlen(cell.literal.string_value) + 1U);
        }
        printf(" wire=%zu hash=%016llx", h->parse_tree.len, (unsigned long long)hash);
    } else {
        CHECK(h->failed);
        CHECK(sqlparser_deparse(h, &sql, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT && sql == NULL);
    }
    putchar('\n');
    sqlparser_string_free(sql);
    sqlparser_string_free(json);
}

static void run_case(const char *source, size_t source_index, size_t mode, int typed)
{
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_t options;
    sqlparser_query_graph_view_t stale;
    sqlparser_graph_dml_t dml;
    sqlparser_graph_dml_cell_t borrowed;
    sqlparser_literal_value_t values[4] = {{0}};
    sqlparser_patch_t p[4] = {{0}};
    sqlparser_patch_list_t list = {p, 2U};
    sqlparser_status_t status;
    size_t i, before_spans, before_commits;
    char long_value[192], long_sql[196];
    const char *selectors[] = {"stmt[0].insert_cell[0][1]", "stmt[0].insert_cell[2][1]",
        "stmt[0].insert_cell[1][1]", "stmt[0].insert_cell[0][1]"};
    const char *strings[] = {"expanded-value", "", "third", "last"};
    const char *raw[] = {"'expanded-value'", "''", "'third'", "'last'"};
    case_number++;
    sqlparser_parse_options_default(&options); options.dialect = SQLPARSER_DIALECT_MYSQL;
    CHECK(sqlparser_parse_with_options(source, &options, &h, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(h, 0U, &stale, &error) == SQLPARSER_STATUS_OK);
    for (i = 0U; i < 4U; i++) {
        p[i].op = SQLPARSER_PATCH_REPLACE; p[i].selector = selectors[i];
        if (typed) {
            values[i].kind = SQLPARSER_LITERAL_KIND_STRING; values[i].string_value = strings[i]; p[i].literal = &values[i];
        } else p[i].sql = raw[i];
    }
    if (mode == 1U) { sqlparser_patch_t swap = p[0]; p[0] = p[1]; p[1] = swap; }
    if (mode == 2U) list.count = 3U;
    if (mode == 3U) { list.count = 4U; }
    if (mode == 4U) p[1].selector = "stmt[0].insert_cell[0][1]";
    if (mode == 5U) p[1].selector = "stmt[0].insert_cell[99][1]";
    if (mode == 6U) p[0].selector = "invalid-selector";
    if (mode == 7U) p[1].selector = "stmt[1].insert_cell[0][1]";
    if (mode == 8U) { h->limits.max_sql_bytes = strlen(source); }
    if (mode == 9U) { h->limits.max_output_bytes = strlen(source); }
    if (mode == 10U || mode == 11U) {
        memset(long_value, 'x', sizeof(long_value) - 1U); long_value[sizeof(long_value) - 1U] = '\0';
        snprintf(long_sql, sizeof(long_sql), "'%s'", long_value);
        if (typed) values[1].string_value = long_value; else p[1].sql = long_sql;
        h->limits.max_sql_bytes = strlen(source);
        if (mode == 10U) p[0].selector = "stmt[0].insert_cell[99][1]";
        else p[1].selector = "stmt[0].insert_cell[99][1]";
    }
    if (mode == 12U) {
        if (typed) values[0].string_value = "can\\'t\nΩ";
        else p[0].sql = "'can''t'";
    }
    if (mode == 13U) {
        size_t index;
        CHECK(sqlparser_query_graph_dml(&stale, &dml, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_span_index_at(&stale, dml.rows, 7U, &index, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_cell_at(&stale, index, &borrowed, &error) == SQLPARSER_STATUS_OK);
        CHECK(borrowed.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
        values[0].kind = SQLPARSER_LITERAL_KIND_STRING; values[0].string_value = borrowed.literal.string_value;
        p[0].sql = NULL; p[0].literal = &values[0];
    }
    if (mode == 14U) {
        /* A failed static source proof must not be recomputed, and immutable
         * source/AST facts must not substitute for current bookkeeping. */
        h->identifier_spelling_status = SQLPARSER_STATUS_UNSUPPORTED;
    }
    before_spans = spans; before_commits = commits;
    applying = 1; status = sqlparser_apply_patch(h, &list, &error); applying = 0;
    snapshot(h, status, error, 0U);
#ifdef SQLPARSER_SOURCE_PROOF_OPTIMIZED
    if (source_index == 0U && (mode < 3U || mode == 13U)) {
        CHECK(status == SQLPARSER_STATUS_OK && commits == before_commits + 1U);
        CHECK(spans == before_spans);
    }
#else
    (void)source_index;
#endif
    (void)before_spans; (void)before_commits;
    CHECK(sqlparser_query_graph_dml(&stale, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    if (status == SQLPARSER_STATUS_OK && !h->failed && mode < 5U) {
        /* A second graph-built batch verifies that the proof was call-local
         * and shifted locations are the new source's offsets. */
        h->limits.max_sql_bytes = h->limits.max_output_bytes = 1048576U;
        list.count = 2U;
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector=selectors[0], .sql="'again'"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector=selectors[1], .sql="'again-long'"};
        applying = 1; status = sqlparser_apply_patch(h, &list, &error); applying = 0;
        snapshot(h, status, error, 1U);
    }
    sqlparser_handle_destroy(h);
}

/* Exercise naturally retained dialect/source bookkeeping after a generic
 * mutation, rather than constructing a private dialect-state layout. */
static void after_generic_mutation(size_t mode, int typed)
{
    const char *source = "INSERT INTO t(id,s,n) VALUES (0,'one',1),(1,'two',2),(2,'three',3)";
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_t options;
    sqlparser_query_graph_view_t stale;
    sqlparser_graph_dml_t dml;
    sqlparser_patch_t prior = {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].value[1]", .sql="'prior'"};
    sqlparser_patch_t p[2] = {{0}};
    sqlparser_patch_list_t one = {&prior, 1U}, list = {p, 2U};
    sqlparser_literal_value_t values[2] = {{0}};
    sqlparser_status_t status;
    size_t i, before = commits, before_spans, value_index;
    char value_selector[80];

    case_number++;
    sqlparser_parse_options_default(&options); options.dialect = SQLPARSER_DIALECT_MYSQL;
    CHECK(sqlparser_parse_with_options(source, &options, &h, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_find_statement_literal_node(h, 0U, 1U, 0, NULL, &value_index, NULL, &error) == SQLPARSER_STATUS_OK);
    snprintf(value_selector, sizeof(value_selector), "stmt[0].value[%zu]", value_index);
    prior.selector = value_selector;
    if (mode == 1U) prior.sql = "N'prior'";
    if (mode == 2U) prior = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
        .selector="stmt[0].relation[0]", .name="RenamedTable"};
    if (mode == 3U) prior.sql = "upper('prior')";
    applying = 1; status = sqlparser_apply_patch(h, &one, &error); applying = 0;
    CHECK(status == SQLPARSER_STATUS_OK && commits == before);
    snapshot(h, status, error, 0U);
    CHECK(sqlparser_statement_query_graph(h, 0U, &stale, &error) == SQLPARSER_STATUS_OK);
    p[0].selector = "stmt[0].insert_cell[1][1]";
    p[1].selector = "stmt[0].insert_cell[2][1]";
    for (i = 0U; i < 2U; i++) {
        p[i].op = SQLPARSER_PATCH_REPLACE;
        if (typed) {
            values[i].kind = SQLPARSER_LITERAL_KIND_STRING;
            values[i].string_value = i ? "" : "after-generic";
            p[i].literal = &values[i];
        } else p[i].sql = i ? "''" : "'after-generic'";
    }
    before_spans = spans;
    applying = 1; status = sqlparser_apply_patch(h, &list, &error); applying = 0;
    CHECK(status == SQLPARSER_STATUS_OK);
    snapshot(h, status, error, 1U);
    CHECK(sqlparser_query_graph_dml(&stale, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
#ifdef SQLPARSER_SOURCE_PROOF_OPTIMIZED
    if (mode == 0U) CHECK(commits == before + 1U && spans == before_spans);
#endif
    (void)before_spans;
    sqlparser_handle_destroy(h);
}

int main(void)
{
    static const char *sources[] = {
        "INSERT INTO t(id,s,n) VALUES (0,'one',1),(1,'two',2),(2,'three',3)",
        " /*head*/ INSERT /*mid*/ INTO t(id,s,n) VALUES (0,'one',1), /*row*/ (1,'two',2),(2,'three',3); -- tail\n",
        "INSERT INTO t(id,s,n) VALUES (0,/*before*/'one',1),(1,'two',2),(2,'three',3)",
        "INSERT INTO t(id,s,n) VALUES (0,'one'/*after*/,1),(1,'two',2),(2,'three',3)",
        "INSERT INTO t(id,s,n) VALUES (0, -- before\n'one',1),(1,'two',2),(2,'three',3)",
        "INSERT INTO t(id,s,n) VALUES (0,'one' -- after\n,1),(1,'two',2),(2,'three',3)",
        "INSERT INTO t(id,s,n) VALUES (0,'one',1),(1,'two',2),(2,'three',-3)",
        "INSERT INTO t(id,s,n) VALUES (0,'one',1),(1,'two',2),(2,'three','Ω')",
        "INSERT INTO t(id,s,n) VALUES (0,('one'),1),(1,'two',2),(2,'three',3)",
        "INSERT INTO `t`(id,s,n) VALUES (0,'one',1),(1,'two',2),(2,'three',3)",
        "INSERT INTO t(id,s,n) VALUES (0,'one',1),(1,'two',2),(2,'three','one'\n 'two')",
        "INSERT INTO t(id,s,n) VALUES (0,'one',1),(1,'two',2),(2,'three','can''t')"
    };
    size_t s, mode, typed;
    for (s = 0U; s < sizeof(sources) / sizeof(sources[0]); s++)
        for (mode = 0U; mode < 15U; mode++) for (typed = 0U; typed < 2U; typed++)
            run_case(sources[s], s, mode, (int)typed);
    for (mode = 0U; mode < 4U; mode++) for (typed = 0U; typed < 2U; typed++)
        after_generic_mutation(mode, (int)typed);
    fprintf(stderr, "INSERT source-proof differential: %zu cases, %zu commits, %zu generic span calls\n", case_number, commits, spans);
    return 0;
}
