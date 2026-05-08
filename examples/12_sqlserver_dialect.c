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
		"SELECT TOP (10) [u].[id], [u].[name] "
		"FROM [dbo].[users] AS [u] "
		"WHERE [u].[id] = @id "
		"ORDER BY [u].[id]";

	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));

	/* SQL Server 不是默认方言，需要通过 parse options 显式指定。 */
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;

	/* 解析时会把 [] 标识符、@ 参数和 TOP 转成核心 AST 可接受的形式。 */
	status = sqlparser_parse_with_options(sql, &options, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	printf("dialect: %s\n", sqlparser_dialect_name(sqlparser_handle_dialect(handle)));

	/* view JSON 用于读取表、列、值片段和可回写 selector。 */
	status = sqlparser_export_view_json(handle, 1, &view_json, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "view export failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("view json:\n%s\n", view_json);

	/*
	 * 反解析会还原 SQL Server 公共形态，不会暴露内部 $1 参数。
	 * 输出不保证逐字符等同于输入，但应保持语义等价。
	 */
	status = sqlparser_deparse(handle, &deparsed_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err.message);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("deparsed sql:\n%s\n", deparsed_sql);

	if (strstr(deparsed_sql, "TOP (10)") == NULL ||
	    strstr(deparsed_sql, "@id") == NULL ||
	    strstr(deparsed_sql, "$1") != NULL) {
		fprintf(stderr, "unexpected SQL Server deparse output\n");
		sqlparser_string_free(deparsed_sql);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	sqlparser_string_free(deparsed_sql);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}
