#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_dml_result_internal.h"
#include "../dialect/sqlparser_dialect_internal.h"
#include "../dialect/sqlparser_dialect_multi_insert_internal.h"
#include "../dialect/sqlparser_dialect_sqlserver_internal.h"
#include "../dialect/sqlparser_dialect_sqlserver_scan.h"

static size_t sqlparser_patch_identifier_token_end(
	const sqlparser_handle_t *handle,
	size_t start);
static size_t sqlparser_patch_parser_identifier_token_end(
	const sqlparser_handle_t *handle,
	size_t start);
static PgQuery__Node *sqlparser_patch_new_insert_column_node(
	const char *name,
	int32_t location,
	sqlparser_error_t *out_error);
static sqlparser_status_t sqlparser_patch_render_merge_insert_cell_source_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error);
static int sqlparser_patch_source_word_at(
	const char *sql,
	size_t length,
	size_t pos,
	const char *word);

static sqlparser_status_t sqlparser_patch_merge_insert_clause(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	PgQuery__MergeStmt **out_merge_stmt,
	PgQuery__MergeWhenClause **out_clause,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeStmt *merge_stmt;
	PgQuery__Node *when_node;
	sqlparser_status_t status;

	if (handle == NULL || selector == NULL || out_clause == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT selector arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (out_merge_stmt != NULL) {
		*out_merge_stmt = NULL;
	}
	*out_clause = NULL;
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
	if (merge_stmt == NULL ||
	    selector->item_index >= merge_stmt->n_merge_when_clauses) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE WHEN index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (merge_stmt->merge_when_clauses == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"MERGE WHEN list is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	when_node = merge_stmt->merge_when_clauses[selector->item_index];
	if (when_node == NULL ||
	    when_node->node_case !=
		    PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ||
	    when_node->merge_when_clause == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"MERGE WHEN node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (when_node->merge_when_clause->command_type !=
	    PG_QUERY__CMD_TYPE__CMD_INSERT) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT selector does not target an INSERT action");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (out_merge_stmt != NULL) {
		*out_merge_stmt = merge_stmt;
	}
	*out_clause = when_node->merge_when_clause;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_parse_selector(
	const char *selector_text,
	sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	if (selector_text == NULL || selector_text[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"patch selector must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_selector_parse(selector_text, selector, out_error);
}

static sqlparser_status_t sqlparser_patch_source_offset(
	const sqlparser_handle_t *handle,
	int32_t parser_location,
	size_t *out_offset,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_identifier_origin_t origin;
	size_t parser_end;
	size_t parser_start;
	size_t source_end;
	sqlparser_status_t status;

	*out_supported = 0;
	if (parser_location < 0) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_identifier_origins_for_handle(
		handle,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (sqlparser_identifier_origin_map_lookup(
		    origins,
		    (size_t)parser_location,
		    1U,
		    &origin) == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE &&
	    origin.source_length > 0U &&
	    origin.source_offset < handle->sql_len) {
		*out_offset = origin.source_offset;
		*out_supported = 1;
		return SQLPARSER_STATUS_OK;
	}
	parser_start = (size_t)parser_location;
	if (!sqlparser_dialect_is_sqlserver_compatible(handle->dialect) ||
	    parser_start >= handle->parser_sql_len ||
	    handle->parser_sql[parser_start] != '"') {
		return SQLPARSER_STATUS_OK;
	}
	parser_end = sqlparser_patch_parser_identifier_token_end(
		handle,
		parser_start);
	if (parser_end <= parser_start || parser_end > handle->parser_sql_len ||
	    sqlparser_identifier_origin_map_lookup(
		    origins,
		    parser_start,
		    parser_end - parser_start,
		    &origin) != SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
	    origin.source_length == 0U ||
	    origin.source_offset >= handle->sql_len ||
	    origin.source_length > handle->sql_len - origin.source_offset) {
		return SQLPARSER_STATUS_OK;
	}
	source_end = origin.source_offset + origin.source_length;
	if ((handle->sql[origin.source_offset] != '[' &&
	     handle->sql[origin.source_offset] != '"') ||
	    sqlparser_patch_identifier_token_end(
		    handle,
		    origin.source_offset) != source_end) {
		return SQLPARSER_STATUS_OK;
	}
	*out_offset = origin.source_offset;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_identifier_quote_at(
	const sqlparser_handle_t *handle,
	size_t start)
{
	if (start >= handle->sql_len) {
		return 0;
	}
	if (handle->sql[start] == '`') {
		return sqlparser_dialect_is_mysql_compatible(handle->dialect);
	}
	if (handle->sql[start] == '[') {
		return sqlparser_dialect_is_sqlserver_compatible(handle->dialect);
	}
	if (handle->sql[start] != '"' ||
	    sqlparser_dialect_is_mysql_compatible(handle->dialect)) {
		return 0;
	}
	return start < 2U || handle->sql[start - 1U] != '&' ||
	       (handle->sql[start - 2U] != 'U' &&
		handle->sql[start - 2U] != 'u');
}

static size_t sqlparser_patch_delimited_span_end(
	const sqlparser_handle_t *handle,
	size_t start)
{
	char close_quote;
	size_t end;

	if (start >= handle->sql_len ||
	    (handle->sql[start] != '`' && handle->sql[start] != '[')) {
		return start;
	}
	close_quote = handle->sql[start] == '[' ? ']' : '`';
	end = start + 1U;
	while (end < handle->sql_len) {
		if (handle->sql[end] != close_quote) {
			end++;
			continue;
		}
		if (end + 1U < handle->sql_len &&
		    handle->sql[end + 1U] == close_quote) {
			end += 2U;
			continue;
		}
		return end + 1U;
	}
	return end;
}

static size_t sqlparser_patch_identifier_token_end(
	const sqlparser_handle_t *handle,
	size_t start)
{
	size_t end;

	if (start >= handle->sql_len) {
		return start;
	}
	if (sqlparser_patch_identifier_quote_at(handle, start)) {
		end = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			start);
		return end > start && end <= handle->sql_len ? end : start;
	}
	end = start;
	while (end < handle->sql_len &&
	       sqlparser_public_char_is_ident(
		       (unsigned char)handle->sql[end])) {
		end++;
	}
	return end;
}

static int sqlparser_patch_sqlserver_json_value_column(
	ProtobufCMessage *message)
{
	PgQuery__JsonFuncExpr *json;

	json = (PgQuery__JsonFuncExpr *)message;
	return json != NULL &&
		json->op == PG_QUERY__JSON_EXPR_OP__JSON_VALUE_OP &&
		json->context_item != NULL &&
		json->context_item->raw_expr != NULL &&
		json->context_item->raw_expr->node_case ==
			PG_QUERY__NODE__NODE_COLUMN_REF &&
		json->context_item->raw_expr->column_ref != NULL;
}

static int sqlparser_patch_sqlserver_control_surface_eligible(
	const sqlparser_handle_t *handle,
	size_t statement_index)
{
	const PgQuery__RawStmt *raw_stmt;
	const sqlparser_control_unit_t *unit;

	if (handle == NULL || handle->control == NULL || handle->ast == NULL ||
	    statement_index >= handle->control->unit_count) {
		return 0;
	}
	unit = &handle->control->units[statement_index];
	if (unit->source_length == 0U ||
	    unit->source_offset > handle->sql_len ||
	    unit->source_length > handle->sql_len - unit->source_offset ||
	    unit->ast_statement_index >= handle->ast->n_stmts ||
	    handle->ast->stmts == NULL) {
		return 0;
	}
	raw_stmt = handle->ast->stmts[unit->ast_statement_index];
	if (raw_stmt == NULL || raw_stmt->stmt == NULL) {
		return 0;
	}
	if (unit->kind == SQLPARSER_CONTROL_UNIT_CONDITION) {
		return raw_stmt->stmt->node_case ==
			PG_QUERY__NODE__NODE_SELECT_STMT;
	}
	return unit->kind == SQLPARSER_CONTROL_UNIT_STATEMENT &&
	       (raw_stmt->stmt->node_case ==
			PG_QUERY__NODE__NODE_SELECT_STMT ||
		raw_stmt->stmt->node_case ==
			PG_QUERY__NODE__NODE_INSERT_STMT ||
		raw_stmt->stmt->node_case ==
			PG_QUERY__NODE__NODE_UPDATE_STMT ||
		raw_stmt->stmt->node_case ==
			PG_QUERY__NODE__NODE_DELETE_STMT ||
		raw_stmt->stmt->node_case ==
			PG_QUERY__NODE__NODE_MERGE_STMT);
}

static int sqlparser_patch_control_surface_span_is_local(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t source_start,
	size_t source_end)
{
	const sqlparser_control_unit_t *unit;
	size_t unit_end;

	if (handle == NULL || handle->control == NULL) {
		return 1;
	}
	if (statement_index >= handle->control->unit_count ||
	    source_start > source_end) {
		return 0;
	}
	unit = &handle->control->units[statement_index];
	if (unit->source_offset > handle->sql_len ||
	    unit->source_length > handle->sql_len - unit->source_offset) {
		return 0;
	}
	unit_end = unit->source_offset + unit->source_length;
	return source_start >= unit->source_offset &&
	       source_end <= unit_end;
}

static int sqlparser_patch_sqlserver_merge_surface_eligible(
	const sqlparser_handle_t *handle,
	const PgQuery__MergeStmt *merge)
{
	const PgQuery__MergeWhenClause *when_clause;
	const PgQuery__Node *when_node;
	size_t ast_target_count;
	size_t cursor;
	size_t depth;
	size_t not_matched_target_count;
	size_t index;
	size_t pos;
	size_t skipped;
	size_t update_count;

	if (handle == NULL || merge == NULL || merge->with_clause != NULL ||
	    merge->n_merge_when_clauses < 2U ||
	    merge->merge_when_clauses == NULL) {
		return 0;
	}
	ast_target_count = 0U;
	update_count = 0U;
	for (index = 0U; index < merge->n_merge_when_clauses; index++) {
		when_node = merge->merge_when_clauses[index];
		if (when_node == NULL ||
		    when_node->node_case !=
			    PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ||
		    when_node->merge_when_clause == NULL) {
			return 0;
		}
		when_clause = when_node->merge_when_clause;
		if (when_clause->match_kind ==
		    PG_QUERY__MERGE_MATCH_KIND__MERGE_WHEN_NOT_MATCHED_BY_TARGET) {
			ast_target_count++;
		}
		if (when_clause->command_type == PG_QUERY__CMD_TYPE__CMD_UPDATE) {
			update_count++;
		}
	}
	if (ast_target_count == 0U || update_count != 1U) {
		return 0;
	}

	depth = 0U;
	not_matched_target_count = 0U;
	pos = 0U;
	while (pos < handle->sql_len) {
		if (sqlparser_public_comment_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			return 0;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > handle->sql_len) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			pos++;
			continue;
		}
		if (depth != 0U ||
		    !sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "when")) {
			pos++;
			continue;
		}
		cursor = pos + 4U;
		while (cursor < handle->sql_len &&
		       isspace((unsigned char)handle->sql[cursor])) {
			cursor++;
		}
		if (!sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    cursor,
			    "not")) {
			pos++;
			continue;
		}
		cursor += 3U;
		while (cursor < handle->sql_len &&
		       isspace((unsigned char)handle->sql[cursor])) {
			cursor++;
		}
		if (!sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    cursor,
			    "matched")) {
			pos++;
			continue;
		}
		cursor += 7U;
		while (cursor < handle->sql_len &&
		       isspace((unsigned char)handle->sql[cursor])) {
			cursor++;
		}
		if (!sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    cursor,
			    "by")) {
			not_matched_target_count++;
			pos = cursor;
			continue;
		}
		cursor += 2U;
		while (cursor < handle->sql_len &&
		       isspace((unsigned char)handle->sql[cursor])) {
			cursor++;
		}
		if (!sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    cursor,
			    "target")) {
			pos++;
			continue;
		}
		not_matched_target_count++;
		pos = cursor + 6U;
	}
	return depth == 0U &&
	       not_matched_target_count == ast_target_count;
}

static int sqlparser_patch_sqlserver_update_surface_eligible(
	const sqlparser_handle_t *handle,
	const PgQuery__UpdateStmt *update)
{
	const PgQuery__JoinExpr *join;
	const PgQuery__Node *from_node;
	const PgQuery__Node *target;
	size_t cursor;
	size_t depth;
	size_t index;
	size_t inner_join_count;
	size_t pos;
	size_t skipped;

	if (handle == NULL || update == NULL || update->relation == NULL ||
	    update->with_clause != NULL || update->where_clause == NULL ||
	    update->n_returning_list != 0U || update->n_target_list == 0U ||
	    update->target_list == NULL || update->n_from_clause != 1U ||
	    update->from_clause == NULL || update->from_clause[0] == NULL) {
		return 0;
	}
	for (index = 0U; index < update->n_target_list; index++) {
		target = update->target_list[index];
		if (target == NULL ||
		    target->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
		    target->res_target == NULL || target->res_target->val == NULL) {
			return 0;
		}
	}
	from_node = update->from_clause[0];
	if (from_node->node_case != PG_QUERY__NODE__NODE_JOIN_EXPR ||
	    from_node->join_expr == NULL) {
		return 0;
	}
	join = from_node->join_expr;
	if (join->jointype != PG_QUERY__JOIN_TYPE__JOIN_INNER ||
	    join->is_natural || join->larg == NULL || join->rarg == NULL ||
	    join->larg->node_case != PG_QUERY__NODE__NODE_RANGE_VAR ||
	    join->larg->range_var == NULL ||
	    join->rarg->node_case != PG_QUERY__NODE__NODE_RANGE_VAR ||
	    join->rarg->range_var == NULL || join->quals == NULL ||
	    join->n_using_clause != 0U || join->join_using_alias != NULL ||
	    join->alias != NULL) {
		return 0;
	}

	depth = 0U;
	inner_join_count = 0U;
	pos = 0U;
	while (pos < handle->sql_len) {
		if (sqlparser_public_comment_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			return 0;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > handle->sql_len) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			pos++;
			continue;
		}
		if (depth != 0U ||
		    !sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "inner")) {
			pos++;
			continue;
		}
		cursor = pos + 5U;
		while (cursor < handle->sql_len &&
		       isspace((unsigned char)handle->sql[cursor])) {
			cursor++;
		}
		if (cursor == pos + 5U ||
		    !sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    cursor,
			    "join")) {
			pos++;
			continue;
		}
		inner_join_count++;
		pos = cursor + 4U;
	}
	return depth == 0U && inner_join_count == 1U;
}

static int sqlparser_patch_insert_values_shape(
	const PgQuery__InsertStmt *insert)
{
	const PgQuery__Node *item;
	const PgQuery__SelectStmt *values;
	size_t index;

	if (insert == NULL || insert->relation == NULL ||
	    insert->n_cols == 0U || insert->cols == NULL ||
	    insert->select_stmt == NULL ||
	    insert->select_stmt->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    insert->select_stmt->select_stmt == NULL) {
		return 0;
	}
	values = insert->select_stmt->select_stmt;
	if (values->n_values_lists == 0U || values->values_lists == NULL) {
		return 0;
	}
	for (index = 0U; index < insert->n_cols; index++) {
		item = insert->cols[index];
		if (item == NULL ||
		    item->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
		    item->res_target == NULL || item->res_target->name == NULL ||
		    item->res_target->name[0] == '\0') {
			return 0;
		}
	}
	for (index = 0U; index < values->n_values_lists; index++) {
		item = values->values_lists[index];
		if (item == NULL || item->node_case != PG_QUERY__NODE__NODE_LIST ||
		    item->list == NULL || item->list->n_items != insert->n_cols ||
		    (item->list->n_items > 0U && item->list->items == NULL)) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_patch_simple_insert_values_shape(
	const PgQuery__InsertStmt *insert)
{
	const PgQuery__SelectStmt *values;

	if (!sqlparser_patch_insert_values_shape(insert) ||
	    insert->with_clause != NULL || insert->on_conflict_clause != NULL ||
	    insert->override !=
		    PG_QUERY__OVERRIDING_KIND__OVERRIDING_NOT_SET) {
		return 0;
	}
	values = insert->select_stmt->select_stmt;
	if (values->n_distinct_clause != 0U || values->into_clause != NULL ||
	    values->n_target_list != 0U || values->n_from_clause != 0U ||
	    values->where_clause != NULL || values->n_group_clause != 0U ||
	    values->group_distinct || values->having_clause != NULL ||
	    values->n_window_clause != 0U || values->n_sort_clause != 0U ||
	    values->limit_offset != NULL || values->limit_count != NULL ||
	    values->n_locking_clause != 0U || values->with_clause != NULL ||
	    (values->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	     values->op != PG_QUERY__SET_OPERATION__SETOP_NONE) ||
	    values->larg != NULL || values->rarg != NULL) {
		return 0;
	}
	return 1;
}

static int sqlparser_patch_sqlserver_simple_insert_surface_eligible(
	const sqlparser_handle_t *handle,
	const PgQuery__InsertStmt *insert)
{
	return insert != NULL && insert->n_returning_list == 0U &&
		sqlparser_patch_simple_insert_values_shape(insert) &&
		sqlparser_dialect_dml_result_count(handle, 0U) == 0U;
}

static int sqlparser_patch_sqlserver_insert_output_surface_eligible(
	const sqlparser_handle_t *handle,
	const PgQuery__InsertStmt *insert)
{
	const PgQuery__Node *item;
	sqlparser_dialect_dml_result_channel_t channel;
	sqlparser_dialect_dml_result_dml_t dml;
	size_t depth;
	size_t index;
	size_t output_count;
	size_t pos;
	size_t skipped;
	size_t target_offset;
	size_t values_count;

	if (insert == NULL || insert->n_returning_list == 0U ||
	    insert->returning_list == NULL ||
	    !sqlparser_patch_simple_insert_values_shape(insert)) {
		return 0;
	}
	for (index = 0U; index < insert->n_returning_list; index++) {
		item = insert->returning_list[index];
		if (item == NULL ||
		    item->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
		    item->res_target == NULL || item->res_target->val == NULL) {
			return 0;
		}
	}
	if (sqlparser_dialect_dml_result_count(handle, 0U) != 1U ||
	    !sqlparser_dialect_dml_result_dml_at(handle, 0U, 0U, &dml) ||
	    dml.kind != SQLPARSER_GRAPH_DML_INSERT ||
	    dml.channel_count == 0U || dml.has_parent) {
		return 0;
	}
	target_offset = 0U;
	for (index = 0U; index < dml.channel_count; index++) {
		if (!sqlparser_dialect_dml_result_channel_at(
			    handle,
			    0U,
			    0U,
			    index,
			    &channel) ||
		    channel.target_offset != target_offset ||
		    channel.target_count == 0U ||
		    channel.target_count >
			    insert->n_returning_list - target_offset ||
		    (channel.kind == SQLPARSER_GRAPH_DML_RESULT_CLIENT &&
		     ((channel.sink_sql != NULL && channel.sink_sql[0] != '\0') ||
		      channel.sink_column_count != 0U)) ||
		    (channel.kind == SQLPARSER_GRAPH_DML_RESULT_SINK &&
		     (channel.sink_sql == NULL || channel.sink_sql[0] == '\0')) ||
		    (channel.kind != SQLPARSER_GRAPH_DML_RESULT_CLIENT &&
		     channel.kind != SQLPARSER_GRAPH_DML_RESULT_SINK)) {
			return 0;
		}
		target_offset += channel.target_count;
	}
	if (target_offset != insert->n_returning_list) {
		return 0;
	}

	depth = 0U;
	output_count = 0U;
	values_count = 0U;
	pos = 0U;
	while (pos < handle->sql_len) {
		if (sqlparser_public_comment_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			if ((handle->sql[pos] == '/' &&
			     handle->sql[pos + 1U] == '*' &&
			     (handle->sql[pos + 2U] == '+' ||
			      handle->sql[pos + 2U] == '!')) ||
			    (handle->sql[pos] == '-' &&
			     handle->sql[pos + 1U] == '-' &&
			     handle->sql[pos + 2U] == '+')) {
				return 0;
			}
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				handle->sql,
				pos);
			if (skipped <= pos || skipped > handle->sql_len) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > handle->sql_len) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			pos++;
			continue;
		}
		if (depth == 0U &&
		    sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "output")) {
			output_count++;
			pos += 6U;
			continue;
		}
		if (depth == 0U &&
		    sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "values")) {
			values_count++;
			pos += 6U;
			continue;
		}
		pos++;
	}
	return depth == 0U && output_count == dml.channel_count &&
	       values_count == 1U;
}

static int sqlparser_patch_sqlserver_transaction_batch_surface_eligible(
	const sqlparser_handle_t *handle)
{
	const PgQuery__RawStmt *raw_stmt;
	const PgQuery__TransactionStmt *transaction;
	size_t index;
	size_t pos;
	size_t skipped;

	if (handle == NULL || handle->ast == NULL ||
	    handle->ast->n_stmts < 3U || handle->ast->stmts == NULL) {
		return 0;
	}
	for (index = 0U; index < handle->ast->n_stmts; index++) {
		raw_stmt = handle->ast->stmts[index];
		if (raw_stmt == NULL || raw_stmt->stmt == NULL) {
			return 0;
		}
		if (index > 0U && index + 1U < handle->ast->n_stmts) {
			if (raw_stmt->stmt->node_case !=
			    PG_QUERY__NODE__NODE_UPDATE_STMT) {
				return 0;
			}
			continue;
		}
		if (raw_stmt->stmt->node_case !=
			    PG_QUERY__NODE__NODE_TRANSACTION_STMT ||
		    raw_stmt->stmt->transaction_stmt == NULL) {
			return 0;
		}
		transaction = raw_stmt->stmt->transaction_stmt;
		if (transaction->n_options != 0U || transaction->chain ||
		    (transaction->savepoint_name != NULL &&
		     transaction->savepoint_name[0] != '\0') ||
		    (transaction->gid != NULL && transaction->gid[0] != '\0') ||
		    (index == 0U &&
		     transaction->kind !=
			     PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_BEGIN &&
		     transaction->kind !=
			     PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_START) ||
		    (index + 1U == handle->ast->n_stmts &&
		     transaction->kind !=
			     PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_COMMIT &&
		     transaction->kind !=
			     PG_QUERY__TRANSACTION_STMT_KIND__TRANS_STMT_ROLLBACK)) {
			return 0;
		}
	}

	pos = 0U;
	while (pos < handle->sql_len) {
		if (sqlparser_public_comment_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			return 0;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > handle->sql_len) {
				return 0;
			}
			pos = skipped;
		} else {
			pos++;
		}
	}
	return 1;
}

static sqlparser_status_t sqlparser_patch_sqlserver_surface_eligible(
	sqlparser_handle_t *handle,
	int *out_eligible,
	sqlparser_error_t *out_error)
{
	const PgQuery__RawStmt *raw_stmt;
	const PgQuery__SelectStmt *stmt;
	ProtobufCMessage *json_value;
	size_t pos;
	size_t skipped;
	sqlparser_status_t status;
	int comment_seen;
	int nested_set_operation;
	int odbc_function_seen;

	if (out_eligible == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server surface eligibility output is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_eligible = 0;
	if (handle == NULL || handle->sql == NULL ||
	    !sqlparser_dialect_is_sqlserver_compatible(handle->dialect) ||
	    handle->control != NULL || handle->ast == NULL ||
	    handle->statement_count != handle->ast->n_stmts ||
	    handle->ast->n_stmts == 0U ||
	    handle->ast->stmts == NULL || handle->ast->stmts[0] == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (handle->ast->n_stmts != 1U) {
		*out_eligible =
			sqlparser_patch_sqlserver_transaction_batch_surface_eligible(
				handle);
		return SQLPARSER_STATUS_OK;
	}
	raw_stmt = handle->ast->stmts[0];
	odbc_function_seen = sqlparser_sqlserver_state_has_odbc_function(
		handle->dialect_state,
		0U);
	if (raw_stmt->stmt != NULL &&
	    raw_stmt->stmt->node_case == PG_QUERY__NODE__NODE_MERGE_STMT &&
	    raw_stmt->stmt->merge_stmt != NULL) {
		*out_eligible =
			sqlparser_patch_sqlserver_merge_surface_eligible(
				handle,
				raw_stmt->stmt->merge_stmt);
		return SQLPARSER_STATUS_OK;
	}
	if (raw_stmt->stmt != NULL &&
	    raw_stmt->stmt->node_case == PG_QUERY__NODE__NODE_INSERT_STMT &&
	    raw_stmt->stmt->insert_stmt != NULL) {
		if (raw_stmt->stmt->insert_stmt->n_returning_list == 0U) {
			*out_eligible =
				sqlparser_patch_sqlserver_simple_insert_surface_eligible(
					handle,
					raw_stmt->stmt->insert_stmt);
			return SQLPARSER_STATUS_OK;
		}
		*out_eligible =
			sqlparser_patch_sqlserver_insert_output_surface_eligible(
				handle,
				raw_stmt->stmt->insert_stmt);
		return SQLPARSER_STATUS_OK;
	}
	if (raw_stmt->stmt != NULL &&
	    raw_stmt->stmt->node_case == PG_QUERY__NODE__NODE_UPDATE_STMT &&
	    raw_stmt->stmt->update_stmt != NULL) {
		*out_eligible =
			sqlparser_patch_sqlserver_update_surface_eligible(
				handle,
				raw_stmt->stmt->update_stmt);
		return SQLPARSER_STATUS_OK;
	}
	if (raw_stmt->stmt == NULL ||
	    raw_stmt->stmt->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
	    raw_stmt->stmt->select_stmt == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	stmt = raw_stmt->stmt->select_stmt;
	if (stmt->into_clause != NULL || stmt->with_clause != NULL ||
	    stmt->n_values_lists != 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (odbc_function_seen) {
		*out_eligible =
			(stmt->op ==
				 PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED ||
			 stmt->op == PG_QUERY__SET_OPERATION__SETOP_NONE) &&
			stmt->larg == NULL && stmt->rarg == NULL;
		return SQLPARSER_STATUS_OK;
	}
	nested_set_operation =
		stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
		stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE &&
		stmt->larg != NULL && stmt->rarg != NULL &&
		((stmt->larg->op !=
			  PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
		  stmt->larg->op != PG_QUERY__SET_OPERATION__SETOP_NONE) ||
		 (stmt->rarg->op !=
			  PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
		  stmt->rarg->op != PG_QUERY__SET_OPERATION__SETOP_NONE));
	if (!nested_set_operation &&
	    ((stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	      stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE) ||
	     stmt->larg != NULL || stmt->rarg != NULL)) {
		return SQLPARSER_STATUS_OK;
	}
	comment_seen = 0;
	pos = 0U;
	while (pos < handle->sql_len) {
		if (sqlparser_public_comment_at(handle->dialect, handle->sql, pos)) {
			if ((handle->sql[pos] == '/' &&
			     handle->sql[pos + 1U] == '*' &&
			     (handle->sql[pos + 2U] == '+' ||
			      handle->sql[pos + 2U] == '!')) ||
			    (handle->sql[pos] == '-' &&
			     handle->sql[pos + 1U] == '-' &&
			     handle->sql[pos + 2U] == '+')) {
				return SQLPARSER_STATUS_OK;
			}
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				handle->sql,
				pos);
			if (skipped <= pos || skipped > handle->sql_len) {
				return SQLPARSER_STATUS_OK;
			}
			comment_seen = 1;
			pos = skipped;
			continue;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		pos = skipped > pos && skipped <= handle->sql_len ?
			skipped : pos + 1U;
	}
	if (nested_set_operation) {
		*out_eligible = !comment_seen;
		return SQLPARSER_STATUS_OK;
	}
	if (comment_seen) {
		*out_eligible = 1;
		return SQLPARSER_STATUS_OK;
	}
	if (stmt->where_clause != NULL) {
		return SQLPARSER_STATUS_OK;
	}
	json_value = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		0U,
		&pg_query__json_func_expr__descriptor,
		sqlparser_patch_sqlserver_json_value_column,
		1,
		0U,
		NULL,
		&json_value,
		out_error);
	if (status == SQLPARSER_STATUS_OK && json_value != NULL) {
		*out_eligible = 1;
	}
	return status;
}

static int sqlparser_patch_identifier_component_span(
	const sqlparser_handle_t *handle,
	size_t path_start,
	size_t component_index,
	size_t *out_start,
	size_t *out_end)
{
	size_t component;
	size_t end;
	size_t pos;

	pos = path_start;
	for (component = 0U; component <= component_index; component++) {
		end = sqlparser_patch_identifier_token_end(handle, pos);
		if (end <= pos) {
			return 0;
		}
		if (component == component_index) {
			*out_start = pos;
			*out_end = end;
			return 1;
		}
		pos = end;
		while (pos < handle->sql_len &&
		       isspace((unsigned char)handle->sql[pos])) {
			pos++;
		}
		if (pos >= handle->sql_len || handle->sql[pos] != '.') {
			return 0;
		}
		pos++;
		while (pos < handle->sql_len &&
		       isspace((unsigned char)handle->sql[pos])) {
			pos++;
		}
	}
	return 0;
}

static const char *sqlparser_patch_identifier_mutation_spelling(
	const sqlparser_handle_t *handle,
	char **slot)
{
	size_t index;

	for (index = 0U; index < handle->identifier_mutation_count; index++) {
		const sqlparser_identifier_mutation_t *mutation;

		mutation = &handle->identifier_mutations[index];
		if (mutation->slot == slot && mutation->spelling != NULL) {
			return mutation->spelling;
		}
	}
	return NULL;
}

static int sqlparser_patch_existing_surface_edit_matches(
	const sqlparser_surface_source_edits_t *edits,
	size_t source_start,
	size_t source_end,
	const char *spelling)
{
	size_t index;
	size_t spelling_length;

	if (edits == NULL || spelling == NULL) {
		return 0;
	}
	spelling_length = strlen(spelling);
	for (index = 0U; index < edits->count; index++) {
		const sqlparser_surface_source_edit_t *edit;

		edit = &edits->items[index];
		if (edit->source_start == source_start &&
		    edit->source_end == source_end &&
		    edit->replacement_length == spelling_length &&
		    memcmp(
			    edit->replacement,
			    spelling,
			    spelling_length) == 0) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_patch_identifier_component_matches(
	const sqlparser_handle_t *handle,
	int32_t location,
	size_t path_start,
	size_t component_index,
	char **slot,
	const char *value,
	const sqlparser_surface_source_edits_t *edits,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const char *mutation_spelling;
	char *spelling;
	sqlparser_status_t status;

	*out_supported = 0;
	if (value == NULL || value[0] == '\0' ||
	    !sqlparser_patch_identifier_component_span(
		    handle,
		    path_start,
		    component_index,
		    out_start,
		    out_end)) {
		return SQLPARSER_STATUS_OK;
	}
	spelling = NULL;
	status = sqlparser_resolve_identifier_component_spelling(
		handle,
		location,
		component_index,
		value,
		&spelling,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		if (status == SQLPARSER_STATUS_NO_MEMORY) {
			return status;
		}
		sqlparser_error_clear(out_error);
	} else {
		if (spelling != NULL &&
		    strlen(spelling) == *out_end - *out_start &&
		    memcmp(
			    handle->sql + *out_start,
			    spelling,
			    *out_end - *out_start) == 0) {
			*out_supported = 1;
		}
		free(spelling);
	}
	if (*out_supported) {
		return SQLPARSER_STATUS_OK;
	}
	mutation_spelling = sqlparser_patch_identifier_mutation_spelling(
		handle,
		slot);
	if (sqlparser_patch_existing_surface_edit_matches(
		    edits,
		    *out_start,
		    *out_end,
		    mutation_spelling)) {
		*out_supported = 1;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_identifier_token_value_matches(
	const sqlparser_handle_t *handle,
	size_t start,
	size_t end,
	const char *value,
	size_t value_length)
{
	char close_quote;
	size_t value_index;

	if (value == NULL || end <= start || end > handle->sql_len) {
		return 0;
	}
	close_quote = '\0';
	if (handle->sql[start] == '"' || handle->sql[start] == '`') {
		close_quote = handle->sql[start++];
	} else if (handle->sql[start] == '[') {
		start++;
		close_quote = ']';
	}
	if (close_quote == '\0') {
		if (value_length != end - start) {
			return 0;
		}
		for (value_index = 0U; start < end;
		     start++, value_index++) {
			if (tolower((unsigned char)handle->sql[start]) !=
			    tolower((unsigned char)value[value_index])) {
				return 0;
			}
		}
		return 1;
	}
	if (end <= start || handle->sql[end - 1U] != close_quote) {
		return 0;
	}
	end--;
	value_index = 0U;
	while (start < end) {
		char character;

		character = handle->sql[start++];
		if (character == close_quote && start < end &&
		    handle->sql[start] == close_quote) {
			start++;
		}
		if (value_index >= value_length ||
		    value[value_index] != character) {
			return 0;
		}
		value_index++;
	}
	return value_index == value_length;
}

static sqlparser_status_t sqlparser_patch_relation_source_span_search(
	const sqlparser_handle_t *handle,
	const char *const *parts,
	size_t component_count,
	size_t anchor,
	size_t *out_start,
	size_t *out_end,
	int *out_supported)
{
	size_t candidate;
	size_t component_end;
	size_t component_index;
	size_t component_start;
	size_t cursor;
	size_t found_end;
	size_t found_start;
	size_t match_count;
	size_t next_candidate;
	size_t part_lengths[3];
	size_t protected_end;
	int anchor_found;
	int identifier_candidate;
	int matched;

	*out_supported = 0;
	if (parts == NULL || component_count == 0U ||
	    component_count > sizeof(part_lengths) / sizeof(part_lengths[0])) {
		return SQLPARSER_STATUS_OK;
	}
	anchor_found = 0;
	match_count = 0U;
	found_start = 0U;
	found_end = 0U;
	for (component_index = 0U;
	     component_index < component_count;
	     component_index++) {
		part_lengths[component_index] = strlen(parts[component_index]);
	}
	for (candidate = 0U; candidate < handle->sql_len;
	     candidate = next_candidate) {
		protected_end = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			candidate);
		if (protected_end == candidate) {
			protected_end = sqlparser_patch_delimited_span_end(
				handle,
				candidate);
		}
		identifier_candidate = sqlparser_patch_identifier_quote_at(
			handle,
			candidate);
		if (protected_end > candidate) {
			next_candidate = protected_end;
			if (!identifier_candidate) {
				continue;
			}
		} else if (sqlparser_public_char_is_ident(
				   (unsigned char)handle->sql[candidate])) {
			next_candidate = sqlparser_patch_identifier_token_end(
				handle,
				candidate);
			if (next_candidate <= candidate) {
				next_candidate = candidate + 1U;
			}
		} else {
			next_candidate = candidate + 1U;
			continue;
		}
		if ((next_candidate < handle->sql_len &&
		     handle->sql[next_candidate] == '\'') ||
		    (next_candidate + 1U < handle->sql_len &&
		     handle->sql[next_candidate] == '&' &&
		     (handle->sql[next_candidate + 1U] == '\'' ||
		      handle->sql[next_candidate + 1U] == '"'))) {
			continue;
		}
		if (candidate > 0U &&
		    (sqlparser_public_char_is_ident(
			     (unsigned char)handle->sql[candidate - 1U]) ||
		     handle->sql[candidate - 1U] == ':' ||
		     handle->sql[candidate - 1U] == '@' ||
		     handle->sql[candidate - 1U] == '$')) {
			continue;
		}
		cursor = candidate;
		while (cursor > 0U &&
		       isspace((unsigned char)handle->sql[cursor - 1U])) {
			cursor--;
		}
		if (cursor > 0U && handle->sql[cursor - 1U] == '.') {
			continue;
		}
		matched = 1;
		for (component_index = 0U;
		     component_index < component_count;
		     component_index++) {
			if (!sqlparser_patch_identifier_component_span(
				    handle,
				    candidate,
				    component_index,
				    &component_start,
				    &component_end) ||
			    !sqlparser_patch_identifier_token_value_matches(
				    handle,
				    component_start,
				    component_end,
				    parts[component_index],
				    part_lengths[component_index])) {
				matched = 0;
				break;
			}
		}
		if (!matched) {
			continue;
		}
		cursor = component_end;
		while (cursor < handle->sql_len &&
		       isspace((unsigned char)handle->sql[cursor])) {
			cursor++;
		}
		if (cursor < handle->sql_len && handle->sql[cursor] == '.') {
			continue;
		}
		if (anchor >= candidate && anchor < component_end) {
			if (anchor_found) {
				return SQLPARSER_STATUS_OK;
			}
			anchor_found = 1;
			found_start = candidate;
			found_end = component_end;
		}
		match_count++;
		if (match_count == 1U) {
			found_start = candidate;
			found_end = component_end;
		}
	}
	if (anchor_found || match_count == 1U) {
		*out_start = found_start;
		*out_end = found_end;
		*out_supported = 1;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_relation_source_span(
	const sqlparser_handle_t *handle,
	PgQuery__RangeVar *relation,
	const sqlparser_surface_source_edits_t *edits,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const char *parts[3];
	char **slots[3];
	size_t component_end;
	size_t component_index;
	size_t component_start;
	size_t component_count;
	size_t cursor;
	size_t source_start;
	sqlparser_status_t status;
	int component_supported;
	int fast_supported;

	*out_supported = 0;
	if (relation == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	component_count = 0U;
	if (relation->catalogname != NULL && relation->catalogname[0] != '\0') {
		parts[component_count++] = relation->catalogname;
		slots[component_count - 1U] = &relation->catalogname;
	}
	if (relation->schemaname != NULL && relation->schemaname[0] != '\0') {
		parts[component_count++] = relation->schemaname;
		slots[component_count - 1U] = &relation->schemaname;
	}
	if (relation->relname != NULL && relation->relname[0] != '\0') {
		parts[component_count++] = relation->relname;
		slots[component_count - 1U] = &relation->relname;
	}
	if (component_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_source_offset(
		handle,
		relation->location,
		&source_start,
		&fast_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (!fast_supported) {
		if (sqlparser_dialect_is_sqlserver_compatible(handle->dialect)) {
			return SQLPARSER_STATUS_OK;
		}
		return sqlparser_patch_relation_source_span_search(
			handle,
			parts,
			component_count,
			SIZE_MAX,
			out_start,
			out_end,
			out_supported);
	}
	for (component_index = 0U;
	     component_index < component_count;
	     component_index++) {
		status = sqlparser_patch_identifier_component_matches(
			handle,
			relation->location,
			source_start,
			component_index,
			slots[component_index],
			parts[component_index],
			edits,
			&component_start,
			&component_end,
			&component_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !component_supported) {
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (sqlparser_dialect_is_sqlserver_compatible(
				    handle->dialect)) {
				return SQLPARSER_STATUS_OK;
			}
			return sqlparser_patch_relation_source_span_search(
				handle,
				parts,
				component_count,
				source_start,
				out_start,
				out_end,
				out_supported);
		}
	}
	cursor = component_end;
	while (cursor < handle->sql_len &&
	       isspace((unsigned char)handle->sql[cursor])) {
		cursor++;
	}
	if (cursor < handle->sql_len && handle->sql[cursor] == '.') {
		*out_supported = 0;
		return SQLPARSER_STATUS_OK;
	}
	*out_start = source_start;
	*out_end = component_end;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_column_qualifier_source_span(
	const sqlparser_handle_t *handle,
	PgQuery__ColumnRef *column_ref,
	const sqlparser_surface_source_edits_t *edits,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	const char *value;
	size_t component_end;
	size_t component_index;
	size_t component_start;
	size_t cursor;
	size_t qualifier_count;
	size_t source_start;
	sqlparser_status_t status;
	int component_supported;
	int source_supported;

	*out_supported = 0;
	if (column_ref == NULL || column_ref->fields == NULL ||
	    column_ref->n_fields < 2U) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_source_offset(
		handle,
		column_ref->location,
		&source_start,
		&source_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !source_supported) {
		return status;
	}
	qualifier_count = column_ref->n_fields - 1U;
	for (component_index = 0U;
	     component_index < qualifier_count;
	     component_index++) {
		node = column_ref->fields[component_index];
		value = NULL;
		if (node == NULL ||
		    !sqlparser_node_string_value(node, &value) ||
		    value == NULL || value[0] == '\0') {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_patch_identifier_component_matches(
			handle,
			column_ref->location,
			source_start,
			component_index,
			&node->string->sval,
			value,
			edits,
			&component_start,
			&component_end,
			&component_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !component_supported) {
			return status;
		}
	}
	cursor = component_end;
	while (cursor < handle->sql_len &&
	       isspace((unsigned char)handle->sql[cursor])) {
		cursor++;
	}
	if (cursor >= handle->sql_len || handle->sql[cursor] != '.') {
		return SQLPARSER_STATUS_OK;
	}
	*out_start = source_start;
	*out_end = component_end;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_assignment_qualifier_source_span(
	const sqlparser_handle_t *handle,
	PgQuery__RangeVar *relation,
	PgQuery__ResTarget *target,
	const sqlparser_surface_source_edits_t *edits,
	size_t *out_start,
	size_t *out_end,
	int *out_present,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	char **slot;
	const char *value;
	size_t component_end;
	size_t component_index;
	size_t component_start;
	size_t cursor;
	size_t source_start;
	sqlparser_status_t status;
	int component_supported;
	int source_supported;

	*out_present = 0;
	*out_supported = 0;
	if (relation == NULL || relation->relname == NULL ||
	    relation->relname[0] == '\0' || target == NULL ||
	    target->name == NULL ||
	    target->name[0] == '\0') {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_source_offset(
		handle,
		target->location,
		&source_start,
		&source_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !source_supported) {
		return status;
	}
	if (target->n_indirection == 0U) {
		const char *mutation_spelling;
		size_t qualifier_end;
		size_t qualifier_start;
		int crossed_line;

		status = sqlparser_patch_identifier_component_matches(
			handle,
			target->location,
			source_start,
			0U,
			&target->name,
			target->name,
			edits,
			&component_start,
			&component_end,
			&component_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !component_supported) {
			return status;
		}
		cursor = component_start;
		crossed_line = 0;
		while (cursor > 0U &&
		       isspace((unsigned char)handle->sql[cursor - 1U])) {
			if (handle->sql[cursor - 1U] == '\n' ||
			    handle->sql[cursor - 1U] == '\r') {
				crossed_line = 1;
			}
			cursor--;
		}
		if (cursor == 0U || handle->sql[cursor - 1U] != '.') {
			if (crossed_line ||
			    (cursor > 0U &&
			     (handle->sql[cursor - 1U] == '/' ||
			      handle->sql[cursor - 1U] == '#' ||
			      handle->sql[cursor - 1U] == '\'' ||
			      handle->sql[cursor - 1U] == '"' ||
			      handle->sql[cursor - 1U] == '`' ||
			      handle->sql[cursor - 1U] == ']'))) {
				return SQLPARSER_STATUS_OK;
			}
			*out_supported = 1;
			return SQLPARSER_STATUS_OK;
		}
		cursor--;
		while (cursor > 0U &&
		       isspace((unsigned char)handle->sql[cursor - 1U])) {
			cursor--;
		}
		qualifier_end = cursor;
		if (qualifier_end == 0U) {
			return SQLPARSER_STATUS_OK;
		}
		if (handle->sql[qualifier_end - 1U] == '`') {
			qualifier_start = qualifier_end - 1U;
			while (qualifier_start > 0U) {
				qualifier_start--;
				if (handle->sql[qualifier_start] != '`') {
					continue;
				}
				if (qualifier_start > 0U &&
				    handle->sql[qualifier_start - 1U] == '`') {
					qualifier_start--;
					continue;
				}
				break;
			}
			if (handle->sql[qualifier_start] != '`') {
				return SQLPARSER_STATUS_OK;
			}
		} else {
			qualifier_start = qualifier_end;
			while (qualifier_start > 0U &&
			       sqlparser_public_char_is_ident(
				       (unsigned char)handle->sql[
					       qualifier_start - 1U])) {
				qualifier_start--;
			}
		}
		if (qualifier_start == qualifier_end ||
		    !sqlparser_patch_identifier_token_value_matches(
			    handle,
			    qualifier_start,
			    qualifier_end,
			    relation->relname,
			    strlen(relation->relname))) {
			mutation_spelling =
				sqlparser_patch_identifier_mutation_spelling(
					handle,
					&relation->relname);
			if (!sqlparser_patch_existing_surface_edit_matches(
				    edits,
				    qualifier_start,
				    qualifier_end,
				    mutation_spelling)) {
				return SQLPARSER_STATUS_OK;
			}
		}
		*out_start = qualifier_start;
		*out_end = qualifier_end;
		*out_present = 1;
		*out_supported = 1;
		return SQLPARSER_STATUS_OK;
	}
	if (target->indirection == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	for (component_index = 0U;
	     component_index < target->n_indirection;
	     component_index++) {
		value = target->name;
		slot = &target->name;
		if (component_index > 0U) {
			node = target->indirection[component_index - 1U];
			value = NULL;
			if (node == NULL ||
			    !sqlparser_node_string_value(node, &value)) {
				return SQLPARSER_STATUS_OK;
			}
			slot = &node->string->sval;
		}
		status = sqlparser_patch_identifier_component_matches(
			handle,
			target->location,
			source_start,
			component_index,
			slot,
			value,
			edits,
			&component_start,
			&component_end,
			&component_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !component_supported) {
			return status;
		}
	}
	cursor = component_end;
	while (cursor < handle->sql_len &&
	       isspace((unsigned char)handle->sql[cursor])) {
		cursor++;
	}
	if (cursor >= handle->sql_len || handle->sql[cursor] != '.') {
		return SQLPARSER_STATUS_OK;
	}
	*out_start = source_start;
	*out_end = component_end;
	*out_present = 1;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_surface_edit_insert_deduplicated(
	sqlparser_surface_source_edits_t *edits,
	size_t source_start,
	size_t source_end,
	const char *replacement,
	size_t replacement_length,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	char *copy;
	size_t index;

	*out_supported = 0;
	for (index = 0U; index < edits->count; index++) {
		sqlparser_surface_source_edit_t *edit;

		edit = &edits->items[index];
		if (edit->source_start != source_start ||
		    edit->source_end != source_end) {
			continue;
		}
		if (edit->replacement_length == replacement_length &&
		    memcmp(
			    edit->replacement,
			    replacement,
			    replacement_length) == 0) {
			*out_supported = 1;
			return SQLPARSER_STATUS_OK;
		}
		copy = sqlparser_strndup(replacement, replacement_length);
		if (copy == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		free(edit->replacement);
		edit->replacement = copy;
		edit->replacement_length = replacement_length;
		*out_supported = 1;
		return SQLPARSER_STATUS_OK;
	}
	return sqlparser_surface_source_edits_insert(
		edits,
		source_start,
		source_end,
		replacement,
		replacement_length,
		out_supported,
		out_error);
}

static sqlparser_status_t
sqlparser_patch_mysql_quoted_column_source_span(
	const sqlparser_handle_t *handle,
	const PgQuery__ColumnRef *column_ref,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_identifier_origin_t origin;
	const char *value;
	size_t parser_end;
	size_t parser_start;
	size_t source_end;
	size_t source_start;
	sqlparser_status_t status;

	*out_supported = 0;
	if (!sqlparser_dialect_is_mysql_compatible(handle->dialect) ||
	    column_ref == NULL || column_ref->n_fields != 1U ||
	    column_ref->fields == NULL || column_ref->fields[0] == NULL ||
	    !sqlparser_node_string_value(column_ref->fields[0], &value) ||
	    value == NULL || value[0] == '\0' || column_ref->location < 0 ||
	    (size_t)column_ref->location >= handle->parser_sql_len) {
		return SQLPARSER_STATUS_OK;
	}
	parser_start = (size_t)column_ref->location;
	if (handle->parser_sql[parser_start] != '"') {
		return SQLPARSER_STATUS_OK;
	}
	parser_end = sqlparser_public_skip_quoted_or_comment(
		handle->dialect,
		handle->parser_sql,
		parser_start);
	if (parser_end <= parser_start || parser_end > handle->parser_sql_len) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_identifier_origins_for_handle(
		handle,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (sqlparser_identifier_origin_map_lookup(
		    origins,
		    parser_start,
		    parser_end - parser_start,
		    &origin) != SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
	    origin.source_offset >= handle->sql_len ||
	    origin.source_length > handle->sql_len - origin.source_offset) {
		return SQLPARSER_STATUS_OK;
	}
	source_start = origin.source_offset;
	source_end = source_start + origin.source_length;
	if (handle->sql[source_start] != '`' ||
	    sqlparser_patch_identifier_token_end(handle, source_start) !=
		    source_end ||
	    !sqlparser_patch_identifier_token_value_matches(
		    handle,
		    source_start,
		    source_end,
		    value,
		    strlen(value))) {
		return SQLPARSER_STATUS_OK;
	}
	*out_start = source_start;
	*out_end = source_end;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_patch_parser_identifier_token_end(
	const sqlparser_handle_t *handle,
	size_t start)
{
	size_t end;

	if (handle == NULL || start >= handle->parser_sql_len) {
		return start;
	}
	if (handle->parser_sql[start] != '"') {
		end = start;
		while (end < handle->parser_sql_len &&
		       sqlparser_public_char_is_ident(
			       (unsigned char)handle->parser_sql[end])) {
			end++;
		}
		return end;
	}
	end = start + 1U;
	while (end < handle->parser_sql_len) {
		if (handle->parser_sql[end] != '"') {
			end++;
			continue;
		}
		if (end + 1U < handle->parser_sql_len &&
		    handle->parser_sql[end + 1U] == '"') {
			end += 2U;
			continue;
		}
		return end + 1U;
	}
	return start;
}

static sqlparser_status_t
sqlparser_patch_mysql_generated_name_source_span(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__ColumnRef *column_ref,
	size_t component_index,
	const char *current_name,
	const char *replacement_sql,
	size_t *out_start,
	size_t *out_end,
	char **out_owned_replacement,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const sqlparser_identifier_origin_map_t *origins;
	const char *alias_prefix;
	const char *qualifier;
	char *spelling;
	size_t alias_length;
	size_t candidate_count;
	size_t parser_end;
	size_t parser_start;
	size_t position;
	size_t qualifier_length;
	size_t replacement_length;
	size_t source_end;
	size_t source_start;
	sqlparser_identifier_origin_t origin;
	sqlparser_identifier_origin_t source_origin;
	sqlparser_status_t status;

	*out_supported = 0;
	*out_owned_replacement = NULL;
	if (!sqlparser_dialect_is_mysql_compatible(handle->dialect) ||
	    column_ref == NULL || column_ref->n_fields != 2U ||
	    component_index != 1U || column_ref->fields == NULL ||
	    column_ref->fields[0] == NULL ||
	    !sqlparser_node_string_value(column_ref->fields[0], &qualifier) ||
	    qualifier == NULL ||
	    (strcmp(qualifier, "EXCLUDED") != 0 &&
	     strcmp(qualifier, "sqlparser_mysql_alias_column") != 0) ||
	    current_name == NULL || current_name[0] == '\0' ||
	    replacement_sql == NULL || replacement_sql[0] == '\0' ||
	    column_ref->location < 0 ||
	    (size_t)column_ref->location >= handle->parser_sql_len) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_identifier_origins_for_handle(
		handle,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	parser_start = (size_t)column_ref->location;
	qualifier_length = strlen(qualifier);
	if (qualifier_length > handle->parser_sql_len - parser_start ||
	    memcmp(
		    handle->parser_sql + parser_start,
		    qualifier,
		    qualifier_length) != 0 ||
	    qualifier_length == handle->parser_sql_len - parser_start ||
	    handle->parser_sql[parser_start + qualifier_length] != '.' ||
	    sqlparser_identifier_origin_map_lookup(
		    origins,
		    parser_start,
		    qualifier_length,
		    &origin) != SQLPARSER_IDENTIFIER_ORIGIN_GENERATED) {
		return SQLPARSER_STATUS_OK;
	}
	parser_start += qualifier_length + 1U;
	parser_end = sqlparser_patch_parser_identifier_token_end(
		handle,
		parser_start);
	if (parser_end <= parser_start ||
	    sqlparser_identifier_origin_map_lookup(
		    origins,
		    parser_start,
		    parser_end - parser_start,
		    &source_origin) != SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
	    source_origin.source_offset >= handle->sql_len ||
	    source_origin.source_length >
		    handle->sql_len - source_origin.source_offset) {
		return SQLPARSER_STATUS_OK;
	}
	source_start = source_origin.source_offset;
	source_end = source_start + source_origin.source_length;
	if (sqlparser_patch_identifier_token_end(handle, source_start) !=
		    source_end ||
	    !sqlparser_patch_identifier_token_value_matches(
		    handle,
		    source_start,
		    source_end,
		    current_name,
		    strlen(current_name))) {
		return SQLPARSER_STATUS_OK;
	}
	spelling = NULL;
	status = sqlparser_resolve_identifier_component_spelling(
		handle,
		column_ref->location,
		component_index,
		current_name,
		&spelling,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (spelling == NULL || strlen(spelling) != source_end - source_start ||
	    memcmp(
		    handle->sql + source_start,
		    spelling,
		    source_end - source_start) != 0) {
		free(spelling);
		return SQLPARSER_STATUS_OK;
	}
	free(spelling);
	candidate_count = 0U;
	for (position = 0U;
	     position + qualifier_length + 1U < handle->parser_sql_len;
	     position++) {
		sqlparser_identifier_origin_t candidate_origin;
		size_t candidate_end;
		size_t candidate_suffix;

		if ((position > 0U &&
		     sqlparser_public_char_is_ident(
			     (unsigned char)handle->parser_sql[position - 1U])) ||
		    memcmp(
			    handle->parser_sql + position,
			    qualifier,
			    qualifier_length) != 0 ||
		    handle->parser_sql[position + qualifier_length] != '.' ||
		    sqlparser_identifier_origin_map_lookup(
			    origins,
			    position,
			    qualifier_length,
			    &origin) != SQLPARSER_IDENTIFIER_ORIGIN_GENERATED) {
			continue;
		}
		candidate_suffix = position + qualifier_length + 1U;
		candidate_end = sqlparser_patch_parser_identifier_token_end(
			handle,
			candidate_suffix);
		if (candidate_end <= candidate_suffix ||
		    sqlparser_identifier_origin_map_lookup(
			    origins,
			    candidate_suffix,
			    candidate_end - candidate_suffix,
			    &candidate_origin) !=
			    SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
		    candidate_origin.source_offset != source_start ||
		    candidate_origin.source_length != source_end - source_start) {
			continue;
		}
		candidate_count++;
		if (position != (size_t)column_ref->location ||
		    candidate_count > 1U) {
			return SQLPARSER_STATUS_OK;
		}
	}
	if (candidate_count != 1U ||
	    !sqlparser_mysql_on_duplicate_name_surface(
		    handle->dialect_state,
		    statement_index,
		    qualifier,
		    current_name,
		    handle->sql,
		    handle->sql_len,
		    source_start,
		    replacement_sql,
		    &alias_prefix)) {
		return SQLPARSER_STATUS_OK;
	}
	if (alias_prefix != NULL) {
		alias_length = strlen(alias_prefix);
		replacement_length = strlen(replacement_sql);
		if (replacement_length > SIZE_MAX - 2U ||
		    alias_length > SIZE_MAX - replacement_length - 2U) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"patch SQL is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		*out_owned_replacement =
			(char *)malloc(alias_length + replacement_length + 2U);
		if (*out_owned_replacement == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		memcpy(*out_owned_replacement, alias_prefix, alias_length);
		(*out_owned_replacement)[alias_length] = '.';
		memcpy(
			*out_owned_replacement + alias_length + 1U,
			replacement_sql,
			replacement_length + 1U);
	}
	*out_start = source_start;
	*out_end = source_end;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_name_source_span(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *replacement_sql,
	size_t *out_start,
	size_t *out_end,
	char **out_owned_replacement,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__ColumnRef *column_ref;
	PgQuery__Node *statement;
	char *spelling;
	sqlparser_name_search_t search;
	size_t component_index;
	size_t source_start;
	sqlparser_status_t status;

	*out_supported = 0;
	*out_owned_replacement = NULL;
	status = sqlparser_get_statement_node(
		handle,
		selector->statement_index,
		&statement,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = selector->item_index;
	status = sqlparser_walk_message_names(
		(ProtobufCMessage *)statement,
		NULL,
		&search);
	if (status != SQLPARSER_STATUS_OK || search.target_slot == NULL ||
	    search.target_field_owner == NULL ||
	    search.target_field_owner->descriptor !=
		    &pg_query__column_ref__descriptor) {
		return status;
	}
	column_ref = (PgQuery__ColumnRef *)search.target_field_owner;
	for (component_index = 0U;
	     component_index < column_ref->n_fields;
	     component_index++) {
		PgQuery__Node *field;

		field = column_ref->fields[component_index];
		if (field != NULL &&
		    field->node_case == PG_QUERY__NODE__NODE_STRING &&
		    field->string != NULL &&
		    &field->string->sval == search.target_slot) {
			break;
		}
	}
	if (component_index >= column_ref->n_fields) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_source_offset(
		handle,
		column_ref->location,
		&source_start,
		out_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (!*out_supported) {
		status = sqlparser_patch_mysql_quoted_column_source_span(
			handle,
			column_ref,
			out_start,
			out_end,
			out_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || *out_supported) {
			return status;
		}
		return sqlparser_patch_mysql_generated_name_source_span(
			handle,
			selector->statement_index,
			column_ref,
			component_index,
			*search.target_slot,
			replacement_sql,
			out_start,
			out_end,
			out_owned_replacement,
			out_supported,
			out_error);
	}
	if (!sqlparser_patch_identifier_component_span(
		    handle,
		    source_start,
		    component_index,
		    out_start,
		    out_end)) {
		*out_supported = 0;
		return SQLPARSER_STATUS_OK;
	}
	spelling = NULL;
	status = sqlparser_resolve_identifier_component_spelling(
		handle,
		column_ref->location,
		component_index,
		*search.target_slot,
		&spelling,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (spelling == NULL || strlen(spelling) != *out_end - *out_start ||
	    memcmp(
		    handle->sql + *out_start,
		    spelling,
		    *out_end - *out_start) != 0) {
		*out_supported = 0;
	}
	free(spelling);
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_source_string_is_adjacent(
	const sqlparser_handle_t *handle,
	size_t start)
{
	size_t end;

	start = sqlparser_public_skip_trivia(
		handle->dialect,
		handle->sql,
		start);
	if (start >= handle->sql_len) {
		return 0;
	}
	if (handle->sql[start] == '\'') {
		return 1;
	}
	end = sqlparser_public_skip_quoted_or_comment(
		handle->dialect,
		handle->sql,
		start);
	if (end > start &&
	    (handle->sql[start] == '$' ||
	     handle->sql[start] == 'q' || handle->sql[start] == 'Q' ||
	     handle->sql[start] == 'n' || handle->sql[start] == 'N')) {
		return 1;
	}
	if (start + 1U < handle->sql_len &&
	    (handle->sql[start + 1U] == '\'' ||
	     (handle->sql[start + 1U] == '&' &&
	      start + 2U < handle->sql_len &&
	      handle->sql[start + 2U] == '\'')) &&
	    (handle->sql[start] == 'b' || handle->sql[start] == 'B' ||
	     handle->sql[start] == 'e' || handle->sql[start] == 'E' ||
	     handle->sql[start] == 'n' || handle->sql[start] == 'N' ||
	     handle->sql[start] == 'q' || handle->sql[start] == 'Q' ||
	     handle->sql[start] == 'u' || handle->sql[start] == 'U' ||
	     handle->sql[start] == 'x' || handle->sql[start] == 'X')) {
		return 1;
	}
	return 0;
}

static int sqlparser_patch_standard_string_source_span(
	const sqlparser_handle_t *handle,
	const PgQuery__AConst *constant,
	size_t start,
	size_t *out_end)
{
	const char *value;
	size_t source;
	size_t value_index;

	if (start >= handle->sql_len || handle->sql[start] != '\'' ||
	    (start > 0U &&
	     (sqlparser_public_char_is_ident(
		     (unsigned char)handle->sql[start - 1U]) ||
	      handle->sql[start - 1U] == '&')) ||
	    constant->sval == NULL || constant->sval->sval == NULL) {
		return 0;
	}
	value = constant->sval->sval;
	value_index = 0U;
	for (source = start + 1U; source < handle->sql_len; source++) {
		if (handle->sql[source] == '\'') {
			if (source + 1U < handle->sql_len &&
			    handle->sql[source + 1U] == '\'') {
				if (value[value_index] != '\'') {
					return 0;
				}
				value_index++;
				source++;
				continue;
			}
			if (value[value_index] != '\0' ||
			    (source + 1U < handle->sql_len &&
			     sqlparser_public_char_is_ident(
				     (unsigned char)handle->sql[source + 1U])) ||
			    sqlparser_patch_source_string_is_adjacent(
				    handle,
				    source + 1U)) {
				return 0;
			}
			*out_end = source + 1U;
			return 1;
		}
		if (value[value_index] == '\0' ||
		    value[value_index] != handle->sql[source]) {
			return 0;
		}
		value_index++;
	}
	return 0;
}

static int sqlparser_patch_integer_source_span(
	const sqlparser_handle_t *handle,
	const PgQuery__AConst *constant,
	size_t start,
	size_t limit,
	size_t *out_end)
{
	uint64_t value;
	size_t end;

	if (start >= limit || limit > handle->sql_len ||
	    !isdigit((unsigned char)handle->sql[start]) ||
	    (start > 0U &&
	     (handle->sql[start - 1U] == '.' ||
	      sqlparser_public_char_is_ident(
		      (unsigned char)handle->sql[start - 1U]))) ||
	    constant->ival == NULL || constant->ival->ival < 0) {
		return 0;
	}
	value = 0U;
	end = start;
	while (end < limit &&
	       isdigit((unsigned char)handle->sql[end])) {
		uint64_t digit;

		digit = (uint64_t)(handle->sql[end] - '0');
		if (value > (UINT64_MAX - digit) / 10U) {
			return 0;
		}
		value = value * 10U + digit;
		end++;
	}
	if (end < handle->sql_len &&
	    (handle->sql[end] == '.' ||
	     sqlparser_public_char_is_ident(
		     (unsigned char)handle->sql[end]))) {
		return 0;
	}
	if (value != (uint64_t)constant->ival->ival) {
		return 0;
	}
	*out_end = end;
	return 1;
}

static int sqlparser_patch_null_token_length(
	const char *sql,
	size_t length,
	size_t start,
	size_t *out_length)
{
	static const char token[] = "null";
	size_t index;

	if (sql == NULL || out_length == NULL || start > length ||
	    sizeof(token) - 1U > length - start ||
	    (start > 0U &&
	     (sql[start - 1U] == '.' ||
	      sqlparser_public_char_is_ident(
		      (unsigned char)sql[start - 1U]))) ||
	    (start + sizeof(token) - 1U < length &&
	     (sql[start + sizeof(token) - 1U] == '.' ||
	      sqlparser_public_char_is_ident(
		      (unsigned char)sql[start + sizeof(token) - 1U])))) {
		return 0;
	}
	for (index = 0U; index < sizeof(token) - 1U; index++) {
		if (tolower((unsigned char)sql[start + index]) != token[index]) {
			return 0;
		}
	}
	*out_length = sizeof(token) - 1U;
	return 1;
}

static int sqlparser_patch_value_parser_token_length(
	const sqlparser_handle_t *handle,
	const PgQuery__AConst *constant,
	size_t *out_length)
{
	size_t end;
	size_t start;
	size_t token_length;
	int closed;

	if (constant->location < 0 ||
	    (size_t)constant->location >= handle->parser_sql_len) {
		return 0;
	}
	start = (size_t)constant->location;
	end = start;
	switch (constant->val_case) {
		case PG_QUERY__A__CONST__VAL_SVAL:
			if (handle->parser_sql[start] != '\'') {
				return 0;
			}
			closed = 0;
			for (end = start + 1U;
			     end < handle->parser_sql_len;
			     end++) {
				if (handle->parser_sql[end] != '\'') {
					continue;
				}
				if (end + 1U < handle->parser_sql_len &&
				    handle->parser_sql[end + 1U] == '\'') {
					end++;
					continue;
				}
				end++;
				closed = 1;
				break;
			}
			if (!closed) {
				return 0;
			}
			break;
		case PG_QUERY__A__CONST__VAL_IVAL:
			while (end < handle->parser_sql_len &&
			       isdigit((unsigned char)handle->parser_sql[end])) {
				end++;
			}
			break;
		case PG_QUERY__A__CONST__VAL__NOT_SET:
			if (!constant->isnull ||
			    !sqlparser_patch_null_token_length(
				    handle->parser_sql,
				    handle->parser_sql_len,
				    start,
				    &token_length)) {
				return 0;
			}
			end = start + token_length;
			break;
		default:
			return 0;
	}
	if (end <= start || end > handle->parser_sql_len) {
		return 0;
	}
	*out_length = end - start;
	return 1;
}

static int sqlparser_patch_param_ref_parser_token_length(
	const sqlparser_handle_t *handle,
	const PgQuery__ParamRef *param_ref,
	size_t *out_length)
{
	uint64_t number;
	size_t end;
	size_t start;

	if (param_ref == NULL || param_ref->location < 0 ||
	    param_ref->number <= 0 ||
	    (size_t)param_ref->location >= handle->parser_sql_len) {
		return 0;
	}
	start = (size_t)param_ref->location;
	if (handle->parser_sql[start] != '$' ||
	    start + 1U >= handle->parser_sql_len ||
	    !isdigit((unsigned char)handle->parser_sql[start + 1U])) {
		return 0;
	}
	number = 0U;
	end = start + 1U;
	while (end < handle->parser_sql_len &&
	       isdigit((unsigned char)handle->parser_sql[end])) {
		uint64_t digit;

		digit = (uint64_t)(handle->parser_sql[end] - '0');
		if (number > (UINT64_MAX - digit) / 10U) {
			return 0;
		}
		number = number * 10U + digit;
		end++;
	}
	if (number != (uint64_t)param_ref->number ||
	    (end < handle->parser_sql_len &&
	     sqlparser_public_char_is_ident(
		     (unsigned char)handle->parser_sql[end]))) {
		return 0;
	}
	*out_length = end - start;
	return 1;
}

static sqlparser_status_t sqlparser_patch_source_origin_span(
	sqlparser_handle_t *handle,
	int32_t parser_location,
	size_t parser_length,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_identifier_origin_t origin;
	sqlparser_status_t status;

	*out_supported = 0;
	if (parser_location < 0 || parser_length == 0U ||
	    (size_t)parser_location > handle->parser_sql_len ||
	    parser_length >
		    handle->parser_sql_len - (size_t)parser_location) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_identifier_origins_for_handle(
		handle,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (sqlparser_identifier_origin_map_lookup(
		    origins,
		    (size_t)parser_location,
		    parser_length,
		    &origin) != SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
	    origin.source_length == 0U ||
	    origin.source_offset > handle->sql_len ||
	    origin.source_length > handle->sql_len - origin.source_offset) {
		return SQLPARSER_STATUS_OK;
	}
	*out_start = origin.source_offset;
	*out_end = origin.source_offset + origin.source_length;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_bind_token(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	if (sql == NULL || start >= end) {
		return 0;
	}
	if (sqlparser_dialect_is_mysql_compatible(dialect)) {
		return end == start + 1U && sql[start] == '?';
	}
	if (sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		if (end == start + 1U && sql[start] == '?') {
			return 1;
		}
		if (sql[start] != '@' || start + 1U >= end ||
		    sql[start + 1U] == '@' ||
		    (!sqlparser_sqlserver_is_ident_start(
			    (unsigned char)sql[start + 1U]) &&
		     !isdigit((unsigned char)sql[start + 1U]))) {
			return 0;
		}
		pos = start + 2U;
		while (pos < end &&
		       sqlparser_sqlserver_is_ident_char(
			       (unsigned char)sql[pos])) {
			pos++;
		}
		return pos == end;
	}
	if (dialect == SQLPARSER_DIALECT_POSTGRESQL ||
	    dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL) {
		pos = start + 1U;
		if (sql[start] != '$' || pos >= end ||
		    !isdigit((unsigned char)sql[pos])) {
			return 0;
		}
		while (pos < end && isdigit((unsigned char)sql[pos])) {
			pos++;
		}
		return pos == end;
	}
	if (!sqlparser_dialect_is_oracle_or_dameng_compatible(
		    dialect)) {
		return 0;
	}
	if (end == start + 1U && sql[start] == '?') {
		return 1;
	}
	if (sql[start] != ':' || start + 1U >= end) {
		return 0;
	}
	pos = start + 1U;
	if (isdigit((unsigned char)sql[pos])) {
		while (pos < end && isdigit((unsigned char)sql[pos])) {
			pos++;
		}
		return pos == end;
	}
	if (!isalpha((unsigned char)sql[pos]) && sql[pos] != '_') {
		return 0;
	}
	for (;;) {
		pos++;
		while (pos < end &&
		       (isalnum((unsigned char)sql[pos]) || sql[pos] == '_' ||
			sql[pos] == '$' || sql[pos] == '#')) {
			pos++;
		}
		if (pos == end) {
			return 1;
		}
		if (sql[pos] != '.' || pos + 1U >= end ||
		    (!isalpha((unsigned char)sql[pos + 1U]) &&
		     sql[pos + 1U] != '_')) {
			return 0;
		}
		pos++;
	}
}

static int sqlparser_patch_bind_source_span(
	const sqlparser_handle_t *handle,
	size_t start,
	size_t end)
{
	if (start >= end || end > handle->sql_len ||
	    (start > 0U &&
	     (handle->sql[start - 1U] == '.' ||
	      (sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	       handle->sql[start - 1U] == '@') ||
	      sqlparser_public_char_is_ident(
		      (unsigned char)handle->sql[start - 1U]))) ||
	    (end < handle->sql_len &&
	     (handle->sql[end] == '.' ||
	      (sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	       handle->sql[end] == '@') ||
	      sqlparser_public_char_is_ident(
		      (unsigned char)handle->sql[end])))) {
		return 0;
	}
	return sqlparser_patch_bind_token(
		handle->dialect,
		handle->sql,
		start,
		end);
}

static int sqlparser_patch_bind_sql(
	const sqlparser_handle_t *handle,
	const char *sql)
{
	return handle != NULL && sql != NULL &&
		sqlparser_patch_bind_token(
			handle->dialect,
			sql,
			0U,
			strlen(sql));
}

static int sqlparser_patch_unsigned_integer_sql(const char *sql)
{
	size_t index;

	if (sql == NULL || !isdigit((unsigned char)sql[0])) {
		return 0;
	}
	for (index = 1U; sql[index] != '\0'; index++) {
		if (!isdigit((unsigned char)sql[index])) {
			return 0;
		}
	}
	return 1;
}

static sqlparser_status_t sqlparser_patch_value_source_span(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__AConst *constant;
	PgQuery__Node *node;
	PgQuery__Node **slot;
	ProtobufCMessage *parent;
	size_t origin_end;
	size_t parser_length;
	sqlparser_status_t status;
	int origin_supported;

	*out_supported = 0;
	slot = NULL;
	parent = NULL;
	status = sqlparser_get_statement_node_slot_by_index(
		handle,
		selector->statement_index,
		selector->item_index,
		&slot,
		&parent,
		out_error);
	if (status != SQLPARSER_STATUS_OK || slot == NULL || *slot == NULL ||
	    parent == NULL) {
		return status;
	}
	node = *slot;
	if (node->node_case == PG_QUERY__NODE__NODE_PARAM_REF &&
	    node->param_ref != NULL) {
		if (!sqlparser_patch_param_ref_parser_token_length(
			    handle,
			    node->param_ref,
			    &parser_length)) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_patch_source_origin_span(
			handle,
			node->param_ref->location,
			parser_length,
			out_start,
			&origin_end,
			&origin_supported,
			out_error);
		if (status == SQLPARSER_STATUS_OK && origin_supported &&
		    sqlparser_patch_bind_source_span(
			    handle, *out_start, origin_end)) {
			*out_end = origin_end;
			*out_supported = 1;
		}
		return status;
	}
	if (parent->descriptor == &pg_query__type_cast__descriptor ||
	    node->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    node->a_const == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	constant = node->a_const;
	if (!sqlparser_patch_value_parser_token_length(
		    handle,
		    constant,
		    &parser_length)) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_source_origin_span(
		handle,
		constant->location,
		parser_length,
		out_start,
		&origin_end,
		&origin_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !origin_supported) {
		return status;
	}
	switch (constant->val_case) {
		case PG_QUERY__A__CONST__VAL_SVAL:
			*out_supported = sqlparser_patch_standard_string_source_span(
				handle,
				constant,
				*out_start,
				out_end);
			break;
		case PG_QUERY__A__CONST__VAL_IVAL:
			*out_supported = sqlparser_patch_integer_source_span(
				handle,
				constant,
				*out_start,
				origin_end,
				out_end);
			break;
		case PG_QUERY__A__CONST__VAL__NOT_SET:
			if (constant->isnull &&
			    sqlparser_patch_null_token_length(
				    handle->sql,
				    handle->sql_len,
				    *out_start,
				    &parser_length)) {
				*out_end = *out_start + parser_length;
				*out_supported = 1;
			}
			break;
		default:
			break;
	}
	if (*out_supported && *out_end != origin_end) {
		*out_supported = 0;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_source_word_at(
	const char *sql,
	size_t length,
	size_t pos,
	const char *word)
{
	size_t index;
	size_t word_length;

	word_length = strlen(word);
	if (word_length > length - pos ||
	    (pos > 0U &&
	     sqlparser_public_char_is_ident((unsigned char)sql[pos - 1U])) ||
	    (pos + word_length < length &&
	     sqlparser_public_char_is_ident(
		     (unsigned char)sql[pos + word_length]))) {
		return 0;
	}
	for (index = 0U; index < word_length; index++) {
		if (tolower((unsigned char)sql[pos + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static size_t sqlparser_patch_sqlserver_odbc_target_start(
	const sqlparser_handle_t *handle,
	size_t start)
{
	size_t candidate;
	size_t expression_start;
	size_t skipped;

	if (handle == NULL || handle->sql == NULL ||
	    !sqlparser_dialect_is_sqlserver_compatible(handle->dialect) ||
	    start > handle->sql_len) {
		return start;
	}
	candidate = 0U;
	while (candidate < start) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			candidate);
		if (skipped != candidate) {
			if (skipped > start) {
				return start;
			}
			candidate = skipped;
			continue;
		}
		if (handle->sql[candidate] != '{') {
			candidate++;
			continue;
		}
		expression_start = sqlparser_public_skip_space(
			handle->sql,
			candidate + 1U);
		if (expression_start >= start ||
		    !sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    expression_start,
			    "fn")) {
			candidate++;
			continue;
		}
		expression_start = sqlparser_public_skip_trivia(
			handle->dialect,
			handle->sql,
			expression_start + 2U);
		if (expression_start == start) {
			return candidate;
		}
		candidate++;
	}
	return start;
}

static int sqlparser_patch_comment_starts_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t pos);

static sqlparser_status_t sqlparser_patch_target_source_span(
	sqlparser_handle_t *handle,
	const PgQuery__Node *target,
	const char *boundary_word,
	int require_boundary,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const PgQuery__ResTarget *res_target;
	size_t bracket_depth;
	size_t depth;
	size_t end;
	size_t origin_end;
	size_t parser_length;
	size_t pos;
	size_t skipped;
	sqlparser_status_t status;
	int origin_supported;
	int supported;
	int stopped_at_boundary;

	*out_supported = 0;
	if (target == NULL ||
	    target->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    target->res_target == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	res_target = target->res_target;
	if (res_target->val != NULL &&
	    res_target->val->node_case == PG_QUERY__NODE__NODE_PARAM_REF &&
	    res_target->val->param_ref != NULL &&
	    res_target->location == res_target->val->param_ref->location) {
		if (!sqlparser_patch_param_ref_parser_token_length(
			    handle,
			    res_target->val->param_ref,
			    &parser_length)) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_patch_source_origin_span(
			handle,
			res_target->location,
			parser_length,
			out_start,
			&origin_end,
			&origin_supported,
			out_error);
		supported = origin_supported &&
			sqlparser_patch_bind_source_span(
				handle, *out_start, origin_end);
	} else {
		status = sqlparser_patch_source_offset(
			handle,
			res_target->location,
			out_start,
			&supported,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK || !supported) {
		return status;
	}
	*out_start = sqlparser_patch_sqlserver_odbc_target_start(
		handle,
		*out_start);
	if (*out_start > 0U &&
	    ((handle->sql[*out_start] == '\'' &&
	      sqlparser_public_char_is_ident(
		      (unsigned char)handle->sql[*out_start - 1U])) ||
	     (handle->sql[*out_start] == '"' && *out_start > 1U &&
	      handle->sql[*out_start - 1U] == '&' &&
	      (handle->sql[*out_start - 2U] == 'U' ||
	       handle->sql[*out_start - 2U] == 'u')))) {
		return SQLPARSER_STATUS_OK;
	}
	bracket_depth = 0U;
	depth = 0U;
	end = *out_start;
	stopped_at_boundary = 0;
	for (pos = *out_start; pos < handle->sql_len; pos++) {
		if (sqlparser_patch_comment_starts_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			return SQLPARSER_STATUS_OK;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			pos = skipped - 1U;
			end = skipped;
			continue;
		}
		if (depth == 0U && bracket_depth == 0U &&
		    (handle->sql[pos] == ',' || handle->sql[pos] == ';' ||
		     handle->sql[pos] == ')')) {
			break;
		}
		if (boundary_word != NULL && pos > *out_start &&
		    handle->sql[pos - 1U] != '.' &&
		    handle->sql[pos - 1U] != '@' &&
		    handle->sql[pos - 1U] != ':' &&
		    handle->sql[pos - 1U] != '$' &&
		    handle->sql[pos - 1U] != '#' &&
		    depth == 0U &&
		    bracket_depth == 0U &&
		    sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    boundary_word)) {
			stopped_at_boundary = 1;
			break;
		}
		if (handle->sql[pos] == '(') {
			depth++;
		} else if (handle->sql[pos] == ')') {
			if (depth == 0U && bracket_depth == 0U) {
				break;
			}
			if (depth > 0U) {
				depth--;
			}
		} else if (handle->sql[pos] == '[') {
			bracket_depth++;
		} else if (handle->sql[pos] == ']') {
			if (bracket_depth == 0U) {
				return SQLPARSER_STATUS_OK;
			}
			bracket_depth--;
		}
		end = pos + 1U;
	}
	while (end > *out_start &&
	       isspace((unsigned char)handle->sql[end - 1U])) {
		end--;
	}
	if (end <= *out_start || depth != 0U || bracket_depth != 0U ||
	    (require_boundary && !stopped_at_boundary)) {
		return SQLPARSER_STATUS_OK;
	}
	*out_end = end;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_target_gap_is_separator(
	const char *sql,
	size_t start,
	size_t end)
{
	int comma_seen;

	comma_seen = 0;
	while (start < end) {
		if (isspace((unsigned char)sql[start])) {
			start++;
			continue;
		}
		if (!comma_seen && sql[start] == ',') {
			comma_seen = 1;
			start++;
			continue;
		}
		return 0;
	}
	return comma_seen;
}

static sqlparser_status_t sqlparser_patch_select_target_insert_source_offset(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__SelectStmt *stmt,
	size_t target_index,
	size_t *out_offset,
	int *out_supported,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_patch_select_target_span(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t target_index,
	PgQuery__SelectStmt **out_stmt,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	size_t terminal_offset;
	sqlparser_status_t status;
	int terminal_supported;
	int terminal_without_from;
	int target_supported;

	*out_supported = 0;
	stmt = NULL;
	status = sqlparser_get_select_stmt_by_target_list_index(
		handle,
		selector->statement_index,
		selector->item_index,
		&stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (target_index >= stmt->n_target_list || stmt->target_list == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (stmt->into_clause != NULL ||
	    (stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	     stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE)) {
		return SQLPARSER_STATUS_OK;
	}
	terminal_without_from = target_index + 1U == stmt->n_target_list &&
		stmt->n_from_clause == 0U;
	if (terminal_without_from &&
	    !sqlparser_dialect_is_sqlserver_compatible(handle->dialect)) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_target_source_span(
		handle,
		stmt->target_list[target_index],
		"from",
		target_index + 1U == stmt->n_target_list &&
			stmt->n_from_clause != 0U,
		out_start,
		out_end,
		&target_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (!target_supported) {
		const PgQuery__Node *target_node;
		const PgQuery__ResTarget *target;

		target_node = stmt->target_list[target_index];
		if (target_node == NULL ||
		    target_node->node_case !=
			    PG_QUERY__NODE__NODE_RES_TARGET) {
			return SQLPARSER_STATUS_OK;
		}
		target = target_node->res_target;
		if (target == NULL ||
		    (target->name != NULL && target->name[0] != '\0') ||
		    target->val == NULL ||
		    target->val->node_case !=
			    PG_QUERY__NODE__NODE_COLUMN_REF) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_patch_mysql_quoted_column_source_span(
			handle,
			target->val->column_ref,
			out_start,
			out_end,
			&target_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !target_supported) {
			return status;
		}
	}
	if (target_index > 0U) {
		size_t previous_end;
		size_t previous_start;
		int previous_supported;

		status = sqlparser_patch_target_source_span(
			handle,
			stmt->target_list[target_index - 1U],
			"from",
			0,
			&previous_start,
			&previous_end,
			&previous_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (!previous_supported || previous_end > *out_start ||
		    !sqlparser_patch_target_gap_is_separator(
			    handle->sql,
			    previous_end,
			    *out_start)) {
			return SQLPARSER_STATUS_OK;
		}
	}
	if (target_index + 1U < stmt->n_target_list) {
		size_t next_end;
		size_t next_start;
		int next_supported;

		status = sqlparser_patch_target_source_span(
			handle,
			stmt->target_list[target_index + 1U],
			"from",
			target_index + 2U == stmt->n_target_list &&
				stmt->n_from_clause != 0U,
			&next_start,
			&next_end,
			&next_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (!next_supported || *out_end > next_start ||
		    !sqlparser_patch_target_gap_is_separator(
			    handle->sql,
			    *out_end,
			    next_start)) {
			size_t ordinal_start;
			int ordinal_supported;

			status =
				sqlparser_patch_select_target_insert_source_offset(
					handle,
					selector->statement_index,
					stmt,
					target_index,
					&ordinal_start,
					&ordinal_supported,
					out_error);
			if (status != SQLPARSER_STATUS_OK ||
			    !ordinal_supported || ordinal_start != *out_start) {
				return status;
			}
		}
	}
	if (terminal_without_from) {
		status = sqlparser_patch_select_target_insert_source_offset(
			handle,
			selector->statement_index,
			stmt,
			stmt->n_target_list,
			&terminal_offset,
			&terminal_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !terminal_supported ||
		    terminal_offset < *out_start ||
		    terminal_offset > *out_end) {
			return status;
		}
		*out_end = terminal_offset;
	}
	if (out_stmt != NULL) {
		*out_stmt = stmt;
	}
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_dml_result_target_list(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	PgQuery__Node ***out_targets,
	size_t *out_target_offset,
	size_t *out_target_count,
	const char **out_boundary_word,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	PgQuery__InsertStmt *insert;
	PgQuery__UpdateStmt *update;
	sqlparser_dialect_dml_result_channel_t channel;
	sqlparser_graph_dml_kind_t kind;
	sqlparser_status_t status;

	*out_targets = NULL;
	*out_target_offset = 0U;
	*out_target_count = 0U;
	*out_boundary_word = NULL;
	if (handle == NULL || selector == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_dialect_dml_result_channel_at(
		    handle,
		    selector->statement_index,
		    selector->item_index,
		    selector->row_index,
		    &channel) ||
	    (channel.kind != SQLPARSER_GRAPH_DML_RESULT_CLIENT &&
	     channel.kind != SQLPARSER_GRAPH_DML_RESULT_SINK)) {
		return SQLPARSER_STATUS_OK;
	}
	message = NULL;
	kind = (sqlparser_graph_dml_kind_t)0;
	status = sqlparser_get_dml_result_message(
		handle,
		selector->statement_index,
		selector->item_index,
		&kind,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK || message == NULL) {
		return status;
	}
	if (kind == SQLPARSER_GRAPH_DML_INSERT &&
	    message->descriptor == &pg_query__insert_stmt__descriptor) {
		insert = (PgQuery__InsertStmt *)message;
		*out_targets = insert->returning_list;
		*out_target_count = insert->n_returning_list;
		*out_boundary_word =
			channel.kind == SQLPARSER_GRAPH_DML_RESULT_SINK ?
				"into" : "values";
	} else if (kind == SQLPARSER_GRAPH_DML_UPDATE &&
		   message->descriptor == &pg_query__update_stmt__descriptor) {
		update = (PgQuery__UpdateStmt *)message;
		*out_targets = update->returning_list;
		*out_target_count = update->n_returning_list;
		if (update->n_from_clause != 0U) {
			*out_boundary_word = "from";
		} else if (update->where_clause != NULL) {
			*out_boundary_word = "where";
		}
	} else {
		return SQLPARSER_STATUS_OK;
	}
	if (*out_boundary_word == NULL || channel.target_count == 0U ||
	    *out_targets == NULL ||
	    channel.target_offset > *out_target_count ||
	    channel.target_count >
		    *out_target_count - channel.target_offset) {
		*out_targets = NULL;
		*out_target_count = 0U;
		return SQLPARSER_STATUS_OK;
	}
	*out_target_offset = channel.target_offset;
	*out_target_count = channel.target_count;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_dml_result_target_span(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t target_index,
	size_t *out_target_count,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **targets;
	size_t absolute_index;
	size_t target_count;
	size_t target_offset;
	const char *boundary_word;
	sqlparser_status_t status;
	int target_supported;

	*out_supported = 0;
	*out_target_count = 0U;
	boundary_word = NULL;
	status = sqlparser_patch_dml_result_target_list(
		handle,
		selector,
		&targets,
		&target_offset,
		&target_count,
		&boundary_word,
		out_error);
	if (status != SQLPARSER_STATUS_OK || targets == NULL ||
	    target_index >= target_count) {
		return status;
	}
	absolute_index = target_offset + target_index;
	status = sqlparser_patch_target_source_span(
		handle,
		targets[absolute_index],
		boundary_word,
		target_index + 1U == target_count,
		out_start,
		out_end,
		&target_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !target_supported) {
		return status;
	}
	if (target_index > 0U) {
		size_t previous_end;
		size_t previous_start;
		int previous_supported;

		status = sqlparser_patch_target_source_span(
			handle,
			targets[absolute_index - 1U],
			boundary_word,
			0,
			&previous_start,
			&previous_end,
			&previous_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !previous_supported ||
		    previous_end > *out_start ||
		    !sqlparser_patch_target_gap_is_separator(
			    handle->sql,
			    previous_end,
			    *out_start)) {
			return status;
		}
	}
	if (target_index + 1U < target_count) {
		size_t next_end;
		size_t next_start;
		int next_supported;

		status = sqlparser_patch_target_source_span(
			handle,
			targets[absolute_index + 1U],
			boundary_word,
			target_index + 2U == target_count,
			&next_start,
			&next_end,
			&next_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !next_supported ||
		    *out_end > next_start ||
		    !sqlparser_patch_target_gap_is_separator(
			    handle->sql,
			    *out_end,
			    next_start)) {
			return status;
		}
	}
	*out_target_count = target_count;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_comment_starts_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t pos)
{
	return sqlparser_public_comment_at(dialect, sql, pos);
}

static int sqlparser_patch_assignment_target_path_matches(
	const sqlparser_handle_t *handle,
	size_t start,
	size_t end,
	size_t anchor)
{
	size_t component_end;
	size_t component_start;
	size_t pos;
	int anchor_seen;

	pos = start;
	anchor_seen = 0;
	for (;;) {
		component_start = pos;
		component_end = sqlparser_patch_identifier_token_end(
			handle,
			component_start);
		if (component_end <= component_start || component_end > end) {
			return 0;
		}
		if (anchor == component_start) {
			anchor_seen = 1;
		}
		pos = component_end;
		while (pos < end && isspace((unsigned char)handle->sql[pos])) {
			pos++;
		}
		if (pos == end) {
			return anchor_seen;
		}
		if (handle->sql[pos] != '.') {
			return 0;
		}
		pos++;
		while (pos < end && isspace((unsigned char)handle->sql[pos])) {
			pos++;
		}
		if (pos == end) {
			return 0;
		}
	}
}

static int sqlparser_patch_assignment_clause_at(
	const char *sql,
	size_t length,
	size_t expression_start,
	size_t pos)
{
	size_t keyword_length;
	size_t next;
	size_t previous;
	char previous_char;

	if (sqlparser_patch_source_word_at(sql, length, pos, "where") ||
	    sqlparser_patch_source_word_at(sql, length, pos, "order") ||
	    sqlparser_patch_source_word_at(sql, length, pos, "limit")) {
		keyword_length = 5U;
	} else if (sqlparser_patch_source_word_at(
			   sql,
			   length,
			   pos,
			   "from")) {
		keyword_length = 4U;
	} else if (sqlparser_patch_source_word_at(
			   sql,
			   length,
			   pos,
			   "returning")) {
		keyword_length = 9U;
	} else {
		return 0;
	}
	next = pos + keyword_length;
	while (next < length && isspace((unsigned char)sql[next])) {
		next++;
	}
	if (next < length && sql[next] == '.') {
		return 0;
	}
	previous = pos;
	while (previous > expression_start &&
	       isspace((unsigned char)sql[previous - 1U])) {
		previous--;
	}
	if (previous <= expression_start) {
		return 0;
	}
	previous_char = sql[previous - 1U];
	return previous_char != '.' && previous_char != '+' &&
		previous_char != '-' && previous_char != '*' &&
		previous_char != '/' && previous_char != '%' &&
		previous_char != '<' && previous_char != '>' &&
		previous_char != '=' && previous_char != '!' &&
		previous_char != '|' && previous_char != '&' &&
		previous_char != '^' && previous_char != '~' &&
		previous_char != ',' && previous_char != '(';
}

static int sqlparser_patch_merge_when_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t length,
	size_t pos)
{
	if (!sqlparser_patch_source_word_at(
		    sql, length, pos, "when")) {
		return 0;
	}
	pos = sqlparser_public_skip_trivia(dialect, sql, pos + 4U);
	if (pos >= length) {
		return 0;
	}
	if (sqlparser_patch_source_word_at(
		    sql, length, pos, "matched")) {
		pos += 7U;
	} else if (sqlparser_patch_source_word_at(
			   sql, length, pos, "not")) {
		pos = sqlparser_public_skip_trivia(dialect, sql, pos + 3U);
		if (pos >= length ||
		    !sqlparser_patch_source_word_at(
			    sql, length, pos, "matched")) {
			return 0;
		}
		pos += 7U;
	} else {
		return 0;
	}
	pos = sqlparser_public_skip_trivia(dialect, sql, pos);
	if (pos < length &&
	    sqlparser_patch_source_word_at(sql, length, pos, "by")) {
		pos = sqlparser_public_skip_trivia(dialect, sql, pos + 2U);
		if (pos >= length ||
		    (!sqlparser_patch_source_word_at(
			     sql, length, pos, "target") &&
		     !sqlparser_patch_source_word_at(
			     sql, length, pos, "source"))) {
			return 0;
		}
	}
	return 1;
}

static sqlparser_status_t sqlparser_patch_assignment_source_span(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeStmt *merge_stmt;
	PgQuery__MergeWhenClause *when_clause;
	PgQuery__Node *target_node;
	PgQuery__Node *returning_node;
	PgQuery__Node *when_node;
	PgQuery__Node **target_list;
	PgQuery__ResTarget *target;
	PgQuery__UpdateStmt *stmt;
	size_t anchor;
	size_t assignment_index;
	size_t cell_index;
	size_t depth;
	size_t equals;
	size_t index;
	size_t output_boundary;
	size_t pos;
	size_t returning_start;
	size_t set_end;
	size_t skipped;
	size_t scan_end;
	size_t scan_start;
	size_t source_end;
	size_t source_start;
	size_t target_count;
	size_t update_action_count;
	sqlparser_status_t status;
	int boundary_is_comma;
	int boundary_is_merge_when;
	int merge_assignment;
	int merge_when_boundary_required;
	int returning_supported;
	int source_supported;
	int update_seen;

	*out_supported = 0;
	if (handle == NULL || selector == NULL || out_start == NULL ||
	    out_end == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	merge_assignment =
		selector->kind == SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT;
	if ((!merge_assignment &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT) ||
	    (!merge_assignment &&
	     !sqlparser_dialect_is_mysql_compatible(handle->dialect) &&
	     !sqlparser_dialect_is_oracle_compatible(handle->dialect) &&
	     !sqlparser_dialect_is_sqlserver_compatible(handle->dialect)) ||
	    (merge_assignment &&
	     !sqlparser_dialect_is_oracle_or_dameng_compatible(
		     handle->dialect) &&
	     !sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	     handle->dialect != SQLPARSER_DIALECT_POSTGRESQL &&
	     handle->dialect != SQLPARSER_DIALECT_VASTBASE_POSTGRESQL)) {
		return SQLPARSER_STATUS_OK;
	}
	target_list = NULL;
	target_count = 0U;
	assignment_index = 0U;
	merge_when_boundary_required = 0;
	stmt = NULL;
	merge_stmt = NULL;
	when_clause = NULL;
	if (!merge_assignment) {
		status = sqlparser_get_update_stmt(
			handle,
			selector->statement_index,
			&stmt,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (stmt == NULL) {
			return SQLPARSER_STATUS_OK;
		}
		target_list = stmt->target_list;
		target_count = stmt->n_target_list;
		assignment_index = selector->item_index;
	} else {
		status = sqlparser_get_merge_stmt_by_dml_index(
			handle,
			selector->statement_index,
			selector->row_index,
			&merge_stmt,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (merge_stmt == NULL || merge_stmt->merge_when_clauses == NULL ||
		    selector->item_index >= merge_stmt->n_merge_when_clauses) {
			return SQLPARSER_STATUS_OK;
		}
		merge_when_boundary_required =
			selector->item_index + 1U <
			merge_stmt->n_merge_when_clauses;
		update_action_count = 0U;
		for (index = 0U;
		     index < merge_stmt->n_merge_when_clauses;
		     index++) {
			when_node = merge_stmt->merge_when_clauses[index];
			if (when_node != NULL &&
			    when_node->node_case ==
				    PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE &&
			    when_node->merge_when_clause != NULL &&
			    when_node->merge_when_clause->command_type ==
				    PG_QUERY__CMD_TYPE__CMD_UPDATE) {
				update_action_count++;
			}
		}
		when_node = merge_stmt->merge_when_clauses[selector->item_index];
		if (update_action_count != 1U || when_node == NULL ||
		    when_node->node_case !=
			    PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ||
		    (when_clause = when_node->merge_when_clause) == NULL ||
		    when_clause->command_type != PG_QUERY__CMD_TYPE__CMD_UPDATE) {
			return SQLPARSER_STATUS_OK;
		}
		target_list = when_clause->target_list;
		target_count = when_clause->n_target_list;
		assignment_index = selector->column_index;
	}
	if (target_list == NULL || assignment_index >= target_count) {
		return SQLPARSER_STATUS_OK;
	}
	target_node = target_list[assignment_index];
	if (target_node == NULL ||
	    target_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    target_node->res_target == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	target = target_node->res_target;
	if (merge_assignment && target->val == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_source_offset(
		handle,
		target->location,
		&anchor,
		&source_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !source_supported) {
		return status;
	}
	if (anchor >= handle->sql_len) {
		return SQLPARSER_STATUS_OK;
	}
	scan_start = 0U;
	scan_end = handle->sql_len;
	if (handle->control != NULL) {
		const sqlparser_control_unit_t *unit;

		if (selector->statement_index >= handle->control->unit_count) {
			return SQLPARSER_STATUS_OK;
		}
		unit = &handle->control->units[selector->statement_index];
		if (unit->source_offset > handle->sql_len ||
		    unit->source_length >
			    handle->sql_len - unit->source_offset) {
			return SQLPARSER_STATUS_OK;
		}
		scan_start = unit->source_offset;
		scan_end = scan_start + unit->source_length;
		if (anchor < scan_start || anchor >= scan_end) {
			return SQLPARSER_STATUS_OK;
		}
	}
	output_boundary = SIZE_MAX;
	if (!merge_assignment &&
	    sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	    stmt->n_returning_list != 0U && stmt->returning_list != NULL &&
	    (returning_node = stmt->returning_list[0]) != NULL &&
	    returning_node->node_case == PG_QUERY__NODE__NODE_RES_TARGET &&
	    returning_node->res_target != NULL &&
	    returning_node->res_target->location >= 0) {
		status = sqlparser_patch_source_offset(
			handle,
			returning_node->res_target->location,
			&returning_start,
			&returning_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (returning_supported && returning_start > scan_start &&
		    returning_start < scan_end) {
			for (pos = scan_start; pos < returning_start;) {
				skipped = sqlparser_public_skip_quoted_or_comment(
					handle->dialect,
					handle->sql,
					pos);
				if (skipped != pos) {
					if (skipped > returning_start) {
						break;
					}
					pos = skipped;
					continue;
				}
				if (sqlparser_patch_source_word_at(
					    handle->sql,
					    handle->sql_len,
					    pos,
					    "output") &&
				    sqlparser_public_skip_trivia(
					    handle->dialect,
					    handle->sql,
					    pos + 6U) == returning_start) {
					output_boundary = pos;
					break;
				}
				pos++;
			}
		}
	}

	depth = 0U;
	set_end = SIZE_MAX;
	update_seen = 0;
	for (pos = scan_start; pos < anchor;) {
		if (sqlparser_patch_comment_starts_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				handle->sql,
				pos);
			if (skipped <= pos || skipped > anchor) {
				return SQLPARSER_STATUS_OK;
			}
			pos = skipped;
			continue;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > anchor) {
				return SQLPARSER_STATUS_OK;
			}
			pos = skipped;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				return SQLPARSER_STATUS_OK;
			}
			depth--;
			pos++;
			continue;
		}
		if (depth == 0U && handle->sql[pos] == ';') {
			set_end = SIZE_MAX;
			update_seen = 0;
			pos++;
			continue;
		}
		if (depth == 0U && !update_seen && set_end == SIZE_MAX &&
		    sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "update")) {
			update_seen = 1;
			pos += 6U;
			continue;
		}
		if (depth == 0U && update_seen && set_end == SIZE_MAX &&
		    sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "set")) {
			set_end = pos + 3U;
			pos += 3U;
			continue;
		}
		pos++;
	}
	if (set_end == SIZE_MAX || set_end > anchor) {
		return SQLPARSER_STATUS_OK;
	}

	cell_index = 0U;
	depth = 0U;
	source_start = set_end;
	while (source_start < anchor &&
	       isspace((unsigned char)handle->sql[source_start])) {
		source_start++;
	}
	for (pos = source_start; pos < anchor;) {
		if (sqlparser_patch_comment_starts_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			return SQLPARSER_STATUS_OK;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > anchor) {
				return SQLPARSER_STATUS_OK;
			}
			pos = skipped;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
		} else if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				return SQLPARSER_STATUS_OK;
			}
			depth--;
		} else if (depth == 0U && handle->sql[pos] == ',') {
			cell_index++;
			source_start = pos + 1U;
			while (source_start < anchor &&
			       isspace((unsigned char)handle->sql[source_start])) {
				source_start++;
			}
		}
		pos++;
	}
	if (depth != 0U || cell_index != assignment_index ||
	    source_start > anchor) {
		return SQLPARSER_STATUS_OK;
	}

	depth = 0U;
	equals = SIZE_MAX;
	source_end = scan_end;
	boundary_is_comma = 0;
	boundary_is_merge_when = 0;
	for (pos = source_start; pos < scan_end;) {
		if (sqlparser_patch_comment_starts_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			return SQLPARSER_STATUS_OK;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > scan_end) {
				return SQLPARSER_STATUS_OK;
			}
			pos = skipped;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				source_end = pos;
				break;
			}
			depth--;
			pos++;
			continue;
		}
		if (depth == 0U && equals == SIZE_MAX &&
		    handle->sql[pos] == '=') {
			equals = pos;
			pos++;
			continue;
		}
		if (depth == 0U && equals != SIZE_MAX) {
			if (pos == output_boundary) {
				source_end = pos;
				break;
			}
			if (handle->sql[pos] == ',') {
				source_end = pos;
				boundary_is_comma = 1;
				break;
			}
			if (merge_assignment &&
			    sqlparser_patch_merge_when_at(
				    handle->dialect,
				    handle->sql,
				    scan_end,
				    pos)) {
				if (!merge_when_boundary_required) {
					return SQLPARSER_STATUS_OK;
				}
				source_end = pos;
				boundary_is_merge_when = 1;
				break;
			}
			if (handle->sql[pos] == ';' ||
			    sqlparser_patch_assignment_clause_at(
				    handle->sql,
				    scan_end,
				    equals + 1U,
				    pos)) {
				source_end = pos;
				break;
			}
		}
		pos++;
	}
	while (source_end > source_start &&
	       isspace((unsigned char)handle->sql[source_end - 1U])) {
		source_end--;
	}
	if (depth != 0U || equals == SIZE_MAX || equals <= source_start ||
	    equals >= source_end ||
	    (merge_when_boundary_required && !boundary_is_merge_when) ||
	    !sqlparser_patch_assignment_target_path_matches(
		    handle,
		    source_start,
		    equals,
		    anchor) ||
	    (boundary_is_comma &&
	     assignment_index + 1U >= target_count) ||
	    (!boundary_is_comma &&
	     assignment_index + 1U != target_count)) {
		return SQLPARSER_STATUS_OK;
	}
	*out_start = source_start;
	*out_end = source_end;
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_select_trailing_clause_at(
	const char *sql,
	size_t length,
	size_t pos)
{
	return sqlparser_patch_source_word_at(sql, length, pos, "into") ||
		sqlparser_patch_source_word_at(sql, length, pos, "where") ||
		sqlparser_patch_source_word_at(sql, length, pos, "group") ||
		sqlparser_patch_source_word_at(sql, length, pos, "having") ||
		sqlparser_patch_source_word_at(sql, length, pos, "window") ||
		sqlparser_patch_source_word_at(sql, length, pos, "qualify") ||
		sqlparser_patch_source_word_at(sql, length, pos, "order") ||
		sqlparser_patch_source_word_at(sql, length, pos, "limit") ||
		sqlparser_patch_source_word_at(sql, length, pos, "offset") ||
		sqlparser_patch_source_word_at(sql, length, pos, "fetch") ||
		sqlparser_patch_source_word_at(sql, length, pos, "for") ||
		sqlparser_patch_source_word_at(sql, length, pos, "connect") ||
		sqlparser_patch_source_word_at(sql, length, pos, "start") ||
		sqlparser_patch_source_word_at(sql, length, pos, "model") ||
		sqlparser_patch_source_word_at(sql, length, pos, "union") ||
		sqlparser_patch_source_word_at(sql, length, pos, "intersect") ||
		sqlparser_patch_source_word_at(sql, length, pos, "except") ||
		sqlparser_patch_source_word_at(sql, length, pos, "minus");
}

static size_t sqlparser_patch_select_set_operator_length(
	const char *sql,
	size_t length,
	size_t pos)
{
	if (sqlparser_patch_source_word_at(sql, length, pos, "union")) {
		return 5U;
	}
	if (sqlparser_patch_source_word_at(sql, length, pos, "intersect")) {
		return 9U;
	}
	if (sqlparser_patch_source_word_at(sql, length, pos, "except")) {
		return 6U;
	}
	if (sqlparser_patch_source_word_at(sql, length, pos, "minus")) {
		return 5U;
	}
	return 0U;
}

static sqlparser_status_t sqlparser_patch_select_target_insert_source_offset(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const PgQuery__SelectStmt *stmt,
	size_t target_index,
	size_t *out_offset,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const PgQuery__ResTarget *first_target;
	const PgQuery__ResTarget *last_target;
	size_t boundary_length;
	size_t boundary_location;
	size_t bracket_depth;
	size_t comma_count;
	size_t depth;
	size_t insertion_comma;
	size_t parser_select_location;
	size_t pos;
	size_t skipped;
	size_t source_boundary_end;
	size_t source_boundary_start;
	size_t source_first_end;
	size_t source_first_start;
	size_t source_select_end;
	size_t source_select_start;
	size_t set_operator_length;
	sqlparser_status_t status;
	int boundary_is_eof;
	int boundary_is_from;
	int source_supported;
	int token_seen;

	*out_supported = 0;
	if (handle == NULL || handle->sql == NULL ||
	    handle->parser_sql == NULL || stmt == NULL ||
	    stmt->n_target_list == 0U || stmt->target_list == NULL ||
	    target_index > stmt->n_target_list ||
	    stmt->into_clause != NULL ||
	    (stmt->op != PG_QUERY__SET_OPERATION__SET_OPERATION_UNDEFINED &&
	     stmt->op != PG_QUERY__SET_OPERATION__SETOP_NONE) ||
	    stmt->target_list[0] == NULL ||
	    stmt->target_list[stmt->n_target_list - 1U] == NULL ||
	    stmt->target_list[0]->node_case !=
		    PG_QUERY__NODE__NODE_RES_TARGET ||
	    stmt->target_list[stmt->n_target_list - 1U]->node_case !=
		    PG_QUERY__NODE__NODE_RES_TARGET) {
		return SQLPARSER_STATUS_OK;
	}
	first_target = stmt->target_list[0]->res_target;
	last_target =
		stmt->target_list[stmt->n_target_list - 1U]->res_target;
	if (first_target == NULL || last_target == NULL ||
	    first_target->location < 0 || last_target->location < 0 ||
	    (size_t)first_target->location >= handle->parser_sql_len ||
	    (size_t)last_target->location >= handle->parser_sql_len ||
	    first_target->location > last_target->location) {
		return SQLPARSER_STATUS_OK;
	}

	parser_select_location = SIZE_MAX;
	for (pos = 0U; pos < (size_t)first_target->location; pos++) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->parser_sql,
			pos);
		if (skipped != pos) {
			if (skipped > handle->parser_sql_len) {
				return SQLPARSER_STATUS_OK;
			}
			pos = skipped - 1U;
			continue;
		}
		if (sqlparser_patch_source_word_at(
			    handle->parser_sql,
			    handle->parser_sql_len,
			    pos,
			    "select")) {
			parser_select_location = pos;
		}
	}
	if (parser_select_location == SIZE_MAX) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_source_origin_span(
		handle,
		(int32_t)parser_select_location,
		6U,
		&source_select_start,
		&source_select_end,
		&source_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !source_supported ||
	    source_select_end - source_select_start != 6U ||
	    !sqlparser_patch_source_word_at(
		    handle->sql,
		    handle->sql_len,
		    source_select_start,
		    "select")) {
		return status;
	}

	boundary_location = SIZE_MAX;
	boundary_length = 0U;
	boundary_is_eof = 0;
	boundary_is_from = 0;
	depth = 0U;
	bracket_depth = 0U;
	for (pos = (size_t)last_target->location;
	     pos < handle->parser_sql_len;
	     pos++) {
		if (sqlparser_patch_comment_starts_at(
			    handle->dialect,
			    handle->parser_sql,
			    pos)) {
			return SQLPARSER_STATUS_OK;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->parser_sql,
			pos);
		if (skipped != pos) {
			if (skipped > handle->parser_sql_len) {
				return SQLPARSER_STATUS_OK;
			}
			pos = skipped - 1U;
			continue;
		}
		if (depth == 0U && bracket_depth == 0U &&
		    sqlparser_patch_source_word_at(
			    handle->parser_sql,
			    handle->parser_sql_len,
			    pos,
			    "from")) {
			if (stmt->n_from_clause == 0U) {
				return SQLPARSER_STATUS_OK;
			}
			boundary_location = pos;
			boundary_length = 4U;
			boundary_is_from = 1;
			break;
		}
		set_operator_length =
			depth == 0U && bracket_depth == 0U ?
				sqlparser_patch_select_set_operator_length(
					handle->parser_sql,
					handle->parser_sql_len,
					pos) :
				0U;
		if (set_operator_length != 0U) {
			boundary_location = pos;
			boundary_length = set_operator_length;
			break;
		}
		if (depth == 0U && bracket_depth == 0U &&
		    sqlparser_patch_select_trailing_clause_at(
			    handle->parser_sql,
			    handle->parser_sql_len,
			    pos)) {
			return SQLPARSER_STATUS_OK;
		}
		if (handle->parser_sql[pos] == '(') {
			depth++;
		} else if (handle->parser_sql[pos] == ')') {
			if (depth == 0U) {
				if (bracket_depth != 0U ||
				    stmt->n_from_clause != 0U) {
					return SQLPARSER_STATUS_OK;
				}
				boundary_location = pos;
				boundary_length = 1U;
				break;
			}
			depth--;
		} else if (handle->parser_sql[pos] == '[') {
			bracket_depth++;
		} else if (handle->parser_sql[pos] == ']') {
			if (bracket_depth == 0U) {
				return SQLPARSER_STATUS_OK;
			}
			bracket_depth--;
		} else if (depth == 0U && bracket_depth == 0U &&
			   handle->parser_sql[pos] == ',') {
			return SQLPARSER_STATUS_OK;
		} else if (depth == 0U && bracket_depth == 0U &&
			   handle->parser_sql[pos] == ';') {
			if (stmt->n_from_clause != 0U) {
				return SQLPARSER_STATUS_OK;
			}
			boundary_location = pos;
			boundary_length = 1U;
			break;
		}
	}
	if (boundary_location == SIZE_MAX) {
		if (stmt->n_from_clause != 0U || depth != 0U ||
		    bracket_depth != 0U) {
			return SQLPARSER_STATUS_OK;
		}
		boundary_is_eof = 1;
		source_boundary_start = handle->sql_len;
		while (source_boundary_start > source_select_end &&
		       isspace((unsigned char)handle->sql[
			       source_boundary_start - 1U])) {
			source_boundary_start--;
		}
		source_boundary_end = source_boundary_start;
		if (source_select_end >= source_boundary_start) {
			return SQLPARSER_STATUS_OK;
		}
	} else {
		if (boundary_length == 0U) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_patch_source_origin_span(
			handle,
			(int32_t)boundary_location,
			boundary_length,
			&source_boundary_start,
			&source_boundary_end,
			&source_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (!source_supported && !boundary_is_from &&
		    stmt->n_from_clause == 0U &&
		    handle->parser_sql[boundary_location] == ';' &&
		    handle->control != NULL &&
		    statement_index < handle->control->unit_count &&
		    sqlparser_patch_control_surface_span_is_local(
			    handle,
			    statement_index,
			    source_select_start,
			    source_select_end)) {
			const sqlparser_control_unit_t *unit;

			unit = &handle->control->units[statement_index];
			source_boundary_start =
				unit->source_offset + unit->source_length;
			source_boundary_end = source_boundary_start;
			boundary_is_eof = 1;
			source_supported = 1;
		}
		if (!source_supported ||
		    (!boundary_is_eof &&
		     source_boundary_end - source_boundary_start !=
			     boundary_length) ||
		    source_select_end >= source_boundary_start) {
			return SQLPARSER_STATUS_OK;
		}
	}
	if (boundary_is_from) {
		if (!sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    source_boundary_start,
			    "from")) {
			return SQLPARSER_STATUS_OK;
		}
	} else if (!boundary_is_eof &&
		   handle->sql[source_boundary_start] !=
		   handle->parser_sql[boundary_location]) {
		return SQLPARSER_STATUS_OK;
	}

	comma_count = 0U;
	insertion_comma = SIZE_MAX;
	depth = 0U;
	bracket_depth = 0U;
	token_seen = 0;
	for (pos = source_select_end; pos < source_boundary_start; pos++) {
		if (sqlparser_patch_comment_starts_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			return SQLPARSER_STATUS_OK;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > source_boundary_start) {
				return SQLPARSER_STATUS_OK;
			}
			token_seen = 1;
			pos = skipped - 1U;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
			token_seen = 1;
		} else if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				return SQLPARSER_STATUS_OK;
			}
			depth--;
			token_seen = 1;
		} else if (handle->sql[pos] == '[') {
			bracket_depth++;
			token_seen = 1;
		} else if (handle->sql[pos] == ']') {
			if (bracket_depth == 0U) {
				return SQLPARSER_STATUS_OK;
			}
			bracket_depth--;
			token_seen = 1;
		} else if (depth == 0U && bracket_depth == 0U &&
			   handle->sql[pos] == ',') {
			comma_count++;
			if (comma_count == target_index) {
				insertion_comma = pos;
			}
		} else if (depth == 0U && bracket_depth == 0U &&
			   handle->sql[pos] == ';') {
			return SQLPARSER_STATUS_OK;
		} else if (!isspace((unsigned char)handle->sql[pos])) {
			token_seen = 1;
		}
	}
	if (!token_seen || depth != 0U || bracket_depth != 0U ||
	    comma_count + 1U != stmt->n_target_list) {
		return SQLPARSER_STATUS_OK;
	}
	if (target_index == 0U) {
		status = sqlparser_patch_target_source_span(
			handle,
			stmt->target_list[0],
			"from",
			0,
			&source_first_start,
			&source_first_end,
			&source_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !source_supported) {
			return status;
		}
		pos = source_select_end;
		while (pos < source_boundary_start &&
		       isspace((unsigned char)handle->sql[pos])) {
			pos++;
		}
		if (pos >= source_boundary_start || pos != source_first_start) {
			return SQLPARSER_STATUS_OK;
		}
		*out_offset = pos;
	} else if (target_index < stmt->n_target_list) {
		if (insertion_comma == SIZE_MAX) {
			return SQLPARSER_STATUS_OK;
		}
		pos = insertion_comma + 1U;
		while (pos < source_boundary_start &&
		       isspace((unsigned char)handle->sql[pos])) {
			pos++;
		}
		if (pos >= source_boundary_start) {
			return SQLPARSER_STATUS_OK;
		}
		*out_offset = pos;
	} else {
		pos = source_boundary_start;
		while (pos > source_select_end &&
		       isspace((unsigned char)handle->sql[pos - 1U])) {
			pos--;
		}
		if (pos <= source_select_end) {
			return SQLPARSER_STATUS_OK;
		}
		*out_offset = pos;
	}
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_patch_insert_target_spacing(
	const sqlparser_handle_t *handle,
	const PgQuery__SelectStmt *stmt,
	size_t target_index,
	size_t source_start,
	size_t *out_leading_space,
	size_t *out_trailing_space)
{
	const PgQuery__ResTarget *current;
	const PgQuery__ResTarget *previous;
	const PgQuery__ColumnRef *current_column_ref;
	const PgQuery__ColumnRef *previous_column_ref;
	const char *current_column;
	const char *previous_column;

	*out_leading_space = 0U;
	*out_trailing_space = 1U;
	if (handle == NULL || handle->sql == NULL || stmt == NULL ||
	    target_index == 0U ||
	    target_index >= stmt->n_target_list || stmt->target_list == NULL ||
	    source_start == 0U || source_start > handle->sql_len ||
	    handle->sql[source_start - 1U] != ',' ||
	    stmt->target_list[target_index - 1U] == NULL ||
	    stmt->target_list[target_index] == NULL ||
	    stmt->target_list[target_index - 1U]->node_case !=
		    PG_QUERY__NODE__NODE_RES_TARGET ||
	    stmt->target_list[target_index]->node_case !=
		    PG_QUERY__NODE__NODE_RES_TARGET) {
		return;
	}
	previous = stmt->target_list[target_index - 1U]->res_target;
	current = stmt->target_list[target_index]->res_target;
	if (previous == NULL || current == NULL || previous->val == NULL ||
	    current->val == NULL ||
	    previous->val->node_case != PG_QUERY__NODE__NODE_COLUMN_REF ||
	    current->val->node_case != PG_QUERY__NODE__NODE_COLUMN_REF) {
		return;
	}
	previous_column_ref = previous->val->column_ref;
	current_column_ref = current->val->column_ref;
	if (previous_column_ref == NULL || current_column_ref == NULL) {
		return;
	}
	if ((sqlparser_dialect_is_oracle_compatible(handle->dialect) ||
	     handle->dialect == SQLPARSER_DIALECT_DAMENG) &&
	    source_start >= 7U &&
	    (previous->name == NULL || previous->name[0] == '\0') &&
	    (current->name == NULL || current->name[0] == '\0') &&
	    previous_column_ref->n_fields == 1U &&
	    previous_column_ref->fields != NULL &&
	    current_column_ref->n_fields >= 2U &&
	    current_column_ref->fields != NULL &&
	    current_column_ref->fields[
		    current_column_ref->n_fields - 1U] != NULL &&
	    current_column_ref->fields[
		    current_column_ref->n_fields - 1U]->node_case ==
		    PG_QUERY__NODE__NODE_A_STAR &&
	    sqlparser_node_string_value(
		    previous_column_ref->fields[0],
		    &previous_column) &&
	    sqlparser_patch_source_word_at(
		    previous_column,
		    strlen(previous_column),
		    0U,
		    "rownum") &&
	    sqlparser_patch_source_word_at(
		    handle->sql,
		    source_start - 1U,
		    source_start - 7U,
		    "rownum")) {
		*out_leading_space = 1U;
		return;
	}
	if (sqlparser_dialect_is_mysql_compatible(handle->dialect) &&
	    source_start >= 2U && handle->sql[source_start - 2U] == '*' &&
	    (previous->name == NULL || previous->name[0] == '\0') &&
	    previous_column_ref->n_fields >= 2U &&
	    previous_column_ref->fields != NULL &&
	    previous_column_ref->fields[
		    previous_column_ref->n_fields - 1U] != NULL &&
	    previous_column_ref->fields[
		    previous_column_ref->n_fields - 1U]->node_case ==
		    PG_QUERY__NODE__NODE_A_STAR &&
	    current_column_ref->n_fields == 1U &&
	    current_column_ref->fields != NULL &&
	    sqlparser_node_string_value(
		    current_column_ref->fields[0],
		    &current_column) &&
	    sqlparser_patch_source_word_at(
		    current_column,
		    strlen(current_column),
		    0U,
		    "rownum") &&
	    sqlparser_patch_source_word_at(
		    handle->sql,
		    handle->sql_len,
		    source_start,
		    "rownum")) {
		*out_leading_space = 1U;
		*out_trailing_space = 0U;
	}
}

typedef struct {
	char *parts[3];
	size_t part_count;
	int32_t location;
	void *dialect_state;
} sqlparser_patch_identifier_path_t;

static void sqlparser_patch_identifier_path_clear(
	const sqlparser_handle_t *handle,
	sqlparser_patch_identifier_path_t *path)
{
	size_t index;

	if (path == NULL) {
		return;
	}
	for (index = 0U; index < sizeof(path->parts) / sizeof(path->parts[0]); index++) {
		free(path->parts[index]);
	}
	sqlparser_handle_discard_dialect_state(handle, path->dialect_state);
	memset(path, 0, sizeof(*path));
}

static int sqlparser_patch_identifier_path_is_exact(
	const sqlparser_handle_t *handle,
	const char *sql_text,
	int32_t location,
	size_t part_count)
{
	const char *spelling;
	size_t cursor;
	size_t length;
	size_t spelling_length;
	size_t index;

	if (handle == NULL || sql_text == NULL || part_count == 0U ||
	    !sqlparser_proto_location_is_identifier_spelling(location)) {
		return 0;
	}
	length = strlen(sql_text);
	cursor = 0U;
	for (index = 0U; index < part_count; index++) {
		while (cursor < length &&
		       isspace((unsigned char)sql_text[cursor])) {
			cursor++;
		}
		spelling = NULL;
		spelling_length = 0U;
		if (!sqlparser_handle_identifier_spelling(
			    handle,
			    location,
			    index,
			    &spelling,
			    &spelling_length) ||
		    spelling == NULL ||
		    spelling_length == 0U ||
		    spelling_length > length - cursor ||
		    memcmp(sql_text + cursor, spelling, spelling_length) != 0) {
			return 0;
		}
		cursor += spelling_length;
		while (cursor < length &&
		       isspace((unsigned char)sql_text[cursor])) {
			cursor++;
		}
		if (index + 1U < part_count) {
			if (cursor >= length || sql_text[cursor] != '.') {
				return 0;
			}
			cursor++;
		}
	}
	while (cursor < length &&
	       isspace((unsigned char)sql_text[cursor])) {
		cursor++;
	}
	return cursor == length;
}

static sqlparser_status_t sqlparser_patch_parse_identifier_path(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *sql_text,
	size_t max_part_count,
	const char *field_name,
	sqlparser_patch_identifier_path_t *out_path,
	sqlparser_error_t *out_error)
{
	PgQuery__ColumnRef *column_ref;
	PgQuery__Node *node;
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
	char *parser_sql;
	void *dialect_state;
	size_t index;
	sqlparser_status_t status;

	if (out_path == NULL || max_part_count == 0U ||
	    max_part_count > sizeof(out_path->parts) / sizeof(out_path->parts[0])) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier path output is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_path, 0, sizeof(*out_path));
	parser_sql = NULL;
	dialect_state = NULL;
	origins = NULL;
	node = NULL;
	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		statement_index,
		sql_text,
		field_name,
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
	status = sqlparser_parse_select_target_node_sql(
		parser_sql,
		&source,
		&node,
		out_error);
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	column_ref =
		node->node_case == PG_QUERY__NODE__NODE_RES_TARGET &&
			node->res_target != NULL &&
			(node->res_target->name == NULL ||
			 node->res_target->name[0] == '\0') &&
			node->res_target->val != NULL &&
			node->res_target->val->node_case ==
				PG_QUERY__NODE__NODE_COLUMN_REF ?
			node->res_target->val->column_ref :
			NULL;
	if (column_ref == NULL || column_ref->n_fields == 0U ||
	    column_ref->n_fields > max_part_count || column_ref->fields == NULL) {
		status = SQLPARSER_STATUS_UNSUPPORTED;
		sqlparser_error_set_message(
			out_error,
			status,
			max_part_count == 1U ?
				"column name must contain exactly one identifier" :
				"relation SQL must contain one identifier path with at most three parts");
	} else {
		for (index = 0U; index < column_ref->n_fields; index++) {
			PgQuery__Node *field;

			field = column_ref->fields[index];
			if (field == NULL ||
			    field->node_case != PG_QUERY__NODE__NODE_STRING ||
			    field->string == NULL ||
			    field->string->sval == NULL ||
			    field->string->sval[0] == '\0') {
				status = SQLPARSER_STATUS_UNSUPPORTED;
				sqlparser_error_set_message(
					out_error,
					status,
					"identifier path contains an invalid component");
				break;
			}
			out_path->parts[index] =
				sqlparser_strdup(field->string->sval);
			if (out_path->parts[index] == NULL) {
				status = SQLPARSER_STATUS_NO_MEMORY;
				sqlparser_error_set_message(
					out_error,
					status,
					"out of memory");
				break;
			}
		}
		if (status == SQLPARSER_STATUS_OK &&
		    !sqlparser_patch_identifier_path_is_exact(
			    handle,
			    sql_text,
			    column_ref->location,
			    column_ref->n_fields)) {
			status = SQLPARSER_STATUS_UNSUPPORTED;
			sqlparser_error_set_message(
				out_error,
				status,
				"identifier path must contain only identifiers and dot separators");
		}
		if (status == SQLPARSER_STATUS_OK) {
			out_path->part_count = column_ref->n_fields;
			out_path->location = column_ref->location;
			out_path->dialect_state = dialect_state;
			dialect_state = NULL;
		}
	}
	sqlparser_free_proto_node(node);
	sqlparser_handle_discard_dialect_state(handle, dialect_state);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_patch_identifier_path_clear(handle, out_path);
	}
	return status;
}

static sqlparser_status_t sqlparser_patch_plan_relation_surface_edits(
	sqlparser_handle_t *handle,
	size_t statement_index,
	PgQuery__RangeVar *relation,
	PgQuery__RangeVar *duplicate_relation,
	const sqlparser_relation_bindings_t *bindings,
	const sqlparser_patch_identifier_path_t *path,
	const char *sql_text,
	sqlparser_surface_source_edits_t *edits,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const char *spelling;
	size_t index;
	size_t replacement_length;
	size_t source_end;
	size_t source_start;
	size_t spelling_length;
	sqlparser_status_t status;
	int edit_supported;
	int qualifier_present;
	int span_supported;
	int surface_eligible;

	*out_supported = 0;
	if (sqlparser_dialect_is_sqlserver_compatible(handle->dialect)) {
		if (handle->control != NULL) {
			surface_eligible =
				sqlparser_patch_sqlserver_control_surface_eligible(
					handle,
					statement_index);
		} else {
			status = sqlparser_patch_sqlserver_surface_eligible(
				handle,
				&surface_eligible,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		if (!surface_eligible) {
			return SQLPARSER_STATUS_OK;
		}
	}
	if (relation == NULL || bindings == NULL || path == NULL ||
	    path->part_count == 0U || sql_text == NULL || edits == NULL ||
	    (bindings->column_ref_count > 0U &&
	     bindings->column_refs == NULL) ||
	    (bindings->assignment_target_count > 0U &&
	     bindings->assignment_targets == NULL)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation surface edit inputs are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	replacement_length = strlen(sql_text);
	if (bindings->column_ref_count > 0U ||
	    bindings->assignment_target_count > 0U) {
		spelling = NULL;
		spelling_length = 0U;
		if (path->part_count != 1U ||
		    !sqlparser_handle_identifier_spelling(
			    handle,
			    path->location,
			    0U,
			    &spelling,
			    &spelling_length) ||
		    spelling == NULL || spelling_length != replacement_length ||
		    memcmp(spelling, sql_text, replacement_length) != 0) {
			return SQLPARSER_STATUS_OK;
		}
	}
	status = sqlparser_patch_relation_source_span(
		handle,
		relation,
		edits,
		&source_start,
		&source_end,
		&span_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !span_supported) {
		return status;
	}
	if (!sqlparser_patch_control_surface_span_is_local(
		    handle,
		    statement_index,
		    source_start,
		    source_end)) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_patch_surface_edit_insert_deduplicated(
		edits,
		source_start,
		source_end,
		sql_text,
		replacement_length,
		&edit_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !edit_supported) {
		return status;
	}
	if (duplicate_relation != NULL && duplicate_relation != relation) {
		status = sqlparser_patch_relation_source_span(
			handle,
			duplicate_relation,
			edits,
			&source_start,
			&source_end,
			&span_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !span_supported) {
			return status;
		}
		if (!sqlparser_patch_control_surface_span_is_local(
			    handle,
			    statement_index,
			    source_start,
			    source_end)) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_patch_surface_edit_insert_deduplicated(
			edits,
			source_start,
			source_end,
			sql_text,
			replacement_length,
			&edit_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !edit_supported) {
			return status;
		}
	}
	for (index = 0U; index < bindings->column_ref_count; index++) {
		status = sqlparser_patch_column_qualifier_source_span(
			handle,
			bindings->column_refs[index],
			edits,
			&source_start,
			&source_end,
			&span_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !span_supported) {
			return status;
		}
		if (!sqlparser_patch_control_surface_span_is_local(
			    handle,
			    statement_index,
			    source_start,
			    source_end)) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_patch_surface_edit_insert_deduplicated(
			edits,
			source_start,
			source_end,
			sql_text,
			replacement_length,
			&edit_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !edit_supported) {
			return status;
		}
	}
	for (index = 0U;
	     index < bindings->assignment_target_count;
	     index++) {
		status = sqlparser_patch_assignment_qualifier_source_span(
			handle,
			relation,
			bindings->assignment_targets[index],
			edits,
			&source_start,
			&source_end,
			&qualifier_present,
			&span_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !span_supported) {
			return status;
		}
		if (!qualifier_present) {
			continue;
		}
		if (!sqlparser_patch_control_surface_span_is_local(
			    handle,
			    statement_index,
			    source_start,
			    source_end)) {
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_patch_surface_edit_insert_deduplicated(
			edits,
			source_start,
			source_end,
			sql_text,
			replacement_length,
			&edit_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !edit_supported) {
			return status;
		}
	}
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_set_relation_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_surface_source_edits_t *surface_edits,
	int *in_out_surface_complete,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	PgQuery__RangeVar *relation;
	PgQuery__RangeVar *selected_relation;
	PgQuery__RangeVar *duplicate_relation;
	sqlparser_relation_bindings_t bindings;
	sqlparser_patch_identifier_path_t path;
	const char *spellings[3];
	const char *values[3];
	const char *spelling;
	void *dialect_state;
	size_t first_part;
	size_t index;
	sqlparser_status_t status;
	int surface_supported;

	if (selector == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_RELATION ||
	    sql_text == NULL || surface_edits == NULL ||
	    in_out_surface_complete == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector kind must be relation");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&bindings, 0, sizeof(bindings));
	status = sqlparser_patch_parse_identifier_path(
		handle,
		selector->statement_index,
		sql_text,
		3U,
		"relation SQL",
		&path,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		selector->statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		1,
		selector->item_index,
		NULL,
		&message,
		out_error);
	if (status == SQLPARSER_STATUS_OK && message == NULL) {
		status = SQLPARSER_STATUS_INVALID_ARGUMENT;
		sqlparser_error_set_message(out_error, status, "relation selector is out of range");
	}
	if (status == SQLPARSER_STATUS_OK) {
		selected_relation = (PgQuery__RangeVar *)message;
		status = sqlparser_find_duplicate_delete_relation(
			handle,
			selector->statement_index,
			selected_relation,
			&relation,
			&duplicate_relation,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_collect_relation_bindings(
			handle,
			selector->statement_index,
			relation,
			&bindings,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && *in_out_surface_complete) {
		status = sqlparser_patch_plan_relation_surface_edits(
			handle,
			selector->statement_index,
			relation,
			duplicate_relation,
			&bindings,
			&path,
			sql_text,
			surface_edits,
			&surface_supported,
			out_error);
		if (status == SQLPARSER_STATUS_OK && !surface_supported) {
			sqlparser_surface_source_edits_release(surface_edits);
			*in_out_surface_complete = 0;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		spellings[0] = NULL;
		spellings[1] = NULL;
		spellings[2] = NULL;
		values[0] = "";
		values[1] = "";
		values[2] = "";
		first_part = 3U - path.part_count;
		for (index = 0U; index < path.part_count; index++) {
			values[first_part + index] = path.parts[index];
			spelling = NULL;
			if (!sqlparser_handle_identifier_spelling(
				    handle,
				    path.location,
				    index,
				    &spelling,
				    NULL) ||
			    spelling == NULL) {
				status = SQLPARSER_STATUS_INTERNAL_ERROR;
				sqlparser_error_set_message(
					out_error,
					status,
					"relation patch identifier spelling is missing");
				break;
			}
			spellings[first_part + index] = spelling;
		}
		if (status == SQLPARSER_STATUS_OK) {
			dialect_state = path.dialect_state;
			path.dialect_state = NULL;
			status = sqlparser_replace_relation_and_bound_qualifiers(
				handle,
				selector->statement_index,
				relation,
				duplicate_relation,
				&bindings,
				values,
				spellings,
				dialect_state,
				out_error);
		}
	}
	sqlparser_relation_bindings_clear(&bindings);
	sqlparser_patch_identifier_path_clear(handle, &path);
	return status;
}

static int sqlparser_patch_node_is_value_expression(const PgQuery__Node *node)
{
	if (node == NULL) {
		return 0;
	}
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_A_CONST:
		case PG_QUERY__NODE__NODE_COLUMN_REF:
		case PG_QUERY__NODE__NODE_PARAM_REF:
		case PG_QUERY__NODE__NODE_A_EXPR:
		case PG_QUERY__NODE__NODE_BOOL_EXPR:
		case PG_QUERY__NODE__NODE_FUNC_CALL:
		case PG_QUERY__NODE__NODE_TYPE_CAST:
		case PG_QUERY__NODE__NODE_COLLATE_CLAUSE:
		case PG_QUERY__NODE__NODE_A_INDIRECTION:
		case PG_QUERY__NODE__NODE_A_ARRAY_EXPR:
		case PG_QUERY__NODE__NODE_ARRAY_EXPR:
		case PG_QUERY__NODE__NODE_NULL_TEST:
		case PG_QUERY__NODE__NODE_BOOLEAN_TEST:
		case PG_QUERY__NODE__NODE_SUB_LINK:
		case PG_QUERY__NODE__NODE_CASE_EXPR:
		case PG_QUERY__NODE__NODE_CASE_WHEN:
		case PG_QUERY__NODE__NODE_ROW_EXPR:
		case PG_QUERY__NODE__NODE_ROW_COMPARE_EXPR:
		case PG_QUERY__NODE__NODE_COALESCE_EXPR:
		case PG_QUERY__NODE__NODE_MIN_MAX_EXPR:
		case PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION:
		case PG_QUERY__NODE__NODE_SET_TO_DEFAULT:
			return 1;
		default:
			return 0;
	}
}

static int sqlparser_patch_value_slot_is_variable_set_arg(
	const PgQuery__Node *statement,
	PgQuery__Node **value_slot)
{
	PgQuery__VariableSetStmt *set_stmt;
	size_t index;

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL ||
	    value_slot == NULL) {
		return 0;
	}

	set_stmt = statement->variable_set_stmt;
	for (index = 0U; index < set_stmt->n_args; index++) {
		if (&set_stmt->args[index] == value_slot) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_patch_set_value_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	int require_literal,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	PgQuery__Node **value_slot;
	PgQuery__Node *replacement;
	PgQuery__Node *semantic_replacement;
	sqlparser_literal_view_t literal_view;
	sqlparser_status_t status;
	char *parser_sql;
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
	void *dialect_state;
	int variable_set_arg;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_VALUE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector kind must be value");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	parser_sql = NULL;
	origins = NULL;
	dialect_state = NULL;
	replacement = NULL;
	semantic_replacement = NULL;
	statement = NULL;
	value_slot = NULL;
	variable_set_arg = 0;
	status = sqlparser_get_statement_node(handle, selector->statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_get_statement_node_slot_by_index(
		handle,
		selector->statement_index,
		selector->item_index,
		&value_slot,
		NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (value_slot == NULL || *value_slot == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "value selector node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	variable_set_arg = sqlparser_patch_value_slot_is_variable_set_arg(statement, value_slot);
	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		selector->statement_index,
		sql_text,
		"value SQL",
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
	if (variable_set_arg) {
		status = sqlparser_parse_variable_set_arg_node_sql(
			parser_sql,
			&source,
			&replacement,
			out_error);
	} else {
		status = sqlparser_parse_update_assignment_node_sql(
			parser_sql,
			&source,
			&replacement,
			out_error);
	}
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	if (require_literal) {
		semantic_replacement = sqlparser_unwrap_grouping_node(replacement);
		if (semantic_replacement == NULL ||
		    semantic_replacement->node_case !=
			    PG_QUERY__NODE__NODE_A_CONST ||
		    semantic_replacement->a_const == NULL) {
			sqlparser_free_proto_node(replacement);
			sqlparser_handle_discard_dialect_state(
				handle, dialect_state);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"replacement SQL is not a literal");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		memset(&literal_view, 0, sizeof(literal_view));
		status = sqlparser_fill_literal_view_from_a_const(
			semantic_replacement->a_const,
			&literal_view,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_free_proto_node(replacement);
			sqlparser_handle_discard_dialect_state(
				handle, dialect_state);
			return status;
		}
	}
	if (!variable_set_arg && !sqlparser_patch_node_is_value_expression(*value_slot)) {
		sqlparser_free_proto_node(replacement);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "value selector does not target an expression node");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	sqlparser_free_proto_node(*value_slot);
	*value_slot = replacement;
	replacement = NULL;
	return sqlparser_handle_commit_ast_with_dialect_state(
		handle, dialect_state, out_error);
}

static sqlparser_status_t sqlparser_patch_set_legacy_literal_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t value_selector;
	size_t node_index;
	sqlparser_status_t status;

	if (selector == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_LITERAL &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_WHERE_LITERAL)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"selector kind must be literal or where_literal");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_find_statement_literal_node(
		handle,
		selector->statement_index,
		selector->item_index,
		selector->kind == SQLPARSER_SELECTOR_KIND_WHERE_LITERAL,
		NULL,
		&node_index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&value_selector, 0, sizeof(value_selector));
	value_selector.kind = SQLPARSER_SELECTOR_KIND_VALUE;
	value_selector.statement_index = selector->statement_index;
	value_selector.item_index = node_index;
	return sqlparser_patch_set_value_sql(
		handle,
		&value_selector,
		sql_text,
		1,
		out_error);
}

static sqlparser_status_t sqlparser_patch_render_string_literal_sql(
	const char *value,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *sql;
	size_t len;
	size_t quote_count;
	size_t index;
	size_t out_index;

	if (value == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "string literal arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	len = strlen(value);
	quote_count = 0U;
	for (index = 0U; index < len; index++) {
		if (value[index] == '\'') {
			quote_count++;
		}
	}
	if (len > SIZE_MAX - quote_count - 3U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "literal SQL is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	sql = (char *)malloc(len + quote_count + 3U);
	if (sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	out_index = 0U;
	sql[out_index++] = '\'';
	for (index = 0U; index < len; index++) {
		sql[out_index++] = value[index];
		if (value[index] == '\'') {
			sql[out_index++] = '\'';
		}
	}
	sql[out_index++] = '\'';
	sql[out_index] = '\0';
	*out_sql = sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_render_literal_sql(
	const sqlparser_literal_value_t *value,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char buffer[64];

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal SQL output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	switch (value->kind) {
		case SQLPARSER_LITERAL_KIND_NULL:
			*out_sql = sqlparser_strdup("NULL");
			break;
		case SQLPARSER_LITERAL_KIND_STRING:
			return sqlparser_patch_render_string_literal_sql(value->string_value, out_sql, out_error);
		case SQLPARSER_LITERAL_KIND_INTEGER:
			(void)snprintf(buffer, sizeof(buffer), "%lld", value->integer_value);
			*out_sql = sqlparser_strdup(buffer);
			break;
		case SQLPARSER_LITERAL_KIND_FLOAT:
			if (value->float_value == NULL || value->float_value[0] == '\0') {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "float literal requires float_value");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			*out_sql = sqlparser_strdup(value->float_value);
			break;
		case SQLPARSER_LITERAL_KIND_BOOLEAN:
			*out_sql = sqlparser_strdup(value->boolean_value ? "TRUE" : "FALSE");
			break;
		case SQLPARSER_LITERAL_KIND_UNKNOWN:
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal kind is invalid");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_render_source_selector_sql(
	const sqlparser_handle_t *handle,
	const char *selector_text,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	if (handle == NULL || selector_text == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "source selector arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	status = sqlparser_patch_parse_selector(selector_text, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	switch (selector.kind) {
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
			return sqlparser_selector_insert_cell_sql(handle, &selector, out_sql, out_error);
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL:
			return sqlparser_patch_render_merge_insert_cell_source_sql(
				handle,
				&selector,
				out_sql,
				out_error);
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGET:
			return sqlparser_selector_select_target_sql(handle, &selector, out_sql, out_error);
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET:
			return sqlparser_dml_result_target_sql(handle, &selector, out_sql, out_error);
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
		case SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT:
			return sqlparser_selector_update_assignment_sql(handle, &selector, out_sql, out_error);
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "source_selector kind cannot be cloned");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}
}

static sqlparser_status_t sqlparser_patch_render_structured_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	const char *text_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	int source_count;

	if (patch == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "patch SQL arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	source_count = 0;
	if (text_sql != NULL) {
		source_count++;
	}
	if (patch->source_selector != NULL) {
		source_count++;
	}
	if (patch->literal != NULL) {
		source_count++;
	}
	if (patch->bind != NULL) {
		source_count++;
	}
	if (source_count != 1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "patch must provide exactly one SQL, literal, or bind value");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (text_sql != NULL) {
		*out_sql = sqlparser_strdup(text_sql);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (patch->source_selector != NULL) {
		return sqlparser_patch_render_source_selector_sql(handle, patch->source_selector, out_sql, out_error);
	}
	if (patch->literal != NULL) {
		return sqlparser_patch_render_literal_sql(patch->literal, out_sql, out_error);
	}
	return sqlparser_render_bind_value_sql(handle, patch->bind, out_sql, out_error);
}

typedef struct {
	size_t start;
	size_t end;
} sqlparser_patch_source_span_t;

typedef struct {
	PgQuery__MergeWhenClause *clause;
	sqlparser_patch_source_span_t *target_spans;
	sqlparser_patch_source_span_t *value_spans;
	size_t target_count;
	size_t value_count;
} sqlparser_patch_merge_insert_surface_t;

static void sqlparser_patch_merge_insert_surface_clear(
	sqlparser_patch_merge_insert_surface_t *surface)
{
	if (surface == NULL) {
		return;
	}
	free(surface->target_spans);
	free(surface->value_spans);
	memset(surface, 0, sizeof(*surface));
}

static int sqlparser_patch_matching_parenthesis(
	const sqlparser_handle_t *handle,
	size_t open,
	size_t *out_close)
{
	size_t depth;
	size_t pos;
	size_t skipped;

	if (handle == NULL || out_close == NULL || open >= handle->sql_len ||
	    handle->sql[open] != '(') {
		return 0;
	}
	depth = 0U;
	for (pos = open; pos < handle->sql_len; pos++) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > handle->sql_len) {
				return 0;
			}
			pos = skipped - 1U;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
		} else if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			if (depth == 0U) {
				*out_close = pos;
				return 1;
			}
		}
	}
	return 0;
}

static int sqlparser_patch_list_item_spans(
	const sqlparser_handle_t *handle,
	size_t open,
	size_t close,
	sqlparser_patch_source_span_t *spans,
	size_t expected_count)
{
	size_t brace_depth;
	size_t bracket_depth;
	size_t count;
	size_t item_start;
	size_t paren_depth;
	size_t pos;
	size_t skipped;

	if (handle == NULL || spans == NULL || expected_count == 0U ||
	    open >= close || close >= handle->sql_len ||
	    handle->sql[open] != '(' || handle->sql[close] != ')') {
		return 0;
	}
	brace_depth = 0U;
	bracket_depth = 0U;
	count = 0U;
	item_start = open + 1U;
	paren_depth = 0U;
	for (pos = item_start; pos <= close; pos++) {
		size_t item_end;
		size_t trimmed_start;

		if (pos < close) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				handle->sql,
				pos);
			if (skipped != pos) {
				if (skipped > close) {
					return 0;
				}
				pos = skipped - 1U;
				continue;
			}
			if (handle->sql[pos] == '(') {
				paren_depth++;
				continue;
			}
			if (handle->sql[pos] == '[') {
				bracket_depth++;
				continue;
			}
			if (handle->sql[pos] == '{') {
				brace_depth++;
				continue;
			}
			if (handle->sql[pos] == ')' && paren_depth > 0U) {
				paren_depth--;
				continue;
			}
			if (handle->sql[pos] == ']' && bracket_depth > 0U) {
				bracket_depth--;
				continue;
			}
			if (handle->sql[pos] == '}' && brace_depth > 0U) {
				brace_depth--;
				continue;
			}
			if (handle->sql[pos] != ',' || paren_depth != 0U ||
			    bracket_depth != 0U || brace_depth != 0U) {
				continue;
			}
		} else if (paren_depth != 0U || bracket_depth != 0U ||
			   brace_depth != 0U) {
			return 0;
		}
		trimmed_start = item_start;
		while (trimmed_start < pos &&
		       isspace((unsigned char)handle->sql[trimmed_start])) {
			trimmed_start++;
		}
		item_end = pos;
		while (item_end > trimmed_start &&
		       isspace((unsigned char)handle->sql[item_end - 1U])) {
			item_end--;
		}
		if (trimmed_start >= item_end || count >= expected_count) {
			return 0;
		}
		spans[count].start = trimmed_start;
		spans[count].end = item_end;
		count++;
		item_start = pos + 1U;
	}
	return count == expected_count;
}

static sqlparser_status_t sqlparser_patch_sqlserver_result_sink_source_span(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t *out_start,
	size_t *out_end,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const char *column_sql;
	sqlparser_dialect_dml_result_channel_t channel;
	sqlparser_patch_source_span_t *column_spans;
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	size_t after_close;
	size_t close;
	size_t column_index;
	size_t column_length;
	size_t relation_end;
	size_t relation_length;
	size_t relation_start;
	size_t output_index;
	size_t pos;
	sqlparser_status_t status;

	*out_supported = 0;
	if (handle == NULL || selector == NULL || out_start == NULL ||
	    out_end == NULL ||
	    (selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK &&
	     selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN) ||
	    !sqlparser_dialect_is_sqlserver_compatible(handle->dialect) ||
	    !sqlparser_dialect_dml_result_channel_at(
		    handle,
		    selector->statement_index,
		    selector->item_index,
		    selector->row_index,
		    &channel) ||
	    channel.kind != SQLPARSER_GRAPH_DML_RESULT_SINK ||
	    channel.sink_sql == NULL || channel.sink_sql[0] == '\0') {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_sqlserver_scanner_init(
		&scanner,
		handle->sql,
		0U,
		handle->sql_len,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	output_index = 0U;
	for (;;) {
		status = sqlparser_sqlserver_scanner_next(
			&scanner, &token, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			return SQLPARSER_STATUS_OK;
		}
		if (token.paren_depth != 0U || token.block_depth != 0U ||
		    token.case_depth != 0U ||
		    !sqlparser_sqlserver_token_word_equal(
			    handle->sql, &token, "output")) {
			continue;
		}
		if (output_index == selector->row_index) {
			break;
		}
		output_index++;
	}

	for (;;) {
		status = sqlparser_sqlserver_scanner_next(
			&scanner, &token, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			return SQLPARSER_STATUS_OK;
		}
		if (token.paren_depth != 0U || token.block_depth != 0U ||
		    token.case_depth != 0U) {
			continue;
		}
		if (sqlparser_sqlserver_token_word_equal(
			    handle->sql, &token, "into")) {
			break;
		}
		if (sqlparser_sqlserver_token_word_equal(
			    handle->sql, &token, "output") ||
		    sqlparser_sqlserver_token_word_equal(
			    handle->sql, &token, "values")) {
			return SQLPARSER_STATUS_OK;
		}
	}
	status = sqlparser_sqlserver_scanner_next(&scanner, &token, out_error);
	if (status != SQLPARSER_STATUS_OK ||
	    token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
		return status;
	}
	relation_start = token.start;
	relation_length = strlen(channel.sink_sql);
	if (relation_length > handle->sql_len - relation_start ||
	    memcmp(
		    handle->sql + relation_start,
		    channel.sink_sql,
		    relation_length) != 0) {
		return SQLPARSER_STATUS_OK;
	}
	relation_end = relation_start + relation_length;
	if (selector->kind == SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK) {
		*out_start = relation_start;
		*out_end = relation_end;
		*out_supported = 1;
		return SQLPARSER_STATUS_OK;
	}

	if (channel.sink_column_count == 0U ||
	    selector->column_index >= channel.sink_column_count) {
		return SQLPARSER_STATUS_OK;
	}
	pos = relation_end;
	while (pos < handle->sql_len &&
	       isspace((unsigned char)handle->sql[pos])) {
		pos++;
	}
	if (pos >= handle->sql_len || handle->sql[pos] != '(' ||
	    !sqlparser_sqlserver_find_matching_paren(
		    handle->sql, pos, &close, &after_close)) {
		return SQLPARSER_STATUS_OK;
	}
	(void)after_close;
	if (channel.sink_column_count >
	    SIZE_MAX / sizeof(*column_spans)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"DML result sink column list is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	column_spans = (sqlparser_patch_source_span_t *)malloc(
		channel.sink_column_count * sizeof(*column_spans));
	if (column_spans == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (!sqlparser_patch_list_item_spans(
		    handle,
		    pos,
		    close,
		    column_spans,
		    channel.sink_column_count)) {
		free(column_spans);
		return SQLPARSER_STATUS_OK;
	}
	for (column_index = 0U;
	     column_index < channel.sink_column_count;
	     column_index++) {
		column_sql = sqlparser_dialect_dml_result_sink_column_at(
			handle->dialect,
			handle->dialect_state,
			selector->statement_index,
			selector->item_index,
			selector->row_index,
			column_index);
		column_length = column_sql != NULL ? strlen(column_sql) : 0U;
		if (column_sql == NULL ||
		    column_length !=
			    column_spans[column_index].end -
				    column_spans[column_index].start ||
		    memcmp(
			    handle->sql + column_spans[column_index].start,
			    column_sql,
			    column_length) != 0) {
			free(column_spans);
			return SQLPARSER_STATUS_OK;
		}
	}
	*out_start = column_spans[selector->column_index].start;
	*out_end = column_spans[selector->column_index].end;
	*out_supported = 1;
	free(column_spans);
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_span_content(
	const sqlparser_handle_t *handle,
	const sqlparser_patch_source_span_t *span,
	size_t *out_start,
	size_t *out_end)
{
	size_t content_end;
	size_t content_start;
	size_t pos;
	size_t skipped;

	if (handle == NULL || span == NULL || out_start == NULL ||
	    out_end == NULL || span->start >= span->end ||
	    span->end > handle->sql_len) {
		return 0;
	}
	content_start = sqlparser_public_skip_trivia(
		handle->dialect,
		handle->sql,
		span->start);
	if (content_start >= span->end) {
		return 0;
	}
	content_end = content_start;
	for (pos = content_start; pos < span->end;) {
		if (sqlparser_patch_comment_starts_at(
			    handle->dialect,
			    handle->sql,
			    pos)) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				handle->sql,
				pos);
			if (skipped <= pos || skipped > span->end) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > span->end) {
				return 0;
			}
			content_end = skipped;
			pos = skipped;
			continue;
		}
		if (!isspace((unsigned char)handle->sql[pos])) {
			content_end = pos + 1U;
		}
		pos++;
	}
	if (content_start >= content_end) {
		return 0;
	}
	*out_start = content_start;
	*out_end = content_end;
	return 1;
}

static sqlparser_status_t sqlparser_patch_merge_source_start(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const PgQuery__MergeStmt *merge_stmt,
	size_t *out_start,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_dml_result_dml_t dml;
	int32_t parser_location;
	size_t anchor;
	size_t dml_count;
	size_t duplicate_ordinal;
	size_t index;
	size_t pos;
	size_t statement_index;
	size_t skipped;
	size_t source_length;
	sqlparser_status_t status;
	int anchor_supported;

	if (handle == NULL || selector == NULL || merge_stmt == NULL ||
	    out_start == NULL || out_supported == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE source location arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_supported = 0;
	if (sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
	    sqlparser_dialect_dml_result_dml_at(
		    handle,
		    selector->statement_index,
		    selector->row_index,
		    &dml) &&
	    dml.kind == SQLPARSER_GRAPH_DML_MERGE &&
	    dml.source_sql != NULL && dml.source_sql[0] != '\0') {
		source_length = strlen(dml.source_sql);
		duplicate_ordinal = 0U;
		for (statement_index = 0U;
		     statement_index <= selector->statement_index;
		     statement_index++) {
			dml_count = statement_index == selector->statement_index ?
				selector->row_index :
				sqlparser_dialect_dml_result_count(
					handle,
					statement_index);
			for (index = 0U; index < dml_count; index++) {
				sqlparser_dialect_dml_result_dml_t previous;

				if (sqlparser_dialect_dml_result_dml_at(
					    handle,
					    statement_index,
					    index,
					    &previous) &&
				    previous.kind == SQLPARSER_GRAPH_DML_MERGE &&
				    previous.source_sql != NULL &&
				    strcmp(previous.source_sql, dml.source_sql) == 0) {
					duplicate_ordinal++;
				}
			}
		}
		for (pos = 0U; pos < handle->sql_len;) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				handle->sql,
				pos);
			if (skipped != pos) {
				pos = skipped;
				continue;
			}
			if (source_length < handle->sql_len - pos &&
			    sqlparser_patch_source_word_at(
				    handle->sql,
				    handle->sql_len,
				    pos,
				    "merge") &&
			    memcmp(handle->sql + pos, dml.source_sql, source_length) == 0 &&
			    handle->sql[pos + source_length] == ')') {
				if (duplicate_ordinal == 0U) {
					*out_start = pos;
					*out_supported = 1;
					return SQLPARSER_STATUS_OK;
				}
				duplicate_ordinal--;
				pos += source_length;
				continue;
			}
			pos++;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (merge_stmt->relation == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	parser_location = merge_stmt->relation->location;
	anchor = 0U;
	anchor_supported = 0;
	status = sqlparser_patch_source_offset(
		handle,
		parser_location,
		&anchor,
		&anchor_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !anchor_supported) {
		return status;
	}
	for (pos = 0U; pos < anchor;) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			if (skipped > anchor) {
				return SQLPARSER_STATUS_OK;
			}
			pos = skipped;
			continue;
		}
		if (sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "merge")) {
			*out_start = pos;
			*out_supported = 1;
			pos += strlen("merge");
			continue;
		}
		pos++;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_merge_insert_surface(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	int need_targets,
	int need_values,
	sqlparser_patch_merge_insert_surface_t *out_surface,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeStmt *merge_stmt;
	PgQuery__MergeWhenClause *clause;
	size_t case_depth;
	size_t depth;
	size_t insert_pos;
	size_t merge_pos;
	size_t pos;
	size_t skipped;
	size_t target_close;
	size_t target_open;
	size_t values_close;
	size_t values_open;
	size_t when_ordinal;
	sqlparser_status_t status;
	int has_target_list;
	int merge_supported;

	if ((!need_targets && !need_values) || out_surface == NULL ||
	    out_supported == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT surface output is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_surface, 0, sizeof(*out_surface));
	*out_supported = 0;
	merge_stmt = NULL;
	clause = NULL;
	status = sqlparser_patch_merge_insert_clause(
		handle,
		selector,
		&merge_stmt,
		&clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	has_target_list = clause->n_target_list > 0U;
	if ((need_targets &&
	     (!has_target_list || clause->target_list == NULL)) ||
	    (need_values &&
	     (clause->n_values == 0U || clause->values == NULL))) {
		return SQLPARSER_STATUS_OK;
	}
	merge_pos = 0U;
	merge_supported = 0;
	status = sqlparser_patch_merge_source_start(
		handle,
		selector,
		merge_stmt,
		&merge_pos,
		&merge_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !merge_supported) {
		return status;
	}
	insert_pos = SIZE_MAX;
	case_depth = 0U;
	depth = 0U;
	when_ordinal = 0U;
	for (pos = merge_pos + strlen("merge"); pos < handle->sql_len;) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped != pos) {
			pos = skipped;
			continue;
		}
		if (handle->sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (handle->sql[pos] == ')') {
			if (depth == 0U) {
				break;
			}
			depth--;
			pos++;
			continue;
		}
		if (sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "case")) {
			case_depth++;
			pos += strlen("case");
			continue;
		}
		if (case_depth > 0U &&
		    sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "end")) {
			case_depth--;
			pos += strlen("end");
			continue;
		}
		if (depth == 0U && case_depth == 0U &&
		    handle->sql[pos] == ';') {
			break;
		}
		if (depth == 0U && case_depth == 0U &&
		    sqlparser_patch_merge_when_at(
			    handle->dialect,
			    handle->sql,
			    handle->sql_len,
			    pos)) {
			if (when_ordinal == selector->item_index) {
				size_t action_case_depth;
				size_t action_depth;
				size_t action_pos;

				action_case_depth = 0U;
				action_depth = 0U;
				for (action_pos = pos + strlen("when");
				     action_pos < handle->sql_len;) {
					skipped =
						sqlparser_public_skip_quoted_or_comment(
							handle->dialect,
							handle->sql,
							action_pos);
					if (skipped != action_pos) {
						action_pos = skipped;
						continue;
					}
					if (handle->sql[action_pos] == '(') {
						action_depth++;
						action_pos++;
						continue;
					}
					if (handle->sql[action_pos] == ')') {
						if (action_depth == 0U) {
							break;
						}
						action_depth--;
						action_pos++;
						continue;
					}
					if (sqlparser_patch_source_word_at(
						    handle->sql,
						    handle->sql_len,
						    action_pos,
						    "case")) {
						action_case_depth++;
						action_pos += strlen("case");
						continue;
					}
					if (action_case_depth > 0U &&
					    sqlparser_patch_source_word_at(
						    handle->sql,
						    handle->sql_len,
						    action_pos,
						    "end")) {
						action_case_depth--;
						action_pos += strlen("end");
						continue;
					}
					if (action_depth == 0U &&
					    action_case_depth == 0U &&
					    (handle->sql[action_pos] == ';' ||
					     sqlparser_patch_merge_when_at(
						     handle->dialect,
						     handle->sql,
						     handle->sql_len,
						     action_pos))) {
						break;
					}
					if (action_depth == 0U &&
					    action_case_depth == 0U &&
					    sqlparser_patch_source_word_at(
						    handle->sql,
						    handle->sql_len,
						    action_pos,
						    "then")) {
						size_t after_then;

						after_then = sqlparser_public_skip_trivia(
							handle->dialect,
							handle->sql,
							action_pos + strlen("then"));
						if (sqlparser_patch_source_word_at(
							    handle->sql,
							    handle->sql_len,
							    after_then,
							    "insert")) {
							insert_pos = after_then;
							break;
						}
					}
					action_pos++;
				}
				break;
			}
			when_ordinal++;
			pos += strlen("when");
			continue;
		}
		pos++;
	}
	if (insert_pos == SIZE_MAX) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		handle->sql,
		insert_pos + strlen("insert"));
	target_open = SIZE_MAX;
	target_close = SIZE_MAX;
	if (has_target_list) {
		if (pos >= handle->sql_len || handle->sql[pos] != '(' ||
		    !sqlparser_patch_matching_parenthesis(
			    handle,
			    pos,
			    &target_close)) {
			return SQLPARSER_STATUS_OK;
		}
		target_open = pos;
		pos = target_close + 1U;
	}
	out_surface->clause = clause;
	out_surface->target_count = clause->n_target_list;
	out_surface->value_count = clause->n_values;
	if (need_targets) {
		if (clause->n_target_list >
		    SIZE_MAX / sizeof(*out_surface->target_spans)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_RESOURCE_LIMIT,
				"MERGE INSERT column list is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		out_surface->target_spans =
			(sqlparser_patch_source_span_t *)calloc(
				clause->n_target_list,
				sizeof(*out_surface->target_spans));
		if (out_surface->target_spans == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (!sqlparser_patch_list_item_spans(
			    handle,
			    target_open,
			    target_close,
			    out_surface->target_spans,
			    clause->n_target_list)) {
			sqlparser_patch_merge_insert_surface_clear(out_surface);
			return SQLPARSER_STATUS_OK;
		}
	}
	if (!need_values) {
		*out_supported = 1;
		return SQLPARSER_STATUS_OK;
	}
	for (;;) {
		pos = sqlparser_public_skip_trivia(
			handle->dialect,
			handle->sql,
			pos);
		if (pos >= handle->sql_len || handle->sql[pos] == ';' ||
		    sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "when")) {
			sqlparser_patch_merge_insert_surface_clear(out_surface);
			return SQLPARSER_STATUS_OK;
		}
		if (sqlparser_patch_source_word_at(
			    handle->sql,
			    handle->sql_len,
			    pos,
			    "values")) {
			pos += strlen("values");
			break;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		pos = skipped != pos ? skipped : pos + 1U;
	}
	values_open = sqlparser_public_skip_trivia(
		handle->dialect,
		handle->sql,
		pos);
	if (values_open >= handle->sql_len ||
	    handle->sql[values_open] != '(' ||
	    !sqlparser_patch_matching_parenthesis(
		    handle,
		    values_open,
		    &values_close)) {
		sqlparser_patch_merge_insert_surface_clear(out_surface);
		return SQLPARSER_STATUS_OK;
	}
	if (clause->n_values > SIZE_MAX / sizeof(*out_surface->value_spans)) {
		sqlparser_patch_merge_insert_surface_clear(out_surface);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"MERGE INSERT value list is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	out_surface->value_spans =
		(sqlparser_patch_source_span_t *)calloc(
			clause->n_values,
			sizeof(*out_surface->value_spans));
	if (out_surface->value_spans == NULL) {
		sqlparser_patch_merge_insert_surface_clear(out_surface);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (!sqlparser_patch_list_item_spans(
		    handle,
		    values_open,
		    values_close,
		    out_surface->value_spans,
		    clause->n_values)) {
		sqlparser_patch_merge_insert_surface_clear(out_surface);
		return SQLPARSER_STATUS_OK;
	}
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_render_merge_insert_cell_source_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_merge_insert_surface_t surface;
	PgQuery__MergeWhenClause *clause;
	PgQuery__Node *value_node;
	char *core_sql;
	size_t source_end;
	size_t source_start;
	sqlparser_status_t status;
	int surface_supported;

	if (handle == NULL || selector == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT source cell arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle->generation == 0UL ||
	    (handle->surface_source_complete &&
	     handle->surface_source_edits.count == 0U)) {
		memset(&surface, 0, sizeof(surface));
		surface_supported = 0;
		status = sqlparser_patch_merge_insert_surface(
			(sqlparser_handle_t *)handle,
			selector,
			0,
			1,
			&surface,
			&surface_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_patch_merge_insert_surface_clear(&surface);
			return status;
		}
		if (surface_supported &&
		    selector->column_index < surface.value_count &&
		    sqlparser_patch_span_content(
			    handle,
			    &surface.value_spans[selector->column_index],
			    &source_start,
			    &source_end)) {
			*out_sql = sqlparser_strndup(
				handle->sql + source_start,
				source_end - source_start);
			sqlparser_patch_merge_insert_surface_clear(&surface);
			if (*out_sql == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			return SQLPARSER_STATUS_OK;
		}
		sqlparser_patch_merge_insert_surface_clear(&surface);
	}

	clause = NULL;
	status = sqlparser_patch_merge_insert_clause(
		(sqlparser_handle_t *)handle,
		selector,
		NULL,
		&clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (clause->values == NULL ||
	    selector->column_index >= clause->n_values) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT cell index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	value_node = clause->values[selector->column_index];
	core_sql = NULL;
	status = sqlparser_render_insert_cell_node_sql(
		handle,
		value_node,
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
		(ProtobufCMessage *const *)&value_node,
		1U,
		"MERGE INSERT cell SQL",
		out_sql,
		out_error);
	free(core_sql);
	return status;
}

static sqlparser_status_t sqlparser_patch_list_insertion(
	const sqlparser_handle_t *handle,
	const sqlparser_patch_source_span_t *spans,
	size_t count,
	size_t index,
	const char *sql_text,
	size_t *out_offset,
	char **out_text,
	sqlparser_error_t *out_error)
{
	const char *separator;
	size_t separator_end;
	size_t separator_length;
	size_t separator_start;
	size_t sql_length;
	char *text;

	if (handle == NULL || spans == NULL || count == 0U || index > count ||
	    sql_text == NULL || sql_text[0] == '\0' || out_offset == NULL ||
	    out_text == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"list insertion arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_text = NULL;
	separator = ", ";
	separator_length = 2U;
	if (count > 1U) {
		if (index == 0U) {
			separator_start = spans[0].end;
			separator_end = spans[1].start;
		} else if (index < count) {
			separator_start = spans[index - 1U].end;
			separator_end = spans[index].start;
		} else {
			separator_start = spans[count - 2U].end;
			separator_end = spans[count - 1U].start;
		}
		if (separator_start > separator_end ||
		    !sqlparser_patch_target_gap_is_separator(
			    handle->sql,
			    separator_start,
			    separator_end)) {
			return SQLPARSER_STATUS_OK;
		}
		separator = handle->sql + separator_start;
		separator_length = separator_end - separator_start;
	}
	sql_length = strlen(sql_text);
	if (sql_length > SIZE_MAX - separator_length - 1U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"patch SQL is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	text = (char *)malloc(sql_length + separator_length + 1U);
	if (text == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (index < count) {
		memcpy(text, sql_text, sql_length);
		memcpy(text + sql_length, separator, separator_length);
		*out_offset = spans[index].start;
	} else {
		memcpy(text, separator, separator_length);
		memcpy(text + separator_length, sql_text, sql_length);
		*out_offset = spans[count - 1U].end;
	}
	text[sql_length + separator_length] = '\0';
	*out_text = text;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_merge_list_deletion(
	const sqlparser_handle_t *handle,
	const sqlparser_patch_source_span_t *spans,
	size_t count,
	size_t index,
	size_t *out_start,
	size_t *out_end)
{
	if (handle == NULL || spans == NULL || count <= 1U || index >= count ||
	    out_start == NULL || out_end == NULL) {
		return 0;
	}
	if (index + 1U < count) {
		if (!sqlparser_patch_target_gap_is_separator(
			    handle->sql,
			    spans[index].end,
			    spans[index + 1U].start)) {
			return 0;
		}
		*out_start = spans[index].start;
		*out_end = spans[index + 1U].start;
	} else {
		if (!sqlparser_patch_target_gap_is_separator(
			    handle->sql,
			    spans[index - 1U].end,
			    spans[index].start)) {
			return 0;
		}
		*out_start = spans[index - 1U].end;
		*out_end = spans[index].end;
	}
	return 1;
}

static int sqlparser_patch_surface_ranges_overlap(
	size_t left_start,
	size_t left_end,
	size_t right_start,
	size_t right_end)
{
	if (left_start == left_end) {
		return left_start >= right_start && left_start <= right_end;
	}
	if (right_start == right_end) {
		return right_start >= left_start && right_start <= left_end;
	}
	return left_start < right_end && right_start < left_end;
}

static sqlparser_status_t sqlparser_patch_insert_column_source_span(
	const sqlparser_handle_t *handle,
	const PgQuery__InsertStmt *stmt,
	size_t column_index,
	sqlparser_patch_source_span_t *out_span,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	const PgQuery__Node *column_node;
	const PgQuery__ResTarget *column;
	size_t source_start;
	sqlparser_status_t status;
	int source_supported;

	if (handle == NULL || stmt == NULL || out_span == NULL ||
	    out_supported == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert column source span arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_supported = 0;
	if (column_index >= stmt->n_cols || stmt->cols == NULL ||
	    (column_node = stmt->cols[column_index]) == NULL ||
	    column_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    (column = column_node->res_target) == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	source_start = 0U;
	source_supported = 0;
	status = sqlparser_patch_source_offset(
		handle,
		column->location,
		&source_start,
		&source_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !source_supported) {
		return status;
	}
	out_span->start = source_start;
	out_span->end = sqlparser_patch_identifier_token_end(
		handle, source_start);
	if (out_span->end <= out_span->start ||
	    out_span->end > handle->sql_len) {
		memset(out_span, 0, sizeof(*out_span));
		return SQLPARSER_STATUS_OK;
	}
	*out_supported = 1;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_patch_surface_insertion_available(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const sqlparser_surface_source_edits_t *edits,
	size_t offset)
{
	size_t edit_index;

	if (handle == NULL || selector == NULL || edits == NULL ||
	    !sqlparser_patch_control_surface_span_is_local(
		    handle,
		    selector->statement_index,
		    offset,
		    offset)) {
		return 0;
	}
	for (edit_index = 0U; edit_index < edits->count; edit_index++) {
		const sqlparser_surface_source_edit_t *edit;

		edit = &edits->items[edit_index];
		if (sqlparser_patch_surface_ranges_overlap(
			    edit->source_start,
			    edit->source_end,
			    offset,
			    offset)) {
			return 0;
		}
	}
	return 1;
}

static sqlparser_status_t sqlparser_patch_plan_insert_columns_surface_edit(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	const sqlparser_selector_t *selector,
	sqlparser_surface_source_edits_t *edits,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *stmt;
	PgQuery__SelectStmt *values_stmt;
	sqlparser_patch_source_span_t span;
	sqlparser_view_expression_source_cache_t source_cache;
	char *insertion;
	char *rendered_default;
	size_t *offsets;
	size_t anchor_index;
	size_t edit_index;
	size_t insert_index;
	size_t plan_count;
	size_t row_index;
	size_t source_offset;
	sqlparser_status_t status;
	int edit_supported;
	int source_result;
	int span_supported;

	if (handle == NULL || patch == NULL || selector == NULL ||
	    edits == NULL || out_supported == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert column surface edit arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_supported = 0;
	if (selector->kind != SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS ||
	    sqlparser_dialect_state_has_multi_insert(
		    handle->dialect, handle->dialect_state)) {
		return SQLPARSER_STATUS_OK;
	}
	stmt = NULL;
	status = sqlparser_get_insert_stmt(
		handle, selector->statement_index, &stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (!sqlparser_patch_insert_values_shape(stmt)) {
		return SQLPARSER_STATUS_OK;
	}
	values_stmt = stmt->select_stmt->select_stmt;
	if (values_stmt->n_values_lists == SIZE_MAX ||
	    values_stmt->n_values_lists + 1U >
		    SIZE_MAX / sizeof(*offsets)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"insert row count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	plan_count = values_stmt->n_values_lists + 1U;
	offsets = (size_t *)calloc(plan_count, sizeof(*offsets));
	if (offsets == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	insertion = NULL;
	rendered_default = NULL;
	insert_index = patch->index < stmt->n_cols ?
		patch->index : stmt->n_cols;
	anchor_index = insert_index < stmt->n_cols ?
		insert_index : stmt->n_cols - 1U;
	span_supported = 0;
	status = sqlparser_patch_insert_column_source_span(
		handle,
		stmt,
		anchor_index,
		&span,
		&span_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !span_supported) {
		goto done;
	}
	offsets[0] = insert_index < stmt->n_cols ?
		span.start : span.end;
	if (!sqlparser_patch_surface_insertion_available(
		    handle, selector, edits, offsets[0])) {
		goto done;
	}
	memset(&source_cache, 0, sizeof(source_cache));
	for (row_index = 0U;
	     row_index < values_stmt->n_values_lists;
	     row_index++) {
		source_result = sqlparser_view_insert_cell_source_span(
			handle,
			edits,
			&source_cache,
			insert_index < stmt->n_cols,
			selector->statement_index,
			row_index,
			anchor_index,
			&span.start,
			&span.end,
			out_error);
		if (source_result < 0) {
			status = out_error != NULL ? out_error->code :
				SQLPARSER_STATUS_INTERNAL_ERROR;
			goto done;
		}
		if (source_result == 0) {
			goto done;
		}
		offsets[row_index + 1U] = insert_index < stmt->n_cols ?
			span.start : span.end;
		if (offsets[row_index + 1U] <= offsets[row_index] ||
		    !sqlparser_patch_surface_insertion_available(
			    handle,
			    selector,
			    edits,
			    offsets[row_index + 1U])) {
			goto done;
		}
	}
	if (patch->name == NULL || patch->name[0] == '\0') {
		goto done;
	}
	status = sqlparser_patch_render_structured_sql(
		handle,
		patch,
		patch->default_sql,
		&rendered_default,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		goto done;
	}
	for (edit_index = 0U; edit_index < plan_count; edit_index++) {
		const char *sql_text;

		sql_text = edit_index == 0U ? patch->name : rendered_default;
		span.start = offsets[edit_index];
		span.end = offsets[edit_index];
		status = sqlparser_patch_list_insertion(
			handle,
			&span,
			1U,
			insert_index == stmt->n_cols ? 1U : 0U,
			sql_text,
			&source_offset,
			&insertion,
			out_error);
		if (status != SQLPARSER_STATUS_OK || insertion == NULL ||
		    source_offset != offsets[edit_index]) {
			goto done;
		}
		edit_supported = 0;
		status = sqlparser_surface_source_edits_insert(
			edits,
			offsets[edit_index],
			offsets[edit_index],
			insertion,
			strlen(insertion),
			&edit_supported,
			out_error);
		free(insertion);
		insertion = NULL;
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
		if (!edit_supported) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"insert column source edits overlap unexpectedly");
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			goto done;
		}
	}
	*out_supported = 1;
done:
	free(insertion);
	free(rendered_default);
	free(offsets);
	return status;
}

static sqlparser_status_t sqlparser_patch_plan_merge_insert_surface_edit(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	const sqlparser_selector_t *selector,
	sqlparser_surface_source_edits_t *edits,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_merge_insert_surface_t surface;
	char *rendered_value;
	char *target_insertion;
	char *value_insertion;
	const char *first_replacement;
	const char *replacement;
	const char *second_replacement;
	size_t edit_index;
	size_t first_end;
	size_t first_start;
	size_t replacement_length;
	size_t second_end;
	size_t second_start;
	size_t source_end;
	size_t source_start;
	sqlparser_status_t status;
	int edit_supported;
	int need_targets;
	int need_values;
	int surface_supported;

	memset(&surface, 0, sizeof(surface));
	*out_supported = 0;
	rendered_value = NULL;
	target_insertion = NULL;
	value_insertion = NULL;
	first_replacement = NULL;
	second_replacement = NULL;
	need_targets =
		selector->kind ==
			SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN ||
		selector->kind ==
			SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS;
	need_values =
		selector->kind == SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL ||
		selector->kind ==
			SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS;
	status = sqlparser_patch_merge_insert_surface(
		handle,
		selector,
		need_targets,
		need_values,
		&surface,
		&surface_supported,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !surface_supported) {
		return status;
	}
	replacement = NULL;
	replacement_length = 0U;
	source_start = 0U;
	source_end = 0U;
	if (patch->op == SQLPARSER_PATCH_REPLACE &&
	    selector->kind == SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN) {
		PgQuery__Node *target_node;
		PgQuery__ResTarget *target;
		int source_supported;

		if (patch->sql == NULL ||
		    selector->column_index >= surface.target_count ||
		    surface.target_spans == NULL ||
		    surface.clause->target_list == NULL) {
			goto done;
		}
		target_node = surface.clause->target_list[selector->column_index];
		if (target_node == NULL ||
		    target_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
		    (target = target_node->res_target) == NULL) {
			goto done;
		}
		status = sqlparser_patch_source_offset(
			handle,
			target->location,
			&source_start,
			&source_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !source_supported) {
			goto done;
		}
		source_end = sqlparser_patch_identifier_token_end(
			handle,
			source_start);
		if (!sqlparser_patch_span_content(
			    handle,
			    &surface.target_spans[selector->column_index],
			    &first_start,
			    &first_end) ||
		    source_start != first_start || source_end != first_end) {
			goto done;
		}
		replacement = patch->sql;
		replacement_length = strlen(patch->sql);
	} else if (patch->op == SQLPARSER_PATCH_REPLACE &&
		   selector->kind ==
			   SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL) {
		if (selector->column_index >= surface.value_count) {
			goto done;
		}
		status = sqlparser_patch_render_structured_sql(
			handle,
			patch,
			patch->sql,
			&rendered_value,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
		if (!sqlparser_patch_span_content(
			    handle,
			    &surface.value_spans[selector->column_index],
			    &source_start,
			    &source_end)) {
			goto done;
		}
		replacement = rendered_value;
		replacement_length = strlen(rendered_value);
	} else if (patch->op == SQLPARSER_PATCH_INSERT_COLUMN &&
		   selector->kind ==
			   SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS) {
		if (patch->name == NULL || patch->name[0] == '\0' ||
		    surface.target_count == 0U ||
		    surface.target_count != surface.value_count ||
		    patch->index > surface.target_count) {
			goto done;
		}
		status = sqlparser_patch_render_structured_sql(
			handle,
			patch,
			patch->default_sql,
			&rendered_value,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			goto done;
		}
		status = sqlparser_patch_list_insertion(
			handle,
			surface.target_spans,
			surface.target_count,
			patch->index,
			patch->name,
			&first_start,
			&target_insertion,
			out_error);
		if (status != SQLPARSER_STATUS_OK || target_insertion == NULL) {
			goto done;
		}
		status = sqlparser_patch_list_insertion(
			handle,
			surface.value_spans,
			surface.value_count,
			patch->index,
			rendered_value,
			&second_start,
			&value_insertion,
			out_error);
		if (status != SQLPARSER_STATUS_OK || value_insertion == NULL) {
			goto done;
		}
		first_end = first_start;
		second_end = second_start;
		first_replacement = target_insertion;
		second_replacement = value_insertion;
		goto add_pair;
	} else if (patch->op == SQLPARSER_PATCH_DELETE_COLUMN &&
		   selector->kind ==
			   SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS) {
		if (surface.target_count != surface.value_count ||
		    patch->index >= surface.target_count ||
		    !sqlparser_patch_merge_list_deletion(
			    handle,
			    surface.target_spans,
			    surface.target_count,
			    patch->index,
			    &first_start,
			    &first_end) ||
		    !sqlparser_patch_merge_list_deletion(
			    handle,
			    surface.value_spans,
			    surface.value_count,
			    patch->index,
			    &second_start,
			    &second_end)) {
			goto done;
		}
		first_replacement = "";
		second_replacement = "";
add_pair:
		if (!sqlparser_patch_control_surface_span_is_local(
			    handle,
			    selector->statement_index,
			    first_start,
			    first_end) ||
		    !sqlparser_patch_control_surface_span_is_local(
			    handle,
			    selector->statement_index,
			    second_start,
			    second_end)) {
			goto done;
		}
		for (edit_index = 0U; edit_index < edits->count; edit_index++) {
			const sqlparser_surface_source_edit_t *edit;

			edit = &edits->items[edit_index];
			if (sqlparser_patch_surface_ranges_overlap(
				    edit->source_start,
				    edit->source_end,
				    first_start,
				    first_end) ||
			    sqlparser_patch_surface_ranges_overlap(
				    edit->source_start,
				    edit->source_end,
				    second_start,
				    second_end)) {
				goto done;
			}
		}
		if (sqlparser_patch_surface_ranges_overlap(
			    first_start,
			    first_end,
			    second_start,
			    second_end)) {
			goto done;
		}
		edit_supported = 0;
		status = sqlparser_surface_source_edits_insert(
			edits,
			first_start,
			first_end,
			first_replacement,
			strlen(first_replacement),
			&edit_supported,
			out_error);
		if (status != SQLPARSER_STATUS_OK || !edit_supported) {
			goto done;
		}
		edit_supported = 0;
		status = sqlparser_surface_source_edits_insert(
			edits,
			second_start,
			second_end,
			second_replacement,
			strlen(second_replacement),
			&edit_supported,
			out_error);
		if (status == SQLPARSER_STATUS_OK && edit_supported) {
			*out_supported = 1;
		}
		goto done;
	} else {
		goto done;
	}
	if (!sqlparser_patch_control_surface_span_is_local(
		    handle,
		    selector->statement_index,
		    source_start,
		    source_end)) {
		goto done;
	}
	edit_supported = 0;
	status = sqlparser_patch_surface_edit_insert_deduplicated(
		edits,
		source_start,
		source_end,
		replacement,
		replacement_length,
		&edit_supported,
		out_error);
	if (status == SQLPARSER_STATUS_OK && edit_supported) {
		*out_supported = 1;
	}
done:
	free(value_insertion);
	free(target_insertion);
	free(rendered_value);
	sqlparser_patch_merge_insert_surface_clear(&surface);
	return status;
}

static sqlparser_status_t sqlparser_patch_plan_surface_edit(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	const sqlparser_selector_t *parsed_selector,
	sqlparser_surface_source_edits_t *edits,
	int *out_supported,
	sqlparser_error_t *out_error)
{
	PgQuery__SelectStmt *stmt;
	char *rendered;
	char *insertion;
	const char *replacement;
	size_t insertion_length;
	size_t replacement_length;
	size_t source_end;
	size_t source_start;
	size_t target_count;
	size_t target_index;
	sqlparser_selector_t selector;
	sqlparser_status_t status;
	int source_result;
	int merge_insert_surface;
	int span_supported;
	int surface_eligible;

	*out_supported = 0;
	if (patch == NULL || patch->selector == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (parsed_selector != NULL) {
		selector = *parsed_selector;
	} else {
		status = sqlparser_patch_parse_selector(
			patch->selector,
			&selector,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	merge_insert_surface =
		((patch->op == SQLPARSER_PATCH_REPLACE &&
		  (selector.kind ==
			   SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN ||
		   selector.kind ==
			   SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL)) ||
		 ((patch->op == SQLPARSER_PATCH_INSERT_COLUMN ||
		   patch->op == SQLPARSER_PATCH_DELETE_COLUMN) &&
		  selector.kind ==
			  SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS &&
		  !sqlparser_dialect_state_has_multi_insert(
			  handle->dialect,
			  handle->dialect_state)));
	if (merge_insert_surface) {
		return sqlparser_patch_plan_merge_insert_surface_edit(
			handle,
			patch,
			&selector,
			edits,
			out_supported,
			out_error);
	}
	if (sqlparser_dialect_is_sqlserver_compatible(handle->dialect)) {
		status = sqlparser_handle_ensure_ast(handle, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (handle->control != NULL) {
			surface_eligible =
				sqlparser_patch_sqlserver_control_surface_eligible(
					handle,
					selector.statement_index);
		} else {
			status = sqlparser_patch_sqlserver_surface_eligible(
				handle,
				&surface_eligible,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		if (!surface_eligible) {
			return SQLPARSER_STATUS_OK;
		}
	}
	if (patch->op == SQLPARSER_PATCH_INSERT_COLUMN &&
	    selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS) {
		return sqlparser_patch_plan_insert_columns_surface_edit(
			handle,
			patch,
			&selector,
			edits,
			out_supported,
			out_error);
	}
	rendered = NULL;
	insertion = NULL;
	replacement = NULL;
	replacement_length = 0U;
	source_start = 0U;
	source_end = 0U;
	span_supported = 0;
	if (patch->op == SQLPARSER_PATCH_REPLACE &&
		   selector.kind == SQLPARSER_SELECTOR_KIND_NAME &&
		   patch->sql != NULL) {
		status = sqlparser_patch_name_source_span(
			handle,
			&selector,
			patch->sql,
			&source_start,
			&source_end,
			&rendered,
			&span_supported,
			out_error);
		replacement = rendered != NULL ? rendered : patch->sql;
		replacement_length = strlen(replacement);
	} else if (patch->op == SQLPARSER_PATCH_REPLACE &&
		   selector.kind == SQLPARSER_SELECTOR_KIND_VALUE) {
		status = sqlparser_patch_render_structured_sql(
			handle,
			patch,
			patch->sql,
			&rendered,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_patch_value_source_span(
			handle,
			&selector,
			&source_start,
			&source_end,
			&span_supported,
			out_error);
		if (!sqlparser_patch_unsigned_integer_sql(rendered) &&
		    !sqlparser_patch_bind_sql(handle, rendered)) {
			span_supported = 0;
		}
		replacement = rendered;
		replacement_length = strlen(rendered);
	} else if (patch->op == SQLPARSER_PATCH_REPLACE &&
		   selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL) {
		status = sqlparser_patch_render_structured_sql(
			handle,
			patch,
			patch->sql,
			&rendered,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		source_result = sqlparser_view_insert_cell_source_span(
			handle,
			edits,
			NULL,
			0,
			selector.statement_index,
			selector.row_index,
			selector.column_index,
			&source_start,
			&source_end,
			out_error);
		if (source_result < 0) {
			free(rendered);
			return out_error != NULL ? out_error->code :
				SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		span_supported = source_result > 0;
		replacement = rendered;
		replacement_length = strlen(rendered);
	} else if (patch->op == SQLPARSER_PATCH_REPLACE &&
		   (selector.kind == SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK ||
		    selector.kind ==
			    SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN) &&
		   sqlparser_dialect_is_sqlserver_compatible(handle->dialect) &&
		   patch->sql != NULL) {
		status = sqlparser_patch_sqlserver_result_sink_source_span(
			handle,
			&selector,
			&source_start,
			&source_end,
			&span_supported,
			out_error);
		replacement = patch->sql;
		replacement_length = strlen(patch->sql);
	} else if (patch->op == SQLPARSER_PATCH_REPLACE &&
		   selector.kind == SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET &&
		   sqlparser_dialect_is_sqlserver_compatible(handle->dialect)) {
		status = sqlparser_patch_render_structured_sql(
			handle,
			patch,
			patch->sql,
			&rendered,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_patch_dml_result_target_span(
			handle,
			&selector,
			selector.column_index,
			&target_count,
			&source_start,
			&source_end,
			&span_supported,
			out_error);
		replacement = rendered;
		replacement_length = strlen(rendered);
	} else if (patch->op == SQLPARSER_PATCH_REPLACE &&
		   selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGET) {
		status = sqlparser_patch_render_structured_sql(
			handle,
			patch,
			patch->sql,
			&rendered,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_patch_select_target_span(
			handle,
			&selector,
			selector.column_index,
			NULL,
			&source_start,
			&source_end,
			&span_supported,
			out_error);
		replacement = rendered;
		replacement_length = strlen(rendered);
	} else if (patch->op == SQLPARSER_PATCH_INSERT_COLUMN &&
		   selector.kind == SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS &&
		   sqlparser_dialect_is_sqlserver_compatible(handle->dialect)) {
		sqlparser_dialect_dml_result_channel_t channel;

		status = sqlparser_patch_render_structured_sql(
			handle,
			patch,
			patch->sql,
			&rendered,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (!sqlparser_dialect_dml_result_channel_at(
			    handle,
			    selector.statement_index,
			    selector.item_index,
			    selector.row_index,
			    &channel) ||
		    (channel.kind != SQLPARSER_GRAPH_DML_RESULT_CLIENT &&
		     channel.kind != SQLPARSER_GRAPH_DML_RESULT_SINK) ||
		    channel.target_count == 0U) {
			free(rendered);
			return SQLPARSER_STATUS_OK;
		}
		target_count = channel.target_count;
		target_index = patch->index < target_count ?
			patch->index : target_count;
		status = sqlparser_patch_dml_result_target_span(
			handle,
			&selector,
			target_index < target_count ?
				target_index : target_count - 1U,
			&target_count,
			&source_start,
			&source_end,
			&span_supported,
			out_error);
		insertion_length = strlen(rendered);
		if (status == SQLPARSER_STATUS_OK && span_supported) {
			if (insertion_length > SIZE_MAX - 3U) {
				free(rendered);
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_RESOURCE_LIMIT,
					"patch SQL is too large");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			insertion = (char *)malloc(insertion_length + 3U);
			if (insertion == NULL) {
				free(rendered);
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			if (target_index < target_count) {
				memcpy(insertion, rendered, insertion_length);
				memcpy(insertion + insertion_length, ", ", 3U);
				source_end = source_start;
			} else {
				memcpy(insertion, ", ", 2U);
				memcpy(
					insertion + 2U,
					rendered,
					insertion_length + 1U);
				source_start = source_end;
			}
		}
		free(rendered);
		rendered = NULL;
		replacement = insertion;
		replacement_length = insertion != NULL ? insertion_length + 2U : 0U;
	} else if (patch->op == SQLPARSER_PATCH_INSERT_COLUMN &&
		   selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGETS) {
		status = sqlparser_patch_render_structured_sql(
			handle,
			patch,
			patch->sql,
			&rendered,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		stmt = NULL;
		status = sqlparser_get_select_stmt_by_target_list_index(
			handle,
			selector.statement_index,
			selector.item_index,
			&stmt,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(rendered);
			return status;
		}
		if (stmt->n_target_list == 0U) {
			free(rendered);
			return SQLPARSER_STATUS_OK;
		}
		target_index = patch->index < stmt->n_target_list ?
			patch->index : stmt->n_target_list;
		if (target_index < stmt->n_target_list) {
			size_t leading_space;
			size_t trailing_space;

			status = sqlparser_patch_select_target_span(
				handle,
				&selector,
				target_index,
				NULL,
				&source_start,
				&source_end,
				&span_supported,
				out_error);
			if (status == SQLPARSER_STATUS_OK && !span_supported) {
				status =
					sqlparser_patch_select_target_insert_source_offset(
						handle,
						selector.statement_index,
						stmt,
						target_index,
						&source_start,
						&span_supported,
						out_error);
				source_end = source_start;
			}
			insertion_length = strlen(rendered);
			if (status == SQLPARSER_STATUS_OK && span_supported) {
				sqlparser_patch_insert_target_spacing(
					handle,
					stmt,
					target_index,
					source_start,
					&leading_space,
					&trailing_space);
				if (insertion_length >
				    SIZE_MAX - 2U - leading_space -
					    trailing_space) {
					free(rendered);
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_RESOURCE_LIMIT,
						"patch SQL is too large");
					return SQLPARSER_STATUS_RESOURCE_LIMIT;
				}
				source_end = source_start;
				insertion = (char *)malloc(
					insertion_length + 2U + leading_space +
					trailing_space);
				if (insertion == NULL) {
					free(rendered);
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_NO_MEMORY,
						"out of memory");
					return SQLPARSER_STATUS_NO_MEMORY;
				}
				if (leading_space != 0U) {
					insertion[0] = ' ';
				}
				memcpy(
					insertion + leading_space,
					rendered,
					insertion_length);
				insertion[leading_space + insertion_length] = ',';
				if (trailing_space != 0U) {
					insertion[
						leading_space + insertion_length + 1U] =
						' ';
				}
				insertion[
					leading_space + insertion_length + 1U +
					trailing_space] = '\0';
			}
		} else {
			status = sqlparser_patch_select_target_span(
				handle,
				&selector,
				stmt->n_target_list - 1U,
				NULL,
				&source_start,
				&source_end,
				&span_supported,
				out_error);
			if (status == SQLPARSER_STATUS_OK && !span_supported) {
				status =
					sqlparser_patch_select_target_insert_source_offset(
						handle,
						selector.statement_index,
						stmt,
						stmt->n_target_list,
						&source_end,
						&span_supported,
						out_error);
			}
			insertion_length = strlen(rendered);
			if (status == SQLPARSER_STATUS_OK && span_supported) {
				if (insertion_length > SIZE_MAX - 3U) {
					free(rendered);
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_RESOURCE_LIMIT,
						"patch SQL is too large");
					return SQLPARSER_STATUS_RESOURCE_LIMIT;
				}
				source_start = source_end;
				insertion = (char *)malloc(insertion_length + 3U);
				if (insertion == NULL) {
					free(rendered);
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_NO_MEMORY,
						"out of memory");
					return SQLPARSER_STATUS_NO_MEMORY;
				}
				memcpy(insertion, ", ", 2U);
				memcpy(
					insertion + 2U,
					rendered,
					insertion_length + 1U);
			}
		}
		free(rendered);
		rendered = NULL;
		replacement = insertion;
		replacement_length =
			insertion != NULL ? strlen(insertion) : 0U;
	} else if ((patch->op == SQLPARSER_PATCH_REPLACE_ASSIGNMENT ||
		    patch->op == SQLPARSER_PATCH_INSERT_ASSIGNMENT) &&
		   ((selector.kind == SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
		     (sqlparser_dialect_is_mysql_compatible(handle->dialect) ||
		      sqlparser_dialect_is_oracle_compatible(handle->dialect) ||
		      sqlparser_dialect_is_sqlserver_compatible(
			      handle->dialect))) ||
		    (selector.kind ==
			     SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT &&
		     (sqlparser_dialect_is_oracle_or_dameng_compatible(
			      handle->dialect) ||
		      sqlparser_dialect_is_sqlserver_compatible(
			      handle->dialect) ||
		      handle->dialect == SQLPARSER_DIALECT_POSTGRESQL ||
		      handle->dialect ==
			      SQLPARSER_DIALECT_VASTBASE_POSTGRESQL))) &&
		   patch->sql != NULL && patch->sql[0] != '\0') {
		status = sqlparser_patch_assignment_source_span(
			handle,
			&selector,
			&source_start,
			&source_end,
			&span_supported,
			out_error);
		if (status == SQLPARSER_STATUS_OK && span_supported &&
		    patch->op == SQLPARSER_PATCH_INSERT_ASSIGNMENT) {
			replacement_length = strlen(patch->sql);
			if (replacement_length > SIZE_MAX - 3U) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_RESOURCE_LIMIT,
					"patch SQL is too large");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			insertion = (char *)malloc(replacement_length + 3U);
			if (insertion == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			memcpy(insertion, patch->sql, replacement_length);
			memcpy(insertion + replacement_length, ", ", 3U);
			source_end = source_start;
			replacement = insertion;
			replacement_length += 2U;
		} else {
			replacement = patch->sql;
			replacement_length = strlen(patch->sql);
		}
	} else {
		return SQLPARSER_STATUS_OK;
	}
	if (status != SQLPARSER_STATUS_OK || !span_supported ||
	    replacement == NULL) {
		free(rendered);
		free(insertion);
		return status;
	}
	if (!sqlparser_patch_control_surface_span_is_local(
		    handle,
		    selector.statement_index,
		    source_start,
		    source_end)) {
		free(rendered);
		free(insertion);
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_surface_source_edits_insert(
		edits,
		source_start,
		source_end,
		replacement,
		replacement_length,
		out_supported,
		out_error);
	free(rendered);
	free(insertion);
	return status;
}

static sqlparser_status_t sqlparser_patch_parse_insert_cell_fragment(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *sql_text,
	const char *field_name,
	PgQuery__Node **out_node,
	void **out_dialect_state,
	sqlparser_error_t *out_error)
{
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
	char *parser_sql;
	void *dialect_state;
	sqlparser_status_t status;

	if (handle == NULL || sql_text == NULL || sql_text[0] == '\0' ||
	    field_name == NULL || out_node == NULL || out_dialect_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell fragment arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node = NULL;
	*out_dialect_state = NULL;
	origins = NULL;
	parser_sql = NULL;
	dialect_state = NULL;
	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		statement_index,
		sql_text,
		field_name,
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
	status = sqlparser_parse_insert_cell_node_sql(
		parser_sql,
		&source,
		out_node,
		out_error);
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	*out_dialect_state = dialect_state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_replace_merge_insert_column(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeWhenClause *clause;
	PgQuery__Node *column_node;
	PgQuery__Node *old_node;
	sqlparser_patch_identifier_path_t path;
	sqlparser_status_t status;

	if (sql_text == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT column replacement requires sql");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	clause = NULL;
	status = sqlparser_patch_merge_insert_clause(
		handle,
		selector,
		NULL,
		&clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (clause->target_list == NULL ||
	    selector->column_index >= clause->n_target_list) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT column index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_identifier_path(
		handle,
		selector->statement_index,
		sql_text,
		1U,
		"MERGE INSERT column SQL",
		&path,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	column_node = sqlparser_patch_new_insert_column_node(
		path.parts[0],
		path.location,
		out_error);
	sqlparser_patch_identifier_path_clear(handle, &path);
	if (column_node == NULL) {
		return out_error != NULL ?
			out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	old_node = clause->target_list[selector->column_index];
	clause->target_list[selector->column_index] = column_node;
	sqlparser_free_proto_node(old_node);
	return sqlparser_handle_commit_ast(handle, out_error);
}

static sqlparser_status_t sqlparser_patch_replace_merge_insert_cell(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeWhenClause *clause;
	PgQuery__Node *old_node;
	PgQuery__Node *replacement;
	void *dialect_state;
	sqlparser_status_t status;

	clause = NULL;
	status = sqlparser_patch_merge_insert_clause(
		handle,
		selector,
		NULL,
		&clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (clause->values == NULL ||
	    selector->column_index >= clause->n_values) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT cell index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	replacement = NULL;
	dialect_state = NULL;
	status = sqlparser_patch_parse_insert_cell_fragment(
		handle,
		selector->statement_index,
		sql_text,
		"MERGE INSERT cell SQL",
		&replacement,
		&dialect_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	old_node = clause->values[selector->column_index];
	clause->values[selector->column_index] = replacement;
	sqlparser_free_proto_node(old_node);
	return sqlparser_handle_commit_ast_with_dialect_state(
		handle,
		dialect_state,
		out_error);
}

static sqlparser_status_t sqlparser_patch_replace(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	const sqlparser_selector_t *parsed_selector,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	if (patch == NULL || patch->selector == NULL || parsed_selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "replace patch requires selector");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	selector = *parsed_selector;

	switch (selector.kind) {
		case SQLPARSER_SELECTOR_KIND_RELATION:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "relation replacement bypassed its atomic patch path");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		case SQLPARSER_SELECTOR_KIND_NAME:
		{
			sqlparser_patch_identifier_path_t path;
			const char *spelling;
			size_t spelling_length;

			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "name replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			status = sqlparser_patch_parse_identifier_path(
				handle,
				selector.statement_index,
				patch->sql,
				1U,
				"name replacement SQL",
				&path,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			spelling = NULL;
			spelling_length = 0U;
			if (!sqlparser_handle_identifier_spelling(
				    handle,
				    path.location,
				    0U,
				    &spelling,
				    &spelling_length) ||
			    spelling_length == 0U) {
				sqlparser_patch_identifier_path_clear(
					handle,
					&path);
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"name replacement spelling is missing");
				return SQLPARSER_STATUS_INTERNAL_ERROR;
			}
			status = sqlparser_statement_set_name_spelling(
				handle,
				selector.statement_index,
				selector.item_index,
				path.parts[0],
				spelling,
				out_error);
			sqlparser_patch_identifier_path_clear(
				handle,
				&path);
			return status;
		}
		case SQLPARSER_SELECTOR_KIND_VALUE:
		{
			char *value_sql;

			value_sql = NULL;
			status = sqlparser_patch_render_structured_sql(handle, patch, patch->sql, &value_sql, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			status = sqlparser_patch_set_value_sql(
				handle,
				&selector,
				value_sql,
				0,
				out_error);
			free(value_sql);
			return status;
		}
		case SQLPARSER_SELECTOR_KIND_ASSIGNMENT:
		case SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT:
			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			return sqlparser_selector_set_update_assignment_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_INSERT_CELL:
		{
			char *cell_sql;

			cell_sql = NULL;
			status = sqlparser_patch_render_structured_sql(handle, patch, patch->sql, &cell_sql, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			status = sqlparser_insert_set_cell_sql_in_place(
				handle,
				selector.statement_index,
				selector.row_index,
				selector.column_index,
				cell_sql,
				out_error);
			free(cell_sql);
			return status;
		}
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN:
			return sqlparser_patch_replace_merge_insert_column(
				handle,
				&selector,
				patch->sql,
				out_error);
		case SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL:
		{
			char *cell_sql;

			cell_sql = NULL;
			status = sqlparser_patch_render_structured_sql(
				handle,
				patch,
				patch->sql,
				&cell_sql,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			status = sqlparser_patch_replace_merge_insert_cell(
				handle,
				&selector,
				cell_sql,
				out_error);
			free(cell_sql);
			return status;
		}
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGETS:
		case SQLPARSER_SELECTOR_KIND_SELECT_TARGET:
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET:
		{
			char *target_sql;

			target_sql = NULL;
			status = sqlparser_patch_render_structured_sql(handle, patch, patch->sql, &target_sql, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGETS) {
				status = sqlparser_selector_set_select_targets_sql(handle, &selector, target_sql, out_error);
			} else if (selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGET) {
				status = sqlparser_selector_set_select_target_sql(handle, &selector, target_sql, out_error);
			} else {
				status = sqlparser_dml_result_set_target_sql(handle, &selector, target_sql, out_error);
			}
			free(target_sql);
			return status;
		}
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK:
			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			return sqlparser_dml_result_set_sink_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN:
			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink column replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			return sqlparser_dml_result_set_sink_column_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_WHERE:
			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "where replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			return sqlparser_selector_set_where_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_CLAUSE:
			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "clause replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			return sqlparser_selector_set_clause_sql(handle, &selector, patch->sql, out_error);
		case SQLPARSER_SELECTOR_KIND_LITERAL:
		case SQLPARSER_SELECTOR_KIND_WHERE_LITERAL:
			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			return sqlparser_patch_set_legacy_literal_sql(
				handle,
				&selector,
				patch->sql,
				out_error);
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "selector kind cannot be replaced");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}
}

static sqlparser_status_t sqlparser_patch_append_condition(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	sqlparser_bool_operator_t bool_operator;
	sqlparser_status_t status;

	if (patch == NULL || patch->selector == NULL || patch->sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "append_condition requires selector and sql");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_WHERE && selector.kind != SQLPARSER_SELECTOR_KIND_CLAUSE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "append_condition selector must be where or clause");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	bool_operator = patch->bool_operator != 0 ?
		patch->bool_operator :
		SQLPARSER_BOOL_OPERATOR_AND;
	if (selector.kind == SQLPARSER_SELECTOR_KIND_CLAUSE) {
		return sqlparser_selector_append_clause_condition(handle, &selector, bool_operator, patch->sql, out_error);
	}
	return sqlparser_selector_append_where_sql(handle, &selector, bool_operator, patch->sql, out_error);
}

static sqlparser_status_t sqlparser_patch_insert_assignment(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	if (patch == NULL || patch->selector == NULL || patch->sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "insert_assignment requires selector and sql");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	    selector.kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "insert_assignment selector must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_selector_insert_update_assignment_sql(handle, &selector, patch->sql, out_error);
}

static sqlparser_status_t sqlparser_patch_delete_assignment(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	if (patch == NULL || patch->selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_assignment requires selector");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	    selector.kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_assignment selector must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_selector_delete_update_assignment(handle, &selector, out_error);
}

static sqlparser_status_t sqlparser_patch_replace_assignment(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	if (patch == NULL || patch->selector == NULL || patch->sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "replace_assignment requires selector and sql");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
	    selector.kind != SQLPARSER_SELECTOR_KIND_MERGE_ASSIGNMENT) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "replace_assignment selector must be assignment");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_selector_set_update_assignment_full_sql(handle, &selector, patch->sql, out_error);
}

static PgQuery__Node *sqlparser_patch_new_insert_column_node(
	const char *name,
	int32_t location,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	PgQuery__ResTarget *target;

	if (name == NULL || name[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "column name must not be NULL or empty");
		return NULL;
	}
	node = (PgQuery__Node *)calloc(1U, sizeof(*node));
	target = (PgQuery__ResTarget *)calloc(1U, sizeof(*target));
	if (node == NULL || target == NULL) {
		free(node);
		free(target);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	pg_query__node__init(node);
	pg_query__res_target__init(target);
	node->node_case = PG_QUERY__NODE__NODE_RES_TARGET;
	node->res_target = target;
	target->name = sqlparser_strdup(name);
	if (target->name == NULL) {
		sqlparser_free_proto_node(node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	sqlparser_mark_proto_generated((ProtobufCMessage *)node);
	target->location = location;
	return node;
}

typedef struct {
	PgQuery__List *row_list;
	PgQuery__Node **next_items;
	size_t next_count;
	PgQuery__Node *cell_node;
} sqlparser_patch_insert_column_row_plan_t;

typedef struct {
	PgQuery__List *row_list;
	PgQuery__Node **next_items;
	size_t next_count;
	PgQuery__Node *removed_node;
} sqlparser_patch_delete_row_plan_t;

static PgQuery__Node **sqlparser_patch_alloc_node_array(size_t count, sqlparser_error_t *out_error)
{
	PgQuery__Node **items;

	if (count == 0U) {
		return NULL;
	}
	if (count > SIZE_MAX / sizeof(*items)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "node array is too large");
		return NULL;
	}
	items = (PgQuery__Node **)calloc(count, sizeof(*items));
	if (items == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
	}
	return items;
}

static void sqlparser_patch_copy_with_insert(
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

static void sqlparser_patch_copy_with_delete(
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

static void sqlparser_patch_insert_column_plan_clear(
	sqlparser_patch_insert_column_row_plan_t *plans,
	size_t count)
{
	size_t index;

	if (plans == NULL) {
		return;
	}
	for (index = 0U; index < count; index++) {
		free(plans[index].next_items);
		sqlparser_free_proto_node(plans[index].cell_node);
	}
	free(plans);
}

static void sqlparser_patch_delete_row_plan_clear(
	sqlparser_patch_delete_row_plan_t *plans,
	size_t count)
{
	size_t index;

	if (plans == NULL) {
		return;
	}
	for (index = 0U; index < count; index++) {
		free(plans[index].next_items);
	}
	free(plans);
}

static sqlparser_status_t sqlparser_patch_insert_merge_insert_pair(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeWhenClause *clause;
	PgQuery__Node *column_node;
	PgQuery__Node *value_node;
	PgQuery__Node **next_targets;
	PgQuery__Node **next_values;
	sqlparser_patch_identifier_path_t column_path;
	char *value_sql;
	void *dialect_state;
	size_t count;
	sqlparser_status_t status;

	if (patch->name == NULL || patch->name[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT pair insertion requires a column name");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	clause = NULL;
	status = sqlparser_patch_merge_insert_clause(
		handle,
		selector,
		NULL,
		&clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	count = clause->n_target_list;
	if (count == 0U || clause->target_list == NULL ||
	    clause->n_values != count || clause->values == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"MERGE INSERT pair insertion requires matching explicit column and value lists");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (patch->index > count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT pair insertion index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (count == SIZE_MAX) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"MERGE INSERT column count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	status = sqlparser_patch_parse_identifier_path(
		handle,
		selector->statement_index,
		patch->name,
		1U,
		"MERGE INSERT column name",
		&column_path,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	column_node = sqlparser_patch_new_insert_column_node(
		column_path.parts[0],
		column_path.location,
		out_error);
	sqlparser_patch_identifier_path_clear(handle, &column_path);
	if (column_node == NULL) {
		return out_error != NULL ?
			out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	value_sql = NULL;
	status = sqlparser_patch_render_structured_sql(
		handle,
		patch,
		patch->default_sql,
		&value_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(column_node);
		return status;
	}
	value_node = NULL;
	dialect_state = NULL;
	status = sqlparser_patch_parse_insert_cell_fragment(
		handle,
		selector->statement_index,
		value_sql,
		"MERGE INSERT value SQL",
		&value_node,
		&dialect_state,
		out_error);
	free(value_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(column_node);
		return status;
	}
	next_targets = sqlparser_patch_alloc_node_array(count + 1U, out_error);
	if (next_targets == NULL) {
		sqlparser_free_proto_node(value_node);
		sqlparser_free_proto_node(column_node);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return out_error != NULL ?
			out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	next_values = sqlparser_patch_alloc_node_array(count + 1U, out_error);
	if (next_values == NULL) {
		free(next_targets);
		sqlparser_free_proto_node(value_node);
		sqlparser_free_proto_node(column_node);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return out_error != NULL ?
			out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	sqlparser_patch_copy_with_insert(
		next_targets,
		clause->target_list,
		count,
		patch->index,
		column_node);
	sqlparser_patch_copy_with_insert(
		next_values,
		clause->values,
		count,
		patch->index,
		value_node);
	free(clause->target_list);
	free(clause->values);
	clause->target_list = next_targets;
	clause->values = next_values;
	clause->n_target_list = count + 1U;
	clause->n_values = count + 1U;
	return sqlparser_handle_commit_ast_with_dialect_state(
		handle,
		dialect_state,
		out_error);
}

static sqlparser_status_t sqlparser_patch_delete_merge_insert_pair(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	const sqlparser_selector_t *selector,
	sqlparser_error_t *out_error)
{
	PgQuery__MergeWhenClause *clause;
	PgQuery__Node *removed_target;
	PgQuery__Node *removed_value;
	PgQuery__Node **next_targets;
	PgQuery__Node **next_values;
	size_t count;
	sqlparser_status_t status;

	clause = NULL;
	status = sqlparser_patch_merge_insert_clause(
		handle,
		selector,
		NULL,
		&clause,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	count = clause->n_target_list;
	if (count == 0U || clause->target_list == NULL ||
	    clause->n_values != count || clause->values == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"MERGE INSERT pair deletion requires matching explicit column and value lists");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (patch->index >= count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE INSERT pair deletion index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (count <= 1U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"cannot delete the last MERGE INSERT column and value");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	next_targets = sqlparser_patch_alloc_node_array(count - 1U, out_error);
	if (next_targets == NULL) {
		return out_error != NULL ?
			out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	next_values = sqlparser_patch_alloc_node_array(count - 1U, out_error);
	if (next_values == NULL) {
		free(next_targets);
		return out_error != NULL ?
			out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	sqlparser_patch_copy_with_delete(
		next_targets,
		clause->target_list,
		count,
		patch->index);
	sqlparser_patch_copy_with_delete(
		next_values,
		clause->values,
		count,
		patch->index);
	removed_target = clause->target_list[patch->index];
	removed_value = clause->values[patch->index];
	free(clause->target_list);
	free(clause->values);
	clause->target_list = next_targets;
	clause->values = next_values;
	clause->n_target_list = count - 1U;
	clause->n_values = count - 1U;
	sqlparser_free_proto_node(removed_target);
	sqlparser_free_proto_node(removed_value);
	return sqlparser_handle_commit_ast(handle, out_error);
}

static sqlparser_status_t sqlparser_patch_insert_column(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	PgQuery__InsertStmt *stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node *column_node;
	PgQuery__Node *default_node;
	PgQuery__Node **next_cols;
	PgQuery__Node **source_nodes;
	PgQuery__Node **clone_nodes;
	PgQuery__List source_list;
	PgQuery__List clone_list;
	sqlparser_patch_insert_column_row_plan_t *plans;
	sqlparser_status_t status;
	char *parser_default_sql;
	char *rendered_default_sql;
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
	void *dialect_state;
	size_t row_index;
	size_t insert_index;
	size_t row_count;

	if (patch == NULL || patch->selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "insert_column requires selector");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGETS) {
		char *target_sql;

		target_sql = NULL;
		status = sqlparser_patch_render_structured_sql(handle, patch, patch->sql, &target_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_select_insert_target_sql_in_place(
			handle,
			selector.statement_index,
			selector.item_index,
			patch->index,
			target_sql,
			out_error);
		free(target_sql);
		return status;
	}
	if (selector.kind == SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS) {
		char *target_sql;

		target_sql = NULL;
		status = sqlparser_patch_render_structured_sql(handle, patch, patch->sql, &target_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_dml_result_insert_target_sql(
			handle, &selector, patch->index, target_sql, out_error);
		free(target_sql);
		return status;
	}
	if (selector.kind == SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMNS) {
		if (patch->name == NULL || patch->name[0] == '\0') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink column insertion requires name");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		return sqlparser_dml_result_insert_sink_column_sql(
			handle, &selector, patch->index, patch->name, out_error);
	}
	if (selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS) {
		char *branch_cell_sql;
		sqlparser_patch_identifier_path_t column_path;

		if (!sqlparser_dialect_state_has_multi_insert(
			    handle->dialect,
			    handle->dialect_state)) {
			return sqlparser_patch_insert_merge_insert_pair(
				handle,
				patch,
				&selector,
				out_error);
		}
		if (selector.row_index != 0U) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"multi-insert branch selector must not include a DML index");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}

		if (patch->name == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "insert branch column requires name");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		status = sqlparser_patch_parse_identifier_path(
			handle,
			selector.statement_index,
			patch->name,
			1U,
			"insert branch column name",
			&column_path,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		sqlparser_patch_identifier_path_clear(handle, &column_path);
		branch_cell_sql = NULL;
		status = sqlparser_patch_render_structured_sql(handle, patch, patch->default_sql, &branch_cell_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_dialect_multi_insert_insert_column_sql(
			handle,
			selector.statement_index,
			selector.item_index,
			patch->index,
			patch->name,
			branch_cell_sql,
			out_error);
		free(branch_cell_sql);
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "insert_column selector is not a column list");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (patch->name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "insert_column requires name");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	{
		sqlparser_patch_identifier_path_t column_path;

		status = sqlparser_patch_parse_identifier_path(
			handle,
			selector.statement_index,
			patch->name,
			1U,
			"insert column name",
			&column_path,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		column_node = sqlparser_patch_new_insert_column_node(
			column_path.parts[0],
			column_path.location,
			out_error);
		sqlparser_patch_identifier_path_clear(handle, &column_path);
		if (column_node == NULL) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	status = sqlparser_get_insert_stmt(handle, selector.statement_index, &stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(column_node);
		return status;
	}

	default_node = NULL;
	next_cols = NULL;
	source_nodes = NULL;
	clone_nodes = NULL;
	plans = NULL;
	parser_default_sql = NULL;
	rendered_default_sql = NULL;
	origins = NULL;
	dialect_state = NULL;
	insert_index = patch->index > stmt->n_cols ? stmt->n_cols : patch->index;
	if (stmt->n_cols == SIZE_MAX) {
		sqlparser_free_proto_node(column_node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "insert column count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	if (sqlparser_insert_source_from_stmt(stmt) == SQLPARSER_INSERT_SOURCE_QUERY) {
		next_cols = sqlparser_patch_alloc_node_array(stmt->n_cols + 1U, out_error);
		if (next_cols == NULL) {
			sqlparser_free_proto_node(column_node);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		sqlparser_patch_copy_with_insert(next_cols, stmt->cols, stmt->n_cols, insert_index, column_node);
		free(stmt->cols);
		stmt->cols = next_cols;
		stmt->n_cols++;
		column_node = NULL;
		next_cols = NULL;
		return sqlparser_handle_commit_ast(handle, out_error);
	}
	status = sqlparser_get_insert_values_stmt(handle, selector.statement_index, &stmt, &values_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(column_node);
		return status;
	}
	row_count = values_stmt->n_values_lists;
	status = sqlparser_patch_render_structured_sql(handle, patch, patch->default_sql, &rendered_default_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(column_node);
		return status;
	}
	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		selector.statement_index,
		rendered_default_sql,
		"insert column default SQL",
		&parser_default_sql,
		&dialect_state,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(rendered_default_sql);
		sqlparser_free_proto_node(column_node);
		return status;
	}
	memset(&source, 0, sizeof(source));
	source.public_sql = rendered_default_sql;
	source.origins = origins;
	source.dialect = handle->dialect;
	source.spelling_handle = handle;
	source.candidate_dialect_state = dialect_state;
	source.statement_index = selector.statement_index;
	status = sqlparser_parse_insert_cell_node_sql(
		parser_default_sql,
		&source,
		&default_node,
		out_error);
	sqlparser_identifier_origin_map_destroy(origins);
	free(rendered_default_sql);
	rendered_default_sql = NULL;
	free(parser_default_sql);
	parser_default_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(column_node);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	next_cols = sqlparser_patch_alloc_node_array(stmt->n_cols + 1U, out_error);
	if (next_cols == NULL) {
		sqlparser_free_proto_node(column_node);
		sqlparser_free_proto_node(default_node);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	sqlparser_patch_copy_with_insert(next_cols, stmt->cols, stmt->n_cols, insert_index, column_node);

	plans = (sqlparser_patch_insert_column_row_plan_t *)calloc(row_count > 0U ? row_count : 1U, sizeof(*plans));
	if (plans == NULL) {
		free(next_cols);
		sqlparser_free_proto_node(column_node);
		sqlparser_free_proto_node(default_node);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	for (row_index = 0U; row_index < row_count; row_index++) {
		PgQuery__List *row_list;
		size_t cell_index;

		if (values_stmt->values_lists[row_index] == NULL ||
		    values_stmt->values_lists[row_index]->node_case != PG_QUERY__NODE__NODE_LIST ||
		    values_stmt->values_lists[row_index]->list == NULL) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			sqlparser_handle_discard_dialect_state(handle, dialect_state);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "insert row node is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		row_list = values_stmt->values_lists[row_index]->list;
		cell_index = patch->index > row_list->n_items ? row_list->n_items : patch->index;
		if (row_list->n_items == SIZE_MAX) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			sqlparser_handle_discard_dialect_state(handle, dialect_state);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "insert row cell count is too large");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		status = sqlparser_clone_proto_node(default_node, &plans[row_index].cell_node, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			sqlparser_handle_discard_dialect_state(handle, dialect_state);
			return status;
		}
		plans[row_index].next_items = sqlparser_patch_alloc_node_array(row_list->n_items + 1U, out_error);
		if (plans[row_index].next_items == NULL) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			sqlparser_handle_discard_dialect_state(handle, dialect_state);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		sqlparser_patch_copy_with_insert(
			plans[row_index].next_items,
			row_list->items,
			row_list->n_items,
			cell_index,
			plans[row_index].cell_node);
		plans[row_index].row_list = row_list;
		plans[row_index].next_count = row_list->n_items + 1U;
	}

	if (row_count > 0U &&
	    handle->dialect_ops != NULL &&
	    handle->dialect_ops->clone_ast_state != NULL) {
		source_nodes = sqlparser_patch_alloc_node_array(row_count, out_error);
		clone_nodes = sqlparser_patch_alloc_node_array(row_count, out_error);
		if (source_nodes == NULL || clone_nodes == NULL) {
			free(source_nodes);
			free(clone_nodes);
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			sqlparser_handle_discard_dialect_state(handle, dialect_state);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		for (row_index = 0U; row_index < row_count; row_index++) {
			source_nodes[row_index] = default_node;
			clone_nodes[row_index] = plans[row_index].cell_node;
		}
		pg_query__list__init(&source_list);
		source_list.n_items = row_count;
		source_list.items = source_nodes;
		pg_query__list__init(&clone_list);
		clone_list.n_items = row_count;
		clone_list.items = clone_nodes;
		status = handle->dialect_ops->clone_ast_state(
			dialect_state,
			selector.statement_index,
			(ProtobufCMessage *)&source_list,
			(ProtobufCMessage *)&clone_list,
			out_error);
		free(source_nodes);
		free(clone_nodes);
		source_nodes = NULL;
		clone_nodes = NULL;
		if (status != SQLPARSER_STATUS_OK) {
			free(next_cols);
			sqlparser_free_proto_node(column_node);
			sqlparser_free_proto_node(default_node);
			sqlparser_patch_insert_column_plan_clear(plans, row_count);
			sqlparser_handle_discard_dialect_state(handle, dialect_state);
			return status;
		}
	}

	free(stmt->cols);
	stmt->cols = next_cols;
	stmt->n_cols++;
	next_cols = NULL;
	column_node = NULL;
	for (row_index = 0U; row_index < row_count; row_index++) {
		free(plans[row_index].row_list->items);
		plans[row_index].row_list->items = plans[row_index].next_items;
		plans[row_index].row_list->n_items = plans[row_index].next_count;
		plans[row_index].next_items = NULL;
		plans[row_index].cell_node = NULL;
	}

	sqlparser_free_proto_node(default_node);
	sqlparser_patch_insert_column_plan_clear(plans, row_count);
	return sqlparser_handle_commit_ast_with_dialect_state(
		handle, dialect_state, out_error);
}

static sqlparser_status_t sqlparser_patch_delete_column(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	PgQuery__InsertStmt *stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node **next_cols;
	PgQuery__Node *removed_column;
	sqlparser_patch_delete_row_plan_t *plans;
	sqlparser_status_t status;
	size_t row_index;
	size_t row_count;

	if (patch == NULL || patch->selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column requires selector");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind == SQLPARSER_SELECTOR_KIND_SELECT_TARGETS) {
		return sqlparser_select_delete_target(
			handle,
			selector.statement_index,
			selector.item_index,
			patch->index,
			out_error);
	}
	if (selector.kind == SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS) {
		return sqlparser_dml_result_delete_target(
			handle, &selector, patch->index, out_error);
	}
	if (selector.kind == SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMNS) {
		return sqlparser_dml_result_delete_sink_column(
			handle, &selector, patch->index, out_error);
	}
	if (selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS &&
	    !sqlparser_dialect_state_has_multi_insert(
		    handle->dialect,
		    handle->dialect_state)) {
		return sqlparser_patch_delete_merge_insert_pair(
			handle,
			patch,
			&selector,
			out_error);
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column selector is not a column list");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_get_insert_stmt(handle, selector.statement_index, &stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	next_cols = NULL;
	removed_column = NULL;
	plans = NULL;
	if (sqlparser_insert_source_from_stmt(stmt) == SQLPARSER_INSERT_SOURCE_QUERY) {
		if (stmt->n_cols == 0U || patch->index >= stmt->n_cols) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column index is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		next_cols = sqlparser_patch_alloc_node_array(stmt->n_cols - 1U, out_error);
		if (stmt->n_cols > 1U && next_cols == NULL) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		sqlparser_patch_copy_with_delete(next_cols, stmt->cols, stmt->n_cols, patch->index);
		removed_column = stmt->cols[patch->index];
		free(stmt->cols);
		stmt->cols = next_cols;
		stmt->n_cols--;
		sqlparser_free_proto_node(removed_column);
		return sqlparser_handle_commit_ast(handle, out_error);
	}
	status = sqlparser_get_insert_values_stmt(handle, selector.statement_index, &stmt, &values_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	row_count = values_stmt->n_values_lists;
	if (stmt->n_cols > 0U) {
		if (patch->index >= stmt->n_cols) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column index is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		next_cols = sqlparser_patch_alloc_node_array(stmt->n_cols - 1U, out_error);
		if (stmt->n_cols > 1U && next_cols == NULL) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		sqlparser_patch_copy_with_delete(next_cols, stmt->cols, stmt->n_cols, patch->index);
		removed_column = stmt->cols[patch->index];
	}
	plans = (sqlparser_patch_delete_row_plan_t *)calloc(row_count > 0U ? row_count : 1U, sizeof(*plans));
	if (plans == NULL) {
		free(next_cols);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	for (row_index = 0U; row_index < row_count; row_index++) {
		PgQuery__List *row_list;

		if (values_stmt->values_lists[row_index] == NULL ||
		    values_stmt->values_lists[row_index]->node_case != PG_QUERY__NODE__NODE_LIST ||
		    values_stmt->values_lists[row_index]->list == NULL) {
			free(next_cols);
			sqlparser_patch_delete_row_plan_clear(plans, row_count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "insert row node is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		row_list = values_stmt->values_lists[row_index]->list;
		if (patch->index >= row_list->n_items) {
			free(next_cols);
			sqlparser_patch_delete_row_plan_clear(plans, row_count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_column index is out of range");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		if (row_list->n_items <= 1U) {
			free(next_cols);
			sqlparser_patch_delete_row_plan_clear(plans, row_count);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "cannot delete the last insert cell");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		plans[row_index].next_items = sqlparser_patch_alloc_node_array(row_list->n_items - 1U, out_error);
		if (plans[row_index].next_items == NULL) {
			free(next_cols);
			sqlparser_patch_delete_row_plan_clear(plans, row_count);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		sqlparser_patch_copy_with_delete(plans[row_index].next_items, row_list->items, row_list->n_items, patch->index);
		plans[row_index].row_list = row_list;
		plans[row_index].next_count = row_list->n_items - 1U;
		plans[row_index].removed_node = row_list->items[patch->index];
	}
	if (stmt->n_cols > 0U) {
		free(stmt->cols);
		stmt->cols = next_cols;
		stmt->n_cols--;
		next_cols = NULL;
		sqlparser_free_proto_node(removed_column);
	}
	for (row_index = 0U; row_index < row_count; row_index++) {
		free(plans[row_index].row_list->items);
		plans[row_index].row_list->items = plans[row_index].next_items;
		plans[row_index].row_list->n_items = plans[row_index].next_count;
		plans[row_index].next_items = NULL;
		sqlparser_free_proto_node(plans[row_index].removed_node);
		plans[row_index].removed_node = NULL;
	}
	sqlparser_patch_delete_row_plan_clear(plans, row_count);
	return sqlparser_handle_commit_ast(handle, out_error);
}

static sqlparser_status_t sqlparser_patch_delete_row(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	PgQuery__InsertStmt *stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node **next_rows;
	PgQuery__Node *removed_row;
	sqlparser_status_t status;
	size_t row_count;

	if (patch == NULL || patch->selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_row requires selector");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector.kind != SQLPARSER_SELECTOR_KIND_INSERT_ROW) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_row selector must be insert_row");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_get_insert_values_stmt(handle, selector.statement_index, &stmt, &values_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	(void)stmt;
	row_count = values_stmt->n_values_lists;
	if (selector.row_index >= row_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "delete_row index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (row_count <= 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "cannot delete the last insert row");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	next_rows = sqlparser_patch_alloc_node_array(row_count - 1U, out_error);
	if (next_rows == NULL) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	sqlparser_patch_copy_with_delete(next_rows, values_stmt->values_lists, row_count, selector.row_index);
	removed_row = values_stmt->values_lists[selector.row_index];
	free(values_stmt->values_lists);
	values_stmt->values_lists = next_rows;
	values_stmt->n_values_lists--;
	sqlparser_free_proto_node(removed_row);
	return sqlparser_handle_commit_ast(handle, out_error);
}

static sqlparser_status_t sqlparser_patch_materialize_surface_edits(
	sqlparser_handle_t *handle,
	sqlparser_surface_source_edits_t *edits,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *replacement;
	sqlparser_parse_options_t options;
	char *patched_sql;
	sqlparser_status_t status;
	int saved_surface_complete;

	if (handle == NULL || edits == NULL || edits->count == 0U ||
	    handle->surface_source_edits.items != NULL ||
	    handle->surface_source_edits.count != 0U ||
	    handle->surface_source_edits.capacity != 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"surface edit materialization state is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	replacement = NULL;
	patched_sql = NULL;
	saved_surface_complete = handle->surface_source_complete;
	handle->surface_source_edits = *edits;
	memset(edits, 0, sizeof(*edits));
	handle->surface_source_complete = 1;
	status = sqlparser_restore_source_envelope(
		handle,
		&patched_sql,
		out_error);
	*edits = handle->surface_source_edits;
	memset(
		&handle->surface_source_edits,
		0,
		sizeof(handle->surface_source_edits));
	handle->surface_source_complete = saved_surface_complete;
	if (status != SQLPARSER_STATUS_OK) {
		free(patched_sql);
		return status;
	}

	sqlparser_parse_options_default(&options);
	options.dialect = handle->dialect;
	options.limits = handle->limits;
	status = sqlparser_parse_with_options(
		patched_sql,
		&options,
		&replacement,
		out_error);
	free(patched_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(replacement);
		return status;
	}
	replacement->generation = handle->generation + 1UL;
	replacement->surface_source_complete = 1;
	sqlparser_surface_source_edits_release(edits);
	sqlparser_handle_replace_contents(handle, replacement);
	sqlparser_handle_destroy(replacement);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_apply_patch_in_place(
	sqlparser_handle_t *handle,
	const sqlparser_patch_list_t *patches,
	sqlparser_surface_source_edits_t *surface_edits,
	int *in_out_surface_complete,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	for (index = 0U; index < patches->count; index++) {
		const sqlparser_patch_t *patch;
		const sqlparser_selector_t *planned_selector;
		sqlparser_selector_t replace_selector;
		sqlparser_selector_t surface_selector;
		size_t prior_surface_count;
		int structural_insert_surface;
		int surface_supported;

		patch = &patches->items[index];
		planned_selector = NULL;
		surface_supported = 0;
		if (patch->op == SQLPARSER_PATCH_REPLACE) {
			status = sqlparser_patch_parse_selector(
				patch->selector,
				&replace_selector,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			planned_selector = &replace_selector;
			if (replace_selector.kind ==
			    SQLPARSER_SELECTOR_KIND_RELATION) {
				if (patch->sql == NULL) {
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_INVALID_ARGUMENT,
						"relation replacement requires sql");
					return SQLPARSER_STATUS_INVALID_ARGUMENT;
				}
				status = sqlparser_patch_set_relation_sql(
					handle,
					&replace_selector,
					patch->sql,
					surface_edits,
					in_out_surface_complete,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				continue;
			}
		}
		if (*in_out_surface_complete &&
		    patch->source_selector != NULL) {
			if (surface_edits->count > 0U) {
				status = sqlparser_patch_materialize_surface_edits(
					handle,
					surface_edits,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
			}
			handle->surface_source_complete = 1;
		}
		if (*in_out_surface_complete) {
			prior_surface_count = surface_edits->count;
			status = sqlparser_patch_plan_surface_edit(
				handle,
				patch,
				planned_selector,
				surface_edits,
				&surface_supported,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			structural_insert_surface = 0;
			if (!surface_supported && prior_surface_count > 0U) {
				if (planned_selector != NULL) {
					structural_insert_surface =
						planned_selector->kind ==
							SQLPARSER_SELECTOR_KIND_MERGE_INSERT_COLUMN ||
						planned_selector->kind ==
							SQLPARSER_SELECTOR_KIND_MERGE_INSERT_CELL;
				} else if (
					patch->op == SQLPARSER_PATCH_INSERT_COLUMN ||
					patch->op == SQLPARSER_PATCH_DELETE_COLUMN) {
					status = sqlparser_patch_parse_selector(
						patch->selector,
						&surface_selector,
						out_error);
					if (status != SQLPARSER_STATUS_OK) {
						return status;
					}
					structural_insert_surface =
						(patch->op ==
							 SQLPARSER_PATCH_INSERT_COLUMN &&
						 surface_selector.kind ==
							 SQLPARSER_SELECTOR_KIND_INSERT_COLUMNS) ||
						(surface_selector.kind ==
							SQLPARSER_SELECTOR_KIND_INSERT_BRANCH_COLUMNS &&
						!sqlparser_dialect_state_has_multi_insert(
							handle->dialect,
							handle->dialect_state));
				}
			}
			if (!surface_supported && structural_insert_surface) {
				status = sqlparser_patch_materialize_surface_edits(
					handle,
					surface_edits,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				status = sqlparser_patch_plan_surface_edit(
					handle,
					patch,
					planned_selector,
					surface_edits,
					&surface_supported,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
			}
			if (!surface_supported) {
				sqlparser_surface_source_edits_release(
					surface_edits);
				*in_out_surface_complete = 0;
			}
		}
		if (*in_out_surface_complete && surface_supported &&
		    patch->op == SQLPARSER_PATCH_REPLACE &&
		      planned_selector != NULL &&
		      planned_selector->kind ==
			      SQLPARSER_SELECTOR_KIND_INSERT_CELL &&
		      sqlparser_dialect_is_oracle_compatible(handle->dialect) &&
		      sqlparser_dialect_state_has_multi_insert(
			      handle->dialect,
			      handle->dialect_state)) {
			status = sqlparser_patch_materialize_surface_edits(
				handle,
				surface_edits,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			continue;
		}
		switch (patch->op) {
			case SQLPARSER_PATCH_REPLACE:
				status = sqlparser_patch_replace(
					handle,
					patch,
					&replace_selector,
					out_error);
				break;
			case SQLPARSER_PATCH_INSERT_COLUMN:
				status = sqlparser_patch_insert_column(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_DELETE_COLUMN:
				status = sqlparser_patch_delete_column(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_DELETE_ROW:
				status = sqlparser_patch_delete_row(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_APPEND_CONDITION:
				status = sqlparser_patch_append_condition(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_INSERT_ASSIGNMENT:
				status = sqlparser_patch_insert_assignment(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_DELETE_ASSIGNMENT:
				status = sqlparser_patch_delete_assignment(handle, patch, out_error);
				break;
			case SQLPARSER_PATCH_REPLACE_ASSIGNMENT:
				status = sqlparser_patch_replace_assignment(handle, patch, out_error);
				break;
			default:
				status = SQLPARSER_STATUS_UNSUPPORTED;
				sqlparser_error_set_message(out_error, status, "patch operation is not supported");
				break;
		}
		if (patch->source_selector != NULL) {
			handle->surface_source_complete = 0;
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (patch->op == SQLPARSER_PATCH_REPLACE_ASSIGNMENT &&
		    sqlparser_dialect_is_mysql_compatible(handle->dialect)) {
			sqlparser_selector_t assignment_selector;

			status = sqlparser_patch_parse_selector(
				patch->selector,
				&assignment_selector,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (assignment_selector.kind ==
				    SQLPARSER_SELECTOR_KIND_ASSIGNMENT &&
			    sqlparser_mysql_statement_update_join_reversed(
				    handle->dialect_state,
				    assignment_selector.statement_index)) {
				sqlparser_surface_source_edits_release(
					surface_edits);
				*in_out_surface_complete = 0;
			}
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_candidate_is_noop(
	const sqlparser_handle_t *handle,
	sqlparser_handle_t *candidate,
	int *out_noop,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *baseline;
	char *baseline_sql;
	char *candidate_sql;
	sqlparser_status_t status;

	*out_noop = 0;
	if (handle->parse_tree.len != candidate->parse_tree.len ||
	    memcmp(
		    handle->parse_tree.data,
		    candidate->parse_tree.data,
		    handle->parse_tree.len) != 0) {
		return SQLPARSER_STATUS_OK;
	}

	baseline = NULL;
	baseline_sql = NULL;
	candidate_sql = NULL;
	status = sqlparser_handle_clone(handle, &baseline, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	baseline->generation = candidate->generation;
	if (candidate->surface_source_complete &&
	    (handle->generation == 0UL ||
	     handle->surface_source_complete)) {
		baseline->surface_source_complete = 1;
	}
	sqlparser_handle_invalidate_derived(baseline);
	status = sqlparser_deparse(baseline, &baseline_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_deparse(
			candidate,
			&candidate_sql,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		*out_noop =
			baseline_sql != NULL &&
			candidate_sql != NULL &&
			strcmp(baseline_sql, candidate_sql) == 0;
	}
	sqlparser_string_free(candidate_sql);
	sqlparser_string_free(baseline_sql);
	sqlparser_handle_destroy(baseline);
	return status;
}

sqlparser_status_t sqlparser_apply_patch(
	sqlparser_handle_t *handle,
	const sqlparser_patch_list_t *patches,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *candidate;
	sqlparser_surface_source_edits_t surface_edits;
	sqlparser_status_t status;
	unsigned long original_generation;
	int noop;
	int surface_complete;

	sqlparser_error_clear(out_error);
	if (handle == NULL || patches == NULL ||
	    (patches->count > 0U && patches->items == NULL)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle and patches must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (patches->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	candidate = NULL;
	memset(&surface_edits, 0, sizeof(surface_edits));
	original_generation = handle->generation;
	status = sqlparser_handle_clone(
		handle,
		&candidate,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	surface_complete =
		handle->generation == 0UL || handle->surface_source_complete;
	if (surface_complete) {
		surface_edits = candidate->surface_source_edits;
		memset(
			&candidate->surface_source_edits,
			0,
			sizeof(candidate->surface_source_edits));
	} else {
		sqlparser_surface_source_edits_release(
			&candidate->surface_source_edits);
	}
	candidate->surface_source_complete = 0;
	status = sqlparser_apply_patch_in_place(
		candidate,
		patches,
		&surface_edits,
		&surface_complete,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_surface_source_edits_release(&surface_edits);
		sqlparser_handle_destroy(candidate);
		return status;
	}
	if (candidate->generation == original_generation) {
		sqlparser_surface_source_edits_release(&surface_edits);
		sqlparser_handle_destroy(candidate);
		sqlparser_error_clear(out_error);
		return SQLPARSER_STATUS_OK;
	}
	if (surface_complete) {
		candidate->surface_source_edits = surface_edits;
		memset(&surface_edits, 0, sizeof(surface_edits));
		candidate->surface_source_complete = 1;
	} else {
		sqlparser_surface_source_edits_release(&surface_edits);
	}
	status = sqlparser_patch_candidate_is_noop(
		handle,
		candidate,
		&noop,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(candidate);
		return status;
	}
	if (noop) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_clear(out_error);
		return SQLPARSER_STATUS_OK;
	}

	candidate->generation = original_generation + 1UL;
	sqlparser_handle_invalidate_derived(candidate);
	sqlparser_handle_replace_contents(handle, candidate);
	sqlparser_handle_destroy(candidate);
	sqlparser_error_clear(out_error);
	return SQLPARSER_STATUS_OK;
}
