#include <stdlib.h>

#include "sqlparser_ast_internal.h"

static sqlparser_status_t sqlparser_get_update_assignment_res_target(
	PgQuery__UpdateStmt *stmt,
	size_t assignment_index,
	PgQuery__ResTarget **out_target,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *target_node;

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

	if (assignment_index >= stmt->n_target_list) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"assignment_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	target_node = stmt->target_list[assignment_index];
	if (target_node == NULL ||
	    target_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    target_node->res_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"update assignment node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_target = target_node->res_target;
	return SQLPARSER_STATUS_OK;
}


sqlparser_status_t sqlparser_update_assignment_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
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
	status = sqlparser_get_update_stmt(mutable_handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = update_stmt->n_target_list;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_assignment(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__ResTarget *target;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_assignment == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_assignment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_assignment_view_clear(out_assignment);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_update_stmt(mutable_handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_update_assignment_res_target(
		update_stmt,
		assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	out_assignment->column_name = target->name != NULL ? target->name : NULL;
	out_assignment->value_kind = sqlparser_node_value_kind(target->val);
	if (out_assignment->value_kind == SQLPARSER_VALUE_KIND_LITERAL) {
		return sqlparser_fill_literal_view_from_a_const(
			target->val->a_const,
			&out_assignment->literal,
			out_error);
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_set_assignment_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__ResTarget *target;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	status = sqlparser_get_update_stmt(handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_update_assignment_res_target(
		update_stmt,
		assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (target->val == NULL ||
	    target->val->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    target->val->a_const == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"update assignment is not a literal");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	status = sqlparser_a_const_set_literal(target->val->a_const, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_update_assignment_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__ResTarget *target;
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
	status = sqlparser_get_update_stmt(mutable_handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_update_assignment_res_target(
		update_stmt,
		assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (target->val == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"update assignment value is missing");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	status = sqlparser_render_update_assignment_node_sql(target->val, out_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_validate_handle_output_text(handle, *out_sql, "update assignment SQL", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_sql);
		*out_sql = NULL;
		return status;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_set_assignment_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__ResTarget *target;
	PgQuery__Node *replacement;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	replacement = NULL;
	status = sqlparser_validate_handle_sql_input(handle, sql_text, "update assignment SQL", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_update_stmt(handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_update_assignment_res_target(
		update_stmt,
		assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_parse_update_assignment_node_sql(sql_text, &replacement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	sqlparser_free_proto_node(target->val);
	target->val = replacement;
	return sqlparser_handle_commit_ast(handle, out_error);
}
