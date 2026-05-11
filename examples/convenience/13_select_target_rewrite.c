#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

int main(void)
{
	const char *sql;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_error_t err;
	char *target_sql;
	char *deparsed_sql;
	size_t target_count;
	int status;

	sql = "SELECT * FROM public.users WHERE status = 'active'";
	handle = NULL;
	reparsed = NULL;
	target_sql = NULL;
	deparsed_sql = NULL;
	memset(&err, 0, sizeof(err));

	/* 第一步：解析完整 SQL，后续 SELECT 列表改写都直接作用在这个 handle 上。 */
	status = sqlparser_parse(sql, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse failed: %s\n", err.message);
		return 1;
	}

	/* 第二步：读取当前 SELECT target list。SELECT * 只有一个 target。 */
	status = sqlparser_select_target_count(handle, 0U, 0U, &target_count, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "select_target_count failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("old target count: %lu\n", (unsigned long)target_count);

	status = sqlparser_select_target_sql(handle, 0U, 0U, 0U, &target_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "select_target_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("old first target: %s\n", target_sql);
	sqlparser_string_free(target_sql);
	target_sql = NULL;

	/* 第三步：把 SELECT * 替换成明确列列表。 */
	status = sqlparser_select_set_targets_sql(handle, 0U, 0U, "id, name, age", &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "select_set_targets_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 第四步：在 SELECT 列表中插入一个表达式列。 */
	status = sqlparser_select_insert_target_sql(handle, 0U, 0U, 2U, "upper(name) AS normalized_name", &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "select_insert_target_sql failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 第五步：删除不再需要的 age 列。 */
	status = sqlparser_select_delete_target(handle, 0U, 0U, 3U, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "select_delete_target failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	/* 第六步：反解析成改写后的 SQL。 */
	status = sqlparser_deparse(handle, &deparsed_sql, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}
	printf("rewritten sql: %s\n", deparsed_sql);

	/* 第七步：把改写后的 SQL 再解析一次，验证生成结果仍是合法 SQL。 */
	status = sqlparser_parse(deparsed_sql, &reparsed, &err);
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
