#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_selector_t selector;
	char *assignment_sql;
	char *cell_sql;
	char *deparsed_sql;
	int status;

	sql =
		"UPDATE public.users SET name = upper(name), updated_at = DEFAULT WHERE id = 1; "
		"INSERT INTO public.audit_log (id, payload, created_at) "
		"VALUES (1, json_build_object('user', 'bob'), DEFAULT)";
	handle = NULL;
	assignment_sql = NULL;
	cell_sql = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));
	memset(&selector, 0, sizeof(selector));

	/*
	 * 第一步：把完整 SQL 解析成 handle。
	 * 后面读取表达式、改写表达式、重新反解析都在这个 handle 上完成。
	 */
	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/*
	 * 第二步：直接读取 UPDATE assignment 的右值 SQL。
	 * 这里拿到的是稳定的反解析结果，不要求调用方自己拼 AST。
	 */
	status = sqlparser_update_assignment_sql(handle, 0U, 0U, &assignment_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "update_assignment_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("old assignment sql: %s\n", assignment_sql);
	sqlparser_string_free(assignment_sql);
	assignment_sql = NULL;

	/*
	 * 第三步：把 updated_at = DEFAULT 改成 updated_at = clock_timestamp()。
	 * 这里修改的是 assignment 右值本身，左侧列名和语义位置不变。
	 */
	status = sqlparser_update_set_assignment_sql(handle, 0U, 1U, "clock_timestamp()", &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "update_set_assignment_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/*
	 * 第四步：通过 selector 读取并改写 INSERT 某个单元格的表达式。
	 * stmt[1].insert_cell[0][2] 对应第一行第三列，也就是 created_at = DEFAULT。
	 */
	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = 1U;
	selector.row_index = 0U;
	selector.column_index = 2U;

	status = sqlparser_selector_insert_cell_sql(handle, &selector, &cell_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "selector_insert_cell_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("old insert cell sql: %s\n", cell_sql);
	sqlparser_string_free(cell_sql);
	cell_sql = NULL;

	status = sqlparser_selector_set_insert_cell_sql(handle, &selector, "now()", &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "selector_set_insert_cell_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 第五步：把改写后的 AST 重新反解析回 SQL。 */
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
