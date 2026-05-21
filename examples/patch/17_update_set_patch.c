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

	sql = "UPDATE public.users SET secret = 'qz$...', status = 'old' WHERE id = 1";
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

	/* assignment[1] 是 status = 'old'，这里整项替换为 status = 'active'。 */
	items[0].op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
	items[0].selector = "stmt[0].assignment[1]";
	items[0].sql = "status = 'active'";

	/* assignment[2] 等于当前 SET 列表长度，因此这里表示追加新赋值项。 */
	items[1].op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	items[1].selector = "stmt[0].assignment[2]";
	items[1].sql = "secret_orig = 'abc'";

	/* 上面两步之后 assignment[1] 仍是 status，这里删除它。 */
	items[2].op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	items[2].selector = "stmt[0].assignment[1]";

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
