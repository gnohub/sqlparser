#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_test_view_assert.h"

static const char *const SQLPARSER_SQLSERVER_CASE_FIXTURE_PATHS[] = {
	"./tests/cases/sqlserver_dialect_input.json",
	"./tests/cases/sqlserver_hook_coverage_input.json"
};

static int fail_case(const char *case_id, const char *case_name, const char *message)
{
	fprintf(stderr, "FAIL [%s %s]: %s\n", case_id != NULL ? case_id : "-", case_name, message);
	return 1;
}

static int fail_case_field(
	const char *case_id,
	const char *case_name,
	const char *field_name,
	const char *expected)
{
	fprintf(stderr,
	        "FAIL [%s %s]: missing %s value '%s'\n",
	        case_id != NULL ? case_id : "-",
	        case_name,
	        field_name,
	        expected);
	return 1;
}

static const char *json_string_or_null(json_t *value)
{
	if (!json_is_string(value)) {
		return NULL;
	}

	return json_string_value(value);
}

static int verify_view_text_array(
	const char *case_id,
	const char *case_name,
	const char *view_json,
	const char *field_name,
	json_t *expected_array)
{
	size_t index;
	json_t *value;

	if (expected_array == NULL) {
		return 0;
	}
	if (!json_is_array(expected_array)) {
		return fail_case(case_id, case_name, "expected string-array metadata must be an array");
	}

	json_array_foreach(expected_array, index, value) {
		const char *expected_text;

		expected_text = json_string_or_null(value);
		if (expected_text == NULL) {
			return fail_case(case_id, case_name, "expected string-array value must be a string");
		}
		if (strcmp(field_name, "tables") == 0) {
			if (!sqlparser_test_view_contains_table(view_json, expected_text)) {
				return fail_case_field(case_id, case_name, field_name, expected_text);
			}
			continue;
		}
		if (view_json == NULL || strstr(view_json, expected_text) == NULL) {
			return fail_case_field(case_id, case_name, field_name, expected_text);
		}
	}

	return 0;
}

static int verify_statement_types(
	const char *case_id,
	const char *case_name,
	const sqlparser_handle_t *handle,
	json_t *expected_array)
{
	size_t index;
	json_t *value;
	sqlparser_error_t error;

	if (expected_array == NULL) {
		return 0;
	}
	if (!json_is_array(expected_array)) {
		return fail_case(case_id, case_name, "expected statement_types metadata must be an array");
	}
	memset(&error, 0, sizeof(error));

	json_array_foreach(expected_array, index, value) {
		const char *expected_text;
		size_t statement_index;
		int found;

		expected_text = json_string_or_null(value);
		if (expected_text == NULL) {
			return fail_case(case_id, case_name, "expected statement_types value must be a string");
		}

		found = 0;
		for (statement_index = 0U;
		     statement_index < sqlparser_statement_count(handle);
		     statement_index++) {
			const char *actual_text;

			actual_text = NULL;
			if (sqlparser_statement_node_name(handle, statement_index, &actual_text, &error) ==
			    SQLPARSER_STATUS_OK &&
			    actual_text != NULL &&
			    strcmp(actual_text, expected_text) == 0) {
				found = 1;
				break;
			}
		}
		if (!found) {
			return fail_case_field(case_id, case_name, "statement_types", expected_text);
		}
	}

	return 0;
}

static int verify_text_contains(
	const char *case_id,
	const char *case_name,
	const char *text,
	const char *field_name,
	json_t *expected_value)
{
	const char *expected;

	expected = json_string_or_null(expected_value);
	if (expected == NULL) {
		return 0;
	}
	if (text == NULL || strstr(text, expected) == NULL) {
		return fail_case_field(case_id, case_name, field_name, expected);
	}
	return 0;
}

static int verify_text_not_contains(
	const char *case_id,
	const char *case_name,
	const char *text,
	const char *field_name,
	json_t *expected_value)
{
	const char *expected;

	expected = json_string_or_null(expected_value);
	if (expected == NULL) {
		return 0;
	}
	if (text != NULL && strstr(text, expected) != NULL) {
		return fail_case_field(case_id, case_name, field_name, expected);
	}
	return 0;
}

static int verify_failure_case(
	const char *case_id,
	const char *case_name,
	const char *sql,
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
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;

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

static int verify_success_case(
	const char *case_id,
	const char *case_name,
	const char *sql,
	json_t *expect_root)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *view_json;
	char *deparse_sql;
	json_t *value;
	int status;

	handle = NULL;
	view_json = NULL;
	deparse_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;

	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [%s %s]: parse failed: %s\n", case_id, case_name, error.message);
		return 1;
	}
	if (handle == NULL) {
		return fail_case(case_id, case_name, "parse succeeded without handle");
	}
	if (sqlparser_handle_dialect(handle) != SQLPARSER_DIALECT_SQLSERVER) {
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

	status = sqlparser_export_view_json(handle, 0, &view_json, &error);
	if (status != SQLPARSER_STATUS_OK || view_json == NULL || view_json[0] == '\0') {
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "view JSON export failed");
	}

	if (verify_statement_types(
		    case_id,
		    case_name,
		    handle,
		    json_object_get(expect_root, "statement_types")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "cte_names",
		    json_object_get(expect_root, "cte_names")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "keywords",
		    json_object_get(expect_root, "keywords")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "tables",
		    json_object_get(expect_root, "tables")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "selected_columns",
		    json_object_get(expect_root, "selected_columns")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "join_columns",
		    json_object_get(expect_root, "join_columns")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "where_columns",
		    json_object_get(expect_root, "where_columns")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "filter_columns",
		    json_object_get(expect_root, "filter_columns")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "insert_columns",
		    json_object_get(expect_root, "insert_columns")) != 0) {
		goto fail;
	}
	if (verify_view_text_array(
		    case_id,
		    case_name,
		    view_json,
		    "update_columns",
		    json_object_get(expect_root, "update_columns")) != 0) {
		goto fail;
	}
	if (sqlparser_test_verify_view_expectations(case_id, case_name, view_json, expect_root) != 0) {
		goto fail;
	}

	status = sqlparser_deparse(handle, &deparse_sql, &error);
	if (status != SQLPARSER_STATUS_OK || deparse_sql == NULL || deparse_sql[0] == '\0') {
		goto fail;
	}
	if (verify_text_contains(
		    case_id,
		    case_name,
		    deparse_sql,
		    "deparse_contains",
		    json_object_get(expect_root, "deparse_contains")) != 0 ||
	    verify_text_not_contains(
		    case_id,
		    case_name,
		    deparse_sql,
		    "deparse_not_contains",
		    json_object_get(expect_root, "deparse_not_contains")) != 0 ||
	    verify_text_contains(
		    case_id,
		    case_name,
		    view_json,
		    "view_contains",
		    json_object_get(expect_root, "view_contains")) != 0 ||
	    verify_text_not_contains(
		    case_id,
		    case_name,
		    view_json,
		    "view_not_contains",
		    json_object_get(expect_root, "view_not_contains")) != 0) {
		goto fail;
	}

	sqlparser_string_free(deparse_sql);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;

fail:
	sqlparser_string_free(deparse_sql);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int expect_contains_text(const char *case_id, const char *case_name, const char *text, const char *expected)
{
	if (text == NULL || expected == NULL || strstr(text, expected) == NULL) {
		return fail_case_field(case_id, case_name, "text", expected);
	}
	return 0;
}

static int expect_not_contains_text(const char *case_id, const char *case_name, const char *text, const char *expected)
{
	if (text != NULL && expected != NULL && strstr(text, expected) != NULL) {
		return fail_case(case_id, case_name, "text contained an internal SQL fragment");
	}
	return 0;
}

static int verify_sqlserver_fragment_rewrite_paths(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *fragment_sql;
	char *deparse_sql;
	int status;

	handle = NULL;
	fragment_sql = NULL;
	deparse_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;

	status = sqlparser_parse_with_options(
		"UPDATE [dbo].[users] SET [name] = @name WHERE [id] = @id",
		&options,
		&handle,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [SF001 sqlserver-fragment-update]: parse failed: %s\n", error.message);
		return 1;
	}

	status = sqlparser_update_assignment_sql(handle, 0U, 0U, &fragment_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_contains_text("SF001", "sqlserver-fragment-update", fragment_sql, "@name") != 0 ||
	    expect_not_contains_text("SF001", "sqlserver-fragment-update", fragment_sql, "$1") != 0) {
		sqlparser_string_free(fragment_sql);
		sqlparser_handle_destroy(handle);
		return fail_case("SF001", "sqlserver-fragment-update", "assignment fragment read failed");
	}
	sqlparser_string_free(fragment_sql);
	fragment_sql = NULL;

	status = sqlparser_update_set_assignment_sql(handle, 0U, 0U, "@new_name", &error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(handle);
		return fail_case("SF001", "sqlserver-fragment-update", "assignment fragment rewrite failed");
	}
	status = sqlparser_deparse(handle, &deparse_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_contains_text("SF001", "sqlserver-fragment-update", deparse_sql, "@new_name") != 0 ||
	    expect_not_contains_text("SF001", "sqlserver-fragment-update", deparse_sql, "$") != 0) {
		sqlparser_string_free(deparse_sql);
		sqlparser_handle_destroy(handle);
		return fail_case("SF001", "sqlserver-fragment-update", "deparse after rewrite failed");
	}

	sqlparser_string_free(deparse_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int run_case(json_t *item)
{
	json_t *expect_root;
	json_t *ok_value;
	const char *case_id;
	const char *case_name;
	const char *sql;
	int expect_ok;

	case_id = json_string_or_null(json_object_get(item, "id"));
	case_name = json_string_or_null(json_object_get(item, "name"));
	sql = json_string_or_null(json_object_get(item, "sql"));
	expect_root = json_object_get(item, "expect");
	if (case_id == NULL || case_name == NULL || sql == NULL || !json_is_object(expect_root)) {
		return fail_case(case_id, case_name != NULL ? case_name : "-", "case is missing id/name/sql/expect");
	}

	ok_value = json_object_get(expect_root, "ok");
	expect_ok = !json_is_false(ok_value);
	if (expect_ok) {
		return verify_success_case(case_id, case_name, sql, expect_root);
	}

	return verify_failure_case(case_id, case_name, sql, expect_root);
}

static int run_fixture_file(const char *fixture_path)
{
	json_error_t error;
	json_t *root;
	json_t *items;
	json_t *item;
	size_t index;

	memset(&error, 0, sizeof(error));
	root = json_load_file(fixture_path, 0, &error);
	if (root == NULL) {
		fprintf(stderr, "FAIL: unable to load fixture %s: %s\n", fixture_path, error.text);
		return 1;
	}

	items = json_object_get(root, "items");
	if (!json_is_array(items)) {
		json_decref(root);
		fprintf(stderr, "FAIL: fixture does not contain an items array\n");
		return 1;
	}

	json_array_foreach(items, index, item) {
		if (!json_is_object(item)) {
			json_decref(root);
			fprintf(stderr, "FAIL: case %lu is not an object\n", (unsigned long)index);
			return 1;
		}
		if (run_case(item) != 0) {
			json_decref(root);
			return 1;
		}
	}

	json_decref(root);
	return 0;
}

int main(void)
{
	size_t fixture_index;

	for (fixture_index = 0U;
	     fixture_index < sizeof(SQLPARSER_SQLSERVER_CASE_FIXTURE_PATHS) /
			     sizeof(SQLPARSER_SQLSERVER_CASE_FIXTURE_PATHS[0]);
	     fixture_index++) {
		if (run_fixture_file(SQLPARSER_SQLSERVER_CASE_FIXTURE_PATHS[fixture_index]) != 0) {
			return 1;
		}
	}
	if (verify_sqlserver_fragment_rewrite_paths() != 0) {
		return 1;
	}
	return 0;
}
