#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"
#include "sqlparser_dialect_sqlserver_internal.h"
#include "sqlparser_dialect_sqlserver_control.h"
#include "sqlparser_dialect_sqlserver_output.h"
#include "sqlparser_dialect_sqlserver_scan.h"

#define SQLPARSER_SQLSERVER_SYSTEM_VARIABLE_PREFIX "@sqlparser_sqlserver_system_variable_"

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
	sqlparser_identifier_origin_writer_t origin_writer;
	size_t pending_input_offset;
	size_t pending_input_length;
} sqlparser_sqlserver_buffer_t;

typedef enum {
	SQLPARSER_SQLSERVER_CAST_TRY_CAST = 1,
	SQLPARSER_SQLSERVER_CAST_CONVERT,
	SQLPARSER_SQLSERVER_CAST_TRY_CONVERT,
	SQLPARSER_SQLSERVER_CAST_PARSE,
	SQLPARSER_SQLSERVER_CAST_TRY_PARSE
} sqlparser_sqlserver_cast_kind_t;

typedef struct {
	size_t ordinal;
	sqlparser_sqlserver_cast_kind_t kind;
	char *tail;
} sqlparser_sqlserver_cast_restore_t;

typedef enum {
	SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE = 0,
	SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_FROM,
	SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_JOIN,
	SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_UPDATE,
	SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_INSERT
} sqlparser_sqlserver_table_hint_anchor_t;

typedef struct {
	char *sql;
	sqlparser_sqlserver_table_hint_anchor_t anchor;
} sqlparser_sqlserver_table_hint_t;

typedef struct sqlparser_sqlserver_state sqlparser_sqlserver_state_t;

typedef struct {
	sqlparser_control_state_t *flow;
	size_t unit_count;
	sqlparser_sqlserver_state_t *unit_states[1];
} sqlparser_sqlserver_control_bundle_t;

struct sqlparser_sqlserver_state {
	char **param_names;
	size_t param_count;
	size_t param_capacity;
	char **unicode_literals;
	size_t *unicode_ordinals;
	size_t unicode_count;
	size_t unicode_capacity;
	size_t unicode_ordinal_capacity;
	size_t literal_count;
	char **top_limits;
	char **top_suffixes;
	size_t top_count;
	size_t top_capacity;
	size_t top_suffix_count;
	size_t top_suffix_capacity;
	sqlparser_sqlserver_table_hint_t *table_hints;
	size_t table_hint_count;
	size_t table_hint_capacity;
	char **query_hints;
	size_t query_hint_count;
	size_t query_hint_capacity;
	char **json_suffixes;
	size_t *json_suffix_ordinals;
	size_t json_suffix_count;
	size_t json_suffix_capacity;
	size_t json_suffix_ordinal_count;
	size_t json_suffix_ordinal_capacity;
	sqlparser_sqlserver_cast_restore_t *cast_restores;
	size_t cast_restore_count;
	size_t cast_restore_capacity;
	size_t cast_count;
	size_t rename_object_count;
	size_t drop_role_like_count;
	size_t *drop_user_ordinals;
	size_t drop_user_count;
	size_t drop_user_capacity;
	sqlparser_sqlserver_output_state_t *output_state;
	sqlparser_sqlserver_control_bundle_t *control;
};

typedef struct {
	char *limit;
	sqlparser_identifier_origin_map_t *origins;
	size_t source_offset;
	size_t depth;
} sqlparser_sqlserver_pending_top_t;

typedef struct {
	sqlparser_sqlserver_pending_top_t *items;
	size_t count;
	size_t capacity;
} sqlparser_sqlserver_pending_top_list_t;

static sqlparser_status_t sqlparser_sqlserver_preprocess_text(
	const char *input_sql,
	sqlparser_sqlserver_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_sqlserver_preprocess_text_origins(
	const char *input_sql,
	sqlparser_sqlserver_state_t *state,
	char **out_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_sqlserver_append_mapped_identifiers(
	sqlparser_sqlserver_buffer_t *out,
	const char *sql,
	const sqlparser_identifier_origin_map_t *origins,
	size_t source_offset,
	sqlparser_error_t *out_error);

static void sqlparser_sqlserver_buffer_release(sqlparser_sqlserver_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}

	free(buffer->data);
	sqlparser_identifier_origin_writer_release(&buffer->origin_writer);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
	buffer->pending_input_offset = 0U;
	buffer->pending_input_length = 0U;
}

static sqlparser_status_t sqlparser_sqlserver_buffer_begin_origin(
	sqlparser_sqlserver_buffer_t *buffer,
	sqlparser_identifier_origin_map_t *origins,
	size_t input_length,
	sqlparser_error_t *out_error)
{
	if (origins == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	return sqlparser_identifier_origin_writer_begin(
		&buffer->origin_writer,
		origins,
		input_length,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_commit_origin(
	sqlparser_sqlserver_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL || buffer->origin_writer.map == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (buffer->pending_input_length > 0U) {
		status = sqlparser_identifier_origin_writer_append_input(
			&buffer->origin_writer,
			buffer->pending_input_offset,
			buffer->pending_input_length,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		buffer->pending_input_length = 0U;
	}
	return sqlparser_identifier_origin_writer_commit(
		&buffer->origin_writer,
		buffer->len,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_flush_input_origin(
	sqlparser_sqlserver_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL || buffer->origin_writer.map == NULL ||
	    buffer->pending_input_length == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_identifier_origin_writer_append_input(
		&buffer->origin_writer,
		buffer->pending_input_offset,
		buffer->pending_input_length,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		buffer->pending_input_length = 0U;
	}
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_buffer_reserve(
	sqlparser_sqlserver_buffer_t *buffer,
	size_t extra,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t required;
	size_t next_capacity;

	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (extra > ((size_t)-1) - buffer->len - 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	required = buffer->len + extra + 1U;
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}

	next_capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
	while (next_capacity < required) {
		if (next_capacity > ((size_t)-1) / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next_capacity *= 2U;
	}

	next = (char *)realloc(buffer->data, next_capacity);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	buffer->data = next;
	buffer->capacity = next_capacity;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_buffer_append_raw_mem(
	sqlparser_sqlserver_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (len == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"append data must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_sqlserver_buffer_reserve(buffer, len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_buffer_append_mem(
	sqlparser_sqlserver_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer != NULL && buffer->origin_writer.map != NULL) {
		status = sqlparser_sqlserver_buffer_flush_input_origin(
			buffer,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_identifier_origin_writer_append_unknown(
			&buffer->origin_writer,
			len,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_sqlserver_buffer_append_raw_mem(
		buffer,
		data,
		len,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_append_input_mem(
	sqlparser_sqlserver_buffer_t *buffer,
	const char *input,
	size_t input_offset,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer != NULL && buffer->origin_writer.map != NULL) {
		if (buffer->pending_input_length > 0U &&
		    input_offset ==
			    buffer->pending_input_offset +
				    buffer->pending_input_length) {
			if (len > SIZE_MAX - buffer->pending_input_length) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_RESOURCE_LIMIT,
					"SQL Server origin span is too large");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			buffer->pending_input_length += len;
		} else {
			status = sqlparser_sqlserver_buffer_flush_input_origin(
				buffer,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			buffer->pending_input_offset = input_offset;
			buffer->pending_input_length = len;
		}
	}
	return sqlparser_sqlserver_buffer_append_raw_mem(
		buffer,
		input != NULL ? input + input_offset : NULL,
		len,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_append_source_identifier(
	sqlparser_sqlserver_buffer_t *buffer,
	const char *data,
	size_t input_offset,
	size_t input_length,
	size_t output_length,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer != NULL && buffer->origin_writer.map != NULL) {
		status = sqlparser_sqlserver_buffer_flush_input_origin(
			buffer,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_identifier_origin_writer_append_source_identifier(
			&buffer->origin_writer,
			input_offset,
			input_length,
			output_length,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_sqlserver_buffer_append_raw_mem(
		buffer,
		data,
		output_length,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_append_generated_identifier(
	sqlparser_sqlserver_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer != NULL && buffer->origin_writer.map != NULL) {
		status = sqlparser_sqlserver_buffer_flush_input_origin(
			buffer,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_identifier_origin_writer_append_generated_identifier(
			&buffer->origin_writer,
			len,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_sqlserver_buffer_append_raw_mem(
		buffer,
		data,
		len,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_append_char(
	sqlparser_sqlserver_buffer_t *buffer,
	char value,
	sqlparser_error_t *out_error)
{
	return sqlparser_sqlserver_buffer_append_mem(buffer, &value, 1U, out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_append_cstr(
	sqlparser_sqlserver_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	return sqlparser_sqlserver_buffer_append_mem(buffer, text, text != NULL ? strlen(text) : 0U, out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_finish(
	sqlparser_sqlserver_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_sqlserver_buffer_reserve(buffer, 0U, out_error);
}

static sqlparser_status_t sqlparser_sqlserver_buffer_reserve_input(
	sqlparser_sqlserver_buffer_t *buffer,
	const char *input,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t input_len;
	size_t required;

	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	input_len = input != NULL ? strlen(input) : 0U;
	if (input_len == (size_t)-1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	required = input_len + 1U;
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}

	next = (char *)realloc(buffer->data, required);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	buffer->data = next;
	buffer->capacity = required;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static char *sqlparser_sqlserver_buffer_take(sqlparser_sqlserver_buffer_t *buffer)
{
	char *data;

	if (buffer == NULL) {
		return NULL;
	}

	data = buffer->data;
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
	buffer->pending_input_offset = 0U;
	buffer->pending_input_length = 0U;
	sqlparser_identifier_origin_writer_release(&buffer->origin_writer);
	return data;
}

static void sqlparser_sqlserver_state_destroy(void *state)
{
	sqlparser_sqlserver_state_t *sqlserver_state;
	size_t index;

	sqlserver_state = (sqlparser_sqlserver_state_t *)state;
	if (sqlserver_state == NULL) {
		return;
	}
	if (sqlserver_state->control != NULL) {
		sqlparser_control_state_release(sqlserver_state->control->flow);
		for (index = 1U; index < sqlserver_state->control->unit_count; index++) {
			sqlparser_sqlserver_state_destroy(sqlserver_state->control->unit_states[index]);
		}
		free(sqlserver_state->control);
	}

	for (index = 0U; index < sqlserver_state->param_count; index++) {
		free(sqlserver_state->param_names[index]);
	}
	for (index = 0U; index < sqlserver_state->unicode_count; index++) {
		free(sqlserver_state->unicode_literals[index]);
	}
	for (index = 0U; index < sqlserver_state->top_count; index++) {
		free(sqlserver_state->top_limits[index]);
	}
	for (index = 0U; index < sqlserver_state->top_suffix_count; index++) {
		free(sqlserver_state->top_suffixes[index]);
	}
	for (index = 0U; index < sqlserver_state->table_hint_count; index++) {
		free(sqlserver_state->table_hints[index].sql);
	}
	for (index = 0U; index < sqlserver_state->query_hint_count; index++) {
		free(sqlserver_state->query_hints[index]);
	}
	for (index = 0U; index < sqlserver_state->json_suffix_count; index++) {
		free(sqlserver_state->json_suffixes[index]);
	}
	for (index = 0U; index < sqlserver_state->cast_restore_count; index++) {
		free(sqlserver_state->cast_restores[index].tail);
	}
	free(sqlserver_state->param_names);
	free(sqlserver_state->unicode_literals);
	free(sqlserver_state->unicode_ordinals);
	free(sqlserver_state->top_limits);
	free(sqlserver_state->top_suffixes);
	free(sqlserver_state->table_hints);
	free(sqlserver_state->query_hints);
	free(sqlserver_state->json_suffixes);
	free(sqlserver_state->json_suffix_ordinals);
	free(sqlserver_state->cast_restores);
	free(sqlserver_state->drop_user_ordinals);
	sqlparser_sqlserver_output_destroy(sqlserver_state->output_state);
	free(sqlserver_state);
}

static sqlparser_status_t sqlparser_sqlserver_state_new(
	sqlparser_sqlserver_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_state_t *state;

	if (out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	state = (sqlparser_sqlserver_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_control_bundle_new(
	sqlparser_sqlserver_state_t *root_state,
	sqlparser_control_state_t *flow,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_control_bundle_t *bundle;
	size_t bytes;
	size_t index;
	sqlparser_status_t status;

	if (root_state == NULL || flow == NULL || flow->unit_count == 0U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server control state is incomplete");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (flow->unit_count - 1U >
	    (SIZE_MAX - sizeof(*bundle)) / sizeof(bundle->unit_states[0])) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "SQL Server control unit state is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	bytes = sizeof(*bundle) +
		(flow->unit_count - 1U) * sizeof(bundle->unit_states[0]);
	bundle = (sqlparser_sqlserver_control_bundle_t *)calloc(1U, bytes);
	if (bundle == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	bundle->flow = flow;
	bundle->unit_count = flow->unit_count;
	bundle->unit_states[0] = root_state;
	for (index = 1U; index < flow->unit_count; index++) {
		status = sqlparser_sqlserver_state_new(&bundle->unit_states[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			while (index > 1U) {
				index--;
				sqlparser_sqlserver_state_destroy(bundle->unit_states[index]);
			}
			free(bundle);
			return status;
		}
	}
	root_state->control = bundle;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_sqlserver_state_t *sqlparser_sqlserver_state_for_statement(
	const sqlparser_sqlserver_state_t *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	if (state == NULL) {
		return NULL;
	}
	if (state->control == NULL) {
		if (out_local_statement_index != NULL) {
			*out_local_statement_index = statement_index;
		}
		return state;
	}
	if (statement_index >= state->control->unit_count) {
		return NULL;
	}
	if (out_local_statement_index != NULL) {
		*out_local_statement_index = 0U;
	}
	return state->control->unit_states[statement_index];
}

static sqlparser_sqlserver_state_t *sqlparser_sqlserver_state_for_statement_mutable(
	sqlparser_sqlserver_state_t *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	return (sqlparser_sqlserver_state_t *)sqlparser_sqlserver_state_for_statement(
		state, statement_index, out_local_statement_index);
}

static sqlparser_status_t sqlparser_sqlserver_size_array_add(
	size_t **values,
	size_t *count,
	size_t *capacity,
	size_t value,
	sqlparser_error_t *out_error)
{
	size_t next_capacity;
	size_t *next;

	if (values == NULL || count == NULL || capacity == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "array output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (*count == *capacity) {
		next_capacity = *capacity == 0U ? 4U : *capacity * 2U;
		if (next_capacity < *capacity ||
		    next_capacity > ((size_t)-1) / sizeof(**values)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (size_t *)realloc(*values, next_capacity * sizeof(**values));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		*values = next;
		*capacity = next_capacity;
	}

	(*values)[*count] = value;
	(*count)++;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_sqlserver_span_has_space(const char *text, size_t start, size_t end)
{
	size_t pos;

	for (pos = start; pos < end; pos++) {
		if (isspace((unsigned char)text[pos])) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_record_drop_role_like(
	const char *input,
	size_t index,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t pos;

	if (input == NULL || state == NULL || !sqlparser_sqlserver_ascii_word_equal(input, index, "drop")) {
		return SQLPARSER_STATUS_OK;
	}

	pos = sqlparser_sqlserver_skip_space(input, index + strlen("drop"));
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "role")) {
		state->drop_role_like_count++;
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_sqlserver_ascii_word_equal(input, pos, "user")) {
		return SQLPARSER_STATUS_OK;
	}

	state->drop_role_like_count++;
	return sqlparser_sqlserver_size_array_add(
		&state->drop_user_ordinals,
		&state->drop_user_count,
		&state->drop_user_capacity,
		state->drop_role_like_count,
		out_error);
}

static int sqlparser_sqlserver_is_drop_user_ordinal(
	const sqlparser_sqlserver_state_t *state,
	size_t ordinal)
{
	size_t index;

	if (state == NULL) {
		return 0;
	}
	for (index = 0U; index < state->drop_user_count; index++) {
		if (state->drop_user_ordinals[index] == ordinal) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_append_pg_quoted_identifier(
	sqlparser_sqlserver_buffer_t *out,
	const char *text,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_sqlserver_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (text[pos] == '"') {
			status = sqlparser_sqlserver_buffer_append_cstr(out, "\"\"", out_error);
		} else {
			status = sqlparser_sqlserver_buffer_append_char(out, text[pos], out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_sqlserver_buffer_append_char(out, '"', out_error);
}

static int sqlparser_sqlserver_word_followed_by_lparen(const char *text, size_t pos, const char *word)
{
	size_t next;

	if (!sqlparser_sqlserver_ascii_word_equal(text, pos, word)) {
		return 0;
	}
	next = sqlparser_sqlserver_skip_space(text, pos + strlen(word));
	return text[next] == '(';
}

static sqlparser_status_t sqlparser_sqlserver_append_quoted_identifier(
	sqlparser_sqlserver_buffer_t *out,
	const char *input,
	size_t *index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t quoted;
	size_t source_start;
	size_t pos;
	sqlparser_status_t status;

	memset(&quoted, 0, sizeof(quoted));
	source_start = *index;
	status = sqlparser_sqlserver_buffer_append_char(&quoted, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + 1U;
	while (input[pos] != '\0') {
		if (input[pos] == ']') {
			if (input[pos + 1U] == ']') {
				status = sqlparser_sqlserver_buffer_append_char(
					&quoted, ']', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&quoted);
					return status;
				}
				pos += 2U;
				continue;
			}
			status = sqlparser_sqlserver_buffer_append_char(
				&quoted, '"', out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&quoted);
				return status;
			}
			*index = pos + 1U;
			status = sqlparser_sqlserver_buffer_append_source_identifier(
				out,
				quoted.data,
				source_start,
				*index - source_start,
				quoted.len,
				out_error);
			sqlparser_sqlserver_buffer_release(&quoted);
			return status;
		}
		if (input[pos] == '"') {
			status = sqlparser_sqlserver_buffer_append_cstr(
				&quoted, "\"\"", out_error);
		} else {
			status = sqlparser_sqlserver_buffer_append_char(
				&quoted, input[pos], out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&quoted);
			return status;
		}
		pos++;
	}

	sqlparser_sqlserver_buffer_release(&quoted);
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated SQL Server bracket-delimited identifier");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_sqlserver_copy_quoted_or_comment(
	const char *sql,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t next;
	sqlparser_status_t status;

	if (sql[*index] == '[') {
		return sqlparser_sqlserver_append_quoted_identifier(out, sql, index, out_error);
	}

	if (sqlparser_sqlserver_can_copy_quoted_or_comment(sql, *index)) {
		next = *index;
		status = sqlparser_sqlserver_quoted_or_comment_span(
			sql,
			*index,
			&next,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_sqlserver_buffer_append_input_mem(
			out,
			sql,
			*index,
			next - *index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		*index = next;
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_use_statement(
	const char *input_sql,
	char **out_sql,
	size_t *out_name_start,
	size_t *out_name_end,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	size_t start;
	size_t end;
	size_t name_start;
	size_t name_end;
	size_t index;
	sqlparser_status_t status;

	if (out_sql == NULL || out_name_start == NULL || out_name_end == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "USE output arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	*out_name_start = 0U;
	*out_name_end = 0U;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = sqlparser_sqlserver_trim_left(input_sql, 0U, strlen(input_sql));
	end = sqlparser_sqlserver_trim_right(input_sql, start, strlen(input_sql));
	if (end > start && input_sql[end - 1U] == ';') {
		end--;
		end = sqlparser_sqlserver_trim_right(input_sql, start, end);
	}
	if (end - start < strlen("use") ||
	    !sqlparser_sqlserver_ascii_word_equal(input_sql, start, "use")) {
		return SQLPARSER_STATUS_OK;
	}
	name_start = start + strlen("use");
	if (name_start >= end || !isspace((unsigned char)input_sql[name_start])) {
		return SQLPARSER_STATUS_OK;
	}
	name_start = sqlparser_sqlserver_trim_left(input_sql, name_start, end);
	name_end = sqlparser_sqlserver_trim_right(input_sql, name_start, end);
	if (name_start >= name_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE requires a database name");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_sqlserver_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(&out, SQLPARSER_INTERNAL_CURRENT_DATABASE, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(&out, " = ", out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	if (input_sql[name_start] == '[') {
		index = name_start;
		status = sqlparser_sqlserver_append_quoted_identifier(&out, input_sql, &index, out_error);
		if (status == SQLPARSER_STATUS_OK &&
		    sqlparser_sqlserver_trim_left(input_sql, index, name_end) != name_end) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE database name has trailing text");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
	} else if (input_sql[name_start] == '"') {
		index = name_start + 1U;
		while (index < name_end) {
			if (input_sql[index] == '"' && index + 1U < name_end && input_sql[index + 1U] == '"') {
				index += 2U;
				continue;
			}
			if (input_sql[index] == '"') {
				index++;
				break;
			}
			index++;
		}
		if (index != name_end) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE database name has trailing text");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_sqlserver_buffer_append_mem(
			&out,
			input_sql + name_start,
			name_end - name_start,
			out_error);
	} else {
		if (sqlparser_sqlserver_span_has_space(input_sql, name_start, name_end)) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE database name must be quoted when it contains whitespace");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_sqlserver_append_pg_quoted_identifier(
			&out,
			input_sql,
			name_start,
			name_end,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_name_start = name_start;
	*out_name_end = name_end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_append_internal_string_literal(
	sqlparser_sqlserver_buffer_t *out,
	const char *text,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_sqlserver_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (text[pos] == '\'') {
			status = sqlparser_sqlserver_buffer_append_cstr(out, "''", out_error);
		} else {
			status = sqlparser_sqlserver_buffer_append_char(out, text[pos], out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_sqlserver_buffer_append_char(out, '\'', out_error);
}

static int sqlparser_sqlserver_is_simple_public_identifier(const char *text, size_t start, size_t end)
{
	size_t pos;

	if (text == NULL || start >= end ||
	    !sqlparser_sqlserver_is_ident_start((unsigned char)text[start])) {
		return 0;
	}
	for (pos = start + 1U; pos < end; pos++) {
		if (!sqlparser_sqlserver_is_ident_char((unsigned char)text[pos])) {
			return 0;
		}
	}
	return 1;
}

static sqlparser_status_t sqlparser_sqlserver_append_public_bracket_identifier(
	sqlparser_sqlserver_buffer_t *out,
	const char *sql,
	size_t *index,
	size_t end,
	sqlparser_error_t *out_error)
{
	size_t start;
	size_t pos;
	size_t content_start;
	int escaped;

	if (out == NULL || sql == NULL || index == NULL || sql[*index] != '[') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bracket identifier arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = *index;
	content_start = start + 1U;
	pos = content_start;
	escaped = 0;
	while (pos < end && sql[pos] != '\0') {
		if (sql[pos] == ']') {
			if (pos + 1U < end && sql[pos + 1U] == ']') {
				escaped = 1;
				pos += 2U;
				continue;
			}
			if (!escaped && sqlparser_sqlserver_is_simple_public_identifier(sql, content_start, pos)) {
				*index = pos + 1U;
				return sqlparser_sqlserver_buffer_append_mem(out, sql + content_start, pos - content_start, out_error);
			}
			*index = pos + 1U;
			return sqlparser_sqlserver_buffer_append_mem(out, sql + start, *index - start, out_error);
		}
		pos++;
	}

	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated SQL Server bracket-delimited identifier");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_sqlserver_append_public_normalized_range(
	sqlparser_sqlserver_buffer_t *out,
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	pos = start;
	while (pos < end && sql[pos] != '\0') {
		size_t skipped;

		if (sql[pos] == '[') {
			status = sqlparser_sqlserver_append_public_bracket_identifier(out, sql, &pos, end, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			continue;
		}

		skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos && skipped <= end) {
			status = sqlparser_sqlserver_buffer_append_mem(out, sql + pos, skipped - pos, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			pos = skipped;
			continue;
		}

		status = sqlparser_sqlserver_buffer_append_char(out, sql[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_sqlserver_identifier_token_end(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	if (sql == NULL || start >= end) {
		return start;
	}
	if (sql[start] == '[') {
		pos = start + 1U;
		while (pos < end && sql[pos] != '\0') {
			if (sql[pos] == ']') {
				if (pos + 1U < end && sql[pos + 1U] == ']') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return start;
	}
	if (sql[start] == '"') {
		pos = start + 1U;
		while (pos < end && sql[pos] != '\0') {
			if (sql[pos] == '"') {
				if (pos + 1U < end && sql[pos + 1U] == '"') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return start;
	}

	pos = start;
	while (pos < end &&
	       sql[pos] != '\0' &&
	       !isspace((unsigned char)sql[pos]) &&
	       sql[pos] != ',' &&
	       sql[pos] != ';' &&
	       sql[pos] != '(' &&
	       sql[pos] != ')' &&
	       sql[pos] != '=') {
		pos++;
	}
	return pos;
}

static int sqlparser_sqlserver_word_at_bounded(
	const char *sql,
	size_t pos,
	size_t end,
	const char *word)
{
	size_t len;

	if (sql == NULL || word == NULL || pos >= end) {
		return 0;
	}
	len = strlen(word);
	return len <= end - pos &&
		sqlparser_sqlserver_ascii_word_equal(sql, pos, word);
}

static int sqlparser_sqlserver_ascii_span_equal(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	size_t index;
	size_t word_len;

	if (sql == NULL || word == NULL || start > end) {
		return 0;
	}
	word_len = strlen(word);
	if (end - start != word_len) {
		return 0;
	}
	for (index = 0U; index < word_len; index++) {
		if (tolower((unsigned char)sql[start + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static size_t sqlparser_sqlserver_skip_space_bounded(const char *sql, size_t pos, size_t end)
{
	while (pos < end && isspace((unsigned char)sql[pos])) {
		pos++;
	}
	return pos;
}

static int sqlparser_sqlserver_identifier_token_equal(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	if (sql == NULL || start >= end) {
		return 0;
	}
	if ((sql[start] == '[' && sql[end - 1U] == ']') ||
	    (sql[start] == '"' && sql[end - 1U] == '"')) {
		start++;
		end--;
	}
	return sqlparser_sqlserver_ascii_span_equal(sql, start, end, word);
}

static size_t sqlparser_sqlserver_multipart_identifier_end(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	size_t part_end;
	size_t parts;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	part_end = sqlparser_sqlserver_identifier_token_end(sql, pos, end);
	if (part_end <= pos) {
		return start;
	}
	pos = part_end;
	parts = 1U;
	while (parts < 4U) {
		size_t dot_pos;

		dot_pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
		if (dot_pos >= end || sql[dot_pos] != '.') {
			break;
		}
		pos = sqlparser_sqlserver_skip_space_bounded(sql, dot_pos + 1U, end);
		part_end = sqlparser_sqlserver_identifier_token_end(sql, pos, end);
		if (part_end <= pos) {
			return start;
		}
		pos = part_end;
		parts++;
	}
	return pos;
}

static int sqlparser_sqlserver_tail_starts_with_two_words(
	const char *sql,
	size_t pos,
	size_t end,
	const char *first,
	const char *second)
{
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, first)) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen(first), end);
	return sqlparser_sqlserver_word_at_bounded(sql, pos, end, second);
}

static int sqlparser_sqlserver_has_identifier_after_word_sequence(
	const char *sql,
	size_t pos,
	size_t end,
	const char *first,
	const char *second)
{
	size_t ident_start;

	if (!sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, first, second)) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen(first), end);
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen(second), end);
	ident_start = pos;
	return sqlparser_sqlserver_identifier_token_end(sql, ident_start, end) > ident_start;
}

static int sqlparser_sqlserver_create_user_with_option_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	static const char *const option_names[] = {
		"default_schema",
		"default_language",
		"sid",
		"type",
		"password",
		"allow_encrypted_value_modifications",
		"object_id"
	};
	size_t option_pos;
	size_t index;

	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "with")) {
		return 0;
	}
	option_pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
	for (index = 0U; index < sizeof(option_names) / sizeof(option_names[0]); index++) {
		if (sqlparser_sqlserver_word_at_bounded(sql, option_pos, end, option_names[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_create_user_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t next;

	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, "without", "login")) {
		return 1;
	}
	if (sqlparser_sqlserver_create_user_with_option_is_supported(sql, pos, end)) {
		return 1;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "for") ||
	    sqlparser_sqlserver_word_at_bounded(sql, pos, end, "from")) {
		next = sqlparser_sqlserver_skip_space_bounded(
			sql,
			pos + (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "for") ? strlen("for") : strlen("from")),
			end);
		if (sqlparser_sqlserver_word_at_bounded(sql, next, end, "login") ||
		    sqlparser_sqlserver_word_at_bounded(sql, next, end, "certificate") ||
		    sqlparser_sqlserver_tail_starts_with_two_words(sql, next, end, "asymmetric", "key") ||
		    sqlparser_sqlserver_tail_starts_with_two_words(sql, next, end, "external", "provider")) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_application_role_create_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t option_pos;

	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "with")) {
		return 0;
	}
	option_pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
	return sqlparser_sqlserver_word_at_bounded(sql, option_pos, end, "password");
}

static int sqlparser_sqlserver_application_role_alter_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	static const char *const option_names[] = {
		"name",
		"password",
		"default_schema"
	};
	size_t option_pos;
	size_t index;

	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "with")) {
		return 0;
	}
	option_pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
	for (index = 0U; index < sizeof(option_names) / sizeof(option_names[0]); index++) {
		if (sqlparser_sqlserver_word_at_bounded(sql, option_pos, end, option_names[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_alter_user_with_option_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	static const char *const option_names[] = {
		"name",
		"default_schema",
		"login",
		"password",
		"default_language",
		"allow_encrypted_value_modifications"
	};
	size_t option_pos;
	size_t index;

	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "with")) {
		return 0;
	}
	option_pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
	for (index = 0U; index < sizeof(option_names) / sizeof(option_names[0]); index++) {
		if (sqlparser_sqlserver_word_at_bounded(sql, option_pos, end, option_names[index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_alter_user_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t next;

	if (sqlparser_sqlserver_alter_user_with_option_is_supported(sql, pos, end)) {
		return 1;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "for") ||
	    sqlparser_sqlserver_word_at_bounded(sql, pos, end, "from")) {
		next = sqlparser_sqlserver_skip_space_bounded(
			sql,
			pos + (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "for") ? strlen("for") : strlen("from")),
			end);
		if (sqlparser_sqlserver_word_at_bounded(sql, next, end, "certificate") ||
		    sqlparser_sqlserver_tail_starts_with_two_words(sql, next, end, "asymmetric", "key") ||
		    sqlparser_sqlserver_tail_starts_with_two_words(sql, next, end, "external", "provider")) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_create_role_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t owner_pos;
	size_t owner_end;

	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "authorization")) {
		return 0;
	}
	owner_pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("authorization"), end);
	owner_end = sqlparser_sqlserver_identifier_token_end(sql, owner_pos, end);
	return owner_end > owner_pos && sqlparser_sqlserver_skip_space_bounded(sql, owner_end, end) >= end;
}

static int sqlparser_sqlserver_alter_role_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	if (sqlparser_sqlserver_has_identifier_after_word_sequence(sql, pos, end, "add", "member") ||
	    sqlparser_sqlserver_has_identifier_after_word_sequence(sql, pos, end, "drop", "member")) {
		return 1;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "with")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
		if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "name")) {
			return 0;
		}
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("name"), end);
		if (pos >= end || sql[pos] != '=') {
			return 0;
		}
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
		return sqlparser_sqlserver_identifier_token_end(sql, pos, end) > pos;
	}
	return 0;
}

static int sqlparser_sqlserver_find_word_bounded(
	const char *sql,
	size_t pos,
	size_t end,
	const char *word,
	size_t *out_pos)
{
	size_t scan;
	size_t word_len;

	if (sql == NULL || word == NULL) {
		return 0;
	}
	word_len = strlen(word);
	scan = pos;
	while (scan < end && sql[scan] != '\0') {
		size_t skipped;

		skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, scan);
		if (skipped > scan) {
			scan = skipped;
			continue;
		}
		if (scan + word_len <= end &&
		    sqlparser_sqlserver_ascii_word_equal(sql, scan, word)) {
			if (out_pos != NULL) {
				*out_pos = scan;
			}
			return 1;
		}
		scan++;
	}
	return 0;
}

static int sqlparser_sqlserver_create_synonym_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t name_start;
	size_t name_end;
	size_t for_pos;
	size_t base_start;

	name_start = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	name_end = sqlparser_sqlserver_multipart_identifier_end(sql, name_start, end);
	if (name_end <= name_start) {
		return 0;
	}
	if (!sqlparser_sqlserver_find_word_bounded(sql, name_end, end, "for", &for_pos)) {
		return 0;
	}
	if (sqlparser_sqlserver_trim_right(sql, name_end, for_pos) > name_end) {
		return 0;
	}
	base_start = sqlparser_sqlserver_skip_space_bounded(sql, for_pos + strlen("for"), end);
	return sqlparser_sqlserver_multipart_identifier_end(sql, base_start, end) > base_start;
}

static int sqlparser_sqlserver_create_type_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t name_start;
	size_t name_end;
	size_t tail_start;

	name_start = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	name_end = sqlparser_sqlserver_multipart_identifier_end(sql, name_start, end);
	if (name_end <= name_start) {
		return 0;
	}
	tail_start = sqlparser_sqlserver_skip_space_bounded(sql, name_end, end);
	if (sqlparser_sqlserver_word_at_bounded(sql, tail_start, end, "from")) {
		tail_start = sqlparser_sqlserver_skip_space_bounded(sql, tail_start + strlen("from"), end);
		return sqlparser_sqlserver_multipart_identifier_end(sql, tail_start, end) > tail_start;
	}
	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, tail_start, end, "external", "name")) {
		tail_start = sqlparser_sqlserver_skip_space_bounded(sql, tail_start + strlen("external"), end);
		tail_start = sqlparser_sqlserver_skip_space_bounded(sql, tail_start + strlen("name"), end);
		return sqlparser_sqlserver_identifier_token_end(sql, tail_start, end) > tail_start;
	}
	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, tail_start, end, "as", "table")) {
		tail_start = sqlparser_sqlserver_skip_space_bounded(sql, tail_start + strlen("as"), end);
		tail_start = sqlparser_sqlserver_skip_space_bounded(sql, tail_start + strlen("table"), end);
		return tail_start < end && sql[tail_start] == '(';
	}
	return 0;
}

static int sqlparser_sqlserver_drop_synonym_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t name_start;
	size_t name_end;

	name_start = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, name_start, end, "if", "exists")) {
		name_start = sqlparser_sqlserver_skip_space_bounded(sql, name_start + strlen("if"), end);
		name_start = sqlparser_sqlserver_skip_space_bounded(sql, name_start + strlen("exists"), end);
	}
	name_end = sqlparser_sqlserver_multipart_identifier_end(sql, name_start, end);
	return name_end > name_start &&
		sqlparser_sqlserver_skip_space_bounded(sql, name_end, end) >= end;
}

static int sqlparser_sqlserver_digits_only_range(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	if (sql == NULL || start >= end) {
		return 0;
	}
	for (pos = start; pos < end; pos++) {
		if (!isdigit((unsigned char)sql[pos])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_sqlserver_compatibility_level_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	static const char *const levels[] = {
		"170",
		"160",
		"150",
		"140",
		"130",
		"120",
		"110",
		"100",
		"90",
		"80"
	};
	size_t index;
	size_t len;

	if (!sqlparser_sqlserver_digits_only_range(sql, start, end)) {
		return 0;
	}
	len = end - start;
	for (index = 0U; index < sizeof(levels) / sizeof(levels[0]); index++) {
		if (strlen(levels[index]) == len &&
		    strncmp(sql + start, levels[index], len) == 0) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_alter_database_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t name_start;
	size_t name_end;
	size_t value_start;
	size_t value_end;

	name_start = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	name_end = sqlparser_sqlserver_identifier_token_end(sql, name_start, end);
	if (name_end <= name_start) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, name_end, end);
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "set")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("set"), end);
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "compatibility_level")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("compatibility_level"), end);
	if (pos >= end || sql[pos] != '=') {
		return 0;
	}
	value_start = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
	value_end = sqlparser_sqlserver_trim_right(sql, value_start, end);
	return sqlparser_sqlserver_compatibility_level_is_supported(sql, value_start, value_end);
}

static int sqlparser_sqlserver_drop_index_entry_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t index_start;
	size_t index_end;
	size_t pos;
	size_t object_start;
	size_t object_end;

	index_start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	index_end = sqlparser_sqlserver_multipart_identifier_end(sql, index_start, end);
	if (index_end <= index_start) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, index_end, end);
	if (pos >= end) {
		return 1;
	}
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "on")) {
		return 0;
	}
	object_start = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("on"), end);
	object_end = sqlparser_sqlserver_multipart_identifier_end(sql, object_start, end);
	return object_end > object_start &&
		sqlparser_sqlserver_skip_space_bounded(sql, object_end, end) >= end;
}

static int sqlparser_sqlserver_drop_index_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t entry_start;
	size_t scan;

	entry_start = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, entry_start, end, "if", "exists")) {
		entry_start = sqlparser_sqlserver_skip_space_bounded(sql, entry_start + strlen("if"), end);
		entry_start = sqlparser_sqlserver_skip_space_bounded(sql, entry_start + strlen("exists"), end);
	}
	if (entry_start >= end) {
		return 0;
	}
	scan = entry_start;
	while (scan <= end) {
		size_t skipped;
		size_t entry_end;

		skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, scan);
		if (skipped > scan && skipped <= end) {
			scan = skipped;
			continue;
		}
		if (scan == end || sql[scan] == ',') {
			entry_end = sqlparser_sqlserver_trim_right(sql, entry_start, scan);
			if (!sqlparser_sqlserver_drop_index_entry_is_supported(sql, entry_start, entry_end)) {
				return 0;
			}
			if (scan == end) {
				return 1;
			}
			entry_start = sqlparser_sqlserver_skip_space_bounded(sql, scan + 1U, end);
			if (entry_start >= end) {
				return 0;
			}
			scan = entry_start;
			continue;
		}
		scan++;
	}
	return 0;
}

static size_t sqlparser_sqlserver_parenthesized_tail_end(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;
	size_t depth;

	if (sql == NULL || start >= end || sql[start] != '(') {
		return start;
	}
	depth = 1U;
	pos = start + 1U;
	while (pos < end && sql[pos] != '\0') {
		size_t skipped;

		skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos && skipped <= end) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')') {
			depth--;
			if (depth == 0U) {
				return pos + 1U;
			}
		}
		pos++;
	}
	return start;
}

static int sqlparser_sqlserver_update_statistics_with_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	static const char *const option_names[] = {
		"fullscan",
		"sample",
		"resample",
		"all",
		"columns",
		"index",
		"norecompute",
		"incremental",
		"persist_sample_percent",
		"stats_stream",
		"rowcount",
		"pagecount",
		"maxdop",
		"auto_drop"
	};
	size_t option_index;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (pos >= end) {
		return 0;
	}
	for (option_index = 0U; option_index < sizeof(option_names) / sizeof(option_names[0]); option_index++) {
		if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, option_names[option_index])) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_update_statistics_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t table_start;
	size_t table_end;
	size_t tail_start;
	size_t tail_end;

	table_start = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	table_end = sqlparser_sqlserver_multipart_identifier_end(sql, table_start, end);
	if (table_end <= table_start) {
		return 0;
	}
	tail_start = sqlparser_sqlserver_skip_space_bounded(sql, table_end, end);
	if (tail_start >= end) {
		return 1;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, tail_start, end, "with")) {
		tail_start = sqlparser_sqlserver_skip_space_bounded(sql, tail_start + strlen("with"), end);
		return sqlparser_sqlserver_update_statistics_with_tail_is_supported(sql, tail_start, end);
	}
	if (sql[tail_start] == '(') {
		tail_end = sqlparser_sqlserver_parenthesized_tail_end(sql, tail_start, end);
		if (tail_end <= tail_start) {
			return 0;
		}
	} else {
		tail_end = sqlparser_sqlserver_multipart_identifier_end(sql, tail_start, end);
		if (tail_end <= tail_start) {
			return 0;
		}
	}
	tail_start = sqlparser_sqlserver_skip_space_bounded(sql, tail_end, end);
	if (tail_start >= end) {
		return 1;
	}
	if (!sqlparser_sqlserver_word_at_bounded(sql, tail_start, end, "with")) {
		return 0;
	}
	tail_start = sqlparser_sqlserver_skip_space_bounded(sql, tail_start + strlen("with"), end);
	return sqlparser_sqlserver_update_statistics_with_tail_is_supported(sql, tail_start, end);
}

static int sqlparser_sqlserver_word_at_bounded_len(
	const char *sql,
	size_t pos,
	size_t end,
	const char *word,
	size_t word_len)
{
	size_t index;
	unsigned char prev;
	unsigned char next;

	if (sql == NULL || word == NULL || word_len == 0U || pos + word_len > end) {
		return 0;
	}
	for (index = 0U; index < word_len; index++) {
		if (tolower((unsigned char)sql[pos + index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	prev = pos == 0U ? 0U : (unsigned char)sql[pos - 1U];
	next = pos + word_len >= end ? 0U : (unsigned char)sql[pos + word_len];
	return !sqlparser_sqlserver_is_ident_char(prev) && !sqlparser_sqlserver_is_ident_char(next);
}

static int sqlparser_sqlserver_set_option_sequence_at(
	const char *sql,
	size_t pos,
	size_t end,
	const char *sequence,
	size_t *out_end)
{
	size_t seq_pos;
	size_t word_start;
	size_t word_end;
	size_t scan;

	if (sql == NULL || sequence == NULL) {
		return 0;
	}

	scan = pos;
	seq_pos = 0U;
	while (sequence[seq_pos] != '\0') {
		while (sequence[seq_pos] == ' ') {
			seq_pos++;
		}
		if (sequence[seq_pos] == '\0') {
			break;
		}

		word_start = seq_pos;
		while (sequence[seq_pos] != '\0' && sequence[seq_pos] != ' ') {
			seq_pos++;
		}
		word_end = seq_pos;
		if (word_end <= word_start ||
		    !sqlparser_sqlserver_word_at_bounded_len(
			    sql,
			    scan,
			    end,
			    sequence + word_start,
			    word_end - word_start)) {
			return 0;
		}

		scan += word_end - word_start;
		scan = sqlparser_sqlserver_skip_space_bounded(sql, scan, end);
	}

	if (out_end != NULL) {
		*out_end = scan;
	}
	return 1;
}

static int sqlparser_sqlserver_variable_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	if (start >= end || sql[start] != '@') {
		return 0;
	}
	pos = start + 1U;
	if (pos >= end ||
	    !(isalpha((unsigned char)sql[pos]) || sql[pos] == '_' || sql[pos] == '@')) {
		return 0;
	}
	pos++;
	while (pos < end && sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos])) {
		pos++;
	}
	return sqlparser_sqlserver_skip_space_bounded(sql, pos, end) >= end;
}

static int sqlparser_sqlserver_integer_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (start < end && sql[start] == '-') {
		start++;
	}
	return sqlparser_sqlserver_digits_only_range(sql, start, end);
}

static int sqlparser_sqlserver_integer_or_variable_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	return sqlparser_sqlserver_integer_tail_is_supported(sql, start, end) ||
		sqlparser_sqlserver_variable_tail_is_supported(sql, start, end);
}

static int sqlparser_sqlserver_set_on_off_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end,
	int allow_multiple)
{
	static const char *const option_names[] = {
		"ansi_defaults",
		"ansi_null_dflt_off",
		"ansi_null_dflt_on",
		"ansi_nulls",
		"ansi_padding",
		"ansi_warnings",
		"arithabort",
		"arithignore",
		"concat_null_yields_null",
		"cursor_close_on_commit",
		"fmtonly",
		"forceplan",
		"implicit_transactions",
		"nocount",
		"noexec",
		"numeric_roundabort",
		"parseonly",
		"quoted_identifier",
		"remote_proc_transactions",
		"result_set_caching",
		"showplan_all",
		"showplan_text",
		"showplan_xml",
		"statistics io",
		"statistics profile",
		"statistics time",
		"statistics xml",
		"xact_abort"
	};
	size_t pos;
	size_t next;
	size_t index;
	int matched;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	for (;;) {
		matched = 0;
		for (index = 0U; index < sizeof(option_names) / sizeof(option_names[0]); index++) {
			if (sqlparser_sqlserver_set_option_sequence_at(sql, pos, end, option_names[index], &next)) {
				matched = 1;
				break;
			}
		}
		if (!matched) {
			return 0;
		}

		pos = sqlparser_sqlserver_skip_space_bounded(sql, next, end);
		if (pos < end && sql[pos] == ',') {
			if (!allow_multiple || strstr(option_names[index], "statistics ") == option_names[index]) {
				return 0;
			}
			pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
			continue;
		}
		break;
	}

	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "on")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("on"), end);
		return pos >= end;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "off")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("off"), end);
		return pos >= end;
	}
	return 0;
}

static int sqlparser_sqlserver_datefirst_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t value;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (sqlparser_sqlserver_variable_tail_is_supported(sql, start, end)) {
		return 1;
	}
	if (end != start + 1U || sql[start] < '1' || sql[start] > '7') {
		return 0;
	}
	value = (size_t)(sql[start] - '0');
	return value >= 1U && value <= 7U;
}

static int sqlparser_sqlserver_dateformat_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	static const char *const formats[] = {"mdy", "dmy", "ymd", "ydm", "myd", "dym"};
	size_t index;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (sqlparser_sqlserver_variable_tail_is_supported(sql, start, end)) {
		return 1;
	}
	for (index = 0U; index < sizeof(formats) / sizeof(formats[0]); index++) {
		if (sqlparser_sqlserver_word_at_bounded(sql, start, end, formats[index]) &&
		    sqlparser_sqlserver_skip_space_bounded(sql, start + strlen(formats[index]), end) >= end) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_sqlserver_deadlock_priority_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	char buffer[8];
	size_t len;
	long value;
	char *tail;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (sqlparser_sqlserver_variable_tail_is_supported(sql, start, end) ||
	    sqlparser_sqlserver_word_at_bounded(sql, start, end, "low") ||
	    sqlparser_sqlserver_word_at_bounded(sql, start, end, "normal") ||
	    sqlparser_sqlserver_word_at_bounded(sql, start, end, "high")) {
		return 1;
	}
	len = end - start;
	if (len == 0U || len >= sizeof(buffer)) {
		return 0;
	}
	memcpy(buffer, sql + start, len);
	buffer[len] = '\0';
	tail = NULL;
	value = strtol(buffer, &tail, 10);
	return tail != buffer && tail != NULL && *tail == '\0' && value >= -10L && value <= 10L;
}

static int sqlparser_sqlserver_language_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t token_end;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (sqlparser_sqlserver_variable_tail_is_supported(sql, start, end)) {
		return 1;
	}
	if (start < end && (sql[start] == '\'' || sql[start] == 'N' || sql[start] == 'n')) {
		if ((sql[start] == 'N' || sql[start] == 'n') && start + 1U < end && sql[start + 1U] == '\'') {
			token_end = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, start + 1U);
			return token_end > start + 1U && sqlparser_sqlserver_skip_space_bounded(sql, token_end, end) >= end;
		}
		token_end = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, start);
		return token_end > start && sqlparser_sqlserver_skip_space_bounded(sql, token_end, end) >= end;
	}
	token_end = sqlparser_sqlserver_identifier_token_end(sql, start, end);
	return token_end > start && sqlparser_sqlserver_skip_space_bounded(sql, token_end, end) >= end;
}

static int sqlparser_sqlserver_context_info_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (sqlparser_sqlserver_variable_tail_is_supported(sql, start, end)) {
		return 1;
	}
	if (end <= start + 2U || sql[start] != '0' || (sql[start + 1U] != 'x' && sql[start + 1U] != 'X')) {
		return 0;
	}
	for (pos = start + 2U; pos < end; pos++) {
		if (!isxdigit((unsigned char)sql[pos])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_sqlserver_fips_flagger_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t token_end;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	if (sqlparser_sqlserver_word_at_bounded(sql, start, end, "off")) {
		return sqlparser_sqlserver_skip_space_bounded(
			sql, start + strlen("off"), end) >= end;
	}
	if (start >= end || sql[start] != '\'') {
		return 0;
	}
	token_end = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, start);
	if (token_end <= start + 1U || token_end > end ||
	    sql[token_end - 1U] != '\'' ||
	    sqlparser_sqlserver_skip_space_bounded(sql, token_end, end) < end) {
		return 0;
	}
	return sqlparser_sqlserver_ascii_span_equal(sql, start + 1U, token_end - 1U, "entry") ||
		sqlparser_sqlserver_ascii_span_equal(sql, start + 1U, token_end - 1U, "intermediate") ||
		sqlparser_sqlserver_ascii_span_equal(sql, start + 1U, token_end - 1U, "full");
}

static int sqlparser_sqlserver_offsets_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	static const char *const clauses[] = {
		"select",
		"from",
		"order",
		"compute",
		"table",
		"procedure",
		"statement",
		"param"
	};
	size_t index;
	size_t pos;
	int matched;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	for (;;) {
		matched = 0;
		for (index = 0U; index < sizeof(clauses) / sizeof(clauses[0]); index++) {
			if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, clauses[index])) {
				pos = sqlparser_sqlserver_skip_space_bounded(
					sql, pos + strlen(clauses[index]), end);
				matched = 1;
				break;
			}
		}
		if (!matched) {
			return 0;
		}
		if (pos >= end || sql[pos] != ',') {
			break;
		}
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "on")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("on"), end);
		return pos >= end;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "off")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("off"), end);
		return pos >= end;
	}
	return 0;
}

static int sqlparser_sqlserver_identity_insert_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	pos = sqlparser_sqlserver_multipart_identifier_end(sql, start, end);
	if (pos <= start) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "on")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("on"), end);
		return pos >= end;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "off")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("off"), end);
		return pos >= end;
	}
	return 0;
}

static int sqlparser_sqlserver_transaction_isolation_tail_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t pos;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "isolation")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("isolation"), end);
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "level")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("level"), end);
	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, "read", "uncommitted")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("read"), end);
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("uncommitted"), end);
		return pos >= end;
	}
	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, "read", "committed")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("read"), end);
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("committed"), end);
		return pos >= end;
	}
	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, "repeatable", "read")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("repeatable"), end);
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("read"), end);
		return pos >= end;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "snapshot")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("snapshot"), end);
		return pos >= end;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "serializable")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("serializable"), end);
		return pos >= end;
	}
	return 0;
}

static int sqlparser_sqlserver_set_statement_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (pos >= end) {
		return 0;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "identity_insert")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("identity_insert"), end);
		return sqlparser_sqlserver_identity_insert_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "transaction")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("transaction"), end);
		return sqlparser_sqlserver_transaction_isolation_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "datefirst")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("datefirst"), end);
		return sqlparser_sqlserver_datefirst_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "dateformat")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("dateformat"), end);
		return sqlparser_sqlserver_dateformat_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "deadlock_priority")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("deadlock_priority"), end);
		return sqlparser_sqlserver_deadlock_priority_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "lock_timeout")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("lock_timeout"), end);
		return sqlparser_sqlserver_integer_or_variable_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "language")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("language"), end);
		return sqlparser_sqlserver_language_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "query_governor_cost_limit")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("query_governor_cost_limit"), end);
		return sqlparser_sqlserver_integer_or_variable_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "rowcount")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("rowcount"), end);
		return sqlparser_sqlserver_integer_or_variable_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "textsize")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("textsize"), end);
		return sqlparser_sqlserver_integer_or_variable_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "context_info")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("context_info"), end);
		return sqlparser_sqlserver_context_info_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "fips_flagger")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("fips_flagger"), end);
		return sqlparser_sqlserver_fips_flagger_tail_is_supported(sql, pos, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "offsets")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("offsets"), end);
		return sqlparser_sqlserver_offsets_tail_is_supported(sql, pos, end);
	}
	return sqlparser_sqlserver_set_on_off_tail_is_supported(sql, pos, end, 1);
}

static int sqlparser_sqlserver_execute_as_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t token_end;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "as")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("as"), end);
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "login")) {
		pos += strlen("login");
	} else if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "user")) {
		pos += strlen("user");
	} else {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (pos >= end || sql[pos] != '=') {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
	if (pos < end && (sql[pos] == 'N' || sql[pos] == 'n') &&
	    pos + 1U < end && sql[pos + 1U] == '\'') {
		pos++;
	}
	if (pos >= end || sql[pos] != '\'') {
		return 0;
	}
	token_end = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
	if (token_end <= pos + 1U || token_end > end || sql[token_end - 1U] != '\'') {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, token_end, end);
	if (pos >= end) {
		return 1;
	}
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "with")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
	if (sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, "no", "revert")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("no"), end);
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("revert"), end);
		return pos >= end;
	}
	if (!sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, "cookie", "into")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("cookie"), end);
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("into"), end);
	return sqlparser_sqlserver_variable_tail_is_supported(sql, pos, end);
}

static int sqlparser_sqlserver_session_context_value_is_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	int has_digit;
	int has_exponent;
	size_t pos;
	size_t token_end;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (start >= end) {
		return 0;
	}
	if ((sql[start] == 'N' || sql[start] == 'n') &&
	    start + 1U < end && sql[start + 1U] == '\'') {
		start++;
	}
	if (sql[start] == '\'') {
		token_end = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, start);
		return token_end > start + 1U && token_end == end &&
			sql[token_end - 1U] == '\'';
	}
	if (sqlparser_sqlserver_variable_tail_is_supported(sql, start, end)) {
		return 1;
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, start, end, "null") ||
	    sqlparser_sqlserver_word_at_bounded(sql, start, end, "default")) {
		token_end = sqlparser_sqlserver_identifier_token_end(sql, start, end);
		return token_end == end;
	}
	if (end > start + 2U && sql[start] == '0' &&
	    (sql[start + 1U] == 'x' || sql[start + 1U] == 'X')) {
		for (pos = start + 2U; pos < end; pos++) {
			if (!isxdigit((unsigned char)sql[pos])) {
				return 0;
			}
		}
		return 1;
	}
	pos = start;
	if (pos < end && (sql[pos] == '+' || sql[pos] == '-')) {
		pos++;
	}
	has_digit = 0;
	while (pos < end && isdigit((unsigned char)sql[pos])) {
		has_digit = 1;
		pos++;
	}
	if (pos < end && sql[pos] == '.') {
		pos++;
		while (pos < end && isdigit((unsigned char)sql[pos])) {
			has_digit = 1;
			pos++;
		}
	}
	if (!has_digit) {
		return 0;
	}
	has_exponent = 0;
	if (pos < end && (sql[pos] == 'e' || sql[pos] == 'E')) {
		pos++;
		if (pos < end && (sql[pos] == '+' || sql[pos] == '-')) {
			pos++;
		}
		while (pos < end && isdigit((unsigned char)sql[pos])) {
			has_exponent = 1;
			pos++;
		}
		if (!has_exponent) {
			return 0;
		}
	}
	return pos == end;
}

static int sqlparser_sqlserver_session_context_arguments_are_supported(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t comma;
	size_t equals;
	size_t item_end;
	size_t item_start;
	size_t name_end;
	int named_started;

	named_started = 0;
	while (start < end) {
		if (sqlparser_sqlserver_find_top_level_char(
			    sql, start, end, ',', &comma)) {
			item_end = sqlparser_sqlserver_trim_right(sql, start, comma);
		} else {
			comma = end;
			item_end = sqlparser_sqlserver_trim_right(sql, start, end);
		}
		item_start = sqlparser_sqlserver_skip_space_bounded(sql, start, item_end);
		if (item_start >= item_end) {
			return 0;
		}
		if (sqlparser_sqlserver_find_top_level_char(
			    sql, item_start, item_end, '=', &equals)) {
			name_end = sqlparser_sqlserver_trim_right(sql, item_start, equals);
			if (!sqlparser_sqlserver_variable_tail_is_supported(
				    sql, item_start, name_end)) {
				return 0;
			}
			named_started = 1;
			if (!sqlparser_sqlserver_session_context_value_is_supported(
				    sql, equals + 1U, item_end)) {
				return 0;
			}
		} else {
			if (named_started) {
				return 0;
			}
			if (!sqlparser_sqlserver_session_context_value_is_supported(
				    sql, item_start, item_end)) {
				return 0;
			}
		}
		if (comma == end) {
			break;
		}
		start = comma + 1U;
		if (sqlparser_sqlserver_skip_space_bounded(sql, start, end) >= end) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_sqlserver_session_context_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t part_end;
	size_t args_start;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "sys")) {
		part_end = pos + strlen("sys");
		pos = sqlparser_sqlserver_skip_space_bounded(sql, part_end, end);
		if (pos >= end || sql[pos] != '.') {
			return 0;
		}
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
		part_end = pos + strlen("sp_set_session_context");
		if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "sp_set_session_context")) {
			return 0;
		}
	} else if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "sp_set_session_context")) {
		part_end = pos + strlen("sp_set_session_context");
	} else {
		part_end = sqlparser_sqlserver_identifier_token_end(sql, pos, end);
		if (part_end <= pos) {
			return 0;
		}
	}
	if (sqlparser_sqlserver_identifier_token_equal(sql, pos, part_end, "sys")) {
		pos = sqlparser_sqlserver_skip_space_bounded(sql, part_end, end);
		if (pos >= end || sql[pos] != '.') {
			return 0;
		}
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
		part_end = sqlparser_sqlserver_identifier_token_end(sql, pos, end);
	}
	if (part_end <= pos ||
	    !sqlparser_sqlserver_identifier_token_equal(sql, pos, part_end, "sp_set_session_context")) {
		return 0;
	}
	args_start = sqlparser_sqlserver_skip_space_bounded(sql, part_end, end);
	return args_start < end &&
		sqlparser_sqlserver_session_context_arguments_are_supported(
			sql, args_start, end);
}

static int sqlparser_sqlserver_revert_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (pos >= end) {
		return 1;
	}
	if (!sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, "with", "cookie")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("cookie"), end);
	if (pos >= end || sql[pos] != '=') {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
	return sqlparser_sqlserver_variable_tail_is_supported(sql, pos, end);
}

static int sqlparser_sqlserver_setuser_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t token_end;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (pos >= end) {
		return 1;
	}
	if (pos < end && (sql[pos] == 'N' || sql[pos] == 'n') &&
	    pos + 1U < end && sql[pos + 1U] == '\'') {
		pos++;
	}
	if (pos >= end || sql[pos] != '\'') {
		return 0;
	}
	token_end = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
	if (token_end <= pos + 1U || token_end > end || sql[token_end - 1U] != '\'') {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, token_end, end);
	if (pos >= end) {
		return 1;
	}
	if (!sqlparser_sqlserver_tail_starts_with_two_words(sql, pos, end, "with", "noreset")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("noreset"), end);
	return pos >= end;
}

static int sqlparser_sqlserver_alter_authorization_tail_is_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	size_t entity_start;
	size_t to_pos;
	size_t owner_start;

	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "on")) {
		return 0;
	}
	entity_start = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("on"), end);
	if (entity_start >= end) {
		return 0;
	}
	if (!sqlparser_sqlserver_find_word_bounded(sql, entity_start, end, "to", &to_pos)) {
		return 0;
	}
	if (sqlparser_sqlserver_trim_right(sql, entity_start, to_pos) <= entity_start) {
		return 0;
	}
	owner_start = sqlparser_sqlserver_skip_space_bounded(sql, to_pos + strlen("to"), end);
	return owner_start < end;
}

static sqlparser_status_t sqlparser_sqlserver_build_internal_raw_statement(
	const char *internal_name,
	const char *input_sql,
	size_t start,
	size_t end,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t public_sql;
	sqlparser_sqlserver_buffer_t out;
	sqlparser_status_t status;

	if (internal_name == NULL || input_sql == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "internal statement arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	memset(&public_sql, 0, sizeof(public_sql));
	memset(&out, 0, sizeof(out));

	status = sqlparser_sqlserver_append_public_normalized_range(&public_sql, input_sql, start, end, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&public_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(&out, "SET ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(&out, internal_name, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(&out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_internal_string_literal(
			&out,
			public_sql.data != NULL ? public_sql.data : "",
			0U,
			public_sql.len,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	sqlparser_sqlserver_buffer_release(&public_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_internal_ddl_statement(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	size_t start;
	size_t end;
	size_t pos;
	size_t name_end;
	const char *internal_name;
	int matched;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = sqlparser_sqlserver_trim_left(input_sql, 0U, strlen(input_sql));
	end = sqlparser_sqlserver_trim_right(input_sql, start, strlen(input_sql));
	if (end > start && input_sql[end - 1U] == ';') {
		end--;
		end = sqlparser_sqlserver_trim_right(input_sql, start, end);
	}
	if (start >= end) {
		return SQLPARSER_STATUS_OK;
	}

	internal_name = NULL;
	matched = 0;
	if (sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "create")) {
		pos = sqlparser_sqlserver_skip_space_bounded(input_sql, start + strlen("create"), end);
		if (sqlparser_sqlserver_tail_starts_with_two_words(input_sql, pos, end, "application", "role")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("application"), end);
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("role"), end);
			name_end = sqlparser_sqlserver_identifier_token_end(input_sql, pos, end);
			if (name_end <= pos) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, name_end, end);
			if (pos < end && sqlparser_sqlserver_application_role_create_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_CREATE_APPLICATION_ROLE;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "synonym")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("synonym"), end);
			if (sqlparser_sqlserver_create_synonym_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_CREATE_SYNONYM;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "type")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("type"), end);
			if (sqlparser_sqlserver_create_type_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_CREATE_TYPE;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "user")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("user"), end);
			name_end = sqlparser_sqlserver_identifier_token_end(input_sql, pos, end);
			if (name_end <= pos) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, name_end, end);
			if (pos < end && sqlparser_sqlserver_create_user_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_CREATE_USER;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "role")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("role"), end);
			name_end = sqlparser_sqlserver_identifier_token_end(input_sql, pos, end);
			if (name_end <= pos) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, name_end, end);
			if (pos < end && sqlparser_sqlserver_create_role_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_CREATE_ROLE;
				matched = 1;
			}
		}
	} else if (sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "alter")) {
		pos = sqlparser_sqlserver_skip_space_bounded(input_sql, start + strlen("alter"), end);
		if (sqlparser_sqlserver_tail_starts_with_two_words(input_sql, pos, end, "application", "role")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("application"), end);
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("role"), end);
			name_end = sqlparser_sqlserver_identifier_token_end(input_sql, pos, end);
			if (name_end <= pos) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, name_end, end);
			if (pos < end && sqlparser_sqlserver_application_role_alter_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_ALTER_APPLICATION_ROLE;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "database")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("database"), end);
			if (sqlparser_sqlserver_alter_database_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_ALTER_DATABASE;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "authorization")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("authorization"), end);
			if (sqlparser_sqlserver_alter_authorization_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_ALTER_AUTHORIZATION;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "user")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("user"), end);
			name_end = sqlparser_sqlserver_identifier_token_end(input_sql, pos, end);
			if (name_end <= pos) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, name_end, end);
			if (pos < end && sqlparser_sqlserver_alter_user_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_ALTER_USER;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "role")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("role"), end);
			name_end = sqlparser_sqlserver_identifier_token_end(input_sql, pos, end);
			if (name_end <= pos) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, name_end, end);
			if (pos < end && sqlparser_sqlserver_alter_role_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_ALTER_ROLE;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "schema")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("schema"), end);
			name_end = sqlparser_sqlserver_identifier_token_end(input_sql, pos, end);
			if (name_end <= pos) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, name_end, end);
			if (pos < end && sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "transfer")) {
				pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("transfer"), end);
				if (pos < end) {
					internal_name = SQLPARSER_INTERNAL_SQLSERVER_ALTER_SCHEMA;
					matched = 1;
				}
			}
		}
	} else if (sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "drop")) {
		pos = sqlparser_sqlserver_skip_space_bounded(input_sql, start + strlen("drop"), end);
		if (sqlparser_sqlserver_tail_starts_with_two_words(input_sql, pos, end, "application", "role")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("application"), end);
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("role"), end);
			name_end = sqlparser_sqlserver_identifier_token_end(input_sql, pos, end);
			if (name_end <= pos) {
				return SQLPARSER_STATUS_OK;
			}
			if (sqlparser_sqlserver_skip_space_bounded(input_sql, name_end, end) >= end) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_DROP_APPLICATION_ROLE;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "synonym")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("synonym"), end);
			if (sqlparser_sqlserver_drop_synonym_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_DROP_SYNONYM;
				matched = 1;
			}
		} else if (sqlparser_sqlserver_word_at_bounded(input_sql, pos, end, "index")) {
			pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("index"), end);
			if (sqlparser_sqlserver_drop_index_tail_is_supported(input_sql, pos, end)) {
				internal_name = SQLPARSER_INTERNAL_SQLSERVER_DROP_INDEX;
				matched = 1;
			}
		}
	} else if (sqlparser_sqlserver_tail_starts_with_two_words(input_sql, start, end, "update", "statistics")) {
		pos = sqlparser_sqlserver_skip_space_bounded(input_sql, start + strlen("update"), end);
		pos = sqlparser_sqlserver_skip_space_bounded(input_sql, pos + strlen("statistics"), end);
		if (sqlparser_sqlserver_update_statistics_tail_is_supported(input_sql, pos, end)) {
			internal_name = SQLPARSER_INTERNAL_SQLSERVER_UPDATE_STATISTICS;
			matched = 1;
		}
	} else if (sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "set")) {
		pos = sqlparser_sqlserver_skip_space_bounded(input_sql, start + strlen("set"), end);
		if (sqlparser_sqlserver_set_statement_tail_is_supported(input_sql, pos, end)) {
			internal_name = SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT;
			matched = 1;
		}
	} else if (sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "execute") ||
	           sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "exec")) {
		pos = start + (sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "execute") ?
			strlen("execute") :
			strlen("exec"));
		if (sqlparser_sqlserver_execute_as_tail_is_supported(input_sql, pos, end) ||
		    sqlparser_sqlserver_session_context_tail_is_supported(input_sql, pos, end)) {
			internal_name = SQLPARSER_INTERNAL_SQLSERVER_EXECUTE_STATEMENT;
			matched = 1;
		}
	} else if (sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "revert")) {
		pos = start + strlen("revert");
		if (sqlparser_sqlserver_revert_tail_is_supported(input_sql, pos, end)) {
			internal_name = SQLPARSER_INTERNAL_SQLSERVER_REVERT_STATEMENT;
			matched = 1;
		}
	} else if (sqlparser_sqlserver_word_at_bounded(input_sql, start, end, "setuser")) {
		pos = start + strlen("setuser");
		if (sqlparser_sqlserver_setuser_tail_is_supported(input_sql, pos, end)) {
			internal_name = SQLPARSER_INTERNAL_SQLSERVER_SETUSER_STATEMENT;
			matched = 1;
		}
	}
	if (!matched) {
		return SQLPARSER_STATUS_OK;
	}

	return sqlparser_sqlserver_build_internal_raw_statement(
		internal_name,
		input_sql,
		start,
		end,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_prepared_statement(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	static const struct {
		const char *proc_name;
		const char *internal_name;
	} specs[] = {
		{"sp_prepare", SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE},
		{"sp_execute", SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE},
		{"sp_prepexec", SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC},
		{"sp_unprepare", SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE},
		{"sp_executesql", SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL}
	};
	sqlparser_sqlserver_buffer_t out;
	size_t start;
	size_t end;
	size_t pos;
	size_t args_start;
	size_t args_end;
	size_t arg_start;
	size_t arg_end;
	size_t scan;
	size_t skipped;
	size_t paren_depth;
	size_t index;
	const char *internal_name;
	int appended_arg;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	start = sqlparser_sqlserver_trim_left(input_sql, 0U, strlen(input_sql));
	end = sqlparser_sqlserver_trim_right(input_sql, start, strlen(input_sql));
	if (end > start && input_sql[end - 1U] == ';') {
		end--;
		end = sqlparser_sqlserver_trim_right(input_sql, start, end);
	}
	if (!sqlparser_sqlserver_ascii_word_equal(input_sql, start, "exec") &&
	    !sqlparser_sqlserver_ascii_word_equal(input_sql, start, "execute")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = start + (sqlparser_sqlserver_ascii_word_equal(input_sql, start, "execute") ? strlen("execute") : strlen("exec"));
	pos = sqlparser_sqlserver_skip_space(input_sql, pos);
	internal_name = NULL;
	for (index = 0U; index < sizeof(specs) / sizeof(specs[0]); index++) {
		if (sqlparser_sqlserver_ascii_word_equal(input_sql, pos, specs[index].proc_name)) {
			pos += strlen(specs[index].proc_name);
			internal_name = specs[index].internal_name;
			break;
		}
	}
	if (internal_name == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	args_start = sqlparser_sqlserver_skip_space(input_sql, pos);
	args_end = sqlparser_sqlserver_trim_right(input_sql, args_start, end);
	if (args_start >= args_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server prepared procedure requires arguments");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_sqlserver_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(&out, internal_name, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(&out, " TO ", out_error);
	}
	appended_arg = 0;
	arg_start = args_start;
	scan = args_start;
	paren_depth = 0U;
	while (status == SQLPARSER_STATUS_OK && scan <= args_end) {
		if (scan < args_end) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(input_sql, scan);
			if (skipped > scan) {
				scan = skipped;
				continue;
			}
			if (input_sql[scan] == '(') {
				paren_depth++;
			} else if (input_sql[scan] == ')' && paren_depth > 0U) {
				paren_depth--;
			} else if (input_sql[scan] != ',' || paren_depth != 0U) {
				scan++;
				continue;
			}
		}

		arg_end = sqlparser_sqlserver_trim_right(input_sql, arg_start, scan);
		arg_start = sqlparser_sqlserver_trim_left(input_sql, arg_start, arg_end);
		if (arg_start >= arg_end) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SQL Server prepared procedure has an empty argument");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		if (appended_arg) {
			status = sqlparser_sqlserver_buffer_append_cstr(&out, ", ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_append_internal_string_literal(&out, input_sql, arg_start, arg_end, out_error);
		}
		appended_arg = 1;
		scan++;
		arg_start = scan;
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_rewrite_use_statements(
	char **io_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	char *statement_sql;
	char *rewritten_sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	size_t use_name_start;
	size_t use_name_end;
	int rewritten;
	int rewritten_use;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start < len) {
		statement_end = sqlparser_sqlserver_statement_end(sql, segment_start, len);
		leading_end = segment_start;
		for (;;) {
			while (leading_end < statement_end &&
			       isspace((unsigned char)sql[leading_end])) {
				leading_end++;
			}
			if (leading_end >= statement_end ||
			    !((sql[leading_end] == '-' &&
			       leading_end + 1U < statement_end &&
			       sql[leading_end + 1U] == '-') ||
			      (sql[leading_end] == '/' &&
			       leading_end + 1U < statement_end &&
			       sql[leading_end + 1U] == '*'))) {
				break;
			}
			{
				size_t skipped;

				skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(
					sql, leading_end);
				if (skipped <= leading_end) {
					break;
				}
				leading_end = skipped < statement_end ?
					skipped :
					statement_end;
			}
		}
		statement_sql = sqlparser_strndup(
			sql + leading_end, statement_end - leading_end);
		if (statement_sql == NULL) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		use_name_start = 0U;
		use_name_end = 0U;
		status = sqlparser_sqlserver_preprocess_use_statement(
			statement_sql,
			&rewritten_sql,
			&use_name_start,
			&use_name_end,
			out_error);
		rewritten_use =
			status == SQLPARSER_STATUS_OK && rewritten_sql != NULL;
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_sqlserver_preprocess_prepared_statement(statement_sql, &rewritten_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_sqlserver_preprocess_internal_ddl_statement(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_sqlserver_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_sqlserver_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				status = sqlparser_sqlserver_buffer_begin_origin(
					&out,
					origins,
					len,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					sqlparser_sqlserver_buffer_release(&out);
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_sqlserver_buffer_append_input_mem(
				&out,
				sql,
				copy_start,
				leading_end - copy_start,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				size_t sentinel_start;
				size_t sentinel_end;

				sentinel_start = strlen("SET ");
				sentinel_end = sentinel_start;
				while (rewritten_sql[sentinel_end] != '\0' &&
				       sqlparser_sqlserver_is_ident_char(
					       (unsigned char)rewritten_sql[sentinel_end])) {
					sentinel_end++;
				}
				status = sqlparser_sqlserver_buffer_append_mem(
					&out,
					rewritten_sql,
					sentinel_start,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_sqlserver_buffer_append_generated_identifier(
						&out,
						rewritten_sql + sentinel_start,
						sentinel_end - sentinel_start,
						out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					if (rewritten_use) {
						size_t value_start;
						size_t rewritten_len;

						value_start = sentinel_end + strlen(" = ");
						rewritten_len = strlen(rewritten_sql);
						status = sqlparser_sqlserver_buffer_append_mem(
							&out,
							rewritten_sql + sentinel_end,
							value_start - sentinel_end,
							out_error);
						if (status == SQLPARSER_STATUS_OK) {
							status = sqlparser_sqlserver_buffer_append_source_identifier(
								&out,
								rewritten_sql + value_start,
								leading_end + use_name_start,
								use_name_end - use_name_start,
								rewritten_len - value_start,
								out_error);
						}
					} else {
						status = sqlparser_sqlserver_buffer_append_cstr(
							&out,
							rewritten_sql + sentinel_end,
							out_error);
					}
				}
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		}
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}

	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_sqlserver_buffer_append_input_mem(
		&out,
		sql,
		copy_start,
		len - copy_start,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_commit_origin(
			&out,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_mask_non_code(
	const char *sql,
	char **out_masked,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t index;
	size_t span_start;
	size_t span_end;
	sqlparser_status_t status;

	if (sql == NULL || out_masked == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server mask arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_masked = NULL;

	len = strlen(sql);
	masked = sqlparser_strndup(sql, len);
	if (masked == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	for (index = 0U; index < len;) {
		span_start = index;
		if ((sql[index] == 'n' || sql[index] == 'N') &&
		    index + 1U < len && sql[index + 1U] == '\'') {
			index++;
		}
		if (!sqlparser_sqlserver_can_copy_quoted_or_comment(sql, index)) {
			index = span_start + 1U;
			continue;
		}

		span_end = index;
		status = sqlparser_sqlserver_quoted_or_comment_span(
			sql,
			index,
			&span_end,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(masked);
			return status;
		}
		memset(masked + span_start, ' ', span_end - span_start);
		index = span_end;
	}

	for (index = 0U; index < len; index++) {
		masked[index] = (char)tolower((unsigned char)masked[index]);
	}

	*out_masked = masked;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_sqlserver_contains_phrase(const char *masked, const char *phrase)
{
	size_t phrase_len;
	size_t last_phrase_pos;
	int needs_left_boundary;
	int needs_right_boundary;
	size_t pos;

	if (masked == NULL || phrase == NULL || phrase[0] == '\0') {
		return 0;
	}

	phrase_len = strlen(phrase);
	last_phrase_pos = phrase_len;
	while (last_phrase_pos > 0U && isspace((unsigned char)phrase[last_phrase_pos - 1U])) {
		last_phrase_pos--;
	}
	if (last_phrase_pos == 0U) {
		return 0;
	}

	needs_left_boundary = sqlparser_sqlserver_is_ident_char((unsigned char)phrase[0]);
	needs_right_boundary = sqlparser_sqlserver_is_ident_char((unsigned char)phrase[last_phrase_pos - 1U]);

	for (pos = 0U; masked[pos] != '\0'; pos++) {
		size_t text_pos;
		size_t phrase_pos;
		int matched;

		if (needs_left_boundary && pos > 0U &&
		    sqlparser_sqlserver_is_ident_char((unsigned char)masked[pos - 1U])) {
			continue;
		}

		text_pos = pos;
		phrase_pos = 0U;
		matched = 1;
		while (phrase[phrase_pos] != '\0') {
			if (isspace((unsigned char)phrase[phrase_pos])) {
				int saw_space;

				saw_space = 0;
				while (isspace((unsigned char)phrase[phrase_pos])) {
					phrase_pos++;
				}
				while (isspace((unsigned char)masked[text_pos])) {
					saw_space = 1;
					text_pos++;
				}
				if (!saw_space) {
					matched = 0;
					break;
				}
				continue;
			}
			if (masked[text_pos] != phrase[phrase_pos]) {
				matched = 0;
				break;
			}
			text_pos++;
			phrase_pos++;
		}

		if (matched &&
		    (!needs_right_boundary || !sqlparser_sqlserver_is_ident_char((unsigned char)masked[text_pos]))) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_sqlserver_raw_contains_word_span(const char *sql, const char *word, size_t word_len)
{
	size_t pos;

	if (sql == NULL || word == NULL || word_len == 0U) {
		return 0;
	}

	for (pos = 0U; sql[pos] != '\0'; pos++) {
		size_t index;

		if (pos > 0U && sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos - 1U])) {
			continue;
		}

		for (index = 0U; index < word_len; index++) {
			if (sql[pos + index] == '\0') {
				break;
			}
			if (tolower((unsigned char)sql[pos + index]) !=
			    tolower((unsigned char)word[index])) {
				break;
			}
		}
		if (index == word_len &&
		    !sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos + word_len])) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_sqlserver_raw_contains_word(const char *sql, const char *word)
{
	return sqlparser_sqlserver_raw_contains_word_span(sql, word, word != NULL ? strlen(word) : 0U);
}

static int sqlparser_sqlserver_raw_may_contain_phrase(const char *sql, const char *phrase)
{
	size_t pos;
	int saw_token;

	if (sql == NULL || phrase == NULL) {
		return 0;
	}

	pos = 0U;
	saw_token = 0;
	while (phrase[pos] != '\0') {
		size_t start;
		size_t len;

		while (phrase[pos] != '\0' &&
		       !sqlparser_sqlserver_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		start = pos;
		while (phrase[pos] != '\0' &&
		       sqlparser_sqlserver_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		len = pos - start;
		if (len == 0U) {
			continue;
		}

		saw_token = 1;
		if (!sqlparser_sqlserver_raw_contains_word_span(sql, phrase + start, len)) {
			return 0;
		}
	}

	return saw_token;
}

static int sqlparser_sqlserver_starts_with_word(const char *masked, const char *word)
{
	size_t pos;

	if (masked == NULL || word == NULL) {
		return 0;
	}

	pos = sqlparser_sqlserver_skip_space(masked, 0U);
	return sqlparser_sqlserver_ascii_word_equal(masked, pos, word);
}

static int sqlparser_sqlserver_starts_with_dml_clause(const char *masked, const char *clause)
{
	static const char *const dml_words[] = {"insert", "update", "delete", "merge"};
	size_t index;

	if (masked == NULL || clause == NULL) {
		return 0;
	}

	for (index = 0U; index < sizeof(dml_words) / sizeof(dml_words[0]); index++) {
		size_t pos;

		pos = sqlparser_sqlserver_skip_space(masked, 0U);
		if (!sqlparser_sqlserver_ascii_word_equal(masked, pos, dml_words[index])) {
			continue;
		}
		pos += strlen(dml_words[index]);
		pos = sqlparser_sqlserver_skip_space(masked, pos);
		return sqlparser_sqlserver_ascii_word_equal(masked, pos, clause);
	}

	return 0;
}

static int sqlparser_sqlserver_previous_word_is(const char *text, size_t pos, const char *word)
{
	size_t end;
	size_t start;

	if (text == NULL || word == NULL || pos == 0U) {
		return 0;
	}

	end = pos;
	while (end > 0U && isspace((unsigned char)text[end - 1U])) {
		end--;
	}
	start = end;
	while (start > 0U && sqlparser_sqlserver_is_ident_char((unsigned char)text[start - 1U])) {
		start--;
	}
	return start < end && sqlparser_sqlserver_ascii_word_equal(text, start, word);
}

static int sqlparser_sqlserver_contains_table_variable_reference(const char *masked)
{
	static const char *const table_source_words[] = {"from", "join", "update", "into"};
	size_t word_index;

	if (masked == NULL || strchr(masked, '@') == NULL) {
		return 0;
	}

	for (word_index = 0U;
	     word_index < sizeof(table_source_words) / sizeof(table_source_words[0]);
	     word_index++) {
		const char *word;
		size_t word_len;
		size_t pos;

		word = table_source_words[word_index];
		word_len = strlen(word);
		for (pos = 0U; masked[pos] != '\0'; pos++) {
			size_t value_pos;

			if (!sqlparser_sqlserver_ascii_word_equal(masked, pos, word)) {
				continue;
			}
			if (strcmp(word, "from") == 0 &&
			    sqlparser_sqlserver_previous_word_is(masked, pos, "distinct")) {
				continue;
			}
			value_pos = sqlparser_sqlserver_skip_space(masked, pos + word_len);
			if (masked[value_pos] == '@') {
				return 1;
			}
		}
	}

	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_reject_unsupported(
	const char *sql,
	sqlparser_error_t *out_error)
{
	static const char *const unsupported_phrases[] = {
		"cross apply",
		"outer apply",
		"pivot",
		"unpivot",
		"for xml",
		"declare",
		"create procedure",
		"alter procedure",
		"create function",
		"alter function",
		"create trigger",
		"alter trigger",
		"begin try",
		"openquery",
		"openrowset",
		"opendatasource",
		"openjson",
		"openxml",
		"table variable",
		"by source"
	};
	char *masked;
	sqlparser_status_t status;
	size_t index;
	int needs_mask;

	needs_mask =
		sqlparser_sqlserver_raw_contains_word(sql, "exec") ||
		sqlparser_sqlserver_raw_contains_word(sql, "execute") ||
		sqlparser_sqlserver_raw_contains_word(sql, "use") ||
		(strchr(sql, '@') != NULL &&
		 (sqlparser_sqlserver_raw_contains_word(sql, "from") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "join") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "update") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "into"))) ||
		(sqlparser_sqlserver_raw_contains_word(sql, "top") &&
		 (sqlparser_sqlserver_raw_contains_word(sql, "insert") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "update") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "delete") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "merge") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "select"))) ||
		(sqlparser_sqlserver_raw_contains_word(sql, "output") &&
		 (sqlparser_sqlserver_raw_contains_word(sql, "insert") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "update") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "delete") ||
		  sqlparser_sqlserver_raw_contains_word(sql, "merge"))) ||
		(sqlparser_sqlserver_raw_contains_word(sql, "select") &&
		 sqlparser_sqlserver_raw_contains_word(sql, "top") &&
		 sqlparser_sqlserver_raw_contains_word(sql, "offset") &&
		 sqlparser_sqlserver_raw_contains_word(sql, "fetch"));
	for (index = 0U; !needs_mask &&
	     index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_sqlserver_raw_may_contain_phrase(sql, unsupported_phrases[index])) {
			needs_mask = 1;
		}
	}
	if (!needs_mask) {
		return SQLPARSER_STATUS_OK;
	}

	masked = NULL;
	status = sqlparser_sqlserver_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_sqlserver_starts_with_word(masked, "exec") ||
	    sqlparser_sqlserver_starts_with_word(masked, "execute") ||
	    sqlparser_sqlserver_starts_with_word(masked, "use")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: batch or procedure execution");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (sqlparser_sqlserver_contains_table_variable_reference(masked)) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: table variable");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (sqlparser_sqlserver_starts_with_dml_clause(masked, "top")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: DML TOP");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (sqlparser_sqlserver_starts_with_word(masked, "insert") &&
	    sqlparser_sqlserver_contains_phrase(masked, "output")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: OUTPUT");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if ((sqlparser_sqlserver_starts_with_word(masked, "update") ||
	     sqlparser_sqlserver_starts_with_word(masked, "delete") ||
	     sqlparser_sqlserver_starts_with_word(masked, "merge")) &&
	    sqlparser_sqlserver_contains_phrase(masked, "output")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: OUTPUT");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (sqlparser_sqlserver_starts_with_word(masked, "select") &&
	    sqlparser_sqlserver_contains_phrase(masked, "top") &&
	    sqlparser_sqlserver_contains_phrase(masked, "offset") &&
	    sqlparser_sqlserver_contains_phrase(masked, "fetch")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: TOP with OFFSET/FETCH");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	for (index = 0U; index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_sqlserver_contains_phrase(masked, unsupported_phrases[index])) {
			char message[256];

			(void)snprintf(
				message,
				sizeof(message),
				"unsupported SQL Server syntax: %s",
				unsupported_phrases[index]);
			free(masked);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, message);
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}

	free(masked);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_array_add(
	char ***items,
	size_t *count,
	size_t *capacity,
	const char *text,
	size_t len,
	sqlparser_error_t *out_error)
{
	char **next;
	char *copy;
	size_t next_capacity;

	if (items == NULL || count == NULL || capacity == NULL || text == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"array arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (*count == *capacity) {
		next_capacity = *capacity == 0U ? 8U : *capacity * 2U;
		if (next_capacity < *capacity) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (char **)realloc(*items, next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		*items = next;
		*capacity = next_capacity;
	}

	copy = sqlparser_strndup(text, len);
	if (copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	(*items)[*count] = copy;
	(*count)++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_size_array_reserve(
	size_t **items,
	size_t *capacity,
	size_t required,
	sqlparser_error_t *out_error)
{
	size_t *next;
	size_t next_capacity;

	if (items == NULL || capacity == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"size-array arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (required <= *capacity) {
		return SQLPARSER_STATUS_OK;
	}

	next_capacity = *capacity == 0U ? 8U : *capacity;
	while (next_capacity < required) {
		if (next_capacity > ((size_t)-1) / 2U) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next_capacity *= 2U;
	}
	if (next_capacity > ((size_t)-1) / sizeof(*next)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	next = (size_t *)realloc(*items, next_capacity * sizeof(*next));
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	*items = next;
	*capacity = next_capacity;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_cast_restore_add(
	sqlparser_sqlserver_state_t *state,
	sqlparser_sqlserver_cast_kind_t kind,
	size_t ordinal,
	const char *tail,
	size_t tail_len,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_cast_restore_t *next;
	char *tail_copy;
	size_t next_capacity;

	if (state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server dialect state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (state->cast_restore_count == state->cast_restore_capacity) {
		next_capacity = state->cast_restore_capacity == 0U ? 4U : state->cast_restore_capacity * 2U;
		if (next_capacity < state->cast_restore_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*next)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_sqlserver_cast_restore_t *)realloc(
			state->cast_restores,
			next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->cast_restores = next;
		state->cast_restore_capacity = next_capacity;
	}

	tail_copy = NULL;
	if (tail != NULL && tail_len > 0U) {
		tail_copy = sqlparser_strndup(tail, tail_len);
		if (tail_copy == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}

	state->cast_restores[state->cast_restore_count].ordinal = ordinal;
	state->cast_restores[state->cast_restore_count].kind = kind;
	state->cast_restores[state->cast_restore_count].tail = tail_copy;
	state->cast_restore_count++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_state_find_or_add_param(
	sqlparser_sqlserver_state_t *state,
	const char *name,
	size_t len,
	int always_new,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"parameter state arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (!always_new) {
		for (index = 0U; index < state->param_count; index++) {
			if (strlen(state->param_names[index]) == len &&
			    strncmp(state->param_names[index], name, len) == 0) {
				*out_param_index = index + 1U;
				return SQLPARSER_STATUS_OK;
			}
		}
	}

	status = sqlparser_sqlserver_array_add(
		&state->param_names,
		&state->param_count,
		&state->param_capacity,
		name,
		len,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_param_index = state->param_count;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_append_pg_param(
	sqlparser_sqlserver_buffer_t *out,
	size_t param_index,
	sqlparser_error_t *out_error)
{
	char text[32];

	(void)snprintf(text, sizeof(text), "$%lu", (unsigned long)param_index);
	return sqlparser_sqlserver_buffer_append_cstr(out, text, out_error);
}

static sqlparser_status_t sqlparser_sqlserver_copy_parameter(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t start;
	size_t end;
	size_t param_index;
	int always_new;
	sqlparser_status_t status;

	start = *index;
	if (input[start] == '@' && input[start + 1U] == '@') {
		sqlparser_sqlserver_buffer_t generated;

		end = start + 2U;
		if (!sqlparser_sqlserver_is_ident_start((unsigned char)input[end])) {
			return sqlparser_sqlserver_buffer_append_char(out, input[(*index)++], out_error);
		}
		while (sqlparser_sqlserver_is_ident_char((unsigned char)input[end])) {
			end++;
		}
		memset(&generated, 0, sizeof(generated));
		status = sqlparser_sqlserver_buffer_append_cstr(
			&generated,
			"\"" SQLPARSER_SQLSERVER_SYSTEM_VARIABLE_PREFIX,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_mem(
				&generated,
				input + start + 2U,
				end - start - 2U,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_char(
				&generated, '"', out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_generated_identifier(
				out,
				generated.data,
				generated.len,
				out_error);
		}
		sqlparser_sqlserver_buffer_release(&generated);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_cstr(
				out, "()", out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		*index = end;
		return SQLPARSER_STATUS_OK;
	}
	if (input[start] == '?') {
		status = sqlparser_sqlserver_state_find_or_add_param(
			state,
			"?",
			1U,
			1,
			&param_index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		status = sqlparser_sqlserver_append_pg_param(out, param_index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		*index = start + 1U;
		return SQLPARSER_STATUS_OK;
	}

	end = start + 1U;
	if (!sqlparser_sqlserver_is_ident_start((unsigned char)input[end]) &&
	    !isdigit((unsigned char)input[end])) {
		return sqlparser_sqlserver_buffer_append_char(out, input[(*index)++], out_error);
	}

	while (sqlparser_sqlserver_is_ident_char((unsigned char)input[end])) {
		end++;
	}

	always_new = 0;
	status = sqlparser_sqlserver_state_find_or_add_param(
		state,
		input + start,
		end - start,
		always_new,
		&param_index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_sqlserver_append_pg_param(out, param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*index = end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_copy_binary_literal(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t start;
	size_t end;
	size_t param_index;
	sqlparser_status_t status;

	start = *index;
	end = start + 2U;
	if (!isxdigit((unsigned char)input[end])) {
		return sqlparser_sqlserver_buffer_append_char(out, input[(*index)++], out_error);
	}
	while (isxdigit((unsigned char)input[end])) {
		end++;
	}

	status = sqlparser_sqlserver_state_find_or_add_param(
		state,
		input + start,
		end - start,
		0,
		&param_index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_sqlserver_append_pg_param(out, param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*index = end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_store_unicode_literal(
	sqlparser_sqlserver_state_t *state,
	const char *literal,
	size_t len,
	size_t ordinal,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server dialect state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_sqlserver_size_array_reserve(
		&state->unicode_ordinals,
		&state->unicode_ordinal_capacity,
		state->unicode_count + 1U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_sqlserver_array_add(
		&state->unicode_literals,
		&state->unicode_count,
		&state->unicode_capacity,
		literal,
		len,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	state->unicode_ordinals[state->unicode_count - 1U] = ordinal;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_copy_unicode_string(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t literal;
	size_t pos;
	sqlparser_status_t status;

	memset(&literal, 0, sizeof(literal));
	status = sqlparser_sqlserver_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_sqlserver_buffer_append_char(&literal, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&literal);
		return status;
	}

	pos = *index + 2U;
	while (input[pos] != '\0') {
		status = sqlparser_sqlserver_buffer_append_char(out, input[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&literal);
			return status;
		}
		status = sqlparser_sqlserver_buffer_append_char(&literal, input[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&literal);
			return status;
		}
		if (input[pos] == '\'') {
			if (input[pos + 1U] == '\'') {
				pos++;
				status = sqlparser_sqlserver_buffer_append_char(out, input[pos], out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&literal);
					return status;
				}
				status = sqlparser_sqlserver_buffer_append_char(&literal, input[pos], out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&literal);
					return status;
				}
			} else {
				pos++;
				status = sqlparser_sqlserver_buffer_finish(&literal, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&literal);
					return status;
				}
				status = sqlparser_sqlserver_store_unicode_literal(
					state,
					literal.data,
					literal.len,
					state->literal_count,
					out_error);
				sqlparser_sqlserver_buffer_release(&literal);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				state->literal_count++;
				*index = pos;
				return SQLPARSER_STATUS_OK;
			}
		}
		pos++;
	}

	sqlparser_sqlserver_buffer_release(&literal);
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated SQL Server Unicode string literal");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static int sqlparser_sqlserver_top_with_ties_has_order_by(const char *input, size_t start)
{
	size_t pos;
	size_t depth;

	if (input == NULL) {
		return 0;
	}
	depth = 0U;
	for (pos = start; input[pos] != '\0';) {
		size_t skipped;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(input, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(input, pos);
			if (skipped > pos) {
				pos = skipped;
				continue;
			}
		}
		if (input[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (input[pos] == ')') {
			if (depth > 0U) {
				depth--;
			}
			pos++;
			continue;
		}
		if (depth == 0U && input[pos] == ';') {
			return 0;
		}
		if (depth == 0U && sqlparser_sqlserver_ascii_word_equal(input, pos, "order")) {
			size_t by_pos;

			by_pos = sqlparser_sqlserver_skip_space(input, pos + strlen("order"));
			return sqlparser_sqlserver_ascii_word_equal(input, by_pos, "by");
		}
		pos++;
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_parse_top_clause(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_state_t *state,
	char **out_public_limit,
	char **out_public_suffix,
	char **out_parser_limit,
	int capture_origins,
	sqlparser_identifier_origin_map_t **out_parser_origins,
	size_t *out_source_offset,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t limit;
	sqlparser_sqlserver_buffer_t suffix;
	char *public_limit;
	char *public_suffix;
	size_t pos;
	size_t expr_start;
	size_t expr_end;
	int paren_depth;
	int with_ties;
	sqlparser_status_t status;

	if (state == NULL || out_public_limit == NULL ||
	    out_public_suffix == NULL || out_parser_limit == NULL ||
	    out_parser_origins == NULL || out_source_offset == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"top limit output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_public_limit = NULL;
	*out_public_suffix = NULL;
	*out_parser_limit = NULL;
	*out_parser_origins = NULL;
	*out_source_offset = 0U;

	pos = sqlparser_sqlserver_skip_space(input, *index);
	if (!sqlparser_sqlserver_ascii_word_equal(input, pos, "top")) {
		return SQLPARSER_STATUS_OK;
	}
	pos += 3U;
	pos = sqlparser_sqlserver_skip_space(input, pos);

	memset(&limit, 0, sizeof(limit));
	if (input[pos] == '(') {
		pos++;
		expr_start = pos;
		paren_depth = 1;
		while (input[pos] != '\0' && paren_depth > 0) {
			if (input[pos] == '(') {
				paren_depth++;
			} else if (input[pos] == ')') {
				paren_depth--;
				if (paren_depth == 0) {
					break;
				}
			}
			pos++;
		}
		if (paren_depth != 0) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_PARSE_ERROR,
				"unterminated SQL Server TOP expression");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		expr_end = pos;
		pos++;
	} else {
		expr_start = pos;
		while (input[pos] != '\0' && !isspace((unsigned char)input[pos]) && input[pos] != ',') {
			pos++;
		}
		expr_end = pos;
	}

	while (expr_end > expr_start && isspace((unsigned char)input[expr_end - 1U])) {
		expr_end--;
	}
	while (expr_start < expr_end && isspace((unsigned char)input[expr_start])) {
		expr_start++;
	}
	if (expr_start == expr_end) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"empty SQL Server TOP expression");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	status = sqlparser_sqlserver_buffer_append_mem(
		&limit,
		input + expr_start,
		expr_end - expr_start,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&limit);
		return status;
	}
	status = sqlparser_sqlserver_buffer_finish(&limit, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&limit);
		return status;
	}

	memset(&suffix, 0, sizeof(suffix));
	with_ties = 0;
	pos = sqlparser_sqlserver_skip_space(input, pos);
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "percent")) {
		status = sqlparser_sqlserver_buffer_append_cstr(&suffix, " PERCENT", out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&limit);
			sqlparser_sqlserver_buffer_release(&suffix);
			return status;
		}
		pos += strlen("percent");
		pos = sqlparser_sqlserver_skip_space(input, pos);
	}
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "with")) {
		size_t ties_pos;

		ties_pos = sqlparser_sqlserver_skip_space(input, pos + 4U);
		if (sqlparser_sqlserver_ascii_word_equal(input, ties_pos, "ties")) {
			status = sqlparser_sqlserver_buffer_append_cstr(&suffix, " WITH TIES", out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&limit);
				sqlparser_sqlserver_buffer_release(&suffix);
				return status;
			}
			pos = ties_pos + strlen("ties");
			with_ties = 1;
		}
	}
	if (with_ties && !sqlparser_sqlserver_top_with_ties_has_order_by(input, pos)) {
		sqlparser_sqlserver_buffer_release(&limit);
		sqlparser_sqlserver_buffer_release(&suffix);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: TOP WITH TIES requires ORDER BY");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	public_limit = sqlparser_sqlserver_buffer_take(&limit);
	if (public_limit == NULL) {
		sqlparser_sqlserver_buffer_release(&suffix);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	public_suffix = NULL;
	if (suffix.len > 0U) {
		status = sqlparser_sqlserver_buffer_finish(&suffix, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(public_limit);
			sqlparser_sqlserver_buffer_release(&suffix);
			return status;
		}
		public_suffix = sqlparser_sqlserver_buffer_take(&suffix);
		if (public_suffix == NULL) {
			free(public_limit);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	if (capture_origins) {
		status = sqlparser_identifier_origin_map_new_identity(
			strlen(public_limit),
			out_parser_origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(public_limit);
			free(public_suffix);
			return status;
		}
	}
	status = sqlparser_sqlserver_preprocess_text_origins(
		public_limit,
		state,
		out_parser_limit,
		*out_parser_origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_map_destroy(*out_parser_origins);
		*out_parser_origins = NULL;
		free(public_limit);
		free(public_suffix);
		return status;
	}

	*out_public_limit = public_limit;
	*out_public_suffix = public_suffix;
	*out_source_offset = expr_start;
	*index = pos;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_store_top_clause(
	sqlparser_sqlserver_state_t *state,
	const char *limit,
	const char *suffix,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (suffix == NULL) {
		suffix = "";
	}
	status = sqlparser_sqlserver_array_add(
		&state->top_limits,
		&state->top_count,
		&state->top_capacity,
		limit,
		strlen(limit),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_sqlserver_array_add(
		&state->top_suffixes,
		&state->top_suffix_count,
		&state->top_suffix_capacity,
		suffix,
		strlen(suffix),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(state->top_limits[state->top_count - 1U]);
		state->top_limits[state->top_count - 1U] = NULL;
		state->top_count--;
		return status;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_append_pending_top_limit(
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_pending_top_t *pending_top,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (pending_top == NULL || pending_top->limit == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_sqlserver_buffer_append_cstr(out, " LIMIT ", out_error);
	if (status == SQLPARSER_STATUS_OK && pending_top->origins != NULL) {
		status = sqlparser_sqlserver_append_mapped_identifiers(
			out,
			pending_top->limit,
			pending_top->origins,
			pending_top->source_offset,
			out_error);
	} else if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(
			out, pending_top->limit, out_error);
	}
	free(pending_top->limit);
	pending_top->limit = NULL;
	sqlparser_identifier_origin_map_destroy(pending_top->origins);
	pending_top->origins = NULL;
	return status;
}

static void sqlparser_sqlserver_pending_top_list_release(sqlparser_sqlserver_pending_top_list_t *pending)
{
	size_t index;

	if (pending == NULL) {
		return;
	}
	for (index = 0U; index < pending->count; index++) {
		free(pending->items[index].limit);
		sqlparser_identifier_origin_map_destroy(
			pending->items[index].origins);
	}
	free(pending->items);
	pending->items = NULL;
	pending->count = 0U;
	pending->capacity = 0U;
}

static int sqlparser_sqlserver_pending_top_list_has_depth(
	const sqlparser_sqlserver_pending_top_list_t *pending,
	size_t depth)
{
	size_t index;

	if (pending == NULL) {
		return 0;
	}
	for (index = 0U; index < pending->count; index++) {
		if (pending->items[index].depth == depth) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_pending_top_list_add(
	sqlparser_sqlserver_pending_top_list_t *pending,
	char **limit,
	sqlparser_identifier_origin_map_t **origins,
	size_t source_offset,
	size_t depth,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_pending_top_t *next;
	size_t next_capacity;

	if (pending == NULL || limit == NULL || *limit == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"pending TOP limit must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (sqlparser_sqlserver_pending_top_list_has_depth(pending, depth)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: multiple TOP clauses in one query scope");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (pending->count == pending->capacity) {
		next_capacity = pending->capacity == 0U ? 4U : pending->capacity * 2U;
		if (next_capacity < pending->capacity) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_sqlserver_pending_top_t *)realloc(
			pending->items,
			next_capacity * sizeof(*pending->items));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		pending->items = next;
		pending->capacity = next_capacity;
	}
	pending->items[pending->count].limit = *limit;
	pending->items[pending->count].origins =
		origins != NULL ? *origins : NULL;
	pending->items[pending->count].source_offset = source_offset;
	pending->items[pending->count].depth = depth;
	pending->count++;
	*limit = NULL;
	if (origins != NULL) {
		*origins = NULL;
	}
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_sqlserver_pending_top_list_remove_at(
	sqlparser_sqlserver_pending_top_list_t *pending,
	size_t index)
{
	if (pending == NULL || index >= pending->count) {
		return;
	}
	free(pending->items[index].limit);
	sqlparser_identifier_origin_map_destroy(pending->items[index].origins);
	if (index + 1U < pending->count) {
		memmove(
			pending->items + index,
			pending->items + index + 1U,
			(pending->count - index - 1U) * sizeof(*pending->items));
	}
	pending->count--;
}

static sqlparser_status_t sqlparser_sqlserver_append_pending_top_limits_at_depth(
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_pending_top_list_t *pending,
	size_t depth,
	sqlparser_error_t *out_error)
{
	size_t index;

	if (pending == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	for (index = 0U; index < pending->count;) {
		sqlparser_status_t status;

		if (pending->items[index].depth != depth) {
			index++;
			continue;
		}
		status = sqlparser_sqlserver_append_pending_top_limit(
			out,
			&pending->items[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		sqlparser_sqlserver_pending_top_list_remove_at(pending, index);
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_append_all_pending_top_limits(
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_pending_top_list_t *pending,
	sqlparser_error_t *out_error)
{
	while (pending != NULL && pending->count > 0U) {
		sqlparser_status_t status;
		size_t index;

		index = pending->count - 1U;
		status = sqlparser_sqlserver_append_pending_top_limit(
			out,
			&pending->items[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		sqlparser_sqlserver_pending_top_list_remove_at(pending, index);
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_append_hash_identifier(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t quoted;
	size_t source_start;
	size_t pos;
	sqlparser_status_t status;

	memset(&quoted, 0, sizeof(quoted));
	source_start = *index;
	pos = *index;
	status = sqlparser_sqlserver_buffer_append_char(&quoted, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	while (sqlparser_sqlserver_is_ident_char((unsigned char)input[pos])) {
		status = sqlparser_sqlserver_buffer_append_char(
			&quoted, input[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&quoted);
			return status;
		}
		pos++;
	}
	status = sqlparser_sqlserver_buffer_append_char(&quoted, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&quoted);
		return status;
	}
	*index = pos;
	status = sqlparser_sqlserver_buffer_append_source_identifier(
		out,
		quoted.data,
		source_start,
		pos - source_start,
		quoted.len,
		out_error);
	sqlparser_sqlserver_buffer_release(&quoted);
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_skip_identity_clause(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t pos;
	int depth;
	sqlparser_status_t status;

	status = sqlparser_sqlserver_buffer_append_cstr(out, "GENERATED BY DEFAULT AS IDENTITY", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + strlen("identity");
	pos = sqlparser_sqlserver_skip_space(input, pos);
	if (input[pos] == '(') {
		depth = 1;
		pos++;
		while (input[pos] != '\0' && depth > 0) {
			if (input[pos] == '(') {
				depth++;
			} else if (input[pos] == ')') {
				depth--;
			}
			pos++;
		}
	}
	*index = pos;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_sqlserver_table_hint_anchor_t sqlparser_sqlserver_table_source_anchor(
	const char *input,
	size_t pos)
{
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "from")) {
		return SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_FROM;
	}
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "join")) {
		return SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_JOIN;
	}
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "update")) {
		return SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_UPDATE;
	}
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "insert")) {
		return SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_INSERT;
	}
	return SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE;
}

static int sqlparser_sqlserver_word_ends_table_source(const char *input, size_t pos)
{
	static const char *const stop_words[] = {
		"where",
		"on",
		"join",
		"left",
		"right",
		"inner",
		"outer",
		"full",
		"cross",
		"group",
		"having",
		"order",
		"union",
		"except",
		"intersect",
		"option",
		"set",
		"values",
		"returning"
	};
	size_t index;

	for (index = 0U; index < sizeof(stop_words) / sizeof(stop_words[0]); index++) {
		if (sqlparser_sqlserver_ascii_word_equal(input, pos, stop_words[index])) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_sqlserver_table_hint_anchor_t sqlparser_sqlserver_table_hint_anchor(
	const char *input,
	size_t hint_pos)
{
	size_t pos;
	size_t depth;
	sqlparser_sqlserver_table_hint_anchor_t anchor;

	if (input == NULL || !sqlparser_sqlserver_word_followed_by_lparen(input, hint_pos, "with")) {
		return SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE;
	}

	depth = 0U;
	anchor = SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE;
	for (pos = 0U; input[pos] != '\0' && pos < hint_pos;) {
		size_t skipped;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(input, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(input, pos);
			if (skipped > pos) {
				pos = skipped;
				continue;
			}
		}
		if (input[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (input[pos] == ')') {
			if (depth > 0U) {
				depth--;
			}
			pos++;
			continue;
		}
		if (input[pos] == ';' || input[pos] == ',') {
			anchor = SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE;
			pos++;
			continue;
		}
		if (sqlparser_sqlserver_is_ident_start((unsigned char)input[pos])) {
			sqlparser_sqlserver_table_hint_anchor_t next_anchor;

			next_anchor = sqlparser_sqlserver_table_source_anchor(input, pos);
			if (next_anchor != SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE) {
				anchor = next_anchor;
			} else if (sqlparser_sqlserver_word_ends_table_source(input, pos)) {
				anchor = SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE;
			}
		}
		pos++;
	}
	return anchor;
}

static sqlparser_status_t sqlparser_sqlserver_table_hint_add(
	sqlparser_sqlserver_state_t *state,
	const char *text,
	size_t len,
	sqlparser_sqlserver_table_hint_anchor_t anchor,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_table_hint_t *next;
	char *copy;
	size_t next_capacity;

	if (state == NULL || text == NULL || anchor == SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "table hint arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (state->table_hint_count == state->table_hint_capacity) {
		next_capacity = state->table_hint_capacity == 0U ? 8U : state->table_hint_capacity * 2U;
		if (next_capacity < state->table_hint_capacity ||
		    next_capacity > SIZE_MAX / sizeof(*next)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (sqlparser_sqlserver_table_hint_t *)realloc(
			state->table_hints,
			next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->table_hints = next;
		state->table_hint_capacity = next_capacity;
	}
	copy = sqlparser_strndup(text, len);
	if (copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->table_hints[state->table_hint_count].sql = copy;
	state->table_hints[state->table_hint_count].anchor = anchor;
	state->table_hint_count++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_store_balanced_hint(
	const char *input,
	size_t *index,
	const char *keyword,
	char ***items,
	size_t *count,
	size_t *capacity,
	sqlparser_error_t *out_error)
{
	size_t open_pos;
	size_t close_pos;
	size_t next_pos;

	if (input == NULL || index == NULL || keyword == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "hint input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (!sqlparser_sqlserver_ascii_word_equal(input, *index, keyword)) {
		return SQLPARSER_STATUS_OK;
	}
	open_pos = sqlparser_sqlserver_skip_space(input, *index + strlen(keyword));
	if (input[open_pos] != '(' ||
	    !sqlparser_sqlserver_find_matching_paren(input, open_pos, &close_pos, &next_pos)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated SQL Server hint");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	(void)close_pos;
	if (sqlparser_sqlserver_array_add(
		    items,
		    count,
		    capacity,
		    input + *index,
		    next_pos - *index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*index = next_pos;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_copy_table_hint(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_table_hint_anchor_t anchor,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t open_pos;
	size_t close_pos;
	size_t next_pos;
	sqlparser_status_t status;

	if (input == NULL || index == NULL || state == NULL ||
	    !sqlparser_sqlserver_ascii_word_equal(input, *index, "with")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "table hint input is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	open_pos = sqlparser_sqlserver_skip_space(input, *index + strlen("with"));
	if (input[open_pos] != '(' ||
	    !sqlparser_sqlserver_find_matching_paren(input, open_pos, &close_pos, &next_pos)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated SQL Server hint");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	(void)close_pos;
	status = sqlparser_sqlserver_table_hint_add(
		state, input + *index, next_pos - *index, anchor, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		*index = next_pos;
	}
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_copy_query_hint(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	return sqlparser_sqlserver_store_balanced_hint(
		input,
		index,
		"option",
		&state->query_hints,
		&state->query_hint_count,
		&state->query_hint_capacity,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_copy_for_json_clause(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_state_t *state,
	size_t statement_ordinal,
	sqlparser_error_t *out_error)
{
	size_t start;
	size_t pos;
	size_t mode_start;
	size_t end;
	size_t depth;

	if (input == NULL || index == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "FOR JSON input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	start = *index;
	if (!sqlparser_sqlserver_ascii_word_equal(input, start, "for")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_sqlserver_skip_space(input, start + strlen("for"));
	if (!sqlparser_sqlserver_ascii_word_equal(input, pos, "json")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_sqlserver_skip_space(input, pos + strlen("json"));
	mode_start = pos;
	if (sqlparser_sqlserver_ascii_word_equal(input, mode_start, "auto")) {
		pos = mode_start + strlen("auto");
	} else if (sqlparser_sqlserver_ascii_word_equal(input, mode_start, "path")) {
		pos = mode_start + strlen("path");
	} else {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unsupported SQL Server FOR JSON mode");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	depth = 0U;
	while (input[pos] != '\0') {
		size_t skipped;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(input, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(input, pos);
			if (skipped > pos) {
				pos = skipped;
				continue;
			}
		}
		if (input[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (input[pos] == ')') {
			if (depth == 0U) {
				break;
			}
			depth--;
			pos++;
			continue;
		}
		if (depth == 0U && input[pos] == ';') {
			break;
		}
		if (depth == 0U && sqlparser_sqlserver_ascii_word_equal(input, pos, "option")) {
			break;
		}
		pos++;
	}

	end = sqlparser_sqlserver_trim_right(input, start, pos);
	if (sqlparser_sqlserver_array_add(
		    &state->json_suffixes,
		    &state->json_suffix_count,
		    &state->json_suffix_capacity,
		    input + start,
		    end - start,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	if (sqlparser_sqlserver_size_array_add(
		    &state->json_suffix_ordinals,
		    &state->json_suffix_ordinal_count,
		    &state->json_suffix_ordinal_capacity,
		    statement_ordinal,
		    out_error) != SQLPARSER_STATUS_OK) {
		free(state->json_suffixes[state->json_suffix_count - 1U]);
		state->json_suffixes[state->json_suffix_count - 1U] = NULL;
		state->json_suffix_count--;
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*index = pos;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_slice(
	const char *input,
	size_t start,
	size_t end,
	sqlparser_sqlserver_state_t *state,
	char **out_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	char *slice;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"slice output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;

	start = sqlparser_sqlserver_trim_left(input, start, end);
	end = sqlparser_sqlserver_trim_right(input, start, end);
	slice = sqlparser_strndup(input + start, end - start);
	if (slice == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	status = sqlparser_sqlserver_preprocess_text_origins(
		slice,
		state,
		out_sql,
		origins,
		out_error);
	free(slice);
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_append_mapped_identifiers(
	sqlparser_sqlserver_buffer_t *out,
	const char *sql,
	const sqlparser_identifier_origin_map_t *origins,
	size_t source_offset,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;
	sqlparser_identifier_origin_t origin;
	sqlparser_identifier_origin_kind_t kind;
	sqlparser_status_t status;
	size_t cursor;
	size_t sql_length;

	sql_length = strlen(sql);
	status = sqlparser_sqlserver_scanner_init(
		&scanner,
		sql,
		0U,
		sql_length,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	cursor = 0U;
	for (;;) {
		status = sqlparser_sqlserver_scanner_next(
			&scanner,
			&token,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (token.start > cursor) {
			status = sqlparser_sqlserver_buffer_append_mem(
				out,
				sql + cursor,
				token.start - cursor,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			break;
		}
		kind = SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_WORD ||
		    token.kind == SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER) {
			kind = sqlparser_identifier_origin_map_lookup(
				origins,
				token.start,
				token.end - token.start,
				&origin);
		}
		if (kind == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE) {
			if (origin.source_offset > SIZE_MAX - source_offset) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_RESOURCE_LIMIT,
					"SQL Server identifier origin offset is too large");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			status = sqlparser_sqlserver_buffer_append_source_identifier(
				out,
				sql + token.start,
				source_offset + origin.source_offset,
				origin.source_length,
				token.end - token.start,
				out_error);
		} else if (kind == SQLPARSER_IDENTIFIER_ORIGIN_GENERATED) {
			status = sqlparser_sqlserver_buffer_append_generated_identifier(
				out,
				sql + token.start,
				token.end - token.start,
				out_error);
		} else {
			status = sqlparser_sqlserver_buffer_append_mem(
				out,
				sql + token.start,
				token.end - token.start,
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		cursor = token.end;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_append_preprocessed_slice(
	sqlparser_sqlserver_buffer_t *out,
	const char *input,
	size_t start,
	size_t end,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	char *processed;
	sqlparser_identifier_origin_map_t *origins;
	sqlparser_status_t status;

	processed = NULL;
	origins = NULL;
	start = sqlparser_sqlserver_trim_left(input, start, end);
	end = sqlparser_sqlserver_trim_right(input, start, end);
	if (out->origin_writer.map != NULL) {
		status = sqlparser_identifier_origin_map_new_identity(
			end - start,
			&origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	status = sqlparser_sqlserver_preprocess_slice(
		input,
		start,
		end,
		state,
		&processed,
		origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_map_destroy(origins);
		return status;
	}
	if (origins != NULL) {
		status = sqlparser_sqlserver_append_mapped_identifiers(
			out,
			processed,
			origins,
			start,
			out_error);
	} else {
		status = sqlparser_sqlserver_buffer_append_cstr(
			out, processed, out_error);
	}
	sqlparser_identifier_origin_map_destroy(origins);
	free(processed);
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_copy_try_cast_keyword(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	(void)input;

	status = sqlparser_sqlserver_cast_restore_add(
		state,
		SQLPARSER_SQLSERVER_CAST_TRY_CAST,
		state->cast_count,
		NULL,
		0U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state->cast_count++;
	status = sqlparser_sqlserver_buffer_append_cstr(out, "CAST", out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index += strlen("try_cast");
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_copy_regular_cast_keyword(
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	state->cast_count++;
	if (sqlparser_sqlserver_buffer_append_cstr(out, "CAST", out_error) != SQLPARSER_STATUS_OK) {
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*index += strlen("cast");
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_copy_convert_function(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_sqlserver_cast_kind_t kind,
	const char *keyword,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t open_pos;
	size_t close_pos;
	size_t next_pos;
	size_t first_comma;
	size_t second_comma;
	size_t type_start;
	size_t type_end;
	size_t expr_start;
	size_t expr_end;
	size_t tail_start;
	size_t tail_end;
	sqlparser_status_t status;

	pos = *index + strlen(keyword);
	open_pos = sqlparser_sqlserver_skip_space(input, pos);
	if (input[open_pos] != '(' ||
	    !sqlparser_sqlserver_find_matching_paren(input, open_pos, &close_pos, &next_pos)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"invalid SQL Server CONVERT expression");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (!sqlparser_sqlserver_find_top_level_char(input, open_pos + 1U, close_pos, ',', &first_comma)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"SQL Server CONVERT requires a type and expression");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	type_start = sqlparser_sqlserver_trim_left(input, open_pos + 1U, first_comma);
	type_end = sqlparser_sqlserver_trim_right(input, type_start, first_comma);
	expr_start = first_comma + 1U;
	second_comma = close_pos;
	if (sqlparser_sqlserver_find_top_level_char(input, expr_start, close_pos, ',', &second_comma)) {
		expr_end = second_comma;
		tail_start = second_comma;
		tail_end = close_pos;
	} else {
		expr_end = close_pos;
		tail_start = close_pos;
		tail_end = close_pos;
	}
	expr_start = sqlparser_sqlserver_trim_left(input, expr_start, expr_end);
	expr_end = sqlparser_sqlserver_trim_right(input, expr_start, expr_end);

	status = sqlparser_sqlserver_cast_restore_add(
		state,
		kind,
		state->cast_count,
		input + tail_start,
		tail_end - tail_start,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state->cast_count++;

	status = sqlparser_sqlserver_buffer_append_cstr(out, "CAST(", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_preprocessed_slice(out, input, expr_start, expr_end, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(out, " AS ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_preprocessed_slice(out, input, type_start, type_end, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_char(out, ')', out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*index = next_pos;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_copy_parse_function(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_sqlserver_cast_kind_t kind,
	const char *keyword,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t open_pos;
	size_t close_pos;
	size_t next_pos;
	size_t as_pos;
	size_t using_pos;
	size_t expr_start;
	size_t expr_end;
	size_t type_start;
	size_t type_end;
	size_t tail_start;
	size_t tail_end;
	sqlparser_status_t status;

	pos = *index + strlen(keyword);
	open_pos = sqlparser_sqlserver_skip_space(input, pos);
	if (input[open_pos] != '(' ||
	    !sqlparser_sqlserver_find_matching_paren(input, open_pos, &close_pos, &next_pos)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"invalid SQL Server PARSE expression");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (!sqlparser_sqlserver_find_top_level_word(input, open_pos + 1U, close_pos, "as", &as_pos)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"SQL Server PARSE requires AS type");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	expr_start = sqlparser_sqlserver_trim_left(input, open_pos + 1U, as_pos);
	expr_end = sqlparser_sqlserver_trim_right(input, expr_start, as_pos);
	type_start = sqlparser_sqlserver_skip_space(input, as_pos + 2U);
	if (sqlparser_sqlserver_find_top_level_word(input, type_start, close_pos, "using", &using_pos)) {
		type_end = sqlparser_sqlserver_trim_right(input, type_start, using_pos);
		tail_start = using_pos;
		tail_end = close_pos;
	} else {
		type_end = sqlparser_sqlserver_trim_right(input, type_start, close_pos);
		tail_start = close_pos;
		tail_end = close_pos;
	}

	status = sqlparser_sqlserver_cast_restore_add(
		state,
		kind,
		state->cast_count,
		input + tail_start,
		tail_end - tail_start,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state->cast_count++;

	status = sqlparser_sqlserver_buffer_append_cstr(out, "CAST(", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_preprocessed_slice(out, input, expr_start, expr_end, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(out, " AS ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_preprocessed_slice(out, input, type_start, type_end, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_char(out, ')', out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*index = next_pos;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_copy_odbc_fn(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t content_start;
	size_t content_end;
	size_t depth;
	sqlparser_status_t status;

	pos = *index + 1U;
	pos = sqlparser_sqlserver_skip_space(input, pos);
	if (!sqlparser_sqlserver_ascii_word_equal(input, pos, "fn")) {
		return sqlparser_sqlserver_buffer_append_char(out, input[(*index)++], out_error);
	}
	pos += 2U;
	content_start = sqlparser_sqlserver_skip_space(input, pos);
	content_end = content_start;
	depth = 0U;
	while (input[content_end] != '\0') {
		size_t skipped;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(input, content_end)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(input, content_end);
			if (skipped > content_end) {
				content_end = skipped;
				continue;
			}
		}
		if (input[content_end] == '(') {
			depth++;
		} else if (input[content_end] == ')' && depth > 0U) {
			depth--;
		} else if (input[content_end] == '}' && depth == 0U) {
			status = sqlparser_sqlserver_append_preprocessed_slice(
				out,
				input,
				content_start,
				content_end,
				state,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			*index = content_end + 1U;
			return SQLPARSER_STATUS_OK;
		}
		content_end++;
	}

	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated ODBC scalar function");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_sqlserver_copy_rename_object(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t object_start;
	size_t to_pos;
	size_t name_start;
	size_t end;
	sqlparser_status_t status;

	pos = *index + strlen("rename");
	pos = sqlparser_sqlserver_skip_space(input, pos);
	if (!sqlparser_sqlserver_ascii_word_equal(input, pos, "object")) {
		return sqlparser_sqlserver_buffer_append_char(out, input[(*index)++], out_error);
	}
	pos += strlen("object");
	object_start = sqlparser_sqlserver_skip_space(input, pos);
	end = object_start;
	while (input[end] != '\0' && input[end] != ';') {
		end++;
	}
	if (!sqlparser_sqlserver_find_top_level_word(input, object_start, end, "to", &to_pos)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid SQL Server RENAME OBJECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	name_start = sqlparser_sqlserver_skip_space(input, to_pos + 2U);

	status = sqlparser_sqlserver_buffer_append_cstr(out, "ALTER TABLE ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_preprocessed_slice(out, input, object_start, to_pos, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(out, " RENAME TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_preprocessed_slice(out, input, name_start, end, state, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	state->rename_object_count++;
	*index = end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_text_origins(
	const char *input_sql,
	sqlparser_sqlserver_state_t *state,
	char **out_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	sqlparser_sqlserver_pending_top_list_t pending_tops;
	size_t index;
	size_t paren_depth;
	size_t statement_ordinal;
	sqlparser_status_t status;

	memset(&out, 0, sizeof(out));
	memset(&pending_tops, 0, sizeof(pending_tops));
	status = sqlparser_sqlserver_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_sqlserver_buffer_begin_origin(
		&out,
		origins,
		strlen(input_sql),
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}
	index = 0U;
	paren_depth = 0U;
	statement_ordinal = 0U;

	while (input_sql[index] != '\0') {
		size_t next_index;

		if (sqlparser_sqlserver_line_is_go(input_sql, index, &next_index)) {
			status = sqlparser_sqlserver_append_all_pending_top_limits(&out, &pending_tops, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			status = sqlparser_sqlserver_buffer_append_cstr(&out, "; ", out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			index = next_index;
			paren_depth = 0U;
			statement_ordinal++;
			continue;
		}

		if ((input_sql[index] == 'n' || input_sql[index] == 'N') && input_sql[index + 1U] == '\'') {
			status = sqlparser_sqlserver_copy_unicode_string(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(input_sql, index)) {
			int is_string_literal;

			is_string_literal = input_sql[index] == '\'';
			status = sqlparser_sqlserver_copy_quoted_or_comment(input_sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			if (is_string_literal) {
				state->literal_count++;
			}
			continue;
		}

		if (input_sql[index] == '@' || input_sql[index] == '?') {
			status = sqlparser_sqlserver_copy_parameter(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (input_sql[index] == '0' &&
		    (input_sql[index + 1U] == 'x' || input_sql[index + 1U] == 'X')) {
			status = sqlparser_sqlserver_copy_binary_literal(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (input_sql[index] == '#' &&
		    sqlparser_sqlserver_is_ident_start((unsigned char)input_sql[index + 1U])) {
			status = sqlparser_sqlserver_append_hash_identifier(input_sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (input_sql[index] == '{') {
			status = sqlparser_sqlserver_copy_odbc_fn(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "with")) {
			sqlparser_sqlserver_table_hint_anchor_t anchor;

			anchor = sqlparser_sqlserver_table_hint_anchor(input_sql, index);
			if (anchor != SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE) {
				status = sqlparser_sqlserver_copy_table_hint(input_sql, &index, anchor, state, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					sqlparser_sqlserver_pending_top_list_release(&pending_tops);
					return status;
				}
				continue;
			}
		}

		if (paren_depth == 0U && sqlparser_sqlserver_ascii_word_equal(input_sql, index, "for")) {
			status = sqlparser_sqlserver_copy_for_json_clause(
				input_sql,
				&index,
				state,
				statement_ordinal,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			if (status == SQLPARSER_STATUS_OK && !sqlparser_sqlserver_ascii_word_equal(input_sql, index, "for")) {
				continue;
			}
		}

		if (paren_depth == 0U &&
		    sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "option")) {
			status = sqlparser_sqlserver_copy_query_hint(input_sql, &index, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "try_cast")) {
			status = sqlparser_sqlserver_copy_try_cast_keyword(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "cast")) {
			status = sqlparser_sqlserver_copy_regular_cast_keyword(&index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "convert")) {
			status = sqlparser_sqlserver_copy_convert_function(
				input_sql,
				&index,
				&out,
				state,
				SQLPARSER_SQLSERVER_CAST_CONVERT,
				"convert",
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "try_convert")) {
			status = sqlparser_sqlserver_copy_convert_function(
				input_sql,
				&index,
				&out,
				state,
				SQLPARSER_SQLSERVER_CAST_TRY_CONVERT,
				"try_convert",
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "parse")) {
			status = sqlparser_sqlserver_copy_parse_function(
				input_sql,
				&index,
				&out,
				state,
				SQLPARSER_SQLSERVER_CAST_PARSE,
				"parse",
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "try_parse")) {
			status = sqlparser_sqlserver_copy_parse_function(
				input_sql,
				&index,
				&out,
				state,
				SQLPARSER_SQLSERVER_CAST_TRY_PARSE,
				"try_parse",
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "drop")) {
			status = sqlparser_sqlserver_record_drop_role_like(input_sql, index, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
		}

		if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "rename")) {
			status = sqlparser_sqlserver_copy_rename_object(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "select")) {
			char *top_public_limit;
			char *top_public_suffix;
			char *top_parser_limit;
			sqlparser_identifier_origin_map_t *top_parser_origins;
			size_t after_select;
			size_t top_pos;
			size_t top_source_offset;

			status = sqlparser_sqlserver_buffer_append_input_mem(
				&out,
				input_sql,
				index,
				6U,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			index += 6U;
			after_select = index;
			while (isspace((unsigned char)input_sql[index])) {
				status = sqlparser_sqlserver_buffer_append_input_mem(
					&out,
					input_sql,
					index,
					1U,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					sqlparser_sqlserver_pending_top_list_release(&pending_tops);
					return status;
				}
				index++;
			}

			if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "all") ||
			    sqlparser_sqlserver_ascii_word_equal(input_sql, index, "distinct")) {
				size_t word_len;

				word_len = sqlparser_sqlserver_ascii_word_equal(input_sql, index, "distinct") ? 8U : 3U;
				status = sqlparser_sqlserver_buffer_append_input_mem(
					&out,
					input_sql,
					index,
					word_len,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					sqlparser_sqlserver_pending_top_list_release(&pending_tops);
					return status;
				}
				index += word_len;
				while (isspace((unsigned char)input_sql[index])) {
					status = sqlparser_sqlserver_buffer_append_input_mem(
						&out,
						input_sql,
						index,
						1U,
						out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_sqlserver_buffer_release(&out);
						sqlparser_sqlserver_pending_top_list_release(&pending_tops);
						return status;
					}
					index++;
				}
			}

			top_pos = index;
			top_public_limit = NULL;
			top_public_suffix = NULL;
			top_parser_limit = NULL;
			top_parser_origins = NULL;
			top_source_offset = 0U;
			status = sqlparser_sqlserver_parse_top_clause(
				input_sql,
				&top_pos,
				state,
				&top_public_limit,
				&top_public_suffix,
				&top_parser_limit,
				out.origin_writer.map != NULL,
				&top_parser_origins,
				&top_source_offset,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				free(top_public_limit);
				free(top_public_suffix);
				free(top_parser_limit);
				sqlparser_identifier_origin_map_destroy(
					top_parser_origins);
				return status;
			}
			if (top_public_limit != NULL) {
				status = sqlparser_sqlserver_store_top_clause(state, top_public_limit, top_public_suffix, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					sqlparser_sqlserver_pending_top_list_release(&pending_tops);
					free(top_public_limit);
					free(top_public_suffix);
					free(top_parser_limit);
					sqlparser_identifier_origin_map_destroy(
						top_parser_origins);
					return status;
				}
				free(top_public_limit);
				free(top_public_suffix);
				status = sqlparser_sqlserver_pending_top_list_add(
					&pending_tops,
					&top_parser_limit,
					&top_parser_origins,
					top_source_offset,
					paren_depth,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					sqlparser_sqlserver_pending_top_list_release(&pending_tops);
					free(top_parser_limit);
					sqlparser_identifier_origin_map_destroy(
						top_parser_origins);
					return status;
				}
				index = sqlparser_sqlserver_skip_space(input_sql, top_pos);
				if (!isspace((unsigned char)input_sql[after_select]) && input_sql[index] != '\0') {
					status = sqlparser_sqlserver_buffer_append_char(&out, ' ', out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_sqlserver_buffer_release(&out);
						sqlparser_sqlserver_pending_top_list_release(&pending_tops);
						return status;
					}
				}
			}
			continue;
		}

		if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "identity")) {
			status = sqlparser_sqlserver_skip_identity_clause(input_sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "by")) {
			size_t target_pos;

			target_pos = sqlparser_sqlserver_skip_space(input_sql, index + 2U);
			if (sqlparser_sqlserver_ascii_word_equal(input_sql, target_pos, "target")) {
				index = target_pos + strlen("target");
				continue;
			}
		}

		if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "save")) {
			size_t trans_pos;

			trans_pos = sqlparser_sqlserver_skip_space(input_sql, index + 4U);
			if (sqlparser_sqlserver_ascii_word_equal(input_sql, trans_pos, "transaction") ||
			    sqlparser_sqlserver_ascii_word_equal(input_sql, trans_pos, "tran")) {
				status = sqlparser_sqlserver_buffer_append_cstr(&out, "SAVEPOINT", out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					sqlparser_sqlserver_pending_top_list_release(&pending_tops);
					return status;
				}
				index = trans_pos;
				while (input_sql[index] != '\0' && !isspace((unsigned char)input_sql[index])) {
					index++;
				}
				continue;
			}
		}

		if (input_sql[index] == '(') {
			paren_depth++;
		} else if (input_sql[index] == ')' && paren_depth > 0U) {
			status = sqlparser_sqlserver_append_pending_top_limits_at_depth(
				&out,
				&pending_tops,
				paren_depth,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
			paren_depth--;
		} else if (input_sql[index] == ';' && paren_depth == 0U) {
			status = sqlparser_sqlserver_append_pending_top_limits_at_depth(
				&out,
				&pending_tops,
				0U,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				sqlparser_sqlserver_pending_top_list_release(&pending_tops);
				return status;
			}
		}

		status = sqlparser_sqlserver_buffer_append_input_mem(
			&out,
			input_sql,
			index,
			1U,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_sqlserver_pending_top_list_release(&pending_tops);
			return status;
		}
		if (input_sql[index] == ';' && paren_depth == 0U) {
			statement_ordinal++;
		}
		index++;
	}

	status = sqlparser_sqlserver_append_all_pending_top_limits(&out, &pending_tops, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		sqlparser_sqlserver_pending_top_list_release(&pending_tops);
		return status;
	}
	sqlparser_sqlserver_pending_top_list_release(&pending_tops);

	status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_commit_origin(
			&out,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_text(
	const char *input_sql,
	sqlparser_sqlserver_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_sqlserver_preprocess_text_origins(
		input_sql,
		state,
		out_sql,
		NULL,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_param_to_public(
	const char *sql,
	size_t *index,
	const sqlparser_sqlserver_state_t *state,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_error_t *out_error)
{
	unsigned long value;
	size_t pos;

	pos = *index + 1U;
	if (!isdigit((unsigned char)sql[pos])) {
		return sqlparser_sqlserver_buffer_append_char(out, sql[(*index)++], out_error);
	}

	value = 0UL;
	while (isdigit((unsigned char)sql[pos])) {
		unsigned int digit;

		digit = (unsigned int)(sql[pos] - '0');
		if (value > (((unsigned long)-1) - digit) / 10UL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "parameter index overflow");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		value = value * 10UL + digit;
		pos++;
	}

	if (state != NULL && value > 0UL && (size_t)value <= state->param_count) {
		*index = pos;
		return sqlparser_sqlserver_buffer_append_cstr(out, state->param_names[value - 1UL], out_error);
	}

	return sqlparser_sqlserver_buffer_append_char(out, sql[(*index)++], out_error);
}

static size_t sqlparser_sqlserver_quoted_literal_end(const char *sql, size_t start)
{
	size_t pos;

	if (sql == NULL || sql[start] != '\'') {
		return 0U;
	}

	pos = start + 1U;
	while (sql[pos] != '\0') {
		if (sql[pos] == '\'' && sql[pos + 1U] == '\'') {
			pos += 2U;
			continue;
		}
		if (sql[pos] == '\'') {
			return pos + 1U;
		}
		pos++;
	}

	return 0U;
}

static sqlparser_status_t sqlparser_sqlserver_append_public_temp_identifier(
	const char *sql,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out == NULL || sql[*index] != '"' || sql[*index + 1U] != '#') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"temporary identifier arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	pos = *index + 1U;
	while (sql[pos] != '\0') {
		if (sql[pos] == '"') {
			if (sql[pos + 1U] == '"') {
				status = sqlparser_sqlserver_buffer_append_char(out, '"', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos += 2U;
				continue;
			}
			*index = pos + 1U;
			return SQLPARSER_STATUS_OK;
		}
		status = sqlparser_sqlserver_buffer_append_char(out, sql[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}

	return sqlparser_sqlserver_buffer_append_char(out, sql[(*index)++], out_error);
}

static sqlparser_status_t sqlparser_sqlserver_append_public_system_variable(
	const char *sql,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t name_start;
	size_t pos;
	sqlparser_status_t status;

	name_start = *index + 1U + strlen(SQLPARSER_SQLSERVER_SYSTEM_VARIABLE_PREFIX);
	pos = name_start;
	while (sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos])) {
		pos++;
	}
	if (pos == name_start ||
	    sql[pos] != '"' ||
	    sql[pos + 1U] != '(' ||
	    sql[pos + 2U] != ')') {
		return sqlparser_sqlserver_buffer_append_char(out, sql[(*index)++], out_error);
	}

	status = sqlparser_sqlserver_buffer_append_cstr(out, "@@", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_mem(
			out,
			sql + name_start,
			pos - name_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		*index = pos + 3U;
	}
	return status;
}

static int sqlparser_sqlserver_literal_matches(
	const char *sql,
	size_t start,
	size_t end,
	const char *expected)
{
	size_t len;

	if (expected == NULL) {
		return 0;
	}
	len = strlen(expected);
	return len == (end - start) && strncmp(sql + start, expected, len) == 0;
}

static sqlparser_status_t sqlparser_sqlserver_postprocess_core(
	const char *core_sql,
	const sqlparser_sqlserver_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	size_t index;
	size_t unicode_index;
	size_t literal_ordinal;
	sqlparser_status_t status;

	memset(&out, 0, sizeof(out));
	status = sqlparser_sqlserver_buffer_reserve_input(&out, core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	unicode_index = 0U;
	literal_ordinal = 0U;
	while (core_sql[index] != '\0') {
		if (core_sql[index] == '\'') {
			size_t literal_end;
			int use_unicode_prefix;

			literal_end = sqlparser_sqlserver_quoted_literal_end(core_sql, index);
			use_unicode_prefix = 0;
			if (literal_end > index &&
			    state != NULL &&
			    unicode_index < state->unicode_count &&
			    state->unicode_ordinals[unicode_index] == literal_ordinal &&
			    sqlparser_sqlserver_literal_matches(
				    core_sql,
				    index,
				    literal_end,
				    state->unicode_literals[unicode_index])) {
				use_unicode_prefix = 1;
				unicode_index++;
			}
			if (use_unicode_prefix) {
				status = sqlparser_sqlserver_buffer_append_char(&out, 'N', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					return status;
				}
			}
			if (literal_end == 0U) {
				status = sqlparser_sqlserver_buffer_append_char(&out, core_sql[index++], out_error);
			} else {
				status = sqlparser_sqlserver_buffer_append_mem(
					&out,
					core_sql + index,
					literal_end - index,
					out_error);
				index = literal_end;
			}
			literal_ordinal++;
		} else if (core_sql[index] == '"' &&
		           strncmp(
			           core_sql + index + 1U,
			           SQLPARSER_SQLSERVER_SYSTEM_VARIABLE_PREFIX,
			           strlen(SQLPARSER_SQLSERVER_SYSTEM_VARIABLE_PREFIX)) == 0) {
			status = sqlparser_sqlserver_append_public_system_variable(
				core_sql, &index, &out, out_error);
		} else if (core_sql[index] == '"' && core_sql[index + 1U] == '#') {
			status = sqlparser_sqlserver_append_public_temp_identifier(core_sql, &index, &out, out_error);
		} else if (core_sql[index] == '$') {
			status = sqlparser_sqlserver_param_to_public(core_sql, &index, state, &out, out_error);
		} else if (sqlparser_sqlserver_ascii_word_equal(core_sql, index, "pg_catalog") &&
		           core_sql[index + strlen("pg_catalog")] == '.' &&
		           sqlparser_sqlserver_ascii_word_equal(core_sql, index + strlen("pg_catalog") + 1U, "bit")) {
			status = sqlparser_sqlserver_buffer_append_cstr(&out, "bit", out_error);
			index += strlen("pg_catalog.bit");
		} else {
			status = sqlparser_sqlserver_buffer_append_char(&out, core_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_append_public_use_value(
	sqlparser_sqlserver_buffer_t *out,
	const char *value_start,
	const char *value_end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;

	if (value_start < value_end && *value_start == '"' && *(value_end - 1) == '"') {
		status = sqlparser_sqlserver_buffer_append_char(out, '[', out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos = value_start + 1;
		while (pos < value_end - 1) {
			if (*pos == '"' && pos + 1 < value_end - 1 && *(pos + 1) == '"') {
				status = sqlparser_sqlserver_buffer_append_char(out, '"', out_error);
				pos += 2;
			} else if (*pos == ']') {
				status = sqlparser_sqlserver_buffer_append_cstr(out, "]]", out_error);
				pos++;
			} else {
				status = sqlparser_sqlserver_buffer_append_char(out, *pos, out_error);
				pos++;
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		return sqlparser_sqlserver_buffer_append_char(out, ']', out_error);
	}

	return sqlparser_sqlserver_buffer_append_mem(
		out,
		value_start,
		(size_t)(value_end - value_start),
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_rewrite_internal_use_statement(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	const char *value_start;
	const char *value_end;
	const char *prefix;
	size_t start;
	size_t end;
	size_t prefix_len;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	start = sqlparser_sqlserver_trim_left(sql, 0U, strlen(sql));
	end = sqlparser_sqlserver_trim_right(sql, start, strlen(sql));
	prefix = "SET " SQLPARSER_INTERNAL_CURRENT_DATABASE " TO ";
	prefix_len = strlen(prefix);
	if (end - start < prefix_len || strncmp(sql + start, prefix, prefix_len) != 0) {
		return SQLPARSER_STATUS_OK;
	}

	value_start = sql + start + prefix_len;
	value_end = sql + end;
	value_end = sqlparser_sqlserver_trim_right(value_start, 0U, (size_t)(value_end - value_start)) + value_start;
	if (value_start >= value_end) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_sqlserver_buffer_append_cstr(&out, "USE ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_public_use_value(&out, value_start, value_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_read_internal_string_arg(
	const char *sql,
	size_t *index,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	size_t pos;
	size_t token_start;
	size_t token_end;
	char quote;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "internal argument output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = NULL;
	pos = sqlparser_sqlserver_skip_space(sql, *index);
	if (sql[pos] == '\'' || sql[pos] == '"') {
		quote = sql[pos];
		pos++;
		memset(&out, 0, sizeof(out));
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					status = sqlparser_sqlserver_buffer_append_char(&out, quote, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_sqlserver_buffer_release(&out);
						return status;
					}
					pos += 2U;
					continue;
				}
				pos++;
				*index = pos;
				*out_value = sqlparser_sqlserver_buffer_take(&out);
				if (*out_value == NULL) {
					*out_value = sqlparser_strdup("");
					if (*out_value == NULL) {
						sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
						return SQLPARSER_STATUS_NO_MEMORY;
					}
				}
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_sqlserver_buffer_append_char(&out, sql[pos], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				return status;
			}
			pos++;
		}
		sqlparser_sqlserver_buffer_release(&out);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated internal SQL Server prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	token_start = pos;
	while (sql[pos] != '\0' && sql[pos] != ',') {
		pos++;
	}
	token_end = sqlparser_sqlserver_trim_right(sql, token_start, pos);
	if (token_start >= token_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal SQL Server prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	*out_value = sqlparser_strndup(sql + token_start, token_end - token_start);
	if (*out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*index = pos;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_sqlserver_internal_set_prefix(
	const char *sql,
	const char *internal_name,
	size_t *out_pos)
{
	size_t pos;
	size_t len;

	pos = sqlparser_sqlserver_skip_space(sql, 0U);
	if (!sqlparser_sqlserver_ascii_word_equal(sql, pos, "set")) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space(sql, pos + strlen("set"));
	len = strlen(internal_name);
	if (strncmp(sql + pos, internal_name, len) != 0 ||
	    sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos + len])) {
		return 0;
	}
	pos = sqlparser_sqlserver_skip_space(sql, pos + len);
	if (!sqlparser_sqlserver_ascii_word_equal(sql, pos, "to") && sql[pos] != '=') {
		return 0;
	}
	pos = sql[pos] == '=' ? pos + 1U : pos + strlen("to");
	*out_pos = pos;
	return 1;
}

static sqlparser_status_t sqlparser_sqlserver_rewrite_internal_prepared_statement(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	static const struct {
		const char *internal_name;
		const char *proc_name;
	} specs[] = {
		{SQLPARSER_INTERNAL_SQLSERVER_SP_PREPARE, "sp_prepare"},
		{SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTE, "sp_execute"},
		{SQLPARSER_INTERNAL_SQLSERVER_SP_PREPEXEC, "sp_prepexec"},
		{SQLPARSER_INTERNAL_SQLSERVER_SP_UNPREPARE, "sp_unprepare"},
		{SQLPARSER_INTERNAL_SQLSERVER_SP_EXECUTESQL, "sp_executesql"}
	};
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	char *args;
	size_t index;
	size_t pos;
	size_t spec_index;
	int appended_arg;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	for (spec_index = 0U; spec_index < sizeof(specs) / sizeof(specs[0]); spec_index++) {
		if (!sqlparser_sqlserver_internal_set_prefix(sql, specs[spec_index].internal_name, &pos)) {
			continue;
		}
		index = pos;
		memset(&out, 0, sizeof(out));
		status = sqlparser_sqlserver_buffer_append_cstr(&out, "EXEC ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_cstr(&out, specs[spec_index].proc_name, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_char(&out, ' ', out_error);
		}
		appended_arg = 0;
		while (status == SQLPARSER_STATUS_OK) {
			args = NULL;
			status = sqlparser_sqlserver_read_internal_string_arg(sql, &index, &args, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(args);
				sqlparser_sqlserver_buffer_release(&out);
				return status;
			}
			if (appended_arg) {
				status = sqlparser_sqlserver_buffer_append_cstr(&out, ", ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_sqlserver_buffer_append_cstr(&out, args, out_error);
			}
			free(args);
			args = NULL;
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
			appended_arg = 1;
			index = sqlparser_sqlserver_skip_space(sql, index);
			if (sql[index] != ',') {
				break;
			}
			index++;
			index = sqlparser_sqlserver_skip_space(sql, index);
		}
		if (status == SQLPARSER_STATUS_OK && sql[index] != '\0') {
			sqlparser_sqlserver_buffer_release(&out);
			return SQLPARSER_STATUS_OK;
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return status;
		}
		free(*io_sql);
		*io_sql = sqlparser_sqlserver_buffer_take(&out);
		if (*io_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_rewrite_internal_raw_statement(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	static const char *const internal_names[] = {
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_APPLICATION_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_APPLICATION_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_DROP_APPLICATION_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_SYNONYM,
		SQLPARSER_INTERNAL_SQLSERVER_DROP_SYNONYM,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_TYPE,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_DATABASE,
		SQLPARSER_INTERNAL_SQLSERVER_DROP_INDEX,
		SQLPARSER_INTERNAL_SQLSERVER_UPDATE_STATISTICS,
		SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_EXECUTE_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_REVERT_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_SETUSER_STATEMENT,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_USER,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_USER,
		SQLPARSER_INTERNAL_SQLSERVER_CREATE_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_ROLE,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_SCHEMA,
		SQLPARSER_INTERNAL_SQLSERVER_ALTER_AUTHORIZATION
	};
	char *public_sql;
	const char *sql;
	size_t pos;
	size_t spec_index;
	size_t end;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	for (spec_index = 0U; spec_index < sizeof(internal_names) / sizeof(internal_names[0]); spec_index++) {
		if (!sqlparser_sqlserver_internal_set_prefix(sql, internal_names[spec_index], &pos)) {
			continue;
		}
		public_sql = NULL;
		status = sqlparser_sqlserver_read_internal_string_arg(sql, &pos, &public_sql, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(public_sql);
			return status;
		}
		end = sqlparser_sqlserver_skip_space(sql, pos);
		if (sql[end] != '\0') {
			free(public_sql);
			return SQLPARSER_STATUS_OK;
		}
		free(*io_sql);
		*io_sql = public_sql;
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_rewrite_internal_use(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	char *statement_sql;
	char *original_statement_sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start < len) {
		statement_end = sqlparser_sqlserver_statement_end(sql, segment_start, len);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		original_statement_sql = statement_sql;
		status = sqlparser_sqlserver_rewrite_internal_use_statement(&statement_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && statement_sql == original_statement_sql) {
			status = sqlparser_sqlserver_rewrite_internal_prepared_statement(&statement_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && statement_sql == original_statement_sql) {
			status = sqlparser_sqlserver_rewrite_internal_raw_statement(&statement_sql, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			free(statement_sql);
			sqlparser_sqlserver_buffer_release(&out);
			return status;
		}
		if (statement_sql != original_statement_sql) {
			if (!rewritten) {
				status = sqlparser_sqlserver_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(statement_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = sqlparser_sqlserver_trim_left(sql, segment_start, statement_end);
			status = sqlparser_sqlserver_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_sqlserver_buffer_append_cstr(&out, statement_sql, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				free(statement_sql);
				sqlparser_sqlserver_buffer_release(&out);
				return status;
			}
			copy_start = statement_end;
		}
		free(statement_sql);
		if (statement_end >= len) {
			break;
		}
		segment_start = statement_end + 1U;
	}

	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_sqlserver_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static char *sqlparser_sqlserver_replace_slice(
	const char *input,
	size_t start,
	size_t end,
	const char *replacement)
{
	sqlparser_sqlserver_buffer_t out;
	sqlparser_error_t ignored_error;

	memset(&out, 0, sizeof(out));
	memset(&ignored_error, 0, sizeof(ignored_error));
	if (sqlparser_sqlserver_buffer_append_mem(&out, input, start, &ignored_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_sqlserver_buffer_append_cstr(&out, replacement, &ignored_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_sqlserver_buffer_append_cstr(&out, input + end, &ignored_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_sqlserver_buffer_finish(&out, &ignored_error) != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return NULL;
	}
	return sqlparser_sqlserver_buffer_take(&out);
}

static int sqlparser_sqlserver_find_last_limit_offset(
	const char *sql,
	size_t *out_limit_pos,
	size_t *out_offset_pos)
{
	size_t pos;
	size_t limit_pos;
	size_t offset_pos;

	limit_pos = (size_t)-1;
	offset_pos = (size_t)-1;
	for (pos = 0U; sql[pos] != '\0'; pos++) {
		if (sqlparser_sqlserver_ascii_word_equal(sql, pos, "limit")) {
			limit_pos = pos;
		} else if (sqlparser_sqlserver_ascii_word_equal(sql, pos, "offset")) {
			offset_pos = pos;
		}
	}

	if (limit_pos == (size_t)-1 && offset_pos == (size_t)-1) {
		return 0;
	}

	if (out_limit_pos != NULL) {
		*out_limit_pos = limit_pos;
	}
	if (out_offset_pos != NULL) {
		*out_offset_pos = offset_pos;
	}
	return 1;
}

static int sqlparser_sqlserver_find_select_for_limit(
	const char *sql,
	size_t limit_pos,
	size_t *out_select_pos)
{
	size_t pos;
	size_t select_pos;
	size_t paren_depth;
	size_t target_depth;

	if (sql == NULL || out_select_pos == NULL) {
		return 0;
	}

	target_depth = 0U;
	paren_depth = 0U;
	for (pos = 0U; sql[pos] != '\0' && pos < limit_pos;) {
		size_t skipped;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(sql, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
			if (skipped > pos) {
				if (skipped > limit_pos) {
					break;
				}
				pos = skipped;
				continue;
			}
		}
		if (sql[pos] == '(') {
			paren_depth++;
		} else if (sql[pos] == ')' && paren_depth > 0U) {
			paren_depth--;
		}
		pos++;
	}
	target_depth = paren_depth;

	select_pos = (size_t)-1;
	paren_depth = 0U;
	pos = 0U;
	while (sql[pos] != '\0' && pos < limit_pos) {
		size_t skipped;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(sql, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
			if (skipped > pos) {
				if (skipped > limit_pos) {
					break;
				}
				pos = skipped;
				continue;
			}
		}
		if (sql[pos] == '(') {
			paren_depth++;
		} else if (sql[pos] == ')' && paren_depth > 0U) {
			paren_depth--;
		} else if (paren_depth == target_depth && sqlparser_sqlserver_ascii_word_equal(sql, pos, "select")) {
			select_pos = pos;
		}
		pos++;
	}

	if (select_pos == (size_t)-1) {
		return 0;
	}

	*out_select_pos = select_pos;
	return 1;
}

static size_t sqlparser_sqlserver_token_end(const char *sql, size_t pos)
{
	while (sql[pos] != '\0' && !isspace((unsigned char)sql[pos]) && sql[pos] != ';') {
		pos++;
	}
	return pos;
}

static sqlparser_status_t sqlparser_sqlserver_apply_top_public(
	char **io_sql,
	const sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	char *sql;
	size_t top_index;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->top_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	for (top_index = 0U; top_index < state->top_count; top_index++) {
		sqlparser_sqlserver_buffer_t out;
		const char *suffix;
		size_t select_pos;
		size_t insert_pos;
		size_t limit_pos;
		size_t offset_pos;
		size_t limit_end;

		if (!sqlparser_sqlserver_find_last_limit_offset(sql, &limit_pos, &offset_pos) ||
		    limit_pos == (size_t)-1 ||
		    (offset_pos != (size_t)-1 && offset_pos < limit_pos) ||
		    !sqlparser_sqlserver_find_select_for_limit(sql, limit_pos, &select_pos)) {
			continue;
		}

		insert_pos = select_pos + strlen("select");
		insert_pos = sqlparser_sqlserver_skip_space(sql, insert_pos);
		if (sqlparser_sqlserver_ascii_word_equal(sql, insert_pos, "distinct")) {
			insert_pos += strlen("distinct");
			insert_pos = sqlparser_sqlserver_skip_space(sql, insert_pos);
		} else if (sqlparser_sqlserver_ascii_word_equal(sql, insert_pos, "all")) {
			insert_pos += strlen("all");
			insert_pos = sqlparser_sqlserver_skip_space(sql, insert_pos);
		}

		limit_end = sqlparser_sqlserver_token_end(sql, sqlparser_sqlserver_skip_space(sql, limit_pos + strlen("limit")));
		while (limit_pos > 0U && isspace((unsigned char)sql[limit_pos - 1U])) {
			limit_pos--;
		}

		memset(&out, 0, sizeof(out));
		if (sqlparser_sqlserver_buffer_append_mem(&out, sql, insert_pos, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_buffer_append_cstr(&out, "TOP (", out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_buffer_append_cstr(&out, state->top_limits[top_index], out_error) !=
			    SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_buffer_append_char(&out, ')', out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		suffix = top_index < state->top_suffix_count ? state->top_suffixes[top_index] : "";
		if (sqlparser_sqlserver_buffer_append_cstr(&out, suffix, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_buffer_append_char(&out, ' ', out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_buffer_append_mem(&out, sql + insert_pos, limit_pos - insert_pos, out_error) !=
			    SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_buffer_append_cstr(&out, sql + limit_end, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		free(sql);
		sql = sqlparser_sqlserver_buffer_take(&out);
		*io_sql = sql;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_apply_offset_public(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	char *sql;
	char *next;
	char limit_text[128];
	char offset_text[128];
	char replacement[320];
	size_t limit_pos;
	size_t offset_pos;
	size_t limit_start;
	size_t limit_end;
	size_t offset_start;
	size_t offset_end;
	size_t len;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	if (!sqlparser_sqlserver_find_last_limit_offset(sql, &limit_pos, &offset_pos) ||
	    limit_pos == (size_t)-1 ||
	    offset_pos == (size_t)-1 ||
	    offset_pos < limit_pos) {
		return SQLPARSER_STATUS_OK;
	}

	limit_start = sqlparser_sqlserver_skip_space(sql, limit_pos + strlen("limit"));
	limit_end = sqlparser_sqlserver_token_end(sql, limit_start);
	offset_start = sqlparser_sqlserver_skip_space(sql, offset_pos + strlen("offset"));
	offset_end = sqlparser_sqlserver_token_end(sql, offset_start);
	len = limit_end - limit_start;
	if (len == 0U || len >= sizeof(limit_text)) {
		return SQLPARSER_STATUS_OK;
	}
	memcpy(limit_text, sql + limit_start, len);
	limit_text[len] = '\0';

	len = offset_end - offset_start;
	if (len == 0U || len >= sizeof(offset_text)) {
		return SQLPARSER_STATUS_OK;
	}
	memcpy(offset_text, sql + offset_start, len);
	offset_text[len] = '\0';

	(void)snprintf(
		replacement,
		sizeof(replacement),
		" OFFSET %s ROWS FETCH NEXT %s ROWS ONLY",
		offset_text,
		limit_text);
	while (limit_pos > 0U && isspace((unsigned char)sql[limit_pos - 1U])) {
		limit_pos--;
	}
	next = sqlparser_sqlserver_replace_slice(sql, limit_pos, offset_end, replacement);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	free(sql);
	*io_sql = next;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_sqlserver_cast_restore_t *sqlparser_sqlserver_cast_restore_for_ordinal(
	const sqlparser_sqlserver_state_t *state,
	size_t ordinal)
{
	size_t index;

	if (state == NULL) {
		return NULL;
	}
	for (index = 0U; index < state->cast_restore_count; index++) {
		if (state->cast_restores[index].ordinal == ordinal) {
			return &state->cast_restores[index];
		}
	}
	return NULL;
}

static int sqlparser_sqlserver_find_public_cast_parts(
	const char *sql,
	size_t cast_pos,
	size_t *out_cast_end,
	size_t *out_expr_start,
	size_t *out_expr_end,
	size_t *out_type_start,
	size_t *out_type_end)
{
	size_t open_pos;
	size_t close_pos;
	size_t next_pos;
	size_t as_pos;
	size_t expr_start;
	size_t expr_end;
	size_t type_start;
	size_t type_end;

	if (!sqlparser_sqlserver_ascii_word_equal(sql, cast_pos, "cast")) {
		return 0;
	}
	open_pos = sqlparser_sqlserver_skip_space(sql, cast_pos + strlen("cast"));
	if (sql[open_pos] != '(' ||
	    !sqlparser_sqlserver_find_matching_paren(sql, open_pos, &close_pos, &next_pos) ||
	    !sqlparser_sqlserver_find_top_level_word(sql, open_pos + 1U, close_pos, "as", &as_pos)) {
		return 0;
	}

	expr_start = sqlparser_sqlserver_trim_left(sql, open_pos + 1U, as_pos);
	expr_end = sqlparser_sqlserver_trim_right(sql, expr_start, as_pos);
	type_start = sqlparser_sqlserver_skip_space(sql, as_pos + 2U);
	type_end = sqlparser_sqlserver_trim_right(sql, type_start, close_pos);

	if (out_cast_end != NULL) {
		*out_cast_end = next_pos;
	}
	if (out_expr_start != NULL) {
		*out_expr_start = expr_start;
	}
	if (out_expr_end != NULL) {
		*out_expr_end = expr_end;
	}
	if (out_type_start != NULL) {
		*out_type_start = type_start;
	}
	if (out_type_end != NULL) {
		*out_type_end = type_end;
	}
	return 1;
}

static int sqlparser_sqlserver_find_reverse_matching_paren(
	const char *sql,
	size_t close_pos,
	size_t *out_open_pos)
{
	size_t pos;
	size_t depth;

	if (sql == NULL || sql[close_pos] != ')') {
		return 0;
	}

	pos = close_pos;
	depth = 1U;
	while (pos > 0U) {
		pos--;
		if (sql[pos] == ')') {
			depth++;
		} else if (sql[pos] == '(') {
			depth--;
			if (depth == 0U) {
				if (out_open_pos != NULL) {
					*out_open_pos = pos;
				}
				return 1;
			}
		}
	}

	return 0;
}

static size_t sqlparser_sqlserver_find_reverse_quoted_start(
	const char *sql,
	size_t end,
	char quote)
{
	size_t pos;

	if (sql == NULL || end == 0U || sql[end - 1U] != quote) {
		return (size_t)-1;
	}

	pos = end - 1U;
	while (pos > 0U) {
		pos--;
		if (sql[pos] != quote) {
			continue;
		}
		if (pos > 0U && sql[pos - 1U] == quote) {
			pos--;
			continue;
		}
		return pos;
	}

	return (size_t)-1;
}

static size_t sqlparser_sqlserver_public_typecast_ident_start(const char *sql, size_t end)
{
	size_t pos;

	pos = end;
	while (pos > 0U) {
		unsigned char ch;

		ch = (unsigned char)sql[pos - 1U];
		if (sqlparser_sqlserver_is_ident_char(ch) ||
		    ch == '.' ||
		    ch == '$' ||
		    ch == '+' ||
		    ch == '-') {
			pos--;
			continue;
		}
		break;
	}

	return pos;
}

static int sqlparser_sqlserver_find_public_typecast_expr_bounds(
	const char *sql,
	size_t colon_pos,
	size_t *out_expr_start,
	size_t *out_expr_end)
{
	size_t expr_end;
	size_t expr_start;

	if (sql == NULL || sql[colon_pos] != ':' || sql[colon_pos + 1U] != ':') {
		return 0;
	}

	expr_end = colon_pos;
	while (expr_end > 0U && isspace((unsigned char)sql[expr_end - 1U])) {
		expr_end--;
	}
	if (expr_end == 0U) {
		return 0;
	}

	if (sql[expr_end - 1U] == ')') {
		size_t open_pos;

		if (!sqlparser_sqlserver_find_reverse_matching_paren(sql, expr_end - 1U, &open_pos)) {
			return 0;
		}
		expr_start = open_pos;
		while (expr_start > 0U && isspace((unsigned char)sql[expr_start - 1U])) {
			expr_start--;
		}
		expr_start = sqlparser_sqlserver_public_typecast_ident_start(sql, expr_start);
	} else if (sql[expr_end - 1U] == '\'' || sql[expr_end - 1U] == '"') {
		expr_start = sqlparser_sqlserver_find_reverse_quoted_start(sql, expr_end, sql[expr_end - 1U]);
		if (expr_start == (size_t)-1) {
			return 0;
		}
	} else {
		expr_start = sqlparser_sqlserver_public_typecast_ident_start(sql, expr_end);
	}

	if (expr_start >= expr_end) {
		return 0;
	}
	if (out_expr_start != NULL) {
		*out_expr_start = expr_start;
	}
	if (out_expr_end != NULL) {
		*out_expr_end = expr_end;
	}
	return 1;
}

static size_t sqlparser_sqlserver_public_typecast_type_atom_end(const char *sql, size_t pos)
{
	while (sql[pos] != '\0') {
		if (sql[pos] == '"') {
			pos = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
			continue;
		}
		if (sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos]) ||
		    sql[pos] == '.') {
			pos++;
			continue;
		}
		break;
	}

	if (sql[pos] == '(') {
		size_t close_pos;
		size_t next_pos;

		if (sqlparser_sqlserver_find_matching_paren(sql, pos, &close_pos, &next_pos)) {
			pos = next_pos;
		}
	}

	return pos;
}

static int sqlparser_sqlserver_find_public_typecast_parts(
	const char *sql,
	size_t colon_pos,
	size_t *out_cast_end,
	size_t *out_expr_start,
	size_t *out_expr_end,
	size_t *out_type_start,
	size_t *out_type_end)
{
	size_t expr_start;
	size_t expr_end;
	size_t type_start;
	size_t type_end;

	if (!sqlparser_sqlserver_find_public_typecast_expr_bounds(
		    sql,
		    colon_pos,
		    &expr_start,
		    &expr_end)) {
		return 0;
	}

	type_start = sqlparser_sqlserver_skip_space(sql, colon_pos + 2U);
	if (!sqlparser_sqlserver_is_ident_char((unsigned char)sql[type_start]) &&
	    sql[type_start] != '"') {
		return 0;
	}

	type_end = sqlparser_sqlserver_public_typecast_type_atom_end(sql, type_start);
	if (sqlparser_sqlserver_ascii_word_equal(sql, type_end + strspn(sql + type_end, " \t\r\n"), "without") ||
	    sqlparser_sqlserver_ascii_word_equal(sql, type_end + strspn(sql + type_end, " \t\r\n"), "with")) {
		size_t qualifier_start;
		size_t qualifier_end;

		qualifier_start = sqlparser_sqlserver_skip_space(sql, type_end);
		qualifier_end = sqlparser_sqlserver_public_typecast_type_atom_end(sql, qualifier_start);
		if (qualifier_end > qualifier_start) {
			size_t next_start;

			type_end = qualifier_end;
			next_start = sqlparser_sqlserver_skip_space(sql, type_end);
			if (sqlparser_sqlserver_ascii_word_equal(sql, next_start, "time")) {
				type_end = sqlparser_sqlserver_public_typecast_type_atom_end(sql, next_start);
				next_start = sqlparser_sqlserver_skip_space(sql, type_end);
			}
			if (sqlparser_sqlserver_ascii_word_equal(sql, next_start, "zone")) {
				type_end = sqlparser_sqlserver_public_typecast_type_atom_end(sql, next_start);
			}
		}
	} else if (sqlparser_sqlserver_ascii_word_equal(sql, type_start, "double")) {
		size_t next_start;

		next_start = sqlparser_sqlserver_skip_space(sql, type_end);
		if (sqlparser_sqlserver_ascii_word_equal(sql, next_start, "precision")) {
			type_end = sqlparser_sqlserver_public_typecast_type_atom_end(sql, next_start);
		}
	}

	if (type_end <= type_start) {
		return 0;
	}
	if (out_cast_end != NULL) {
		*out_cast_end = type_end;
	}
	if (out_expr_start != NULL) {
		*out_expr_start = expr_start;
	}
	if (out_expr_end != NULL) {
		*out_expr_end = expr_end;
	}
	if (out_type_start != NULL) {
		*out_type_start = type_start;
	}
	if (out_type_end != NULL) {
		*out_type_end = type_end;
	}
	return 1;
}

static sqlparser_status_t sqlparser_sqlserver_append_mem_range(
	sqlparser_sqlserver_buffer_t *out,
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	if (end <= start) {
		return SQLPARSER_STATUS_OK;
	}
	return sqlparser_sqlserver_buffer_append_mem(out, sql + start, end - start, out_error);
}

static sqlparser_status_t sqlparser_sqlserver_append_restored_cast(
	sqlparser_sqlserver_buffer_t *out,
	const char *sql,
	size_t cast_pos,
	size_t cast_end,
	size_t expr_start,
	size_t expr_end,
	size_t type_start,
	size_t type_end,
	const sqlparser_sqlserver_cast_restore_t *restore,
	int source_is_typecast,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (restore->kind == SQLPARSER_SQLSERVER_CAST_TRY_CAST) {
		status = sqlparser_sqlserver_buffer_append_cstr(out, "TRY_CAST", out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (!source_is_typecast) {
			return sqlparser_sqlserver_append_mem_range(
				out,
				sql,
				cast_pos + strlen("CAST"),
				cast_end,
				out_error);
		}
		status = sqlparser_sqlserver_buffer_append_char(out, '(', out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_append_mem_range(out, sql, expr_start, expr_end, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_cstr(out, " AS ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_append_mem_range(out, sql, type_start, type_end, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_char(out, ')', out_error);
		}
		return status;
	}

	if (restore->kind == SQLPARSER_SQLSERVER_CAST_CONVERT ||
	    restore->kind == SQLPARSER_SQLSERVER_CAST_TRY_CONVERT) {
		status = sqlparser_sqlserver_buffer_append_cstr(
			out,
			restore->kind == SQLPARSER_SQLSERVER_CAST_TRY_CONVERT ? "TRY_CONVERT(" : "CONVERT(",
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_append_mem_range(out, sql, type_start, type_end, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_cstr(out, ", ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_append_mem_range(out, sql, expr_start, expr_end, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && restore->tail != NULL) {
			status = sqlparser_sqlserver_buffer_append_cstr(out, restore->tail, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_char(out, ')', out_error);
		}
		return status;
	}

	status = sqlparser_sqlserver_buffer_append_cstr(
		out,
		restore->kind == SQLPARSER_SQLSERVER_CAST_TRY_PARSE ? "TRY_PARSE(" : "PARSE(",
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_mem_range(out, sql, expr_start, expr_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(out, " AS ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_mem_range(out, sql, type_start, type_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && restore->tail != NULL && restore->tail[0] != '\0') {
		status = sqlparser_sqlserver_buffer_append_char(out, ' ', out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_cstr(out, restore->tail, out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_char(out, ')', out_error);
	}
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_apply_cast_public(
	char **io_sql,
	const sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	size_t pos;
	size_t copy_start;
	size_t cast_ordinal;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->cast_restore_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	sql = *io_sql;
	pos = 0U;
	copy_start = 0U;
	cast_ordinal = 0U;
	while (sql[pos] != '\0') {
		size_t skipped;
		size_t cast_end;
		size_t expr_start;
		size_t expr_end;
		size_t type_start;
		size_t type_end;
		const sqlparser_sqlserver_cast_restore_t *restore;
		int source_is_typecast;
		size_t replace_start;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(sql, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
			if (skipped > pos) {
				pos = skipped;
				continue;
			}
		}

		source_is_typecast = 0;
		if (sqlparser_sqlserver_find_public_cast_parts(
			    sql,
			    pos,
			    &cast_end,
			    &expr_start,
			    &expr_end,
			    &type_start,
			    &type_end)) {
			replace_start = pos;
		} else if (sqlparser_sqlserver_find_public_typecast_parts(
				   sql,
				   pos,
				   &cast_end,
				   &expr_start,
				   &expr_end,
				   &type_start,
				   &type_end)) {
			source_is_typecast = 1;
			replace_start = expr_start;
		} else {
			pos++;
			continue;
		}

		restore = sqlparser_sqlserver_cast_restore_for_ordinal(state, cast_ordinal);
		cast_ordinal++;
		if (restore == NULL) {
			pos = cast_end;
			continue;
		}

		status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, replace_start, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return status;
		}

		status = sqlparser_sqlserver_append_restored_cast(
			&out,
			sql,
			pos,
			cast_end,
			expr_start,
			expr_end,
			type_start,
			type_end,
			restore,
			source_is_typecast,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return status;
		}

		pos = cast_end;
		copy_start = cast_end;
	}

	status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, strlen(sql), out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_apply_rename_public(
	char **io_sql,
	const sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	char *sql;
	size_t pos;
	size_t end;
	size_t rename_pos;
	sqlparser_sqlserver_buffer_t out;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->rename_object_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	pos = sqlparser_sqlserver_skip_space(sql, 0U);
	if (!sqlparser_sqlserver_ascii_word_equal(sql, pos, "alter")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_sqlserver_skip_space(sql, pos + strlen("alter"));
	if (!sqlparser_sqlserver_ascii_word_equal(sql, pos, "table")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_sqlserver_skip_space(sql, pos + strlen("table"));
	end = strlen(sql);
	if (!sqlparser_sqlserver_find_top_level_word(sql, pos, end, "rename", &rename_pos)) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_sqlserver_buffer_append_cstr(&out, "RENAME OBJECT ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_append_mem_range(&out, sql, pos, rename_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		pos = sqlparser_sqlserver_skip_space(sql, rename_pos + strlen("rename"));
		if (sqlparser_sqlserver_ascii_word_equal(sql, pos, "to")) {
			pos += strlen("to");
		}
		pos = sqlparser_sqlserver_skip_space(sql, pos);
		status = sqlparser_sqlserver_buffer_append_cstr(&out, "TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(&out, sql + pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_apply_drop_user_public(
	char **io_sql,
	const sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	size_t pos;
	size_t copy_start;
	size_t drop_role_ordinal;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->drop_user_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	sql = *io_sql;
	pos = 0U;
	copy_start = 0U;
	drop_role_ordinal = 0U;
	rewritten = 0;
	while (sql[pos] != '\0') {
		size_t skipped;
		size_t role_pos;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(sql, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
			if (skipped > pos) {
				pos = skipped;
				continue;
			}
		}

		if (!sqlparser_sqlserver_ascii_word_equal(sql, pos, "drop")) {
			pos++;
			continue;
		}

		role_pos = sqlparser_sqlserver_skip_space(sql, pos + strlen("drop"));
		if (!sqlparser_sqlserver_ascii_word_equal(sql, role_pos, "role")) {
			pos++;
			continue;
		}

		drop_role_ordinal++;
		if (!sqlparser_sqlserver_is_drop_user_ordinal(state, drop_role_ordinal)) {
			pos = role_pos + strlen("role");
			continue;
		}

		status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, role_pos, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_cstr(&out, "USER", out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return status;
		}
		pos = role_pos + strlen("role");
		copy_start = pos;
		rewritten = 1;
	}

	if (!rewritten) {
		sqlparser_sqlserver_buffer_release(&out);
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, strlen(sql), out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_sqlserver_table_hint_insert_pos(const char *sql, size_t source_word_pos)
{
	size_t pos;
	size_t depth;

	if (sqlparser_sqlserver_ascii_word_equal(sql, source_word_pos, "from")) {
		pos = source_word_pos + strlen("from");
	} else if (sqlparser_sqlserver_ascii_word_equal(sql, source_word_pos, "join")) {
		pos = source_word_pos + strlen("join");
	} else if (sqlparser_sqlserver_ascii_word_equal(sql, source_word_pos, "update")) {
		pos = source_word_pos + strlen("update");
	} else if (sqlparser_sqlserver_ascii_word_equal(sql, source_word_pos, "insert")) {
		pos = sqlparser_sqlserver_skip_space(sql, source_word_pos + strlen("insert"));
		if (sqlparser_sqlserver_ascii_word_equal(sql, pos, "into")) {
			pos = sqlparser_sqlserver_skip_space(sql, pos + strlen("into"));
		}
		while (sql[pos] != '\0') {
			size_t skipped;

			if (sqlparser_sqlserver_can_copy_quoted_or_comment(sql, pos)) {
				skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
				if (skipped > pos) {
					pos = skipped;
					continue;
				}
			}
			if (sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos]) || sql[pos] == '.') {
				pos++;
				continue;
			}
			break;
		}
		return pos;
	} else {
		return source_word_pos;
	}

	pos = sqlparser_sqlserver_skip_space(sql, pos);
	depth = 0U;
	while (sql[pos] != '\0') {
		size_t skipped;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(sql, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
			if (skipped > pos) {
				pos = skipped;
				continue;
			}
		}
		if (sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth == 0U) {
				break;
			}
			depth--;
			pos++;
			continue;
		}
		if (depth == 0U &&
		    (sql[pos] == ',' ||
		     sql[pos] == ';' ||
		     sqlparser_sqlserver_word_ends_table_source(sql, pos))) {
			break;
		}
		pos++;
	}
	return sqlparser_sqlserver_trim_right(sql, source_word_pos, pos);
}

static sqlparser_status_t sqlparser_sqlserver_apply_table_hints_public(
	char **io_sql,
	const sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	size_t pos;
	size_t copy_start;
	size_t hint_index;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->table_hint_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	sql = *io_sql;
	pos = 0U;
	copy_start = 0U;
	hint_index = 0U;
	rewritten = 0;
	while (sql[pos] != '\0' && hint_index < state->table_hint_count) {
		size_t skipped;
		size_t insert_pos;
		sqlparser_sqlserver_table_hint_anchor_t anchor;

		if (sqlparser_sqlserver_can_copy_quoted_or_comment(sql, pos)) {
			skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, pos);
			if (skipped > pos) {
				pos = skipped;
				continue;
			}
		}
		anchor = sqlparser_sqlserver_table_source_anchor(sql, pos);
		if (anchor == SQLPARSER_SQLSERVER_TABLE_HINT_ANCHOR_NONE ||
		    anchor != state->table_hints[hint_index].anchor) {
			pos++;
			continue;
		}

		insert_pos = sqlparser_sqlserver_table_hint_insert_pos(sql, pos);
		if (insert_pos <= pos) {
			pos++;
			continue;
		}
		status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, insert_pos, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_char(&out, ' ', out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_cstr(
				&out, state->table_hints[hint_index].sql, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return status;
		}
		hint_index++;
		copy_start = insert_pos;
		pos = insert_pos;
		rewritten = 1;
	}

	if (!rewritten) {
		sqlparser_sqlserver_buffer_release(&out);
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, strlen(sql), out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_apply_statement_suffixes_public(
	char **io_sql,
	char *const *suffixes,
	size_t suffix_count,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	size_t len;
	size_t start;
	size_t copy_start;
	size_t suffix_index;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || suffixes == NULL || suffix_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	sql = *io_sql;
	len = strlen(sql);
	start = 0U;
	copy_start = 0U;
	suffix_index = 0U;
	rewritten = 0;
	while (sql[start] != '\0' && suffix_index < suffix_count) {
		size_t statement_end;
		size_t insert_pos;

		statement_end = sqlparser_sqlserver_statement_end(sql, start, len);
		insert_pos = sqlparser_sqlserver_trim_right(sql, start, statement_end);
		if (insert_pos > start) {
			status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, insert_pos, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_sqlserver_buffer_append_char(&out, ' ', out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_sqlserver_buffer_append_cstr(
					&out,
					suffixes[suffix_index],
					out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				return status;
			}
			suffix_index++;
			copy_start = insert_pos;
			rewritten = 1;
		}
		if (sql[statement_end] == ';') {
			start = statement_end + 1U;
		} else {
			start = statement_end;
		}
	}

	if (!rewritten) {
		sqlparser_sqlserver_buffer_release(&out);
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, len, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_apply_json_suffixes_public(
	char **io_sql,
	const sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	const char *sql;
	size_t len;
	size_t start;
	size_t copy_start;
	size_t suffix_index;
	size_t statement_ordinal;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || state == NULL || state->json_suffix_count == 0U) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	sql = *io_sql;
	len = strlen(sql);
	start = 0U;
	copy_start = 0U;
	suffix_index = 0U;
	statement_ordinal = 0U;
	rewritten = 0;
	while (sql[start] != '\0' && suffix_index < state->json_suffix_count) {
		size_t statement_end;
		size_t insert_pos;
		size_t suffix_ordinal;

		statement_end = sqlparser_sqlserver_statement_end(sql, start, len);
		suffix_ordinal = suffix_index < state->json_suffix_ordinal_count ?
			state->json_suffix_ordinals[suffix_index] :
			statement_ordinal;
		if (suffix_ordinal == statement_ordinal) {
			insert_pos = sqlparser_sqlserver_trim_right(sql, start, statement_end);
			if (insert_pos > start) {
				status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, insert_pos, out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_sqlserver_buffer_append_char(&out, ' ', out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_sqlserver_buffer_append_cstr(
						&out,
						state->json_suffixes[suffix_index],
						out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					return status;
				}
				suffix_index++;
				copy_start = insert_pos;
				rewritten = 1;
			}
		}
		if (sql[statement_end] == ';') {
			start = statement_end + 1U;
		} else {
			start = statement_end;
		}
		statement_ordinal++;
	}

	if (!rewritten) {
		sqlparser_sqlserver_buffer_release(&out);
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_sqlserver_append_mem_range(&out, sql, copy_start, len, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_sqlserver_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_apply_query_hints_public(
	char **io_sql,
	const sqlparser_sqlserver_state_t *state,
	sqlparser_error_t *out_error)
{
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	return sqlparser_sqlserver_apply_statement_suffixes_public(
		io_sql,
		state->query_hints,
		state->query_hint_count,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_postprocess_text(
	const char *core_sql,
	const sqlparser_sqlserver_state_t *state,
	int restore_statement_hints,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	sqlparser_status_t status;

	public_sql = NULL;
	status = sqlparser_sqlserver_postprocess_core(core_sql, state, &public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_sqlserver_apply_cast_public(&public_sql, state, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_apply_drop_user_public(&public_sql, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_apply_rename_public(&public_sql, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_apply_top_public(&public_sql, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && (state == NULL || state->top_count == 0U)) {
		status = sqlparser_sqlserver_apply_offset_public(&public_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && restore_statement_hints) {
		status = sqlparser_sqlserver_apply_table_hints_public(&public_sql, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && restore_statement_hints) {
		status = sqlparser_sqlserver_output_postprocess(
			&public_sql,
			state != NULL ? state->output_state : NULL,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && restore_statement_hints) {
		status = sqlparser_sqlserver_apply_json_suffixes_public(&public_sql, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && restore_statement_hints) {
		status = sqlparser_sqlserver_apply_query_hints_public(&public_sql, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_rewrite_internal_use(&public_sql, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}

	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_rewritten_owned(
	char *preprocess_sql,
	const sqlparser_limits_t *limits,
	unsigned int candidates,
	sqlparser_sqlserver_state_t *state,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	*out_parser_sql = NULL;
	status = origins != NULL ?
		sqlparser_sqlserver_output_preprocess_identifier_origins(
			&preprocess_sql,
			limits,
			candidates,
			&state->output_state,
			origins,
			out_error) :
		sqlparser_sqlserver_output_preprocess(
			&preprocess_sql,
			limits,
			candidates,
			&state->output_state,
			out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(preprocess_sql);
		return status;
	}

	status = sqlparser_sqlserver_reject_unsupported(preprocess_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(preprocess_sql);
		return status;
	}

	status = sqlparser_sqlserver_preprocess_text_origins(
		preprocess_sql,
		state,
		out_parser_sql,
		origins,
		out_error);
	free(preprocess_sql);
	return status;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_statement_owned(
	char *preprocess_sql,
	const sqlparser_limits_t *limits,
	sqlparser_sqlserver_state_t *state,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	unsigned int candidates;
	sqlparser_status_t status;

	*out_parser_sql = NULL;
	status = sqlparser_sqlserver_rewrite_use_statements(
		&preprocess_sql, origins, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(preprocess_sql);
		return status;
	}
	candidates = sqlparser_sqlserver_candidate_mask(preprocess_sql);
	return sqlparser_sqlserver_preprocess_rewritten_owned(
		preprocess_sql,
		limits,
		candidates,
		state,
		out_parser_sql,
		origins,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_control(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	sqlparser_sqlserver_state_t *state,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	size_t index;
	sqlparser_status_t status;

	memset(&out, 0, sizeof(out));
	status = sqlparser_sqlserver_buffer_begin_origin(
		&out,
		origins,
		strlen(input_sql),
		out_error);
	for (index = 0U; index < state->control->flow->unit_count; index++) {
		const sqlparser_control_unit_t *unit;
		sqlparser_sqlserver_state_t *unit_state;
		sqlparser_identifier_origin_map_t *unit_origins;
		char *source_sql;
		char *parser_sql;

		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
		unit = &state->control->flow->units[index];
		unit_state = state->control->unit_states[index];
		unit_origins = NULL;
		if (origins != NULL) {
			status = sqlparser_identifier_origin_map_new_identity(
				unit->source_length,
				&unit_origins,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				break;
			}
		}
		source_sql = sqlparser_strndup(
			input_sql + unit->source_offset,
			unit->source_length);
		if (source_sql == NULL) {
			sqlparser_identifier_origin_map_destroy(unit_origins);
			status = SQLPARSER_STATUS_NO_MEMORY;
			sqlparser_error_set_message(out_error, status, "out of memory");
			break;
		}
		parser_sql = NULL;
		if (unit->kind == SQLPARSER_CONTROL_UNIT_CONDITION) {
			status = sqlparser_sqlserver_preprocess_text_origins(
				source_sql,
				unit_state,
				&parser_sql,
				unit_origins,
				out_error);
			free(source_sql);
		} else {
			status = sqlparser_sqlserver_preprocess_statement_owned(
				source_sql,
				limits,
				unit_state,
				&parser_sql,
				unit_origins,
				out_error);
		}
		if (status == SQLPARSER_STATUS_OK && parser_sql == NULL) {
			status = SQLPARSER_STATUS_INTERNAL_ERROR;
			sqlparser_error_set_message(
				out_error,
				status,
				"SQL Server control unit parser SQL is missing");
		}
		if (status == SQLPARSER_STATUS_OK &&
		    unit->kind == SQLPARSER_CONTROL_UNIT_CONDITION) {
			status = sqlparser_sqlserver_buffer_append_cstr(
				&out, "SELECT 1 WHERE ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = unit_origins != NULL ?
				sqlparser_sqlserver_append_mapped_identifiers(
					&out,
					parser_sql,
					unit_origins,
					unit->source_offset,
					out_error) :
				sqlparser_sqlserver_buffer_append_cstr(
					&out, parser_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_sqlserver_buffer_append_cstr(&out, ";\n", out_error);
		}
		free(parser_sql);
		sqlparser_identifier_origin_map_destroy(unit_origins);
		if (status != SQLPARSER_STATUS_OK) {
			break;
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_commit_origin(
			&out,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
	}
	*out_parser_sql = sqlparser_sqlserver_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_state_t *state;
	sqlparser_control_state_t *flow;
	char *preprocess_sql;
	unsigned int candidates;
	sqlparser_status_t status;

	if (out_parser_sql == NULL || out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect preprocess output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_state = NULL;
	if (input_sql == NULL || limits == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL input and limits must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	state = NULL;
	status = sqlparser_sqlserver_state_new(&state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	preprocess_sql = sqlparser_strdup(input_sql);
	if (preprocess_sql == NULL) {
		sqlparser_sqlserver_state_destroy(state);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_sqlserver_rewrite_use_statements(
		&preprocess_sql, NULL, out_error);
	candidates = status == SQLPARSER_STATUS_OK ?
		sqlparser_sqlserver_candidate_mask(preprocess_sql) : 0U;
	flow = NULL;
	if (status == SQLPARSER_STATUS_OK &&
	    (candidates & SQLPARSER_SQLSERVER_CANDIDATE_CONTROL) != 0U) {
		status = sqlparser_sqlserver_control_parse(input_sql, limits, &flow, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && flow != NULL) {
		free(preprocess_sql);
		preprocess_sql = NULL;
		status = sqlparser_sqlserver_control_bundle_new(state, flow, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			flow = NULL;
			status = sqlparser_sqlserver_preprocess_control(
				input_sql,
				limits,
				state,
				out_parser_sql,
				NULL,
				out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK && state->control == NULL) {
		status = sqlparser_sqlserver_preprocess_rewritten_owned(
			preprocess_sql,
			limits,
			candidates,
			state,
			out_parser_sql,
			NULL,
			out_error);
		preprocess_sql = NULL;
	}
	free(preprocess_sql);
	sqlparser_control_state_release(flow);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_state_destroy(state);
		return status;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_sqlserver_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_state_t *state;
	sqlparser_control_state_t *flow;
	char *candidate_sql;
	char *preprocess_sql;
	unsigned int candidates;
	sqlparser_status_t status;

	if (out_parser_sql != NULL) {
		*out_parser_sql = NULL;
	}
	if (out_state != NULL) {
		*out_state = NULL;
	}
	if (input_sql == NULL || limits == NULL || out_parser_sql == NULL ||
	    out_state == NULL || origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server identifier origin arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	state = NULL;
	flow = NULL;
	candidate_sql = sqlparser_strdup(input_sql);
	if (candidate_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_sqlserver_rewrite_use_statements(
		&candidate_sql, NULL, out_error);
	candidates = status == SQLPARSER_STATUS_OK ?
		sqlparser_sqlserver_candidate_mask(candidate_sql) : 0U;
	if (status == SQLPARSER_STATUS_OK &&
	    (candidates & SQLPARSER_SQLSERVER_CANDIDATE_CONTROL) != 0U) {
		status = sqlparser_sqlserver_control_parse(
			input_sql, limits, &flow, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_state_new(&state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && flow != NULL) {
		status = sqlparser_sqlserver_control_bundle_new(
			state, flow, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			flow = NULL;
			status = sqlparser_sqlserver_preprocess_control(
				input_sql,
				limits,
				state,
				out_parser_sql,
				origins,
				out_error);
		}
	} else if (status == SQLPARSER_STATUS_OK) {
		preprocess_sql = sqlparser_strdup(input_sql);
		if (preprocess_sql == NULL) {
			status = SQLPARSER_STATUS_NO_MEMORY;
			sqlparser_error_set_message(
				out_error,
				status,
				"out of memory");
		} else {
			status = sqlparser_sqlserver_preprocess_statement_owned(
				preprocess_sql,
				limits,
				state,
				out_parser_sql,
				origins,
				out_error);
		}
	}
	free(candidate_sql);
	sqlparser_control_state_release(flow);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_parser_sql);
		*out_parser_sql = NULL;
		sqlparser_sqlserver_state_destroy(state);
		return status;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_fragment(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_state_t *unit_state;

	if (out_parser_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL fragment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"SQL Server dialect state is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	unit_state = sqlparser_sqlserver_state_for_statement_mutable(
		(sqlparser_sqlserver_state_t *)state, statement_index, NULL);
	if (unit_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server fragment statement index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_sqlserver_preprocess_text(
		input_sql,
		unit_state,
		out_parser_sql,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *public_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect deparse output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	public_sql = NULL;
	status = sqlparser_sqlserver_postprocess_text(
		core_sql,
		(const sqlparser_sqlserver_state_t *)state,
		1,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dialect_rewrite_like_escape(&public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_postprocess_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_sqlserver_state_t *unit_state;
	char *public_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"core SQL fragment must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	unit_state = sqlparser_sqlserver_state_for_statement(
		(const sqlparser_sqlserver_state_t *)state, statement_index, NULL);
	if (unit_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server fragment statement index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	public_sql = NULL;
	status = sqlparser_sqlserver_postprocess_text(
		core_sql,
		unit_state,
		0,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dialect_rewrite_like_escape(&public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_clone_state_single(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_sqlserver_state_t *source;
	sqlparser_sqlserver_state_t *clone;
	size_t index;
	sqlparser_status_t status;

	if (out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;

	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	source = (const sqlparser_sqlserver_state_t *)state;
	status = sqlparser_sqlserver_state_new(&clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	for (index = 0U; index < source->param_count; index++) {
		status = sqlparser_sqlserver_array_add(
			&clone->param_names,
			&clone->param_count,
			&clone->param_capacity,
			source->param_names[index],
			strlen(source->param_names[index]),
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	for (index = 0U; index < source->unicode_count; index++) {
		status = sqlparser_sqlserver_store_unicode_literal(
			clone,
			source->unicode_literals[index],
			strlen(source->unicode_literals[index]),
			source->unicode_ordinals[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	clone->literal_count = source->literal_count;
	for (index = 0U; index < source->top_count; index++) {
		const char *suffix;

		suffix = index < source->top_suffix_count ? source->top_suffixes[index] : "";
		status = sqlparser_sqlserver_store_top_clause(
			clone,
			source->top_limits[index],
			suffix,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	for (index = 0U; index < source->table_hint_count; index++) {
		status = sqlparser_sqlserver_table_hint_add(
			clone,
			source->table_hints[index].sql,
			strlen(source->table_hints[index].sql),
			source->table_hints[index].anchor,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	for (index = 0U; index < source->query_hint_count; index++) {
		status = sqlparser_sqlserver_array_add(
			&clone->query_hints,
			&clone->query_hint_count,
			&clone->query_hint_capacity,
			source->query_hints[index],
			strlen(source->query_hints[index]),
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	for (index = 0U; index < source->json_suffix_count; index++) {
		status = sqlparser_sqlserver_array_add(
			&clone->json_suffixes,
			&clone->json_suffix_count,
			&clone->json_suffix_capacity,
			source->json_suffixes[index],
			strlen(source->json_suffixes[index]),
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	for (index = 0U; index < source->json_suffix_ordinal_count; index++) {
		status = sqlparser_sqlserver_size_array_add(
			&clone->json_suffix_ordinals,
			&clone->json_suffix_ordinal_count,
			&clone->json_suffix_ordinal_capacity,
			source->json_suffix_ordinals[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	for (index = 0U; index < source->cast_restore_count; index++) {
		status = sqlparser_sqlserver_cast_restore_add(
			clone,
			source->cast_restores[index].kind,
			source->cast_restores[index].ordinal,
			source->cast_restores[index].tail,
			source->cast_restores[index].tail != NULL ? strlen(source->cast_restores[index].tail) : 0U,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	clone->cast_count = source->cast_count;
	clone->rename_object_count = source->rename_object_count;
	clone->drop_role_like_count = source->drop_role_like_count;
	for (index = 0U; index < source->drop_user_count; index++) {
		status = sqlparser_sqlserver_size_array_add(
			&clone->drop_user_ordinals,
			&clone->drop_user_count,
			&clone->drop_user_capacity,
			source->drop_user_ordinals[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			return status;
		}
	}
	status = sqlparser_sqlserver_output_clone(
		source->output_state,
		&clone->output_state,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_state_destroy(clone);
		return status;
	}

	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_clone_state(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_sqlserver_state_t *source;
	sqlparser_sqlserver_state_t *clone;
	sqlparser_sqlserver_control_bundle_t *bundle;
	size_t bytes;
	size_t index;
	sqlparser_status_t status;

	status = sqlparser_sqlserver_clone_state_single(state, out_state, out_error);
	if (status != SQLPARSER_STATUS_OK || state == NULL) {
		return status;
	}
	source = (const sqlparser_sqlserver_state_t *)state;
	clone = (sqlparser_sqlserver_state_t *)*out_state;
	if (source->control == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (source->control->flow != NULL || source->control->unit_count == 0U ||
	    source->control->unit_count - 1U >
		    (SIZE_MAX - sizeof(*bundle)) / sizeof(bundle->unit_states[0])) {
		sqlparser_sqlserver_state_destroy(clone);
		*out_state = NULL;
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"SQL Server control dialect state cannot be cloned");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	bytes = sizeof(*bundle) +
		(source->control->unit_count - 1U) * sizeof(bundle->unit_states[0]);
	bundle = (sqlparser_sqlserver_control_bundle_t *)calloc(1U, bytes);
	if (bundle == NULL) {
		sqlparser_sqlserver_state_destroy(clone);
		*out_state = NULL;
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	bundle->unit_count = source->control->unit_count;
	bundle->unit_states[0] = clone;
	clone->control = bundle;
	for (index = 1U; index < bundle->unit_count; index++) {
		void *unit_clone;

		unit_clone = NULL;
		status = sqlparser_sqlserver_clone_state_single(
			source->control->unit_states[index],
			&unit_clone,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_state_destroy(clone);
			*out_state = NULL;
			return status;
		}
		bundle->unit_states[index] = (sqlparser_sqlserver_state_t *)unit_clone;
	}
	return SQLPARSER_STATUS_OK;
}

const sqlparser_sqlserver_output_state_t *sqlparser_sqlserver_state_output(
	const void *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	const sqlparser_sqlserver_state_t *sqlserver_state;

	sqlserver_state = sqlparser_sqlserver_state_for_statement(
		(const sqlparser_sqlserver_state_t *)state,
		statement_index,
		out_local_statement_index);
	return sqlserver_state != NULL ? sqlserver_state->output_state : NULL;
}

sqlparser_sqlserver_output_state_t *sqlparser_sqlserver_state_output_mutable(
	void *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	sqlparser_sqlserver_state_t *sqlserver_state;

	sqlserver_state = sqlparser_sqlserver_state_for_statement_mutable(
		(sqlparser_sqlserver_state_t *)state,
		statement_index,
		out_local_statement_index);
	return sqlserver_state != NULL ? sqlserver_state->output_state : NULL;
}

static sqlparser_status_t sqlparser_sqlserver_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_sqlserver_state_t *sqlserver_state;
	size_t index;
	size_t literal_end;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sqlserver_state = sqlparser_sqlserver_state_for_statement(
		(const sqlparser_sqlserver_state_t *)state, statement_index, NULL);
	literal_end = core_sql[0] == '\'' ? sqlparser_sqlserver_quoted_literal_end(core_sql, 0U) : 0U;
	if (literal_end > 0U && core_sql[literal_end] == '\0' && sqlserver_state != NULL) {
		for (index = 0U; index < sqlserver_state->unicode_count; index++) {
			if (sqlserver_state->unicode_ordinals[index] == literal_index &&
			    sqlparser_sqlserver_literal_matches(
				    core_sql,
				    0U,
				    literal_end,
				    sqlserver_state->unicode_literals[index])) {
				sqlparser_sqlserver_buffer_t out;
				sqlparser_status_t status;

				memset(&out, 0, sizeof(out));
				status = sqlparser_sqlserver_buffer_append_char(&out, 'N', out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_sqlserver_buffer_append_cstr(&out, core_sql, out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_sqlserver_buffer_finish(&out, out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					return status;
				}
				*out_sql = out.data;
				return SQLPARSER_STATUS_OK;
			}
		}
	}

	*out_sql = sqlparser_strdup(core_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static const PgQuery__VariableSetStmt *sqlparser_sqlserver_session_set_statement(
	const PgQuery__Node *statement)
{
	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL ||
	    statement->variable_set_stmt->name == NULL) {
		return NULL;
	}
	return statement->variable_set_stmt;
}

static const char *sqlparser_sqlserver_session_raw_argument(
	const PgQuery__VariableSetStmt *set_stmt,
	const char *internal_name)
{
	const PgQuery__Node *arg;

	if (set_stmt == NULL || internal_name == NULL ||
	    strcmp(set_stmt->name, internal_name) != 0 ||
	    set_stmt->n_args != 1U || set_stmt->args == NULL) {
		return NULL;
	}
	arg = set_stmt->args[0];
	if (arg == NULL ||
	    arg->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    arg->a_const == NULL ||
	    arg->a_const->val_case != PG_QUERY__A__CONST__VAL_SVAL ||
	    arg->a_const->sval == NULL) {
		return NULL;
	}
	return arg->a_const->sval->sval;
}

static int sqlparser_sqlserver_session_parse_integer(
	const char *sql,
	size_t start,
	size_t end,
	long long *out_value)
{
	unsigned long long value;
	unsigned long long limit;
	unsigned int digit;
	int negative;

	if (sql == NULL || out_value == NULL || start >= end) {
		return 0;
	}
	negative = 0;
	if (sql[start] == '+' || sql[start] == '-') {
		negative = sql[start] == '-';
		start++;
		if (start == end) {
			return 0;
		}
	}
	value = 0U;
	limit = negative ? (unsigned long long)LLONG_MAX + 1U : (unsigned long long)LLONG_MAX;
	while (start < end) {
		if (!isdigit((unsigned char)sql[start])) {
			return 0;
		}
		digit = (unsigned int)(sql[start] - '0');
		if (value > (limit - digit) / 10U) {
			return 0;
		}
		value = value * 10U + digit;
		start++;
	}
	*out_value = negative ?
		(value == (unsigned long long)LLONG_MAX + 1U ? LLONG_MIN : -(long long)value) :
		(long long)value;
	return 1;
}

static char *sqlparser_sqlserver_session_decode_string(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	size_t pos;
	sqlparser_status_t status;

	if (sql == NULL || start >= end) {
		return NULL;
	}
	if ((sql[start] == 'N' || sql[start] == 'n') &&
	    start + 1U < end && sql[start + 1U] == '\'') {
		start++;
	}
	if (end - start < 2U || sql[start] != '\'' || sql[end - 1U] != '\'') {
		return NULL;
	}
	memset(&out, 0, sizeof(out));
	for (pos = start + 1U; pos < end - 1U; pos++) {
		if (sql[pos] == '\'' && pos + 1U < end - 1U && sql[pos + 1U] == '\'') {
			pos++;
		}
		status = sqlparser_sqlserver_buffer_append_char(&out, sql[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			return NULL;
		}
	}
	status = sqlparser_sqlserver_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return NULL;
	}
	return sqlparser_sqlserver_buffer_take(&out);
}

static int sqlparser_sqlserver_session_span_is_keyword(
	const char *sql,
	size_t start,
	size_t end)
{
	static const char *const keywords[] = {
		"default", "high", "low", "noreset", "null", "off", "on"
	};
	size_t index;
	int has_space;

	for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
		if (sqlparser_sqlserver_ascii_span_equal(
			    sql, start, end, keywords[index])) {
			return 1;
		}
	}
	has_space = 0;
	for (index = start; index < end; index++) {
		if (isspace((unsigned char)sql[index])) {
			has_space = 1;
		} else if (!isalpha((unsigned char)sql[index]) &&
			   sql[index] != '_') {
			return 0;
		}
	}
	return has_space;
}

static sqlparser_status_t sqlparser_sqlserver_session_emit_span_value(
	const char *sql,
	size_t start,
	size_t end,
	size_t item_index,
	const char *name,
	size_t name_length,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;
	char *decoded;
	long long integer_value;
	size_t index;
	int is_binary_literal;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server session value is empty");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name != NULL ? name_length : 0U;
	decoded = sqlparser_sqlserver_session_decode_string(sql, start, end, out_error);
	if (decoded != NULL) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_STRING;
		value.literal.string_value = decoded;
		if (emitter->add_value(
			    emitter->context,
			    item_index,
			    &value,
			    out_error) != SQLPARSER_STATUS_OK) {
			free(decoded);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		free(decoded);
		return SQLPARSER_STATUS_OK;
	}
	if (out_error != NULL &&
	    (out_error->code == SQLPARSER_STATUS_NO_MEMORY ||
	     out_error->code == SQLPARSER_STATUS_RESOURCE_LIMIT)) {
		return out_error->code;
	}
	sqlparser_error_clear(out_error);
	if (sqlparser_sqlserver_session_parse_integer(
		    sql, start, end, &integer_value)) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
		value.literal.integer_value = integer_value;
	} else if ((end - start > 1U &&
	            sql[start] == '@' &&
	            sql[start + 1U] != '@') ||
	           (end - start == 1U && sql[start] == '?')) {
		size_t key_start;

		key_start = sql[start] == '@' ? start + 1U : start;
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_BIND;
		value.bind_kind = sql[start] == '?' ?
			SQLPARSER_BIND_KIND_POSITIONAL : SQLPARSER_BIND_KIND_NAMED;
		value.bind_key = sql + key_start;
		value.bind_key_length = end - key_start;
		value.bind_sql = sql + start;
		value.bind_sql_length = end - start;
		value.source_sql = sql;
		value.source_offset = start;
	} else {
		is_binary_literal =
			end - start > 2U && sql[start] == '0' &&
			(sql[start + 1U] == 'x' || sql[start + 1U] == 'X');
		for (index = start + 2U; is_binary_literal && index < end; index++) {
			is_binary_literal = isxdigit((unsigned char)sql[index]) != 0;
		}
		value.kind = (end - start > 2U &&
			      sql[start] == '@' &&
			      sql[start + 1U] == '@') ||
			     is_binary_literal ?
			SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION :
			(sqlparser_sqlserver_session_span_is_keyword(sql, start, end) ?
				SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD :
				(sqlparser_sqlserver_identifier_token_end(sql, start, end) == end ?
					SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER :
					SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION));
		value.text = sql + start;
		value.text_length = end - start;
	}
	return emitter->add_value(
		emitter->context,
		item_index,
		&value,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_session_emit_text_value(
	const char *sql,
	size_t start,
	size_t end,
	size_t item_index,
	const char *name,
	sqlparser_graph_session_value_kind_t kind,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;

	start = sqlparser_sqlserver_skip_space_bounded(sql, start, end);
	end = sqlparser_sqlserver_trim_right(sql, start, end);
	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name != NULL ? strlen(name) : 0U;
	value.kind = kind;
	value.text = sql + start;
	value.text_length = end - start;
	return emitter->add_value(
		emitter->context,
		item_index,
		&value,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_session_add_item(
	const char *sql,
	size_t name_start,
	size_t name_end,
	sqlparser_graph_session_target_kind_t target_kind,
	const sqlparser_dialect_session_emitter_t *emitter,
	size_t *out_item_index,
	sqlparser_error_t *out_error)
{
	name_start = sqlparser_sqlserver_skip_space_bounded(sql, name_start, name_end);
	name_end = sqlparser_sqlserver_trim_right(sql, name_start, name_end);
	return emitter->add_item(
		emitter->context,
		SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		target_kind,
		name_start < name_end ? sql + name_start : NULL,
		name_start < name_end ? name_end - name_start : 0U,
		out_item_index,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_session_project_set_options(
	const char *sql,
	size_t start,
	size_t end,
	size_t state_start,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t comma;
	size_t item_end;
	size_t item_index;

	while (start < state_start) {
		if (!sqlparser_sqlserver_find_top_level_char(
			    sql, start, state_start, ',', &comma)) {
			comma = state_start;
		}
		item_end = sqlparser_sqlserver_trim_right(sql, start, comma);
		if (sqlparser_sqlserver_session_add_item(
			    sql,
			    start,
			    item_end,
			    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			    emitter,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_session_emit_span_value(
			    sql,
			    state_start,
			    end,
			    item_index,
			    NULL,
			    0U,
			    emitter,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (comma == state_start) {
			break;
		}
		start = sqlparser_sqlserver_skip_space_bounded(sql, comma + 1U, state_start);
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_session_project_set(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t comma;
	size_t item_index;
	size_t name_end;
	size_t name_start;
	size_t scan;
	size_t state_start;
	size_t token_end;
	size_t value_start;

	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_SET,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "transaction")) {
		scan = sqlparser_sqlserver_skip_space_bounded(
			sql, pos + strlen("transaction"), end);
		scan = sqlparser_sqlserver_skip_space_bounded(
			sql, scan + strlen("isolation"), end);
		value_start = sqlparser_sqlserver_skip_space_bounded(
			sql, scan + strlen("level"), end);
		if (emitter->add_item(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			    SQLPARSER_GRAPH_SESSION_TARGET_TRANSACTION,
			    NULL,
			    0U,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return sqlparser_sqlserver_session_emit_text_value(
			sql,
			value_start,
			end,
			item_index,
			"isolation_level",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			emitter,
			out_error);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "identity_insert")) {
		name_start = pos;
		name_end = pos + strlen("identity_insert");
		value_start = sqlparser_sqlserver_skip_space_bounded(sql, name_end, end);
		token_end = sqlparser_sqlserver_multipart_identifier_end(sql, value_start, end);
		state_start = sqlparser_sqlserver_skip_space_bounded(sql, token_end, end);
		if (sqlparser_sqlserver_session_add_item(
			    sql,
			    name_start,
			    name_end,
			    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			    emitter,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_session_emit_text_value(
			    sql,
			    value_start,
			    token_end,
			    item_index,
			    "object",
			    SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			    emitter,
			    out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_sqlserver_session_emit_span_value(
			    sql,
			    state_start,
			    end,
			    item_index,
			    "state",
			    strlen("state"),
			    emitter,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return SQLPARSER_STATUS_OK;
	}

	state_start = end;
	while (state_start > pos && !isspace((unsigned char)sql[state_start - 1U])) {
		state_start--;
	}
	if ((sqlparser_sqlserver_ascii_span_equal(sql, state_start, end, "on") ||
	     sqlparser_sqlserver_ascii_span_equal(sql, state_start, end, "off")) &&
	    sqlparser_sqlserver_find_top_level_char(
		    sql, pos, state_start, ',', &comma)) {
		return sqlparser_sqlserver_session_project_set_options(
			sql, pos, end, state_start, emitter, out_error);
	}

	name_start = pos;
	name_end = sqlparser_sqlserver_identifier_token_end(sql, name_start, end);
	value_start = sqlparser_sqlserver_skip_space_bounded(sql, name_end, end);
	if (sqlparser_sqlserver_word_at_bounded(sql, name_start, end, "statistics")) {
		token_end = sqlparser_sqlserver_identifier_token_end(sql, value_start, end);
		name_end = token_end;
		value_start = sqlparser_sqlserver_skip_space_bounded(sql, token_end, end);
	}
	if (sqlparser_sqlserver_word_at_bounded(sql, name_start, end, "context_info")) {
		if (sqlparser_sqlserver_session_add_item(
			    sql,
			    name_start,
			    name_end,
			    SQLPARSER_GRAPH_SESSION_TARGET_SESSION_CONTEXT,
			    emitter,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	} else if (sqlparser_sqlserver_session_add_item(
			   sql,
			   name_start,
			   name_end,
			   SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
			   emitter,
			   &item_index,
			   out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return sqlparser_sqlserver_session_emit_span_value(
		sql,
		value_start,
		end,
		item_index,
		NULL,
		0U,
		emitter,
		out_error);
}

static size_t sqlparser_sqlserver_session_string_end(
	const char *sql,
	size_t start,
	size_t end)
{
	size_t quote_start;

	quote_start = start;
	if (quote_start + 1U < end &&
	    (sql[quote_start] == 'N' || sql[quote_start] == 'n') &&
	    sql[quote_start + 1U] == '\'') {
		quote_start++;
	}
	return sqlparser_sqlserver_skip_quoted_or_comment_span(sql, quote_start);
}

static sqlparser_status_t sqlparser_sqlserver_session_project_execute_as(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_session_target_kind_t target_kind;
	size_t item_index;
	size_t token_end;
	size_t value_start;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "as")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server execution context is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("as"), end);
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "login")) {
		target_kind = SQLPARSER_GRAPH_SESSION_TARGET_LOGIN;
		pos += strlen("login");
	} else if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "user")) {
		target_kind = SQLPARSER_GRAPH_SESSION_TARGET_USER;
		pos += strlen("user");
	} else {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server execution context target is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (pos >= end || sql[pos] != '=') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server execution context assignment is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	value_start = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
	token_end = sqlparser_sqlserver_session_string_end(sql, value_start, end);
	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_ASSUME,
		    out_error) != SQLPARSER_STATUS_OK ||
	    emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    target_kind,
		    NULL,
		    0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_sqlserver_session_emit_span_value(
		    sql,
		    value_start,
		    token_end,
		    item_index,
		    NULL,
		    0U,
		    emitter,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, token_end, end);
	if (pos >= end) {
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "with")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server execution context option is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("with"), end);
	if (sqlparser_sqlserver_tail_starts_with_two_words(
		    sql, pos, end, "no", "revert")) {
		return sqlparser_sqlserver_session_emit_text_value(
			sql,
			pos,
			end,
			item_index,
			"revert",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			emitter,
			out_error);
	}
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "cookie")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server execution context cookie is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("cookie"), end);
	if (!sqlparser_sqlserver_word_at_bounded(sql, pos, end, "into")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server execution context cookie target is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + strlen("into"), end);
	return sqlparser_sqlserver_session_emit_span_value(
		sql,
		pos,
		end,
		item_index,
		"cookie",
		strlen("cookie"),
		emitter,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_session_project_context(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	static const char *const positional_names[] = {"key", "value", "read_only"};
	const char *value_name;
	size_t comma;
	size_t equals;
	size_t item_end;
	size_t item_index;
	size_t item_start;
	size_t name_end;
	size_t name_start;
	size_t ordinal;
	size_t part_end;
	size_t value_name_length;
	size_t value_start;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (sqlparser_sqlserver_word_at_bounded(sql, pos, end, "sys")) {
		part_end = pos + strlen("sys");
		pos = sqlparser_sqlserver_skip_space_bounded(sql, part_end, end);
		if (pos >= end || sql[pos] != '.') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server session context procedure is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		pos = sqlparser_sqlserver_skip_space_bounded(sql, pos + 1U, end);
	}
	if (!sqlparser_sqlserver_word_at_bounded(
		    sql, pos, end, "sp_set_session_context")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server session context procedure is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	part_end = pos + strlen("sp_set_session_context");
	pos = sqlparser_sqlserver_skip_space_bounded(sql, part_end, end);
	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_SET,
		    out_error) != SQLPARSER_STATUS_OK ||
	    emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_SESSION_CONTEXT,
		    NULL,
		    0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	ordinal = 0U;
	while (pos < end) {
		if (!sqlparser_sqlserver_find_top_level_char(
			    sql, pos, end, ',', &comma)) {
			comma = end;
		}
		item_start = sqlparser_sqlserver_skip_space_bounded(sql, pos, comma);
		item_end = sqlparser_sqlserver_trim_right(sql, item_start, comma);
		name_start = 0U;
		name_end = 0U;
		if (sqlparser_sqlserver_find_top_level_char(
			    sql, item_start, item_end, '=', &equals)) {
			name_start = item_start;
			name_end = sqlparser_sqlserver_trim_right(
				sql, name_start, equals);
			if (name_start < name_end && sql[name_start] == '@') {
				name_start++;
			}
			value_start = sqlparser_sqlserver_skip_space_bounded(
				sql, equals + 1U, item_end);
			value_name = sql + name_start;
			value_name_length = name_end - name_start;
		} else {
			value_start = item_start;
			value_name = ordinal < sizeof(positional_names) / sizeof(positional_names[0]) ?
				positional_names[ordinal] : NULL;
			value_name_length = value_name != NULL ? strlen(value_name) : 0U;
		}
		if (sqlparser_sqlserver_session_emit_span_value(
			    sql,
			    value_start,
			    item_end,
			    item_index,
			    value_name,
			    value_name_length,
			    emitter,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		ordinal++;
		if (comma == end) {
			break;
		}
		pos = comma + 1U;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_session_project_revert(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t equals;
	size_t item_index;

	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_REVERT,
		    out_error) != SQLPARSER_STATUS_OK ||
	    emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_AUTHORIZATION,
		    NULL,
		    0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	if (pos >= end) {
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_sqlserver_find_top_level_char(
		    sql, pos, end, '=', &equals)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "SQL Server REVERT cookie is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, equals + 1U, end);
	return sqlparser_sqlserver_session_emit_span_value(
		sql,
		pos,
		end,
		item_index,
		"cookie",
		strlen("cookie"),
		emitter,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_session_project_setuser(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_graph_session_action_t action;
	size_t item_index;
	size_t token_end;
	size_t value_start;

	pos = sqlparser_sqlserver_skip_space_bounded(sql, pos, end);
	action = pos >= end ?
		SQLPARSER_GRAPH_SESSION_ACTION_REVERT :
		SQLPARSER_GRAPH_SESSION_ACTION_ASSUME;
	if (emitter->set_action(emitter->context, action, out_error) != SQLPARSER_STATUS_OK ||
	    emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_USER,
		    NULL,
		    0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (pos >= end) {
		return SQLPARSER_STATUS_OK;
	}
	value_start = pos;
	token_end = sqlparser_sqlserver_session_string_end(sql, value_start, end);
	if (sqlparser_sqlserver_session_emit_span_value(
		    sql,
		    value_start,
		    token_end,
		    item_index,
		    NULL,
		    0U,
		    emitter,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_sqlserver_skip_space_bounded(sql, token_end, end);
	return pos < end ?
		sqlparser_sqlserver_session_emit_text_value(
			sql,
			pos + strlen("with"),
			end,
			item_index,
			"reset",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			emitter,
			out_error) :
		SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_session_emit_database(
	const PgQuery__VariableSetStmt *set_stmt,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	size_t item_index;
	size_t index;

	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
		    out_error) != SQLPARSER_STATUS_OK ||
	    emitter->add_item(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_DATABASE,
		    NULL,
		    0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	for (index = 0U; index < set_stmt->n_args; index++) {
		if (emitter->add_ast_value(
			    emitter->context,
			    item_index,
			    NULL,
			    set_stmt->args[index],
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_project_session(
	const sqlparser_handle_t *handle,
	const void *state,
	size_t statement_index,
	const PgQuery__Node *statement,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	const PgQuery__VariableSetStmt *set_stmt;
	const char *raw_sql;
	size_t end;
	size_t pos;

	(void)handle;
	(void)state;
	(void)statement_index;
	if (emitter == NULL || emitter->set_action == NULL ||
	    emitter->add_item == NULL || emitter->add_value == NULL ||
	    emitter->add_ast_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session graph emitter is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	set_stmt = sqlparser_sqlserver_session_set_statement(statement);
	if (set_stmt == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (strcmp(set_stmt->name, SQLPARSER_INTERNAL_CURRENT_DATABASE) == 0) {
		return sqlparser_sqlserver_session_emit_database(
			set_stmt, emitter, out_error);
	}

	raw_sql = sqlparser_sqlserver_session_raw_argument(
		set_stmt,
		SQLPARSER_INTERNAL_SQLSERVER_SET_STATEMENT);
	if (raw_sql != NULL) {
		end = strlen(raw_sql);
		pos = sqlparser_sqlserver_skip_space_bounded(raw_sql, 0U, end);
		pos = sqlparser_sqlserver_skip_space_bounded(
			raw_sql, pos + strlen("set"), end);
		return sqlparser_sqlserver_session_project_set(
			raw_sql, pos, end, emitter, out_error);
	}
	raw_sql = sqlparser_sqlserver_session_raw_argument(
		set_stmt,
		SQLPARSER_INTERNAL_SQLSERVER_EXECUTE_STATEMENT);
	if (raw_sql != NULL) {
		end = strlen(raw_sql);
		pos = sqlparser_sqlserver_skip_space_bounded(raw_sql, 0U, end);
		pos += sqlparser_sqlserver_word_at_bounded(
			raw_sql, pos, end, "execute") ?
			strlen("execute") : strlen("exec");
		return sqlparser_sqlserver_word_at_bounded(
			       raw_sql,
			       sqlparser_sqlserver_skip_space_bounded(raw_sql, pos, end),
			       end,
			       "as") ?
			sqlparser_sqlserver_session_project_execute_as(
				raw_sql, pos, end, emitter, out_error) :
			sqlparser_sqlserver_session_project_context(
				raw_sql, pos, end, emitter, out_error);
	}
	raw_sql = sqlparser_sqlserver_session_raw_argument(
		set_stmt,
		SQLPARSER_INTERNAL_SQLSERVER_REVERT_STATEMENT);
	if (raw_sql != NULL) {
		end = strlen(raw_sql);
		pos = sqlparser_sqlserver_skip_space_bounded(raw_sql, 0U, end);
		pos = sqlparser_sqlserver_skip_space_bounded(
			raw_sql, pos + strlen("revert"), end);
		return sqlparser_sqlserver_session_project_revert(
			raw_sql, pos, end, emitter, out_error);
	}
	raw_sql = sqlparser_sqlserver_session_raw_argument(
		set_stmt,
		SQLPARSER_INTERNAL_SQLSERVER_SETUSER_STATEMENT);
	if (raw_sql != NULL) {
		end = strlen(raw_sql);
		pos = sqlparser_sqlserver_skip_space_bounded(raw_sql, 0U, end);
		pos = sqlparser_sqlserver_skip_space_bounded(
			raw_sql, pos + strlen("setuser"), end);
		return sqlparser_sqlserver_session_project_setuser(
			raw_sql, pos, end, emitter, out_error);
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_postprocess_control_unit(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	int is_condition,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_sqlserver_state_t *unit_state;

	unit_state = sqlparser_sqlserver_state_for_statement(
		(const sqlparser_sqlserver_state_t *)state, statement_index, NULL);
	if (unit_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server control statement index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (is_condition) {
		return sqlparser_sqlserver_postprocess_fragment(
			core_sql, unit_state, 0U, out_sql, out_error);
	}
	return sqlparser_sqlserver_postprocess_deparse(
		core_sql, unit_state, out_sql, out_error);
}

static sqlparser_control_state_t *sqlparser_sqlserver_take_control_state(void *state)
{
	sqlparser_sqlserver_state_t *sqlserver_state;
	sqlparser_control_state_t *flow;

	sqlserver_state = (sqlparser_sqlserver_state_t *)state;
	if (sqlserver_state == NULL || sqlserver_state->control == NULL) {
		return NULL;
	}
	flow = sqlserver_state->control->flow;
	sqlserver_state->control->flow = NULL;
	return flow;
}

static const sqlparser_dialect_ops_t SQLPARSER_SQLSERVER_OPS = {
	SQLPARSER_DIALECT_SQLSERVER,
	"sqlserver",
	sqlparser_sqlserver_preprocess,
	sqlparser_sqlserver_preprocess_fragment,
	sqlparser_sqlserver_postprocess_deparse,
	sqlparser_sqlserver_clone_state,
	sqlparser_sqlserver_state_destroy,
	sqlparser_sqlserver_postprocess_literal_fragment,
	NULL,
	NULL,
	NULL,
	NULL,
	sqlparser_sqlserver_postprocess_fragment,
	sqlparser_sqlserver_postprocess_control_unit,
	sqlparser_sqlserver_take_control_state,
	sqlparser_sqlserver_project_session
};

const sqlparser_dialect_ops_t *sqlparser_dialect_sqlserver_ops(void)
{
	return &SQLPARSER_SQLSERVER_OPS;
}
