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

	/*
	 * 核心层接口只给“精确索引能力”。
	 * 如果调用方想按列名定位，就自己先枚举一遍，再拿到精确索引去改。
	 */
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
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_literal_view_t old_literal;
	sqlparser_literal_value_t new_literal;
	char *deparsed_sql;
	size_t name_column_index;
	int status;

	sql = "INSERT INTO public.users (id, name, age) VALUES "
	      "(1, 'bob', 18), "
	      "(2, 'alice', 19)";
	handle = NULL;
	deparsed_sql = NULL;
	name_column_index = 0U;
	memset(&err, 0, sizeof(err));
	memset(&old_literal, 0, sizeof(old_literal));
	memset(&new_literal, 0, sizeof(new_literal));

	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	if (find_insert_column_index(handle, 0U, "name", &name_column_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 先读后改，确认自己拿到的确实是想改的那个 literal。 */
	status = sqlparser_insert_cell_literal(handle, 0U, 0U, name_column_index, &old_literal, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "insert_cell_literal failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("old row0 name: %s\n", old_literal.string_value);

	/* 这里把第 1 行的 name 从 bob 改成 carol。 */
	new_literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	new_literal.string_value = "carol";
	status = sqlparser_insert_set_cell_literal(handle, 0U, 0U, name_column_index, &new_literal, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "insert_set_cell_literal failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* mutation 成功后，再统一 deparse 回 SQL。 */
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
