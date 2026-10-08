/* Cached Oracle source lookup versus the unchanged generic scanner.
 * Optional --wrap=sqlparser_oracle_multi_insert_certified_cell_span plus
 * -DSQLPARSER_SPAN_TEST_WRAPPERS observes admission without replacing results. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "../../src/core/sqlparser_ast_internal.h"
#include "../../src/dialect/sqlparser_dialect_oracle_internal.h"

static sqlparser_error_t error;
static size_t hits;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s: %s\n", __LINE__, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_SPAN_TEST_WRAPPERS
int __real_sqlparser_oracle_multi_insert_certified_cell_span(const sqlparser_handle_t *, size_t, size_t, size_t *, size_t *);
int __wrap_sqlparser_oracle_multi_insert_certified_cell_span(const sqlparser_handle_t *h, size_t b, size_t c, size_t *s, size_t *e)
{
    int result = __real_sqlparser_oracle_multi_insert_certified_cell_span(h, b, c, s, e);
    hits += result != 0;
    return result;
}
#endif
static sqlparser_handle_t *parse(sqlparser_dialect_t dialect, const char *sql)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_default(&options);
    options.dialect = dialect;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    return h;
}
static void verify_handles(sqlparser_handle_t *actual, sqlparser_handle_t *reference,
                           int expect_hit, int allow_comments)
{
    sqlparser_view_expression_source_cache_t a = {0}, r = {0};
    const sqlparser_dialect_multi_insert_t *multi = sqlparser_oracle_state_multi_insert(actual->dialect_state);
    size_t pass, b, c, before = hits;
    CHECK(multi && multi->branch_count);
    sqlparser_oracle_multi_insert_invalidate_source(reference);
    /* First successful call must perform generic whole-statement validation. */
    for (pass = 0U; pass < 3U; ++pass) {
        for (b = multi->branch_count; b-- > 0U;) {
            for (c = multi->branches[b].cell_count; c-- > 0U;) {
                size_t as = 0U, ae = 0U, rs = 0U, re = 0U;
                int av = sqlparser_view_insert_cell_source_span(actual, NULL, &a, allow_comments, 0U, b, c, &as, &ae, &error);
                int rv = sqlparser_view_insert_cell_source_span(reference, NULL, &r, allow_comments, 0U, b, c, &rs, &re, &error);
                CHECK(av == rv && as == rs && ae == re);
                if (pass == 0U && b + 1U == multi->branch_count && c + 1U == multi->branches[b].cell_count)
                    CHECK(hits == before);
            }
        }
    }
#ifdef SQLPARSER_SPAN_TEST_WRAPPERS
    if (expect_hit >= 0 && (hits > before) != expect_hit) {
        fprintf(stderr, "span hit mismatch dialect=%d allow_comments=%d expect=%d hits=%zu SQL=%s\n",
            (int)actual->dialect, allow_comments, expect_hit, hits - before, actual->sql);
        fprintf(stderr, "spans complete=%d identity=%d outer=%d count=%zu current=%d cache=%d\n",
            multi->oracle_spans_complete, multi->oracle_spans_identity, multi->oracle_outer_identity,
            multi->oracle_span_count, sqlparser_oracle_multi_insert_source_is_current(actual), a.valid);
        CHECK((hits > before) == expect_hit);
    }
#else
    (void)expect_hit;
#endif
}
static void verify(sqlparser_dialect_t dialect, const char *sql, int expect_hit)
{
    int allow_comments;
    for (allow_comments = 0; allow_comments <= 1; ++allow_comments) {
        sqlparser_handle_t *actual = parse(dialect, sql), *reference = parse(dialect, sql);
        verify_handles(actual, reference, expect_hit, allow_comments);
        /* Mutation invalidation must independently shut the helper off. */
        sqlparser_oracle_multi_insert_invalidate_source(actual);
        { size_t start = 0U, end = 0U;
          CHECK(!sqlparser_oracle_multi_insert_certified_cell_span(actual, 0U, 0U, &start, &end)); }
        sqlparser_handle_destroy(actual);
        sqlparser_handle_destroy(reference);
    }
}

static void expect_miss(sqlparser_handle_t *h)
{
    size_t start = 13U, end = 17U;
    CHECK(!sqlparser_oracle_multi_insert_certified_cell_span(h, 0U, 0U, &start, &end));
    CHECK(start == 13U && end == 17U);
}
static void provenance_mismatches(sqlparser_dialect_t dialect)
{
    sqlparser_handle_t *h = parse(dialect,
        "INSERT ALL INTO t VALUES ('a', 12) INTO u VALUES ('b') SELECT 1 FROM dual");
    sqlparser_dialect_multi_insert_t *multi = (sqlparser_dialect_multi_insert_t *)
        sqlparser_oracle_state_multi_insert(h->dialect_state);
    sqlparser_dialect_multi_insert_t saved = *multi;
    const sqlparser_dialect_ops_t *owner = h->dialect_ops;
    sqlparser_dialect_t saved_dialect = h->dialect;
    size_t start, end;
    CHECK(sqlparser_oracle_multi_insert_certified_cell_span(h, 0U, 0U, &start, &end));
#define BAD(field, value) do { multi->oracle_source_provenance.field = (value); expect_miss(h); *multi = saved; } while (0)
    BAD(sql, h->sql + 1U);
    BAD(sql, NULL);
    BAD(parser_sql, h->parser_sql + 1U);
    BAD(wire, h->parse_tree.data + 1U);
    BAD(wire, NULL);
    BAD(state, NULL);
    BAD(sql_length, h->sql_len + 1U);
    BAD(parser_sql_length, h->parser_sql_len + 1U);
    BAD(wire_length, h->parse_tree.len + 1U);
    BAD(generation, h->generation + 1UL);
#undef BAD
    h->dialect_ops = NULL; expect_miss(h); h->dialect_ops = owner;
    h->dialect = SQLPARSER_DIALECT_MYSQL; expect_miss(h); h->dialect = saved_dialect;
    h->generation++; expect_miss(h); h->generation--;
    h->surface_source_edits.count = 1U; expect_miss(h); h->surface_source_edits.count = 0U;
    h->patch_batch_flags |= SQLPARSER_PATCH_BATCH_AST_DIRTY; expect_miss(h);
    h->patch_batch_flags &= ~SQLPARSER_PATCH_BATCH_AST_DIRTY;
    h->patch_batch_flags |= SQLPARSER_PATCH_BATCH_MULTI_INSERT_DIRTY; expect_miss(h);
    h->patch_batch_flags &= ~SQLPARSER_PATCH_BATCH_MULTI_INSERT_DIRTY;
    CHECK(!sqlparser_oracle_multi_insert_certified_cell_span(h, multi->branch_count, 0U, &start, &end));
    CHECK(!sqlparser_oracle_multi_insert_certified_cell_span(h, 0U, multi->branches[0].cell_count, &start, &end));
    CHECK(!sqlparser_oracle_multi_insert_certified_cell_span(h, 0U, 0U, NULL, &end));
    CHECK(!sqlparser_oracle_multi_insert_certified_cell_span(h, 0U, 0U, &start, NULL));
    CHECK(sqlparser_oracle_multi_insert_certified_cell_span(h, 0U, 0U, &start, &end));
    sqlparser_handle_destroy(h);
}

static void clone_and_commits(sqlparser_dialect_t dialect)
{
    static const char *source = "INSERT ALL INTO t VALUES ('a', 12, 'keep') INTO u VALUES ('b') SELECT 1 FROM dual";
    static const char *values[] = {"a much longer value", "", "z'z"};
    static const char *expected[] = {
        "INSERT ALL INTO t VALUES ('a much longer value', 12, 'keep') INTO u VALUES ('b') SELECT 1 FROM dual",
        "INSERT ALL INTO t VALUES ('', 12, 'keep') INTO u VALUES ('b') SELECT 1 FROM dual",
        "INSERT ALL INTO t VALUES ('z''z', 12, 'keep') INTO u VALUES ('b') SELECT 1 FROM dual"
    };
    sqlparser_handle_t *original = parse(dialect, source), *clone = NULL, *reference;
    size_t round, pass;
    /* Clone before any graph/source cache exists, then destroy its owner. */
    CHECK(sqlparser_handle_clone(original, &clone, &error) == SQLPARSER_STATUS_OK);
    sqlparser_handle_destroy(original);
    reference = parse(dialect, source);
    verify_handles(clone, reference, 1, 0);
    sqlparser_handle_destroy(reference);
    sqlparser_handle_destroy(clone);
    original = parse(dialect, source);
    for (round = 0U; round < sizeof(values) / sizeof(values[0]); ++round) {
        sqlparser_literal_value_t literal = {0};
        sqlparser_patch_t patch = {0};
        sqlparser_patch_list_t patches = {&patch, 1U};
        char *output = NULL;
        literal.kind = SQLPARSER_LITERAL_KIND_STRING;
        literal.string_value = values[round];
        patch.op = SQLPARSER_PATCH_REPLACE;
        patch.selector = "stmt[0].insert_cell[0][0]";
        patch.literal = &literal;
        CHECK(sqlparser_apply_patch(original, &patches, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_deparse(original, &output, &error) == SQLPARSER_STATUS_OK);
        CHECK(strcmp(output, expected[round]) == 0);
        sqlparser_string_free(output);
        for (pass = 0U; pass < 3U; ++pass) {
            sqlparser_query_graph_view_t graph;
            CHECK(sqlparser_statement_query_graph(original, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
            reference = parse(dialect, expected[round]);
            /* Use a fresh scoped cache after each mutation, just as callers do. */
            verify_handles(original, reference, 1, (int)(pass % 2U));
            CHECK(sqlparser_handle_clone(original, &clone, &error) == SQLPARSER_STATUS_OK);
            verify_handles(clone, reference, 1, (int)(pass % 2U));
            sqlparser_handle_destroy(clone);
            sqlparser_handle_destroy(reference);
        }
    }
    sqlparser_handle_destroy(original);
}

static void comment_policy(sqlparser_dialect_t dialect)
{
    sqlparser_handle_t *h = parse(dialect,
        "INSERT ALL INTO t VALUES (1 /*inline*/ + 2, 'a') INTO u VALUES ('x') SELECT 1 FROM dual");
    sqlparser_view_expression_source_cache_t cache = {0};
    size_t start = 0U, end = 0U;
    CHECK(sqlparser_view_insert_cell_source_span(h, NULL, &cache, 0, 0U, 0U, 0U,
        &start, &end, &error) == 0);
    CHECK(start == 0U && end == 0U);
    CHECK(sqlparser_view_insert_cell_source_span(h, NULL, &cache, 1, 0U, 0U, 0U,
        &start, &end, &error) == 1);
    CHECK(end > start && end - start == strlen("1 /*inline*/ + 2"));
    CHECK(memcmp(h->sql + start, "1 /*inline*/ + 2", end - start) == 0);
    CHECK(sqlparser_view_insert_cell_source_span(h, NULL, &cache, 0, 0U, 0U, 0U,
        &start, &end, &error) == 0);
    CHECK(start == 0U && end == 0U);
    sqlparser_handle_destroy(h);
}
int main(void)
{
    static const sqlparser_dialect_t dialects[] = { SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE };
    size_t d;
    for (d = 0U; d < sizeof(dialects) / sizeof(dialects[0]); ++d) {
        provenance_mismatches(dialects[d]);
        comment_policy(dialects[d]);
        clone_and_commits(dialects[d]);
        verify(dialects[d], " /*before*/ INSERT ALL INTO t VALUES ('a', 12, 'b''c', current_date) INTO u VALUES ('x', NULL) SELECT 1 FROM dual; /*after*/", 0);
        verify(dialects[d], "INSERT ALL INTO t VALUES (1 /*inline*/ + 2, 'a') INTO u VALUES ('x') SELECT 1 FROM dual", -1);
        verify(dialects[d], "INSERT ALL INTO t VALUES ('a', N'b') INTO u VALUES ('x') SELECT 1 FROM dual", 0);
        verify(dialects[d], "INSERT ALL WHEN 1=1 THEN INTO t VALUES ('a', 1) ELSE INTO u VALUES ('b') SELECT 1 FROM dual", 0);
        verify(dialects[d], "INSERT ALL INTO t VALUES ('a', ARRAY[1,2], 'b') INTO u VALUES ('c') SELECT 1 FROM dual", 0);
    }
    puts("Oracle cached span parity passed");
    return 0;
}
