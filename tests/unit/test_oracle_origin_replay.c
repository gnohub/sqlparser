/* Exact origin-map parity and lifetime coverage for lazy Oracle state replay.
 * GNU-linker builds may define SQLPARSER_ORIGIN_REPLAY_WRAPPERS and use
 * -Wl,--wrap=sqlparser_dialect_preprocess_identifier_origins to verify that
 * only eligible fresh Oracle-family multi-inserts avoid preprocessing.
 * SQLPARSER_ORIGIN_REPLAY_ALLOC_WRAPPERS plus malloc/calloc/realloc/free GNU
 * wrappers enables every-allocation failure/retry checks (also NULL errors). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_internal.h"
#include "sqlparser_identifier_origin_internal.h"
#include "../../src/core/sqlparser_ast_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"
#include "../../src/dialect/sqlparser_dialect_oracle_internal.h"

static size_t replay_calls;
#ifdef SQLPARSER_ORIGIN_REPLAY_WRAPPERS
sqlparser_status_t __real_sqlparser_dialect_preprocess_identifier_origins(
    sqlparser_dialect_t, const char *, const sqlparser_limits_t *, char **,
    void **, sqlparser_identifier_origin_map_t **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_dialect_preprocess_identifier_origins(
    sqlparser_dialect_t dialect, const char *sql, const sqlparser_limits_t *limits,
    char **parser_sql, void **state, sqlparser_identifier_origin_map_t **origins,
    sqlparser_error_t *error)
{
    replay_calls++;
    return __real_sqlparser_dialect_preprocess_identifier_origins(
        dialect, sql, limits, parser_sql, state, origins, error);
}
#endif

#ifdef SQLPARSER_ORIGIN_REPLAY_ALLOC_WRAPPERS
static size_t allocation_calls, fail_allocation, allocation_live;
static int allocation_active;
static void *allocation_pointers[4096];
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);

static size_t allocation_slot(void *pointer)
{
    size_t index;
    if (pointer != NULL)
        for (index = 0U; index < 4096U; index++)
            if (allocation_pointers[index] == pointer) return index;
    return 4096U;
}

static void allocation_track(void *pointer)
{
    size_t index;
    if (pointer == NULL) return;
    for (index = 0U; index < 4096U; index++) {
        if (allocation_pointers[index] == NULL) {
            allocation_pointers[index] = pointer;
            allocation_live++;
            return;
        }
    }
    fputs("FAIL: replay allocation ledger exhausted\n", stderr);
    abort();
}

void *__wrap_malloc(size_t size)
{
    void *pointer;
    if (allocation_active && ++allocation_calls == fail_allocation) return NULL;
    pointer = __real_malloc(size);
    if (allocation_active) allocation_track(pointer);
    return pointer;
}
void *__wrap_calloc(size_t count, size_t size)
{
    void *pointer;
    if (allocation_active && ++allocation_calls == fail_allocation) return NULL;
    pointer = __real_calloc(count, size);
    if (allocation_active) allocation_track(pointer);
    return pointer;
}
void *__wrap_realloc(void *pointer, size_t size)
{
    size_t slot = allocation_slot(pointer);
    void *next;
    if (allocation_active && ++allocation_calls == fail_allocation) return NULL;
    next = __real_realloc(pointer, size);
    if (slot != 4096U && (next != NULL || size == 0U)) {
        allocation_pointers[slot] = next;
        if (next == NULL) allocation_live--;
    } else if (slot == 4096U && allocation_active) {
        allocation_track(next);
    }
    return next;
}
void __wrap_free(void *pointer)
{
    size_t slot = allocation_slot(pointer);
    if (slot != 4096U) {
        allocation_pointers[slot] = NULL;
        allocation_live--;
    }
    __real_free(pointer);
}
#endif

static int failure(const char *label, const sqlparser_error_t *error)
{
    fprintf(stderr, "FAIL: %s: %s\n", label, error != NULL ? error->message : "");
    return 0;
}

static void clear_origins(sqlparser_handle_t *handle)
{
    sqlparser_identifier_origin_map_destroy(handle->identifier_origins);
    handle->identifier_origins = NULL;
}

static int verify_map(sqlparser_handle_t *handle, const char *label)
{
    sqlparser_error_t error = {0};
    sqlparser_identifier_origin_map_t *reference = NULL;
    const sqlparser_identifier_origin_map_t *actual = NULL, *cached = NULL;
    char *parser_sql = NULL;
    void *state = NULL;
    size_t offset, length, actual_calls, expected_calls;
    int same_sql, eligible, ok = 0;

    if (sqlparser_dialect_preprocess_identifier_origins(handle->dialect,
        handle->sql, &handle->limits, &parser_sql, &state, &reference, &error) !=
        SQLPARSER_STATUS_OK) goto done;
    if (strcmp(parser_sql, handle->parser_sql) != 0) goto done;
    same_sql = handle->sql_len == handle->parser_sql_len &&
        memcmp(handle->sql, handle->parser_sql, handle->sql_len) == 0;
    eligible = handle->generation == 0UL &&
        (handle->dialect == SQLPARSER_DIALECT_ORACLE ||
         handle->dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE ||
         handle->dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE) &&
        sqlparser_oracle_state_has_multi_insert(handle->dialect_state);
    if (sqlparser_oracle_multi_insert_source_is_current(handle) &&
        sqlparser_oracle_state_multi_insert(handle->dialect_state)->oracle_outer_identity)
        eligible = 1;
    expected_calls = !same_sql && !eligible;
    clear_origins(handle);
    replay_calls = 0U;
    if (sqlparser_identifier_origins_for_handle(handle, &actual, &error) !=
        SQLPARSER_STATUS_OK) goto done;
    actual_calls = replay_calls;
#ifdef SQLPARSER_ORIGIN_REPLAY_WRAPPERS
    if (actual_calls != expected_calls) {
        fprintf(stderr, "FAIL: %s replay calls=%zu expected=%zu\n",
            label, actual_calls, expected_calls);
        goto done;
    }
#else
    (void)actual_calls;
    (void)expected_calls;
#endif
    if (sqlparser_identifier_origin_map_output_length(actual) !=
        sqlparser_identifier_origin_map_output_length(reference)) goto done;
    /* Every span, including empty and one-byte-out-of-bounds spans, checks
     * exact SOURCE/GENERATED/UNKNOWN behavior rather than only identifiers. */
    for (offset = 0U; offset <= handle->parser_sql_len + 1U; offset++) {
        for (length = 0U; length <= handle->parser_sql_len + 1U - offset; length++) {
            sqlparser_identifier_origin_t left, right;
            sqlparser_identifier_origin_kind_t a, b;
            a = sqlparser_identifier_origin_map_lookup(actual, offset, length, &left);
            b = sqlparser_identifier_origin_map_lookup(reference, offset, length, &right);
            if (a != b || left.kind != right.kind ||
                left.source_offset != right.source_offset ||
                left.source_length != right.source_length) {
                fprintf(stderr, "FAIL: %s origin span %zu:%zu\n", label, offset, length);
                goto done;
            }
        }
    }
    replay_calls = 0U;
    if (sqlparser_identifier_origins_for_handle(handle, &cached, &error) !=
        SQLPARSER_STATUS_OK || cached != actual || replay_calls != 0U) goto done;
    ok = 1;
done:
    if (!ok) failure(label, &error);
    if (state != NULL && handle->dialect_ops->destroy_state != NULL)
        handle->dialect_ops->destroy_state(state);
    sqlparser_identifier_origin_map_destroy(reference);
    free(parser_sql);
    return ok;
}

static int parse_owned(sqlparser_dialect_t dialect, const char *sql,
    sqlparser_handle_t **handle)
{
    sqlparser_error_t error = {0};
    sqlparser_parse_options_t options;
    char *input = sqlparser_strdup(sql);
    sqlparser_status_t status;
    sqlparser_parse_options_default(&options);
    options.dialect = dialect;
    options.limits.max_sql_bytes = 16U * 1024U * 1024U;
    options.limits.max_output_bytes = 16U * 1024U * 1024U;
    status = sqlparser_parse_with_options(input, &options, handle, &error);
    if (input != NULL) memset(input, 'x', strlen(input));
    free(input); /* All subsequent work must use handle-owned source/state. */
    if (status != SQLPARSER_STATUS_OK) return failure(sql, &error);
    return 1;
}

static int verify_graph_and_deparse(sqlparser_handle_t *handle)
{
    sqlparser_query_graph_view_t graph;
    sqlparser_error_t error = {0};
    char *sql = NULL;
    int ok = sqlparser_statement_query_graph(handle, 0U, &graph, &error) ==
        SQLPARSER_STATUS_OK && sqlparser_deparse(handle, &sql, &error) ==
        SQLPARSER_STATUS_OK && sql != NULL;
    free(sql);
    return ok || failure("graph/deparse", &error);
}

static int verify_lifecycle(sqlparser_dialect_t dialect, const char *sql)
{
    sqlparser_handle_t *handle = NULL, *clone = NULL, *reparsed = NULL;
    sqlparser_error_t error = {0};
    sqlparser_patch_t patch = {0};
    sqlparser_patch_list_t patches = {0};
    char *output = NULL;
    int ok = 0;
    if (!parse_owned(dialect, sql, &handle) ||
        !verify_map(handle, "fresh multi-insert") ||
        !verify_graph_and_deparse(handle)) goto done;
    if (sqlparser_handle_clone(handle, &clone, &error) != SQLPARSER_STATUS_OK)
        goto done;
    sqlparser_handle_destroy(handle);
    handle = NULL;
    if (!verify_map(clone, "clone after source destruction") ||
        !verify_graph_and_deparse(clone)) goto done;
    patch.op = SQLPARSER_PATCH_REPLACE;
    patch.selector = "stmt[0].insert_cell[0][0]";
    patch.sql = "'replacement'";
    patches.items = &patch;
    patches.count = 1U;
    if (sqlparser_apply_patch(clone, &patches, &error) != SQLPARSER_STATUS_OK ||
        clone->generation == 0UL || !verify_map(clone, "mutated fallback") ||
        !verify_graph_and_deparse(clone) ||
        sqlparser_deparse(clone, &output, &error) != SQLPARSER_STATUS_OK)
        goto done;
    if (!parse_owned(dialect, output, &reparsed) ||
        !verify_map(reparsed, "fresh reparse") ||
        !verify_graph_and_deparse(reparsed)) goto done;
    /* Actual destructive reparse increments generation and binds its fresh
     * state certificate before the next lazy origin request. */
    if (sqlparser_handle_reparse_destructive(reparsed, &output, &error) !=
        SQLPARSER_STATUS_OK || output != NULL ||
        !verify_map(reparsed, "destructive reparse fallback")) goto done;
    ok = 1;
done:
    if (!ok) failure("lifecycle", &error);
    free(output);
    sqlparser_handle_destroy(reparsed);
    sqlparser_handle_destroy(clone);
    sqlparser_handle_destroy(handle);
    return ok;
}

static int verify_replay_rejects_changed_bytes(sqlparser_dialect_t dialect)
{
    static const char sql[] = "INSERT ALL INTO Target (Value) VALUES (1) "
        "SELECT \"MiXeD\" FROM \"Source\"";
    sqlparser_handle_t *handle = NULL;
    sqlparser_error_t error = {0};
    const sqlparser_identifier_origin_map_t *origins = NULL;
    size_t index;
    char saved;
    int ok = 0;
    if (!parse_owned(dialect, sql, &handle)) goto done;
    for (index = 0U; index < handle->parser_sql_len; index++) {
        clear_origins(handle);
        saved = handle->parser_sql[index];
        handle->parser_sql[index] = saved == 'x' ? 'y' : 'x';
        if (sqlparser_identifier_origins_for_handle(handle, &origins, &error) !=
            SQLPARSER_STATUS_INTERNAL_ERROR || origins != NULL ||
            handle->identifier_origins != NULL) {
            handle->parser_sql[index] = saved;
            goto done;
        }
        handle->parser_sql[index] = saved;
    }
    clear_origins(handle);
    index = (size_t)(strstr(handle->sql, "MiXeD") - handle->sql);
    saved = handle->sql[index];
    handle->sql[index] = 'x';
    if (sqlparser_identifier_origins_for_handle(handle, &origins, &error) !=
        SQLPARSER_STATUS_INTERNAL_ERROR || origins != NULL ||
        handle->identifier_origins != NULL) {
        handle->sql[index] = saved;
        goto done;
    }
    handle->sql[index] = saved;
    ok = verify_map(handle, "retry after rejected replay");
done:
    if (!ok) failure("replay byte validation", &error);
    sqlparser_handle_destroy(handle);
    return ok;
}

static int verify_allocation_failures(sqlparser_dialect_t dialect)
{
#ifdef SQLPARSER_ORIGIN_REPLAY_ALLOC_WRAPPERS
    sqlparser_handle_t *handle = NULL;
    sqlparser_error_t error = {0};
    const sqlparser_identifier_origin_map_t *origins;
    sqlparser_status_t status;
    size_t fail;
    int null_error, completed, ok = 0;
    if (!parse_owned(dialect,
        "INSERT ALL INTO Target (Value) VALUES ('a') "
        "SELECT \"MiXeD\" AS Alias FROM SourceName WHERE Field=:NamedBind", &handle))
        goto done;
    for (null_error = 0; null_error <= 1; null_error++) {
        completed = 0;
        for (fail = 1U; fail < 256U; fail++) {
            clear_origins(handle);
            if (allocation_live != 0U) goto done;
            origins = NULL;
            allocation_calls = 0U;
            fail_allocation = fail;
            allocation_active = 1;
            status = sqlparser_identifier_origins_for_handle(handle, &origins,
                null_error ? NULL : &error);
            allocation_active = 0;
            if (allocation_calls < fail) {
                if (status != SQLPARSER_STATUS_OK || origins == NULL) goto done;
                completed = 1;
                break;
            }
            if (status != SQLPARSER_STATUS_NO_MEMORY || origins != NULL ||
                handle->identifier_origins != NULL || allocation_live != 0U) goto done;
            if (!verify_map(handle, "immediate retry after origin allocation failure")) goto done;
        }
        if (!completed) goto done;
    }
    clear_origins(handle);
    if (allocation_live != 0U) goto done;
    ok = verify_map(handle, "retry after each origin allocation failure");
done:
    allocation_active = 0;
    if (!ok) failure("origin allocation failure cleanup", &error);
    sqlparser_handle_destroy(handle);
    return ok;
#else
    (void)dialect;
    return 1;
#endif
}

static int verify_vastbase_owner_guards(void)
{
    sqlparser_handle_t *handle = NULL;
    sqlparser_handle_t view;
    sqlparser_dialect_ops_t impostor;
    sqlparser_identifier_origin_map_t *origins = NULL;
    sqlparser_error_t error = {0};
    unsigned char invalid_state = 0U;
    int ok = 0;
    if (!parse_owned(SQLPARSER_DIALECT_VASTBASE_ORACLE,
        "INSERT ALL INTO T (C) VALUES (CURRENT_TIMESTAMP) SELECT C FROM SourceTable", &handle)) goto done;
    if (sqlparser_vastbase_oracle_try_replay_identifier_origins(handle, &origins, &error) !=
        SQLPARSER_STATUS_OK || origins == NULL || handle->identifier_origins != NULL ||
        sqlparser_identifier_origin_map_output_length(origins) != handle->parser_sql_len) goto done;
    sqlparser_identifier_origin_map_destroy(origins); origins = NULL;
    view = *handle;
    impostor = *handle->dialect_ops;
    view.dialect_ops = &impostor;
    view.dialect_state = &invalid_state;
    if (sqlparser_vastbase_oracle_try_replay_identifier_origins(&view, &origins, &error) !=
        SQLPARSER_STATUS_OK || origins != NULL) goto done;
    view = *handle; view.dialect = SQLPARSER_DIALECT_ORACLE; view.dialect_state = &invalid_state;
    if (sqlparser_vastbase_oracle_try_replay_identifier_origins(&view, &origins, NULL) !=
        SQLPARSER_STATUS_OK || origins != NULL) goto done;
    view = *handle; view.generation = 1UL; view.dialect_state = &invalid_state;
    if (sqlparser_vastbase_oracle_try_replay_identifier_origins(&view, &origins, &error) !=
        SQLPARSER_STATUS_OK || origins != NULL) goto done;
    view = *handle; view.dialect_state = NULL;
    if (sqlparser_vastbase_oracle_try_replay_identifier_origins(&view, &origins, NULL) !=
        SQLPARSER_STATUS_OK || origins != NULL) goto done;
    if (sqlparser_vastbase_oracle_try_replay_identifier_origins(NULL, &origins, &error) !=
        SQLPARSER_STATUS_INVALID_ARGUMENT || origins != NULL ||
        sqlparser_vastbase_oracle_try_replay_identifier_origins(handle, NULL, NULL) !=
        SQLPARSER_STATUS_INVALID_ARGUMENT) goto done;
    ok = verify_map(handle, "unchanged owner after rejected impostors");
done:
    if (!ok) failure("Vastbase exact owner admission", &error);
    sqlparser_identifier_origin_map_destroy(origins);
    sqlparser_handle_destroy(handle);
    return ok;
}

/* Separate supported session statements exercise the real Vastbase outer
 * writer without inventing unsupported session-plus-multi-insert syntax. */
static int verify_vastbase_outer_sessions(void)
{
    static const char *cases[] = {
        "ALTER SESSION SET CURRENT_SCHEMA TO \"AppMixed\"",
        "ALTER SESSION SET NAMES 'UTF8'",
        "SELECT q'[can''t; ALTER SESSION SET CURRENT_SCHEMA hidden;]' AS C FROM SourceTable",
        "SELECT nq'{can''t; ALTER SESSION SET CURRENT_SCHEMA hidden;}' AS C FROM SourceTable"
    };
    size_t i;
    for (i = 0U; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        sqlparser_handle_t *h = NULL;
        sqlparser_identifier_origin_map_t *origins = NULL;
        sqlparser_error_t error = {0};
        if (!parse_owned(SQLPARSER_DIALECT_VASTBASE_ORACLE, cases[i], &h)) return 0;
        if (sqlparser_vastbase_oracle_try_replay_identifier_origins(h, &origins, &error) !=
            SQLPARSER_STATUS_OK || origins != NULL || !verify_map(h, "Vastbase outer-session exact origins")) {
            sqlparser_handle_destroy(h); return failure("Vastbase outer session", &error);
        }
        sqlparser_handle_destroy(h);
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const struct {
        sqlparser_dialect_t dialect;
        const char *sql;
    } selects[] = {
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT \"MiXeD\" AS \"Alias\" FROM \"Source\" WHERE \"MiXeD\"=$1"},
        {SQLPARSER_DIALECT_MYSQL, "SELECT `MiXeD` AS `Alias` FROM `Source` WHERE `MiXeD`=?"},
        {SQLPARSER_DIALECT_ORACLE, "SELECT \"MiXeD\" AS \"Alias\" FROM \"Source\" WHERE \"MiXeD\"=:MixedBind"},
        {SQLPARSER_DIALECT_SQLSERVER, "SELECT TOP 5 [MiXeD] AS [Alias] FROM [Source] WHERE [MiXeD]=@MixedBind"},
        {SQLPARSER_DIALECT_DAMENG, "SELECT \"MiXeD\" AS \"Alias\" FROM \"Source\" WHERE \"MiXeD\"=:MixedBind"},
        {SQLPARSER_DIALECT_VASTBASE_ORACLE, "SELECT \"MiXeD\" AS \"Alias\" FROM \"Source\" WHERE \"MiXeD\"=:MixedBind"},
        {SQLPARSER_DIALECT_VASTBASE_MYSQL, "SELECT `MiXeD` AS `Alias` FROM `Source` WHERE `MiXeD`=?"},
        {SQLPARSER_DIALECT_VASTBASE_POSTGRESQL, "SELECT \"MiXeD\" AS \"Alias\" FROM \"Source\" WHERE \"MiXeD\"=$1"},
        {SQLPARSER_DIALECT_VASTBASE_SQLSERVER, "SELECT TOP 5 [MiXeD] AS [Alias] FROM [Source] WHERE [MiXeD]=@MixedBind"},
        {SQLPARSER_DIALECT_KINGBASE_ORACLE, "SELECT \"MiXeD\" AS \"Alias\" FROM \"Source\" WHERE \"MiXeD\"=:MixedBind"},
        {SQLPARSER_DIALECT_KINGBASE_MYSQL, "SELECT `MiXeD` AS `Alias` FROM `Source` WHERE `MiXeD`=?"},
        {SQLPARSER_DIALECT_KINGBASE_POSTGRESQL, "SELECT \"MiXeD\" AS \"Alias\" FROM \"Source\" WHERE \"MiXeD\"=$1"},
        {SQLPARSER_DIALECT_KINGBASE_SQLSERVER, "SELECT TOP 5 [MiXeD] AS [Alias] FROM [Source] WHERE [MiXeD]=@MixedBind"}
    };
    static const sqlparser_dialect_t multi_dialects[] = {
        SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE,
        SQLPARSER_DIALECT_VASTBASE_ORACLE, SQLPARSER_DIALECT_DAMENG
    };
    static const char *multi_cases[] = {
        "INSERT ALL INTO \"Target One\" (\"Value\") VALUES ('first') "
        "INTO App.Other (Value) VALUES (:B) SELECT \"Mixed Col\" AS \"Alias Col\", "
        "Src.Other AS Another FROM \"Source Table\" Src WHERE Src.Other=:B",
        "/* lead */ INSERT FIRST WHEN 1=1 THEN INTO \"FirstTarget\" (Value) VALUES ('a') "
        "ELSE INTO LastTarget (Value) VALUES ('b') "
        "SELECT \"MiXeD\" AS AliasName FROM \"Source\";  ",
        "INSERT ALL INTO TargetTable (Value) VALUES (q'[branch; text]') "
        "SELECT q'[source; text]' AS \"QuotedAlias\" "
        "FROM SchemaName.\"SourceName\" WHERE \"Field\"=:BindName",
        "INSERT ALL INTO TargetTable (Value) VALUES (1) "
        "SELECT \"MiXeD\" AS Result FROM \"Source\" UNION ALL "
        "SELECT Other AS Result FROM OtherSource",
        "INSERT ALL WHEN 1=1 THEN INTO TargetTable (Value) VALUES (1) "
        "WHEN 2=2 THEN INTO OtherTarget (Value) VALUES (2) "
        "ELSE INTO LastTarget (Value) VALUES (3) "
        "SELECT Src.\"MiXeD\" FROM SchemaName.\"SourceName\"@RemoteLink Src",
        "INSERT ALL INTO TargetTable (Value) VALUES (1) "
        "SELECT NodeId FROM TreeSource START WITH ParentId IS NULL "
        "CONNECT BY PRIOR NodeId=ParentId",
        "/* lead ; */ INSERT ALL INTO TargetTable (Value) VALUES (q'[can''t; SET SCHEMA fake;]') "
        "SELECT q'{can''t; SET SCHEMA fake;}' AS TextValue FROM SourceName -- tail ;\n"
    };
    size_t i, j;
    for (i = 0U; i < sizeof(selects) / sizeof(selects[0]); i++) {
        sqlparser_handle_t *handle = NULL;
        if (!parse_owned(selects[i].dialect, selects[i].sql, &handle) ||
            !verify_map(handle, "all-13 transformed SELECT control") ||
            !verify_graph_and_deparse(handle)) return 1;
        sqlparser_handle_destroy(handle);
        if (!parse_owned(selects[i].dialect,
            "SELECT PlainColumn AS PlainAlias FROM PlainTable WHERE PlainColumn=1", &handle) ||
            !verify_map(handle, "all-13 plain SELECT control") ||
            !verify_graph_and_deparse(handle)) return 1;
        sqlparser_handle_destroy(handle);
    }
    for (i = 0U; i < sizeof(multi_dialects) / sizeof(multi_dialects[0]); i++) {
        for (j = 0U; j < sizeof(multi_cases) / sizeof(multi_cases[0]); j++) {
            if (multi_dialects[i] == SQLPARSER_DIALECT_VASTBASE_ORACLE && j == 5U) {
                sqlparser_parse_options_t options;
                sqlparser_error_t error = {0};
                sqlparser_handle_t *h = NULL;
                sqlparser_parse_options_default(&options); options.dialect = multi_dialects[i];
                if (sqlparser_parse_with_options(multi_cases[j], &options, &h, &error) !=
                    SQLPARSER_STATUS_UNSUPPORTED || h != NULL || strcmp(error.message,
                    "hierarchical query is not supported for this dialect") != 0)
                    return failure("Vastbase preserves outer no-hierarchy flag", &error);
            } else if (!verify_lifecycle(multi_dialects[i], multi_cases[j])) return 1;
        }
    }
    /* Origin-only national-literal coverage. The graph test separately records
     * the exact combined q-quote/national-literal baseline reproducer, including
     * its existing "national literal AST owner is missing" failure. */
    for (i = 0U; i < sizeof(multi_dialects) / sizeof(multi_dialects[0]); i++) {
        sqlparser_handle_t *handle = NULL;
        if (!parse_owned(multi_dialects[i],
            "INSERT ALL INTO Target (Value) VALUES (1) "
            "SELECT N'national' AS Nat FROM SourceName", &handle) ||
            !verify_map(handle, "national-literal origin parity")) return 1;
        sqlparser_handle_destroy(handle);
    }
    for (i = 0U; i < 3U; i++) {
        if (!verify_replay_rejects_changed_bytes(multi_dialects[i]) ||
            !verify_allocation_failures(multi_dialects[i])) return 1;
    }
    if (!verify_vastbase_owner_guards() || !verify_vastbase_outer_sessions()) return 1;
    if (argc > 1) {
        FILE *file = fopen(argv[1], "rb");
        char *input;
        long size;
        sqlparser_handle_t *handle = NULL;
        if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
            (size = ftell(file)) < 0 || fseek(file, 0L, SEEK_SET) != 0) return 1;
        input = malloc((size_t)size + 1U);
        if (input == NULL || fread(input, 1U, (size_t)size, file) != (size_t)size) return 1;
        input[size] = '\0';
        fclose(file);
        for (i = 0U; i < 3U; i++) {
            if (!parse_owned(multi_dialects[i], input, &handle) ||
                !verify_map(handle, "actual Oracle-family fixture") ||
                !verify_graph_and_deparse(handle)) return 1;
            sqlparser_handle_destroy(handle);
        }
        free(input);
    }
    puts("PASS: Oracle lazy origin replay, all-13 SELECT controls, exact span parity, ownership, clone, mutation, reparse and byte validation");
    return 0;
}
