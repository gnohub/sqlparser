#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_internal.h"

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

static sqlparser_status_t sqlparser_render_quoted_identifier(
	const char *identifier,
	char open_quote,
	char close_quote,
	char escaped,
	char **out_spelling,
	sqlparser_error_t *out_error)
{
	char *spelling;
	size_t escape_count;
	size_t identifier_length;
	size_t index;
	size_t output_length;

	identifier_length = strlen(identifier);
	escape_count = 0U;
	for (index = 0U; index < identifier_length; index++) {
		if (identifier[index] == escaped) {
			escape_count++;
		}
	}
	if (escape_count > SIZE_MAX - identifier_length ||
	    identifier_length + escape_count > SIZE_MAX - 3U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	spelling = (char *)malloc(identifier_length + escape_count + 3U);
	if (spelling == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	output_length = 0U;
	spelling[output_length++] = open_quote;
	for (index = 0U; index < identifier_length; index++) {
		spelling[output_length++] = identifier[index];
		if (identifier[index] == escaped) {
			spelling[output_length++] = identifier[index];
		}
	}
	spelling[output_length++] = close_quote;
	spelling[output_length] = '\0';
	*out_spelling = spelling;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_render_identifier_path_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_path_view_t *path,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char **spellings;
	char *sql;
	size_t index;
	size_t offset;
	size_t output_length;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_validate_identifier_path(
		path,
		"identifier path must not be empty",
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (path->part_count > SIZE_MAX / sizeof(*spellings)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_RESOURCE_LIMIT,
			"identifier path is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	spellings = (char **)calloc(path->part_count, sizeof(*spellings));
	if (spellings == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	output_length = path->part_count - 1U;
	status = SQLPARSER_STATUS_OK;
	for (index = 0U; index < path->part_count; index++) {
		size_t spelling_length;

		if (sqlparser_dialect_is_mysql_compatible(handle->dialect)) {
			status = sqlparser_render_default_identifier_spelling(
				path->parts[index],
				&spellings[index],
				out_error);
			if (status == SQLPARSER_STATUS_OK &&
			    (spellings[index][0] == '"' ||
			     (path->part_count == 1U &&
			      sqlparser_identifier_is_sysdate(path->parts[index])))) {
				free(spellings[index]);
				spellings[index] = NULL;
				status = sqlparser_render_quoted_identifier(
					path->parts[index],
					'`',
					'`',
					'`',
					&spellings[index],
					out_error);
			}
		} else if (path->part_count == 1U &&
			   sqlparser_identifier_is_sysdate(path->parts[index])) {
			status = sqlparser_render_quoted_identifier(
				path->parts[index],
				'"',
				'"',
				'"',
				&spellings[index],
				out_error);
		} else {
			status = sqlparser_render_default_identifier_spelling(
				path->parts[index],
				&spellings[index],
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		spelling_length = strlen(spellings[index]);
		if (spelling_length > SIZE_MAX - output_length) {
			status = SQLPARSER_STATUS_RESOURCE_LIMIT;
			sqlparser_error_set_message(
				out_error,
				status,
				"identifier path is too large");
			break;
		}
		output_length += spelling_length;
	}
	if (status == SQLPARSER_STATUS_OK && output_length == SIZE_MAX) {
		status = SQLPARSER_STATUS_RESOURCE_LIMIT;
		sqlparser_error_set_message(
			out_error,
			status,
			"identifier path is too large");
	}
	if (status == SQLPARSER_STATUS_OK) {
		sql = (char *)malloc(output_length + 1U);
		if (sql == NULL) {
			status = SQLPARSER_STATUS_NO_MEMORY;
			sqlparser_error_set_message(
				out_error,
				status,
				"out of memory");
		} else {
			offset = 0U;
			for (index = 0U; index < path->part_count; index++) {
				size_t spelling_length;

				if (index > 0U) {
					sql[offset++] = '.';
				}
				spelling_length = strlen(spellings[index]);
				memcpy(sql + offset, spellings[index], spelling_length);
				offset += spelling_length;
			}
			sql[offset] = '\0';
			*out_sql = sql;
		}
	}
	for (index = 0U; index < path->part_count; index++) {
		free(spellings[index]);
	}
	free(spellings);
	return status;
}

PgQuery__Node *sqlparser_alloc_string_node(
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
	string_node->location = SQLPARSER_PROTO_LOCATION_GENERATED;
	string_node->sval = sqlparser_strdup(text);
	if (string_node->sval == NULL) {
		sqlparser_free_proto_node(node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}

	return node;
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
	target->location = SQLPARSER_PROTO_LOCATION_GENERATED;
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
