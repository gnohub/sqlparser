#include <string.h>

#include "sqlparser_dialect_dml_result_internal.h"
#include "sqlparser_dialect_sqlserver_internal.h"

static const sqlparser_sqlserver_output_state_t *sqlparser_dialect_sqlserver_output_state(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	if (out_local_statement_index != NULL) {
		*out_local_statement_index = statement_index;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		return NULL;
	}
	return sqlparser_sqlserver_state_output(
		state, statement_index, out_local_statement_index);
}

static sqlparser_sqlserver_output_state_t *sqlparser_dialect_sqlserver_output_state_mutable(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	if (out_local_statement_index != NULL) {
		*out_local_statement_index = statement_index;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		return NULL;
	}
	return sqlparser_sqlserver_state_output_mutable(
		state, statement_index, out_local_statement_index);
}

static unsigned int sqlparser_dialect_postgresql_merge_target_reference_kinds(
	const PgQuery__MergeStmt *stmt)
{
	unsigned int kinds;
	size_t index;

	kinds = SQLPARSER_DIALECT_DML_TARGET_REFERENCE_NONE;
	if (stmt == NULL || stmt->merge_when_clauses == NULL) {
		return kinds;
	}
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *when_node;
		PgQuery__MergeWhenClause *when_clause;

		when_node = stmt->merge_when_clauses[index];
		when_clause = when_node != NULL &&
			when_node->node_case == PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ?
			when_node->merge_when_clause : NULL;
		if (when_clause == NULL) {
			continue;
		}
		switch (when_clause->command_type) {
			case PG_QUERY__CMD_TYPE__CMD_INSERT:
			case PG_QUERY__CMD_TYPE__CMD_UPDATE:
				kinds |= SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER;
				break;
			case PG_QUERY__CMD_TYPE__CMD_DELETE:
				kinds |= SQLPARSER_DIALECT_DML_TARGET_REFERENCE_BEFORE;
				break;
			default:
				break;
		}
	}
	return kinds;
}

static int sqlparser_dialect_postgresql_dml_node(
	PgQuery__Node *node,
	sqlparser_dialect_dml_result_dml_t *out_dml)
{
	if (node == NULL || out_dml == NULL) {
		return 0;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (node->insert_stmt != NULL) {
				out_dml->kind = SQLPARSER_GRAPH_DML_INSERT;
				out_dml->target_count = node->insert_stmt->n_returning_list;
				out_dml->message = (ProtobufCMessage *)node->insert_stmt;
				out_dml->target_reference_kinds =
					SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER;
			}
			break;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			if (node->update_stmt != NULL) {
				out_dml->kind = SQLPARSER_GRAPH_DML_UPDATE;
				out_dml->target_count = node->update_stmt->n_returning_list;
				out_dml->message = (ProtobufCMessage *)node->update_stmt;
				out_dml->target_reference_kinds =
					SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER;
			}
			break;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			if (node->delete_stmt != NULL) {
				out_dml->kind = SQLPARSER_GRAPH_DML_DELETE;
				out_dml->target_count = node->delete_stmt->n_returning_list;
				out_dml->message = (ProtobufCMessage *)node->delete_stmt;
				out_dml->target_reference_kinds =
					SQLPARSER_DIALECT_DML_TARGET_REFERENCE_BEFORE;
			}
			break;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			if (node->merge_stmt != NULL) {
				out_dml->kind = SQLPARSER_GRAPH_DML_MERGE;
				out_dml->target_count = node->merge_stmt->n_returning_list;
				out_dml->message = (ProtobufCMessage *)node->merge_stmt;
				out_dml->target_reference_kinds =
					sqlparser_dialect_postgresql_merge_target_reference_kinds(
						node->merge_stmt);
			}
			break;
		default:
			break;
	}
	if (out_dml->message == NULL) {
		return 0;
	}
	out_dml->channel_count = out_dml->target_count > 0U ? 1U : 0U;
	return 1;
}

static PgQuery__WithClause *sqlparser_dialect_postgresql_with_clause(
	PgQuery__Node *statement)
{
	if (statement == NULL) {
		return NULL;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return statement->select_stmt != NULL ?
				statement->select_stmt->with_clause : NULL;
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			return statement->insert_stmt != NULL ?
				statement->insert_stmt->with_clause : NULL;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return statement->update_stmt != NULL ?
				statement->update_stmt->with_clause : NULL;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return statement->delete_stmt != NULL ?
				statement->delete_stmt->with_clause : NULL;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return statement->merge_stmt != NULL ?
				statement->merge_stmt->with_clause : NULL;
		default:
			return NULL;
	}
}

static PgQuery__Node *sqlparser_dialect_postgresql_statement(
	const sqlparser_handle_t *handle,
	size_t statement_index)
{
	if (handle == NULL ||
	    !sqlparser_dialect_uses_postgresql_placeholders(handle->dialect) ||
	    handle->ast == NULL || handle->ast->stmts == NULL ||
	    statement_index >= handle->ast->n_stmts ||
	    handle->ast->stmts[statement_index] == NULL) {
		return NULL;
	}
	return handle->ast->stmts[statement_index]->stmt;
}

typedef struct {
	sqlparser_dialect_dml_result_visit_fn visitor;
	void *context;
	size_t count;
} sqlparser_dialect_postgresql_dml_visit_state_t;

static int sqlparser_dialect_postgresql_dml_visit_node(
	sqlparser_dialect_postgresql_dml_visit_state_t *state,
	PgQuery__Node *node,
	PgQuery__CommonTableExpr *cte,
	size_t parent_dml_index,
	int has_parent)
{
	PgQuery__WithClause *with_clause;
	sqlparser_dialect_dml_result_dml_t dml;
	size_t child_parent_dml_index;
	size_t index;
	int child_has_parent;
	int status;

	if (state == NULL || node == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	child_parent_dml_index = parent_dml_index;
	child_has_parent = has_parent;
	if (sqlparser_dialect_postgresql_dml_node(node, &dml)) {
		dml.cte = cte;
		dml.parent_dml_index = parent_dml_index;
		dml.has_parent = has_parent;
		if (state->visitor != NULL) {
			status = state->visitor(state->count, &dml, state->context);
			if (status != 0) {
				return status;
			}
		}
		child_parent_dml_index = state->count;
		child_has_parent = 1;
		state->count++;
	}
	with_clause = sqlparser_dialect_postgresql_with_clause(node);
	if (with_clause == NULL || with_clause->ctes == NULL) {
		return 0;
	}
	for (index = 0U; index < with_clause->n_ctes; index++) {
		PgQuery__Node *cte_node;
		PgQuery__CommonTableExpr *child_cte;

		cte_node = with_clause->ctes[index];
		child_cte = cte_node != NULL &&
			cte_node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR ?
			cte_node->common_table_expr : NULL;
		if (child_cte == NULL || child_cte->ctequery == NULL) {
			continue;
		}
		status = sqlparser_dialect_postgresql_dml_visit_node(
			state,
			child_cte->ctequery,
			child_cte,
			child_parent_dml_index,
			child_has_parent);
		if (status != 0) {
			return status;
		}
	}
	return 0;
}

int sqlparser_dialect_postgresql_dml_result_visit(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_dialect_dml_result_visit_fn visitor,
	void *context,
	size_t *out_count)
{
	sqlparser_dialect_postgresql_dml_visit_state_t state;
	PgQuery__Node *statement;
	int status;

	if (out_count != NULL) {
		*out_count = 0U;
	}
	statement = sqlparser_dialect_postgresql_statement(handle, statement_index);
	if (statement == NULL) {
		return 0;
	}
	memset(&state, 0, sizeof(state));
	state.visitor = visitor;
	state.context = context;
	status = sqlparser_dialect_postgresql_dml_visit_node(
		&state, statement, NULL, 0U, 0);
	if (out_count != NULL) {
		*out_count = state.count;
	}
	return status;
}

typedef struct {
	size_t target_index;
	sqlparser_dialect_dml_result_dml_t *out_dml;
	int found;
} sqlparser_dialect_postgresql_dml_at_context_t;

static int sqlparser_dialect_postgresql_dml_at_visitor(
	size_t dml_index,
	const sqlparser_dialect_dml_result_dml_t *dml,
	void *context)
{
	sqlparser_dialect_postgresql_dml_at_context_t *find;

	find = (sqlparser_dialect_postgresql_dml_at_context_t *)context;
	if (find == NULL || dml == NULL || dml_index != find->target_index) {
		return 0;
	}
	*find->out_dml = *dml;
	find->found = 1;
	return 1;
}

static int sqlparser_dialect_postgresql_dml_at(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	sqlparser_dialect_dml_result_dml_t *out_dml)
{
	sqlparser_dialect_postgresql_dml_at_context_t find;

	if (out_dml == NULL) {
		return 0;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	memset(&find, 0, sizeof(find));
	find.target_index = dml_index;
	find.out_dml = out_dml;
	(void)sqlparser_dialect_postgresql_dml_result_visit(
		handle,
		statement_index,
		sqlparser_dialect_postgresql_dml_at_visitor,
		&find,
		NULL);
	return find.found;
}

size_t sqlparser_dialect_dml_result_count(
	const sqlparser_handle_t *handle,
	size_t statement_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t local_statement_index;
	size_t count;
	size_t index;

	if (handle == NULL) {
		return 0U;
	}
	if (sqlparser_dialect_uses_postgresql_placeholders(handle->dialect)) {
		count = 0U;
		(void)sqlparser_dialect_postgresql_dml_result_visit(
			handle,
			statement_index,
			NULL,
			NULL,
			&count);
		return count;
	}
	output = sqlparser_dialect_sqlserver_output_state(
		handle->dialect,
		handle->dialect_state,
		statement_index,
		&local_statement_index);
	count = 0U;
	for (index = 0U; index < sqlparser_sqlserver_output_dml_count(output); index++) {
		sqlparser_sqlserver_output_dml_view_t dml;

		if (sqlparser_sqlserver_output_dml_at(output, index, &dml) &&
		    dml.statement_index == local_statement_index) {
			count++;
		}
	}
	return count;
}

static int sqlparser_dialect_dml_global_index(
	const sqlparser_sqlserver_output_state_t *output,
	size_t statement_index,
	size_t dml_index,
	size_t *out_index)
{
	size_t index;
	size_t ordinal;

	ordinal = 0U;
	for (index = 0U; index < sqlparser_sqlserver_output_dml_count(output); index++) {
		sqlparser_sqlserver_output_dml_view_t dml;

		if (!sqlparser_sqlserver_output_dml_at(output, index, &dml) ||
		    dml.statement_index != statement_index) {
			continue;
		}
		if (ordinal == dml_index) {
			if (out_index != NULL) {
				*out_index = index;
			}
			return 1;
		}
		ordinal++;
	}
	return 0;
}

int sqlparser_dialect_dml_result_dml_at(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	sqlparser_dialect_dml_result_dml_t *out_dml)
{
	const sqlparser_sqlserver_output_state_t *output;
	sqlparser_sqlserver_output_dml_view_t dml;
	size_t global_index;
	size_t local_statement_index;

	if (out_dml == NULL) {
		return 0;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	if (handle != NULL &&
	    sqlparser_dialect_uses_postgresql_placeholders(handle->dialect)) {
		return sqlparser_dialect_postgresql_dml_at(
			handle, statement_index, dml_index, out_dml);
	}
	if (handle == NULL) {
		return 0;
	}
	output = sqlparser_dialect_sqlserver_output_state(
		handle->dialect,
		handle->dialect_state,
		statement_index,
		&local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index) ||
	    !sqlparser_sqlserver_output_dml_at(output, global_index, &dml)) {
		return 0;
	}
	out_dml->kind = dml.kind;
	out_dml->channel_count = dml.channel_count;
	out_dml->parent_dml_index = dml.parent_dml_index;
	out_dml->has_parent = dml.has_parent;
	out_dml->source_name = dml.source_name;
	out_dml->source_sql = dml.source_sql;
	out_dml->has_duplicate_target_relation = dml.has_duplicate_target_relation;
	return 1;
}

int sqlparser_dialect_dml_result_channel_at(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	sqlparser_dialect_dml_result_channel_t *out_channel)
{
	const sqlparser_sqlserver_output_state_t *output;
	sqlparser_sqlserver_output_channel_view_t channel;
	size_t global_index;
	size_t local_statement_index;

	if (out_channel == NULL) {
		return 0;
	}
	memset(out_channel, 0, sizeof(*out_channel));
	if (handle != NULL &&
	    sqlparser_dialect_uses_postgresql_placeholders(handle->dialect)) {
		sqlparser_dialect_dml_result_dml_t dml;

		memset(&dml, 0, sizeof(dml));
		if (channel_index != 0U ||
		    !sqlparser_dialect_postgresql_dml_at(
			    handle,
			    statement_index,
			    dml_index,
			    &dml) ||
		    dml.target_count == 0U) {
			return 0;
		}
		out_channel->kind = SQLPARSER_GRAPH_DML_RESULT_CLIENT;
		out_channel->target_count = dml.target_count;
		return 1;
	}
	if (handle == NULL) {
		return 0;
	}
	output = sqlparser_dialect_sqlserver_output_state(
		handle->dialect,
		handle->dialect_state,
		statement_index,
		&local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index) ||
	    !sqlparser_sqlserver_output_channel_at(output, global_index, channel_index, &channel)) {
		return 0;
	}
	out_channel->kind = channel.kind;
	out_channel->target_offset = channel.target_offset;
	out_channel->target_count = channel.target_count;
	out_channel->sink_sql = channel.sink_sql;
	out_channel->sink_column_count = channel.sink_column_count;
	return 1;
}

const char *sqlparser_dialect_dml_result_sink_column_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index)) {
		return NULL;
	}
	return sqlparser_sqlserver_output_sink_column_at(
		output,
		global_index,
		channel_index,
		column_index);
}

const char *sqlparser_dialect_dml_result_action_marker_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index)) {
		return NULL;
	}
	return sqlparser_sqlserver_output_action_marker_at(
		output, global_index, channel_index, target_index);
}

sqlparser_status_t sqlparser_dialect_dml_result_preprocess_target_sql(
	sqlparser_dialect_t dialect,
	const void *state,
	const char *public_sql,
	sqlparser_graph_dml_kind_t dml_kind,
	char **out_sql,
	char **out_action_marker,
	sqlparser_error_t *out_error)
{
	(void)state;
	if (sqlparser_dialect_uses_postgresql_placeholders(dialect)) {
		if (public_sql == NULL || out_sql == NULL || out_action_marker == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"DML result target SQL and outputs must not be NULL");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		*out_action_marker = NULL;
		*out_sql = sqlparser_strdup(public_sql);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "dialect does not support DML result targets");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return sqlparser_sqlserver_output_preprocess_target_sql(
		public_sql, dml_kind, out_sql, out_action_marker, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_postprocess_target_sql(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *parser_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *action_marker;

	if (sqlparser_dialect_uses_postgresql_placeholders(dialect)) {
		if (parser_sql == NULL || out_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"DML result target SQL and output must not be NULL");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		*out_sql = sqlparser_strdup(parser_sql);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "dialect does not support DML result targets");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	action_marker = sqlparser_dialect_dml_result_action_marker_at(
		dialect, state, statement_index, dml_index, channel_index, target_index);
	return sqlparser_sqlserver_output_postprocess_target_sql(
		parser_sql, action_marker, out_sql, out_error);
}

static sqlparser_status_t sqlparser_dialect_dml_result_mutable_index(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	sqlparser_sqlserver_output_state_t **out_output,
	size_t *out_global_index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state_mutable(
		dialect, state, statement_index, &local_statement_index);
	if (output == NULL ||
	    !sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, out_global_index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_output = output;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dialect_dml_result_adjust_target_count(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	ptrdiff_t delta,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	if (sqlparser_dialect_uses_postgresql_placeholders(dialect)) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_adjust_target_count(
		output, global_index, channel_index, target_index, delta, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_set_action_marker(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *marker,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	if (sqlparser_dialect_uses_postgresql_placeholders(dialect)) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_action_marker(
		output, global_index, channel_index, target_index, marker, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_set_sink_sql(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	const char *sink_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_sink_sql(
		output, global_index, channel_index, sink_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_set_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_sink_column(
		output, global_index, channel_index, column_index, column_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_insert_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_insert_sink_column(
		output, global_index, channel_index, column_index, column_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_delete_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_delete_sink_column(
		output, global_index, channel_index, column_index, out_error);
}
