#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_ascii_string_internal.h"
#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

static size_t cases;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d case=%zu: %s\n", \
    __FILE__, __LINE__, cases, #x); abort(); } } while (0)

/* Scalar reference recognizer, kept independent of the bounded implementation. */
static int original(const char *sql)
{
    const unsigned char *cursor;
    if (sql == NULL || sql[0] != '\'') return 0;
    for (cursor = (const unsigned char *)sql + 1U; *cursor != 0U; cursor++) {
        if (*cursor == '\'') return cursor[1] == 0U;
        if (*cursor < 0x20U || *cursor > 0x7eU || *cursor == '\\') return 0;
    }
    return 0;
}

static void same(const char *sql)
{
    cases++;
    CHECK(sqlparser_patch_plain_ascii_string_sql(sql) == original(sql));
}

static void byte_position_lengths(void)
{
    size_t length, offset, position;
    unsigned byte;
    same(NULL);
    for (offset = 0U; offset < 8U; offset++) for (length = 0U; length <= 96U; length++) {
        /* Exact right allocation boundary also exercises ASan red zones. */
        char *storage = malloc(offset + length + 1U), *sql;
        CHECK(storage != NULL); sql = storage + offset;
        memset(sql, 'x', length); sql[length] = '\0';
        if (length) sql[0] = sql[length - 1U] = '\'';
        same(sql);
        for (position = 0U; position < length; position++) {
            char previous = sql[position];
            for (byte = 0U; byte < 256U; byte++) {
                sql[position] = (char)byte;
                same(sql);
            }
            sql[position] = previous;
        }
        free(storage);
    }
}

static void byte_pairs(void)
{
    char sql[19] = "'xxxxxxxxxxxxxxxx'";
    size_t position;
    unsigned left, right;
    /* Exhaust each adjacent-byte combination, including carries/borrows both
     * within a loaded word and across the boundary between two words. */
    for (position = 1U; position < 16U; position++) {
        for (left = 0U; left < 256U; left++) for (right = 0U; right < 256U; right++) {
            sql[position] = (char)left; sql[position + 1U] = (char)right;
            same(sql);
        }
        sql[position] = sql[position + 1U] = 'x';
    }
}

static void random_and_long(void)
{
    char sql[8195];
    uint32_t state = 1U;
    size_t round, length, index;
    for (round = 0U; round < 100000U; round++) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        length = (state >> 16U) % 513U;
        sql[0] = '\'';
        for (index = 1U; index <= length; index++) {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            sql[index] = (char)((round & 1U) ? state >> 24U : 0x20U + ((state >> 16U) % 95U));
        }
        sql[length + 1U] = '\''; sql[length + 2U] = '\0';
        same(sql);
    }
    for (length = 0U; length <= 8192U; length++) {
        memset(sql + 1U, 'a', length); sql[0] = '\'';
        sql[length + 1U] = '\''; sql[length + 2U] = '\0';
        same(sql);
    }
}

static void guarded_ends(void)
{
#ifndef _WIN32
    long page = sysconf(_SC_PAGESIZE);
    size_t length;
    char *mapping;
    CHECK(page > 128);
    mapping = mmap(NULL, (size_t)page * 3U, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED);
    CHECK(mprotect(mapping + page, (size_t)page, PROT_READ | PROT_WRITE) == 0);
    for (length = 0U; length <= 96U; length++) {
        char *sql = mapping + page * 2 - length - 1U;
        memset(sql, 'x', length); sql[length] = '\0';
        if (length) sql[0] = sql[length - 1U] = '\'';
        same(sql);
        if (length > 2U) { sql[length / 2U] = '\\'; same(sql); }
    }
    /* These probes preserve the original helper's early decisions even when
     * no terminator follows the rejecting byte. This is deliberately a
     * private-helper robustness check, not a general nonterminated patch-input
     * guarantee. A closing quote still requires its following byte. */
    {
        unsigned byte;
        char *end = mapping + page * 2;
        end[-1] = '\0'; same(end - 1);
        end[-2] = '\''; end[-1] = '\0'; same(end - 2);
        for (byte = 0U; byte < 256U; byte++) {
            if (byte < 0x20U || byte > 0x7eU || byte == '\\') {
                end[-2] = '\''; end[-1] = (char)byte;
                same(end - 2);
            }
            end[-3] = '\''; end[-2] = '\''; end[-1] = (char)byte;
            same(end - 3);
        }
    }
    CHECK(munmap(mapping, (size_t)page * 3U) == 0);
#endif
}

int main(void)
{
    byte_position_lengths(); byte_pairs(); random_and_long(); guarded_ends();
    printf("ASCII recognizer differential: %zu cases, exhaustive bytes/pairs/positions/lengths and guarded ends passed\n", cases);
    return 0;
}
