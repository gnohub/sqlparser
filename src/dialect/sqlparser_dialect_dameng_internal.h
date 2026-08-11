#ifndef SQLPARSER_DIALECT_DAMENG_INTERNAL_H
#define SQLPARSER_DIALECT_DAMENG_INTERNAL_H

#include "sqlparser_dialect_dml_result_internal.h"
#include "sqlparser_dialect_multi_insert_types.h"

int sqlparser_dameng_state_has_multi_insert(const void *state);
const sqlparser_dialect_multi_insert_t *sqlparser_dameng_state_multi_insert(const void *state);
const sqlparser_dialect_returning_into_state_t *
sqlparser_dameng_state_returning_into(const void *state);
sqlparser_status_t sqlparser_dameng_render_bind_value(
	const sqlparser_bind_value_t *bind,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_render_literal_value(
	const sqlparser_literal_value_t *value,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_multi_insert_set_cell_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_multi_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_multi_insert_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	char **out_sql,
	sqlparser_error_t *out_error);
sqlparser_status_t sqlparser_dameng_multi_insert_insert_column_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *column_sql,
	const char *cell_sql,
	sqlparser_error_t *out_error);

#endif
