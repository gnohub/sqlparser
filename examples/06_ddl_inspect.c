#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_statement_kind_t kind;
	sqlparser_name_view_t name;
	const char *node_name;
	char *deparsed_sql;
	char *parse_tree_json;
	size_t name_count;
	size_t index;
	size_t view_name_index;
	int status;

	sql = "DROP VIEW IF EXISTS public.v_orders";
	handle = NULL;
	deparsed_sql = NULL;
	parse_tree_json = NULL;
	view_name_index = (size_t)-1;
	memset(&err, 0, sizeof(err));
	memset(&name, 0, sizeof(name));

	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/* 对 DDL，第一步至少要能稳定识别 statement kind 和具体节点名。 */
	status = sqlparser_statement_kind(handle, 0U, &kind, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_kind failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = sqlparser_statement_node_name(handle, 0U, &node_name, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_node_name failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("ddl kind: %s\n", sqlparser_statement_kind_name(kind));
	printf("ddl node: %s\n", node_name);

	/*
	 * 对 DDL，通用 name 层会把对象名列表稳定吐出来。
	 * DROP VIEW public.v_orders 这里通常能看到 public 和 v_orders 两个原子。
	 */
	status = sqlparser_statement_name_count(handle, 0U, &name_count, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_name_count failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	for (index = 0U; index < name_count; index++) {
		status = sqlparser_statement_name(handle, 0U, index, &name, &err);
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "statement_name failed: %s\n", err.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		printf("name[%lu]: owner=%s field=%s value=%s\n",
		       (unsigned long)index,
		       name.owner_type != NULL ? name.owner_type : "(null)",
		       name.field_name != NULL ? name.field_name : "(null)",
		       name.value != NULL ? name.value : "(null)");

		if (name.value != NULL && strcmp(name.value, "v_orders") == 0) {
			view_name_index = index;
		}
	}

	/*
	 * 这里演示的是核心层精确改写：按 name_index 直接把视图名改掉，
	 * 然后再统一走 deparse，恢复成新的 SQL。
	 */
	if (view_name_index != (size_t)-1) {
		status = sqlparser_statement_set_name(handle, 0U, view_name_index, "v_orders_archive", &err);
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "statement_set_name failed: %s\n", err.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}

	status = sqlparser_deparse(handle, &deparsed_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("rewritten sql: %s\n", deparsed_sql);
	sqlparser_string_free(deparsed_sql);

	status = sqlparser_export_parse_tree_json(handle, 1, &parse_tree_json, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "export_parse_tree_json failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("%s\n", parse_tree_json);
	sqlparser_string_free(parse_tree_json);
	sqlparser_handle_destroy(handle);
	return 0;
}
