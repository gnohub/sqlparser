#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_dml_result_internal.h"

typedef struct {
	PgQuery__Node ***items;
	size_t *count;
} sqlparser_dml_result_list_t;

typedef enum {
	SQLPARSER_DML_RESULT_SINK_SET_RELATION = 0,
	SQLPARSER_DML_RESULT_SINK_SET_COLUMN,
	SQLPARSER_DML_RESULT_SINK_INSERT_COLUMN,
	SQLPARSER_DML_RESULT_SINK_DELETE_COLUMN
} sqlparser_dml_result_sink_operation_t;

static const ProtobufCMessageDescriptor *sqlparser_dml_result_descriptor(
	sqlparser_graph_dml_kind_t kind)
{
	switch (kind) {
		case SQLPARSER_GRAPH_DML_INSERT:
			return &pg_query__insert_stmt__descriptor;
		case SQLPARSER_GRAPH_DML_UPDATE:
			return &pg_query__update_stmt__descriptor;
		case SQLPARSER_GRAPH_DML_DELETE:
			return &pg_query__delete_stmt__descriptor;
		case SQLPARSER_GRAPH_DML_MERGE:
			return &pg_query__merge_stmt__descriptor;
		default:
			return NULL;
	}
}

static sqlparser_status_t sqlparser_dml_result_list_from_message(
	ProtobufCMessage *message,
	sqlparser_graph_dml_kind_t kind,
	sqlparser_dml_result_list_t *out_list,
	sqlparser_error_t *out_error)
{
	memset(out_list, 0, sizeof(*out_list));
	switch (kind) {
		case SQLPARSER_GRAPH_DML_INSERT:
		{
			PgQuery__InsertStmt *stmt = (PgQuery__InsertStmt *)message;
			out_list->items = &stmt->returning_list;
			out_list->count = &stmt->n_returning_list;
			break;
		}
		case SQLPARSER_GRAPH_DML_UPDATE:
		{
			PgQuery__UpdateStmt *stmt = (PgQuery__UpdateStmt *)message;
			out_list->items = &stmt->returning_list;
			out_list->count = &stmt->n_returning_list;
			break;
		}
		case SQLPARSER_GRAPH_DML_DELETE:
		{
			PgQuery__DeleteStmt *stmt = (PgQuery__DeleteStmt *)message;
			out_list->items = &stmt->returning_list;
			out_list->count = &stmt->n_returning_list;
			break;
		}
		case SQLPARSER_GRAPH_DML_MERGE:
		{
			PgQuery__MergeStmt *stmt = (PgQuery__MergeStmt *)message;
			out_list->items = &stmt->returning_list;
			out_list->count = &stmt->n_returning_list;
			break;
		}
		default:
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result kind is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_get_dml_result_message(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	sqlparser_graph_dml_kind_t *out_kind,
	ProtobufCMessage **out_message,
	sqlparser_error_t *out_error)
{
	const ProtobufCMessageDescriptor *descriptor;
	ProtobufCMessage *message;
	sqlparser_dialect_dml_result_dml_t dml;
	size_t kind_ordinal;
	size_t index;
	sqlparser_status_t status;

	if (handle == NULL || out_message == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result handle and output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_message = NULL;
	memset(&dml, 0, sizeof(dml));
	if (!sqlparser_dialect_dml_result_dml_at(
		    handle->dialect,
		    handle->dialect_state,
		    statement_index,
		    dml_index,
		    &dml)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	descriptor = sqlparser_dml_result_descriptor(dml.kind);
	if (descriptor == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result kind is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	kind_ordinal = 0U;
	for (index = 0U; index < dml_index; index++) {
		sqlparser_dialect_dml_result_dml_t previous;

		if (!sqlparser_dialect_dml_result_dml_at(
			    handle->dialect,
			    handle->dialect_state,
			    statement_index,
			    index,
			    &previous)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result metadata is inconsistent");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (previous.kind == dml.kind) {
			kind_ordinal++;
		}
	}
	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		statement_index,
		descriptor,
		NULL,
		1,
		kind_ordinal,
		NULL,
		&message,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (message == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result AST node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (out_kind != NULL) {
		*out_kind = dml.kind;
	}
	*out_message = message;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_get_merge_stmt_by_dml_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	PgQuery__MergeStmt **out_stmt,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	PgQuery__Node *statement;
	sqlparser_graph_dml_kind_t kind;
	sqlparser_status_t status;

	if (out_stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"MERGE statement output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_stmt = NULL;
	if (dml_index == 0U) {
		statement = NULL;
		status = sqlparser_get_statement_node(
			handle,
			statement_index,
			&statement,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (statement == NULL ||
		    statement->node_case != PG_QUERY__NODE__NODE_MERGE_STMT ||
		    statement->merge_stmt == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"DML selector does not target a MERGE statement");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		*out_stmt = statement->merge_stmt;
		return SQLPARSER_STATUS_OK;
	}

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
	if (kind != SQLPARSER_GRAPH_DML_MERGE ||
	    message == NULL ||
	    message->descriptor != &pg_query__merge_stmt__descriptor) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"DML selector does not target a MERGE statement");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	*out_stmt = (PgQuery__MergeStmt *)message;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dml_result_get_list(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	sqlparser_dialect_dml_result_dml_t *out_dml,
	sqlparser_dml_result_list_t *out_list,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	sqlparser_status_t status;

	message = NULL;
	if (!sqlparser_dialect_dml_result_dml_at(
		    handle->dialect,
		    handle->dialect_state,
		    statement_index,
		    dml_index,
		    out_dml)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_get_dml_result_message(
		handle, statement_index, dml_index, NULL, &message, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_dml_result_list_from_message(message, out_dml->kind, out_list, out_error);
}

static sqlparser_status_t sqlparser_dml_result_get_channel(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	sqlparser_dialect_dml_result_dml_t *out_dml,
	sqlparser_dialect_dml_result_channel_t *out_channel,
	sqlparser_dml_result_list_t *out_list,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_dml_result_get_list(
		handle,
		selector->statement_index,
		selector->item_index,
		out_dml,
		out_list,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (!sqlparser_dialect_dml_result_channel_at(
		    handle->dialect,
		    handle->dialect_state,
		    selector->statement_index,
		    selector->item_index,
		    selector->row_index,
		    out_channel) ||
	    out_channel->target_offset > *out_list->count ||
	    out_channel->target_count > *out_list->count - out_channel->target_offset) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result channel index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dml_result_parse_target(
	sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_graph_dml_kind_t dml_kind,
	const char *public_sql,
	PgQuery__Node **out_node,
	void **out_state,
	char **out_action_marker,
	sqlparser_error_t *out_error)
{
	char *result_sql;
	char *parser_sql;
	sqlparser_generated_source_t source;
	sqlparser_identifier_origin_map_t *origins;
	void *candidate_state;
	sqlparser_status_t status;

	*out_node = NULL;
	*out_state = NULL;
	*out_action_marker = NULL;
	result_sql = NULL;
	parser_sql = NULL;
	origins = NULL;
	candidate_state = NULL;
	status = sqlparser_dialect_dml_result_preprocess_target_sql(
		handle->dialect,
		handle->dialect_state,
		public_sql,
		dml_kind,
		&result_sql,
		out_action_marker,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_preprocess_handle_sql_fragment_with_origins(
			handle,
			statement_index,
			result_sql,
			"DML result target SQL",
			&parser_sql,
			&candidate_state,
			&origins,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		memset(&source, 0, sizeof(source));
		source.public_sql = result_sql;
		source.origins = origins;
		source.dialect = handle->dialect;
		source.spelling_handle = handle;
		status = sqlparser_parse_select_target_node_sql(
			parser_sql,
			&source,
			out_node,
			out_error);
	}
	sqlparser_identifier_origin_map_destroy(origins);
	free(result_sql);
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(*out_node);
		*out_node = NULL;
		free(*out_action_marker);
		*out_action_marker = NULL;
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	*out_state = candidate_state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dml_result_clone_state(
	const sqlparser_handle_t *handle,
	void **out_state,
	sqlparser_error_t *out_error)
{
	*out_state = NULL;
	if (handle->dialect_state == NULL || handle->dialect_ops == NULL ||
	    handle->dialect_ops->clone_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "DML result dialect state cannot be cloned");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return handle->dialect_ops->clone_state(handle->dialect_state, out_state, out_error);
}

static void sqlparser_dml_result_commit_state(sqlparser_handle_t *handle, void *state)
{
	sqlparser_handle_adopt_dialect_state(handle, state);
	handle->generation++;
	sqlparser_handle_invalidate_derived(handle);
}

sqlparser_status_t sqlparser_dml_result_target_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_dml_result_dml_t dml;
	sqlparser_dialect_dml_result_channel_t channel;
	sqlparser_dml_result_list_t list;
	char *core_sql;
	char *fragment_sql;
	size_t absolute_index;
	sqlparser_status_t status;

	if (handle == NULL || selector == NULL || out_sql == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target selector is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	status = sqlparser_dml_result_get_channel(
		(sqlparser_handle_t *)handle, selector, &dml, &channel, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector->column_index >= channel.target_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	absolute_index = channel.target_offset + selector->column_index;
	core_sql = NULL;
	fragment_sql = NULL;
	status = sqlparser_render_select_target_node_sql(
		handle,
		(*list.items)[absolute_index],
		&core_sql,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_postprocess_handle_sql_fragment(
			handle,
			selector->statement_index,
			core_sql,
			"DML result target SQL",
			&fragment_sql,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dialect_dml_result_postprocess_target_sql(
			handle->dialect,
			handle->dialect_state,
			selector->statement_index,
			selector->item_index,
			selector->row_index,
			selector->column_index,
			fragment_sql,
			out_sql,
			out_error);
	}
	free(core_sql);
	free(fragment_sql);
	return status;
}

sqlparser_status_t sqlparser_dml_result_set_target_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_dml_result_dml_t dml;
	sqlparser_dialect_dml_result_channel_t channel;
	sqlparser_dml_result_list_t list;
	PgQuery__Node *replacement;
	PgQuery__Node *removed;
	void *candidate_state;
	char *action_marker;
	size_t absolute_index;
	sqlparser_status_t status;

	if (handle == NULL || selector == NULL || sql_text == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGET) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target replacement is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_dml_result_get_channel(handle, selector, &dml, &channel, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (selector->column_index >= channel.target_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	replacement = NULL;
	candidate_state = NULL;
	action_marker = NULL;
	status = sqlparser_dml_result_parse_target(
		handle,
		selector->statement_index,
		dml.kind,
		sql_text,
		&replacement,
		&candidate_state,
		&action_marker,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dialect_dml_result_set_action_marker(
		handle->dialect,
		candidate_state,
		selector->statement_index,
		selector->item_index,
		selector->row_index,
		selector->column_index,
		action_marker,
		out_error);
	free(action_marker);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(replacement);
		sqlparser_handle_sweep_identifier_spellings(handle);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	absolute_index = channel.target_offset + selector->column_index;
	removed = (*list.items)[absolute_index];
	(*list.items)[absolute_index] = replacement;
	sqlparser_free_proto_node(removed);
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, candidate_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dml_result_insert_target_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t target_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_dml_result_dml_t dml;
	sqlparser_dialect_dml_result_channel_t channel;
	sqlparser_dml_result_list_t list;
	PgQuery__Node *node;
	PgQuery__Node **next;
	void *candidate_state;
	char *action_marker;
	size_t absolute_index;
	size_t old_count;
	sqlparser_status_t status;

	if (handle == NULL || selector == NULL || sql_text == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target insertion is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_dml_result_get_channel(handle, selector, &dml, &channel, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	old_count = *list.count;
	if (old_count == SIZE_MAX || old_count + 1U > SIZE_MAX / sizeof(*next)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "DML result target count is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	node = NULL;
	candidate_state = NULL;
	action_marker = NULL;
	status = sqlparser_dml_result_parse_target(
		handle,
		selector->statement_index,
		dml.kind,
		sql_text,
		&node,
		&candidate_state,
		&action_marker,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (target_index > channel.target_count) {
		target_index = channel.target_count;
	}
	status = sqlparser_dialect_dml_result_adjust_target_count(
		handle->dialect,
		candidate_state,
		selector->statement_index,
		selector->item_index,
		selector->row_index,
		target_index,
		1,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(action_marker);
		sqlparser_free_proto_node(node);
		sqlparser_handle_sweep_identifier_spellings(handle);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	status = sqlparser_dialect_dml_result_set_action_marker(
		handle->dialect,
		candidate_state,
		selector->statement_index,
		selector->item_index,
		selector->row_index,
		target_index,
		action_marker,
		out_error);
	free(action_marker);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(node);
		sqlparser_handle_sweep_identifier_spellings(handle);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	absolute_index = channel.target_offset + target_index;
	next = (PgQuery__Node **)malloc((old_count + 1U) * sizeof(*next));
	if (next == NULL) {
		sqlparser_free_proto_node(node);
		sqlparser_handle_sweep_identifier_spellings(handle);
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (absolute_index > 0U) {
		memcpy(next, *list.items, absolute_index * sizeof(*next));
	}
	next[absolute_index] = node;
	if (absolute_index < old_count) {
		memcpy(
			next + absolute_index + 1U,
			*list.items + absolute_index,
			(old_count - absolute_index) * sizeof(*next));
	}
	free(*list.items);
	*list.items = next;
	*list.count = old_count + 1U;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, candidate_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dml_result_delete_target(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t target_index,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_dml_result_dml_t dml;
	sqlparser_dialect_dml_result_channel_t channel;
	sqlparser_dml_result_list_t list;
	PgQuery__Node **next;
	PgQuery__Node *removed;
	void *candidate_state;
	size_t absolute_index;
	size_t old_count;
	sqlparser_status_t status;

	if (handle == NULL || selector == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_TARGETS) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target deletion is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_dml_result_get_channel(handle, selector, &dml, &channel, &list, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (target_index >= channel.target_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result target index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	candidate_state = NULL;
	status = sqlparser_dml_result_clone_state(handle, &candidate_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dialect_dml_result_adjust_target_count(
		handle->dialect,
		candidate_state,
		selector->statement_index,
		selector->item_index,
		selector->row_index,
		target_index,
		-1,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	old_count = *list.count;
	absolute_index = channel.target_offset + target_index;
	next = old_count > 1U ? (PgQuery__Node **)malloc((old_count - 1U) * sizeof(*next)) : NULL;
	if (old_count > 1U && next == NULL) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (absolute_index > 0U) {
		memcpy(next, *list.items, absolute_index * sizeof(*next));
	}
	if (absolute_index + 1U < old_count) {
		memcpy(
			next + absolute_index,
			*list.items + absolute_index + 1U,
			(old_count - absolute_index - 1U) * sizeof(*next));
	}
	removed = (*list.items)[absolute_index];
	free(*list.items);
	*list.items = next;
	*list.count = old_count - 1U;
	sqlparser_free_proto_node(removed);
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, candidate_state);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dml_result_mutate_sink(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	size_t index,
	sqlparser_dml_result_sink_operation_t operation,
	sqlparser_error_t *out_error)
{
	void *candidate_state;
	sqlparser_status_t status;

	candidate_state = NULL;
	status = sqlparser_dml_result_clone_state(handle, &candidate_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	switch (operation) {
		case SQLPARSER_DML_RESULT_SINK_SET_RELATION:
			status = sqlparser_dialect_dml_result_set_sink_sql(
				handle->dialect, candidate_state, selector->statement_index,
				selector->item_index, selector->row_index, sql_text, out_error);
			break;
		case SQLPARSER_DML_RESULT_SINK_SET_COLUMN:
			status = sqlparser_dialect_dml_result_set_sink_column(
				handle->dialect, candidate_state, selector->statement_index,
				selector->item_index, selector->row_index, index, sql_text, out_error);
			break;
		case SQLPARSER_DML_RESULT_SINK_INSERT_COLUMN:
			status = sqlparser_dialect_dml_result_insert_sink_column(
				handle->dialect, candidate_state, selector->statement_index,
				selector->item_index, selector->row_index, index, sql_text, out_error);
			break;
		case SQLPARSER_DML_RESULT_SINK_DELETE_COLUMN:
			status = sqlparser_dialect_dml_result_delete_sink_column(
				handle->dialect, candidate_state, selector->statement_index,
				selector->item_index, selector->row_index, index, out_error);
			break;
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, candidate_state);
		return status;
	}
	sqlparser_dml_result_commit_state(handle, candidate_state);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dml_result_set_sink_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	if (handle == NULL || selector == NULL || sql_text == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink replacement is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_dml_result_mutate_sink(
		handle, selector, sql_text, 0U, SQLPARSER_DML_RESULT_SINK_SET_RELATION, out_error);
}

sqlparser_status_t sqlparser_dml_result_set_sink_column_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	if (handle == NULL || selector == NULL || sql_text == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMN) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink column replacement is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_dml_result_mutate_sink(
		handle, selector, sql_text, selector->column_index,
		SQLPARSER_DML_RESULT_SINK_SET_COLUMN, out_error);
}

sqlparser_status_t sqlparser_dml_result_insert_sink_column_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	if (handle == NULL || selector == NULL || sql_text == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMNS) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink column insertion is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_dml_result_mutate_sink(
		handle, selector, sql_text, column_index,
		SQLPARSER_DML_RESULT_SINK_INSERT_COLUMN, out_error);
}

sqlparser_status_t sqlparser_dml_result_delete_sink_column(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	size_t column_index,
	sqlparser_error_t *out_error)
{
	if (handle == NULL || selector == NULL ||
	    selector->kind != SQLPARSER_SELECTOR_KIND_DML_RESULT_SINK_COLUMNS) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result sink column deletion is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_dml_result_mutate_sink(
		handle, selector, NULL, column_index,
		SQLPARSER_DML_RESULT_SINK_DELETE_COLUMN, out_error);
}
