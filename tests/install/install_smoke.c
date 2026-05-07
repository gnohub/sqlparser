#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

static int expect_true(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		return 1;
	}

	return 0;
}

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *model_json;
	char *deparsed_sql;
	int rc;

	sql = "UPDATE public.users SET name = upper(name), updated_at = DEFAULT WHERE id = 1";
	handle = NULL;
	model_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "installed parse should succeed") != 0) {
		fprintf(stderr, "parse error: %s\n", error.message);
		return 1;
	}

	if (expect_true(handle != NULL, "installed parse should return a handle") != 0) {
		return 1;
	}

	if (expect_true(strcmp(sqlparser_version_string(), "0.2.0-dev") == 0, "version string should be exported") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strcmp(sqlparser_model_schema_string(), "sqlparser.model/v1") == 0, "model schema should be exported") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_model_json(handle, 0, &model_json, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "installed model export should succeed") != 0 ||
	    expect_true(model_json != NULL, "installed model export should return text") != 0 ||
	    expect_true(strstr(model_json, "\"schema\":\"sqlparser.model/v1\"") != NULL, "model export should carry schema") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_update_set_assignment_sql(handle, 0U, 1U, "clock_timestamp()", &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "installed assignment rewrite should succeed") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "installed deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "clock_timestamp()") != NULL, "rewritten SQL should contain clock_timestamp()") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(model_json);
	sqlparser_handle_destroy(handle);
	return 0;
}
