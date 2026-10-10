/* The proof-reuse reader accepts nonminimal varints. Its unchanged original
 * wi_certified_u32 is the oracle, NOT the strict/canonical wi_varint reader.
 * This is correctness-only coverage; it makes no performance assertions. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/core/sqlparser_wire_insert.c"

#ifndef SQLPARSER_WIRE_SCALAR_INSERT_DEFENSIVE_ROWS
#include "sqlparser_reference_certified_cell.inc"

static size_t reader_cases, cell_cases;
static uint32_t random_state = UINT32_C(0x7135bc09);
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s (reader=%zu cell=%zu)\n", \
        __FILE__, __LINE__, #condition, reader_cases, cell_cases); \
    abort(); \
} } while (0)

static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void check_reader(const uint8_t *bytes, size_t length)
{
    const uint8_t *actual = bytes, *expected = bytes;
    uint32_t a = UINT32_C(0xa5e71d93), e = a;
    int ar = wsi_certified_location_u32(&actual, bytes + length, &a);
    int er = wi_certified_u32(&expected, bytes + length, &e);
    ++reader_cases;
    CHECK(ar == er && actual == expected && a == e);
    if (!ar) CHECK(actual == bytes && a == UINT32_C(0xa5e71d93));
}

static void check_cell(const uint8_t *bytes, size_t length)
{
    const uint8_t *actual = bytes, *expected = bytes;
    sqlparser_wire_scalar_cell_t a, e;
    int ar, er;
    memset(&a, 0xa5, sizeof(a));
    memset(&e, 0xa5, sizeof(e));
    ar = wsi_certified_cell(&actual, bytes + length, &a);
    er = reference_certified_cell(&expected, bytes + length, &e);
    ++cell_cases;
    CHECK(ar == er && actual == expected && memcmp(&a, &e, sizeof(a)) == 0);
    if (!ar) CHECK(actual == bytes);
}

static size_t encode(uint8_t *bytes, uint32_t value)
{
    size_t used = 0U;
    do {
        bytes[used++] = (uint8_t)((value & 0x7fU) | (value >= 0x80U ? 0x80U : 0U));
        value >>= 7;
    } while (value != 0U);
    return used;
}

static void exhaustive_fast_pattern(void)
{
    uint8_t bytes[4];
    uint32_t payload;
    size_t length;
    /* All 128^3 continuation patterns 1,1,0, including every nonminimal
     * ending. Each has all four logical bounds, for 8,388,608 comparisons. */
    for (payload = 0U; payload < (UINT32_C(1) << 21); payload++) {
        bytes[0] = (uint8_t)(0x80U | (payload & 0x7fU));
        bytes[1] = (uint8_t)(0x80U | ((payload >> 7) & 0x7fU));
        bytes[2] = (uint8_t)(payload >> 14);
        for (length = 0U; length <= 3U; length++) check_reader(bytes, length);
        /* The corresponding 1,1,1 pattern is truncated, not a fast match. */
        bytes[2] |= 0x80U;
        check_reader(bytes, 3U);
    }
    /* Exhaust every two-byte prefix with a trailing byte. Early termination
     * at byte one/two must ignore that suffix and retain exact consumption. */
    for (payload = 0U; payload < UINT32_C(0x10000); payload++) {
        bytes[0] = (uint8_t)payload;
        bytes[1] = (uint8_t)(payload >> 8);
        bytes[2] = 0x00U;
        check_reader(bytes, 2U);
        check_reader(bytes, 3U);
        bytes[2] = 0xffU;
        check_reader(bytes, 3U);
        if (payload < 256U) check_reader(bytes, 1U);
    }
}

static void boundary_and_malformed(void)
{
    static const uint32_t values[] = {0U, 1U, 127U, 128U, 16383U, 16384U,
        2097151U, 2097152U, 268435455U, 268435456U,
        INT32_MAX, (uint32_t)INT32_MAX + 1U, UINT32_MAX};
    uint8_t bytes[16];
    size_t value_index, length, used, expanded, pos;
    unsigned pattern, fifth;
    for (value_index = 0U; value_index < sizeof(values) / sizeof(values[0]); value_index++) {
        used = encode(bytes, values[value_index]);
        bytes[used] = 0xffU; bytes[used + 1U] = 0U;
        for (length = 0U; length <= used + 2U; length++) check_reader(bytes, length);
        /* Keep extending the last group with zero, through ten bytes. Some
         * nonminimal <=5-byte values are accepted; >5 still use old failure. */
        for (expanded = used + 1U; expanded <= 10U; expanded++) {
            bytes[expanded - 2U] |= 0x80U;
            bytes[expanded - 1U] = 0U;
            for (length = 0U; length <= expanded; length++) check_reader(bytes, length);
        }
    }
    /* Every fifth-byte value, with sixteen low/high payload combinations in
     * the preceding groups, every truncation, and trailing bytes to ten. */
    for (pattern = 0U; pattern < 16U; pattern++) for (fifth = 0U; fifth < 256U; fifth++) {
        for (pos = 0U; pos < 4U; pos++) bytes[pos] = (pattern & (1U << pos)) ? 0xffU : 0x80U;
        bytes[4] = (uint8_t)fifth;
        for (pos = 5U; pos < 10U; pos++) bytes[pos] = pos == 9U ? 0U : 0x80U;
        for (length = 0U; length <= 10U; length++) check_reader(bytes, length);
    }
    for (pattern = 0U; pattern < 100000U; pattern++) {
        for (pos = 0U; pos < 10U; pos++) bytes[pos] = (uint8_t)random_u32();
        for (length = 0U; length <= 10U; length++) check_reader(bytes, length);
    }
}

static size_t cell_prefix(uint8_t *bytes, unsigned shape)
{
    sqlparser_wire_scalar_cell_t cell = {0};
    wi_cell_sizes sizes;
    wi_writer writer = {bytes, bytes + 128U, 0};
    size_t used;
    cell.location = 64;
    cell.kind = shape < 2U ? SQLPARSER_WIRE_SCALAR_INTEGER :
        shape < 4U ? SQLPARSER_WIRE_SCALAR_STRING :
        shape == 4U ? SQLPARSER_WIRE_SCALAR_FLOAT : SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION;
    cell.integer = shape == 0U ? 0 : shape == 1U ? 23 :
        shape == 5U ? PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIMESTAMP : 0;
    cell.text = shape == 4U ? "12.50" : "abc";
    cell.length = shape == 2U ? 0U : shape == 4U ? 5U : 3U;
    CHECK(wsi_measure_cell(&cell, &sizes));
    wsi_write_cell(&writer, &cell, &sizes);
    used = (size_t)(writer.next - bytes);
    CHECK(!writer.failed && used > 1U && bytes[used - 1U] == 64U);
    return used - 1U;
}

static void cell_state_differential(void)
{
    static const uint32_t locations[] = {0U, 1U, 127U, 128U, 16383U, 16384U,
        2097151U, 2097152U, 268435455U, 268435456U,
        INT32_MAX, (uint32_t)INT32_MAX + 1U, UINT32_MAX};
    uint8_t bytes[128], saved[128];
    unsigned shape, byte;
    size_t index, prefix, encoded, length, expanded, pos;
    for (shape = 0U; shape < 6U; shape++) {
        prefix = cell_prefix(saved, shape);
        for (index = 0U; index < sizeof(locations) / sizeof(locations[0]); index++) {
            memcpy(bytes, saved, prefix);
            encoded = encode(bytes + prefix, locations[index]);
            bytes[prefix + encoded] = 0xffU; bytes[prefix + encoded + 1U] = 0U;
            for (length = 0U; length <= prefix + encoded + 2U; length++) check_cell(bytes, length);
            for (expanded = encoded + 1U; expanded <= 10U; expanded++) {
                bytes[prefix + expanded - 2U] |= 0x80U;
                bytes[prefix + expanded - 1U] = 0U;
                for (length = 0U; length <= prefix + expanded; length++) check_cell(bytes, length);
            }
        }
        /* Inject each possible byte at every prefix/location position and
         * compare the complete partially written cell on every short bound.
         * Mutated inner lengths deliberately need not form a public proof. */
        memcpy(bytes, saved, prefix);
        encoded = encode(bytes + prefix, 16384U);
        for (pos = 0U; pos < prefix + encoded; pos++) {
            uint8_t original = bytes[pos];
            for (byte = 0U; byte < 256U; byte++) {
                bytes[pos] = (uint8_t)byte;
                for (length = 0U; length <= prefix + encoded; length++) check_cell(bytes, length);
            }
            bytes[pos] = original;
        }
    }
}

static void exact_allocation_ends(void)
{
    size_t alignment, length, pos;
    uint8_t bytes[128];
    unsigned shape;
    for (alignment = 0U; alignment < 8U; alignment++) for (length = 0U; length <= 10U; length++) {
        size_t prefix = alignment + 1U;
        uint8_t *allocation = malloc(prefix + length), *start;
        CHECK(allocation != NULL);
        start = allocation + prefix;
        for (pos = 0U; pos < length; pos++) start[pos] = 0x80U;
        if (length) start[length - 1U] = 0U;
        check_reader(start, length);
        for (pos = 0U; pos < length; pos++) start[pos] = (uint8_t)random_u32();
        check_reader(start, length);
        free(allocation);
    }
    for (shape = 0U; shape < 6U; shape++) {
        size_t prefix = cell_prefix(bytes, shape), used = prefix + encode(bytes + prefix, 16384U);
        for (alignment = 0U; alignment < 8U; alignment++) for (length = 0U; length <= used; length++) {
            size_t offset = alignment + 1U;
            uint8_t *allocation = malloc(offset + length), *start;
            CHECK(allocation != NULL);
            start = allocation + offset;
            memcpy(start, bytes, length);
            check_cell(start, length);
            free(allocation);
        }
    }
}

int main(void)
{
    exhaustive_fast_pattern();
    boundary_and_malformed();
    cell_state_differential();
    exact_allocation_ends();
    printf("certified location u32 differential: %zu reader cases, %zu cell-state cases passed; nonminimal, cursor/output, overflow, truncation and exact-end coverage\n",
        reader_cases, cell_cases);
    return 0;
}
#else
int main(void)
{
    puts("certified location u32 differential skipped: defensive rows build has no certified decoder");
    return 0;
}
#endif
