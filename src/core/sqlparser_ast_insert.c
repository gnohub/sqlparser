#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_internal.h"
#include "../dialect/sqlparser_dialect_multi_insert_internal.h"

static sqlparser_status_t sqlparser_get_insert_cell_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__Node **out_value_node,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_get_insert_cell_slot(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__Node ***out_value_slot,
	sqlparser_error_t *out_error);

static int sqlparser_bind_key_is_digits(const char *key)
{
	size_t index;

	if (key == NULL || key[0] == '\0') {
		return 0;
	}
	for (index = 0U; key[index] != '\0'; index++) {
		if (!isdigit((unsigned char)key[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_bind_key_is_identifier(const char *key)
{
	size_t index;

	if (key == NULL || key[0] == '\0') {
		return 0;
	}
	if (!(isalpha((unsigned char)key[0]) || key[0] == '_')) {
		return 0;
	}
	for (index = 1U; key[index] != '\0'; index++) {
		if (!(isalnum((unsigned char)key[index]) || key[index] == '_' || key[index] == '$' || key[index] == '#')) {
			return 0;
		}
	}
	return 1;
}

sqlparser_status_t sqlparser_render_bind_value_sql(
	const sqlparser_handle_t *handle,
	const sqlparser_bind_value_t *bind,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char buffer[SQLPARSER_BIND_TEXT_CAPACITY + 4U];
	const char *key;
	sqlparser_dialect_t dialect;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || bind == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	key = bind->key;
	dialect = sqlparser_handle_dialect(handle);

	if (bind->kind == SQLPARSER_BIND_KIND_POSITIONAL) {
		if (key == NULL || key[0] == '\0') {
			if (sqlparser_dialect_uses_postgresql_placeholders(dialect)) {
				*out_sql = sqlparser_strdup("$1");
			} else {
				*out_sql = sqlparser_strdup("?");
			}
			if (*out_sql == NULL) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			return SQLPARSER_STATUS_OK;
		}
		if (strlen(key) >= SQLPARSER_BIND_TEXT_CAPACITY) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind key is too long");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		if (!sqlparser_bind_key_is_digits(key)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "positional bind key must be numeric");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		if (sqlparser_dialect_uses_postgresql_placeholders(dialect)) {
			(void)snprintf(buffer, sizeof(buffer), "$%s", key);
		} else if (sqlparser_dialect_uses_oracle_placeholders(dialect)) {
			(void)snprintf(buffer, sizeof(buffer), ":%s", key);
		} else {
			(void)snprintf(buffer, sizeof(buffer), "?");
		}
		*out_sql = sqlparser_strdup(buffer);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	if (bind->kind == SQLPARSER_BIND_KIND_NAMED) {
		if (key != NULL && strlen(key) >= SQLPARSER_BIND_TEXT_CAPACITY) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind key is too long");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		if (!sqlparser_bind_key_is_identifier(key)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "named bind key is invalid");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		if (sqlparser_dialect_uses_oracle_placeholders(dialect)) {
			(void)snprintf(buffer, sizeof(buffer), ":%s", key);
		} else if (sqlparser_dialect_uses_sqlserver_placeholders(dialect)) {
			(void)snprintf(buffer, sizeof(buffer), "@%s", key);
		} else {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "named bind is not supported by this dialect");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
		*out_sql = sqlparser_strdup(buffer);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind kind must be positional or named");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}

static sqlparser_status_t sqlparser_get_insert_column_res_target(
	PgQuery__InsertStmt *stmt,
	size_t column_index,
	PgQuery__ResTarget **out_target,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *column_node;

	if (out_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_target must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_target = NULL;
	if (stmt == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"statement must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (column_index >= stmt->n_cols) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"column_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	column_node = stmt->cols[column_index];
	if (column_node == NULL ||
	    column_node->node_case != PG_QUERY__NODE__NODE_RES_TARGET ||
	    column_node->res_target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"insert column node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_target = column_node->res_target;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_insert_cell_a_const(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__AConst **out_literal,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *value_node;
	sqlparser_status_t status;

	if (out_literal == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_literal = NULL;
	status = sqlparser_get_insert_cell_node(
		handle,
		statement_index,
		row_index,
		column_index,
		&value_node,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	value_node = sqlparser_unwrap_grouping_node(value_node);
	if (value_node == NULL ||
	    value_node->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    value_node->a_const == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"insert cell is not a literal");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	*out_literal = value_node->a_const;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_insert_cell_node(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__Node **out_value_node,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **value_slot;
	sqlparser_status_t status;

	if (out_value_node == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_value_node must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_value_node = NULL;
	value_slot = NULL;
	status = sqlparser_get_insert_cell_slot(
		handle,
		statement_index,
		row_index,
		column_index,
		&value_slot,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (value_slot == NULL || *value_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"insert cell node is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	*out_value_node = *value_slot;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_get_insert_cell_slot(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	PgQuery__Node ***out_value_slot,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__SelectStmt *values_stmt;
	PgQuery__Node *row_node;
	PgQuery__List *row_list;
	sqlparser_status_t status;

	if (out_value_slot == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_value_slot must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_value_slot = NULL;
	status = sqlparser_get_insert_values_stmt(
		handle,
		statement_index,
		&insert_stmt,
		&values_stmt,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	(void)insert_stmt;

	if (row_index >= values_stmt->n_values_lists) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"row_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	row_node = values_stmt->values_lists[row_index];
	if (row_node == NULL ||
	    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
	    row_node->list == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"insert row node is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	row_list = row_node->list;
	if (column_index >= row_list->n_items) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"column_index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_value_slot = &row_list->items[column_index];
	return SQLPARSER_STATUS_OK;
}


sqlparser_status_t sqlparser_insert_source_kind(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_insert_source_kind_t *out_kind,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_kind == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_kind must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_kind = SQLPARSER_INSERT_SOURCE_UNKNOWN;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_kind = sqlparser_insert_source_from_stmt(insert_stmt);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_column_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
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
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_count = insert_stmt->n_cols;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_column_name(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t column_index,
	const char **out_column_name,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__ResTarget *target;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

	if (out_column_name == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_column_name must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	*out_column_name = NULL;
	sqlparser_error_clear(out_error);
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_get_insert_column_res_target(insert_stmt, column_index, &target, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_column_name = target->name != NULL ? target->name : NULL;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_row_count(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	PgQuery__InsertStmt *insert_stmt;
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
	status = sqlparser_get_insert_stmt(mutable_handle, statement_index, &insert_stmt, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_insert_source_from_stmt(insert_stmt) == SQLPARSER_INSERT_SOURCE_VALUES &&
	    insert_stmt->select_stmt != NULL &&
	    insert_stmt->select_stmt->select_stmt != NULL) {
		*out_count = insert_stmt->select_stmt->select_stmt->n_values_lists;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_cell_literal(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	sqlparser_literal_view_t *out_literal,
	sqlparser_error_t *out_error)
{
	PgQuery__AConst *literal;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;

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
	status = sqlparser_get_insert_cell_a_const(
		mutable_handle,
		statement_index,
		row_index,
		column_index,
		&literal,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return sqlparser_fill_literal_view_from_a_const_with_sql(
		literal,
		sqlparser_effective_parser_sql(handle),
		out_literal,
		out_error);
}

sqlparser_status_t sqlparser_insert_set_cell_literal_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	PgQuery__AConst *literal;
	sqlparser_status_t status;

	status = sqlparser_get_insert_cell_a_const(
		handle,
		statement_index,
		row_index,
		column_index,
		&literal,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_a_const_set_literal(literal, value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_handle_commit_ast(handle, out_error);
}

sqlparser_status_t sqlparser_insert_set_cell_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	char *rendered_sql;
	sqlparser_patch_t patch;
	sqlparser_literal_view_t current_literal;
	sqlparser_selector_t selector;
	sqlparser_status_t status;
	int multi_insert;

	multi_insert =
		handle != NULL &&
		sqlparser_dialect_state_has_multi_insert(
			handle->dialect,
			handle->dialect_state);
	rendered_sql = NULL;
	if (multi_insert) {
		status = sqlparser_dialect_multi_insert_render_literal_value(
			handle,
			value,
			&rendered_sql,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	} else {
		memset(&current_literal, 0, sizeof(current_literal));
		status = sqlparser_insert_cell_literal(
			handle,
			statement_index,
			row_index,
			column_index,
			&current_literal,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = statement_index;
	selector.row_index = row_index;
	selector.column_index = column_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = rendered_sql;
	patch.literal = multi_insert ? NULL : value;
	status = sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
	free(rendered_sql);
	return status;
}

sqlparser_status_t sqlparser_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	PgQuery__Node *value_node;
	sqlparser_status_t status;
	sqlparser_handle_t *mutable_handle;
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
	if (handle != NULL &&
	    sqlparser_dialect_state_has_multi_insert(handle->dialect, handle->dialect_state)) {
		return sqlparser_dialect_multi_insert_cell_sql(
			handle,
			statement_index,
			row_index,
			column_index,
			out_sql,
			out_error);
	}
	mutable_handle = (sqlparser_handle_t *)handle;
	status = sqlparser_get_insert_cell_node(
		mutable_handle,
		statement_index,
		row_index,
		column_index,
		&value_node,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (handle != NULL &&
	    (handle->generation == 0UL ||
	     handle->surface_source_complete)) {
		int source_status;

		source_status = sqlparser_view_insert_cell_source_sql(
			handle,
			statement_index,
			row_index,
			column_index,
			out_sql,
			out_error);
		if (source_status != 0) {
			return source_status > 0 ?
				SQLPARSER_STATUS_OK :
				(out_error != NULL ?
					out_error->code :
					SQLPARSER_STATUS_INTERNAL_ERROR);
		}
	}

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
		statement_index,
		core_sql,
		SQLPARSER_FRAGMENT_CONTEXT_EXPRESSION,
		(ProtobufCMessage *const *)&value_node,
		1U,
		"insert cell SQL",
		out_sql,
		out_error);
	free(core_sql);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_set_cell_sql_in_place(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	PgQuery__Node **value_slot;
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
	value_slot = NULL;
	replacement = NULL;
	if (handle != NULL &&
	    sqlparser_dialect_state_has_multi_insert(handle->dialect, handle->dialect_state)) {
		return sqlparser_dialect_multi_insert_set_cell_sql_in_place(
			handle,
			statement_index,
			row_index,
			column_index,
			sql_text,
			out_error);
	}
	status = sqlparser_preprocess_handle_sql_fragment_with_origins(
		handle,
		statement_index,
		sql_text,
		"insert cell SQL",
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
		&replacement,
		out_error);
	sqlparser_identifier_origin_map_destroy(origins);
	free(parser_sql);
	parser_sql = NULL;
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	status = sqlparser_get_insert_cell_slot(
		handle,
		statement_index,
		row_index,
		column_index,
		&value_slot,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(replacement);
		sqlparser_handle_sweep_identifier_spellings(handle);
		sqlparser_handle_discard_dialect_state(handle, dialect_state);
		return status;
	}

	sqlparser_free_proto_node(*value_slot);
	*value_slot = replacement;
	replacement = NULL;
	status = sqlparser_handle_commit_ast_with_dialect_state(
		handle, dialect_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_free_proto_node(replacement);
		return status;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_insert_set_cell_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;

	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = statement_index;
	selector.row_index = row_index;
	selector.column_index = column_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = sql_text;
	return sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
}

sqlparser_status_t sqlparser_insert_set_cell_bind(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	const sqlparser_bind_value_t *bind,
	sqlparser_error_t *out_error)
{
	char *rendered_sql;
	sqlparser_patch_t patch;
	sqlparser_selector_t selector;
	sqlparser_status_t status;
	int multi_insert;

	multi_insert =
		handle != NULL &&
		sqlparser_dialect_state_has_multi_insert(
			handle->dialect,
			handle->dialect_state);
	rendered_sql = NULL;
	if (multi_insert) {
		status = sqlparser_dialect_multi_insert_render_bind_value(
			handle,
			bind,
			&rendered_sql,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
	selector.statement_index = statement_index;
	selector.row_index = row_index;
	selector.column_index = column_index;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.sql = rendered_sql;
	patch.bind = multi_insert ? NULL : bind;
	status = sqlparser_selector_apply_single_patch(
		handle,
		&selector,
		&patch,
		out_error);
	free(rendered_sql);
	return status;
}
