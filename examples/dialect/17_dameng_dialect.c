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
		"SET SCHEMA KDES; "
		"SELECT TOP 2 u.id, u.name "
		"FROM users u WHERE u.id = :id "
		"ORDER BY u.id";

	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));

	/* 达梦不是默认方言，调用方需要通过 parse options 显式指定。 */
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_DAMENG;

	/* 解析时会把达梦 schema 切换、TOP 和 :name bind 转成核心 AST 可接受的形式。 */
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
	 * 反解析输出达梦可接受的公共 SQL，不暴露内部 $1 参数。
	 * TOP 会被规范化为达梦同样支持的 LIMIT 形式。
	 */
	status = sqlparser_deparse(handle, &deparsed_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err.message);
		sqlparser_string_free(view_json);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("deparsed sql:\n%s\n", deparsed_sql);

	if (strstr(deparsed_sql, ":id") == NULL ||
	    strstr(deparsed_sql, "LIMIT 2") == NULL ||
	    strstr(deparsed_sql, "$1") != NULL) {
		fprintf(stderr, "unexpected Dameng deparse output\n");
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
