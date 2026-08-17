#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_internal.h"

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

typedef struct {
	PgQuery__Node ***items;
	size_t *count;
	size_t assignment_index;
	PgQuery__UpdateStmt *update_stmt;
} sqlparser_assignment_list_ref_t;

static sqlparser_status_t sqlparser_get_insert_assignment_list_ref(
	PgQuery__InsertStmt *insert_stmt,
	sqlparser_assignment_list_ref_t *out_ref,
	sqlparser_error_t *out_error)
{
	if (insert_stmt == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "INSERT statement is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (insert_stmt->on_conflict_clause == NULL ||
	    insert_stmt->on_conflict_clause->action !=
		    PG_QUERY__ON_CONFLICT_ACTION__ONCONFLICT_UPDATE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "INSERT statement does not have a conflict UPDATE assignment list");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	out_ref->items = &insert_stmt->on_conflict_clause->target_list;
	out_ref->count = &insert_stmt->on_conflict_clause->n_target_list;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_root_assignment_list_ref(
	sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_assignment_list_ref_t *out_ref,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;

	statement = NULL;
	status = sqlparser_get_statement_node(
		handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (statement == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "statement node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (statement->node_case == PG_QUERY__NODE__NODE_UPDATE_STMT) {
		if (statement->update_stmt == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "UPDATE statement is missing");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		out_ref->items = &statement->update_stmt->target_list;
		out_ref->count = &statement->update_stmt->n_target_list;
		out_ref->update_stmt = statement->update_stmt;
		return SQLPARSER_STATUS_OK;
	}
	if (statement->node_case == PG_QUERY__NODE__NODE_INSERT_STMT) {
		return sqlparser_get_insert_assignment_list_ref(
			statement->insert_stmt, out_ref, out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "statement does not have an UPDATE assignment list");
	return SQLPARSER_STATUS_UNSUPPORTED;
}

static sqlparser_status_t sqlparser_get_dml_assignment_list_ref(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	sqlparser_assignment_list_ref_t *out_ref,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	sqlparser_graph_dml_kind_t kind;
	sqlparser_status_t status;

	message = NULL;
	kind = (sqlparser_graph_dml_kind_t)0;
	status = sqlparser_get_dml_result_message(
		handle,
		statement_index,
		dml_index,
		&kind,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (kind == SQLPARSER_GRAPH_DML_UPDATE) {
		if (message == NULL ||
		    message->descriptor != &pg_query__update_stmt__descriptor) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "UPDATE DML metadata does not match the statement AST");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		out_ref->update_stmt = (PgQuery__UpdateStmt *)message;
		out_ref->items = &out_ref->update_stmt->target_list;
		out_ref->count = &out_ref->update_stmt->n_target_list;
		return SQLPARSER_STATUS_OK;
	}
	if (kind == SQLPARSER_GRAPH_DML_INSERT) {
		if (message == NULL ||
		    message->descriptor != &pg_query__insert_stmt__descriptor) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "INSERT DML metadata does not match the statement AST");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return sqlparser_get_insert_assignment_list_ref(
			(PgQuery__InsertStmt *)message, out_ref, out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "DML assignment selector does not target an UPDATE or INSERT conflict action");
	return SQLPARSER_STATUS_UNSUPPORTED;
}

static sqlparser_status_t sqlparser_get_assignment_list_ref(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_assignment_list_ref_t *out_ref,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeStmt *merge_stmt;
	PgQuery__Node *when_node;
	PgQuery__MergeWhenClause *when_clause;
	sqlparser_status_t status;

	if (out_ref == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment list output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_ref, 0, sizeof(*out_ref));
	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector kind must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (selector->kind == SQLPARSER_SELECTOR_KIND_ASSIGNMENT) {
		out_ref->assignment_index = selector->item_index;
		status = selector->row_index == 0U ?
			sqlparser_get_root_assignment_list_ref(
				handle,
				selector->statement_index,
				out_ref,
				out_error) :
			sqlparser_get_dml_assignment_list_ref(
				handle,
				selector->statement_index,
				selector->row_index - 1U,
				out_ref,
				out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		return SQLPARSER_STATUS_OK;
	}

	merge_stmt = NULL;
	status = sqlparser_get_merge_stmt_by_dml_index(
		handle,
		selector->statement_index,
		selector->row_index,
		&merge_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector->item_index >= merge_stmt->n_merge_when_clauses) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "merge WHEN index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (merge_stmt->merge_when_clauses == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MERGE WHEN list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	when_node = merge_stmt->merge_when_clauses[selector->item_index];
	if (when_node == NULL ||
	    when_node->node_case != PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ||
	    when_node->merge_when_clause == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "MERGE WHEN node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	when_clause = when_node->merge_when_clause;
	if (when_clause->command_type != PG_QUERY__CMD_TYPE__CMD_UPDATE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "merge assignment selector does not target an UPDATE action");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	out_ref->items = &when_clause->target_list;
	out_ref->count = &when_clause->n_target_list;
	out_ref->assignment_index = selector->column_index;
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

static sqlparser_status_t sqlparser_update_build_literal_node(
	const sqlparser_literal_value_t *value,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	PgQuery__AConst *a_const;
	sqlparser_status_t status;

	if (out_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node = NULL;
	if (value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	node = (PgQuery__Node *)calloc(1U, sizeof(*node));
	a_const = (PgQuery__AConst *)calloc(1U, sizeof(*a_const));
	if (node == NULL || a_const == NULL) {
		free(node);
		free(a_const);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	pg_query__node__init(node);
	pg_query__a__const__init(a_const);
	node->node_case = PG_QUERY__NODE__NODE_A_CONST;
	node->a_const = a_const;
	a_const->location = SQLPARSER_PROTO_LOCATION_GENERATED;

	status = sqlparser_a_const_set_literal(a_const, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(node);
		return status;
	}

	*out_node = node;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_parse_update_assignment_nodes_sql(
	const char *sql_text,
	const sqlparser_generated_source_t *source,
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
	status = sqlparser_mark_proto_nodes_generated_with_fragment_source(
		nodes,
		stmt->n_target_list,
		wrapped_sql,
		strlen(prefix),
		source,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_update_free_node_array(nodes, stmt->n_target_list);
		nodes = NULL;
		goto done;
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
	const sqlparser_generated_source_t *source,
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
	status = sqlparser_parse_update_assignment_nodes_sql(
		sql_text,
		source,
		&nodes,
		&count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (count != 1U) {
		sqlparser_update_free_node_array(nodes, count);
		if (source != NULL && source->spelling_handle != NULL) {
			sqlparser_handle_sweep_identifier_spellings(
				source->spelling_handle);
		}
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

static sqlparser_status_t sqlparser_get_assignment_res_target(
	const sqlparser_assignment_list_ref_t *list,
	size_t assignment_index,
	PgQuery__ResTarget **out_target,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **items;
	size_t count;
	PgQuery__Node *target_node;

	if (out_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_target must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_target = NULL;
	if (list == NULL || list->items == NULL || list->count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"assignment list must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	count = *list->count;
	if (assignment_index >= count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"assignment_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	items = *list->items;
	if (items == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"assignment list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	target_node = items[assignment_index];
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

static sqlparser_status_t sqlparser_validate_assignment_nodes(
	const sqlparser_assignment_list_ref_t *list,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **items;
	size_t count;
	size_t index;

	if (list == NULL || list->items == NULL || list->count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment list must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	count = *list->count;
	if (count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	items = *list->items;
	if (items == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "assignment list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	for (index = 0U; index < count; index++) {
		PgQuery__Node *node;

		node = items[index];
		if (node == NULL ||
		    node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
		    node->res_target == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "update assignment node is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_update_assignment_effective_column_name(const PgQuery__ResTarget *target)
{
	const char *name;

	if (target == NULL) {
		return NULL;
	}
	if (target->n_indirection > 0U && target->indirection != NULL &&
	    sqlparser_node_string_value(target->indirection[target->n_indirection - 1U], &name) &&
	    name != NULL && name[0] != '\0') {
		return name;
	}
	return target->name;
}

sqlparser_status_t sqlparser_update_assignment_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	if (out_count == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_count must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_count = 0U;
	sqlparser_error_clear(out_error);
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = statement_index;
	status = sqlparser_get_assignment_list_ref(
		(sqlparser_handle_t *)handle,
		&selector,
		&list,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = *list.count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_assignment_by_selector(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	PgQuery__ResTarget *target;
	PgQuery__Node *value_node;
	sqlparser_status_t status;

	if (out_assignment == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_assignment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_assignment_view_clear(out_assignment);
	sqlparser_error_clear(out_error);
	status = sqlparser_get_assignment_list_ref(
		(sqlparser_handle_t *)handle,
		selector,
		&list,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_assignment_res_target(
		&list,
		list.assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	out_assignment->column_name = sqlparser_update_assignment_effective_column_name(target);
	out_assignment->value_kind = sqlparser_node_value_kind(target->val);
	if (out_assignment->value_kind == SQLPARSER_VALUE_KIND_LITERAL) {
		value_node = sqlparser_unwrap_grouping_node(target->val);
		return sqlparser_fill_literal_view_from_a_const_with_sql(
			value_node->a_const,
			sqlparser_effective_parser_sql(handle),
			&out_assignment->literal,
			out_error);
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_assignment(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	sqlparser_assignment_view_t *out_assignment,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = statement_index;
	selector.item_index = assignment_index;
	return sqlparser_assignment_by_selector(handle, &selector, out_assignment, out_error);
}

sqlparser_status_t sqlparser_assignment_set_literal_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	PgQuery__ResTarget *target;
	PgQuery__Node *old_value;
	PgQuery__Node *new_value;
	PgQuery__Node **value_slot;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	new_value = NULL;
	status = sqlparser_get_assignment_list_ref(handle, selector, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_assignment_res_target(
		&list,
		list.assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	value_slot = &target->val;
	while (sqlparser_node_is_grouping_wrapper(*value_slot)) {
		value_slot = &(*value_slot)->a_indirection->arg;
	}
	if (*value_slot != NULL &&
	    (*value_slot)->node_case == PG_QUERY__NODE__NODE_A_CONST &&
	    (*value_slot)->a_const != NULL) {
		status = sqlparser_a_const_set_literal((*value_slot)->a_const, value, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		return sqlparser_handle_commit_ast(handle, out_error);
	}

	if (*value_slot == NULL ||
	    (*value_slot)->node_case != PG_QUERY__NODE__NODE_PARAM_REF) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"update assignment value is not a literal or bind");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	status = sqlparser_update_build_literal_node(value, &new_value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	old_value = *value_slot;
	*value_slot = new_value;
	new_value = NULL;
	sqlparser_free_proto_node(old_value);
	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_assignment_value_node_index_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t *out_node_index,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	PgQuery__ResTarget *target;
	PgQuery__Node *value_node;
	sqlparser_status_t status;

	if (out_node_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_node_index must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node_index = 0U;
	status = sqlparser_get_assignment_list_ref(
		handle,
		selector,
		&list,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_get_assignment_res_target(
		&list,
		list.assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	value_node = sqlparser_unwrap_grouping_node(target->val);
	if (value_node == NULL ||
	    (value_node->node_case != PG_QUERY__NODE__NODE_A_CONST &&
	     value_node->node_case != PG_QUERY__NODE__NODE_PARAM_REF)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"update assignment value is not a literal or bind");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return sqlparser_find_statement_node_index_by_node(
		handle,
		selector->statement_index,
		value_node,
		out_node_index,
		out_error);
}

sqlparser_status_t sqlparser_update_set_assignment_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = statement_index;
	selector.item_index = assignment_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.literal = value;
	return sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
}

sqlparser_status_t sqlparser_assignment_sql_by_selector(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	PgQuery__ResTarget *target;
	sqlparser_status_t status;
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
	status = sqlparser_get_assignment_list_ref(
		(sqlparser_handle_t *)handle,
		selector,
		&list,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_assignment_res_target(
		&list,
		list.assignment_index,
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

	status = sqlparser_render_update_assignment_node_sql(
		handle,
		target->val,
		&core_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		selector->statement_index,
		core_sql,
		SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
		(ProtobufCMessage *const *)&target->val,
		1U,
		"update assignment SQL",
		out_sql,
		out_error);
	free(core_sql);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_assignment_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = statement_index;
	selector.item_index = assignment_index;
	return sqlparser_assignment_sql_by_selector(handle, &selector, out_sql, out_error);
}

sqlparser_status_t sqlparser_assignment_set_sql_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	PgQuery__ResTarget *target;
	PgQuery__Node *replacement;
	sqlparser_status_t status;
	char *parser_sql;
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
	void *dialect_state;

	sqlparser_error_clear(out_error);
	parser_sql = NULL;
	origins = NULL;
	dialect_state = NULL;
	replacement = NULL;
	status = sqlparser_get_assignment_list_ref(handle, selector, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_get_assignment_res_target(
		&list,
		list.assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		selector->statement_index,
		sql_text,
		"update assignment value SQL",
		&parser_sql,
		&dialect_state,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&source, 0, sizeof(source));
	source.public_sql = sql_text;
	source.origins = origins;
	source.dialect = handle->dialect;
	source.spelling_handle = handle;
	source.candidate_dialect_state = dialect_state;
	source.statement_index = selector->statement_index;
	status = sqlparser_parse_update_assignment_node_sql(
		parser_sql,
		&source,
		&replacement,
		out_error);
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	sqlparser_free_proto_node(target->val);
	target->val = replacement;
	replacement = NULL;
	status = sqlparser_handle_commit_ast_with_dialect_state(
		handle, dialect_state, out_error);
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
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = statement_index;
	selector.item_index = assignment_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = sql_text;
	return sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
}

sqlparser_status_t sqlparser_assignment_insert_sql_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *assignment_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	PgQuery__Node **next_nodes;
	PgQuery__Node **old_nodes;
	PgQuery__Node *new_node;
	size_t old_count;
	size_t index;
	char *parser_sql;
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
	void *dialect_state;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	memset(&list, 0, sizeof(list));
	next_nodes = NULL;
	old_nodes = NULL;
	new_node = NULL;
	parser_sql = NULL;
	origins = NULL;
	dialect_state = NULL;

	status = sqlparser_get_assignment_list_ref(handle, selector, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	old_count = *list.count;
	if (list.assignment_index > old_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (old_count == ((size_t)-1)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "update SET list is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_validate_assignment_nodes(&list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		selector->statement_index,
		assignment_sql,
		"update assignment SQL",
		&parser_sql,
		&dialect_state,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&source, 0, sizeof(source));
	source.public_sql = assignment_sql;
	source.origins = origins;
	source.dialect = handle->dialect;
	source.spelling_handle = handle;
	source.candidate_dialect_state = dialect_state;
	source.statement_index = selector->statement_index;
	status = sqlparser_parse_single_update_assignment_sql(
		parser_sql,
		&source,
		&new_node,
		out_error);
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	next_nodes = sqlparser_update_alloc_node_array(old_count + 1U, out_error);
	if (next_nodes == NULL) {
		sqlparser_free_proto_node(new_node);
		sqlparser_handle_sweep_identifier_spellings(handle);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_NO_MEMORY;
	}

	old_nodes = *list.items;
	for (index = 0U; index < list.assignment_index; index++) {
		next_nodes[index] = old_nodes[index];
	}
	next_nodes[list.assignment_index] = new_node;
	for (index = list.assignment_index; index < old_count; index++) {
		next_nodes[index + 1U] = old_nodes[index];
	}
	new_node = NULL;
	*list.items = next_nodes;
	*list.count = old_count + 1U;
	next_nodes = NULL;
	free(old_nodes);

	status = sqlparser_handle_commit_ast_with_dialect_state(
		handle, dialect_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_insert_assignment_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const char *assignment_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = statement_index;
	selector.item_index = assignment_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_INSERT_ASSIGNMENT;
	patch.sql = assignment_sql;
	return sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
}

sqlparser_status_t sqlparser_assignment_insert_from_assignment_value_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *insert_selector,
	const sqlparser_identifier_path_view_t *target,
	const sqlparser_selector_t *source_selector,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t insert_list;
	sqlparser_assignment_list_ref_t source_list;
	PgQuery__ResTarget *source_target;
	PgQuery__Node **next_nodes;
	PgQuery__Node **old_nodes;
	PgQuery__Node *cloned_value;
	PgQuery__Node *new_node;
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_generated_source_t generated_source;
	void *candidate_state;
	size_t old_count;
	size_t index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	memset(&insert_list, 0, sizeof(insert_list));
	memset(&source_list, 0, sizeof(source_list));
	source_target = NULL;
	next_nodes = NULL;
	old_nodes = NULL;
	cloned_value = NULL;
	new_node = NULL;
	origins = NULL;
	memset(&generated_source, 0, sizeof(generated_source));
	candidate_state = NULL;

	if (handle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (insert_selector == NULL || source_selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment selectors must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (insert_selector->statement_index != source_selector->statement_index) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "assignment value clone across statements is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	status = sqlparser_get_assignment_list_ref(handle, insert_selector, &insert_list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_get_assignment_list_ref(handle, source_selector, &source_list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	old_count = *insert_list.count;
	if (insert_list.assignment_index > old_count ||
	    source_list.assignment_index >= *source_list.count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (old_count == ((size_t)-1)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "update SET list is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_validate_assignment_nodes(&insert_list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_validate_assignment_nodes(&source_list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_get_assignment_res_target(
		&source_list,
		source_list.assignment_index,
		&source_target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (source_target->val == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "source assignment value is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (handle->dialect_state != NULL) {
		if (handle->dialect_ops == NULL ||
		    handle->dialect_ops->clone_state == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"dialect state cannot be cloned");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		status = handle->dialect_ops->clone_state(
			handle->dialect_state,
			&candidate_state,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (handle->dialect_ops->bind_ast_state != NULL) {
			status = handle->dialect_ops->bind_ast_state(
				candidate_state, handle->ast, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_handle_discard_dialect_state(
					handle, candidate_state);
				return status;
			}
		}
	}

	status = sqlparser_clone_proto_node(source_target->val, &cloned_value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	if (handle->dialect_ops != NULL &&
	    handle->dialect_ops->clone_ast_state != NULL) {
		status = handle->dialect_ops->clone_ast_state(
			candidate_state,
			insert_selector->statement_index,
			(ProtobufCMessage *)source_target->val,
			(ProtobufCMessage *)cloned_value,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_free_proto_node(cloned_value);
			sqlparser_handle_discard_dialect_state(
				handle, candidate_state);
			return status;
		}
	}
	status = sqlparser_identifier_origins_for_handle(
		handle, &origins, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		generated_source.public_sql = handle->sql;
		generated_source.origins = origins;
		generated_source.dialect = handle->dialect;
		generated_source.spelling_handle = handle;
		generated_source.statement_index =
			insert_selector->statement_index;
		status = sqlparser_mark_proto_generated_with_fragment_source(
			(ProtobufCMessage *)cloned_value,
			handle->parser_sql,
			0U,
			&generated_source,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(cloned_value);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	status = sqlparser_build_update_assignment_identifier_node(target, cloned_value, &new_node, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(cloned_value);
		sqlparser_handle_sweep_identifier_spellings(handle);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	cloned_value = NULL;

	next_nodes = sqlparser_update_alloc_node_array(old_count + 1U, out_error);
	if (next_nodes == NULL) {
		sqlparser_free_proto_node(new_node);
		sqlparser_handle_sweep_identifier_spellings(handle);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_NO_MEMORY;
	}

	old_nodes = *insert_list.items;
	for (index = 0U; index < insert_list.assignment_index; index++) {
		next_nodes[index] = old_nodes[index];
	}
	next_nodes[insert_list.assignment_index] = new_node;
	for (index = insert_list.assignment_index; index < old_count; index++) {
		next_nodes[index + 1U] = old_nodes[index];
	}
	new_node = NULL;
	*insert_list.items = next_nodes;
	*insert_list.count = old_count + 1U;
	next_nodes = NULL;
	free(old_nodes);

	status = candidate_state != NULL ?
		sqlparser_handle_commit_ast_with_dialect_state(
			handle, candidate_state, out_error) :
		sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_assignment_delete_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	PgQuery__Node **next_nodes;
	PgQuery__Node **old_nodes;
	PgQuery__Node *deleted_node;
	size_t old_count;
	size_t index;
	size_t next_index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	memset(&list, 0, sizeof(list));
	next_nodes = NULL;
	old_nodes = NULL;
	deleted_node = NULL;

	status = sqlparser_get_assignment_list_ref(handle, selector, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	old_count = *list.count;
	if (list.assignment_index >= old_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (old_count <= 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "update SET list cannot be empty");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	status = sqlparser_validate_assignment_nodes(&list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	next_nodes = sqlparser_update_alloc_node_array(old_count - 1U, out_error);
	if (next_nodes == NULL) {
		return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
			out_error->code :
			SQLPARSER_STATUS_NO_MEMORY;
	}

	old_nodes = *list.items;
	next_index = 0U;
	for (index = 0U; index < old_count; index++) {
		if (index == list.assignment_index) {
			deleted_node = old_nodes[index];
			continue;
		}
		next_nodes[next_index] = old_nodes[index];
		next_index++;
	}
	*list.items = next_nodes;
	*list.count = old_count - 1U;
	next_nodes = NULL;
	free(old_nodes);
	sqlparser_free_proto_node(deleted_node);

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_update_delete_assignment(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = statement_index;
	selector.item_index = assignment_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_DELETE_ASSIGNMENT;
	return sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
}

sqlparser_status_t sqlparser_assignment_set_full_sql_by_selector(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *assignment_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_assignment_list_ref_t list;
	PgQuery__ResTarget *target;
	PgQuery__Node *replacement;
	PgQuery__Node *old_node;
	char *parser_sql;
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
	void *dialect_state;
	sqlparser_status_t status;
	int reorient_result;

	sqlparser_error_clear(out_error);
	memset(&list, 0, sizeof(list));
	target = NULL;
	replacement = NULL;
	old_node = NULL;
	parser_sql = NULL;
	origins = NULL;
	dialect_state = NULL;

	status = sqlparser_get_assignment_list_ref(handle, selector, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_get_assignment_res_target(
		&list,
		list.assignment_index,
		&target,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		selector->statement_index,
		assignment_sql,
		"update assignment SQL",
		&parser_sql,
		&dialect_state,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&source, 0, sizeof(source));
	source.public_sql = assignment_sql;
	source.origins = origins;
	source.dialect = handle->dialect;
	source.spelling_handle = handle;
	source.candidate_dialect_state = dialect_state;
	source.statement_index = selector->statement_index;
	status = sqlparser_parse_single_update_assignment_sql(
		parser_sql,
		&source,
		&replacement,
		out_error);
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	if (list.update_stmt != NULL &&
	    sqlparser_dialect_is_mysql_compatible(handle->dialect)) {
		reorient_result =
			sqlparser_mysql_reorient_replaced_update_join(
				dialect_state,
				selector->statement_index,
				list.update_stmt,
				replacement->res_target);
		if (reorient_result < 0) {
			sqlparser_free_proto_node(replacement);
			sqlparser_handle_sweep_identifier_spellings(handle);
			sqlparser_handle_discard_dialect_state(
				handle,
				dialect_state);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"replacement assignment cannot safely determine the MySQL UPDATE JOIN target");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}

	(void)target;
	old_node = (*list.items)[list.assignment_index];
	(*list.items)[list.assignment_index] = replacement;
	replacement = NULL;
	sqlparser_free_proto_node(old_node);

	status = sqlparser_handle_commit_ast_with_dialect_state(
		handle, dialect_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_update_set_assignment_full_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t assignment_index,
	const char *assignment_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
	selector.statement_index = statement_index;
	selector.item_index = assignment_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
	patch.sql = assignment_sql;
	return sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
}

static sqlparser_status_t sqlparser_render_update_assignment_nodes_sql(
	const sqlparser_handle_t *handle,
	PgQuery__Node *const *nodes,
	size_t count,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	static const char prefix[] = "UPDATE __sqlparser_target__ SET ";
	PgQuery__ParseResult *ast;
	PgQuery__Node *statement;
	PgQuery__UpdateStmt *stmt;
	PgQuery__Node **replacement;
	PgQuery__List list;
	char *wrapped_sql;
	char *deparsed_sql;
	size_t index;
	sqlparser_status_t status;

	*out_sql = NULL;
	ast = NULL;
	replacement = NULL;
	wrapped_sql = NULL;
	deparsed_sql = NULL;
	status = sqlparser_build_wrapped_sql(
		prefix,
		"__sqlparser_column__ = NULL",
		NULL,
		&wrapped_sql,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_parse_wrapper_ast(wrapped_sql, &ast, out_error);
	}
	if (status == SQLPARSER_STATUS_OK &&
	    (ast->n_stmts != 1U ||
	     ast->stmts == NULL ||
	     ast->stmts[0] == NULL ||
	     ast->stmts[0]->stmt == NULL ||
	     ast->stmts[0]->stmt->node_case !=
		     PG_QUERY__NODE__NODE_UPDATE_STMT ||
	     ast->stmts[0]->stmt->update_stmt == NULL)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"wrapped update SET parse tree is invalid");
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (status == SQLPARSER_STATUS_OK) {
		replacement = sqlparser_update_alloc_node_array(count, out_error);
		if (replacement == NULL) {
			status = out_error != NULL &&
					out_error->code != SQLPARSER_STATUS_OK ?
				out_error->code :
				SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	for (index = 0U;
	     status == SQLPARSER_STATUS_OK && index < count;
	     index++) {
		status = sqlparser_clone_proto_node(
			nodes[index],
			&replacement[index],
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		pg_query__list__init(&list);
		list.n_items = count;
		list.items = replacement;
		status = sqlparser_mark_proto_generated_from_handle(
			handle,
			(ProtobufCMessage *)&list,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		statement = ast->stmts[0]->stmt;
		stmt = statement->update_stmt;
		sqlparser_update_free_node_array(
			stmt->target_list,
			stmt->n_target_list);
		stmt->target_list = replacement;
		stmt->n_target_list = count;
		replacement = NULL;
		status = sqlparser_deparse_wrapper_ast(
			handle,
			ast,
			&deparsed_sql,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_extract_wrapped_value_sql(
			deparsed_sql,
			prefix,
			NULL,
			out_sql,
			out_error);
	}

	sqlparser_update_free_node_array(replacement, count);
	if (ast != NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
	}
	free(wrapped_sql);
	free(deparsed_sql);
	return status;
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

	if (update_stmt->n_target_list == 0U ||
	    update_stmt->target_list == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"update SET list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	core_sql = NULL;
	status = sqlparser_render_update_assignment_nodes_sql(
		handle,
		update_stmt->target_list,
		update_stmt->n_target_list,
		&core_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postprocess_handle_sql_fragment(
		handle,
		statement_index,
		core_sql != NULL ? core_sql : "",
		SQLPARSER_FRAGMENT_CONTEXT_UPDATE_SET,
		(ProtobufCMessage *const *)update_stmt->target_list,
		update_stmt->n_target_list,
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
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
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
	origins = NULL;
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

	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		statement_index,
		sql_text,
		"update SET list SQL",
		&parser_sql,
		&dialect_state,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&source, 0, sizeof(source));
	source.public_sql = sql_text;
	source.origins = origins;
	source.dialect = handle->dialect;
	source.spelling_handle = handle;
	source.candidate_dialect_state = dialect_state;
	source.statement_index = statement_index;
	status = sqlparser_parse_update_assignment_nodes_sql(
		parser_sql,
		&source,
		&nodes,
		&count,
		out_error);
	sqlparser_identifier_origin_map_destroy(origins);
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

	return sqlparser_handle_commit_ast_with_dialect_state(
		handle, dialect_state, out_error);
}
