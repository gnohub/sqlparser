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

	sql = "SELECT * FROM public.users WHERE status = 'active'";
	handle = NULL;
	reparsed = NULL;
	rewritten_sql = NULL;
	memset(&err, 0, sizeof(err));
	memset(patch_items, 0, sizeof(patch_items));

	/* 解析后，结构级子句可以通过 stmt[n].clause[m] selector 统一改写。 */
	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/* clause[0] 是 SELECT 输出列表，替换 * 为明确字段列表。 */
	patch_items[0].op = SQLPARSER_PATCH_REPLACE;
	patch_items[0].selector = "stmt[0].clause[0]";
	patch_items[0].sql = "id, name";

	/* clause[1] 是 WHERE，追加一段 AND 条件。 */
	patch_items[1].op = SQLPARSER_PATCH_APPEND_CONDITION;
	patch_items[1].selector = "stmt[0].clause[1]";
	patch_items[1].bool_operator = SQLPARSER_BOOL_OPERATOR_AND;
	patch_items[1].sql = "deleted_at IS NULL";

	/* clause[2] 是 ORDER BY 槽位；原 SQL 没有 ORDER BY，因此这里是新增。 */
	patch_items[2].op = SQLPARSER_PATCH_REPLACE;
	patch_items[2].selector = "stmt[0].clause[2]";
	patch_items[2].sql = "name DESC, id ASC";

	patches.items = patch_items;
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
