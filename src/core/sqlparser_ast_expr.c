#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"

static sqlparser_status_t sqlparser_build_wrapped_sql(
	const char *prefix,
	const char *sql_text,
	const char *suffix,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	size_t prefix_len;
	size_t sql_len;
	size_t suffix_len;
	size_t total_len;
	char *wrapped;

	if (prefix == NULL || sql_text == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"wrapped SQL builder requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	prefix_len = strlen(prefix);
	sql_len = strlen(sql_text);
	suffix_len = suffix != NULL ? strlen(suffix) : 0U;
	total_len = prefix_len + sql_len + suffix_len;
	wrapped = (char *)malloc(total_len + 1U);
	if (wrapped == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	memcpy(wrapped, prefix, prefix_len);
	memcpy(wrapped + prefix_len, sql_text, sql_len);
	if (suffix_len > 0U) {
		memcpy(wrapped + prefix_len + sql_len, suffix, suffix_len);
	}
	wrapped[total_len] = '\0';
	*out_sql = wrapped;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_parse_wrapper_ast(
	const char *wrapped_sql,
	PgQuery__ParseResult **out_ast,
	sqlparser_error_t *out_error)
{
	PgQueryProtobufParseResult parse_result;
	PgQuery__ParseResult *ast;

	if (wrapped_sql == NULL || out_ast == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"wrapped parse requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_ast = NULL;
	sqlparser_pg_query_prepare();
	parse_result = pg_query_parse_protobuf(wrapped_sql);
	if (parse_result.error != NULL) {
		sqlparser_error_from_pg(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			wrapped_sql,
			parse_result.error);
		pg_query_free_protobuf_parse_result(parse_result);
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	ast = pg_query__parse_result__unpack(
		NULL,
		parse_result.parse_tree.len,
		(const uint8_t *)parse_result.parse_tree.data);
	pg_query_free_protobuf_parse_result(parse_result);
	if (ast == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to unpack wrapped parse tree");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_ast = ast;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_deparse_wrapper_ast(
	const PgQuery__ParseResult *ast,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	size_t packed_size;
	size_t packed_len;
	uint8_t *buffer;
	PgQueryProtobuf parse_tree;
	PgQueryDeparseResult deparse_result;

	if (ast == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"wrapped deparse requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	packed_size = pg_query__parse_result__get_packed_size((PgQuery__ParseResult *)ast);
	if (packed_size == 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to pack wrapped parse tree");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	buffer = (uint8_t *)malloc(packed_size);
	if (buffer == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	packed_len = pg_query__parse_result__pack((PgQuery__ParseResult *)ast, buffer);
	if (packed_len != packed_size) {
		free(buffer);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to serialize wrapped parse tree");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	parse_tree.len = packed_size;
	parse_tree.data = (char *)buffer;
	sqlparser_pg_query_prepare();
	deparse_result = pg_query_deparse_protobuf(parse_tree);
	free(buffer);
	if (deparse_result.error != NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			deparse_result.error->message != NULL ? deparse_result.error->message : "failed to deparse wrapped SQL");
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_sql = sqlparser_strdup(deparse_result.query != NULL ? deparse_result.query : "");
	pg_query_free_deparse_result(deparse_result);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_extract_wrapped_value_sql(
	const char *wrapped_sql,
	const char *prefix,
	const char *suffix,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	size_t wrapped_len;
	size_t prefix_len;
	size_t suffix_len;

	if (wrapped_sql == NULL || prefix == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"wrapped SQL extractor requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	wrapped_len = strlen(wrapped_sql);
	prefix_len = strlen(prefix);
	suffix_len = suffix != NULL ? strlen(suffix) : 0U;
	if (wrapped_len < prefix_len + suffix_len ||
	    strncmp(wrapped_sql, prefix, prefix_len) != 0 ||
	    (suffix_len > 0U &&
	     strcmp(wrapped_sql + wrapped_len - suffix_len, suffix) != 0)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped SQL format is not recognized");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_sql = sqlparser_strndup(
		wrapped_sql + prefix_len,
		wrapped_len - prefix_len - suffix_len);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_wrapper_insert_cell_slot(
	PgQuery__ParseResult *ast,
	PgQuery__Node ***out_slot,
	sqlparser_error_t *out_error)
{
	PgQuery__RawStmt *raw_stmt;
	PgQuery__Node *statement;
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node *row_node;
	PgQuery__List *row_list;

	if (ast == NULL || out_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"wrapped insert slot lookup requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_slot = NULL;
	if (ast->n_stmts != 1U || ast->stmts == NULL || ast->stmts[0] == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped insert parse tree is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	raw_stmt = ast->stmts[0];
	statement = raw_stmt->stmt;
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_INSERT_STMT ||
	    statement->insert_stmt == NULL ||
	    statement->insert_stmt->select_stmt == NULL ||
	    statement->insert_stmt->select_stmt->select_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped insert parse tree does not contain INSERT VALUES");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	insert_stmt = statement->insert_stmt;
	values_stmt = insert_stmt->select_stmt->select_stmt;
	if (values_stmt->n_values_lists != 1U || values_stmt->values_lists == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped insert parse tree does not contain one VALUES row");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	row_node = values_stmt->values_lists[0];
	if (row_node == NULL ||
	    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
	    row_node->list == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped insert row node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	row_list = row_node->list;
	if (row_list->n_items != 1U || row_list->items == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped insert parse tree does not contain one VALUES cell");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_slot = &row_list->items[0];
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_wrapper_update_assignment_slot(
	PgQuery__ParseResult *ast,
	PgQuery__Node ***out_slot,
	sqlparser_error_t *out_error)
{
	PgQuery__RawStmt *raw_stmt;
	PgQuery__Node *statement;
	PgQuery__UpdateStmt *update_stmt;
	PgQuery__ResTarget *target;

	if (ast == NULL || out_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"wrapped update slot lookup requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_slot = NULL;
	if (ast->n_stmts != 1U || ast->stmts == NULL || ast->stmts[0] == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped update parse tree is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	raw_stmt = ast->stmts[0];
	statement = raw_stmt->stmt;
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_UPDATE_STMT ||
	    statement->update_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped update parse tree does not contain UPDATE");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	update_stmt = statement->update_stmt;
	if (update_stmt->n_target_list != 1U || update_stmt->target_list == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped update parse tree does not contain one assignment");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	if (update_stmt->target_list[0] == NULL ||
	    update_stmt->target_list[0]->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    update_stmt->target_list[0]->res_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped update assignment node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	target = update_stmt->target_list[0]->res_target;
	*out_slot = &target->val;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_parse_insert_cell_node_sql(
	const char *sql_text,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	const char *suffix;
	char *wrapped_sql;
	PgQuery__ParseResult *ast;
	PgQuery__Node **slot;
	sqlparser_status_t status;

	if (sql_text == NULL || sql_text[0] == '\0' || out_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell SQL must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_node = NULL;
	prefix = "INSERT INTO __sqlparser_target__ VALUES (";
	suffix = ")";
	wrapped_sql = NULL;
	ast = NULL;
	slot = NULL;
	status = sqlparser_build_wrapped_sql(prefix, sql_text, suffix, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_insert_cell_slot(ast, &slot, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_clone_proto_node(*slot, out_node, out_error);
	}

	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	return status;
}

sqlparser_status_t sqlparser_parse_update_assignment_node_sql(
	const char *sql_text,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	char *wrapped_sql;
	PgQuery__ParseResult *ast;
	PgQuery__Node **slot;
	sqlparser_status_t status;

	if (sql_text == NULL || sql_text[0] == '\0' || out_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"assignment SQL must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_node = NULL;
	prefix = "UPDATE __sqlparser_target__ SET __sqlparser_column__ = ";
	wrapped_sql = NULL;
	ast = NULL;
	slot = NULL;
	status = sqlparser_build_wrapped_sql(prefix, sql_text, NULL, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_update_assignment_slot(ast, &slot, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_clone_proto_node(*slot, out_node, out_error);
	}

	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	return status;
}

sqlparser_status_t sqlparser_render_insert_cell_node_sql(
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	const char *suffix;
	char *wrapped_sql;
	char *deparsed_sql;
	PgQuery__ParseResult *ast;
	PgQuery__Node **slot;
	PgQuery__Node *replacement;
	sqlparser_status_t status;

	if (node == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell render requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	prefix = "INSERT INTO __sqlparser_target__ VALUES (";
	suffix = ")";
	wrapped_sql = NULL;
	deparsed_sql = NULL;
	ast = NULL;
	slot = NULL;
	replacement = NULL;
	status = sqlparser_build_wrapped_sql(prefix, "NULL", suffix, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_insert_cell_slot(ast, &slot, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_clone_proto_node(node, &replacement, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(*slot);
		*slot = replacement;
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

sqlparser_status_t sqlparser_render_update_assignment_node_sql(
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	char *wrapped_sql;
	char *deparsed_sql;
	PgQuery__ParseResult *ast;
	PgQuery__Node **slot;
	PgQuery__Node *replacement;
	sqlparser_status_t status;

	if (node == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"assignment render requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	prefix = "UPDATE __sqlparser_target__ SET __sqlparser_column__ = ";
	wrapped_sql = NULL;
	deparsed_sql = NULL;
	ast = NULL;
	slot = NULL;
	replacement = NULL;
	status = sqlparser_build_wrapped_sql(prefix, "NULL", NULL, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_update_assignment_slot(ast, &slot, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_clone_proto_node(node, &replacement, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(*slot);
		*slot = replacement;
		replacement = NULL;
		status = sqlparser_deparse_wrapper_ast(ast, &deparsed_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_extract_wrapped_value_sql(deparsed_sql, prefix, NULL, out_sql, out_error);
	}

	sqlparser_free_proto_node(replacement);
	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	free(deparsed_sql);
	return status;
}
