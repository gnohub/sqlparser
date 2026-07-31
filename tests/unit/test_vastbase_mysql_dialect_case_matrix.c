#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_test_view_assert.h"

#define SQLPARSER_VASTBASE_MYSQL_CASE_FIXTURE_PATH "./tests/cases/vastbase_mysql_dialect_input.json"

static int fail_case(const char *case_id, const char *case_name, const char *message)
{
	fprintf(stderr, "FAIL [%s %s]: %s\n", case_id != NULL ? case_id : "-", case_name, message);
	return 1;
}

static const char *json_string_or_null(json_t *value)
{
	return json_is_string(value) ? json_string_value(value) : NULL;
}

static int verify_failure_case(const char *case_id, const char *case_name, const char *sql, json_t *expect_root)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	json_t *value;
	const char *message_contains;
	int status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_VASTBASE_MYSQL;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "parse was expected to fail");
	}
	if (handle != NULL) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "failed parse should not return a handle");
	}
	value = json_object_get(expect_root, "error_code");
	if (json_is_integer(value) && status != (int)json_integer_value(value)) {
		return fail_case(case_id, case_name, "unexpected parse error code");
	}
	message_contains = json_string_or_null(json_object_get(expect_root, "error_message_contains"));
	if (message_contains != NULL && strstr(error.message, message_contains) == NULL) {
		return fail_case(case_id, case_name, "parse error message did not match expectation");
	}
	if (error.message[0] == '\0') {
		return fail_case(case_id, case_name, "parse failure should provide a message");
	}
	return 0;
}

static int verify_success_case(const char *case_id, const char *case_name, const char *sql, json_t *expect_root)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *view_json;
	char *deparse_sql;
	json_t *value;
	int regression_failed;
	int status;

	handle = NULL;
	view_json = NULL;
	deparse_sql = NULL;
	regression_failed = 0;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_VASTBASE_MYSQL;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [%s %s]: parse failed: %s\n", case_id, case_name, error.message);
		return 1;
	}
	if (handle == NULL) {
		return fail_case(case_id, case_name, "parse succeeded without handle");
	}
	status = sqlparser_deparse(handle, &deparse_sql, &error);
	if (status != SQLPARSER_STATUS_OK || deparse_sql == NULL || deparse_sql[0] == '\0') {
		(void)fail_case(case_id, case_name, "deparse failed");
		regression_failed = 1;
	}
	if (sqlparser_test_verify_exact_deparse(
		    case_id,
		    case_name,
		    sql,
		    deparse_sql) != 0) {
		regression_failed = 1;
	}
	if (sqlparser_test_verify_ast_identifier_spelling(
		    case_id,
		    case_name,
		    handle) != 0) {
		regression_failed = 1;
	}
	if (regression_failed) {
		sqlparser_string_free(deparse_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparse_sql);
	deparse_sql = NULL;

	if (sqlparser_handle_dialect(handle) != SQLPARSER_DIALECT_VASTBASE_MYSQL) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "handle dialect mismatch");
	}
	if (strcmp(sqlparser_original_sql(handle), sql) != 0) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "original SQL was not preserved");
	}
	value = json_object_get(expect_root, "statement_count");
	if (json_is_integer(value) &&
	    sqlparser_statement_count(handle) != (size_t)json_integer_value(value)) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "statement count mismatch");
	}
	if (sqlparser_test_verify_statement_types(case_id, case_name, handle, json_object_get(expect_root, "statement_types")) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	status = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (status != SQLPARSER_STATUS_OK || view_json == NULL || view_json[0] == '\0') {
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "view JSON export failed");
	}
	if (sqlparser_test_verify_view_expectations(case_id, case_name, handle, view_json, expect_root) != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (sqlparser_test_verify_merge_assignment_mutations(
		    case_id,
		    case_name,
		    handle) != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparse_sql);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

int main(void)
{
	json_error_t error;
	json_t *root;
	json_t *items;
	size_t index;
	json_t *item;
	int failed;

	failed = 0;
	if (sqlparser_test_verify_case_schema_gate(
		    1, 0, 0) != 0) {
		return 1;
	}
	root = json_load_file(SQLPARSER_VASTBASE_MYSQL_CASE_FIXTURE_PATH, 0, &error);
	if (root == NULL) {
		fprintf(stderr, "FAIL: unable to load fixture %s: %s\n", SQLPARSER_VASTBASE_MYSQL_CASE_FIXTURE_PATH, error.text);
		return 1;
	}
	items = json_object_get(root, "items");
	if (!json_is_array(items)) {
		json_decref(root);
		fprintf(stderr, "FAIL: fixture does not contain an items array\n");
		return 1;
	}
	json_array_foreach(items, index, item) {
		json_t *expect_root;
		const char *case_id;
		const char *case_name;
		const char *sql;
		int expected_ok;

		if (sqlparser_test_validate_case_schema(
			    item,
			    1,
			    0,
			    0,
			    &expected_ok) != 0) {
			failed = 1;
			continue;
		}
		case_id = json_string_or_null(json_object_get(item, "id"));
		case_name = json_string_or_null(json_object_get(item, "name"));
		sql = json_string_or_null(json_object_get(item, "sql"));
		expect_root = json_object_get(item, "expect");
		if (expected_ok) {
			if (verify_success_case(case_id, case_name, sql, expect_root) != 0) {
				failed = 1;
			}
		} else if (verify_failure_case(case_id, case_name, sql, expect_root) != 0) {
			failed = 1;
		}
	}
	json_decref(root);
	return failed;
}
