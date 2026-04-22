#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	const char *patch_json;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	char *model_json;
	char *deparsed_sql;
	int status;

	sql = "UPDATE public.users SET name = 'bob' WHERE id = 1";
	patch_json =
		"{\"changes\":["
		"{\"selector\":\"stmt[0].assignment[0]\",\"literal\":{\"kind\":\"string\",\"string_value\":\"carol\"}},"
		"{\"selector\":\"stmt[0].where_literal[0]\",\"literal\":{\"kind\":\"integer\",\"integer_value\":2}}"
		"]}";
	handle = NULL;
	model_json = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));

	/*
	 * 第一步：先把 SQL 解析成 handle。
	 * 后面的模型导出、patch 导入、重新 deparse 都围绕这一个 handle 展开。
	 */
	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/*
	 * 第二步：导出稳定模型 JSON。
	 * 这不是 libpg_query 原始 JSON，而是 sqlparser 对外承诺的稳定编辑模型。
	 */
	status = sqlparser_export_model_json(handle, 1, &model_json, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "export_model_json failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("%s\n", model_json);
	sqlparser_string_free(model_json);
	model_json = NULL;

	/*
	 * 第三步：把一个 selector patch JSON 回放到同一个 handle。
	 * 这里演示的是两处精确改写：
	 * 1. assignment[0]：把 name = 'bob' 改成 name = 'carol'
	 * 2. where_literal[0]：把 id = 1 改成 id = 2
	 */
	status = sqlparser_apply_model_json(handle, patch_json, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "apply_model_json failed: %s\n", err.message);
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
