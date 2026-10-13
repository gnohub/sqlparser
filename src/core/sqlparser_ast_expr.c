#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "sqlparser_render_scaffold.h"

sqlparser_status_t sqlparser_build_wrapped_sql(
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

sqlparser_status_t sqlparser_parse_wrapper_ast(
	const char *wrapped_sql,
	PgQuery__ParseResult **out_ast,
	sqlparser_error_t *out_error)
{
	PgQueryOwnedParseResult parse_result;
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
	parse_result =
		sqlparser_parse_owned_preserving_identifier_spelling(
			wrapped_sql);
	if (parse_result.error != NULL) {
		sqlparser_error_from_pg(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			wrapped_sql,
			parse_result.error);
		pg_query_free_owned_parse_result(parse_result);
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	if (parse_result.parse_tree == NULL) {
		pg_query_free_owned_parse_result(parse_result);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	ast = parse_result.parse_tree;
	parse_result.parse_tree = NULL;
	pg_query_free_owned_parse_result(parse_result);

	*out_ast = ast;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_deparse_wrapper_ast(
	const sqlparser_handle_t *handle,
	const PgQuery__ParseResult *ast,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQueryDeparseResult deparse_result;

	if (ast == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"wrapped deparse requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	sqlparser_pg_query_prepare();
	deparse_result = handle != NULL ?
		sqlparser_deparse_ast_for_handle(
			handle,
			ast,
			SIZE_MAX,
			0U,
			0U) :
		pg_query_deparse_ast(ast);
	if (deparse_result.error != NULL || deparse_result.query == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			deparse_result.error != NULL &&
					deparse_result.error->message != NULL ?
				deparse_result.error->message :
				"failed to deparse wrapped SQL");
		pg_query_free_deparse_result(deparse_result);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_sql = sqlparser_strdup(deparse_result.query);
	pg_query_free_deparse_result(deparse_result);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

/* Only callers holding an exclusively owned parse_wrapper_ast tree may use
 * this admission check before transferring a subtree. It does not itself prove
 * ownership. Preserve clone_proto_node's nonempty-root diagnostics without
 * allocating another copy of the fresh parser-owned tree. */
sqlparser_status_t sqlparser_validate_owned_wrapper_node(
	const PgQuery__Node *node, sqlparser_error_t *out_error)
{
	const ProtobufCFieldDescriptor *selected;
	const ProtobufCMessage *child = NULL;
	if (node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT,
			"node clone requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	selected = protobuf_c_message_descriptor_get_field(
		&pg_query__node__descriptor, (uint32_t)node->node_case);
	if (selected != NULL)
		memcpy(&child, (const uint8_t *)node + selected->offset, sizeof(child));
	if (node->base.n_unknown_fields == 0U &&
	    (selected == NULL || child == NULL || child == selected->default_value)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR,
			"cannot clone an empty node");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_extract_wrapped_value_sql(
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

static sqlparser_status_t sqlparser_get_wrapper_variable_set_arg_slot(
	PgQuery__ParseResult *ast,
	PgQuery__Node ***out_slot,
	sqlparser_error_t *out_error)
{
	PgQuery__RawStmt *raw_stmt;
	PgQuery__Node *statement;
	PgQuery__VariableSetStmt *set_stmt;

	if (ast == NULL || out_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"wrapped SET slot lookup requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_slot = NULL;
	if (ast->n_stmts != 1U || ast->stmts == NULL || ast->stmts[0] == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped SET parse tree is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	raw_stmt = ast->stmts[0];
	statement = raw_stmt->stmt;
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped parse tree does not contain SET");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	set_stmt = statement->variable_set_stmt;
	if (set_stmt->n_args != 1U || set_stmt->args == NULL || set_stmt->args[0] == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped SET parse tree does not contain one argument");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_slot = &set_stmt->args[0];
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_parse_insert_cell_node_sql(
	const char *sql_text,
	const sqlparser_generated_source_t *source,
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
		/* The wrapper is a freshly unpacked, exclusively owned tree. Transfer
		 * its cell instead of serializing, unpacking, and freeing a clone.
		 * Detaching first also keeps error cleanup single-owner. */
		*out_node = *slot;
		*slot = NULL;
		status = sqlparser_mark_proto_generated_with_fragment_source(
			(ProtobufCMessage *)*out_node,
			wrapped_sql,
			strlen(prefix),
			source,
			out_error);
	}

	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(*out_node);
		*out_node = NULL;
	}
	return status;
}

sqlparser_status_t sqlparser_parse_update_assignment_node_sql(
	const char *sql_text,
	const sqlparser_generated_source_t *source,
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
		status = sqlparser_validate_owned_wrapper_node(*slot, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			/* Transfer before marking/binding, which may fail. The residual
			 * wrapper must no longer own this output on any cleanup path. */
			*out_node = *slot;
			*slot = NULL;
			status = sqlparser_mark_proto_generated_with_fragment_source(
				(ProtobufCMessage *)*out_node,
				wrapped_sql,
				strlen(prefix),
				source,
				out_error);
		}
	}

	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(*out_node);
		*out_node = NULL;
	}
	return status;
}

sqlparser_status_t sqlparser_parse_variable_set_arg_node_sql(
	const char *sql_text,
	const sqlparser_generated_source_t *source,
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
			"SET argument SQL must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_node = NULL;
	prefix = "SET __sqlparser_session_parameter__ = ";
	wrapped_sql = NULL;
	ast = NULL;
	slot = NULL;
	status = sqlparser_build_wrapped_sql(prefix, sql_text, NULL, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_variable_set_arg_slot(ast, &slot, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_validate_owned_wrapper_node(*slot, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			/* Transfer before marking/binding, which may fail. The residual
			 * wrapper must no longer own this output on any cleanup path. */
			*out_node = *slot;
			*slot = NULL;
			status = sqlparser_mark_proto_generated_with_fragment_source(
				(ProtobufCMessage *)*out_node,
				wrapped_sql,
				strlen(prefix),
				source,
				out_error);
		}
	}

	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(*out_node);
		*out_node = NULL;
	}
	return status;
}

sqlparser_status_t sqlparser_render_insert_cell_node_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	const char *suffix;
	sqlparser_render_scaffold_t scaffold;
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
	deparsed_sql = NULL;
	ast = NULL;
	slot = NULL;
	replacement = NULL;
	status = sqlparser_render_scaffold_init(
		&scaffold, SQLPARSER_RENDER_SCAFFOLD_INSERT_CELL, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		ast = &scaffold.ast;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_insert_cell_slot(ast, &slot, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_clone_proto_node(node, &replacement, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && handle != NULL) {
		status = sqlparser_mark_proto_generated_from_handle(
			handle,
			(ProtobufCMessage *)replacement,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		*slot = replacement;
		status = sqlparser_deparse_wrapper_ast(
			handle,
			ast,
			&deparsed_sql,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_extract_wrapped_value_sql(deparsed_sql, prefix, suffix, out_sql, out_error);
	}

	/* The scaffold borrows replacement; never recursively free its stack AST. */
	sqlparser_free_proto_node(replacement);
	free(deparsed_sql);
	return status;
}

sqlparser_status_t sqlparser_render_variable_set_arg_node_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *deparse_prefix;
	sqlparser_render_scaffold_t scaffold;
	char *deparsed_sql;
	PgQuery__ParseResult *ast;
	PgQuery__Node **slot;
	PgQuery__Node *replacement;
	sqlparser_status_t status;

	if (node == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SET argument render requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	deparse_prefix = "SET __sqlparser_session_parameter__ TO ";
	deparsed_sql = NULL;
	ast = NULL;
	slot = NULL;
	replacement = NULL;
	status = sqlparser_render_scaffold_init(
		&scaffold, SQLPARSER_RENDER_SCAFFOLD_VARIABLE_SET_ARG, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		ast = &scaffold.ast;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_variable_set_arg_slot(ast, &slot, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_clone_proto_node(node, &replacement, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && handle != NULL) {
		status = sqlparser_mark_proto_generated_from_handle(
			handle,
			(ProtobufCMessage *)replacement,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		*slot = replacement;
		status = sqlparser_deparse_wrapper_ast(
			handle,
			ast,
			&deparsed_sql,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_extract_wrapped_value_sql(deparsed_sql, deparse_prefix, NULL, out_sql, out_error);
	}

	/* The scaffold borrows replacement; never recursively free its stack AST. */
	sqlparser_free_proto_node(replacement);
	free(deparsed_sql);
	return status;
}

sqlparser_status_t sqlparser_render_update_assignment_node_sql(
	const sqlparser_handle_t *handle,
	const PgQuery__Node *node,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	sqlparser_render_scaffold_t scaffold;
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
	deparsed_sql = NULL;
	ast = NULL;
	slot = NULL;
	replacement = NULL;
	status = sqlparser_render_scaffold_init(
		&scaffold, SQLPARSER_RENDER_SCAFFOLD_UPDATE_ASSIGNMENT, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		ast = &scaffold.ast;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_update_assignment_slot(ast, &slot, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_clone_proto_node(node, &replacement, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && handle != NULL) {
		status = sqlparser_mark_proto_generated_from_handle(
			handle,
			(ProtobufCMessage *)replacement,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		*slot = replacement;
		status = sqlparser_deparse_wrapper_ast(
			handle,
			ast,
			&deparsed_sql,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_extract_wrapped_value_sql(deparsed_sql, prefix, NULL, out_sql, out_error);
	}

	/* The scaffold borrows replacement; never recursively free its stack AST. */
	sqlparser_free_proto_node(replacement);
	free(deparsed_sql);
	return status;
}
