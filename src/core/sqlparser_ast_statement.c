#include <string.h>

#include "sqlparser_ast_internal.h"

sqlparser_status_t sqlparser_statement_kind(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_statement_kind_t *out_kind,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_kind == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_kind must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_kind = SQLPARSER_STATEMENT_KIND_UNKNOWN;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_kind = sqlparser_statement_kind_from_case(statement->node_case);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_node_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	const char **out_name,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_name = NULL;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_name = sqlparser_statement_node_name_from_case(statement->node_case);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_target_relation(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	PgQuery__RangeVar *relation;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_relation_view_clear(out_relation);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	relation = NULL;
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (statement->insert_stmt != NULL) {
				relation = statement->insert_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			if (statement->update_stmt != NULL) {
				relation = statement->update_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			if (statement->delete_stmt != NULL) {
				relation = statement->delete_stmt->relation;
			}
			break;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			if (statement->merge_stmt != NULL) {
				relation = statement->merge_stmt->relation;
			}
			break;
		default:
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"statement does not expose a single target relation");
			return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"target relation is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	sqlparser_fill_relation_view(relation, out_relation);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_relation_count(
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
		&pg_query__range_var__descriptor,
		NULL,
		0,
		0U,
		out_count,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_statement_relation(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	sqlparser_relation_view_t *out_relation,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	ProtobufCMessage *message;
	sqlparser_status_t status;

	if (out_relation == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_relation_view_clear(out_relation);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	message = NULL;
	status = sqlparser_search_statement_messages(
		mutable_handle,
		statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		1,
		relation_index,
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
			"relation_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_fill_relation_view((PgQuery__RangeVar *)message, out_relation);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_set_relation_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t relation_index,
	const char *schema_name,
	const char *table_name,
	sqlparser_error_t *out_error)
{
	PgQuery__RangeVar *relation;
	ProtobufCMessage *message;
	sqlparser_status_t status;

	if (table_name == NULL || table_name[0] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"table_name must not be NULL or empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_error_clear(out_error);
	message = NULL;
	status = sqlparser_search_statement_messages(
		handle,
		statement_index,
		&pg_query__range_var__descriptor,
		NULL,
		1,
		relation_index,
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
			"relation_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	relation = (PgQuery__RangeVar *)message;
	status = sqlparser_replace_proto_string(&relation->relname, table_name, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_replace_proto_string(
		&relation->schemaname,
		schema_name != NULL ? schema_name : "",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_statement_name_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;
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
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = search.seen;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	sqlparser_name_view_t *out_name,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_name_view_clear(out_name);
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_statement_node(mutable_handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = name_index;
	search.name_view = out_name;
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_statement_set_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t name_index,
	const char *value,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	sqlparser_name_search_t search;
	sqlparser_status_t status;

	if (value == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlparser_error_clear(out_error);
	status = sqlparser_get_statement_node(handle, statement_index, &statement, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&search, 0, sizeof(search));
	search.want_target = 1;
	search.target_index = name_index;
	status = sqlparser_walk_message_names((ProtobufCMessage *)statement, NULL, &search);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (search.target_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"name_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_replace_proto_string(search.target_slot, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_handle_commit_ast(handle, out_error);
}
