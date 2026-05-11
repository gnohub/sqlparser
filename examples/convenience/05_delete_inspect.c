#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_relation_view_t relation;
	sqlparser_where_literal_view_t where_literal;
	sqlparser_literal_value_t new_literal;
	char *deparsed_sql;
	size_t where_count;
	size_t index;
	int status;

	sql = "DELETE FROM public.users WHERE status = 'inactive' AND age > 30";
	handle = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));
	memset(&relation, 0, sizeof(relation));
	memset(&where_literal, 0, sizeof(where_literal));
	memset(&new_literal, 0, sizeof(new_literal));

	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/* DELETE 先看目标表。 */
	status = sqlparser_statement_target_relation(handle, 0U, &relation, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_target_relation failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	printf("delete target: %s.%s\n", relation.schema_name, relation.table_name);

	/* 再用核心层直接枚举 WHERE 里的 literal。 */
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

		if (where_literal.literal.kind == SQLPARSER_LITERAL_KIND_STRING) {
			printf("where literal[%lu]: column=%s value=%s\n",
			       (unsigned long)index,
			       where_literal.column_name,
			       where_literal.literal.string_value);
		} else if (where_literal.literal.kind == SQLPARSER_LITERAL_KIND_INTEGER) {
			printf("where literal[%lu]: column=%s value=%lld\n",
			       (unsigned long)index,
			       where_literal.column_name,
			       where_literal.literal.integer_value);
		}
	}

	/* 这里把 status = 'inactive' 改成 status = 'archived'。 */
	new_literal.kind = SQLPARSER_LITERAL_KIND_STRING;
	new_literal.string_value = "archived";
	status = sqlparser_statement_where_set_literal(handle, 0U, 0U, &new_literal, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "statement_where_set_literal failed: %s\n", err.message);
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
	sqlparser_string_free(deparsed_sql);
	sqlparser_handle_destroy(handle);
	return 0;
}
