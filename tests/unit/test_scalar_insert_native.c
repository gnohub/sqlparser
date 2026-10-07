/* General scalar VALUES wire/error parity against the ordinary observed entry point,
 * which never opts into source-certified construction. Optional GNU wrappers
 * assert actual grammar admission/fallback and inject native allocator failures.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "sqlparser_internal.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "src/pg_query_internal.h"
#include "src/pg_query_observer.h"
#include "common/keywords.h"
#include "parser/parser.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static const char *stage = "start";
static size_t cases;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s stage=%s cases=%zu\n", __FILE__, __LINE__, #x, stage, cases); abort(); } } while (0)

#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
static size_t grammar_calls, native_calls, native_attempts, fail_at, injected;
static int native_depth, armed, check_admission;
List *__real_raw_parser_with_options(const char *, RawParseMode, bool);
List *__wrap_raw_parser_with_options(const char *sql, RawParseMode mode, bool preserve)
{
    grammar_calls++;
    /* Failed source admission must have allocated no native nodes. */
    CHECK(!check_admission || native_calls == 0U);
    CHECK(!armed || native_attempts == 0U);
    return __real_raw_parser_with_options(sql, mode, preserve);
}
void *__real_palloc(Size);
void *__real_palloc0(Size);
void *__real_MemoryContextAlloc(MemoryContext, Size);
void *__real_repalloc(void *, Size);
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
char *__real_strdup(const char *);
char *__real_strndup(const char *, size_t);
/* Static metadata does not perturb allocator failure positions. Track all
 * wrapped allocations acquired while injection is armed through shutdown. */
static struct { void *pointer; size_t size; } native_blocks[4096];
static size_t native_live_bytes, native_live_blocks;
static void native_forget(void *pointer)
{
    for (size_t i = 0U; pointer != NULL && i < 4096U; ++i)
        if (native_blocks[i].pointer == pointer) {
            native_live_bytes -= native_blocks[i].size;
            native_live_blocks--;
            native_blocks[i].pointer = NULL; native_blocks[i].size = 0U;
            return;
        }
}
static int native_tracked(void *pointer)
{
    for (size_t i = 0U; pointer != NULL && i < 4096U; ++i)
        if (native_blocks[i].pointer == pointer) return (int)i + 1;
    return 0;
}
static void native_remember(void *pointer, size_t size)
{
    if (pointer == NULL) return;
    CHECK(!native_tracked(pointer));
    for (size_t i = 0U; i < 4096U; ++i)
        if (native_blocks[i].pointer == NULL) {
            native_blocks[i].pointer = pointer; native_blocks[i].size = size;
            native_live_bytes += size; native_live_blocks++;
            return;
        }
    CHECK(0);
}
void *__wrap_palloc(Size size)
{
    void *p;
    native_calls++;
    native_depth++;
    p = __real_palloc(size);
    native_depth--;
    return p;
}
void *__wrap_palloc0(Size size)
{
    void *p;
    native_calls++;
    native_depth++;
    p = __real_palloc0(size);
    native_depth--;
    return p;
}
void *__wrap_MemoryContextAlloc(MemoryContext context, Size size)
{
    void *p;
    native_calls++;
    native_depth++;
    p = __real_MemoryContextAlloc(context, size);
    native_depth--;
    return p;
}
void *__wrap_repalloc(void *pointer, Size size)
{
    void *p;
    native_calls++;
    native_depth++;
    p = __real_repalloc(pointer, size);
    native_depth--;
    return p;
}
static int native_allocation_failure(void)
{
    if (armed && native_depth > 0 && ++native_attempts == fail_at)
    {
        injected++;
        return 1;
    }
    return 0;
}
void *__wrap_malloc(size_t size)
{
    void *pointer = native_allocation_failure() ? NULL : __real_malloc(size);
    if (armed) native_remember(pointer, size);
    return pointer;
}
void *__wrap_realloc(void *pointer, size_t size)
{
    int tracked = native_tracked(pointer);
    void *next;
    if (native_allocation_failure()) return NULL;
    next = __real_realloc(pointer, size);
    if (next != NULL) {
        if (tracked) {
            size_t slot = (size_t)tracked - 1U;
            native_live_bytes -= native_blocks[slot].size; native_live_blocks--;
            native_blocks[slot].pointer = NULL; native_blocks[slot].size = 0U;
        }
        if (tracked || armed) native_remember(next, size);
    }
    return next;
}
char *__wrap_strdup(const char *text)
{
    char *pointer = __real_strdup(text);
    if (armed) native_remember(pointer, strlen(text) + 1U);
    return pointer;
}
char *__wrap_strndup(const char *text, size_t size)
{
    char *pointer = __real_strndup(text, size);
    if (armed) native_remember(pointer, strnlen(text, size) + 1U);
    return pointer;
}
void __wrap_free(void *pointer)
{
    native_forget(pointer);
    __real_free(pointer);
}
#endif

typedef struct observation
{
    size_t calls;
    size_t statements;
    PgQueryProtobuf wire;
} observation;

static void observe(const PgQuery__ParseResult *tree, void *context)
{
    observation *o = context;
    CHECK(o->calls == 0U);
    o->calls++;
    o->statements = tree->n_stmts;
    o->wire.len = pg_query__parse_result__get_packed_size(tree);
    o->wire.data = malloc(o->wire.len ? o->wire.len : 1U);
    CHECK(o->wire.data != NULL);
    CHECK(pg_query__parse_result__pack(tree, (unsigned char *)o->wire.data) == o->wire.len);
}

static void text_equal(const char *a, const char *b)
{
    CHECK((a == NULL) == (b == NULL));
    if (a != NULL) CHECK(strcmp(a, b) == 0);
}

static void wire_equal(PgQueryProtobuf a, PgQueryProtobuf b)
{
    CHECK(a.len == b.len);
    CHECK((a.data == NULL) == (b.data == NULL));
    if (a.len) CHECK(memcmp(a.data, b.data, a.len) == 0);
}

#ifdef SQLPARSER_SCALAR_INSERT_RECORD
/* The same test executable linked against the immutable native baseline emits
 * a complete-byte oracle. Its ordinary observed entry point always invokes
 * the original lexer and grammar. Compare these records with cmp, not hashes. */
static void record_bytes(const void *bytes, size_t length)
{
    uint64_t size = bytes == NULL ? UINT64_MAX : (uint64_t)length;
    CHECK(fwrite(&size, sizeof(size), 1U, stdout) == 1U);
    if (bytes != NULL && length != 0U)
        CHECK(fwrite(bytes, 1U, length, stdout) == length);
}
static void record_text(const char *text)
{
    record_bytes(text, text != NULL ? strlen(text) : 0U);
}
static void record_result(PgQueryProtobufParseResult result, observation observed,
                          size_t statements, int certified)
{
    int fields[4] = {result.error != NULL, certified, 0, 0};
    uint64_t counts[3] = {(uint64_t)statements, (uint64_t)observed.calls, (uint64_t)observed.statements};
    if (result.error != NULL)
    {
        fields[2] = result.error->cursorpos;
        fields[3] = result.error->lineno;
    }
    record_bytes(fields, sizeof(fields)); record_bytes(counts, sizeof(counts));
    record_text(result.stderr_buffer);
    record_bytes(result.parse_tree.data, result.parse_tree.len);
    record_bytes(observed.wire.data, observed.wire.len);
    if (result.error != NULL)
    {
        record_text(result.error->message); record_text(result.error->filename);
        record_text(result.error->funcname); record_text(result.error->context);
    }
}
#endif

static void parity(const char *sql, int options, int expected_fast)
{
    size_t count = 0U;
    int certified = 0;
    observation actual_observer = {0}, reference_observer = {0};
    PgQueryProtobufParseResult actual, reference;
    char *unchanged = malloc(strlen(sql) + 1U);
    CHECK(unchanged != NULL);
    strcpy(unchanged, sql);
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    grammar_calls = native_calls = 0U;
    check_admission = 1;
#else
    (void)expected_fast;
#endif
    actual = pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(
        sql, options, observe, &actual_observer, &count, &certified);
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    check_admission = 0;
    if (expected_fast >= 0) CHECK(grammar_calls == (expected_fast ? 0U : 1U));
    grammar_calls = 0U;
#endif
    reference = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        sql, options, observe, &reference_observer);
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    CHECK(grammar_calls == 1U);
#endif
    CHECK(strcmp(sql, unchanged) == 0);
    CHECK((actual.error == NULL) == (reference.error == NULL));
    text_equal(actual.stderr_buffer, reference.stderr_buffer);
    if (actual.error != NULL)
    {
        text_equal(actual.error->message, reference.error->message);
        text_equal(actual.error->filename, reference.error->filename);
        text_equal(actual.error->funcname, reference.error->funcname);
        text_equal(actual.error->context, reference.error->context);
        CHECK(actual.error->cursorpos == reference.error->cursorpos);
        CHECK(actual.error->lineno == reference.error->lineno);
        CHECK(actual_observer.calls == 0U && reference_observer.calls == 0U);
    }
    else
    {
        CHECK(reference_observer.calls == 1U);
        wire_equal(reference_observer.wire, reference.parse_tree);
        if (certified)
        {
            CHECK(actual_observer.calls == 0U);
            CHECK(count == reference_observer.statements);
        }
        else
        {
            CHECK(actual_observer.calls == 1U);
            wire_equal(actual_observer.wire, reference_observer.wire);
        }
    }
    wire_equal(actual.parse_tree, reference.parse_tree);
    pg_query_exit();
    wire_equal(actual.parse_tree, reference.parse_tree);
#ifdef SQLPARSER_SCALAR_INSERT_RECORD
    record_result(actual, actual_observer, count, certified);
#endif
    pg_query_free_protobuf_parse_result(actual);
    pg_query_free_protobuf_parse_result(reference);
    free(actual_observer.wire.data);
    free(reference_observer.wire.data);
    free(unchanged);
    cases++;
}

/* Use one varied row repeatedly so only the source construction, never the
 * native expected AST or output bytes, is shared with the implementation. */
static char *fixture(size_t rows, size_t padding, const char *table,
                     const char *columns, const char *values, const char *tail)
{
    size_t capacity = padding + strlen(table) + strlen(columns) + strlen(tail) +
        256U + rows * (strlen(values) + 8U);
    char *sql = malloc(capacity);
    size_t used = padding;
    CHECK(sql != NULL);
    memset(sql, ' ', padding);
    used += (size_t)snprintf(sql + used, capacity - used,
                            "InSeRt INTO %s(%s) VaLuEs ", table, columns);
    for (size_t i = 0; i < rows; ++i)
        used += (size_t)snprintf(sql + used, capacity - used,
                                "%s(%s)", i ? "," : "", values);
    CHECK(used + strlen(tail) < capacity);
    strcpy(sql + used, tail);
    return sql;
}

static void check_fixture(size_t rows, size_t padding, const char *table,
                          const char *columns, const char *values,
                          const char *tail, int options, int fast)
{
    char *sql = fixture(rows, padding, table, columns, values, tail);
    parity(sql, options, fast);
    free(sql);
}

static void scalar(const char *text, int fast)
{
    char values[16384];
    CHECK(strlen(text) + 24U < sizeof(values));
    snprintf(values, sizeof(values), "'张三李四',%s,CURRENT_DATE", text);
    check_fixture(32U, 4096U, "Test_Lib.TargetTable", "FirstName,Amount,WhenMade",
                  values, "; \t\n", 0, fast);
}

static void positives(void)
{
    static const char *numbers[] = {
        "0", "00000", "1", "2147483647", "2147483648", "2147483649",
        "4294967296", "9223372036854775808", "0002147483648",
        "-0", "-7", "-2147483647", "-2147483648", "-0002147483648",
        "- \t\r\n\v\f17", "- \t\r\n\v\f2147483648",
        "1.0", "-1.0", "100.50", "-0.0", "1.", ".1", "-.1", "- 1.",
        "00000.0", "0002147483648.00", "1e2", "1E2", "-1e2", "1e+2",
        "1e-2", "1.e2", ".1e2", "- .1E-01", "1.00e+00000001",
        "0001E2", "2147483647E0", "0e999999999999999999999999999999"
    };
    static const char *functions[] = {
        "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP", "LOCALTIME",
        "LOCALTIMESTAMP", "CURRENT_ROLE", "CURRENT_USER", "SESSION_USER",
        "USER", "CURRENT_CATALOG", "CURRENT_SCHEMA", "cUrReNt_TiMeStAmP",
        "CURRENT_TIME(0)", "CURRENT_TIME(6)", "CURRENT_TIME(2147483647)",
        "CURRENT_TIMESTAMP(0)", "CURRENT_TIMESTAMP(6)",
        "CURRENT_TIMESTAMP(000000000000000000000000000001)",
        "CURRENT_TIMESTAMP \t\n( \t\n 2147483647 \t\n )",
        "LOCALTIME(0)", "LOCALTIME(3)", "LOCALTIMESTAMP(0)", "LOCALTIMESTAMP(9)"
    };
    static const char *strings[] = {
        "''", "'ordinary ASCII string'", "'张三李四'", "'é'", "'€'", "'😀'",
        "'a é € 😀 z'", "'--/*;,+1.0e2 CURRENT_TIMESTAMP(6)'"
    };
    stage = "general scalar positives";
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) scalar(numbers[i], 1);
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i) scalar(functions[i], 1);
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); ++i) scalar(strings[i], 1);
    for (size_t columns = 1U; columns <= 19U; ++columns)
    {
        char names[512] = "", values[2048] = "";
        static const char *cells[] = {"'张三李四'", "1", "-2147483648", "100.50", "CURRENT_TIMESTAMP", "CURRENT_TIME(6)"};
        for (size_t i = 0; i < columns; ++i)
        {
            size_t at = strlen(names);
            snprintf(names + at, sizeof(names) - at, "%sCol_%zu", i ? "," : "", i);
            if (i) strcat(values, ",");
            strcat(values, cells[i % 6U]);
        }
        check_fixture(32U + columns, 4096U, "t", names, values, "", 0, 1);
        check_fixture(32U, 4096U, "Schema_1 . Table_2", names, values, ";", 0, 1);
        check_fixture(32U, 4096U, "Catalog_1 . Schema_2 . Table_3", names, values, " ; \n", 0, 1);
    }
    check_fixture(5000U, 0U, "TEST_LIB.TEACHER_STATISTICS",
        "STAT_DATE,TEACHER_ID,TEACHER_NAME_ENCRYPT,PHONE_ENCRYPT,TOTAL_TEACHING,TOTAL_HOURS,CHECK_STATUS,CREATE_TIME,UPDATE_TIME",
        "'202505','T1001','张三李四','13800138000',20,100.50,0,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP", "", 0, 1);
}

static void boundaries(void)
{
    static const size_t sizes[] = {1U, 126U, 127U, 128U, 129U, 1023U, 1024U, 2048U, 8193U};
    stage = "scalar source, row, string and float boundaries";
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        char text[16384];
        size_t n = sizes[i];
        text[0] = '-'; memset(text + 1U, '9', n); text[n + 1U] = '\0'; scalar(text, 1);
        text[0] = '\''; memset(text + 1U, 'a', n); text[n + 1U] = '\''; text[n + 2U] = '\0'; scalar(text, 1);
    }
    for (size_t rows = 31U; rows <= 33U; ++rows)
        check_fixture(rows, 4096U, "s.t", "a,b,c", "1,1.0,'é'", "", 0, rows >= 32U);
    for (size_t n = 4095U; n <= 4097U; ++n)
    {
        char *sql = fixture(32U, 0U, "s.t", "a,b,c", "1,1.0,'é'", "");
        size_t padding = n - strlen(sql);
        free(sql);
        check_fixture(32U, padding, "s.t", "a,b,c", "1,1.0,'é'", "", 0, n >= 4096U);
    }
    {
        char name[4097];
        memset(name, 'Q', sizeof(name) - 1U); name[sizeof(name) - 1U] = '\0';
        check_fixture(32U, 0U, name, "a,b,c", "1,1.0,'é'", "", 0, 1);
        check_fixture(32U, 0U, "s.t", name, "CURRENT_TIMESTAMP(6)", ";", 0, 1);
    }
}

static void exclusions(void)
{
    static const char *scalars[] = {
        "+1", "--1", "- -1", "-+1", "-/*comment*/1", "- --comment\n1",
        "-", "- ", "-1junk", "-2147483648junk", "2147483648junk", "1e",
        "1e+", "1e-", "1e2junk", "1.0junk", "1..2", ".", "-.", "-0x12",
        "0x12", "0o17", "0b11", "1_000", "1.0_0", "1e1_0", "-1_000",
        "NULL", "DEFAULT", "true", "false", "1+2", "1::int", "(1)",
        "CURRENT_TIMESTAMP()", "CURRENT_TIMESTAMP(-1)", "CURRENT_TIMESTAMP(+1)",
        "CURRENT_TIMESTAMP(2147483648)", "CURRENT_TIMESTAMP(1.0)",
        "CURRENT_TIMESTAMP(1e0)", "CURRENT_TIMESTAMP(0x1)", "CURRENT_TIMESTAMP(1_0)",
        "CURRENT_TIMESTAMP(6,3)", "CURRENT_DATE(1)", "CURRENT_USER(1)",
        "CURRENT_TIMESTAMPz", "CURRENT_TIMESTAMP$", "CURRENT_TIMESTAMPé",
        "CURRENT_TIMESTAMP /*comment*/ (6)", "now()", "SYSTEM_USER",
        "E'x'", "N'x'", "U&'x'", "$$x$$", "'a''b'", "'a\\b'", "'a'\n'b'",
        "'a'/*x*/", "'a\nb'", "'a\177b'", "'unterminated", "concat('a','b')",
        "'\x80'", "'\xc0\x80'", "'\xc1\xbf'", "'\xc2'", "'\xe0\x80\x80'",
        "'\xed\xa0\x80'", "'\xf0\x80\x80\x80'", "'\xf4\x90\x80\x80'", "'\xf5\x80\x80\x80'", "'\xff'"
    };
    static const char *relations[] = {
        "a.b.c.d", "a..b", ".a", "a.", "a . *", "a[1]", "\"Mixed\"",
        "`t`", "t$1", "t\xc3\xa9", "t AS alias_name", "t/*comment*/", "ONLY t"
    };
    static const char *columns[] = {"", "a,", "a,,b", "\"A\",b,c", "a[1],b,c", "a/*x*/,b,c", "a$1,b,c", "aé,b,c"};
    static const char *tails[] = {
        ";;", ";SELECT 1", ";SELECT )", ",", ",(1)", ",(1,2,3,4)",
        ",(1,2,'unterminated", " RETURNING *", " ON CONFLICT DO NOTHING",
        " -- comment", " /* comment */", " junk", "\xc3\xa9"
    };
    stage = "general scalar conservative fallbacks and invalid SQL";
    for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); ++i) scalar(scalars[i], 0);
    for (size_t i = 0; i < sizeof(relations) / sizeof(relations[0]); ++i)
        check_fixture(32U, 4096U, relations[i], "a,b,c", "1,1.0,'é'", "", 0, 0);
    for (size_t i = 0; i < sizeof(columns) / sizeof(columns[0]); ++i)
        check_fixture(32U, 4096U, "s.t", columns[i], "1,1.0,'é'", "", 0, 0);
    for (size_t i = 0; i < sizeof(tails) / sizeof(tails[0]); ++i)
        check_fixture(80U, 4096U, "s.t", "a,b,c", "1,1.0,'é'", tails[i], 0, 0);
    for (int option = 1; option <= 256; option <<= 1)
        check_fixture(32U, 4096U, "s.t", "a,b,c", "1,1.0,'é'", "", option, 0);
    /* Raw grammar allows unequal VALUES lengths, but admission declines them.
     * Public validation, if relevant, retains the normal native-tree path. */
    check_fixture(32U, 4096U, "s.t", "a,b", "1,1.0,'é'", "", 0, 0);
    check_fixture(32U, 4096U, "s.t", "a,b,c,d", "1,1.0,'é'", "", 0, 0);
}

static void keywords(void)
{
    char mixed[NAMEDATALEN], names[256];
    stage = "every native keyword in relation and column identifier positions";
    for (int i = 0; i < ScanKeywords.num_keywords; ++i)
    {
        const char *word = GetScanKeyword(i, &ScanKeywords);
        CHECK(strlen(word) < sizeof(mixed)); strcpy(mixed, word);
        for (size_t j = 0; j < strlen(word); j += 2U)
            if (mixed[j] >= 'a' && mixed[j] <= 'z') mixed[j] -= 'a' - 'A';
        for (unsigned position = 0; position < 4U; ++position)
        {
            if (position == 0U) snprintf(names, sizeof(names), "%s.s.t", mixed);
            if (position == 1U) snprintf(names, sizeof(names), "cat.%s.t", mixed);
            if (position == 2U) snprintf(names, sizeof(names), "cat.s.%s", mixed);
            if (position == 3U) snprintf(names, sizeof(names), "a,%s,c", mixed);
            check_fixture(32U, 4096U, position < 3U ? names : "s.t",
                position == 3U ? names : "a,b,c", "1,1.0,'é'", "", 0, 0);
        }
    }
}

static unsigned random_state = 0x198635b7U;
static unsigned next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void randomized(void)
{
    static const char *tokens[] = {"1", "-2147483648", "1.000e-10", "'UTF8 é € 😀'", "CURRENT_TIMESTAMP", "LOCALTIME(6)"};
    stage = "random scalar placements and deterministic source mutations";
    for (unsigned i = 0; i < 400U; ++i)
    {
        char names[256] = "", values[1024] = "";
        size_t n = 1U + next_random() % 13U;
        char *sql;
        for (size_t j = 0; j < n; ++j)
        {
            size_t at = strlen(names);
            snprintf(names + at, sizeof(names) - at, "%sCol_%zu", j ? "," : "", j);
            if (j) strcat(values, ",");
            strcat(values, tokens[next_random() % 6U]);
        }
        sql = fixture(32U + next_random() % 32U, 4096U, "Db.Sch.Tab", names, values, i % 2U ? ";" : "");
        parity(sql, 0, 1);
        for (unsigned j = 0; j < 4U; ++j)
        {
            size_t at = 4096U + next_random() % (strlen(sql) - 4096U);
            char saved = sql[at]; sql[at] = (char)(1U + next_random() % 255U);
            parity(sql, 0, -1); sql[at] = saved;
        }
        free(sql);
    }
}

#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
static void native_oom(void)
{
    char *sql = fixture(128U, 4096U, "Db.Sch.Tab", "a,b,c,d,e,f,g,h,i",
        "'张三李四',1,-2147483648,100.50,CURRENT_TIMESTAMP,LOCALTIME(6),'',2147483647,-1.5e+3", ";");
    size_t boundaries = 0U;
    stage = "general scalar native allocation failures and fresh-call recovery";
    for (size_t at = 0U; at <= boundaries; ++at)
    {
        PgQueryProtobufParseResult parsed;
        size_t count = 0U; int certified = 0;
        pg_query_exit(); pg_query_init();
        native_attempts = injected = grammar_calls = 0U;
        native_depth = 0; fail_at = at; armed = 1;
        parsed = pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(
            sql, 0, NULL, NULL, &count, &certified);
        armed = 0; native_depth = 0;
        CHECK(grammar_calls == 0U);
        if (at == 0U)
        {
            boundaries = native_attempts;
            CHECK(boundaries > 0U && !parsed.error && parsed.parse_tree.data && certified);
        }
        else
        {
            CHECK(injected == 1U);
            if (parsed.error != NULL)
                CHECK(strstr(parsed.error->message, "out of memory") != NULL);
            else
            {
                PgQueryProtobufParseResult reference =
                    pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(sql, 0, NULL, NULL);
                CHECK(reference.error == NULL && parsed.parse_tree.data != NULL && certified);
                wire_equal(parsed.parse_tree, reference.parse_tree);
                pg_query_free_protobuf_parse_result(reference);
            }
        }
        pg_query_free_protobuf_parse_result(parsed);
        CHECK(CurrentMemoryContext == TopMemoryContext);
        parity(sql, 0, 1);
        CHECK(native_live_bytes == 0U && native_live_blocks == 0U);
    }
    printf("scalar native constructor allocation boundaries covered: %zu zero_wrapped_balance=yes\n", boundaries);
    free(sql);
}
#endif

int main(void)
{
    positives(); boundaries(); exclusions(); keywords(); randomized();
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    native_oom();
#endif
    pg_query_exit();
    printf("scalar INSERT native parity passed: %zu full-wire/error/observer cases\n", cases);
    return 0;
}
