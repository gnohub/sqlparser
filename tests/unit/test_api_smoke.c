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
	char *json_text;
	char *pretty_json_text;
	char *summary_json_text;
	char *deparsed_sql;
	int rc;

	sql = "SELECT u.id, u.name, o.order_no FROM public.users u JOIN public.orders o ON u.id = o.user_id WHERE o.status = 'paid'";
	handle = NULL;
	json_text = NULL;
	pretty_json_text = NULL;
	summary_json_text = NULL;
	deparsed_sql = NULL;

	rc = sqlparser_parse(sql, &handle, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "parse should succeed") != 0) {
		fprintf(stderr, "parse error: %s\n", error.message);
		return 1;
	}

	if (expect_true(handle != NULL, "handle should not be NULL") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(sqlparser_statement_count(handle) == 1U, "statement count should be 1") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strcmp(sqlparser_original_sql(handle), sql) == 0, "original sql should match") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_parse_tree_json(handle, 0, &json_text, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "compact JSON export should succeed") != 0) {
		fprintf(stderr, "json error: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(json_text, "SelectStmt") != NULL, "compact JSON should include SelectStmt") != 0) {
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_parse_tree_json(handle, 1, &pretty_json_text, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "pretty JSON export should succeed") != 0) {
		fprintf(stderr, "pretty json error: %s\n", error.message);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strchr(pretty_json_text, '\n') != NULL, "pretty JSON should contain newlines") != 0) {
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_export_summary_json(handle, 1, &summary_json_text, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "summary JSON export should succeed") != 0) {
		fprintf(stderr, "summary error: %s\n", error.message);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "SelectStmt") != NULL, "summary JSON should include SelectStmt") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "users") != NULL, "summary JSON should include table name") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "\"keywords\"") != NULL, "summary JSON should include keywords") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "\"select\"") != NULL, "summary JSON should include select keyword") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "\"selected_columns\"") != NULL, "summary JSON should include selected_columns") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "\"join_columns\"") != NULL, "summary JSON should include join_columns") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "\"where_columns\"") != NULL, "summary JSON should include where_columns") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "\"all_referenced_columns\"") != NULL, "summary JSON should include all_referenced_columns") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "order_no") != NULL, "summary JSON should include selected join target column") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "user_id") != NULL, "summary JSON should include join predicate column") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(summary_json_text, "status") != NULL, "summary JSON should include where predicate column") != 0) {
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	rc = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_true(rc == SQLPARSER_STATUS_OK, "deparse should succeed") != 0) {
		fprintf(stderr, "deparse error: %s\n", error.message);
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (expect_true(strstr(deparsed_sql, "SELECT u.id, u.name, o.order_no") != NULL, "deparsed SQL should contain target list") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(summary_json_text);
		sqlparser_string_free(pretty_json_text);
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(summary_json_text);
	sqlparser_string_free(pretty_json_text);
	sqlparser_string_free(json_text);
	sqlparser_handle_destroy(handle);

	return 0;
}
