#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

#define SQLPARSER_ARRAY_LEN(array_value) (sizeof(array_value) / sizeof((array_value)[0]))

typedef struct {
	sqlparser_dialect_t dialect;
	const char *sql;
} sqlparser_stability_case_t;

static int expect_true(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		return 1;
	}

	return 0;
}

static int expect_status(
	sqlparser_status_t actual,
	sqlparser_status_t expected,
	const sqlparser_error_t *error,
	const char *message)
{
	if (actual != expected) {
		fprintf(stderr,
		        "FAIL: %s: expected=%d actual=%d message=%s\n",
		        message,
		        (int)expected,
		        (int)actual,
		        error != NULL ? error->message : "unknown");
		return 1;
	}

	return 0;
}

static int expect_not_ok(sqlparser_status_t actual, const char *message)
{
	if (actual == SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: %s: expected non-OK status\n", message);
		return 1;
	}

	return 0;
}

static int parse_with_dialect(
	const char *sql,
	sqlparser_dialect_t dialect,
	sqlparser_handle_t **out_handle,
	sqlparser_error_t *out_error)
{
	sqlparser_parse_options_t options;

	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	return sqlparser_parse_with_options(sql, &options, out_handle, out_error);
}

static int verify_roundtrip_case(const sqlparser_stability_case_t *test_case)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *summary_json;
	char *model_json;
	char *deparsed_sql;
	sqlparser_status_t status;

	handle = NULL;
	summary_json = NULL;
	model_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	status = parse_with_dialect(test_case->sql, test_case->dialect, &handle, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "generated SQL should parse") != 0 ||
	    expect_true(handle != NULL, "parse should return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_export_summary_json(handle, 0, &summary_json, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "summary export should succeed") != 0 ||
	    expect_true(summary_json != NULL && summary_json[0] != '\0', "summary JSON should be non-empty") != 0) {
		sqlparser_string_free(summary_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_export_model_json(handle, 0, &model_json, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "model export should succeed") != 0 ||
	    expect_true(model_json != NULL && strstr(model_json, "sqlparser.model/v1") != NULL, "model JSON should carry schema") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_string_free(summary_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "deparse should succeed") != 0 ||
	    expect_true(deparsed_sql != NULL && deparsed_sql[0] != '\0', "deparse output should be non-empty") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(model_json);
		sqlparser_string_free(summary_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	if (test_case->dialect == SQLPARSER_DIALECT_ORACLE &&
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "Oracle deparse should not expose internal bind names") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(model_json);
		sqlparser_string_free(summary_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(model_json);
	sqlparser_string_free(summary_json);
	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_generated_success_corpus(void)
{
	char sql_buffer[512];
	sqlparser_stability_case_t test_case;
	size_t index;

	for (index = 0U; index < 96U; index++) {
		if ((index % 6U) == 0U) {
			(void)snprintf(
				sql_buffer,
				sizeof(sql_buffer),
				"SELECT u.id, u.name FROM public.users u WHERE u.id = %lu AND u.name <> 'user_%lu'",
				(unsigned long)index,
				(unsigned long)index);
			test_case.dialect = SQLPARSER_DIALECT_POSTGRESQL;
		} else if ((index % 6U) == 1U) {
			(void)snprintf(
				sql_buffer,
				sizeof(sql_buffer),
				"INSERT INTO public.users (id, name, active) VALUES (%lu, 'user_%lu', true)",
				(unsigned long)index,
				(unsigned long)index);
			test_case.dialect = SQLPARSER_DIALECT_POSTGRESQL;
		} else if ((index % 6U) == 2U) {
			(void)snprintf(
				sql_buffer,
				sizeof(sql_buffer),
				"UPDATE public.users SET name = 'user_%lu', updated_at = now() WHERE id = %lu",
				(unsigned long)index,
				(unsigned long)index);
			test_case.dialect = SQLPARSER_DIALECT_POSTGRESQL;
		} else if ((index % 6U) == 3U) {
			(void)snprintf(
				sql_buffer,
				sizeof(sql_buffer),
				"INSERT INTO `users` (`id`, `name`) VALUES (%lu, 'mysql_%lu')",
				(unsigned long)index,
				(unsigned long)index);
			test_case.dialect = SQLPARSER_DIALECT_MYSQL;
		} else if ((index % 6U) == 4U) {
			(void)snprintf(
				sql_buffer,
				sizeof(sql_buffer),
				"UPDATE users SET name = :name_%lu WHERE id = %lu",
				(unsigned long)index,
				(unsigned long)index);
			test_case.dialect = SQLPARSER_DIALECT_ORACLE;
		} else {
			(void)snprintf(
				sql_buffer,
				sizeof(sql_buffer),
				"SELECT q'[user_%lu]' AS label FROM dual WHERE id = :id_%lu",
				(unsigned long)index,
				(unsigned long)index);
			test_case.dialect = SQLPARSER_DIALECT_ORACLE;
		}

		test_case.sql = sql_buffer;
		if (verify_roundtrip_case(&test_case) != 0) {
			fprintf(stderr, "FAIL: generated corpus index %lu\n", (unsigned long)index);
			return 1;
		}
	}

	return 0;
}

static int test_malformed_inputs_do_not_return_handles(void)
{
	static const sqlparser_stability_case_t cases[] = {
		{SQLPARSER_DIALECT_POSTGRESQL, "SELECT * FROM"},
		{SQLPARSER_DIALECT_POSTGRESQL, "INSERT INTO t (id) VALUES ("},
		{SQLPARSER_DIALECT_MYSQL, "SELECT * FROM `unterminated"},
		{SQLPARSER_DIALECT_ORACLE, "SELECT q'[unterminated' FROM dual"},
		{SQLPARSER_DIALECT_ORACLE, "BEGIN NULL; END;"}
	};
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;
	size_t index;

	for (index = 0U; index < SQLPARSER_ARRAY_LEN(cases); index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));

		status = parse_with_dialect(cases[index].sql, cases[index].dialect, &handle, &error);
		if (expect_not_ok(status, "malformed SQL should not parse") != 0 ||
		    expect_true(handle == NULL, "failed parse should not return a handle") != 0) {
			sqlparser_handle_destroy(handle);
			fprintf(stderr, "FAIL: malformed corpus index %lu\n", (unsigned long)index);
			return 1;
		}
	}

	return 0;
}

static int test_argument_validation(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_statement_kind_t kind;
	char *json_text;
	sqlparser_status_t status;

	handle = NULL;
	json_text = NULL;
	memset(&error, 0, sizeof(error));

	status = sqlparser_parse(NULL, &handle, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL SQL should be rejected") != 0 ||
	    expect_true(handle == NULL, "NULL SQL should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_parse("SELECT 1", NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL out_handle should be rejected") != 0) {
		return 1;
	}

	status = sqlparser_parse("SELECT 1", &handle, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "valid parse should succeed") != 0) {
		return 1;
	}

	status = sqlparser_statement_kind(handle, 0U, NULL, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL statement kind output should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_kind(NULL, 0U, &kind, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL handle should be rejected") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_export_model_json(NULL, 0, &json_text, &error);
	if (expect_status(status, SQLPARSER_STATUS_INVALID_ARGUMENT, &error, "NULL handle model export should be rejected") != 0 ||
	    expect_true(json_text == NULL, "failed model export should not return text") != 0) {
		sqlparser_string_free(json_text);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(handle);
	return 0;
}

static int test_resource_limits(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_limits_t limits;
	char *model_json;
	char *deparsed_sql;
	sqlparser_status_t status;

	handle = NULL;
	model_json = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	sqlparser_limits_default(&limits);
	limits.max_sql_bytes = 8U;
	status = sqlparser_parse_with_limits("SELECT 123456789", &limits, &handle, &error);
	if (expect_status(status, SQLPARSER_STATUS_RESOURCE_LIMIT, &error, "SQL byte limit should be enforced") != 0 ||
	    expect_true(handle == NULL, "SQL limit failure should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_limits_default(&limits);
	limits.max_statement_count = 1U;
	status = sqlparser_parse_with_limits("SELECT 1; SELECT 2", &limits, &handle, &error);
	if (expect_status(status, SQLPARSER_STATUS_RESOURCE_LIMIT, &error, "statement count limit should be enforced") != 0 ||
	    expect_true(handle == NULL, "statement limit failure should not return a handle") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_parse("UPDATE public.users SET name = 'bob' WHERE id = 1", &handle, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "parse for model limit should succeed") != 0) {
		return 1;
	}

	status = sqlparser_export_model_json(handle, 0, &model_json, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "model export for limit test should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_limits_default(&limits);
	limits.max_model_json_bytes = 8U;
	status = sqlparser_apply_model_json_with_limits(handle, model_json, &limits, &error);
	if (expect_status(status, SQLPARSER_STATUS_RESOURCE_LIMIT, &error, "model JSON byte limit should be enforced") != 0) {
		sqlparser_string_free(model_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "handle should remain usable after model limit failure") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, "bob") != NULL, "deparse should preserve original value after model limit failure") != 0) {
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

static int test_failed_fragment_write_is_atomic(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *fragment_sql;
	char *deparsed_sql;
	sqlparser_status_t status;

	handle = NULL;
	fragment_sql = NULL;
	deparsed_sql = NULL;
	memset(&error, 0, sizeof(error));

	status = parse_with_dialect(
		"UPDATE users SET name = :name WHERE id = :id",
		SQLPARSER_DIALECT_ORACLE,
		&handle,
		&error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "Oracle update should parse") != 0) {
		return 1;
	}

	status = sqlparser_update_assignment_sql(handle, 0U, 0U, &fragment_sql, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "Oracle assignment SQL read should succeed") != 0 ||
	    expect_true(fragment_sql != NULL && strcmp(fragment_sql, ":name") == 0, "Oracle assignment SQL should be public bind text") != 0) {
		sqlparser_string_free(fragment_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(fragment_sql);
	fragment_sql = NULL;

	status = sqlparser_update_set_assignment_sql(handle, 0U, 0U, ":", &error);
	if (expect_not_ok(status, "invalid Oracle assignment fragment should fail") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "deparse after failed fragment write should succeed") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, ":name") != NULL, "failed fragment write should preserve old bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "failed fragment write should not expose internal bind") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	sqlparser_string_free(deparsed_sql);
	deparsed_sql = NULL;

	status = sqlparser_update_set_assignment_sql(handle, 0U, 0U, ":new_name", &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "valid Oracle assignment fragment should succeed") != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_deparse(handle, &deparsed_sql, &error);
	if (expect_status(status, SQLPARSER_STATUS_OK, &error, "deparse after valid fragment write should succeed") != 0 ||
	    expect_true(deparsed_sql != NULL && strstr(deparsed_sql, ":new_name") != NULL, "valid fragment write should use new bind") != 0 ||
	    expect_true(strstr(deparsed_sql, "$1") == NULL, "valid fragment write should not expose internal bind") != 0) {
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}

int main(void)
{
	if (test_generated_success_corpus() != 0 ||
	    test_malformed_inputs_do_not_return_handles() != 0 ||
	    test_argument_validation() != 0 ||
	    test_resource_limits() != 0 ||
	    test_failed_fragment_write_is_atomic() != 0) {
		return 1;
	}

	return 0;
}
