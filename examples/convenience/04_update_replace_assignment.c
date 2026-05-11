#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

static int find_assignment_index(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *column_name,
	size_t *out_index)
{
	sqlparser_error_t err;
	sqlparser_assignment_view_t assignment;
	size_t assignment_count;
	size_t index;

	memset(&err, 0, sizeof(err));
	memset(&assignment, 0, sizeof(assignment));
	*out_index = 0U;
	if (sqlparser_update_assignment_count(handle, statement_index, &assignment_count, &err) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "update_assignment_count failed: %s\n", err.message);
		return 1;
	}

	/* 先枚举 assignment，再决定修改哪一项，这比库里硬编码 by-name API 更稳。 */
	for (index = 0U; index < assignment_count; index++) {
		if (sqlparser_update_assignment(handle, statement_index, index, &assignment, &err) !=
		    SQLPARSER_STATUS_OK) {
			fprintf(stderr, "update_assignment failed: %s\n", err.message);
			return 1;
		}
		if (assignment.column_name != NULL && strcmp(assignment.column_name, column_name) == 0) {
			*out_index = index;
			return 0;
		}
	}

	fprintf(stderr, "assignment not found: %s\n", column_name);
	return 1;
}

static int find_where_literal_index(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char *column_name,
	size_t *out_index)
{
	sqlparser_error_t err;
	sqlparser_where_literal_view_t where_literal;
	size_t where_count;
	size_t index;

	memset(&err, 0, sizeof(err));
	memset(&where_literal, 0, sizeof(where_literal));
	*out_index = 0U;
	if (sqlparser_statement_where_literal_count(handle, statement_index, &where_count, &err) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_where_literal_count failed: %s\n", err.message);
		return 1;
	}

	/*
	 * 这里演示“按列名找 WHERE literal”的上层写法。
	 * 核心层不给 by-name API，但调用方可以先枚举，再定位精确索引。
	 */
	for (index = 0U; index < where_count; index++) {
		if (sqlparser_statement_where_literal(handle, statement_index, index, &where_literal, &err) !=
		    SQLPARSER_STATUS_OK) {
			fprintf(stderr, "statement_where_literal failed: %s\n", err.message);
			return 1;
		}
		if (where_literal.column_name != NULL &&
		    strcmp(where_literal.column_name, column_name) == 0) {
			*out_index = index;
			return 0;
		}
	}

	fprintf(stderr, "where literal not found for column: %s\n", column_name);
	return 1;
}

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_assignment_view_t assignment;
	sqlparser_where_literal_view_t where_literal;
	sqlparser_literal_value_t new_literal;
	sqlparser_literal_value_t new_where_literal;
	char *deparsed_sql;
	size_t assignment_index;
	size_t where_index;
	int status;

	sql = "UPDATE public.users SET name = 'bob', age = 18 WHERE id = 1 AND status = 'active'";
	handle = NULL;
	deparsed_sql = NULL;
	assignment_index = 0U;
	where_index = 0U;
	memset(&err, 0, sizeof(err));
	memset(&assignment, 0, sizeof(assignment));
	memset(&where_literal, 0, sizeof(where_literal));
	memset(&new_literal, 0, sizeof(new_literal));
	memset(&new_where_literal, 0, sizeof(new_where_literal));

	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	if (find_assignment_index(handle, 0U, "name", &assignment_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}
	if (find_where_literal_index(handle, 0U, "id", &where_index) != 0) {
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_update_assignment(handle, 0U, assignment_index, &assignment, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "update_assignment failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("old assignment value: %s\n", assignment.literal.string_value);

	status = sqlparser_statement_where_literal(handle, 0U, where_index, &where_literal, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_where_literal failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("old where value: %lld\n", where_literal.literal.integer_value);

	/* 这里只改 SET 子句右值本身，不改语义结构。 */
	new_literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	new_literal.string_value = "carol";
	status = sqlparser_update_set_assignment_literal(handle, 0U, assignment_index, &new_literal, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "update_set_assignment_literal failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 第二个修改点是 WHERE 子句里的 literal。 */
	new_where_literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
	new_where_literal.integer_value = 2LL;
	status = sqlparser_statement_where_set_literal(handle, 0U, where_index, &new_where_literal, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_where_set_literal failed: %s\n", err.message);
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
