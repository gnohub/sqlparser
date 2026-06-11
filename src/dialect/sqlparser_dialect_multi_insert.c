#include "sqlparser_dialect_internal.h"
#include "sqlparser_dialect_dameng_internal.h"
#include "sqlparser_dialect_multi_insert_internal.h"
#include "sqlparser_dialect_oracle_internal.h"

int sqlparser_dialect_state_has_multi_insert(sqlparser_dialect_t dialect, const void *state)
{
	if (sqlparser_dialect_is_oracle_compatible(dialect)) {
		return sqlparser_oracle_state_has_multi_insert(state);
	}
	if (dialect == SQLPARSER_DIALECT_DAMENG) {
		return sqlparser_dameng_state_has_multi_insert(state);
	}
	return 0;
}

const sqlparser_dialect_multi_insert_t *sqlparser_dialect_state_multi_insert(
	sqlparser_dialect_t dialect,
	const void *state)
{
	if (sqlparser_dialect_is_oracle_compatible(dialect)) {
		return sqlparser_oracle_state_multi_insert(state);
	}
	if (dialect == SQLPARSER_DIALECT_DAMENG) {
		return sqlparser_dameng_state_multi_insert(state);
	}
	return NULL;
}

sqlparser_status_t sqlparser_dialect_multi_insert_set_cell_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	if (handle != NULL && sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
		return sqlparser_oracle_multi_insert_set_cell_sql(
			handle,
			statement_index,
			branch_index,
			column_index,
			sql_text,
			out_error);
	}
	if (handle != NULL && handle->dialect == SQLPARSER_DIALECT_DAMENG) {
		return sqlparser_dameng_multi_insert_set_cell_sql(
			handle,
			statement_index,
			branch_index,
			column_index,
			sql_text,
			out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a multi-table INSERT cell");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}

sqlparser_status_t sqlparser_dialect_multi_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (handle != NULL && sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
		return sqlparser_oracle_multi_insert_cell_sql(
			handle,
			statement_index,
			branch_index,
			column_index,
			out_sql,
			out_error);
	}
	if (handle != NULL && handle->dialect == SQLPARSER_DIALECT_DAMENG) {
		return sqlparser_dameng_multi_insert_cell_sql(
			handle,
			statement_index,
			branch_index,
			column_index,
			out_sql,
			out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a multi-table INSERT cell");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}

sqlparser_status_t sqlparser_dialect_multi_insert_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (handle != NULL && sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
		return sqlparser_oracle_multi_insert_condition_sql(
			handle,
			statement_index,
			branch_index,
			out_sql,
			out_error);
	}
	if (handle != NULL && handle->dialect == SQLPARSER_DIALECT_DAMENG) {
		return sqlparser_dameng_multi_insert_condition_sql(
			handle,
			statement_index,
			branch_index,
			out_sql,
			out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a multi-table INSERT condition");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}

sqlparser_status_t sqlparser_dialect_multi_insert_set_cell_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	if (handle != NULL && sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
		return sqlparser_oracle_multi_insert_set_cell_literal(
			handle,
			statement_index,
			branch_index,
			column_index,
			value,
			out_error);
	}
	if (handle != NULL && handle->dialect == SQLPARSER_DIALECT_DAMENG) {
		return sqlparser_dameng_multi_insert_set_cell_literal(
			handle,
			statement_index,
			branch_index,
			column_index,
			value,
			out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a multi-table INSERT cell");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}

sqlparser_status_t sqlparser_dialect_multi_insert_set_cell_bind(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const sqlparser_bind_value_t *bind,
	sqlparser_error_t *out_error)
{
	if (handle != NULL && sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
		return sqlparser_oracle_multi_insert_set_cell_bind(
			handle,
			statement_index,
			branch_index,
			column_index,
			bind,
			out_error);
	}
	if (handle != NULL && handle->dialect == SQLPARSER_DIALECT_DAMENG) {
		return sqlparser_dameng_multi_insert_set_cell_bind(
			handle,
			statement_index,
			branch_index,
			column_index,
			bind,
			out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a multi-table INSERT cell");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}

sqlparser_status_t sqlparser_dialect_multi_insert_insert_column_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *column_sql,
	const char *cell_sql,
	sqlparser_error_t *out_error)
{
	if (handle != NULL && sqlparser_dialect_is_oracle_compatible(handle->dialect)) {
		return sqlparser_oracle_multi_insert_insert_column_sql(
			handle,
			statement_index,
			branch_index,
			column_index,
			column_sql,
			cell_sql,
			out_error);
	}
	if (handle != NULL && handle->dialect == SQLPARSER_DIALECT_DAMENG) {
		return sqlparser_dameng_multi_insert_insert_column_sql(
			handle,
			statement_index,
			branch_index,
			column_index,
			column_sql,
			cell_sql,
			out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not a multi-table INSERT branch");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}
