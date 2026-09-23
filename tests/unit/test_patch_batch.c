#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jansson.h>
#include "sqlparser_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"

static double now(void);
static int profile_case;
static const char *case_name = "legacy";
static int count_active;
static size_t full_parse_calls, handle_clone_calls, deparse_calls;

#ifdef SQLPARSER_PATCH_BATCH_COUNTS
sqlparser_status_t __real_sqlparser_parse_with_options(const char *, const sqlparser_parse_options_t *, sqlparser_handle_t **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_parse_with_options(const char *sql, const sqlparser_parse_options_t *options, sqlparser_handle_t **handle, sqlparser_error_t *error)
{
	if (count_active) full_parse_calls++;
	return __real_sqlparser_parse_with_options(sql, options, handle, error);
}
sqlparser_status_t __real_sqlparser_handle_clone(const sqlparser_handle_t *, sqlparser_handle_t **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_handle_clone(const sqlparser_handle_t *source, sqlparser_handle_t **handle, sqlparser_error_t *error)
{
	if (count_active) handle_clone_calls++;
	return __real_sqlparser_handle_clone(source, handle, error);
}
sqlparser_status_t __real_sqlparser_deparse(const sqlparser_handle_t *, char **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_deparse(const sqlparser_handle_t *handle, char **sql, sqlparser_error_t *error)
{
	if (count_active) deparse_calls++;
	return __real_sqlparser_deparse(handle, sql, error);
}
#endif

static sqlparser_status_t apply_batch(sqlparser_handle_t *handle,
	const sqlparser_patch_list_t *list, sqlparser_error_t *error)
{
	sqlparser_status_t status;
	full_parse_calls = handle_clone_calls = deparse_calls = 0U;
	count_active = 1;
	status = sqlparser_apply_patch(handle, list, error);
	count_active = 0;
	return status;
}

static void print_patch_counts(void)
{
#ifdef SQLPARSER_PATCH_BATCH_COUNTS
	printf(" full_parses=%zu handle_clones=%zu deparse_calls=%zu",
		full_parse_calls, handle_clone_calls, deparse_calls);
#endif
}

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
	double parse_start, apply_start, json_start, deparse_start, end;
	int result = 1;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	list.items = items;
	list.count = count;
	parse_start = now();
	if (sqlparser_parse_with_options(sql, &options, &handle, &error) != SQLPARSER_STATUS_OK)
		goto done;
	apply_start = now();
	generation = handle->generation;
	if (apply_batch(handle, &list, &error) != SQLPARSER_STATUS_OK ||
	    handle->generation != generation + (strcmp(sql, expected) != 0 ? 1UL : 0UL)) goto done;
	json_start = now();
	if (sqlparser_export_view_json(handle, 0, &view, &error) != SQLPARSER_STATUS_OK) goto done;
	deparse_start = now();
	if (sqlparser_deparse(handle, &output, &error) != SQLPARSER_STATUS_OK) goto done;
	end = now();
	if (sqlparser_parse_with_options(expected, &options, &reference, &error) != SQLPARSER_STATUS_OK ||
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
	if (profile_case) {
		printf("batch-profile case=%s dialect=%s patches=%zu input=%zu output=%zu parse=%.6f apply=%.6f json=%.6f deparse=%.6f",
			case_name, sqlparser_dialect_name(dialect), count, strlen(sql), strlen(output),
			apply_start-parse_start, json_start-apply_start, deparse_start-json_start, end-deparse_start);
		print_patch_counts();
		puts(" check=ok");
	}
	result = 0;
done:
	if (result != 0)
		fprintf(stderr, "batch case failed case=%s dialect=%d sql=%s output=%s error=%s\n",
			case_name, (int)dialect, sql, output != NULL ? output : "", error.message);
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
	const sqlparser_patch_t *items, size_t count, size_t limit, size_t output_limit)
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
	sqlparser_status_t status;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	if (limit != 0U) {
		options.limits.max_sql_bytes = limit;
	}
	if (output_limit != 0U) options.limits.max_output_bytes = output_limit;
	if (sqlparser_parse_with_options(sql, &options, &handle, &error) != SQLPARSER_STATUS_OK) goto done;
	/* JSON snapshots can exceed a deliberately small SQL output cap. */
	handle->limits.max_output_bytes = SIZE_MAX;
	status = sqlparser_export_view_json(handle, 0, &before, &error);
	handle->limits.max_output_bytes = options.limits.max_output_bytes;
	if (status != SQLPARSER_STATUS_OK) goto done;
	generation = handle->generation;
	if (sqlparser_apply_patch(handle, &list, &error) == SQLPARSER_STATUS_OK ||
	    handle->generation != generation ||
	    sqlparser_deparse(handle, &output, &error) != SQLPARSER_STATUS_OK ||
	    strcmp(output, sql) != 0)
		goto done;
	handle->limits.max_output_bytes = SIZE_MAX;
	status = sqlparser_export_view_json(handle, 0, &after, &error);
	handle->limits.max_output_bytes = options.limits.max_output_bytes;
	if (status != SQLPARSER_STATUS_OK || strcmp(before, after) != 0) goto done;
	result = 0;
done:
	if (result != 0)
		fprintf(stderr, "batch rollback failed case=%s dialect=%d error=%s\n", case_name, (int)dialect, error.message);
	sqlparser_string_free(output);
	sqlparser_string_free(before);
	sqlparser_string_free(after);
	sqlparser_handle_destroy(handle);
	return result;
}

static int check_rollback(sqlparser_dialect_t dialect, const char *sql,
	const sqlparser_patch_t *items, size_t count)
{
	return check_rollback_limits(dialect, sql, items, count, 0U, 0U);
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
	if (check_rollback_limits(SQLPARSER_DIALECT_POSTGRESQL, sql, items, 2U, 500U, 0U)) return 1;
	snprintf(sql, sizeof(sql), "INSERT ALL INTO t(a,b,c) VALUES('orig','%s','%s') SELECT 1 FROM dual", original, original);
	items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_branch_columns[0]", .index=3U, .name="backup", .source_selector="stmt[0].insert_cell[0][0]"};
	items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .literal=&large_literal};
	items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .literal=&small_literal};
	return check_rollback_limits(SQLPARSER_DIALECT_ORACLE, sql, items, 3U, 500U, 0U);
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

static const char *batch_path_names[] = {
	"insert_cell", "insert_copy", "update_literal", "where_literal", "select_target", "merge_cell",
	"expr_literal", "expr_raw", "expr_float", "expr_bind", "expr_field", "expr_whole",
	"expr_insert", "expr_delete", "expr_repeat", "mixed_update_expr",
	"multi_all", "multi_first", "multi_raw", "multi_float", "multi_bind", "multi_copy",
	"multi_comment_all", "multi_comment_first"
};

static int check_batch_path(sqlparser_dialect_t dialect, const char *name, size_t n, size_t limit)
{
	buffer_t sql[2] = {{0}, {0}};
	sqlparser_patch_t *items = NULL;
	sqlparser_literal_value_t *literals = NULL;
	sqlparser_bind_value_t *binds = NULL;
	char (*selectors)[96] = NULL, (*text)[32] = NULL, (*payload)[96] = NULL;
	char (*keys)[24] = NULL, (*bind_sql)[32] = NULL;
	char source[96];
	size_t i, side;
	int result = 1, known = 0;
	int multi = strncmp(name, "multi_", 6U) == 0;
	int expression = strncmp(name, "expr_", 5U) == 0;
	int copy = strstr(name, "copy") != NULL;
	int raw = strstr(name, "raw") != NULL, floating = strstr(name, "float") != NULL;
	int binding = strstr(name, "bind") != NULL;
	int update = strcmp(name, "update_literal") == 0, merge = strcmp(name, "merge_cell") == 0;
	int where = strcmp(name, "where_literal") == 0, target = strcmp(name, "select_target") == 0;
	int insert_arg = strcmp(name, "expr_insert") == 0, delete_arg = strcmp(name, "expr_delete") == 0;
	int field = strcmp(name, "expr_field") == 0, whole = strcmp(name, "expr_whole") == 0;
	int repeat = strcmp(name, "expr_repeat") == 0, mixed = strcmp(name, "mixed_update_expr") == 0;
	int pg = dialect == SQLPARSER_DIALECT_POSTGRESQL || dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL || dialect == SQLPARSER_DIALECT_KINGBASE_POSTGRESQL;
	int mysql = sqlparser_dialect_is_mysql_compatible(dialect);
	int mssql = sqlparser_dialect_is_sqlserver_compatible(dialect);
	for (i = 0U; i < sizeof(batch_path_names)/sizeof(batch_path_names[0]); i++)
		if (strcmp(name, batch_path_names[i]) == 0) known = 1;
	if (!known || n == 0U || n > 500U || limit == 0U || limit > n || (mixed && n % 2U != 0U) ||
	    (multi && !sqlparser_dialect_is_oracle_or_dameng_compatible(dialect)) || (merge && mysql)) return 1;
	items = calloc(n, sizeof(*items)); literals = calloc(n, sizeof(*literals)); binds = calloc(n, sizeof(*binds));
	selectors = calloc(n, sizeof(*selectors)); text = calloc(n, sizeof(*text)); payload = calloc(n, sizeof(*payload));
	keys = calloc(n, sizeof(*keys)); bind_sql = calloc(n, sizeof(*bind_sql));
	for (side = 0U; side < 2U; side++) {
		sql[side].capacity = 2048U + n * 512U;
		sql[side].text = calloc(sql[side].capacity, 1U);
	}
	if (!items || !literals || !binds || !selectors || !text || !payload || !keys || !bind_sql || !sql[0].text || !sql[1].text) goto done;
	snprintf(source, sizeof(source), multi ? "stmt[0].insert_cell[%zu][0]" : "stmt[0].insert_cell[0][%zu]", n);
	for (i = 0U; i < n; i++) {
		snprintf(text[i], sizeof(text[i]), "changed_%zu", i);
		snprintf(keys[i], sizeof(keys[i]), pg || mysql ? "%zu" : "p%zu", i + 1U);
		binds[i].kind = pg || mysql ? SQLPARSER_BIND_KIND_POSITIONAL : SQLPARSER_BIND_KIND_NAMED;
		binds[i].key = keys[i];
		snprintf(bind_sql[i], sizeof(bind_sql[i]), pg ? "$%zu" : mysql ? "?" : mssql ? "@p%zu" : ":p%zu", i + 1U);
		literals[i].kind = floating ? SQLPARSER_LITERAL_KIND_FLOAT : SQLPARSER_LITERAL_KIND_STRING;
		literals[i].string_value = text[i]; literals[i].float_value = "1.25";
		snprintf(payload[i], sizeof(payload[i]), whole ? "CONCAT('%s','z')" : "'%s'", text[i]);
		if (mixed) snprintf(selectors[i], sizeof(selectors[i]), i % 2U ? "stmt[0].expression_arg[%zu][0]" : "stmt[0].assignment[%zu]", i/2U);
		else if (multi) snprintf(selectors[i], sizeof(selectors[i]), "stmt[0].insert_cell[%zu][0]", i);
		else if (merge) snprintf(selectors[i], sizeof(selectors[i]), "stmt[0].merge_insert_cell[0][%zu]", i);
		else if (update) snprintf(selectors[i], sizeof(selectors[i]), "stmt[0].assignment[%zu]", i);
		else if (where) snprintf(selectors[i], sizeof(selectors[i]), "stmt[0].where_literal[%zu]", i);
		else if (target) snprintf(selectors[i], sizeof(selectors[i]), "stmt[0].select_target[0][%zu]", i);
		else if (expression) snprintf(selectors[i], sizeof(selectors[i]), whole ? "stmt[0].expression[%zu]" : insert_arg || delete_arg ? "stmt[0].expression_args[%zu]" : "stmt[0].expression_arg[%zu][0]", repeat ? 0U : i);
		else snprintf(selectors[i], sizeof(selectors[i]), "stmt[0].insert_cell[0][%zu]", i);
		items[i].selector = selectors[i];
		items[i].op = insert_arg ? SQLPARSER_PATCH_INSERT_ARGUMENT : delete_arg ? SQLPARSER_PATCH_DELETE_ARGUMENT : SQLPARSER_PATCH_REPLACE;
		items[i].index = insert_arg ? 1U : 0U;
		if (delete_arg) continue;
		if (copy) items[i].source_selector = source;
		else if (binding) items[i].bind = &binds[i];
		else if (raw || whole) items[i].sql = payload[i];
		else items[i].literal = &literals[i];
	}
	for (side = 0U; side < 2U; side++) {
		buffer_t *s = &sql[side];
		if (mixed) {
			append(s, "UPDATE t SET ");
			for (i = 0U; i < n/2U; i++) append(s, "%sc%zu='%s'", i ? "," : "", i, side && i*2U < limit ? text[i*2U] : "old");
			append(s, " WHERE ");
			for (i = 0U; i < n/2U; i++) append(s, side && mssql ? "%sname LIKE CONCAT('%s', 'z')" : "%sname LIKE CONCAT('%s','z')",
				i ? " OR " : "", side && i*2U + 1U < limit ? text[i*2U + 1U] : "old");
			append(s, ";");
		} else if (multi) {
			append(s, strstr(name, "first") ? "INSERT FIRST WHEN 1=1 THEN " : "INSERT ALL ");
			if (strstr(name, "comment")) append(s, "/*batch-head*/ ");
			for (i = 0U; i <= n; i++) {
				const char *v = i == n ? "'source'" : !side || i >= limit ? "'old'" : copy ? "'source'" : floating ? "1.25" : binding ? bind_sql[i] : payload[i];
				append(s, "INTO t(c) VALUES(%s) ", v);
			}
			append(s, "SELECT 1 FROM dual");
			if (strstr(name, "comment")) append(s, " /*batch-tail*/");
			append(s, ";");
		} else if (expression) {
			append(s, "SELECT id FROM t WHERE ");
			for (i = 0U; i <= n; i++) {
				const char *v = i == n ? "'source'" : !side || (repeat ? i != 0U : i >= limit) ? (field ? "a" : "'old'") :
					floating ? "1.25" : binding ? bind_sql[i] : payload[repeat ? limit - 1U : i];
				if (i) append(s, " OR ");
				if (insert_arg || delete_arg) {
					append(s, "name LIKE COALESCE(");
					if (!(side && delete_arg && i < limit)) append(s, i == n ? "'source'," : "'old',");
					if (side && insert_arg && i < limit) append(s, "'%s',", text[i]);
					append(s, "'z','w')");
				} else if (whole && side && i < limit) append(s, "name LIKE %s", payload[i]);
				else append(s, "name LIKE CONCAT(%s,'z')", v);
			}
			append(s, ";");
		} else if (where || target) {
			append(s, where ? "SELECT id FROM t WHERE " : "SELECT ");
			for (i = 0U; i < n; i++) {
				if (i) append(s, where ? " AND " : ",");
				if (where) append(s, "c%zu=", i);
				append(s, "'%s'", side && i < limit ? text[i] : "old");
			}
			append(s, where ? ";" : " FROM t;");
		} else {
			if (update) append(s, "UPDATE t SET ");
			else {
				append(s, merge ? "MERGE INTO t USING s ON(t.id=s.id) WHEN NOT MATCHED THEN INSERT(" : "INSERT INTO t(");
				for (i = 0U; i <= n; i++) append(s, "%sc%zu", i ? "," : "", i);
				append(s, ") VALUES(");
			}
			for (i = 0U; i <= n; i++) {
				if (i) append(s, ",");
				if (update) append(s, "c%zu=", i);
				append(s, "'%s'", i == n ? "source" : !side || i >= limit ? "old" : copy ? "source" : text[i]);
			}
			append(s, update ? " WHERE id=1;" : ");");
		}
	}
	case_name = name;
	if (check_case(dialect, sql[0].text, items, limit, sql[1].text)) goto done;
	if (!profile_case) {
		const char *saved = items[limit - 1U].selector;
		items[limit - 1U].selector = "stmt[999].assignment[0]";
		if (check_rollback(dialect, sql[0].text, items, limit)) goto done;
		items[limit - 1U].selector = saved;
	}
	result = 0;
done:
	free(sql[0].text); free(sql[1].text); free(items); free(literals); free(binds);
	free(selectors); free(text); free(payload); free(keys); free(bind_sql);
	return result;
}

static int check_batch_paths(void)
{
	int d;
	size_t i, count = 0U;
	for (d = SQLPARSER_DIALECT_POSTGRESQL; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++) {
		for (i = 0U; i < sizeof(batch_path_names)/sizeof(batch_path_names[0]); i++) {
			const char *name = batch_path_names[i];
			if ((strncmp(name, "multi_", 6U) == 0 && !sqlparser_dialect_is_oracle_or_dameng_compatible((sqlparser_dialect_t)d)) ||
			    (strcmp(name, "merge_cell") == 0 && sqlparser_dialect_is_mysql_compatible((sqlparser_dialect_t)d))) continue;
			if (check_batch_path((sqlparser_dialect_t)d, name, 6U, 6U)) return 1;
			count++;
		}
	}
	printf("batch-path-cases positive=%zu rollback=%zu check=ok\n", count, profile_case ? 0U : count);
	return 0;
}

static int check_batch_dependencies(void)
{
	sqlparser_literal_value_t x = {.kind=SQLPARSER_LITERAL_KIND_STRING, .string_value="x'/*literal*/"};
	sqlparser_literal_value_t y = {.kind=SQLPARSER_LITERAL_KIND_STRING, .string_value="y"};
	sqlparser_patch_t items[7];
	char sql[1024], expected[1024], original[181], large[301];
	sqlparser_literal_value_t large_value = {.kind=SQLPARSER_LITERAL_KIND_STRING, .string_value=large};
	size_t positive = 0U, rollback = 0U;
	int d, output_limit, commented;
	memset(original, 'o', 180U); original[180] = 0;
	memset(large, 'l', 300U); large[300] = 0;
	for (d = SQLPARSER_DIALECT_POSTGRESQL; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++) {
		sqlparser_dialect_t dialect = (sqlparser_dialect_t)d;
		const char *bind = dialect == SQLPARSER_DIALECT_POSTGRESQL || dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL || dialect == SQLPARSER_DIALECT_KINGBASE_POSTGRESQL ? "$1" :
			sqlparser_dialect_is_mysql_compatible(dialect) ? "?" : sqlparser_dialect_is_sqlserver_compatible(dialect) ? "@p" : ":p";
		case_name = "source_dependency";
		strcpy(sql, "INSERT INTO t(a,b,c,d) VALUES('a','b','c','d')");
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&x};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .source_selector="stmt[0].insert_cell[0][0]"};
		items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&y};
		items[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .source_selector="stmt[0].insert_cell[0][1]"};
		items[4] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][3]", .source_selector="stmt[0].insert_cell[0][0]"};
		if (check_case(dialect, sql, items, 5U, "INSERT INTO t(a,b,c,d) VALUES('y','x''/*literal*/','x''/*literal*/','y')")) return 1;
		positive++;
		items[3].selector = "stmt[0].insert_cell[0][99]";
		if (check_rollback(dialect, sql, items, 5U)) return 1;
		rollback++;
		case_name = "copied_bind_lifetime";
		snprintf(sql, sizeof(sql), "INSERT INTO t(a,b,c) VALUES(%s,'old','tail')", bind);
		snprintf(expected, sizeof(expected), "INSERT INTO t(a,b,c) VALUES('y',%s,'tail')", bind);
		items[0] = items[1]; items[1] = items[2];
		if (check_case(dialect, sql, items, 2U, expected)) return 1;
		positive++;
		case_name = "expression_ordinal_shift";
		strcpy(sql, "SELECT id FROM t WHERE name LIKE CONCAT(LOWER('a'),'b') OR name LIKE CONCAT('c','d')");
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][0]", .literal=&x};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[1][1]", .literal=&y};
		if (check_case(dialect, sql, items, 2U, "SELECT id FROM t WHERE name LIKE CONCAT('x''/*literal*/','b') OR name LIKE CONCAT('c','y')")) return 1;
		positive++;
		case_name = "argument_index_shift_and_comments";
		strcpy(sql, "SELECT id FROM t WHERE name LIKE COALESCE(/*batch-head*/'a','b','c') /*batch-tail*/");
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ARGUMENT, .selector="stmt[0].expression_args[0]", .index=1U};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ARGUMENT, .selector="stmt[0].expression_args[0]", .index=1U, .literal=&x};
		items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][2]", .literal=&y};
		if (check_case(dialect, sql, items, 3U, "SELECT id FROM t WHERE name LIKE COALESCE(/*batch-head*/'a','x''/*literal*/','y') /*batch-tail*/")) return 1;
		positive++;
		items[1].literal = NULL; items[1].sql = "(";
		if (check_rollback(dialect, sql, items, 3U)) return 1;
		rollback++;
		case_name = "fragment_boundary_before_later_replacement";
		strcpy(sql, "SELECT id FROM t WHERE name LIKE CONCAT('a','b')");
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][0]", .sql="1) RETURNING 1 --"};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][0]", .literal=&y};
		if (check_rollback(dialect, sql, items, 2U)) return 1;
		rollback++;
		case_name = "replacement_trivia_survives_repeated_argument";
		items[0].sql = "/*batch-head*/ 'x' /*batch-tail*/";
		if (check_case(dialect, sql, items, 2U, "SELECT id FROM t WHERE name LIKE CONCAT(/*batch-head*/ 'y' /*batch-tail*/,'b')")) return 1;
		positive++;
		items[0].sql = "'x' /*batch-tail*/";
		if (check_case(dialect, sql, items, 2U, "SELECT id FROM t WHERE name LIKE CONCAT('y' /*batch-tail*/,'b')")) return 1;
		positive++;
		case_name = "intermediate_expression_limit";
		snprintf(sql, sizeof(sql), "SELECT id FROM t WHERE name LIKE CONCAT('%s','%s')", original, original);
		items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][0]", .literal=&large_value};
		items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][1]", .literal=&y};
		for (output_limit = 0; output_limit < 2; output_limit++) {
			if (check_rollback_limits(dialect, sql, items, 2U, output_limit ? 0U : 500U, output_limit ? 500U : 0U)) return 1;
			rollback++;
		}
		if (sqlparser_dialect_is_oracle_or_dameng_compatible(dialect)) {
			case_name = "multi_fragment_boundary_before_later_replacement";
			strcpy(sql, "INSERT ALL INTO t(a,b) VALUES('a','b') SELECT 1 FROM dual");
			items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="1) RETURNING 1 --"};
			items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&y};
			if (check_rollback(dialect, sql, items, 2U)) return 1;
			rollback++;
			case_name = "cross_branch_source_dependency";
			strcpy(sql, "INSERT ALL INTO t(a,b) VALUES('a','b') INTO t(a,b) VALUES('c','d') INTO t(a,b) VALUES('e','f') SELECT 1 FROM dual");
			items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&x};
			items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[1][0]", .source_selector="stmt[0].insert_cell[0][0]"};
			items[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&y};
			items[3] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[2][0]", .source_selector="stmt[0].insert_cell[1][0]"};
			if (check_case(dialect, sql, items, 4U, "INSERT ALL INTO t(a,b) VALUES('y','b') INTO t(a,b) VALUES('x''/*literal*/','d') INTO t(a,b) VALUES('x''/*literal*/','f') SELECT 1 FROM dual")) return 1;
			positive++;
			case_name = "intermediate_multi_insert_limit";
			for (commented = 0; commented < 2; commented++) {
				snprintf(sql, sizeof(sql), "INSERT ALL %sINTO t(a,b) VALUES('%s','%s') SELECT 1 FROM dual", commented ? "/*batch-head*/ " : "", original, original);
				items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&large_value};
				items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .literal=&y};
				for (output_limit = 0; output_limit < 2; output_limit++) {
					if (check_rollback_limits(dialect, sql, items, 2U, output_limit ? 0U : 500U, output_limit ? 500U : 0U)) return 1;
					rollback++;
				}
			}
		}
	}
	case_name = "pseudo_column_expression_ordinal_shift";
	strcpy(sql, "SELECT id FROM t WHERE name LIKE CONCAT('a','b') OR name LIKE CONCAT('c','d')");
	items[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[0][0]", .sql="ROWNUM"};
	items[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].expression_arg[2][1]", .literal=&y};
	if (check_case(SQLPARSER_DIALECT_ORACLE, sql, items, 2U,
	    "SELECT id FROM t WHERE name LIKE CONCAT(ROWNUM,'b') OR name LIKE CONCAT('c','y')")) return 1;
	positive++;
	case_name = "system_variable_before_later_replacement";
	items[0].sql = "@@VERSION";
	items[1].selector = "stmt[0].expression_arg[0][0]";
	if (check_rollback(SQLPARSER_DIALECT_SQLSERVER, sql, items, 2U)) return 1;
	rollback++;
	printf("batch-dependency-cases positive=%zu rollback=%zu check=ok\n", positive, rollback);
	return 0;
}

static int benchmark_fixture(const char *path, size_t limit)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error = {0};
	sqlparser_handle_t *handle = NULL, *reparsed = NULL;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_branch_t branch;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_dml_column_t column;
	sqlparser_literal_value_t literal = {.kind=SQLPARSER_LITERAL_KIND_STRING, .string_value="simulated_ciphertext"};
	sqlparser_patch_t items[250] = {{0}};
	sqlparser_patch_list_t list = {items, limit};
	char *selectors[250] = {0}, *before[800] = {0}, *columns[800] = {0};
	char *sql = NULL, *output = NULL, *json = NULL, *actual = NULL, *reparsed_json = NULL;
	json_t *actual_view = NULL, *reparsed_view = NULL;
	FILE *file = NULL;
	long file_size;
	size_t length, b, c, i, index, count = 0U;
	double t0, t1, t2, t3, t4, t5, t6;
	int result = 1;
	if (limit == 0U || limit > 250U) return 1;
	file = fopen(path, "rb");
	if (file == NULL || fseek(file, 0L, SEEK_END) != 0 || (file_size = ftell(file)) <= 0L || file_size > 32768L ||
	    fseek(file, 0L, SEEK_SET) != 0) goto done;
	length = (size_t)file_size;
	sql = calloc(length + 1U, 1U);
	if (sql == NULL || fread(sql, 1U, length, file) != length) goto done;
	if (length > 0U && sql[length - 1U] == '\n') sql[--length] = '\0';
	if (length != 27530U) goto done;
	sqlparser_parse_options_default(&options); options.dialect = SQLPARSER_DIALECT_ORACLE;
	t0 = now();
	if (sqlparser_parse_with_options(sql, &options, &handle, &error) != SQLPARSER_STATUS_OK) goto done;
	t1 = now();
	if (sqlparser_statement_query_graph(handle, 0U, &graph, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_query_graph_dml(&graph, &dml, &error) != SQLPARSER_STATUS_OK || dml.branches.count != 50U) goto done;
	for (b = 0U; b < 50U; b++) {
		if (sqlparser_query_graph_span_index_at(&graph, dml.branches, b, &index, &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error) != SQLPARSER_STATUS_OK ||
		    branch.target_columns.count != 16U || branch.rows.count != 16U) goto done;
		for (c = 1U; c <= 5U; c++) {
			if (sqlparser_query_graph_span_index_at(&graph, branch.rows, c, &index, &error) != SQLPARSER_STATUS_OK ||
			    sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error) != SQLPARSER_STATUS_OK ||
			    !cell.has_selector || cell.literal.kind != SQLPARSER_LITERAL_KIND_STRING ||
			    sqlparser_selector_format(&cell.selector, &selectors[count], &error) != SQLPARSER_STATUS_OK) goto done;
			items[count] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector=selectors[count], .literal=&literal};
			count++;
		}
	}
	t2 = now();
	/* Snapshots used for verification are outside the measured API stages. */
	for (b = 0U; b < 50U; b++) {
		if (sqlparser_query_graph_span_index_at(&graph, dml.branches, b, &index, &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error) != SQLPARSER_STATUS_OK) goto done;
		for (c = 0U; c < 16U; c++) {
			if (sqlparser_insert_cell_sql(handle, 0U, b, c, &before[b*16U+c], &error) != SQLPARSER_STATUS_OK ||
			    sqlparser_query_graph_span_index_at(&graph, branch.target_columns, c, &index, &error) != SQLPARSER_STATUS_OK ||
			    sqlparser_query_graph_dml_column_at(&graph, index, &column, &error) != SQLPARSER_STATUS_OK ||
			    column.column_name == NULL || (columns[b*16U+c] = strdup(column.column_name)) == NULL) goto done;
		}
	}
	t3 = now();
	if (apply_batch(handle, &list, &error) != SQLPARSER_STATUS_OK) goto done;
	t4 = now();
	if (sqlparser_export_view_json(handle, 0, &json, &error) != SQLPARSER_STATUS_OK) goto done;
	t5 = now();
	if (sqlparser_deparse(handle, &output, &error) != SQLPARSER_STATUS_OK) goto done;
	t6 = now();
	if (sqlparser_parse_with_options(output, &options, &reparsed, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(reparsed, 0U, &graph, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_query_graph_dml(&graph, &dml, &error) != SQLPARSER_STATUS_OK || dml.branches.count != 50U) goto done;
	if (sqlparser_export_view_json(reparsed, 0, &reparsed_json, &error) != SQLPARSER_STATUS_OK) goto done;
	actual_view = json_loads(json, 0, NULL); reparsed_view = json_loads(reparsed_json, 0, NULL);
	if (actual_view == NULL || reparsed_view == NULL || !json_equal(actual_view, reparsed_view)) goto done;
	for (b = 0U; b < 50U; b++) {
		if (sqlparser_query_graph_span_index_at(&graph, dml.branches, b, &index, &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_dml_branch_at(&graph, index, &branch, &error) != SQLPARSER_STATUS_OK ||
		    branch.target_columns.count != 16U || branch.rows.count != 16U) goto done;
		for (c = 0U; c < 16U; c++) {
			const char *expected = c >= 1U && c <= 5U && b*5U+c-1U < limit ? "'simulated_ciphertext'" : before[b*16U+c];
			if (sqlparser_insert_cell_sql(reparsed, 0U, b, c, &actual, &error) != SQLPARSER_STATUS_OK || strcmp(actual, expected) != 0 ||
			    sqlparser_query_graph_span_index_at(&graph, branch.target_columns, c, &index, &error) != SQLPARSER_STATUS_OK ||
			    sqlparser_query_graph_dml_column_at(&graph, index, &column, &error) != SQLPARSER_STATUS_OK ||
			    column.column_name == NULL || strcmp(column.column_name, columns[b*16U+c]) != 0) goto done;
			sqlparser_string_free(actual); actual = NULL;
		}
	}
	printf("batch-profile case=oracle_values dialect=oracle patches=%zu input=%zu output=%zu parse=%.6f construct=%.6f apply=%.6f json=%.6f deparse=%.6f",
		limit, length, strlen(output), t1-t0, t2-t1, t4-t3, t5-t4, t6-t5);
	print_patch_counts();
	puts(" check=ok");
	result = 0;
done:
	if (result) fprintf(stderr, "Oracle batch fixture failed path=%s error=%s\n", path, error.message);
	if (file) fclose(file);
	for (i = 0U; i < 250U; i++) sqlparser_string_free(selectors[i]);
	for (i = 0U; i < 800U; i++) { sqlparser_string_free(before[i]); free(columns[i]); }
	free(sql); sqlparser_string_free(output); sqlparser_string_free(json); sqlparser_string_free(actual); sqlparser_string_free(reparsed_json);
	json_decref(actual_view); json_decref(reparsed_view);
	sqlparser_handle_destroy(handle); sqlparser_handle_destroy(reparsed);
	return result;
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
	if (apply_batch(handle, &list, &error) != SQLPARSER_STATUS_OK) goto failed;
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
	printf("batch-bench mode=%s n=%zu patches=%zu input=%zu output=%zu parse=%.6f apply=%.6f deparse=%.6f handle_bytes=%zu",
		mode, n, list.count, sql.length, strlen(output), apply_start-parse_start, deparse_start-apply_start, end-deparse_start, sizeof(sqlparser_handle_t));
	print_patch_counts();
	puts(" check=ok");
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
	int d;
	if (argc == 2 && strcmp(argv[1], "--profile-all") == 0) {
		profile_case = 1;
		return check_batch_paths();
	}
	if ((argc == 5 || argc == 6) && strcmp(argv[1], "--profile") == 0) {
		size_t n = (size_t)strtoul(argv[4], NULL, 10);
		size_t limit = argc == 6 ? (size_t)strtoul(argv[5], NULL, 10) : n;
		profile_case = 1;
		for (d = SQLPARSER_DIALECT_POSTGRESQL; d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; d++)
			if (strcmp(argv[3], sqlparser_dialect_name((sqlparser_dialect_t)d)) == 0)
				return check_batch_path((sqlparser_dialect_t)d, argv[2], n, limit);
		return 1;
	}
	if (argc == 4 && strcmp(argv[1], "--fixture") == 0)
		return benchmark_fixture(argv[2], (size_t)strtoul(argv[3], NULL, 10));
	if ((argc == 4 || argc == 5) && strcmp(argv[1], "--bench") == 0)
		return benchmark(argv[2], (size_t)strtoul(argv[3], NULL, 10),
			argc == 5 ? (size_t)strtoul(argv[4], NULL, 10) : 0U);
	if (argc != 1) return 1;
	return check_semantics() || check_resource_limits() || check_batch_paths() || check_batch_dependencies() ||
		benchmark_fixture("tests/cases/patch_batch_oracle_insert_all.sql", 250U) || benchmark("oracle", 3U, 0U);
}
