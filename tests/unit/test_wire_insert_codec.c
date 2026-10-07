/* Private codec-only contract tests. Optional malloc fault injection:
 * -DSQLPARSER_WIRE_CODEC_WRAPPERS -Wl,--wrap=malloc */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_wire_insert_internal.h"

static sqlparser_error_t error;
static size_t checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_WIRE_CODEC_WRAPPERS
static size_t fail_at, allocation_count, allocation_failures;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n)
{
    if (fail_at != 0U && ++allocation_count == fail_at) { allocation_failures++; return NULL; }
    return __real_malloc(n);
}
static void arm(size_t n) { fail_at = n; allocation_count = allocation_failures = 0U; }
static void disarm(void) { fail_at = 0U; CHECK(allocation_failures == 1U); }
#endif

static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_handle_t *handle = NULL;
    sqlparser_parse_options_t options;
    sqlparser_parse_options_default(&options);
    options.dialect = SQLPARSER_DIALECT_MYSQL;
    CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(handle != NULL && handle->ast == NULL);
    return handle;
}

static char *source(size_t padding, int semicolon)
{
    static const unsigned int integers[] = {1U, 0U, 127U, 128U, 16383U, 16384U, INT32_MAX};
    size_t row, used = padding;
    char *sql = (char *)malloc(padding + 64U * 64U + 128U);
    CHECK(sql != NULL);
    memset(sql, ' ', padding);
    used += (size_t)sprintf(sql + used, "InSeRt INTO t(id,text_col) VALUES ");
    for (row = 0U; row < 64U; row++) {
        used += (size_t)sprintf(sql + used, "%s(%u,'%s')", row == 0U ? "" : ",",
            integers[row % 7U], row == 1U ? "" : "small-secret-00001");
    }
    if (semicolon) strcpy(sql + used, " \t; \r\n");
    return sql;
}

static uint32_t getvar(const unsigned char *bytes, size_t length, size_t *pos)
{
    uint32_t value = 0U;
    unsigned shift;
    for (shift = 0U; shift < 35U; shift += 7U) {
        unsigned byte;
        CHECK(*pos < length);
        byte = bytes[(*pos)++];
        value |= (uint32_t)(byte & 0x7fU) << shift;
        if (!(byte & 0x80U)) return value;
    }
    CHECK(0); return 0U;
}
static size_t putvar(unsigned char *bytes, uint32_t value)
{
    size_t n = 0U;
    do {
        bytes[n++] = (unsigned char)((value & 0x7fU) | (value >= 128U ? 128U : 0U));
        value >>= 7;
    } while (value != 0U);
    return n;
}
static size_t field_end(const unsigned char *bytes, size_t length, size_t start)
{
    size_t pos = start;
    uint32_t key = getvar(bytes, length, &pos);
    if ((key & 7U) == 0U) (void)getvar(bytes, length, &pos);
    else { uint32_t n; CHECK((key & 7U) == 2U); n = getvar(bytes, length, &pos); CHECK(n <= length - pos); pos += n; }
    return pos;
}

enum mutation { UNKNOWN, DUPLICATE, NONMINIMAL_KEY, REORDER, EXPLICIT_ZERO, EXPLICIT_EMPTY, NONMINIMAL_LENGTH, NONMINIMAL_SCALAR, OVERFLOW_SCALAR, WRONG_SCALAR, STRING_LOCATION, STRING_PAYLOAD, OVERFLOW_LENGTH };
/* Re-encode each ancestor length so a nested mutation tests that message,
 * rather than being rejected only because an outer envelope was truncated. */
static unsigned char *mutate(const unsigned char *bytes, size_t length,
    const unsigned *path, size_t depth, enum mutation kind, size_t *out_length)
{
    unsigned char *out = (unsigned char *)malloc(length * 2U + 64U);
    CHECK(out != NULL);
    if (depth == 0U) {
        size_t first = length == 0U ? 0U : field_end(bytes, length, 0U);
        if (kind == UNKNOWN) {
            memcpy(out, bytes, length); out[length] = 0xf8U; out[length + 1U] = 7U; out[length + 2U] = 1U;
            *out_length = length + 3U;
        } else if (kind == DUPLICATE) {
            CHECK(first != 0U); memcpy(out, bytes, first); memcpy(out + first, bytes, length); *out_length = length + first;
        } else if (kind == NONMINIMAL_KEY) {
            size_t key_end = 0U;
            (void)getvar(bytes, length, &key_end);
            memcpy(out, bytes, key_end); out[key_end - 1U] |= 0x80U; out[key_end] = 0U;
            memcpy(out + key_end + 1U, bytes + key_end, length - key_end); *out_length = length + 1U;
        } else if (kind == NONMINIMAL_LENGTH) {
            size_t length_end = 0U;
            CHECK((getvar(bytes, length, &length_end) & 7U) == 2U);
            (void)getvar(bytes, length, &length_end);
            memcpy(out, bytes, length_end); out[length_end - 1U] |= 0x80U; out[length_end] = 0U;
            memcpy(out + length_end + 1U, bytes + length_end, length - length_end); *out_length = length + 1U;
        } else if (kind == NONMINIMAL_SCALAR || kind == OVERFLOW_SCALAR || kind == WRONG_SCALAR) {
            size_t pos = 0U, value_begin = 0U, value_end = 0U;
            uint32_t value = 0U;
            while (pos < length) {
                size_t begin = pos;
                uint32_t key = getvar(bytes, length, &pos);
                if ((key & 7U) == 0U) {
                    value_begin = pos; value = getvar(bytes, length, &pos); value_end = pos; break;
                }
                pos = field_end(bytes, length, begin);
            }
            CHECK(value_end > value_begin);
            if (kind == NONMINIMAL_SCALAR) {
                memcpy(out, bytes, value_end); out[value_end - 1U] |= 0x80U; out[value_end] = 0U;
                memcpy(out + value_end + 1U, bytes + value_end, length - value_end);
                *out_length = length + 1U;
            } else {
                size_t used = value_begin;
                memcpy(out, bytes, value_begin);
                if (kind == OVERFLOW_SCALAR) {
                    memset(out + used, 0xff, 4U); out[used + 4U] = 0x10U; used += 5U;
                } else used += putvar(out + used, value == INT32_MAX ? value - 1U : value + 1U);
                memcpy(out + used, bytes + value_end, length - value_end);
                *out_length = used + length - value_end;
            }
        } else if (kind == STRING_LOCATION) {
            memcpy(out, bytes, length); out[length] = 16U; out[length + 1U] = 1U; *out_length = length + 2U;
        } else if (kind == STRING_PAYLOAD || kind == OVERFLOW_LENGTH) {
            size_t pos = 0U, key_end;
            uint32_t n;
            CHECK((getvar(bytes, length, &pos) & 7U) == 2U); key_end = pos;
            n = getvar(bytes, length, &pos); CHECK(n > 0U && n <= length - pos);
            if (kind == STRING_PAYLOAD) {
                memcpy(out, bytes, length); out[pos] ^= 1U; *out_length = length;
            } else {
                memcpy(out, bytes, key_end); memset(out + key_end, 0xff, 4U); out[key_end + 4U] = 0x10U;
                memcpy(out + key_end + 5U, bytes + pos, length - pos);
                *out_length = key_end + 5U + length - pos;
            }
        } else if (kind == REORDER) {
            size_t second = field_end(bytes, length, first);
            CHECK(first != 0U && second > first);
            memcpy(out, bytes + first, second - first); memcpy(out + second - first, bytes, first);
            memcpy(out + second, bytes + second, length - second); *out_length = length;
        } else {
            CHECK(length == 0U); out[0] = kind == EXPLICIT_ZERO ? 8U : 10U; out[1] = 0U; *out_length = 2U;
        }
        return out;
    } else {
        size_t pos = 0U;
        unsigned occurrence = path[0] >> 16;
        while (pos < length) {
            size_t begin = pos, key_end, payload;
            uint32_t key = getvar(bytes, length, &pos), n;
            key_end = pos;
            if ((key & 7U) != 2U) { pos = field_end(bytes, length, begin); continue; }
            n = getvar(bytes, length, &pos); payload = pos;
            CHECK(n <= length - pos); pos += n;
            if ((key >> 3) == (path[0] & 0xffffU)) {
                size_t child_length, used;
                if (occurrence != 0U) { occurrence--; continue; }
                unsigned char *child = mutate(bytes + payload, n, path + 1U, depth - 1U, kind, &child_length);
                /* A duplicated high-level field can be almost the whole wire. */
                free(out); out = (unsigned char *)malloc(length + child_length + 16U); CHECK(out != NULL);
                memcpy(out, bytes, key_end); used = key_end + putvar(out + key_end, (uint32_t)child_length);
                memcpy(out + used, child, child_length); used += child_length;
                memcpy(out + used, bytes + pos, length - pos); *out_length = used + length - pos;
                free(child); return out;
            }
        }
    }
    CHECK(0); free(out); return NULL;
}

static void reject_wire(sqlparser_handle_t *handle, const unsigned char *wire, size_t length)
{
    PgQueryProtobuf saved = handle->parse_tree;
    handle->parse_tree.data = (char *)wire; handle->parse_tree.len = length;
    { sqlparser_wire_insert_cell_t cells[2];
      sqlparser_wire_insert_t *miss = sqlparser_wire_insert_certify(handle);
      CHECK(miss == NULL);
      CHECK(!sqlparser_wire_insert_certified_row(miss, 0U, cells));
    }
    CHECK(handle->parse_tree.data == (const char *)wire && handle->parse_tree.len == length && handle->ast == NULL);
    handle->parse_tree = saved;
}

static void canonical_tests(void)
{
    static const unsigned paths[][12] = {
        {0}, {2}, {2,1}, {2,1,PG_QUERY__NODE__NODE_INSERT_STMT},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,1},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,2},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,2,PG_QUERY__NODE__NODE_RES_TARGET},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,3},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,3,PG_QUERY__NODE__NODE_SELECT_STMT},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,3,PG_QUERY__NODE__NODE_SELECT_STMT,10},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,3,PG_QUERY__NODE__NODE_SELECT_STMT,10,PG_QUERY__NODE__NODE_LIST},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,3,PG_QUERY__NODE__NODE_SELECT_STMT,10,PG_QUERY__NODE__NODE_LIST,1},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,3,PG_QUERY__NODE__NODE_SELECT_STMT,10,PG_QUERY__NODE__NODE_LIST,1,PG_QUERY__NODE__NODE_A_CONST},
        {2,1,PG_QUERY__NODE__NODE_INSERT_STMT,3,PG_QUERY__NODE__NODE_SELECT_STMT,10,PG_QUERY__NODE__NODE_LIST,1,PG_QUERY__NODE__NODE_A_CONST,PG_QUERY__A__CONST__VAL_IVAL}
    };
    static const size_t depths[] = {0,1,2,3,4,4,5,4,5,6,7,8,9,10};
    char *sql = source(0U, 1);
    sqlparser_handle_t *handle = parse(sql);
    sqlparser_wire_insert_t *certificate = sqlparser_wire_insert_certify(handle);
    PgQueryProtobuf packed = {0};
    sqlparser_surface_source_edits_t none = {0};
    size_t i, kind;
    CHECK(certificate != NULL && certificate->row_count == 64U);
    CHECK(sqlparser_wire_insert_pack(certificate, &none, &packed) == SQLPARSER_STATUS_OK);
    CHECK(packed.len == handle->parse_tree.len && !memcmp(packed.data, handle->parse_tree.data, packed.len)); free(packed.data);
    for (i = 0U; i < sizeof(paths) / sizeof(paths[0]); i++) {
        for (kind = UNKNOWN; kind <= NONMINIMAL_KEY; kind++) {
            size_t length;
            unsigned char *wire = mutate((const unsigned char *)handle->parse_tree.data,
                handle->parse_tree.len, paths[i], depths[i], (enum mutation)kind, &length);
            reject_wire(handle, wire, length); free(wire);
        }
    }
    /* Descend into the nonempty second-cell String arm, not only the first
     * integer. Every ancestor length is repaired so failures exercise inner
     * canonical fields, bounded varints and source parity. */
    { unsigned string_path[10], constant_path[9];
      static const enum mutation string_mutations[] = {
          UNKNOWN, DUPLICATE, NONMINIMAL_KEY, NONMINIMAL_LENGTH,
          STRING_LOCATION, STRING_PAYLOAD, OVERFLOW_LENGTH
      };
      static const enum mutation scalar_mutations[] = {NONMINIMAL_SCALAR, OVERFLOW_SCALAR, WRONG_SCALAR};
      size_t j;
      memcpy(string_path, paths[13], sizeof(string_path));
      string_path[7] = 1U | (1U << 16); string_path[9] = PG_QUERY__A__CONST__VAL_SVAL;
      memcpy(constant_path, string_path, sizeof(constant_path));
      for (j = 0U; j < sizeof(string_mutations) / sizeof(string_mutations[0]); j++) {
        size_t n;
        unsigned char *wire = mutate((const unsigned char *)handle->parse_tree.data, handle->parse_tree.len,
            string_path, 10U, string_mutations[j], &n);
        reject_wire(handle, wire, n); free(wire);
      }
      for (j = 0U; j < sizeof(scalar_mutations) / sizeof(scalar_mutations[0]); j++) {
        const unsigned *targets[] = {paths[13], paths[12], constant_path};
        const size_t target_depths[] = {10U, 9U, 9U}; size_t k;
        for (k = 0U; k < 3U; k++) {
          size_t n;
          unsigned char *wire = mutate((const unsigned char *)handle->parse_tree.data, handle->parse_tree.len,
              targets[k], target_depths[k], scalar_mutations[j], &n);
          reject_wire(handle, wire, n); free(wire);
        }
      }
    }
    /* Messages with two or more distinct ordered fields. */
    { static const unsigned targets[] = {0,1,3,4,6,8,12};
      for (i = 0U; i < sizeof(targets) / sizeof(targets[0]); i++) {
        size_t n, target = targets[i];
        unsigned char *wire = mutate((const unsigned char *)handle->parse_tree.data, handle->parse_tree.len,
            paths[target], depths[target], REORDER, &n);
        reject_wire(handle, wire, n); free(wire);
      }
    }
    for (i = 0U; i < handle->parse_tree.len; i++) reject_wire(handle, (const unsigned char *)handle->parse_tree.data, i);
    { size_t n;
      unsigned char *wire = mutate((const unsigned char *)handle->parse_tree.data, handle->parse_tree.len,
          paths[1], depths[1], NONMINIMAL_LENGTH, &n);
      reject_wire(handle, wire, n); free(wire);
    }
    /* Explicit zero scalar / empty string fields must be absent, rather than
     * serialized as a semantically equivalent but noncanonical default. */
    { char *zero_sql = source(0U, 0), *number, *quote, *end_quote;
      sqlparser_handle_t *zero_handle;
      unsigned scalar_path[10], string_path[10];
      unsigned char *wire; size_t n;
      number = strstr(zero_sql, "VALUES (") + strlen("VALUES ("); *number = '0';
      quote = strchr(number, '\''); end_quote = strchr(quote + 1U, '\'');
      memmove(quote + 1U, end_quote, strlen(end_quote) + 1U);
      zero_handle = parse(zero_sql);
      memcpy(scalar_path, paths[13], sizeof(scalar_path)); memcpy(string_path, scalar_path, sizeof(string_path));
      string_path[7] = 1U | (1U << 16); string_path[9] = PG_QUERY__A__CONST__VAL_SVAL;
      wire = mutate((const unsigned char *)zero_handle->parse_tree.data, zero_handle->parse_tree.len,
          scalar_path, 10U, EXPLICIT_ZERO, &n); reject_wire(zero_handle, wire, n); free(wire);
      wire = mutate((const unsigned char *)zero_handle->parse_tree.data, zero_handle->parse_tree.len,
          string_path, 10U, EXPLICIT_EMPTY, &n); reject_wire(zero_handle, wire, n); free(wire);
      sqlparser_handle_destroy(zero_handle); free(zero_sql);
    }
#ifdef SQLPARSER_WIRE_CODEC_WRAPPERS
    for (i = 1U; i <= 2U; i++) {
        arm(i); CHECK(sqlparser_wire_insert_certify(handle) == NULL); disarm();
        CHECK(handle->ast == NULL);
    }
    arm(1U); CHECK(sqlparser_wire_insert_pack(certificate, &none, &packed) == SQLPARSER_STATUS_UNSUPPORTED); disarm();
    CHECK(packed.data == NULL && packed.len == 0U);
    arm(2U); CHECK(sqlparser_wire_insert_pack(certificate, &none, &packed) == SQLPARSER_STATUS_NO_MEMORY); disarm();
    CHECK(packed.data == NULL && packed.len == 0U);
#endif
    sqlparser_wire_insert_destroy(certificate); sqlparser_handle_destroy(handle); free(sql);
}

static char *edited_source(const char *sql, const sqlparser_surface_source_edits_t *edits)
{
    size_t i, length = strlen(sql), from = 0U, used = 0U;
    char *result;
    for (i = 0U; i < edits->count; i++) length += edits->items[i].replacement_length - (edits->items[i].source_end - edits->items[i].source_start);
    result = (char *)malloc(length + 1U); CHECK(result != NULL);
    for (i = 0U; i < edits->count; i++) {
        const sqlparser_surface_source_edit_t *e = &edits->items[i];
        memcpy(result + used, sql + from, e->source_start - from); used += e->source_start - from;
        memcpy(result + used, e->replacement, e->replacement_length); used += e->replacement_length; from = e->source_end;
    }
    strcpy(result + used, sql + from); CHECK(strlen(result) == length); return result;
}

static void pack_tests(size_t padding, int semicolon, int growing)
{
    static const size_t rows[] = {0U, 1U, 31U, 63U};
    static const size_t growing_lengths[] = {127U, 128U, 16383U, 16384U};
    static const size_t mixed_lengths[] = {0U, 1U, 49U, 2U};
    const size_t *lengths = growing ? growing_lengths : mixed_lengths;
    char *sql = source(padding, semicolon), *expected;
    sqlparser_handle_t *handle = parse(sql), *reference;
    sqlparser_wire_insert_t *certificate = sqlparser_wire_insert_certify(handle);
    sqlparser_surface_source_edit_t items[4], reverse[4];
    sqlparser_surface_source_edits_t edits = {items, 4U, 4U}, invalid = {reverse, 4U, 4U};
    PgQueryProtobuf packed = {0};
    size_t i;
    CHECK(certificate != NULL);
    for (i = 0U; i < 64U; i++) {
        sqlparser_wire_insert_cell_t cells[2], fast[2];
        CHECK(sqlparser_wire_insert_row(certificate, i, cells));
        CHECK(sqlparser_wire_insert_certified_row(certificate, i, fast));
        CHECK(!memcmp(cells, fast, sizeof(cells)));
        CHECK(cells[0].location < cells[1].location && sql[cells[1].location] == '\'');
        CHECK(i == 0U || certificate->row_offsets[i] > certificate->row_offsets[i - 1U]);
        CHECK(cells[0].integer == (int32_t)((unsigned[]){1,0,127,128,16383,16384,INT32_MAX}[i % 7U]));
    }
    CHECK(!sqlparser_wire_insert_row(certificate, 64U, (sqlparser_wire_insert_cell_t[2]){{0}}));
    CHECK(!sqlparser_wire_insert_certified_row(certificate, 64U, (sqlparser_wire_insert_cell_t[2]){{0}}));
    { sqlparser_wire_insert_t bounded = *certificate; size_t length;
      for (length = certificate->row_offsets[0]; length < certificate->row_offsets[1]; length++) {
        sqlparser_wire_insert_cell_t cells[2];
        bounded.wire_length = length;
        CHECK(!sqlparser_wire_insert_certified_row(&bounded, 0U, cells));
      }
    }
    for (i = 0U; i < 4U; i++) {
        sqlparser_wire_insert_cell_t cells[2];
        CHECK(sqlparser_wire_insert_row(certificate, rows[i], cells));
        items[i].source_start = (size_t)cells[1].location;
        items[i].source_end = items[i].source_start + cells[1].length + 2U;
        items[i].replacement_length = lengths[i] + 2U;
        items[i].replacement = (char *)malloc(lengths[i] + 3U); CHECK(items[i].replacement != NULL);
        memset(items[i].replacement + 1U, (int)('a' + i), lengths[i]);
        items[i].replacement[0] = items[i].replacement[lengths[i] + 1U] = '\'';
        items[i].replacement[lengths[i] + 2U] = '\0';
    }
    for (i = 0U; i < 4U; i++) reverse[i] = items[3U - i];
    CHECK(sqlparser_wire_insert_pack(certificate, &invalid, &packed) == SQLPARSER_STATUS_UNSUPPORTED && packed.data == NULL);
    items[0].source_end--; CHECK(sqlparser_wire_insert_pack(certificate, &edits, &packed) == SQLPARSER_STATUS_UNSUPPORTED); items[0].source_end++;
    items[2].replacement[1] = '\\'; CHECK(sqlparser_wire_insert_pack(certificate, &edits, &packed) == SQLPARSER_STATUS_UNSUPPORTED); items[2].replacement[1] = 'c';
    /* The proven entry retains all structural guards, including exact
     * string-cell membership, despite reusing the token-content proof. */
    CHECK(sqlparser_wire_insert_pack_proven_edits(certificate, &invalid, &packed) == SQLPARSER_STATUS_UNSUPPORTED && packed.data == NULL);
    items[0].source_end--;
    CHECK(sqlparser_wire_insert_pack_proven_edits(certificate, &edits, &packed) == SQLPARSER_STATUS_UNSUPPORTED && packed.data == NULL);
    items[0].source_end++;
    {
        char saved = items[2].replacement[0];
        items[2].replacement[0] = 'x';
        CHECK(sqlparser_wire_insert_pack_proven_edits(certificate, &edits, &packed) == SQLPARSER_STATUS_UNSUPPORTED && packed.data == NULL);
        items[2].replacement[0] = saved;
    }
#ifdef SQLPARSER_WIRE_CODEC_WRAPPERS
    arm(1U); CHECK(sqlparser_wire_insert_pack_proven_edits(certificate, &edits, &packed) == SQLPARSER_STATUS_UNSUPPORTED); disarm();
    CHECK(packed.data == NULL && packed.len == 0U);
    arm(2U); CHECK(sqlparser_wire_insert_pack_proven_edits(certificate, &edits, &packed) == SQLPARSER_STATUS_NO_MEMORY); disarm();
    CHECK(packed.data == NULL && packed.len == 0U);
#endif
    expected = edited_source(sql, &edits); reference = parse(expected);
    {
        PgQueryProtobuf defensive = {0};
        CHECK(sqlparser_wire_insert_pack(certificate, &edits, &defensive) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_wire_insert_pack_proven_edits(certificate, &edits, &packed) == SQLPARSER_STATUS_OK);
        CHECK(packed.len == defensive.len && memcmp(packed.data, defensive.data, packed.len) == 0);
        free(defensive.data);
    }
    for (i = 0U; i < 4U; i++) free(items[i].replacement);
    sqlparser_wire_insert_destroy(certificate); sqlparser_handle_destroy(handle); free(sql); free(expected);
    CHECK(packed.len == reference->parse_tree.len && !memcmp(packed.data, reference->parse_tree.data, packed.len));
    free(packed.data); sqlparser_handle_destroy(reference);
}

static void replacement_byte_tests(void)
{
    char *sql = source(0U, 1), replacement[] = "'x'";
    sqlparser_handle_t *handle = parse(sql);
    sqlparser_wire_insert_t *certificate = sqlparser_wire_insert_certify(handle);
    sqlparser_wire_insert_cell_t cells[2];
    sqlparser_surface_source_edit_t item;
    sqlparser_surface_source_edits_t edits = {&item, 1U, 1U};
    unsigned byte;
    CHECK(certificate != NULL && sqlparser_wire_insert_row(certificate, 31U, cells));
    item.source_start = (size_t)cells[1].location;
    item.source_end = item.source_start + cells[1].length + 2U;
    item.replacement = replacement; item.replacement_length = 3U;
    for (byte = 0U; byte <= 255U; byte++) {
        PgQueryProtobuf defensive = {0}, proven = {0};
        int plain = byte >= 0x20U && byte <= 0x7eU && byte != 0x27U && byte != 0x5cU;
        replacement[1] = (char)byte;
        CHECK(sqlparser_wire_insert_pack(certificate, &edits, &defensive) ==
            (plain ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_UNSUPPORTED));
        if (plain) {
            CHECK(sqlparser_wire_insert_pack_proven_edits(certificate, &edits, &proven) == SQLPARSER_STATUS_OK);
            CHECK(proven.len == defensive.len && memcmp(proven.data, defensive.data, proven.len) == 0);
        } else CHECK(defensive.data == NULL && defensive.len == 0U);
        free(defensive.data); free(proven.data);
    }
    sqlparser_wire_insert_destroy(certificate); sqlparser_handle_destroy(handle); free(sql);
}

int main(void)
{
    canonical_tests(); replacement_byte_tests();
    pack_tests(0U, 0, 1); pack_tests(80U, 1, 1); pack_tests(16330U, 1, 1);
    pack_tests(80U, 1, 0); pack_tests(16330U, 1, 0);
    pg_query_exit();
    printf("wire codec: canonical rejection, row/identity/varint parity, independent ownership and allocation failures passed (%zu checks)\n", checks);
    return 0;
}
