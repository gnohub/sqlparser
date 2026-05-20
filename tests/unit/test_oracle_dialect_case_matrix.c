#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_test_view_assert.h"

#define SQLPARSER_ORACLE_CASE_FIXTURE_PATH "./tests/cases/oracle_dialect_input.json"

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
		return fail_case(case_id, case_name, "expected object-array metadata must be an array");
	}

	json_array_foreach(expected_array, index, value) {
		const char *expected_text;

		expected_text = json_string_or_null(value);
		if (expected_text == NULL) {
			return fail_case(case_id, case_name, "expected object-array value must be a string");
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
	options.dialect = SQLPARSER_DIALECT_ORACLE;

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
	const char *deparse_contains;
	const char *view_contains;
	const char *view_not_contains;
	int status;

	handle = NULL;
	view_json = NULL;
	deparse_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	status = sqlparser_parse_with_options(sql, &options, &handle, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [%s %s]: parse failed: %s\n", case_id, case_name, error.message);
		return 1;
	}
	if (handle == NULL) {
		return fail_case(case_id, case_name, "parse succeeded without handle");
	}
	if (sqlparser_handle_dialect(handle) != SQLPARSER_DIALECT_ORACLE) {
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

	deparse_contains = json_string_or_null(json_object_get(expect_root, "deparse_contains"));
	status = sqlparser_deparse(handle, &deparse_sql, &error);
	if (status != SQLPARSER_STATUS_OK || deparse_sql == NULL || deparse_sql[0] == '\0') {
		(void)fail_case(case_id, case_name, "deparse failed");
		goto fail;
	}
	if (deparse_contains != NULL && strstr(deparse_sql, deparse_contains) == NULL) {
		(void)fail_case_field(case_id, case_name, "deparse_contains", deparse_contains);
		goto fail;
	}

	view_contains = json_string_or_null(json_object_get(expect_root, "view_contains"));
	view_not_contains = json_string_or_null(json_object_get(expect_root, "view_not_contains"));
	if (view_contains != NULL && strstr(view_json, view_contains) == NULL) {
		(void)fail_case_field(case_id, case_name, "view_contains", view_contains);
		goto fail;
	}
	if (view_not_contains != NULL && strstr(view_json, view_not_contains) != NULL) {
		(void)fail_case_field(case_id, case_name, "view_not_contains", view_not_contains);
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

static int expect_deparse_contains(
	const char *case_id,
	const char *case_name,
	sqlparser_handle_t *handle,
	const char *expected)
{
	sqlparser_error_t error;
	char *sql;
	int status;

	memset(&error, 0, sizeof(error));
	sql = NULL;
	status = sqlparser_deparse(handle, &sql, &error);
	if (status != SQLPARSER_STATUS_OK || sql == NULL) {
		sqlparser_string_free(sql);
		return fail_case(case_id, case_name, "deparse failed after rewrite");
	}
	if (strstr(sql, expected) == NULL) {
		sqlparser_string_free(sql);
		return fail_case_field(case_id, case_name, "deparse", expected);
	}
	if (strstr(sql, "$") != NULL) {
		sqlparser_string_free(sql);
		return fail_case(case_id, case_name, "deparse leaked internal parameter syntax");
	}

	sqlparser_string_free(sql);
	return 0;
}

static int expect_public_fragment(
	const char *case_id,
	const char *case_name,
	const char *field_name,
	const char *sql,
	const char *expected)
{
	if (sql == NULL || strstr(sql, expected) == NULL) {
		return fail_case_field(case_id, case_name, field_name, expected);
	}
	if (strstr(sql, "$") != NULL) {
		return fail_case(case_id, case_name, "SQL fragment leaked internal parameter syntax");
	}

	return 0;
}

static int verify_oracle_fragment_rewrite_paths(void)
{
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patches;
	char *fragment_sql;
	char *view_json;
	int status;

	memset(&error, 0, sizeof(error));
	memset(&patch, 0, sizeof(patch));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	fragment_sql = NULL;
	view_json = NULL;

	handle = NULL;
	status = sqlparser_parse_with_options(
		"UPDATE users SET name = :name WHERE id = :id",
		&options,
		&handle,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [oracle-fragment-update]: parse failed: %s\n", error.message);
		return 1;
	}
	status = sqlparser_update_assignment_sql(handle, 0U, 0U, &fragment_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_public_fragment("OF001", "oracle-fragment-update", "assignment_sql", fragment_sql, ":name") != 0) {
		fprintf(stderr, "FAIL [oracle-fragment-update]: public fragment read failed: %s\n", error.message);
		sqlparser_string_free(fragment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(fragment_sql);
	fragment_sql = NULL;

	status = sqlparser_update_set_assignment_sql(handle, 0U, 0U, ":new_name", &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [oracle-fragment-update]: rewrite failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains("OF001", "oracle-fragment-update", handle, ":new_name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	status = sqlparser_update_set_assignment_sql(handle, 0U, 0U, ":second_name", &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [oracle-fragment-update]: second rewrite failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains("OF001", "oracle-fragment-update", handle, ":second_name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	status = sqlparser_parse_with_options(
		"INSERT INTO users (id, name) VALUES (:id, 'bob')",
		&options,
		&handle,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [oracle-fragment-insert]: parse failed: %s\n", error.message);
		return 1;
	}
	status = sqlparser_insert_cell_sql(handle, 0U, 0U, 0U, &fragment_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_public_fragment("OF002", "oracle-fragment-insert", "insert_cell_sql", fragment_sql, ":id") != 0) {
		fprintf(stderr, "FAIL [oracle-fragment-insert]: public fragment read failed: %s\n", error.message);
		sqlparser_string_free(fragment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(fragment_sql);
	fragment_sql = NULL;

	status = sqlparser_insert_set_cell_sql(handle, 0U, 0U, 0U, ":new_id", &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [oracle-fragment-insert]: rewrite failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains("OF002", "oracle-fragment-insert", handle, ":new_id") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	status = sqlparser_parse_with_options(
		"UPDATE users SET name = :name WHERE id = :id",
		&options,
		&handle,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [oracle-view-fragment-update]: parse failed: %s\n", error.message);
		return 1;
	}
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].assignment[0]";
	patch.sql = ":view_name";
	patches.items = &patch;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [oracle-view-fragment-update]: apply failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains("OF003", "oracle-view-fragment-update", handle, ":view_name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	status = sqlparser_export_view_json(handle, 0U, &view_json, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_public_fragment("OF003", "oracle-view-fragment-update", "view_json", view_json, ":view_name") != 0) {
		fprintf(stderr, "FAIL [oracle-view-fragment-update]: view JSON export failed: %s\n", error.message);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
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

	root = json_load_file(SQLPARSER_ORACLE_CASE_FIXTURE_PATH, 0, &error);
	if (root == NULL) {
		fprintf(stderr, "FAIL: unable to load fixture %s: %s\n", SQLPARSER_ORACLE_CASE_FIXTURE_PATH, error.text);
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
		json_t *ok_value;
		const char *case_id;
		const char *case_name;
		const char *sql;
		int expected_ok;

		case_id = json_string_or_null(json_object_get(item, "id"));
		case_name = json_string_or_null(json_object_get(item, "name"));
		sql = json_string_or_null(json_object_get(item, "sql"));
		expect_root = json_object_get(item, "expect");
		if (case_id == NULL || case_name == NULL || sql == NULL || !json_is_object(expect_root)) {
			json_decref(root);
			fprintf(stderr, "FAIL: case %lu is missing id/name/sql/expect\n", (unsigned long)index);
			return 1;
		}

		ok_value = json_object_get(expect_root, "ok");
		expected_ok = ok_value == NULL ? 1 : json_is_true(ok_value);
		if (expected_ok) {
			if (verify_success_case(case_id, case_name, sql, expect_root) != 0) {
				json_decref(root);
				return 1;
			}
		} else if (verify_failure_case(case_id, case_name, sql, expect_root) != 0) {
			json_decref(root);
			return 1;
		}
	}

	json_decref(root);
	if (verify_oracle_fragment_rewrite_paths() != 0) {
		return 1;
	}

	return 0;
}
