#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_error_t err;
	sqlparser_patch_t items[3];
	sqlparser_patch_list_t patches;
	char *rewritten_sql;
	int status;

	sql = "SELECT * FROM public.users WHERE status = 'active'";
	handle = NULL;
	reparsed = NULL;
	rewritten_sql = NULL;
	memset(&err, 0, sizeof(err));
	memset(items, 0, sizeof(items));

	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/* 用统一 patch 入口把 SELECT * 展开为明确输出列。 */
	items[0].op = SQLPARSER_PATCH_REPLACE;
	items[0].selector = "stmt[0].select_targets[0]";
	items[0].sql = "id, name, age";

	/* 在输出列表中插入一个表达式列。 */
	items[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	items[1].selector = "stmt[0].select_targets[0]";
	items[1].index = 2U;
	items[1].sql = "upper(name) AS normalized_name";

	/* 删除第 4 个输出项，也就是上面展开出来的 age。 */
	items[2].op = SQLPARSER_PATCH_DELETE_COLUMN;
	items[2].selector = "stmt[0].select_targets[0]";
	items[2].index = 3U;

	patches.items = items;
	patches.count = 3U;
	status = sqlparser_apply_patch(handle, &patches, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "apply_patch failed: %s\n", err.message);
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
