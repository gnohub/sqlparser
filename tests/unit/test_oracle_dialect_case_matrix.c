#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "sqlparser/sqlparser.h"

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

static int json_array_contains_string(json_t *array, const char *expected)
{
	size_t index;
	json_t *value;

	if (!json_is_array(array) || expected == NULL) {
		return 0;
	}

	json_array_foreach(array, index, value) {
		const char *text;

		text = json_string_or_null(value);
		if (text != NULL && strcmp(text, expected) == 0) {
			return 1;
		}
	}

	return 0;
}

static int json_object_array_contains_string(json_t *array, const char *key, const char *expected)
{
	size_t index;
	json_t *value;

	if (!json_is_array(array) || key == NULL || expected == NULL) {
		return 0;
	}

	json_array_foreach(array, index, value) {
		json_t *field;
		const char *text;

		if (!json_is_object(value)) {
			continue;
		}

		field = json_object_get(value, key);
		text = json_string_or_null(field);
		if (text != NULL && strcmp(text, expected) == 0) {
			return 1;
		}
	}

	return 0;
}

static int verify_summary_string_array(
	const char *case_id,
	const char *case_name,
	json_t *summary_root,
	const char *field_name,
	json_t *expected_array)
{
	size_t index;
	json_t *value;
	json_t *actual_array;

	if (expected_array == NULL) {
		return 0;
	}
	if (!json_is_array(expected_array)) {
		return fail_case(case_id, case_name, "expected string-array metadata must be an array");
	}

	actual_array = json_object_get(summary_root, field_name);
	if (!json_is_array(actual_array)) {
		return fail_case(case_id, case_name, "summary string-array field is missing");
	}

	json_array_foreach(expected_array, index, value) {
		const char *expected_text;

		expected_text = json_string_or_null(value);
		if (expected_text == NULL) {
			return fail_case(case_id, case_name, "expected string-array value must be a string");
		}
		if (!json_array_contains_string(actual_array, expected_text)) {
			return fail_case_field(case_id, case_name, field_name, expected_text);
		}
	}

	return 0;
}

static int verify_summary_object_array(
	const char *case_id,
	const char *case_name,
	json_t *summary_root,
	const char *field_name,
	const char *object_key,
	json_t *expected_array)
{
	size_t index;
	json_t *value;
	json_t *actual_array;

	if (expected_array == NULL) {
		return 0;
	}
	if (!json_is_array(expected_array)) {
		return fail_case(case_id, case_name, "expected object-array metadata must be an array");
	}

	actual_array = json_object_get(summary_root, field_name);
	if (!json_is_array(actual_array)) {
		return fail_case(case_id, case_name, "summary object-array field is missing");
	}

	json_array_foreach(expected_array, index, value) {
		const char *expected_text;

		expected_text = json_string_or_null(value);
		if (expected_text == NULL) {
			return fail_case(case_id, case_name, "expected object-array value must be a string");
		}
		if (!json_object_array_contains_string(actual_array, object_key, expected_text)) {
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
	char *summary_json;
	char *model_json;
	char *deparse_sql;
	json_error_t json_error;
	json_t *summary_root;
	json_t *value;
	const char *deparse_contains;
	const char *model_contains;
	const char *model_not_contains;
	int status;

	handle = NULL;
	summary_json = NULL;
	model_json = NULL;
	deparse_sql = NULL;
	summary_root = NULL;
	memset(&error, 0, sizeof(error));
	memset(&json_error, 0, sizeof(json_error));
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

	status = sqlparser_export_summary_json(handle, 0, &summary_json, &error);
	if (status != SQLPARSER_STATUS_OK || summary_json == NULL || summary_json[0] == '\0') {
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "summary JSON export failed");
	}

	summary_root = json_loads(summary_json, 0, &json_error);
	if (summary_root == NULL) {
		sqlparser_string_free(summary_json);
		sqlparser_handle_destroy(handle);
		return fail_case(case_id, case_name, "summary JSON could not be decoded");
	}

	if (verify_summary_string_array(
		    case_id,
		    case_name,
		    summary_root,
		    "statement_types",
		    json_object_get(expect_root, "statement_types")) != 0) {
		goto fail;
	}
	if (verify_summary_string_array(
		    case_id,
		    case_name,
		    summary_root,
		    "keywords",
		    json_object_get(expect_root, "keywords")) != 0) {
		goto fail;
	}
	if (verify_summary_object_array(
		    case_id,
		    case_name,
		    summary_root,
		    "tables",
		    "name",
		    json_object_get(expect_root, "tables")) != 0) {
		goto fail;
	}
	if (verify_summary_object_array(
		    case_id,
		    case_name,
		    summary_root,
		    "selected_columns",
		    "column",
		    json_object_get(expect_root, "selected_columns")) != 0) {
		goto fail;
	}
	if (verify_summary_object_array(
		    case_id,
		    case_name,
		    summary_root,
		    "join_columns",
		    "column",
		    json_object_get(expect_root, "join_columns")) != 0) {
		goto fail;
	}
	if (verify_summary_object_array(
		    case_id,
		    case_name,
		    summary_root,
		    "where_columns",
		    "column",
		    json_object_get(expect_root, "where_columns")) != 0) {
		goto fail;
	}
	if (verify_summary_object_array(
		    case_id,
		    case_name,
		    summary_root,
		    "insert_columns",
		    "column",
		    json_object_get(expect_root, "insert_columns")) != 0) {
		goto fail;
	}
	if (verify_summary_object_array(
		    case_id,
		    case_name,
		    summary_root,
		    "update_columns",
		    "column",
		    json_object_get(expect_root, "update_columns")) != 0) {
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

	model_contains = json_string_or_null(json_object_get(expect_root, "model_contains"));
	model_not_contains = json_string_or_null(json_object_get(expect_root, "model_not_contains"));
	if (model_contains != NULL || model_not_contains != NULL) {
		status = sqlparser_export_model_json(handle, 0, &model_json, &error);
		if (status != SQLPARSER_STATUS_OK || model_json == NULL || model_json[0] == '\0') {
			(void)fail_case(case_id, case_name, "model JSON export failed");
			goto fail;
		}
		if (model_contains != NULL && strstr(model_json, model_contains) == NULL) {
			(void)fail_case_field(case_id, case_name, "model_contains", model_contains);
			goto fail;
		}
		if (model_not_contains != NULL && strstr(model_json, model_not_contains) != NULL) {
			(void)fail_case_field(case_id, case_name, "model_not_contains", model_not_contains);
			goto fail;
		}
	}

	json_decref(summary_root);
	sqlparser_string_free(deparse_sql);
	sqlparser_string_free(model_json);
	sqlparser_string_free(summary_json);
	sqlparser_handle_destroy(handle);
	return 0;

fail:
	if (summary_root != NULL) {
		json_decref(summary_root);
	}
	sqlparser_string_free(deparse_sql);
	sqlparser_string_free(model_json);
	sqlparser_string_free(summary_json);
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
	char *fragment_sql;
	char *model_json;
	int status;

	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	fragment_sql = NULL;
	model_json = NULL;

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
		fprintf(stderr, "FAIL [oracle-model-fragment-update]: parse failed: %s\n", error.message);
		return 1;
	}
	status = sqlparser_apply_model_json(
		handle,
		"{\"schema\":\"sqlparser.model/v1\",\"changes\":["
		"{\"selector\":\"stmt[0].assignment[0]\",\"sql\":\":model_name\"}"
		"]}",
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL [oracle-model-fragment-update]: apply failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains("OF003", "oracle-model-fragment-update", handle, ":model_name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	status = sqlparser_export_model_json(handle, 0U, &model_json, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_public_fragment("OF003", "oracle-model-fragment-update", "model_json", model_json, ":model_name") != 0) {
		fprintf(stderr, "FAIL [oracle-model-fragment-update]: model JSON export failed: %s\n", error.message);
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(model_json);
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
