/* Fail every actual allocation in the production private column constructor.
 * Include the Oracle TU, as the branch-growth test does: it replaces that
 * archive member without adding a production test hook. The same caller can
 * include the frozen source via SQLPARSER_ORACLE_COLUMN_SOURCE; its default
 * sweep must fail the ownership ledger on the original partial-column leak.
 * --record emits no-fault status, errors, allocations and column bytes for
 * byte comparison against that frozen source. It still requires a clean ledger.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL column OOM line=%d case=%zu null=%d fail=%zu calls=%zu live=%zu: %s\n", __LINE__, case_number, null_error, failure, calls, live_count, #x); abort(); } } while (0)
typedef struct { void *pointer; size_t ordinal, size; } allocation;
typedef struct { size_t size; char kind; int old; } allocation_event;
static allocation ledger[2048];
static allocation_event events[2048];
static size_t case_number, failure, calls, live_count, ledger_end;
static int active, null_error;

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static size_t find_slot(void *pointer)
{
    size_t i;
    if (pointer) for (i = 0U; i < ledger_end; ++i)
        if (ledger[i].pointer == pointer) return i;
    return 2048U;
}
static int reject(char kind, size_t size, int old)
{
    if (!active) return 0;
    CHECK(calls < 2048U);
    events[calls] = (allocation_event){size, kind, old};
    return ++calls == failure;
}
static void track(void *pointer, size_t size)
{
    size_t i;
    if (!active || !pointer) return;
    CHECK(find_slot(pointer) == 2048U);
    for (i = 0U; i < ledger_end && ledger[i].pointer; ++i) {}
    CHECK(i < 2048U);
    ledger[i] = (allocation){pointer, calls, size};
    if (i == ledger_end) ++ledger_end;
    ++live_count;
}
void *__wrap_malloc(size_t size)
{
    void *pointer;
    if (reject('m', size, 0)) return NULL;
    pointer = __real_malloc(size); track(pointer, size); return pointer;
}
void *__wrap_calloc(size_t count, size_t size)
{
    void *pointer;
    if (reject('c', count * size, 0)) return NULL;
    pointer = __real_calloc(count, size); track(pointer, count * size); return pointer;
}
void *__wrap_realloc(void *pointer, size_t size)
{
    size_t slot = find_slot(pointer);
    void *next;
    if (reject('r', size, pointer != NULL)) return NULL;
    next = __real_realloc(pointer, size);
    if (next || !size) {
        if (slot < 2048U) { ledger[slot].pointer = NULL; --live_count; }
        track(next, size);
    }
    return next;
}
void __wrap_free(void *pointer)
{
    size_t slot = find_slot(pointer);
    if (slot < 2048U) { ledger[slot].pointer = NULL; --live_count; }
    __real_free(pointer);
}

#ifndef SQLPARSER_ORACLE_COLUMN_SOURCE
#define SQLPARSER_ORACLE_COLUMN_SOURCE "../../src/dialect/sqlparser_dialect_oracle.c"
#define SQLPARSER_ORACLE_COLUMN_HAS_SPAN_PROOF 1
#endif
#include SQLPARSER_ORACLE_COLUMN_SOURCE

typedef struct {
    const char *sql;
    size_t count;
    const char *names[9];
    const char *slices[9];
} fixture;
static size_t exercise(const fixture *f, size_t fail_at, int record)
{
    sqlparser_dialect_multi_insert_column_t *columns = (void *)(uintptr_t)1U;
    size_t count = 999U, i;
    sqlparser_error_t error;
    sqlparser_status_t status;
    CHECK(live_count == 0U);
    memset(&error, 0, sizeof(error));
    error.code = SQLPARSER_STATUS_UNSUPPORTED;
    strcpy(error.message, "unchanged success diagnostic");
    failure = fail_at; calls = 0U; active = 1;
#ifdef SQLPARSER_ORACLE_COLUMN_HAS_SPAN_PROOF
    {
        int span_safe = 1;
        status = sqlparser_oracle_parse_column_list(f->sql, 0U, strlen(f->sql),
            &columns, &count, &span_safe, null_error ? NULL : &error);
    }
#else
    status = sqlparser_oracle_parse_column_list(f->sql, 0U, strlen(f->sql),
        &columns, &count, null_error ? NULL : &error);
#endif
    active = 0;
    if (fail_at) {
        CHECK(calls == fail_at);
        CHECK(status == SQLPARSER_STATUS_NO_MEMORY);
        if (!null_error) CHECK(error.code == status && !strcmp(error.message, "out of memory"));
    } else if (!f->count) {
        CHECK(status == (null_error ? SQLPARSER_STATUS_NO_MEMORY : SQLPARSER_STATUS_PARSE_ERROR));
        if (!null_error) CHECK(error.code == status && !strcmp(error.message, "column list contains an empty item"));
    } else {
        CHECK(status == SQLPARSER_STATUS_OK && columns && count == f->count);
        CHECK(error.code == SQLPARSER_STATUS_UNSUPPORTED && !strcmp(error.message, "unchanged success diagnostic"));
        for (i = 0U; i < count; ++i) {
            CHECK(columns[i].name && columns[i].sql);
            if (f->names[i]) CHECK(!strcmp(columns[i].name, f->names[i]));
            if (f->slices[i]) CHECK(!strcmp(columns[i].sql, f->slices[i]));
        }
    }
    if (status != SQLPARSER_STATUS_OK) CHECK(columns == NULL && count == 0U);
    if (record) {
        printf("case=%zu null=%d status=%d error=%d,%zu,%zu,%zu,%s columns=%zu calls=%zu\n",
            case_number, null_error, status, error.code, (size_t)error.cursor,
            (size_t)error.line, (size_t)error.column, error.message, count, calls);
        for (i = 0U; i < calls; ++i)
            printf("alloc=%c,%zu,%d\n", events[i].kind, events[i].size, events[i].old);
        for (i = 0U; i < count; ++i)
            printf("column=%zu:%s;%zu:%s\n", strlen(columns[i].name), columns[i].name,
                strlen(columns[i].sql), columns[i].sql);
    }
    for (i = 0U; i < count; ++i) { free(columns[i].name); free(columns[i].sql); }
    free(columns);
    if (live_count) for (i = 0U; i < ledger_end; ++i)
        if (ledger[i].pointer) fprintf(stderr, "unreleased allocation ordinal=%zu size=%zu\n", ledger[i].ordinal, ledger[i].size);
    CHECK(live_count == 0U);
    return calls;
}
int main(int argc, char **argv)
{
    fixture cases[] = {
        {"a", 1U, {"a"}, {"a"}},
        {" A , b ", 2U, {"A", "b"}, {"A", "b"}},
        {"\"x\"", 1U, {"x"}, {"\"x\""}},
        {"\"\"", 1U, {""}, {"\"\""}},
        {"\"a\"\"b\",\"a,b\",C", 3U, {"a\"b", "a,b", "C"}, {"\"a\"\"b\"", "\"a,b\"", "C"}},
        {"a,b,c,d,e,f,g,h,i", 9U, {"a", "b", "c", "d", "e", "f", "g", "h", "i"}, {NULL}},
        {"\"雪\",\"Ω\",plain", 3U, {"雪", "Ω", "plain"}, {NULL}},
        {"", 0U, {NULL}, {NULL}},
        {" \t ", 0U, {NULL}, {NULL}},
        {",a", 0U, {NULL}, {NULL}},
        {"a,", 0U, {NULL}, {NULL}},
        {"a,,b", 0U, {NULL}, {NULL}},
        {"a,b,c,d,", 0U, {NULL}, {NULL}},
        {"a,b,c,d,e,f,g,h, ", 0U, {NULL}, {NULL}}
    };
    char long_name[1025], long_quoted[1027], long_list[1080];
    fixture large;
    size_t i, fail_at, count, total = 0U;
    int record = argc == 2 && !strcmp(argv[1], "--record");
    CHECK(argc == 1 || record);
    memset(long_name, 'n', sizeof(long_name) - 1U); long_name[sizeof(long_name) - 1U] = '\0';
    snprintf(long_quoted, sizeof(long_quoted), "\"%s\"", long_name);
    snprintf(long_list, sizeof(long_list), "a,b,c,d,%s,e,f,g,h", long_quoted);
    large = (fixture){long_list, 9U, {"a", "b", "c", "d", long_name, "e", "f", "g", "h"}, {NULL}};
    for (i = 0U; i <= sizeof(cases) / sizeof(cases[0]); ++i) {
        const fixture *f = i == sizeof(cases) / sizeof(cases[0]) ? &large : &cases[i];
        case_number = i;
        for (null_error = 0; null_error < 2; ++null_error) {
            count = exercise(f, 0U, record);
            if (!record) for (fail_at = 1U; fail_at <= count; ++fail_at) {
                (void)exercise(f, fail_at, 0); ++total;
            }
        }
    }
    if (!record) printf("PASS: private Oracle column constructor, %zu fixtures, %zu allocation failures, nullable errors, clean ledger\n", i, total);
    return 0;
}
