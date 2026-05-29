#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_error_t err;
	sqlparser_bind_value_t bind;
	sqlparser_patch_t items[2];
	sqlparser_patch_list_t patches;
	char *rewritten_sql;
	int status;

	sql =
		"INSERT ALL "
		"INTO KDES.DBP_CRYPTO_TEST (ID, SECRET) VALUES (1, :secret1) "
		"INTO KDES.DBP_PHONE_TEST (ID, PHONE) VALUES (2, :phone1) "
		"SELECT 1 FROM DUAL";

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	handle = NULL;
	reparsed = NULL;
	rewritten_sql = NULL;
	memset(&err, 0, sizeof(err));
	memset(&bind, 0, sizeof(bind));
	memset(items, 0, sizeof(items));

	status = sqlparser_parse_with_options(sql, &options, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/* 给第 1 个 INTO 分支追加 SECRET_COPY，并让库按 Oracle 方言生成 :secret_copy。 */
	bind.kind = SQLPARSER_BIND_KIND_NAMED;
	bind.key = "secret_copy";
	items[0].op = SQLPARSER_PATCH_INSERT_COLUMN;
	items[0].selector = "stmt[0].insert_branch_columns[0]";
	items[0].index = 2U;
	items[0].name = "SECRET_COPY";
	items[0].bind = &bind;

	/* 再追加 SECRET_CLONE，cell 值从已有 stmt[0].insert_cell[0][1] 克隆。 */
	items[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
	items[1].selector = "stmt[0].insert_branch_columns[0]";
	items[1].index = 3U;
	items[1].name = "SECRET_CLONE";
	items[1].source_selector = "stmt[0].insert_cell[0][1]";

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

	status = sqlparser_parse_with_options(rewritten_sql, &options, &reparsed, &err);
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
