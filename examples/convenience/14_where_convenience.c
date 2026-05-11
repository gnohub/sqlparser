#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	char *rewritten_sql;
	int status;

	sql = "UPDATE public.users SET name = 'bob'";
	handle = NULL;
	rewritten_sql = NULL;
	memset(&err, 0, sizeof(err));

	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/* 便捷接口适合调用方已经明确 statement_index / where_index 的场景。 */
	status = sqlparser_statement_set_where_sql(handle, 0U, 0U, "id = 1", &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_set_where_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_append_where_sql(
		handle,
		0U,
		0U,
		SQLPARSER_BOOL_OPERATOR_AND,
		"status = 'active'",
		&err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_append_where_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_deparse(handle, &rewritten_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("rewritten sql: %s\n", rewritten_sql);

	sqlparser_string_free(rewritten_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}
