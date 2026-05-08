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
	char *view_json;
	char *pretty_view_json;
	char *deparsed_sql;
	int rc;

	sql = "SELECT u.id, u.name, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id WHERE o.status = 'paid'";
	handle = NULL;
	view_json = NULL;
	pretty_view_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "parse should succeed") != 0) {
		fprintf(stderr, "parse error: %s\n", error.message);
		return 1;
	}
	if (expect_true(handle != NULL, "handle should not be NULL") != 0 ||
	    expect_true(sqlparser_statement_count(handle) == 1U, "statement count should be 1") != 0 ||
	    expect_true(strcmp(sqlparser_original_sql(handle), sql) == 0, "original sql should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "compact view JSON export should succeed") != 0 ||
	    expect_true(view_json != NULL, "compact view JSON should return text") != 0 ||
	    expect_true(strstr(view_json, "\"statements\"") != NULL, "view JSON should include statements") != 0 ||
	    expect_true(strstr(view_json, "\"table\":\"users\"") != NULL, "view JSON should include users table") != 0 ||
	    expect_true(strstr(view_json, "\"table\":\"orders\"") != NULL, "view JSON should include orders table") != 0 ||
	    expect_true(strstr(view_json, "\"name\":\"order_no\"") != NULL, "view JSON should include selected join target column") != 0 ||
	    expect_true(strstr(view_json, "\"name\":\"user_id\"") != NULL, "view JSON should include join predicate column") != 0 ||
	    expect_true(strstr(view_json, "\"name\":\"status\"") != NULL, "view JSON should include where predicate column") != 0 ||
	    expect_true(strstr(view_json, "\"keyword\":\"where\"") != NULL, "view JSON should include where keyword") != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_view_json(handle, 1, &pretty_view_json, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "pretty view JSON export should succeed") != 0 ||
	    expect_true(pretty_view_json != NULL, "pretty view JSON should return text") != 0 ||
	    expect_true(strchr(pretty_view_json, '\n') != NULL, "pretty view JSON should contain newlines") != 0) {
		sqlparser_string_free(pretty_view_json);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "deparse should succeed") != 0 ||
	    expect_true(strstr(deparsed_sql, "SELECT u.id, u.name, o.order_no") != NULL, "deparsed SQL should contain target list") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(pretty_view_json);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(pretty_view_json);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}
