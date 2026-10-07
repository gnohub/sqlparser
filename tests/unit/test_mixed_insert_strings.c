/* Functional proof for ordinary string edits in general INSERT VALUES trees.
 * These are deliberately small cases, not a performance benchmark. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#ifdef SQLPARSER_MIXED_STRING_WRAPPERS
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "postgres.h"
#include "parser/parser.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

static sqlparser_error_t error;
static size_t cases, grammar_calls, commits, failure_index;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d case=%zu failure=%zu %s: %s\n", \
    __FILE__, __LINE__, cases, failure_index, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_MIXED_STRING_WRAPPERS
List *__real_raw_parser_with_options(const char *, RawParseMode, bool);
List *__wrap_raw_parser_with_options(const char *sql, RawParseMode mode, bool preserve)
{
    grammar_calls++;
    return __real_raw_parser_with_options(sql, mode, preserve);
}
sqlparser_status_t __real_sqlparser_handle_commit_certified_insert_strings(sqlparser_handle_t *, char **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_handle_commit_certified_insert_strings(sqlparser_handle_t *h, char **sql, sqlparser_error_t *e)
{
    commits++;
    return __real_sqlparser_handle_commit_certified_insert_strings(h, sql, e);
}
#define CHECK_NO_REPARSE(before) CHECK(grammar_calls == (before))
#define CHECK_COMMIT(before) CHECK(commits == (before) + 1U)
#else
#define CHECK_NO_REPARSE(before) ((void)(before))
#define CHECK_COMMIT(before) ((void)(before))
#endif

#ifdef SQLPARSER_MIXED_STRING_ALLOC_WRAPPERS
/* Track only allocations originating during apply. Frees/reallocations remain
 * visible afterwards so handle destruction must release every owned buffer.
 * Fixed test bookkeeping cannot create extra failure-injection boundaries. */
static int allocation_armed;
static size_t allocation_calls, allocation_failures, live_allocations;
static void *tracked[2048];
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);

static size_t tracked_slot(void *p)
{
    size_t i;
    if (p != NULL && live_allocations != 0U) for (i = 0U; i < sizeof(tracked) / sizeof(tracked[0]); i++)
        if (tracked[i] == p) return i;
    return sizeof(tracked) / sizeof(tracked[0]);
}

static void track(void *p)
{
    size_t i;
    if (p == NULL) return;
    for (i = 0U; i < sizeof(tracked) / sizeof(tracked[0]); i++) if (tracked[i] == NULL) {
        tracked[i] = p; live_allocations++; return;
    }
    abort();
}

static int reject_allocation(void)
{
    if (!allocation_armed) return 0;
    allocation_calls++;
    if (allocation_calls != failure_index) return 0;
    allocation_failures++;
    return 1;
}

void *__wrap_malloc(size_t n)
{
    void *p;
    if (reject_allocation()) return NULL;
    p = __real_malloc(n);
    if (allocation_armed) track(p);
    return p;
}

void *__wrap_calloc(size_t n, size_t size)
{
    void *p;
    if (reject_allocation()) return NULL;
    p = __real_calloc(n, size);
    if (allocation_armed) track(p);
    return p;
}

void *__wrap_realloc(void *p, size_t n)
{
    size_t slot = tracked_slot(p);
    void *result;
    if (reject_allocation()) return NULL;
    result = __real_realloc(p, n);
    if (result != NULL || n == 0U) {
        if (slot < sizeof(tracked) / sizeof(tracked[0])) {
            tracked[slot] = result;
            if (result == NULL) live_allocations--;
        } else if (allocation_armed) track(result);
    }
    return result;
}

void __wrap_free(void *p)
{
    size_t slot = tracked_slot(p);
    if (slot < sizeof(tracked) / sizeof(tracked[0])) {
        tracked[slot] = NULL; live_allocations--;
    }
    __real_free(p);
}
#endif

static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_default(&options);
    options.dialect = SQLPARSER_DIALECT_MYSQL;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    return h;
}

static char *copy(const char *s)
{
    char *out = malloc(strlen(s) + 1U);
    CHECK(out != NULL);
    strcpy(out, s);
    return out;
}

static void discard(char *s)
{
    if (s != NULL) { memset(s, 0xa7, strlen(s)); free(s); }
}

static void append(char *sql, size_t *length, const char *format, ...)
{
    int n;
    va_list args;
    va_start(args, format);
    n = vsnprintf(sql + *length, 8192U - *length, format, args);
    va_end(args);
    CHECK(n >= 0 && (size_t)n < 8192U - *length);
    *length += (size_t)n;
}

/* Qualified relation, unedited UTF-8, decimal and negative literals, SQL-value
 * functions and several different nested protobuf expression node kinds. */
static char *make_sql(const char *raw[3], size_t columns, int terminator)
{
    static const char *extra[] = {
        "'Ω中'", "12.50", "CURRENT_TIMESTAMP",
        "coalesce(lower('MiXeD'), concat('nested', 'Ω'))",
        "NULL", "TRUE", "-2147483649", "'untouched'",
        "CAST('123' AS DECIMAL(8,2))",
        "CASE WHEN 1=1 THEN 'yes' ELSE 'no' END",
        "ARRAY[1,2]", "ROW('r',42)"
    };
    char *sql = malloc(8192U);
    size_t length = 0U, row, column;
    CHECK(sql != NULL && columns >= 5U && columns <= 13U);
    append(sql, &length, "/* head */ INSERT INTO warehouse.mixed_values(");
    for (column = 0U; column < columns; column++)
        append(sql, &length, "%sc%zu", column ? "," : "", column);
    append(sql, &length, ") VALUES ");
    for (row = 0U; row < 3U; row++) {
        append(sql, &length, "%s(%s", row ? ", /* next */ " : "", raw[row]);
        for (column = 1U; column < columns; column++) append(sql, &length, ",%s", extra[column - 1U]);
        append(sql, &length, ")");
    }
    if (terminator) append(sql, &length, "; /* tail */\n");
    return sql;
}

static int same(const char *a, const char *b)
{
    return a == NULL || b == NULL ? a == b : strcmp(a, b) == 0;
}

/* Comparing native wire bytes checks every nested location and RawStmt length,
 * beyond the source/deparse/graph public checks below. */
static void compare(sqlparser_handle_t *h, sqlparser_handle_t *reference, size_t columns)
{
    sqlparser_query_graph_view_t graph, expected_graph;
    sqlparser_bind_occurrence_view_t binds, expected_binds;
    size_t i, count, expected_count, row, column;
    char *actual = NULL, *expected = NULL;
    CHECK(h->parse_tree.len == reference->parse_tree.len);
    CHECK(memcmp(h->parse_tree.data, reference->parse_tree.data, h->parse_tree.len) == 0);
    CHECK(sqlparser_deparse(h, &actual, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(reference, &expected, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(actual, expected) == 0);
    sqlparser_string_free(actual); sqlparser_string_free(expected);
    CHECK(sqlparser_export_view_json(h, 0U, &actual, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_export_view_json(reference, 0U, &expected, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(actual, expected) == 0);
    sqlparser_string_free(actual); sqlparser_string_free(expected);
    CHECK(sqlparser_handle_bind_occurrences(h, &binds, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_bind_occurrences(reference, &expected_binds, &error) == SQLPARSER_STATUS_OK);
    CHECK(binds.count == expected_binds.count && binds.count == 0U);
    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(reference, 0U, &expected_graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_count(&graph, &count, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_count(&expected_graph, &expected_count, &error) == SQLPARSER_STATUS_OK);
    CHECK(count == expected_count);
    for (i = 0U; i < count; i++) {
        sqlparser_graph_expression_t a, b;
        CHECK(sqlparser_query_graph_expression_at(&graph, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_expression_at(&expected_graph, i, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(a.kind == b.kind && a.arguments.count == b.arguments.count);
        CHECK(same(a.sql, b.sql) && same(a.name, b.name));
    }
    for (row = 0U; row < 3U; row++) for (column = 0U; column < columns; column++) {
        sqlparser_selector_t selector;
        char text[80];
        snprintf(text, sizeof(text), "stmt[0].insert_cell[%zu][%zu]", row, column);
        CHECK(sqlparser_selector_parse(text, &selector, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_selector_insert_cell_sql(h, &selector, &actual, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_selector_insert_cell_sql(reference, &selector, &expected, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(actual, expected) == 0);
        sqlparser_string_free(actual); sqlparser_string_free(expected);
    }
}

static void owned_batches(size_t columns, int typed, int terminator)
{
    const char *raw[3] = {"'原文Ω'", "'second'", "'last'"};
    static const char *replacements[3][3] = {
        {"expanded-?-$1-:name", "", "third-expanded"},
        {"x", "second-round-longer", "z"},
        {"x", "second-round-longer", "z"}
    };
    static const size_t order[] = {2U, 0U, 1U};
    char raw_storage[3][96], *input = make_sql(raw, columns, terminator);
    sqlparser_handle_t *h = parse(input);
    size_t round, i;
    cases++;
    discard(input);
    for (round = 0U; round < 3U; round++) {
        sqlparser_patch_t patches[3] = {{0}};
        sqlparser_literal_value_t literals[3] = {{0}};
        sqlparser_patch_list_t list = {patches, 3U};
        sqlparser_query_graph_view_t stale;
        sqlparser_graph_dml_t dml;
        sqlparser_handle_t *reference;
        size_t before_grammar, before_commits;
        unsigned long generation = h->generation;
        CHECK(sqlparser_statement_query_graph(h, 0U, &stale, &error) == SQLPARSER_STATUS_OK);
        for (i = 0U; i < 3U; i++) {
            size_t row = order[i];
            char selector[80];
            snprintf(selector, sizeof(selector), "stmt[0].insert_cell[%zu][0]", row);
            snprintf(raw_storage[row], sizeof(raw_storage[row]), "'%s'", replacements[round][row]);
            raw[row] = raw_storage[row];
            patches[i].op = SQLPARSER_PATCH_REPLACE;
            patches[i].selector = copy(selector);
            if (typed) {
                literals[i].kind = SQLPARSER_LITERAL_KIND_STRING;
                literals[i].string_value = copy(replacements[round][row]);
                patches[i].literal = &literals[i];
            } else patches[i].sql = copy(raw[row]);
        }
        before_grammar = grammar_calls; before_commits = commits;
        memset(&error, 0x5a, sizeof(error));
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK_NO_REPARSE(before_grammar);
        CHECK_COMMIT(before_commits);
        CHECK(error.code == SQLPARSER_STATUS_OK && error.message[0] == '\0');
        CHECK(h->generation == generation + 1UL);
        for (i = 0U; i < 3U; i++) {
            discard((char *)patches[i].selector);
            discard((char *)patches[i].sql);
            discard((char *)literals[i].string_value);
        }
        CHECK(sqlparser_query_graph_dml(&stale, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        if (round == 1U) {
            /* Also cover a second apply without materializing a graph or
             * deparsing between calls. It may use the ordinary fallback. */
            sqlparser_patch_t immediate = {.op=SQLPARSER_PATCH_REPLACE,
                .selector=copy("stmt[0].insert_cell[2][0]"), .sql=copy("'immediate'")};
            sqlparser_patch_list_t one = {&immediate, 1U};
            CHECK(sqlparser_apply_patch(h, &one, &error) == SQLPARSER_STATUS_OK);
            discard((char *)immediate.selector); discard((char *)immediate.sql);
            strcpy(raw_storage[2], "'immediate'");
        }
        input = make_sql(raw, columns, terminator);
        CHECK(strcmp(sqlparser_original_sql(h), input) == 0);
        reference = parse(input); discard(input);
        compare(h, reference, columns);
        sqlparser_handle_destroy(reference);
    }
    /* After shifted nested locations have survived three commits, mutate a
     * later expression through the unchanged public string selector API. */
    {
        sqlparser_handle_t *reference = parse(sqlparser_original_sql(h));
        sqlparser_patch_t patch = {.op=SQLPARSER_PATCH_REPLACE,
            .selector="stmt[0].insert_cell[2][4]", .sql="concat(upper('later'), 'Ω')"};
        sqlparser_patch_list_t list = {&patch, 1U};
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_apply_patch(reference, &list, &error) == SQLPARSER_STATUS_OK);
        compare(h, reference, columns);
        sqlparser_handle_destroy(reference);
    }
    sqlparser_handle_destroy(h);
}

static void fallback_cases(void)
{
    static const char *replacement_sql[] = {"'can''t'", "'Ω'", "'line\nline'"};
    static const char *replacement_value[] = {"can't", "Ω", "line\nline", "back\\slash"};
    size_t typed, i;
    for (typed = 0U; typed < 2U; typed++) for (i = 0U; i < (typed ? 4U : 3U); i++) {
        const char *raw[3] = {"'first'", "'second'", "'last'"};
        char *input = make_sql(raw, 9U, 1);
        sqlparser_handle_t *h = parse(input), *reference;
        sqlparser_query_graph_view_t graph;
        sqlparser_literal_value_t literal = {0};
        sqlparser_literal_view_t value;
        sqlparser_selector_t selector;
        sqlparser_patch_t patch = {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]"};
        sqlparser_patch_list_t list = {&patch, 1U};
        cases++; discard(input);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        if (typed) {
            literal.kind = SQLPARSER_LITERAL_KIND_STRING;
            literal.string_value = copy(replacement_value[i]); patch.literal = &literal;
        } else patch.sql = copy(replacement_sql[i]);
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        discard((char *)patch.sql); discard((char *)literal.string_value);
        CHECK(sqlparser_selector_parse(patch.selector, &selector, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_selector_insert_cell_literal(h, &selector, &value, &error) == SQLPARSER_STATUS_OK);
        CHECK(value.kind == SQLPARSER_LITERAL_KIND_STRING && strcmp(value.string_value, replacement_value[i]) == 0);
        reference = parse(sqlparser_original_sql(h));
        compare(h, reference, 9U);
        sqlparser_handle_destroy(reference); sqlparser_handle_destroy(h);
    }
}

static void invalid_fragments(void)
{
    static const char *invalid[] = {"'unterminated", "'valid' trailing junk", "'a', 'b'", "'a'; SELECT 1"};
    size_t i;
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        const char *raw[3] = {"'first'", "'second'", "'last'"};
        char *input = make_sql(raw, 9U, 1);
        sqlparser_handle_t *h = parse(input);
        sqlparser_query_graph_view_t graph;
        sqlparser_patch_t patches[2] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'valid'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[2][0]", .sql=invalid[i]}
        };
        sqlparser_patch_list_t list = {patches, 2U};
        cases++; discard(input);
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_apply_patch(h, &list, &error) != SQLPARSER_STATUS_OK);
        CHECK(error.code != SQLPARSER_STATUS_OK);
        sqlparser_handle_destroy(h);
    }
}

static void allocation_failure_sweep(void)
{
#ifdef SQLPARSER_MIXED_STRING_ALLOC_WRAPPERS
    const char *old_raw[3] = {"'原文Ω'", "'second'", "'last'"};
    const char *new_raw[3] = {"'grown-first-value'", "''", "'last-expanded'"};
    const char *values[3] = {"grown-first-value", "", "last-expanded"};
    char *source = make_sql(old_raw, 9U, 1), *expected = make_sql(new_raw, 9U, 1);
    sqlparser_handle_t *reference = parse(expected);
    size_t typed;
    for (typed = 0U; typed < 2U; typed++) {
        size_t count = 0U;
        cases++;
        for (failure_index = 0U; failure_index <= count; failure_index++) {
            sqlparser_handle_t *h = parse(source);
            sqlparser_query_graph_view_t graph;
            sqlparser_patch_t *patches = calloc(3U, sizeof(*patches));
            sqlparser_literal_value_t *literals = calloc(3U, sizeof(*literals));
            sqlparser_patch_list_t list = {patches, 3U};
            sqlparser_status_t status;
            size_t i, before_grammar, before_commits;
            CHECK(patches != NULL && literals != NULL && live_allocations == 0U);
            CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
            for (i = 0U; i < 3U; i++) {
                size_t row = 2U - i;
                char selector[80];
                snprintf(selector, sizeof(selector), "stmt[0].insert_cell[%zu][0]", row);
                patches[i].op = SQLPARSER_PATCH_REPLACE;
                patches[i].selector = copy(selector);
                if (typed) {
                    literals[i].kind = SQLPARSER_LITERAL_KIND_STRING;
                    literals[i].string_value = copy(values[row]);
                    patches[i].literal = &literals[i];
                } else patches[i].sql = copy(new_raw[row]);
            }
            before_grammar = grammar_calls; before_commits = commits;
            allocation_calls = allocation_failures = 0U;
            allocation_armed = 1;
            status = sqlparser_apply_patch(h, &list, &error);
            allocation_armed = 0;
            /* The caller releases every patch/string before inspecting output
             * or destroying a failed handle. Initial parse was never armed. */
            for (i = 0U; i < 3U; i++) {
                discard((char *)patches[i].selector); discard((char *)patches[i].sql);
                discard((char *)literals[i].string_value);
            }
            free(patches); free(literals);
            if (failure_index == 0U) {
                CHECK(status == SQLPARSER_STATUS_OK && allocation_failures == 0U);
                CHECK_COMMIT(before_commits);
                count = allocation_calls;
                CHECK(count > 0U);
            } else CHECK(allocation_failures == 1U);
            if (status == SQLPARSER_STATUS_OK) {
                CHECK_NO_REPARSE(before_grammar);
                CHECK(strcmp(sqlparser_original_sql(h), expected) == 0);
                compare(h, reference, 9U);
            } else {
                CHECK(status == SQLPARSER_STATUS_NO_MEMORY && error.code == SQLPARSER_STATUS_NO_MEMORY);
                CHECK(h->failed);
                /* No graph, deparse, or other read on a terminal handle. */
            }
            sqlparser_handle_destroy(h);
            CHECK(live_allocations == 0U);
        }
        printf("Mixed INSERT apply allocation sweep: typed=%zu boundaries=%zu, no retained allocations\n", typed, count);
    }
    failure_index = 0U;
    sqlparser_handle_destroy(reference); free(source); free(expected);
#endif
}

int main(void)
{
    static const size_t columns[] = {5U, 8U, 9U, 10U, 13U};
    size_t shape, typed, terminator;
    for (shape = 0U; shape < sizeof(columns) / sizeof(columns[0]); shape++)
        for (typed = 0U; typed < 2U; typed++) for (terminator = 0U; terminator < 2U; terminator++)
            owned_batches(columns[shape], (int)typed, (int)terminator);
    fallback_cases(); invalid_fragments(); allocation_failure_sweep();
    printf("Mixed INSERT string edits: %zu cases; wire/location, source, graph, ownership and fallback checks passed\n", cases);
    return 0;
}
