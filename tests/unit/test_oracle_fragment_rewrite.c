#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

static int fail_case(
	const char *case_id,
	const char *case_name,
	const char *message)
{
	fprintf(
		stderr,
		"FAIL [%s %s]: %s\n",
		case_id != NULL ? case_id : "-",
		case_name,
		message);
	return 1;
}

static int fail_case_field(
	const char *case_id,
	const char *case_name,
	const char *field_name,
	const char *expected)
{
	fprintf(
		stderr,
		"FAIL [%s %s]: missing %s value '%s'\n",
		case_id != NULL ? case_id : "-",
		case_name,
		field_name,
		expected);
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
		return fail_case(
			case_id,
			case_name,
			"deparse failed after rewrite");
	}
	if (strstr(sql, expected) == NULL) {
		sqlparser_string_free(sql);
		return fail_case_field(
			case_id, case_name, "deparse", expected);
	}
	if (strstr(sql, "$") != NULL) {
		sqlparser_string_free(sql);
		return fail_case(
			case_id,
			case_name,
			"deparse leaked internal parameter syntax");
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
		return fail_case_field(
			case_id, case_name, field_name, expected);
	}
	if (strstr(sql, "$") != NULL) {
		return fail_case(
			case_id,
			case_name,
			"SQL fragment leaked internal parameter syntax");
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
		fprintf(
			stderr,
			"FAIL [oracle-fragment-update]: parse failed: %s\n",
			error.message);
		return 1;
	}
	status = sqlparser_update_assignment_sql(
		handle, 0U, 0U, &fragment_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_public_fragment(
		    "OF001",
		    "oracle-fragment-update",
		    "assignment_sql",
		    fragment_sql,
		    ":name") != 0) {
		fprintf(
			stderr,
			"FAIL [oracle-fragment-update]: public fragment read failed: %s\n",
			error.message);
		sqlparser_string_free(fragment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(fragment_sql);
	fragment_sql = NULL;

	status = sqlparser_update_set_assignment_sql(
		handle, 0U, 0U, ":new_name", &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL [oracle-fragment-update]: rewrite failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains(
		    "OF001",
		    "oracle-fragment-update",
		    handle,
		    ":new_name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	status = sqlparser_update_set_assignment_sql(
		handle, 0U, 0U, ":second_name", &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL [oracle-fragment-update]: second rewrite failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains(
		    "OF001",
		    "oracle-fragment-update",
		    handle,
		    ":second_name") != 0) {
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
		fprintf(
			stderr,
			"FAIL [oracle-fragment-insert]: parse failed: %s\n",
			error.message);
		return 1;
	}
	status = sqlparser_insert_cell_sql(
		handle, 0U, 0U, 0U, &fragment_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_public_fragment(
		    "OF002",
		    "oracle-fragment-insert",
		    "insert_cell_sql",
		    fragment_sql,
		    ":id") != 0) {
		fprintf(
			stderr,
			"FAIL [oracle-fragment-insert]: public fragment read failed: %s\n",
			error.message);
		sqlparser_string_free(fragment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(fragment_sql);
	fragment_sql = NULL;

	status = sqlparser_insert_set_cell_sql(
		handle, 0U, 0U, 0U, ":new_id", &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL [oracle-fragment-insert]: rewrite failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains(
		    "OF002",
		    "oracle-fragment-insert",
		    handle,
		    ":new_id") != 0) {
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
		fprintf(
			stderr,
			"FAIL [oracle-view-fragment-update]: parse failed: %s\n",
			error.message);
		return 1;
	}
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].assignment[0]";
	patch.sql = ":view_name";
	patches.items = &patch;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, &error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL [oracle-view-fragment-update]: apply failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (expect_deparse_contains(
		    "OF003",
		    "oracle-view-fragment-update",
		    handle,
		    ":view_name") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	status = sqlparser_export_view_json(
		handle, 0U, &view_json, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    expect_public_fragment(
		    "OF003",
		    "oracle-view-fragment-update",
		    "view_json",
		    view_json,
		    ":view_name") != 0) {
		fprintf(
			stderr,
			"FAIL [oracle-view-fragment-update]: view JSON export failed: %s\n",
			error.message);
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
	return verify_oracle_fragment_rewrite_paths();
}
