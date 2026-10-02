/* Single-thread public API pipeline benchmark. See bench/README.en.md. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlparser/sqlparser.h"

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}
static void check(sqlparser_status_t status, const sqlparser_error_t *error, const char *stage) {
    if (status != SQLPARSER_STATUS_OK) {
        fprintf(stderr, "%s: %d %s\n", stage, status, error->message);
        exit(1);
    }
}
static void *allocate(size_t count, size_t size) {
    void *p = calloc(count, size);
    if (!p) { perror("calloc"); exit(1); }
    return p;
}
int main(int argc, char **argv) {
    size_t rows = argc > 1 ? strtoul(argv[1], NULL, 10) : 5000;
    int repetitions = argc > 2 ? atoi(argv[2]) : 31;
    int warmups = argc > 3 ? atoi(argv[3]) : 5;
    const char *mode = argc > 4 ? argv[4] : "replace";
    int unchanged = strcmp(mode, "unchanged") == 0;
    int noop = strcmp(mode, "noop") == 0;
    int invalid = strcmp(mode, "invalid") == 0;
    int typed = strcmp(mode, "literal") == 0;
    const char *dialect_name = argc > 5 ? argv[5] : "mysql";
    sqlparser_parse_options_t options;
    sqlparser_parse_options_default(&options);
    if (strcmp(dialect_name, "mysql") == 0) options.dialect = SQLPARSER_DIALECT_MYSQL;
    else if (strcmp(dialect_name, "postgresql") == 0) options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
    else return 2;
    char *input, *expected, *p, *q;
    size_t row;
    int run;
    if (!rows || rows > 100000 || repetitions < 1 || warmups < 0 ||
        (!unchanged && !noop && !invalid && !typed && strcmp(mode, "replace") != 0)) return 2;
    input = allocate(rows * 80 + 128, 1);
    expected = allocate(rows * 80 + 128, 1);
    p = input + sprintf(input, "INSERT INTO t(id, text_col) VALUES ");
    q = expected + sprintf(expected, "INSERT INTO t(id, text_col) VALUES ");
    for (row = 1; row <= rows; row++) {
        p += sprintf(p, "%s(%zu,'small-secret-%04zu')", row == 1 ? "" : ",", row, row);
        q += sprintf(q, "%s(%zu,'masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567')", row == 1 ? "" : ",", row, row);
    }
    if (unchanged || noop || invalid) strcpy(expected, input);
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("dialect,rows,mode,run,parse_ms,graph_ms,construct_ms,apply_ms,deparse_ms,total_ms,cleanup_ms\n");
    for (run = -warmups; run < repetitions; run++) {
        sqlparser_handle_t *handle = NULL;
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_dml_t dml;
        sqlparser_error_t error;
        sqlparser_patch_t *patches;
        sqlparser_patch_list_t list;
        char (*replacements)[80];
        sqlparser_literal_value_t *literals;
        char *output = NULL;
        double t[7], end;
        size_t count = 0, index;
        memset(&error, 0, sizeof(error));
        t[0] = now_ms();
        check(sqlparser_parse_with_options(input, &options, &handle, &error), &error, "parse");
        t[1] = now_ms();
        check(sqlparser_statement_query_graph(handle, 0, &graph, &error), &error, "graph");
        t[2] = now_ms();
        check(sqlparser_query_graph_dml(&graph, &dml, &error), &error, "dml");
        if (dml.rows.count != rows * 2) { fprintf(stderr, "wrong graph size\n"); return 1; }
        patches = allocate(rows, sizeof(*patches));
        replacements = allocate(rows, sizeof(*replacements));
        literals = allocate(rows, sizeof(*literals));
        for (index = 0; index < dml.rows.count; index++) {
            sqlparser_graph_dml_cell_t cell;
            size_t cell_index;
            char *selector = NULL;
            check(sqlparser_query_graph_span_index_at(&graph, dml.rows, index, &cell_index, &error), &error, "cell index");
            check(sqlparser_query_graph_dml_cell_at(&graph, cell_index, &cell, &error), &error, "cell");
            if (cell.column_ordinal != 1) continue;
            if (!cell.has_selector || cell.row_index != count) { fprintf(stderr, "wrong selector\n"); return 1; }
            check(sqlparser_selector_format(&cell.selector, &selector, &error), &error, "selector format");
            patches[count].op = SQLPARSER_PATCH_REPLACE;
            patches[count].selector = selector;
            if (noop) sprintf(replacements[count], "'small-secret-%04zu'", count + 1);
            else sprintf(replacements[count], "'masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567'", count + 1);
            patches[count].sql = replacements[count];
            if (typed) {
                size_t n = strlen(replacements[count]);
                replacements[count][n - 1] = '\0';
                literals[count].kind = SQLPARSER_LITERAL_KIND_STRING;
                literals[count].string_value = replacements[count] + 1;
                patches[count].literal = &literals[count];
                patches[count].sql = NULL;
            }
            count++;
        }
        if (count != rows) return 1;
        if (invalid) patches[rows - 1].sql = "'unterminated";
        list.items = patches;
        list.count = unchanged ? 0 : rows;
        t[3] = now_ms();
        if (invalid) {
            if (sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK) {
                fprintf(stderr, "invalid patch accepted\n"); return 1;
            }
        } else check(sqlparser_apply_patch(handle, &list, &error), &error, "apply");
        t[4] = now_ms();
        /* A failed apply is terminal. Negative diagnostics stop here and
         * destroy the handle; only successful pipelines include deparse. */
        if (!invalid) check(sqlparser_deparse(handle, &output, &error), &error, "deparse");
        t[5] = now_ms();
        if (invalid ? strcmp(input, expected) != 0 : (!output || strcmp(output, expected))) {
            fprintf(stderr, "output/input mismatch\n"); return 1;
        }
        /* Correctness verification and release are intentionally outside total. */
        t[6] = now_ms();
        for (index = 0; index < rows; index++) sqlparser_string_free((char *)patches[index].selector);
        free(literals);
        free(replacements);
        free(patches);
        sqlparser_string_free(output);
        sqlparser_handle_destroy(handle);
        end = now_ms();
        if (run >= 0) printf("%s,%zu,%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", dialect_name, rows, mode, run,
            t[1]-t[0], t[2]-t[1], t[3]-t[2], t[4]-t[3], t[5]-t[4], t[5]-t[0], end-t[6]);
    }
    free(input);
    free(expected);
    return 0;
}
