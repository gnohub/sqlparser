#ifndef SQLPARSER_DIALECT_MULTI_INSERT_INTERNAL_H
#define SQLPARSER_DIALECT_MULTI_INSERT_INTERNAL_H

#include "sqlparser_dialect_multi_insert_types.h"

int sqlparser_dialect_state_has_multi_insert(sqlparser_dialect_t dialect, const void *state);
const sqlparser_dialect_multi_insert_t *sqlparser_dialect_state_multi_insert(
	sqlparser_dialect_t dialect,
	const void *state);
sqlparser_status_t sqlparser_dialect_multi_insert_set_cell_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dialect_multi_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dialect_multi_insert_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dialect_multi_insert_set_cell_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dialect_multi_insert_set_cell_bind(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const sqlparser_bind_value_t *bind,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dialect_multi_insert_insert_column_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *column_sql,
	const char *cell_sql,
	sqlparser_error_t *out_error);

#endif
