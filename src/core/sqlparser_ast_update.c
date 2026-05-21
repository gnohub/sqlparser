#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"

static int sqlparser_update_stmt_has_target_list(ProtobufCMessage *message)
{
	PgQuery__UpdateStmt *stmt;

	if (message == NULL || message->descriptor != &pg_query__update_stmt__descriptor) {
		return 0;
	}
	stmt = (PgQuery__UpdateStmt *)message;
	return stmt->n_target_list > 0U && stmt->target_list != NULL;
}

static sqlparser_status_t sqlparser_get_update_stmt_by_target_list_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	PgQuery__UpdateStmt **out_stmt,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	sqlparser_status_t status;

	if (out_stmt == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_stmt must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_stmt = NULL;
	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		statement_index,
		&pg_query__update_stmt__descriptor,
		sqlparser_update_stmt_has_target_list,
		1,
		target_list_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "update SET list selector is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_stmt = (PgQuery__UpdateStmt *)message;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_update_free_node_array(PgQuery__Node **nodes, size_t count)
{
	size_t index;

	if (nodes == NULL) {
		return;
	}
	for (index = 0U; index < count; index++) {
		sqlparser_free_proto_node(nodes[index]);
	}
	free(nodes);
}

static PgQuery__Node **sqlparser_update_alloc_node_array(size_t count, sqlparser_error_t *out_error)
{
	PgQuery__Node **nodes;

	if (count == 0U || count > ((size_t)-1) / sizeof(*nodes)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "update SET list is too large");
		return NULL;
	}
	nodes = (PgQuery__Node **)calloc(count, sizeof(*nodes));
	if (nodes == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return nodes;
}

static int sqlparser_update_append_text(
	char **buffer,
	size_t *len,
	size_t *capacity,
	const char *text,
	sqlparser_error_t *out_error)
{
	size_t text_len;
	size_t required;
	size_t next_capacity;
	char *next;

	if (buffer == NULL || len == NULL || capacity == NULL || text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "append text arguments must not be NULL");
		return -1;
	}
	text_len = strlen(text);
	if (text_len > ((size_t)-1) - *len - 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return -1;
	}
	required = *len + text_len + 1U;
	if (required > *capacity) {
		next_capacity = *capacity == 0U ? 128U : *capacity;
		while (next_capacity < required) {
			if (next_capacity > ((size_t)-1) / 2U) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return -1;
			}
			next_capacity *= 2U;
		}
		next = (char *)realloc(*buffer, next_capacity);
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return -1;
		}
		*buffer = next;
		*capacity = next_capacity;
	}
	memcpy(*buffer + *len, text, text_len);
	*len += text_len;
	(*buffer)[*len] = '\0';
	return 0;
}

static sqlparser_status_t sqlparser_parse_update_assignment_nodes_sql(
	const char *sql_text,
	PgQuery__Node ***out_nodes,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	static const char prefix[] = "UPDATE __sqlparser_target__ SET ";
	char *wrapped_sql;
	PgQuery__ParseResult *ast;
	PgQuery__RawStmt *raw_stmt;
	PgQuery__Node *statement;
	PgQuery__UpdateStmt *stmt;
	PgQuery__Node **nodes;
	size_t index;
	sqlparser_status_t status;

	if (sql_text == NULL || sql_text[0] == '\0' || out_nodes == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "update SET list SQL must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_nodes = NULL;
	*out_count = 0U;
	wrapped_sql = NULL;
	ast = NULL;
	nodes = NULL;

	status = sqlparser_build_wrapped_sql(prefix, sql_text, NULL, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		free(wrapped_sql);
		return status;
	}

	if (ast->n_stmts != 1U || ast->stmts == NULL || ast->stmts[0] == NULL) {
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
		sqlparser_error_set_message(out_error, status, "wrapped update SET parse tree is invalid");
		goto done;
	}
	raw_stmt = ast->stmts[0];
	statement = raw_stmt->stmt;
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_UPDATE_STMT ||
	    statement->update_stmt == NULL) {
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
		sqlparser_error_set_message(out_error, status, "wrapped update SET parse tree does not contain UPDATE");
		goto done;
	}
	stmt = statement->update_stmt;
	if (stmt->n_target_list == 0U || stmt->target_list == NULL) {
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
		sqlparser_error_set_message(out_error, status, "wrapped update SET parse tree does not contain assignments");
		goto done;
	}

	nodes = sqlparser_update_alloc_node_array(stmt->n_target_list, out_error);
	if (nodes == NULL) {
		status = out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_NO_MEMORY;
		goto done;
	}
	for (index = 0U; index < stmt->n_target_list; index++) {
		status = sqlparser_clone_proto_node(stmt->target_list[index], &nodes[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_update_free_node_array(nodes, stmt->n_target_list);
			nodes = NULL;
			goto done;
		}
	}

	*out_nodes = nodes;
	*out_count = stmt->n_target_list;
	nodes = NULL;
	status = SQLPARSER_STATUS_OK;

done:
	sqlparser_update_free_node_array(nodes, ast != NULL && ast->n_stmts == 1U && ast->stmts != NULL &&
			ast->stmts[0] != NULL && ast->stmts[0]->stmt != NULL &&
			ast->stmts[0]->stmt->node_case == PG_QUERY__NODE__NODE_UPDATE_STMT &&
			ast->stmts[0]->stmt->update_stmt != NULL ?
		ast->stmts[0]->stmt->update_stmt->n_target_list :
		0U);
	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	return status;
}

static sqlparser_status_t sqlparser_parse_single_update_assignment_sql(
	const char *sql_text,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **nodes;
	size_t count;
	sqlparser_status_t status;

	if (out_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node = NULL;
	nodes = NULL;
	count = 0U;
	status = sqlparser_parse_update_assignment_nodes_sql(sql_text, &nodes, &count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (count != 1U) {
		sqlparser_update_free_node_array(nodes, count);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"update assignment patch expects exactly one assignment");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	*out_node = nodes[0];
	nodes[0] = NULL;
	sqlparser_update_free_node_array(nodes, count);
	return SQLPARSER_STATUS_OK;
}

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

	if (stmt->target_list == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"update assignment list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
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

static sqlparser_status_t sqlparser_validate_update_assignment_nodes(
	const PgQuery__UpdateStmt *stmt,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (stmt == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "statement must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (stmt->n_target_list == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (stmt->target_list == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "update assignment list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	for (index = 0U; index < stmt->n_target_list; index++) {
		PgQuery__Node *node;

		node = stmt->target_list[index];
		if (node == NULL ||
		    node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
		    node->res_target == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "update assignment node is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
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
	char *core_sql;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	core_sql = NULL;
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

	status = sqlparser_render_update_assignment_node_sql(target->val, &core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		core_sql,
		"update assignment SQL",
		out_sql,
		out_error);
	free(core_sql);
	if (status != SQLPARSER_STATUS_OK) {
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
	char *parser_sql;
	void *dialect_state;

	sqlparser_error_clear(out_error);
	parser_sql = NULL;
	dialect_state = NULL;
	replacement = NULL;
	status = sqlparser_preprocess_handle_sql_fragment(
		handle,
		sql_text,
		"update assignment SQL",
		&parser_sql,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_update_stmt(handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(parser_sql);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	status = sqlparser_get_update_assignment_res_target(
		update_stmt,
		assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(parser_sql);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	status = sqlparser_parse_update_assignment_node_sql(parser_sql, &replacement, out_error);
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	sqlparser_free_proto_node(target->val);
	target->val = replacement;
	replacement = NULL;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_insert_assignment_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const char *assignment_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__Node **next_nodes;
	PgQuery__Node **old_nodes;
	PgQuery__Node *new_node;
	size_t old_count;
	size_t index;
	char *parser_sql;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	update_stmt = NULL;
	next_nodes = NULL;
	old_nodes = NULL;
	new_node = NULL;
	parser_sql = NULL;
	dialect_state = NULL;

	status = sqlparser_get_update_stmt(handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	old_count = update_stmt->n_target_list;
	if (assignment_index > old_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (old_count == ((size_t)-1)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "update SET list is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_validate_update_assignment_nodes(update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_preprocess_handle_sql_fragment(
		handle,
		assignment_sql,
		"update assignment SQL",
		&parser_sql,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_parse_single_update_assignment_sql(parser_sql, &new_node, out_error);
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	next_nodes = sqlparser_update_alloc_node_array(old_count + 1U, out_error);
	if (next_nodes == NULL) {
		sqlparser_free_proto_node(new_node);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_NO_MEMORY;
	}

	for (index = 0U; index < assignment_index; index++) {
		next_nodes[index] = update_stmt->target_list[index];
	}
	next_nodes[assignment_index] = new_node;
	for (index = assignment_index; index < old_count; index++) {
		next_nodes[index + 1U] = update_stmt->target_list[index];
	}
	new_node = NULL;
	old_nodes = update_stmt->target_list;
	update_stmt->target_list = next_nodes;
	update_stmt->n_target_list = old_count + 1U;
	next_nodes = NULL;
	free(old_nodes);

	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_delete_assignment(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__Node **next_nodes;
	PgQuery__Node **old_nodes;
	PgQuery__Node *deleted_node;
	size_t old_count;
	size_t index;
	size_t next_index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	update_stmt = NULL;
	next_nodes = NULL;
	old_nodes = NULL;
	deleted_node = NULL;

	status = sqlparser_get_update_stmt(handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	old_count = update_stmt->n_target_list;
	if (assignment_index >= old_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (old_count <= 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "update SET list cannot be empty");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	status = sqlparser_validate_update_assignment_nodes(update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	next_nodes = sqlparser_update_alloc_node_array(old_count - 1U, out_error);
	if (next_nodes == NULL) {
		return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_NO_MEMORY;
	}

	next_index = 0U;
	for (index = 0U; index < old_count; index++) {
		if (index == assignment_index) {
			deleted_node = update_stmt->target_list[index];
			continue;
		}
		next_nodes[next_index] = update_stmt->target_list[index];
		next_index++;
	}
	old_nodes = update_stmt->target_list;
	update_stmt->target_list = next_nodes;
	update_stmt->n_target_list = old_count - 1U;
	next_nodes = NULL;
	free(old_nodes);
	sqlparser_free_proto_node(deleted_node);

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_update_set_assignment_full_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const char *assignment_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__ResTarget *target;
	PgQuery__Node *replacement;
	PgQuery__Node *old_node;
	char *parser_sql;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	update_stmt = NULL;
	target = NULL;
	replacement = NULL;
	old_node = NULL;
	parser_sql = NULL;
	dialect_state = NULL;

	status = sqlparser_get_update_stmt(handle, statement_index, &update_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_get_update_assignment_res_target(update_stmt, assignment_index, &target, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_preprocess_handle_sql_fragment(
		handle,
		assignment_sql,
		"update assignment SQL",
		&parser_sql,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_parse_single_update_assignment_sql(parser_sql, &replacement, out_error);
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	(void)target;
	old_node = update_stmt->target_list[assignment_index];
	update_stmt->target_list[assignment_index] = replacement;
	replacement = NULL;
	sqlparser_free_proto_node(old_node);

	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_render_update_assignments_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	char *core_sql;
	size_t len;
	size_t capacity;
	size_t index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	update_stmt = NULL;
	status = sqlparser_get_update_stmt_by_target_list_index(
		(sqlparser_handle_t *)handle,
		statement_index,
		target_list_index,
		&update_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	core_sql = NULL;
	len = 0U;
	capacity = 0U;
	for (index = 0U; index < update_stmt->n_target_list; index++) {
		PgQuery__Node *node;
		PgQuery__ResTarget *target;
		char *value_sql;

		node = update_stmt->target_list[index];
		if (node == NULL ||
		    node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
		    node->res_target == NULL ||
		    node->res_target->name == NULL ||
		    node->res_target->val == NULL) {
			free(core_sql);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "update SET list node is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		target = node->res_target;
		value_sql = NULL;
		status = sqlparser_render_update_assignment_node_sql(target->val, &value_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(core_sql);
			return status;
		}
		if ((index > 0U && sqlparser_update_append_text(&core_sql, &len, &capacity, ", ", out_error) != 0) ||
		    sqlparser_update_append_text(&core_sql, &len, &capacity, target->name, out_error) != 0 ||
		    sqlparser_update_append_text(&core_sql, &len, &capacity, " = ", out_error) != 0 ||
		    sqlparser_update_append_text(&core_sql, &len, &capacity, value_sql, out_error) != 0) {
			free(value_sql);
			free(core_sql);
			return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
				out_error->code :
				SQLPARSER_STATUS_NO_MEMORY;
		}
		free(value_sql);
	}

	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		core_sql != NULL ? core_sql : "",
		"update SET list SQL",
		out_sql,
		out_error);
	free(core_sql);
	return status;
}

sqlparser_status_t sqlparser_update_set_assignments_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__Node **nodes;
	size_t count;
	size_t index;
	char *parser_sql;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || sql_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and SET list SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	update_stmt = NULL;
	nodes = NULL;
	count = 0U;
	parser_sql = NULL;
	dialect_state = NULL;
	status = sqlparser_get_update_stmt_by_target_list_index(
		handle,
		statement_index,
		target_list_index,
		&update_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_preprocess_handle_sql_fragment(
		handle,
		sql_text,
		"update SET list SQL",
		&parser_sql,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_parse_update_assignment_nodes_sql(parser_sql, &nodes, &count, out_error);
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	for (index = 0U; index < update_stmt->n_target_list; index++) {
		sqlparser_free_proto_node(update_stmt->target_list[index]);
	}
	free(update_stmt->target_list);
	update_stmt->target_list = nodes;
	update_stmt->n_target_list = count;
	nodes = NULL;

	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}
