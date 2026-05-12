#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"

typedef struct {
	char **bind_names;
	size_t bind_count;
	size_t bind_capacity;
	int saw_minus;
} sqlparser_dameng_state_t;

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
} sqlparser_dameng_buffer_t;

static void sqlparser_dameng_buffer_release(sqlparser_dameng_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}
	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
}

static sqlparser_status_t sqlparser_dameng_buffer_reserve(
	sqlparser_dameng_buffer_t *buffer,
	size_t extra,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t required;
	size_t next_capacity;

	if (buffer == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "buffer must not be NULL");
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

static sqlparser_status_t sqlparser_dameng_buffer_append_mem(
	sqlparser_dameng_buffer_t *buffer,
	const char *data,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (len == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (data == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "append data must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	status = sqlparser_dameng_buffer_reserve(buffer, len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_buffer_append_char(
	sqlparser_dameng_buffer_t *buffer,
	char value,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_buffer_append_mem(buffer, &value, 1U, out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_append_cstr(
	sqlparser_dameng_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_buffer_append_mem(buffer, text, text != NULL ? strlen(text) : 0U, out_error);
}

static sqlparser_status_t sqlparser_dameng_buffer_finish(
	sqlparser_dameng_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	return sqlparser_dameng_buffer_reserve(buffer, 0U, out_error);
}

static char *sqlparser_dameng_buffer_take(sqlparser_dameng_buffer_t *buffer)
{
	char *data;

	if (buffer == NULL) {
		return NULL;
	}
	data = buffer->data;
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
	return data;
}

static sqlparser_status_t sqlparser_dameng_buffer_reserve_input(
	sqlparser_dameng_buffer_t *buffer,
	const char *input,
	sqlparser_error_t *out_error)
{
	size_t len;

	len = input != NULL ? strlen(input) : 0U;
	return sqlparser_dameng_buffer_reserve(buffer, len, out_error);
}

static void sqlparser_dameng_state_destroy(void *state)
{
	sqlparser_dameng_state_t *dameng_state;
	size_t index;

	dameng_state = (sqlparser_dameng_state_t *)state;
	if (dameng_state == NULL) {
		return;
	}
	for (index = 0U; index < dameng_state->bind_count; index++) {
		free(dameng_state->bind_names[index]);
	}
	free(dameng_state->bind_names);
	free(dameng_state);
}

static sqlparser_status_t sqlparser_dameng_state_new(
	sqlparser_dameng_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_state_t *state;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	state = (sqlparser_dameng_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_is_ident_start(unsigned char c)
{
	return isalpha(c) || c == '_';
}

static int sqlparser_dameng_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '$' || c == '#';
}

static int sqlparser_dameng_is_word_boundary(const char *text, size_t pos, size_t len)
{
	unsigned char prev;
	unsigned char next;

	prev = pos == 0U ? 0U : (unsigned char)text[pos - 1U];
	next = text[pos + len] == '\0' ? 0U : (unsigned char)text[pos + len];
	return !sqlparser_dameng_is_ident_char(prev) && !sqlparser_dameng_is_ident_char(next);
}

static int sqlparser_dameng_ascii_word_equal(const char *text, size_t pos, const char *word)
{
	size_t index;
	size_t len;

	if (text == NULL || word == NULL) {
		return 0;
	}
	len = strlen(word);
	for (index = 0U; index < len; index++) {
		if (text[pos + index] == '\0' ||
		    tolower((unsigned char)text[pos + index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return sqlparser_dameng_is_word_boundary(text, pos, len);
}

static size_t sqlparser_dameng_skip_space(const char *text, size_t pos)
{
	while (text[pos] != '\0' && isspace((unsigned char)text[pos])) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_dameng_trim_right(const char *text, size_t start, size_t end)
{
	while (end > start && isspace((unsigned char)text[end - 1U])) {
		end--;
	}
	return end;
}

static size_t sqlparser_dameng_q_quote_prefix_len(const char *text)
{
	if (text == NULL) {
		return 0U;
	}
	if ((text[0] == 'q' || text[0] == 'Q') && text[1] == '\'') {
		return 1U;
	}
	if ((text[0] == 'n' || text[0] == 'N') &&
	    (text[1] == 'q' || text[1] == 'Q') &&
	    text[2] == '\'') {
		return 2U;
	}
	return 0U;
}

static char sqlparser_dameng_q_quote_close_char(char open_char)
{
	switch (open_char) {
		case '[':
			return ']';
		case '{':
			return '}';
		case '(':
			return ')';
		case '<':
			return '>';
		default:
			return open_char;
	}
}

static int sqlparser_dameng_copy_q_quote(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t prefix_len;
	size_t pos;
	char close_char;

	prefix_len = sqlparser_dameng_q_quote_prefix_len(input + *index);
	if (prefix_len == 0U) {
		return 0;
	}
	if (prefix_len == 2U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported Dameng syntax: national q-quoted string literal");
		return -1;
	}
	pos = *index;
	if (sqlparser_dameng_buffer_append_char(out, '\'', out_error) != SQLPARSER_STATUS_OK) {
		return -1;
	}
	pos += prefix_len + 2U;
	close_char = sqlparser_dameng_q_quote_close_char(input[pos - 1U]);
	while (input[pos] != '\0') {
		if (input[pos] == close_char && input[pos + 1U] == '\'') {
			if (sqlparser_dameng_buffer_append_char(out, '\'', out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			*index = pos + 2U;
			return 1;
		}
		if (input[pos] == '\'' &&
		    sqlparser_dameng_buffer_append_char(out, '\'', out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (sqlparser_dameng_buffer_append_char(out, input[pos], out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		pos++;
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated Dameng q-quoted string literal");
	return -1;
}

static int sqlparser_dameng_copy_quoted_or_comment(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;

	if (input == NULL || index == NULL || out == NULL) {
		return 0;
	}

	if (input[*index] == '-' && input[*index + 1U] == '-') {
		while (input[*index] != '\0') {
			if (sqlparser_dameng_buffer_append_char(out, input[*index], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (input[*index] == '\n') {
				(*index)++;
				break;
			}
			(*index)++;
		}
		return 1;
	}

	if (input[*index] == '/' && input[*index + 1U] == '*') {
		if (sqlparser_dameng_buffer_append_cstr(out, "/*", out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		*index += 2U;
		while (input[*index] != '\0') {
			if (input[*index] == '*' && input[*index + 1U] == '/') {
				if (sqlparser_dameng_buffer_append_cstr(out, "*/", out_error) != SQLPARSER_STATUS_OK) {
					return -1;
				}
				*index += 2U;
				return 1;
			}
			if (sqlparser_dameng_buffer_append_char(out, input[*index], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			(*index)++;
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated Dameng block comment");
		return -1;
	}

	if (sqlparser_dameng_q_quote_prefix_len(input + *index) > 0U) {
		return sqlparser_dameng_copy_q_quote(input, index, out, out_error);
	}

	if (input[*index] == 'N' || input[*index] == 'n') {
		if (input[*index + 1U] != '\'') {
			return 0;
		}
		if (sqlparser_dameng_buffer_append_char(out, input[*index], out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		(*index)++;
	}

	if (input[*index] != '\'' && input[*index] != '"') {
		return 0;
	}

	quote = input[*index];
	pos = *index;
	if (sqlparser_dameng_buffer_append_char(out, input[pos], out_error) != SQLPARSER_STATUS_OK) {
		return -1;
	}
	pos++;
	while (input[pos] != '\0') {
		if (sqlparser_dameng_buffer_append_char(out, input[pos], out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (input[pos] == quote) {
			if (input[pos + 1U] == quote) {
				pos++;
				if (sqlparser_dameng_buffer_append_char(out, input[pos], out_error) != SQLPARSER_STATUS_OK) {
					return -1;
				}
			} else {
				pos++;
				*index = pos;
				return 1;
			}
		}
		pos++;
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated Dameng quoted literal");
	return -1;
}

static sqlparser_status_t sqlparser_dameng_state_find_or_add_bind(
	sqlparser_dameng_state_t *state,
	const char *name,
	size_t len,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	char **next;
	char *name_copy;
	size_t index;
	size_t next_capacity;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind state arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	for (index = 0U; index < state->bind_count; index++) {
		if (strlen(state->bind_names[index]) == len &&
		    strncmp(state->bind_names[index], name, len) == 0) {
			*out_param_index = index + 1U;
			return SQLPARSER_STATUS_OK;
		}
	}

	if (state->bind_count == state->bind_capacity) {
		next_capacity = state->bind_capacity == 0U ? 8U : state->bind_capacity * 2U;
		if (next_capacity < state->bind_capacity) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (char **)realloc(state->bind_names, next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->bind_names = next;
		state->bind_capacity = next_capacity;
	}

	name_copy = sqlparser_strndup(name, len);
	if (name_copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->bind_names[state->bind_count] = name_copy;
	state->bind_count++;
	*out_param_index = state->bind_count;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_state_append_bind(
	sqlparser_dameng_state_t *state,
	const char *name,
	size_t len,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	char **next;
	char *name_copy;
	size_t next_capacity;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind state arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	if (state->bind_count == state->bind_capacity) {
		next_capacity = state->bind_capacity == 0U ? 8U : state->bind_capacity * 2U;
		if (next_capacity < state->bind_capacity) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next = (char **)realloc(state->bind_names, next_capacity * sizeof(*next));
		if (next == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->bind_names = next;
		state->bind_capacity = next_capacity;
	}

	name_copy = sqlparser_strndup(name, len);
	if (name_copy == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	state->bind_names[state->bind_count] = name_copy;
	state->bind_count++;
	*out_param_index = state->bind_count;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_append_pg_param(
	sqlparser_dameng_buffer_t *out,
	size_t param_index,
	sqlparser_error_t *out_error)
{
	char text[32];

	(void)snprintf(text, sizeof(text), "$%lu", (unsigned long)param_index);
	return sqlparser_dameng_buffer_append_cstr(out, text, out_error);
}

static sqlparser_status_t sqlparser_dameng_copy_bind_placeholder(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_dameng_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t start;
	size_t end;
	size_t param_index;
	sqlparser_status_t status;

	start = *index;
	end = start + 1U;
	if (isdigit((unsigned char)input[end])) {
		while (isdigit((unsigned char)input[end])) {
			end++;
		}
	} else if (sqlparser_dameng_is_ident_start((unsigned char)input[end])) {
		end++;
		while (sqlparser_dameng_is_ident_char((unsigned char)input[end])) {
			end++;
		}
		while (input[end] == '.' &&
		       sqlparser_dameng_is_ident_start((unsigned char)input[end + 1U])) {
			end += 2U;
			while (sqlparser_dameng_is_ident_char((unsigned char)input[end])) {
				end++;
			}
		}
	} else {
		return sqlparser_dameng_buffer_append_char(out, input[(*index)++], out_error);
	}

	status = sqlparser_dameng_state_find_or_add_bind(state, input + start, end - start, &param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_append_pg_param(out, param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_copy_question_placeholder(
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_dameng_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t param_index;
	sqlparser_status_t status;

	status = sqlparser_dameng_state_append_bind(state, "?", 1U, &param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_append_pg_param(out, param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	(*index)++;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_dameng_parse_unsigned_token(
	const char *input,
	size_t *pos,
	size_t *out_start,
	size_t *out_end)
{
	size_t start;

	*pos = sqlparser_dameng_skip_space(input, *pos);
	start = *pos;
	while (isdigit((unsigned char)input[*pos])) {
		(*pos)++;
	}
	if (*pos == start) {
		return 0;
	}
	*out_start = start;
	*out_end = *pos;
	return 1;
}

static sqlparser_status_t sqlparser_dameng_append_limit_clause(
	sqlparser_dameng_buffer_t *out,
	const char *input,
	size_t first_start,
	size_t first_end,
	size_t second_start,
	size_t second_end,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_dameng_buffer_append_cstr(out, " LIMIT ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_mem(out, input + second_start, second_end - second_start, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && first_end > first_start) {
		status = sqlparser_dameng_buffer_append_cstr(out, " OFFSET ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK && first_end > first_start) {
		status = sqlparser_dameng_buffer_append_mem(out, input + first_start, first_end - first_start, out_error);
	}
	return status;
}

static sqlparser_status_t sqlparser_dameng_try_rewrite_limit_comma(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t first_start;
	size_t first_end;
	size_t second_start;
	size_t second_end;
	size_t after_first;

	if (!sqlparser_dameng_ascii_word_equal(input, *index, "limit")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = *index + strlen("limit");
	if (!sqlparser_dameng_parse_unsigned_token(input, &pos, &first_start, &first_end)) {
		return SQLPARSER_STATUS_OK;
	}
	after_first = sqlparser_dameng_skip_space(input, pos);
	if (input[after_first] != ',') {
		return SQLPARSER_STATUS_OK;
	}
	pos = after_first + 1U;
	if (!sqlparser_dameng_parse_unsigned_token(input, &pos, &second_start, &second_end)) {
		return SQLPARSER_STATUS_OK;
	}
	*index = pos;
	return sqlparser_dameng_append_limit_clause(out, input, first_start, first_end, second_start, second_end, out_error);
}

static sqlparser_status_t sqlparser_dameng_parse_top_clause(
	const char *input,
	size_t top_pos,
	size_t *out_pos,
	char **out_limit,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t limit;
	size_t pos;
	size_t first_start;
	size_t first_end;
	size_t second_start;
	size_t second_end;
	size_t after_first;
	sqlparser_status_t status;

	memset(&limit, 0, sizeof(limit));
	*out_limit = NULL;
	pos = top_pos;
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "top")) {
		return SQLPARSER_STATUS_OK;
	}
	pos += strlen("top");
	if (!sqlparser_dameng_parse_unsigned_token(input, &pos, &first_start, &first_end)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: TOP expression");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	after_first = sqlparser_dameng_skip_space(input, pos);
	second_start = 0U;
	second_end = 0U;
	if (input[after_first] == ',') {
		pos = after_first + 1U;
		if (!sqlparser_dameng_parse_unsigned_token(input, &pos, &second_start, &second_end)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: TOP offset");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	} else {
		second_start = first_start;
		second_end = first_end;
		first_start = 0U;
		first_end = 0U;
		pos = after_first;
	}

	if (sqlparser_dameng_ascii_word_equal(input, pos, "percent")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: TOP PERCENT");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_dameng_ascii_word_equal(input, pos, "with")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: TOP WITH TIES");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	status = sqlparser_dameng_append_limit_clause(
		&limit,
		input,
		first_start,
		first_end,
		second_start,
		second_end,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&limit);
		return status;
	}
	status = sqlparser_dameng_buffer_finish(&limit, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&limit);
		return status;
	}
	*out_limit = sqlparser_dameng_buffer_take(&limit);
	if (*out_limit == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_pos = pos;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_append_pending_limit(
	sqlparser_dameng_buffer_t *out,
	char **pending_limit,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (pending_limit == NULL || *pending_limit == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dameng_buffer_append_cstr(out, *pending_limit, out_error);
	free(*pending_limit);
	*pending_limit = NULL;
	return status;
}

static sqlparser_status_t sqlparser_dameng_try_rewrite_set_schema(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	int *out_rewritten,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t value_start;
	size_t value_end;
	sqlparser_status_t status;

	if (out_rewritten != NULL) {
		*out_rewritten = 0;
	}
	if (!sqlparser_dameng_ascii_word_equal(input, *index, "set")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, *index + strlen("set"));
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "schema")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, pos + strlen("schema"));
	value_start = pos;
	while (input[pos] != '\0' && input[pos] != ';') {
		pos++;
	}
	value_end = sqlparser_dameng_trim_right(input, value_start, pos);
	if (value_start >= value_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "SET SCHEMA requires a value");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	status = sqlparser_dameng_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, SQLPARSER_INTERNAL_CURRENT_SCHEMA, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_mem(out, input + value_start, value_end - value_start, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = pos;
	if (out_rewritten != NULL) {
		*out_rewritten = 1;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_try_rewrite_alter_session(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	int *out_rewritten,
	sqlparser_error_t *out_error)
{
	size_t pos;
	size_t param_start;
	size_t param_end;
	size_t value_start;
	size_t value_end;
	sqlparser_status_t status;

	if (out_rewritten != NULL) {
		*out_rewritten = 0;
	}
	if (!sqlparser_dameng_ascii_word_equal(input, *index, "alter")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, *index + strlen("alter"));
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "session")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, pos + strlen("session"));
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "set")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, pos + strlen("set"));
	param_start = pos;
	while (sqlparser_dameng_is_ident_char((unsigned char)input[pos])) {
		pos++;
	}
	param_end = pos;
	if (param_start == param_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET requires a parameter name");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (param_end - param_start != strlen("current_schema") ||
	    !sqlparser_dameng_ascii_word_equal(input, param_start, "current_schema")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: alter session parameter");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	pos = sqlparser_dameng_skip_space(input, pos);
	if (input[pos] != '=') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET CURRENT_SCHEMA requires '='");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_dameng_skip_space(input, pos + 1U);
	value_start = pos;
	while (input[pos] != '\0' && input[pos] != ';') {
		pos++;
	}
	value_end = sqlparser_dameng_trim_right(input, value_start, pos);
	if (value_start >= value_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET CURRENT_SCHEMA requires a value");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	status = sqlparser_dameng_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, SQLPARSER_INTERNAL_CURRENT_SCHEMA, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_mem(out, input + value_start, value_end - value_start, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = pos;
	if (out_rewritten != NULL) {
		*out_rewritten = 1;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_append_internal_string_literal(
	sqlparser_dameng_buffer_t *out,
	const char *input,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_dameng_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (input[pos] == '\'') {
			status = sqlparser_dameng_buffer_append_cstr(out, "''", out_error);
		} else {
			status = sqlparser_dameng_buffer_append_char(out, input[pos], out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_dameng_buffer_append_char(out, '\'', out_error);
}

static size_t sqlparser_dameng_statement_token_end(const char *input, size_t pos)
{
	while (input[pos] != '\0' && input[pos] != ';' && !isspace((unsigned char)input[pos])) {
		pos++;
	}
	return pos;
}

static sqlparser_status_t sqlparser_dameng_try_rewrite_exec_sql_prepared(
	const char *input,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	int *out_rewritten,
	sqlparser_error_t *out_error)
{
	const char *internal_name;
	size_t pos;
	size_t name_start;
	size_t name_end;
	size_t value_start;
	size_t value_end;
	sqlparser_status_t status;

	if (out_rewritten != NULL) {
		*out_rewritten = 0;
	}
	if (!sqlparser_dameng_ascii_word_equal(input, *index, "exec")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, *index + strlen("exec"));
	if (!sqlparser_dameng_ascii_word_equal(input, pos, "sql")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_dameng_skip_space(input, pos + strlen("sql"));
	internal_name = NULL;
	value_start = 0U;
	value_end = 0U;
	if (sqlparser_dameng_ascii_word_equal(input, pos, "prepare")) {
		pos = sqlparser_dameng_skip_space(input, pos + strlen("prepare"));
		name_start = pos;
		name_end = sqlparser_dameng_statement_token_end(input, pos);
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL PREPARE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_dameng_skip_space(input, name_end);
		if (!sqlparser_dameng_ascii_word_equal(input, pos, "from")) {
			return SQLPARSER_STATUS_OK;
		}
		value_start = sqlparser_dameng_skip_space(input, pos + strlen("from"));
		pos = value_start;
		while (input[pos] != '\0' && input[pos] != ';') {
			pos++;
		}
		value_end = sqlparser_dameng_trim_right(input, value_start, pos);
		if (value_start >= value_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL PREPARE FROM requires SQL text");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		internal_name = SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE;
	} else if (sqlparser_dameng_ascii_word_equal(input, pos, "execute")) {
		pos = sqlparser_dameng_skip_space(input, pos + strlen("execute"));
		name_start = pos;
		name_end = sqlparser_dameng_statement_token_end(input, pos);
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL EXECUTE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_dameng_skip_space(input, name_end);
		if (sqlparser_dameng_ascii_word_equal(input, pos, "using")) {
			value_start = sqlparser_dameng_skip_space(input, pos + strlen("using"));
			pos = value_start;
			while (input[pos] != '\0' && input[pos] != ';') {
				pos++;
			}
			value_end = sqlparser_dameng_trim_right(input, value_start, pos);
			if (value_start >= value_end) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL EXECUTE USING requires parameters");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
		} else {
			while (input[pos] != '\0' && input[pos] != ';') {
				if (!isspace((unsigned char)input[pos])) {
					return SQLPARSER_STATUS_OK;
				}
				pos++;
			}
		}
		internal_name = SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE;
	} else if (sqlparser_dameng_ascii_word_equal(input, pos, "deallocate")) {
		pos = sqlparser_dameng_skip_space(input, pos + strlen("deallocate"));
		if (!sqlparser_dameng_ascii_word_equal(input, pos, "prepare")) {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_dameng_skip_space(input, pos + strlen("prepare"));
		name_start = pos;
		name_end = sqlparser_dameng_statement_token_end(input, pos);
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXEC SQL DEALLOCATE PREPARE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_dameng_skip_space(input, name_end);
		while (input[pos] != '\0' && input[pos] != ';') {
			if (!isspace((unsigned char)input[pos])) {
				return SQLPARSER_STATUS_OK;
			}
			pos++;
		}
		internal_name = SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE;
	} else {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_dameng_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, internal_name, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_cstr(out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_append_internal_string_literal(out, input, name_start, name_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && value_start < value_end) {
		status = sqlparser_dameng_buffer_append_cstr(out, ", ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_append_internal_string_literal(out, input, value_start, value_end, out_error);
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = pos;
	if (out_rewritten != NULL) {
		*out_rewritten = 1;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_dameng_skip_ident_word(const char *input, size_t pos)
{
	while (sqlparser_dameng_is_ident_char((unsigned char)input[pos])) {
		pos++;
	}
	return pos;
}

static int sqlparser_dameng_next_word_is(const char *input, size_t pos, const char *word)
{
	pos = sqlparser_dameng_skip_space(input, sqlparser_dameng_skip_ident_word(input, pos));
	return sqlparser_dameng_ascii_word_equal(input, pos, word);
}

static int sqlparser_dameng_create_targets_procedure(const char *input, size_t pos)
{
	pos = sqlparser_dameng_skip_space(input, sqlparser_dameng_skip_ident_word(input, pos));
	if (sqlparser_dameng_ascii_word_equal(input, pos, "or")) {
		pos = sqlparser_dameng_skip_space(input, pos + strlen("or"));
		if (!sqlparser_dameng_ascii_word_equal(input, pos, "replace")) {
			return 0;
		}
		pos = sqlparser_dameng_skip_space(input, pos + strlen("replace"));
	}
	return sqlparser_dameng_ascii_word_equal(input, pos, "procedure");
}

static sqlparser_status_t sqlparser_dameng_reject_unsupported_at(
	const char *input,
	size_t index,
	sqlparser_error_t *out_error)
{
	if (input[index] == '@') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: database link");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_dameng_ascii_word_equal(input, index, "begin")) {
		size_t next;

		next = sqlparser_dameng_skip_space(input, index + strlen("begin"));
		if (input[next] != '\0' && input[next] != ';' &&
		    !sqlparser_dameng_ascii_word_equal(input, next, "work") &&
		    !sqlparser_dameng_ascii_word_equal(input, next, "transaction")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}
	if (sqlparser_dameng_ascii_word_equal(input, index, "connect") ||
	    sqlparser_dameng_ascii_word_equal(input, index, "pivot") ||
	    sqlparser_dameng_ascii_word_equal(input, index, "procedure") ||
	    sqlparser_dameng_ascii_word_equal(input, index, "returning")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_dameng_ascii_word_equal(input, index, "insert") &&
	    sqlparser_dameng_next_word_is(input, index, "all")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: INSERT ALL");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_dameng_ascii_word_equal(input, index, "create") &&
	    sqlparser_dameng_create_targets_procedure(input, index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: CREATE PROCEDURE");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_preprocess_text(
	const char *input_sql,
	sqlparser_dameng_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	char *pending_limit;
	size_t pending_limit_depth;
	size_t index;
	size_t paren_depth;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "Dameng dialect state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&out, 0, sizeof(out));
	pending_limit = NULL;
	pending_limit_depth = 0U;
	index = 0U;
	paren_depth = 0U;
	while (input_sql[index] != '\0') {
		int copied;

		copied = sqlparser_dameng_copy_quoted_or_comment(input_sql, &index, &out, out_error);
		if (copied < 0) {
			free(pending_limit);
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
		}
		if (copied > 0) {
			continue;
		}

		status = sqlparser_dameng_reject_unsupported_at(input_sql, index, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(pending_limit);
			sqlparser_dameng_buffer_release(&out);
			return status;
		}

		if (input_sql[index] == '(') {
			paren_depth++;
			status = sqlparser_dameng_buffer_append_char(&out, input_sql[index++], out_error);
		} else if (input_sql[index] == ')') {
			if (pending_limit != NULL && paren_depth == pending_limit_depth) {
				status = sqlparser_dameng_append_pending_limit(&out, &pending_limit, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_dameng_buffer_release(&out);
					return status;
				}
			}
			if (paren_depth > 0U) {
				paren_depth--;
			}
			status = sqlparser_dameng_buffer_append_char(&out, input_sql[index++], out_error);
		} else if (input_sql[index] == ';') {
			status = sqlparser_dameng_append_pending_limit(&out, &pending_limit, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_char(&out, input_sql[index++], out_error);
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "set")) {
			int rewritten;

			rewritten = 0;
			status = sqlparser_dameng_try_rewrite_set_schema(input_sql, &index, &out, &rewritten, out_error);
			if (status == SQLPARSER_STATUS_OK && !rewritten) {
				status = sqlparser_dameng_buffer_append_char(&out, input_sql[index++], out_error);
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "alter")) {
			int rewritten;

			rewritten = 0;
			status = sqlparser_dameng_try_rewrite_alter_session(input_sql, &index, &out, &rewritten, out_error);
			if (status == SQLPARSER_STATUS_OK && !rewritten) {
				status = sqlparser_dameng_buffer_append_char(&out, input_sql[index++], out_error);
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "exec")) {
			int rewritten;

			rewritten = 0;
			status = sqlparser_dameng_try_rewrite_exec_sql_prepared(input_sql, &index, &out, &rewritten, out_error);
			if (status == SQLPARSER_STATUS_OK && !rewritten) {
				status = sqlparser_dameng_buffer_append_char(&out, input_sql[index++], out_error);
			}
		} else if (input_sql[index] == '?') {
			status = sqlparser_dameng_copy_question_placeholder(&index, &out, state, out_error);
		} else if (input_sql[index] == ':' && input_sql[index + 1U] != '=' &&
		           input_sql[index + 1U] != ':' && input_sql[index + 1U] != '\0') {
			status = sqlparser_dameng_copy_bind_placeholder(input_sql, &index, &out, state, out_error);
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "minus")) {
			state->saw_minus = 1;
			status = sqlparser_dameng_buffer_append_cstr(&out, "EXCEPT", out_error);
			index += strlen("minus");
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "limit")) {
			size_t before;

			before = index;
			status = sqlparser_dameng_try_rewrite_limit_comma(input_sql, &index, &out, out_error);
			if (status == SQLPARSER_STATUS_OK && index == before) {
				status = sqlparser_dameng_buffer_append_char(&out, input_sql[index++], out_error);
			}
		} else if (sqlparser_dameng_ascii_word_equal(input_sql, index, "select")) {
			size_t after_select;
			size_t top_pos;

			status = sqlparser_dameng_buffer_append_mem(&out, input_sql + index, strlen("select"), out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(pending_limit);
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			after_select = index + strlen("select");
			top_pos = sqlparser_dameng_skip_space(input_sql, after_select);
			if (sqlparser_dameng_ascii_word_equal(input_sql, top_pos, "top")) {
				char *top_limit;
				size_t next_pos;

				if (pending_limit != NULL) {
					free(pending_limit);
					sqlparser_dameng_buffer_release(&out);
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "unsupported Dameng syntax: nested TOP");
					return SQLPARSER_STATUS_UNSUPPORTED;
				}
				if (sqlparser_dameng_buffer_append_mem(&out, input_sql + after_select, top_pos - after_select, out_error) !=
				    SQLPARSER_STATUS_OK) {
					sqlparser_dameng_buffer_release(&out);
					return SQLPARSER_STATUS_NO_MEMORY;
				}
				top_limit = NULL;
				next_pos = top_pos;
				status = sqlparser_dameng_parse_top_clause(input_sql, top_pos, &next_pos, &top_limit, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(top_limit);
					sqlparser_dameng_buffer_release(&out);
					return status;
				}
				pending_limit = top_limit;
				pending_limit_depth = paren_depth;
				index = sqlparser_dameng_skip_space(input_sql, next_pos);
			} else {
				index = after_select;
			}
		} else {
			status = sqlparser_dameng_buffer_append_char(&out, input_sql[index++], out_error);
		}

		if (status != SQLPARSER_STATUS_OK) {
			free(pending_limit);
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_dameng_append_pending_limit(&out, &pending_limit, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_dameng_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_state_t *state;
	sqlparser_status_t status;

	(void)limits;
	if (out_parser_sql == NULL || out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_state = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	state = NULL;
	status = sqlparser_dameng_state_new(&state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_preprocess_text(input_sql, state, out_parser_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_state_destroy(state);
		return status;
	}
	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_preprocess_fragment(
	const char *input_sql,
	void *state,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	if (out_parser_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "dialect fragment output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Dameng dialect state is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	return sqlparser_dameng_preprocess_text(input_sql, (sqlparser_dameng_state_t *)state, out_parser_sql, out_error);
}

static sqlparser_status_t sqlparser_dameng_param_to_bind(
	const char *sql,
	size_t *index,
	const sqlparser_dameng_state_t *state,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	unsigned long value;
	size_t pos;

	pos = *index + 1U;
	if (!isdigit((unsigned char)sql[pos])) {
		return sqlparser_dameng_buffer_append_char(out, sql[(*index)++], out_error);
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

	if (state != NULL && value > 0UL && (size_t)value <= state->bind_count) {
		*index = pos;
		return sqlparser_dameng_buffer_append_cstr(out, state->bind_names[value - 1UL], out_error);
	}

	return sqlparser_dameng_buffer_append_char(out, sql[(*index)++], out_error);
}

static size_t sqlparser_dameng_quoted_literal_end(const char *sql, size_t start)
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

static size_t sqlparser_dameng_skip_optional_timestamp_zone(const char *sql, size_t pos)
{
	size_t scan;
	size_t word_pos;

	scan = pos;
	while (isspace((unsigned char)sql[scan])) {
		scan++;
	}
	if (sqlparser_dameng_ascii_word_equal(sql, scan, "without") ||
	    sqlparser_dameng_ascii_word_equal(sql, scan, "with")) {
		word_pos = scan;
		while (sqlparser_dameng_is_ident_char((unsigned char)sql[word_pos])) {
			word_pos++;
		}
		while (isspace((unsigned char)sql[word_pos])) {
			word_pos++;
		}
		if (sqlparser_dameng_ascii_word_equal(sql, word_pos, "time")) {
			while (sqlparser_dameng_is_ident_char((unsigned char)sql[word_pos])) {
				word_pos++;
			}
			while (isspace((unsigned char)sql[word_pos])) {
				word_pos++;
			}
			if (sqlparser_dameng_ascii_word_equal(sql, word_pos, "zone")) {
				while (sqlparser_dameng_is_ident_char((unsigned char)sql[word_pos])) {
					word_pos++;
				}
				return word_pos;
			}
		}
	}
	return pos;
}

static int sqlparser_dameng_copy_cast_literal(
	const char *sql,
	size_t *index,
	sqlparser_dameng_buffer_t *out,
	sqlparser_error_t *out_error)
{
	const char *prefix;
	size_t literal_end;
	size_t cast_name_pos;
	size_t cast_end;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out == NULL || sql[*index] != '\'') {
		return 0;
	}
	literal_end = sqlparser_dameng_quoted_literal_end(sql, *index);
	if (literal_end == 0U || sql[literal_end] != ':' || sql[literal_end + 1U] != ':') {
		return 0;
	}

	prefix = NULL;
	cast_name_pos = literal_end + 2U;
	cast_end = cast_name_pos;
	if (sqlparser_dameng_ascii_word_equal(sql, cast_name_pos, "date")) {
		prefix = "DATE ";
		cast_end += strlen("date");
	} else if (sqlparser_dameng_ascii_word_equal(sql, cast_name_pos, "timestamp")) {
		prefix = "TIMESTAMP ";
		cast_end += strlen("timestamp");
		cast_end = sqlparser_dameng_skip_optional_timestamp_zone(sql, cast_end);
	}
	if (prefix == NULL) {
		return 0;
	}

	status = sqlparser_dameng_buffer_append_cstr(out, prefix, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	status = sqlparser_dameng_buffer_append_mem(out, sql + *index, literal_end - *index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	*index = cast_end;
	return 1;
}

static sqlparser_status_t sqlparser_dameng_postprocess_text(
	const char *core_sql,
	const sqlparser_dameng_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	status = sqlparser_dameng_buffer_reserve_input(&out, core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	while (core_sql[index] != '\0') {
		int copied;

		copied = sqlparser_dameng_copy_cast_literal(core_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		copied = sqlparser_dameng_copy_quoted_or_comment(core_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_dameng_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if (core_sql[index] == '$') {
			status = sqlparser_dameng_param_to_bind(core_sql, &index, state, &out, out_error);
		} else if (state != NULL && state->saw_minus &&
		           sqlparser_dameng_ascii_word_equal(core_sql, index, "except")) {
			status = sqlparser_dameng_buffer_append_cstr(&out, "MINUS", out_error);
			index += strlen("except");
		} else if (sqlparser_dameng_ascii_word_equal(core_sql, index, "truncate")) {
			status = sqlparser_dameng_buffer_append_cstr(&out, "TRUNCATE TABLE ", out_error);
			index += strlen("truncate");
			while (isspace((unsigned char)core_sql[index])) {
				index++;
			}
			if (sqlparser_dameng_ascii_word_equal(core_sql, index, "table")) {
				index += strlen("table");
				while (isspace((unsigned char)core_sql[index])) {
					index++;
				}
			}
		} else {
			status = sqlparser_dameng_buffer_append_char(&out, core_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_dameng_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_dameng_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_dameng_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	char quote;
	size_t pos;

	quote = sql[index];
	if (quote == '\'' || quote == '"') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == quote && sql[pos + 1U] == quote) {
				pos += 2U;
				continue;
			}
			if (sql[pos] == quote) {
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}
	if (sql[index] == '-' && sql[index + 1U] == '-') {
		pos = index + 2U;
		while (sql[pos] != '\0' && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if (sql[index] == '/' && sql[index + 1U] == '*') {
		pos = index + 2U;
		while (sql[pos] != '\0') {
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				return pos + 2U;
			}
			pos++;
		}
		return pos;
	}
	return index;
}

static size_t sqlparser_dameng_statement_end(const char *sql, size_t start)
{
	size_t index;
	size_t skipped;

	index = start;
	while (sql[index] != '\0') {
		skipped = sqlparser_dameng_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}
		if (sql[index] == ';') {
			break;
		}
		index++;
	}
	return index;
}

static sqlparser_status_t sqlparser_dameng_postprocess_session_switch(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	const char *prefix_to;
	const char *prefix_eq;
	const char *value_start;
	size_t start;
	size_t end;
	size_t prefix_len;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	start = sqlparser_dameng_skip_space(core_sql, 0U);
	end = sqlparser_dameng_trim_right(core_sql, start, strlen(core_sql));
	prefix_to = "SET " SQLPARSER_INTERNAL_CURRENT_SCHEMA " TO ";
	prefix_eq = "SET " SQLPARSER_INTERNAL_CURRENT_SCHEMA " = ";
	prefix_len = strlen(prefix_to);
	if (end - start >= prefix_len && strncmp(core_sql + start, prefix_to, prefix_len) == 0) {
		value_start = core_sql + start + prefix_len;
	} else {
		prefix_len = strlen(prefix_eq);
		if (end - start < prefix_len || strncmp(core_sql + start, prefix_eq, prefix_len) != 0) {
			return SQLPARSER_STATUS_OK;
		}
		value_start = core_sql + start + prefix_len;
	}

	if ((size_t)(value_start - core_sql) >= end) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_dameng_buffer_append_cstr(&out, "ALTER SESSION SET CURRENT_SCHEMA = ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_append_mem(
			&out,
			value_start,
			end - (size_t)(value_start - core_sql),
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_dameng_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_read_internal_string_arg(
	const char *sql,
	size_t *index,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
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
	pos = sqlparser_dameng_skip_space(sql, *index);
	if (sql[pos] == '\'' || sql[pos] == '"') {
		quote = sql[pos];
		pos++;
		memset(&out, 0, sizeof(out));
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					status = sqlparser_dameng_buffer_append_char(&out, quote, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_dameng_buffer_release(&out);
						return status;
					}
					pos += 2U;
					continue;
				}
				pos++;
				*index = pos;
				*out_value = sqlparser_dameng_buffer_take(&out);
				if (*out_value == NULL) {
					*out_value = sqlparser_strdup("");
					if (*out_value == NULL) {
						sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
						return SQLPARSER_STATUS_NO_MEMORY;
					}
				}
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_dameng_buffer_append_char(&out, sql[pos], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_buffer_release(&out);
				return status;
			}
			pos++;
		}
		sqlparser_dameng_buffer_release(&out);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated internal Dameng prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	token_start = pos;
	while (sql[pos] != '\0' && sql[pos] != ',') {
		pos++;
	}
	token_end = sqlparser_dameng_trim_right(sql, token_start, pos);
	if (token_start >= token_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal Dameng prepared argument");
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

static int sqlparser_dameng_internal_set_prefix(
	const char *sql,
	const char *internal_name,
	size_t *out_pos)
{
	size_t pos;
	size_t len;

	pos = sqlparser_dameng_skip_space(sql, 0U);
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "set")) {
		return 0;
	}
	pos = sqlparser_dameng_skip_space(sql, pos + strlen("set"));
	len = strlen(internal_name);
	if (strncmp(sql + pos, internal_name, len) != 0 ||
	    sqlparser_dameng_is_ident_char((unsigned char)sql[pos + len])) {
		return 0;
	}
	pos = sqlparser_dameng_skip_space(sql, pos + len);
	if (!sqlparser_dameng_ascii_word_equal(sql, pos, "to") && sql[pos] != '=') {
		return 0;
	}
	pos = sql[pos] == '=' ? pos + 1U : pos + strlen("to");
	*out_pos = pos;
	return 1;
}

static sqlparser_status_t sqlparser_dameng_postprocess_exec_sql_prepared(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	static const struct {
		const char *internal_name;
		const char *prefix;
		const char *middle;
		int needs_second_arg;
	} specs[] = {
		{SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_PREPARE, "EXEC SQL PREPARE ", " FROM ", 1},
		{SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_EXECUTE, "EXEC SQL EXECUTE ", " USING ", 0},
		{SQLPARSER_INTERNAL_DAMENG_EXEC_SQL_DEALLOCATE_PREPARE, "EXEC SQL DEALLOCATE PREPARE ", NULL, 0}
	};
	sqlparser_dameng_buffer_t out;
	char *arg0;
	char *arg1;
	size_t pos;
	size_t index;
	size_t spec_index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	for (spec_index = 0U; spec_index < sizeof(specs) / sizeof(specs[0]); spec_index++) {
		if (!sqlparser_dameng_internal_set_prefix(core_sql, specs[spec_index].internal_name, &pos)) {
			continue;
		}
		arg0 = NULL;
		arg1 = NULL;
		index = pos;
		status = sqlparser_dameng_read_internal_string_arg(core_sql, &index, &arg0, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(arg0);
			return status;
		}
		index = sqlparser_dameng_skip_space(core_sql, index);
		if (core_sql[index] == ',') {
			index++;
			status = sqlparser_dameng_read_internal_string_arg(core_sql, &index, &arg1, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(arg0);
				free(arg1);
				return status;
			}
			index = sqlparser_dameng_skip_space(core_sql, index);
		}
		if (core_sql[index] != '\0') {
			free(arg0);
			free(arg1);
			return SQLPARSER_STATUS_OK;
		}
		if (specs[spec_index].needs_second_arg && arg1 == NULL) {
			free(arg0);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "prepared statement SQL argument is missing");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}

		memset(&out, 0, sizeof(out));
		status = sqlparser_dameng_buffer_append_cstr(&out, specs[spec_index].prefix, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_append_cstr(&out, arg0, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && arg1 != NULL && specs[spec_index].middle != NULL) {
			status = sqlparser_dameng_buffer_append_cstr(&out, specs[spec_index].middle, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(&out, arg1, out_error);
			}
		}
		free(arg0);
		free(arg1);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_dameng_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_dameng_buffer_take(&out);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_rewrite_session_switches(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_dameng_buffer_t out;
	const char *sql;
	char *statement_sql;
	char *rewritten_sql;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	int rewritten;
	sqlparser_status_t status;

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
		statement_end = sqlparser_dameng_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_dameng_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_dameng_postprocess_session_switch(statement_sql, &rewritten_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_dameng_postprocess_exec_sql_prepared(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_dameng_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_dameng_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = sqlparser_dameng_skip_space(sql, segment_start);
			status = sqlparser_dameng_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_dameng_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_dameng_buffer_release(&out);
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
	status = sqlparser_dameng_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_dameng_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_dameng_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_dameng_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *public_sql;
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

	public_sql = NULL;
	status = sqlparser_dameng_postprocess_text(core_sql, (const sqlparser_dameng_state_t *)state, &public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_dameng_rewrite_session_switches(&public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_dameng_clone_state(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_dameng_state_t *source;
	sqlparser_dameng_state_t *clone;
	size_t index;
	sqlparser_status_t status;

	if (out_state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;
	if (state == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	source = (const sqlparser_dameng_state_t *)state;
	status = sqlparser_dameng_state_new(&clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clone->saw_minus = source->saw_minus;
	for (index = 0U; index < source->bind_count; index++) {
		size_t param_index;

		status = sqlparser_dameng_state_append_bind(
			clone,
			source->bind_names[index],
			strlen(source->bind_names[index]),
			&param_index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_dameng_state_destroy(clone);
			return status;
		}
		(void)param_index;
	}
	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_dialect_ops_t SQLPARSER_DAMENG_OPS = {
	SQLPARSER_DIALECT_DAMENG,
	"dameng",
	sqlparser_dameng_preprocess,
	sqlparser_dameng_preprocess_fragment,
	sqlparser_dameng_postprocess_deparse,
	sqlparser_dameng_clone_state,
	sqlparser_dameng_state_destroy,
	NULL
};

const sqlparser_dialect_ops_t *sqlparser_dialect_dameng_ops(void)
{
	return &SQLPARSER_DAMENG_OPS;
}
