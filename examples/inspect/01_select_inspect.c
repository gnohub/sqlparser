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
	sqlparser_where_literal_view_t where_literal;
	char *view_json;
	size_t name_count;
	size_t name_index;
	size_t where_count;
	size_t index;
	int status;

	sql = "SELECT u.id, u.name, o.order_no FROM public.users u "
	      "JOIN public.orders o ON u.id = o.user_id "
	      "WHERE o.status = 'paid'";
	handle = NULL;
	view_json = NULL;
	memset(&err, 0, sizeof(err));
	memset(&name, 0, sizeof(name));
	memset(&where_literal, 0, sizeof(where_literal));

	/* 第一步：把一条完整 SQL 解析成可复用 handle。 */
	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/* 第二步：先看语句大类，再看更精确的节点名。 */
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

	printf("statement kind: %s\n", sqlparser_statement_kind_name(kind));
	printf("statement node: %s\n", node_name);

	/*
	 * 通用 name 层会把 statement 里的“名称原子”枚举出来。
	 * 这里能直接看到多表查询中的 schema / table / alias / column_ref 等名字。
	 */
	status = sqlparser_statement_name_count(handle, 0U, &name_count, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_name_count failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	for (name_index = 0U; name_index < name_count; name_index++) {
		status = sqlparser_statement_name(handle, 0U, name_index, &name, &err);
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "statement_name failed: %s\n", err.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		/*
		 * name 层是低层 primitive。
		 * 真正给业务侧展示时，通常要按 owner_type / field_name 做一次过滤。
		 */
		if (!((name.owner_type != NULL && strcmp(name.owner_type, "ColumnRef") == 0 &&
		       name.field_name != NULL && strcmp(name.field_name, "fields") == 0) ||
		      (name.owner_type != NULL && strcmp(name.owner_type, "RangeVar") == 0 &&
		       name.field_name != NULL &&
		       (strcmp(name.field_name, "schemaname") == 0 ||
		        strcmp(name.field_name, "relname") == 0)) ||
		      (name.owner_type != NULL && strcmp(name.owner_type, "Alias") == 0 &&
		       name.field_name != NULL && strcmp(name.field_name, "aliasname") == 0))) {
			continue;
		}

		printf("name[%lu]: owner=%s field=%s value=%s\n",
		       (unsigned long)name_index,
		       name.owner_type != NULL ? name.owner_type : "(null)",
		       name.field_name != NULL ? name.field_name : "(null)",
		       name.value != NULL ? name.value : "(null)");
	}

	/*
	 * 核心层已经可以直接遍历 WHERE 里的 literal。
	 * 这里输出的是“第几个 literal、它对应哪个列、当前值是什么”。
	 */
	status = sqlparser_statement_where_literal_count(handle, 0U, &where_count, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_where_literal_count failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	for (index = 0U; index < where_count; index++) {
		status = sqlparser_statement_where_literal(handle, 0U, index, &where_literal, &err);
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "statement_where_literal failed: %s\n", err.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		printf("where literal[%lu]: column=%s operator=%s value=%s\n",
		       (unsigned long)index,
		       where_literal.column_name != NULL ? where_literal.column_name : "(null)",
		       where_literal.operator_name != NULL ? where_literal.operator_name : "(null)",
		       where_literal.literal.string_value != NULL ? where_literal.literal.string_value : "(null)");
	}

	/*
	 * view JSON 是按表、列和值片段组织后的结构化输出。
	 */
	status = sqlparser_export_view_json(handle, 1, &view_json, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "export_view_json failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("%s\n", view_json);
	sqlparser_string_free(view_json);
	sqlparser_handle_destroy(handle);
	return 0;
}
