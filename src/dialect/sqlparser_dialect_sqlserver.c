#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
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

typedef struct {
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
	size_t top_count;
	size_t top_capacity;
	sqlparser_sqlserver_cast_restore_t *cast_restores;
	size_t cast_restore_count;
	size_t cast_restore_capacity;
	size_t cast_count;
	size_t rename_object_count;
} sqlparser_sqlserver_state_t;

static sqlparser_status_t sqlparser_sqlserver_preprocess_text(
	const char *input_sql,
	sqlparser_sqlserver_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error);

static void sqlparser_sqlserver_buffer_release(sqlparser_sqlserver_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}

	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
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

static sqlparser_status_t sqlparser_sqlserver_buffer_append_mem(
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

	for (index = 0U; index < sqlserver_state->param_count; index++) {
		free(sqlserver_state->param_names[index]);
	}
	for (index = 0U; index < sqlserver_state->unicode_count; index++) {
		free(sqlserver_state->unicode_literals[index]);
	}
	for (index = 0U; index < sqlserver_state->top_count; index++) {
		free(sqlserver_state->top_limits[index]);
	}
	for (index = 0U; index < sqlserver_state->cast_restore_count; index++) {
		free(sqlserver_state->cast_restores[index].tail);
	}
	free(sqlserver_state->param_names);
	free(sqlserver_state->unicode_literals);
	free(sqlserver_state->unicode_ordinals);
	free(sqlserver_state->top_limits);
	free(sqlserver_state->cast_restores);
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

static int sqlparser_sqlserver_is_ident_start(unsigned char c)
{
	return isalpha(c) || c == '_' || c == '#';
}

static int sqlparser_sqlserver_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '$' || c == '#' || c == '@';
}

static int sqlparser_sqlserver_is_word_boundary(const char *text, size_t pos, size_t len)
{
	unsigned char prev;
	unsigned char next;

	prev = pos == 0U ? 0U : (unsigned char)text[pos - 1U];
	next = text[pos + len] == '\0' ? 0U : (unsigned char)text[pos + len];
	return !sqlparser_sqlserver_is_ident_char(prev) && !sqlparser_sqlserver_is_ident_char(next);
}

static int sqlparser_sqlserver_ascii_word_equal(const char *text, size_t pos, const char *word)
{
	size_t index;
	size_t len;

	if (text == NULL || word == NULL) {
		return 0;
	}

	len = strlen(word);
	for (index = 0U; index < len; index++) {
		if (text[pos + index] == '\0') {
			return 0;
		}
		if (tolower((unsigned char)text[pos + index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}

	return sqlparser_sqlserver_is_word_boundary(text, pos, len);
}

static size_t sqlparser_sqlserver_skip_space(const char *text, size_t pos)
{
	while (isspace((unsigned char)text[pos])) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_sqlserver_trim_left(const char *text, size_t start, size_t end)
{
	while (start < end && isspace((unsigned char)text[start])) {
		start++;
	}
	return start;
}

static size_t sqlparser_sqlserver_trim_right(const char *text, size_t start, size_t end)
{
	(void)start;
	while (end > start && isspace((unsigned char)text[end - 1U])) {
		end--;
	}
	return end;
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
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_sqlserver_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + 1U;
	while (input[pos] != '\0') {
		if (input[pos] == ']') {
			if (input[pos + 1U] == ']') {
				status = sqlparser_sqlserver_buffer_append_char(out, ']', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos += 2U;
				continue;
			}
			status = sqlparser_sqlserver_buffer_append_char(out, '"', out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			*index = pos + 1U;
			return SQLPARSER_STATUS_OK;
		}
		if (input[pos] == '"') {
			status = sqlparser_sqlserver_buffer_append_cstr(out, "\"\"", out_error);
		} else {
			status = sqlparser_sqlserver_buffer_append_char(out, input[pos], out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}

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
	char quote;
	size_t pos;

	if (sql[*index] == '[') {
		return sqlparser_sqlserver_append_quoted_identifier(out, sql, index, out_error);
	}

	quote = sql[*index];
	if (quote == '\'' || quote == '"') {
		pos = *index;
		if (sqlparser_sqlserver_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
		while (sql[pos] != '\0') {
			if (sqlparser_sqlserver_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					pos++;
					if (sqlparser_sqlserver_buffer_append_char(out, sql[pos], out_error) !=
					    SQLPARSER_STATUS_OK) {
						return SQLPARSER_STATUS_NO_MEMORY;
					}
				} else {
					pos++;
					break;
				}
			}
			pos++;
		}
		*index = pos;
		return SQLPARSER_STATUS_OK;
	}

	if (sql[*index] == '-' && sql[*index + 1U] == '-') {
		pos = *index;
		while (sql[pos] != '\0') {
			if (sqlparser_sqlserver_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			if (sql[pos] == '\n') {
				pos++;
				break;
			}
			pos++;
		}
		*index = pos;
		return SQLPARSER_STATUS_OK;
	}

	if (sql[*index] == '/' && sql[*index + 1U] == '*') {
		pos = *index;
		while (sql[pos] != '\0') {
			if (sqlparser_sqlserver_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				pos++;
				if (sqlparser_sqlserver_buffer_append_char(out, sql[pos], out_error) !=
				    SQLPARSER_STATUS_OK) {
					return SQLPARSER_STATUS_NO_MEMORY;
				}
				pos++;
				break;
			}
			pos++;
		}
		*index = pos;
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static size_t sqlparser_sqlserver_skip_quoted_or_comment_span(const char *sql, size_t index);

static sqlparser_status_t sqlparser_sqlserver_preprocess_use_statement(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	size_t start;
	size_t end;
	size_t name_start;
	size_t name_end;
	size_t index;
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
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_sqlserver_statement_end(const char *sql, size_t start)
{
	size_t index;
	size_t skipped;

	index = start;
	while (sql[index] != '\0') {
		skipped = sqlparser_sqlserver_skip_quoted_or_comment_span(sql, index);
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

static sqlparser_status_t sqlparser_sqlserver_rewrite_use_statements(
	char **io_sql,
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
	int rewritten;

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
		statement_end = sqlparser_sqlserver_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_sqlserver_preprocess_use_statement(statement_sql, &rewritten_sql, out_error);
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
				rewritten = 1;
			}
			leading_end = sqlparser_sqlserver_trim_left(sql, segment_start, statement_end);
			status = sqlparser_sqlserver_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_sqlserver_buffer_append_cstr(&out, rewritten_sql, out_error);
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

static size_t sqlparser_sqlserver_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	char quote;
	size_t pos;

	if (sql[index] == '[') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == ']' && sql[pos + 1U] == ']') {
				pos += 2U;
				continue;
			}
			if (sql[pos] == ']') {
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}

	if (sql[index] == '\'' || sql[index] == '"') {
		quote = sql[index];
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

static int sqlparser_sqlserver_can_copy_quoted_or_comment(const char *sql, size_t index)
{
	return sql[index] == '[' ||
	       sql[index] == '\'' ||
	       sql[index] == '"' ||
	       (sql[index] == '-' && sql[index + 1U] == '-') ||
	       (sql[index] == '/' && sql[index + 1U] == '*');
}

static sqlparser_status_t sqlparser_sqlserver_mask_non_code(
	const char *sql,
	char **out_masked,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t index;

	len = strlen(sql);
	masked = sqlparser_strndup(sql, len);
	if (masked == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	for (index = 0U; index < len; index++) {
		if ((masked[index] == 'n' || masked[index] == 'N') && masked[index + 1U] == '\'') {
			masked[index] = ' ';
			index++;
		}
		if (masked[index] == '\'') {
			index++;
			while (index < len) {
				if (masked[index] == '\'' && masked[index + 1U] == '\'') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '\'') {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '"' || masked[index] == '[') {
			char close_quote;

			close_quote = masked[index] == '[' ? ']' : '"';
			index++;
			while (index < len) {
				if (masked[index] == close_quote && masked[index + 1U] == close_quote) {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == close_quote) {
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '-' && masked[index + 1U] == '-') {
			while (index < len && masked[index] != '\n') {
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '/' && masked[index + 1U] == '*') {
			masked[index] = ' ';
			masked[index + 1U] = ' ';
			index += 2U;
			while (index < len) {
				if (masked[index] == '*' && masked[index + 1U] == '/') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index++;
					break;
				}
				masked[index] = ' ';
				index++;
			}
		}
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

static sqlparser_status_t sqlparser_sqlserver_reject_unsupported(
	const char *sql,
	sqlparser_error_t *out_error)
{
	static const char *const unsupported_phrases[] = {
		"with (",
		"cross apply",
		"outer apply",
		"pivot",
		"unpivot",
		"for xml",
		"for json",
		"option (",
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

	if (strstr(masked, "from @") != NULL ||
	    strstr(masked, "join @") != NULL ||
	    strstr(masked, "update @") != NULL ||
	    strstr(masked, "into @") != NULL) {
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
		end = start + 2U;
		if (!sqlparser_sqlserver_is_ident_start((unsigned char)input[end])) {
			return sqlparser_sqlserver_buffer_append_char(out, input[(*index)++], out_error);
		}
		while (sqlparser_sqlserver_is_ident_char((unsigned char)input[end])) {
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

static sqlparser_status_t sqlparser_sqlserver_parse_top_clause(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_state_t *state,
	char **out_public_limit,
	char **out_parser_limit,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t limit;
	char *public_limit;
	size_t pos;
	size_t expr_start;
	size_t expr_end;
	int paren_depth;
	sqlparser_status_t status;

	if (state == NULL || out_public_limit == NULL || out_parser_limit == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"top limit output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_public_limit = NULL;
	*out_parser_limit = NULL;

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

	pos = sqlparser_sqlserver_skip_space(input, pos);
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "percent")) {
		sqlparser_sqlserver_buffer_release(&limit);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported SQL Server syntax: TOP PERCENT");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_sqlserver_ascii_word_equal(input, pos, "with")) {
		size_t ties_pos;

		ties_pos = sqlparser_sqlserver_skip_space(input, pos + 4U);
		if (sqlparser_sqlserver_ascii_word_equal(input, ties_pos, "ties")) {
			sqlparser_sqlserver_buffer_release(&limit);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"unsupported SQL Server syntax: TOP WITH TIES");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}

	public_limit = sqlparser_sqlserver_buffer_take(&limit);
	if (public_limit == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_sqlserver_preprocess_text(public_limit, state, out_parser_limit, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_limit);
		return status;
	}

	*out_public_limit = public_limit;
	*index = pos;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_store_top_limit(
	sqlparser_sqlserver_state_t *state,
	const char *limit,
	sqlparser_error_t *out_error)
{
	return sqlparser_sqlserver_array_add(
		&state->top_limits,
		&state->top_count,
		&state->top_capacity,
		limit,
		strlen(limit),
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_append_pending_top_limit(
	sqlparser_sqlserver_buffer_t *out,
	char **pending_top_limit,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (pending_top_limit == NULL || *pending_top_limit == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_sqlserver_buffer_append_cstr(out, " LIMIT ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_buffer_append_cstr(out, *pending_top_limit, out_error);
	}
	free(*pending_top_limit);
	*pending_top_limit = NULL;
	return status;
}

static int sqlparser_sqlserver_line_is_go(const char *input, size_t index, size_t *out_next)
{
	size_t line_start;
	size_t pos;
	size_t token_start;
	size_t token_end;

	line_start = index;
	while (line_start > 0U && input[line_start - 1U] != '\n' && input[line_start - 1U] != '\r') {
		line_start--;
	}
	pos = line_start;
	while (isspace((unsigned char)input[pos]) && input[pos] != '\n' && input[pos] != '\r') {
		pos++;
	}
	if (!sqlparser_sqlserver_ascii_word_equal(input, pos, "go")) {
		return 0;
	}
	token_start = pos;
	pos += 2U;
	token_end = pos;
	while (input[pos] != '\0' && input[pos] != '\n' && input[pos] != '\r') {
		if (!isspace((unsigned char)input[pos]) && !isdigit((unsigned char)input[pos])) {
			return 0;
		}
		pos++;
	}
	(void)token_start;
	(void)token_end;
	while (input[pos] == '\r' || input[pos] == '\n') {
		pos++;
	}
	if (out_next != NULL) {
		*out_next = pos;
	}
	return 1;
}

static sqlparser_status_t sqlparser_sqlserver_append_hash_identifier(
	const char *input,
	size_t *index,
	sqlparser_sqlserver_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	pos = *index;
	status = sqlparser_sqlserver_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	while (sqlparser_sqlserver_is_ident_char((unsigned char)input[pos])) {
		status = sqlparser_sqlserver_buffer_append_char(out, input[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}
	status = sqlparser_sqlserver_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	*index = pos;
	return SQLPARSER_STATUS_OK;
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

static int sqlparser_sqlserver_find_matching_paren(
	const char *input,
	size_t open_pos,
	size_t *out_close_pos,
	size_t *out_next_pos)
{
	size_t pos;
	size_t depth;

	if (input == NULL || input[open_pos] != '(') {
		return 0;
	}

	pos = open_pos + 1U;
	depth = 1U;
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
		} else if (input[pos] == ')') {
			depth--;
			if (depth == 0U) {
				if (out_close_pos != NULL) {
					*out_close_pos = pos;
				}
				if (out_next_pos != NULL) {
					*out_next_pos = pos + 1U;
				}
				return 1;
			}
		}
		pos++;
	}

	return 0;
}

static int sqlparser_sqlserver_find_top_level_char(
	const char *input,
	size_t start,
	size_t end,
	char target,
	size_t *out_pos)
{
	size_t pos;
	size_t depth;

	depth = 0U;
	for (pos = start; pos < end;) {
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
		} else if (input[pos] == ')' && depth > 0U) {
			depth--;
		} else if (depth == 0U && input[pos] == target) {
			if (out_pos != NULL) {
				*out_pos = pos;
			}
			return 1;
		}
		pos++;
	}

	return 0;
}

static int sqlparser_sqlserver_find_top_level_word(
	const char *input,
	size_t start,
	size_t end,
	const char *word,
	size_t *out_pos)
{
	size_t pos;
	size_t depth;
	size_t word_len;

	word_len = strlen(word);
	depth = 0U;
	for (pos = start; pos + word_len <= end;) {
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
		if (input[pos] == ')' && depth > 0U) {
			depth--;
			pos++;
			continue;
		}
		if (depth == 0U && sqlparser_sqlserver_ascii_word_equal(input, pos, word)) {
			if (out_pos != NULL) {
				*out_pos = pos;
			}
			return 1;
		}
		pos++;
	}

	return 0;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_slice(
	const char *input,
	size_t start,
	size_t end,
	sqlparser_sqlserver_state_t *state,
	char **out_sql,
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

	status = sqlparser_sqlserver_preprocess_text(slice, state, out_sql, out_error);
	free(slice);
	return status;
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
	sqlparser_status_t status;

	processed = NULL;
	status = sqlparser_sqlserver_preprocess_slice(input, start, end, state, &processed, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_sqlserver_buffer_append_cstr(out, processed, out_error);
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

static sqlparser_status_t sqlparser_sqlserver_preprocess_text(
	const char *input_sql,
	sqlparser_sqlserver_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_buffer_t out;
	char *pending_top_limit;
	size_t index;
	size_t paren_depth;
	sqlparser_status_t status;

	memset(&out, 0, sizeof(out));
	pending_top_limit = NULL;
	status = sqlparser_sqlserver_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	paren_depth = 0U;

	while (input_sql[index] != '\0') {
		size_t next_index;

		if (sqlparser_sqlserver_line_is_go(input_sql, index, &next_index)) {
			status = sqlparser_sqlserver_append_pending_top_limit(&out, &pending_top_limit, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				return status;
			}
			status = sqlparser_sqlserver_buffer_append_cstr(&out, "; ", out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				return status;
			}
			index = next_index;
			paren_depth = 0U;
			continue;
		}

		if ((input_sql[index] == 'n' || input_sql[index] == 'N') && input_sql[index + 1U] == '\'') {
			status = sqlparser_sqlserver_copy_unicode_string(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
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
				free(pending_top_limit);
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
				free(pending_top_limit);
				return status;
			}
			continue;
		}

		if (input_sql[index] == '0' &&
		    (input_sql[index + 1U] == 'x' || input_sql[index + 1U] == 'X')) {
			status = sqlparser_sqlserver_copy_binary_literal(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
				return status;
			}
			continue;
		}

		if (input_sql[index] == '#' &&
		    sqlparser_sqlserver_is_ident_start((unsigned char)input_sql[index + 1U])) {
			status = sqlparser_sqlserver_append_hash_identifier(input_sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
				return status;
			}
			continue;
		}

		if (input_sql[index] == '{') {
			status = sqlparser_sqlserver_copy_odbc_fn(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "try_cast")) {
			status = sqlparser_sqlserver_copy_try_cast_keyword(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_word_followed_by_lparen(input_sql, index, "cast")) {
			status = sqlparser_sqlserver_copy_regular_cast_keyword(&index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
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
				free(pending_top_limit);
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
				free(pending_top_limit);
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
				free(pending_top_limit);
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
				free(pending_top_limit);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "rename")) {
			status = sqlparser_sqlserver_copy_rename_object(input_sql, &index, &out, state, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
				return status;
			}
			continue;
		}

		if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "select")) {
			char *top_public_limit;
			char *top_parser_limit;
			size_t after_select;
			size_t top_pos;

			status = sqlparser_sqlserver_buffer_append_mem(&out, input_sql + index, 6U, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
				return status;
			}
			index += 6U;
			after_select = index;
			while (isspace((unsigned char)input_sql[index])) {
				status = sqlparser_sqlserver_buffer_append_char(&out, input_sql[index], out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					free(pending_top_limit);
					return status;
				}
				index++;
			}

			if (sqlparser_sqlserver_ascii_word_equal(input_sql, index, "all") ||
			    sqlparser_sqlserver_ascii_word_equal(input_sql, index, "distinct")) {
				size_t word_len;

				word_len = sqlparser_sqlserver_ascii_word_equal(input_sql, index, "distinct") ? 8U : 3U;
				status = sqlparser_sqlserver_buffer_append_mem(&out, input_sql + index, word_len, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					free(pending_top_limit);
					return status;
				}
				index += word_len;
				while (isspace((unsigned char)input_sql[index])) {
					status = sqlparser_sqlserver_buffer_append_char(&out, input_sql[index], out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_sqlserver_buffer_release(&out);
						free(pending_top_limit);
						return status;
					}
					index++;
				}
			}

			top_pos = index;
			top_public_limit = NULL;
			top_parser_limit = NULL;
			status = sqlparser_sqlserver_parse_top_clause(
				input_sql,
				&top_pos,
				state,
				&top_public_limit,
				&top_parser_limit,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				free(pending_top_limit);
				free(top_public_limit);
				free(top_parser_limit);
				return status;
			}
			if (top_public_limit != NULL) {
				if (pending_top_limit != NULL || paren_depth > 0U) {
					sqlparser_sqlserver_buffer_release(&out);
					free(pending_top_limit);
					free(top_public_limit);
					free(top_parser_limit);
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_UNSUPPORTED,
						"unsupported SQL Server syntax: nested TOP");
					return SQLPARSER_STATUS_UNSUPPORTED;
				}
				status = sqlparser_sqlserver_store_top_limit(state, top_public_limit, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_sqlserver_buffer_release(&out);
					free(pending_top_limit);
					free(top_public_limit);
					free(top_parser_limit);
					return status;
				}
				free(top_public_limit);
				pending_top_limit = top_parser_limit;
				index = sqlparser_sqlserver_skip_space(input_sql, top_pos);
				if (!isspace((unsigned char)input_sql[after_select]) && input_sql[index] != '\0') {
					status = sqlparser_sqlserver_buffer_append_char(&out, ' ', out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_sqlserver_buffer_release(&out);
						free(pending_top_limit);
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
				free(pending_top_limit);
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
					free(pending_top_limit);
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
			paren_depth--;
		} else if (input_sql[index] == ';' && paren_depth == 0U) {
			status = sqlparser_sqlserver_append_pending_top_limit(&out, &pending_top_limit, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_sqlserver_buffer_release(&out);
				return status;
			}
		}

		status = sqlparser_sqlserver_buffer_append_char(&out, input_sql[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_sqlserver_buffer_release(&out);
			free(pending_top_limit);
			return status;
		}
		index++;
	}

	status = sqlparser_sqlserver_append_pending_top_limit(&out, &pending_top_limit, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_buffer_release(&out);
		return status;
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
		statement_end = sqlparser_sqlserver_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_sqlserver_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		original_statement_sql = statement_sql;
		status = sqlparser_sqlserver_rewrite_internal_use_statement(&statement_sql, out_error);
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

	if (sql == NULL || out_select_pos == NULL) {
		return 0;
	}

	select_pos = (size_t)-1;
	paren_depth = 0U;
	pos = 0U;
	while (sql[pos] != '\0' && pos < limit_pos) {
		if (sql[pos] == '\'') {
			size_t literal_end;

			literal_end = sqlparser_sqlserver_quoted_literal_end(sql, pos);
			if (literal_end == 0U || literal_end > limit_pos) {
				break;
			}
			pos = literal_end;
			continue;
		}
		if (sql[pos] == '"') {
			pos++;
			while (sql[pos] != '\0' && pos < limit_pos) {
				if (sql[pos] == '"' && sql[pos + 1U] == '"') {
					pos += 2U;
					continue;
				}
				if (sql[pos] == '"') {
					pos++;
					break;
				}
				pos++;
			}
			continue;
		}
		if (sql[pos] == '(') {
			paren_depth++;
		} else if (sql[pos] == ')' && paren_depth > 0U) {
			paren_depth--;
		} else if (paren_depth == 0U && sqlparser_sqlserver_ascii_word_equal(sql, pos, "select")) {
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
		    sqlparser_sqlserver_buffer_append_cstr(&out, ") ", out_error) != SQLPARSER_STATUS_OK ||
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

static sqlparser_status_t sqlparser_sqlserver_postprocess_text(
	const char *core_sql,
	const sqlparser_sqlserver_state_t *state,
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
		status = sqlparser_sqlserver_apply_rename_public(&public_sql, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_sqlserver_apply_top_public(&public_sql, state, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && (state == NULL || state->top_count == 0U)) {
		status = sqlparser_sqlserver_apply_offset_public(&public_sql, out_error);
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

static sqlparser_status_t sqlparser_sqlserver_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_state_t *state;
	char *preprocess_sql;
	sqlparser_status_t status;

	(void)limits;

	if (out_parser_sql == NULL || out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect preprocess output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_state = NULL;

	if (input_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL input must not be NULL");
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
	status = sqlparser_sqlserver_rewrite_use_statements(&preprocess_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(preprocess_sql);
		sqlparser_sqlserver_state_destroy(state);
		return status;
	}

	status = sqlparser_sqlserver_reject_unsupported(preprocess_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(preprocess_sql);
		sqlparser_sqlserver_state_destroy(state);
		return status;
	}

	status = sqlparser_sqlserver_preprocess_text(preprocess_sql, state, out_parser_sql, out_error);
	free(preprocess_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_sqlserver_state_destroy(state);
		return status;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_preprocess_fragment(
	const char *input_sql,
	void *state,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
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

	return sqlparser_sqlserver_preprocess_text(
		input_sql,
		(sqlparser_sqlserver_state_t *)state,
		out_parser_sql,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
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

	return sqlparser_sqlserver_postprocess_text(
		core_sql,
		(const sqlparser_sqlserver_state_t *)state,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_sqlserver_clone_state(
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
		status = sqlparser_sqlserver_array_add(
			&clone->top_limits,
			&clone->top_count,
			&clone->top_capacity,
			source->top_limits[index],
			strlen(source->top_limits[index]),
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

	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
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

	sqlserver_state = (const sqlparser_sqlserver_state_t *)state;
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

static const sqlparser_dialect_ops_t SQLPARSER_SQLSERVER_OPS = {
	SQLPARSER_DIALECT_SQLSERVER,
	"sqlserver",
	sqlparser_sqlserver_preprocess,
	sqlparser_sqlserver_preprocess_fragment,
	sqlparser_sqlserver_postprocess_deparse,
	sqlparser_sqlserver_clone_state,
	sqlparser_sqlserver_state_destroy,
	sqlparser_sqlserver_postprocess_literal_fragment
};

const sqlparser_dialect_ops_t *sqlparser_dialect_sqlserver_ops(void)
{
	return &SQLPARSER_SQLSERVER_OPS;
}
