/* General scalar wire codec: canonical bytes, location remapping, conservative
 * admission and independent allocation failures. No performance assertions. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_wire_insert_internal.h"

static sqlparser_error_t error;
static size_t checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_SCALAR_CODEC_WRAPPERS
static size_t fail_at, allocations, failures;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__wrap_malloc(size_t n) {
    if (fail_at != 0U && ++allocations == fail_at) { failures++; return NULL; }
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t size) {
    if (fail_at != 0U && ++allocations == fail_at) { failures++; return NULL; }
    return __real_calloc(n, size);
}
static void arm(size_t index) { fail_at = index; allocations = failures = 0U; }
static void disarm(void) { fail_at = 0U; CHECK(failures == 1U); }
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

static char *source(size_t columns, size_t padding, int semicolon, int changed, const char *expression)
{
    size_t row, col, used = padding;
    char *sql = malloc(padding + 64U * columns * 96U + 2048U);
    CHECK(sql != NULL);
    memset(sql, ' ', padding);
    used += (size_t)sprintf(sql + used, "InSeRt INTO test_lib.some_table(");
    for (col = 0U; col < columns; col++) used += (size_t)sprintf(sql + used, "%sc%zu", col ? "," : "", col);
    used += (size_t)sprintf(sql + used, ") VALUES ");
    for (row = 0U; row < 64U; row++) {
        used += (size_t)sprintf(sql + used, "%s(", row ? "," : "");
        for (col = 0U; col < columns; col++) {
            const char *token;
            if (col == 2U) token = changed ? (row % 3U == 0U ? "''" : row % 3U == 1U ? "'x'" : "'this replacement is considerably longer'") : "'中文-original'";
            else if (col + 1U == columns) token = expression;
            else token = (row + col) % 4U == 0U ? "0" : (row + col) % 4U == 1U ? "127" :
                (row + col) % 4U == 2U ? "100.50" : "''";
            used += (size_t)sprintf(sql + used, "%s%s", col ? "," : "", token);
        }
        used += (size_t)sprintf(sql + used, ")");
    }
    if (semicolon) strcpy(sql + used, " \t; \r\n");
    return sql;
}

static void same_wire(const PgQueryProtobuf *wire, const sqlparser_handle_t *expected)
{
    CHECK(wire->len == expected->parse_tree.len);
    CHECK(memcmp(wire->data, expected->parse_tree.data, wire->len) == 0);
}

static void roundtrip(size_t columns, size_t padding, int semicolon, const char *expression)
{
    char *sql = source(columns, padding, semicolon, 0, expression);
    char *expected = source(columns, padding, semicolon, 1, expression);
    sqlparser_handle_t *handle = parse(sql), *reference = parse(expected);
    sqlparser_wire_scalar_insert_t *cert = sqlparser_wire_scalar_insert_certify(handle);
    sqlparser_surface_source_edit_t items[64] = {{0}};
    sqlparser_surface_source_edits_t edits = {0}, none = {0};
    sqlparser_wire_scalar_cell_t *cells = calloc(columns, sizeof(*cells));
    sqlparser_wire_scalar_cell_t *fast = calloc(columns, sizeof(*fast));
    PgQueryProtobuf packed = {0};
    size_t row, column;
    CHECK(cert != NULL && cells != NULL && fast != NULL && cert->column_count == columns && cert->row_count == 64U);
    CHECK(sqlparser_wire_scalar_insert_pack(cert, &none, &packed) == SQLPARSER_STATUS_OK);
    same_wire(&packed, handle); free(packed.data);
    for (row = 0U; row < 64U; row++) {
        const char *replacement = row % 3U == 0U ? "''" : row % 3U == 1U ? "'x'" : "'this replacement is considerably longer'";
        CHECK(sqlparser_wire_scalar_insert_row(cert, row, cells));
        CHECK(sqlparser_wire_scalar_insert_certified_row(cert, row, fast));
        CHECK(memcmp(cells, fast, columns * sizeof(*cells)) == 0);
        for (column = 0U; column < columns; column++) {
            sqlparser_wire_scalar_cell_t cell, direct;
            CHECK(sqlparser_wire_scalar_insert_cell(cert, row, column, &cell));
            CHECK(sqlparser_wire_scalar_insert_certified_cell(cert, row, column, &direct));
            CHECK(memcmp(&cell, &direct, sizeof(cell)) == 0);
            CHECK(cell.kind == cells[column].kind && cell.location == cells[column].location && cell.length == cells[column].length && cell.integer == cells[column].integer);
            if (cell.kind != SQLPARSER_WIRE_SCALAR_INTEGER) CHECK(cell.text != NULL && memcmp(cell.text, cells[column].text, cell.length) == 0);
        }
        items[row].source_start = (size_t)cells[2].location;
        items[row].source_end = items[row].source_start + cells[2].length + 2U;
        items[row].replacement = (char *)replacement;
        items[row].replacement_length = strlen(replacement);
    }
    edits.items = items; edits.count = edits.capacity = 64U;
    CHECK(sqlparser_wire_scalar_insert_pack(cert, &edits, &packed) == SQLPARSER_STATUS_OK);
    same_wire(&packed, reference); free(packed.data);
    CHECK(sqlparser_wire_scalar_insert_pack_proven_edits(cert, &edits, &packed) == SQLPARSER_STATUS_OK);
    same_wire(&packed, reference); free(packed.data);
    CHECK(!sqlparser_wire_scalar_insert_row(cert, 64U, cells));
    CHECK(!sqlparser_wire_scalar_insert_cell(cert, 0U, columns, cells));
    CHECK(!sqlparser_wire_scalar_insert_certified_row(cert, 64U, cells));
    CHECK(!sqlparser_wire_scalar_insert_certified_cell(cert, 0U, columns, cells));
    {
        /* The immutable row certificate may borrow an allocation larger than
         * its advertised wire length. Every incomplete outer row declines. */
        static const size_t selected[] = {0U, 31U, 63U};
        size_t selected_index;
        for (selected_index = 0U; selected_index < sizeof(selected) / sizeof(selected[0]); selected_index++) {
            size_t selected_row = selected[selected_index];
            size_t begin = cert->row_offsets[selected_row], pos = begin + 1U, cut;
            uint32_t row_length = 0U;
            unsigned shift = 0U;
            unsigned char byte;
            sqlparser_wire_scalar_insert_t bounded = *cert;
            do {
                CHECK(pos < cert->wire_length && shift <= 28U);
                byte = (unsigned char)cert->wire[pos++];
                row_length |= (uint32_t)(byte & 0x7fU) << shift;
                shift += 7U;
            } while (byte & 0x80U);
            CHECK(row_length <= cert->wire_length - pos);
            for (cut = begin; cut < pos + row_length; cut++) {
                bounded.wire_length = cut;
                CHECK(!sqlparser_wire_scalar_insert_certified_row(&bounded, selected_row, fast));
                CHECK(!sqlparser_wire_scalar_insert_certified_cell(&bounded, selected_row, 0U, fast));
            }
            bounded.wire_length = pos + row_length;
            CHECK(sqlparser_wire_scalar_insert_certified_row(&bounded, selected_row, fast));
        }
    }
    {
        sqlparser_surface_source_edit_t bad = items[0];
        sqlparser_surface_source_edits_t invalid = {0};
        invalid.items = &bad; invalid.count = invalid.capacity = 1U;
        bad.replacement = "'has\\escape'"; bad.replacement_length = strlen(bad.replacement);
        CHECK(sqlparser_wire_scalar_insert_pack(cert, &invalid, &packed) == SQLPARSER_STATUS_UNSUPPORTED && packed.data == NULL);
        bad = items[0]; bad.source_end++;
        CHECK(sqlparser_wire_scalar_insert_pack(cert, &invalid, &packed) == SQLPARSER_STATUS_UNSUPPORTED && packed.data == NULL);
        bad = items[0]; bad.source_start--;
        CHECK(sqlparser_wire_scalar_insert_pack(cert, &invalid, &packed) == SQLPARSER_STATUS_UNSUPPORTED && packed.data == NULL);
        bad = items[0]; bad.replacement = "'can''t'"; bad.replacement_length = strlen(bad.replacement);
        CHECK(sqlparser_wire_scalar_insert_pack(cert, &invalid, &packed) == SQLPARSER_STATUS_UNSUPPORTED && packed.data == NULL);
    }
#ifdef SQLPARSER_SCALAR_CODEC_WRAPPERS
    {
        size_t index;
        for (index = 1U; index <= 3U; index++) {
            sqlparser_wire_scalar_insert_t *miss;
            arm(index); miss = sqlparser_wire_scalar_insert_certify(handle); disarm(); CHECK(miss == NULL);
            CHECK(handle->ast == NULL && !handle->failed);
        }
        arm(1U); CHECK(sqlparser_wire_scalar_insert_pack(cert, &edits, &packed) == SQLPARSER_STATUS_UNSUPPORTED); disarm();
        CHECK(packed.data == NULL);
        arm(2U); CHECK(sqlparser_wire_scalar_insert_pack(cert, &edits, &packed) == SQLPARSER_STATUS_NO_MEMORY); disarm();
        CHECK(packed.data == NULL);
        CHECK(sqlparser_wire_scalar_insert_pack(cert, &edits, &packed) == SQLPARSER_STATUS_OK);
        same_wire(&packed, reference); free(packed.data);
    }
#endif
    /* Every truncated wire must decline rather than read into the allocation's
     * remaining bytes. Length is the public bound, not malloc usable size. */
    {
        size_t saved = handle->parse_tree.len, length;
        for (length = 0U; length < saved; length++) {
            sqlparser_wire_scalar_insert_t *miss;
            handle->parse_tree.len = length;
            miss = sqlparser_wire_scalar_insert_certify(handle);
            CHECK(miss == NULL);
        }
        handle->parse_tree.len = saved;
    }
    free(fast); free(cells); sqlparser_wire_scalar_insert_destroy(cert);
    sqlparser_handle_destroy(reference); sqlparser_handle_destroy(handle); free(expected); free(sql);
}

static void long_string_boundaries(void)
{
    static const size_t lengths[] = {127U, 128U, 16383U, 16384U};
    size_t index;
    for (index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); index++) {
        char *base = source(13U, 0U, 1, 0, "CURRENT_TIMESTAMP");
        const char *old = strstr(base, "'中文-original'");
        size_t before, old_length = strlen("'中文-original'"), length = lengths[index];
        char *sql, *expected;
        sqlparser_handle_t *handle, *reference;
        sqlparser_wire_scalar_insert_t *cert;
        sqlparser_wire_scalar_cell_t cells[13], fast[13];
        sqlparser_surface_source_edit_t edit = {0};
        sqlparser_surface_source_edits_t edits = {0}, none = {0};
        PgQueryProtobuf packed = {0};
        CHECK(old != NULL);
        before = (size_t)(old - base);
        sql = malloc(strlen(base) - old_length + length + 3U);
        expected = malloc(strlen(base) + 1U);
        CHECK(sql != NULL && expected != NULL);
        memcpy(sql, base, before); sql[before] = '\'';
        memset(sql + before + 1U, 'q', length); sql[before + length + 1U] = '\'';
        strcpy(sql + before + length + 2U, old + old_length);
        memcpy(expected, base, before); strcpy(expected + before, "''");
        strcpy(expected + before + 2U, old + old_length);
        handle = parse(sql); reference = parse(expected);
        cert = sqlparser_wire_scalar_insert_certify(handle); CHECK(cert != NULL);
        CHECK(sqlparser_wire_scalar_insert_row(cert, 0U, cells));
        CHECK(sqlparser_wire_scalar_insert_certified_row(cert, 0U, fast));
        CHECK(memcmp(cells, fast, sizeof(cells)) == 0 && cells[2].length == length);
        CHECK(sqlparser_wire_scalar_insert_pack(cert, &none, &packed) == SQLPARSER_STATUS_OK);
        same_wire(&packed, handle); free(packed.data);
        edit.source_start = before; edit.source_end = before + length + 2U;
        edit.replacement = "''"; edit.replacement_length = 2U;
        edits.items = &edit; edits.count = edits.capacity = 1U;
        CHECK(sqlparser_wire_scalar_insert_pack(cert, &edits, &packed) == SQLPARSER_STATUS_OK);
        same_wire(&packed, reference); free(packed.data);
        CHECK(sqlparser_wire_scalar_insert_pack_proven_edits(cert, &edits, &packed) == SQLPARSER_STATUS_OK);
        same_wire(&packed, reference); free(packed.data);
        sqlparser_wire_scalar_insert_destroy(cert); sqlparser_handle_destroy(handle);
        sqlparser_handle_destroy(reference); free(base); free(sql); free(expected);
    }
}

int main(void)
{
    static const char *functions[] = {"CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP", "LOCALTIME", "LOCALTIMESTAMP", "CURRENT_ROLE", "CURRENT_USER", "USER", "SESSION_USER", "CURRENT_CATALOG", "CURRENT_SCHEMA"};
    static const char *fallbacks[] = {"CURRENT_TIMESTAMP(3)", "CURRENT_TIME(0)", "LOCALTIMESTAMP(6)", "LOCALTIME(2)", "NULL", "TRUE", "1 + 2", "-1"};
    static const size_t arities[] = {3U, 8U, 9U, 10U, 13U};
    size_t index;
    /* Arity three uses its last column as the string target. */
    for (index = 0U; index < sizeof(arities) / sizeof(arities[0]); index++) roundtrip(arities[index], 0U, (int)(index % 2U), "CURRENT_TIMESTAMP");
    roundtrip(5U, 126U, 1, "CURRENT_TIMESTAMP");
    roundtrip(5U, 16370U, 0, "CURRENT_TIMESTAMP");
    for (index = 0U; index < sizeof(functions) / sizeof(functions[0]); index++) roundtrip(5U, 0U, 1, functions[index]);
    for (index = 0U; index < sizeof(fallbacks) / sizeof(fallbacks[0]); index++) {
        char *sql = source(5U, 0U, 1, 0, fallbacks[index]);
        sqlparser_handle_t *handle = parse(sql);
        sqlparser_wire_scalar_insert_t *cert = sqlparser_wire_scalar_insert_certify(handle);
        CHECK(cert == NULL); sqlparser_handle_destroy(handle); free(sql);
    }
    long_string_boundaries();
    pg_query_exit();
    printf("scalar wire codec: %zu checks passed\n", checks);
    return 0;
}
