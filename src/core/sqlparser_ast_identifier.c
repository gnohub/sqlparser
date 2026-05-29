#include <stdlib.h>

#include "sqlparser_ast_internal.h"

static sqlparser_status_t sqlparser_validate_identifier_path(
	const sqlparser_identifier_path_view_t *path,
	const char *field_name,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (path == NULL || path->parts == NULL || path->part_count == 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			field_name != NULL ? field_name : "identifier path must not be empty");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	for (index = 0U; index < path->part_count; index++) {
		if (path->parts[index] == NULL || path->parts[index][0] == '\0') {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"identifier path part must not be NULL or empty");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
	}

	return SQLPARSER_STATUS_OK;
}

static PgQuery__Node *sqlparser_alloc_string_node(
	const char *text,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	PgQuery__String *string_node;

	node = (PgQuery__Node *)calloc(1U, sizeof(*node));
	string_node = (PgQuery__String *)calloc(1U, sizeof(*string_node));
	if (node == NULL || string_node == NULL) {
		free(node);
		free(string_node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}

	pg_query__node__init(node);
	pg_query__string__init(string_node);
	node->node_case = PG_QUERY__NODE__NODE_STRING;
	node->string = string_node;
	string_node->sval = sqlparser_strdup(text);
	if (string_node->sval == NULL) {
		sqlparser_free_proto_node(node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}

	return node;
}

sqlparser_status_t sqlparser_build_identifier_path_node(
	const sqlparser_identifier_path_view_t *path,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	PgQuery__ColumnRef *column_ref;
	PgQuery__Node **fields;
	size_t index;
	sqlparser_status_t status;

	if (out_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node = NULL;

	status = sqlparser_validate_identifier_path(path, "identifier path must not be empty", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	node = (PgQuery__Node *)calloc(1U, sizeof(*node));
	column_ref = (PgQuery__ColumnRef *)calloc(1U, sizeof(*column_ref));
	fields = (PgQuery__Node **)calloc(path->part_count, sizeof(*fields));
	if (node == NULL || column_ref == NULL || fields == NULL) {
		free(node);
		free(column_ref);
		free(fields);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	pg_query__node__init(node);
	pg_query__column_ref__init(column_ref);
	node->node_case = PG_QUERY__NODE__NODE_COLUMN_REF;
	node->column_ref = column_ref;
	column_ref->n_fields = path->part_count;
	column_ref->fields = fields;
	column_ref->location = -1;

	for (index = 0U; index < path->part_count; index++) {
		fields[index] = sqlparser_alloc_string_node(path->parts[index], out_error);
		if (fields[index] == NULL) {
			sqlparser_free_proto_node(node);
			return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
				out_error->code :
				SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	*out_node = node;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_build_select_target_identifier_node(
	const sqlparser_identifier_path_view_t *path,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	PgQuery__ResTarget *target;
	PgQuery__Node *value_node;
	sqlparser_status_t status;

	if (out_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node = NULL;

	value_node = NULL;
	status = sqlparser_build_identifier_path_node(path, &value_node, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	node = (PgQuery__Node *)calloc(1U, sizeof(*node));
	target = (PgQuery__ResTarget *)calloc(1U, sizeof(*target));
	if (node == NULL || target == NULL) {
		free(node);
		free(target);
		sqlparser_free_proto_node(value_node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	pg_query__node__init(node);
	pg_query__res_target__init(target);
	node->node_case = PG_QUERY__NODE__NODE_RES_TARGET;
	node->res_target = target;
	target->val = value_node;
	target->location = -1;

	*out_node = node;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_build_update_assignment_identifier_node(
	const sqlparser_identifier_path_view_t *target_path,
	PgQuery__Node *value_node,
	PgQuery__Node **out_node,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	PgQuery__ResTarget *target;
	PgQuery__Node **indirection;
	size_t index;
	size_t indirection_count;
	sqlparser_status_t status;

	if (out_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_node = NULL;
	if (value_node == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "assignment value node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_validate_identifier_path(target_path, "assignment target path must not be empty", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	node = (PgQuery__Node *)calloc(1U, sizeof(*node));
	target = (PgQuery__ResTarget *)calloc(1U, sizeof(*target));
	if (node == NULL || target == NULL) {
		free(node);
		free(target);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	pg_query__node__init(node);
	pg_query__res_target__init(target);
	node->node_case = PG_QUERY__NODE__NODE_RES_TARGET;
	node->res_target = target;
	target->location = -1;
	target->name = sqlparser_strdup(target_path->parts[0]);
	if (target->name == NULL) {
		sqlparser_free_proto_node(node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	indirection_count = target_path->part_count - 1U;
	if (indirection_count > 0U) {
		indirection = (PgQuery__Node **)calloc(indirection_count, sizeof(*indirection));
		if (indirection == NULL) {
			sqlparser_free_proto_node(node);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		target->n_indirection = indirection_count;
		target->indirection = indirection;
		for (index = 0U; index < indirection_count; index++) {
			indirection[index] = sqlparser_alloc_string_node(target_path->parts[index + 1U], out_error);
			if (indirection[index] == NULL) {
				sqlparser_free_proto_node(node);
				return out_error != NULL && out_error->code != SQLPARSER_STATUS_OK ?
					out_error->code :
					SQLPARSER_STATUS_NO_MEMORY;
			}
		}
	}

	target->val = value_node;
	*out_node = node;
	return SQLPARSER_STATUS_OK;
}
