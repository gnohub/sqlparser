#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_test_view_assert.h"

#define SQLPARSER_CASE_FIXTURE_PATH "./tests/cases/sql_batch_input.json"

static int fail_case(const char *case_name, const char *message)
{
	fprintf(stderr, "FAIL [%s]: %s\n", case_name, message);
	return 1;
}

static int fail_case_field(const char *case_name, const char *field_name, const char *expected)
{
	fprintf(stderr, "FAIL [%s]: missing %s value '%s'\n", case_name, field_name, expected);
	return 1;
}

static const char *json_string_or_null(json_t *value)
{
	return json_is_string(value) ? json_string_value(value) : NULL;
}

static int parse_dialect_or_default(json_t *value, sqlparser_dialect_t *out_dialect)
{
	const char *dialect;

	if (out_dialect == NULL) {
		return -1;
	}
	*out_dialect = SQLPARSER_DIALECT_POSTGRESQL;
	if (value == NULL) {
		return 0;
	}
	if (!json_is_string(value)) {
		return -1;
	}
	dialect = json_string_value(value);
	if (strcmp(dialect, "postgresql") == 0 || strcmp(dialect, "postgres") == 0 || strcmp(dialect, "pg") == 0) {
		*out_dialect = SQLPARSER_DIALECT_POSTGRESQL;
		return 0;
	}
	if (strcmp(dialect, "mysql") == 0) {
		*out_dialect = SQLPARSER_DIALECT_MYSQL;
		return 0;
	}
	if (strcmp(dialect, "oracle") == 0) {
		*out_dialect = SQLPARSER_DIALECT_ORACLE;
		return 0;
	}
	if (strcmp(dialect, "sqlserver") == 0 || strcmp(dialect, "mssql") == 0) {
		*out_dialect = SQLPARSER_DIALECT_SQLSERVER;
		return 0;
	}
	if (strcmp(dialect, "dameng") == 0 || strcmp(dialect, "dm") == 0) {
		*out_dialect = SQLPARSER_DIALECT_DAMENG;
		return 0;
	}
	return -1;
}

static int text_contains_array_values(
	const char *case_name,
	const char *field_name,
	const char *text,
	json_t *expected_array)
{
	size_t index;
	json_t *value;

	if (expected_array == NULL) {
		return 0;
	}
	if (!json_is_array(expected_array)) {
		return fail_case(case_name, "expected metadata must be an array");
	}
	json_array_foreach(expected_array, index, value) {
		const char *expected;

		expected = json_string_or_null(value);
		if (expected == NULL) {
			return fail_case(case_name, "expected metadata value must be a string");
		}
			if (strcmp(field_name, "tables") == 0) {
				if (!sqlparser_test_view_contains_table(text, expected)) {
					return fail_case_field(case_name, field_name, expected);
				}
				continue;
			}
			if (text == NULL || strstr(text, expected) == NULL) {
				return fail_case_field(case_name, field_name, expected);
			}
	}
	return 0;
}

static int verify_statement_types(
	const char *case_name,
	const sqlparser_handle_t *handle,
	json_t *expected_array)
{
	size_t index;
	json_t *value;

	if (expected_array == NULL) {
		return 0;
	}
	if (!json_is_array(expected_array)) {
		return fail_case(case_name, "statement_types must be an array");
	}
	json_array_foreach(expected_array, index, value) {
		const char *expected;
		size_t statement_index;
		int found;

		(void)index;
		expected = json_string_or_null(value);
		if (expected == NULL) {
			return fail_case(case_name, "statement_types value must be a string");
		}
		found = 0;
		for (statement_index = 0U;
		     statement_index < sqlparser_statement_count(handle);
		     statement_index++) {
			const char *actual;
			sqlparser_error_t error;

			actual = NULL;
			if (sqlparser_statement_node_name(handle, statement_index, &actual, &error) ==
			    SQLPARSER_STATUS_OK &&
			    actual != NULL &&
			    strcmp(actual, expected) == 0) {
				found = 1;
				break;
			}
		}
		if (!found) {
			return fail_case_field(case_name, "statement_types", expected);
		}
	}
	return 0;
}

static int verify_failure_case(
	const char *case_name,
	const char *sql,
	sqlparser_dialect_t dialect,
	json_t *expect_root)
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
	options.dialect = dialect;
	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_name, "parse was expected to fail");
	}
	if (handle != NULL) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_name, "failed parse should not return a handle");
	}
	value = json_object_get(expect_root, "error_code");
	if (json_is_integer(value) && status != (int)json_integer_value(value)) {
		return fail_case(case_name, "unexpected parse error code");
	}
	message_contains = json_string_or_null(json_object_get(expect_root, "error_message_contains"));
	if (message_contains != NULL && strstr(error.message, message_contains) == NULL) {
		return fail_case(case_name, "parse error message did not match expectation");
	}
	if (error.message[0] == '\0') {
		return fail_case(case_name, "parse failure should provide a message");
	}
	return 0;
}

static int verify_success_case(
	const char *case_name,
	const char *sql,
	sqlparser_dialect_t dialect,
	json_t *expect_root)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *view_json;
	char *deparse_sql;
	json_t *value;
	const char *deparse_contains;
	int status;

	handle = NULL;
	view_json = NULL;
	deparse_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;

	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [%s]: parse failed: %s\n", case_name, error.message);
		return 1;
	}
	if (handle == NULL) {
		return fail_case(case_name, "parse succeeded without handle");
	}
	if (strcmp(sqlparser_original_sql(handle), sql) != 0) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_name, "original SQL did not round-trip");
	}

	value = json_object_get(expect_root, "statement_count");
	if (json_is_integer(value) &&
	    sqlparser_statement_count(handle) != (size_t)json_integer_value(value)) {
		sqlparser_handle_destroy(handle);
		return fail_case(case_name, "statement count mismatch");
	}
	if (verify_statement_types(case_name, handle, json_object_get(expect_root, "statement_types")) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (status != SQLPARSER_STATUS_OK || view_json == NULL || view_json[0] == '\0') {
		sqlparser_handle_destroy(handle);
		return fail_case(case_name, "view JSON export failed");
	}
	if (text_contains_array_values(case_name, "keywords", view_json, json_object_get(expect_root, "keywords")) != 0 ||
	    text_contains_array_values(case_name, "tables", view_json, json_object_get(expect_root, "tables")) != 0 ||
	    text_contains_array_values(case_name, "selected_columns", view_json, json_object_get(expect_root, "selected_columns")) != 0 ||
	    text_contains_array_values(case_name, "join_columns", view_json, json_object_get(expect_root, "join_columns")) != 0 ||
	    text_contains_array_values(case_name, "where_columns", view_json, json_object_get(expect_root, "where_columns")) != 0 ||
	    text_contains_array_values(case_name, "filter_columns", view_json, json_object_get(expect_root, "filter_columns")) != 0 ||
	    text_contains_array_values(case_name, "insert_columns", view_json, json_object_get(expect_root, "insert_columns")) != 0 ||
	    text_contains_array_values(case_name, "update_columns", view_json, json_object_get(expect_root, "update_columns")) != 0 ||
	    text_contains_array_values(case_name, "all_referenced_columns", view_json, json_object_get(expect_root, "all_referenced_columns")) != 0 ||
	    sqlparser_test_verify_view_expectations(NULL, case_name, view_json, expect_root) != 0) {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	deparse_contains = json_string_or_null(json_object_get(expect_root, "deparse_contains"));
	status = sqlparser_deparse(handle, &deparse_sql, &error);
	if (status != SQLPARSER_STATUS_OK || deparse_sql == NULL || deparse_sql[0] == '\0') {
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return fail_case(case_name, "deparse export failed");
	}
	if (deparse_contains != NULL && strstr(deparse_sql, deparse_contains) == NULL) {
		sqlparser_string_free(deparse_sql);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return fail_case_field(case_name, "deparse_contains", deparse_contains);
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

	root = json_load_file(SQLPARSER_CASE_FIXTURE_PATH, 0, &error);
	if (root == NULL) {
		fprintf(stderr, "FAIL: unable to load fixture %s: %s\n", SQLPARSER_CASE_FIXTURE_PATH, error.text);
		return 1;
	}
	items = json_is_array(root) ? root : json_object_get(root, "items");
	if (!json_is_array(items)) {
		json_decref(root);
		fprintf(stderr, "FAIL: fixture does not contain an items array\n");
		return 1;
	}

	json_array_foreach(items, index, item) {
		json_t *expect_root;
		json_t *ok_value;
		const char *case_name;
		const char *sql;
		sqlparser_dialect_t dialect;
		int expected_ok;

		case_name = json_string_or_null(json_object_get(item, "name"));
		sql = json_string_or_null(json_object_get(item, "sql"));
		expect_root = json_object_get(item, "expect");
		if (case_name == NULL || sql == NULL || !json_is_object(expect_root)) {
			json_decref(root);
			fprintf(stderr, "FAIL: case %lu is missing name/sql/expect\n", (unsigned long)index);
			return 1;
		}
		if (parse_dialect_or_default(json_object_get(item, "dialect"), &dialect) != 0) {
			json_decref(root);
			fprintf(stderr, "FAIL: case %lu has invalid dialect\n", (unsigned long)index);
			return 1;
		}
		ok_value = json_object_get(expect_root, "ok");
		expected_ok = ok_value == NULL ? 1 : json_is_true(ok_value);
		if (expected_ok) {
			if (verify_success_case(case_name, sql, dialect, expect_root) != 0) {
				json_decref(root);
				return 1;
			}
		} else if (verify_failure_case(case_name, sql, dialect, expect_root) != 0) {
			json_decref(root);
			return 1;
		}
	}

	json_decref(root);
	return 0;
}
