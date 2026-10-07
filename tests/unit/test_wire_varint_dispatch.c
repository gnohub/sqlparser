/* The private reader is included so no test hook or library ABI is needed.
 * Differential references retain the generic loop and reader side effects. */
#include <stdio.h>
#include "../../src/core/sqlparser_wire_insert.c"

static size_t cases;
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s (case %zu)\n", __FILE__, __LINE__, #x, cases); \
    abort(); \
} } while (0)

static int reference_varint(wi_reader *r, uint32_t *out)
{
    uint32_t value = 0U;
    unsigned shift;
    for (shift = 0U; shift <= 28U; shift += 7U) {
        uint8_t b;
        if (r->next == r->end) return 0;
        b = *r->next++;
        if (shift == 28U && (b & 0xf0U) != 0U) return 0;
        value |= (uint32_t)(b & 0x7fU) << shift;
        if ((b & 0x80U) == 0U) {
            if (shift != 0U && b == 0U) return 0;
            *out = value;
            return 1;
        }
    }
    return 0;
}

static int reference_key_is(const wi_reader *r, unsigned field, unsigned type)
{
    wi_reader copy = *r;
    uint32_t key;
    return reference_varint(&copy, &key) && key == ((field << 3) | type);
}

static int reference_message(wi_reader *r, unsigned field, wi_reader *child)
{
    uint32_t key, length;
    if (!reference_varint(r, &key) || key != ((field << 3) | 2U) ||
        !reference_varint(r, &length) || (size_t)length > (size_t)(r->end - r->next)) return 0;
    child->next = r->next;
    child->end = child->next + length;
    r->next = child->end;
    return 1;
}

static int reference_scalar(wi_reader *r, unsigned field, uint32_t *out)
{
    uint32_t key;
    return reference_varint(r, &key) && key == (field << 3) &&
        reference_varint(r, out) && *out != 0U && *out <= INT32_MAX;
}

static int reference_optional_scalar(wi_reader *r, unsigned field, uint32_t *out)
{
    *out = 0U;
    return !reference_key_is(r, field, 0U) || reference_scalar(r, field, out);
}

static void check_varint(const uint8_t *bytes, size_t n)
{
    wi_reader actual = {bytes, bytes + n}, expected = actual;
    uint32_t a = UINT32_C(0xa5e71d93), e = a;
    int ar = wi_varint(&actual, &a), er = reference_varint(&expected, &e);
    ++cases;
    CHECK(ar == er && a == e && actual.next == expected.next && actual.end == expected.end);
}

static void check_readers(const uint8_t *bytes, size_t n, unsigned field)
{
    wi_reader actual = {bytes, bytes + n}, expected = actual;
    wi_reader ac = {bytes + n, bytes}, ec = ac;
    uint32_t a = UINT32_C(0xa5e71d93), e = a;
    int ar, er;
    ++cases;
    ar = wi_message(&actual, field, &ac); er = reference_message(&expected, field, &ec);
    CHECK(ar == er && actual.next == expected.next && actual.end == expected.end &&
        ac.next == ec.next && ac.end == ec.end);
    actual.next = expected.next = bytes;
    ar = wi_scalar(&actual, field, &a); er = reference_scalar(&expected, field, &e);
    CHECK(ar == er && a == e && actual.next == expected.next && actual.end == expected.end);
    actual.next = expected.next = bytes;
    ar = wi_optional_scalar(&actual, field, &a); er = reference_optional_scalar(&expected, field, &e);
    CHECK(ar == er && a == e && actual.next == expected.next && actual.end == expected.end);
    actual.next = expected.next = bytes;
    CHECK(wi_key_is(&actual, field, 0U) == reference_key_is(&expected, field, 0U));
    CHECK(wi_key_is(&actual, field, 2U) == reference_key_is(&expected, field, 2U));
    CHECK(actual.next == bytes && actual.end == bytes + n);
}

static size_t encode(uint8_t *bytes, uint32_t value)
{
    size_t n = 0U;
    do {
        bytes[n++] = (uint8_t)((value & 0x7fU) | (value >= 128U ? 0x80U : 0U));
        value >>= 7;
    } while (value != 0U);
    return n;
}

static void explicit_cases(void)
{
    static const struct {
        uint8_t bytes[6]; size_t length, consumed; int success; uint32_t value;
    } tests[] = {
        {{0}, 0, 0, 0, 0}, {{0}, 1, 1, 1, 0}, {{0x7f}, 1, 1, 1, 127},
        {{0x80}, 1, 1, 0, 0}, {{0x80, 0}, 2, 2, 0, 0},
        {{0x80, 1}, 2, 2, 1, 128}, {{0xff, 0x7f}, 2, 2, 1, 16383},
        {{0x80, 0x80, 1}, 3, 3, 1, 16384},
        {{0x80, 0x80, 0x80, 1}, 4, 4, 1, UINT32_C(2097152)},
        {{0x80, 0x80, 0x80, 0x80, 1}, 5, 5, 1, UINT32_C(268435456)},
        {{0xff, 0xff, 0xff, 0xff, 0x0f}, 5, 5, 1, UINT32_MAX},
        {{0xff, 0xff, 0xff, 0xff, 0x10}, 5, 5, 0, 0},
        {{0x80, 0x80, 0x80, 0x80, 0}, 5, 5, 0, 0},
        {{0x80, 0x80, 0x80, 0x80, 0x80, 1}, 6, 5, 0, 0},
        {{1, 0xff, 0xff, 0xff, 0xff, 0xff}, 6, 1, 1, 1}
    };
    size_t i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        wi_reader r = {tests[i].bytes, tests[i].bytes + tests[i].length};
        uint32_t value = UINT32_C(0xa5e71d93);
        ++cases;
        CHECK(wi_varint(&r, &value) == tests[i].success);
        CHECK(r.next == tests[i].bytes + tests[i].consumed);
        CHECK(value == (tests[i].success ? tests[i].value : UINT32_C(0xa5e71d93)));
    }
}

static uint32_t random_state = UINT32_C(0xb38a16d5);
static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

int main(void)
{
    static const unsigned fields[] = {0U, 1U, 10U, 11U, 16U, 127U, 128U, 268U, 2047U, UINT32_C(0x1fffffff)};
    static const uint8_t payloads[] = {0U, 1U, 0x0fU, 0x10U, 0x3fU, 0x40U, 0x7eU, 0x7fU};
    uint8_t bytes[20] = {0};
    uint32_t i;
    size_t n, j;
    explicit_cases();
    /* Exhaust every byte sequence up to length three, including truncation,
     * nonminimal endings, termination with trailing bytes, and all payloads. */
    check_varint(bytes, 0U);
    for (i = 0U; i < UINT32_C(0x1000000); i++) {
        bytes[0] = (uint8_t)i; bytes[1] = (uint8_t)(i >> 8); bytes[2] = (uint8_t)(i >> 16);
        check_varint(bytes, 3U);
        if (i < UINT32_C(0x10000)) {
            check_varint(bytes, 2U);
            for (j = 0U; j < sizeof(fields) / sizeof(fields[0]); j++) check_readers(bytes, 2U, fields[j]);
        }
        if (i < 256U) check_varint(bytes, 1U);
    }
    /* Every possible fifth byte, across combinations of low/high payload
     * bits in the preceding four continuation bytes, with every truncation. */
    for (i = 0U; i < UINT32_C(0x100000); i++) {
        bytes[0] = (uint8_t)(0x80U | payloads[(i >> 8) & 7U]);
        bytes[1] = (uint8_t)(0x80U | payloads[(i >> 11) & 7U]);
        bytes[2] = (uint8_t)(0x80U | payloads[(i >> 14) & 7U]);
        bytes[3] = (uint8_t)(0x80U | payloads[(i >> 17) & 7U]);
        bytes[4] = (uint8_t)i;
        for (n = 0U; n <= 5U; n++) check_varint(bytes, n);
    }
    /* Full-width canonical values, extra zero groups, random malformed
     * streams and message/scalar cursor/output behavior at every length. */
    for (i = 0U; i < 100000U; i++) {
        size_t used = encode(bytes, random_u32());
        for (n = 0U; n <= used; n++) check_varint(bytes, n);
        bytes[used - 1U] |= 0x80U; bytes[used] = 0U;
        check_varint(bytes, used + 1U);
        for (j = 0U; j < sizeof(bytes); j++) bytes[j] = (uint8_t)random_u32();
        for (n = 0U; n <= sizeof(bytes); n++) {
            check_varint(bytes, n);
            check_readers(bytes, n, fields[i % (sizeof(fields) / sizeof(fields[0]))]);
        }
    }
    /* Exact allocation ends let ASan detect an otherwise invisible overread
     * beyond a logical reader bound. Include misalignment and empty ranges. */
    for (i = 0U; i < 4096U; i++) {
        size_t prefix = i % 8U, length = i % 13U;
        uint8_t *allocation = (uint8_t *)malloc(prefix + length + 1U);
        uint8_t *start;
        CHECK(allocation != NULL);
        start = allocation + prefix + 1U;
        for (j = 0U; j < length; j++) start[j] = (uint8_t)random_u32();
        check_varint(start, length);
        check_readers(start, length, fields[i % (sizeof(fields) / sizeof(fields[0]))]);
        free(allocation);
    }
    printf("wire varint dispatch differential: %zu cases passed\n", cases);
    return 0;
}
