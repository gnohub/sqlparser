/* SQLServer-family regression for the strict mixed-scalar wire graph and ORIGINAL string
 * selector API. The 5,000-row cases are correctness checks, never benchmarks.
 * Native oracle: the observed parser + non-NULL observer bypasses both scalar
 * recognition and direct wire output. Reference graphs force AST construction.
 * The dedicated Makefile target wraps unpack, destructive reparse, raw cell
 * fragment parsing, native certification and allocation ownership boundaries.
 * No public API or production implementation is replaced. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "sqlparser_wire_insert_internal.h"
#include "sqlparser_dialect_internal.h"
#include "sqlparser_dialect_national_literal_internal.h"
#include "sqlparser_ast_internal.h"
#include "src/pg_query_observer.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static sqlparser_error_t error;
static const char *stage = "initialization";
static size_t cases, unpacks, failure_index;
static sqlparser_dialect_t dialect = SQLPARSER_DIALECT_SQLSERVER;
static const struct { sqlparser_dialect_t dialect; const char *name; int mysql; } families[] = {
    { SQLPARSER_DIALECT_SQLSERVER, "sqlserver", 0 },
    { SQLPARSER_DIALECT_VASTBASE_SQLSERVER, "vastbase-sqlserver", 0 },
    { SQLPARSER_DIALECT_KINGBASE_SQLSERVER, "kingbase-sqlserver", 0 }
};
static size_t family_index, destructive_reparses, raw_fragment_parses;
static int force_legacy;
static size_t suppressed_capabilities, legacy_applies, legacy_ops_reads;
static void native_observer(const PgQuery__ParseResult *tree, void *context);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d case=%zu dialect=%s stage=%s fault=%zu %s: %s\n", \
    __FILE__, __LINE__, cases, family_index < COUNT(families) ? families[family_index].name : "all", stage, failure_index, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
/* Cross-object linker wrapping disables optimized graph AND commit routes on
 * the reference handle. This prevents comparing another handle through
 * the same fast path. The production parser remains deliberately unchanged. */
const sqlparser_dialect_ops_t *__real_sqlparser_dialect_get_ops(sqlparser_dialect_t);
static const sqlparser_dialect_ops_t *legacy_ops(sqlparser_dialect_t d)
{
    static sqlparser_dialect_ops_t copies[SQLPARSER_DIALECT_KINGBASE_SQLSERVER + 1];
    const sqlparser_dialect_ops_t *ops = __real_sqlparser_dialect_get_ops(d);
    CHECK(ops && d >= SQLPARSER_DIALECT_POSTGRESQL && d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER);
    copies[d] = *ops;
    copies[d].plain_scalar_native_validation = 0;
    copies[d].plain_ascii_string_fragments = 0;
    copies[d].state_is_plain_insert_strings = NULL;
    ++legacy_ops_reads; return &copies[d];
}
const sqlparser_dialect_ops_t *__wrap_sqlparser_dialect_get_ops(sqlparser_dialect_t d)
{
    return force_legacy ? legacy_ops(d) : __real_sqlparser_dialect_get_ops(d);
}
int __real_sqlparser_dialect_supports_plain_scalar_insert(const sqlparser_handle_t *);
int __wrap_sqlparser_dialect_supports_plain_scalar_insert(const sqlparser_handle_t *h)
{
    if (force_legacy) { ++suppressed_capabilities; return 0; }
    return __real_sqlparser_dialect_supports_plain_scalar_insert(h);
}
PgQuery__ParseResult *__real_pg_query__parse_result__unpack(ProtobufCAllocator *, size_t, const uint8_t *);
PgQuery__ParseResult *__wrap_pg_query__parse_result__unpack(ProtobufCAllocator *a, size_t n, const uint8_t *p)
{
    ++unpacks;
    return __real_pg_query__parse_result__unpack(a, n, p);
}
sqlparser_status_t __real_sqlparser_handle_reparse_destructive(sqlparser_handle_t *, char **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_handle_reparse_destructive(sqlparser_handle_t *h, char **sql, sqlparser_error_t *e)
{
    ++destructive_reparses;
    return __real_sqlparser_handle_reparse_destructive(h, sql, e);
}
sqlparser_status_t __real_sqlparser_parse_insert_cell_node_sql(const char *, const sqlparser_generated_source_t *, PgQuery__Node **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_parse_insert_cell_node_sql(const char *sql, const sqlparser_generated_source_t *source, PgQuery__Node **node, sqlparser_error_t *e)
{
    ++raw_fragment_parses;
    return __real_sqlparser_parse_insert_cell_node_sql(sql, source, node, e);
}
static void legacy_observer(const PgQuery__ParseResult *tree, void *context)
{
    (void)context;
    CHECK(tree != NULL);
}
PgQueryProtobufParseResult __real_sqlparser_parse_protobuf_preserving_identifier_spelling(const char *);
PgQueryProtobufParseResult __wrap_sqlparser_parse_protobuf_preserving_identifier_spelling(const char *sql)
{
    if (force_legacy) return pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        sql, PG_QUERY_PARSE_DEFAULT, legacy_observer, NULL);
    return __real_sqlparser_parse_protobuf_preserving_identifier_spelling(sql);
}
PgQueryProtobufParseResult __real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
    const char *, int, PgQueryProtobufObserver, void *, size_t *, int *, PgQueryNativeScalarInsertProof *);
PgQueryProtobufParseResult __wrap_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
    const char *sql, int options, PgQueryProtobufObserver observer, void *context,
    size_t *statements, int *certified, PgQueryNativeScalarInsertProof *proof)
{
    if (force_legacy) {
        *statements = 0U; *certified = 0;
        if (proof) memset(proof, 0, sizeof(*proof));
        /* A non-NULL observer disables both the scalar recognizer and direct
         * writer. Preserve the real dialect validation callback when present. */
        return pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(sql, options,
            observer ? observer : legacy_observer, observer ? context : NULL);
    }
    return __real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native(
        sql, options, observer, context, statements, certified, proof);
}
#define NO_UNPACKS(n) CHECK(unpacks == (n))
#define NO_REPARSE(n) CHECK(destructive_reparses == (n))
#define NO_FRAGMENT(n) CHECK(raw_fragment_parses == (n))
#else
#define NO_UNPACKS(n) ((void)(n))
#define NO_REPARSE(n) ((void)(n))
#define NO_FRAGMENT(n) ((void)(n))
#endif

#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
/* The ledger covers malloc/calloc/realloc ownership acquired while the tested
 * operation runs. Frees remain tracked through caller cleanup and destruction.
 * Native PG-context OOM may terminate even unchanged code; omit only those
 * context-local allocations, as the existing wire-graph failure suite does. */
static int allocation_armed;
static unsigned native_depth;
static size_t allocation_calls, allocation_failures, live_allocations, ledger_end;
static void *ledger[16384];
static int record_calloc;
static size_t recorded_count;
static struct { void *pointer; size_t bytes; } recorded_calloc[512];
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
    if (record_calloc && !native_depth && p) {
        CHECK(recorded_count < COUNT(recorded_calloc));
        recorded_calloc[recorded_count].pointer = p;
        recorded_calloc[recorded_count++].bytes = n * s;
    }
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
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    CHECK(h != NULL); return h;
}
static sqlparser_status_t legacy_apply(sqlparser_handle_t *h, const sqlparser_patch_list_t *list, sqlparser_error_t *e)
{
    sqlparser_status_t status;
    force_legacy = 1;
#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
    h->dialect_ops = legacy_ops(h->dialect);
#endif
    status = sqlparser_handle_ensure_ast(h, e);
    if (status == SQLPARSER_STATUS_OK) {
        CHECK(h->ast != NULL);
        ++legacy_applies;
        status = sqlparser_apply_patch(h, list, e);
    }
    force_legacy = 0;
    return status;
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
    sqlparser_handle_t *h;
    force_legacy = 1; h = parse(sql); force_legacy = 0;
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
    append(&b, fallback == 9 ? (families[family_index].mysql ? "INSERT INTO `warehouse`.`MixedValues`(" : "INSERT INTO \"warehouse\".\"MixedValues\"(") : "INSERT INTO warehouse.MixedValues(");
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
static const char *fixture_path;
static const char *golden_directory;
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long length;
    char *data;
    CHECK(f != NULL && fseek(f, 0L, SEEK_END) == 0);
    length = ftell(f); CHECK(length >= 0 && fseek(f, 0L, SEEK_SET) == 0);
    data = malloc((size_t)length + 1U); CHECK(data);
    CHECK(fread(data, 1U, (size_t)length, f) == (size_t)length && fclose(f) == 0);
    data[length] = '\0'; return data;
}
static char *actual_fixture(const char *const *strings)
{
    const char *needle = "'张三李四'";
    char *input = read_file(fixture_path);
    const char *cursor = input, *match;
    size_t rows = 0U;
    buffer out = {0};
    while ((match = strstr(cursor, needle)) != NULL) {
        CHECK(rows < 5000U);
        append(&out, "%.*s'%s'", (int)(match - cursor), cursor, strings ? strings[rows] : "张三李四");
        ++rows; cursor = match + strlen(needle);
    }
    CHECK(rows == 5000U); append(&out, "%s", cursor); free(input); return out.data;
}
static char *primary_source(size_t rows, const char *const *strings)
{
    buffer b = {0}; size_t r;
    if (rows == 5000U && fixture_path) return actual_fixture(strings);
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
static void certificate_equal(const sqlparser_wire_scalar_insert_t *, const sqlparser_wire_scalar_insert_t *);
static void assert_fast(sqlparser_handle_t *h, size_t rows, size_t columns)
{
    const sqlparser_wire_scalar_insert_t *cert = sqlparser_query_graph_wire_scalar_insert(h);
    sqlparser_wire_scalar_insert_t *strict;
    CHECK(cert && h->ast == NULL && cert->row_count == rows && cert->column_count == columns);
    strict = sqlparser_wire_scalar_insert_certify(h); CHECK(strict);
    certificate_equal(cert, strict); sqlparser_wire_scalar_insert_destroy(strict);
}
static void lifecycle(size_t rows, size_t columns, int typed, size_t padding, int primary)
{
    size_t round, r, rounds = primary ? 1U : 3U;
    char **values = calloc(rows, sizeof(*values)), **retained = calloc(rounds, sizeof(*retained));
    char **snapshots = calloc(rounds, sizeof(*snapshots));
    char *input = primary ? primary_source(rows, NULL) : scalar_source(rows, columns, NULL, padding, 0);
    sqlparser_handle_t *h = parse(input);
    CHECK(h->native_scalar_provenance == NULL); /* native parser restriction is retained */
    {
        sqlparser_query_graph_view_t initial_graph, legacy_graph;
        sqlparser_handle_t *legacy = reference(input, &legacy_graph);
        CHECK(sqlparser_statement_query_graph(h, 0U, &initial_graph, &error) == SQLPARSER_STATUS_OK);
        graph_metadata(&initial_graph, &legacy_graph, rows, columns);
        CHECK(sqlparser_statement_query_graph(h, 0U, &initial_graph, &error) == SQLPARSER_STATUS_OK);
        if (rows >= 32U) assert_fast(h, rows, columns);
        sqlparser_handle_destroy(legacy);
    }
    CHECK(values && retained && snapshots); poison_free(input); ++cases;
    for (round = 0U; round < rounds; ++round) {
        sqlparser_query_graph_view_t graph, expected_graph;
        sqlparser_graph_dml_t dml;
        sqlparser_handle_t *ref;
        owned_batch b; sqlparser_patch_list_t list;
        char *expected, *reference_output = NULL;
        size_t before, reparses, fragments;
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
        if (primary && rows == 5000U && golden_directory) {
            char path[1024], *golden;
            CHECK(snprintf(path, sizeof(path), "%s/%s_%s.sql", golden_directory,
                families[family_index].name, typed ? "typed" : "raw") < (int)sizeof(path));
            golden = read_file(path); same_text(expected, golden); free(golden);
        }
        ref = reference(expected, &expected_graph);
        before = unpacks; reparses = destructive_reparses; fragments = raw_fragment_parses;
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
        if (rows >= 32U) { NO_UNPACKS(before); NO_REPARSE(reparses); NO_FRAGMENT(fragments); CHECK(h->ast == NULL); }
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
        CHECK(legacy_apply(ref, &list, &error) == SQLPARSER_STATUS_OK);
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
    static const char *raw[] = {"upper('mixed')", "'can''t'", "'Ω中'", "'line\nline'", "42", "CURRENT_TIMESTAMP(3)",
        "N'national-中'", " 'padded' ", "'back\\slash'", "'tab\ttext'", "'carriage\rtext'", "'x' /* trailing comment */"};
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
        CHECK(legacy_apply(ref, &list, &error) == SQLPARSER_STATUS_OK);
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
            CHECK(legacy_apply(ref, &one, &error) == SQLPARSER_STATUS_OK);
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
        b = legacy_apply(ref, &list, &be); a = sqlparser_apply_patch(h, &list, &ae);
        CHECK(a != SQLPARSER_STATUS_OK && a == b && ae.code == be.code);
        CHECK(ae.cursor == be.cursor && ae.line == be.line && ae.column == be.column);
        CHECK(strcmp(ae.message, be.message) == 0);
        CHECK(h->failed && ref->failed);
        /* A failed apply is destroy-only: do not issue graph/deparse reads. */
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
    }
}

static void allocation_boundaries(void)
{
#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
    enum { ROWS = 65, COLS = 10 };
    const char *values[ROWS];
    char *sql = scalar_source(ROWS, COLS, NULL, 0U, 0), *expected;
    size_t op, r;
    for (r = 0U; r < ROWS; ++r) values[r] = r % 3U == 0U ? "" : r % 3U == 1U ? "x" : "expanded-abcdefghijklmnopqrstuvwxyz";
    expected = scalar_source(ROWS, COLS, values, 0U, 0);
    for (op = 1U; op < 5U; ++op) {
        size_t boundaries = 0U;
        for (failure_index = 0U; failure_index <= boundaries; ++failure_index) {
            sqlparser_handle_t *h = parse(sql);
            sqlparser_wire_scalar_insert_t *cert = NULL;
            sqlparser_query_graph_view_t graph;
            sqlparser_status_t status = SQLPARSER_STATUS_OK;
            owned_batch patches = {0}; sqlparser_patch_list_t list = {0};
            char *out = NULL;
            stage = op == 1U ? "strict descriptor allocation miss remains retryable" :
                op == 2U ? "graph allocation fallback and retry" : "patch allocation terminal boundaries";
            ++cases; CHECK(live_allocations == 0U && native_depth == 0U); ledger_end = 0U;
            sqlparser_pg_query_prepare();
            if (op >= 3U) {
                CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
                patches = batch(&graph, ROWS, COLS, values, op == 4U, 1);
                list = (sqlparser_patch_list_t){patches.patches, patches.count};
            }
            allocation_calls = allocation_failures = 0U; allocation_armed = 1;
            if (op == 1U) cert = sqlparser_wire_scalar_insert_certify(h);
            else if (op == 2U) status = sqlparser_statement_query_graph(h, 0U, &graph, &error);
            else status = sqlparser_apply_patch(h, &list, &error);
            allocation_armed = 0;
            if (op >= 3U) release_batch(&patches);
            if (!failure_index) {
                CHECK(status == SQLPARSER_STATUS_OK && allocation_failures == 0U);
                boundaries = allocation_calls; CHECK(boundaries > 0U && boundaries < 4096U);
                if (op == 1U) CHECK(cert);
            } else CHECK(allocation_failures == 1U);
            if (op == 1U) {
                if (failure_index) CHECK(cert == NULL);
                CHECK(!h->failed && h->native_scalar_provenance == NULL);
                sqlparser_wire_scalar_insert_destroy(cert); cert = sqlparser_wire_scalar_insert_certify(h); CHECK(cert);
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
        printf("SQLServer wire allocation ledger: dialect=%s operation=%zu boundaries=%zu passed\n", families[family_index].name, op, boundaries);
    }
    failure_index = 0U; free(expected); free(sql);
#endif
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

static void provenance_guards(void)
{
    char *sql = scalar_source(65U, 13U, NULL, 0U, 0);
    size_t mode;
    stage = "strict owned source, wire, generation and AST lifetime guards";
    for (mode = 0U; mode < 7U; ++mode) {
        sqlparser_handle_t *h = parse(sql), *clone = NULL;
        sqlparser_wire_scalar_insert_t *strict;
        sqlparser_query_graph_view_t graph;
        ++cases; CHECK(h->native_scalar_provenance == NULL);
        if (mode == 1U) {
            char *owned = malloc(h->parse_tree.len); CHECK(owned);
            memcpy(owned, h->parse_tree.data, h->parse_tree.len);
            free(h->parse_tree.data); h->parse_tree.data = owned;
        }
        if (mode == 2U) {
            char *old = h->sql, *old_parser = h->parser_sql;
            h->sql = copy(old); h->parser_sql = h->sql;
            if (old_parser != old) free(old_parser);
            free(old);
        }
        if (mode == 3U) {
            char *old_parser = h->parser_sql;
            h->parser_sql = copy(h->sql);
            if (old_parser != h->sql) free(old_parser);
        }
        if (mode == 4U) {
            ++h->generation;
            CHECK(sqlparser_wire_scalar_insert_certify(h) == NULL);
            h->surface_source_complete = 1;
        }
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
        }
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        strict = sqlparser_wire_scalar_insert_certify(h);
        if (mode != 5U) CHECK(strict);
        sqlparser_wire_scalar_insert_destroy(strict);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        if (mode != 5U) assert_fast(h, 65U, 13U);
        sqlparser_handle_destroy(h);
    }
    {
        sqlparser_handle_t *h = parse(sql);
        CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_wire_scalar_insert_certify(h) == NULL);
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        CHECK(sqlparser_handle_flush_ast(h, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        sqlparser_handle_destroy(h); ++cases;
    }
    free(sql);
}

static void unproven_wire_mutations(void)
{
    char *sql = scalar_source(33U, 8U, NULL, 4096U, 0);
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

/* Layout mirrors are accessed exclusively through offsetof/sizeof/memcpy.
 * Allocation-size equality catches added fields; each field is independently
 * poisoned and restored before any destructor or generic fallback may run. */
typedef struct {
	size_t param_count;
	size_t unicode_count;
	size_t literal_count;
	size_t top_count;
	size_t select_count;
	size_t bare_bit_count;
	size_t bit_word_count;
	size_t table_hint_count;
	size_t table_source_count;
	size_t query_hint_count;
	size_t json_suffix_count;
	size_t save_restore_count;
	size_t rename_object_count;
	size_t drop_role_like_count;
	size_t cast_restore_count;
	size_t cast_count;
	size_t odbc_fn_count;
	size_t output_dml_count;
	int active;
} sqlserver_checkpoint_layout;
typedef struct {
	void *params;
	size_t param_count;
	size_t param_capacity;
	void *unicode_restores;
	size_t unicode_count;
	size_t unicode_capacity;
	size_t literal_count;
	void *top_restores;
	size_t top_count;
	size_t top_capacity;
	size_t select_count;
	size_t bit_word_count;
	void *bare_bit_restores;
	size_t bare_bit_count;
	size_t bare_bit_capacity;
	void *table_hints;
	size_t table_hint_count;
	size_t table_hint_capacity;
	size_t table_source_count;
	char **query_hints;
	size_t query_hint_count;
	size_t query_hint_capacity;
	char **json_suffixes;
	size_t *json_suffix_ordinals;
	size_t json_suffix_count;
	size_t json_suffix_capacity;
	size_t json_suffix_ordinal_count;
	size_t json_suffix_ordinal_capacity;
	void *cast_restores;
	size_t cast_restore_count;
	size_t cast_restore_capacity;
	size_t cast_count;
	void *odbc_fn_restores;
	size_t odbc_fn_count;
	size_t odbc_fn_capacity;
	void *save_restores;
	size_t save_restore_count;
	size_t save_restore_capacity;
	size_t rename_object_count;
	size_t drop_role_like_count;
	size_t *drop_user_ordinals;
	size_t drop_user_count;
	size_t drop_user_capacity;
	void *output_state;
	void *control;
	sqlserver_checkpoint_layout fragment;
	int owners_bound;
} sqlserver_state_layout;
typedef struct { const char *name; size_t offset, size; int pointer; } state_field;
#define FIELD(type, name) {#name, offsetof(type, name), sizeof(((type *)0)->name), 0}
#define POINTER(type, name) {#name, offsetof(type, name), sizeof(((type *)0)->name), 1}
static const state_field sqlserver_fields[] = {
    POINTER(sqlserver_state_layout, params),
    FIELD(sqlserver_state_layout, param_count),
    FIELD(sqlserver_state_layout, param_capacity),
    POINTER(sqlserver_state_layout, unicode_restores),
    FIELD(sqlserver_state_layout, unicode_count),
    FIELD(sqlserver_state_layout, unicode_capacity),
    FIELD(sqlserver_state_layout, literal_count),
    POINTER(sqlserver_state_layout, top_restores),
    FIELD(sqlserver_state_layout, top_count),
    FIELD(sqlserver_state_layout, top_capacity),
    FIELD(sqlserver_state_layout, select_count),
    FIELD(sqlserver_state_layout, bit_word_count),
    POINTER(sqlserver_state_layout, bare_bit_restores),
    FIELD(sqlserver_state_layout, bare_bit_count),
    FIELD(sqlserver_state_layout, bare_bit_capacity),
    POINTER(sqlserver_state_layout, table_hints),
    FIELD(sqlserver_state_layout, table_hint_count),
    FIELD(sqlserver_state_layout, table_hint_capacity),
    FIELD(sqlserver_state_layout, table_source_count),
    POINTER(sqlserver_state_layout, query_hints),
    FIELD(sqlserver_state_layout, query_hint_count),
    FIELD(sqlserver_state_layout, query_hint_capacity),
    POINTER(sqlserver_state_layout, json_suffixes),
    POINTER(sqlserver_state_layout, json_suffix_ordinals),
    FIELD(sqlserver_state_layout, json_suffix_count),
    FIELD(sqlserver_state_layout, json_suffix_capacity),
    FIELD(sqlserver_state_layout, json_suffix_ordinal_count),
    FIELD(sqlserver_state_layout, json_suffix_ordinal_capacity),
    POINTER(sqlserver_state_layout, cast_restores),
    FIELD(sqlserver_state_layout, cast_restore_count),
    FIELD(sqlserver_state_layout, cast_restore_capacity),
    FIELD(sqlserver_state_layout, cast_count),
    POINTER(sqlserver_state_layout, odbc_fn_restores),
    FIELD(sqlserver_state_layout, odbc_fn_count),
    FIELD(sqlserver_state_layout, odbc_fn_capacity),
    POINTER(sqlserver_state_layout, save_restores),
    FIELD(sqlserver_state_layout, save_restore_count),
    FIELD(sqlserver_state_layout, save_restore_capacity),
    FIELD(sqlserver_state_layout, rename_object_count),
    FIELD(sqlserver_state_layout, drop_role_like_count),
    POINTER(sqlserver_state_layout, drop_user_ordinals),
    FIELD(sqlserver_state_layout, drop_user_count),
    FIELD(sqlserver_state_layout, drop_user_capacity),
    POINTER(sqlserver_state_layout, output_state),
    POINTER(sqlserver_state_layout, control),
    FIELD(sqlserver_state_layout, fragment.param_count),
    FIELD(sqlserver_state_layout, fragment.unicode_count),
    FIELD(sqlserver_state_layout, fragment.literal_count),
    FIELD(sqlserver_state_layout, fragment.top_count),
    FIELD(sqlserver_state_layout, fragment.select_count),
    FIELD(sqlserver_state_layout, fragment.bare_bit_count),
    FIELD(sqlserver_state_layout, fragment.bit_word_count),
    FIELD(sqlserver_state_layout, fragment.table_hint_count),
    FIELD(sqlserver_state_layout, fragment.table_source_count),
    FIELD(sqlserver_state_layout, fragment.query_hint_count),
    FIELD(sqlserver_state_layout, fragment.json_suffix_count),
    FIELD(sqlserver_state_layout, fragment.save_restore_count),
    FIELD(sqlserver_state_layout, fragment.rename_object_count),
    FIELD(sqlserver_state_layout, fragment.drop_role_like_count),
    FIELD(sqlserver_state_layout, fragment.cast_restore_count),
    FIELD(sqlserver_state_layout, fragment.cast_count),
    FIELD(sqlserver_state_layout, fragment.odbc_fn_count),
    FIELD(sqlserver_state_layout, fragment.output_dml_count),
    FIELD(sqlserver_state_layout, fragment.active),
    FIELD(sqlserver_state_layout, owners_bound),
};
#undef POINTER
#undef FIELD

static void state_predicate_guards(void)
{
    char *sql = scalar_source(65U, 13U, NULL, 0U, 0);
    sqlparser_handle_t *h;
    sqlparser_wire_scalar_insert_t *strict;
    size_t count = COUNT(sqlserver_fields), i, strings;
#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
    record_calloc = 1; recorded_count = 0U;
#endif
    h = parse(sql);
#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
    record_calloc = 0;
    {
        size_t index;
        for (index = 0U; index < recorded_count; ++index) if (recorded_calloc[index].pointer == h->dialect_state) break;
        CHECK(index < recorded_count && recorded_calloc[index].bytes == sizeof(sqlserver_state_layout));
    }
#endif
    strict = sqlparser_wire_scalar_insert_certify(h);
    stage = "every SQLServer owning-state pointer count capacity and checkpoint rejects nonplain metadata";
    CHECK(strict && h->native_scalar_provenance == NULL);
    strings = strict->string_count;
    sqlparser_wire_scalar_insert_destroy(strict);
    CHECK(sqlparser_dialect_state_is_plain_insert_strings(h, strings));
    for (i = 0U; i < count; ++i) {
        const state_field *field = &sqlserver_fields[i];
        unsigned char saved[sizeof(size_t) + sizeof(void *)];
        unsigned char *address = (unsigned char *)h->dialect_state + field->offset;
        size_t changed = strings + 1U;
        int flag = 1;
        void *pointer = &flag;
        CHECK(field->size <= sizeof(saved)); memcpy(saved, address, field->size);
        if (field->pointer) memcpy(address, &pointer, field->size);
        else if (field->size == sizeof(int)) memcpy(address, &flag, field->size);
        else memcpy(address, &changed, field->size);
        stage = field->name;
        CHECK(!sqlparser_dialect_state_is_plain_insert_strings(h, strings));
        CHECK(sqlparser_wire_scalar_insert_certify(h) == NULL);
        CHECK(sqlparser_wire_scalar_insert_from_native(h) == NULL);
        memcpy(address, saved, field->size);
        CHECK(sqlparser_dialect_state_is_plain_insert_strings(h, strings)); ++cases;
    }
    {
        sqlparser_handle_t manufactured = *h;
        sqlparser_dialect_ops_t foreign = *h->dialect_ops;
        unsigned char guard = 0;
        manufactured.dialect_state = &guard; manufactured.dialect_ops = &foreign;
        CHECK(!sqlparser_dialect_supports_plain_scalar_insert(&manufactured));
        CHECK(!sqlparser_dialect_state_is_plain_insert_strings(&manufactured, strings));
        manufactured.dialect_ops = NULL;
        CHECK(!sqlparser_dialect_state_is_plain_insert_strings(&manufactured, strings));
        manufactured.dialect_ops = h->dialect_ops; manufactured.dialect_state = NULL;
        CHECK(!sqlparser_dialect_state_is_plain_insert_strings(&manufactured, strings));
    }
    sqlparser_handle_destroy(h); free(sql);
}

static void capability_scope(void)
{
    size_t i;
    stage = "SQLServer strict wire admission preserves native parser restriction";
    for (i = 0U; i < COUNT(families); ++i) {
        sqlparser_handle_t fake = {0};
        fake.dialect = families[i].dialect; fake.dialect_ops = sqlparser_dialect_get_ops(fake.dialect);
        CHECK(fake.dialect_ops != NULL && sqlparser_dialect_supports_plain_scalar_insert(&fake));
        CHECK(fake.dialect_ops->plain_scalar_native_validation == 0);
        CHECK(fake.dialect_ops->state_is_plain_insert_strings != NULL); ++cases;
    }
}

static void same_error(sqlparser_status_t a, const sqlparser_error_t *ae,
    sqlparser_status_t b, const sqlparser_error_t *be)
{
    CHECK(a == b && ae->code == be->code && ae->cursor == be->cursor &&
        ae->line == be->line && ae->column == be->column);
    same_text(ae->message, be->message);
}

/* Explicit native grammar + legacy serializer is also the diagnostic oracle.
 * Do not require dialect-specific unsupported syntax to become accepted. */
static void syntax_fallback_case(const char *sql, size_t max_statements, int patchable)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *a = NULL, *b = NULL;
    sqlparser_status_t sa, sb;
    sqlparser_error_t ae, be;
    char *at = NULL, *bt = NULL;
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    if (max_statements) options.limits.max_statement_count = max_statements;
    sa = sqlparser_parse_with_options(sql, &options, &a, &ae);
    force_legacy = 1; sb = sqlparser_parse_with_options(sql, &options, &b, &be); force_legacy = 0;
    same_error(sa, &ae, sb, &be); ++cases;
    if (sa == SQLPARSER_STATUS_OK) {
        sqlparser_query_graph_view_t ag, bg;
        CHECK(a && b); same_wire(a, b);
        CHECK(a->native_scalar_provenance == NULL);
        CHECK(sqlparser_wire_scalar_insert_from_native(a) == NULL);
        CHECK(sqlparser_wire_scalar_insert_certify(a) == NULL);
        CHECK(sqlparser_handle_ensure_ast(b, &error) == SQLPARSER_STATUS_OK);
        sa = sqlparser_statement_query_graph(a, 0U, &ag, &ae);
        sb = sqlparser_statement_query_graph(b, 0U, &bg, &be); same_error(sa, &ae, sb, &be);
        if (sa == SQLPARSER_STATUS_OK) {
            CHECK(sqlparser_query_graph_wire_scalar_insert(a) == NULL);
            sa = sqlparser_export_view_json(a, 0U, &at, &ae);
            sb = sqlparser_export_view_json(b, 0U, &bt, &be); same_error(sa, &ae, sb, &be);
            if (sa == SQLPARSER_STATUS_OK) same_text(at, bt);
            sqlparser_string_free(at); sqlparser_string_free(bt); at = bt = NULL;
        }
        if (patchable) {
            sqlparser_patch_t p = {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql="'fallback-grown'"};
            sqlparser_patch_list_t list = {&p, 1U};
            sa = sqlparser_apply_patch(a, &list, &ae);
            sb = legacy_apply(b, &list, &be); same_error(sa, &ae, sb, &be);
        }
        CHECK(a->failed == b->failed);
        if (!a->failed) {
            sa = sqlparser_deparse(a, &at, &ae);
            sb = sqlparser_deparse(b, &bt, &be); same_error(sa, &ae, sb, &be);
            if (sa == SQLPARSER_STATUS_OK) same_text(at, bt);
        }
    } else CHECK(!a && !b);
    sqlparser_string_free(at); sqlparser_string_free(bt); sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
}

static void extended_syntax_fallbacks(void)
{
    static const char *expressions[] = {"N'national-中'", "E'escape\\ntext'", "$tag$dollar-Ω$tag$", "'can''t'", "'back\\slash'", "upper('text')", "-1", "TRUE", "NULL"};
    size_t mode, r;
    stage = "national E dollar escaped strings quoted names and DML tails preserve native baseline";
    for (mode = 0U; mode < COUNT(expressions) + 3U; ++mode) {
        buffer b = {0};
        append(&b, "INSERT %4096sINTO %s(a,b,c) VALUES ", "", mode == COUNT(expressions) ? (families[family_index].mysql ? "`QuotedTable`" : "\"QuotedTable\"") : "t");
        for (r = 0U; r < 33U; ++r) append(&b, "%s(%zu,'ordinary',%s)", r ? "," : "", r,
            mode < COUNT(expressions) ? expressions[mode] : "'original'");
        if (mode == COUNT(expressions) + 1U) append(&b, " RETURNING a");
        if (mode == COUNT(expressions) + 2U) append(&b, " ON CONFLICT(a) DO NOTHING");
        syntax_fallback_case(b.data, 0U, 1); free(b.data);
    }
    {
        static const char *tails[] = {
            "INTO t(a,b,c) VALUES (1 + 2, 'plain', 'original')",
            "INTO t(a,b,c) SELECT id, 'plain', 'original' FROM employees START WITH id = 1 CONNECT BY PRIOR id = manager_id",
            "INTO t(a,b,c) SELECT id, 'plain', 'original' FROM employees CONNECT BY PRIOR id = manager_id START WITH id = 1"
        };
        size_t i;
        stage = "large INSERT certification misses retain AExpr and hierarchy validation";
        for (i = 0U; i < COUNT(tails); ++i) {
            buffer padded = {0};
            /* Prefix qualifies; padding follows INSERT, rather than preceding
             * it, so every new family must enter the conservative route. */
            append(&padded, "INSERT %4096s%s", "", tails[i]);
            syntax_fallback_case(padded.data, 0U, i == 0U);
            free(padded.data);
        }
    }
    syntax_fallback_case("SELECT 1; SELECT 2", 1U, 0);
    syntax_fallback_case("INSERT INTO t(a,b,c) VALUES (1,'x','bad' trailing)", 0U, 0);
    syntax_fallback_case("INSERT INTO t(a,b,c) VALUES (1,'x','ok'); garbage", 1U, 0);
    if (dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL || dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL) {
        stage = "Vastbase rewriting remains on its original native validation path";
        syntax_fallback_case("SET statement_timeout = 1000", 0U, 0);
        syntax_fallback_case("SELECT id FROM employees START WITH id = 1 CONNECT BY PRIOR id = manager_id", 0U, 0);
        syntax_fallback_case("SELECT id FROM employees CONNECT BY PRIOR id = manager_id START WITH id = 1", 0U, 0);
    }
}

static void noninsert_validation_parity(void)
{
    static const char *sql[] = {
        "SELECT 1, 'plain', CURRENT_TIMESTAMP FROM source_table WHERE id = 7",
        "SELECT -1, 1 + 2, abs(3), NULL, TRUE FROM source_table WHERE id <> 4",
        "SELECT id FROM employees START WITH id = 1 CONNECT BY PRIOR id = manager_id",
        "SELECT id FROM employees CONNECT BY PRIOR id = manager_id START WITH id = 1",
        "SELECT id FROM employees ORDER SIBLINGS BY id",
        "MERGE INTO target_table t USING source_table s ON t.id = s.id WHEN MATCHED THEN UPDATE SET value = s.value WHEN NOT MATCHED THEN INSERT (id, value) VALUES (s.id, s.value)",
        "SELECT 1; SELECT 2",
        "SELECT 1 + )"
    };
    size_t i;
    stage = "enabled-family SELECT MERGE hierarchy validation matches native legacy baseline";
    for (i = 0U; i < COUNT(sql); ++i) {
        buffer source = {0};
        /* Long non-INSERT statements must preserve the conservative old route. */
        append(&source, "%4096s%s", "", sql[i]);
        syntax_fallback_case(source.data, i == 6U ? 1U : 0U, 0);
        free(source.data);
    }
}

static sqlparser_status_t patch_parity(const char *source, const sqlparser_patch_list_t *list,
    size_t sql_limit, size_t output_limit)
{
    sqlparser_handle_t *a = parse(source), *b = parse(source);
    sqlparser_query_graph_view_t ag;
    sqlparser_status_t sa, sb;
    sqlparser_error_t ae, be;
    char *as = NULL, *bs = NULL;
    CHECK(sqlparser_statement_query_graph(a, 0U, &ag, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_ensure_ast(b, &error) == SQLPARSER_STATUS_OK);
    if (sql_limit) a->limits.max_sql_bytes = b->limits.max_sql_bytes = sql_limit;
    if (output_limit) a->limits.max_output_bytes = b->limits.max_output_bytes = output_limit;
    sa = sqlparser_apply_patch(a, list, &ae); sb = legacy_apply(b, list, &be);
    same_error(sa, &ae, sb, &be); CHECK(a->failed == b->failed); ++cases;
    if (!a->failed) {
        sa = sqlparser_deparse(a, &as, &ae); sb = sqlparser_deparse(b, &bs, &be);
        same_error(sa, &ae, sb, &be); if (sa == SQLPARSER_STATUS_OK) same_text(as, bs);
        CHECK(a->failed == b->failed);
    }
    sqlparser_string_free(as); sqlparser_string_free(bs); sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
    return sa;
}

static void resource_and_structure_boundaries(void)
{
    char *source = primary_source(33U, NULL);
    size_t length = strlen(source), mode, order, offset;
    char *large = malloc(length + 140U);
    CHECK(large);
    stage = "raw fragment source/output limits retain original error and edit ordering";
    for (mode = 0U; mode < 2U; ++mode) for (order = 0U; order < 2U; ++order) {
        sqlparser_patch_t p[2] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="invalid-selector", .sql="'first'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql=large}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        if (mode) p[0].selector = "stmt[99].insert_cell[0][2]";
        large[0] = '\''; memset(large + 1U, 'x', length + 100U);
        large[length + 101U] = '\''; large[length + 102U] = '\0';
        if (order) { sqlparser_patch_t t = p[0]; p[0] = p[1]; p[1] = t; }
        patch_parity(source, &list, length, length * 4U);
    }
    for (mode = 0U; mode < 2U; ++mode) for (offset = 0U; offset < 3U; ++offset)
    for (order = 0U; order < 2U; ++order) {
        size_t fragment_length = length + offset - 1U;
        sqlparser_patch_t p[3] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql=large},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[1][2]", .sql="''"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql="''"}
        };
        sqlparser_patch_list_t list = {p, 2U};
        large[0] = '\''; memset(large + 1U, 'x', fragment_length - 2U);
        large[fragment_length - 1U] = '\''; large[fragment_length] = '\0';
        if (order) { sqlparser_patch_t t = p[0]; p[0] = p[1]; p[1] = t; }
        patch_parity(source, &list, mode ? length * 4U : length, mode ? length : length * 4U);
        list.count = 3U;
        patch_parity(source, &list, mode ? length * 4U : length, mode ? length : length * 4U);
    }
    stage = "structural mutations and source-selector dependencies use unchanged fallback";
    for (mode = 0U; mode < 4U; ++mode) {
        sqlparser_patch_t p[2] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql="'before-structure'"},
            {.op=SQLPARSER_PATCH_DELETE_COLUMN, .selector="stmt[0].insert_columns", .index=0U}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        if (mode == 1U) p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN,
            .selector="stmt[0].insert_columns", .index=1U, .name="added", .default_sql="42"};
        if (mode == 2U) p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ROW,
            .selector="stmt[0].insert_row[1]"};
        if (mode == 3U) p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[1][2]", .source_selector="stmt[0].insert_cell[0][2]"};
        CHECK(patch_parity(source, &list, 0U, 0U) == SQLPARSER_STATUS_OK);
    }
    free(large); free(source);
}


/* Ordinary syntax creates each retained SQLServer state family. These cases
 * compare diagnostics too, because unsupported dialect-specific grammar must
 * remain unsupported rather than being smuggled through native recognition. */
static void sqlserver_state_fallbacks(void)
{
    static const struct { const char *sql; int patchable; } input[] = {
        {"INSERT INTO dbo.t(a,b,c) VALUES (@p,'plain','original')", 1},
        {"INSERT INTO dbo.t(a,b,c) VALUES (?,N'national-中','original')", 1},
        {"INSERT INTO dbo.t(a,b,c) VALUES (1,N'national-中','original')", 1},
        {"INSERT INTO dbo.t WITH (TABLOCK) (a,b,c) VALUES (1,'plain','original')", 1},
        {"INSERT INTO dbo.t(a,b,c) OUTPUT INSERTED.a VALUES (1,'plain','original')", 1},
        {"INSERT INTO dbo.t(a,b,c) OUTPUT INSERTED.a INTO dbo.audit VALUES (1,'plain','original')", 1},
        {"INSERT INTO dbo.t(a,b,c) VALUES (TRY_CAST(1 AS BIT),'plain','original')", 1},
        {"INSERT INTO dbo.t(a,b,c) VALUES (TRY_CONVERT(int,'1',0),'plain','original')", 1},
        {"INSERT INTO dbo.t(a,b,c) VALUES ({fn ABS(-1)},'plain','original')", 1},
        {"INSERT INTO dbo.t(a,b,c) VALUES ((SELECT TOP (1) id FROM dbo.u),'plain','original')", 1},
        {"INSERT INTO [dbo].[QuotedTable]([a],[b],[c]) VALUES (1,'plain','original')", 1},
        {"SELECT TOP (2) CAST(1 AS BIT), N'national', @value FROM dbo.u WITH (NOLOCK) OPTION (RECOMPILE)", 0},
        {"SELECT id FROM dbo.u FOR JSON PATH, INCLUDE_NULL_VALUES", 0},
        {"SAVE TRANSACTION savepoint_name", 0},
        {"EXEC sp_rename 'dbo.u.old_name', 'new_name', 'COLUMN'", 0},
        {"DROP ROLE app_role; DROP USER app_user", 0},
        {"DROP USER IF EXISTS app_user", 0},
        {"IF @enabled = 1 INSERT INTO dbo.t(a,b,c) VALUES (1,'plain','original') ELSE SELECT 0", 0},
        {"BEGIN TRY INSERT INTO dbo.t(a,b,c) VALUES (1,'plain','original') END TRY BEGIN CATCH SELECT ERROR_MESSAGE() END CATCH", 0},
        {"DECLARE @x INT; SET @x = 1; SELECT @x", 0},
        {"EXEC(N'SELECT 1')", 0},
        {"INSERT INTO dbo.t(a,b,c) VALUES (1,'plain','original'); SELECT 1", 0}
    };
    size_t i;
    stage = "SQLServer owned-state syntax retains legacy graph patch output and control behavior";
    for (i = 0U; i < COUNT(input); ++i) syntax_fallback_case(input[i].sql, 0U, input[i].patchable);
}

static int bound_owners(sqlparser_handle_t *h)
{
    int bound;
    CHECK(h && h->dialect_state);
    memcpy(&bound, (unsigned char *)h->dialect_state + offsetof(sqlserver_state_layout, owners_bound), sizeof(bound));
    return bound;
}

static void ast_owner_lifecycle(void)
{
    static const char *expressions[] = {
        "upper('mixed')", "N'national-中'",
        "(SELECT TOP (1) TRY_CAST({fn ABS(-1)} AS BIT) FROM dbo.u WITH (NOLOCK))"
    };
    size_t mode, round;
    stage = "wire graph to expression fallback to literal patch and independent clone owner binding";
    for (mode = 0U; mode < COUNT(expressions); ++mode) {
        char *input = primary_source(33U, NULL), *a = NULL, *b = NULL;
        sqlparser_query_graph_view_t graph, bg;
        sqlparser_handle_t *h = parse(input), *ref = reference(input, &bg), *clone = NULL, *ref_clone = NULL;
        sqlparser_patch_t p[2] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql=expressions[mode]},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[1][2]", .sql="'first'"}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        size_t before;
        ++cases; poison_free(input);
        CHECK(!bound_owners(h) && bound_owners(ref));
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        assert_fast(h, 33U, 9U); before = unpacks;
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(unpacks > before);
        CHECK(legacy_apply(ref, &list, &error) == SQLPARSER_STATUS_OK);
        for (round = 0U; round < 2U; ++round) {
            p[0].selector = "stmt[0].insert_cell[2][2]"; p[0].sql = round ? "''" : "'expanded-literal-after-expression'";
            p[1].selector = "stmt[0].insert_cell[32][2]"; p[1].sql = "'tail'";
            CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
            CHECK(legacy_apply(ref, &list, &error) == SQLPARSER_STATUS_OK);
        }
        CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK && bound_owners(h));
        CHECK(sqlparser_wire_scalar_insert_certify(h) == NULL);
        CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_handle_clone(ref, &ref_clone, &error) == SQLPARSER_STATUS_OK);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
        h = clone; ref = ref_clone;
        CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK && bound_owners(h));
        p[0].selector = "stmt[0].insert_cell[3][2]"; p[0].sql = "'clone-owned'";
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(legacy_apply(ref, &list, &error) == SQLPARSER_STATUS_OK);
        same_wire(h, ref);
        CHECK(sqlparser_deparse(h, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(ref, &b, &error) == SQLPARSER_STATUS_OK); same_text(a, b);
        deep_parity(h, ref, 33U, 9U, 0);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
        same_text(a, b); sqlparser_string_free(a); sqlparser_string_free(b);
    }
}

/* The old storage stays live until the graph is destroyed, so a rejection can
 * safely continue through generic AST planning. Equal bytes are insufficient
 * for reuse of a certificate tied to a different owned source/wire buffer. */
static void stale_certificate_storage(void)
{
    size_t mode;
    for (mode = 0U; mode < 3U; ++mode) {
        char *input = primary_source(33U, NULL), *retired_sql = NULL, *retired_parser = NULL, *retired_wire = NULL;
        char *a = NULL, *b = NULL;
        sqlparser_query_graph_view_t graph, bg;
        sqlparser_handle_t *h = parse(input), *ref = reference(input, &bg);
        sqlparser_patch_t p[2] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql="'new-owner'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[32][2]", .sql="''"}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        size_t before;
        stage = "cached certificate cannot commit against replaced source wire or generation ownership";
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        assert_fast(h, 33U, 9U); poison_free(input); ++cases;
        if (mode == 0U) {
            retired_sql = h->sql;
            if (h->parser_sql != h->sql) retired_parser = h->parser_sql;
            h->sql = copy(h->sql); h->parser_sql = h->sql;
        } else if (mode == 1U) {
            retired_wire = h->parse_tree.data;
            h->parse_tree.data = malloc(h->parse_tree.len); CHECK(h->parse_tree.data);
            memcpy(h->parse_tree.data, retired_wire, h->parse_tree.len);
        } else {
            ++h->generation; h->surface_source_complete = 1;
            CHECK(sqlparser_query_graph_wire_scalar_insert(h) == NULL);
        }
        before = unpacks;
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(unpacks > before);
        CHECK(legacy_apply(ref, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(h, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(ref, &b, &error) == SQLPARSER_STATUS_OK); same_text(a, b);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref);
        free(retired_sql); free(retired_parser); free(retired_wire);
        sqlparser_string_free(a); sqlparser_string_free(b);
    }
}

/* Prove the comparison route really invokes generic AST + final reparse,
 * while its fast counterpart consumes neither. */
static void oracle_route_is_legacy(void)
{
    char *input = primary_source(33U, NULL);
    sqlparser_handle_t *h, *ref;
    sqlparser_query_graph_view_t graph, bg;
    sqlparser_patch_t p[2] = {
        {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql="'oracle-first'"},
        {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[32][2]", .sql="'oracle-last'"}
    };
    sqlparser_patch_list_t list = {p, COUNT(p)};
    size_t before, ops_reads = legacy_ops_reads;
    stage = "oracle instrumentation proves independently forced legacy apply";
    ref = reference(input, &bg);
    CHECK(ref->ast && !sqlparser_query_graph_wire_scalar_insert(ref));
    CHECK(legacy_ops_reads > ops_reads);
    CHECK(ref->dialect_ops != sqlparser_dialect_get_ops(ref->dialect));
    CHECK(!ref->dialect_ops->plain_ascii_string_fragments &&
        !ref->dialect_ops->plain_scalar_native_validation &&
        ref->dialect_ops->state_is_plain_insert_strings == NULL);
    before = destructive_reparses;
    CHECK(legacy_apply(ref, &list, &error) == SQLPARSER_STATUS_OK);
    CHECK(destructive_reparses > before);
    h = parse(input);
    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    before = destructive_reparses;
    CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
    NO_REPARSE(before); same_wire(h, ref);
    sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref); free(input); ++cases;
}

int main(int argc, char **argv)
{
    static const size_t widths[] = {8U, 9U, 10U, 13U};
    static const size_t heights[] = {31U, 32U, 33U};
    static const size_t paddings[] = {95U, 16350U};
    size_t w, h, typed, p;
    char label[160];
    if (argc > 1) fixture_path = argv[1];
    if (argc > 2) golden_directory = argv[2];
    capability_scope();
    for (family_index = 0U; family_index < COUNT(families); ++family_index) {
        dialect = families[family_index].dialect;
        oracle_route_is_legacy();
        for (typed = 0U; typed < 2U; ++typed) {
            stage = typed ? "5000 x 9 typed 49-byte original-selector lifecycle" : "5000 x 9 raw 49-byte original-selector lifecycle";
            lifecycle(5000U, 9U, (int)typed, 0U, 1);
        }
        for (w = 0U; w < COUNT(widths); ++w) for (h = 0U; h < COUNT(heights); ++h)
        for (typed = 0U; typed < 2U; ++typed) {
            snprintf(label, sizeof(label), "mixed scalar %zu rows x %zu columns typed=%zu", heights[h], widths[w], typed);
            stage = label; lifecycle(heights[h], widths[w], (int)typed, 0U, 0);
        }
        for (p = 0U; p < COUNT(paddings); ++p) {
            stage = "mixed-scalar source-location varint boundaries";
            lifecycle(33U, 9U, 0, paddings[p], 0);
        }
        fallback_sources(); ordered_and_fragment_fallbacks(); invalid_fragments();
        sqlserver_state_fallbacks(); ast_owner_lifecycle(); stale_certificate_storage();
        provenance_guards(); unproven_wire_mutations(); state_predicate_guards();
        extended_syntax_fallbacks(); noninsert_validation_parity(); resource_and_structure_boundaries(); allocation_boundaries();
        printf("SQLServer wire pipeline: %s functional checks passed\n", families[family_index].name);
    }
    CHECK(legacy_applies > 0U && legacy_ops_reads > 0U);
    pg_query_exit();
    printf("SQLServer wire pipeline: %zu correctness cases; three 45k-cell graphs, original raw/typed selectors, 49-byte patches, native legacy-wire parity, source positions, ownership, stale proofs, fallback diagnostics and exhaustive allocation faults passed\n", cases);
    return 0;
}
