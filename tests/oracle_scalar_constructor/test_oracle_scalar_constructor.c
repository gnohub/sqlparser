/* Independent Oracle constructor differential checks.
 * Build this test with either the reference or current Oracle source and
 * its matching library. Expected behavior comes from the frozen legacy constructor, never
 * from the proposed shortcut or a second optimized parse of expected SQL.
 * The included frozen helper supplies complete public graph field records;
 * its main is renamed, and its optional wrapper hooks remain disabled. */
#ifndef SCALAR_FROZEN_HELPER
#error Supply SCALAR_FROZEN_HELPER as the owned-commit graph-record helper path
#endif
#ifndef SCALAR_ORACLE_SOURCE
#error Supply SCALAR_ORACLE_SOURCE as the Oracle production C path to inspect
#endif
#define main frozen_owned_commit_main
#include SCALAR_FROZEN_HELPER
#undef main
#include SCALAR_ORACLE_SOURCE
#include "frozen_legacy_parse_value_item.inc"
#include "scalar_state.h"
#include "scalar_alloc.h"

static void scalar_begin_record(const char *kind, size_t dialect, size_t fixture,
    size_t phase, size_t fail, int null_error, text_buffer *b)
{
    CHECK(record_sink == NULL); memset(b, 0, sizeof(*b)); record_sink = b;
    record_text(kind); record_number(dialect); record_number(fixture);
    record_number(phase); record_number(fail); record_number(null_error);
}
static void scalar_end_record(text_buffer *b)
{
    size_t i;
    CHECK(record_sink == b); record_sink = NULL;
    /* Hex means one physical line per record even for newline/control SQL.
     * A byte comparator retains every error byte and complete state field. */
    for (i = 0; i < b->length; ++i) printf("%02x", (unsigned char)b->data[i]);
    putchar('\n'); free(b->data); memset(b, 0, sizeof(*b));
}
static void scalar_record_bytes(const void *data, size_t length)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = data; size_t i;
    record_number(data != NULL); record_number(length);
    for (i = 0; i < length; ++i) {
        char pair[2] = {hex[p[i] >> 4], hex[p[i] & 15]};
        append_bytes(record_sink, pair, 2);
    }
}
static void scalar_record_origins(sqlparser_handle_t *h)
{
    const sqlparser_identifier_origin_map_t *map = NULL, *cached = NULL;
    sqlparser_error_t e = {0}; sqlparser_status_t status;
    size_t offset, length;
    status = sqlparser_identifier_origins_for_handle(h, &map, &e);
    scalar_record_error(status, &e); if (status != SQLPARSER_STATUS_OK) return;
    record_number(sqlparser_identifier_origin_map_output_length(map));
    for (offset = 0; offset <= h->parser_sql_len + 1; ++offset) {
        /* Exhaust all ranges for short fixtures. Large boundary fixtures use
         * every start with 0/1/2/full/out-of-bounds lengths plus full suffix.
         * No quadratic work is hidden in the larger width fixtures. */
        size_t limit = h->parser_sql_len <= 768 ? h->parser_sql_len + 1 - offset : 2;
        for (length = 0; length <= limit; ++length) {
            sqlparser_identifier_origin_t origin = {0};
            record_number(offset); record_number(length);
            record_number(sqlparser_identifier_origin_map_lookup(map, offset, length, &origin));
            record_number(origin.kind); record_number(origin.source_offset); record_number(origin.source_length);
        }
        if (h->parser_sql_len > 768) {
            sqlparser_identifier_origin_t origin = {0};
            length = offset <= h->parser_sql_len ? h->parser_sql_len - offset : 0;
            record_number(offset); record_number(length);
            record_number(sqlparser_identifier_origin_map_lookup(map, offset, length, &origin));
            record_number(origin.kind); record_number(origin.source_offset); record_number(origin.source_length);
        }
    }
    memset(&e, 0, sizeof(e));
    status = sqlparser_identifier_origins_for_handle(h, &cached, &e);
    scalar_record_error(status, &e); record_number(cached == map);
}
static void scalar_record_handle(sqlparser_handle_t *h, int origins, int graph)
{
    char *text = NULL; size_t i, j, count; sqlparser_error_t e = {0};
    sqlparser_status_t status;
    record_number(h != NULL); if (!h) return;
    record_text(h->sql); record_text(h->parser_sql); record_text(h->current_sql);
    record_text(h->current_parser_sql); record_number(h->sql_len); record_number(h->parser_sql_len);
    record_number(h->generation); record_number(h->statement_count);
    scalar_record_bytes(h->parse_tree.data, h->parse_tree.len);
    scalar_record_state(h->dialect_state, h);
    status = sqlparser_deparse(h, &text, &e); scalar_record_error(status, &e);
    record_text(text); free(text);
    if (graph) for (i = 0; i < sqlparser_statement_count(h); ++i) {
        sqlparser_query_graph_view_t g;
        memset(&e, 0, sizeof(e)); memset(&g, 0, sizeof(g));
        status = sqlparser_statement_query_graph(h, i, &g, &e);
        scalar_record_error(status, &e);
        if (status == SQLPARSER_STATUS_OK) record_graph(h, &g);
        count = 0; memset(&e, 0, sizeof(e));
        status = sqlparser_statement_literal_count(h, i, &count, &e);
        scalar_record_error(status, &e); record_number(count);
        if (status == SQLPARSER_STATUS_OK) for (j = 0; j < count; ++j) {
            sqlparser_literal_view_t literal; memset(&literal, 0, sizeof(literal));
            memset(&e, 0, sizeof(e));
            status = sqlparser_statement_literal(h, i, j, &literal, &e);
            scalar_record_error(status, &e);
            if (status == SQLPARSER_STATUS_OK) record_sqlparser_literal_view_t(&literal);
        }
    }
    if (origins) scalar_record_origins(h);
    scalar_record_state(h->dialect_state, h);
    if (h->ast) {
        size_t n = protobuf_c_message_get_packed_size((ProtobufCMessage *)h->ast);
        unsigned char *wire = malloc(n ? n : 1); CHECK(wire);
        CHECK(protobuf_c_message_pack((ProtobufCMessage *)h->ast, wire) == n);
        scalar_record_bytes(wire, n); free(wire);
    } else scalar_record_bytes(NULL, 0);
}

static const char *const scalar_tokens[] = {
    "''", "'a'", "''''", "'O''Reilly'", "'张三 Ω 😀'", "'-- :bind @db MINUS N''x'''",
    "0", "+0", "-0", "0000123", "+123", "-123", "2147483647", "2147483648",
    "-2147483648", "-2147483649", "9223372036854775807", "9223372036854775808",
    "-9223372036854775808", "-9223372036854775809", "999999999999999999999999999999999",
    ".0", "0.", "1.25", "+.5", "-.5", "-0.", "+000.001", "999999999999.999999999",
    "NULL", "null", "NuLl", "current_date", "CURRENT_TIME", "current_timestamp",
    "localtime", "LOCALtimestamp", "current_role", "CURRENT_USER", "session_user", "user",
    "current_catalog", "CURRENT_SCHEMA", "  'trimmed' \t\r\n", "\t -12.5 \n",
    "N'national'", "n'张三'", "q'[O'Reilly]'", "Q'{Ω :q MINUS}'", "nq'<national>'",
    "$$dollar :d MINUS$$", "$tag$quoted N'x'$tag$", ":named", ":1", "?", "$1",
    "CURRENT_TIMESTAMP(3)", "localtime(0)", "current_time (6)", "current_date_extra",
    "current_userx", "user_name", "NULLIF(1, 2)", "NULLx", "xNULL", "NULL::text",
    "1e2", "1E-2", "0x12", "1_000", "1..2", ".", "+", "-", "++1", "--1",
    "1 + 2", "- 1", "(+1)", "f('before', N'after', :p)", "'a' || 'b'",
    "'slash\\x'", "'new\nline'", "'tab\ttext'", "'del\177text'", "'control\001text'",
    "'a' 'b'", "'a'b'", "'unterminated", "q'[unterminated'", "'a'/*tail*/",
    "/*head*/'a'", "1 --tail\n", "/* unterminated", "'a';", "", " \t\r\n",
    "(SELECT N'before' FROM T@L MINUS SELECT 'after' FROM U@M)",
    "(SELECT :b FROM T EXCEPT SELECT :c FROM U)", "CONNECT_BY_ROOT x", "PRIOR x",
    "'utf8-\xc3\xa9'", "'invalid-utf8-\xff'", "current_schema/*x*/", "USER()"
};
static const char *const scalar_seeds[] = {
    "",
    "SELECT 'ordinary', N'national', :before FROM T@remote MINUS SELECT 'other', N'n2', :after FROM U@other EXCEPT SELECT 'third', N'n3', :last FROM V",
    "UPDATE T SET C = :before RETURNING C INTO :out; SELECT PRIOR id, CONNECT_BY_ROOT name FROM Tree START WITH parent IS NULL CONNECT BY PRIOR id = parent"
};
static const char *const scalar_tail[] = {"'after'", "N'after-national'", ":after_bind",
    "(SELECT N'tail' FROM AfterTable@AfterLink MINUS SELECT 'last' FROM LastTable EXCEPT SELECT 'end' FROM EndTable)"};
static char *scalar_boundary_tokens[20];
static size_t scalar_token_count(void)
{ return COUNT(scalar_tokens) + COUNT(scalar_boundary_tokens); }
static const char *scalar_token(size_t i)
{ return i < COUNT(scalar_tokens) ? scalar_tokens[i] : scalar_boundary_tokens[i - COUNT(scalar_tokens)]; }
static void scalar_init_boundaries(void)
{
    static const size_t lengths[] = {127,128,129,255,256,257,511,512,513,1024};
    size_t i, kind, j;
    for (kind = 0; kind < 2; ++kind) for (i = 0; i < COUNT(lengths); ++i) {
        text_buffer b = {0}; append(&b, "'");
        for (j = 0; j < lengths[i]; ++j)
            append(&b, kind && j % 11 == 0 ? "''" : "a");
        append(&b, "'"); scalar_boundary_tokens[kind * COUNT(lengths) + i] = b.data;
    }
}
static void scalar_seed_error(sqlparser_error_t *e, int mode)
{
    memset(e, 0, sizeof(*e));
    if (mode == 2) {
        e->code = SQLPARSER_STATUS_UNSUPPORTED; e->cursor = -17;
        e->line = 31; e->column = 47; strcpy(e->message, "preexisting-error-must-not-be-sanitized");
    }
}

static sqlparser_oracle_state_t *scalar_make_seed(size_t seed)
{
    sqlparser_oracle_state_t *s = NULL; char *sql = NULL; sqlparser_error_t e = {0};
    CHECK(sqlparser_oracle_state_new(&s, &e) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_oracle_preprocess_text(scalar_seeds[seed < COUNT(scalar_seeds) ? seed : 1], s, &sql, &e) == SQLPARSER_STATUS_OK);
    free(sql);
    if (seed >= COUNT(scalar_seeds))
        s->national_literals.literal_count = seed == COUNT(scalar_seeds) ? SIZE_MAX - 1 : SIZE_MAX;
    /* Nonzero fragment positions reveal mistaken resets as well as counts. */
    sqlparser_dialect_national_literals_begin_fragment(&s->national_literals);
    sqlparser_dialect_minuses_begin_fragment(&s->minuses);
    return s;
}
typedef sqlparser_status_t (*scalar_constructor_fn)(const char *, size_t, size_t,
    sqlparser_oracle_state_t *, sqlparser_dialect_multi_insert_value_t *, uint32_t *, sqlparser_error_t *);

static char *scalar_primitive_once(scalar_constructor_fn constructor, size_t token, size_t seed,
    size_t fail, int null_error, size_t *calls)
{
    sqlparser_oracle_state_t *s = scalar_make_seed(seed);
    sqlparser_dialect_multi_insert_value_t value; sqlparser_error_t e = {0};
    sqlparser_status_t status; uint32_t flags = UINT32_MAX; size_t i;
    text_buffer b = {0}; char *input = copy_text(scalar_token(token));
    scalar_seed_error(&e, null_error);
    CHECK(!record_sink); record_sink = &b;
    scalar_record_state(s, NULL);
    scalar_start(fail);
    status = constructor(input, 0, strlen(input), s, &value, &flags, null_error == 1 ? NULL : &e);
    scalar_stop(); *calls = scalar_calls;
    /* Capture the exact result/error before any further API can clear it. */
    scalar_record_error(status, null_error == 1 ? NULL : &e);
    scalar_record_allocations(); record_number(flags); scalar_record_value(&value);
    scalar_record_state(s, NULL);
    memset(input, 'x', strlen(input)); free(input);
    if (status == SQLPARSER_STATUS_OK && value.public_sql && value.parser_sql) {
        char *saved = copy_text(value.parser_sql);
        CHECK(value.public_sql != value.parser_sql);
        if (*value.public_sql) value.public_sql[0] ^= 1;
        CHECK(strcmp(value.parser_sql, saved) == 0); free(saved);
    }
    sqlparser_oracle_value_clear(&value);
    /* Continue the exact existing state even after failure: state mutations
     * before swallowed/propagated OOM must agree with the frozen constructor. */
    for (i = 0; i < COUNT(scalar_tail); ++i) {
        memset(&e, 0, sizeof(e)); flags = 0;
        status = constructor(scalar_tail[i], 0, strlen(scalar_tail[i]), s, &value, &flags, &e);
        scalar_record_error(status, &e); record_number(flags); scalar_record_value(&value);
        scalar_record_state(s, NULL); sqlparser_oracle_value_clear(&value);
    }
    {
        void *clone = NULL;
        memset(&e, 0, sizeof(e)); status = sqlparser_oracle_clone_state(s, &clone, &e);
        scalar_record_error(status, &e); scalar_record_state(clone, NULL);
        sqlparser_oracle_state_destroy(s); s = NULL;
        scalar_record_state(clone, NULL); sqlparser_oracle_state_destroy(clone);
    }
    CHECK(!scalar_live && !scalar_native_depth); record_sink = NULL;
    return b.data;
}
static void scalar_primitive(int allocations)
{
    size_t token, seed, fail, calls, legacy_calls; int null_error;
    for (token = 0; token < scalar_token_count(); ++token)
    for (seed = 0; seed < COUNT(scalar_seeds) + 2; ++seed)
    for (null_error = 0; null_error < 3; ++null_error) {
        size_t stop = allocations ? 65535 : 0;
        for (fail = 0; fail <= stop; ++fail) {
            char *actual = scalar_primitive_once(sqlparser_oracle_parse_value_item, token, seed, fail, null_error, &calls);
            char *legacy = scalar_primitive_once(frozen_legacy_parse_value_item, token, seed, fail, null_error, &legacy_calls);
            text_buffer b;
            CHECK(calls == legacy_calls); same_text(actual, legacy);
            scalar_begin_record(allocations ? "primitive-oom" : "primitive", 0, token, seed, fail, null_error, &b);
            record_text(actual); scalar_end_record(&b); free(actual); free(legacy);
            if (!allocations || (fail && calls < fail)) break;
        }
        CHECK(!allocations || fail <= stop);
    }
}

static const char *const scalar_sql[] = {
    "INSERT ALL INTO T(A,B,C,D,E,F,G,H,I) VALUES ('old','O''Reilly','张三',-1,+2,.5,1.,NULL,CURRENT_DATE) INTO U(A) VALUES ('tail') SELECT 1 FROM Dual",
    "INSERT ALL INTO T(A,B,C,D,E,F) VALUES (:before, N'national', 'middle', q'[q-string]', 'after', :last) INTO U(A,B) VALUES ('tail', N'last-national') SELECT N'source', :source FROM Src@Link MINUS SELECT 'other', :other FROM OtherSrc EXCEPT SELECT 'end', :end FROM EndSrc",
    "INSERT FIRST WHEN :b = 1 THEN INTO T(A,B,C) VALUES (N'before', 'cell', :after) WHEN x = 'x' THEN INTO U(A) VALUES ('u') ELSE INTO V(A,B) VALUES ('v', CURRENT_USER) SELECT x FROM S@Remote",
    "INSERT ALL INTO T(A,B) VALUES ('first', N'next') SELECT PRIOR id, CONNECT_BY_ROOT name FROM Tree START WITH parent IS NULL CONNECT BY PRIOR id = parent",
    "INSERT ALL INTO T(A,B) VALUES (1, 'before') SELECT 1 FROM Dual; UPDATE U SET C='after' RETURNING C INTO :out",
    "UPDATE U SET C=N'before' RETURNING C INTO :out; INSERT ALL INTO T(A,B) VALUES ('after', :in) SELECT 1 FROM Dual",
    "INSERT ALL INTO T VALUES ('one') INTO U VALUES ('two', 2, NULL) INTO V VALUES (CURRENT_SCHEMA, 'three') SELECT 1 FROM Dual",
    "INSERT ALL INTO T(A,B) VALUES ('x', .) SELECT 1 FROM Dual",
    "INSERT ALL INTO T(A,B) VALUES ('x', 'unterminated) SELECT 1 FROM Dual",
    "INSERT ALL INTO T(A,B) VALUES ('x', CURRENT_TIMESTAMP(3)) SELECT 1 FROM Dual",
    "INSERT ALL INTO \"MixedCase\"@\"Remote\"(\"Col\",B) VALUES ('x', :b) SELECT 1 FROM Dual"
};
static const sqlparser_dialect_t scalar_dialects[] = {
    SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE
};
static sqlparser_handle_t *scalar_parse_owned(sqlparser_dialect_t dialect, const char *sql,
    sqlparser_status_t *status, sqlparser_error_t *e, int null_error, size_t fail, int trace)
{
    sqlparser_parse_options_t options; sqlparser_handle_t *h = NULL;
    char *owned = copy_text(sql);
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    options.limits.max_sql_bytes = 8U * 1024U * 1024U;
    options.limits.max_output_bytes = 32U * 1024U * 1024U;
    if (trace) scalar_start(fail);
    *status = sqlparser_parse_with_options(owned, &options, &h, null_error == 1 ? NULL : e);
    if (trace) scalar_stop();
    memset(owned, 'x', strlen(owned)); free(owned);
    return h;
}
static void scalar_handle_case(sqlparser_dialect_t dialect, const char *sql, size_t fixture, int mutate)
{
    sqlparser_error_t e = {0}; sqlparser_status_t status;
    sqlparser_handle_t *h; text_buffer b; size_t round;
    h = scalar_parse_owned(dialect, sql, &status, &e, 0, 0, 1);
    scalar_begin_record("constructor", dialect, fixture, 0, 0, 0, &b);
    scalar_record_error(status, &e); scalar_record_allocations();
    if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 1, 1);
    else record_number(h != NULL);
    scalar_end_record(&b);
    if (status == SQLPARSER_STATUS_OK && h) {
        sqlparser_handle_t *clone = NULL;
        memset(&e, 0, sizeof(e)); status = sqlparser_handle_clone(h, &clone, &e);
        scalar_begin_record("clone", dialect, fixture, 0, 0, 0, &b);
        scalar_record_error(status, &e); sqlparser_handle_destroy(h); h = clone;
        if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 1, 1);
        else record_number(h != NULL);
        scalar_end_record(&b);
    }
    if (status == SQLPARSER_STATUS_OK && h && mutate) for (round = 0; round < 7; ++round) {
        sqlparser_patch_t patches[2] = {{0}}; sqlparser_literal_value_t literal = {0};
        sqlparser_patch_list_t list = {patches, round == 4 ? 2 : 1};
        char *raw = copy_text(round == 1 ? "N'national-after'" : round == 2 ? ":changed_bind" :
            round == 3 ? "CURRENT_TIMESTAMP" : "'raw-''Ω'''");
        char *decoded = copy_text("typed-张三-'quoted'");
        patches[0].op = SQLPARSER_PATCH_REPLACE; patches[0].selector = "stmt[0].insert_cell[0][0]";
        if (round == 0 || round == 5) {
            literal.kind = SQLPARSER_LITERAL_KIND_STRING; literal.string_value = decoded;
            patches[0].literal = &literal;
        } else patches[0].sql = raw;
        patches[1] = patches[0]; patches[1].sql = "'duplicate-final'"; patches[1].literal = NULL;
        memset(&e, 0, sizeof(e));
        status = round == 6 ? sqlparser_oracle_multi_insert_insert_column_sql(
            h, 0, 0, 0, "ExtraColumn", "'new-column-cell'", &e) : sqlparser_apply_patch(h, &list, &e);
        memset(raw, 'x', strlen(raw)); memset(decoded, 'x', strlen(decoded)); free(raw); free(decoded);
        scalar_begin_record("patch", dialect, fixture, round, 0, 0, &b);
        scalar_record_error(status, &e);
        if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 1, 1);
        else record_number(h != NULL);
        scalar_end_record(&b);
        if (status != SQLPARSER_STATUS_OK) break;
        if (round == 2) {
            sqlparser_handle_t *clone = NULL;
            memset(&e, 0, sizeof(e)); status = sqlparser_handle_clone(h, &clone, &e);
            scalar_begin_record("patched-clone", dialect, fixture, round, 0, 0, &b);
            scalar_record_error(status, &e); sqlparser_handle_destroy(h); h = clone;
            if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 1, 1);
            else record_number(h != NULL);
            scalar_end_record(&b); if (status != SQLPARSER_STATUS_OK || !h) break;
        }
    }
    sqlparser_handle_destroy(h); CHECK(!scalar_live && !scalar_native_depth);
}
static char *scalar_width_fixture(size_t width)
{
    text_buffer b = {0}; size_t row, col;
    static const char *const values[] = {"'ordinary'", "-2147483649", "+.5", "NULL",
        "CURRENT_TIMESTAMP", "N'national'", ":bind", "q'[q]'", "'Ω''张三'"};
    append(&b, "INSERT ALL ");
    for (row = 0; row < 3; ++row) {
        size_t n = row == 1 ? 1 : row == 2 ? width + 1 : width;
        append_format(&b, "INTO T%zu VALUES (", row);
        for (col = 0; col < n; ++col) {
            if (col) append(&b, ", "); append(&b, values[(row + col) % COUNT(values)]);
        }
        append(&b, ") ");
    }
    append(&b, "SELECT N'source', :src FROM SourceTable@Remote MINUS SELECT 'end', :end FROM OtherTable");
    return b.data;
}
static void scalar_handles(void)
{
    size_t d, i; static const size_t widths[] = {1,2,3,8,9,10,31,32,33,63,64,65};
    for (d = 0; d < COUNT(scalar_dialects); ++d) {
        for (i = 0; i < COUNT(scalar_sql); ++i)
            scalar_handle_case(scalar_dialects[d], scalar_sql[i], i, i < 4 || i == 6);
        for (i = 0; i < COUNT(widths); ++i) {
            char *sql = scalar_width_fixture(widths[i]);
            scalar_handle_case(scalar_dialects[d], sql, 100 + i, 1); free(sql);
        }
        for (i = 0; i < scalar_token_count(); ++i) {
            text_buffer b = {0}; append(&b, "INSERT ALL INTO T VALUES (N'before', ");
            append(&b, scalar_token(i)); append(&b, ", 'after', N'last', :tail) SELECT 1 FROM Dual");
            scalar_handle_case(scalar_dialects[d], b.data, 1000 + i, 0); free(b.data);
        }
    }
}
static void scalar_constructor_oom(void)
{
    size_t d, fixture, fail, calls; int cloning, null_error;
    /* Full trace/success payload on every ordinal. Optional-proof and swallowed
     * failures remain successful records with their precise state and error. */
    for (d = 0; d < COUNT(scalar_dialects); ++d)
    for (fixture = 0; fixture < 3; ++fixture)
    for (cloning = 0; cloning < 2; ++cloning)
    for (null_error = 0; null_error < 3; ++null_error) {
        sqlparser_error_t e = {0}; sqlparser_status_t status;
        sqlparser_handle_t *source = NULL;
        if (cloning) {
            source = scalar_parse_owned(scalar_dialects[d], scalar_sql[fixture], &status, &e, 0, 0, 0);
            CHECK(status == SQLPARSER_STATUS_OK && source);
            /* The legacy mixed national-literal fixtures can legitimately
             * reject AST owner binding. Clone their original parsed state;
             * the plain fixture separately covers an AST-backed source. */
            if (fixture == 0)
                CHECK(sqlparser_handle_ensure_ast(source, &e) == SQLPARSER_STATUS_OK);
        }
        for (fail = 0; fail < 65536; ++fail) {
            text_buffer b; sqlparser_handle_t *h = NULL;
            scalar_seed_error(&e, null_error);
            if (cloning) {
                scalar_start(fail); status = sqlparser_handle_clone(source, &h, null_error == 1 ? NULL : &e); scalar_stop();
            } else h = scalar_parse_owned(scalar_dialects[d], scalar_sql[fixture], &status, &e, null_error, fail, 1);
            calls = scalar_calls;
            scalar_begin_record(cloning ? "clone-oom" : "constructor-oom", scalar_dialects[d], fixture, 0, fail, null_error, &b);
            scalar_record_error(status, null_error == 1 ? NULL : &e); scalar_record_allocations();
            if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 0, 1);
            else record_number(h != NULL);
            sqlparser_handle_destroy(h);
            scalar_record_residue(); scalar_end_record(&b);
            if (scalar_live) fprintf(stderr, "legacy-residue dialect=%d fixture=%zu clone=%d error-mode=%d fail=%zu live=%zu\n",
                scalar_dialects[d], fixture, cloning, null_error, fail, scalar_live);
            scalar_reclaim_recorded_residue(); CHECK(!scalar_native_depth);
            if (fail && calls < fail) { CHECK(status == SQLPARSER_STATUS_OK); break; }
        }
        CHECK(fail < 65536); sqlparser_handle_destroy(source);
    }
}
static void scalar_admission(void)
{
#ifdef SCALAR_OPTIMIZED
    static const char *const strings[] = {"''", "''''", "'a''b'", "'张三 Ω 😀'", "'NULL :bind @remote MINUS'"};
    static const char *const scalars[] = {"NULL", "nUlL", "0", "+0", "-0", "0001", "+123", "-123",
        "9223372036854775808", "-9223372036854775809", ".5", "1.", "+.5", "-0.00",
        "current_date", "CURRENT_TIME", "current_timestamp", "localtime", "LOCALtimestamp",
        "current_role", "CURRENT_USER", "session_user", "user", "current_catalog", "CURRENT_SCHEMA"};
    static const char *const fallback[] = {NULL, "", " ", "SYSTEM_USER", "system_user", "CURRENT_TIMESTAMP(3)",
        "CURRENT_TIME (6)", "LOCALTIME(0)", "CURRENT_DATE_extra", "NULLx", "user_name", "USER()",
        "N'x'", "q'[x]'", "nq'{x}'", "$$x$$", "$t$x$t$", "E'x'", "B'01'", "U&'x'",
        ":bind", "?", "$1", "1e2", "1E-2", "0x1", "1_000", ".", "+", "-", "1..0",
        "1+2", "- 1", "(+1)", "'x' || 'y'", "'x'/*comment*/", "/*comment*/1", "--1",
        "'slash\\x'", "'new\nline'", "'tab\ttext'", "'del\177text'", "'control\001text'",
        "'a' 'b'", "'a'b'", "'unterminated", "'x';", "'x' ", " 'x'"};
    size_t i, n, checks = 0;
    for (i = 0; i < COUNT(strings); ++i) {
        n = SIZE_MAX; CHECK(sqlparser_oracle_value_identity_kind(strings[i], &n) == 1);
        CHECK(n == strlen(strings[i])); ++checks;
    }
    for (i = 0; i < COUNT(scalars); ++i) {
        char *suffix; size_t length = strlen(scalars[i]);
        n = SIZE_MAX; CHECK(sqlparser_oracle_value_identity_kind(scalars[i], &n) == 2);
        CHECK(n == length); ++checks;
        suffix = malloc(length + 2); CHECK(suffix); memcpy(suffix, scalars[i], length);
        suffix[length] = 'x'; suffix[length + 1] = 0;
        CHECK(sqlparser_oracle_value_identity_kind(suffix, &n) == 0); ++checks; free(suffix);
    }
    for (i = 0; i < COUNT(fallback); ++i) {
        n = SIZE_MAX; CHECK(sqlparser_oracle_value_identity_kind(fallback[i], &n) == 0); ++checks;
    }
    for (i = 0; i < COUNT(scalar_boundary_tokens); ++i) {
        n = SIZE_MAX; CHECK(sqlparser_oracle_value_identity_kind(scalar_boundary_tokens[i], &n) == 1);
        CHECK(n == strlen(scalar_boundary_tokens[i])); ++checks;
    }
    printf("optimized-admission-checks=%zu\n", checks);
#else
    fputs("--admission requires SCALAR_OPTIMIZED build\n", stderr); exit(2);
#endif
}
int main(int argc, char **argv)
{
    size_t i;
    if (argc != 2) { fputs("usage: test_oracle_scalar_constructor --primitive|--primitive-oom|--handles|--constructor-oom\n", stderr); return 2; }
    scalar_init_boundaries();
    if (!strcmp(argv[1], "--primitive")) scalar_primitive(0);
    else if (!strcmp(argv[1], "--primitive-oom")) scalar_primitive(1);
    else if (!strcmp(argv[1], "--handles")) scalar_handles();
    else if (!strcmp(argv[1], "--constructor-oom")) scalar_constructor_oom();
    else if (!strcmp(argv[1], "--admission")) scalar_admission();
    else return 2;
    for (i = 0; i < COUNT(scalar_boundary_tokens); ++i) free(scalar_boundary_tokens[i]);
    CHECK(!record_sink && !scalar_live && !scalar_active && !scalar_native_depth);
    return 0;
}
