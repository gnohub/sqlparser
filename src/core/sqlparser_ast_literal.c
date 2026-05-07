#include <string.h>

#include "sqlparser_ast_internal.h"

sqlparser_value_kind_t sqlparser_node_value_kind(const PgQuery__Node *node)
{
	if (node == NULL) {
		return SQLPARSER_VALUE_KIND_UNKNOWN;
	}

	if (node->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT) {
		return SQLPARSER_VALUE_KIND_DEFAULT;
	}

	if (node->node_case == PG_QUERY__NODE__NODE_A_CONST && node->a_const != NULL) {
		return SQLPARSER_VALUE_KIND_LITERAL;
	}

	return SQLPARSER_VALUE_KIND_EXPRESSION;
}

static int sqlparser_a_const_is_supported(const PgQuery__AConst *a_const)
{
	if (a_const == NULL) {
		return 0;
	}

	if (a_const->isnull) {
		return 1;
	}

	switch (a_const->val_case) {
		case PG_QUERY__A__CONST__VAL_SVAL:
		case PG_QUERY__A__CONST__VAL_IVAL:
		case PG_QUERY__A__CONST__VAL_FVAL:
		case PG_QUERY__A__CONST__VAL_BOOLVAL:
			return 1;
		default:
			return 0;
	}
}

static int sqlparser_message_accept_supported_a_const(ProtobufCMessage *message)
{
	PgQuery__AConst *a_const;

	if (message == NULL || message->descriptor != &pg_query__a__const__descriptor) {
		return 0;
	}

	a_const = (PgQuery__AConst *)message;
	return sqlparser_a_const_is_supported(a_const);
}

static sqlparser_status_t sqlparser_record_where_literal(
	PgQuery__AConst *a_const,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (!sqlparser_a_const_is_supported(a_const)) {
		return SQLPARSER_STATUS_OK;
	}

	if (!search->want_target) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (search->seen != search->target_index) {
		search->seen++;
		return SQLPARSER_STATUS_OK;
	}

	if (search->literal_view != NULL) {
		search->literal_view->table_name = context != NULL ? context->table_name : NULL;
		search->literal_view->column_name = context != NULL ? context->column_name : NULL;
		search->literal_view->operator_name = context != NULL ? context->operator_name : NULL;
		status = sqlparser_fill_literal_view_from_a_const(
			a_const,
			&search->literal_view->literal,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	search->literal_node = a_const;
	search->seen++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_expression_literals(
	PgQuery__Node *node,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_walk_select_stmt_where_literals(
	PgQuery__SelectStmt *select_stmt,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_walk_node_array_literals(
	size_t item_count,
	PgQuery__Node **items,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	for (index = 0U; index < item_count; index++) {
		status = sqlparser_walk_expression_literals(items[index], context, search, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (search->want_target && search->literal_node != NULL) {
			return SQLPARSER_STATUS_OK;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_select_stmt_where_literals(
	PgQuery__SelectStmt *select_stmt,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (select_stmt == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	if (select_stmt->where_clause != NULL) {
		status = sqlparser_walk_expression_literals(
			select_stmt->where_clause,
			context,
			search,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (select_stmt->larg != NULL) {
		status = sqlparser_walk_select_stmt_where_literals(
			select_stmt->larg,
			context,
			search,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (select_stmt->rarg != NULL) {
		status = sqlparser_walk_select_stmt_where_literals(
			select_stmt->rarg,
			context,
			search,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_walk_expression_literals(
	PgQuery__Node *node,
	const sqlparser_predicate_context_t *context,
	sqlparser_where_literal_search_t *search,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (node == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (search->want_target && search->literal_node != NULL) {
		return SQLPARSER_STATUS_OK;
	}

	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_A_CONST:
			return sqlparser_record_where_literal(node->a_const, context, search, out_error);
		case PG_QUERY__NODE__NODE_A_EXPR:
			if (node->a_expr != NULL) {
				sqlparser_predicate_context_t left_context;
				sqlparser_predicate_context_t right_context;
				const char *left_table;
				const char *left_column;
				const char *right_table;
				const char *right_column;
				const char *operator_name;

				left_context = context != NULL ? *context : (sqlparser_predicate_context_t){NULL, NULL, NULL};
				right_context = left_context;
				left_table = NULL;
				left_column = NULL;
				right_table = NULL;
				right_column = NULL;
				operator_name = sqlparser_a_expr_operator_name(node->a_expr);
				left_context.operator_name = operator_name;
				right_context.operator_name = operator_name;

				if (sqlparser_try_extract_column_ref(
					    node->a_expr->lexpr,
					    &left_table,
					    &left_column)) {
					right_context.table_name = left_table;
					right_context.column_name = left_column;
				}
				if (sqlparser_try_extract_column_ref(
					    node->a_expr->rexpr,
					    &right_table,
					    &right_column)) {
					left_context.table_name = right_table;
					left_context.column_name = right_column;
				}

				status = sqlparser_walk_expression_literals(
					node->a_expr->lexpr,
					&left_context,
					search,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				return sqlparser_walk_expression_literals(
					node->a_expr->rexpr,
					&right_context,
					search,
					out_error);
			}
			return SQLPARSER_STATUS_OK;
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
			if (node->bool_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->bool_expr->n_args,
				node->bool_expr->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_TYPE_CAST:
			if (node->type_cast == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->type_cast->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
			if (node->collate_clause == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->collate_clause->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_RELABEL_TYPE:
			if (node->relabel_type == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->relabel_type->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_COERCE_VIA_IO:
			if (node->coerce_via_io == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->coerce_via_io->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
			if (node->a_indirection == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_walk_expression_literals(
				node->a_indirection->arg,
				context,
				search,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			return sqlparser_walk_node_array_literals(
				node->a_indirection->n_indirection,
				node->a_indirection->indirection,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
			if (node->a_array_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->a_array_expr->n_elements,
				node->a_array_expr->elements,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_ROW_EXPR:
			if (node->row_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->row_expr->n_args,
				node->row_expr->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_LIST:
			if (node->list == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->list->n_items,
				node->list->items,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_FUNC_CALL:
			if (node->func_call == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->func_call->n_args,
				node->func_call->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_NULL_TEST:
			if (node->null_test == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->null_test->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
			if (node->boolean_test == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_expression_literals(
				node->boolean_test->arg,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
			if (node->coalesce_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->coalesce_expr->n_args,
				node->coalesce_expr->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
			if (node->min_max_expr == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_walk_node_array_literals(
				node->min_max_expr->n_args,
				node->min_max_expr->args,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_SUB_LINK:
			if (node->sub_link == NULL) {
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_walk_expression_literals(
				node->sub_link->testexpr,
				context,
				search,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			return sqlparser_walk_expression_literals(
				node->sub_link->subselect,
				context,
				search,
				out_error);
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return sqlparser_walk_select_stmt_where_literals(
				node->select_stmt,
				context,
				search,
				out_error);
		default:
			return SQLPARSER_STATUS_OK;
	}
}


sqlparser_status_t sqlparser_statement_where_literal_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	sqlparser_where_literal_search_t search;
	PgQuery__Node *where_clause;
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
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_where_clause(
		mutable_handle,
		statement_index,
		&where_clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (where_clause == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&search, 0, sizeof(search));
	status = sqlparser_walk_expression_literals(where_clause, NULL, &search, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = search.seen;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_where_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	sqlparser_where_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	sqlparser_where_literal_search_t search;
	PgQuery__Node *where_clause;
	sqlparser_status_t status;

	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_where_literal_view_clear(out_literal);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_where_clause(
		mutable_handle,
		statement_index,
		&where_clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (where_clause == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"where literal index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = literal_index;
	search.literal_view = out_literal;
	status = sqlparser_walk_expression_literals(where_clause, NULL, &search, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (search.literal_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"where literal index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_where_set_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	sqlparser_where_literal_search_t search;
	PgQuery__Node *where_clause;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	status = sqlparser_get_statement_where_clause(
		handle,
		statement_index,
		&where_clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (where_clause == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"where literal index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = literal_index;
	status = sqlparser_walk_expression_literals(where_clause, NULL, &search, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (search.literal_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"where literal index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_a_const_set_literal(search.literal_node, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_statement_literal_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
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
	return sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__a__const__descriptor,
		sqlparser_message_accept_supported_a_const,
		0,
		0U,
		out_count,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_statement_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	ProtobufCMessage *message;
	sqlparser_status_t status;

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
	message = NULL;
	status = sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__a__const__descriptor,
		sqlparser_message_accept_supported_a_const,
		1,
		literal_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_fill_literal_view_from_a_const((PgQuery__AConst *)message, out_literal, out_error);
}

sqlparser_status_t sqlparser_statement_set_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t literal_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		statement_index,
		&pg_query__a__const__descriptor,
		sqlparser_message_accept_supported_a_const,
		1,
		literal_index,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"literal_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_a_const_set_literal((PgQuery__AConst *)message, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}
