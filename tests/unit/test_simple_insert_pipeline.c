/* Public pipeline proof, not a benchmark. Define
 * SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS and link GNU
 * --wrap=raw_parser_with_options to instrument actual grammar entry. Define SQLPARSER_SIMPLE_INSERT_BASELINE
 * only for a reference build without native INSERT recognition: semantics remain identical,
 * but its initial parse must use grammar. --record emits complete outputs and
 * invalid-parse diagnostics for byte comparison (no timing or hashes).
 *
 * primary5000 matches the public batch INSERT benchmark workload. No private
 * parser/AST entry is called: parse -> graph -> actual graph selectors ->
 * one apply -> adjacent deparse. Source and patch ownership are tested too.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "sqlparser/sqlparser.h"
#ifdef SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "postgres.h"
#include "parser/parser.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

static const char *case_name = "start", *stage = "start";
static const char *filter_case;
static int filter_typed = -1, filter_changes = -1, filter_before = -1;
static sqlparser_error_t error;
static size_t cases;
static int record;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, \
    "%s:%d: %s case=%s stage=%s completed=%zu error=%s\n", \
    __FILE__, __LINE__, #x, case_name, stage, cases, error.message); abort(); \
} } while (0)

enum phase { PARSE, GRAPH, CONSTRUCT, APPLY, DEPARSE, VERIFY, PHASE_COUNT };
static enum phase phase;
static size_t grammar[PHASE_COUNT];
#ifdef SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS
List *__real_raw_parser_with_options(const char *, RawParseMode, bool);
List *__wrap_raw_parser_with_options(const char *sql, RawParseMode mode, bool preserve)
{
    grammar[phase]++;
    return __real_raw_parser_with_options(sql, mode, preserve);
}
#endif

typedef struct buffer { char *data; size_t length, capacity; } buffer;
typedef struct fixture {
    const char *name, *table, *first, *second;
    size_t rows, exact_length, long_string, exceptional_row;
    const char *exceptional_number;
    int fast, primary;
} fixture;
enum changes { NONE, FEW, ALL };

static void *allocate(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1U, size);
    CHECK(p != NULL);
    return p;
}

static void reserve(buffer *b, size_t extra)
{
    size_t needed = b->length + extra + 1U;
    if (needed > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 256U;
        while (capacity < needed) capacity *= 2U;
        b->data = realloc(b->data, capacity);
        CHECK(b->data != NULL);
        b->capacity = capacity;
    }
}

static void append(buffer *b, const char *format, ...)
{
    va_list args, copy;
    int n;
    va_start(args, format); va_copy(copy, args);
    n = vsnprintf(NULL, 0U, format, copy); va_end(copy);
    CHECK(n >= 0);
    reserve(b, (size_t)n);
    CHECK(vsnprintf(b->data + b->length, b->capacity - b->length, format, args) == n);
    va_end(args); b->length += (size_t)n;
}

static int selected(const fixture *f, enum changes changes, size_t row)
{
    return changes == ALL || (changes == FEW &&
        (row == 0U || row == f->rows / 2U || row + 1U == f->rows));
}

static char *original_text(const fixture *f, size_t row)
{
    size_t length = f->long_string ? f->long_string : 64U;
    char *text = allocate(length + 1U, 1U);
    int n = snprintf(text, length + 1U, "small-secret-%04zu", row + 1U);
    CHECK(n > 0 && (size_t)n <= length);
    if (f->long_string) {
        memset(text + n, (int)('a' + row % 26U), length - (size_t)n);
        text[length] = '\0';
    }
    return text;
}

/* The expected-SQL generator does not inspect parser output or patch data,
 * search/replace source spans, or parse/deparse an oracle handle. Changed
 * rows and complete literals are generated directly from fixture row numbers. */
static buffer source_text(const fixture *f, enum changes changes, size_t padding)
{
    buffer b = {0};
    size_t row;
    if (padding) {
        static const char whitespace[] = " \t\n\r\f";
        reserve(&b, padding);
        for (row = 0U; row < padding; row++) b.data[row] = whitespace[row % 5U];
        b.length = padding; b.data[b.length] = '\0';
    }
    append(&b, "INSERT INTO %s(%s, %s) VALUES ", f->table, f->first, f->second);
    for (row = 0U; row < f->rows; row++) {
        append(&b, "%s(", row ? "," : "");
        if (f->exceptional_number != NULL && (row == f->exceptional_row || f->exceptional_row == SIZE_MAX))
            append(&b, "%s", f->exceptional_number);
        else append(&b, "%zu", row + 1U);
        if (selected(f, changes, row))
            append(&b, ",'masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567')", row + 1U);
        else {
            char *text = original_text(f, row);
            append(&b, ",'%s')", text); free(text);
        }
    }
    return b;
}

static void equal_bytes(const char *actual, const char *expected)
{
    size_t a, e, i;
    CHECK(actual != NULL && expected != NULL);
    a = strlen(actual); e = strlen(expected);
    if (a != e || memcmp(actual, expected, a) != 0) {
        for (i = 0U; i < a && i < e && actual[i] == expected[i]; i++) {}
        fprintf(stderr, "byte mismatch offset=%zu actual_length=%zu expected_length=%zu\n", i, a, e);
        CHECK(0);
    }
}

/* Volatile stores cannot disappear as a dead write immediately before free. */
static void poison(void *pointer, size_t size)
{
    volatile unsigned char *p = pointer;
    while (size--) *p++ = 0xa5U;
}

static void record_bytes(const void *data, size_t size)
{
    uint64_t length = (uint64_t)size;
    CHECK(fwrite(&length, sizeof(length), 1U, stdout) == 1U);
    CHECK(size == 0U || fwrite(data, 1U, size, stdout) == size);
}

static void check_initial_admission(const fixture *f)
{
#if !defined(SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS)
    (void)f;
#elif defined(SQLPARSER_SIMPLE_INSERT_BASELINE)
    (void)f;
    CHECK(grammar[PARSE] == 1U);
#else
    CHECK(grammar[PARSE] == (f->fast ? 0U : 1U));
#endif
}

static void check_names(sqlparser_handle_t *handle, const fixture *f)
{
    sqlparser_relation_view_t relation;
    const char *name;
    size_t count;
    CHECK(sqlparser_statement_count(handle) == 1U);
    CHECK(sqlparser_statement_target_relation(handle, 0U, &relation, &error) == SQLPARSER_STATUS_OK);
    CHECK(relation.table_name != NULL && strcmp(relation.table_name, f->table) == 0);
    CHECK(sqlparser_insert_column_count(handle, 0U, &count, &error) == SQLPARSER_STATUS_OK && count == 2U);
    CHECK(sqlparser_insert_row_count(handle, 0U, &count, &error) == SQLPARSER_STATUS_OK && count == f->rows);
    CHECK(sqlparser_insert_column_name(handle, 0U, 0U, &name, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(name, f->first) == 0);
    CHECK(sqlparser_insert_column_name(handle, 0U, 1U, &name, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(name, f->second) == 0);
}

typedef struct numeric_expectation {
    const char *name, *source, *floating;
    long long integer;
} numeric_expectation;
static const numeric_expectation numeric_cases[] = {
    {"negative", "-7", NULL, -7},
    {"int32-overflow", "2147483648", "2147483648", 0},
    {"negative-zero", "-0", NULL, 0},
    {"negative-leading-zero", "-0007", NULL, -7},
    {"negative-int32-max", "-2147483647", NULL, -2147483647},
    {"negative-int32-min-float", "-2147483648", "-2147483648", 0},
    {"negative-overflow-zeroes", "-0002147483649", "-0002147483649", 0},
    {"overflow-leading-zeroes", "0002147483648", "0002147483648", 0},
    {"above-uint64", "18446744073709551616", "18446744073709551616", 0},
    {"negative-gap", "- \t\r\n\v\f0002147483648", "-0002147483648", 0},
    {"negative-small-gap", "- \t0007", NULL, -7}
};

static void check_number(const fixture *f, size_t row, sqlparser_literal_view_t value)
{
    if (f->exceptional_number && (row == f->exceptional_row || f->exceptional_row == SIZE_MAX)) {
        size_t i;
        for (i = 0U; i < sizeof(numeric_cases)/sizeof(numeric_cases[0]); ++i) {
            const numeric_expectation *expected = &numeric_cases[i];
            if (strcmp(f->exceptional_number, expected->source) != 0) continue;
            if (expected->floating != NULL) {
                CHECK(value.kind == SQLPARSER_LITERAL_KIND_FLOAT);
                equal_bytes(value.float_value, expected->floating);
            } else CHECK(value.kind == SQLPARSER_LITERAL_KIND_INTEGER &&
                         value.integer_value == expected->integer);
            break;
        }
        CHECK(i < sizeof(numeric_cases)/sizeof(numeric_cases[0]));
    } else CHECK(value.kind == SQLPARSER_LITERAL_KIND_INTEGER && value.integer_value == (long long)(row + 1U));
}

static void release_patches(sqlparser_patch_t *patches,
                            sqlparser_literal_value_t *literals,
                            char (*replacement)[80], size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++) {
        char *selector = (char *)patches[i].selector;
        poison(selector, strlen(selector)); sqlparser_string_free(selector);
    }
    poison(literals, count * sizeof(*literals));
    poison(replacement, count * sizeof(*replacement));
    poison(patches, count * sizeof(*patches));
    free(literals); free(replacement); free(patches);
}

static void pipeline(const fixture *f, enum changes changes, int typed, int free_before)
{
    buffer input = source_text(f, NONE, 0U), expected;
    sqlparser_parse_options_t options;
    sqlparser_handle_t *handle = NULL;
    sqlparser_query_graph_view_t graph;
    sqlparser_graph_dml_t dml;
    sqlparser_patch_t *patches = allocate(f->rows, sizeof(*patches));
    sqlparser_literal_value_t *literals = allocate(f->rows, sizeof(*literals));
    char (*replacement)[80] = allocate(f->rows, sizeof(*replacement));
    sqlparser_patch_list_t list;
    char *output = NULL, *second_output = NULL;
    size_t padding = 0U, ordinal, count = 0U, input_length, pipeline_calls;

    case_name = f->name; stage = "fixture";
    if (f->exact_length) {
        CHECK(input.length <= f->exact_length);
        padding = f->exact_length - input.length;
        free(input.data); input = source_text(f, NONE, padding);
        CHECK(input.length == f->exact_length);
    }
    expected = source_text(f, changes, padding); input_length = input.length;
    memset(grammar, 0, sizeof(grammar)); memset(&error, 0, sizeof(error));
    sqlparser_parse_options_default(&options); options.dialect = SQLPARSER_DIALECT_MYSQL;
    /* The bounded 5,000 x 1,024-byte case is about 5 MiB, above the public
     * default 4 MiB caps. Only this fixture needs larger public limits. */
    if (input.length > options.limits.max_sql_bytes) options.limits.max_sql_bytes = input.length;
    if (input.length > options.limits.max_output_bytes) options.limits.max_output_bytes = input.length;
    phase = PARSE; stage = "initial public parse";
    CHECK(sqlparser_parse_with_options(input.data, &options, &handle, &error) == SQLPARSER_STATUS_OK);
    CHECK(handle != NULL); check_initial_admission(f);
    /* Caller source lifetime ends before any graph/selector work. */
    poison(input.data, input.length + 1U); free(input.data);
    phase = GRAPH; stage = "public graph";
    CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
    CHECK(dml.rows.count == f->rows * 2U && dml.target_columns.count == 2U);
#ifdef SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS
    CHECK(grammar[GRAPH] == 0U);
#endif
    phase = CONSTRUCT; stage = "actual graph selectors and patch construction";
    for (ordinal = 0U; ordinal < dml.rows.count; ordinal++) {
        sqlparser_graph_dml_cell_t cell;
        size_t index, row = ordinal / 2U;
        char selector_expected[96], *selector = NULL;
        CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, ordinal, &index, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error) == SQLPARSER_STATUS_OK);
        CHECK(cell.row_index == row && cell.column_ordinal == ordinal % 2U);
        CHECK(cell.has_selector && cell.selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL);
        CHECK(cell.selector.statement_index == 0U && cell.selector.row_index == row);
        CHECK(cell.selector.column_index == ordinal % 2U);
        CHECK(cell.kind == SQLPARSER_GRAPH_VALUE_LITERAL);
        if (cell.column_ordinal == 0U) { check_number(f, row, cell.literal); continue; }
        {
            char *text = original_text(f, row);
            CHECK(cell.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
            equal_bytes(cell.literal.string_value, text); free(text);
        }
        if (!selected(f, changes, row)) continue;
        CHECK(sqlparser_selector_format(&cell.selector, &selector, &error) == SQLPARSER_STATUS_OK);
        patches[count].selector = selector;
        CHECK(snprintf(selector_expected, sizeof(selector_expected),
            "stmt[0].insert_cell[%zu][1]", row) > 0);
        equal_bytes(patches[count].selector, selector_expected);
        patches[count].op = SQLPARSER_PATCH_REPLACE;
        if (typed) {
            CHECK(snprintf(replacement[count], sizeof(replacement[count]),
                "masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567", row + 1U) > 0);
            literals[count].kind = SQLPARSER_LITERAL_KIND_STRING;
            literals[count].string_value = replacement[count]; patches[count].literal = &literals[count];
        } else {
            CHECK(snprintf(replacement[count], sizeof(replacement[count]),
                "'masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567'", row + 1U) > 0);
            patches[count].sql = replacement[count];
        }
        count++;
    }
    CHECK(count == (changes == ALL ? f->rows : changes == FEW ? 3U : 0U));
#ifdef SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS
    CHECK(grammar[CONSTRUCT] == 0U);
#endif
    list.items = count ? patches : NULL; list.count = count;
    phase = APPLY; stage = "single apply";
    CHECK(sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK);
    /* after matches timed adjacency: no intervening parser/graph/metadata
     * API call or caller-buffer cleanup. before is a separate lifetime test. */
    if (free_before) release_patches(patches, literals, replacement, count);
    phase = DEPARSE; stage = "adjacent deparse";
    CHECK(sqlparser_deparse(handle, &output, &error) == SQLPARSER_STATUS_OK);
    if (!free_before) release_patches(patches, literals, replacement, count);
    equal_bytes(output, expected.data);
    pipeline_calls = grammar[PARSE] + grammar[GRAPH] + grammar[CONSTRUCT] + grammar[APPLY] + grammar[DEPARSE];
#ifdef SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS
    if (f->primary) {
        /* The optimized path bypasses grammar for this pipeline. The generic
         * reference makes one initial grammar call, without extra patch parses. */
        CHECK(grammar[APPLY] == 0U && grammar[DEPARSE] == 0U);
        CHECK(pipeline_calls == grammar[PARSE]);
    }
#endif
    phase = VERIFY; stage = "post-caller-lifetime graph and second deparse";
    check_names(handle, f);
    CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
    CHECK(dml.rows.count == f->rows * 2U);
    for (ordinal = 0U; ordinal < f->rows; ordinal++) {
        sqlparser_literal_view_t value;
        char expected_replacement[80], *original = NULL;
        const char *wanted;
        CHECK(sqlparser_insert_cell_literal(handle, 0U, ordinal, 1U, &value, &error) == SQLPARSER_STATUS_OK);
        CHECK(value.kind == SQLPARSER_LITERAL_KIND_STRING);
        if (selected(f, changes, ordinal)) {
            CHECK(snprintf(expected_replacement, sizeof(expected_replacement),
                "masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567", ordinal + 1U) > 0);
            wanted = expected_replacement;
        } else { original = original_text(f, ordinal); wanted = original; }
        equal_bytes(value.string_value, wanted); free(original);
        CHECK(sqlparser_insert_cell_literal(handle, 0U, ordinal, 0U, &value, &error) == SQLPARSER_STATUS_OK);
        check_number(f, ordinal, value);
    }
    CHECK(sqlparser_deparse(handle, &second_output, &error) == SQLPARSER_STATUS_OK);
    equal_bytes(second_output, expected.data); sqlparser_handle_destroy(handle);
    stage = "caller output survives handle destruction";
    equal_bytes(output, expected.data); equal_bytes(second_output, expected.data);
    if (record) {
        uint64_t fields[] = {f->rows, count, (uint64_t)typed, (uint64_t)free_before, input_length};
        record_bytes(f->name, strlen(f->name)); record_bytes(fields, sizeof(fields));
        record_bytes(output, strlen(output)); record_bytes(second_output, strlen(second_output));
    } else {
        printf("pipeline case=%s rows=%zu bytes=%zu patches=%zu mode=%s free=%s ",
            f->name, f->rows, input_length, count, typed ? "typed" : "raw", free_before ? "before" : "after");
#ifdef SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS
        printf("grammar=parse:%zu,graph:%zu,construct:%zu,apply:%zu,deparse:%zu,total:%zu,verify:%zu ",
            grammar[PARSE], grammar[GRAPH], grammar[CONSTRUCT], grammar[APPLY], grammar[DEPARSE], pipeline_calls, grammar[VERIFY]);
#else
        (void)pipeline_calls;
        printf("grammar=uninstrumented ");
#endif
        puts("exact=ok lifetime=ok");
    }
    sqlparser_string_free(output); sqlparser_string_free(second_output); free(expected.data); cases++;
}

static void run(const fixture *f, enum changes changes, int typed, int before)
{
    if (filter_case && strcmp(filter_case, f->name) != 0) return;
    if (filter_typed >= 0 && filter_typed != typed) return;
    if (filter_changes >= 0 && filter_changes != (int)changes) return;
    if (filter_before >= 0 && filter_before != before) return;
    pipeline(f, changes, typed, before);
}

/* Malformed inputs are diagnostics only: never graph/apply/deparse successes. */
static void invalid_parse_only(size_t bad_row)
{
    fixture f = {"invalid5000-middle", "t", "id", "text_col", 5000U, 0U, 0U, 0U, NULL, 0, 0};
    sqlparser_parse_options_t options;
    sqlparser_handle_t *handle = NULL;
    buffer input;
    sqlparser_status_t status;
    if (bad_row == 4999U) f.name = "invalid5000-last";
    if (filter_case && strcmp(filter_case, f.name) != 0) return;
    if (filter_typed >= 0 || filter_changes >= 0 || filter_before >= 0) return;
    f.exceptional_row = bad_row; f.exceptional_number = ")";
    input = source_text(&f, NONE, 0U); case_name = f.name; stage = "invalid parse only";
    memset(grammar, 0, sizeof(grammar)); memset(&error, 0, sizeof(error));
    sqlparser_parse_options_default(&options); options.dialect = SQLPARSER_DIALECT_MYSQL;
    phase = PARSE;
    status = sqlparser_parse_with_options(input.data, &options, &handle, &error);
    CHECK(status == SQLPARSER_STATUS_PARSE_ERROR && handle == NULL);
#ifdef SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS
    CHECK(grammar[PARSE] == 1U);
#endif
    CHECK(error.code == SQLPARSER_STATUS_PARSE_ERROR && error.message[0] != '\0');
    CHECK(error.cursor > 0 && (size_t)error.cursor <= input.length + 1U);
    if (record) {
        int fields[] = {(int)status, (int)error.code, error.cursor, error.line, error.column};
        uint64_t row = bad_row;
        record_bytes(f.name, strlen(f.name)); record_bytes(&row, sizeof(row));
        record_bytes(fields, sizeof(fields)); record_bytes(error.message, strlen(error.message));
    } else {
        printf("invalid-parse-only case=%s row=%zu ", f.name, bad_row);
#ifdef SQLPARSER_SIMPLE_INSERT_PIPELINE_WRAPPERS
        printf("grammar=%zu ", grammar[PARSE]);
#else
        printf("grammar=uninstrumented ");
#endif
        printf("code=%d cursor=%d line=%d column=%d message=%s\n",
            (int)status, error.cursor, error.line, error.column, error.message);
    }
    poison(input.data, input.length + 1U); free(input.data); cases++;
}

int main(int argc, char **argv)
{
    fixture f = {"primary5000", "t", "id", "text_col", 5000U, 0U, 0U, 0U, NULL, 1, 1};
    char long_table[193], long_first[162], long_second[194], name[80];
    size_t rows, bytes, i;
    int typed, before, arg;
    for (arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "--record") == 0) record = 1;
        else if (strncmp(argv[arg], "--case=", 7U) == 0) filter_case = argv[arg] + 7U;
        else if (strcmp(argv[arg], "--mode=raw") == 0) filter_typed = 0;
        else if (strcmp(argv[arg], "--mode=typed") == 0) filter_typed = 1;
        else if (strcmp(argv[arg], "--patches=all") == 0) filter_changes = ALL;
        else if (strcmp(argv[arg], "--patches=few") == 0) filter_changes = FEW;
        else if (strcmp(argv[arg], "--patches=none") == 0) filter_changes = NONE;
        else if (strcmp(argv[arg], "--free=before") == 0) filter_before = 1;
        else if (strcmp(argv[arg], "--free=after") == 0) filter_before = 0;
        else {
            fprintf(stderr, "usage: %s [--record] [--case=NAME] [--mode=raw|typed] [--patches=all|few|none] [--free=before|after]\n", argv[0]);
            return 2;
        }
    }
    if (!record) setvbuf(stdout, NULL, _IOLBF, 0U);
    for (typed = 0; typed <= 1; typed++) {
        for (before = 0; before <= 1; before++) run(&f, ALL, typed, before);
        run(&f, NONE, typed, 0); run(&f, FEW, typed, 0);
    }
    f.primary = 0;
    for (rows = 31U; rows <= 32U; rows++) for (bytes = 4095U; bytes <= 4096U; bytes++) {
        CHECK(snprintf(name, sizeof(name), "threshold-%zu-rows-%zu-bytes", rows, bytes) > 0);
        f.name = name; f.rows = rows; f.exact_length = bytes; f.fast = rows >= 32U && bytes >= 4096U;
        for (typed = 0; typed <= 1; typed++) run(&f, ALL, typed, 0);
    }
    f.exact_length = 0U; f.rows = 5000U; f.fast = 1;
    for (i = 0U; i < 3U * sizeof(numeric_cases)/sizeof(numeric_cases[0]); i++) {
        const numeric_expectation *number = &numeric_cases[i / 3U];
        CHECK(snprintf(name, sizeof(name), "valid-%s-%s", number->name,
            i % 3U == 0U ? "first" : i % 3U == 1U ? "middle" : "last") > 0);
        f.name = name;
        f.exceptional_row = i % 3U == 0U ? 0U : i % 3U == 1U ? f.rows / 2U : f.rows - 1U;
        f.exceptional_number = number->source;
        for (typed = 0; typed <= 1; typed++) for (before = 0; before <= 1; before++)
            run(&f, ALL, typed, before);
    }
    for (i = 0U; i < 3U; ++i) {
        static const char *all_names[] = {"all-negative5000", "all-wide5000", "all-negative-wide5000"};
        static const char *all_numbers[] = {"-7", "2147483648", "-2147483648"};
        f.name = all_names[i]; f.exceptional_number = all_numbers[i]; f.exceptional_row = SIZE_MAX;
        for (typed = 0; typed <= 1; ++typed) for (before = 0; before <= 1; ++before)
            run(&f, ALL, typed, before);
    }
    f.exceptional_number = NULL; f.fast = 1; f.rows = 32U;
    for (i = 0U; i < 3U; i++) {
        f.long_string = i == 0U ? 1023U : i == 1U ? 1024U : 8193U;
        CHECK(snprintf(name, sizeof(name), "long-strings-%zu", f.long_string) > 0); f.name = name;
        for (typed = 0; typed <= 1; typed++) run(&f, ALL, typed, 1);
    }
    f.rows = 5000U; f.long_string = 1024U; f.name = "long-strings5000-1024";
    for (typed = 0; typed <= 1; typed++) run(&f, ALL, typed, 0);
    f.long_string = 0U; f.rows = 5000U; f.name = "mixed-case-identifiers";
    f.table = "test_table"; f.first = "ID"; f.second = "SECRET_VALUE";
    for (typed = 0; typed <= 1; typed++) run(&f, ALL, typed, 0);
    memset(long_table, 'T', sizeof(long_table) - 1U); long_table[sizeof(long_table) - 1U] = '\0';
    memset(long_first, 'a', sizeof(long_first) - 1U); long_first[sizeof(long_first) - 1U] = '\0';
    memset(long_second, 'B', sizeof(long_second) - 1U); long_second[sizeof(long_second) - 1U] = '\0';
    f.name = "long-identifiers"; f.table = long_table; f.first = long_first; f.second = long_second;
    f.rows = 32U; f.exact_length = 4096U;
    for (typed = 0; typed <= 1; typed++) run(&f, ALL, typed, 1);
    f.name = "long-identifiers5000"; f.rows = 5000U; f.exact_length = 0U;
    for (typed = 0; typed <= 1; typed++) run(&f, ALL, typed, 0);
    invalid_parse_only(2500U); invalid_parse_only(4999U);
    if (!cases) { fprintf(stderr, "filters selected no cases\n"); return 2; }
    if (!record) printf("public INSERT pipeline proof: %zu cases passed; invalid diagnostics are separate\n", cases);
    return 0;
}
