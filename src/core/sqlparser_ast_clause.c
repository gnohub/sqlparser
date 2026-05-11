#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"

static int sqlparser_clause_field_name_is(const ProtobufCFieldDescriptor *field, const char *name)
{
	return field != NULL && field->name != NULL && strcmp(field->name, name) == 0;
}

static int sqlparser_clause_field_is_where(const ProtobufCFieldDescriptor *field)
{
	const ProtobufCMessageDescriptor *message_descriptor;

	if (field == NULL ||
	    field->type != PROTOBUF_C_TYPE_MESSAGE ||
	    field->label == PROTOBUF_C_LABEL_REPEATED ||
	    !sqlparser_clause_field_name_is(field, "where_clause")) {
		return 0;
	}

	message_descriptor = (const ProtobufCMessageDescriptor *)field->descriptor;
	return message_descriptor == &pg_query__node__descriptor;
}

static int sqlparser_clause_select_has_base_slot(const PgQuery__SelectStmt *stmt)
{
	if (stmt == NULL || stmt->n_values_lists > 0U) {
		return 0;
	}
	if (stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	    stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE &&
	    stmt->n_target_list == 0U &&
	    stmt->n_from_clause == 0U &&
	    stmt->where_clause == NULL &&
	    stmt->n_sort_clause == 0U) {
		return stmt->larg != NULL || stmt->rarg != NULL;
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

static int sqlparser_clause_select_allows_where_slot(const PgQuery__SelectStmt *stmt)
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

static int sqlparser_clause_message_allows_empty_where(
	ProtobufCMessage *message,
	const ProtobufCFieldDescriptor *field)
{
	(void)field;
	if (message == NULL || message->descriptor == NULL) {
		return 0;
	}

	if (message->descriptor == &pg_query__select_stmt__descriptor) {
		return sqlparser_clause_select_allows_where_slot((const PgQuery__SelectStmt *)message);
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

static int sqlparser_clause_select_has_target_list_slot(ProtobufCMessage *message)
{
	const PgQuery__SelectStmt *stmt;

	if (message == NULL || message->descriptor != &pg_query__select_stmt__descriptor) {
		return 0;
	}
	stmt = (const PgQuery__SelectStmt *)message;
	return stmt->n_target_list > 0U && stmt->target_list != NULL;
}

static int sqlparser_clause_select_has_order_by_slot(ProtobufCMessage *message)
{
	const PgQuery__SelectStmt *stmt;

	if (message == NULL || message->descriptor != &pg_query__select_stmt__descriptor) {
		return 0;
	}
	stmt = (const PgQuery__SelectStmt *)message;
	return sqlparser_clause_select_has_base_slot(stmt);
}

static int sqlparser_clause_select_has_visible_order_by_slot(
	ProtobufCMessage *message,
	const sqlparser_clause_search_t *search)
{
	const PgQuery__SelectStmt *stmt;

	if (!sqlparser_clause_select_has_order_by_slot(message)) {
		return 0;
	}
	stmt = (const PgQuery__SelectStmt *)message;
	return search == NULL || search->set_operand_depth == 0U || stmt->n_sort_clause > 0U;
}

static int sqlparser_clause_select_is_set_operation(const PgQuery__SelectStmt *stmt)
{
	return stmt != NULL &&
		stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
		stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE;
}

static sqlparser_status_t sqlparser_clause_record(
	sqlparser_clause_search_t *search,
	sqlparser_clause_kind_t kind,
	PgQuery__SelectStmt *select_stmt)
{
	size_t internal_index;

	if (search == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	internal_index = 0U;
	switch (kind) {
		case SQLPARSER_CLAUSE_KIND_SELECT_LIST:
			internal_index = search->select_list_seen++;
			break;
		case SQLPARSER_CLAUSE_KIND_WHERE:
			internal_index = search->where_seen++;
			break;
		case SQLPARSER_CLAUSE_KIND_ORDER_BY:
			internal_index = search->order_by_seen++;
			break;
		default:
			return SQLPARSER_STATUS_OK;
	}

	if (search->want_target && search->seen == search->target_index) {
		search->target_kind = kind;
		search->target_internal_index = internal_index;
		search->target_select_stmt = select_stmt;
	}
	search->seen++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_clause_walk_message(
	ProtobufCMessage *message,
	sqlparser_clause_search_t *search)
{
	const ProtobufCMessageDescriptor *descriptor;
	PgQuery__SelectStmt *select_stmt;
	uint8_t *base;
	unsigned index;
	int defer_set_order_by;

	if (message == NULL || search == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->want_target && search->target_kind != SQLPARSER_CLAUSE_KIND_UNKNOWN) {
		return SQLPARSER_STATUS_OK;
	}

	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	select_stmt = NULL;
	defer_set_order_by = 0;
	if (message->descriptor == &pg_query__select_stmt__descriptor) {
		select_stmt = (PgQuery__SelectStmt *)message;
		defer_set_order_by = sqlparser_clause_select_is_set_operation(select_stmt);
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

		if (message->descriptor == &pg_query__select_stmt__descriptor &&
		    sqlparser_clause_field_name_is(field, "target_list") &&
		    sqlparser_clause_select_has_target_list_slot(message)) {
			(void)sqlparser_clause_record(search, SQLPARSER_CLAUSE_KIND_SELECT_LIST, select_stmt);
			if (search->want_target && search->target_kind != SQLPARSER_CLAUSE_KIND_UNKNOWN) {
				return SQLPARSER_STATUS_OK;
			}
		}
		if (sqlparser_clause_field_is_where(field) &&
		    sqlparser_clause_message_allows_empty_where(message, field)) {
			(void)sqlparser_clause_record(search, SQLPARSER_CLAUSE_KIND_WHERE, NULL);
			if (search->want_target && search->target_kind != SQLPARSER_CLAUSE_KIND_UNKNOWN) {
				return SQLPARSER_STATUS_OK;
			}
		}
		if (message->descriptor == &pg_query__select_stmt__descriptor &&
		    sqlparser_clause_field_name_is(field, "sort_clause") &&
		    !defer_set_order_by &&
		    sqlparser_clause_select_has_visible_order_by_slot(message, search)) {
			(void)sqlparser_clause_record(search, SQLPARSER_CLAUSE_KIND_ORDER_BY, select_stmt);
			if (search->want_target && search->target_kind != SQLPARSER_CLAUSE_KIND_UNKNOWN) {
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

				status = sqlparser_clause_walk_message(items[item_index], search);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				if (search->want_target && search->target_kind != SQLPARSER_CLAUSE_KIND_UNKNOWN) {
					return SQLPARSER_STATUS_OK;
				}
			}
		} else {
			ProtobufCMessage *child;
			sqlparser_status_t status;
			int child_is_set_operand;

			child = *(ProtobufCMessage **)(base + field->offset);
			if (child == NULL) {
				continue;
			}
			child_is_set_operand = defer_set_order_by &&
				(sqlparser_clause_field_name_is(field, "larg") ||
				 sqlparser_clause_field_name_is(field, "rarg"));
			if (child_is_set_operand) {
				search->set_operand_depth++;
			}
			status = sqlparser_clause_walk_message(child, search);
			if (child_is_set_operand) {
				search->set_operand_depth--;
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (search->want_target && search->target_kind != SQLPARSER_CLAUSE_KIND_UNKNOWN) {
				return SQLPARSER_STATUS_OK;
			}
		}
	}

	if (defer_set_order_by && sqlparser_clause_select_has_visible_order_by_slot(message, search)) {
		(void)sqlparser_clause_record(search, SQLPARSER_CLAUSE_KIND_ORDER_BY, select_stmt);
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_resolve_statement_clause(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	sqlparser_clause_kind_t *out_kind,
	size_t *out_internal_index,
	PgQuery__SelectStmt **out_select_stmt,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_clause_search_t search;
	sqlparser_status_t status;

	if (out_kind == NULL || out_internal_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_kind = SQLPARSER_CLAUSE_KIND_UNKNOWN;
	*out_internal_index = 0U;
	if (out_select_stmt != NULL) {
		*out_select_stmt = NULL;
	}
	statement = NULL;
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = clause_index;
	status = sqlparser_clause_walk_message((ProtobufCMessage *)statement, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_kind == SQLPARSER_CLAUSE_KIND_UNKNOWN) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause selector is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_kind = search.target_kind;
	*out_internal_index = search.target_internal_index;
	if (out_select_stmt != NULL) {
		*out_select_stmt = search.target_select_stmt;
	}
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_clause_free_node_array(PgQuery__Node **nodes, size_t count)
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

static PgQuery__Node **sqlparser_clause_alloc_node_array(size_t count, sqlparser_error_t *out_error)
{
	PgQuery__Node **items;

	if (count == 0U) {
		return NULL;
	}
	if (count > SIZE_MAX / sizeof(*items)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "clause node array is too large");
		return NULL;
	}
	items = (PgQuery__Node **)calloc(count, sizeof(*items));
	if (items == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return items;
}

static sqlparser_status_t sqlparser_get_wrapper_order_by_stmt(
	PgQuery__ParseResult *ast,
	PgQuery__SelectStmt **out_stmt,
	sqlparser_error_t *out_error)
{
	PgQuery__RawStmt *raw_stmt;
	PgQuery__Node *statement;

	if (ast == NULL || out_stmt == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "wrapped ORDER BY lookup requires non-NULL arguments");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_stmt = NULL;
	if (ast->n_stmts != 1U || ast->stmts == NULL || ast->stmts[0] == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "wrapped ORDER BY parse tree is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	raw_stmt = ast->stmts[0];
	statement = raw_stmt->stmt;
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    statement->select_stmt == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "wrapped ORDER BY parse tree does not contain SELECT");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_stmt = statement->select_stmt;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_parse_order_by_nodes_sql(
	const char *sql_text,
	PgQuery__Node ***out_nodes,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	char *wrapped_sql;
	PgQuery__ParseResult *ast;
	PgQuery__SelectStmt *stmt;
	PgQuery__Node **nodes;
	sqlparser_status_t status;
	size_t index;

	if (sql_text == NULL || out_nodes == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "ORDER BY SQL outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_nodes = NULL;
	*out_count = 0U;
	if (sql_text[0] == '\0') {
		return SQLPARSER_STATUS_OK;
	}

	prefix = "SELECT 1 FROM __sqlparser_source__ ORDER BY ";
	wrapped_sql = NULL;
	ast = NULL;
	stmt = NULL;
	nodes = NULL;
	status = sqlparser_build_wrapped_sql(prefix, sql_text, NULL, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_order_by_stmt(ast, &stmt, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && stmt->n_sort_clause == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "wrapped ORDER BY parse tree does not contain sort clause");
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (status == SQLPARSER_STATUS_OK) {
		nodes = sqlparser_clause_alloc_node_array(stmt->n_sort_clause, out_error);
		if (nodes == NULL) {
			status = out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		for (index = 0U; index < stmt->n_sort_clause; index++) {
			status = sqlparser_clone_proto_node(stmt->sort_clause[index], &nodes[index], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		*out_nodes = nodes;
		*out_count = stmt->n_sort_clause;
		nodes = NULL;
	}

	sqlparser_clause_free_node_array(nodes, stmt != NULL ? stmt->n_sort_clause : 0U);
	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	return status;
}

static sqlparser_status_t sqlparser_render_order_by_nodes_sql(
	PgQuery__Node **nodes,
	size_t count,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	char *wrapped_sql;
	char *deparsed_sql;
	PgQuery__ParseResult *ast;
	PgQuery__SelectStmt *stmt;
	PgQuery__Node **replacement;
	sqlparser_status_t status;
	size_t index;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	prefix = "SELECT 1 FROM __sqlparser_source__ ORDER BY ";
	wrapped_sql = NULL;
	deparsed_sql = NULL;
	ast = NULL;
	stmt = NULL;
	replacement = NULL;
	status = sqlparser_build_wrapped_sql(prefix, "1", NULL, &wrapped_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_get_wrapper_order_by_stmt(ast, &stmt, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		replacement = sqlparser_clause_alloc_node_array(count, out_error);
		if (replacement == NULL) {
			status = out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		for (index = 0U; index < count; index++) {
			status = sqlparser_clone_proto_node(nodes[index], &replacement[index], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_clause_free_node_array(stmt->sort_clause, stmt->n_sort_clause);
		stmt->sort_clause = replacement;
		stmt->n_sort_clause = count;
		replacement = NULL;
		status = sqlparser_deparse_wrapper_ast(ast, &deparsed_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_extract_wrapped_value_sql(deparsed_sql, prefix, NULL, out_sql, out_error);
	}

	sqlparser_clause_free_node_array(replacement, count);
	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	free(deparsed_sql);
	return status;
}

static sqlparser_status_t sqlparser_statement_order_by_sql(
	const sqlparser_handle_t *handle,
	PgQuery__SelectStmt *stmt,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *core_sql;
	sqlparser_status_t status;

	if (handle == NULL || stmt == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle, statement, and out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	core_sql = NULL;
	status = sqlparser_render_order_by_nodes_sql(stmt->sort_clause, stmt->n_sort_clause, &core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_postprocess_handle_sql_fragment(handle, core_sql, "ORDER BY SQL", out_sql, out_error);
	free(core_sql);
	return status;
}

static sqlparser_status_t sqlparser_statement_set_order_by_sql(
	sqlparser_handle_t *handle,
	PgQuery__SelectStmt *stmt,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **nodes;
	size_t count;
	char *parser_sql;
	void *dialect_state;
	sqlparser_status_t status;
	size_t index;

	if (handle == NULL || stmt == NULL || sql_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle, statement, and ORDER BY SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	nodes = NULL;
	count = 0U;
	parser_sql = NULL;
	dialect_state = NULL;
	status = sqlparser_preprocess_handle_sql_fragment(
		handle,
		sql_text,
		"ORDER BY SQL",
		&parser_sql,
		&dialect_state,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_order_by_nodes_sql(parser_sql, &nodes, &count, out_error);
	}
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	for (index = 0U; index < stmt->n_sort_clause; index++) {
		sqlparser_free_proto_node(stmt->sort_clause[index]);
	}
	free(stmt->sort_clause);
	stmt->sort_clause = nodes;
	stmt->n_sort_clause = count;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_clause_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_clause_search_t search;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_count = 0U;
	statement = NULL;
	status = sqlparser_get_statement_node((sqlparser_handle_t *)handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&search, 0, sizeof(search));
	status = sqlparser_clause_walk_message((ProtobufCMessage *)statement, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*out_count = search.seen;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_clause(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	sqlparser_clause_view_t *out_clause,
	sqlparser_error_t *out_error)
{
	sqlparser_clause_kind_t kind;
	size_t internal_index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || out_clause == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and out_clause must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_clause, 0, sizeof(*out_clause));
	kind = SQLPARSER_CLAUSE_KIND_UNKNOWN;
	internal_index = 0U;
	status = sqlparser_resolve_statement_clause(
		(sqlparser_handle_t *)handle,
		statement_index,
		clause_index,
		&kind,
		&internal_index,
		NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	(void)internal_index;
	out_clause->handle = handle;
	out_clause->statement_index = statement_index;
	out_clause->clause_index = clause_index;
	out_clause->kind = kind;
	out_clause->selector.kind = SQLPARSER_SELECTOR_KIND_CLAUSE;
	out_clause->selector.statement_index = statement_index;
	out_clause->selector.item_index = clause_index;
	out_clause->has_selector = 1;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_clause_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_clause_kind_t kind;
	PgQuery__SelectStmt *select_stmt;
	size_t internal_index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	kind = SQLPARSER_CLAUSE_KIND_UNKNOWN;
	select_stmt = NULL;
	internal_index = 0U;
	status = sqlparser_resolve_statement_clause(
		(sqlparser_handle_t *)handle,
		statement_index,
		clause_index,
		&kind,
		&internal_index,
		&select_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	switch (kind) {
		case SQLPARSER_CLAUSE_KIND_SELECT_LIST:
			return sqlparser_render_select_targets_sql(handle, statement_index, internal_index, out_sql, out_error);
		case SQLPARSER_CLAUSE_KIND_WHERE:
			return sqlparser_statement_where_sql(handle, statement_index, internal_index, out_sql, out_error);
		case SQLPARSER_CLAUSE_KIND_ORDER_BY:
			return sqlparser_statement_order_by_sql(handle, select_stmt, out_sql, out_error);
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "clause kind is not readable");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}
}

sqlparser_status_t sqlparser_statement_set_clause_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_clause_kind_t kind;
	PgQuery__SelectStmt *select_stmt;
	size_t internal_index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || sql_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and clause SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	kind = SQLPARSER_CLAUSE_KIND_UNKNOWN;
	select_stmt = NULL;
	internal_index = 0U;
	status = sqlparser_resolve_statement_clause(
		handle,
		statement_index,
		clause_index,
		&kind,
		&internal_index,
		&select_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	switch (kind) {
		case SQLPARSER_CLAUSE_KIND_SELECT_LIST:
			return sqlparser_select_set_targets_sql(handle, statement_index, internal_index, sql_text, out_error);
		case SQLPARSER_CLAUSE_KIND_WHERE:
			return sqlparser_statement_set_where_sql(handle, statement_index, internal_index, sql_text, out_error);
		case SQLPARSER_CLAUSE_KIND_ORDER_BY:
			return sqlparser_statement_set_order_by_sql(handle, select_stmt, sql_text, out_error);
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "clause kind is not writable");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}
}

sqlparser_status_t sqlparser_statement_append_clause_condition(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t clause_index,
	sqlparser_bool_operator_t bool_operator,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_clause_kind_t kind;
	size_t internal_index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || sql_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle and condition SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	kind = SQLPARSER_CLAUSE_KIND_UNKNOWN;
	internal_index = 0U;
	status = sqlparser_resolve_statement_clause(
		handle,
		statement_index,
		clause_index,
		&kind,
		&internal_index,
		NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (kind != SQLPARSER_CLAUSE_KIND_WHERE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "append_condition selector must target a where clause");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_statement_append_where_sql(
		handle,
		statement_index,
		internal_index,
		bool_operator,
		sql_text,
		out_error);
}
