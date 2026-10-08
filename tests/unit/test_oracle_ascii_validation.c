/* Strict raw ASCII fragment proof, all Oracle owners. The capability-off
 * reference keeps the real parser and the exact same registered owner.
 * Reuse the independent full graph/origin/lifetime and OOM ledger checks.
 * --fixture/--golden-dir verify the actual 5000-cell raw and typed outputs;
 * --alloc additionally sweeps allocation failures independently, never pairing
 * allocation ordinals after a removed parse changes the allocation count. */
#define main oracle_owned_commit_main
#include "test_oracle_owned_commit.c"
#undef main
#ifdef SQLPARSER_ORACLE_COMMIT_WRAPPERS
#include "../../src/internal/sqlparser_ascii_string_internal.h"

sqlparser_status_t sqlparser_apply_patch_ascii_reference(sqlparser_handle_t *, const sqlparser_patch_list_t *, sqlparser_error_t *);
sqlparser_status_t sqlparser_test_ascii_validate(sqlparser_handle_t *, const char *, sqlparser_selector_kind_t, int *, sqlparser_error_t *);
static size_t fragment_calls;
sqlparser_status_t __real_sqlparser_parse_insert_cell_node_sql(const char *, const sqlparser_generated_source_t *, PgQuery__Node **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_parse_insert_cell_node_sql(const char *sql, const sqlparser_generated_source_t *source, PgQuery__Node **node, sqlparser_error_t *e)
{
    ++fragment_calls;
    return __real_sqlparser_parse_insert_cell_node_sql(sql, source, node, e);
}
static void ascii_same_error(sqlparser_status_t a, const sqlparser_error_t *ae,
    sqlparser_status_t b, const sqlparser_error_t *be)
{
    CHECK(a == b && ae->code == be->code && ae->cursor == be->cursor &&
        ae->line == be->line && ae->column == be->column);
    same_text(ae->message, be->message);
}
static void ascii_case(sqlparser_dialect_t dialect, const char *fragment, int admitted, int invalid)
{
    const char *input = "INSERT ALL INTO T (A, B) VALUES ('old', 'keep') SELECT 1 FROM Dual";
    unsigned limits, duplicate;
    CHECK(sqlparser_patch_plain_ascii_string_sql(fragment) == admitted);
    if (admitted) {
        PgQuery__Node *node = NULL; size_t n = strlen(fragment) - 2U;
        CHECK(__real_sqlparser_parse_insert_cell_node_sql(fragment, NULL, &node, &error) == SQLPARSER_STATUS_OK);
        CHECK(node && node->node_case == PG_QUERY__NODE__NODE_A_CONST && node->a_const &&
            !node->a_const->isnull && node->a_const->val_case == PG_QUERY__A__CONST__VAL_SVAL &&
            node->a_const->sval && node->a_const->sval->sval);
        CHECK(strlen(node->a_const->sval->sval) == n && !memcmp(node->a_const->sval->sval, fragment + 1, n));
        sqlparser_free_proto_node(node);
    }
    for (limits = 0U; limits < 4U; ++limits) for (duplicate = 0U; duplicate < 2U; ++duplicate) {
        sqlparser_parse_options_t options; sqlparser_handle_t *h[2] = {NULL, NULL};
        sqlparser_query_graph_view_t old[2]; sqlparser_patch_t p[2] = {{0}};
        sqlparser_patch_list_t list = {p, 2U}; sqlparser_error_t e[2]; sqlparser_status_t status[2];
        char *output[2] = {NULL, NULL}, *records[2] = {NULL, NULL}; size_t calls[2], mode;
        sqlparser_status_t graph_status[2] = {SQLPARSER_STATUS_OK, SQLPARSER_STATUS_OK}; sqlparser_error_t graph_errors[2];
        char *owned = copy_text(fragment);
        stage = "Oracle raw ASCII fragment differential"; ++case_number;
        sqlparser_parse_options_default(&options); options.dialect = dialect;
        options.limits.max_statement_count = 1U;
        if (limits & 2U) options.limits.max_output_bytes = strlen(input);
        for (mode = 0U; mode < 2U; ++mode) {
            CHECK(sqlparser_parse_with_options(input, &options, &h[mode], &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_statement_query_graph(h[mode], 0U, &old[mode], &error) == SQLPARSER_STATUS_OK);
            /* Oracle initial normalization can exceed public input length.
             * Clamp only after valid construction, identically on both sides. */
            if (limits & 1U) h[mode]->limits.max_sql_bytes = h[mode]->parser_sql_len > strlen(input) ? h[mode]->parser_sql_len : strlen(input);
        }
        CHECK(h[0]->dialect_ops == h[1]->dialect_ops && !h[1]->dialect_ops->plain_scalar_native_validation);
        CHECK(h[0]->dialect_ops->plain_ascii_string_fragments);
        p[0].op = p[1].op = SQLPARSER_PATCH_REPLACE;
        p[0].selector = "stmt[0].insert_cell[0][0]"; p[0].sql = owned;
        p[1].selector = duplicate ? p[0].selector : "stmt[0].insert_cell[0][1]"; p[1].sql = "'done'";
        for (mode = 0U; mode < 2U; ++mode) {
            fragment_calls = 0U; status[mode] = (mode ? sqlparser_apply_patch_ascii_reference : sqlparser_apply_patch)(h[mode], &list, &e[mode]);
            calls[mode] = fragment_calls;
        }
        memset(owned, 'x', strlen(owned)); free(owned);
        ascii_same_error(status[0], &e[0], status[1], &e[1]);
        if (invalid) CHECK(status[0] != SQLPARSER_STATUS_OK);
        if (admitted && limits == 0U && !duplicate) {
            if (status[0] != SQLPARSER_STATUS_OK || calls[0] != 0U || calls[1] < 2U) fprintf(stderr, "ASCII route dialect=%d fragment=%s status=%d calls=%zu/%zu\n", dialect, fragment, status[0], calls[0], calls[1]);
            CHECK(status[0] == SQLPARSER_STATUS_OK && calls[0] == 0U && calls[1] >= 2U);
        }
        for (mode = 0U; mode < 2U; ++mode) {
            stale_graph(&old[mode]);
            if (status[mode] == SQLPARSER_STATUS_OK) {
                CHECK(sqlparser_deparse(h[mode], &output[mode], &error) == SQLPARSER_STATUS_OK);
                /* Export serialization is larger than SQL; the configured
                 * SQL/output limit has already been exercised above. */
                h[mode]->limits.max_output_bytes = 64U * 1024U * 1024U;
                { sqlparser_query_graph_view_t fresh;
                  graph_status[mode] = sqlparser_statement_query_graph(h[mode], 0U, &fresh, &graph_errors[mode]);
                  if (graph_status[mode] == SQLPARSER_STATUS_OK) {
                      char *json = NULL;
                      graph_status[mode] = sqlparser_export_view_json(h[mode], 0U, &json, &graph_errors[mode]);
                      sqlparser_string_free(json);
                      if (graph_status[mode] == SQLPARSER_STATUS_OK) records[mode] = graph_record(h[mode], 0);
                  }
                }
            } else CHECK(sqlparser_test_failed_handle(h[mode]));
        }
        if (status[0] == SQLPARSER_STATUS_OK) {
            ascii_same_error(graph_status[0], &graph_errors[0], graph_status[1], &graph_errors[1]);
            if (admitted) CHECK(graph_status[0] == SQLPARSER_STATUS_OK);
        }
        same_text(output[0], output[1]); same_text(records[0], records[1]);
        for (mode = 0U; mode < 2U; ++mode) {
            free(records[mode]); sqlparser_handle_destroy(h[mode]);
        }
        /* Outputs outlive both original handles. */
        same_text(output[0], output[1]); free(output[0]); free(output[1]);
    }
}
static void ascii_oom(sqlparser_dialect_t dialect)
{
    const char *input = "INSERT ALL INTO T (A, B) VALUES ('old', 'keep') SELECT 1 FROM Dual";
    unsigned mode; size_t fail;
    for (mode = 0U; mode < 2U; ++mode) {
        int complete = 0;
        for (fail = 1U; fail < 32768U; ++fail) {
            sqlparser_handle_t *h = parse(dialect, input);
            sqlparser_patch_t p[2] = {
                {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'ASCII ; SELECT -- /* content */'"},
                {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .sql="''"}};
            sqlparser_patch_list_t list = {p, 2U}; sqlparser_status_t status; size_t calls;
            stage = "Oracle ASCII independent OOM ledger";
            allocation_calls = 0U; allocation_fail = fail; allocation_active = 1;
            status = (mode ? sqlparser_apply_patch_ascii_reference : sqlparser_apply_patch)(h, &list, &error);
            allocation_active = 0; calls = allocation_calls;
            if (status != SQLPARSER_STATUS_OK) CHECK(sqlparser_test_failed_handle(h));
            else { char *out = verify(h, "INSERT ALL INTO T (A, B) VALUES ('ASCII ; SELECT -- /* content */', '') SELECT 1 FROM Dual", 0); free(out); }
            sqlparser_handle_destroy(h); CHECK(allocation_live == 0U && native_depth == 0U);
            if (calls < fail) { CHECK(status == SQLPARSER_STATUS_OK); complete = 1; break; }
        }
        CHECK(complete);
    }
}
static void ascii_guard_matrix(void)
{
    int d, ordinal, copied, selector;
    for (d = SQLPARSER_DIALECT_POSTGRESQL; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; ++d) {
        const sqlparser_dialect_ops_t *owner = sqlparser_dialect_get_ops((sqlparser_dialect_t)d);
        sqlparser_handle_t *original = parse((sqlparser_dialect_t)d, "SELECT 1");
        for (copied = 0; copied < 3; ++copied) for (ordinal = -1; ordinal <= 3; ++ordinal)
        for (selector = 0; selector < 2; ++selector) {
            sqlparser_handle_t fake = *original; sqlparser_dialect_ops_t copy = *owner;
            int incoming = ordinal, exact_oracle, expected_skip;
            fake.dialect_ops = copied == 1 ? &copy : owner;
            if (copied == 2) fake.dialect = d == SQLPARSER_DIALECT_ORACLE ? SQLPARSER_DIALECT_POSTGRESQL : SQLPARSER_DIALECT_ORACLE;
            exact_oracle = copied == 0 && (d == SQLPARSER_DIALECT_ORACLE || d == SQLPARSER_DIALECT_KINGBASE_ORACLE || d == SQLPARSER_DIALECT_VASTBASE_ORACLE);
            expected_skip = selector == 0 && owner->plain_ascii_string_fragments &&
                (ordinal == 2 || (ordinal == 1 && exact_oracle));
            stage = "ASCII exact owner/ordinal/selector admission matrix";
            fragment_calls = 0U;
            CHECK(sqlparser_test_ascii_validate(&fake, "'plain'", selector ? SQLPARSER_SELECTOR_KIND_EXPRESSION : SQLPARSER_SELECTOR_KIND_INSERT_CELL, &incoming, &error) == SQLPARSER_STATUS_OK);
            CHECK(fragment_calls == (expected_skip ? 0U : 1U));
            CHECK(incoming == (selector && ordinal ? 0 : ordinal));
        }
        sqlparser_handle_destroy(original);
    }
}
int main(int argc, char **argv)
{
    static const sqlparser_dialect_t ds[] = {SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE};
    static const char *fallback[] = {"N'national'", "q'[alternative]'", "'can''t'", "'back\\slash'", "'line\nfeed'", "'tab\there'", "'雪'", "'\177'", "'\377'", "('grouped')", "'a'||'b'", "NULL", "DEFAULT", "1", "'x'\n'y'", "E'escape'", "$tag$dollar$tag$"};
    static const char *invalid[] = {"", "'unterminated", "'x' trailing", "'x', 'y'", "'x'); SELECT 1; --"};
    size_t d, i; unsigned byte; char token[4] = {'\'', 0, '\'', 0}, large[260]; int alloc = 0;
    for (i = 1U; i < (size_t)argc; ++i) if (!strcmp(argv[i], "--alloc")) alloc = 1;
    ascii_guard_matrix();
    for (d = 0U; d < COUNT(ds); ++d) {
        ascii_case(ds[d], "''", 1, 0);
        for (byte = 0x20U; byte <= 0x7eU; ++byte) if (byte != '\'' && byte != '\\') { token[1] = (char)byte; ascii_case(ds[d], token, 1, 0); }
        ascii_case(ds[d], "'; -- /*! SELECT NULL DEFAULT INTO VALUES @ : */'", 1, 0);
        large[0] = '\''; memset(large + 1, 'a', 256U); large[257] = '\''; large[258] = 0;
        ascii_case(ds[d], large, 1, 0);
        for (i = 0U; i < COUNT(fallback); ++i) ascii_case(ds[d], fallback[i], 0, 0);
        for (i = 0U; i < COUNT(invalid); ++i) ascii_case(ds[d], invalid[i], 0, i < 2U);
        if (alloc) ascii_oom(ds[d]);
    }
    puts("PASS: Oracle-family raw ASCII grammar, generic differential, diagnostics, limits and lifetimes");
    return oracle_owned_commit_main(argc, argv);
}

#else
/* Non-GNU builds retain the public semantic/fresh-parser comparison and
 * ownership suite. Fragment-call probes and isolated planner copies require
 * the dedicated GNU linker-wrapped target above. */
int main(int argc, char **argv)
{
    return oracle_owned_commit_main(argc, argv);
}
#endif
