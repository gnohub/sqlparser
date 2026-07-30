#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
	const char *origin_input;
	size_t origin_input_length;
	size_t origin_pending_offset;
	size_t origin_pending_length;
	sqlparser_identifier_origin_writer_t origin_writer;
} sqlparser_postgresql_buffer_t;

typedef struct {
	char **national_literals;
	size_t *national_literal_ordinals;
	size_t national_literal_count;
	size_t national_literal_capacity;
	size_t national_literal_ordinal_capacity;
	size_t literal_count;
} sqlparser_postgresql_state_t;

static void sqlparser_postgresql_buffer_release(sqlparser_postgresql_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}
	free(buffer->data);
	sqlparser_identifier_origin_writer_release(&buffer->origin_writer);
	memset(buffer, 0, sizeof(*buffer));
}

static sqlparser_status_t sqlparser_postgresql_buffer_begin_origin(
	sqlparser_postgresql_buffer_t *buffer,
	const char *input,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	size_t input_length;

	if (origins == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (buffer == NULL || input == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"PostgreSQL origin input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	input_length = strlen(input);
	status = sqlparser_identifier_origin_writer_begin(
		&buffer->origin_writer,
		origins,
		input_length,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	buffer->origin_input = input;
	buffer->origin_input_length = input_length;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_buffer_flush_origin_input(
	sqlparser_postgresql_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL || buffer->origin_writer.map == NULL ||
	    buffer->origin_pending_length == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_identifier_origin_writer_append_input(
		&buffer->origin_writer,
		buffer->origin_pending_offset,
		buffer->origin_pending_length,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	buffer->origin_pending_offset = 0U;
	buffer->origin_pending_length = 0U;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_buffer_commit_origin(
	sqlparser_postgresql_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL || buffer->origin_writer.map == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_postgresql_buffer_flush_origin_input(
		buffer,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_identifier_origin_writer_commit(
			&buffer->origin_writer,
			buffer->len,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	buffer->origin_input = NULL;
	buffer->origin_input_length = 0U;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_buffer_reserve(
	sqlparser_postgresql_buffer_t *buffer,
	size_t required,
	sqlparser_error_t *out_error)
{
	size_t next_capacity;
	char *next;

	if (buffer == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}
	next_capacity = buffer->capacity == 0U ? 64U : buffer->capacity;
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
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_buffer_reserve_input(
	sqlparser_postgresql_buffer_t *buffer,
	const char *input,
	sqlparser_error_t *out_error)
{
	size_t len;

	if (input == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	len = strlen(input);
	if (len == (size_t)-1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return sqlparser_postgresql_buffer_reserve(buffer, len + 1U, out_error);
}

static sqlparser_status_t sqlparser_postgresql_buffer_append_char(
	sqlparser_postgresql_buffer_t *buffer,
	char value,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (buffer->origin_writer.map != NULL) {
		status = sqlparser_postgresql_buffer_flush_origin_input(
			buffer,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_identifier_origin_writer_append_unknown(
				&buffer->origin_writer,
				1U,
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	status = sqlparser_postgresql_buffer_reserve(buffer, buffer->len + 2U, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	buffer->data[buffer->len++] = value;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_buffer_append_input_char(
	sqlparser_postgresql_buffer_t *buffer,
	const char *input,
	size_t input_offset,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (buffer == NULL || input == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"PostgreSQL input character must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (buffer->origin_writer.map != NULL) {
		if (input != buffer->origin_input ||
		    input_offset >= buffer->origin_input_length) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"PostgreSQL origin input changed during preprocess");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		if (buffer->origin_pending_length == 0U) {
			buffer->origin_pending_offset = input_offset;
			buffer->origin_pending_length = 1U;
		} else if (input_offset ==
			   buffer->origin_pending_offset +
				   buffer->origin_pending_length) {
			buffer->origin_pending_length++;
		} else {
			status = sqlparser_postgresql_buffer_flush_origin_input(
				buffer,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			buffer->origin_pending_offset = input_offset;
			buffer->origin_pending_length = 1U;
		}
	}
	status = sqlparser_postgresql_buffer_reserve(
		buffer,
		buffer->len + 2U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	buffer->data[buffer->len++] = input[input_offset];
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_buffer_append_cstr(
	sqlparser_postgresql_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	size_t len;
	sqlparser_status_t status;

	if (text == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	len = strlen(text);
	if (buffer->origin_writer.map != NULL) {
		status = sqlparser_postgresql_buffer_flush_origin_input(
			buffer,
			out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_identifier_origin_writer_append_unknown(
				&buffer->origin_writer,
				len,
				out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	status = sqlparser_postgresql_buffer_reserve(buffer, buffer->len + len + 1U, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memcpy(buffer->data + buffer->len, text, len);
	buffer->len += len;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_buffer_finish(
	sqlparser_postgresql_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_postgresql_buffer_reserve(buffer, buffer->len + 1U, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static char *sqlparser_postgresql_buffer_take(sqlparser_postgresql_buffer_t *buffer)
{
	char *data;

	if (buffer == NULL) {
		return NULL;
	}
	data = buffer->data;
	memset(buffer, 0, sizeof(*buffer));
	return data;
}

static void sqlparser_postgresql_state_destroy(void *state)
{
	sqlparser_postgresql_state_t *pg_state;
	size_t index;

	if (state == NULL) {
		return;
	}
	pg_state = (sqlparser_postgresql_state_t *)state;
	for (index = 0U; index < pg_state->national_literal_count; index++) {
		free(pg_state->national_literals[index]);
	}
	free(pg_state->national_literals);
	free(pg_state->national_literal_ordinals);
	free(pg_state);
}

static sqlparser_status_t sqlparser_postgresql_state_new(
	sqlparser_postgresql_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_postgresql_state_t *state;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	state = (sqlparser_postgresql_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_size_array_reserve(
	size_t **items,
	size_t *capacity,
	size_t required,
	sqlparser_error_t *out_error)
{
	size_t next_capacity;
	size_t *next;

	if (items == NULL || capacity == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "array arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (required <= *capacity) {
		return SQLPARSER_STATUS_OK;
	}
	next_capacity = *capacity == 0U ? 4U : *capacity;
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

static sqlparser_status_t sqlparser_postgresql_store_national_literal(
	sqlparser_postgresql_state_t *state,
	const char *literal,
	size_t len,
	size_t ordinal,
	sqlparser_error_t *out_error)
{
	char **next;
	char *copy;
	size_t next_capacity;
	sqlparser_status_t status;

	if (state == NULL || literal == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "national literal arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_postgresql_size_array_reserve(
		&state->national_literal_ordinals,
		&state->national_literal_ordinal_capacity,
		state->national_literal_count + 1U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (state->national_literal_count == state->national_literal_capacity) {
		next_capacity = state->national_literal_capacity == 0U ? 4U : state->national_literal_capacity * 2U;
		if (next_capacity < state->national_literal_capacity ||
		    next_capacity > ((size_t)-1) / sizeof(*next)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (char **)realloc(state->national_literals, next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->national_literals = next;
		state->national_literal_capacity = next_capacity;
	}

	copy = sqlparser_strndup(literal, len);
	if (copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->national_literals[state->national_literal_count] = copy;
	state->national_literal_ordinals[state->national_literal_count] = ordinal;
	state->national_literal_count++;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_postgresql_national_literal_matches(
	const sqlparser_postgresql_state_t *state,
	size_t ordinal,
	const char *sql,
	size_t start,
	size_t end)
{
	size_t index;
	size_t len;

	if (state == NULL || sql == NULL || end <= start) {
		return 0;
	}
	len = end - start;
	for (index = 0U; index < state->national_literal_count; index++) {
		if (state->national_literal_ordinals[index] == ordinal &&
		    strlen(state->national_literals[index]) == len &&
		    strncmp(state->national_literals[index], sql + start, len) == 0) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_postgresql_is_ident_char(char c)
{
	return (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') ||
	       c == '_' ||
	       ((unsigned char)c >= 0x80U);
}

static int sqlparser_postgresql_is_n_string_literal(const char *sql)
{
	return sql != NULL &&
	       (sql[0] == 'N' || sql[0] == 'n') &&
	       sql[1] == '\'';
}

static size_t sqlparser_postgresql_quoted_literal_end(const char *sql, size_t start)
{
	size_t index;

	if (sql == NULL || sql[start] != '\'') {
		return start;
	}
	index = start + 1U;
	while (sql[index] != '\0') {
		if (sql[index] == '\'') {
			index++;
			if (sql[index] == '\'') {
				index++;
				continue;
			}
			return index;
		}
		index++;
	}
	return index;
}

static sqlparser_status_t sqlparser_postgresql_copy_single_quoted_literal(
	const char *sql,
	size_t *index,
	sqlparser_postgresql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out == NULL || sql[*index] != '\'') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "single quoted literal is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	status = sqlparser_postgresql_buffer_append_input_char(
		out,
		sql,
		*index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	(*index)++;
	while (sql[*index] != '\0') {
		status = sqlparser_postgresql_buffer_append_input_char(
			out,
			sql,
			*index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (sql[*index] == '\'') {
			(*index)++;
			if (sql[*index] == '\'') {
				status = sqlparser_postgresql_buffer_append_input_char(
					out,
					sql,
					*index,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				(*index)++;
				continue;
			}
			break;
		}
		(*index)++;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_copy_n_string_literal(
	const char *sql,
	size_t *index,
	sqlparser_postgresql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	if (sql == NULL || index == NULL || !sqlparser_postgresql_is_n_string_literal(sql + *index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "national string literal is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	(*index)++;
	return sqlparser_postgresql_copy_single_quoted_literal(sql, index, out, out_error);
}

static int sqlparser_postgresql_copy_double_quoted_identifier(
	const char *sql,
	size_t *index,
	sqlparser_postgresql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out == NULL || sql[*index] != '"') {
		return 0;
	}
	status = sqlparser_postgresql_buffer_append_input_char(
		out,
		sql,
		*index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	(*index)++;
	while (sql[*index] != '\0') {
		status = sqlparser_postgresql_buffer_append_input_char(
			out,
			sql,
			*index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (sql[*index] == '"') {
			(*index)++;
			if (sql[*index] == '"') {
				status = sqlparser_postgresql_buffer_append_input_char(
					out,
					sql,
					*index,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return -1;
				}
				(*index)++;
				continue;
			}
			break;
		}
		(*index)++;
	}
	return 1;
}

static int sqlparser_postgresql_dollar_quote_tag_end(const char *sql, size_t start, size_t *out_end)
{
	size_t index;

	if (sql == NULL || out_end == NULL || sql[start] != '$') {
		return 0;
	}
	index = start + 1U;
	while (sql[index] != '\0' && sql[index] != '$') {
		if (!sqlparser_postgresql_is_ident_char(sql[index])) {
			return 0;
		}
		index++;
	}
	if (sql[index] != '$') {
		return 0;
	}
	*out_end = index + 1U;
	return 1;
}

static int sqlparser_postgresql_copy_dollar_quoted_literal(
	const char *sql,
	size_t *index,
	sqlparser_postgresql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t tag_end;
	size_t tag_len;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out == NULL ||
	    !sqlparser_postgresql_dollar_quote_tag_end(sql, *index, &tag_end)) {
		return 0;
	}
	tag_len = tag_end - *index;
	while (sql[*index] != '\0') {
		status = sqlparser_postgresql_buffer_append_input_char(
			out,
			sql,
			*index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		(*index)++;
		if (*index >= tag_end &&
		    strncmp(sql + *index - tag_len, sql + tag_end - tag_len, tag_len) == 0 &&
		    *index > tag_end) {
			break;
		}
	}
	return 1;
}

static int sqlparser_postgresql_copy_comment(
	const char *sql,
	size_t *index,
	sqlparser_postgresql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out == NULL) {
		return 0;
	}
	if (sql[*index] == '-' && sql[*index + 1U] == '-') {
		while (sql[*index] != '\0') {
			status = sqlparser_postgresql_buffer_append_input_char(
				out,
				sql,
				*index,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[*index] == '\n') {
				(*index)++;
				break;
			}
			(*index)++;
		}
		return 1;
	}
	if (sql[*index] == '/' && sql[*index + 1U] == '*') {
		status = sqlparser_postgresql_buffer_append_input_char(
			out,
			sql,
			*index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		(*index)++;
		while (sql[*index] != '\0') {
			status = sqlparser_postgresql_buffer_append_input_char(
				out,
				sql,
				*index,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[*index] == '*' && sql[*index + 1U] == '/') {
				(*index)++;
				status = sqlparser_postgresql_buffer_append_input_char(
					out,
					sql,
					*index,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return -1;
				}
				(*index)++;
				break;
			}
			(*index)++;
		}
		return 1;
	}
	return 0;
}

static sqlparser_status_t sqlparser_postgresql_preprocess_text(
	const char *input_sql,
	sqlparser_postgresql_state_t *state,
	char **out_parser_sql,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_postgresql_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	if (out_parser_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect preprocess output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_postgresql_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postgresql_buffer_begin_origin(
		&out,
		input_sql,
		origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_postgresql_buffer_release(&out);
		return status;
	}

	index = 0U;
	while (input_sql[index] != '\0') {
		int copied;

		copied = sqlparser_postgresql_copy_comment(input_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_postgresql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		copied = sqlparser_postgresql_copy_dollar_quoted_literal(input_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_postgresql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		copied = sqlparser_postgresql_copy_double_quoted_identifier(input_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_postgresql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if ((index == 0U || !sqlparser_postgresql_is_ident_char(input_sql[index - 1U])) &&
		    sqlparser_postgresql_is_n_string_literal(input_sql + index)) {
			size_t literal_start;

			literal_start = out.len;
			status = sqlparser_postgresql_copy_n_string_literal(input_sql, &index, &out, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_postgresql_store_national_literal(
					state,
					out.data + literal_start,
					out.len - literal_start,
					state->literal_count,
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				state->literal_count++;
			}
		} else if (input_sql[index] == '\'') {
			status = sqlparser_postgresql_copy_single_quoted_literal(input_sql, &index, &out, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				state->literal_count++;
			}
		} else {
			status = sqlparser_postgresql_buffer_append_input_char(
				&out,
				input_sql,
				index,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_postgresql_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_postgresql_buffer_finish(&out, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_postgresql_buffer_commit_origin(
			&out,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_postgresql_buffer_release(&out);
		return status;
	}
	*out_parser_sql = sqlparser_postgresql_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_preprocess_internal(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	sqlparser_postgresql_state_t *state;
	sqlparser_status_t status;

	(void)limits;

	if (out_parser_sql == NULL || out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect preprocess output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_state = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_postgresql_state_new(&state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_postgresql_preprocess_text(
		input_sql,
		state,
		out_parser_sql,
		origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_postgresql_state_destroy(state);
		return status;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	return sqlparser_postgresql_preprocess_internal(
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		NULL,
		out_error);
}

sqlparser_status_t sqlparser_postgresql_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	if (out_parser_sql != NULL) {
		*out_parser_sql = NULL;
	}
	if (out_state != NULL) {
		*out_state = NULL;
	}
	if (origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin map must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_postgresql_preprocess_internal(
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
}

static sqlparser_status_t sqlparser_postgresql_preprocess_fragment(
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	(void)statement_index;
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "PostgreSQL dialect state is missing");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	return sqlparser_postgresql_preprocess_text(
		input_sql,
		(sqlparser_postgresql_state_t *)state,
		out_parser_sql,
		NULL,
		out_error);
}

static sqlparser_status_t sqlparser_postgresql_postprocess_national_literals(
	const char *sql,
	const sqlparser_postgresql_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_postgresql_buffer_t out;
	sqlparser_status_t status;
	size_t index;
	size_t literal_ordinal;

	memset(&out, 0, sizeof(out));
	status = sqlparser_postgresql_buffer_reserve_input(&out, sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	index = 0U;
	literal_ordinal = 0U;
	while (sql[index] != '\0') {
		int copied;

		copied = sqlparser_postgresql_copy_comment(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_postgresql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		copied = sqlparser_postgresql_copy_dollar_quoted_literal(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_postgresql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		copied = sqlparser_postgresql_copy_double_quoted_identifier(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_postgresql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if (sql[index] == '\'') {
			size_t literal_end;

			literal_end = sqlparser_postgresql_quoted_literal_end(sql, index);
			if (literal_end > index &&
			    sqlparser_postgresql_national_literal_matches(state, literal_ordinal, sql, index, literal_end)) {
				status = sqlparser_postgresql_buffer_append_char(&out, 'N', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_postgresql_buffer_release(&out);
					return status;
				}
			}
			status = sqlparser_postgresql_copy_single_quoted_literal(sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_postgresql_buffer_release(&out);
				return status;
			}
			literal_ordinal++;
		} else {
			status = sqlparser_postgresql_buffer_append_input_char(
				&out,
				sql,
				index,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_postgresql_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_postgresql_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_postgresql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_postgresql_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect deparse output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_postgresql_postprocess_national_literals(
		core_sql,
		(const sqlparser_postgresql_state_t *)state,
		out_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dialect_rewrite_like_escape(out_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_sql);
		*out_sql = NULL;
		return status;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_postgresql_state_t *pg_state;
	size_t literal_end;
	sqlparser_postgresql_buffer_t out;
	sqlparser_status_t status;

	(void)statement_index;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "core SQL must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	pg_state = (const sqlparser_postgresql_state_t *)state;
	literal_end = core_sql[0] == '\'' ? sqlparser_postgresql_quoted_literal_end(core_sql, 0U) : 0U;
	if (literal_end > 0U &&
	    core_sql[literal_end] == '\0' &&
	    sqlparser_postgresql_national_literal_matches(pg_state, literal_index, core_sql, 0U, literal_end)) {
		memset(&out, 0, sizeof(out));
		status = sqlparser_postgresql_buffer_append_char(&out, 'N', out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_postgresql_buffer_append_cstr(&out, core_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_postgresql_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_postgresql_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_postgresql_buffer_take(&out);
		return SQLPARSER_STATUS_OK;
	}

	*out_sql = sqlparser_strdup(core_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_postgresql_clone_state(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_postgresql_state_t *source;
	sqlparser_postgresql_state_t *clone;
	sqlparser_status_t status;
	size_t index;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	source = (const sqlparser_postgresql_state_t *)state;
	status = sqlparser_postgresql_state_new(&clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clone->literal_count = source->literal_count;
	for (index = 0U; index < source->national_literal_count; index++) {
		status = sqlparser_postgresql_store_national_literal(
			clone,
			source->national_literals[index],
			strlen(source->national_literals[index]),
			source->national_literal_ordinals[index],
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_postgresql_state_destroy(clone);
			return status;
		}
	}

	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_dialect_ops_t SQLPARSER_POSTGRESQL_OPS = {
	SQLPARSER_DIALECT_POSTGRESQL,
	"postgresql",
	sqlparser_postgresql_preprocess,
	sqlparser_postgresql_preprocess_fragment,
	sqlparser_postgresql_postprocess_deparse,
	sqlparser_postgresql_clone_state,
	sqlparser_postgresql_state_destroy,
	sqlparser_postgresql_postprocess_literal_fragment,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

const sqlparser_dialect_ops_t *sqlparser_dialect_postgresql_ops(void)
{
	return &SQLPARSER_POSTGRESQL_OPS;
}
