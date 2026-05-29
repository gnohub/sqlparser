#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"

static int sqlparser_select_stmt_has_target_list(ProtobufCMessage *message)
{
	PgQuery__SelectStmt *stmt;

	if (message == NULL || message->descriptor != &pg_query__select_stmt__descriptor) {
		return 0;
	}
	stmt = (PgQuery__SelectStmt *)message;
	return stmt->n_target_list > 0U && stmt->target_list != NULL;
}

static void sqlparser_select_free_node_array(PgQuery__Node **nodes, size_t count)
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

static void sqlparser_select_free_string_array(char **items, size_t count)
{
	size_t index;

	if (items == NULL) {
		return;
	}
	for (index = 0U; index < count; index++) {
		free(items[index]);
	}
	free(items);
}

static PgQuery__Node **sqlparser_select_alloc_node_array(size_t count, sqlparser_error_t *out_error)
{
	PgQuery__Node **items;

	if (count == 0U) {
		return NULL;
	}
	if (count > SIZE_MAX / sizeof(*items)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "select target array is too large");
		return NULL;
	}
	items = (PgQuery__Node **)calloc(count, sizeof(*items));
	if (items == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return items;
}

static void sqlparser_select_copy_with_insert(
	PgQuery__Node **dest,
	PgQuery__Node **source,
	size_t count,
	size_t index,
	PgQuery__Node *node)
{
	if (index > count) {
		index = count;
	}
	if (index > 0U && source != NULL) {
		memcpy(dest, source, index * sizeof(*dest));
	}
	dest[index] = node;
	if (index < count && source != NULL) {
		memcpy(dest + index + 1U, source + index, (count - index) * sizeof(*dest));
	}
}

static void sqlparser_select_copy_with_delete(
	PgQuery__Node **dest,
	PgQuery__Node **source,
	size_t count,
	size_t index)
{
	if (index > 0U && source != NULL) {
		memcpy(dest, source, index * sizeof(*dest));
	}
	if (index + 1U < count && source != NULL) {
		memcpy(dest + index, source + index + 1U, (count - index - 1U) * sizeof(*dest));
	}
}

sqlparser_status_t sqlparser_get_select_stmt_by_target_list_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	PgQuery__SelectStmt **out_stmt,
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
		&pg_query__select_stmt__descriptor,
		sqlparser_select_stmt_has_target_list,
		1,
		target_list_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target list selector is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_stmt = (PgQuery__SelectStmt *)message;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_find_select_target_list_index_by_stmt(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__SelectStmt *stmt,
	size_t *out_index,
	sqlparser_error_t *out_error)
{
	size_t count;
	size_t index;
	sqlparser_status_t status;

	if (out_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_index = (size_t)-1;
	if (stmt == NULL || stmt->n_target_list == 0U || stmt->target_list == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target list is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	count = 0U;
	status = sqlparser_search_statement_messages(
		handle,
		statement_index,
		&pg_query__select_stmt__descriptor,
		sqlparser_select_stmt_has_target_list,
		0,
		0U,
		&count,
		NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (index = 0U; index < count; index++) {
		ProtobufCMessage *message;

		message = NULL;
		status = sqlparser_search_statement_messages(
			handle,
			statement_index,
			&pg_query__select_stmt__descriptor,
			sqlparser_select_stmt_has_target_list,
			1,
			index,
			NULL,
			&message,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if ((PgQuery__SelectStmt *)message == stmt) {
			*out_index = index;
			return SQLPARSER_STATUS_OK;
		}
	}

	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "select target list was not found");
	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static sqlparser_status_t sqlparser_get_wrapper_select_stmt(
	PgQuery__ParseResult *ast,
	PgQuery__SelectStmt **out_stmt,
	sqlparser_error_t *out_error)
{
	PgQuery__RawStmt *raw_stmt;
	PgQuery__Node *statement;

	if (ast == NULL || out_stmt == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "wrapped SELECT lookup requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_stmt = NULL;
	if (ast->n_stmts != 1U || ast->stmts == NULL || ast->stmts[0] == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "wrapped SELECT parse tree is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	raw_stmt = ast->stmts[0];
	statement = raw_stmt->stmt;
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    statement->select_stmt == NULL ||
	    statement->select_stmt->n_target_list == 0U ||
	    statement->select_stmt->target_list == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "wrapped SELECT parse tree does not contain target list");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_stmt = statement->select_stmt;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_parse_select_target_nodes_sql(
	const char *sql_text,
	PgQuery__Node ***out_nodes,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	const char *suffix;
	char *wrapped_sql;
	PgQuery__ParseResult *ast;
	PgQuery__SelectStmt *stmt;
	PgQuery__Node **nodes;
	sqlparser_status_t status;
	size_t index;

	if (sql_text == NULL || sql_text[0] == '\0' || out_nodes == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target SQL must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_nodes = NULL;
	*out_count = 0U;
	prefix = "SELECT ";
	suffix = " FROM __sqlparser_source__";
	wrapped_sql = NULL;
	ast = NULL;
	stmt = NULL;
	nodes = NULL;
	status = sqlparser_build_wrapped_sql(prefix, sql_text, suffix, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_select_stmt(ast, &stmt, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		nodes = sqlparser_select_alloc_node_array(stmt->n_target_list, out_error);
		if (nodes == NULL) {
			status = out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		for (index = 0U; index < stmt->n_target_list; index++) {
			status = sqlparser_clone_proto_node(stmt->target_list[index], &nodes[index], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		*out_nodes = nodes;
		*out_count = stmt->n_target_list;
		nodes = NULL;
	}

	sqlparser_select_free_node_array(nodes, stmt != NULL ? stmt->n_target_list : 0U);
	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	return status;
}

sqlparser_status_t sqlparser_parse_select_target_node_sql(
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
	status = sqlparser_parse_select_target_nodes_sql(sql_text, &nodes, &count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (count != 1U) {
		sqlparser_select_free_node_array(nodes, count);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "select target SQL must contain exactly one target");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	*out_node = nodes[0];
	nodes[0] = NULL;
	sqlparser_select_free_node_array(nodes, count);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_render_select_target_node_sql(
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	const char *suffix;
	char *wrapped_sql;
	char *deparsed_sql;
	PgQuery__ParseResult *ast;
	PgQuery__SelectStmt *stmt;
	PgQuery__Node *replacement;
	sqlparser_status_t status;

	if (node == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target render requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	prefix = "SELECT ";
	suffix = " FROM __sqlparser_source__";
	wrapped_sql = NULL;
	deparsed_sql = NULL;
	ast = NULL;
	stmt = NULL;
	replacement = NULL;
	status = sqlparser_build_wrapped_sql(prefix, "NULL", suffix, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_select_stmt(ast, &stmt, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_clone_proto_node(node, &replacement, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(stmt->target_list[0]);
		stmt->target_list[0] = replacement;
		replacement = NULL;
		status = sqlparser_deparse_wrapper_ast(ast, &deparsed_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_extract_wrapped_value_sql(deparsed_sql, prefix, suffix, out_sql, out_error);
	}

	sqlparser_free_proto_node(replacement);
	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	free(deparsed_sql);
	return status;
}

sqlparser_status_t sqlparser_select_target_list_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	if (handle == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;
	return sqlparser_search_statement_messages(
		(sqlparser_handle_t *)handle,
		statement_index,
		&pg_query__select_stmt__descriptor,
		sqlparser_select_stmt_has_target_list,
		0,
		0U,
		out_count,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_select_target_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;
	stmt = NULL;
	status = sqlparser_get_select_stmt_by_target_list_index(
		(sqlparser_handle_t *)handle,
		statement_index,
		target_list_index,
		&stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*out_count = stmt->n_target_list;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_select_target_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	char *core_sql;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	stmt = NULL;
	status = sqlparser_get_select_stmt_by_target_list_index(
		(sqlparser_handle_t *)handle,
		statement_index,
		target_list_index,
		&stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (target_index >= stmt->n_target_list) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	core_sql = NULL;
	status = sqlparser_render_select_target_node_sql(stmt->target_list[target_index], &core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		core_sql,
		"select target SQL",
		out_sql,
		out_error);
	free(core_sql);
	return status;
}

static sqlparser_status_t sqlparser_select_target_sql_array(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	char ***out_items,
	size_t *out_count,
	size_t *out_total_len,
	sqlparser_error_t *out_error)
{
	char **items;
	size_t count;
	size_t index;
	size_t total_len;
	sqlparser_status_t status;

	if (out_items == NULL || out_count == NULL || out_total_len == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_items = NULL;
	*out_count = 0U;
	*out_total_len = 0U;
	count = 0U;
	status = sqlparser_select_target_count(handle, statement_index, target_list_index, &count, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (count == 0U || count > SIZE_MAX / sizeof(*items)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "select target list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	items = (char **)calloc(count, sizeof(*items));
	if (items == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	total_len = 0U;
	for (index = 0U; index < count; index++) {
		status = sqlparser_select_target_sql(
			handle,
			statement_index,
			target_list_index,
			index,
			&items[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_select_free_string_array(items, count);
			return status;
		}
		if (items[index] == NULL || strlen(items[index]) > SIZE_MAX - total_len) {
			sqlparser_select_free_string_array(items, count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "select target SQL is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		total_len += strlen(items[index]);
	}
	*out_items = items;
	*out_count = count;
	*out_total_len = total_len;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_render_select_targets_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char **items;
	char *sql;
	size_t count;
	size_t total_len;
	size_t capacity;
	size_t offset;
	size_t index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	items = NULL;
	count = 0U;
	total_len = 0U;
	status = sqlparser_select_target_sql_array(
		handle,
		statement_index,
		target_list_index,
		&items,
		&count,
		&total_len,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (count > (SIZE_MAX - total_len - 1U) / 2U) {
		sqlparser_select_free_string_array(items, count);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "select target SQL is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	capacity = total_len + ((count - 1U) * 2U) + 1U;
	sql = (char *)malloc(capacity);
	if (sql == NULL) {
		sqlparser_select_free_string_array(items, count);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	offset = 0U;
	for (index = 0U; index < count; index++) {
		size_t len;

		if (index > 0U) {
			sql[offset++] = ',';
			sql[offset++] = ' ';
		}
		len = strlen(items[index]);
		memcpy(sql + offset, items[index], len);
		offset += len;
		free(items[index]);
	}
	free(items);
	sql[offset] = '\0';
	*out_sql = sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_select_parse_public_targets(
	sqlparser_handle_t *handle,
	const char *sql_text,
	int require_single,
	PgQuery__Node ***out_nodes,
	size_t *out_count,
	void **out_dialect_state,
	sqlparser_error_t *out_error)
{
	char *parser_sql;
	void *dialect_state;
	sqlparser_status_t status;

	if (out_nodes == NULL || out_count == NULL || out_dialect_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_nodes = NULL;
	*out_count = 0U;
	*out_dialect_state = NULL;
	parser_sql = NULL;
	dialect_state = NULL;
	status = sqlparser_preprocess_handle_sql_fragment(
		handle,
		sql_text,
		"select target SQL",
		&parser_sql,
		&dialect_state,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_select_target_nodes_sql(parser_sql, out_nodes, out_count, out_error);
	}
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	if (require_single && *out_count != 1U) {
		sqlparser_select_free_node_array(*out_nodes, *out_count);
		*out_nodes = NULL;
		*out_count = 0U;
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "select target SQL must contain exactly one target");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	*out_dialect_state = dialect_state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_select_replace_target_list(
	sqlparser_handle_t *handle,
	PgQuery__SelectStmt *stmt,
	PgQuery__Node **nodes,
	size_t count,
	void *dialect_state,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	if (stmt == NULL || nodes == NULL || count == 0U) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target list replacement is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	for (index = 0U; index < stmt->n_target_list; index++) {
		sqlparser_free_proto_node(stmt->target_list[index]);
	}
	free(stmt->target_list);
	stmt->target_list = nodes;
	stmt->n_target_list = count;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_select_set_targets_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	PgQuery__Node **nodes;
	size_t count;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	stmt = NULL;
	nodes = NULL;
	count = 0U;
	dialect_state = NULL;
	status = sqlparser_get_select_stmt_by_target_list_index(handle, statement_index, target_list_index, &stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_select_parse_public_targets(
		handle,
		sql_text,
		0,
		&nodes,
		&count,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_select_replace_target_list(handle, stmt, nodes, count, dialect_state, out_error);
}

sqlparser_status_t sqlparser_select_set_target_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	PgQuery__Node **nodes;
	PgQuery__Node *replacement;
	size_t count;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	stmt = NULL;
	nodes = NULL;
	replacement = NULL;
	count = 0U;
	dialect_state = NULL;
	status = sqlparser_get_select_stmt_by_target_list_index(handle, statement_index, target_list_index, &stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (target_index >= stmt->n_target_list) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_select_parse_public_targets(
		handle,
		sql_text,
		1,
		&nodes,
		&count,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	replacement = nodes[0];
	free(nodes);
	nodes = NULL;
	sqlparser_free_proto_node(stmt->target_list[target_index]);
	stmt->target_list[target_index] = replacement;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_select_replace_target_with_columns(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	const sqlparser_identifier_path_view_t *columns,
	size_t column_count,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	PgQuery__Node **column_nodes;
	PgQuery__Node **next_targets;
	PgQuery__Node *removed_node;
	size_t old_count;
	size_t next_count;
	size_t index;
	size_t next_index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	stmt = NULL;
	column_nodes = NULL;
	next_targets = NULL;
	removed_node = NULL;

	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (columns == NULL || column_count == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "columns must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_get_select_stmt_by_target_list_index(handle, statement_index, target_list_index, &stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	old_count = stmt->n_target_list;
	if (target_index >= old_count || stmt->target_list == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (column_count > ((size_t)-1) - old_count + 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "select target count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	column_nodes = sqlparser_select_alloc_node_array(column_count, out_error);
	if (column_nodes == NULL) {
		return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_NO_MEMORY;
	}
	for (index = 0U; index < column_count; index++) {
		status = sqlparser_build_select_target_identifier_node(&columns[index], &column_nodes[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_select_free_node_array(column_nodes, column_count);
			return status;
		}
	}

	next_count = old_count - 1U + column_count;
	next_targets = sqlparser_select_alloc_node_array(next_count, out_error);
	if (next_targets == NULL) {
		sqlparser_select_free_node_array(column_nodes, column_count);
		return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_NO_MEMORY;
	}

	next_index = 0U;
	for (index = 0U; index < target_index; index++) {
		next_targets[next_index++] = stmt->target_list[index];
	}
	for (index = 0U; index < column_count; index++) {
		next_targets[next_index++] = column_nodes[index];
		column_nodes[index] = NULL;
	}
	removed_node = stmt->target_list[target_index];
	for (index = target_index + 1U; index < old_count; index++) {
		next_targets[next_index++] = stmt->target_list[index];
	}
	free(column_nodes);
	column_nodes = NULL;
	free(stmt->target_list);
	stmt->target_list = next_targets;
	stmt->n_target_list = next_count;
	next_targets = NULL;
	sqlparser_free_proto_node(removed_node);

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_select_insert_target_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	PgQuery__Node **nodes;
	PgQuery__Node **next_targets;
	size_t count;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	stmt = NULL;
	nodes = NULL;
	next_targets = NULL;
	count = 0U;
	dialect_state = NULL;
	status = sqlparser_get_select_stmt_by_target_list_index(handle, statement_index, target_list_index, &stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (stmt->n_target_list == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "select target count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_select_parse_public_targets(
		handle,
		sql_text,
		1,
		&nodes,
		&count,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	next_targets = sqlparser_select_alloc_node_array(stmt->n_target_list + 1U, out_error);
	if (next_targets == NULL) {
		sqlparser_select_free_node_array(nodes, count);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	if (target_index > stmt->n_target_list) {
		target_index = stmt->n_target_list;
	}
	sqlparser_select_copy_with_insert(next_targets, stmt->target_list, stmt->n_target_list, target_index, nodes[0]);
	nodes[0] = NULL;
	sqlparser_select_free_node_array(nodes, count);
	free(stmt->target_list);
	stmt->target_list = next_targets;
	stmt->n_target_list++;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_select_delete_target(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t target_list_index,
	size_t target_index,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	PgQuery__Node **next_targets;
	PgQuery__Node *removed;

	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	stmt = NULL;
	next_targets = NULL;
	removed = NULL;
	if (sqlparser_get_select_stmt_by_target_list_index(handle, statement_index, target_list_index, &stmt, out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (target_index >= stmt->n_target_list) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "select target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (stmt->n_target_list <= 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "cannot delete the last select target");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	next_targets = sqlparser_select_alloc_node_array(stmt->n_target_list - 1U, out_error);
	if (next_targets == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	sqlparser_select_copy_with_delete(next_targets, stmt->target_list, stmt->n_target_list, target_index);
	removed = stmt->target_list[target_index];
	free(stmt->target_list);
	stmt->target_list = next_targets;
	stmt->n_target_list--;
	sqlparser_free_proto_node(removed);
	return sqlparser_handle_commit_ast(handle, out_error);
}
