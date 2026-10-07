/* Functional regression for the mixed-scalar wire graph and ORIGINAL string
 * selector API. The 5,000-row cases are correctness checks, never benchmarks.
 * Native oracle: the observed parser + non-NULL observer bypasses both scalar
 * recognition and direct wire output. Reference graphs force AST construction.
 * Optional SQLPARSER_SCALAR_WIRE_WRAPPERS: --wrap=pg_query__parse_result__unpack
 * Optional SQLPARSER_SCALAR_WIRE_ALLOC_WRAPPERS additionally requires
 * --wrap=malloc,calloc,realloc,free,pg_query_enter_memory_context,
 * pg_query_exit_memory_context. No public API or implementation is replaced. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "sqlparser_wire_insert_internal.h"
#include "src/pg_query_observer.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static sqlparser_error_t error;
static const char *stage = "initialization";
static size_t cases, unpacks, failure_index;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d case=%zu stage=%s fault=%zu %s: %s\n", \
    __FILE__, __LINE__, cases, stage, failure_index, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_SCALAR_WIRE_WRAPPERS
PgQuery__ParseResult *__real_pg_query__parse_result__unpack(ProtobufCAllocator *, size_t, const uint8_t *);
PgQuery__ParseResult *__wrap_pg_query__parse_result__unpack(ProtobufCAllocator *a, size_t n, const uint8_t *p)
{
    ++unpacks;
    return __real_pg_query__parse_result__unpack(a, n, p);
}
#define NO_UNPACKS(n) CHECK(unpacks == (n))
#else
#define NO_UNPACKS(n) ((void)(n))
#endif

#ifdef SQLPARSER_SCALAR_WIRE_ALLOC_WRAPPERS
/* The ledger covers malloc/calloc/realloc ownership acquired while the tested
 * operation runs. Frees remain tracked through caller cleanup and destruction.
 * Native PG-context OOM may terminate even unchanged code; omit only those
 * context-local allocations, as the existing wire-graph failure suite does. */
static int allocation_armed;
static unsigned native_depth;
static size_t allocation_calls, allocation_failures, live_allocations, ledger_end;
static void *ledger[16384];
struct MemoryContextData;
struct MemoryContextData *__real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(struct MemoryContextData *);
struct MemoryContextData *__wrap_pg_query_enter_memory_context(void)
{
    ++native_depth;
    return __real_pg_query_enter_memory_context();
}
void __wrap_pg_query_exit_memory_context(struct MemoryContextData *c)
{
    __real_pg_query_exit_memory_context(c);
    CHECK(native_depth != 0U); --native_depth;
}
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static size_t slot(void *p)
{
    size_t i;
    if (p && live_allocations) for (i = 0U; i < ledger_end; ++i) if (ledger[i] == p) return i;
    return COUNT(ledger);
}
static void track(void *p)
{
    size_t i;
    if (!p) return;
    for (i = 0U; i < ledger_end; ++i) if (!ledger[i]) break;
    CHECK(i < COUNT(ledger));
    ledger[i] = p; ++live_allocations;
    if (i == ledger_end) ++ledger_end;
}
static int reject_allocation(void)
{
    if (!allocation_armed || native_depth) return 0;
    ++allocation_calls;
    if (allocation_calls != failure_index) return 0;
    ++allocation_failures; return 1;
}
void *__wrap_malloc(size_t n)
{
    void *p;
    if (reject_allocation()) return NULL;
    p = __real_malloc(n);
    if (allocation_armed && !native_depth) track(p);
    return p;
}
void *__wrap_calloc(size_t n, size_t s)
{
    void *p;
    if (reject_allocation()) return NULL;
    p = __real_calloc(n, s);
    if (allocation_armed && !native_depth) track(p);
    return p;
}
void *__wrap_realloc(void *p, size_t n)
{
    size_t i = slot(p);
    void *q;
    if (reject_allocation()) return NULL;
    q = __real_realloc(p, n);
    if (q || !n) {
        if (i < COUNT(ledger)) { ledger[i] = q; if (!q) --live_allocations; }
        else if (allocation_armed && !native_depth) track(q);
    }
    return q;
}
void __wrap_free(void *p)
{
    size_t i = slot(p);
    if (i < COUNT(ledger)) { ledger[i] = NULL; --live_allocations; }
    __real_free(p);
}
#endif

typedef struct { char *data; size_t length, capacity; } buffer;
static void append(buffer *b, const char *format, ...)
{
    va_list args, again;
    int n;
    va_start(args, format); va_copy(again, args);
    n = vsnprintf(NULL, 0U, format, again); va_end(again);
    CHECK(n >= 0);
    if (b->length + (size_t)n + 1U > b->capacity) {
        size_t need = b->length + (size_t)n + 1U;
        b->capacity = need * 2U + 128U;
        b->data = realloc(b->data, b->capacity); CHECK(b->data != NULL);
    }
    CHECK(vsnprintf(b->data + b->length, b->capacity - b->length, format, args) == n);
    va_end(args); b->length += (size_t)n;
}
static char *copy(const char *s)
{
    char *p = malloc(strlen(s) + 1U); CHECK(p != NULL); strcpy(p, s); return p;
}
static void poison_free(char *s)
{
    if (s) { memset(s, 0xa7, strlen(s)); free(s); }
}
static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_default(&options); options.dialect = SQLPARSER_DIALECT_MYSQL;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    CHECK(h != NULL); return h;
}
static void native_observer(const PgQuery__ParseResult *tree, void *context)
{
    size_t *calls = context; CHECK(tree && tree->n_stmts == 1U); ++*calls;
}
static void native_wire(sqlparser_handle_t *h, const char *expected)
{
    size_t calls = 0U;
    PgQueryProtobufParseResult native = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        expected, PG_QUERY_PARSE_DEFAULT, native_observer, &calls);
    CHECK(native.error == NULL && calls == 1U && native.parse_tree.data != NULL);
    CHECK(h->parse_tree.len == native.parse_tree.len);
    CHECK(memcmp(h->parse_tree.data, native.parse_tree.data, h->parse_tree.len) == 0);
    pg_query_free_protobuf_parse_result(native);
}
static sqlparser_handle_t *reference(const char *sql, sqlparser_query_graph_view_t *graph)
{
    sqlparser_handle_t *h = parse(sql);
    native_wire(h, sql);
    CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(h, 0U, graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_wire_scalar_insert(h) == NULL);
    return h;
}
static void same_text(const char *a, const char *b)
{
    CHECK((a == NULL) == (b == NULL)); if (a) CHECK(strcmp(a, b) == 0);
}
static void same_literal(sqlparser_literal_view_t *a, sqlparser_literal_view_t *b)
{
    same_text(a->string_value, b->string_value); same_text(a->float_value, b->float_value);
    a->string_value = b->string_value = NULL; a->float_value = b->float_value = NULL;
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
/* Compare every public metadata byte after normalizing only owned pointers and
 * handle generations. No per-cell expression extraction on large INSERTs. */
static void graph_metadata(const sqlparser_query_graph_view_t *ag,
    const sqlparser_query_graph_view_t *bg, size_t rows, size_t columns)
{
    sqlparser_query_graph_view_t av = *ag, bv = *bg;
    sqlparser_graph_dml_t ad, bd;
    size_t i, ai, bi, ac, bc;
    av.handle = bv.handle = NULL; av.generation = bv.generation = 0UL;
    CHECK(memcmp(&av, &bv, sizeof(av)) == 0);
    for (i = 0U; i < ag->block_count; ++i) {
        sqlparser_graph_block_t a, b;
        CHECK(sqlparser_query_graph_block_at(ag, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_block_at(bg, i, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        same_span(ag, a.relations, bg, b.relations); same_span(ag, a.targets, bg, b.targets);
        same_span(ag, a.predicates, bg, b.predicates);
    }
    for (i = 0U; i < ag->relation_count; ++i) {
        sqlparser_graph_relation_t a, b;
        CHECK(sqlparser_query_graph_relation_at(ag, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_relation_at(bg, i, &b, &error) == SQLPARSER_STATUS_OK);
#define REL_TEXT(m) same_text(a.m, b.m); a.m = b.m = NULL
        REL_TEXT(database_name); REL_TEXT(schema_name); REL_TEXT(object_name);
        REL_TEXT(alias_name); REL_TEXT(link_name);
#undef REL_TEXT
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    CHECK(sqlparser_query_graph_dml_count(ag, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_count(bg, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == 1U && ac == bc);
    CHECK(sqlparser_query_graph_dml(ag, &ad, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(bg, &bd, &error) == SQLPARSER_STATUS_OK);
    CHECK(memcmp(&ad, &bd, sizeof(ad)) == 0);
    CHECK(ad.target_columns.count == columns && ad.rows.count == rows * columns);
    same_span(ag, ad.target_columns, bg, bd.target_columns); same_span(ag, ad.rows, bg, bd.rows);
    same_span(ag, ad.assignments, bg, bd.assignments); same_span(ag, ad.delete_targets, bg, bd.delete_targets);
    same_span(ag, ad.branches, bg, bd.branches);
    for (i = 0U; i < columns; ++i) {
        sqlparser_graph_dml_column_t a, b;
        CHECK(sqlparser_query_graph_span_index_at(ag, ad.target_columns, i, &ai, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_span_index_at(bg, bd.target_columns, i, &bi, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_column_at(ag, ai, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_column_at(bg, bi, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a.column_name, b.column_name); a.column_name = b.column_name = NULL;
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    for (i = 0U; i < rows * columns; ++i) {
        sqlparser_graph_dml_cell_t a, b;
        sqlparser_selector_t selector;
        char expected[80], *formatted = NULL;
        CHECK(sqlparser_query_graph_span_index_at(ag, ad.rows, i, &ai, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_span_index_at(bg, bd.rows, i, &bi, &error) == SQLPARSER_STATUS_OK);
        CHECK(ai == i && bi == i);
        CHECK(sqlparser_query_graph_dml_cell_at(ag, ai, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_cell_at(bg, bi, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(a.index == i && a.row_index == i / columns && a.column_ordinal == i % columns);
        CHECK(a.has_selector && a.selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL);
        CHECK(sqlparser_selector_format(&a.selector, &formatted, &error) == SQLPARSER_STATUS_OK);
        snprintf(expected, sizeof(expected), "stmt[0].insert_cell[%zu][%zu]", i / columns, i % columns);
        CHECK(strcmp(formatted, expected) == 0);
        CHECK(sqlparser_selector_parse(formatted, &selector, &error) == SQLPARSER_STATUS_OK);
        CHECK(memcmp(&selector, &a.selector, sizeof(selector)) == 0);
        sqlparser_string_free(formatted);
        same_literal(&a.literal, &b.literal); CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    CHECK(sqlparser_query_graph_dml_result_count(ag, 0U, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_result_count(bg, 0U, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc && ac == 0U);
    {
        int ah, bh;
        CHECK(sqlparser_query_graph_dml_parent(ag, 0U, &ai, &ah, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_parent(bg, 0U, &bi, &bh, &error) == SQLPARSER_STATUS_OK);
        CHECK(ai == bi && ah == bh);
    }
}
static void deep_parity(sqlparser_handle_t *h, sqlparser_handle_t *ref, size_t rows, size_t columns, int large)
{
    size_t r, c, count, expected_count, i;
    sqlparser_query_graph_view_t a, b;
    sqlparser_bind_occurrence_view_t ab, bb;
    char *as = NULL, *bs = NULL;
    CHECK(sqlparser_statement_query_graph(h, 0U, &a, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(ref, 0U, &b, &error) == SQLPARSER_STATUS_OK);
    graph_metadata(&a, &b, rows, columns);
    CHECK(sqlparser_handle_bind_occurrences(h, &ab, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_bind_occurrences(ref, &bb, &error) == SQLPARSER_STATUS_OK);
    CHECK(ab.count == bb.count && ab.count == 0U);
    CHECK(sqlparser_query_graph_expression_count(&a, &count, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_count(&b, &expected_count, &error) == SQLPARSER_STATUS_OK);
    CHECK(count == expected_count);
    for (i = 0U; i < count; ++i) {
        sqlparser_graph_expression_t x, y;
        if (large && i != 0U && i != count / 2U && i + 1U != count) continue;
        CHECK(sqlparser_query_graph_expression_at(&a, i, &x, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_expression_at(&b, i, &y, &error) == SQLPARSER_STATUS_OK);
        same_text(x.sql, y.sql); same_text(x.name, y.name); x.sql = y.sql = x.name = y.name = NULL;
        CHECK(memcmp(&x, &y, sizeof(x)) == 0);
    }
    for (r = 0U; r < rows; ++r) {
        if (large && r != 0U && r != rows / 2U && r + 1U != rows) continue;
        for (c = 0U; c < columns; ++c) {
            CHECK(sqlparser_insert_cell_sql(h, 0U, r, c, &as, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_insert_cell_sql(ref, 0U, r, c, &bs, &error) == SQLPARSER_STATUS_OK);
            same_text(as, bs); sqlparser_string_free(as); sqlparser_string_free(bs); as = bs = NULL;
        }
    }
    if (!large) {
        CHECK(sqlparser_export_view_json(h, 0U, &as, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_export_view_json(ref, 0U, &bs, &error) == SQLPARSER_STATUS_OK);
        same_text(as, bs); sqlparser_string_free(as); sqlparser_string_free(bs);
    }
}

static const char *functions_sql[] = {
    "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP", "LOCALTIME", "LOCALTIMESTAMP",
    "CURRENT_ROLE", "CURRENT_USER", "USER", "SESSION_USER", "CURRENT_CATALOG", "CURRENT_SCHEMA"
};
static char *scalar_source(size_t rows, size_t columns, const char *const *strings,
    size_t padding, int fallback)
{
    static const unsigned integers[] = {0U, 127U, 128U, 16383U, 16384U, 2147483647U};
    static const char *numbers[] = {"100.50", "1.25e+03", "2147483648", "0.000", "1E-12", "18446744073709551616", "0001.250", "1."};
    static const char *precision[] = {"CURRENT_TIME(0)", "CURRENT_TIME(6)", "CURRENT_TIMESTAMP(3)",
        "LOCALTIME(6)", "LOCALTIMESTAMP(0)", "CURRENT_TIMESTAMP /* precision */ (3)"};
    static const char *unsupported[] = {
        "coalesce(lower('MiXeD'),'Ω')", "'can''t'", "-2147483649", "NULL", "TRUE", ".5", "'line\nline'"
    };
    buffer b = {0};
    size_t row, col;
    CHECK(columns >= 5U && columns <= 13U);
    while (padding--) append(&b, " ");
    if (fallback == 8) append(&b, "/* source comment */ ");
    append(&b, fallback == 9 ? "INSERT INTO `warehouse`.`MixedValues`(" : "INSERT INTO warehouse.MixedValues(");
    for (col = 0U; col < columns; ++col) append(&b, "%sc%zu", col ? "," : "", col);
    append(&b, ") VALUES ");
    for (row = 0U; row < rows; ++row) {
        append(&b, "%s(", row ? ",\n" : "");
        for (col = 0U; col < columns; ++col) {
            if (col) append(&b, ",");
            if (col == 0U && fallback > 0 && fallback < 8) append(&b, "%s", unsupported[fallback - 1]);
            else if (col == 0U && fallback >= 10) append(&b, "%s", precision[fallback - 10]);
            else if (col == 0U) append(&b, "%u", integers[row % COUNT(integers)]);
            else if (col == 1U || col == 11U) append(&b, "'Ω中'");
            else if (col == 2U) append(&b, "'%s'", strings ? strings[row] : "original-张三");
            else if (col == 3U || col == 8U) append(&b, "%s", numbers[row % COUNT(numbers)]);
            else if (col == 4U || col == 7U || col == 9U || col == 10U)
                append(&b, "%s", functions_sql[(row + col - 4U) % COUNT(functions_sql)]);
            else if (col == 5U) append(&b, "'untouched'");
            else append(&b, "2147483647");
        }
        append(&b, ")");
    }
    append(&b, ";\n"); return b.data;
}
static char *primary_source(size_t rows, const char *const *strings)
{
    buffer b = {0}; size_t r;
    append(&b, "INSERT INTO TEST_LIB.TEACHER_STATISTICS(STAT_DATE,TEACHER_ID,TEACHER_NAME_ENCRYPT,PHONE_ENCRYPT,TOTAL_TEACHING,TOTAL_HOURS,CHECK_STATUS,CREATE_TIME,UPDATE_TIME) VALUES ");
    for (r = 0U; r < rows; ++r) append(&b,
        "%s('202505','T%zu','%s','13800138000',20,100.50,0,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)",
        r ? "," : "", 1001U + r, strings ? strings[r] : "张三李四");
    return b.data;
}
typedef struct {
    sqlparser_patch_t *patches;
    sqlparser_literal_value_t *literals;
    size_t count;
} owned_batch;
static owned_batch batch(const sqlparser_query_graph_view_t *graph, size_t rows, size_t columns,
    const char *const *strings, int typed, int reverse)
{
    owned_batch b = {0}; sqlparser_graph_dml_t dml; size_t i;
    b.count = rows; b.patches = calloc(rows, sizeof(*b.patches)); b.literals = calloc(rows, sizeof(*b.literals));
    CHECK(b.patches && b.literals);
    CHECK(sqlparser_query_graph_dml(graph, &dml, &error) == SQLPARSER_STATUS_OK);
    for (i = 0U; i < rows * columns; ++i) {
        size_t row = reverse ? rows - i / columns - 1U : i / columns;
        size_t column = i % columns, index;
        size_t patch_index = i / columns;
        sqlparser_graph_dml_cell_t cell;
        char *selector = NULL;
        CHECK(sqlparser_query_graph_span_index_at(graph, dml.rows, row * columns + column, &index, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_cell_at(graph, index, &cell, &error) == SQLPARSER_STATUS_OK);
        CHECK(cell.index == row * columns + column && cell.row_index == row && cell.column_ordinal == column);
        CHECK(cell.has_selector && cell.selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL);
        if (column != 2U) continue;
        CHECK(sqlparser_selector_format(&cell.selector, &selector, &error) == SQLPARSER_STATUS_OK);
        b.patches[patch_index].op = SQLPARSER_PATCH_REPLACE; b.patches[patch_index].selector = selector;
        if (typed) {
            b.literals[patch_index].kind = SQLPARSER_LITERAL_KIND_STRING;
            b.literals[patch_index].string_value = copy(strings[row]);
            b.patches[patch_index].literal = &b.literals[patch_index];
        } else {
            buffer raw = {0}; append(&raw, "'%s'", strings[row]); b.patches[patch_index].sql = raw.data;
        }
    }
    return b;
}
static void release_batch(owned_batch *b)
{
    size_t i;
    for (i = 0U; i < b->count; ++i) {
        poison_free((char *)b->patches[i].selector); poison_free((char *)b->patches[i].sql);
        poison_free((char *)b->literals[i].string_value);
    }
    memset(b->patches, 0xa7, b->count * sizeof(*b->patches));
    memset(b->literals, 0xa7, b->count * sizeof(*b->literals));
    free(b->patches); free(b->literals); memset(b, 0, sizeof(*b));
}
static void assert_fast(sqlparser_handle_t *h, size_t rows, size_t columns)
{
    const sqlparser_wire_scalar_insert_t *cert = sqlparser_query_graph_wire_scalar_insert(h);
    CHECK(cert && h->ast == NULL && cert->row_count == rows && cert->column_count == columns);
}
static void lifecycle(size_t rows, size_t columns, int typed, size_t padding, int primary)
{
    size_t round, r, rounds = primary ? 1U : 3U;
    char **values = calloc(rows, sizeof(*values)), **retained = calloc(rounds, sizeof(*retained));
    char **snapshots = calloc(rounds, sizeof(*snapshots));
    char *input = primary ? primary_source(rows, NULL) : scalar_source(rows, columns, NULL, padding, 0);
    sqlparser_handle_t *h = parse(input);
    CHECK(values && retained && snapshots); poison_free(input); ++cases;
    for (round = 0U; round < rounds; ++round) {
        sqlparser_query_graph_view_t graph, expected_graph;
        sqlparser_graph_dml_t dml;
        sqlparser_handle_t *ref;
        owned_batch b; sqlparser_patch_list_t list;
        char *expected, *reference_output = NULL;
        size_t before;
        unsigned long generation = h->generation;
        for (r = 0U; r < rows; ++r) {
            char text[128]; free(values[r]);
            if (primary) {
                snprintf(text, sizeof(text), "masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567", r + 1U);
                CHECK(strlen(text) == 49U);
            } else if ((r + round) % 3U == 0U) strcpy(text, "");
            else if ((r + round) % 3U == 1U) snprintf(text, sizeof(text), "grow-%04zu-abcdefghijklmnopqrstuvwxyz123456789", r);
            else snprintf(text, sizeof(text), "x%zu", r);
            values[r] = copy(text);
        }
        expected = primary ? primary_source(rows, (const char *const *)values) :
            scalar_source(rows, columns, (const char *const *)values, padding, 0);
        ref = reference(expected, &expected_graph);
        before = unpacks;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        if (rows >= 32U) { assert_fast(h, rows, columns); NO_UNPACKS(before); }
        b = batch(&graph, rows, columns, (const char *const *)values, typed, round % 2U == 0U);
        list.items = b.patches; list.count = b.count;
        memset(&error, 0x5a, sizeof(error));
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(error.code == SQLPARSER_STATUS_OK && error.message[0] == '\0');
        CHECK(h->generation == generation + 1UL);
        release_batch(&b); /* Caller selectors, values and objects gone first. */
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(sqlparser_deparse(h, &retained[round], &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(retained[round], expected) == 0);
        snapshots[round] = copy(expected);
        for (r = 0U; r <= round; ++r) same_text(retained[r], snapshots[r]);
        CHECK(strcmp(sqlparser_original_sql(h), expected) == 0);
        if (rows >= 32U) { NO_UNPACKS(before); CHECK(h->ast == NULL); }
        native_wire(h, expected);
        CHECK(sqlparser_deparse(ref, &reference_output, &error) == SQLPARSER_STATUS_OK);
        same_text(retained[round], reference_output); sqlparser_string_free(reference_output);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        graph_metadata(&graph, &expected_graph, rows, columns);
        if (rows >= 32U) assert_fast(h, rows, columns);
        if (round + 1U == rounds) deep_parity(h, ref, rows, columns, primary);
        free(expected); sqlparser_handle_destroy(ref);
    }
    sqlparser_handle_destroy(h);
    for (round = 0U; round < rounds; ++round) {
        same_text(retained[round], snapshots[round]);
        sqlparser_string_free(retained[round]); free(snapshots[round]);
    }
    for (r = 0U; r < rows; ++r) free(values[r]);
    free(values); free(retained); free(snapshots);
}

static void same_wire(sqlparser_handle_t *a, sqlparser_handle_t *b)
{
    CHECK(a->parse_tree.len == b->parse_tree.len);
    CHECK(memcmp(a->parse_tree.data, b->parse_tree.data, a->parse_tree.len) == 0);
}
static void fallback_sources(void)
{
    size_t mode, typed, row;
    stage = "unsupported source shapes retain generic behavior";
    for (mode = 1U; mode <= 15U; ++mode) for (typed = 0U; typed < 2U; ++typed) {
        enum { ROWS = 33, COLS = 9 };
        const char *values[ROWS];
        char *input = scalar_source(ROWS, COLS, NULL, 0U, (int)mode), *a = NULL, *b = NULL;
        sqlparser_query_graph_view_t graph, bg;
        sqlparser_handle_t *h = parse(input), *ref = parse(input);
        owned_batch patches;
        sqlparser_patch_list_t list;
        ++cases; poison_free(input);
        CHECK(sqlparser_handle_ensure_ast(ref, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_statement_query_graph(ref, 0U, &bg, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_wire_scalar_insert(h) == NULL && h->ast != NULL);
        graph_metadata(&graph, &bg, ROWS, COLS);
        for (row = 0U; row < ROWS; ++row) values[row] = row % 3U ? "" : "fallback-expanded-value";
        patches = batch(&graph, ROWS, COLS, values, (int)typed, 1);
        list = (sqlparser_patch_list_t){patches.patches, patches.count};
        CHECK(sqlparser_apply_patch(ref, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        release_batch(&patches); same_wire(h, ref);
        CHECK(sqlparser_deparse(h, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(ref, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a, b); sqlparser_string_free(a); sqlparser_string_free(b);
        deep_parity(h, ref, ROWS, COLS, 0);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
    }
}

/* Duplicate selectors have ordered last-wins semantics. Non-string fragments,
 * borrowed graph strings, escaped values and immediate repeated applies retain
 * the generic implementation as the independent patch oracle. */
static void ordered_and_fragment_fallbacks(void)
{
    static const char *raw[] = {"upper('mixed')", "'can''t'", "'Ω中'", "'line\nline'", "42", "CURRENT_TIMESTAMP(3)"};
    static const char *typed_values[] = {"can't", "Ω中", "line\nline", "back\\slash"};
    size_t mode, round;
    stage = "duplicate selectors and replacement fallback parity";
    for (mode = 0U; mode < 2U + COUNT(raw) + COUNT(typed_values); ++mode) {
        char *input = primary_source(33U, NULL), *a = NULL, *b = NULL;
        sqlparser_handle_t *h = parse(input), *ref = parse(input);
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_dml_t dml;
        sqlparser_patch_t p[3] = {{0}};
        sqlparser_literal_value_t literal = {0};
        sqlparser_patch_list_t list = {p, COUNT(p)};
        ++cases; poison_free(input);
        CHECK(sqlparser_handle_ensure_ast(ref, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        assert_fast(h, 33U, 9U);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector=copy("stmt[0].insert_cell[32][2]"), .sql=copy("'first-expanded'")};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector=copy("stmt[0].insert_cell[0][2]"), .sql=copy("''")};
        p[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector=copy(mode == 0U ? "stmt[0].insert_cell[32][2]" : "stmt[0].insert_cell[1][2]"),
            .sql=copy("'last-wins'")};
        if (mode == 1U) {
            sqlparser_graph_dml_cell_t borrowed;
            CHECK(sqlparser_query_graph_dml_cell_at(&graph, 2U, &borrowed, &error) == SQLPARSER_STATUS_OK);
            literal.kind = SQLPARSER_LITERAL_KIND_STRING; literal.string_value = borrowed.literal.string_value;
            poison_free((char *)p[2].sql); p[2].sql = NULL; p[2].literal = &literal;
        } else if (mode >= 2U && mode < 2U + COUNT(raw)) {
            poison_free((char *)p[2].sql); p[2].sql = copy(raw[mode - 2U]);
        } else if (mode >= 2U + COUNT(raw)) {
            literal.kind = SQLPARSER_LITERAL_KIND_STRING;
            literal.string_value = copy(typed_values[mode - 2U - COUNT(raw)]);
            poison_free((char *)p[2].sql); p[2].sql = NULL; p[2].literal = &literal;
        }
        CHECK(sqlparser_apply_patch(ref, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        for (round = 0U; round < COUNT(p); ++round) {
            poison_free((char *)p[round].selector); poison_free((char *)p[round].sql);
        }
        if (mode >= 2U + COUNT(raw)) poison_free((char *)literal.string_value);
        memset(p, 0xa7, sizeof(p)); memset(&literal, 0xa7, sizeof(literal));
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        /* No deparse or new graph before this next apply. */
        for (round = 0U; round < 2U; ++round) {
            sqlparser_patch_t immediate = {.op=SQLPARSER_PATCH_REPLACE,
                .selector=copy("stmt[0].insert_cell[2][2]"), .sql=copy(round ? "''" : "'immediate-grow'")};
            sqlparser_patch_list_t one = {&immediate, 1U};
            CHECK(sqlparser_apply_patch(ref, &one, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_apply_patch(h, &one, &error) == SQLPARSER_STATUS_OK);
            poison_free((char *)immediate.selector); poison_free((char *)immediate.sql);
        }
        same_wire(h, ref);
        CHECK(sqlparser_deparse(h, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(ref, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a, b); sqlparser_string_free(a); sqlparser_string_free(b);
        deep_parity(h, ref, 33U, 9U, 0);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
    }
}
static void invalid_fragments(void)
{
    static const char *bad[] = {"'unterminated", "'valid' trailing junk", "'a', 'b'", "'a'; SELECT 1", "CURRENT_TIMESTAMP(foo)", "1+)"};
    size_t i;
    stage = "invalid fragments fail safely with native error parity";
    for (i = 0U; i < COUNT(bad) + 2U; ++i) {
        char *input = primary_source(32U, NULL);
        sqlparser_handle_t *h = parse(input), *ref = parse(input);
        sqlparser_query_graph_view_t graph;
        sqlparser_patch_t p[] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql="'valid'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[31][2]", .sql="'valid'"}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        sqlparser_status_t a, b; sqlparser_error_t ae, be;
        ++cases; poison_free(input);
        CHECK(sqlparser_handle_ensure_ast(ref, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        assert_fast(h, 32U, 9U);
        if (i < COUNT(bad)) p[1].sql = bad[i];
        else p[1].selector = i == COUNT(bad) ? "invalid-selector" : "stmt[0].insert_cell[32][2]";
        b = sqlparser_apply_patch(ref, &list, &be); a = sqlparser_apply_patch(h, &list, &ae);
        CHECK(a != SQLPARSER_STATUS_OK && a == b && ae.code == be.code);
        CHECK(ae.cursor == be.cursor && ae.line == be.line && ae.column == be.column);
        CHECK(strcmp(ae.message, be.message) == 0);
        CHECK(h->failed && ref->failed);
        /* A failed apply is destroy-only: do not issue graph/deparse reads. */
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
    }
}

static void allocation_lifetimes(void)
{
#ifdef SQLPARSER_SCALAR_WIRE_ALLOC_WRAPPERS
    enum { ROWS = 32, COLS = 9 };
    const char *values[ROWS];
    char *source, *expected;
    size_t op, r;
    for (r = 0U; r < ROWS; ++r) values[r] = r % 3U == 0U ? "" : r % 3U == 1U ? "expanded-abcdefghijklmnopqrstuvwxyz" : "x";
    source = primary_source(ROWS, NULL); expected = primary_source(ROWS, values);
    for (op = 0U; op < 3U; ++op) {
        size_t boundaries = 0U;
        for (failure_index = 0U; failure_index <= boundaries; ++failure_index) {
            sqlparser_handle_t *h = parse(source);
            sqlparser_query_graph_view_t graph;
            sqlparser_status_t status;
            owned_batch b = {0}; sqlparser_patch_list_t list = {0};
            char *out = NULL;
            stage = op == 0U ? "scalar graph allocation ownership/retry" : "scalar apply allocation ownership/destroy-only";
            ++cases; CHECK(live_allocations == 0U && native_depth == 0U); ledger_end = 0U;
            if (op) {
                CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
                assert_fast(h, ROWS, COLS);
                b = batch(&graph, ROWS, COLS, values, op == 2U, 1);
                list = (sqlparser_patch_list_t){b.patches, b.count};
            }
            allocation_calls = allocation_failures = 0U; allocation_armed = 1;
            status = op ? sqlparser_apply_patch(h, &list, &error) : sqlparser_statement_query_graph(h, 0U, &graph, &error);
            allocation_armed = 0;
            if (op) release_batch(&b);
            if (failure_index == 0U) {
                CHECK(status == SQLPARSER_STATUS_OK && allocation_failures == 0U);
                boundaries = allocation_calls; CHECK(boundaries > 0U && boundaries < 4096U);
            } else CHECK(allocation_failures == 1U);
            if (!op) {
                CHECK(!h->failed);
                if (status != SQLPARSER_STATUS_OK) {
                    CHECK(h->query_graph == NULL);
                    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
                }
                CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK);
                CHECK(strcmp(out, source) == 0);
            } else if (status == SQLPARSER_STATUS_OK) {
                CHECK(h->generation == 1UL && !h->failed);
                CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK);
                CHECK(strcmp(out, expected) == 0); native_wire(h, expected);
            } else {
                CHECK(status == SQLPARSER_STATUS_NO_MEMORY && error.code == SQLPARSER_STATUS_NO_MEMORY);
                CHECK(h->failed && h->query_graph == NULL && h->ast == NULL);
                CHECK(h->parse_tree.data == NULL && h->parse_tree.len == 0U);
            }
            sqlparser_string_free(out); sqlparser_handle_destroy(h);
            CHECK(live_allocations == 0U && native_depth == 0U);
        }
        printf("scalar allocation ledger: operation=%zu boundaries=%zu passed\n", op, boundaries);
    }
    failure_index = 0U; free(source); free(expected);
#endif
}
int main(void)
{
    static const size_t widths[] = {5U, 8U, 9U, 10U, 13U};
    static const size_t heights[] = {31U, 32U, 33U, 65U};
    static const size_t paddings[] = {0U, 95U, 16350U};
    size_t w, h, typed, p;
    char label[128];
    for (typed = 0U; typed < 2U; ++typed) {
        stage = typed ? "5000 x 9 typed original-selector lifecycle" : "5000 x 9 raw original-selector lifecycle";
        lifecycle(5000U, 9U, (int)typed, 0U, 1);
    }
    for (w = 0U; w < COUNT(widths); ++w) for (h = 0U; h < COUNT(heights); ++h)
    for (typed = 0U; typed < 2U; ++typed) {
        snprintf(label, sizeof(label), "mixed scalar %zu rows x %zu columns typed=%zu", heights[h], widths[w], typed);
        stage = label; lifecycle(heights[h], widths[w], (int)typed, 0U, 0);
    }
    for (p = 1U; p < COUNT(paddings); ++p) {
        stage = "mixed-scalar source-location varint boundaries";
        lifecycle(33U, 9U, 0, paddings[p], 0);
    }
    fallback_sources(); ordered_and_fragment_fallbacks(); invalid_fragments(); allocation_lifetimes();
    pg_query_exit();
    printf("scalar wire pipeline: %zu functional cases; 45k-cell metadata, native packed wire, actual string selectors, grow/shrink/empty ownership, fallback and stale-view checks passed\n", cases);
    return 0;
}
