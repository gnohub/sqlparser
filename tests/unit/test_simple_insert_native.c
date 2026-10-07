/* Complete native-wire/error parity against the ordinary observed entry point,
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

#ifdef SQLPARSER_SIMPLE_INSERT_RECORD
/* The same test executable linked against a reference build emits an independent
 * complete-byte oracle. No source recognizer exists in that reference library.
 * These native-host binary records are compared with cmp, not hash summaries. */
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
#ifdef SQLPARSER_SIMPLE_INSERT_RECORD
    record_result(actual, actual_observer, count, certified);
#endif
    pg_query_free_protobuf_parse_result(actual);
    pg_query_free_protobuf_parse_result(reference);
    free(actual_observer.wire.data);
    free(reference_observer.wire.data);
    free(unchanged);
    cases++;
}

static char *fixture(size_t rows, size_t padding, const char *table,
                     const char *first, const char *second, const char *integer,
                     const char *string, const char *gap, const char *tail)
{
    size_t cap = padding + strlen(table) + strlen(first) + strlen(second) + strlen(tail) +
        256U + rows * (strlen(integer) + strlen(string) + 8U * strlen(gap) + 32U);
    char *sql = calloc(cap, 1U);
    size_t used = padding;
    CHECK(sql != NULL);
    memset(sql, ' ', padding);
    used += (size_t)snprintf(sql + used, cap - used,
        "InSeRt%sINTO%s%s%s(%s%s,%s%s%s)%sVaLuEs%s", gap, gap, table, gap,
        gap, first, gap, second, gap, gap, gap);
    for (size_t i = 0U; i < rows; i++)
        used += (size_t)snprintf(sql + used, cap - used, "%s(%s%s%s,%s%s%s)%s",
            i ? "," : "", gap, integer, gap, gap, string, gap, gap);
    CHECK(used + strlen(tail) < cap);
    strcpy(sql + used, tail);
    return sql;
}

static void check_fixture(size_t rows, size_t padding, const char *table,
                          const char *first, const char *second, const char *integer,
                          const char *string, const char *gap, const char *tail,
                          int options, int fast)
{
    char *sql = fixture(rows, padding, table, first, second, integer, string, gap, tail);
    parity(sql, options, fast);
    free(sql);
}

static void positives(void)
{
    static const char *numbers[] = {"0", "00", "00000000000000000000000000000000000000001", "127", "128", "16383", "16384", "2147483647", "2147483648", "2147483649", "4294967296", "9223372036854775808", "0002147483648", "-0", "-0000", "-7", "-0007", "-2147483647", "-2147483648", "-2147483649", "-0002147483648", "- 7", "-\t\r\n\v\f0002147483648"};
    static const char *gaps[] = {" ", "\t", "\r\n", "\v", "\f", " \t\r\n\v\f"};
    static const char *strings[] = {"''", "'x'", "'SELECT,; -- /* $$$$ 0123 xyz'", "'abcdefghijklmnopqrstuvwxyz'"};
    static const char *tails[] = {"", ";", " ; \t\r\n"};
    char long_name[513], long_string[2051];
    stage = "native defaults, locations, identifiers and scalar bounds";
    for (size_t i = 0; i < sizeof(numbers)/sizeof(numbers[0]); ++i)
        for (size_t j = 0; j < sizeof(gaps)/sizeof(gaps[0]); ++j)
            for (size_t k = 0; k < sizeof(strings)/sizeof(strings[0]); ++k)
                check_fixture(32U + i, 4096U, "_Target_17", "FirstColumn", "_second2",
                    numbers[i], strings[k], gaps[j], tails[(i+j+k)%3U], 0, 1);
    memset(long_name, 'Q', sizeof(long_name)-1U); long_name[sizeof(long_name)-1U] = '\0';
    memset(long_string, 'z', sizeof(long_string)-1U); long_string[0] = '\'';
    long_string[sizeof(long_string)-2U] = '\''; long_string[sizeof(long_string)-1U] = '\0';
    check_fixture(32U, 0U, long_name, long_name, long_name, "1", long_string, " ", ";", 0, 1);
    for (size_t rows = 31U; rows <= 33U; ++rows)
        check_fixture(rows, 4096U, "t", "a", "b", "1", "'x'", " ", "", 0, rows >= 32U);
    for (size_t length = 4095U; length <= 4097U; ++length)
    {
        char *short_sql = fixture(32U, 0U, "t", "a", "b", "1", "'x'", " ", "");
        size_t padding = length - strlen(short_sql);
        free(short_sql);
        check_fixture(32U, padding, "t", "a", "b", "1", "'x'", " ", "", 0, length >= 4096U);
    }
}

static void numeric_lengths(void)
{
    static const size_t lengths[] = {1U, 10U, 126U, 127U, 128U, 129U, 1023U, 1024U, 8193U};
    stage = "numeric spelling and native psprintf allocation boundaries";
    for (size_t i = 0U; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
        size_t length = lengths[i];
        char *number = malloc(length + 3U);
        CHECK(number != NULL);
        number[0] = '-'; number[1] = ' ';
        memset(number + 2U, '9', length); number[length + 2U] = '\0';
        check_fixture(32U,4096U,"t","a","b",number,"'x'"," ",";",0,1);
        check_fixture(32U,4096U,"t","a","b",number + 2U,"'x'"," ",";",0,1);
        memset(number + 2U, '0', length); number[length + 1U] = '7';
        check_fixture(32U,4096U,"t","a","b",number,"'x'"," ",";",0,1);
        free(number);
    }
}

static void keywords(void)
{
    char mixed[NAMEDATALEN];
    stage = "all native keywords in all three identifier positions";
    for (int i = 0; i < ScanKeywords.num_keywords; ++i)
    {
        const char *word = GetScanKeyword(i, &ScanKeywords);
        CHECK(strlen(word) < sizeof(mixed));
        strcpy(mixed, word);
        for (size_t j = 0; j < strlen(word); j += 2U)
            if (mixed[j] >= 'a' && mixed[j] <= 'z') mixed[j] -= 'a' - 'A';
        for (unsigned position = 0; position < 3U; ++position)
            check_fixture(32U, 4096U, position == 0U ? mixed : "t",
                position == 1U ? mixed : "a", position == 2U ? mixed : "b",
                "1", "'x'", " ", "", 0, 0);
    }
}

static void exclusions(void)
{
    static const char *names[] = {"s.t", "\"Mixed\"", "`t`", "t$1", "t\xc3\xa9", "t AS alias_name", "t/*comment*/"};
    static const char *numbers[] = {"+1", "--1", "- -1", "-+1", "-/*comment*/1", "- --comment\n1", "-", "- ", "-1junk", "-2147483648junk", "2147483648junk", "-1.0", "-1e2", "-0x12", "-1_000", "-(1)", "1.0", "1e2", "0x12", "0o17", "0b11", "1_000", "NULL", "DEFAULT", "1+2", "1::int", "1junk"};
    static const char *strings[] = {"E'x'", "N'x'", "U&'x'", "$$x$$", "'a''b'", "'a\\b'", "'a'\n'b'", "'a'/*x*/", "'a\nb'", "'a\177b'", "'\xc3\xa9'", "NULL", "DEFAULT", "concat('a','b')"};
    static const char *tails[] = {";;", ";SELECT 1", ";SELECT )", ",", ",(1)", ",(1,'x',2)", ",(1,'unterminated", " RETURNING *", " ON CONFLICT DO NOTHING", " -- comment", " /* comment */", " junk"};
    stage = "excluded lexical syntax and late failures";
    for (size_t i=0; i<sizeof(names)/sizeof(names[0]); ++i)
        check_fixture(32U,4096U,names[i],"a","b","1","'x'"," ","",0,
            strcmp(names[i], "s.t") == 0);
    for (size_t i=0; i<sizeof(numbers)/sizeof(numbers[0]); ++i)
        check_fixture(32U,4096U,"t","a","b",numbers[i],"'x'"," ","",0,
            strcmp(numbers[i], "-1.0") == 0 || strcmp(numbers[i], "-1e2") == 0 ||
            strcmp(numbers[i], "1.0") == 0 || strcmp(numbers[i], "1e2") == 0);
    for (size_t i=0; i<sizeof(strings)/sizeof(strings[0]); ++i)
        check_fixture(32U,4096U,"t","a","b","1",strings[i]," ","",0,
            strcmp(strings[i], "'\xc3\xa9'") == 0);
    for (size_t i=0; i<sizeof(tails)/sizeof(tails[0]); ++i)
        check_fixture(5000U,0U,"t","a","b","1","'x'"," ",tails[i],0,0);
    for (int option=1; option<=256; option<<=1)
        check_fixture(32U,4096U,"t","a","b","1","'x'"," ","",option,0);
}

static unsigned random_state = 0x76acf213U;
static unsigned next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void randomized(void)
{
    char name[80], string[132], number[32];
    stage = "deterministic varied names, payloads and source mutations";
    for (unsigned i=0; i<600U; ++i)
    {
        size_t n = next_random()%100U;
        char *sql;
        snprintf(name,sizeof(name),"_Random_%x_%u",next_random(),i);
        snprintf(number,sizeof(number),"%u",next_random() & INT32_MAX);
        string[0]='\'';
        for (size_t j=0;j<n;++j)
        {
            unsigned c = 0x20U + next_random()%95U;
            if (c=='\'' || c=='\\') c='x';
            string[j+1U]=(char)c;
        }
        string[n+1U]='\''; string[n+2U]='\0';
        sql=fixture(32U+next_random()%64U,4096U,name,"a","B",number,string," \n",i%2U?";":"");
        parity(sql,0,1);
        for (unsigned j=0;j<3U;++j)
        {
            size_t at=4096U+next_random()%(strlen(sql)-4096U);
            char saved=sql[at];
            sql[at]=(char)(1U+next_random()%255U);
            parity(sql,0,-1);
            sql[at]=saved;
        }
        free(sql);
    }
}

#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
static void native_oom_number(const char *number)
{
    char *sql=fixture(5000U,0U,"t","a","b",number,"'x'"," ",";");
    size_t boundaries=0U;
    stage="guarded native allocator failures and fresh-call recovery";
    for (size_t at=0U;at<=boundaries;++at)
    {
        PgQueryProtobufParseResult parsed;
        size_t count=0U; int certified=0;
        pg_query_exit(); pg_query_init();
        native_attempts=injected=grammar_calls=0U; native_depth=0; fail_at=at; armed=1;
        parsed=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(sql,0,NULL,NULL,&count,&certified);
        armed=0; native_depth=0;
        CHECK(grammar_calls==0U);
        if (at==0U)
        {
            boundaries=native_attempts;
            CHECK(boundaries>0U && !parsed.error && parsed.parse_tree.data && certified);
        }
        else
        {
            CHECK(injected==1U);
            if (parsed.error != NULL)
                CHECK(strstr(parsed.error->message,"out of memory")!=NULL);
            else
            {
                /* AllocSet may retry a failed large block with a smaller
                 * request. A recovered allocation must still publish the
                 * complete native-equivalent result, never a partial tree. */
                PgQueryProtobufParseResult reference =
                    pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
                        sql, 0, NULL, NULL);
                CHECK(reference.error == NULL && parsed.parse_tree.data != NULL && certified);
                wire_equal(parsed.parse_tree, reference.parse_tree);
                pg_query_free_protobuf_parse_result(reference);
                printf("native allocator recovered injected request=%zu attempts=%zu\n", at, native_attempts);
            }
        }
        pg_query_free_protobuf_parse_result(parsed);
        CHECK(CurrentMemoryContext==TopMemoryContext);
        parity(sql,0,1);
        CHECK(native_live_bytes == 0U && native_live_blocks == 0U);
    }
    printf("native constructor allocation boundaries covered: %zu zero_wrapped_balance=yes number=%s\n", boundaries, number);
    free(sql);
}
#endif

int main(void)
{
    positives(); numeric_lengths(); keywords(); exclusions(); randomized();
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    native_oom_number("1");
    native_oom_number("-7");
    native_oom_number("2147483648");
    native_oom_number("-2147483648");
    {
        char number[258];
        memset(number, '9', sizeof(number)-1U);
        number[0] = '-'; number[sizeof(number)-1U] = '\0';
        native_oom_number(number);
    }
#endif
    pg_query_exit();
    printf("simple INSERT native parity passed: %zu full-wire/error/observer cases\n",cases);
    return 0;
}
