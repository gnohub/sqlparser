#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>
#include "sqlparser/sqlparser.h"

typedef struct {
	const char *name;
	const char *value;
	const char *standard_sql;
	const char *mysql_sql;
	const char *escape_sql;
} string_case_t;

/* Independent expected spellings: never derive expectations from the library renderer. */
static const string_case_t strings[] = {
	{"plain", "plain", "'plain'", "'plain'", "E'plain'"},
	{"single-backslash", "x\\y", "'x\\y'", "'x\\\\y'", "E'x\\\\y'"},
	{"path", "C:\\new\\tab\\tail", "'C:\\new\\tab\\tail'", "'C:\\\\new\\\\tab\\\\tail'", "E'C:\\\\new\\\\tab\\\\tail'"},
	{"trailing-backslash", "trail\\", "'trail\\'", "'trail\\\\'", "E'trail\\\\'"},
	{"consecutive-backslashes", "\\\\server\\share", "'\\\\server\\share'", "'\\\\\\\\server\\\\share'", "E'\\\\\\\\server\\\\share'"},
	{"quote-before-backslash", "a'\\b", "'a''\\b'", "'a''\\\\b'", "E'a''\\\\b'"},
	{"backslash-before-quote", "a\\'b", "'a\\''b'", "'a\\\\''b'", "E'a\\\\''b'"},
	{"unicode", "中\\文", "'中\\文'", "'中\\\\文'", "E'中\\\\文'"},
	{"escape-looking-text", "\\n\\t\\r", "'\\n\\t\\r'", "'\\\\n\\\\t\\\\r'", "E'\\\\n\\\\t\\\\r'"},
	{"embedded-prefix", "E'keep\\text'", "'E''keep\\text'''", "'E''keep\\\\text'''", "E'E''keep\\\\text'''"}
};

enum {
	READ_TARGET, SET_TARGET, SELECTOR_SET_TARGET, SET_TARGETS,
	PATCH_SQL, PATCH_LITERAL, SET_LITERAL, COPY_TARGET,
	READ_NATIONAL, SET_NATIONAL, COPY_NATIONAL,
	INSERT_SQL, INSERT_LITERAL, UPDATE_SQL, UPDATE_LITERAL,
	WHERE_LITERAL, ARGUMENT_LITERAL, GUARDED_TARGET, METHOD_COUNT
};

static const char *methods[] = {
	"read-target", "set-target", "selector-set-target", "set-targets",
	"patch-sql", "patch-literal", "set-literal", "copy-target",
	"read-national", "set-national", "copy-national",
	"insert-sql", "insert-literal", "update-sql", "update-literal",
	"where-literal", "argument-literal", "guarded-target"
};

static sqlparser_dialect_t current_dialect;
static const char *current_method;
static const char *current_sample;

static int mysql_style(void)
{
	return current_dialect == SQLPARSER_DIALECT_MYSQL ||
		current_dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL ||
		current_dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL;
}

static int postgres_style(void)
{
	return current_dialect == SQLPARSER_DIALECT_POSTGRESQL ||
		current_dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL ||
		current_dialect == SQLPARSER_DIALECT_KINGBASE_POSTGRESQL;
}

static const char *select_suffix(void)
{
	return current_dialect == SQLPARSER_DIALECT_ORACLE ||
		current_dialect == SQLPARSER_DIALECT_DAMENG ||
		current_dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE ||
		current_dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE ? " FROM dual;" : ";";
}

static int failure(const char *stage, const char *actual, const char *expected)
{
	fprintf(stderr, "FAIL [string-literal/%s/%s/%s] %s\n",
		sqlparser_dialect_name(current_dialect), current_method, current_sample, stage);
	if (expected != NULL) fprintf(stderr, "  expected: %s\n", expected);
	if (actual != NULL) fprintf(stderr, "  actual:   %s\n", actual);
	return 1;
}

static void format_sql(char *out, size_t capacity, const char *format,
	const string_case_t *const *values, size_t count, unsigned national, unsigned escaped)
{
	char parts[3][256] = {{0}};
	size_t index;

	if (count > 3U) abort();
	for (index = 0U; index < count; index++) {
		const char *literal = mysql_style() ? values[index]->mysql_sql :
			(escaped & (1U << index)) ? values[index]->escape_sql : values[index]->standard_sql;
		int length = snprintf(parts[index], sizeof(parts[index]), "%s%s",
			(national & (1U << index)) ? "N" : "", literal);
		if (length < 0 || (size_t)length >= sizeof(parts[index])) abort();
	}
	{
		int length = snprintf(out, capacity, format, parts[0], parts[1], parts[2]);
		if (length < 0 || (size_t)length >= capacity) abort();
	}
}

static int check_text(const char *actual, const char *format,
	const string_case_t *const *values, size_t count, unsigned national, const char *stage)
{
	char expected[2048];
	unsigned mask, variants = postgres_style() ? 1U << count : 1U;

	/* PostgreSQL may use either equivalent spelling, independently for each cell. */
	for (mask = 0U; mask < variants; mask++) {
		format_sql(expected, sizeof(expected), format, values, count, national, mask);
		if (actual != NULL && strcmp(actual, expected) == 0) return 0;
	}
	format_sql(expected, sizeof(expected), format, values, count, national, 0U);
	return failure(stage, actual, expected);
}

static int check_values(sqlparser_handle_t *handle, const string_case_t *const *values, size_t count)
{
	size_t index;
	int failed = 0;

	for (index = 0U; index < count; index++) {
		sqlparser_selector_t selector = {.kind = SQLPARSER_SELECTOR_KIND_LITERAL, .item_index = index};
		sqlparser_literal_view_t value;
		sqlparser_error_t error = {0};
		if (sqlparser_selector_literal(handle, &selector, &value, &error) != SQLPARSER_STATUS_OK) {
			failed |= failure("read semantic value", error.message, values[index]->value);
		} else if (value.kind != SQLPARSER_LITERAL_KIND_STRING || value.string_value == NULL ||
			   strcmp(value.string_value, values[index]->value) != 0) {
			failed |= failure("semantic value changed", value.string_value, values[index]->value);
		}
	}
	return failed;
}

static int check_views(sqlparser_handle_t *left, sqlparser_handle_t *right)
{
	char *left_text = NULL, *right_text = NULL;
	json_t *left_json = NULL, *right_json = NULL;
	sqlparser_error_t error = {0};
	int failed = 1;

	if (sqlparser_export_view_json(left, 0, &left_text, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_export_view_json(right, 0, &right_text, &error) != SQLPARSER_STATUS_OK) {
		failure("export View", error.message, NULL);
		goto done;
	}
	left_json = json_loads(left_text, 0, NULL);
	right_json = json_loads(right_text, 0, NULL);
	if (left_json == NULL || right_json == NULL || !json_equal(left_json, right_json)) {
		failure("View mismatch", left_text, right_text);
		goto done;
	}
	failed = 0;
done:
	json_decref(left_json);
	json_decref(right_json);
	sqlparser_string_free(left_text);
	sqlparser_string_free(right_text);
	return failed;
}

/* reader: 0 SELECT target, 1 INSERT cell, 2 UPDATE value, 3 no separate fragment API. */
static int check_result(sqlparser_handle_t *handle, sqlparser_handle_t *expected_handle, const char *format,
	const char *fragment_format, const string_case_t *const *values, size_t count,
	unsigned national, int reader, unsigned long generation)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error = {0};
	sqlparser_handle_t *reparsed = NULL;
	sqlparser_query_graph_view_t graph;
	char *output = NULL, *fragment = NULL;
	size_t index;
	int failed = 0;

	sqlparser_parse_options_default(&options);
	options.dialect = current_dialect;
	if (sqlparser_deparse(handle, &output, &error) != SQLPARSER_STATUS_OK) {
		failed = failure("deparse", error.message, NULL);
		goto done;
	}
	failed |= check_text(output, format, values, count, national, "whole SQL");
	failed |= check_values(handle, values, count);
	if (sqlparser_statement_query_graph(handle, 0U, &graph, &error) != SQLPARSER_STATUS_OK ||
	    graph.generation != generation) failed |= failure("generation mismatch", error.message, NULL);
	for (index = 0U; reader != 3 && index < count; index++) {
		sqlparser_status_t status;
		const string_case_t *cell[] = {values[index]};
		unsigned cell_national = (national >> index) & 1U;
		if (reader == 0) status = sqlparser_select_target_sql(handle, 0U, 0U, index, &fragment, &error);
		else if (reader == 1) status = sqlparser_insert_cell_sql(handle, 0U, 0U, index, &fragment, &error);
		else status = sqlparser_update_assignment_sql(handle, 0U, index, &fragment, &error);
		if (status != SQLPARSER_STATUS_OK) failed |= failure("read fragment", error.message, NULL);
		else failed |= check_text(fragment, fragment_format, cell, 1U, cell_national, "fragment SQL");
		sqlparser_string_free(fragment);
		fragment = NULL;
		if (reader == 0) {
			sqlparser_selector_t selector = {.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET, .column_index = index};
			status = sqlparser_selector_select_target_sql(handle, &selector, &fragment, &error);
			if (status != SQLPARSER_STATUS_OK) failed |= failure("selector read fragment", error.message, NULL);
			else failed |= check_text(fragment, fragment_format, cell, 1U, cell_national, "selector fragment SQL");
			sqlparser_string_free(fragment);
			fragment = NULL;
		}
	}
	failed |= check_views(handle, expected_handle);
	if (sqlparser_parse_with_options(output, &options, &reparsed, &error) != SQLPARSER_STATUS_OK) {
		failed |= failure("reparse", error.message, NULL);
		goto done;
	}
	failed |= check_values(reparsed, values, count);
	failed |= check_views(handle, reparsed);
done:
	sqlparser_string_free(fragment);
	sqlparser_string_free(output);
	sqlparser_handle_destroy(reparsed);
	return failed;
}

static int run_case(int method, const string_case_t *value)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error = {0};
	sqlparser_handle_t *handle = NULL, *expected_handle = NULL;
	sqlparser_query_graph_view_t before;
	sqlparser_selector_t selector = {.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET};
	sqlparser_literal_value_t literal = {.kind = SQLPARSER_LITERAL_KIND_STRING, .string_value = value->value};
	sqlparser_patch_t patch = {.op = SQLPARSER_PATCH_REPLACE, .selector = "stmt[0].select_target[0][0]"};
	sqlparser_patch_list_t patches = {&patch, 1U};
	const string_case_t *values[] = {value, value};
	char sql[2048], expected_sql[2048], format[512], target_format[256], fragment[256], replacement[512];
	unsigned national = method == READ_NATIONAL || method == SET_NATIONAL || method == COPY_NATIONAL;
	int copied = method == COPY_TARGET || method == COPY_NATIONAL;
	int read_only = method == READ_TARGET || method == READ_NATIONAL;
	int reader = 0, failed = 1;
	size_t value_count = copied ? 2U : 1U;
	unsigned national_mask = national ? copied ? 3U : 1U : 0U;
	sqlparser_status_t status;

	current_method = methods[method];
	current_sample = value->name;
	sqlparser_parse_options_default(&options);
	options.dialect = current_dialect;
	snprintf(format, sizeof(format), "SELECT %%s AS probe_value%s", select_suffix());
	strcpy(target_format, "%s AS probe_value");
	snprintf(sql, sizeof(sql), "SELECT 1 AS probe_value%s", select_suffix());
	format_sql(fragment, sizeof(fragment), "%s", values, 1U, national, 0U);
	if (read_only) format_sql(sql, sizeof(sql), format, values, 1U, national, 0U);
	if (copied) {
		char original_format[512];
		snprintf(original_format, sizeof(original_format), "SELECT %%s AS source_value, 1 AS probe_value%s", select_suffix());
		format_sql(sql, sizeof(sql), original_format, values, 1U, national, 0U);
		snprintf(format, sizeof(format), "SELECT %%s AS source_value, %%s AS source_value%s", select_suffix());
		strcpy(target_format, "%s AS source_value");
	}
	if (method == PATCH_LITERAL) {
		snprintf(format, sizeof(format), "SELECT %%s%s", select_suffix());
		strcpy(target_format, "%s");
	} else if (method == INSERT_SQL || method == INSERT_LITERAL) {
		strcpy(sql, "INSERT INTO t(v) VALUES(1);");
		strcpy(format, "INSERT INTO t(v) VALUES(%s);");
		strcpy(target_format, "%s");
		reader = 1;
	} else if (method == UPDATE_SQL || method == UPDATE_LITERAL) {
		strcpy(sql, "UPDATE t SET v = 1 WHERE id = 7;");
		strcpy(format, "UPDATE t SET v = %s WHERE id = 7;");
		strcpy(target_format, "%s");
		reader = 2;
	} else if (method == WHERE_LITERAL) {
		strcpy(sql, "SELECT id FROM t WHERE v = 'old';");
		strcpy(format, "SELECT id FROM t WHERE v = %s;");
		reader = 3;
	} else if (method == ARGUMENT_LITERAL) {
		strcpy(sql, "SELECT id FROM t WHERE v LIKE CONCAT('old', 'z');");
		strcpy(format, "SELECT id FROM t WHERE v LIKE CONCAT(%s, 'z');");
		reader = 3;
	} else if (method == GUARDED_TARGET) {
		const char *open_quote = mysql_style() ? "\x60" :
			current_dialect == SQLPARSER_DIALECT_SQLSERVER ||
			current_dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER ||
			current_dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER ? "[" : "\"";
		const char *close_quote = strcmp(open_quote, "[") == 0 ? "]" : open_quote;
		snprintf(target_format, sizeof(target_format), "%%s AS %sE'keep\\name%s", open_quote, close_quote);
		snprintf(format, sizeof(format), "/* keep E'comment\\\\' */ SELECT %s%s -- keep E'tail\\\\'\n",
			target_format, select_suffix());
		snprintf(sql, sizeof(sql), format, "1");
	}
	snprintf(replacement, sizeof(replacement), target_format, fragment);
	format_sql(expected_sql, sizeof(expected_sql), format, values, value_count, national_mask, 0U);
	if (sqlparser_parse_with_options(expected_sql, &options, &expected_handle, &error) != SQLPARSER_STATUS_OK ||
	    check_values(expected_handle, values, value_count)) {
		failure("invalid case expectation", error.message, expected_sql);
		goto done;
	}
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(handle, 0U, &before, &error) != SQLPARSER_STATUS_OK) {
		failure("parse case input", error.message, sql);
		goto done;
	}
	switch (method) {
		case READ_TARGET: case READ_NATIONAL: break;
		case SET_TARGET: case SET_NATIONAL: case GUARDED_TARGET:
			status = sqlparser_select_set_target_sql(handle, 0U, 0U, 0U, replacement, &error); break;
		case SELECTOR_SET_TARGET:
			status = sqlparser_selector_set_select_target_sql(handle, &selector, replacement, &error); break;
		case SET_TARGETS:
			status = sqlparser_select_set_targets_sql(handle, 0U, 0U, replacement, &error); break;
		case PATCH_SQL:
			patch.sql = replacement;
			status = sqlparser_apply_patch(handle, &patches, &error); break;
		case PATCH_LITERAL:
			patch.literal = &literal;
			status = sqlparser_apply_patch(handle, &patches, &error); break;
		case SET_LITERAL:
			selector.kind = SQLPARSER_SELECTOR_KIND_LITERAL;
			status = sqlparser_selector_set_literal(handle, &selector, &literal, &error); break;
		case COPY_TARGET: case COPY_NATIONAL:
			patch.selector = "stmt[0].select_target[0][1]";
			patch.source_selector = "stmt[0].select_target[0][0]";
			status = sqlparser_apply_patch(handle, &patches, &error); break;
		case INSERT_SQL:
			status = sqlparser_insert_set_cell_sql(handle, 0U, 0U, 0U, fragment, &error); break;
		case INSERT_LITERAL:
			status = sqlparser_insert_set_cell_literal(handle, 0U, 0U, 0U, &literal, &error); break;
		case UPDATE_SQL:
			status = sqlparser_update_set_assignment_sql(handle, 0U, 0U, fragment, &error); break;
		case UPDATE_LITERAL:
			status = sqlparser_update_set_assignment_literal(handle, 0U, 0U, &literal, &error); break;
		case WHERE_LITERAL:
			status = sqlparser_statement_where_set_literal(handle, 0U, 0U, &literal, &error); break;
		case ARGUMENT_LITERAL:
			patch.selector = "stmt[0].expression_arg[0][0]";
			patch.literal = &literal;
			status = sqlparser_apply_patch(handle, &patches, &error); break;
	}
	if (status != SQLPARSER_STATUS_OK) {
		failure("rewrite must succeed", error.message, replacement);
		goto done;
	}
	failed = check_result(handle, expected_handle, format, target_format, values, value_count,
		national_mask, reader, before.generation + (read_only ? 0UL : 1UL));
done:
	sqlparser_handle_destroy(expected_handle);
	sqlparser_handle_destroy(handle);
	return failed;
}

static int check_batch(int rollback)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error = {0};
	sqlparser_handle_t *handle = NULL, *expected_handle = NULL;
	sqlparser_query_graph_view_t before, after;
	sqlparser_literal_value_t first = {.kind = SQLPARSER_LITERAL_KIND_STRING, .string_value = strings[1].value};
	sqlparser_literal_value_t last = {.kind = SQLPARSER_LITERAL_KIND_STRING, .string_value = strings[3].value};
	sqlparser_patch_t items[] = {
		{.op = SQLPARSER_PATCH_REPLACE, .selector = "stmt[0].select_target[0][0]", .literal = &first},
		{.op = SQLPARSER_PATCH_REPLACE, .selector = "stmt[0].select_target[0][1]", .source_selector = "stmt[0].select_target[0][0]"},
		{.op = SQLPARSER_PATCH_REPLACE, .selector = "stmt[0].select_target[0][0]", .literal = &last},
		{.op = SQLPARSER_PATCH_REPLACE, .selector = "stmt[0].select_target[0][2]", .source_selector = "stmt[0].select_target[0][0]"},
		{.op = SQLPARSER_PATCH_REPLACE, .selector = "stmt[0].select_target[0][99]", .literal = &first}
	};
	sqlparser_patch_list_t patches = {items, rollback ? 5U : 4U};
	const string_case_t *values[] = {&strings[3], &strings[1], &strings[3]};
	char sql[512], format[512], expected_sql[512];
	char *output = NULL, *before_view = NULL, *after_view = NULL;
	sqlparser_status_t status;
	int failed = 1;

	current_method = rollback ? "batch-rollback" : "batch-source-order";
	current_sample = "replace-copy-replace-copy";
	sqlparser_parse_options_default(&options);
	options.dialect = current_dialect;
	snprintf(sql, sizeof(sql), "SELECT 'old' AS a, 'old' AS b, 'old' AS c%s", select_suffix());
	snprintf(format, sizeof(format), "SELECT %%s, %%s, %%s%s", select_suffix());
	format_sql(expected_sql, sizeof(expected_sql), format, values, 3U, 0U, 0U);
	if (sqlparser_parse_with_options(expected_sql, &options, &expected_handle, &error) != SQLPARSER_STATUS_OK ||
	    check_values(expected_handle, values, 3U)) {
		failure("invalid batch expectation", error.message, expected_sql);
		goto done;
	}
	if (sqlparser_parse_with_options(sql, &options, &handle, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(handle, 0U, &before, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_export_view_json(handle, 0, &before_view, &error) != SQLPARSER_STATUS_OK) {
		failure("batch setup", error.message, sql);
		goto done;
	}
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (!rollback) {
		if (status != SQLPARSER_STATUS_OK) {
			failure("batch rewrite must succeed", error.message, NULL);
			goto done;
		}
		failed = check_result(handle, expected_handle, format, "%s", values, 3U, 0U, 0, before.generation + 1UL);
	} else {
		failed = 0;
		if (status != SQLPARSER_STATUS_INVALID_ARGUMENT)
			failed |= failure("batch must reach final invalid selector", error.message,
				"SQLPARSER_STATUS_INVALID_ARGUMENT");
		if (sqlparser_deparse(handle, &output, &error) != SQLPARSER_STATUS_OK ||
		    output == NULL || strcmp(output, sql) != 0 ||
		    sqlparser_statement_query_graph(handle, 0U, &after, &error) != SQLPARSER_STATUS_OK ||
		    after.generation != before.generation ||
		    sqlparser_export_view_json(handle, 0, &after_view, &error) != SQLPARSER_STATUS_OK ||
		    before_view == NULL || after_view == NULL || strcmp(before_view, after_view) != 0) {
			failed |= failure("batch rollback changed SQL, View or generation", error.message, sql);
			goto done;
		}
	}
done:
	sqlparser_string_free(output);
	sqlparser_string_free(before_view);
	sqlparser_string_free(after_view);
	sqlparser_handle_destroy(expected_handle);
	sqlparser_handle_destroy(handle);
	return failed;
}

int main(int argc, char **argv)
{
	size_t total = 0U, total_failed = 0U, index;
	int dialect, method;

	if (argc > 3) {
		fprintf(stderr, "Usage: %s [dialect [method]]\n", argv[0]);
		return 2;
	}
	for (dialect = SQLPARSER_DIALECT_POSTGRESQL; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
		size_t count = 0U, failed = 0U;
		current_dialect = (sqlparser_dialect_t)dialect;
		if (argc > 1 && strcmp(argv[1], sqlparser_dialect_name(current_dialect)) != 0) continue;
		for (method = 0; method < METHOD_COUNT; method++) {
			if (argc > 2 && strcmp(argv[2], methods[method]) != 0) continue;
			if (postgres_style() && (method == READ_NATIONAL || method == SET_NATIONAL || method == COPY_NATIONAL)) continue;
			for (index = 0U; index < sizeof(strings) / sizeof(strings[0]); index++) {
				failed += run_case(method, &strings[index]) != 0;
				count++;
			}
		}
		for (method = 0; method < 2; method++) {
			if (argc > 2 && strcmp(argv[2], method ? "batch-rollback" : "batch-source-order") != 0) continue;
			failed += check_batch(method) != 0;
			count++;
		}
		printf("string-literal dialect=%s cases=%zu passed=%zu failed=%zu\n",
			sqlparser_dialect_name(current_dialect), count, count - failed, failed);
		total += count;
		total_failed += failed;
	}
	printf("string-literal total=%zu passed=%zu failed=%zu\n", total, total - total_failed, total_failed);
	return total == 0U ? 2 : total_failed != 0U;
}
