#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_error_t err;
	sqlparser_patch_t items[2];
	sqlparser_patch_list_t patches;
	char *rewritten_sql;
	int status;

	sql = "INSERT INTO public.users (id, name) VALUES (1, 'bob'), (2, 'alice')";
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

	/* 给 INSERT ... VALUES 增加 age 列，并为每一行填入默认 SQL 片段。 */
	items[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	items[0].selector = "stmt[0].insert_columns";
	items[0].index = 2U;
	items[0].name = "age";
	items[0].default_sql = "18";

	/* 删除 name 列；同一 patch 列表按数组顺序执行。 */
	items[1].op = SQLPARSER_PATCH_DELETE_COLUMN;
	items[1].selector = "stmt[0].insert_columns";
	items[1].index = 1U;

	patches.items = items;
	patches.count = 2U;
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
