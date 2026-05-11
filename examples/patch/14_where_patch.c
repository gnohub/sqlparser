#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_error_t err;
	sqlparser_patch_t patch_items[3];
	sqlparser_patch_list_t patches;
	char *rewritten_sql;
	int status;

	sql = "UPDATE public.users SET name = 'bob'";
	handle = NULL;
	reparsed = NULL;
	rewritten_sql = NULL;
	memset(&err, 0, sizeof(err));
	memset(patch_items, 0, sizeof(patch_items));

	/* 第一步：解析完整 SQL。这里原始 UPDATE 没有 WHERE。 */
	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/*
	 * 第二步：组装统一 patch 列表。
	 * selector 可以来自 SQL View JSON/C View，也可以由调用方自己的规则生成。
	 */
	patch_items[0].op = SQLPARSER_PATCH_REPLACE;
	patch_items[0].selector = "stmt[0].clause[0]";
	patch_items[0].sql = "id = 1";

	patch_items[1].op = SQLPARSER_PATCH_APPEND_CONDITION;
	patch_items[1].selector = "stmt[0].clause[0]";
	patch_items[1].bool_operator = SQLPARSER_BOOL_OPERATOR_AND;
	patch_items[1].sql = "status = 'active'";

	patch_items[2].op = SQLPARSER_PATCH_APPEND_CONDITION;
	patch_items[2].selector = "stmt[0].clause[0]";
	patch_items[2].bool_operator = SQLPARSER_BOOL_OPERATOR_OR;
	patch_items[2].sql = "external_id = 'u-1'";

	patches.items = patch_items;
	patches.count = 3U;
	status = sqlparser_apply_patch(handle, &patches, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "apply_patch failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 第三步：反解析整条 SQL，并再次解析验证改写结果合法。 */
	status = sqlparser_deparse(handle, &rewritten_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("rewritten sql: %s\n", rewritten_sql);

	status = sqlparser_parse(rewritten_sql, &reparsed, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "reparse failed: %s\n", err.message);
		sqlparser_string_free(rewritten_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(rewritten_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}
