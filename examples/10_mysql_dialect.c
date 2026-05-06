#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

static int find_insert_column_index(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *column_name,
	size_t *out_index)
{
	sqlparser_error_t err;
	size_t column_count;
	size_t index;

	memset(&err, 0, sizeof(err));
	*out_index = 0U;
	if (sqlparser_insert_column_count(handle, statement_index, &column_count, &err) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "insert_column_count failed: %s\n", err.message);
		return 1;
	}

	/* 调用方可以先枚举列名，再用精确索引执行读取或改写。 */
	for (index = 0U; index < column_count; index++) {
		const char *current_name;

		if (sqlparser_insert_column_name(handle, statement_index, index, &current_name, &err) !=
		    SQLPARSER_STATUS_OK) {
			fprintf(stderr, "insert_column_name failed: %s\n", err.message);
			return 1;
		}
		if (current_name != NULL && strcmp(current_name, column_name) == 0) {
			*out_index = index;
			return 0;
		}
	}

	fprintf(stderr, "column not found: %s\n", column_name);
	return 1;
}

int main(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_literal_view_t old_literal;
	sqlparser_literal_value_t new_literal;
	char *deparsed_sql;
	size_t name_column_index;
	int status;

	sql = "INSERT INTO `users` (`id`, `name`) VALUES (1, \"bob\"), (2, 'alice')";
	handle = NULL;
	deparsed_sql = NULL;
	name_column_index = 0U;
	memset(&err, 0, sizeof(err));
	memset(&old_literal, 0, sizeof(old_literal));
	memset(&new_literal, 0, sizeof(new_literal));

	/*
	 * MySQL 不是默认方言，调用方需要通过 parse options 显式指定。
	 * 默认不传 options 时使用 PostgreSQL 语法。
	 */
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;

	status = sqlparser_parse_with_options(sql, &options, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	printf("dialect: %s\n", sqlparser_dialect_name(sqlparser_handle_dialect(handle)));

	if (find_insert_column_index(handle, 0U, "name", &name_column_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 读取第一行 name 字段，确认双引号字符串已经作为字符串字面量处理。 */
	status = sqlparser_insert_cell_literal(handle, 0U, 0U, name_column_index, &old_literal, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "insert_cell_literal failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("old row0 name: %s\n", old_literal.string_value);

	/* 将第一行 name 从 bob 改为 carol。 */
	new_literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	new_literal.string_value = "carol";
	status = sqlparser_insert_set_cell_literal(handle, 0U, 0U, name_column_index, &new_literal, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "insert_set_cell_literal failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_deparse(handle, &deparsed_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("rewritten sql: %s\n", deparsed_sql);
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}
