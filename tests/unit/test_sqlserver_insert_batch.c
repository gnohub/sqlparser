/* Small public-API proofs for the SQL Server deferred INSERT-batch route.
 * This is a correctness test, not a timing harness. A copied but otherwise
 * identical dialect-ops table deliberately fails the new registered-owner
 * guard, providing the old per-cell route for semantic/error comparison. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_ast_internal.h"
#include "sqlparser_dialect_internal.h"
#include "sqlparser_test_failure.h"

static sqlparser_dialect_t dialect;
static sqlparser_error_t error;
static const char *stage;
static size_t reparses, cases;
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d dialect=%d stage=%s: %s: %s\n", \
    __FILE__, __LINE__, dialect, stage, #x, error.message); abort(); } } while (0)

#ifdef SQLPARSER_SQLSERVER_BATCH_WRAPPERS
sqlparser_status_t __real_sqlparser_handle_reparse_destructive(sqlparser_handle_t *, char **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_handle_reparse_destructive(sqlparser_handle_t *h, char **sql, sqlparser_error_t *e)
{
    ++reparses;
    return __real_sqlparser_handle_reparse_destructive(h, sql, e);
}
#define EXPECT_REPARSES(n) do { if (reparses != (n)) fprintf(stderr, "reparses=%zu expected=%zu\n", reparses, (size_t)(n)); CHECK(reparses == (n)); } while (0)

/* Exercise apply-owned allocations without faulting PostgreSQL's private
 * memory-context allocator, which can terminate the unchanged native parser. */
static int allocation_armed;
static unsigned native_depth;
static size_t allocation_calls, failure_at, failures, live, ledger_end;
static void *ledger[8192];
struct MemoryContextData;
struct MemoryContextData *__real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(struct MemoryContextData *);
struct MemoryContextData *__wrap_pg_query_enter_memory_context(void)
{
    ++native_depth; return __real_pg_query_enter_memory_context();
}
void __wrap_pg_query_exit_memory_context(struct MemoryContextData *context)
{
    __real_pg_query_exit_memory_context(context); CHECK(native_depth != 0U); --native_depth;
}
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static size_t slot(void *p)
{
    size_t i;
    if (p && live) for (i = 0U; i < ledger_end; i++) if (ledger[i] == p) return i;
    return COUNT(ledger);
}
static void track(void *p)
{
    size_t i;
    if (!p) return;
    for (i = 0U; i < ledger_end; i++) if (!ledger[i]) break;
    CHECK(i < COUNT(ledger)); ledger[i] = p; ++live;
    if (i == ledger_end) ++ledger_end;
}
static int reject_allocation(void)
{
    if (!allocation_armed || native_depth) return 0;
    if (++allocation_calls != failure_at) return 0;
    ++failures; return 1;
}
void *__wrap_malloc(size_t n)
{
    void *p;
    if (reject_allocation()) return NULL;
    p = __real_malloc(n); if (allocation_armed && !native_depth) track(p); return p;
}
void *__wrap_calloc(size_t n, size_t size)
{
    void *p;
    if (reject_allocation()) return NULL;
    p = __real_calloc(n, size); if (allocation_armed && !native_depth) track(p); return p;
}
void *__wrap_realloc(void *p, size_t n)
{
    size_t i = slot(p);
    void *q;
    if (reject_allocation()) return NULL;
    q = __real_realloc(p, n);
    if (q || !n) {
        if (i < COUNT(ledger)) { ledger[i] = q; if (!q) --live; }
        else if (allocation_armed && !native_depth) track(q);
    }
    return q;
}
void __wrap_free(void *p)
{
    size_t i = slot(p);
    if (i < COUNT(ledger)) { ledger[i] = NULL; --live; }
    __real_free(p);
}
#else
#define EXPECT_REPARSES(n) ((void)(n))
#endif

static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    return h;
}

static void apply(sqlparser_handle_t *h, const sqlparser_patch_t *p, size_t n)
{
    sqlparser_patch_list_t list = {p, n};
    CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
}

static void same_view(sqlparser_handle_t *a, sqlparser_handle_t *b)
{
    char *av = NULL, *bv = NULL;
    CHECK(sqlparser_export_view_json(a, 0, &av, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_export_view_json(b, 0, &bv, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(av, bv) == 0);
    sqlparser_string_free(av); sqlparser_string_free(bv);
}

static void cell(sqlparser_handle_t *h, size_t statement, size_t row, size_t column, const char *expected)
{
    char *text = NULL;
    CHECK(sqlparser_insert_cell_sql(h, statement, row, column, &text, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(text, expected) == 0); sqlparser_string_free(text);
}

static const char input[] =
    "/*head*/ INSERT INTO alpha(a,b,c) VALUES ('one',1,CURRENT_TIMESTAMP),('two',2,100.50);\n"
    "INSERT INTO data.beta(x,y) VALUES ('three','keep');\n"
    "INSERT INTO gamma(z) VALUES ('four'),('张三李四'); /*tail*/";
static const char expected[] =
    "/*head*/ INSERT INTO alpha(a,b,c) VALUES ('a',1,CURRENT_TIMESTAMP),('temporary',2,100.50);\n"
    "INSERT INTO data.beta(x,y) VALUES ('longer-value','last');\n"
    "INSERT INTO gamma(z) VALUES (''),('omega'); /*tail*/";

static void positive(int typed, int prebuild)
{
    static const char *selectors[] = {
        "stmt[2].insert_cell[1][0]", "stmt[0].insert_cell[1][0]",
        "stmt[0001].insert_cell[0000][0000]", "stmt[2].insert_cell[0][0]",
        "stmt[0].insert_cell[0][0]", "stmt[1].insert_cell[0][1]"
    };
    static const char *strings[] = {"omega", "temporary", "longer-value", "", "a", "last"};
    static const char *raw[] = {"'omega'", "'temporary'", "'longer-value'", "''", "'a'", "'last'"};
    sqlparser_handle_t *h = parse(input), *old = parse(input), *gold;
    sqlparser_dialect_ops_t ops = *old->dialect_ops;
    sqlparser_query_graph_view_t graphs[3];
    sqlparser_patch_t patches[COUNT(selectors)] = {{0}};
    sqlparser_literal_value_t values[COUNT(selectors)] = {{0}};
    char *actual = NULL, *legacy = NULL;
    size_t i;

    stage = "cross-statement order, source bytes and graph semantics";
    old->dialect_ops = &ops;
    for (i = 0U; i < 3U; i++) if (prebuild)
        CHECK(sqlparser_statement_query_graph(h, i, &graphs[i], &error) == SQLPARSER_STATUS_OK);
    for (i = 0U; i < COUNT(selectors); i++) {
        patches[i].op = SQLPARSER_PATCH_REPLACE; patches[i].selector = selectors[i];
        values[i].kind = SQLPARSER_LITERAL_KIND_STRING; values[i].string_value = strings[i];
        if (typed) patches[i].literal = &values[i]; else patches[i].sql = raw[i];
    }
    reparses = 0U; apply(h, patches, COUNT(patches)); EXPECT_REPARSES(1U);
    if (prebuild) for (i = 0U; i < 3U; i++) {
        sqlparser_graph_dml_t dml;
        CHECK(sqlparser_query_graph_dml(&graphs[i], &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    }
    CHECK(sqlparser_deparse(h, &actual, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(actual, expected) == 0);
    gold = parse(expected);
    same_view(h, gold);
    CHECK(h->parse_tree.len == gold->parse_tree.len);
    CHECK(memcmp(h->parse_tree.data, gold->parse_tree.data, h->parse_tree.len) == 0);
    apply(old, patches, COUNT(patches)); same_view(h, old);
    CHECK(sqlparser_deparse(old, &legacy, &error) == SQLPARSER_STATUS_OK);
    /* Legacy SQL may normalize layout. The new exact source-preserving output
     * above is checked independently, never normalized to make strcmp pass. */
    if (strcmp(actual, legacy) != 0) ++cases;
    stage = "new apply must re-prove the new source";
    reparses = 0U; apply(h, patches, COUNT(patches)); EXPECT_REPARSES(1U);
    cell(h, 0U, 1U, 0U, "'temporary'"); same_view(h, gold);
    stage = "duplicate targets retain caller order and existing materialization";
    patches[1].selector = patches[0].selector;
    patches[2].selector = patches[0].selector;
    apply(h, patches, 3U); apply(gold, patches, 3U); same_view(h, gold);
    cell(h, 2U, 1U, 0U, "'longer-value'");
    sqlparser_string_free(actual); sqlparser_string_free(legacy);
    sqlparser_handle_destroy(gold); sqlparser_handle_destroy(old); sqlparser_handle_destroy(h);
    ++cases;
}

static void fallback_and_mixed(void)
{
    static const char *sources[] = {
        "INSERT INTO t(a) VALUES ('a'); SELECT 'untouched'; INSERT INTO u(a) VALUES ('b');",
        "INSERT INTO t(a) VALUES ('a'); INSERT INTO u(a) OUTPUT inserted.a VALUES ('b');",
        "INSERT INTO [t](a) VALUES ('a'); INSERT INTO u(a) VALUES ('b');",
        "INSERT INTO t(a) VALUES ('a'); INSERT INTO u(a,b) VALUES ('b',UPPER('stay'));",
        "INSERT INTO t(a) VALUES ('a'); INSERT INTO u(a,b) VALUES ('b',NULL);",
        "INSERT INTO t(a) VALUES ('a'); INSERT INTO u(a,b) VALUES ('b',CURRENT_TIMESTAMP(2));"
    };
    size_t i;
    stage = "mixed, rewritten, output and non-scalar batches retain fallback";
    for (i = 0U; i < COUNT(sources); i++) {
        sqlparser_handle_t *a = parse(sources[i]), *b = parse(sources[i]);
        sqlparser_dialect_ops_t ops = *b->dialect_ops;
        sqlparser_patch_t p[] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'first'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector=i == 0U ? "stmt[2].insert_cell[0][0]" : "stmt[1].insert_cell[0][0]", .sql="'last'"}
        };
        char *as = NULL, *bs = NULL;
        b->dialect_ops = &ops;
        apply(a, p, COUNT(p)); apply(b, p, COUNT(p)); same_view(a, b);
        CHECK(sqlparser_deparse(a, &as, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(b, &bs, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(as, bs) == 0);
        sqlparser_string_free(as); sqlparser_string_free(bs);
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b); ++cases;
    }
    stage = "nonplain raw strings retain the legacy fragment route";
    {
        static const char *raw[] = {"'Ω'", "'can''t'", "'back\\slash'", "'unterminated''"};
        for (i = 0U; i < COUNT(raw); i++) {
            sqlparser_handle_t *a = parse(input), *b = parse(input);
            sqlparser_dialect_ops_t ops = *b->dialect_ops;
            sqlparser_patch_t p[] = {
                {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[2].insert_cell[0][0]", .sql="'first'"},
                {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql=raw[i]}
            };
            sqlparser_patch_list_t list = {p, COUNT(p)};
            sqlparser_error_t ea, eb;
            sqlparser_status_t sa, sb;
            b->dialect_ops = &ops;
            sa = sqlparser_apply_patch(a, &list, &ea); sb = sqlparser_apply_patch(b, &list, &eb);
            CHECK(sa == sb); CHECK(strcmp(ea.message, eb.message) == 0);
            CHECK(ea.cursor == eb.cursor && ea.line == eb.line && ea.column == eb.column);
            if (sa == SQLPARSER_STATUS_OK) same_view(a, b);
            else { CHECK(sqlparser_test_failed_handle(a)); CHECK(sqlparser_test_failed_handle(b)); }
            sqlparser_handle_destroy(a); sqlparser_handle_destroy(b); ++cases;
        }
    }
    stage = "structural materialization and source dependency reset the proof";
    {
        sqlparser_handle_t *a = parse(input), *b = parse(input);
        sqlparser_dialect_ops_t ops = *b->dialect_ops;
        sqlparser_patch_t p[] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[2].insert_cell[1][0]", .sql="'before'"},
            {.op=SQLPARSER_PATCH_DELETE_ROW, .selector="stmt[0].insert_row[0]"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'shifted'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[1].insert_cell[0][0]", .source_selector="stmt[0].insert_cell[0][0]"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[2].insert_cell[0][0]", .sql="'after'"}
        };
        b->dialect_ops = &ops;
        apply(a, p, COUNT(p)); apply(b, p, COUNT(p)); same_view(a, b);
        cell(a, 0U, 0U, 0U, "'shifted'"); cell(a, 1U, 0U, 0U, "'shifted'");
        cell(a, 2U, 0U, 0U, "'after'"); cell(a, 2U, 1U, 0U, "'before'");
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b); ++cases;
    }
}

static void errors_and_limits(void)
{
    static const char *bad[] = {"broken", "stmt[3].insert_cell[0][0]", "stmt[2].insert_cell[2][0]", "stmt[1].insert_cell[0][99]"};
    size_t i, limit;
    stage = "ordered errors, duplicate errors and byte-limit fallback";
    for (i = 0U; i < COUNT(bad) + 3U; i++) for (limit = 0U; limit < 3U; limit++) {
        sqlparser_handle_t *a = parse(input), *b = parse(input);
        sqlparser_dialect_ops_t ops = *b->dialect_ops;
        sqlparser_patch_t p[] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[2].insert_cell[0][0]", .sql="'new value'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'second'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'third'"}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        sqlparser_status_t sa, sb;
        sqlparser_error_t ea, eb;
        char *as = NULL, *bs = NULL;
        b->dialect_ops = &ops;
        if (i < COUNT(bad)) p[1].selector = bad[i];
        else if (i == COUNT(bad)) p[1].sql = "'unterminated";
        else if (i == COUNT(bad) + 1U) p[0].sql = "'unterminated";
        if (limit == 1U) a->limits.max_sql_bytes = b->limits.max_sql_bytes = strlen(input) + 1U;
        if (limit == 2U) a->limits.max_output_bytes = b->limits.max_output_bytes = strlen(input) + 1U;
        sa = sqlparser_apply_patch(a, &list, &ea); sb = sqlparser_apply_patch(b, &list, &eb);
        if (sa != sb || strcmp(ea.message, eb.message) != 0) fprintf(stderr,
            "error case=%zu limit=%zu actual=%d %s reference=%d %s\n", i, limit, sa, ea.message, sb, eb.message);
        CHECK(sa == sb); CHECK(strcmp(ea.message, eb.message) == 0);
        CHECK(ea.cursor == eb.cursor && ea.line == eb.line && ea.column == eb.column);
        CHECK(a->failed == b->failed);
        if (sa == SQLPARSER_STATUS_OK) {
            sa = sqlparser_deparse(a, &as, &ea); sb = sqlparser_deparse(b, &bs, &eb);
            CHECK(sa == sb); CHECK(strcmp(ea.message, eb.message) == 0);
            if (sa == SQLPARSER_STATUS_OK) {
                a->limits.max_output_bytes = b->limits.max_output_bytes = SIZE_MAX;
                same_view(a, b);
            }
        } else { CHECK(sqlparser_test_failed_handle(a)); CHECK(sqlparser_test_failed_handle(b)); }
        sqlparser_string_free(as); sqlparser_string_free(bs);
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b); ++cases;
    }
}

static void borrowed_inputs(void)
{
    sqlparser_handle_t *h = parse(input);
    sqlparser_literal_view_t borrowed;
    sqlparser_literal_value_t value = {0};
    sqlparser_patch_t p[2] = {{0}};
    stage = "snapshot borrowed AST strings before final materialization";
    CHECK(sqlparser_insert_cell_literal(h, 2U, 1U, 0U, &borrowed, &error) == SQLPARSER_STATUS_OK);
    value.kind = SQLPARSER_LITERAL_KIND_STRING; value.string_value = borrowed.string_value;
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&value};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[1].insert_cell[0][0]", .literal=&value};
    apply(h, p, COUNT(p));
    cell(h, 0U, 0U, 0U, "'张三李四'"); cell(h, 1U, 0U, 0U, "'张三李四'");
    sqlparser_handle_destroy(h); ++cases;
}

static void clone_and_failures(void)
{
    sqlparser_handle_t *h = parse(input), *clone = NULL;
    sqlparser_patch_t p[] = {
        {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[2].insert_cell[0][0]", .sql="'new value'"},
        {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[1][0]", .sql="'newer'"}
    };
    stage = "cloned handles have independent planning state";
    CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
    apply(clone, p, COUNT(p)); cell(h, 2U, 0U, 0U, "'four'");
    cell(clone, 2U, 0U, 0U, "'new value'");
    sqlparser_handle_destroy(clone); sqlparser_handle_destroy(h); ++cases;
#ifdef SQLPARSER_SQLSERVER_BATCH_WRAPPERS
    {
        size_t count = 0U, fault;
        sqlparser_patch_list_t list = {p, COUNT(p)};
        stage = "allocation failures clean up deferred edits and final reparse";
        /* First measure the completed route, then fault every reached outer
         * allocation. Every failure must leave a terminal, destructible handle. */
        for (fault = 0U; fault <= count; fault++) {
            sqlparser_status_t status;
            h = parse(input);
            CHECK(live == 0U); ledger_end = 0U;
            allocation_calls = 0U; failures = 0U; failure_at = fault;
            allocation_armed = 1;
            status = sqlparser_apply_patch(h, &list, &error);
            allocation_armed = 0;
            if (fault == 0U) { CHECK(status == SQLPARSER_STATUS_OK); count = allocation_calls; CHECK(count != 0U); }
            else {
                CHECK(failures == 1U);
                if (status != SQLPARSER_STATUS_OK) CHECK(sqlparser_test_failed_handle(h));
            }
            sqlparser_handle_destroy(h); CHECK(live == 0U); CHECK(native_depth == 0U); ++cases;
        }
    }
#endif
}

int main(void)
{
    static const sqlparser_dialect_t dialects[] = {SQLPARSER_DIALECT_SQLSERVER,
        SQLPARSER_DIALECT_VASTBASE_SQLSERVER, SQLPARSER_DIALECT_KINGBASE_SQLSERVER};
    size_t d;
    int typed, prebuild;
    stage = "initialization";
    for (d = 0U; d < COUNT(dialects); d++) {
        dialect = dialects[d];
        for (typed = 0; typed < 2; typed++) for (prebuild = 0; prebuild < 2; prebuild++) positive(typed, prebuild);
        fallback_and_mixed(); errors_and_limits(); borrowed_inputs(); clone_and_failures();
    }
    printf("SQLSERVER_INSERT_BATCH_PASSED cases=%zu\n", cases);
    return 0;
}
