/* PostgreSQL identity preprocessing regression records. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sqlparser_internal.h"
#include "src/pg_query_observer.h"

#ifndef PG_DIALECT_SOURCE
#define PG_DIALECT_SOURCE "../../src/dialect/sqlparser_dialect_postgresql.c"
#endif
#include PG_DIALECT_SOURCE

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *current_case = "setup";
static size_t fixture_count;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "case=%s line=%d assertion=%s\n", current_case, __LINE__, #x); \
    exit(2); \
} } while (0)

enum expectation { OBSERVE = -1, INVALID = 0, LEGAL = 1 };

static void emit_bytes(const char *key, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    printf("%s %zu:", key, size);
    for (i = 0U; i < size; ++i) printf("%02x", (unsigned)p[i]);
    putchar('\n');
}

static void emit_text(const char *key, const char *text)
{
    if (text == NULL) printf("%s NULL\n", key);
    else emit_bytes(key, text, strlen(text));
}

static void emit_status(const char *key, sqlparser_status_t status,
                        const sqlparser_error_t *error)
{
    printf("%s status=%d", key, (int)status);
    if (error == NULL) { puts(" error=NULL"); return; }
    printf(" code=%d cursor=%d line=%d column=%d\n", (int)error->code,
           error->cursor, error->line, error->column);
    emit_text("error-message", error->message);
}

static void emit_state(const char *key, const void *opaque)
{
    const sqlparser_postgresql_state_t *state = opaque;
    const sqlparser_dialect_national_literals_t *n;
    size_t i;
    if (state == NULL) { printf("%s NULL\n", key); return; }
    n = &state->national_literals;
    printf("%s items=%d count=%zu capacity=%zu literal_count=%zu "
           "fragment_start=%zu fragment_literal_base=%zu\n", key,
           n->items != NULL, n->count, n->capacity, n->literal_count,
           n->fragment_start, n->fragment_literal_base);
    CHECK(n->count <= n->capacity);
    CHECK(n->count == 0U || n->items != NULL);
    for (i = 0U; i < n->count; ++i) {
        printf("national-item=%zu ordinal=%zu owner=%d\n", i,
               n->items[i].ordinal, n->items[i].owner != NULL);
        emit_text("national-sql", n->items[i].sql);
        emit_text("national-surface", n->items[i].surface_sql);
        /* Preprocessing and its direct state clones never bind an AST. */
        CHECK(n->items[i].owner == NULL);
    }
}

/* Canonical recursive map dump: represent a SOURCE/GENERATED interval in one
 * record; split only ambiguous intervals. This captures every output byte's
 * provenance without an unconditional long-input getter loop. */
static void emit_origin_range(const sqlparser_identifier_origin_map_t *map,
                              size_t start, size_t length)
{
    sqlparser_identifier_origin_t origin = {0};
    sqlparser_identifier_origin_kind_t kind;
    kind = sqlparser_identifier_origin_map_lookup(map, start, length, &origin);
    if (kind == SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN && length > 1U) {
        size_t half = length / 2U;
        emit_origin_range(map, start, half);
        emit_origin_range(map, start + half, length - half);
    } else {
        printf("origin start=%zu length=%zu kind=%d origin_kind=%d "
               "source=%zu source_length=%zu\n", start, length, (int)kind,
               (int)origin.kind, origin.source_offset, origin.source_length);
    }
}

static void emit_origin(const char *key,
                        const sqlparser_identifier_origin_map_t *map)
{
    size_t length = sqlparser_identifier_origin_map_output_length(map);
    printf("%s present=%d length=%zu\n", key, map != NULL, length);
    if (length != 0U) emit_origin_range(map, 0U, length);
}

static char *make_insert(size_t rows, size_t minimum_length,
                         const char *table, const char *columns,
                         const char *values, const char *tail)
{
    size_t capacity, at, i, length;
    char *sql;
    capacity = 64U + strlen(table) + strlen(columns) +
               rows * (strlen(values) + 3U) + strlen(tail) + minimum_length;
    sql = malloc(capacity);
    CHECK(sql != NULL);
    at = (size_t)snprintf(sql, capacity, "INSERT INTO %s (%s) VALUES ", table, columns);
    CHECK(at < capacity);
    for (i = 0U; i < rows; ++i) {
        int written = snprintf(sql + at, capacity - at, "%s(%s)",
                               i == 0U ? "" : ",", values);
        CHECK(written >= 0 && (size_t)written < capacity - at);
        at += (size_t)written;
    }
    length = strlen(tail);
    memcpy(sql + at, tail, length); at += length;
    while (at < minimum_length) sql[at++] = ' ';
    sql[at] = '\0';
    return sql;
}

static void emit_proof(const char *sql, int expected)
{
    PgQueryIdentityScalarInsertProof p = {0};
    int hit = pg_query_prove_mysql_identity_scalar_insert(sql, &p);
    printf("proof hit=%d source=%zu rows=%zu columns=%zu strings=%zu statement=%d\n",
           hit, p.source_length, p.row_count, p.column_count,
           p.string_count, p.statement_length);
    if (expected >= 0) CHECK(hit == expected);
    if (!hit) {
        CHECK(p.source_length == 0U && p.row_count == 0U &&
              p.column_count == 0U && p.string_count == 0U && p.statement_length == 0);
    }
}

static void preprocess(const char *sql, int with_origins, int nullable_error)
{
    sqlparser_limits_t limits;
    sqlparser_error_t error = {0};
    sqlparser_identifier_origin_map_t *origins = NULL, *origin_clone = NULL;
    void *state = NULL, *clone = NULL;
    char *out = NULL;
    sqlparser_status_t status;
    sqlparser_limits_default(&limits);
    printf("preprocess origins=%d nullable_error=%d\n", with_origins, nullable_error);
    if (with_origins) {
        CHECK(sqlparser_identifier_origin_map_new_identity(sql != NULL ? strlen(sql) : 0U,
              &origins, &error) == SQLPARSER_STATUS_OK);
        memset(&error, 0, sizeof(error));
        status = sqlparser_postgresql_preprocess_identifier_origins(sql, &limits,
                 &out, &state, origins, nullable_error ? NULL : &error);
    } else {
        status = sqlparser_postgresql_preprocess(sql, &limits, &out, &state,
                                                nullable_error ? NULL : &error);
    }
    emit_status("preprocess", status, nullable_error ? NULL : &error);
    emit_text("preprocess-output", out);
    emit_state("preprocess-state", state);
    if (sql != NULL) CHECK(status == SQLPARSER_STATUS_OK && out != NULL && state != NULL);
    else CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT && out == NULL && state == NULL);
    if (with_origins) {
        emit_origin("preprocess-origin", origins);
        memset(&error, 0, sizeof(error));
        status = sqlparser_identifier_origin_map_clone(origins, &origin_clone, &error);
        emit_status("origin-clone", status, &error);
        CHECK(status == SQLPARSER_STATUS_OK && origin_clone != NULL);
        emit_origin("origin-clone", origin_clone);
    }
    memset(&error, 0, sizeof(error));
    status = sqlparser_postgresql_clone_state(state, &clone, &error);
    emit_status("state-clone", status, &error);
    CHECK(status == SQLPARSER_STATUS_OK);
    emit_state("state-clone", clone);
    free(out);
    sqlparser_postgresql_state_destroy(state);
    /* The clone is used after its original has been destroyed. */
    emit_state("state-clone-after-source-destroy", clone);
    sqlparser_postgresql_state_destroy(clone);
    sqlparser_identifier_origin_map_destroy(origins);
    emit_origin("origin-clone-after-source-destroy", origin_clone);
    sqlparser_identifier_origin_map_destroy(origin_clone);
}

static void emit_literal(const char *key, const sqlparser_literal_view_t *v)
{
    printf("%s kind=%d integer=%lld boolean=%d quoted=%d\n", key,
           (int)v->kind, v->integer_value, v->boolean_value, v->quoted_identifier);
    emit_text("literal-string", v->string_value);
    emit_text("literal-float", v->float_value);
}

static void inspect_handle(sqlparser_handle_t *handle, const char *phase,
                           int full_graph, int strict)
{
    sqlparser_error_t error = {0};
    sqlparser_status_t status;
    char *text = NULL;
    size_t statement_count = sqlparser_statement_count(handle), si;
    printf("handle phase=%s dialect=%d statements=%zu\n", phase,
           (int)sqlparser_handle_dialect(handle), statement_count);
    emit_text("original-sql", sqlparser_original_sql(handle));
    /* Observe the complete parser output, without modifying private state. */
    emit_text("parser-sql", handle->parser_sql);
    emit_bytes("parse-tree", handle->parse_tree.data, handle->parse_tree.len);
    status = sqlparser_deparse(handle, &text, &error);
    emit_status("deparse", status, &error);
    emit_text("deparse-output", text);
    CHECK(!strict || (status == SQLPARSER_STATUS_OK && text != NULL));
    sqlparser_string_free(text);
    for (si = 0U; si < statement_count; ++si) {
        sqlparser_statement_kind_t kind = 0;
        sqlparser_query_graph_view_t graph = {0};
        size_t rows = 0U, columns = 0U;
        memset(&error, 0, sizeof(error));
        status = sqlparser_statement_kind(handle, si, &kind, &error);
        emit_status("statement-kind", status, &error);
        printf("statement index=%zu kind=%d\n", si, (int)kind);
        CHECK(!strict || status == SQLPARSER_STATUS_OK);
        memset(&error, 0, sizeof(error));
        status = sqlparser_statement_query_graph(handle, si, &graph, &error);
        emit_status("query-graph", status, &error);
        CHECK(!strict || status == SQLPARSER_STATUS_OK);
        printf("graph statement=%zu generation=%lu root=%zu has_root=%d blocks=%zu "
               "relations=%zu targets=%zu fields=%zu values=%zu sets=%zu "
               "predicates=%zu has_dml=%d branches=%zu\n", graph.statement_index,
               graph.generation, graph.root_block_index, graph.has_root_block,
               graph.block_count, graph.relation_count, graph.target_count,
               graph.field_count, graph.value_count, graph.set_count,
               graph.predicate_count, graph.has_dml, graph.dml_branch_count);
        if (full_graph && si == 0U) {
            text = NULL; memset(&error, 0, sizeof(error));
            status = sqlparser_export_view_json(handle, 0, &text, &error);
            emit_status("view-json", status, &error);
            emit_text("view-json-output", text);
            CHECK(!strict || (status == SQLPARSER_STATUS_OK && text != NULL));
            sqlparser_string_free(text);
        }
        memset(&error, 0, sizeof(error));
        status = sqlparser_insert_row_count(handle, si, &rows, &error);
        emit_status("insert-rows", status, &error);
        printf("rows=%zu\n", rows);
        if (status != SQLPARSER_STATUS_OK) continue;
        memset(&error, 0, sizeof(error));
        status = sqlparser_insert_column_count(handle, si, &columns, &error);
        emit_status("insert-columns", status, &error);
        printf("columns=%zu\n", columns);
        CHECK(!strict || status == SQLPARSER_STATUS_OK);
        /* Two endpoint reads cover lazy/read paths. Full graph JSON above
         * supplies the complete graph oracle; there is no per-cell getter sweep. */
        if (rows != 0U && columns != 0U) {
            size_t read;
            for (read = 0U; read < (rows == 1U && columns == 1U ? 1U : 2U); ++read) {
                size_t r = read == 0U ? 0U : rows - 1U;
                size_t c = read == 0U ? 0U : columns - 1U;
                sqlparser_literal_view_t literal = {0};
                printf("read row=%zu column=%zu\n", r, c);
                memset(&error, 0, sizeof(error));
                status = sqlparser_insert_cell_literal(handle, si, r, c, &literal, &error);
                emit_status("read-literal", status, &error);
                if (status == SQLPARSER_STATUS_OK) emit_literal("read-literal", &literal);
                text = NULL; memset(&error, 0, sizeof(error));
                status = sqlparser_insert_cell_sql(handle, si, r, c, &text, &error);
                emit_status("read-cell-sql", status, &error);
                emit_text("read-cell-sql-output", text);
                CHECK(!strict || (status == SQLPARSER_STATUS_OK && text != NULL));
                sqlparser_string_free(text);
            }
        }
    }
}

static void ordinary_patch(sqlparser_handle_t *handle, int typed)
{
    sqlparser_patch_t patch = {0};
    sqlparser_patch_list_t list = {0};
    sqlparser_literal_value_t literal = {0};
    sqlparser_error_t error = {0};
    sqlparser_status_t status;
    patch.op = SQLPARSER_PATCH_REPLACE;
    patch.selector = "stmt[0].insert_cell[0][0]";
    if (typed) {
        literal.kind = SQLPARSER_LITERAL_KIND_STRING;
        literal.string_value = "typed patch UTF8 \xc3\xa9 ' quote \\ slash";
        patch.literal = &literal;
    } else {
        patch.sql = "'ordinary patch''s value'";
    }
    list.items = &patch; list.count = 1U;
    status = sqlparser_apply_patch(handle, &list, &error);
    emit_status(typed ? "typed-patch" : "raw-patch", status, &error);
    CHECK(status == SQLPARSER_STATUS_OK);
    inspect_handle(handle, typed ? "after-typed-patch" : "after-raw-patch", 1, 1);
}

static void public_case(const char *sql, enum expectation expected, int lifecycle)
{
    static const sqlparser_dialect_t dialects[] = {
        SQLPARSER_DIALECT_POSTGRESQL,
        SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
        SQLPARSER_DIALECT_KINGBASE_POSTGRESQL
    };
    size_t di;
    for (di = 0U; di < COUNT(dialects); ++di) {
        sqlparser_parse_options_t options;
        sqlparser_handle_t *handle = NULL, *clone = NULL;
        sqlparser_error_t error = {0};
        sqlparser_status_t status;
        sqlparser_parse_options_default(&options);
        options.dialect = dialects[di];
        printf("public dialect=%d expectation=%d lifecycle=%d\n",
               (int)options.dialect, (int)expected, lifecycle);
        status = sqlparser_parse_with_options(sql, &options, &handle, &error);
        emit_status("parse", status, &error);
        printf("parse-handle=%d\n", handle != NULL);
        if (expected == LEGAL) CHECK(status == SQLPARSER_STATUS_OK && handle != NULL);
        if (expected == INVALID) CHECK(status != SQLPARSER_STATUS_OK && handle == NULL);
        if (status == SQLPARSER_STATUS_OK) {
            CHECK(handle != NULL);
            inspect_handle(handle, "parsed", 1, expected == LEGAL);
            if (lifecycle) {
                memset(&error, 0, sizeof(error));
                status = sqlparser_handle_clone(handle, &clone, &error);
                emit_status("handle-clone", status, &error);
                CHECK(status == SQLPARSER_STATUS_OK && clone != NULL);
                inspect_handle(clone, "cloned", 1, 1);
                ordinary_patch(handle, 0);
                /* Independent clone survives source mutation and destruction. */
                sqlparser_handle_destroy(handle); handle = NULL;
                inspect_handle(clone, "clone-after-source-destroy", 0, 1);
                ordinary_patch(clone, 1);
            }
        } else CHECK(handle == NULL);
        sqlparser_handle_destroy(handle);
        sqlparser_handle_destroy(clone);
        if (expected == INVALID || lifecycle) {
            handle = NULL;
            status = sqlparser_parse_with_options(sql, &options, &handle, NULL);
            emit_status("parse-null-error", status, NULL);
            printf("parse-null-error-handle=%d\n", handle != NULL);
            if (expected == LEGAL) CHECK(status == SQLPARSER_STATUS_OK && handle != NULL);
            if (expected == INVALID) CHECK(status != SQLPARSER_STATUS_OK && handle == NULL);
            sqlparser_handle_destroy(handle);
        }
    }
}

static void run_case(const char *name, const char *sql, int expected_proof,
                     enum expectation expected_parse, int lifecycle, int origin)
{
    current_case = name;
    printf("CASE %s\n", name);
    emit_text("input", sql);
    emit_proof(sql, expected_proof);
    preprocess(sql, 0, 0);
    if (origin) preprocess(sql, 1, 0);
    if (lifecycle || sql == NULL) preprocess(sql, 0, 1);
    public_case(sql, expected_parse, lifecycle);
    ++fixture_count;
}

static void generated_case(const char *name, size_t rows, size_t length,
                           const char *table, const char *columns,
                           const char *values, const char *tail,
                           int proof, enum expectation parse, int lifecycle, int origin)
{
    char *sql;
    current_case = name;
    sql = make_insert(rows, length, table, columns, values, tail);
    run_case(name, sql, proof, parse, lifecycle, origin);
    free(sql);
}

static void fragment_sequence(void)
{
    static const char *fragments[] = {
        "'plain'", "N'national fragment'", "E'a\\nb'", "U&'d\\0061t'",
        "$$dollar ' literal$$", "'one''two'", "'back\\slash'", "CURRENT_TIMESTAMP(6)"
    };
    void *state = NULL, *clone = NULL;
    char *out = NULL, *bulk;
    sqlparser_error_t error = {0};
    sqlparser_status_t status;
    size_t i, before;
    current_case = "fragment-accumulation";
    puts("CASE fragment-accumulation");
    status = sqlparser_postgresql_preprocess("SELECT 'plain', N'initial'", NULL,
                                           &out, &state, &error);
    emit_status("fragment-seed", status, &error);
    CHECK(status == SQLPARSER_STATUS_OK && state != NULL);
    emit_text("fragment-seed-output", out); free(out); out = NULL;
    emit_state("fragment-seed-state", state);
    bulk = make_insert(32U, 4096U, "s.t", "a,b,c", "'bulk',1,CURRENT_TIMESTAMP", ";");
    /* A complete proof-hit SQL used as a real fragment stresses +=, rather
     * than accidental assignment, with already nonzero real state counters. */
    before = ((sqlparser_postgresql_state_t *)state)->national_literals.literal_count;
    memset(&error, 0, sizeof(error));
    status = sqlparser_postgresql_preprocess_fragment(bulk, state, 0U, &out, &error);
    emit_status("fragment-bulk", status, &error);
    CHECK(status == SQLPARSER_STATUS_OK);
    CHECK(((sqlparser_postgresql_state_t *)state)->national_literals.literal_count == before + 32U);
    emit_text("fragment-bulk-output", out); free(out); out = NULL;
    emit_state("fragment-bulk-state", state);
    for (i = 0U; i < COUNT(fragments); ++i) {
        memset(&error, 0, sizeof(error));
        printf("fragment index=%zu\n", i);
        status = sqlparser_postgresql_preprocess_fragment(fragments[i], state, i,
                                                         &out, &error);
        emit_status("fragment", status, &error);
        CHECK(status == SQLPARSER_STATUS_OK);
        emit_text("fragment-output", out); free(out); out = NULL;
        emit_state("fragment-state", state);
    }
    memset(&error, 0, sizeof(error));
    status = sqlparser_postgresql_clone_state(state, &clone, &error);
    emit_status("fragment-state-clone", status, &error);
    CHECK(status == SQLPARSER_STATUS_OK && clone != NULL);
    emit_state("fragment-clone", clone);
    sqlparser_postgresql_state_destroy(state);
    before = ((sqlparser_postgresql_state_t *)clone)->national_literals.literal_count;
    status = sqlparser_postgresql_preprocess_fragment(bulk, clone, 1U, &out, NULL);
    emit_status("fragment-clone-bulk-null-error", status, NULL);
    CHECK(status == SQLPARSER_STATUS_OK);
    CHECK(((sqlparser_postgresql_state_t *)clone)->national_literals.literal_count == before + 32U);
    emit_text("fragment-clone-bulk-output", out); free(out);
    emit_state("fragment-clone-final", clone);
    sqlparser_postgresql_state_destroy(clone); free(bulk);
    ++fixture_count;
}

static void invalid_arguments(void)
{
    int nullable, mode;
    current_case = "invalid-arguments";
    puts("CASE invalid-arguments");
    for (nullable = 0; nullable < 2; ++nullable) {
        for (mode = 0; mode < 5; ++mode) {
            char *out = NULL;
            void *state = NULL;
            sqlparser_error_t error = {0};
            sqlparser_error_t *ep = nullable ? NULL : &error;
            sqlparser_status_t status;
            printf("arguments nullable=%d mode=%d\n", nullable, mode);
            if (mode == 0)
                status = sqlparser_postgresql_preprocess("SELECT 1", NULL, NULL, &state, ep);
            else if (mode == 1)
                status = sqlparser_postgresql_preprocess("SELECT 1", NULL, &out, NULL, ep);
            else if (mode == 2)
                status = sqlparser_postgresql_preprocess_identifier_origins("SELECT 1", NULL,
                                                                          &out, &state, NULL, ep);
            else if (mode == 3)
                status = sqlparser_postgresql_preprocess_fragment("'x'", NULL, 0U, &out, ep);
            else
                status = sqlparser_postgresql_clone_state(NULL, NULL, ep);
            emit_status("invalid-argument", status, ep);
            CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT && out == NULL && state == NULL);
            emit_text("invalid-output", out); emit_state("invalid-state", state);
        }
        {
            sqlparser_postgresql_state_t *state = NULL;
            sqlparser_error_t error = {0};
            sqlparser_status_t status;
            char *out = NULL;
            CHECK(sqlparser_postgresql_state_new(&state, &error) == SQLPARSER_STATUS_OK);
            memset(&error, 0, sizeof(error));
            status = sqlparser_postgresql_preprocess_fragment(NULL, state, 0U, &out,
                                                             nullable ? NULL : &error);
            emit_status("fragment-null-input", status, nullable ? NULL : &error);
            CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT && out == NULL);
            emit_state("fragment-null-state", state);
            status = sqlparser_postgresql_preprocess_fragment("'x'", state, 0U, NULL,
                                                             nullable ? NULL : &error);
            emit_status("fragment-null-output", status, nullable ? NULL : &error);
            CHECK(status == SQLPARSER_STATUS_INVALID_ARGUMENT);
            emit_state("fragment-null-output-state", state);
            sqlparser_postgresql_state_destroy(state);
        }
    }
    ++fixture_count;
}

int main(void)
{
    static const char *original_columns =
        "STAT_DATE,TEACHER_ID,TEACHER_NAME_ENCRYPT,PHONE_ENCRYPT,TOTAL_TEACHING,"
        "TOTAL_HOURS,CHECK_STATUS,CREATE_TIME,UPDATE_TIME";
    static const char *original_values =
        "'202505','T1001','\xe5\xbc\xa0\xe4\xb8\x89\xe6\x9d\x8e\xe5\x9b\x9b',"
        "'13800138000',20,100.50,0,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP";
    static const struct {
        const char *name, *values;
        int proof;
        enum expectation parse;
        int lifecycle, origin;
    } rows[] = {
        {"numeric-integer", "'x',2147483648,-2147483648", 1, LEGAL, 0, 0},
        {"numeric-spaced-sign", "'x',- 7,- .5E+3", 1, LEGAL, 0, 0},
        {"numeric-float", "'x',100.50,1.e-2", 1, LEGAL, 0, 0},
        {"sql-values", "'x',CURRENT_TIME(6),SESSION_USER", 1, LEGAL, 0, 0},
        {"sql-value-names", "'x',CURRENT_CATALOG,CURRENT_SCHEMA", 1, LEGAL, 0, 0},
        {"strings-empty", "'','',''", 1, LEGAL, 0, 0},
        {"strings-utf8", "'UTF8 \xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80',1,CURRENT_DATE", 1, LEGAL, 1, 1},
        {"strings-punctuation", "'N E U& $$ -- /* */ ; ` quoted \" ? auto_increment',1,2", 1, LEGAL, 0, 0},
        {"doubled-quote", "'one''two',1,2", 0, LEGAL, 1, 1},
        {"backslash", "'back\\slash',1,2", 0, LEGAL, 0, 0},
        {"literal-newline", "'line\nline',1,2", 0, LEGAL, 0, 0},
        {"literal-tab-control", "'tab\tcontrol\001',1,2", 0, LEGAL, 0, 0},
        {"national-string", "N'national',1,2", 0, LEGAL, 1, 1},
        {"escape-string", "E'a\\nb',1,2", 0, LEGAL, 0, 1},
        {"unicode-string", "U&'d\\0061t',1,2", 0, LEGAL, 0, 1},
        {"dollar-string", "$$a ' b$$,1,2", 0, LEGAL, 0, 1},
        {"tagged-dollar-string", "$tag$a '' b$tag$,1,2", 0, LEGAL, 0, 0},
        {"null-default-boolean", "NULL,DEFAULT,TRUE", 0, LEGAL, 0, 0},
        {"expression", "'x',1+2,CURRENT_TIMESTAMP()", 0, INVALID, 0, 0},
        {"expression-legal", "'x',1+2,3", 0, LEGAL, 0, 0},
        {"comment-in-row", "'x',/* N'ignored' */1,2", 0, LEGAL, 0, 1},
        {"string-concatenation", "'one'\n 'two',1,2", 0, LEGAL, 0, 0},
        /* Invalid UTF-8 must miss the proof. Public behavior is deliberately
         * observed, not assumed: the baseline API may not validate encoding. */
        {"utf8-stray-continuation", "'\x80',1,2", 0, OBSERVE, 0, 0},
        {"utf8-overlong", "'\xc0\x80',1,2", 0, OBSERVE, 0, 0},
        {"utf8-surrogate", "'\xed\xa0\x80',1,2", 0, OBSERVE, 0, 0},
        {"utf8-out-of-range", "'\xf4\x90\x80\x80',1,2", 0, OBSERVE, 0, 0},
        {"utf8-truncated", "'\xe2\x82',1,2", 0, OBSERVE, 0, 0},
        {"unterminated-string", "'unterminated,1,2", 0, INVALID, 0, 0}
    };
    static const struct { const char *name, *tail; int proof; enum expectation parse; } tails[] = {
        {"tail-semicolon", "; \t\n", 1, LEGAL},
        {"tail-two-semicolons", ";;", 0, LEGAL},
        {"tail-line-comment", " -- trailing N'comment'\n", 0, LEGAL},
        {"tail-block-comment", " /* trailing 'comment' */", 0, LEGAL},
        {"tail-nested-comment", " /* outer /* inner */ outer */", 0, LEGAL},
        {"tail-returning", " RETURNING a", 0, LEGAL},
        {"tail-second-statement", "; SELECT 1", 0, LEGAL},
        {"tail-invalid", " nonsense", 0, INVALID},
        {"tail-invalid-after-semicolon", "; BOGUS", 0, INVALID},
        {"tail-unclosed-comment", " /* unclosed", 0, INVALID}
    };
    static const struct { const char *name, *table, *columns; int proof; enum expectation parse; } names[] = {
        {"mixed-case-names", "Db.Sch.Tab", "A,b,C", 1, LEGAL},
        {"quoted-names", "\"Sch\".\"Ta\"\"ble\"", "\"A\",b,c", 0, LEGAL},
        {"utf8-name", "s.\xe8\xa1\xa8", "a,b,c", 0, LEGAL},
        {"mysql-excluded-table", "s.unsigned", "a,b,c", 0, LEGAL},
        {"mysql-excluded-column", "s.t", "a,auto_increment,c", 0, LEGAL},
        {"backtick-name", "`s`.`t`", "a,b,c", 0, INVALID}
    };
    size_t i, r, n;
    generated_case("original-like-32", 32U, 4096U, "TEST_LIB.TEACHER_STATISTICS",
                   original_columns, original_values, "", 1, LEGAL, 1, 1);
    generated_case("original-like-64", 64U, 4096U, "TEST_LIB.TEACHER_STATISTICS",
                   original_columns, original_values, ";", 1, LEGAL, 1, 0);
    for (r = 31U; r <= 32U; ++r) {
        for (n = 4095U; n <= 4096U; ++n) {
            char name[80];
            char *sql = make_insert(r, n, "s.t", "a,b,c", "'x',1,CURRENT_DATE", "");
            snprintf(name, sizeof(name), "boundary-rows-%zu-bytes-%zu", r, n);
            CHECK(strlen(sql) == n);
            run_case(name, sql, r >= 32U && n >= 4096U, LEGAL, 0, 1);
            free(sql);
        }
    }
    for (i = 0U; i < COUNT(rows); ++i)
        generated_case(rows[i].name, 32U, 4096U, "s.t", "a,b,c", rows[i].values, "",
                       rows[i].proof, rows[i].parse, rows[i].lifecycle, rows[i].origin);
    for (i = 0U; i < COUNT(tails); ++i)
        generated_case(tails[i].name, 32U, 4096U, "s.t", "a,b,c", "'x',1,CURRENT_DATE",
                       tails[i].tail, tails[i].proof, tails[i].parse, 0, 0);
    for (i = 0U; i < COUNT(names); ++i)
        generated_case(names[i].name, 32U, 4096U, names[i].table, names[i].columns,
                       "'x',1,CURRENT_DATE", "", names[i].proof, names[i].parse, 0, 1);
    {
        char long_name[161];
        memset(long_name, 'q', sizeof(long_name) - 1U); long_name[160] = '\0';
        generated_case("long-name", 32U, 4096U, long_name, "a,b,c", "'x',1,CURRENT_DATE",
                       "", 1, LEGAL, 0, 0);
    }
    generated_case("longer-input-256", 256U, 32768U, "s.t", "a,b,c",
                   "'longer payload UTF8 \xc3\xa9',100.50,CURRENT_TIMESTAMP", ";", 1, LEGAL, 0, 1);
    run_case("short-legal", "INSERT INTO t(a) VALUES ('x')", 0, LEGAL, 1, 1);
    run_case("short-invalid", "INSERT INTO t(a) VALUES (", 0, INVALID, 0, 1);
    run_case("empty-input", "", 0, OBSERVE, 0, 1);
    run_case("null-input", NULL, 0, INVALID, 0, 1);
    run_case("leading-comment", "/* N'ignored' */ INSERT INTO t(a) VALUES ('x')",
             0, LEGAL, 0, 1);
    generated_case("tail-line-comment-close-paren", 256U, 4096U, "s.t", "a,b,c",
                   "'x',1,CURRENT_DATE", " -- comment ends )\n", 0, LEGAL, 0, 0);
    generated_case("tail-line-comment-semicolon", 256U, 4096U, "s.t", "a,b,c",
                   "'x',1,CURRENT_DATE", " -- comment ends ;\n", 0, LEGAL, 0, 0);
    run_case("only-scanner-whitespace", " \t\n\r\v\f", 0, OBSERVE, 0, 1);
    for (i = 0U; i < 6U; ++i) {
        const char ws[] = " \t\n\r\v\f";
        char tail[4] = {';', ws[i], ws[i], 0};
        char name[80]; snprintf(name, sizeof(name), "semicolon-scanner-space-%zu", i);
        generated_case(name, 32U, 4096U, "s.t", "a,b,c", "'x',1,CURRENT_DATE",
                       tail, 1, LEGAL, 0, 0);
    }
    {
        char *sql = make_insert(32U, 4096U, "s.t", "a,b,c", "'x',1,CURRENT_DATE", "");
        char *last = strrchr(sql, '('), *digit; char *expression;
        size_t offset, length = strlen(sql);
        CHECK(last != NULL); digit = strstr(last, ",1,"); CHECK(digit != NULL);
        offset = (size_t)(digit + 2 - sql);
        expression = malloc(length + 3U); CHECK(expression != NULL);
        memcpy(expression, sql, offset); memcpy(expression + offset, "+0", 2U);
        memcpy(expression + offset + 2U, sql + offset, length - offset + 1U);
        run_case("last-cell-expression-miss", expression, 0, LEGAL, 0, 0);
        free(expression); free(sql);
    }
    {
        size_t blanks;
        for (blanks = 63U; blanks <= 65U; ++blanks) {
            char tail[100], name[80];
            strcpy(tail, " -- )"); memset(tail + 5U, ' ', blanks); tail[5U + blanks] = 0;
            snprintf(name, sizeof(name), "comment-parenthesis-tail-%zu-spaces", blanks);
            generated_case(name, 256U, 4096U, "s.t", "a,b,c", "'x',1,CURRENT_DATE",
                           tail, 0, LEGAL, 0, 0);
        }
    }
    fragment_sequence();
    invalid_arguments();
    pg_query_exit();
    printf("DONE fixtures=%zu ordinary-only=yes\n", fixture_count);
    CHECK(!ferror(stdout));
    return 0;
}
