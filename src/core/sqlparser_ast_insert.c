#include <stdlib.h>

#include "sqlparser_ast_internal.h"

static sqlparser_status_t sqlparser_get_insert_cell_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__Node **out_value_node,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_get_insert_cell_slot(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__Node ***out_value_slot,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_get_insert_column_res_target(
	PgQuery__InsertStmt *stmt,
	size_t column_index,
	PgQuery__ResTarget **out_target,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *column_node;

	if (out_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_target must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_target = NULL;
	if (stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (column_index >= stmt->n_cols) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"column_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	column_node = stmt->cols[column_index];
	if (column_node == NULL ||
	    column_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    column_node->res_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"insert column node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_target = column_node->res_target;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_insert_cell_a_const(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__AConst **out_literal,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *value_node;
	sqlparser_status_t status;

	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_literal = NULL;
	status = sqlparser_get_insert_cell_node(
		handle,
		statement_index,
		row_index,
		column_index,
		&value_node,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    value_node->a_const == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"insert cell is not a literal");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	*out_literal = value_node->a_const;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_insert_cell_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__Node **out_value_node,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **value_slot;
	sqlparser_status_t status;

	if (out_value_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_value_node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_value_node = NULL;
	value_slot = NULL;
	status = sqlparser_get_insert_cell_slot(
		handle,
		statement_index,
		row_index,
		column_index,
		&value_slot,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (value_slot == NULL || *value_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"insert cell node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_value_node = *value_slot;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_insert_cell_slot(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__Node ***out_value_slot,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node *row_node;
	PgQuery__List *row_list;
	sqlparser_status_t status;

	if (out_value_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_value_slot must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_value_slot = NULL;
	status = sqlparser_get_insert_values_stmt(
		handle,
		statement_index,
		&insert_stmt,
		&values_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	(void)insert_stmt;

	if (row_index >= values_stmt->n_values_lists) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"row_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	row_node = values_stmt->values_lists[row_index];
	if (row_node == NULL ||
	    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
	    row_node->list == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"insert row node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	row_list = row_node->list;
	if (column_index >= row_list->n_items) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"column_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_value_slot = &row_list->items[column_index];
	return SQLPARSER_STATUS_OK;
}


sqlparser_status_t sqlparser_insert_source_kind(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_insert_source_kind_t *out_kind,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_kind == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_kind must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_kind = SQLPARSER_INSERT_SOURCE_UNKNOWN;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_kind = sqlparser_insert_source_from_stmt(insert_stmt);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_column_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = insert_stmt->n_cols;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_column_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t column_index,
	const char **out_column_name,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__ResTarget *target;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_column_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_column_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_column_name = NULL;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_insert_column_res_target(insert_stmt, column_index, &target, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_column_name = target->name != NULL ? target->name : NULL;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_row_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_insert_source_from_stmt(insert_stmt) == SQLPARSER_INSERT_SOURCE_VALUES &&
	    insert_stmt->select_stmt != NULL &&
	    insert_stmt->select_stmt->select_stmt != NULL) {
		*out_count = insert_stmt->select_stmt->select_stmt->n_values_lists;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_cell_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	PgQuery__AConst *literal;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_literal_view_clear(out_literal);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_cell_a_const(
		mutable_handle,
		statement_index,
		row_index,
		column_index,
		&literal,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_fill_literal_view_from_a_const(literal, out_literal, out_error);
}

sqlparser_status_t sqlparser_insert_set_cell_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	PgQuery__AConst *literal;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	status = sqlparser_get_insert_cell_a_const(
		handle,
		statement_index,
		row_index,
		column_index,
		&literal,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_a_const_set_literal(literal, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *value_node;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_cell_node(
		mutable_handle,
		statement_index,
		row_index,
		column_index,
		&value_node,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_render_insert_cell_node_sql(value_node, out_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_validate_handle_output_text(handle, *out_sql, "insert cell SQL", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_sql);
		*out_sql = NULL;
		return status;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_set_cell_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **value_slot;
	PgQuery__Node *replacement;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	value_slot = NULL;
	replacement = NULL;
	status = sqlparser_validate_handle_sql_input(handle, sql_text, "insert cell SQL", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_parse_insert_cell_node_sql(sql_text, &replacement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_insert_cell_slot(
		handle,
		statement_index,
		row_index,
		column_index,
		&value_slot,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(replacement);
		return status;
	}

	sqlparser_free_proto_node(*value_slot);
	*value_slot = replacement;
	replacement = NULL;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(replacement);
		return status;
	}

	return SQLPARSER_STATUS_OK;
}
