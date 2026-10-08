/* General scalar wire codec: canonical bytes, location remapping, conservative
 * admission and independent allocation failures. No performance assertions. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/core/sqlparser_wire_insert.c"

static sqlparser_error_t error;
static size_t checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error.message); abort(); } } while (0)
#ifdef SQLPARSER_SCALAR_CODEC_WRAPPERS
static size_t fail_at, fail_also_at, allocations, failures, allocation_sizes[8];
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__wrap_malloc(size_t n) {
    if (fail_at != 0U) {
        ++allocations;
        if (allocations <= 8U) allocation_sizes[allocations - 1U] = n;
        if (allocations == fail_at || allocations == fail_also_at) { failures++; return NULL; }
    }
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t size) {
    if (fail_at != 0U && ++allocations == fail_at) { failures++; return NULL; }
    return __real_calloc(n, size);
}
static void arm(size_t index) { fail_at = index; fail_also_at = 0U; allocations = failures = 0U; }
static void disarm(void) { fail_at = 0U; CHECK(failures == 1U); }
#endif

#ifdef SQLPARSER_SCALAR_CODEC_WRAPPERS
/* Compare the optional validated write plan with the unchanged two-pass path
 * on every successful normal pack, including zero, partial and all edits. */
static sqlparser_status_t differential_pack(const sqlparser_wire_scalar_insert_t *cert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out, int proven)
{
    sqlparser_status_t status = proven ?
        sqlparser_wire_scalar_insert_pack_proven_edits(cert, edits, out) :
        sqlparser_wire_scalar_insert_pack(cert, edits, out);
    if (status == SQLPARSER_STATUS_OK && fail_at == 0U) {
        PgQueryProtobuf old = {0};
        sqlparser_status_t old_status;
        arm(2U);
        old_status = proven ? sqlparser_wire_scalar_insert_pack_proven_edits(cert, edits, &old) :
            sqlparser_wire_scalar_insert_pack(cert, edits, &old);
        disarm();
        CHECK(old_status == SQLPARSER_STATUS_OK && old.len == out->len);
        CHECK(memcmp(old.data, out->data, old.len) == 0);
        free(old.data);
    }
    return status;
}
#define sqlparser_wire_scalar_insert_pack(c, e, o) differential_pack(c, e, o, 0)
#define sqlparser_wire_scalar_insert_pack_proven_edits(c, e, o) differential_pack(c, e, o, 1)
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
        /* Optional validated-plan allocation failure retains the old codec. */
        arm(2U); CHECK(sqlparser_wire_scalar_insert_pack(cert, &edits, &packed) == SQLPARSER_STATUS_OK); disarm();
        same_wire(&packed, reference); free(packed.data);
        arm(3U); CHECK(sqlparser_wire_scalar_insert_pack(cert, &edits, &packed) == SQLPARSER_STATUS_NO_MEMORY); disarm();
        CHECK(packed.data == NULL && packed.len == 0U);
        arm(2U); fail_also_at = 3U;
        CHECK(sqlparser_wire_scalar_insert_pack(cert, &edits, &packed) == SQLPARSER_STATUS_NO_MEMORY);
        fail_at = fail_also_at = 0U;
        CHECK(failures == 2U && packed.data == NULL && packed.len == 0U);
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

/* Include the private implementation above rather than exposing a test ABI.
 * Every short capacity must match the original checked writer byte-for-byte,
 * including untouched bytes, cursor movement and sticky failure state. */
static void planned_writer_boundaries(void)
{
    static const uint32_t values[] = {0U, 1U, 126U, 127U, 128U, 129U,
        16382U, 16383U, 16384U, 16385U, 2097151U, 2097152U,
        268435455U, 268435456U, INT32_MAX};
    static const uint32_t lengths[] = {0U, 1U, 117U, 118U, 119U, 120U,
        126U, 127U, 128U, 129U, 16370U, 16371U, 16382U, 16383U, 16384U};
    size_t kind, index;
    char *text = malloc(16384U);
    uint8_t *actual = malloc(16448U), *expected = malloc(16448U);
    CHECK(text != NULL && actual != NULL && expected != NULL);
    memset(text, 'q', 16384U);
    for (kind = SQLPARSER_WIRE_SCALAR_INTEGER; kind <= SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION; ++kind) {
        for (index = 0U; index < sizeof(values) / sizeof(values[0]); ++index) {
            sqlparser_wire_scalar_cell_t cell = {0};
            wi_cell_sizes sizes;
            wsi_planned_cell entry;
            size_t count, capacity, sticky;
            cell.kind = (sqlparser_wire_scalar_kind_t)kind;
            cell.location = (int32_t)values[index];
            cell.integer = kind == SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION ?
                PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_DATE : (int32_t)values[index];
            cell.text = text;
            cell.length = lengths[index];
            if (kind == SQLPARSER_WIRE_SCALAR_FLOAT && cell.length == 0U) cell.length = 1U;
            CHECK(wsi_measure_cell(&cell, &sizes));
            wsi_plan_cell(&entry, &cell, &sizes);
            count = (size_t)wi_envelope(WI_LIST_ITEMS, sizes.node);
            for (sticky = 0U; sticky <= 1U; ++sticky) {
                for (capacity = 0U; capacity <= count + 1U; ++capacity) {
                    wi_writer a = {actual, actual + capacity, (int)sticky};
                    wi_writer b = {expected, expected + capacity, (int)sticky};
                    memset(actual, 0xa5, count + 2U);
                    memset(expected, 0xa5, count + 2U);
                    wsi_write_planned_cell(&a, &entry);
                    wsi_write_cell(&b, &cell, &sizes);
                    CHECK(a.failed == b.failed && a.next - actual == b.next - expected);
                    CHECK(memcmp(actual, expected, count + 2U) == 0);
                    if (capacity >= count) CHECK(a.next == actual + count && a.failed == (int)sticky);
                }
            }
        }
    }
    free(expected); free(actual); free(text);
}

#ifdef SQLPARSER_SCALAR_CODEC_WRAPPERS
static void plan_budget_and_lifetime(void)
{
    const size_t entry_size = sizeof(wsi_planned_cell);
    const size_t maximum_rows = (2U * 1024U * 1024U) / entry_size / 9U;
    const size_t rows[] = {5000U, maximum_rows, maximum_rows + 1U};
    size_t i;
    CHECK(entry_size == sizeof(const char *) + 24U);
    for (i = 0U; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        size_t row, used;
        char *sql = malloc(rows[i] * 64U + 256U);
        sqlparser_handle_t *handle;
        sqlparser_wire_scalar_insert_t *cert;
        sqlparser_surface_source_edits_t none = {0};
        PgQueryProtobuf packed = {0};
        PgQuery__ParseResult *tree;
        CHECK(sql != NULL);
        used = (size_t)sprintf(sql, "INSERT INTO budget_test(a,b,c,d,e,f,g,h,i) VALUES ");
        for (row = 0U; row < rows[i]; ++row)
            used += (size_t)sprintf(sql + used, "%s(1,127,'text',100.50,0,128,16384,2,3)", row ? "," : "");
        strcpy(sql + used, ";");
        handle = parse(sql);
        cert = sqlparser_wire_scalar_insert_certify(handle); CHECK(cert != NULL);
        arm(SIZE_MAX);
        /* Parenthesized symbol bypasses the differential helper: the oversized
         * case intentionally skips the optional allocation altogether. */
        CHECK((sqlparser_wire_scalar_insert_pack)(cert, &none, &packed) == SQLPARSER_STATUS_OK);
        fail_at = 0U;
        CHECK(failures == 0U);
        CHECK(allocations == (rows[i] <= maximum_rows ? 3U : 2U));
        CHECK(allocation_sizes[0] == rows[i] * sizeof(uint32_t));
        if (rows[i] <= maximum_rows) {
            CHECK(allocation_sizes[1] == rows[i] * 9U * entry_size);
            CHECK(allocation_sizes[1] <= 2U * 1024U * 1024U);
            printf("validated plan: %zu-byte entries, %zu cells, %zu transient bytes plus %zu row-size bytes\n", entry_size, rows[i] * 9U, allocation_sizes[1], rows[i] * sizeof(uint32_t));
        }
        same_wire(&packed, handle);
        if (rows[i] <= maximum_rows) {
            PgQueryProtobuf fallback = {0};
            arm(2U);
            CHECK((sqlparser_wire_scalar_insert_pack)(cert, &none, &fallback) == SQLPARSER_STATUS_OK);
            disarm();
            CHECK(fallback.len == packed.len && memcmp(fallback.data, packed.data, packed.len) == 0);
            free(fallback.data);
        }
        sqlparser_wire_scalar_insert_destroy(cert);
        sqlparser_handle_destroy(handle);
        memset(sql, 0xa5, used); free(sql);
        tree = pg_query__parse_result__unpack(NULL, packed.len, (const uint8_t *)packed.data);
        CHECK(tree != NULL && tree->n_stmts == 1U);
        CHECK(tree->stmts[0]->stmt->insert_stmt->select_stmt->select_stmt->n_values_lists == rows[i]);
        pg_query__parse_result__free_unpacked(tree, NULL);
        free(packed.data);
    }
}
#endif

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
    planned_writer_boundaries();
#ifdef SQLPARSER_SCALAR_CODEC_WRAPPERS
    plan_budget_and_lifetime();
#endif
    pg_query_exit();
    printf("scalar wire codec: %zu checks passed\n", checks);
    return 0;
}
