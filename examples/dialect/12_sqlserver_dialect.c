#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_control_flow_view_t control_flow;
	sqlparser_error_t err;
	char *view_json;
	char *deparsed_sql;
	int status;

	sql =
		"IF @a = 1 "
		"UPDATE dbo.t SET a = 2 "
		"OUTPUT DELETED.a AS old_a, INSERTED.a AS new_a "
		"WHERE id = @id "
		"ELSE "
		"INSERT dbo.t(a) OUTPUT INSERTED.id VALUES (1)";

	handle = NULL;
	view_json = NULL;
	deparsed_sql = NULL;
	memset(&control_flow, 0, sizeof(control_flow));
	memset(&err, 0, sizeof(err));

	/* SQL Server 不是默认方言，需要通过 parse options 显式指定。 */
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;

	/* 解析 SQL Server 控制流以及分支中的 DML OUTPUT。 */
	status = sqlparser_parse_with_options(sql, &options, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	printf("dialect: %s\n", sqlparser_dialect_name(sqlparser_handle_dialect(handle)));
	/* 读取 IF 节点、分支和分支语句组成的只读控制流拓扑。 */
	status = sqlparser_handle_control_flow(handle, &control_flow, &err);
	if (status != SQLPARSER_STATUS_OK ||
	    control_flow.node_count != 1U ||
	    control_flow.branch_count != 2U) {
		fprintf(stderr, "control flow inspect failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("control nodes: %lu, branches: %lu\n",
	       (unsigned long)control_flow.node_count,
	       (unsigned long)control_flow.branch_count);

	/* View JSON 同时展示控制流拓扑、结果通道和可回写 selector。 */
	status = sqlparser_export_view_json(handle, 1, &view_json, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "view export failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("view json:\n%s\n", view_json);

	/*
	 * 反解析会还原 IF/ELSE、OUTPUT 和 SQL Server 参数，不暴露内部形态。
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

	if (strstr(view_json, "\"control_flow\"") == NULL ||
	    strstr(view_json, "\"result_channels\"") == NULL ||
	    strstr(deparsed_sql, "OUTPUT DELETED.a AS old_a, INSERTED.a AS new_a") == NULL ||
	    strstr(deparsed_sql, "OUTPUT INSERTED.id") == NULL ||
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
