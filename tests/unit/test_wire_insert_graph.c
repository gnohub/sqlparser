/* Private wire graph's observable contract, cache lifetimes, and fallback
 * parity. The generic graph is forced independently before every comparison.
 * Optional wrappers: SQLPARSER_WIRE_GRAPH_WRAPPERS requires
 * --wrap=pg_query__parse_result__unpack. SQLPARSER_WIRE_GRAPH_ALLOC_WRAPPERS
 * additionally requires --wrap=malloc,--wrap=calloc,--wrap=realloc and
 * --wrap=pg_query_enter_memory_context,--wrap=pg_query_exit_memory_context. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "sqlparser_internal.h"
#include "../../src/internal/sqlparser_wire_insert_internal.h"

static sqlparser_error_t error;
static size_t cases, unpacks;
static uint8_t *tracked_wire;
static size_t tracked_wire_length, tracked_unpacks;
static const char *stage;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d case=%zu stage=%s %s: %s\n", __FILE__, __LINE__, cases, stage ? stage : "", #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_WIRE_GRAPH_WRAPPERS
PgQuery__ParseResult *__real_pg_query__parse_result__unpack(ProtobufCAllocator *, size_t, const uint8_t *);
PgQuery__ParseResult *__wrap_pg_query__parse_result__unpack(ProtobufCAllocator *a, size_t n, const uint8_t *p)
{
    ++unpacks;
    if (tracked_wire && n == tracked_wire_length && memcmp(p, tracked_wire, n) == 0) ++tracked_unpacks;
    return __real_pg_query__parse_result__unpack(a, n, p);
}
#define UNPACKS(n) CHECK(unpacks == (n))
#define MAIN_UNPACKS(n) CHECK(tracked_unpacks == (n))
#else
#define UNPACKS(n) ((void)(n))
#define MAIN_UNPACKS(n) ((void)(n))
#endif

#ifdef SQLPARSER_WIRE_GRAPH_ALLOC_WRAPPERS
static size_t allocation_count, allocation_fail_at, allocation_fail_at_second, allocation_failures;
static int allocation_armed;
static unsigned native_context_depth;
/* Native PostgreSQL context OOM can exit the process in the generic path.
 * This suite injects failures at sqlparser/protobuf-c ownership boundaries,
 * not inside native parser/deparser contexts. Dedicated native output-OOM
 * tests retain their existing coverage. Both context wrappers are required. */
struct MemoryContextData;
struct MemoryContextData *__real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(struct MemoryContextData *);
struct MemoryContextData *__wrap_pg_query_enter_memory_context(void)
{
    ++native_context_depth;
    return __real_pg_query_enter_memory_context();
}
void __wrap_pg_query_exit_memory_context(struct MemoryContextData *context)
{
    __real_pg_query_exit_memory_context(context);
    CHECK(native_context_depth != 0U);
    --native_context_depth;
}
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static int reject_allocation(void)
{
    if (!allocation_armed || native_context_depth != 0U) return 0;
    ++allocation_count;
    if (allocation_count != allocation_fail_at && allocation_count != allocation_fail_at_second) return 0;
    ++allocation_failures;
    return 1;
}
void *__wrap_malloc(size_t n) { return reject_allocation() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t s) { return reject_allocation() ? NULL : __real_calloc(n, s); }
void *__wrap_realloc(void *p, size_t n) { return reject_allocation() ? NULL : __real_realloc(p, n); }
#endif

static sqlparser_handle_t *parse(const char *sql, int dialect)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_default(&options);
    options.dialect = (sqlparser_dialect_t)dialect;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    CHECK(h != NULL && h->ast == NULL);
    return h;
}

/* Large whitespace prefixes cross source-location varints without consuming
 * the compact certificate's text budget. */
static char *source(size_t rows, size_t padding, const char *table,
    const char *col0, const char *col1, const char *const *values, const char *first)
{
    static const unsigned int integers[] = {0U, 127U, 128U, 16383U, 16384U, 2147483647U};
    size_t i, size = padding + strlen(table) + strlen(col0) + strlen(col1) + 64U;
    char *sql, *p;
    for (i = 0U; i < rows; ++i) size += strlen(values ? values[i] : "old") + 48U;
    if (first) size += strlen(first);
    sql = malloc(size); CHECK(sql != NULL);
    memset(sql, ' ', padding); p = sql + padding;
    p += sprintf(p, "INSERT INTO %s(%s,%s) VALUES\n", table, col0, col1);
    for (i = 0U; i < rows; ++i) {
        p += sprintf(p, "%s(", i ? ",\n" : "");
        if (i == 0U && first) p += sprintf(p, "%s", first);
        else p += sprintf(p, "%u", integers[i % (sizeof(integers) / sizeof(integers[0]))]);
        p += sprintf(p, ",'%s')", values ? values[i] : "old");
    }
    p += sprintf(p, ";\n"); CHECK((size_t)(p - sql) < size);
    return sql;
}

static void same_text(const char *a, const char *b)
{
    CHECK((a == NULL) == (b == NULL));
    if (a) CHECK(strcmp(a, b) == 0);
}

/* Public getters zero the complete output structures. Comparing them after
 * checking/removing every pointer covers flags, selectors, reserved-zero
 * values, ordinals, source links and spans, rather than only cell payloads. */
static void same_literal(sqlparser_literal_view_t *a, sqlparser_literal_view_t *b)
{
    same_text(a->string_value, b->string_value); same_text(a->float_value, b->float_value);
    a->string_value = b->string_value = NULL;
    a->float_value = b->float_value = NULL;
    CHECK(memcmp(a, b, sizeof(*a)) == 0);
}

static void same_span(const sqlparser_query_graph_view_t *a, sqlparser_index_span_t as,
    const sqlparser_query_graph_view_t *b, sqlparser_index_span_t bs)
{
    size_t i, ai, bi;
    CHECK(as.offset == bs.offset && as.count == bs.count);
    for (i = 0U; i < as.count; ++i) {
        CHECK(sqlparser_query_graph_span_index_at(a, as, i, &ai, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_span_index_at(b, bs, i, &bi, &error) == SQLPARSER_STATUS_OK);
        CHECK(ai == bi);
    }
    CHECK(sqlparser_query_graph_span_index_at(a, as, as.count, &ai, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
}

static void same_graph(sqlparser_handle_t *actual, sqlparser_handle_t *reference)
{
    sqlparser_query_graph_view_t ag, bg, av, bv;
    sqlparser_graph_dml_t ad, bd;
    size_t i, ai, bi, ac, bc;
    CHECK(sqlparser_handle_ensure_ast(reference, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(actual, 0U, &ag, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(reference, 0U, &bg, &error) == SQLPARSER_STATUS_OK);
    av = ag; bv = bg; av.handle = bv.handle = NULL; av.generation = bv.generation = 0UL;
    CHECK(memcmp(&av, &bv, sizeof(av)) == 0);
    CHECK(ag.target_count == 0U && ag.field_count == 0U && ag.value_count == 0U);
    CHECK(ag.set_count == 0U && ag.predicate_count == 0U && ag.dml_branch_count == 0U);
    CHECK(sqlparser_query_graph_expression_count(&ag, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_count(&bg, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc && ac == 0U);
    CHECK(sqlparser_query_graph_expression_argument_count(&ag, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_argument_count(&bg, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc && ac == 0U);
    for (i = 0U; i < ag.block_count; ++i) {
        sqlparser_graph_block_t a, b;
        CHECK(sqlparser_query_graph_block_at(&ag, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_block_at(&bg, i, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        same_span(&ag, a.relations, &bg, b.relations);
        same_span(&ag, a.targets, &bg, b.targets);
        same_span(&ag, a.predicates, &bg, b.predicates);
    }
    for (i = 0U; i < ag.relation_count; ++i) {
        sqlparser_graph_relation_t a, b;
        CHECK(sqlparser_query_graph_relation_at(&ag, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_relation_at(&bg, i, &b, &error) == SQLPARSER_STATUS_OK);
#define REL_TEXT(member) same_text(a.member, b.member); a.member = b.member = NULL
        REL_TEXT(database_name); REL_TEXT(schema_name); REL_TEXT(object_name);
        REL_TEXT(alias_name); REL_TEXT(link_name);
#undef REL_TEXT
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    CHECK(sqlparser_query_graph_dml_count(&ag, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_count(&bg, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc && ac == 1U);
    CHECK(sqlparser_query_graph_dml(&ag, &ad, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(&bg, &bd, &error) == SQLPARSER_STATUS_OK);
    CHECK(memcmp(&ad, &bd, sizeof(ad)) == 0);
    same_span(&ag, ad.target_columns, &bg, bd.target_columns);
    same_span(&ag, ad.rows, &bg, bd.rows);
    same_span(&ag, ad.assignments, &bg, bd.assignments);
    same_span(&ag, ad.delete_targets, &bg, bd.delete_targets);
    same_span(&ag, ad.branches, &bg, bd.branches);
    for (i = 0U; i < ad.target_columns.count; ++i) {
        sqlparser_graph_dml_column_t a, b;
        CHECK(sqlparser_query_graph_span_index_at(&ag, ad.target_columns, i, &ai, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_span_index_at(&bg, bd.target_columns, i, &bi, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_column_at(&ag, ai, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_column_at(&bg, bi, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a.column_name, b.column_name); a.column_name = b.column_name = NULL;
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    for (i = 0U; i < ad.rows.count; ++i) {
        sqlparser_graph_dml_cell_t a, b;
        CHECK(sqlparser_query_graph_span_index_at(&ag, ad.rows, i, &ai, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_span_index_at(&bg, bd.rows, i, &bi, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_cell_at(&ag, ai, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_cell_at(&bg, bi, &b, &error) == SQLPARSER_STATUS_OK);
        same_literal(&a.literal, &b.literal); CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    CHECK(sqlparser_query_graph_dml_result_count(&ag, 0U, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_result_count(&bg, 0U, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc && ac == 0U);
    {
        int ah, bh;
        CHECK(sqlparser_query_graph_dml_parent(&ag, 0U, &ai, &ah, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_parent(&bg, 0U, &bi, &bh, &error) == SQLPARSER_STATUS_OK);
        CHECK(ai == bi && ah == bh);
    }
}

static void same_wire(sqlparser_handle_t *a, sqlparser_handle_t *b)
{
    CHECK(a->parse_tree.len == b->parse_tree.len);
    CHECK(memcmp(a->parse_tree.data, b->parse_tree.data, a->parse_tree.len) == 0);
}

static void same_json(sqlparser_handle_t *a, sqlparser_handle_t *b)
{
    char *as = NULL, *bs = NULL;
    CHECK(sqlparser_export_view_json(a, 0, &as, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_export_view_json(b, 0, &bs, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(as, bs) == 0); sqlparser_string_free(as); sqlparser_string_free(bs);
}

static sqlparser_graph_dml_cell_t cell(const sqlparser_query_graph_view_t *graph, size_t row, size_t column)
{
    sqlparser_graph_dml_t dml;
    sqlparser_graph_dml_cell_t out;
    size_t index;
    CHECK(sqlparser_query_graph_dml(graph, &dml, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_span_index_at(graph, dml.rows, row * 2U + column, &index, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_cell_at(graph, index, &out, &error) == SQLPARSER_STATUS_OK);
    return out;
}

static void assert_fast(sqlparser_handle_t *h)
{
    const sqlparser_wire_insert_t *cert = sqlparser_query_graph_wire_insert(h);
    CHECK(h->ast == NULL && cert != NULL && cert->row_count >= 32U);
    CHECK(cert->text_bytes + 4U * cert->row_count + sizeof(*cert) <= 32U * cert->row_count);
}

static void native_repeated(void)
{
    /* The first quote is 30 bytes after the prefix, giving exact original
     * string locations 126/127/128 and 16382/16383/16384. */
    static const size_t paddings[] = {0U, 96U, 97U, 98U, 16352U, 16353U, 16354U, 16356U};
    size_t mode, round, i;
    stage = "native repeats, source locations and borrowed inputs";
    for (mode = 0U; mode < sizeof(paddings) / sizeof(paddings[0]); ++mode) {
        enum { ROWS = 32, ROUNDS = 4 };
        const char *values[ROWS];
        char storage[ROWS][40], *sql, *retained[ROUNDS] = {0};
        sqlparser_handle_t *h;
        for (i = 0U; i < ROWS; ++i) {
            snprintf(storage[i], sizeof(storage[i]), "v%zu", i); values[i] = storage[i];
        }
        values[0] = ""; values[1] = "?-$1-:name-! []";
        values[2] = "stmt[0].insert_cell[0][1]";
        values[3] = "stmt[0].insert_cell[1][1]";
        sql = source(ROWS, paddings[mode], "t", "id", "s", values, NULL);
        h = parse(sql, SQLPARSER_DIALECT_MYSQL); ++cases;
        for (round = 0U; round < ROUNDS; ++round) {
            sqlparser_handle_t *ref;
            sqlparser_query_graph_view_t graph;
            sqlparser_graph_dml_t dml;
            sqlparser_graph_dml_cell_t c0, c1, s0, s1;
            sqlparser_patch_t p[2] = {{0}};
            sqlparser_literal_value_t literal[2] = {{0}};
            sqlparser_patch_list_t list = {p, 2U}, empty = {NULL, 0U};
            char *raw[2] = {0}, *expected;
            unsigned long generation = h->generation;
            size_t before = unpacks;
            CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
            assert_fast(h); UNPACKS(before);
            {
                sqlparser_wire_insert_cell_t locations[2];
                CHECK(sqlparser_wire_insert_row(sqlparser_query_graph_wire_insert(h), 0U, locations));
                CHECK(locations[0].location == (int32_t)(paddings[mode] + 28U));
                CHECK(locations[1].location == (int32_t)(paddings[mode] + 30U));
                CHECK(h->sql[locations[1].location] == '\'');
            }
            c0 = cell(&graph, 0U, 1U); c1 = cell(&graph, 1U, 1U);
            s0 = cell(&graph, 2U, 1U); s1 = cell(&graph, 3U, 1U);
            CHECK(sqlparser_apply_patch(h, &empty, &error) == SQLPARSER_STATUS_OK);
            CHECK(h->generation == generation);
            CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
            for (i = 0U; i < 2U; ++i) {
                p[i].op = SQLPARSER_PATCH_REPLACE;
                p[i].selector = i ? s1.literal.string_value : s0.literal.string_value;
                if ((mode + round) & 1U) {
                    literal[i].kind = SQLPARSER_LITERAL_KIND_STRING;
                    literal[i].string_value = i ? c0.literal.string_value : c1.literal.string_value;
                    p[i].literal = &literal[i];
                } else {
                    const char *v = i ? c0.literal.string_value : c1.literal.string_value;
                    raw[i] = malloc(strlen(v) + 3U); CHECK(raw[i] != NULL);
                    sprintf(raw[i], "'%s'", v); p[i].sql = raw[i];
                }
            }
            /* Reverse source ordering exercises edit sorting and both string
             * width changes. The fourth round is an exact no-op batch. */
            if (round == ROUNDS - 1U) {
                literal[0].kind = literal[1].kind = SQLPARSER_LITERAL_KIND_STRING;
                literal[0].string_value = c0.literal.string_value;
                literal[1].string_value = c1.literal.string_value;
                p[0].literal = &literal[0]; p[1].literal = &literal[1];
                p[0].sql = p[1].sql = NULL;
            } else {
                const char *swap = values[0]; values[0] = values[1]; values[1] = swap;
            }
            if (round & 1U) { sqlparser_patch_t swap = p[0]; p[0] = p[1]; p[1] = swap; }
            before = unpacks;
            CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
            UNPACKS(before); CHECK(h->ast == NULL && h->generation == generation + 1UL);
            CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
            for (i = 0U; i < 2U; ++i) if (raw[i]) { memset(raw[i], 0xa7, strlen(raw[i])); free(raw[i]); }
            expected = source(ROWS, paddings[mode], "t", "id", "s", values, NULL);
            CHECK(strcmp(sqlparser_original_sql(h), expected) == 0);
            ref = parse(expected, SQLPARSER_DIALECT_MYSQL); free(expected);
            same_wire(h, ref); /* Includes every native A_Const location. */
            CHECK(sqlparser_deparse(h, &retained[round], &error) == SQLPARSER_STATUS_OK);
            CHECK(h->ast == NULL);
            same_graph(h, ref); assert_fast(h);
            if (round == ROUNDS - 1U) same_json(h, ref);
            sqlparser_handle_destroy(ref);
        }
        sqlparser_handle_destroy(h); free(sql);
        for (round = 0U; round < ROUNDS; ++round) {
            sqlparser_handle_t *ref = parse(retained[round], SQLPARSER_DIALECT_MYSQL);
            sqlparser_handle_destroy(ref); sqlparser_string_free(retained[round]);
        }
    }
}

static void read_and_json_lifetimes(void)
{
    size_t mode;
    stage = "read-only graph and JSON lifetimes";
    for (mode = 0U; mode < 4U; ++mode) {
        char *sql = source(32U, 0U, "t", "id", "s", NULL, NULL), *json = NULL;
        sqlparser_handle_t *h = parse(sql, SQLPARSER_DIALECT_MYSQL), *ref = parse(sql, SQLPARSER_DIALECT_MYSQL);
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_dml_cell_t borrowed, after;
        sqlparser_graph_relation_t relation, relation_after;
        sqlparser_literal_view_t literal;
        unsigned long generation = h->generation;
        size_t before = unpacks;
        ++cases;
        if (mode == 0U) {
            CHECK(sqlparser_insert_cell_literal(h, 0U, 0U, 1U, &literal, &error) == SQLPARSER_STATUS_OK);
            CHECK(strcmp(literal.string_value, "old") == 0 && h->ast != NULL);
            UNPACKS(before + 1U);
        }
        before = unpacks;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        UNPACKS(before);
        if (mode != 0U) assert_fast(h);
        borrowed = cell(&graph, 31U, 1U);
        CHECK(sqlparser_query_graph_relation_at(&graph, 0U, &relation, &error) == SQLPARSER_STATUS_OK);
        if (mode == 1U) {
            CHECK(sqlparser_insert_cell_literal(h, 0U, 0U, 1U, &literal, &error) == SQLPARSER_STATUS_OK);
            CHECK(strcmp(literal.string_value, "old") == 0); UNPACKS(before + 1U);
        }
        if (mode == 3U) {
            h->limits.max_output_bytes = 1U;
            CHECK(sqlparser_export_view_json(h, 0, &json, &error) == SQLPARSER_STATUS_RESOURCE_LIMIT);
            CHECK(json == NULL && !h->failed);
            h->limits.max_output_bytes = 1048576U;
        } else {
            CHECK(sqlparser_export_view_json(h, 0, &json, &error) == SQLPARSER_STATUS_OK);
            sqlparser_string_free(json); json = NULL;
        }
        CHECK(h->generation == generation);
        after = cell(&graph, 31U, 1U);
        CHECK(after.literal.string_value == borrowed.literal.string_value);
        CHECK(strcmp(borrowed.literal.string_value, "old") == 0);
        CHECK(sqlparser_query_graph_relation_at(&graph, 0U, &relation_after, &error) == SQLPARSER_STATUS_OK);
        CHECK(relation.object_name == relation_after.object_name && strcmp(relation.object_name, "t") == 0);
        same_graph(h, ref); same_json(h, ref);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref); free(sql);
    }
}

static void wire_size_boundaries(void)
{
    static const size_t row_counts[] = {33U, 127U, 128U, 1024U};
    static const size_t lengths[] = {0U, 127U, 128U, 16383U, 16384U};
    size_t mode, width;
    stage = "bulk wire and replacement-length varints";
    for (mode = 0U; mode < sizeof(row_counts) / sizeof(row_counts[0]); ++mode)
    for (width = 0U; width < sizeof(lengths) / sizeof(lengths[0]); ++width) {
        size_t rows = row_counts[mode], i, before;
        const char **values = malloc(rows * sizeof(*values));
        char *value = malloc(lengths[width] + 1U), *sql, *expected, *output = NULL;
        char last_selector[80];
        sqlparser_handle_t *h, *ref;
        sqlparser_query_graph_view_t graph;
        sqlparser_patch_t patches[2] = {{0}};
        sqlparser_literal_value_t literal = {0};
        sqlparser_patch_list_t list = {patches, 2U};
        CHECK(values != NULL && value != NULL); ++cases;
        memset(value, 'x', lengths[width]); value[lengths[width]] = '\0';
        for (i = 0U; i < rows; ++i) values[i] = "old";
        sql = source(rows, 0U, "t", "id", "s", values, NULL);
        h = parse(sql, SQLPARSER_DIALECT_MYSQL);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK); assert_fast(h);
        snprintf(last_selector, sizeof(last_selector), "stmt[0].insert_cell[%zu][1]", rows - 1U);
        literal.kind = SQLPARSER_LITERAL_KIND_STRING; literal.string_value = value;
        patches[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector=last_selector, .literal=&literal};
        patches[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .sql="''"};
        before = unpacks;
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        UNPACKS(before); CHECK(h->ast == NULL);
        values[0] = ""; values[rows - 1U] = value;
        expected = source(rows, 0U, "t", "id", "s", values, NULL);
        ref = parse(expected, SQLPARSER_DIALECT_MYSQL);
        CHECK(strcmp(sqlparser_original_sql(h), expected) == 0); same_wire(h, ref);
        memset(value, 0xa7, lengths[width]); free(value);
        CHECK(sqlparser_deparse(h, &output, &error) == SQLPARSER_STATUS_OK); CHECK(h->ast == NULL);
        same_graph(h, ref); same_json(h, ref);
        sqlparser_string_free(output); sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
        free(values); free(expected); free(sql);
    }
}

static void fallback_eligibility(void)
{
    size_t mode, i;
    stage = "strict eligibility and long-string varints";
    for (mode = 0U; mode < 18U; ++mode) {
        const char *values[32], *table = "t", *c0 = "id", *first = NULL;
        char *sql, *extended, *large = NULL;
        size_t rows = mode == 0U ? 31U : 32U;
        int dialect = mode == 1U ? SQLPARSER_DIALECT_POSTGRESQL : SQLPARSER_DIALECT_MYSQL;
        sqlparser_handle_t *h, *ref;
        sqlparser_query_graph_view_t graph;
        for (i = 0U; i < 32U; ++i) values[i] = "old";
        if (mode == 2U) table = "`t`";
        if (mode == 3U) table = "s.t";
        if (mode == 4U) c0 = "`id`";
        if (mode == 5U) first = "-1";
        if (mode == 6U) first = "2147483648";
        if (mode == 7U) values[0] = "can''t";
        if (mode == 8U) values[0] = "line\nline";
        if (mode == 9U) values[0] = "Ω";
        if (mode == 10U) values[0] = "back\\slash";
        if (mode == 16U) table = "t AS alias";
        if (mode == 17U) c0 = "id,extra";
        if (mode >= 12U && mode < 16U) {
            size_t len = mode == 12U ? 127U : mode == 13U ? 128U : mode == 14U ? 16383U : 16384U;
            large = malloc(len + 1U); CHECK(large != NULL); memset(large, 'x', len); large[len] = '\0';
            for (i = 0U; i < 32U; ++i) values[i] = large;
        }
        sql = source(rows, 0U, table, c0, "s", values, first);
        if (mode == 11U) {
            extended = malloc(strlen(sql) + 20U); CHECK(extended != NULL);
            sprintf(extended, "/* source */ %s", sql); free(sql); sql = extended;
        }
        ++cases; h = parse(sql, dialect); ref = parse(sql, dialect);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_wire_insert(h) == NULL);
        /* The owner-certified PostgreSQL case, qualified names, large
         * numeric Floats and UTF-8 strings use the separate general scalar
         * certificate. The old two-column codec remains MySQL-only. */
        if (mode == 1U || mode == 3U || mode == 6U || mode == 9U)
            CHECK(sqlparser_query_graph_wire_scalar_insert(h) != NULL && h->ast == NULL);
        else CHECK(sqlparser_query_graph_wire_scalar_insert(h) == NULL && h->ast != NULL);
        same_graph(h, ref); same_json(h, ref); same_wire(h, ref);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref); free(sql); free(large);
    }
}

static void long_strings_and_repeated_fallback(void)
{
    enum { ROWS = 64 };
    size_t length, row, round;
    const size_t lengths[] = {32U, 33U, 49U};
    stage = "long-string early admission and repeated fast/generic transitions";
    for (length = 0U; length < sizeof(lengths) / sizeof(lengths[0]); length++) {
        const char *values[ROWS];
        char text[50], *sql;
        sqlparser_handle_t *h, *reference;
        sqlparser_wire_insert_t *certificate;
        memset(text, 'x', lengths[length]); text[lengths[length]] = '\0';
        for (row = 0U; row < ROWS; row++) values[row] = row == 0U ? text : "old";
        sql = source(ROWS, 0U, "t", "id", "s", values, NULL);
        h = parse(sql, SQLPARSER_DIALECT_MYSQL);
#ifdef SQLPARSER_WIRE_GRAPH_ALLOC_WRAPPERS
        allocation_count = allocation_failures = 0U; allocation_fail_at = 0U;
        allocation_armed = 1;
#endif
        certificate = sqlparser_wire_insert_certify(h);
#ifdef SQLPARSER_WIRE_GRAPH_ALLOC_WRAPPERS
        allocation_armed = 0;
        if (lengths[length] > 32U) CHECK(allocation_count == 0U);
#endif
        CHECK((certificate != NULL) == (lengths[length] == 32U));
        sqlparser_wire_insert_destroy(certificate);
        reference = parse(sql, SQLPARSER_DIALECT_MYSQL);
        same_graph(h, reference);
        if (lengths[length] == 32U) assert_fast(h);
        else CHECK(h->ast == NULL && sqlparser_query_graph_wire_insert(h) == NULL &&
            sqlparser_query_graph_wire_scalar_insert(h) != NULL);
        same_wire(h, reference); same_json(h, reference);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(reference); free(sql); ++cases;
    }
    {
        char *sql = source(ROWS, 0U, "t", "id", "s", NULL, NULL);
        sqlparser_handle_t *h = parse(sql, SQLPARSER_DIALECT_MYSQL);
        sqlparser_handle_t *reference = parse(sql, SQLPARSER_DIALECT_MYSQL);
        free(sql);
        for (round = 0U; round < 3U; round++) {
            sqlparser_query_graph_view_t graph, reference_graph;
            sqlparser_patch_t p[ROWS] = {{0}}, rp[ROWS] = {{0}};
            sqlparser_literal_value_t literal = {0}, reference_literal = {0};
            sqlparser_patch_list_t list = {p, ROWS}, reference_list = {rp, ROWS};
            char selectors[ROWS][64], *raw = NULL, *a = NULL, *b = NULL;
            CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
            if (round == 0U) assert_fast(h);
            else CHECK(h->ast == NULL && sqlparser_query_graph_wire_insert(h) == NULL &&
            sqlparser_query_graph_wire_scalar_insert(h) != NULL);
            CHECK(sqlparser_handle_ensure_ast(reference, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_statement_query_graph(reference, 0U, &reference_graph, &error) == SQLPARSER_STATUS_OK);
            if (round == 1U) {
                sqlparser_graph_dml_cell_t borrowed = cell(&graph, 0U, 1U);
                sqlparser_graph_dml_cell_t reference_borrowed = cell(&reference_graph, 0U, 1U);
                literal.kind = reference_literal.kind = SQLPARSER_LITERAL_KIND_STRING;
                literal.string_value = borrowed.literal.string_value;
                reference_literal.string_value = reference_borrowed.literal.string_value;
                CHECK(strlen(literal.string_value) == 49U);
            } else {
                size_t n = round == 0U ? 49U : 5U;
                raw = malloc(n + 3U); CHECK(raw != NULL);
                raw[0] = raw[n + 1U] = '\''; memset(raw + 1U, 'q', n); raw[n + 2U] = '\0';
            }
            for (row = 0U; row < ROWS; row++) {
                snprintf(selectors[row], sizeof(selectors[row]), "stmt[0].insert_cell[%zu][1]", row);
                p[row].op = rp[row].op = SQLPARSER_PATCH_REPLACE;
                p[row].selector = rp[row].selector = selectors[row];
                if (round == 1U) { p[row].literal = &literal; rp[row].literal = &reference_literal; }
                else p[row].sql = rp[row].sql = raw;
            }
            CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_apply_patch(reference, &reference_list, &error) == SQLPARSER_STATUS_OK);
            if (raw != NULL) { memset(raw, 0xa7, strlen(raw)); free(raw); }
            CHECK(sqlparser_deparse(h, &a, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_deparse(reference, &b, &error) == SQLPARSER_STATUS_OK);
            CHECK(!strcmp(a, b)); same_wire(h, reference);
            CHECK(h->generation == round + 1U);
            sqlparser_string_free(a); sqlparser_string_free(b); ++cases;
        }
        same_graph(h, reference); assert_fast(h);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(reference);
    }
}

static void same_error(sqlparser_status_t a, const sqlparser_error_t *ae,
    sqlparser_status_t b, const sqlparser_error_t *be)
{
    CHECK(a == b && ae->code == be->code && ae->cursor == be->cursor);
    CHECK(ae->line == be->line && ae->column == be->column);
    CHECK(strcmp(ae->message, be->message) == 0);
}

/* Ordered generic-oracle batches include duplicate selectors, a graph-borrowed
 * raw fragment, late parse errors and selector-vs-limit error precedence. */
static void patch_fallback_parity(void)
{
    size_t mode;
    stage = "ordered fallback and error parity";
    for (mode = 0U; mode < 19U; ++mode) {
        const char *values[32];
        size_t row;
        char *sql, *asql = NULL, *bsql = NULL;
        for (row = 0U; row < 32U; ++row) values[row] = mode == 18U ? "123" : "old";
        sql = source(32U, 0U, "t", "id", "s", values, NULL);
        sqlparser_handle_t *a = parse(sql, SQLPARSER_DIALECT_MYSQL), *b = parse(sql, SQLPARSER_DIALECT_MYSQL);
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_dml_t dml;
        sqlparser_patch_t p[3] = {{0}};
        sqlparser_literal_value_t integer = {0};
        sqlparser_patch_list_t list = {p, 2U};
        sqlparser_status_t ast, bst;
        sqlparser_error_t ae, be;
        unsigned long generation = a->generation;
        char *long_sql = malloc(strlen(sql) + 200U);
        ++cases; CHECK(long_sql != NULL);
        long_sql[0] = '\''; memset(long_sql + 1U, 'x', strlen(sql) + 100U);
        long_sql[strlen(sql) + 101U] = '\''; long_sql[strlen(sql) + 102U] = '\0';
        CHECK(sqlparser_statement_query_graph(a, 0U, &graph, &error) == SQLPARSER_STATUS_OK); assert_fast(a);
        CHECK(sqlparser_handle_ensure_ast(b, &error) == SQLPARSER_STATUS_OK);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .sql="'expanded'"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[31][1]", .sql="''"};
        if (mode == 0U) { p[1].selector = p[0].selector; p[1].sql = "'last-wins'"; }
        if (mode == 1U) { p[1].selector = "stmt[0].insert_cell[1][0]"; p[1].sql = "42"; }
        if (mode == 2U) { integer.kind = SQLPARSER_LITERAL_KIND_INTEGER; integer.integer_value = 42; p[1].literal = &integer; p[1].sql = NULL; }
        if (mode == 3U) p[1].sql = "upper('mixed')";
        if (mode == 4U) p[1].sql = "'can''t'";
        if (mode == 5U) { p[1].sql = NULL; p[1].source_selector = p[0].selector; }
        if (mode == 6U) p[0].selector = "invalid-selector";
        if (mode == 7U) p[1].selector = "invalid-selector";
        if (mode == 8U) p[1].selector = "stmt[0].insert_cell[99][1]";
        if (mode == 9U) p[1].sql = "'unterminated";
        if (mode == 10U) { p[0].selector = "invalid-selector"; p[1].sql = "'unterminated"; }
        if (mode == 11U) { p[0].sql = "'unterminated"; p[1].selector = "invalid-selector"; }
        if (mode >= 12U && mode < 18U) {
            if (mode & 1U) a->limits.max_output_bytes = b->limits.max_output_bytes = strlen(sql);
            else a->limits.max_sql_bytes = b->limits.max_sql_bytes = strlen(sql);
            p[1].sql = long_sql;
            if (mode == 14U || mode == 15U) p[0].selector = "stmt[0].insert_cell[99][1]";
            if (mode == 16U || mode == 17U) { p[0].sql = long_sql; p[1].sql = "'valid'"; p[1].selector = "invalid-selector"; }
        }
        if (mode == 18U) {
            sqlparser_graph_dml_cell_t borrowed = cell(&graph, 31U, 1U);
            p[1].sql = borrowed.literal.string_value;
        }
        /* Reference first also exercises graph-owned raw payloads without
         * accessing them after the tested handle invalidates its graph. */
        bst = sqlparser_apply_patch(b, &list, &be);
        tracked_wire_length = a->parse_tree.len;
        tracked_wire = malloc(tracked_wire_length); CHECK(tracked_wire != NULL);
        memcpy(tracked_wire, a->parse_tree.data, tracked_wire_length); tracked_unpacks = 0U;
        ast = sqlparser_apply_patch(a, &list, &ae);
        free(tracked_wire); tracked_wire = NULL;
        if (mode < 6U || mode == 18U) {
#ifdef SQLPARSER_WIRE_GRAPH_WRAPPERS
            if (tracked_unpacks != 1U) fprintf(stderr, "fallback mode=%zu main-unpacks=%zu status=%d ast=%p\n", mode, tracked_unpacks, (int)ast, (void *)a->ast);
#endif
            MAIN_UNPACKS(1U);
        }
        same_error(ast, &ae, bst, &be);
        CHECK(a->failed == b->failed && a->generation == b->generation);
        if (ast == SQLPARSER_STATUS_OK) CHECK(a->generation == generation + 1UL);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        if (ast == SQLPARSER_STATUS_OK) same_wire(a, b);
        free(long_sql); /* No caller payload is retained for deparse. */
        ast = sqlparser_deparse(a, &asql, &ae); bst = sqlparser_deparse(b, &bsql, &be);
        same_error(ast, &ae, bst, &be); same_text(asql, bsql); CHECK(a->failed == b->failed);
        if (ast == SQLPARSER_STATUS_OK) {
            a->limits.max_output_bytes = b->limits.max_output_bytes = 1048576U;
            same_json(a, b);
        }
        sqlparser_string_free(asql); sqlparser_string_free(bsql);
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b); free(sql);
    }
}


/* Each alias starts in the original compact graph slab. A one-item list must
 * miss both string batch planners, even for the second-column string case.
 * The oracle is prepared independently and owns a generic AST before apply. */
enum { GENERIC_ALIAS_MODES = 8, GENERIC_ALIAS_READS = 3 };
static char *generic_alias_source(void)
{
    const char *values[32];
    size_t row;
    for (row = 0U; row < 32U; row++) values[row] = "old";
    values[1] = "from_original_slab";
    values[2] = "stmt[0].insert_cell[0][0]";
    values[3] = "stmt[0].insert_cell[1][1]";
    values[4] = "stmt[0].relation[0]";
    values[5] = "renamed_from_slab";
    values[6] = "123.5";
    values[7] = "stmt[0].insert_cell[0][1]";
    values[8] = "4242";
    CHECK(strlen(values[2]) == 25U && strlen(values[3]) == 25U);
    return source(32U, 0U, "t", "id", "s", values, NULL);
}

typedef struct {
    sqlparser_handle_t *handle;
    sqlparser_query_graph_view_t graph;
    sqlparser_patch_t patch;
    sqlparser_literal_value_t literal;
    char *sql, *sql_copy;
    char raw[16];
} generic_alias_fixture_t;

static void generic_alias_prepare(generic_alias_fixture_t *f, size_t mode,
    size_t read_mode, int reference)
{
    sqlparser_graph_dml_cell_t values[9], after;
    sqlparser_graph_relation_t relation, relation_after;
    sqlparser_literal_view_t literal;
    char *json = NULL;
    size_t row;
    unsigned long generation;
    memset(f, 0, sizeof(*f));
    f->sql = generic_alias_source();
    f->sql_copy = malloc(strlen(f->sql) + 1U); CHECK(f->sql_copy != NULL);
    strcpy(f->sql_copy, f->sql); strcpy(f->raw, "4242");
    f->handle = parse(f->sql, SQLPARSER_DIALECT_MYSQL);
    if (reference) CHECK(sqlparser_handle_ensure_ast(f->handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(f->handle, 0U, &f->graph, &error) == SQLPARSER_STATUS_OK);
    if (!reference) assert_fast(f->handle);
    else CHECK(sqlparser_query_graph_wire_insert(f->handle) == NULL);
    generation = f->handle->generation;
    for (row = 0U; row < 9U; row++) values[row] = cell(&f->graph, row, 1U);
    CHECK(sqlparser_query_graph_relation_at(&f->graph, 0U, &relation, &error) == SQLPARSER_STATUS_OK);
    if (!reference && read_mode == 1U) {
        CHECK(sqlparser_insert_cell_literal(f->handle, 0U, 1U, 1U, &literal, &error) == SQLPARSER_STATUS_OK);
        CHECK(!strcmp(literal.string_value, "from_original_slab"));
    }
    if (!reference && read_mode == 2U) {
        CHECK(sqlparser_export_view_json(f->handle, 0, &json, &error) == SQLPARSER_STATUS_OK);
        sqlparser_string_free(json);
    }
    CHECK(f->handle->generation == generation);
    if (!reference) {
        CHECK((f->handle->ast != NULL) == (read_mode != 0U));
        CHECK(sqlparser_query_graph_wire_insert(f->handle) != NULL);
    }
    /* The old slab, rather than newly materialized AST strings, still backs
     * every alias passed to apply. Read-only materialization must preserve it. */
    for (row = 0U; row < 9U; row++) {
        after = cell(&f->graph, row, 1U);
        CHECK(after.literal.string_value == values[row].literal.string_value);
    }
    CHECK(sqlparser_query_graph_relation_at(&f->graph, 0U, &relation_after, &error) == SQLPARSER_STATUS_OK);
    CHECK(relation.object_name == relation_after.object_name);
    f->patch.op = SQLPARSER_PATCH_REPLACE;
    f->patch.selector = values[2].literal.string_value;
    f->literal.kind = SQLPARSER_LITERAL_KIND_STRING;
    if (mode == 0U) f->patch.sql = f->raw;
    if (mode == 7U) f->patch.sql = values[8].literal.string_value;
    if (mode == 1U || mode == 6U) {
        f->patch.selector = values[4].literal.string_value;
        f->patch.name = mode == 1U ? values[5].literal.string_value : relation.object_name;
    }
    if (mode == 2U) f->patch.source_selector = values[3].literal.string_value;
    if (mode == 3U || mode == 5U) {
        f->literal.string_value = values[1].literal.string_value;
        f->patch.literal = &f->literal;
        if (mode == 5U) f->patch.selector = values[7].literal.string_value;
    }
    if (mode == 4U) {
        f->literal.kind = SQLPARSER_LITERAL_KIND_FLOAT;
        f->literal.float_value = values[6].literal.string_value;
        f->patch.literal = &f->literal;
    }
}

static void generic_alias_release_inputs(generic_alias_fixture_t *f)
{
    CHECK(!strcmp(f->sql, f->sql_copy));
    CHECK(!strcmp(f->raw, "4242"));
    memset(f->sql, 0xa7, strlen(f->sql)); free(f->sql); f->sql = NULL;
    free(f->sql_copy); f->sql_copy = NULL;
    memset(f->raw, 0xa7, sizeof(f->raw));
    memset(&f->patch, 0xa7, sizeof(f->patch));
    memset(&f->literal, 0xa7, sizeof(f->literal));
}

static void generic_alias_compare(generic_alias_fixture_t *a, generic_alias_fixture_t *b)
{
    sqlparser_graph_dml_t dml;
    char *asql = NULL, *bsql = NULL;
    CHECK(!a->handle->failed && a->handle->generation == 1UL);
    CHECK(sqlparser_query_graph_dml(&a->graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    CHECK(sqlparser_query_graph_dml(&b->graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    same_wire(a->handle, b->handle);
    /* No caller buffer or payload structure may be retained by either path. */
    generic_alias_release_inputs(a); generic_alias_release_inputs(b);
    CHECK(sqlparser_deparse(a->handle, &asql, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(b->handle, &bsql, &error) == SQLPARSER_STATUS_OK);
    same_text(asql, bsql); same_wire(a->handle, b->handle);
    same_graph(a->handle, b->handle); same_json(a->handle, b->handle);
    sqlparser_string_free(asql); sqlparser_string_free(bsql);
}

static void generic_alias_lifetimes(void)
{
    size_t mode, read_mode;
    stage = "one-item generic fallback with original graph aliases";
    for (mode = 0U; mode < GENERIC_ALIAS_MODES; mode++)
    for (read_mode = 0U; read_mode < GENERIC_ALIAS_READS; read_mode++) {
        generic_alias_fixture_t a, b;
        sqlparser_patch_list_t ap, bp;
        sqlparser_patch_t saved_patch;
        sqlparser_literal_value_t saved_literal;
        generic_alias_prepare(&a, mode, read_mode, 0);
        generic_alias_prepare(&b, mode, read_mode, 1);
        ap = (sqlparser_patch_list_t){&a.patch, 1U};
        bp = (sqlparser_patch_list_t){&b.patch, 1U};
        saved_patch = a.patch; saved_literal = a.literal;
        CHECK(sqlparser_apply_patch(b.handle, &bp, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_apply_patch(a.handle, &ap, &error) == SQLPARSER_STATUS_OK);
        CHECK(!memcmp(&saved_patch, &a.patch, sizeof(saved_patch)));
        CHECK(!memcmp(&saved_literal, &a.literal, sizeof(saved_literal)));
        generic_alias_compare(&a, &b);
        sqlparser_handle_destroy(a.handle); sqlparser_handle_destroy(b.handle); ++cases;
    }
}


#if (defined(__unix__) || defined(__APPLE__)) && defined(MAP_ANONYMOUS)
typedef struct {
    void *mapping;
    size_t size;
    const char *text;
} readonly_text_t;

static readonly_text_t readonly_text(const char *text)
{
    readonly_text_t out;
    long page_size = sysconf(_SC_PAGESIZE);
    size_t length = strlen(text) + 1U;
    char *page;
    CHECK(page_size > 0 && length < (size_t)page_size);
    out.size = 3U * (size_t)page_size;
    out.mapping = mmap(NULL, out.size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(out.mapping != MAP_FAILED);
    page = (char *)out.mapping + page_size;
    CHECK(mprotect(page, (size_t)page_size, PROT_READ | PROT_WRITE) == 0);
    /* Terminate at the right guard page to catch reads beyond the input too. */
    out.text = page + page_size - length;
    memcpy((char *)out.text, text, length);
    CHECK(mprotect(page, (size_t)page_size, PROT_READ) == 0);
    return out;
}

static void readonly_patch_buffers(void)
{
    size_t mode, read_mode, index;
    stage = "read-only guarded selectors and patch payloads";
    for (mode = 0U; mode < 4U; mode++)
    for (read_mode = 0U; read_mode < 3U; read_mode++) {
        const char *selectors[2] = {"stmt[0].insert_cell[0][1]", "stmt[0].insert_cell[1][1]"};
        const char *payload = (mode & 1U) ? "readonly_literal" : "'readonly_raw'";
        readonly_text_t guarded[3];
        char *sql = source(32U, 0U, "t", "id", "s", NULL, NULL), *a = NULL, *b = NULL;
        sqlparser_handle_t *h = parse(sql, SQLPARSER_DIALECT_MYSQL);
        sqlparser_handle_t *ref = parse(sql, SQLPARSER_DIALECT_MYSQL);
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_dml_t dml;
        sqlparser_literal_view_t view;
        sqlparser_patch_t patches[2] = {{0}}, references[2] = {{0}};
        sqlparser_literal_value_t literal = {0}, reference_literal = {0};
        sqlparser_patch_list_t list = {patches, mode < 2U ? 2U : 1U};
        sqlparser_patch_list_t reference_list = {references, list.count};
        size_t before;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK); assert_fast(h);
        CHECK(sqlparser_handle_ensure_ast(ref, &error) == SQLPARSER_STATUS_OK);
        if (read_mode == 1U) CHECK(sqlparser_insert_cell_literal(h, 0U, 0U, 1U, &view, &error) == SQLPARSER_STATUS_OK);
        if (read_mode == 2U) {
            CHECK(sqlparser_export_view_json(h, 0, &a, &error) == SQLPARSER_STATUS_OK);
            sqlparser_string_free(a); a = NULL;
        }
        for (index = 0U; index < 2U; index++) guarded[index] = readonly_text(selectors[index]);
        guarded[2] = readonly_text(payload);
        literal.kind = reference_literal.kind = SQLPARSER_LITERAL_KIND_STRING;
        literal.string_value = guarded[2].text; reference_literal.string_value = payload;
        for (index = 0U; index < 2U; index++) {
            patches[index].op = references[index].op = SQLPARSER_PATCH_REPLACE;
            patches[index].selector = guarded[index].text; references[index].selector = selectors[index];
            if (mode & 1U) { patches[index].literal = &literal; references[index].literal = &reference_literal; }
            else { patches[index].sql = guarded[2].text; references[index].sql = payload; }
        }
        CHECK(sqlparser_apply_patch(ref, &reference_list, &error) == SQLPARSER_STATUS_OK);
        before = unpacks;
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        if (mode < 2U && read_mode == 0U) UNPACKS(before);
        CHECK(h->generation == 1UL);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        for (index = 0U; index < 3U; index++) {
            CHECK(!strcmp(guarded[index].text, index < 2U ? selectors[index] : payload));
            CHECK(munmap(guarded[index].mapping, guarded[index].size) == 0);
        }
        /* Every guarded caller page is gone before any output is read. */
        memset(patches, 0xa7, sizeof(patches)); memset(&literal, 0xa7, sizeof(literal));
        same_wire(h, ref);
        CHECK(sqlparser_deparse(h, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(ref, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a, b); same_graph(h, ref); same_json(h, ref);
        sqlparser_string_free(a); sqlparser_string_free(b);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref); free(sql); ++cases;
    }
}
#else
/* Semantic ownership cases above remain active on platforms without mmap. */
static void readonly_patch_buffers(void) { }
#endif

#ifdef SQLPARSER_WIRE_GRAPH_ALLOC_WRAPPERS
/* Exercise the new allocation boundaries directly. Certificate scratch OOM
 * may fall back successfully; graph allocation failures leave a retryable
 * handle. Every failed nonempty apply is terminal and releases all storage. */
static void wire_allocation_lifetimes(void)
{
    size_t fault, allocations = 0U;
    int operation;
    for (operation = 0; operation < 3; operation++) {
        for (fault = 0U; fault <= allocations; fault++) {
            char *sql = source(64U, 0U, "t", "id", "s", NULL, NULL), *output = NULL;
            sqlparser_handle_t *h = parse(sql, SQLPARSER_DIALECT_MYSQL);
            sqlparser_query_graph_view_t graph;
            sqlparser_graph_dml_cell_t borrowed;
            sqlparser_patch_t patches[64] = {{0}};
            sqlparser_literal_value_t literals[64] = {{0}};
            char selectors[64][64];
            sqlparser_patch_list_t list = {patches, 64U};
            sqlparser_status_t status;
            size_t row, failures;
            ++cases;
            stage = operation == 0 ? "wire graph OOM/retry" : "wire apply OOM/terminal";
            if (operation != 0) {
                CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
                assert_fast(h);
                borrowed = cell(&graph, 0U, 1U);
                for (row = 0U; row < 64U; row++) {
                    snprintf(selectors[row], sizeof(selectors[row]), "stmt[0].insert_cell[%zu][1]", row);
                    patches[row].op = SQLPARSER_PATCH_REPLACE;
                    patches[row].selector = selectors[row];
                    if (operation == 1) patches[row].sql = "'changed'";
                    else {
                        literals[row].kind = SQLPARSER_LITERAL_KIND_STRING;
                        literals[row].string_value = borrowed.literal.string_value;
                        patches[row].literal = &literals[row];
                    }
                }
            }
            allocation_count = allocation_failures = 0U;
            allocation_fail_at = fault; allocation_armed = 1;
            status = operation == 0 ? sqlparser_statement_query_graph(h, 0U, &graph, &error) :
                sqlparser_apply_patch(h, &list, &error);
            allocation_armed = 0; failures = allocation_failures;
            if (fault == 0U) {
                CHECK(status == SQLPARSER_STATUS_OK);
                allocations = allocation_count;
                CHECK(allocations != 0U);
            } else CHECK(failures == 1U);
            if (operation == 0) {
                CHECK(!h->failed);
                if (status != SQLPARSER_STATUS_OK) {
                    CHECK(h->query_graph == NULL);
                    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
                }
                borrowed = cell(&graph, 63U, 1U);
                CHECK(!strcmp(borrowed.literal.string_value, "old"));
            } else if (status != SQLPARSER_STATUS_OK) {
                CHECK(h->failed && h->query_graph == NULL && h->ast == NULL && h->parse_tree.data == NULL);
            } else {
                CHECK(!h->failed && h->generation == 1U);
                CHECK(sqlparser_deparse(h, &output, &error) == SQLPARSER_STATUS_OK);
                CHECK(output != NULL);
                if (operation == 2) CHECK(!strcmp(output, sql));
                else CHECK(strstr(output, "'changed'") != NULL && strstr(output, "'old'") == NULL);
            }
            sqlparser_string_free(output);
            sqlparser_handle_destroy(h);
            free(sql);
        }
        printf("wire allocation sweep operation=%d boundaries=%zu passed\n", operation, allocations);
        allocations = 0U;
    }
}


/* Bound each sweep to the observed successful operation's allocations. Inputs,
 * the generic oracle, failpoint counters and diagnostics are all prepared while
 * injection is disarmed. Failed nonempty applies are only inspected internally
 * and destroyed; they are never passed to another API or retried. */
static void generic_alias_allocation_lifetimes(void)
{
    size_t mode, read_mode;
    char label[128];
    for (mode = 0U; mode < GENERIC_ALIAS_MODES; mode++)
    for (read_mode = 0U; read_mode < GENERIC_ALIAS_READS; read_mode++) {
        generic_alias_fixture_t reference;
        sqlparser_patch_list_t reference_list;
        char *expected = NULL;
        size_t fault, boundaries = 0U;
        stage = "prepare generic alias OOM oracle";
        generic_alias_prepare(&reference, mode, read_mode, 1);
        reference_list = (sqlparser_patch_list_t){&reference.patch, 1U};
        CHECK(sqlparser_apply_patch(reference.handle, &reference_list, &error) == SQLPARSER_STATUS_OK);
        generic_alias_release_inputs(&reference);
        CHECK(sqlparser_deparse(reference.handle, &expected, &error) == SQLPARSER_STATUS_OK);
        for (fault = 0U; fault <= boundaries; fault++) {
            generic_alias_fixture_t actual;
            sqlparser_patch_list_t list;
            sqlparser_patch_t saved_patch;
            sqlparser_literal_value_t saved_literal;
            sqlparser_graph_dml_t dml;
            sqlparser_status_t status;
            size_t failures, count;
            char *output = NULL;
            snprintf(label, sizeof(label), "generic alias OOM mode=%zu read=%zu fault=%zu", mode, read_mode, fault);
            stage = label;
            generic_alias_prepare(&actual, mode, read_mode, 0);
            list = (sqlparser_patch_list_t){&actual.patch, 1U};
            saved_patch = actual.patch; saved_literal = actual.literal;
            allocation_count = allocation_failures = 0U;
            allocation_fail_at = fault; allocation_fail_at_second = 0U; allocation_armed = 1;
            status = sqlparser_apply_patch(actual.handle, &list, &error);
            allocation_armed = 0; failures = allocation_failures; count = allocation_count;
            if (fault == 0U) {
                CHECK(status == SQLPARSER_STATUS_OK);
                boundaries = count; CHECK(boundaries > 0U && boundaries <= 4096U);
            } else CHECK(failures == 1U);
            CHECK(!memcmp(&saved_patch, &actual.patch, sizeof(saved_patch)));
            CHECK(!memcmp(&saved_literal, &actual.literal, sizeof(saved_literal)));
            generic_alias_release_inputs(&actual);
            if (status == SQLPARSER_STATUS_OK) {
                CHECK(!actual.handle->failed && actual.handle->generation == 1UL);
                CHECK(sqlparser_query_graph_dml(&actual.graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
                same_wire(actual.handle, reference.handle);
                CHECK(sqlparser_deparse(actual.handle, &output, &error) == SQLPARSER_STATUS_OK);
                same_text(output, expected);
                same_graph(actual.handle, reference.handle); same_json(actual.handle, reference.handle);
            } else {
                CHECK(actual.handle->failed && actual.handle->generation != actual.graph.generation);
                CHECK(actual.handle->query_graph == NULL && actual.handle->ast == NULL);
                CHECK(actual.handle->parse_tree.data == NULL && actual.handle->parse_tree.len == 0U);
            }
            sqlparser_string_free(output); sqlparser_handle_destroy(actual.handle); ++cases;
        }
        printf("generic alias allocation sweep mode=%zu read=%zu boundaries=%zu passed\n", mode, read_mode, boundaries);
        sqlparser_string_free(expected); sqlparser_handle_destroy(reference.handle);
    }
}

typedef struct {
    size_t allocations, failures;
    int recovered;
} wire_recovery_result_t;

static wire_recovery_result_t wire_recovery_probe(int operation, size_t first, size_t second)
{
    char *sql = generic_alias_source(), *output = NULL, *expected = NULL;
    sqlparser_handle_t *h = parse(sql, SQLPARSER_DIALECT_MYSQL);
    sqlparser_handle_t *reference = parse(sql, SQLPARSER_DIALECT_MYSQL);
    sqlparser_query_graph_view_t graph;
    sqlparser_graph_dml_t dml;
    sqlparser_patch_t patches[2] = {{0}}, rp[2] = {{0}};
    sqlparser_patch_list_t list = {patches, 2U}, reference_list = {rp, 2U};
    sqlparser_status_t status;
    wire_recovery_result_t result;
    size_t before = unpacks;
    CHECK(sqlparser_handle_ensure_ast(reference, &error) == SQLPARSER_STATUS_OK);
    if (operation != 0) {
        sqlparser_graph_dml_cell_t selector0, selector1;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK); assert_fast(h);
        selector0 = cell(&graph, 7U, 1U); selector1 = cell(&graph, 3U, 1U);
        patches[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector=selector0.literal.string_value, .sql="'changed'"};
        patches[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector=selector1.literal.string_value, .sql="'retained'"};
        rp[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[0][1]", .sql="'changed'"};
        rp[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[1][1]", .sql="'retained'"};
        CHECK(sqlparser_apply_patch(reference, &reference_list, &error) == SQLPARSER_STATUS_OK);
    }
    before = unpacks;
    allocation_count = allocation_failures = 0U;
    allocation_fail_at = first; allocation_fail_at_second = second; allocation_armed = 1;
    status = operation == 0 ? sqlparser_statement_query_graph(h, 0U, &graph, &error) :
        sqlparser_apply_patch(h, &list, &error);
    allocation_armed = 0;
    result.allocations = allocation_count; result.failures = allocation_failures;
    allocation_fail_at_second = 0U;
    result.recovered = first != 0U && second == 0U && status == SQLPARSER_STATUS_OK;
    CHECK(result.failures == (first != 0U) + (size_t)(second != 0U));
    if (result.recovered) {
        /* A narrow certificate OOM can now recover through the separate
         * scalar certificate. Other recoveries still require generic unpack.
         * The exact injected failure count above remains mandatory. */
#ifdef SQLPARSER_WIRE_GRAPH_WRAPPERS
        CHECK(unpacks > before || (operation == 0 && h->ast == NULL &&
            sqlparser_query_graph_wire_scalar_insert(h) != NULL));
#else
        (void)before;
#endif
    }
    if (operation == 0) {
        CHECK(!h->failed && h->generation == 0UL);
        if (status != SQLPARSER_STATUS_OK) {
            CHECK(h->query_graph == NULL);
            CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        }
        same_graph(h, reference); same_wire(h, reference);
    } else if (status == SQLPARSER_STATUS_OK) {
        CHECK(!h->failed && h->generation == 1UL);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        same_wire(h, reference);
        CHECK(sqlparser_deparse(h, &output, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(reference, &expected, &error) == SQLPARSER_STATUS_OK);
        same_text(output, expected); same_graph(h, reference); same_json(h, reference);
    } else {
        CHECK(h->failed && h->generation != graph.generation && h->query_graph == NULL);
        CHECK(h->ast == NULL && h->parse_tree.data == NULL && h->parse_tree.len == 0U);
    }
    sqlparser_string_free(output); sqlparser_string_free(expected);
    sqlparser_handle_destroy(h); sqlparser_handle_destroy(reference); free(sql); ++cases;
    return result;
}

/* Scratch-certificate and wire-packer allocation misses deliberately fall
 * back. Discover those successful first failures, then fail every subsequent
 * allocation of that observed fallback path as well. This covers allocations
 * that a single-failure sweep of the normal fast path can never reach. */
static void recovered_fallback_allocation_lifetimes(void)
{
    int operation;
    char label[128];
    for (operation = 0; operation < 2; operation++) {
        size_t first, recoveries = 0U, secondary_cases = 0U;
        wire_recovery_result_t baseline;
        stage = "discover scratch-certificate/packer OOM fallback";
        baseline = wire_recovery_probe(operation, 0U, 0U);
        CHECK(baseline.allocations > 0U && baseline.allocations <= 4096U);
        for (first = 1U; first <= baseline.allocations; first++) {
            wire_recovery_result_t recovered;
            size_t second;
            snprintf(label, sizeof(label), "recovered wire OOM operation=%d first=%zu", operation, first);
            stage = label;
            recovered = wire_recovery_probe(operation, first, 0U);
            if (!recovered.recovered) continue;
            ++recoveries; CHECK(recovered.allocations > first && recovered.allocations <= 4096U);
            for (second = first + 1U; second <= recovered.allocations; second++) {
                snprintf(label, sizeof(label), "recovered wire OOM operation=%d first=%zu second=%zu", operation, first, second);
                stage = label;
                (void)wire_recovery_probe(operation, first, second); ++secondary_cases;
            }
        }
        CHECK(recoveries > 0U && secondary_cases > 0U);
        printf("wire recovered fallback sweep operation=%d recoveries=%zu second-failures=%zu passed\n",
            operation, recoveries, secondary_cases);
    }
}

static void json_allocation_lifetimes(void)
{
    size_t fault;
    stage = "JSON failure retains graph and borrowed strings";
    /* Sweep until a successful export encounters no injected failure. This
     * covers AST materialization, JSON construction and final serialization. */
    for (fault = 1U; ; ++fault) {
        char *sql = source(32U, 0U, "t", "id", "s", NULL, NULL), *json = NULL;
        sqlparser_handle_t *h = parse(sql, SQLPARSER_DIALECT_MYSQL);
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_dml_cell_t before, after;
        sqlparser_status_t status;
        size_t failures;
        ++cases;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK); assert_fast(h);
        before = cell(&graph, 31U, 1U);
        allocation_count = allocation_failures = 0U; allocation_fail_at = fault; allocation_armed = 1;
        status = sqlparser_export_view_json(h, 0, &json, &error);
        allocation_armed = 0; failures = allocation_failures;
        CHECK(!h->failed);
        if (status != SQLPARSER_STATUS_OK) CHECK(json == NULL && failures == 1U);
        after = cell(&graph, 31U, 1U);
        CHECK(after.literal.string_value == before.literal.string_value);
        CHECK(strcmp(before.literal.string_value, "old") == 0);
        sqlparser_string_free(json); json = NULL;
        CHECK(sqlparser_export_view_json(h, 0, &json, &error) == SQLPARSER_STATUS_OK);
        sqlparser_string_free(json); sqlparser_handle_destroy(h); free(sql);
        if (!failures) break;
    }
}
#endif

int main(void)
{
    native_repeated(); wire_size_boundaries(); read_and_json_lifetimes(); fallback_eligibility(); long_strings_and_repeated_fallback(); patch_fallback_parity(); generic_alias_lifetimes(); readonly_patch_buffers();
#ifdef SQLPARSER_WIRE_GRAPH_ALLOC_WRAPPERS
    wire_allocation_lifetimes();
    generic_alias_allocation_lifetimes();
    recovered_fallback_allocation_lifetimes();
    json_allocation_lifetimes();
    CHECK(native_context_depth == 0U);
#endif
    printf("wire INSERT graph: %zu cases; graph structures, native wire/locations, views, lifetimes, ordered fallback and limits passed\n", cases);
    return 0;
}
