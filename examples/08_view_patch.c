#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_patch_t patch_items[2];
	sqlparser_patch_list_t patch_list;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	char *view_json;
	char *deparsed_sql;
	int status;

	sql = "UPDATE public.users SET name = 'bob' WHERE id = 1";
	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));
	memset(patch_items, 0, sizeof(patch_items));

	/*
	 * 第一步：先把 SQL 解析成 handle。
	 * 后面的 view 导出、patch 应用、重新 deparse 都围绕这一个 handle 展开。
	 */
	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/*
	 * 第二步：导出 view JSON。
	 * 这不是 libpg_query 原始 JSON，而是按表、列、值组织后的结构化视图。
	 */
	status = sqlparser_export_view_json(handle, 1, &view_json, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "export_view_json failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("%s\n", view_json);
	sqlparser_string_free(view_json);
	view_json = NULL;

	/*
	 * 第三步：把 selector patch 结构体应用到同一个 handle。
	 * 这里演示的是两处精确改写：
	 * 1. assignment[0]：把 name = 'bob' 改成 name = 'carol'
	 * 2. where_literal[0]：把 id = 1 改成 id = 2
	 */
	patch_items[0].op = SQLPARSER_PATCH_REPLACE;
	patch_items[0].selector = "stmt[0].assignment[0]";
	patch_items[0].sql = "'carol'";
	patch_items[1].op = SQLPARSER_PATCH_REPLACE;
	patch_items[1].selector = "stmt[0].where_literal[0]";
	patch_items[1].sql = "2";
	patch_list.items = patch_items;
	patch_list.count = 2U;
	status = sqlparser_apply_patch(handle, &patch_list, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "apply_patch failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 第四步：把改写后的 AST 重新反解析成 SQL。 */
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
