/* Regression coverage for literal rebase. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "literal rebase [%s]:%d: %s: %s\n", case_name, __LINE__, #x, error.message); exit(1); } } while (0)
static const char *case_name = "initialization";
static sqlparser_error_t error;
static size_t cases;
/* Optional control run links this exact source against the reference archive. */
static int baseline_mode;
#ifdef SQLPARSER_LITERAL_REBASE_WRAP
static size_t reparses;
sqlparser_status_t __real_sqlparser_handle_reparse_destructive(sqlparser_handle_t *, char **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_handle_reparse_destructive(sqlparser_handle_t *h, char **sql, sqlparser_error_t *e)
{
    ++reparses;
    return __real_sqlparser_handle_reparse_destructive(h, sql, e);
}
#define RESET_REPARSES() (reparses = 0U)
#define CHECK_REPARSES(admitted) CHECK((admitted) ? reparses == 0U : reparses > 0U)
#else
#define RESET_REPARSES() ((void)0)
#define CHECK_REPARSES(admitted) ((void)(admitted))
#endif

static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_t options;
    sqlparser_parse_options_default(&options);
    options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    CHECK(h != NULL);
    CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK);
    CHECK(h->ast != NULL);
    return h;
}

static void same_ast(sqlparser_handle_t *left, sqlparser_handle_t *right)
{
    size_t a, b, first;
    unsigned char *x, *y;
    CHECK(sqlparser_handle_ensure_ast(left, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_ensure_ast(right, &error) == SQLPARSER_STATUS_OK);
    a = protobuf_c_message_get_packed_size(&left->ast->base);
    b = protobuf_c_message_get_packed_size(&right->ast->base);
    if (a != b) fprintf(stderr, "AST byte lengths: %zu versus %zu\n", a, b);
    CHECK(a == b);
    x = malloc(a ? a : 1U); y = malloc(b ? b : 1U);
    CHECK(x != NULL && y != NULL);
    CHECK(protobuf_c_message_pack(&left->ast->base, x) == a);
    CHECK(protobuf_c_message_pack(&right->ast->base, y) == b);
    if (memcmp(x, y, a) != 0) {
        for (first = 0U; first < a && x[first] == y[first]; ++first) {}
        fprintf(stderr, "AST first byte difference at %zu: %02x versus %02x\n", first, x[first], y[first]);
    }
    CHECK(memcmp(x, y, a) == 0);
    free(x); free(y);
}

static void same_public(sqlparser_handle_t *left, sqlparser_handle_t *right)
{
    sqlparser_bind_occurrence_view_t av, bv;
    size_t i;
    char *a = NULL, *b = NULL;
    CHECK(sqlparser_deparse(left, &a, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(right, &b, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(a, b) == 0);
    sqlparser_string_free(a); sqlparser_string_free(b); a = b = NULL;
    CHECK(sqlparser_export_view_json(left, 0, &a, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_export_view_json(right, 0, &b, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(a, b) == 0);
    sqlparser_string_free(a); sqlparser_string_free(b);
    CHECK(sqlparser_handle_bind_occurrences(left, &av, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_bind_occurrences(right, &bv, &error) == SQLPARSER_STATUS_OK);
    CHECK(av.count == bv.count);
    for (i = 0U; i < av.count; ++i) {
        sqlparser_bind_occurrence_t x, y;
        CHECK(sqlparser_bind_occurrence_at(&av, i, &x, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_bind_occurrence_at(&bv, i, &y, &error) == SQLPARSER_STATUS_OK);
        CHECK(x.position == y.position && x.kind == y.kind);
        CHECK((x.key == NULL) == (y.key == NULL));
        CHECK(x.key == NULL || strcmp(x.key, y.key) == 0);
        CHECK((x.sql == NULL) == (y.sql == NULL));
        CHECK(x.sql == NULL || strcmp(x.sql, y.sql) == 0);
    }
}

static void verify_result(sqlparser_handle_t *h, const char *expected, int admitted)
{
    sqlparser_handle_t *fresh, *clone = NULL;
    char *out = NULL, *snapshot;
    CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK);
    if (expected != NULL && strcmp(out, expected) != 0)
        fprintf(stderr, "expected: %s\nactual:   %s\n", expected, out);
    CHECK(expected == NULL || strcmp(out, expected) == 0);
    /* A retained AST alone does not prove that source locations were rebased. */
    if (strcmp(sqlparser_original_sql(h), out) != 0) fprintf(stderr, "original: %s\noutput:   %s\n", sqlparser_original_sql(h), out);
    CHECK(strcmp(sqlparser_original_sql(h), out) == 0);
    CHECK(h->surface_source_edits.count == 0U);
    CHECK(out != sqlparser_original_sql(h));
    snapshot = sqlparser_strdup(out); CHECK(snapshot != NULL);
    fresh = parse(out);
    same_ast(h, fresh);
    same_public(h, fresh);
    CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
    CHECK(clone != NULL && clone->ast != h->ast);
    same_ast(h, clone);
    same_public(h, clone);
    sqlparser_handle_destroy(clone);
    sqlparser_handle_destroy(fresh);
    CHECK(strcmp(out, snapshot) == 0);
    CHECK_REPARSES(admitted);
    free(snapshot); sqlparser_string_free(out);
}

static void apply_checked(sqlparser_handle_t *h, sqlparser_patch_t *patches,
    size_t count, const char *expected, int admitted)
{
    sqlparser_patch_list_t list = {patches, count}, empty = {NULL, 0U};
    sqlparser_query_graph_view_t old_graph, graph;
    PgQuery__ParseResult *ast = h->ast;
    unsigned long generation = h->generation;
    size_t expression_count;
    CHECK(sqlparser_statement_query_graph(h, 0U, &old_graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_count(&old_graph, &expression_count, &error) == SQLPARSER_STATUS_OK);
    admitted = admitted && !baseline_mode;
    RESET_REPARSES();
    CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
    CHECK(h->generation == generation + 1UL);
    if (admitted) CHECK(h->ast == ast);
    CHECK_REPARSES(admitted);
    CHECK(sqlparser_query_graph_expression_count(&old_graph, &expression_count, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    verify_result(h, expected, admitted);
    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    generation = h->generation; ast = h->ast;
    CHECK(sqlparser_apply_patch(h, &empty, &error) == SQLPARSER_STATUS_OK);
    CHECK(h->generation == generation && h->ast == ast);
    CHECK(sqlparser_query_graph_expression_count(&graph, &expression_count, &error) == SQLPARSER_STATUS_OK);
    CHECK_REPARSES(admitted);
    ++cases;
}

/* Resolve literal ordinals through the public API so tests cover the API's
 * traversal order without hard-coding CTE/DDL protobuf field ordering. */
static void literal_selector(sqlparser_handle_t *h, size_t statement,
    sqlparser_literal_kind_t kind, const char *text, long long number, char selector[80])
{
    size_t count, i, found = 0U, matched = 0U;
    CHECK(sqlparser_statement_literal_count(h, statement, &count, &error) == SQLPARSER_STATUS_OK);
    for (i = 0U; i < count; ++i) {
        sqlparser_literal_view_t value;
        CHECK(sqlparser_statement_literal(h, statement, i, &value, &error) == SQLPARSER_STATUS_OK);
        if (value.kind != kind) continue;
        if (kind == SQLPARSER_LITERAL_KIND_STRING ? value.string_value != NULL && strcmp(value.string_value, text) == 0 : value.integer_value == number) {
            found = i; ++matched;
        }
    }
    CHECK(matched == 1U);
    snprintf(selector, 80, "stmt[%zu].literal[%zu]", statement, found);
}

static void replace_one(const char *name, const char *source, size_t statement,
    sqlparser_literal_kind_t old_kind, const char *old_text, long long old_integer,
    sqlparser_literal_kind_t new_kind, const char *new_text, long long new_integer,
    const char *expected, int admitted)
{
    sqlparser_handle_t *h;
    sqlparser_literal_value_t value = {0};
    sqlparser_patch_t patch = {0};
    char selector[80], *input = sqlparser_strdup(source);
    case_name = name; CHECK(input != NULL);
    h = parse(input);
    literal_selector(h, statement, old_kind, old_text, old_integer, selector);
    value.kind = new_kind; value.string_value = new_text; value.integer_value = new_integer;
    patch.op = SQLPARSER_PATCH_REPLACE; patch.selector = selector; patch.literal = &value;
    apply_checked(h, &patch, 1U, expected, admitted);
    literal_selector(h, statement, new_kind, new_text, new_integer, selector);
    CHECK(strcmp(input, source) == 0);
    sqlparser_handle_destroy(h);
    CHECK(strcmp(input, source) == 0); free(input);
}

static void ordinary_strings(void)
{
    static const struct { const char *name, *sql, *old, *value, *expected; } rows[] = {
        {"join grow and following bind", "SELECT 'old' AS label, (SELECT r.id FROM rhs r WHERE r.id = $2) AS nested FROM lhs l JOIN rhs r ON l.id = r.id WHERE l.kind = $1 /*tail*/", "old", "a substantially longer label", "SELECT 'a substantially longer label' AS label, (SELECT r.id FROM rhs r WHERE r.id = $2) AS nested FROM lhs l JOIN rhs r ON l.id = r.id WHERE l.kind = $1 /*tail*/"},
        {"nested CTE shrink", "WITH outer_cte AS (WITH inner_cte AS (SELECT 'long original' AS value) SELECT value FROM inner_cte) SELECT value, (SELECT 17) AS nested FROM outer_cte", "long original", "x", "WITH outer_cte AS (WITH inner_cte AS (SELECT 'x' AS value) SELECT value FROM inner_cte) SELECT value, (SELECT 17) AS nested FROM outer_cte"},
        {"empty before alias and comments", "SELECT 'old' /*keep*/ AS label, (SELECT 17) AS nested; --tail\n", "old", "", "SELECT '' /*keep*/ AS label, (SELECT 17) AS nested; --tail\n"},
        {"quoted UTF-8", "SELECT 'old' AS label, (SELECT 17) AS nested FROM names WHERE id = $7", "old", "caf\xc3\xa9 'quoted' \xe6\x9d\xb1\xe4\xba\xac", "SELECT 'caf\xc3\xa9 ''quoted'' \xe6\x9d\xb1\xe4\xba\xac' AS label, (SELECT 17) AS nested FROM names WHERE id = $7"},
        {"update default expression", "UPDATE records SET label = 'old', score = (SELECT max(score) FROM history) WHERE id = $1", "old", "updated", "UPDATE records SET label = 'updated', score = (SELECT max(score) FROM history) WHERE id = $1"},
        {"delete nested filter", "DELETE FROM records WHERE label = 'old' AND id IN (SELECT id FROM history WHERE rank > 3)", "old", "gone", "DELETE FROM records WHERE label = 'gone' AND id IN (SELECT id FROM history WHERE rank > 3)"},
        {"DDL default", "CREATE TABLE sample (label text DEFAULT 'old', CHECK (length(label) > 2)); SELECT (SELECT 17)", "old", "new default", "CREATE TABLE sample (label text DEFAULT 'new default', CHECK (length(label) > 2)); SELECT (SELECT 17)"},
        {"DDL check string", "CREATE TABLE sample (label text CHECK (label <> 'old')); SELECT (SELECT 17);", "old", "", "CREATE TABLE sample (label text CHECK (label <> '')); SELECT (SELECT 17);"},
        {"SET string", "SET application_name TO 'old'; SELECT (SELECT 17)", "old", "worker", "SET application_name TO 'worker'; SELECT (SELECT 17)"},
        {"second statement final semicolon", "SELECT (SELECT 17); SELECT 'old' AS label, $1;", "old", "second", "SELECT (SELECT 17); SELECT 'second' AS label, $1;"},
        {"second statement no final semicolon", "SELECT (SELECT 17); SELECT 'old' AS label, $1", "old", "second", "SELECT (SELECT 17); SELECT 'second' AS label, $1"}
    };
    size_t i;
    for (i = 0U; i < COUNT(rows); ++i)
        replace_one(rows[i].name, rows[i].sql, i >= 9U ? 1U : 0U,
            SQLPARSER_LITERAL_KIND_STRING, rows[i].old, 0,
            SQLPARSER_LITERAL_KIND_STRING, rows[i].value, 0, rows[i].expected, 1);
}

static void ordinary_integers(void)
{
    /* Scalar subqueries alone do not populate the RHS-expression graph. The
     * abs() predicate forces the existing post-mutation materialization. */
    replace_one("integer grow", "SELECT 7 AS number, (SELECT 17) AS nested, $3 FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_INTEGER, NULL, 7, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 2147483647LL,
        "SELECT 2147483647 AS number, (SELECT 17) AS nested, $3 FROM rhs WHERE id = abs(17)", 1);
    replace_one("integer shrink and following statement", "SELECT 12345 AS number, (SELECT 17) AS nested FROM rhs WHERE id = abs(17); SELECT $3", 0,
        SQLPARSER_LITERAL_KIND_INTEGER, NULL, 12345, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 0,
        "SELECT 0 AS number, (SELECT 17) AS nested FROM rhs WHERE id = abs(17); SELECT $3", 1);
    replace_one("integer DDL check", "CREATE TABLE sample (score int CHECK (score > 77)); SELECT id FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_INTEGER, NULL, 77, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 10000,
        "CREATE TABLE sample (score int CHECK (score > 10000)); SELECT id FROM rhs WHERE id = abs(17)", 1);
}

static void sequential_and_borrowed(void)
{
    const char *source = "SELECT 'left' AS first, 'right' AS second, 7 AS number, (SELECT 17) AS nested, $1 FROM rhs WHERE id = abs(17)";
    sqlparser_handle_t *h;
    sqlparser_patch_t patches[3] = {{0}};
    sqlparser_literal_value_t values[3] = {{0}};
    sqlparser_literal_view_t borrowed;
    char left[80], right[80], integer[80];
    size_t i;
    case_name = "mixed deferred and applied, ordered duplicate";
    h = parse(source);
    literal_selector(h, 0U, SQLPARSER_LITERAL_KIND_STRING, "left", 0, left);
    literal_selector(h, 0U, SQLPARSER_LITERAL_KIND_STRING, "right", 0, right);
    literal_selector(h, 0U, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 7, integer);
    for (i = 0U; i < COUNT(patches); ++i) patches[i].op = SQLPARSER_PATCH_REPLACE;
    values[0].kind = SQLPARSER_LITERAL_KIND_STRING; values[0].string_value = "intermediate longer value";
    values[1].kind = SQLPARSER_LITERAL_KIND_INTEGER; values[1].integer_value = 123456;
    values[2].kind = SQLPARSER_LITERAL_KIND_STRING; values[2].string_value = "final";
    patches[0].selector = left; patches[0].literal = &values[0];
    patches[1].selector = integer; patches[1].literal = &values[1];
    patches[2].selector = left; patches[2].literal = &values[2];
    apply_checked(h, patches, 3U, "SELECT 'final' AS first, 'right' AS second, 123456 AS number, (SELECT 17) AS nested, $1 FROM rhs WHERE id = abs(17)", 1);
    sqlparser_handle_destroy(h);

    case_name = "borrowed literal snapshot across earlier materialization";
    h = parse(source);
    CHECK(sqlparser_statement_literal(h, 0U, 0U, &borrowed, &error) == SQLPARSER_STATUS_OK);
    CHECK(borrowed.kind == SQLPARSER_LITERAL_KIND_STRING && strcmp(borrowed.string_value, "left") == 0);
    values[0].string_value = "replacement that grows";
    values[2].string_value = borrowed.string_value;
    patches[0].selector = left; patches[0].literal = &values[0];
    patches[1].selector = integer; patches[1].literal = &values[1];
    patches[2].selector = right; patches[2].literal = &values[2]; patches[2].source_selector = NULL;
    apply_checked(h, patches, 3U, "SELECT 'replacement that grows' AS first, 'left' AS second, 123456 AS number, (SELECT 17) AS nested, $1 FROM rhs WHERE id = abs(17)", 1);
    /* Independent outputs and clones must outlive further source rebases. */
    {
        sqlparser_handle_t *old_clone = NULL, *fresh;
        char *old_output = NULL, *snapshot, *clone_output = NULL;
        CHECK(sqlparser_deparse(h, &old_output, &error) == SQLPARSER_STATUS_OK);
        snapshot = sqlparser_strdup(old_output); CHECK(snapshot != NULL);
        CHECK(sqlparser_handle_clone(h, &old_clone, &error) == SQLPARSER_STATUS_OK);
        /* Repeated mutation rebases already-shifted offsets, not initial SQL. */
        case_name = "second mutation on rebased handle";
        values[0].string_value = "";
        apply_checked(h, patches, 1U, "SELECT '' AS first, 'left' AS second, 123456 AS number, (SELECT 17) AS nested, $1 FROM rhs WHERE id = abs(17)", 1);
        sqlparser_handle_destroy(h);
        CHECK(strcmp(old_output, snapshot) == 0);
        CHECK(sqlparser_deparse(old_clone, &clone_output, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(clone_output, snapshot) == 0);
        fresh = parse(snapshot); same_ast(old_clone, fresh); same_public(old_clone, fresh);
        sqlparser_handle_destroy(fresh); sqlparser_handle_destroy(old_clone);
        sqlparser_string_free(old_output); sqlparser_string_free(clone_output); free(snapshot);
    }
}

static void sequential_source_copy(void)
{
    sqlparser_handle_t *h;
    sqlparser_patch_t patches[3] = {{0}};
    sqlparser_literal_value_t first = {0}, last = {0};
    case_name = "ordered duplicate and source selector";
    h = parse("INSERT INTO records(a,b,c) VALUES ('left','right',abs(17));");
    first.kind = last.kind = SQLPARSER_LITERAL_KIND_STRING;
    first.string_value = "intermediate"; last.string_value = "final";
    patches[0].op = patches[1].op = patches[2].op = SQLPARSER_PATCH_REPLACE;
    patches[0].selector = patches[1].selector = "stmt[0].insert_cell[0][0]";
    patches[0].literal = &first; patches[1].literal = &last;
    patches[2].selector = "stmt[0].insert_cell[0][1]";
    patches[2].source_selector = "stmt[0].insert_cell[0][0]";
    apply_checked(h, patches, 3U, "INSERT INTO records(a,b,c) VALUES ('final','final',abs(17));", 0);
    sqlparser_handle_destroy(h);
    h = parse("/* source */ INSERT INTO records(a,b,c) VALUES ('left','right',abs(17));");
    apply_checked(h, patches, 3U, "/* source */ INSERT INTO records(a,b,c) VALUES ('final','final',abs(17));", 1);
    sqlparser_handle_destroy(h);
}

static void conservative_fallbacks(void)
{
    replace_one("TypeName ancestor typmod", "SELECT CAST('value' AS varchar(7)), (SELECT 17) FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_INTEGER, NULL, 7, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 123,
        "SELECT CAST('value' AS varchar(123)), (SELECT 17) FROM rhs WHERE id = abs(17)", 0);
    replace_one("E string source", "SELECT E'old' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_STRING, "old", 0, SQLPARSER_LITERAL_KIND_STRING, "new", 0,
        "SELECT 'new' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    replace_one("dollar string source", "SELECT $tag$old$tag$ AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_STRING, "old", 0, SQLPARSER_LITERAL_KIND_STRING, "new", 0,
        "SELECT 'new' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    replace_one("backslash source", "SELECT 'a\\b' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_STRING, "a\\b", 0, SQLPARSER_LITERAL_KIND_STRING, "new", 0,
        "SELECT 'new' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    replace_one("backslash replacement", "SELECT 'old' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_STRING, "old", 0, SQLPARSER_LITERAL_KIND_STRING, "a\\b", 0,
        "SELECT 'a\\b' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    replace_one("adjacent newline strings", "SELECT 'old'\n' tail' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_STRING, "old tail", 0, SQLPARSER_LITERAL_KIND_STRING, "new", 0,
        "SELECT 'new' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    replace_one("negative source", "SELECT -7 AS number, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_INTEGER, NULL, -7, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 12,
        "SELECT 12 AS number, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    replace_one("negative replacement", "SELECT 7 AS number, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_INTEGER, NULL, 7, SQLPARSER_LITERAL_KIND_INTEGER, NULL, -12,
        "SELECT -12 AS number, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    replace_one("literal category change", "SELECT 'old' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0,
        SQLPARSER_LITERAL_KIND_STRING, "old", 0, SQLPARSER_LITERAL_KIND_INTEGER, NULL, 12,
        "SELECT 12 AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    {
        sqlparser_handle_t *h;
        sqlparser_patch_t patch = {0};
        case_name = "structural target replacement";
        h = parse("SELECT 'old' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)");
        patch.op = SQLPARSER_PATCH_REPLACE; patch.selector = "stmt[0].select_target[0][0]";
        patch.sql = "upper('new') AS label";
        apply_checked(h, &patch, 1U, "SELECT upper('new') AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
        sqlparser_handle_destroy(h);
    }
}

/* Function-pointer equivalence does not establish the registered owner's
 * identity. A copied operation table must retain the generic rebuild route. */
static void copied_owner_fallback(void)
{
    sqlparser_handle_t *h;
    sqlparser_dialect_ops_t copied_ops;
    sqlparser_patch_t patch = {0};
    sqlparser_literal_value_t value = {0};
    const sqlparser_dialect_ops_t *registered;
    case_name = "copied PostgreSQL operations force fallback";
    h = parse("SELECT 'old' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)");
    registered = sqlparser_dialect_get_ops(SQLPARSER_DIALECT_POSTGRESQL);
    CHECK(registered != NULL && h->dialect_ops == registered);
    copied_ops = *registered;
    h->dialect_ops = &copied_ops;
    value.kind = SQLPARSER_LITERAL_KIND_STRING;
    value.string_value = "a growing replacement";
    patch.op = SQLPARSER_PATCH_REPLACE;
    patch.selector = "stmt[0].literal[0]";
    patch.literal = &value;
    apply_checked(h, &patch, 1U,
        "SELECT 'a growing replacement' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", 0);
    sqlparser_handle_destroy(h);
}

/* Malformed encodings must reach the native path. The parser build's own
 * byte-string behavior remains authoritative; do not impose new rejection. */
static void invalid_utf8_fallbacks(void)
{
    static const char *values[] = {"bad\xff", "bad\xc0\xaf", "bad\xed\xa0\x80", "bad\xf4\x90\x80\x80", "bad\xe2\x82"};
    size_t i;
    for (i = 0U; i < COUNT(values); ++i) {
        sqlparser_handle_t *h, *fresh, *clone = NULL;
        sqlparser_patch_t patch = {0};
        sqlparser_literal_value_t value = {0};
        sqlparser_patch_list_t list = {&patch, 1U};
        sqlparser_error_t first_error;
        sqlparser_status_t first_status, second_status;
        char expected[256], *out = NULL, *first_json = NULL, *second_json = NULL;
        case_name = "invalid UTF-8 retains native fallback semantics";
        h = parse("SELECT 'old' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)");
        value.kind = SQLPARSER_LITERAL_KIND_STRING; value.string_value = values[i];
        patch.op = SQLPARSER_PATCH_REPLACE; patch.selector = "stmt[0].literal[0]"; patch.literal = &value;
        RESET_REPARSES();
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK_REPARSES(0);
        CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK);
        snprintf(expected, sizeof(expected), "SELECT '%s' AS label, (SELECT 17) AS nested FROM rhs WHERE id = abs(17)", values[i]);
        CHECK(strcmp(expected, out) == 0 && strcmp(sqlparser_original_sql(h), out) == 0);
        fresh = parse(out);
        same_ast(h, fresh);
        CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
        same_ast(h, clone);
        first_status = sqlparser_export_view_json(h, 0, &first_json, &first_error);
        second_status = sqlparser_export_view_json(fresh, 0, &second_json, &error);
        CHECK(first_status == second_status);
        if (first_status == SQLPARSER_STATUS_OK) CHECK(strcmp(first_json, second_json) == 0);
        else CHECK(first_error.code == error.code && strcmp(first_error.message, error.message) == 0);
        sqlparser_string_free(first_json); sqlparser_string_free(second_json); sqlparser_string_free(out);
        sqlparser_handle_destroy(clone); sqlparser_handle_destroy(fresh); sqlparser_handle_destroy(h); ++cases;
    }
}

int main(int argc, char **argv)
{
    CHECK(argc == 1 || (argc == 2 && strcmp(argv[1], "--baseline") == 0));
    baseline_mode = argc == 2;
    ordinary_strings();
    ordinary_integers();
    sequential_and_borrowed();
    sequential_source_copy();
    conservative_fallbacks();
    invalid_utf8_fallbacks();
    copied_owner_fallback();
    printf("Literal source rebase: %zu ordinary cases passed%s\n", cases,
#ifdef SQLPARSER_LITERAL_REBASE_WRAP
        " (wrapped rebuild route checked)"
#else
        " (build with SQLPARSER_LITERAL_REBASE_WRAP for rebuild counters)"
#endif
    );
    return 0;
}
