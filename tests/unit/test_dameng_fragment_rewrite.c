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

static int verify_dameng_fragment_rewrite_paths(void)
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
	options.dialect = SQLPARSER_DIALECT_DAMENG;

	status = sqlparser_parse_with_options(
		"UPDATE users SET name = :name WHERE id = :id",
		&options,
		&handle,
		&error);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL [DF001 dameng-fragment-update]: parse failed: %s\n",
			error.message);
		return 1;
	}

	status = sqlparser_update_assignment_sql(
		handle, 0U, 0U, &fragment_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    fragment_sql == NULL ||
	    strstr(fragment_sql, ":name") == NULL ||
	    strstr(fragment_sql, "$1") != NULL) {
		sqlparser_string_free(fragment_sql);
		sqlparser_handle_destroy(handle);
		return fail_case(
			"DF001",
			"dameng-fragment-update",
			"assignment fragment read failed");
	}
	sqlparser_string_free(fragment_sql);
	fragment_sql = NULL;

	status = sqlparser_update_set_assignment_sql(
		handle, 0U, 0U, ":new_name", &error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(handle);
		return fail_case(
			"DF001",
			"dameng-fragment-update",
			"assignment fragment rewrite failed");
	}
	status = sqlparser_deparse(handle, &deparse_sql, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    deparse_sql == NULL ||
	    strstr(deparse_sql, ":new_name") == NULL ||
	    strstr(deparse_sql, "$") != NULL) {
		sqlparser_string_free(deparse_sql);
		sqlparser_handle_destroy(handle);
		return fail_case(
			"DF001",
			"dameng-fragment-update",
			"deparse after rewrite failed");
	}

	sqlparser_string_free(deparse_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

int main(void)
{
	return verify_dameng_fragment_rewrite_paths();
}
