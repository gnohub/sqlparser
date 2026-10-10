/* Cold Oracle constructor certificate versus independently frozen
 * statement, generic multi-insert, and top-level callers. Every instrumentation
 * wrapper calls through and returns the real result. */
#include <ctype.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "../../src/core/sqlparser_ast_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"
#include "../../src/dialect/sqlparser_dialect_oracle_internal.h"
#include "../../src/dialect/sqlparser_dialect_sqlserver_scan.h"
#include "../oracle_spancert/frozen_surface_scanners.inc"
#include "../oracle_spancert/frozen_oracle_scanners.inc"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *stage = "initialization", *fixture = "";
static size_t comparisons, positive_calls, negative_calls, parsed_negatives, rejected_inputs;
static size_t initial_calls, initial_hits;
static int tracing, side;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Oracle initial span certificate: %s line %d case=%zu: %s\nSQL: %s\n", stage, __LINE__, comparisons, #x, fixture); abort(); } } while (0)

typedef struct { int site, dialect; size_t pos, result; } event;
static event events[2][32768];
static size_t event_count[2], scan_calls[2][4];
static int site_id(const char *name)
{
    if (strstr(name, "public_statement_span_in_sql")) return 1;
    if (strstr(name, "multi_insert_cell_source_span")) return 2;
    if (strstr(name, "view_insert_cell_source_span")) return 3;
    return 0;
}
static size_t record_scan(sqlparser_dialect_t dialect, size_t pos, size_t result, const char *name)
{
    event *entry;
    int site;
    if (!tracing) return result;
    site = site_id(name);
    CHECK(event_count[side] < COUNT(events[side]));
    entry = &events[side][event_count[side]++];
    entry->site = site; entry->dialect = dialect; entry->pos = pos; entry->result = result;
    scan_calls[side][site]++;
    return result;
}
static size_t candidate_scan(sqlparser_dialect_t d, const char *s, size_t p, const char *name)
{ return record_scan(d, p, sqlparser_public_skip_quoted_or_comment(d, s, p), name); }
static size_t frozen_scan(sqlparser_dialect_t d, const char *s, size_t p, const char *name)
{ return record_scan(d, p, spancert_frozen_sqlparser_public_skip_quoted_or_comment(d, s, p), name); }

/* Used only by final_checks: alter a same-length fixture after each real span
 * validator succeeds, before the caller's mandatory final scanner. Both old
 * and new validation really run; neither wrapper forces success or offsets. */
static const char *fault_bytes;
static int fault_delimiter;
static void apply_final_fault(const sqlparser_handle_t *h, size_t start, size_t end)
{
    if (fault_bytes != NULL) {
        CHECK(end - start == strlen(fault_bytes));
        memcpy(h->sql + start, fault_bytes, end - start);
    }
    if (fault_delimiter == -1) { CHECK(start > 0U); h->sql[start - 1U] = ':'; }
    if (fault_delimiter == 1) { CHECK(end < h->sql_len); h->sql[end] = ':'; }
}
static int candidate_initial(const sqlparser_handle_t *h, size_t row, size_t col,
    size_t *ss, size_t *se, size_t *vp, size_t *start, size_t *end)
{
    int result = sqlparser_oracle_multi_insert_initial_cell_span(h, row, col, ss, se, vp, start, end);
    if (tracing) { initial_calls++; initial_hits += result != 0; }
    if (result) apply_final_fault(h, *start, *end);
    return result;
}
#define sqlparser_public_skip_quoted_or_comment(d, s, p) candidate_scan((d), (s), (p), __func__)
#define sqlparser_oracle_multi_insert_initial_cell_span candidate_initial
#include "../../src/core/sqlparser_view.c"
#undef sqlparser_oracle_multi_insert_initial_cell_span
#undef sqlparser_public_skip_quoted_or_comment

#define spancert_frozen_sqlparser_public_skip_quoted_or_comment(d, s, p) frozen_scan((d), (s), (p), __func__)
#include "../oracle_spancert/frozen_view_scanners.inc"
static int frozen_multi(const sqlparser_handle_t *h, const sqlparser_dialect_multi_insert_t *m,
    sqlparser_view_expression_source_cache_t *cache, size_t ss, size_t se,
    size_t row, size_t col, size_t *start, size_t *end)
{
    int result = spancert_frozen_sqlparser_view_multi_insert_cell_source_span(h, m, cache, ss, se, row, col, start, end);
    if (result) apply_final_fault(h, *start, *end);
    return result;
}
#define spancert_frozen_sqlparser_view_multi_insert_cell_source_span frozen_multi
#include "../oracle_spancert/frozen_entry_scanners.inc"
#undef spancert_frozen_sqlparser_view_multi_insert_cell_source_span
#undef spancert_frozen_sqlparser_public_skip_quoted_or_comment

static sqlparser_error_t error_sentinel(void)
{
    sqlparser_error_t error;
    memset(&error, 0xa5, sizeof(error));
    error.code = SQLPARSER_STATUS_UNSUPPORTED;
    error.cursor = 17; error.line = 23; error.column = 31;
    strcpy(error.message, "retained error sentinel");
    return error;
}
static sqlparser_view_expression_source_cache_t cache_sentinel(int valid)
{
    sqlparser_view_expression_source_cache_t cache;
    memset(&cache, 0xa5, sizeof(cache));
    cache.resume = 101U; cache.search_position = 103U; cache.last_location = 107U;
    cache.valid = valid; cache.statement_end = 109U; cache.statement_start = 113U;
    return cache;
}
static void same_cache(const sqlparser_view_expression_source_cache_t *a,
                       const sqlparser_view_expression_source_cache_t *b)
{
    CHECK(a->resume == b->resume && a->search_position == b->search_position &&
          a->last_location == b->last_location && a->valid == b->valid &&
          a->statement_end == b->statement_end && a->statement_start == b->statement_start &&
          a->multi_branch_index == b->multi_branch_index);
}
static void same_error(const sqlparser_error_t *a, const sqlparser_error_t *b)
{
    CHECK(a->code == b->code && a->cursor == b->cursor && a->line == b->line &&
          a->column == b->column && !memcmp(a->message, b->message, sizeof(a->message)));
}
static void begin_comparison(void)
{
    memset(scan_calls, 0, sizeof(scan_calls));
    event_count[0] = event_count[1] = initial_calls = initial_hits = 0U;
    tracing = 1; side = 0;
}
static void end_comparison(int expect_hit)
{
    size_t i;
    tracing = 0;
    if (expect_hit > 0) {
        CHECK(initial_calls == 1U && initial_hits == 1U);
        CHECK(scan_calls[1][1] == 0U && scan_calls[1][2] == 0U);
        CHECK(scan_calls[0][1] + scan_calls[0][2] > 0U);
        positive_calls++;
    } else {
        CHECK(initial_hits == 0U);
        CHECK(event_count[0] == event_count[1]);
        for (i = 0U; i < event_count[0]; i++) {
            const event *a = &events[0][i], *b = &events[1][i];
            CHECK(a->site == b->site && a->dialect == b->dialect &&
                  a->pos == b->pos && a->result == b->result);
        }
        negative_calls++;
    }
    comparisons++;
}
static int compare_insert(sqlparser_handle_t *a, sqlparser_handle_t *b,
    sqlparser_view_expression_source_cache_t *ac, sqlparser_view_expression_source_cache_t *bc,
    int comments, size_t statement, size_t row, size_t col, unsigned nulls, int expect_hit)
{
    size_t as = 117U, ae = 219U, bs = as, be = ae;
    int av, bv;
    sqlparser_error_t aerror = error_sentinel(), berror = aerror;
    begin_comparison();
    bv = spancert_frozen_sqlparser_view_insert_cell_source_span(b, NULL, bc, comments,
        statement, row, col, nulls & 1U ? NULL : &bs, nulls & 2U ? NULL : &be,
        nulls & 4U ? NULL : &berror);
    side = 1;
    av = sqlparser_view_insert_cell_source_span(a, NULL, ac, comments, statement, row, col,
        nulls & 1U ? NULL : &as, nulls & 2U ? NULL : &ae, nulls & 4U ? NULL : &aerror);
    if (av != bv || as != bs || ae != be)
        fprintf(stderr, "target=(%zu,%zu,%zu) nulls=%u result candidate=%d [%zu,%zu) frozen=%d [%zu,%zu)\n",
            statement, row, col, nulls, av, as, ae, bv, bs, be);
    CHECK(av == bv && as == bs && ae == be);
    same_error(&aerror, &berror);
    if (ac != NULL) { CHECK(bc != NULL); same_cache(ac, bc); }
    if (a && b) CHECK(a->generation == b->generation && a->failed == b->failed);
    end_comparison(expect_hit);
    return av;
}
static sqlparser_handle_t *parse_handle(sqlparser_dialect_t dialect, const char *sql, int required)
{
    sqlparser_parse_options_t options;
    sqlparser_error_t error;
    sqlparser_handle_t *handle = NULL;
    sqlparser_status_t status;
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    status = sqlparser_parse_with_options(sql, &options, &handle, &error);
    if (status != SQLPARSER_STATUS_OK) {
        if (required > 0) fprintf(stderr, "parse dialect=%d status=%d error=%s\n", (int)dialect, (int)status, error.message);
        CHECK(required <= 0 && handle == NULL);
        if (required == -1) CHECK(status == SQLPARSER_STATUS_UNSUPPORTED);
        rejected_inputs++;
    }
    return handle;
}
static sqlparser_handle_t *parse_reference_handle(sqlparser_dialect_t dialect, const char *sql, int required)
{
    sqlparser_parse_options_t options;
    sqlparser_error_t error;
    sqlparser_handle_t *handle = NULL;
    CHECK(required == 1);
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    /* Frozen scanners consume the legacy cell representation. Keep their
     * source parsing independent from the compact constructor under test. */
    CHECK(sqlparser_oracle_parse_legacy_replacement(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    return handle;
}
static void helper_miss(sqlparser_handle_t *handle, size_t row, size_t col)
{
    size_t ss = 11U, se = 13U, vp = 17U, start = 19U, end = 23U;
    CHECK(!sqlparser_oracle_multi_insert_initial_cell_span(handle, row, col, &ss, &se, &vp, &start, &end));
    CHECK(ss == 11U && se == 13U && vp == 17U && start == 19U && end == 23U);
}
static void verify_fixture(sqlparser_dialect_t dialect, const char *sql, int certified, int required)
{
    sqlparser_handle_t *a, *b;
    const sqlparser_dialect_multi_insert_t *multi;
    size_t row, col, pass, valid, nulls;
    int comments;
    static const int cold_valid[] = {0, 1, 3, -7};
    fixture = sql;
    a = parse_handle(dialect, sql, required);
    if (a == NULL) return;
    b = parse_reference_handle(dialect, sql, 1);
    multi = sqlparser_dialect_state_multi_insert(a->dialect, a->dialect_state);
    CHECK(multi != NULL && multi->branch_count > 0U);
    CHECK((multi->oracle_generic_spans_equivalent != 0) == certified);
    if (!certified) { helper_miss(a, 0U, 0U); parsed_negatives++; }
    for (comments = 0; comments <= 1; comments++) {
        /* Every target must independently succeed or miss from a cold cache,
         * including the last column in an arbitrary later branch. */
        for (row = 0U; row < multi->branch_count; row++) for (col = 0U; col < multi->branches[row].cell_count; col++) {
            for (valid = 0U; valid < COUNT(cold_valid); valid++) {
                sqlparser_view_expression_source_cache_t ac = cache_sentinel(cold_valid[valid]), bc = ac;
                int result = compare_insert(a, b, &ac, &bc, comments, 0U, row, col, 0U, certified);
                if (certified) CHECK(result == 1 && ac.valid == 2);
            }
            compare_insert(a, b, NULL, NULL, comments, 0U, row, col, 0U, certified);
        }
        for (pass = 0U; pass < 3U; pass++) {
            sqlparser_view_expression_source_cache_t ac = cache_sentinel(0), bc = ac;
            /* A non-first target is the first validation of this planning pass. */
            row = pass % multi->branch_count;
            col = multi->branches[row].cell_count - 1U;
            compare_insert(a, b, &ac, &bc, comments, 0U, row, col, 0U, certified);
            for (row = 0U; row < multi->branch_count; row++) {
                size_t r = pass & 1U ? multi->branch_count - row - 1U : row;
                for (col = 0U; col < multi->branches[r].cell_count; col++) {
                    size_t c = pass & 1U ? multi->branches[r].cell_count - col - 1U : col;
                    int hit = certified && ac.valid != 2;
                    compare_insert(a, b, &ac, &bc, comments, 0U, r, c, 0U, hit);
                    compare_insert(a, b, &ac, &bc, comments, 0U, r, c, 0U, certified && ac.valid != 2);
                }
                compare_insert(a, b, &ac, &bc, comments, 0U, r, multi->branches[r].cell_count, 0U, 0);
                compare_insert(a, b, &ac, &bc, comments, 0U, r, SIZE_MAX, 0U, 0);
            }
            compare_insert(a, b, &ac, &bc, comments, 0U, multi->branch_count, 0U, 0U, 0);
            compare_insert(a, b, &ac, &bc, comments, 0U, SIZE_MAX, 0U, 0U, 0);
            compare_insert(a, b, &ac, &bc, comments, 1U, 0U, 0U, 0U, 0);
            compare_insert(a, b, &ac, &bc, comments, SIZE_MAX, 0U, 0U, 0U, 0);
        }
        for (nulls = 0U; nulls < 8U; nulls++) {
            sqlparser_view_expression_source_cache_t ac = cache_sentinel(0), bc = ac;
            compare_insert(a, b, &ac, &bc, comments, 0U, 0U, 0U, (unsigned)nulls,
                certified && !(nulls & 3U));
        }
        for (row = 0U; row <= multi->branch_count; row++) {
            sqlparser_view_expression_source_cache_t ac = cache_sentinel(0), bc = ac;
            size_t c = row == multi->branch_count ? 0U : multi->branches[row].cell_count;
            compare_insert(a, b, &ac, &bc, comments, 0U, row, c, 0U, 0);
            same_cache(&ac, &bc);
        }
    }
    helper_miss(a, multi->branch_count, 0U); helper_miss(a, SIZE_MAX, SIZE_MAX);
    sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
}

static const sqlparser_dialect_t oracle_dialects[] = {
    SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE
};
static void positive_matrix(void)
{
    static const char *const sqls[] = {
        "INSERT ALL INTO t VALUES ('abc', 12) INTO u VALUES ('last') SELECT 1 FROM dual",
        "INSERT ALL INTO t VALUES ('abc', 12) INTO u VALUES ('last') SELECT 1 FROM dual; \t\n",
        "INSERT ALL INTO t VALUES ('abc', 12) INTO u VALUES ('last') SELECT name#x FROM dual",
        "\t\nInSeRt AlL\r\nINTO schema_17.table_2 (col_1, col2, col3, col4) VALUES ('', 'a''b', '\xce\xa9\xe4\xb8\xad\xc3\xa9', '\"quoted\"') INTO table3 (value_1) VALUES (-2.25) SELECT 7 + 2 AS n FROM dual\t\r\n",
        "INSERT ALL INTO t (a,b,c,d,e,f,g,h) VALUES (0, +17, -42, .5, 5., +.25, -0.75, NULL) INTO u VALUES (CURRENT_DATE, CURRENT_TIME, CURRENT_TIMESTAMP, LOCALTIME, LOCALTIMESTAMP, CURRENT_ROLE) INTO v VALUES (CURRENT_USER, SESSION_USER, USER, CURRENT_CATALOG, CURRENT_SCHEMA) SELECT col_a, col_b FROM schema_1.table_2 WHERE col_c = 7",
        "INSERT ALL INTO values_table (values_x, x_values) VALUES ('q N E $$ [] {} ; /* -- # \",', 'x''''y') INTO all_values (a) VALUES (99) SELECT 1 FROM dual"
    };
    size_t d, i;
    stage = "certified arbitrary branches, names, columns and cell tokens";
    for (d = 0U; d < COUNT(oracle_dialects); d++) for (i = 0U; i < COUNT(sqls); i++)
        verify_fixture(oracle_dialects[d], sqls[i], 1, 1);
}
static void negative_matrix(void)
{
    static const struct { const char *text; int required; } cells[] = {
        {"ARRAY[1,2]", 1}, {"{1,2}", 0}, {"q'[a,)]'", 1}, {"Q'{a}'", 1},
        {"N'x'", 1}, {"NQ'!a)!'", 1}, {"E'a\\\\b'", 1}, {"$$a,)$", 0},
        {"$$a,)$$", 1}, {"$tag$a,)$tag$", 1}, {"1 /* comment */ + 2", 1},
        {"1 -- comment\n + 2", 1}, {"1 /*outer /*inner*/ tail*/ + 2", 0},
        {"\"quoted_name\"", 1}, {"\xce\xa9name", 1}, {"name#x", 0},
        {"name$x", 1}, {"(1 + 2)", 1}, {"1e3", 1}, {"'a\\b'", 1},
        {"'a\nb'", 1}, {"'a\177b'", 1}, {"#E'a\\'b'", 0},
        {"\xce\xa9" "E'a\\'b'", 0}, {"#$$a$$", 0}, {"\xce\xa9$$a$$", 0}
    };
    static const struct { const char *suffix; int required; } sources[] = {
        {"SELECT 'str' FROM dual", 1}, {"SELECT q'[x]' FROM dual", 1},
        {"SELECT N'x' FROM dual", 1}, {"SELECT E'x' FROM dual", 1},
        {"SELECT $$x$$ FROM dual", 1}, {"SELECT \"quoted\" FROM dual", 1},
        {"SELECT ARRAY[1,2] FROM dual", 1}, {"SELECT {1,2} FROM dual", 0},
        {"SELECT \xce\xa9name FROM dual", 1}, {"SELECT #$$x$$ FROM dual", 0},
        {"SELECT name$x FROM dual", 1}, {"SELECT 1 /*comment*/ FROM dual", 1},
        {"SELECT 1 --comment\n FROM dual", 1}, {"SELECT 1 /*outer /*inner*/ tail*/ FROM dual", 0},
        {"SELECT 1 FROM dual;;", 1},
        {"SELECT 1 FROM dual; /*tail*/", 1}, {"SELECT 1 FROM dual /*tail*/", 1},
        {"SELECT 1 FROM dual --tail", 1}, {"SELECT 1 FROM dual; SELECT 2 FROM dual", 0},
        {"SELECT 1 FROM dual UNION ALL SELECT 'x' FROM dual", 1},
        {"SELECT 1 FROM dual WHERE x = :bind_name", 1},
        {"SELECT 1 FROM dual@remote_db", 1}, {"SELECT 1 FROM dual CONNECT BY PRIOR id = parent_id", 1}
    };
    static const struct { const char *header; int required; } headers[] = {
        {"INTO \"quoted_table\" VALUES ('last', 2)", 1},
        {"INTO \xce\xa9table VALUES ('last', 2)", 1},
        {"INTO t#x VALUES ('last', 2)", 0}, {"INTO t$x VALUES ('last', 2)", 1},
        {"INTO t (a#x,b) VALUES ('last', 2)", 0}, {"INTO t (a$x,b) VALUES ('last', 2)", 1},
        {"INTO t (\"quoted_col\",b) VALUES ('last', 2)", 1},
        {"INTO t (\xce\xa9" "col,b) VALUES ('last', 2)", 1},
        {"INTO values VALUES ('last', 2)", 0},
        {"INTO schema_1.values VALUES ('last', 2)", 0},
        {"INTO t /*header*/ VALUES ('last', 2)", 1},
        {"INTO t (a, /*column*/ b) VALUES ('last', 2)", 1}
    };
    static const char *const conditional[] = {
        "INSERT ALL WHEN 1=1 THEN INTO t VALUES ('a', 1) ELSE INTO u VALUES ('b', 2) SELECT 1 FROM dual",
        "INSERT FIRST WHEN 1=1 THEN INTO t VALUES ('a', 1) ELSE INTO u VALUES ('b', 2) SELECT 1 FROM dual",
        " /*head*/ INSERT ALL INTO t VALUES ('a', 1) INTO u VALUES ('b', 2) SELECT 1 FROM dual"
    };
    char sql[2048]; size_t d, i; int n;
    stage = "unsafe last non-target branch, headers and source force original path";
    for (d = 0U; d < COUNT(oracle_dialects); d++) {
        for (i = 0U; i < COUNT(cells); i++) {
            n = snprintf(sql, sizeof(sql), "INSERT ALL INTO t VALUES ('abc', 1) INTO middle_t VALUES ('safe', 3) INTO last_t VALUES ('last', %s) SELECT 1 FROM dual", cells[i].text);
            CHECK(n >= 0 && (size_t)n < sizeof(sql));
            verify_fixture(oracle_dialects[d], sql, 0, cells[i].required);
        }
        for (i = 0U; i < COUNT(sources); i++) {
            n = snprintf(sql, sizeof(sql), "INSERT ALL INTO t VALUES ('abc', 1) INTO u VALUES ('last', 2) %s", sources[i].suffix);
            CHECK(n >= 0 && (size_t)n < sizeof(sql));
            /* Vastbase Oracle deliberately rejects hierarchical source queries.
             * Keep their existing UNSUPPORTED status; accepted dialects must parse. */
            verify_fixture(oracle_dialects[d], sql, 0,
                oracle_dialects[d] == SQLPARSER_DIALECT_VASTBASE_ORACLE &&
                strstr(sources[i].suffix, "CONNECT BY") != NULL ? -1 : sources[i].required);
        }
        for (i = 0U; i < COUNT(headers); i++) {
            n = snprintf(sql, sizeof(sql), "INSERT ALL INTO t VALUES ('abc', 1) %s SELECT 1 FROM dual", headers[i].header);
            CHECK(n >= 0 && (size_t)n < sizeof(sql));
            verify_fixture(oracle_dialects[d], sql, 0, headers[i].required);
        }
        for (i = 0U; i < COUNT(conditional); i++) verify_fixture(oracle_dialects[d], conditional[i], 0, 1);
    }
}

static void compare_statement(const sqlparser_handle_t *h, const char *sql, int current,
    size_t statement, unsigned nulls)
{
    size_t as = 117U, ae = 219U, bs = as, be = ae; int a, b;
    begin_comparison();
    b = spancert_frozen_sqlparser_view_public_statement_span_in_sql(h, sql, current, statement,
        nulls & 1U ? NULL : &bs, nulls & 2U ? NULL : &be);
    side = 1;
    a = sqlparser_view_public_statement_span_in_sql(h, sql, current, statement,
        nulls & 1U ? NULL : &as, nulls & 2U ? NULL : &ae);
    CHECK(a == b && as == bs && ae == be);
    end_comparison(0);
}
static void statement_matrix(void)
{
    static const char *const texts[] = {
        "", ";;;", " SELECT 1; SELECT 2;", "SELECT 1\nGO\nSELECT 2",
        "GO 2\r\nSELECT 1; GO 0\n", "SELECT q'[;GO]', NQ'{;}', E'a\\';b', $$;$$;SELECT 2",
        "SELECT [;GO],`a;`,\"b;\";/*outer /*;*/tail*/SELECT 3", "SELECT 'unterminated;GO",
        "INSERT ALL INTO t VALUES ('abc') INTO u VALUES ('last') SELECT 1 FROM dual; /*tail*/",
        "\200;\377\n--comment\nSELECT 1"
    };
    sqlparser_handle_t h;
    sqlparser_control_state_t control;
    sqlparser_control_unit_t units[2];
    int d; size_t i, j, mask;
    stage = "frozen statement boundaries, GO, control offsets and NULL guards";
    memset(&h, 0, sizeof(h));
    for (d = -1; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++) {
        h.dialect = (sqlparser_dialect_t)d;
        for (i = 0U; i < COUNT(texts); i++) {
            fixture = texts[i]; h.sql_len = strlen(texts[i]);
            for (j = 0U; j < 5U; j++) for (mask = 0U; mask < 4U; mask++)
                compare_statement(&h, texts[i], 0, j, (unsigned)mask);
            compare_statement(&h, texts[i], 0, SIZE_MAX, 0U);
        }
        for (mask = 0U; mask < 4U; mask++) {
            compare_statement(NULL, "GO", 0, 0U, (unsigned)mask);
            compare_statement(&h, NULL, 0, 0U, (unsigned)mask);
        }
        fixture = "  SELECT 1;   SELECT 2  "; h.sql_len = strlen(fixture);
        memset(&control, 0, sizeof(control)); memset(units, 0, sizeof(units));
        control.units = units; control.unit_count = 2U; h.control = &control;
        units[0].source_length = units[0].current_length = 12U;
        units[1].source_offset = units[1].current_offset = 12U;
        units[1].source_length = units[1].current_length = h.sql_len - 12U;
        for (i = 0U; i < 4U; i++) for (j = 0U; j < 2U; j++) compare_statement(&h, fixture, (int)j, i, 0U);
        units[0].source_offset = h.sql_len + 1U; compare_statement(&h, fixture, 0, 0U, 0U);
        units[0].source_offset = 0U; units[0].source_length = h.sql_len + 1U;
        compare_statement(&h, fixture, 0, 0U, 0U);
        units[0].current_offset = SIZE_MAX; units[0].current_length = 1U;
        compare_statement(&h, fixture, 1, 0U, 0U); h.control = NULL;
    }
}
static void compare_multi(const sqlparser_handle_t *h, const sqlparser_dialect_multi_insert_t *multi,
    sqlparser_view_expression_source_cache_t *ac, sqlparser_view_expression_source_cache_t *bc,
    size_t ss, size_t se, size_t row, size_t col)
{
    size_t as = 117U, ae = 219U, bs = as, be = ae; int a, b;
    begin_comparison();
    b = spancert_frozen_sqlparser_view_multi_insert_cell_source_span(h, multi, bc, ss, se, row, col, &bs, &be);
    side = 1;
    a = sqlparser_view_multi_insert_cell_source_span(h, multi, ac, ss, se, row, col, &as, &ae);
    CHECK(a == b && as == bs && ae == be); if (ac) same_cache(ac, bc);
    end_comparison(0);
}
static void generic_matrix(void)
{
    static const char *const fragments[] = {
        "123", "'a''b'", "'\xce\xa9\xe4\xb8\xad\"'", "ARRAY[1,2]", "{1,2}", "([1,2])", "([1,2]", "]", "}", ")",
        "q'[a,)]'", "Q'{a}'", "N'a'", "NQ'!a)!'", "E'a\\'b'", "$tag$x,)$tag$",
        "#E'a\\'b'", "\200E'a\\'b'", "#$$a$$", "\200$$a$$", "\200values(1)", "#values(1)",
        "1 /*inline*/ + 2", "1 --line\n + 2", "1 /*outer /*inner*/tail*/ + 2",
        "[a]]b]", "`a``b`", "\"a\"\"b\"", "q'[unclosed", "'unclosed", "/*unclosed", "$tag$unclosed"
    };
    sqlparser_handle_t h;
    sqlparser_dialect_multi_insert_t multi;
    sqlparser_dialect_multi_insert_branch_t branches[3];
    sqlparser_dialect_multi_insert_value_t cells[3][2];
    char sql[1024]; size_t i, pass, row, col; int d, n;
    stage = "frozen generic closure with malformed final branch and all dialects";
    memset(&h, 0, sizeof(h)); memset(&multi, 0, sizeof(multi));
    memset(branches, 0, sizeof(branches)); memset(cells, 0, sizeof(cells));
    multi.mode = SQLPARSER_DIALECT_MULTI_INSERT_ALL; multi.branches = branches; multi.branch_count = 3U;
    multi.source_public_sql = "SELECT 1 FROM dual";
    for (row = 0U; row < 3U; row++) {
        branches[row].ordinal = row; branches[row].cells = cells[row]; branches[row].cell_count = 2U;
        cells[row][0].public_sql = "'keep'"; cells[row][1].public_sql = "42";
    }
    for (d = -1; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++) for (i = 0U; i < COUNT(fragments); i++) {
        sqlparser_view_expression_source_cache_t ac = cache_sentinel(0), bc = ac;
        n = snprintf(sql, sizeof(sql), "INSERT ALL INTO t VALUES ('keep',42) INTO u VALUES ('keep',42) INTO last_t VALUES ('keep',%s) SELECT 1 FROM dual", fragments[i]);
        CHECK(n >= 0 && (size_t)n < sizeof(sql)); fixture = sql;
        h.sql = sql; h.sql_len = (size_t)n; h.dialect = (sqlparser_dialect_t)d;
        cells[2][1].public_sql = (char *)fragments[i];
        for (pass = 0U; pass < 3U; pass++) for (row = 0U; row < 4U; row++) for (col = 0U; col < 3U; col++)
            compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, pass & 1U ? 3U - row : row, col);
        compare_multi(&h, &multi, NULL, NULL, 0U, h.sql_len, 0U, 0U);
        ac = cache_sentinel(0); bc = ac;
        compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, SIZE_MAX, SIZE_MAX);
        compare_multi(NULL, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U);
        compare_multi(&h, NULL, &ac, &bc, 0U, h.sql_len, 0U, 0U);
        compare_multi(&h, &multi, &ac, &bc, h.sql_len, h.sql_len, 0U, 0U);
        multi.branches = NULL; compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U); multi.branches = branches;
        multi.source_public_sql = NULL; compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U); multi.source_public_sql = "SELECT 1 FROM dual";
        branches[2].ordinal = 99U; compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U); branches[2].ordinal = 2U;
        branches[2].cell_count = 1U; compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U); branches[2].cell_count = 2U;
        cells[2][1].public_sql = NULL; compare_multi(&h, &multi, &ac, &bc, 0U, h.sql_len, 0U, 0U);
    }
}
static void helper_nulls(void)
{
    size_t d, mask;
    stage = "helper miss leaves all five output sentinels untouched";
    fixture = "INSERT ALL INTO t VALUES ('abc', 12) INTO u VALUES ('last') SELECT 1 FROM dual";
    helper_miss(NULL, 0U, 0U);
    for (d = 0U; d < COUNT(oracle_dialects); d++) {
        sqlparser_handle_t *h = parse_handle(oracle_dialects[d], fixture, 1);
        for (mask = 1U; mask < 32U; mask++) {
            size_t ss = 11U, se = 13U, vp = 17U, start = 19U, end = 23U;
            CHECK(!sqlparser_oracle_multi_insert_initial_cell_span(h, 0U, 0U,
                mask & 1U ? NULL : &ss, mask & 2U ? NULL : &se, mask & 4U ? NULL : &vp,
                mask & 8U ? NULL : &start, mask & 16U ? NULL : &end));
            CHECK(ss == 11U && se == 13U && vp == 17U && start == 19U && end == 23U);
        }
        sqlparser_handle_destroy(h);
    }
}
static void final_checks(void)
{
    static const struct { const char *bytes; int comments, expected, delimiter; } faults[] = {
        {"q'[xx", 0, 0, 0}, {"nq'[x", 0, 0, 0}, {"'abc\\", 0, 0, 0}, {"'x''y", 0, 0, 0},
        {"/*xxx", 1, 0, 0}, {"/*x*/", 0, 0, 0}, {"/*x*/", 1, 1, 0},
        {"$a$x$", 0, 1, 0}, {"N'xx'", 0, 1, 0}, {"E'x\\'", 0, 0, 0},
        {NULL, 0, 0, -1}, {NULL, 0, 0, 1}
    };
    size_t d, i;
    stage = "cold certificate hit retains final delimiters, comments and quote bounds";
    fixture = "INSERT ALL INTO t VALUES ('abc', 'keep') INTO u VALUES ('last') SELECT 1 FROM dual";
    for (d = 0U; d < COUNT(oracle_dialects); d++) for (i = 0U; i < COUNT(faults); i++) {
        sqlparser_handle_t *a = parse_handle(oracle_dialects[d], fixture, 1), *b = parse_reference_handle(oracle_dialects[d], fixture, 1);
        sqlparser_view_expression_source_cache_t ac = cache_sentinel(0), bc = ac;
        fault_bytes = faults[i].bytes; fault_delimiter = faults[i].delimiter;
        CHECK(compare_insert(a, b, &ac, &bc, faults[i].comments, 0U, 0U, 0U, 0U, 1) == faults[i].expected);
        CHECK(ac.valid == 2 && bc.valid == 2);
        fault_bytes = NULL; fault_delimiter = 0;
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
    }
}
static void ordinary_and_guards(void)
{
    size_t d, col, mask;
    stage = "unchanged ordinary INSERT and error sentinel ordering";
    fixture = "INSERT INTO t VALUES ('a''b', 12, '\xce\xa9\xe4\xb8\xad')";
    for (d = 0U; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++) {
        sqlparser_handle_t *a = parse_handle((sqlparser_dialect_t)d, fixture, 1), *b = parse_reference_handle((sqlparser_dialect_t)d, fixture, 1);
        sqlparser_view_expression_source_cache_t ac = {0}, bc = {0};
        for (col = 0U; col < 4U; col++) compare_insert(a, b, &ac, &bc, 0, 0U, 0U, col, 0U, 0);
        for (col = 4U; col-- > 0U;) compare_insert(a, b, &ac, &bc, 1, 0U, 0U, col, 0U, 0);
        compare_insert(a, b, NULL, NULL, 0, 0U, 0U, 0U, 0U, 0);
        compare_insert(a, b, &ac, &bc, 0, 99U, 0U, 0U, 0U, 0);
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
    }
    fixture = "NULL handle and NULL outputs";
    for (mask = 0U; mask < 8U; mask++) {
        sqlparser_handle_t a = {0}, b = {0};
        sqlparser_view_expression_source_cache_t ac = cache_sentinel(0), bc = ac;
        compare_insert(NULL, NULL, &ac, &bc, 0, 0U, 0U, 0U, (unsigned)mask, 0);
        compare_insert(&a, &b, &ac, &bc, 0, 0U, 0U, 0U, (unsigned)mask, 0);
    }
}
static void cross_locale_certificate(void)
{
    static const char *const locales[] = {
        "tr_TR.UTF-8", "tr_TR.utf8", "tr_TR.ISO8859-9", "tr_TR", "Turkish_Turkey.1254", "C.UTF-8"
    };
    size_t d, i; int tested = 0;
    stage = "parse certificate and use it after an LC_CTYPE change";
    fixture = "INSERT ALL INTO t VALUES ('abc', 1) INTO u VALUES ('last', 2) SELECT 1 FROM dual";
    for (d = 0U; d < COUNT(oracle_dialects); d++) for (i = 0U; i < COUNT(locales); i++) {
        sqlparser_handle_t *a, *b;
        sqlparser_view_expression_source_cache_t ac, bc;
        int eligible;
        CHECK(setlocale(LC_CTYPE, "C") != NULL);
        a = parse_handle(oracle_dialects[d], fixture, 1); b = parse_reference_handle(oracle_dialects[d], fixture, 1);
        if (setlocale(LC_CTYPE, locales[i]) != NULL) {
            size_t ss = 0U, se = 0U, vp = 0U, cs = 0U, ce = 0U;
            ac = cache_sentinel(0); bc = ac;
            eligible = sqlparser_oracle_multi_insert_initial_cell_span(a, 0U, 0U, &ss, &se, &vp, &cs, &ce);
            if (tolower((unsigned char)'I') != 'i') CHECK(!eligible);
            compare_insert(a, b, &ac, &bc, 0, 0U, 0U, 0U, 0U, eligible);
            compare_insert(a, b, NULL, NULL, 0, 0U, 1U, 1U, 0U, eligible);
            tested++;
        }
        CHECK(setlocale(LC_CTYPE, "C") != NULL);
        ac = cache_sentinel(0); bc = ac;
        compare_insert(a, b, &ac, &bc, 0, 0U, 1U, 0U, 0U, 1);
        sqlparser_handle_destroy(a); sqlparser_handle_destroy(b);
    }
    printf("Cross-locale parse/use combinations exercised: %d (unavailable locales skipped)\n", tested);
}
static void generated_positive(void)
{
    char sql[8192]; size_t used = 0U, row, col, d; int n;
    stage = "many branches with varying column counts and keyword-like names";
    n = snprintf(sql, sizeof(sql), "INSERT ALL "); CHECK(n > 0); used = (size_t)n;
    for (row = 0U; row < 17U; row++) {
        n = snprintf(sql + used, sizeof(sql) - used, "INTO schema_%zu.values_table_%zu (", row, row);
        CHECK(n > 0 && (size_t)n < sizeof(sql) - used); used += (size_t)n;
        for (col = 0U; col <= row % 4U; col++) {
            n = snprintf(sql + used, sizeof(sql) - used, "%svalues_%zu", col ? "," : "", col);
            CHECK(n > 0 && (size_t)n < sizeof(sql) - used); used += (size_t)n;
        }
        n = snprintf(sql + used, sizeof(sql) - used, ") VALUES (");
        CHECK(n > 0 && (size_t)n < sizeof(sql) - used); used += (size_t)n;
        for (col = 0U; col <= row % 4U; col++) {
            n = snprintf(sql + used, sizeof(sql) - used, "%s'row_%zu_col_%zu_\xce\xa9_\"_''_'", col ? "," : "", row, col);
            CHECK(n > 0 && (size_t)n < sizeof(sql) - used); used += (size_t)n;
        }
        n = snprintf(sql + used, sizeof(sql) - used, ") ");
        CHECK(n > 0 && (size_t)n < sizeof(sql) - used); used += (size_t)n;
    }
    n = snprintf(sql + used, sizeof(sql) - used, "SELECT 1 + 2 AS value_x FROM dual");
    CHECK(n > 0 && (size_t)n < sizeof(sql) - used);
    for (d = 0U; d < COUNT(oracle_dialects); d++) verify_fixture(oracle_dialects[d], sql, 1, 1);
}
static void high_byte_trim_matrix(void)
{
    static const char *const locales[] = {"C", "C.UTF-8", "tr_TR.ISO8859-9", "tr_TR", "en_US.ISO8859-1", "fr_FR.ISO8859-1"};
    static const unsigned char bytes[] = {0x80U, 0xa0U, 0xffU};
    char sql[512]; size_t i, b, d; int n;
    stage = "non-ASCII bytes discarded around cell and source boundaries";
    for (i = 0U; i < COUNT(locales); i++) if (setlocale(LC_CTYPE, locales[i]) != NULL) {
        for (b = 0U; b < COUNT(bytes); b++) for (d = 0U; d < COUNT(oracle_dialects); d++) {
            n = snprintf(sql, sizeof(sql), "INSERT ALL INTO t VALUES ('abc',1) INTO u VALUES (%c'last'%c,2) SELECT 1 FROM dual", bytes[b], bytes[b]);
            CHECK(n > 0 && (size_t)n < sizeof(sql)); verify_fixture(oracle_dialects[d], sql, 0, 0);
            n = snprintf(sql, sizeof(sql), "INSERT ALL INTO t VALUES ('abc',1) INTO u VALUES ('last',2) SELECT 1 FROM dual%c", bytes[b]);
            CHECK(n > 0 && (size_t)n < sizeof(sql)); verify_fixture(oracle_dialects[d], sql, 0, 0);
        }
    }
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
}

int main(void)
{
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
    statement_matrix(); generic_matrix(); positive_matrix(); generated_positive(); negative_matrix();
    high_byte_trim_matrix(); helper_nulls(); final_checks(); ordinary_and_guards(); cross_locale_certificate();
    if (setlocale(LC_CTYPE, "C.UTF-8") != NULL) { statement_matrix(); generic_matrix(); positive_matrix(); negative_matrix(); }
    CHECK(positive_calls > 0U && negative_calls > 0U && parsed_negatives >= 30U);
    printf("Oracle initial span certificate: %zu frozen comparisons; %zu cold hits; %zu original-path trace comparisons; %zu parsed negative fixtures; %zu unsupported malformed inputs\n",
        comparisons, positive_calls, negative_calls, parsed_negatives, rejected_inputs);
    return 0;
}
