/* Private source-location cache differential tests. No public hooks or timing. */
#include <stdio.h>
#include "../../src/core/sqlparser_view.c"
static size_t checks;
static sqlparser_error_t error;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error.message); abort(); } } while (0)
/* The frozen layout is an independent width-portable no-growth oracle. */
typedef struct {
    sqlparser_graph_dml_cell_payload_t payload;
    size_t dml_index, row_index, column_ordinal, selector_item_index;
    uint8_t kind, selector_kind, flags;
} old_cell_layout;
_Static_assert(sizeof(old_cell_layout) == sizeof(sqlparser_graph_dml_cell_cache_t), "DML cell grew");
_Static_assert(_Alignof(old_cell_layout) == _Alignof(sqlparser_graph_dml_cell_cache_t), "DML cell alignment changed");
#ifdef SQLPARSER_TARGET_CACHE_WRAPPERS
static size_t allocation_call, fail_at;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static int fail_allocation(void) { return fail_at != 0U && ++allocation_call == fail_at; }
void *__wrap_malloc(size_t n) { return fail_allocation() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t z) { return fail_allocation() ? NULL : __real_calloc(n, z); }
void *__wrap_realloc(void *p, size_t n) { return fail_allocation() ? NULL : __real_realloc(p, n); }
static void arm(size_t at) { allocation_call = 0U; fail_at = at; }
static size_t disarm(void) { fail_at = 0U; return allocation_call; }
#else
static void arm(size_t at) { (void)at; }
static size_t disarm(void) { return 0U; }
#endif

static char *source(size_t rows)
{
    char *sql = malloc(rows * 100U + 4200U);
    size_t row, used;
    CHECK(sql != NULL);
    used = (size_t)sprintf(sql, "INSERT INTO target_test(a,b,c,d,e) VALUES ");
    for (row = 0U; row < rows; ++row)
        used += (size_t)sprintf(sql + used, "%s(%zu,'alpha','原始',12.5,CURRENT_TIMESTAMP)", row ? "," : "", row);
    sql[used++] = ';';
    /* Exercise native admission even in the small OOM matrix (4096-byte gate). */
    while (used < 4096U) sql[used++] = ' ';
    sql[used] = '\0';
    return sql;
}

static sqlparser_handle_t *parse_graph(const char *sql)
{
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_t options;
    sqlparser_query_graph_view_t graph;
    sqlparser_parse_options_default(&options);
    options.dialect = SQLPARSER_DIALECT_MYSQL;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    CHECK(h != NULL && h->native_scalar_provenance != NULL);
    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(h->query_graph != NULL && h->query_graph->wire_scalar_insert != NULL);
    return h;
}

static void miss(const sqlparser_handle_t *h, const sqlparser_wire_scalar_insert_t *insert,
    size_t row, size_t column)
{
    static const char sentinel[] = "unchanged";
    int32_t location = -777;
    const char *string = sentinel;
    sqlparser_error_t saved;
    memset(&error, 0xa5, sizeof(error)); memcpy(&saved, &error, sizeof(saved));
    arm(SIZE_MAX);
    CHECK(!sqlparser_query_graph_native_scalar_string_target(h, insert, row, column, &location, &string));
    CHECK(disarm() == 0U);
    CHECK(location == -777 && string == sentinel && memcmp(&error, &saved, sizeof(error)) == 0);
}

static void proof_matrix(void)
{
    char *sql = source(64U);
    sqlparser_handle_t *h = parse_graph(sql), saved_handle = *h;
    sqlparser_query_graph_cache_t *cache = h->query_graph, saved_cache = *cache;
    sqlparser_wire_scalar_insert_t *insert = cache->wire_scalar_insert, saved_insert = *insert;
    sqlparser_native_scalar_provenance_t *provenance = h->native_scalar_provenance, saved_provenance = *provenance;
    sqlparser_graph_dml_cell_cache_t *cell = &cache->dml_cells[2], saved_cell = *cell;
    size_t row, column, scenario;
    for (row = 0U; row < 64U; ++row) {
        for (column = 0U; column < 5U; ++column) {
            sqlparser_wire_scalar_cell_t old;
            int32_t location = -1;
            const char *string = NULL;
            int found;
            arm(SIZE_MAX);
            found = sqlparser_query_graph_native_scalar_string_target(h, insert, row, column, &location, &string);
            CHECK(disarm() == 0U);
            CHECK(sqlparser_wire_scalar_insert_certified_cell(insert, row, column, &old));
            CHECK(found == (old.kind == SQLPARSER_WIRE_SCALAR_STRING));
            if (found) {
                CHECK(location == old.location);
                CHECK(string == sqlparser_query_graph_wire_scalar_string(h, row, column));
                CHECK(strlen(string) == old.length && memcmp(string, old.text, old.length) == 0);
            } else CHECK(location == -1 && string == NULL);
        }
    }
    for (scenario = 0U; scenario < 37U; ++scenario) {
        switch (scenario) {
        case 0: h->failed = 1; break;
        case 1: h->generation = 1UL; break;
        case 2: h->ast = (void *)h; break;
        case 3: h->control = (void *)h; break;
        case 4: h->patch_batch_flags = 1U; break;
        case 5: h->surface_source_edits.count = 1U; break;
        case 6: h->query_graph = NULL; break;
        case 7: cache->statement_count = 2U; break;
        case 8: cache->generation++; break;
        case 9: h->query_graph_generation++; break;
        case 10: cache->wire_scalar_insert = NULL; break;
        case 11: insert->sql = sql; break;
        case 12: insert->sql_length++; break;
        case 13: insert->wire = sql; break;
        case 14: insert->wire_length++; break;
        case 15: h->native_scalar_provenance = NULL; break;
        case 16: provenance->sql = sql; break;
        case 17: provenance->wire = sql; break;
        case 18: provenance->wire_length++; break;
        case 19: provenance->proof.source_length++; break;
        case 20: provenance->proof.row_count++; break;
        case 21: provenance->proof.column_count++; break;
        case 22: cache->dml_cell_count = 2U; break;
        case 23: cell->flags &= ~SQLPARSER_GRAPH_DML_CELL_HAS_NATIVE_SOURCE; break;
        case 24: cell->flags &= ~SQLPARSER_GRAPH_DML_CELL_HAS_SELECTOR; break;
        case 25: cell->kind = SQLPARSER_GRAPH_VALUE_BIND; break;
        case 26: cell->payload.literal.kind = SQLPARSER_LITERAL_KIND_INTEGER; break;
        case 27: cell->selector_kind = SQLPARSER_SELECTOR_KIND_VALUE; break;
        case 28: cell->row_index++; break;
        case 29: cell->column_ordinal++; break;
        case 30: cell->selector_position.certified_source_location = SIZE_MAX; break;
        case 31: cell->payload.literal.string_value = NULL; break;
        case 32: h->parser_sql = sql; break;
        case 33: h->dialect = SQLPARSER_DIALECT_POSTGRESQL; break;
        case 34: h->dialect_ops = NULL; break;
        case 35: h->parse_tree.data = NULL; insert->wire = NULL; provenance->wire = NULL; break;
        case 36: h->parse_tree.len = insert->wire_length = provenance->wire_length = 0U; break;
        }
        miss(h, insert, 0U, 2U);
        *h = saved_handle; *cache = saved_cache; *insert = saved_insert;
        *provenance = saved_provenance; *cell = saved_cell;
    }
    miss(NULL, insert, 0U, 2U); miss(h, NULL, 0U, 2U);
    miss(h, insert, 64U, 2U); miss(h, insert, 0U, 5U);
    insert->row_count = provenance->proof.row_count = SIZE_MAX;
    insert->column_count = provenance->proof.column_count = 3U;
    miss(h, insert, SIZE_MAX / 3U + 1U, 0U);
    *insert = saved_insert; *provenance = saved_provenance;
    {
        int32_t location = -99;
        const char *string = sql;
        CHECK(!sqlparser_query_graph_native_scalar_string_target(h, insert, 0U, 2U, NULL, &string) && string == sql);
        CHECK(!sqlparser_query_graph_native_scalar_string_target(h, insert, 0U, 2U, &location, NULL) && location == -99);
    }
    /* Source spelling checks deliberately remain in the existing planner. */
    CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK);
    CHECK(h->native_scalar_provenance == NULL);
    miss(h, insert, 0U, 2U);
    sqlparser_handle_destroy(h); free(sql);
}

static void disable_targets(sqlparser_handle_t *h)
{
    size_t i;
    for (i = 0U; i < h->query_graph->dml_cell_count; ++i)
        h->query_graph->dml_cells[i].flags &= ~SQLPARSER_GRAPH_DML_CELL_HAS_NATIVE_SOURCE;
}

static size_t differential_apply(size_t rows, int typed, size_t fail)
{
    char *sql = source(rows);
    sqlparser_handle_t *a = parse_graph(sql), *b = parse_graph(sql);
    sqlparser_patch_t *patches = calloc(rows, sizeof(*patches));
    sqlparser_literal_value_t *literals = calloc(rows, sizeof(*literals));
    char (*selectors)[80] = calloc(rows, sizeof(*selectors));
    sqlparser_error_t ea, eb;
    sqlparser_status_t sa, sb;
    sqlparser_patch_list_t list;
    size_t row, ca, cb;
    CHECK(patches && literals && selectors);
    disable_targets(b);
    for (row = 0U; row < rows; ++row) {
        snprintf(selectors[row], 80U, "stmt[0].insert_cell[%zu][2]", row);
        patches[row].op = SQLPARSER_PATCH_REPLACE;
        patches[row].selector = selectors[row];
        if (typed) {
            literals[row].kind = SQLPARSER_LITERAL_KIND_STRING;
            literals[row].string_value = row % 3U == 0U ? "" : row % 3U == 1U ? "a much longer replacement" : "x";
            patches[row].literal = &literals[row];
        } else patches[row].sql = row % 3U == 0U ? "''" : row % 3U == 1U ? "'a much longer replacement'" : "'x'";
    }
    list.items = patches; list.count = rows;
    memset(&ea, 0xa5, sizeof(ea)); memset(&eb, 0xa5, sizeof(eb));
    arm(fail); sa = sqlparser_apply_patch(a, &list, &ea); ca = disarm();
    arm(fail); sb = sqlparser_apply_patch(b, &list, &eb); cb = disarm();
    CHECK(sa == sb && ca == cb && memcmp(&ea, &eb, sizeof(ea)) == 0);
    if (sa == SQLPARSER_STATUS_OK) {
        char *oa = NULL, *ob = NULL;
        CHECK(a->generation == 1UL && a->native_scalar_provenance == NULL && a->query_graph == NULL);
        CHECK(a->parse_tree.len == b->parse_tree.len && memcmp(a->parse_tree.data, b->parse_tree.data, a->parse_tree.len) == 0);
        CHECK(strcmp(a->sql, b->sql) == 0);
        CHECK(sqlparser_deparse(a, &oa, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(b, &ob, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(oa, ob) == 0);
        free(oa); free(ob);
        /* A second graph/patch generation must not revive native target proof. */
        if (fail == SIZE_MAX) {
            sqlparser_query_graph_view_t ga, gb;
            CHECK(sqlparser_statement_query_graph(a, 0U, &ga, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_statement_query_graph(b, 0U, &gb, &error) == SQLPARSER_STATUS_OK);
            miss(a, sqlparser_query_graph_wire_scalar_insert(a), 0U, 2U);
            CHECK(sqlparser_apply_patch(a, &list, &ea) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_apply_patch(b, &list, &eb) == SQLPARSER_STATUS_OK);
            CHECK(a->parse_tree.len == b->parse_tree.len && memcmp(a->parse_tree.data, b->parse_tree.data, a->parse_tree.len) == 0);
        }
    }
    sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
    free(selectors); free(literals); free(patches); free(sql);
    return ca;
}

static void clone_lifetime(void)
{
    char *sql = source(64U);
    sqlparser_handle_t *h = parse_graph(sql), *clone = NULL;
    sqlparser_query_graph_view_t graph;
    CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
    CHECK(clone->query_graph == NULL && clone->native_scalar_provenance == NULL);
    CHECK(sqlparser_statement_query_graph(clone, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    miss(clone, sqlparser_query_graph_wire_scalar_insert(clone), 0U, 2U);
    sqlparser_handle_destroy(clone); sqlparser_handle_destroy(h); free(sql);
}

int main(void)
{
    size_t mode;
    proof_matrix(); clone_lifetime();
    for (mode = 0U; mode < 2U; ++mode) {
        size_t allocations = differential_apply(64U, (int)mode, SIZE_MAX), i;
        (void)differential_apply(5000U, (int)mode, SIZE_MAX);
#ifdef SQLPARSER_TARGET_CACHE_WRAPPERS
        for (i = 1U; i <= allocations; ++i) (void)differential_apply(64U, (int)mode, i);
#else
        (void)allocations; (void)i;
#endif
    }
    pg_query_exit();
    printf("native target cache: %zu checks; proof misses, decode parity, full wire, clone/AST/commit and OOM passed\n", checks);
    return 0;
}
