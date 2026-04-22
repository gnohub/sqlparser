#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	size_t index;
	size_t statement_count;
	int status;

	sql = "BEGIN; "
	      "INSERT INTO public.users (id, name) VALUES (1, 'bob'); "
	      "UPDATE public.users SET name = 'carol' WHERE id = 1; "
	      "COMMIT";
	handle = NULL;
	memset(&err, 0, sizeof(err));

	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	statement_count = sqlparser_statement_count(handle);
	for (index = 0U; index < statement_count; index++) {
		sqlparser_statement_kind_t kind;
		const char *node_name;

		/* 多语句场景下，所有核心接口都靠 stmt_index 精确定位。 */
		status = sqlparser_statement_kind(handle, index, &kind, &err);
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "statement_kind failed: %s\n", err.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		status = sqlparser_statement_node_name(handle, index, &node_name, &err);
		if (status != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "statement_node_name failed: %s\n", err.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}

		printf("stmt[%lu]: kind=%s node=%s\n",
		       (unsigned long)index,
		       sqlparser_statement_kind_name(kind),
		       node_name);
	}

	sqlparser_handle_destroy(handle);
	return 0;
}
