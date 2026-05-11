#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_insert_source_kind_t source_kind;
	size_t row_count;
	int status;

	sql = "INSERT INTO archive_users (id, name) "
	      "SELECT id, name FROM users WHERE active = true";
	handle = NULL;
	memset(&err, 0, sizeof(err));

	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/*
	 * 这里特意演示 INSERT ... SELECT。
	 * 它不是行列 values grid，所以不能强行套用“第几行第几列”的修改接口。
	 */
	status = sqlparser_insert_source_kind(handle, 0U, &source_kind, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "insert_source_kind failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_insert_row_count(handle, 0U, &row_count, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "insert_row_count failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("insert source: %s\n", sqlparser_insert_source_kind_name(source_kind));
	printf("values row count: %lu\n", (unsigned long)row_count);

	sqlparser_handle_destroy(handle);
	return 0;
}
