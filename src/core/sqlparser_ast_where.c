#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"

static int sqlparser_where_field_is_clause(const ProtobufCFieldDescriptor *field)
{
	const ProtobufCMessageDescriptor *message_descriptor;

	if (field == NULL ||
	    field->type != PROTOBUF_C_TYPE_MESSAGE ||
	    field->label == PROTOBUF_C_LABEL_REPEATED ||
	    field->name == NULL ||
	    strcmp(field->name, "where_clause") != 0) {
		return 0;
	}

	message_descriptor = (const ProtobufCMessageDescriptor *)field->descriptor;
	return message_descriptor == &pg_query__node__descriptor;
}

static int sqlparser_where_select_stmt_has_clause_slot(const PgQuery__SelectStmt *stmt)
{
	if (stmt == NULL) {
		return 0;
	}
	if (stmt->n_values_lists > 0U) {
		return 0;
	}
	if (stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	    stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE &&
	    stmt->n_target_list == 0U &&
	    stmt->n_from_clause == 0U &&
	    stmt->where_clause == NULL) {
		return 0;
	}
	return stmt->n_target_list > 0U ||
		stmt->n_from_clause > 0U ||
		stmt->where_clause != NULL ||
		stmt->having_clause != NULL ||
		stmt->with_clause != NULL ||
		stmt->n_group_clause > 0U ||
		stmt->n_sort_clause > 0U ||
		stmt->limit_offset != NULL ||
		stmt->limit_count != NULL;
}

static int sqlparser_where_message_allows_empty_clause(
	ProtobufCMessage *message,
	const ProtobufCFieldDescriptor *field)
{
	if (message == NULL || field == NULL || message->descriptor == NULL) {
		return 0;
	}

	if (message->descriptor == &pg_query__select_stmt__descriptor) {
		return sqlparser_where_select_stmt_has_clause_slot((const PgQuery__SelectStmt *)message);
	}
	if (message->descriptor == &pg_query__copy_stmt__descriptor) {
		const PgQuery__CopyStmt *stmt = (const PgQuery__CopyStmt *)message;
		return stmt->is_from || stmt->where_clause != NULL;
	}
	if (message->descriptor == &pg_query__on_conflict_clause__descriptor) {
		const PgQuery__OnConflictClause *clause = (const PgQuery__OnConflictClause *)message;
		return clause->action == PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_UPDATE ||
			clause->where_clause != NULL;
	}
	if (message->descriptor == &pg_query__constraint__descriptor) {
		const PgQuery__Constraint *constraint = (const PgQuery__Constraint *)message;
		return constraint->contype == PG_QUERY__CONSTR_TYPE__CONSTR_EXCLUSION ||
			constraint->where_clause != NULL;
	}

	return 1;
}

static sqlparser_status_t sqlparser_where_record_clause_slot(
	ProtobufCMessage *message,
	const ProtobufCFieldDescriptor *field,
	sqlparser_where_clause_search_t *search)
{
	uint8_t *base;
	PgQuery__Node **slot;

	if (message == NULL || field == NULL || search == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_where_field_is_clause(field) ||
	    !sqlparser_where_message_allows_empty_clause(message, field)) {
		return SQLPARSER_STATUS_OK;
	}

	base = (uint8_t *)message;
	slot = (PgQuery__Node **)(base + field->offset);
	if (!search->want_target) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}
	if (search->seen == search->target_index) {
		search->target_slot = slot;
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}
	search->seen++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_message_where_slots(
	ProtobufCMessage *message,
	sqlparser_where_clause_search_t *search)
{
	const ProtobufCMessageDescriptor *descriptor;
	uint8_t *base;
	unsigned index;

	if (message == NULL || search == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->want_target && search->target_slot != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	base = (uint8_t *)message;
	for (index = 0U; index < descriptor->n_fields; index++) {
		const ProtobufCFieldDescriptor *field;

		field = &descriptor->fields[index];
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
			const int case_value = *(const int *)(base + field->quantifier_offset);

			if (case_value != (int)field->id) {
				continue;
			}
		}

		if (sqlparser_where_field_is_clause(field)) {
			sqlparser_status_t status;

			status = sqlparser_where_record_clause_slot(message, field, search);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (search->want_target && search->target_slot != NULL) {
				return SQLPARSER_STATUS_OK;
			}
		}

		if (field->type != PROTOBUF_C_TYPE_MESSAGE) {
			continue;
		}

		if (field->label == PROTOBUF_C_LABEL_REPEATED) {
			size_t item_count;
			ProtobufCMessage **items;
			size_t item_index;

			item_count = *(const size_t *)(base + field->quantifier_offset);
			items = *(ProtobufCMessage ***)(base + field->offset);
			for (item_index = 0U; item_index < item_count; item_index++) {
				sqlparser_status_t status;

				status = sqlparser_walk_message_where_slots(items[item_index], search);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (search->want_target && search->target_slot != NULL) {
					return SQLPARSER_STATUS_OK;
				}
			}
		} else {
			ProtobufCMessage *child;
			sqlparser_status_t status;

			child = *(ProtobufCMessage **)(base + field->offset);
			if (child == NULL) {
				continue;
			}
			status = sqlparser_walk_message_where_slots(child, search);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (search->want_target && search->target_slot != NULL) {
				return SQLPARSER_STATUS_OK;
			}
		}
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_count_statement_where_clauses(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_where_clause_search_t search;
	sqlparser_status_t status;

	if (out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;

	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	status = sqlparser_walk_message_where_slots((ProtobufCMessage *)statement, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*out_count = search.seen;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_get_statement_where_clause_slot(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	PgQuery__Node ***out_slot,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_where_clause_search_t search;
	sqlparser_status_t status;

	if (out_slot == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_slot must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_slot = NULL;

	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = where_index;
	status = sqlparser_walk_message_where_slots((ProtobufCMessage *)statement, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "where selector is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_slot = search.target_slot;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_wrapper_where_slot(
	PgQuery__ParseResult *ast,
	PgQuery__Node ***out_slot,
	sqlparser_error_t *out_error)
{
	PgQuery__RawStmt *raw_stmt;
	PgQuery__Node *statement;

	if (ast == NULL || out_slot == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "wrapped WHERE lookup requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_slot = NULL;
	if (ast->n_stmts != 1U || ast->stmts == NULL || ast->stmts[0] == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "wrapped WHERE parse tree is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	raw_stmt = ast->stmts[0];
	statement = raw_stmt->stmt;
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    statement->select_stmt == NULL ||
	    statement->select_stmt->where_clause == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "wrapped WHERE parse tree does not contain WHERE");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_slot = &statement->select_stmt->where_clause;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_parse_where_node_sql(
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
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "WHERE SQL must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_node = NULL;
	prefix = "SELECT 1 FROM __sqlparser_source__ WHERE ";
	wrapped_sql = NULL;
	ast = NULL;
	slot = NULL;
	status = sqlparser_build_wrapped_sql(prefix, sql_text, NULL, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_where_slot(ast, &slot, out_error);
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

sqlparser_status_t sqlparser_render_where_node_sql(
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
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "WHERE render requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_sql = NULL;
	prefix = "SELECT 1 FROM __sqlparser_source__ WHERE ";
	wrapped_sql = NULL;
	deparsed_sql = NULL;
	ast = NULL;
	slot = NULL;
	replacement = NULL;
	status = sqlparser_build_wrapped_sql(prefix, "TRUE", NULL, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_where_slot(ast, &slot, out_error);
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

static PgQuery__BoolExprType sqlparser_where_pg_bool_operator(sqlparser_bool_operator_t bool_operator)
{
	switch (bool_operator) {
		case SQLPARSER_BOOL_OPERATOR_OR:
			return PG_QUERY__BOOL_EXPR_TYPE__OR_EXPR;
		case SQLPARSER_BOOL_OPERATOR_AND:
		default:
			return PG_QUERY__BOOL_EXPR_TYPE__AND_EXPR;
	}
}

static int sqlparser_where_bool_operator_is_valid(sqlparser_bool_operator_t bool_operator)
{
	return bool_operator == SQLPARSER_BOOL_OPERATOR_AND ||
		bool_operator == SQLPARSER_BOOL_OPERATOR_OR;
}

static PgQuery__Node *sqlparser_where_new_bool_node(
	PgQuery__BoolExprType bool_operator,
	PgQuery__Node *left,
	PgQuery__Node *right,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	PgQuery__BoolExpr *expr;
	PgQuery__Node **args;

	node = (PgQuery__Node *)calloc(1U, sizeof(*node));
	expr = (PgQuery__BoolExpr *)calloc(1U, sizeof(*expr));
	args = (PgQuery__Node **)calloc(2U, sizeof(*args));
	if (node == NULL || expr == NULL || args == NULL) {
		free(node);
		free(expr);
		free(args);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}

	pg_query__node__init(node);
	pg_query__bool_expr__init(expr);
	node->node_case = PG_QUERY__NODE__NODE_BOOL_EXPR;
	node->bool_expr = expr;
	expr->boolop = bool_operator;
	expr->n_args = 2U;
	expr->args = args;
	expr->location = -1;
	args[0] = left;
	args[1] = right;
	return node;
}

static sqlparser_status_t sqlparser_where_append_node(
	PgQuery__Node **slot,
	sqlparser_bool_operator_t bool_operator,
	PgQuery__Node *condition,
	sqlparser_error_t *out_error)
{
	PgQuery__BoolExprType pg_bool_operator;
	PgQuery__Node *combined;

	if (slot == NULL || condition == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "WHERE append received invalid arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (!sqlparser_where_bool_operator_is_valid(bool_operator)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bool_operator must be AND or OR");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	pg_bool_operator = sqlparser_where_pg_bool_operator(bool_operator);
	if (*slot == NULL) {
		*slot = condition;
		return SQLPARSER_STATUS_OK;
	}

	if ((*slot)->node_case == PG_QUERY__NODE__NODE_BOOL_EXPR &&
	    (*slot)->bool_expr != NULL &&
	    (*slot)->bool_expr->boolop == pg_bool_operator) {
		PgQuery__BoolExpr *expr;
		PgQuery__Node **next_args;
		size_t next_count;

		expr = (*slot)->bool_expr;
		if (expr->n_args == SIZE_MAX) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "WHERE condition count is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		next_count = expr->n_args + 1U;
		if (next_count > SIZE_MAX / sizeof(*next_args)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "WHERE condition array is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		next_args = (PgQuery__Node **)realloc(expr->args, next_count * sizeof(*next_args));
		if (next_args == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		expr->args = next_args;
		expr->args[expr->n_args] = condition;
		expr->n_args = next_count;
		return SQLPARSER_STATUS_OK;
	}

	combined = sqlparser_where_new_bool_node(pg_bool_operator, *slot, condition, out_error);
	if (combined == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*slot = combined;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_where_count(
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
	return sqlparser_count_statement_where_clauses((sqlparser_handle_t *)handle, statement_index, out_count, out_error);
}

sqlparser_status_t sqlparser_statement_where_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **slot;
	char *core_sql;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;

	slot = NULL;
	status = sqlparser_get_statement_where_clause_slot(
		(sqlparser_handle_t *)handle,
		statement_index,
		where_index,
		&slot,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (slot == NULL || *slot == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	core_sql = NULL;
	status = sqlparser_render_where_node_sql(*slot, &core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		core_sql,
		"WHERE SQL",
		out_sql,
		out_error);
	free(core_sql);
	return status;
}

static sqlparser_status_t sqlparser_statement_parse_public_where(
	sqlparser_handle_t *handle,
	const char *sql_text,
	PgQuery__Node **out_node,
	void **out_dialect_state,
	sqlparser_error_t *out_error)
{
	char *parser_sql;
	void *dialect_state;
	sqlparser_status_t status;

	if (out_node == NULL || out_dialect_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "WHERE parse outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node = NULL;
	*out_dialect_state = NULL;
	parser_sql = NULL;
	dialect_state = NULL;
	status = sqlparser_preprocess_handle_sql_fragment(
		handle,
		sql_text,
		"WHERE SQL",
		&parser_sql,
		&dialect_state,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_where_node_sql(parser_sql, out_node, out_error);
	}
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	*out_dialect_state = dialect_state;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_set_where_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **slot;
	PgQuery__Node *replacement;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	slot = NULL;
	replacement = NULL;
	dialect_state = NULL;
	status = sqlparser_get_statement_where_clause_slot(handle, statement_index, where_index, &slot, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_statement_parse_public_where(handle, sql_text, &replacement, &dialect_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	sqlparser_free_proto_node(*slot);
	*slot = replacement;
	replacement = NULL;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_append_where_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t where_index,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **slot;
	PgQuery__Node *condition;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	slot = NULL;
	condition = NULL;
	dialect_state = NULL;
	status = sqlparser_get_statement_where_clause_slot(handle, statement_index, where_index, &slot, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_statement_parse_public_where(handle, sql_text, &condition, &dialect_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_where_append_node(slot, bool_operator, condition, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(condition);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	condition = NULL;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}
