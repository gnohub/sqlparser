#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_multi_insert_internal.h"

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

static sqlparser_status_t sqlparser_patch_set_relation_sql(
	sqlparser_handle_t *handle,
	const sqlparser_selector_t *selector,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	ProtobufCMessage *message;
	PgQuery__RangeVar *relation;
	sqlparser_patch_identifier_path_t path;
	const char *values[3];
	const char *current[3];
	int32_t previous_location;
	size_t first_part;
	size_t index;
	int names_changed;
	sqlparser_status_t status;

	if (selector == NULL || selector->kind != SQLPARSER_SELECTOR_KIND_RELATION) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector kind must be relation");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

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
		relation = (PgQuery__RangeVar *)message;
		current[0] = relation->catalogname != NULL ? relation->catalogname : "";
		current[1] = relation->schemaname != NULL ? relation->schemaname : "";
		current[2] = relation->relname != NULL ? relation->relname : "";
		values[0] = "";
		values[1] = "";
		values[2] = "";
		first_part = 3U - path.part_count;
		for (index = 0U; index < path.part_count; index++) {
			values[first_part + index] = path.parts[index];
		}
		names_changed = 0;
		for (index = 0U; index < 3U; index++) {
			names_changed =
				names_changed || strcmp(current[index], values[index]) != 0;
		}
		previous_location = relation->location;
		relation->location = path.location;
		status = sqlparser_replace_relation_identifier_slots(
			handle,
			selector->statement_index,
			relation,
			values,
			out_error);
		if (status == SQLPARSER_STATUS_OK && !names_changed) {
			status = sqlparser_handle_commit_ast(handle, out_error);
		}
		if (status != SQLPARSER_STATUS_OK && handle->ast != NULL) {
			relation->location = previous_location;
		}
	}

	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_handle_adopt_dialect_state(handle, path.dialect_state);
		path.dialect_state = NULL;
	}
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
	sqlparser_error_t *out_error)
{
	PgQuery__Node *statement;
	PgQuery__Node **value_slot;
	PgQuery__Node *replacement;
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
	if (!variable_set_arg && !sqlparser_patch_node_is_value_expression(*value_slot)) {
		sqlparser_free_proto_node(replacement);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "value selector does not target an expression node");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	sqlparser_free_proto_node(*value_slot);
	*value_slot = replacement;
	replacement = NULL;
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_patch_literal_value_from_sql(
	const char *sql_text,
	sqlparser_literal_value_t *out_value,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *node;
	sqlparser_literal_view_t view;
	sqlparser_status_t status;

	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_value must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	memset(&view, 0, sizeof(view));
	node = NULL;
	status = sqlparser_parse_insert_cell_node_sql(
		sql_text,
		NULL,
		&node,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (node == NULL || node->node_case != PG_QUERY__NODE__NODE_A_CONST || node->a_const == NULL) {
		sqlparser_free_proto_node(node);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "replacement SQL is not a literal");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	status = sqlparser_fill_literal_view_from_a_const(node->a_const, &view, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		out_value->kind = view.kind;
		out_value->string_value = view.string_value;
		out_value->float_value = view.float_value;
		out_value->integer_value = view.integer_value;
		out_value->boolean_value = view.boolean_value;
		status = SQLPARSER_STATUS_OK;
		if (out_value->kind == SQLPARSER_LITERAL_KIND_STRING && out_value->string_value != NULL) {
			out_value->string_value = sqlparser_strdup(out_value->string_value);
			status = out_value->string_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
		} else if (out_value->kind == SQLPARSER_LITERAL_KIND_FLOAT && out_value->float_value != NULL) {
			out_value->float_value = sqlparser_strdup(out_value->float_value);
			status = out_value->float_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		}
	}
	sqlparser_free_proto_node(node);
	return status;
}

static void sqlparser_patch_literal_value_clear(sqlparser_literal_value_t *value)
{
	if (value == NULL) {
		return;
	}
	if (value->kind == SQLPARSER_LITERAL_KIND_STRING) {
		free((char *)value->string_value);
	} else if (value->kind == SQLPARSER_LITERAL_KIND_FLOAT) {
		free((char *)value->float_value);
	}
	memset(value, 0, sizeof(*value));
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

static sqlparser_status_t sqlparser_patch_replace(
	sqlparser_handle_t *handle,
	const sqlparser_patch_t *patch,
	sqlparser_error_t *out_error)
{
	sqlparser_selector_t selector;
	sqlparser_status_t status;

	if (patch == NULL || patch->selector == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "replace patch requires selector");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_patch_parse_selector(patch->selector, &selector, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	switch (selector.kind) {
		case SQLPARSER_SELECTOR_KIND_RELATION:
			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			return sqlparser_patch_set_relation_sql(handle, &selector, patch->sql, out_error);
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
			status = sqlparser_patch_set_value_sql(handle, &selector, value_sql, out_error);
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
			status = sqlparser_selector_set_insert_cell_sql(handle, &selector, cell_sql, out_error);
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
		{
			sqlparser_literal_value_t value;
			if (patch->sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal replacement requires sql");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			status = sqlparser_patch_literal_value_from_sql(patch->sql, &value, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (selector.kind == SQLPARSER_SELECTOR_KIND_LITERAL) {
				status = sqlparser_selector_set_literal(handle, &selector, &value, out_error);
			} else {
				status = sqlparser_selector_set_where_literal(handle, &selector, &value, out_error);
			}
			sqlparser_patch_literal_value_clear(&value);
			return status;
		}
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
		status = sqlparser_select_insert_target_sql(
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
	status = sqlparser_handle_commit_ast(handle, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}
	sqlparser_handle_adopt_dialect_state(handle, dialect_state);
	return SQLPARSER_STATUS_OK;
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

static sqlparser_status_t sqlparser_apply_patch_in_place(
	sqlparser_handle_t *handle,
	const sqlparser_patch_list_t *patches,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	for (index = 0U; index < patches->count; index++) {
		const sqlparser_patch_t *patch;

		patch = &patches->items[index];
		switch (patch->op) {
			case SQLPARSER_PATCH_REPLACE:
				status = sqlparser_patch_replace(handle, patch, out_error);
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
		if (status != SQLPARSER_STATUS_OK) {
			return status;
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
	sqlparser_status_t status;
	unsigned long original_generation;
	int noop;

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
	original_generation = handle->generation;
	status = sqlparser_handle_clone(
		handle,
		&candidate,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_apply_patch_in_place(
		candidate,
		patches,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(candidate);
		return status;
	}
	if (candidate->generation == original_generation) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_clear(out_error);
		return SQLPARSER_STATUS_OK;
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
