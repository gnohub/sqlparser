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
	sqlparser_patch_t item;
	sqlparser_patch_list_t patches;
	char *deparsed_sql;
	int status;

	sql = "INSERT INTO `users` (`id`, `name`) VALUES (1, \"bob\"), (2, 'alice')";
	handle = NULL;
	reparsed = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));
	memset(&item, 0, sizeof(item));

	/*
	 * MySQL 不是默认方言，调用方需要通过 parse options 显式指定。
	 * 默认不传 options 时使用 PostgreSQL 语法。
	 */
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;

	status = sqlparser_parse_with_options(sql, &options, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	printf("dialect: %s\n", sqlparser_dialect_name(sqlparser_handle_dialect(handle)));

	/* 通过统一 patch 入口按 selector 修改第一行 name 字段。 */
	item.op = SQLPARSER_PATCH_REPLACE;
	item.selector = "stmt[0].insert_cell[0][1]";
	item.sql = "'carol'";
	patches.items = &item;
	patches.count = 1U;
	status = sqlparser_apply_patch(handle, &patches, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "apply_patch failed: %s\n", err.message);
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

	/* 改写后的 SQL 继续按 MySQL 方言重新解析，验证回写结果合法。 */
	status = sqlparser_parse_with_options(deparsed_sql, &options, &reparsed, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "reparse failed: %s\n", err.message);
		sqlparser_string_free(deparsed_sql);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}
