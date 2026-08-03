#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

typedef struct {
	const char *name;
	const char *sql;
	const char *selector;
	const char *expected_patched_sql;
} sqlparser_dynamic_execute_case_t;

static int sqlparser_dynamic_execute_positive(
	sqlparser_dialect_t dialect,
	const sqlparser_dynamic_execute_case_t *test_case)
{
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patch;
	char *deparsed_sql;
	char *reparsed_sql;
	char *view_json;
	sqlparser_status_t status;
	int failed;

	handle = NULL;
	reparsed = NULL;
	deparsed_sql = NULL;
	reparsed_sql = NULL;
	view_json = NULL;
	failed = 0;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(
		test_case->sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK || handle == NULL) {
		fprintf(
			stderr,
			"FAIL [%s %s]: parse failed: %s\n",
			sqlparser_dialect_name(dialect),
			test_case->name,
			error.message);
		failed = 1;
		goto done;
	}
	status = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (status != SQLPARSER_STATUS_OK || deparsed_sql == NULL ||
	    strcmp(deparsed_sql, test_case->sql) != 0) {
		fprintf(
			stderr,
			"FAIL [%s %s]: generation-0 deparse mismatch: %s\n",
			sqlparser_dialect_name(dialect),
			test_case->name,
			deparsed_sql != NULL ? deparsed_sql : error.message);
		failed = 1;
		goto done;
	}
	status = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (status != SQLPARSER_STATUS_OK || view_json == NULL ||
	    strstr(view_json, "\"keyword\":\"execute\"") == NULL ||
	    strstr(view_json, "\"session\"") != NULL) {
		fprintf(
			stderr,
			"FAIL [%s %s]: execute View is invalid: %s\n",
			sqlparser_dialect_name(dialect),
			test_case->name,
			view_json != NULL ? view_json : error.message);
		failed = 1;
		goto done;
	}
	if (test_case->selector == NULL) {
		goto done;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = test_case->selector;
	patch.sql = "42";
	patch_list.items = &patch;
	patch_list.count = 1U;
	memset(&error, 0, sizeof(error));
	status = sqlparser_apply_patch(handle, &patch_list, &error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(handle, &deparsed_sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || deparsed_sql == NULL ||
	    strcmp(deparsed_sql, test_case->expected_patched_sql) != 0) {
		fprintf(
			stderr,
			"FAIL [%s %s]: patched deparse mismatch: %s\n",
			sqlparser_dialect_name(dialect),
			test_case->name,
			deparsed_sql != NULL ? deparsed_sql : error.message);
		failed = 1;
		goto done;
	}
	memset(&error, 0, sizeof(error));
	status = sqlparser_parse_with_options(
		deparsed_sql, &options, &reparsed, &error);
	if (status == SQLPARSER_STATUS_OK && reparsed != NULL) {
		status = sqlparser_deparse(reparsed, &reparsed_sql, &error);
	}
	if (status != SQLPARSER_STATUS_OK || reparsed_sql == NULL ||
	    strcmp(reparsed_sql, deparsed_sql) != 0) {
		fprintf(
			stderr,
			"FAIL [%s %s]: patched SQL closure failed: %s\n",
			sqlparser_dialect_name(dialect),
			test_case->name,
			reparsed_sql != NULL ? reparsed_sql : error.message);
		failed = 1;
	}

done:
	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(reparsed_sql);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	sqlparser_handle_destroy(reparsed);
	return failed;
}

static int sqlparser_dynamic_execute_negative(
	sqlparser_dialect_t dialect,
	const char *name,
	const char *sql)
{
	static const char expected_error[] =
		"unsupported SQL Server syntax: batch or procedure execution";
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_status_t status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_UNSUPPORTED || handle != NULL ||
	    strcmp(error.message, expected_error) != 0) {
		fprintf(
			stderr,
			"FAIL [%s %s]: unsupported boundary changed: status=%d message=%s\n",
			sqlparser_dialect_name(dialect),
			name,
			status,
			error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	return 0;
}

int main(void)
{
	static const sqlparser_dynamic_execute_case_t positive_cases[] = {
		{
			"standalone-escaped-string",
			"EXECUTE(N'SELECT ''x'' AS [Value]')",
			NULL,
			NULL
		},
		{
			"standalone-comment-gaps",
			"EXECUTE/*token-gap*/( /*value*/ N'SELECT 1' /*close*/ )",
			NULL,
			NULL
		},
		{
			"if-short-exec",
			"IF @run = 1 EXEC(N'SELECT 1') ELSE SELECT 0",
			"stmt[0].value[1]",
			"IF 42 = 1 EXEC(N'SELECT 1') ELSE SELECT 0"
		},
		{
			"inner-semicolon",
			"IF @run = 1 EXECUTE(N'SELECT 1; SELECT 2') ELSE SELECT 0; SELECT 3",
			"stmt[0].value[1]",
			"IF 42 = 1 EXECUTE(N'SELECT 1; SELECT 2') ELSE SELECT 0; SELECT 3"
		}
	};
	static const struct {
		const char *name;
		const char *sql;
	} negative_cases[] = {
		{
			"procedure-exec",
			"EXEC [dbo].[rebuild_user_cache]"
		},
		{
			"procedure-exec-in-if",
			"IF @run = 1 EXEC [dbo].[rebuild_user_cache] ELSE SELECT 0"
		},
		{
			"variable-dynamic-exec",
			"IF @run = 1 EXECUTE(@sql) ELSE SELECT 0"
		},
		{
			"concatenated-dynamic-exec",
			"EXECUTE(N'SELECT ' + @sql)"
		},
		{
			"nested-parentheses",
			"EXECUTE((N'SELECT 1'))"
		},
		{
			"later-procedure-execute",
			"SELECT 1; EXECUTE rebuild_user_cache"
		},
		{
			"dynamic-then-procedure-execute",
			"EXECUTE(N'SELECT 1'); EXECUTE rebuild_user_cache"
		}
	};
	static const sqlparser_dialect_t dialects[] = {
		SQLPARSER_DIALECT_SQLSERVER,
		SQLPARSER_DIALECT_VASTBASE_SQLSERVER
	};
	size_t case_index;
	size_t dialect_index;
	int failed;

	failed = 0;
	for (dialect_index = 0U;
	     dialect_index < sizeof(dialects) / sizeof(dialects[0]);
	     dialect_index++) {
		for (case_index = 0U;
		     case_index < sizeof(positive_cases) / sizeof(positive_cases[0]);
		     case_index++) {
			failed |= sqlparser_dynamic_execute_positive(
				dialects[dialect_index],
				&positive_cases[case_index]);
		}
		for (case_index = 0U;
		     case_index < sizeof(negative_cases) / sizeof(negative_cases[0]);
		     case_index++) {
			failed |= sqlparser_dynamic_execute_negative(
				dialects[dialect_index],
				negative_cases[case_index].name,
				negative_cases[case_index].sql);
		}
	}
	if (failed == 0) {
		printf("sqlserver dynamic execute tests passed\n");
	}
	return failed;
}
