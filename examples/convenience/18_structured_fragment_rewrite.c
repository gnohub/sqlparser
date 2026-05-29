#include <stdio.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

static int reparse_current_sql(sqlparser_handle_t *handle, sqlparser_error_t *err)
{
	sqlparser_parse_options_t options;
	sqlparser_handle_t *reparsed;
	char *sql;
	int status;

	sql = NULL;
	reparsed = NULL;
	status = sqlparser_deparse(handle, &sql, err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "deparse failed: %s\n", err->message);
		return 1;
	}

	sqlparser_parse_options_default(&options);
	options.dialect = sqlparser_handle_dialect(handle);
	status = sqlparser_parse_with_options(sql, &options, &reparsed, err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "reparse failed: %s\n", err->message);
		sqlparser_string_free(sql);
		return 1;
	}

	printf("rewritten sql: %s\n", sql);
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(sql);
	return 0;
}

static int rewrite_update_backup_column(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_parse_options_t options;
	sqlparser_selector_t insert_selector;
	sqlparser_selector_t source_selector;
	const char *backup_parts[] = {"phone_backup"};
	sqlparser_identifier_path_view_t backup_column;
	int status;

	handle = NULL;
	memset(&err, 0, sizeof(err));
	memset(&insert_selector, 0, sizeof(insert_selector));
	memset(&source_selector, 0, sizeof(source_selector));

	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	status = sqlparser_parse_with_options("UPDATE users SET phone = ? WHERE id = ?", &options, &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse update failed: %s\n", err.message);
		return 1;
	}

	/* assignment[0] 是 phone = ?；这里在它前面插入 phone_backup = 原右值。 */
	status = sqlparser_selector_parse("stmt[0].assignment[0]", &insert_selector, &err);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_selector_parse("stmt[0].assignment[0]", &source_selector, &err);
	}
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "selector parse failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	backup_column.parts = backup_parts;
	backup_column.part_count = 1U;
	status = sqlparser_selector_insert_update_assignment_from_assignment_value(
		handle,
		&insert_selector,
		&backup_column,
		&source_selector,
		&err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "structured update rewrite failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = reparse_current_sql(handle, &err);
	sqlparser_handle_destroy(handle);
	return status;
}

static int rewrite_select_star(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t err;
	sqlparser_selector_t target_selector;
	const char *id_parts[] = {"u", "id"};
	const char *name_parts[] = {"u", "name"};
	const char *phone_parts[] = {"u", "phone"};
	sqlparser_identifier_path_view_t columns[3];
	int status;

	handle = NULL;
	memset(&err, 0, sizeof(err));
	memset(&target_selector, 0, sizeof(target_selector));

	status = sqlparser_parse("SELECT u.* FROM users u WHERE u.status = 'active'", &handle, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "parse select failed: %s\n", err.message);
		return 1;
	}

	/* select_target[0][0] 是 u.*；调用方给出展开后的列，sqlparser 负责生成列 target。 */
	status = sqlparser_selector_parse("stmt[0].select_target[0][0]", &target_selector, &err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "selector parse failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	columns[0].parts = id_parts;
	columns[0].part_count = 2U;
	columns[1].parts = name_parts;
	columns[1].part_count = 2U;
	columns[2].parts = phone_parts;
	columns[2].part_count = 2U;
	status = sqlparser_selector_replace_select_target_with_columns(
		handle,
		&target_selector,
		columns,
		3U,
		&err);
	if (status != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "structured select rewrite failed: %s\n", err.message);
		sqlparser_handle_destroy(handle);
		return 1;
	}

	status = reparse_current_sql(handle, &err);
	sqlparser_handle_destroy(handle);
	return status;
}

int main(void)
{
	if (rewrite_update_backup_column() != 0) {
		return 1;
	}
	if (rewrite_select_star() != 0) {
		return 1;
	}
	return 0;
}
