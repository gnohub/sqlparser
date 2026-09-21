#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jansson.h>
#include "sqlparser_internal.h"

static int check_case(sqlparser_dialect_t dialect, const char *sql,
	const sqlparser_patch_t *items, size_t count, const char *expected)
{
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t list;
	sqlparser_error_t error;
	sqlparser_handle_t *handle = NULL;
	sqlparser_handle_t *reference = NULL;
	char *output = NULL;
	char *view = NULL;
	char *reference_view = NULL;
	json_t *actual_json = NULL;
	json_t *expected_json = NULL;
	unsigned long generation;
	int result = 1;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	list.items = items;
	list.count = count;
	if (sqlparser_parse_with_options(sql, &options, &handle, &error) != SQLPARSER_STATUS_OK)
		goto done;
	generation = handle->generation;
	if (sqlparser_apply_patch(handle, &list, &error) != SQLPARSER_STATUS_OK ||
	    handle->generation != generation + (strcmp(sql, expected) != 0 ? 1UL : 0UL) ||
	    sqlparser_deparse(handle, &output, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_export_view_json(handle, 0, &view, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_parse_with_options(expected, &options, &reference, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_export_view_json(reference, 0, &reference_view, &error) != SQLPARSER_STATUS_OK)
		goto done;
	actual_json = json_loads(view, 0, NULL);
	expected_json = json_loads(reference_view, 0, NULL);
	if (actual_json == NULL || expected_json == NULL || !json_equal(actual_json, expected_json))
		goto done;
	if (strstr(expected, "/*batch-head*/") != NULL && strstr(output, "/*batch-head*/") == NULL)
		goto done;
	if (strstr(expected, "/*batch-tail*/") != NULL && strstr(output, "/*batch-tail*/") == NULL)
		goto done;
	json_decref(expected_json);
	expected_json = NULL;
	sqlparser_handle_destroy(reference);
	reference = NULL;
	sqlparser_string_free(reference_view);
	reference_view = NULL;
	if (sqlparser_parse_with_options(output, &options, &reference, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_export_view_json(reference, 0, &reference_view, &error) != SQLPARSER_STATUS_OK)
		goto done;
	expected_json = json_loads(reference_view, 0, NULL);
	if (expected_json == NULL || !json_equal(actual_json, expected_json))
		goto done;
	result = 0;
done:
	if (result != 0)
		fprintf(stderr, "batch case failed dialect=%d sql=%s output=%s error=%s\n",
			(int)dialect, sql, output != NULL ? output : "", error.message);
	json_decref(actual_json);
	json_decref(expected_json);
	sqlparser_string_free(view);
	sqlparser_string_free(reference_view);
	sqlparser_string_free(output);
	sqlparser_handle_destroy(reference);
	sqlparser_handle_destroy(handle);
	return result;
}

static int check_rollback_limits(sqlparser_dialect_t dialect, const char *sql,
	const sqlparser_patch_t *items, size_t count, size_t limit)
{
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t list = {items, count};
	sqlparser_error_t error;
	sqlparser_handle_t *handle = NULL;
	char *output = NULL;
	char *before = NULL;
	char *after = NULL;
	unsigned long generation;
	int result = 1;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	if (limit != 0U) {
		options.limits.max_sql_bytes = limit;
	}
	if (sqlparser_parse_with_options(sql, &options, &handle, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_export_view_json(handle, 0, &before, &error) != SQLPARSER_STATUS_OK)
		goto done;
	generation = handle->generation;
	if (sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK ||
	    handle->generation != generation ||
	    sqlparser_deparse(handle, &output, &error) != SQLPARSER_STATUS_OK ||
	    strcmp(output, sql) != 0 ||
	    sqlparser_export_view_json(handle, 0, &after, &error) != SQLPARSER_STATUS_OK ||
	    strcmp(before, after) != 0)
		goto done;
	result = 0;
done:
	if (result != 0)
		fprintf(stderr, "batch rollback failed dialect=%d error=%s\n", (int)dialect, error.message);
	sqlparser_string_free(output);
	sqlparser_string_free(before);
	sqlparser_string_free(after);
	sqlparser_handle_destroy(handle);
	return result;
}

static int check_rollback(sqlparser_dialect_t dialect, const char *sql,
	const sqlparser_patch_t *items, size_t count)
{
	return check_rollback_limits(dialect, sql, items, count, 0U);
}

static int check_resource_limits(void)
{
	char sql[1024];
	char original[181];
	char large[301];
	sqlparser_literal_value_t large_literal = {SQLPARSER_LITERAL_KIND_STRING, large, NULL, 0, 0};
	sqlparser_literal_value_t small_literal = {SQLPARSER_LITERAL_KIND_STRING, "s", NULL, 0, 0};
	sqlparser_patch_t items[3];

	memset(original, 'o', 180U); original[180] = 0;
	memset(large, 'x', 300U); large[300] = 0;
	snprintf(sql, sizeof(sql), "SELECT a FROM t WHERE a LIKE CONCAT('%s','%s')", original, original);
	items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][0]", .literal=&large_literal};
	items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][1]", .literal=&small_literal};
	if (check_rollback_limits(SQLPARSER_DIALECT_POSTGRESQL, sql, items, 2U, 500U)) return 1;
	snprintf(sql, sizeof(sql), "INSERT ALL INTO t(a,b,c) VALUES('orig','%s','%s') SELECT 1 FROM dual", original, original);
	items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_branch_columns[0]", .index=3U, .name="backup", .source_selector="stmt[0].insert_cell[0][0]"};
	items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .literal=&large_literal};
	items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .literal=&small_literal};
	return check_rollback_limits(SQLPARSER_DIALECT_ORACLE, sql, items, 3U, 500U);
}

static int check_semantics(void)
{
	sqlparser_patch_t items[9];
	sqlparser_literal_value_t ten = {SQLPARSER_LITERAL_KIND_INTEGER, NULL, NULL, 10, 0};
	sqlparser_literal_value_t twenty = {SQLPARSER_LITERAL_KIND_INTEGER, NULL, NULL, 20, 0};
	sqlparser_literal_value_t one = {SQLPARSER_LITERAL_KIND_INTEGER, NULL, NULL, 1, 0};
	sqlparser_literal_value_t x = {SQLPARSER_LITERAL_KIND_STRING, "x", NULL, 0, 0};
	sqlparser_literal_value_t y = {SQLPARSER_LITERAL_KIND_STRING, "y", NULL, 0, 0};
	const char *sql;
	char bind_sql[256];
	char bind_expected[256];
	int d;

	for (d = SQLPARSER_DIALECT_POSTGRESQL; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++) {
		sqlparser_dialect_t dialect = (sqlparser_dialect_t)d;
		memset(items, 0, sizeof(items));
		sql = "UPDATE t SET a = 1, b = 2 WHERE id = 3";
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[0]", .literal=&ten};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ASSIGNMENT, .selector="stmt[0].assignment[2]", .name="a_copy", .source_selector="stmt[0].assignment[0]"};
		items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[0]", .literal=&twenty};
		items[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ASSIGNMENT, .selector="stmt[0].assignment[1]"};
		items[4] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ASSIGNMENT, .selector="stmt[0].assignment[2]", .name="second_copy", .source_selector="stmt[0].assignment[1]"};
		if (check_case(dialect, sql, items, 5U,
		    "UPDATE t SET a = 20, a_copy = 10, second_copy = 10 WHERE id = 3")) return 1;
		items[5] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[999]", .literal=&ten};
		if (check_rollback(dialect, sql, items, 6U)) return 1;
		items[2] = items[5];
		if (check_rollback(dialect, sql, items, 5U)) return 1;
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[0]", .literal=&one};
		if (check_case(dialect, sql, items, 2U, sql)) return 1;
		{
			const char *a, *b, *id;
			if (dialect == SQLPARSER_DIALECT_POSTGRESQL || dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL || dialect == SQLPARSER_DIALECT_KINGBASE_POSTGRESQL) {
				a = "$1"; b = "$2"; id = "$3";
			} else if (dialect == SQLPARSER_DIALECT_MYSQL || dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL || dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL) {
				a = "?"; b = "?"; id = "?";
			} else if (dialect == SQLPARSER_DIALECT_SQLSERVER || dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER || dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER) {
				a = "@a"; b = "@b"; id = "@id";
			} else {
				a = ":a"; b = ":b"; id = ":id";
			}
			snprintf(bind_sql, sizeof(bind_sql), "UPDATE t SET a = %s, b = %s WHERE id = %s", a, b, id);
			snprintf(bind_expected, sizeof(bind_expected), "UPDATE t SET a = 10, copied = %s WHERE id = %s", b, id);
		}
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ASSIGNMENT, .selector="stmt[0].assignment[2]", .name="copied", .source_selector="stmt[0].assignment[1]"};
		items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ASSIGNMENT, .selector="stmt[0].assignment[1]"};
		if (check_case(dialect, bind_sql, items, 3U, bind_expected)) return 1;

		memset(items, 0, sizeof(items));
		sql = "SELECT a, b FROM t WHERE name LIKE CONCAT('a', 'b')";
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][0]", .literal=&x};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][1]", .literal=&y};
		items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][0]", .literal=&y};
		items[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ARGUMENT, .selector="stmt[0].expression_args[0]", .index=1U, .literal=&x};
		items[4] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ARGUMENT, .selector="stmt[0].expression_args[0]", .index=2U};
		items[5] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].select_target[0][0]", .sql="c"};
		if (check_case(dialect, sql, items, 6U,
		    "SELECT c, b FROM t WHERE name LIKE CONCAT('y', 'x')")) return 1;
		items[6] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][99]", .literal=&x};
		if (check_rollback(dialect, sql, items, 7U)) return 1;
		items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][1]", .sql="LOWER('z')"};
		items[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[1][0]", .literal=&x};
		if (check_case(dialect, sql, items, 4U,
		    "SELECT a, b FROM t WHERE name LIKE CONCAT('x', LOWER('x'))")) return 1;
		items[2].sql = "(";
		if (check_rollback(dialect, sql, items, 3U)) return 1;
		sql = "/*batch-head*/ SELECT a FROM t WHERE name LIKE CONCAT('a', 'b') /*batch-tail*/";
		if (check_case(dialect, sql, items, 2U,
		    "/*batch-head*/ SELECT a FROM t WHERE name LIKE CONCAT('x', 'y') /*batch-tail*/")) return 1;

		sql = "UPDATE t SET a = 1; UPDATE u SET b = 2";
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[0]", .literal=&ten};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[1].assignment[0]", .literal=&twenty};
		items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ASSIGNMENT, .selector="stmt[1].assignment[1]", .name="copied", .source_selector="stmt[1].assignment[0]"};
		items[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[0]", .literal=&twenty};
		if (check_case(dialect, sql, items, 4U,
		    "UPDATE t SET a = 20; UPDATE u SET b = 20, copied = 20")) return 1;
		items[2].source_selector = "stmt[0].assignment[0]";
		if (check_rollback(dialect, sql, items, 4U)) return 1;

		sql = "INSERT INTO t (a, b) VALUES (1, 2)";
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&ten};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_columns", .index=2U, .name="a_copy", .source_selector="stmt[0].insert_cell[0][0]"};
		items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&twenty};
		if (check_case(dialect, sql, items, 3U, "INSERT INTO t (a, b, a_copy) VALUES (20, 2, 10)")) return 1;
		items[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_columns", .index=3U, .name="missing_value"};
		if (check_rollback(dialect, sql, items, 4U)) return 1;
		if (dialect != SQLPARSER_DIALECT_MYSQL && dialect != SQLPARSER_DIALECT_VASTBASE_MYSQL &&
		    dialect != SQLPARSER_DIALECT_KINGBASE_MYSQL) {
			sql = "MERGE INTO t USING s ON (t.id = s.id) WHEN MATCHED THEN UPDATE SET a = 1, b = 2 WHEN NOT MATCHED THEN INSERT(id,a) VALUES(s.id,3)";
			items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].merge_assignment[0][0]", .literal=&ten};
			items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].merge_assignment[0][1]", .literal=&twenty};
			items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].merge_insert_cell[1][1]", .literal=&ten};
			if (check_case(dialect, sql, items, 3U,
			    "MERGE INTO t USING s ON (t.id = s.id) WHEN MATCHED THEN UPDATE SET a = 10, b = 20 WHEN NOT MATCHED THEN INSERT(id,a) VALUES(s.id,10)")) return 1;
		}

		if (dialect == SQLPARSER_DIALECT_ORACLE || dialect == SQLPARSER_DIALECT_DAMENG ||
		    dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE || dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE) {
			sql = "INSERT ALL INTO t(a,b) VALUES(1,2) INTO t(a,b) VALUES(3,4) SELECT 1 FROM dual";
			items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_branch_columns[0]", .index=2U, .name="a_copy", .source_selector="stmt[0].insert_cell[0][0]"};
			items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&ten};
			items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_branch_columns[1]", .index=0U, .name="other_copy", .source_selector="stmt[0].insert_cell[0][0]"};
			items[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[1][1]", .literal=&twenty};
			if (check_case(dialect, sql, items, 4U,
			    "INSERT ALL INTO t(a,b,a_copy) VALUES(10,2,1) INTO t(other_copy,a,b) VALUES(10,20,4) SELECT 1 FROM dual")) return 1;
			items[4] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[90][0]", .literal=&ten};
			if (check_rollback(dialect, sql, items, 5U)) return 1;
			items[4] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].select_target[0][0]", .sql="99"};
			items[5] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_branch_columns[0]", .index=3U, .name="source_copy", .source_selector="stmt[0].select_target[0][0]"};
			if (check_case(dialect, sql, items, 6U,
			    "INSERT ALL INTO t(a,b,a_copy,source_copy) VALUES(10,2,1,99) INTO t(other_copy,a,b) VALUES(10,20,4) SELECT 99 FROM dual")) return 1;
			items[6] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_branch_columns[0]", .index=4U, .name="without_value"};
			if (check_rollback(dialect, sql, items, 7U)) return 1;
			items[6] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="("};
			if (check_rollback(dialect, sql, items, 7U)) return 1;
			items[7] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&ten};
			if (check_rollback(dialect, sql, items, 8U)) return 1;
		}
	}
	return 0;
}

typedef struct { char *text; size_t length; size_t capacity; } buffer_t;

static void append(buffer_t *buffer, const char *format, ...)
{
	va_list args;
	int count;
	va_start(args, format);
	count = vsnprintf(buffer->text + buffer->length, buffer->capacity - buffer->length, format, args);
	va_end(args);
	if (count < 0 || (size_t)count >= buffer->capacity - buffer->length) abort();
	buffer->length += (size_t)count;
}

static double now(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (double)time.tv_sec + (double)time.tv_nsec / 1e9;
}

static int benchmark(const char *mode, size_t n, size_t limit)
{
	const char *columns = "FPE_A,FPE_B,CLOB_A,CLOB_B,CLOB_C,ID,AMOUNT,CREATED_AT,NOTE,OPTVAL,REF_CODE,FLAG,CREATED_BY,COUNTER,BATCH_NO,DETAIL";
	const char *names[] = {"FPE_A_BAK","FPE_B_BAK","CLOB_A_BAK","CLOB_B_BAK","CLOB_C_BAK"};
	buffer_t sql;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle = NULL;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_dml_assignment_t assignment;
	sqlparser_graph_expression_t expression_view;
	sqlparser_graph_expression_argument_t argument;
	sqlparser_graph_value_t value;
	sqlparser_literal_value_t *values;
	sqlparser_patch_t *patches;
	sqlparser_patch_list_t list;
	char (*selectors)[80];
	char (*sources)[80];
	char (*original)[5][65];
	char cipher[5][781];
	char note[33], detail[41], reference[21];
	char *output = NULL;
	size_t count, b, j, k, index;
	double parse_start, apply_start, deparse_start, end;
	int multi = strcmp(mode, "oracle") == 0;
	int expression = strcmp(mode, "expression") == 0;
	int copy = strcmp(mode, "update-copy") == 0;
	int result = 1;

	if (n == 0U || n > 500U ||
	    (!multi && !expression && !copy && strcmp(mode, "update") != 0)) return 1;
	count = multi ? n * 10U : (copy ? n * 2U : n);
	if (limit > count || ((multi || copy) && limit != 0U)) return 1;
	patches = calloc(count, sizeof(*patches));
	original = calloc(n, sizeof(*original));
	values = calloc(count, sizeof(*values));
	selectors = calloc(count, sizeof(*selectors));
	sources = calloc(count, sizeof(*sources));
	sql.capacity = 1024U * 1024U;
	sql.length = 0U;
	sql.text = calloc(sql.capacity, 1U);
	if (!patches || !original || !values || !selectors || !sources || !sql.text) goto done;
	sqlparser_parse_options_default(&options);
	options.dialect = multi ? SQLPARSER_DIALECT_ORACLE : SQLPARSER_DIALECT_POSTGRESQL;
	memset(&error, 0, sizeof(error));
	if (multi) {
		memset(note, 'n', 32U); note[32] = 0;
		memset(detail, 'd', 40U); detail[40] = 0;
		memset(reference, 'r', 20U); reference[20] = 0;
		for (j = 0U; j < 5U; j++) {
			size_t c = j < 2U ? 11U : (j == 2U ? 760U : (j == 3U ? 780U : 740U));
			memset(cipher[j], j < 2U ? (int)('7' + j) : (int)('A' + j), c); cipher[j][c] = 0;
		}
		append(&sql, "INSERT ALL ");
		k = 0U;
		for (b = 0U; b < n; b++) {
			for (j = 0U; j < 5U; j++) {
				if (j < 2U) {
					snprintf(original[b][j], 65U, "%011llu", (j+1U)*10000000000ULL+b);
				} else {
					int prefix = snprintf(original[b][j], 65U, "ORIG_C%zu_B%03zu_", j, b);
					memset(original[b][j] + prefix, (int)('a' + j), 64U - (size_t)prefix);
					original[b][j][64] = 0;
				}
			}
			append(&sql, "INTO PERF_T (%s) VALUES ('%s','%s','%s','%s','%s',%zu,123.45,TIMESTAMP '2026-09-21 12:34:56','%s',NULL,'%s','Y','worker',%zu,20260921,'%s') ",
				columns, original[b][0], original[b][1], original[b][2], original[b][3], original[b][4], b+1U, note, reference, b+100U, detail);
			for (j = 0U; j < 5U; j++) {
				snprintf(selectors[k], 80U, "stmt[0].insert_branch_columns[%zu]", b);
				snprintf(sources[k], 80U, "stmt[0].insert_cell[%zu][%zu]", b, j);
				patches[k] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector=selectors[k], .index=16U+j, .name=names[j], .source_selector=sources[k]};
				k++;
				snprintf(selectors[k], 80U, "stmt[0].insert_cell[%zu][%zu]", b, j);
				values[k].kind = SQLPARSER_LITERAL_KIND_STRING;
				values[k].string_value = cipher[j];
				patches[k] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector=selectors[k], .literal=&values[k]};
				k++;
			}
		}
		append(&sql, "SELECT 1 FROM DUAL");
	} else {
		append(&sql, expression ? "SELECT id FROM t WHERE name LIKE CONCAT(" : "UPDATE t SET ");
		for (j = 0U; j < n; j++) {
			if (j) append(&sql, ", ");
			if (expression) append(&sql, "'original'");
			else append(&sql, "c%zu = 'original'", j);
			k = copy ? j * 2U : j;
			if (copy) {
				snprintf(selectors[k], 80U, "stmt[0].assignment[%zu]", n+j);
				snprintf(sources[k], 80U, "stmt[0].assignment[%zu]", j);
				snprintf(sources[k+1U], 80U, "backup_%zu", j);
				patches[k] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ASSIGNMENT, .selector=selectors[k], .name=sources[k+1U], .source_selector=sources[k]};
				k++;
			}
			snprintf(selectors[k], 80U, expression ? "stmt[0].expression_arg[0][%zu]" : "stmt[0].assignment[%zu]", j);
			values[k].kind = SQLPARSER_LITERAL_KIND_STRING;
			values[k].string_value = "encrypted_value_0123456789";
			patches[k] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector=selectors[k], .literal=&values[k]};
		}
		append(&sql, expression ? ")" : " WHERE id = 1");
	}
	list.items = patches;
	list.count = limit != 0U ? limit : count;
	parse_start = now();
	if (sqlparser_parse_with_options(sql.text, &options, &handle, &error) != SQLPARSER_STATUS_OK) goto failed;
	apply_start = now();
	if (sqlparser_apply_patch(handle, &list, &error) != SQLPARSER_STATUS_OK) goto failed;
	deparse_start = now();
	if (sqlparser_deparse(handle, &output, &error) != SQLPARSER_STATUS_OK) goto failed;
	end = now();
	if (multi) {
		if (sqlparser_statement_query_graph(handle, 0U, &graph, &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml(&graph, &dml, &error) != SQLPARSER_STATUS_OK || dml.branches.count != n) goto failed;
		for (b = 0U; b < n; b++) {
			if (sqlparser_query_graph_span_index_at(&graph, dml.branches, b, &index, &error) != SQLPARSER_STATUS_OK ||
			    sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error) != SQLPARSER_STATUS_OK ||
			    branch.target_columns.count != 21U || branch.rows.count != 21U) goto failed;
			for (j = 0U; j < 10U; j++) {
				size_t ordinal = j < 5U ? j : 16U + j - 5U;
				const char *expected = j < 5U ? cipher[j] : original[b][j-5U];
				if (sqlparser_query_graph_span_index_at(&graph, branch.rows, ordinal, &index, &error) != SQLPARSER_STATUS_OK ||
				    sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error) != SQLPARSER_STATUS_OK ||
				    cell.literal.kind != SQLPARSER_LITERAL_KIND_STRING || !cell.literal.string_value || strcmp(cell.literal.string_value, expected)) goto failed;
			}
		}
	} else {
		if (sqlparser_statement_query_graph(handle, 0U, &graph, &error) != SQLPARSER_STATUS_OK) goto failed;
		if (expression) {
			if (sqlparser_query_graph_expression_at(&graph, 0U, &expression_view, &error) != SQLPARSER_STATUS_OK ||
			    expression_view.arguments.count != n) goto failed;
		} else if (sqlparser_query_graph_dml(&graph, &dml, &error) != SQLPARSER_STATUS_OK ||
		    dml.assignments.count != (copy ? 2U*n : n)) goto failed;
		for (j = 0U; j < (copy ? 2U*n : n); j++) {
			const char *actual;
			const char *expected = copy ? (j < n ? values[2U*j+1U].string_value : "original") :
				(j < list.count ? values[j].string_value : "original");
			if (expression) {
				if (sqlparser_query_graph_span_index_at(&graph, expression_view.arguments, j, &index, &error) != SQLPARSER_STATUS_OK ||
				    sqlparser_query_graph_expression_argument_at(&graph, index, &argument, &error) != SQLPARSER_STATUS_OK ||
				    argument.kind != SQLPARSER_GRAPH_EXPRESSION_ARGUMENT_LITERAL || !argument.has_value ||
				    sqlparser_query_graph_value_at(&graph, argument.value_index, &value, &error) != SQLPARSER_STATUS_OK) goto failed;
				actual = value.literal.string_value;
			} else {
				if (sqlparser_query_graph_span_index_at(&graph, dml.assignments, j, &index, &error) != SQLPARSER_STATUS_OK ||
				    sqlparser_query_graph_dml_assignment_at(&graph, index, &assignment, &error) != SQLPARSER_STATUS_OK) goto failed;
				actual = assignment.literal.string_value;
			}
			if (actual == NULL || strcmp(actual, expected) != 0) goto failed;
		}
	}
	printf("batch-bench mode=%s n=%zu patches=%zu input=%zu output=%zu parse=%.6f apply=%.6f deparse=%.6f handle_bytes=%zu check=ok\n",
		mode, n, list.count, sql.length, strlen(output), apply_start-parse_start, deparse_start-apply_start, end-deparse_start, sizeof(sqlparser_handle_t));
	result = 0;
	goto done;
failed:
	fprintf(stderr, "batch benchmark failed mode=%s error=%s\n", mode, error.message);
done:
	sqlparser_string_free(output);
	sqlparser_handle_destroy(handle);
	free(sql.text); free(original); free(patches); free(values); free(selectors); free(sources);
	return result;
}

int main(int argc, char **argv)
{
	if ((argc == 4 || argc == 5) && strcmp(argv[1], "--bench") == 0)
		return benchmark(argv[2], (size_t)strtoul(argv[3], NULL, 10),
			argc == 5 ? (size_t)strtoul(argv[4], NULL, 10) : 0U);
	return check_semantics() || check_resource_limits() || benchmark("oracle", 3U, 0U);
}
