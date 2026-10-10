/* Certified lazy Oracle origins: complete graph/state records against a
 * separately parsed fallback handle, every origin span against independent
 * preprocessing, exact-owner admission, lifecycle and allocation failures.
 * --record is byte-compared to this caller linked to the reference
 * archive. Reuse only the existing complete record and allocation machinery;
 * none of its old main's scenarios is substituted for these checks. */
#define main sqlparser_oracle_commit_test_main
#include "test_oracle_owned_commit.c"
#undef main

static size_t origin_attempts, origin_hits, origin_legacy_calls;
static int origin_observe;
#ifdef SQLPARSER_ORIGIN_CERT_WRAPPERS
#ifndef SQLPARSER_ORIGIN_CERT_BASELINE
sqlparser_status_t __real_sqlparser_oracle_try_replay_certified_multi_insert_origins(
    const sqlparser_handle_t *, sqlparser_identifier_origin_map_t **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_oracle_try_replay_certified_multi_insert_origins(
    const sqlparser_handle_t *h, sqlparser_identifier_origin_map_t **origins,
    sqlparser_error_t *e)
{
    sqlparser_status_t status;
    if (origin_observe) ++origin_attempts;
    status = __real_sqlparser_oracle_try_replay_certified_multi_insert_origins(h, origins, e);
    if (origin_observe && status == SQLPARSER_STATUS_OK && origins && *origins) ++origin_hits;
    return status;
}
#endif
sqlparser_status_t __real_sqlparser_oracle_replay_identifier_origins(
    const char *, const char *, const void *, sqlparser_identifier_origin_map_t *, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_oracle_replay_identifier_origins(
    const char *sql, const char *parser, const void *state,
    sqlparser_identifier_origin_map_t *origins, sqlparser_error_t *e)
{
    if (origin_observe) ++origin_legacy_calls;
    return __real_sqlparser_oracle_replay_identifier_origins(sql, parser, state, origins, e);
}
#endif

static void origin_drop(sqlparser_handle_t *h)
{
    sqlparser_identifier_origin_map_destroy(h->identifier_origins);
    h->identifier_origins = NULL;
}

static void origin_verify(sqlparser_handle_t *h, const char *expected, int expect_hit)
{
    sqlparser_handle_t *reference = parse(h->dialect, expected);
    char *actual_record, *reference_record, *actual_sql = NULL, *reference_sql = NULL;
    const void *state = h->dialect_state;
    const char *source = h->sql, *parser = h->parser_sql, *wire = h->parse_tree.data;
    size_t wire_length = h->parse_tree.len;
    unsigned long generation = h->generation;
    /* The reference must use the old lazy replay, never the new proof route. */
    sqlparser_oracle_multi_insert_invalidate_source(reference);
    origin_drop(h);
    sqlparser_query_graph_cache_release(h->query_graph);
    h->query_graph = NULL;
    origin_attempts = origin_hits = origin_legacy_calls = 0U;
    origin_observe = 1;
    actual_record = graph_record(h, 1);
    origin_observe = 0;
#if defined(SQLPARSER_ORIGIN_CERT_WRAPPERS) && !defined(SQLPARSER_ORIGIN_CERT_BASELINE)
    CHECK(origin_attempts == 1U && origin_hits == (size_t)expect_hit);
    CHECK(origin_legacy_calls == (size_t)!expect_hit);
#else
    (void)expect_hit;
#endif
    CHECK(h->dialect_state == state && h->sql == source && h->parser_sql == parser);
    CHECK(h->parse_tree.data == wire && h->parse_tree.len == wire_length && h->generation == generation);
    reference_record = graph_record(reference, 1);
    same_text(actual_record, reference_record);
    compare_origins(h, reference);
    CHECK(sqlparser_deparse(h, &actual_sql, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(reference, &reference_sql, &error) == SQLPARSER_STATUS_OK);
    same_text(actual_sql, reference_sql);
    if (recording) printf("case=%zu;dialect=%s;generation=%lu;sql=%zu:%s;graph_state=%s\n",
        case_number, sqlparser_dialect_name(h->dialect), generation,
        strlen(actual_sql), actual_sql, actual_record);
    free(actual_record); free(reference_record); free(actual_sql); free(reference_sql);
    sqlparser_handle_destroy(reference);
}

static void origin_fixture(sqlparser_dialect_t dialect, const char *sql)
{
    sqlparser_handle_t *h = parse(dialect, sql), *clone = NULL;
    ++case_number; stage = "initial graph uses constructor certificate";
    CHECK(sqlparser_oracle_multi_insert_source_is_current(h));
    CHECK(multi(h)->oracle_outer_identity && h->identifier_origins == NULL);
    origin_verify(h, sql, 1);
    sqlparser_handle_destroy(h);
    h = parse(dialect, sql);
    CHECK(h->identifier_origins == NULL);
    CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_oracle_multi_insert_source_is_current(clone));
    sqlparser_handle_destroy(h);
    stage = "cold clone after original destruction";
    origin_verify(clone, sql, 1);
    sqlparser_handle_destroy(clone);
}

static char *origin_replace(const char *sql, const char *from, const char *to)
{
    const char *at = strstr(sql, from);
    text_buffer out = {0};
    CHECK(at && !strstr(at + strlen(from), from));
    append_bytes(&out, sql, (size_t)(at - sql)); append(&out, to); append(&out, at + strlen(from));
    return out.data;
}

static void origin_lifecycle(sqlparser_dialect_t dialect)
{
    static const char initial[] = "INSERT ALL INTO \"Target One\" (\"Value\",Stamp) "
        "VALUES ('seed',CURRENT_TIMESTAMP) INTO Other (A,B,C) VALUES (1,'two',DEFAULT) "
        "SELECT \"MiXeD\" AS \"Alias\" FROM \"Source\"";
    static const char *old_sql[] = {"'seed'", "'longer replacement'"};
    static const char *new_sql[] = {"'longer replacement'", "'O''Brien'"};
    sqlparser_handle_t *h = parse(dialect, initial), *clone = NULL;
    char *expected = copy_text(initial), *next;
    size_t i;
    ++case_number;
    for (i = 0U; i < COUNT(new_sql); ++i) {
        sqlparser_patch_t patch = {0}; sqlparser_patch_list_t list = {&patch, 1U};
        sqlparser_literal_value_t literal = {0};
        unsigned long generation = h->generation;
        next = origin_replace(expected, old_sql[i], new_sql[i]); free(expected); expected = next;
        patch.op = SQLPARSER_PATCH_REPLACE;
        patch.selector = "stmt[0].insert_cell[0][0]";
        if (i == 0U) patch.sql = new_sql[i];
        else { literal.kind = SQLPARSER_LITERAL_KIND_STRING; literal.string_value = "O'Brien"; patch.literal = &literal; }
        stage = "repeated raw and typed apply";
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(h->generation == generation + 1UL && sqlparser_oracle_multi_insert_source_is_current(h));
        origin_verify(h, expected, 1);
        CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
        sqlparser_handle_destroy(h); h = clone; clone = NULL;
        stage = "shifted-source clone after original destruction";
        origin_verify(h, expected, 1);
    }
    stage = "destructive reparse certificate before graph";
    next = copy_text(expected);
    CHECK(sqlparser_handle_reparse_destructive(h, &next, &error) == SQLPARSER_STATUS_OK && !next);
    CHECK(sqlparser_oracle_multi_insert_source_is_current(h));
    origin_verify(h, expected, 1);
    free(expected); sqlparser_handle_destroy(h);
}

#ifndef SQLPARSER_ORIGIN_CERT_BASELINE
static void origin_expect_miss(const sqlparser_handle_t *view)
{
    sqlparser_identifier_origin_map_t *map = NULL;
    CHECK(sqlparser_oracle_try_replay_certified_multi_insert_origins(view, &map, &error) == SQLPARSER_STATUS_OK);
    CHECK(map == NULL);
}

static void origin_guards(sqlparser_dialect_t dialect)
{
    static const char sql[] = "INSERT ALL INTO T(C) VALUES ('a') SELECT \"MiXeD\" FROM \"Source\"";
    sqlparser_handle_t *h = parse(dialect, sql), view;
    sqlparser_dialect_multi_insert_t *m = (sqlparser_dialect_multi_insert_t *)multi(h);
    sqlparser_dialect_ops_t impostor = *h->dialect_ops;
    sqlparser_identifier_origin_map_t *map = NULL;
    unsigned char invalid_state = 0U;
    ++case_number; stage = "owner and provenance misses";
    view = *h; view.dialect_ops = &impostor; view.dialect_state = &invalid_state; origin_expect_miss(&view);
    view = *h; view.dialect = SQLPARSER_DIALECT_MYSQL; view.dialect_state = &invalid_state; origin_expect_miss(&view);
    view = *h; view.dialect_state = NULL; origin_expect_miss(&view);
    view = *h; ++view.generation; origin_expect_miss(&view);
    view = *h; ++view.sql_len; origin_expect_miss(&view);
    view = *h; ++view.parser_sql_len; origin_expect_miss(&view);
    view = *h; ++view.parse_tree.len; origin_expect_miss(&view);
    view = *h; view.sql = h->parser_sql; origin_expect_miss(&view);
    view = *h; view.parser_sql = h->sql; origin_expect_miss(&view);
    view = *h; view.parse_tree.data = h->sql; origin_expect_miss(&view);
    view = *h; view.surface_source_edits.count = 1U; origin_expect_miss(&view);
    view = *h; view.patch_batch_flags |= SQLPARSER_PATCH_BATCH_AST_DIRTY; origin_expect_miss(&view);
    view = *h; view.patch_batch_flags |= SQLPARSER_PATCH_BATCH_MULTI_INSERT_DIRTY; origin_expect_miss(&view);
    m->oracle_outer_identity = 0;
    origin_expect_miss(h);
    origin_verify(h, sql, 0);
    m->oracle_outer_identity = 1;
    /* Cell spans/identity are unrelated to the complete source-SELECT map. */
    m->oracle_spans_complete = 0; m->oracle_spans_identity = 0;
    CHECK(sqlparser_oracle_try_replay_certified_multi_insert_origins(h, &map, NULL) == SQLPARSER_STATUS_OK && map);
    sqlparser_identifier_origin_map_destroy(map); map = NULL;
    sqlparser_oracle_multi_insert_invalidate_source(h);
    origin_expect_miss(h); origin_verify(h, sql, 0);
    CHECK(sqlparser_oracle_try_replay_certified_multi_insert_origins(NULL, &map, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT && !map);
    CHECK(sqlparser_oracle_try_replay_certified_multi_insert_origins(h, NULL, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    sqlparser_handle_destroy(h);
}

static void origin_error_pair(sqlparser_handle_t *h)
{
    sqlparser_identifier_origin_map_t *actual = NULL, *reference = NULL;
    const sqlparser_identifier_origin_map_t *cached = NULL;
    sqlparser_error_t a = {0}, b = {0}; sqlparser_status_t sa, sb;
    CHECK(sqlparser_identifier_origin_map_new_identity(h->sql_len, &reference, &error) == SQLPARSER_STATUS_OK);
    sa = sqlparser_oracle_try_replay_certified_multi_insert_origins(h, &actual, &a);
    sb = sqlparser_oracle_replay_identifier_origins(h->sql, h->parser_sql, h->dialect_state, reference, &b);
    CHECK(sa == SQLPARSER_STATUS_INTERNAL_ERROR && sa == sb && actual == NULL);
    CHECK(a.code == b.code && a.cursor == b.cursor && a.line == b.line && a.column == b.column);
    same_text(a.message, b.message);
    CHECK(sqlparser_oracle_try_replay_certified_multi_insert_origins(h, &actual, NULL) == sa && !actual);
    origin_drop(h);
    CHECK(sqlparser_identifier_origins_for_handle(h, &cached, &a) == sa);
    CHECK(!cached && !h->identifier_origins);
    sqlparser_identifier_origin_map_destroy(reference);
}

static void origin_errors(sqlparser_dialect_t dialect)
{
    static const char sql[] = "INSERT ALL INTO T(C) VALUES ('a') SELECT \"MiXeD\" FROM \"Source\"";
    sqlparser_handle_t *h = parse(dialect, sql);
    size_t i; char saved;
    ++case_number; stage = "prefix and retained-source byte errors";
    for (i = 0U; i < h->parser_sql_len; ++i) {
        saved = h->parser_sql[i]; h->parser_sql[i] = saved == 'x' ? 'y' : 'x';
        origin_error_pair(h); h->parser_sql[i] = saved;
    }
    i = (size_t)(strstr(h->sql, "MiXeD") - h->sql);
    saved = h->sql[i]; h->sql[i] = 'x'; origin_error_pair(h); h->sql[i] = saved;
    origin_verify(h, sql, 1);
    sqlparser_handle_destroy(h);
}
#endif

static void origin_oom(sqlparser_dialect_t dialect)
{
#ifdef SQLPARSER_ORACLE_COMMIT_WRAPPERS
    static const char sql[] = "INSERT ALL INTO T(C) VALUES ('a') "
        "SELECT \"MiXeD\" AS Alias FROM SourceName WHERE Field=:NamedBind";
    sqlparser_handle_t *h = parse(dialect, sql);
    const sqlparser_identifier_origin_map_t *origins = NULL;
    size_t total, fail; int null_error;
    ++case_number; stage = "every lazy origin allocation failure";
    origin_drop(h); CHECK(!allocation_live);
    allocation_calls = 0U; allocation_fail = (size_t)-1; allocation_active = 1;
    CHECK(sqlparser_identifier_origins_for_handle(h, &origins, &error) == SQLPARSER_STATUS_OK);
    allocation_active = 0; total = allocation_calls; CHECK(total > 0U);
    origin_drop(h); CHECK(!allocation_live);
    for (null_error = 0; null_error <= 1; ++null_error) {
        for (fail = 1U; fail <= total; ++fail) {
            sqlparser_status_t status;
            origin_drop(h); CHECK(!allocation_live); origins = NULL;
            memset(&error, 0, sizeof(error));
            allocation_calls = 0U; allocation_fail = fail; allocation_active = 1;
            status = sqlparser_identifier_origins_for_handle(h, &origins, null_error ? NULL : &error);
            allocation_active = 0;
            CHECK(status == SQLPARSER_STATUS_NO_MEMORY && !origins && !h->identifier_origins && !allocation_live);
            CHECK(sqlparser_oracle_multi_insert_source_is_current(h));
            CHECK(sqlparser_identifier_origins_for_handle(h, &origins, &error) == SQLPARSER_STATUS_OK && origins);
        }
    }
    origin_drop(h); CHECK(!allocation_live);
    origin_verify(h, sql, 1);
    sqlparser_handle_destroy(h); CHECK(!allocation_live);
#else
    (void)dialect;
#endif
}

int main(int argc, char **argv)
{
    static const sqlparser_dialect_t dialects[] = {
        SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE
    };
    static const char *fixtures[] = {
        "INSERT ALL INTO T(A,B,C,D,E) VALUES ('one',20,100.50,NULL,CURRENT_TIMESTAMP) "
        "INTO Other(X,Y) VALUES ('two',CURRENT_TIMESTAMP) SELECT \"MiXeD\" AS Alias FROM \"Source\"",
        "/* leading ; */ INSERT FIRST WHEN 1=1 THEN INTO \"First Target\" (\"Quoted\",B,C) "
        "VALUES ('first',:BindName,AliasName) ELSE INTO LastTarget(Value) VALUES (DEFAULT) "
        "SELECT Src.\"MiXeD\" AS AliasName FROM SchemaName.\"SourceName\" Src WHERE Src.Other=:BindName; ",
        "INSERT ALL INTO App.T(C,D) VALUES (q'[branch; ''text]',COALESCE(1,2)) "
        "SELECT q'{source; text}' AS TextValue FROM SourceName UNION ALL SELECT 'other' FROM OtherSource"
    };
    size_t i, j; int oom = 0;
    for (i = 1U; i < (size_t)argc; ++i) {
        if (!strcmp(argv[i], "--record")) recording = 1;
        else if (!strcmp(argv[i], "--alloc")) oom = 1;
        else CHECK(0);
    }
    for (i = 0U; i < COUNT(dialects); ++i) {
        for (j = 0U; j < COUNT(fixtures); ++j) origin_fixture(dialects[i], fixtures[j]);
        origin_lifecycle(dialects[i]);
#ifndef SQLPARSER_ORIGIN_CERT_BASELINE
        if (!recording) { origin_guards(dialects[i]); origin_errors(dialects[i]); }
#endif
        if (oom) origin_oom(dialects[i]);
    }
    if (!recording) puts("PASS: certified Oracle-family lazy origins, complete graph/state/origin parity, exact owners, lifecycle, errors and requested allocation sweeps");
    return 0;
}
