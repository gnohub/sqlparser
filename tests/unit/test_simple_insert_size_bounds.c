/* Exercise native allocation-size admission using only scalar sizes. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "pg_query.h"
#include "src/pg_query_internal.h"
#include "parser/scansup.h"
#include "port/pg_bitutils.h"
#include "src/pg_query_simple_insert.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)

static void source_bounds(void)
{
    size_t limit = (size_t)MaxAllocSize - 2U;
    if (limit > INT_MAX - 2U) limit = INT_MAX - 2U;
    if (limit > INT32_MAX - 2U) limit = INT32_MAX - 2U;
    CHECK(!pg_query_simple_insert_source_size(0U));
    CHECK(!pg_query_simple_insert_source_size(4095U));
    CHECK(pg_query_simple_insert_source_size(4096U));
    CHECK(pg_query_simple_insert_source_size(limit - 1U));
    CHECK(pg_query_simple_insert_source_size(limit));
    CHECK(!pg_query_simple_insert_source_size(limit + 1U));
    CHECK(!pg_query_simple_insert_source_size(MaxAllocSize));
    CHECK(!pg_query_simple_insert_source_size((size_t)MaxAllocSize + 1U));
    CHECK(!pg_query_simple_insert_source_size(SIZE_MAX));
    CHECK(limit + 2U <= MaxAllocSize);
    printf("source admission max=%zu; native scanbuf request=%zu\n", limit, limit + 2U);
}

static void number_bounds(void)
{
    for (unsigned negative = 0U; negative <= 1U; ++negative) {
        size_t extra = negative ? 2U : 1U;
        size_t limit = MaxAllocSize - extra;
        if (limit > INT_MAX - extra) limit = INT_MAX - extra;
        CHECK(!pg_query_simple_insert_number_size(0U, negative));
        CHECK(pg_query_simple_insert_number_size(1U, negative));
        CHECK(pg_query_simple_insert_number_size(126U, negative));
        CHECK(pg_query_simple_insert_number_size(127U, negative));
        CHECK(pg_query_simple_insert_number_size(128U, negative));
        CHECK(pg_query_simple_insert_number_size(limit - 1U, negative));
        CHECK(pg_query_simple_insert_number_size(limit, negative));
        CHECK(!pg_query_simple_insert_number_size(limit + 1U, negative));
        CHECK(!pg_query_simple_insert_number_size(SIZE_MAX, negative));
        CHECK(limit + 1U <= MaxAllocSize);
        CHECK(limit + extra <= MaxAllocSize);
        CHECK(limit + extra <= INT_MAX);
    }
}

static void string_bounds(void)
{
    size_t largest = 1024U;
    while (largest <= MaxAllocSize / 2U && largest <= INT_MAX / 2U)
        largest *= 2U;
    CHECK(pg_query_simple_insert_string_size(0U));
    CHECK(pg_query_simple_insert_string_size(1023U));
    CHECK(pg_query_simple_insert_string_size(1024U));
    CHECK(pg_query_simple_insert_string_size(largest - 1U));
    CHECK(!pg_query_simple_insert_string_size(largest));
    CHECK(!pg_query_simple_insert_string_size(largest + 1U));
    CHECK(!pg_query_simple_insert_string_size(MaxAllocSize));
    CHECK(!pg_query_simple_insert_string_size(INT_MAX));
    CHECK(!pg_query_simple_insert_string_size(SIZE_MAX));
    /* Use the actual scanner rounding primitive as an independent oracle
     * around every power-of-two boundary within its documented domain. */
    for (size_t power = 1024U; power <= MaxAllocSize + 1U; power *= 2U)
    {
        for (int delta = -2; delta <= 1; ++delta)
        {
            size_t length = power - 2U + (unsigned)(delta + 2);
            uint32 rounded = pg_nextpower2_32((uint32)(length + 1U));
            int expected = rounded <= MaxAllocSize && rounded <= INT_MAX;
            CHECK(pg_query_simple_insert_string_size(length) == expected);
        }
    }
    CHECK(pg_nextpower2_32((uint32)largest) == largest);
    CHECK(pg_nextpower2_32((uint32)largest + 1U) > MaxAllocSize);
    printf("string admission max=%zu; native scratch max=%zu; first rejected scratch=%u\n",
        largest - 1U, largest, pg_nextpower2_32((uint32)largest + 1U));
}

static void native_row_sizes(size_t rows)
{
    size_t header = (offsetof(List, initial_elements) - 1U) / sizeof(ListCell) + 1U;
    uint32 initial_capacity = pg_nextpower2_32((uint32)(rows + header < 8U ? 8U : rows + header));
    uint32 grown_capacity = pg_nextpower2_32((uint32)(rows < 16U ? 16U : rows));
    size_t initial_bytes = offsetof(List, initial_elements) +
        ((size_t)initial_capacity - header) * sizeof(ListCell);
    size_t grown_bytes = (size_t)grown_capacity * sizeof(ListCell);
    size_t debug_bytes = offsetof(List, initial_elements) + rows * sizeof(ListCell);
    CHECK(initial_bytes <= MaxAllocSize);
    CHECK(grown_bytes <= MaxAllocSize);
    CHECK(debug_bytes <= MaxAllocSize);
    CHECK(initial_capacity <= INT_MAX);
    CHECK(grown_capacity <= INT_MAX);
    CHECK(rows + header <= INT_MAX);
    CHECK(rows + 1U <= INT_MAX);
}

static void row_bounds(void)
{
    size_t header = (offsetof(List, initial_elements) - 1U) / sizeof(ListCell) + 1U;
    size_t limit = MaxAllocSize / sizeof(ListCell) / 2U;
    if (limit > INT_MAX / 2U) limit = INT_MAX / 2U;
    CHECK(limit >= header);
    limit -= header;
    CHECK(pg_query_simple_insert_rows_size(32U));
    CHECK(pg_query_simple_insert_rows_size(limit - 1U));
    CHECK(pg_query_simple_insert_rows_size(limit));
    CHECK(!pg_query_simple_insert_rows_size(limit + 1U));
    CHECK(!pg_query_simple_insert_rows_size(INT_MAX));
    CHECK(!pg_query_simple_insert_rows_size(SIZE_MAX));
    native_row_sizes(1U);
    native_row_sizes(2U);
    for (size_t rows = 16U; rows <= limit; rows *= 2U)
    {
        native_row_sizes(rows);
        for (size_t offset = 0; offset <= 2U * header; ++offset)
        {
            size_t near = rows - header + offset;
            if (pg_query_simple_insert_rows_size(near)) native_row_sizes(near);
        }
    }
    {
        uint32 capacity = pg_nextpower2_32((uint32)limit);
        uint32 initial = pg_nextpower2_32((uint32)(limit + header));
        native_row_sizes(limit - 1U);
        native_row_sizes(limit);
        printf("row admission max=%zu; header cells=%zu; initial request=%zu; grown request=%zu; debug request=%zu\n",
            limit, header, offsetof(List, initial_elements) + ((size_t)initial - header) * sizeof(ListCell),
            (size_t)capacity * sizeof(ListCell), offsetof(List, initial_elements) + limit * sizeof(ListCell));
    }
}

int main(void)
{
    source_bounds(); number_bounds(); string_bounds(); row_bounds();
    printf("simple INSERT scalar allocation boundaries passed: %u checks; no large allocations\n", checks);
    return 0;
}
