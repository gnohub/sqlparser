#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	char *view_json;
	char *deparsed_sql;
	int status;

	sql =
		"SELECT q'[Bob's order]' AS label, u.id "
		"FROM users u WHERE u.id = :id "
		"MINUS SELECT 'archived' AS label, a.id FROM archived_users a WHERE a.id = :id";

	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));

	/* Oracle 不是默认方言，调用方需要通过 parse options 显式指定。 */
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;

	/* 解析时会在内部把 Oracle bind 和 MINUS 转成核心 AST 可接受的形式。 */
	status = sqlparser_parse_with_options(sql, &options, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	printf("dialect: %s\n", sqlparser_dialect_name(sqlparser_handle_dialect(handle)));

	/* view JSON 用于查看 query_graph、字段关联值和可回写 selector。 */
	status = sqlparser_export_view_json(handle, 1, &view_json, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "view export failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("view json:\n%s\n", view_json);

	/*
	 * 反解析会把内部 $1/$2 参数还原为原始 Oracle bind 名称，
	 * 并把 EXCEPT 还原为 Oracle 的 MINUS。
	 */
	status = sqlparser_deparse(handle, &deparsed_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err.message);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("deparsed sql:\n%s\n", deparsed_sql);

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}
