/* Functional provenance regression. Native eligibility is not interchangeable
 * with wire eligibility. A trusted descriptor must equal independent strict
 * certification, and provenance may only survive the exact generation-zero
 * immutable owned source/wire pair. This suite never times any operation.
 *
 * Allocation wrappers omit only PostgreSQL memory-context allocations, whose
 * fatal OOM contract is unchanged. Caller storage is poisoned/freed before
 * adjacent deparse, and every tracked allocation must be released.
 */
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
#ifdef SQLPARSER_NATIVE_PROVENANCE_WRAPPERS
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

#ifdef SQLPARSER_NATIVE_PROVENANCE_WRAPPERS
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
static sqlparser_handle_t *ast_reference(const char *sql, sqlparser_query_graph_view_t *graph)
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

static size_t strict_calls, trusted_calls, trusted_hits;
#ifdef SQLPARSER_NATIVE_PROVENANCE_WRAPPERS
sqlparser_wire_scalar_insert_t *__real_sqlparser_wire_scalar_insert_certify(const sqlparser_handle_t *);
sqlparser_wire_scalar_insert_t *__real_sqlparser_wire_scalar_insert_from_native(const sqlparser_handle_t *);
sqlparser_wire_scalar_insert_t *__wrap_sqlparser_wire_scalar_insert_certify(const sqlparser_handle_t *h)
{
    ++strict_calls;
    return __real_sqlparser_wire_scalar_insert_certify(h);
}
sqlparser_wire_scalar_insert_t *__wrap_sqlparser_wire_scalar_insert_from_native(const sqlparser_handle_t *h)
{
    sqlparser_wire_scalar_insert_t *d;
    ++trusted_calls;
    d = __real_sqlparser_wire_scalar_insert_from_native(h);
    if (d) ++trusted_hits;
    return d;
}
#define STRICT_COUNT(before, count) CHECK(strict_calls == (before) + (count))
#define TRUSTED_COUNT(before, count) CHECK(trusted_hits == (before) + (count))
#else
#define STRICT_COUNT(before, count) ((void)(before), (void)(count))
#define TRUSTED_COUNT(before, count) ((void)(before), (void)(count))
#endif

static char *fixture(size_t rows, size_t columns, const char *first,
    const char *const *strings, size_t padding)
{
    static const char *numbers[] = {"100.50", "1.25e+03", "2147483648", "0.000", "1E-12", "0001.250", "1."};
    static const char *functions[] = {"CURRENT_TIMESTAMP", "CURRENT_TIME", "CURRENT_DATE",
        "LOCALTIME", "LOCALTIMESTAMP", "CURRENT_ROLE", "CURRENT_USER", "USER",
        "SESSION_USER", "CURRENT_CATALOG", "CURRENT_SCHEMA"};
    buffer b = {0};
    size_t r, c;
    CHECK(columns >= 8U && columns <= 13U);
    /* The public native-certified parse route starts at 4,096 source bytes. */
    if (!padding) padding = 4096U;
    while (padding--) append(&b, " ");
    append(&b, "INSERT INTO warehouse.MixedValues(");
    for (c = 0U; c < columns; ++c) append(&b, "%sc%zu", c ? "," : "", c);
    append(&b, ") VALUES ");
    for (r = 0U; r < rows; ++r) {
        append(&b, "%s(", r ? ",\n" : "");
        for (c = 0U; c < columns; ++c) {
            if (c) append(&b, ",");
            if (c == 0U) append(&b, "%s", first ? first : "2147483647");
            else if (c == 2U) append(&b, "'%s'", strings ? strings[r] : "original-张三");
            else if (c == 1U || c == 5U || c == 11U) append(&b, "'Ω中'");
            else if (c == 3U || c == 8U) append(&b, "%s", numbers[r % COUNT(numbers)]);
            else append(&b, "%s", functions[(r + c) % COUNT(functions)]);
        }
        append(&b, ")");
    }
    append(&b, ";\n");
    return b.data;
}

static void forget_provenance(sqlparser_handle_t *h)
{
    free(h->native_scalar_provenance);
    h->native_scalar_provenance = NULL;
}

/* Compare all retained certificate metadata and every decoded cell against
 * the independent initial certifier, including source locations and spelling. */
static void certificate_equal(const sqlparser_wire_scalar_insert_t *a,
    const sqlparser_wire_scalar_insert_t *b)
{
    sqlparser_wire_scalar_insert_t x, y;
    sqlparser_wire_scalar_cell_t ac[13], bc[13];
    size_t r, c;
    CHECK(a && b && a->column_count <= COUNT(ac));
    x = *a; y = *b;
    x.names = y.names = NULL; x.row_offsets = y.row_offsets = NULL;
    CHECK(memcmp(&x, &y, sizeof(x)) == 0);
    CHECK(memcmp(a->names, b->names, (a->column_count + 3U) * sizeof(*a->names)) == 0);
    CHECK(memcmp(a->row_offsets, b->row_offsets, a->row_count * sizeof(*a->row_offsets)) == 0);
    for (r = 0U; r < a->row_count; ++r) {
        CHECK(sqlparser_wire_scalar_insert_certified_row(a, r, ac));
        CHECK(sqlparser_wire_scalar_insert_row(b, r, bc));
        CHECK(memcmp(ac, bc, a->column_count * sizeof(*ac)) == 0);
        for (c = 0U; c < a->column_count; ++c) {
            sqlparser_wire_scalar_cell_t direct;
            CHECK(sqlparser_wire_scalar_insert_certified_cell(a, r, c, &direct));
            CHECK(memcmp(&direct, &bc[c], sizeof(direct)) == 0);
        }
    }
}

static void initial_parity(size_t rows, size_t columns, size_t padding)
{
    char *sql = fixture(rows, columns, NULL, NULL, padding);
    sqlparser_handle_t *h = parse(sql), *strict = parse(sql), *native;
    sqlparser_wire_scalar_insert_t *a, *b;
    sqlparser_query_graph_view_t ag, bg, ng;
    size_t before, hits, unpacked;
    stage = "trusted descriptor and public graph equal strict initial certification";
    ++cases;
    CHECK(h->native_scalar_provenance && strict->native_scalar_provenance);
    native = ast_reference(sql, &ng);
    a = sqlparser_wire_scalar_insert_from_native(h);
    b = sqlparser_wire_scalar_insert_certify(h);
    certificate_equal(a, b);
    sqlparser_wire_scalar_insert_destroy(a); sqlparser_wire_scalar_insert_destroy(b);
    poison_free(sql); forget_provenance(strict);
    before = strict_calls; hits = trusted_hits; unpacked = unpacks;
    CHECK(sqlparser_statement_query_graph(h, 0U, &ag, &error) == SQLPARSER_STATUS_OK);
    STRICT_COUNT(before, 0U); TRUSTED_COUNT(hits, 1U); NO_UNPACKS(unpacked);
    CHECK(h->ast == NULL && sqlparser_query_graph_wire_scalar_insert(h));
    before = strict_calls; hits = trusted_hits;
    CHECK(sqlparser_statement_query_graph(strict, 0U, &bg, &error) == SQLPARSER_STATUS_OK);
    STRICT_COUNT(before, 1U); TRUSTED_COUNT(hits, 0U); NO_UNPACKS(unpacked);
    CHECK(strict->ast == NULL && sqlparser_query_graph_wire_scalar_insert(strict));
    graph_metadata(&ag, &bg, rows, columns); graph_metadata(&ag, &ng, rows, columns);
    before = strict_calls; hits = trusted_hits;
    CHECK(sqlparser_statement_query_graph(h, 0U, &ag, &error) == SQLPARSER_STATUS_OK);
    STRICT_COUNT(before, 0U); TRUSTED_COUNT(hits, 0U); /* Cached graph is immutable. */
    sqlparser_handle_destroy(native); sqlparser_handle_destroy(strict); sqlparser_handle_destroy(h);
}

static void provenance_guards(void)
{
    char *sql = fixture(33U, 10U, NULL, NULL, 0U);
    size_t mode;
    stage = "unproven handles and stale source/wire/generation never reuse native proof";
    for (mode = 0U; mode < 7U; ++mode) {
        sqlparser_handle_t *h = parse(sql), *clone = NULL;
        sqlparser_wire_scalar_insert_t *strict;
        sqlparser_query_graph_view_t graph;
        char *owned = NULL;
        size_t before, hits;
        ++cases; CHECK(h->native_scalar_provenance);
        if (mode == 0U) forget_provenance(h);
        if (mode == 1U) {
            /* Retain the old proof while replacing wire with equal bytes. */
            owned = malloc(h->parse_tree.len); CHECK(owned);
            memcpy(owned, h->parse_tree.data, h->parse_tree.len);
            free(h->parse_tree.data); h->parse_tree.data = owned; owned = NULL;
        }
        if (mode == 2U) {
            char *old = h->sql;
            owned = copy(old); h->sql = h->parser_sql = owned; owned = NULL; free(old);
        }
        if (mode == 3U) {
            /* Equal bytes are insufficient if the owned parser source differs. */
            h->parser_sql = copy(h->sql);
        }
        if (mode == 4U) { ++h->generation; h->surface_source_complete = 1; }
        if (mode == 5U) {
            CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
            CHECK(clone && clone->native_scalar_provenance == NULL);
            sqlparser_handle_destroy(h); h = clone;
        }
        if (mode == 6U) {
            size_t length = h->parse_tree.len;
            --h->parse_tree.len;
            CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
            CHECK(sqlparser_wire_scalar_insert_certify(h) == NULL);
            h->parse_tree.len = length;
            forget_provenance(h);
        }
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        strict = sqlparser_wire_scalar_insert_certify(h);
        if (mode != 5U) CHECK(strict); /* Clone dialect bookkeeping can require AST fallback. */
        sqlparser_wire_scalar_insert_destroy(strict);
        before = strict_calls; hits = trusted_hits;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        STRICT_COUNT(before, 1U); TRUSTED_COUNT(hits, 0U);
        if (mode != 5U) CHECK(h->ast == NULL && sqlparser_query_graph_wire_scalar_insert(h));
        sqlparser_handle_destroy(h);
    }
    {
        sqlparser_handle_t *h = parse(sql);
        CHECK(h->native_scalar_provenance);
        CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK);
        CHECK(h->native_scalar_provenance == NULL);
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        CHECK(sqlparser_handle_flush_ast(h, &error) == SQLPARSER_STATUS_OK);
        CHECK(h->native_scalar_provenance == NULL);
        sqlparser_handle_destroy(h); ++cases;
    }
    free(sql);
}

static void unproven_wire_mutations(void)
{
    char *sql = fixture(33U, 8U, NULL, NULL, 0U);
    sqlparser_handle_t *h = parse(sql);
    sqlparser_wire_scalar_insert_t *strict = sqlparser_wire_scalar_insert_certify(h);
    sqlparser_wire_scalar_cell_t cells[8];
    size_t offsets[3], i, length = h->parse_tree.len;
    CHECK(strict && sqlparser_wire_scalar_insert_row(strict, 0U, cells));
    offsets[0] = (size_t)(cells[2].text - h->parse_tree.data);
    offsets[1] = (size_t)(strict->names[2].text - h->parse_tree.data);
    offsets[2] = strict->row_offsets[0];
    sqlparser_wire_scalar_insert_destroy(strict); forget_provenance(h);
    stage = "manufactured mutated or truncated wire retains full strict validation";
    for (i = 0U; i < COUNT(offsets); ++i) {
        char saved = h->parse_tree.data[offsets[i]];
        h->parse_tree.data[offsets[i]] ^= 0x20;
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        CHECK(sqlparser_wire_scalar_insert_certify(h) == NULL);
        h->parse_tree.data[offsets[i]] = saved; ++cases;
    }
    for (i = 0U; i < length; ++i) {
        h->parse_tree.len = i;
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        CHECK(sqlparser_wire_scalar_insert_certify(h) == NULL);
    }
    h->parse_tree.len = length;
    strict = sqlparser_wire_scalar_insert_certify(h); CHECK(strict);
    sqlparser_wire_scalar_insert_destroy(strict); sqlparser_handle_destroy(h); free(sql); ++cases;
}

static void broader_native_fallbacks(void)
{
    static const char *tokens[] = {"-0", "-1", "-2147483648", "-2147483649", "- 1.25", ".5", "-.5",
        "CURRENT_TIME(0)", "CURRENT_TIME(6)", "CURRENT_TIMESTAMP(3)", "LOCALTIME(0)", "LOCALTIMESTAMP(6)"};
    size_t i;
    stage = "native grammar broader subset cannot masquerade as graph provenance";
    for (i = 0U; i < COUNT(tokens); ++i) {
        char *sql = fixture(33U, 13U, tokens[i], NULL, 0U), *a = NULL, *b = NULL;
        sqlparser_handle_t *h = parse(sql), *reference;
        sqlparser_query_graph_view_t ag, bg;
        size_t hits = trusted_hits;
        ++cases;
        CHECK(h->native_scalar_provenance == NULL);
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        CHECK(sqlparser_wire_scalar_insert_certify(h) == NULL);
        reference = ast_reference(sql, &bg); poison_free(sql);
        CHECK(sqlparser_statement_query_graph(h, 0U, &ag, &error) == SQLPARSER_STATUS_OK);
        TRUSTED_COUNT(hits, 0U);
        CHECK(h->ast && sqlparser_query_graph_wire_scalar_insert(h) == NULL);
        graph_metadata(&ag, &bg, 33U, 13U);
        CHECK(sqlparser_deparse(h, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(reference, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a, b); sqlparser_string_free(a); sqlparser_string_free(b);
        sqlparser_handle_destroy(reference); sqlparser_handle_destroy(h);
    }
}

static void mutation_lifecycle(size_t columns, int typed)
{
    enum { ROWS = 33, ROUNDS = 3 };
    char *input = fixture(ROWS, columns, NULL, NULL, 0U);
    sqlparser_handle_t *h = parse(input);
    char *values[ROWS] = {0}, *outputs[ROUNDS] = {0}, *snapshots[ROUNDS] = {0};
    size_t round, r;
    stage = "original graph selectors grow shrink empty and release caller storage before deparse";
    CHECK(h->native_scalar_provenance); poison_free(input); ++cases;
    for (round = 0U; round < ROUNDS; ++round) {
        sqlparser_query_graph_view_t graph, expected_graph;
        sqlparser_graph_dml_t dml;
        sqlparser_handle_t *expected;
        owned_batch patches;
        sqlparser_patch_list_t list;
        unsigned long generation = h->generation;
        size_t before, hits, unpacked;
        char *source;
        for (r = 0U; r < ROWS; ++r) {
            char text[128]; free(values[r]);
            if ((round + r) % 3U == 0U) strcpy(text, "");
            else if ((round + r) % 3U == 1U) snprintf(text, sizeof(text), "x%zu", r);
            else snprintf(text, sizeof(text), "expanded-%04zu-abcdefghijklmnopqrstuvwxyz0123456789", r);
            values[r] = copy(text);
        }
        source = fixture(ROWS, columns, NULL, (const char *const *)values, 0U);
        expected = parse(source); forget_provenance(expected);
        CHECK(sqlparser_statement_query_graph(expected, 0U, &expected_graph, &error) == SQLPARSER_STATUS_OK);
        before = strict_calls; hits = trusted_hits; unpacked = unpacks;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        if (!round) { STRICT_COUNT(before, 0U); TRUSTED_COUNT(hits, 1U); }
        CHECK(h->ast == NULL && sqlparser_query_graph_wire_scalar_insert(h));
        patches = batch(&graph, ROWS, columns, (const char *const *)values, typed, (int)(round % 2U));
        list = (sqlparser_patch_list_t){patches.patches, patches.count};
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(h->generation == generation + 1UL && h->native_scalar_provenance == NULL);
        release_batch(&patches);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(sqlparser_deparse(h, &outputs[round], &error) == SQLPARSER_STATUS_OK);
        same_text(outputs[round], source); snapshots[round] = copy(source);
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        CHECK(h->ast == NULL); NO_UNPACKS(unpacked);
        native_wire(h, source);
        before = strict_calls; hits = trusted_hits;
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        STRICT_COUNT(before, 1U); TRUSTED_COUNT(hits, 0U);
        graph_metadata(&graph, &expected_graph, ROWS, columns);
        for (r = 0U; r <= round; ++r) same_text(outputs[r], snapshots[r]);
        sqlparser_handle_destroy(expected); free(source);
    }
    sqlparser_handle_destroy(h);
    for (round = 0U; round < ROUNDS; ++round) {
        same_text(outputs[round], snapshots[round]); sqlparser_string_free(outputs[round]); free(snapshots[round]);
    }
    for (r = 0U; r < ROWS; ++r) free(values[r]);
}

static void allocation_boundaries(void)
{
#ifdef SQLPARSER_NATIVE_PROVENANCE_WRAPPERS
    enum { ROWS = 33, COLS = 10 };
    const char *values[ROWS];
    char *sql = fixture(ROWS, COLS, NULL, NULL, 4096U), *expected;
    size_t op, r;
    for (r = 0U; r < ROWS; ++r) values[r] = r % 3U == 0U ? "" : r % 3U == 1U ? "x" : "expanded-abcdefghijklmnopqrstuvwxyz";
    expected = fixture(ROWS, COLS, NULL, values, 4096U);
    for (op = 0U; op < 5U; ++op) {
        size_t boundaries = 0U;
        for (failure_index = 0U; failure_index <= boundaries; ++failure_index) {
            sqlparser_handle_t *h = op ? parse(sql) : NULL;
            sqlparser_wire_scalar_insert_t *cert = NULL;
            sqlparser_query_graph_view_t graph;
            sqlparser_parse_options_t options;
            sqlparser_status_t status = SQLPARSER_STATUS_OK;
            owned_batch patches = {0}; sqlparser_patch_list_t list = {0};
            char *out = NULL;
            stage = op == 0U ? "parse provenance allocation fallback and ownership" :
                op == 1U ? "trusted descriptor allocation miss remains retryable" :
                op == 2U ? "graph allocation fallback and retry" : "patch allocation terminal boundaries";
            ++cases; CHECK(live_allocations == 0U && native_depth == 0U); ledger_end = 0U;
            sqlparser_parse_options_default(&options); options.dialect = SQLPARSER_DIALECT_MYSQL;
            sqlparser_pg_query_prepare();
            if (op >= 3U) {
                CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
                patches = batch(&graph, ROWS, COLS, values, op == 4U, 1);
                list = (sqlparser_patch_list_t){patches.patches, patches.count};
            }
            allocation_calls = allocation_failures = 0U; allocation_armed = 1;
            if (op == 0U) status = sqlparser_parse_with_options(sql, &options, &h, &error);
            else if (op == 1U) cert = sqlparser_wire_scalar_insert_from_native(h);
            else if (op == 2U) status = sqlparser_statement_query_graph(h, 0U, &graph, &error);
            else status = sqlparser_apply_patch(h, &list, &error);
            allocation_armed = 0;
            if (op >= 3U) release_batch(&patches);
            if (!failure_index) {
                CHECK(status == SQLPARSER_STATUS_OK && allocation_failures == 0U);
                boundaries = allocation_calls; CHECK(boundaries > 0U && boundaries < 4096U);
                if (op == 1U) CHECK(cert);
            } else CHECK(allocation_failures == 1U);
            if (op == 0U && status != SQLPARSER_STATUS_OK) {
                CHECK(status == SQLPARSER_STATUS_NO_MEMORY && h == NULL);
            } else if (op == 1U) {
                if (failure_index) CHECK(cert == NULL);
                CHECK(!h->failed && h->native_scalar_provenance);
                sqlparser_wire_scalar_insert_destroy(cert); cert = sqlparser_wire_scalar_insert_from_native(h); CHECK(cert);
            } else if (op <= 2U) {
                CHECK(h && !h->failed);
                if (status != SQLPARSER_STATUS_OK) {
                    CHECK(h->query_graph == NULL);
                    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
                }
                CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK); same_text(out, sql);
            } else if (status == SQLPARSER_STATUS_OK) {
                CHECK(!h->failed && h->generation == 1UL && h->native_scalar_provenance == NULL);
                CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK); same_text(out, expected);
            } else {
                CHECK(status == SQLPARSER_STATUS_NO_MEMORY && error.code == SQLPARSER_STATUS_NO_MEMORY);
                CHECK(h->failed && h->query_graph == NULL && h->ast == NULL);
                CHECK(h->parse_tree.data == NULL && h->native_scalar_provenance == NULL);
            }
            sqlparser_wire_scalar_insert_destroy(cert); sqlparser_string_free(out); sqlparser_handle_destroy(h);
            CHECK(live_allocations == 0U && native_depth == 0U);
        }
        printf("native provenance allocation ledger: operation=%zu boundaries=%zu passed\n", op, boundaries);
    }
    failure_index = 0U; free(expected); free(sql);
#endif
}

int main(void)
{
    static const size_t widths[] = {8U, 10U, 13U};
    size_t width, typed;
    for (width = 0U; width < COUNT(widths); ++width) {
        initial_parity(32U, widths[width], 0U);
        initial_parity(65U, widths[width], 16350U);
        for (typed = 0U; typed < 2U; ++typed) mutation_lifecycle(widths[width], (int)typed);
    }
    provenance_guards(); unproven_wire_mutations(); broader_native_fallbacks(); allocation_boundaries();
    pg_query_exit();
    printf("native wire provenance: %zu functional cases; strict descriptor/graph parity, immutable ownership, stale proof rejection, broader-native fallback, and OOM passed\n", cases);
    return 0;
}
