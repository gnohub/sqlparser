#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
} sqlparser_mysql_buffer_t;

typedef struct {
	size_t positional_param_count;
} sqlparser_mysql_state_t;

static sqlparser_status_t sqlparser_mysql_state_new(
	sqlparser_mysql_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_state_t *state;

	if (out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_state = NULL;

	state = (sqlparser_mysql_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static void sqlparser_mysql_state_destroy(void *state)
{
	free(state);
}

static void sqlparser_mysql_buffer_release(sqlparser_mysql_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}

	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
}

static sqlparser_status_t sqlparser_mysql_buffer_reserve(
	sqlparser_mysql_buffer_t *buffer,
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

static sqlparser_status_t sqlparser_mysql_buffer_append_mem(
	sqlparser_mysql_buffer_t *buffer,
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

	status = sqlparser_mysql_buffer_reserve(buffer, len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_buffer_append_char(
	sqlparser_mysql_buffer_t *buffer,
	char value,
	sqlparser_error_t *out_error)
{
	return sqlparser_mysql_buffer_append_mem(buffer, &value, 1U, out_error);
}

static sqlparser_status_t sqlparser_mysql_buffer_append_cstr(
	sqlparser_mysql_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	return sqlparser_mysql_buffer_append_mem(buffer, text, text != NULL ? strlen(text) : 0U, out_error);
}

static sqlparser_status_t sqlparser_mysql_append_pg_param(
	sqlparser_mysql_buffer_t *out,
	size_t param_index,
	sqlparser_error_t *out_error)
{
	char text[32];

	(void)snprintf(text, sizeof(text), "$%lu", (unsigned long)param_index);
	return sqlparser_mysql_buffer_append_cstr(out, text, out_error);
}

static char *sqlparser_mysql_buffer_take(sqlparser_mysql_buffer_t *buffer)
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

static sqlparser_status_t sqlparser_mysql_buffer_reserve_input(
	sqlparser_mysql_buffer_t *buffer,
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

static int sqlparser_mysql_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_';
}

static int sqlparser_mysql_is_word_boundary(const char *text, size_t pos, size_t len)
{
	unsigned char prev;
	unsigned char next;

	prev = pos == 0U ? 0U : (unsigned char)text[pos - 1U];
	next = text[pos + len] == '\0' ? 0U : (unsigned char)text[pos + len];
	return !sqlparser_mysql_is_ident_char(prev) && !sqlparser_mysql_is_ident_char(next);
}

static int sqlparser_mysql_ascii_word_equal(const char *text, size_t pos, const char *word)
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

	return sqlparser_mysql_is_word_boundary(text, pos, len);
}

static sqlparser_status_t sqlparser_mysql_append_pg_string_char(
	sqlparser_mysql_buffer_t *out,
	char value,
	sqlparser_error_t *out_error)
{
	if (value == '\'') {
		return sqlparser_mysql_buffer_append_cstr(out, "''", out_error);
	}

	return sqlparser_mysql_buffer_append_char(out, value, out_error);
}

static sqlparser_status_t sqlparser_mysql_copy_string_literal(
	const char *input,
	size_t *index,
	char quote,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	size_t pos;

	status = sqlparser_mysql_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + 1U;
	while (input[pos] != '\0') {
		char c;

		c = input[pos];
		if (c == '\\' && input[pos + 1U] != '\0') {
			pos++;
			c = input[pos];
			switch (c) {
				case '0':
					c = '0';
					break;
				case 'b':
					c = '\b';
					break;
				case 'n':
					c = '\n';
					break;
				case 'r':
					c = '\r';
					break;
				case 't':
					c = '\t';
					break;
				case 'Z':
					c = 26;
					break;
				default:
					break;
			}
			status = sqlparser_mysql_append_pg_string_char(out, c, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			pos++;
			continue;
		}
		if (c == quote) {
			if (input[pos + 1U] == quote) {
				status = sqlparser_mysql_append_pg_string_char(out, quote, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos += 2U;
				continue;
			}
			status = sqlparser_mysql_buffer_append_char(out, '\'', out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			*index = pos + 1U;
			return SQLPARSER_STATUS_OK;
		}

		status = sqlparser_mysql_append_pg_string_char(out, c, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated MySQL string literal");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_mysql_copy_backtick_identifier(
	const char *input,
	size_t *index,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;
	size_t pos;

	status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + 1U;
	while (input[pos] != '\0') {
		char c;

		c = input[pos];
		if (c == '`') {
			if (input[pos + 1U] == '`') {
				status = sqlparser_mysql_buffer_append_char(out, '`', out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				pos += 2U;
				continue;
			}
			status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			*index = pos + 1U;
			return SQLPARSER_STATUS_OK;
		}
		if (c == '"') {
			status = sqlparser_mysql_buffer_append_cstr(out, "\"\"", out_error);
		} else {
			status = sqlparser_mysql_buffer_append_char(out, c, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated MySQL quoted identifier");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_mysql_preprocess_quotes(
	const char *input_sql,
	sqlparser_mysql_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	while (input_sql[index] != '\0') {
		char c;

		c = input_sql[index];
		if (c == '-' && input_sql[index + 1U] == '-') {
			while (input_sql[index] != '\0') {
				status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
				if (status != SQLPARSER_STATUS_OK || input_sql[index] == '\n') {
					break;
				}
				index++;
			}
			if (status == SQLPARSER_STATUS_OK && input_sql[index] == '\n') {
				index++;
			}
		} else if (c == '/' && input_sql[index + 1U] == '*') {
			status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
			while (status == SQLPARSER_STATUS_OK && input_sql[index] != '\0') {
				status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
				if (input_sql[index] == '*' && input_sql[index + 1U] == '/') {
					index++;
					if (status == SQLPARSER_STATUS_OK) {
						status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
					}
					index++;
					break;
				}
				index++;
			}
		} else if (c == '`') {
			status = sqlparser_mysql_copy_backtick_identifier(input_sql, &index, &out, out_error);
		} else if (c == '\'' || c == '"') {
			status = sqlparser_mysql_copy_string_literal(input_sql, &index, c, &out, out_error);
		} else if (c == '#') {
			status = sqlparser_mysql_buffer_append_cstr(&out, "--", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
				while (input_sql[index] != '\0' && input_sql[index] != '\n') {
					status = sqlparser_mysql_buffer_append_char(&out, input_sql[index], out_error);
					if (status != SQLPARSER_STATUS_OK) {
						break;
					}
					index++;
				}
			}
		} else if (c == '?' && state != NULL) {
			if (state->positional_param_count == (size_t)-1) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				sqlparser_mysql_buffer_release(&out);
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			state->positional_param_count++;
			status = sqlparser_mysql_append_pg_param(&out, state->positional_param_count, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		} else {
			status = sqlparser_mysql_buffer_append_char(&out, c, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_mysql_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	char quote;
	size_t pos;

	if (sql == NULL) {
		return index;
	}

	quote = sql[index];
	if (quote == '\'' || quote == '"' || quote == '`') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}

	if (sql[index] == '-' && sql[index + 1U] == '-') {
		pos = index + 2U;
		while (sql[pos] != '\0') {
			if (sql[pos] == '\n') {
				return pos + 1U;
			}
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

static sqlparser_status_t sqlparser_mysql_mask_non_code(
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
		} else if (masked[index] == '"') {
			index++;
			while (index < len) {
				if (masked[index] == '"' && masked[index + 1U] == '"') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index += 2U;
					continue;
				}
				if (masked[index] == '"') {
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

static int sqlparser_mysql_contains_phrase(const char *masked, const char *phrase)
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

	needs_left_boundary = sqlparser_mysql_is_ident_char((unsigned char)phrase[0]);
	needs_right_boundary = sqlparser_mysql_is_ident_char((unsigned char)phrase[last_phrase_pos - 1U]);

	for (pos = 0U; masked[pos] != '\0'; pos++) {
		size_t text_pos;
		size_t phrase_pos;
		int matched;

		if (needs_left_boundary && pos > 0U &&
		    sqlparser_mysql_is_ident_char((unsigned char)masked[pos - 1U])) {
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
		    (!needs_right_boundary || !sqlparser_mysql_is_ident_char((unsigned char)masked[text_pos]))) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_mysql_word_at(const char *masked, size_t pos, const char *word)
{
	size_t index;

	if (masked == NULL || word == NULL || word[0] == '\0') {
		return 0;
	}
	if (pos > 0U && sqlparser_mysql_is_ident_char((unsigned char)masked[pos - 1U])) {
		return 0;
	}
	for (index = 0U; word[index] != '\0'; index++) {
		if (masked[pos + index] != word[index]) {
			return 0;
		}
	}
	return !sqlparser_mysql_is_ident_char((unsigned char)masked[pos + index]);
}

static int sqlparser_mysql_first_top_level_word_is(const char *masked, const char *word)
{
	size_t pos;

	if (masked == NULL || word == NULL) {
		return 0;
	}
	for (pos = 0U; masked[pos] != '\0'; pos++) {
		if (!isspace((unsigned char)masked[pos])) {
			return sqlparser_mysql_word_at(masked, pos, word);
		}
	}
	return 0;
}

static int sqlparser_mysql_top_level_word_before(
	const char *masked,
	const char *word,
	const char *before_word)
{
	size_t pos;
	int depth;

	if (masked == NULL || word == NULL) {
		return 0;
	}

	depth = 0;
	for (pos = 0U; masked[pos] != '\0'; pos++) {
		if (masked[pos] == '(') {
			depth++;
			continue;
		}
		if (masked[pos] == ')') {
			if (depth > 0) {
				depth--;
			}
			continue;
		}
		if (depth != 0) {
			continue;
		}
		if (before_word != NULL && sqlparser_mysql_word_at(masked, pos, before_word)) {
			return 0;
		}
		if (sqlparser_mysql_word_at(masked, pos, word)) {
			return 1;
		}
	}
	return 0;
}

static size_t sqlparser_mysql_find_top_level_word_between(
	const char *masked,
	const char *word,
	size_t start,
	size_t end)
{
	size_t pos;
	int depth;

	if (masked == NULL || word == NULL || word[0] == '\0') {
		return (size_t)-1;
	}
	depth = 0;
	for (pos = start; pos < end && masked[pos] != '\0'; pos++) {
		if (masked[pos] == '(') {
			depth++;
			continue;
		}
		if (masked[pos] == ')') {
			if (depth > 0) {
				depth--;
			}
			continue;
		}
		if (depth == 0 && sqlparser_mysql_word_at(masked, pos, word)) {
			return pos;
		}
	}
	return (size_t)-1;
}

static size_t sqlparser_mysql_find_top_level_char_between(
	const char *sql,
	char needle,
	size_t start,
	size_t end)
{
	size_t pos;
	int depth;

	if (sql == NULL) {
		return (size_t)-1;
	}
	depth = 0;
	for (pos = start; pos < end && sql[pos] != '\0'; pos++) {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped - 1U;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth > 0) {
				depth--;
			}
			continue;
		}
		if (depth == 0 && sql[pos] == needle) {
			return pos;
		}
	}
	return (size_t)-1;
}

static int sqlparser_mysql_is_unsupported_update_join(const char *masked)
{
	return sqlparser_mysql_first_top_level_word_is(masked, "update") &&
		sqlparser_mysql_top_level_word_before(masked, "join", "set");
}

static int sqlparser_mysql_is_unsupported_delete_join(const char *masked)
{
	return sqlparser_mysql_first_top_level_word_is(masked, "delete") &&
		sqlparser_mysql_top_level_word_before(masked, "join", "where");
}

static int sqlparser_mysql_raw_contains_word_span(const char *sql, const char *word, size_t word_len)
{
	size_t pos;

	if (sql == NULL || word == NULL || word_len == 0U) {
		return 0;
	}

	for (pos = 0U; sql[pos] != '\0'; pos++) {
		size_t index;

		if (pos > 0U && sqlparser_mysql_is_ident_char((unsigned char)sql[pos - 1U])) {
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
		    !sqlparser_mysql_is_ident_char((unsigned char)sql[pos + word_len])) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_mysql_raw_may_contain_phrase(const char *sql, const char *phrase)
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
		       !sqlparser_mysql_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		start = pos;
		while (phrase[pos] != '\0' &&
		       sqlparser_mysql_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		len = pos - start;
		if (len == 0U) {
			continue;
		}

		saw_token = 1;
		if (!sqlparser_mysql_raw_contains_word_span(sql, phrase + start, len)) {
			return 0;
		}
	}

	return saw_token;
}

static sqlparser_status_t sqlparser_mysql_reject_unsupported(
	const char *sql,
	sqlparser_error_t *out_error)
{
	static const char *const unsupported_phrases[] = {
		"insert ignore",
		"insert delayed",
		"insert low_priority",
		"insert high_priority",
		"on duplicate key update",
		"replace into",
		"update join",
		"update ignore",
		"delete join",
		"delete ignore",
		"auto_increment",
		"unsigned",
		"zerofill",
		"engine =",
		"engine=",
		"charset =",
		"charset=",
		"character set =",
		"character set=",
		"collate =",
		"collate="
	};
	char *masked;
	sqlparser_status_t status;
	size_t index;
	int needs_mask;

	needs_mask = 0;
	for (index = 0U; index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_mysql_raw_may_contain_phrase(sql, unsupported_phrases[index])) {
			needs_mask = 1;
			break;
		}
	}
	if (!needs_mask) {
		return SQLPARSER_STATUS_OK;
	}

	masked = NULL;
	status = sqlparser_mysql_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_mysql_is_unsupported_update_join(masked)) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported MySQL syntax: update join");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_mysql_is_unsupported_delete_join(masked)) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported MySQL syntax: delete join");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	for (index = 0U; index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_mysql_contains_phrase(masked, unsupported_phrases[index])) {
			char message[256];

			(void)snprintf(
				message,
				sizeof(message),
				"unsupported MySQL syntax: %s",
				unsupported_phrases[index]);
			free(masked);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, message);
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}

	free(masked);
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_copy_single_quoted_or_comment(
	const char *sql,
	size_t *index,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;

	quote = sql[*index];
	if (quote == '\'') {
		pos = *index;
		if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		pos++;
		while (sql[pos] != '\0') {
			if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					pos++;
					if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
						return -1;
					}
				} else {
					pos++;
					break;
				}
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	if (sql[*index] == '-' && sql[*index + 1U] == '-') {
		pos = *index;
		while (sql[pos] != '\0') {
			if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == '\n') {
				pos++;
				break;
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	if (sql[*index] == '/' && sql[*index + 1U] == '*') {
		pos = *index;
		while (sql[pos] != '\0') {
			if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				pos++;
				if (sqlparser_mysql_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
					return -1;
				}
				pos++;
				break;
			}
			pos++;
		}
		*index = pos;
		return 1;
	}

	return 0;
}

static const char *sqlparser_mysql_trim_left(const char *start, const char *end)
{
	while (start < end && isspace((unsigned char)*start)) {
		start++;
	}
	return start;
}

static const char *sqlparser_mysql_trim_right(const char *start, const char *end)
{
	while (end > start && isspace((unsigned char)*(end - 1))) {
		end--;
	}
	return end;
}

static size_t sqlparser_mysql_skip_space(const char *text, size_t pos)
{
	while (isspace((unsigned char)text[pos])) {
		pos++;
	}
	return pos;
}

static int sqlparser_mysql_span_has_space(const char *start, const char *end)
{
	const char *pos;

	for (pos = start; pos < end; pos++) {
		if (isspace((unsigned char)*pos)) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_mysql_append_pg_quoted_identifier(
	sqlparser_mysql_buffer_t *out,
	const char *start,
	const char *end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (*pos == '"') {
			status = sqlparser_mysql_buffer_append_cstr(out, "\"\"", out_error);
		} else {
			status = sqlparser_mysql_buffer_append_char(out, *pos, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_mysql_buffer_append_char(out, '"', out_error);
}

static sqlparser_status_t sqlparser_mysql_append_internal_string_literal(
	sqlparser_mysql_buffer_t *out,
	const char *start,
	const char *end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (*pos == '\'') {
			status = sqlparser_mysql_buffer_append_cstr(out, "''", out_error);
		} else {
			status = sqlparser_mysql_buffer_append_char(out, *pos, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_mysql_buffer_append_char(out, '\'', out_error);
}

static sqlparser_status_t sqlparser_mysql_append_internal_set(
	sqlparser_mysql_buffer_t *out,
	const char *internal_name,
	const char *arg0_start,
	const char *arg0_end,
	const char *arg1_start,
	const char *arg1_end,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_cstr(out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(out, internal_name, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_internal_string_literal(out, arg0_start, arg0_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && arg1_start != NULL && arg1_end != NULL) {
		status = sqlparser_mysql_buffer_append_cstr(out, ", ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_append_internal_string_literal(out, arg1_start, arg1_end, out_error);
		}
	}
	return status;
}

static size_t sqlparser_mysql_statement_token_end(const char *sql, size_t pos, size_t end)
{
	if (pos < end && sql[pos] == '`') {
		pos++;
		while (pos < end) {
			if (sql[pos] == '`') {
				if (pos + 1U < end && sql[pos + 1U] == '`') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return end;
	}
	while (pos < end && !isspace((unsigned char)sql[pos]) && sql[pos] != ';' && sql[pos] != ',') {
		pos++;
	}
	return pos;
}

static sqlparser_status_t sqlparser_mysql_preprocess_use_statement(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *start;
	const char *end;
	const char *name_start;
	const char *name_end;
	char *quoted_name;
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

	start = input_sql;
	end = input_sql + strlen(input_sql);
	start = sqlparser_mysql_trim_left(start, end);
	end = sqlparser_mysql_trim_right(start, end);
	if (end > start && *(end - 1) == ';') {
		end--;
		end = sqlparser_mysql_trim_right(start, end);
	}
	if ((size_t)(end - start) < strlen("use") ||
	    !sqlparser_mysql_ascii_word_equal(start, 0U, "use")) {
		return SQLPARSER_STATUS_OK;
	}
	name_start = start + strlen("use");
	if (name_start >= end || !isspace((unsigned char)*name_start)) {
		return SQLPARSER_STATUS_OK;
	}
	name_start = sqlparser_mysql_trim_left(name_start, end);
	name_end = sqlparser_mysql_trim_right(name_start, end);
	if (name_start >= name_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE requires a database name");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, SQLPARSER_INTERNAL_CURRENT_DATABASE, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " = ", out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}

	quoted_name = NULL;
	if (*name_start == '`') {
		char *slice;

		slice = sqlparser_strndup(name_start, (size_t)(name_end - name_start));
		if (slice == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		status = sqlparser_mysql_preprocess_quotes(slice, NULL, &quoted_name, out_error);
		free(slice);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, quoted_name, out_error);
		}
		free(quoted_name);
	} else if (*name_start == '"') {
		const char *pos;

		pos = name_start + 1;
		while (pos < name_end) {
			if (*pos == '"' && pos + 1 < name_end && *(pos + 1) == '"') {
				pos += 2;
				continue;
			}
			if (*pos == '"') {
				pos++;
				break;
			}
			pos++;
		}
		if (pos != name_end) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE database name has trailing text");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_mysql_buffer_append_mem(&out, name_start, (size_t)(name_end - name_start), out_error);
	} else {
		if (sqlparser_mysql_span_has_space(name_start, name_end)) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "USE database name must be quoted when it contains whitespace");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_mysql_append_pg_quoted_identifier(&out, name_start, name_end, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_preprocess_prepared_statement(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *start;
	const char *end;
	const char *name_start;
	const char *name_end;
	const char *value_start;
	const char *value_end;
	size_t pos;
	const char *internal_name;
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

	start = input_sql;
	end = input_sql + strlen(input_sql);
	start = sqlparser_mysql_trim_left(start, end);
	end = sqlparser_mysql_trim_right(start, end);
	if (end > start && *(end - 1) == ';') {
		end--;
		end = sqlparser_mysql_trim_right(start, end);
	}
	if (start >= end) {
		return SQLPARSER_STATUS_OK;
	}

	internal_name = NULL;
	value_start = NULL;
	value_end = NULL;
	pos = 0U;
	if (sqlparser_mysql_ascii_word_equal(start, pos, "prepare")) {
		pos = sqlparser_mysql_skip_space(start, pos + strlen("prepare"));
		name_start = start + pos;
		name_end = start + sqlparser_mysql_statement_token_end(start, pos, (size_t)(end - start));
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "PREPARE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_mysql_skip_space(start, (size_t)(name_end - start));
		if (!sqlparser_mysql_ascii_word_equal(start, pos, "from")) {
			return SQLPARSER_STATUS_OK;
		}
		value_start = start + sqlparser_mysql_skip_space(start, pos + strlen("from"));
		value_end = sqlparser_mysql_trim_right(value_start, end);
		if (value_start >= value_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "PREPARE FROM requires a SQL source");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		internal_name = SQLPARSER_INTERNAL_MYSQL_PREPARE;
	} else if (sqlparser_mysql_ascii_word_equal(start, pos, "execute")) {
		pos = sqlparser_mysql_skip_space(start, pos + strlen("execute"));
		name_start = start + pos;
		name_end = start + sqlparser_mysql_statement_token_end(start, pos, (size_t)(end - start));
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXECUTE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_mysql_skip_space(start, (size_t)(name_end - start));
		if (pos < (size_t)(end - start)) {
			if (!sqlparser_mysql_ascii_word_equal(start, pos, "using")) {
				return SQLPARSER_STATUS_OK;
			}
			value_start = start + sqlparser_mysql_skip_space(start, pos + strlen("using"));
			value_end = sqlparser_mysql_trim_right(value_start, end);
			if (value_start >= value_end) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXECUTE USING requires parameters");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
		}
		internal_name = SQLPARSER_INTERNAL_MYSQL_EXECUTE;
	} else if (sqlparser_mysql_ascii_word_equal(start, pos, "deallocate") ||
	           sqlparser_mysql_ascii_word_equal(start, pos, "drop")) {
		int is_drop;

		is_drop = sqlparser_mysql_ascii_word_equal(start, pos, "drop");
		pos += is_drop ? strlen("drop") : strlen("deallocate");
		pos = sqlparser_mysql_skip_space(start, pos);
		if (!sqlparser_mysql_ascii_word_equal(start, pos, "prepare")) {
			return SQLPARSER_STATUS_OK;
		}
		pos = sqlparser_mysql_skip_space(start, pos + strlen("prepare"));
		name_start = start + pos;
		name_end = start + sqlparser_mysql_statement_token_end(start, pos, (size_t)(end - start));
		if (name_start >= name_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "DEALLOCATE PREPARE requires a statement name");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_mysql_skip_space(start, (size_t)(name_end - start));
		if (pos < (size_t)(end - start)) {
			return SQLPARSER_STATUS_OK;
		}
		internal_name = is_drop ? SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE : SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE;
	} else {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_append_internal_set(
		&out,
		internal_name,
		name_start,
		name_end,
		value_start,
		value_end,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_mysql_statement_end(const char *sql, size_t start)
{
	size_t index;
	size_t skipped;

	index = start;
	while (sql[index] != '\0') {
		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
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

static sqlparser_status_t sqlparser_mysql_rewrite_use_statements(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
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
		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_mysql_preprocess_use_statement(statement_sql, &rewritten_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_mysql_preprocess_prepared_statement(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = (size_t)(sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end) - sql);
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
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
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_simple_limit_operand(const char *start, const char *end)
{
	const char *pos;

	while (start < end && isspace((unsigned char)*start)) {
		start++;
	}
	while (end > start && isspace((unsigned char)*(end - 1))) {
		end--;
	}
	if (start >= end) {
		return 0;
	}

	if (*start == '?') {
		return start + 1 == end;
	}

	if (*start == '$') {
		pos = start + 1;
		if (pos >= end || !isdigit((unsigned char)*pos)) {
			return 0;
		}
		while (pos < end && isdigit((unsigned char)*pos)) {
			pos++;
		}
		return pos == end;
	}

	pos = start;
	while (pos < end && isdigit((unsigned char)*pos)) {
		pos++;
	}

	return pos == end;
}

static sqlparser_status_t sqlparser_mysql_rewrite_limit_offset_count(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	sqlparser_status_t status;
	size_t index;
	size_t copy_start;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	rewritten = 0;
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}

		if (sqlparser_mysql_ascii_word_equal(sql, index, "limit")) {
			size_t first_start_pos;
			size_t comma_pos;
			size_t second_start_pos;
			size_t second_end_pos;
			const char *first_start;
			const char *first_end;
			const char *second_start;
			const char *second_end;

			first_start_pos = index + 5U;
			while (isspace((unsigned char)sql[first_start_pos])) {
				first_start_pos++;
			}
			comma_pos = first_start_pos;
			while (sql[comma_pos] != '\0' && sql[comma_pos] != ',' && sql[comma_pos] != ';') {
				comma_pos++;
			}
			if (sql[comma_pos] != ',') {
				index++;
				continue;
			}
			second_start_pos = comma_pos + 1U;
			second_end_pos = second_start_pos;
			while (sql[second_end_pos] != '\0' && sql[second_end_pos] != ';') {
				second_end_pos++;
			}

			first_start = sqlparser_mysql_trim_left(sql + first_start_pos, sql + comma_pos);
			first_end = sqlparser_mysql_trim_right(first_start, sql + comma_pos);
			second_start = sqlparser_mysql_trim_left(sql + second_start_pos, sql + second_end_pos);
			second_end = sqlparser_mysql_trim_right(second_start, sql + second_end_pos);
			if (!sqlparser_mysql_simple_limit_operand(first_start, first_end) ||
			    !sqlparser_mysql_simple_limit_operand(second_start, second_end)) {
				index++;
				continue;
			}

			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, "LIMIT ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					second_start,
					(size_t)(second_end - second_start),
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, " OFFSET ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					first_start,
					(size_t)(first_end - first_start),
					out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			index = second_end_pos;
			copy_start = index;
			continue;
		}

		index++;
	}

	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static int sqlparser_mysql_find_on_duplicate_key_update(
	const char *masked,
	size_t start,
	size_t end,
	size_t *out_tail_start)
{
	size_t pos;
	size_t next;

	if (out_tail_start != NULL) {
		*out_tail_start = 0U;
	}
	pos = start;
	while (pos < end) {
		pos = sqlparser_mysql_find_top_level_word_between(masked, "on", pos, end);
		if (pos == (size_t)-1) {
			return 0;
		}
		next = sqlparser_mysql_skip_space(masked, pos + strlen("on"));
		if (next < end && sqlparser_mysql_word_at(masked, next, "duplicate")) {
			next = sqlparser_mysql_skip_space(masked, next + strlen("duplicate"));
			if (next < end && sqlparser_mysql_word_at(masked, next, "key")) {
				next = sqlparser_mysql_skip_space(masked, next + strlen("key"));
				if (next < end && sqlparser_mysql_word_at(masked, next, "update")) {
					if (out_tail_start != NULL) {
						*out_tail_start = next + strlen("update");
					}
					return 1;
				}
			}
		}
		pos += strlen("on");
	}
	return 0;
}

static int sqlparser_mysql_extract_alias_span(
	const char *start,
	const char *end,
	const char **out_alias_start,
	const char **out_alias_end)
{
	const char *alias_start;

	if (out_alias_start != NULL) {
		*out_alias_start = NULL;
	}
	if (out_alias_end != NULL) {
		*out_alias_end = NULL;
	}
	start = sqlparser_mysql_trim_left(start, end);
	end = sqlparser_mysql_trim_right(start, end);
	if (start >= end) {
		return 0;
	}
	alias_start = end;
	while (alias_start > start && !isspace((unsigned char)*(alias_start - 1))) {
		alias_start--;
	}
	if (alias_start <= start) {
		return 0;
	}
	if (out_alias_start != NULL) {
		*out_alias_start = alias_start;
	}
	if (out_alias_end != NULL) {
		*out_alias_end = end;
	}
	return 1;
}

static int sqlparser_mysql_span_equal(
	const char *left_start,
	const char *left_end,
	const char *right_start,
	const char *right_end)
{
	size_t left_len;
	size_t right_len;

	if (left_start == NULL || left_end == NULL || right_start == NULL || right_end == NULL) {
		return 0;
	}
	left_len = (size_t)(left_end - left_start);
	right_len = (size_t)(right_end - right_start);
	return left_len == right_len && strncmp(left_start, right_start, left_len) == 0;
}

static int sqlparser_mysql_extract_relation_name_span(
	const char *relation_start,
	const char *relation_end,
	const char *alias_start,
	const char **out_name_start,
	const char **out_name_end)
{
	const char *name_start;
	const char *name_end;
	size_t search_start;
	size_t relation_len;
	size_t last_dot;

	if (out_name_start != NULL) {
		*out_name_start = NULL;
	}
	if (out_name_end != NULL) {
		*out_name_end = NULL;
	}
	if (relation_start == NULL || relation_end == NULL || relation_start >= relation_end) {
		return 0;
	}
	name_start = sqlparser_mysql_trim_left(relation_start, relation_end);
	name_end = alias_start != NULL && alias_start > name_start ? alias_start : relation_end;
	name_end = sqlparser_mysql_trim_right(name_start, name_end);
	if (name_start >= name_end) {
		return 0;
	}
	relation_len = (size_t)(name_end - name_start);
	search_start = 0U;
	last_dot = (size_t)-1;
	while (search_start < relation_len) {
		size_t dot_offset;

		dot_offset = sqlparser_mysql_find_top_level_char_between(name_start, '.', search_start, relation_len);
		if (dot_offset == (size_t)-1) {
			break;
		}
		last_dot = dot_offset;
		search_start = dot_offset + 1U;
	}
	if (last_dot != (size_t)-1) {
		name_start = sqlparser_mysql_trim_left(name_start + last_dot + 1U, name_end);
		name_end = sqlparser_mysql_trim_right(name_start, name_end);
	}
	if (name_start >= name_end) {
		return 0;
	}
	if (out_name_start != NULL) {
		*out_name_start = name_start;
	}
	if (out_name_end != NULL) {
		*out_name_end = name_end;
	}
	return 1;
}

static int sqlparser_mysql_span_matches_relation_qualifier(
	const char *qualifier_start,
	const char *qualifier_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end)
{
	if (qualifier_start == NULL || qualifier_end == NULL || qualifier_start >= qualifier_end) {
		return 0;
	}
	if (alias_start != NULL &&
	    alias_end != NULL &&
	    sqlparser_mysql_span_equal(qualifier_start, qualifier_end, alias_start, alias_end)) {
		return 1;
	}
	return relation_name_start != NULL &&
		relation_name_end != NULL &&
		sqlparser_mysql_span_equal(qualifier_start, qualifier_end, relation_name_start, relation_name_end);
}

static int sqlparser_mysql_span_ci_equals_cstr(const char *start, const char *end, const char *word)
{
	size_t index;
	size_t len;

	if (start == NULL || end == NULL || word == NULL) {
		return 0;
	}
	len = strlen(word);
	if ((size_t)(end - start) != len) {
		return 0;
	}
	for (index = 0U; index < len; index++) {
		if (tolower((unsigned char)start[index]) != tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_mysql_previous_token_span(
	const char *start,
	const char *end,
	const char **out_token_start,
	const char **out_token_end)
{
	const char *token_end;
	const char *token_start;

	if (out_token_start != NULL) {
		*out_token_start = NULL;
	}
	if (out_token_end != NULL) {
		*out_token_end = NULL;
	}
	if (start == NULL || end == NULL || start >= end) {
		return 0;
	}
	token_end = sqlparser_mysql_trim_right(start, end);
	if (token_end <= start) {
		return 0;
	}
	token_start = token_end;
	while (token_start > start && sqlparser_mysql_is_ident_char((unsigned char)*(token_start - 1))) {
		token_start--;
	}
	if (token_start == token_end) {
		return 0;
	}
	if (out_token_start != NULL) {
		*out_token_start = token_start;
	}
	if (out_token_end != NULL) {
		*out_token_end = token_end;
	}
	return 1;
}

static int sqlparser_mysql_normalize_inner_join_target_end(
	const char *start,
	const char **io_end)
{
	const char *token_start;
	const char *token_end;

	if (io_end == NULL || *io_end == NULL) {
		return 0;
	}
	if (!sqlparser_mysql_previous_token_span(start, *io_end, &token_start, &token_end)) {
		return 1;
	}
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "inner") ||
	    sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "cross")) {
		*io_end = sqlparser_mysql_trim_right(start, token_start);
		return 1;
	}
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "left") ||
	    sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "right")) {
		return 0;
	}
	if (sqlparser_mysql_span_ci_equals_cstr(token_start, token_end, "outer")) {
		const char *prev_start;
		const char *prev_end;

		if (sqlparser_mysql_previous_token_span(start, token_start, &prev_start, &prev_end) &&
		    (sqlparser_mysql_span_ci_equals_cstr(prev_start, prev_end, "left") ||
		     sqlparser_mysql_span_ci_equals_cstr(prev_start, prev_end, "right"))) {
			return 0;
		}
	}
	return 1;
}

static const char *sqlparser_mysql_strip_assignment_qualifier(
	const char *left_start,
	const char *left_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end)
{
	size_t dot_offset;
	const char *qualifier_start;
	const char *qualifier_end;

	if (left_start == NULL || left_end == NULL || left_start >= left_end) {
		return left_start;
	}
	dot_offset = sqlparser_mysql_find_top_level_char_between(
		left_start,
		'.',
		0U,
		(size_t)(left_end - left_start));
	if (dot_offset == (size_t)-1) {
		return left_start;
	}
	qualifier_start = sqlparser_mysql_trim_left(left_start, left_start + dot_offset);
	qualifier_end = sqlparser_mysql_trim_right(qualifier_start, left_start + dot_offset);
	if (!sqlparser_mysql_span_matches_relation_qualifier(
		    qualifier_start,
		    qualifier_end,
		    alias_start,
		    alias_end,
		    relation_name_start,
		    relation_name_end)) {
		return left_start;
	}
	return sqlparser_mysql_trim_left(left_start + dot_offset + 1U, left_end);
}

static sqlparser_status_t sqlparser_mysql_validate_update_join_assignment_target(
	const char *left_start,
	const char *left_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end,
	sqlparser_error_t *out_error)
{
	size_t dot_offset;
	const char *qualifier_start;
	const char *qualifier_end;

	if (left_start == NULL || left_end == NULL || left_start >= left_end) {
		return SQLPARSER_STATUS_OK;
	}
	dot_offset = sqlparser_mysql_find_top_level_char_between(
		left_start,
		'.',
		0U,
		(size_t)(left_end - left_start));
	if (dot_offset == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	qualifier_start = sqlparser_mysql_trim_left(left_start, left_start + dot_offset);
	qualifier_end = sqlparser_mysql_trim_right(qualifier_start, left_start + dot_offset);
	if (sqlparser_mysql_span_matches_relation_qualifier(
		    qualifier_start,
		    qualifier_end,
		    alias_start,
		    alias_end,
		    relation_name_start,
		    relation_name_end)) {
		return SQLPARSER_STATUS_OK;
	}
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_UNSUPPORTED,
		"unsupported MySQL syntax: update join assignment target");
	return SQLPARSER_STATUS_UNSUPPORTED;
}

static sqlparser_status_t sqlparser_mysql_append_update_join_assignments(
	sqlparser_mysql_buffer_t *out,
	const char *assign_start,
	const char *assign_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end,
	sqlparser_error_t *out_error)
{
	const char *segment_start;
	int first;

	segment_start = assign_start;
	first = 1;
	while (segment_start < assign_end) {
		size_t segment_offset;
		size_t eq_offset;
		const char *segment_end;
		const char *left_start;
		const char *left_end;
		const char *right_start;
		const char *right_end;
		sqlparser_status_t status;

		segment_offset = sqlparser_mysql_find_top_level_char_between(
			assign_start,
			',',
			(size_t)(segment_start - assign_start),
			(size_t)(assign_end - assign_start));
		segment_end = segment_offset == (size_t)-1 ? assign_end : assign_start + segment_offset;
		eq_offset = sqlparser_mysql_find_top_level_char_between(
			segment_start,
			'=',
			0U,
			(size_t)(segment_end - segment_start));
		if (eq_offset == (size_t)-1) {
			left_start = sqlparser_mysql_trim_left(segment_start, segment_end);
			left_end = sqlparser_mysql_trim_right(left_start, segment_end);
			right_start = NULL;
			right_end = NULL;
		} else {
			left_start = sqlparser_mysql_trim_left(segment_start, segment_start + eq_offset);
			left_end = sqlparser_mysql_trim_right(left_start, segment_start + eq_offset);
			right_start = sqlparser_mysql_trim_left(segment_start + eq_offset + 1U, segment_end);
			right_end = sqlparser_mysql_trim_right(right_start, segment_end);
			status = sqlparser_mysql_validate_update_join_assignment_target(
				left_start,
				left_end,
				alias_start,
				alias_end,
				relation_name_start,
				relation_name_end,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			left_start = sqlparser_mysql_strip_assignment_qualifier(
				left_start,
				left_end,
				alias_start,
				alias_end,
				relation_name_start,
				relation_name_end);
		}
		if (!first) {
			status = sqlparser_mysql_buffer_append_cstr(out, ", ", out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		first = 0;
		status = sqlparser_mysql_buffer_append_mem(out, left_start, (size_t)(left_end - left_start), out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		if (right_start != NULL) {
			status = sqlparser_mysql_buffer_append_cstr(out, " = ", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(out, right_start, (size_t)(right_end - right_start), out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		segment_start = segment_offset == (size_t)-1 ? assign_end : segment_end + 1;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_validate_delete_join_target(
	const char *delete_target_start,
	const char *delete_target_end,
	const char *alias_start,
	const char *alias_end,
	const char *relation_name_start,
	const char *relation_name_end,
	sqlparser_error_t *out_error)
{
	const char *target_start;
	const char *target_end;

	if (delete_target_start == NULL || delete_target_end == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	target_start = sqlparser_mysql_trim_left(delete_target_start, delete_target_end);
	target_end = sqlparser_mysql_trim_right(target_start, delete_target_end);
	if (target_start >= target_end) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_mysql_find_top_level_char_between(
		    target_start,
		    ',',
		    0U,
		    (size_t)(target_end - target_start)) != (size_t)-1) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported MySQL syntax: delete join multiple targets");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (sqlparser_mysql_span_matches_relation_qualifier(
		    target_start,
		    target_end,
		    alias_start,
		    alias_end,
		    relation_name_start,
		    relation_name_end)) {
		return SQLPARSER_STATUS_OK;
	}
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_UNSUPPORTED,
		"unsupported MySQL syntax: delete join target");
	return SQLPARSER_STATUS_UNSUPPORTED;
}

static sqlparser_status_t sqlparser_mysql_append_join_on_call(
	sqlparser_mysql_buffer_t *out,
	const char *join_start,
	const char *join_end,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_cstr(out, SQLPARSER_INTERNAL_MYSQL_JOIN_ON, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_char(out, '(', out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(out, join_start, (size_t)(join_end - join_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_char(out, ')', out_error);
	}
	return status;
}

static int sqlparser_mysql_unwrap_join_on_call(
	const char **io_start,
	const char **io_end)
{
	const char *start;
	const char *end;
	const char *cursor;
	const char *lparen;
	const char *inner_start;
	const char *inner_end;
	size_t prefix_len;
	int depth;

	if (io_start == NULL || io_end == NULL || *io_start == NULL || *io_end == NULL) {
		return 0;
	}
	start = sqlparser_mysql_trim_left(*io_start, *io_end);
	end = sqlparser_mysql_trim_right(start, *io_end);
	prefix_len = strlen(SQLPARSER_INTERNAL_MYSQL_JOIN_ON);
	if ((size_t)(end - start) < prefix_len + 2U ||
	    !sqlparser_mysql_span_ci_equals_cstr(start, start + prefix_len, SQLPARSER_INTERNAL_MYSQL_JOIN_ON)) {
		return 0;
	}
	cursor = start + prefix_len;
	while (cursor < end && isspace((unsigned char)*cursor)) {
		cursor++;
	}
	if (cursor >= end || *cursor != '(') {
		return 0;
	}
	lparen = cursor;
	depth = 0;
	while (cursor < end) {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(start, (size_t)(cursor - start));
		if (skipped > (size_t)(cursor - start)) {
			cursor = start + skipped;
			continue;
		}
		if (*cursor == '(') {
			depth++;
		} else if (*cursor == ')') {
			depth--;
			if (depth == 0) {
				if (cursor != end - 1) {
					return 0;
				}
				inner_start = sqlparser_mysql_trim_left(lparen + 1, cursor);
				inner_end = sqlparser_mysql_trim_right(inner_start, cursor);
				if (inner_start >= inner_end) {
					return 0;
				}
				*io_start = inner_start;
				*io_end = inner_end;
				return 1;
			}
			if (depth < 0) {
				return 0;
			}
		}
		cursor++;
	}
	return 0;
}

static sqlparser_status_t sqlparser_mysql_rewrite_on_duplicate_statement(
	const char *statement_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t tail_start;
	size_t on_pos;
	size_t end_pos;
	const char *tail;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	end_pos = strlen(statement_sql);
	if (!sqlparser_mysql_find_on_duplicate_key_update(masked, 0U, end_pos, &tail_start)) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	on_pos = sqlparser_mysql_find_top_level_word_between(masked, "on", 0U, end_pos);
	free(masked);
	if (on_pos == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	tail = sqlparser_mysql_trim_left(statement_sql + tail_start, statement_sql + end_pos);
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, on_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(
			&out,
			"ON CONFLICT ON CONSTRAINT sqlparser_mysql_duplicate_key DO UPDATE SET ",
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, tail, (size_t)((statement_sql + end_pos) - tail), out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_update_join_statement(
	const char *statement_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t start_pos;
	size_t target_start;
	size_t join_pos;
	size_t on_pos;
	size_t set_pos;
	size_t where_pos;
	const char *end;
	const char *target_start_ptr;
	const char *target_end_ptr;
	const char *source_start_ptr;
	const char *source_end_ptr;
	const char *join_start_ptr;
	const char *join_end_ptr;
	const char *assign_start_ptr;
	const char *assign_end_ptr;
	const char *where_start_ptr;
	const char *where_end_ptr;
	const char *alias_start;
	const char *alias_end;
	const char *relation_name_start;
	const char *relation_name_end;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	len = strlen(statement_sql);
	start_pos = (size_t)(sqlparser_mysql_trim_left(statement_sql, statement_sql + len) - statement_sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "update")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	target_start = sqlparser_mysql_skip_space(masked, start_pos + strlen("update"));
	set_pos = sqlparser_mysql_find_top_level_word_between(masked, "set", target_start, len);
	join_pos = set_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "join", target_start, set_pos);
	on_pos = join_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "on", join_pos + strlen("join"), set_pos);
	where_pos = set_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "where", set_pos + strlen("set"), len);
	free(masked);
	if (set_pos == (size_t)-1 || join_pos == (size_t)-1 || on_pos == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	end = sqlparser_mysql_trim_right(statement_sql, statement_sql + len);
	target_start_ptr = sqlparser_mysql_trim_left(statement_sql + target_start, statement_sql + join_pos);
	target_end_ptr = sqlparser_mysql_trim_right(target_start_ptr, statement_sql + join_pos);
	if (!sqlparser_mysql_normalize_inner_join_target_end(target_start_ptr, &target_end_ptr)) {
		return SQLPARSER_STATUS_OK;
	}
	source_start_ptr = sqlparser_mysql_trim_left(statement_sql + join_pos + strlen("join"), statement_sql + on_pos);
	source_end_ptr = sqlparser_mysql_trim_right(source_start_ptr, statement_sql + on_pos);
	join_start_ptr = sqlparser_mysql_trim_left(statement_sql + on_pos + strlen("on"), statement_sql + set_pos);
	join_end_ptr = sqlparser_mysql_trim_right(join_start_ptr, statement_sql + set_pos);
	assign_start_ptr = sqlparser_mysql_trim_left(statement_sql + set_pos + strlen("set"), where_pos == (size_t)-1 ? end : statement_sql + where_pos);
	assign_end_ptr = sqlparser_mysql_trim_right(assign_start_ptr, where_pos == (size_t)-1 ? end : statement_sql + where_pos);
	where_start_ptr = where_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(statement_sql + where_pos + strlen("where"), end);
	where_end_ptr = where_start_ptr == NULL ? NULL : sqlparser_mysql_trim_right(where_start_ptr, end);
	if (target_start_ptr >= target_end_ptr || source_start_ptr >= source_end_ptr ||
	    join_start_ptr >= join_end_ptr || assign_start_ptr >= assign_end_ptr) {
		return SQLPARSER_STATUS_OK;
	}
	alias_start = NULL;
	alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(target_start_ptr, target_end_ptr, &alias_start, &alias_end);
	relation_name_start = NULL;
	relation_name_end = NULL;
	(void)sqlparser_mysql_extract_relation_name_span(
		target_start_ptr,
		target_end_ptr,
		alias_start,
		&relation_name_start,
		&relation_name_end);
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "UPDATE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, target_start_ptr, (size_t)(target_end_ptr - target_start_ptr), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " SET ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_update_join_assignments(
			&out,
			assign_start_ptr,
			assign_end_ptr,
			alias_start,
			alias_end,
			relation_name_start,
			relation_name_end,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " FROM ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, source_start_ptr, (size_t)(source_end_ptr - source_start_ptr), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " WHERE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_join_on_call(&out, join_start_ptr, join_end_ptr, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && where_start_ptr != NULL && where_start_ptr < where_end_ptr) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " AND ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, where_start_ptr, (size_t)(where_end_ptr - where_start_ptr), out_error);
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_delete_join_statement(
	const char *statement_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *masked;
	size_t len;
	size_t start_pos;
	size_t from_pos;
	size_t join_pos;
	size_t on_pos;
	size_t where_pos;
	const char *end;
	const char *target_start_ptr;
	const char *target_end_ptr;
	const char *source_start_ptr;
	const char *source_end_ptr;
	const char *join_start_ptr;
	const char *join_end_ptr;
	const char *delete_target_start_ptr;
	const char *delete_target_end_ptr;
	const char *where_start_ptr;
	const char *where_end_ptr;
	const char *alias_start;
	const char *alias_end;
	const char *relation_name_start;
	const char *relation_name_end;
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;

	*out_sql = NULL;
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(statement_sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	len = strlen(statement_sql);
	start_pos = (size_t)(sqlparser_mysql_trim_left(statement_sql, statement_sql + len) - statement_sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "delete")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	from_pos = sqlparser_mysql_find_top_level_word_between(masked, "from", start_pos + strlen("delete"), len);
	join_pos = from_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "join", from_pos + strlen("from"), len);
	on_pos = join_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "on", join_pos + strlen("join"), len);
	where_pos = on_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "where", on_pos + strlen("on"), len);
	free(masked);
	if (from_pos == (size_t)-1 || join_pos == (size_t)-1 || on_pos == (size_t)-1) {
		return SQLPARSER_STATUS_OK;
	}
	end = sqlparser_mysql_trim_right(statement_sql, statement_sql + len);
	target_start_ptr = sqlparser_mysql_trim_left(statement_sql + from_pos + strlen("from"), statement_sql + join_pos);
	target_end_ptr = sqlparser_mysql_trim_right(target_start_ptr, statement_sql + join_pos);
	if (!sqlparser_mysql_normalize_inner_join_target_end(target_start_ptr, &target_end_ptr)) {
		return SQLPARSER_STATUS_OK;
	}
	alias_start = NULL;
	alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(target_start_ptr, target_end_ptr, &alias_start, &alias_end);
	relation_name_start = NULL;
	relation_name_end = NULL;
	(void)sqlparser_mysql_extract_relation_name_span(
		target_start_ptr,
		target_end_ptr,
		alias_start,
		&relation_name_start,
		&relation_name_end);
	delete_target_start_ptr = sqlparser_mysql_trim_left(statement_sql + start_pos + strlen("delete"), statement_sql + from_pos);
	delete_target_end_ptr = sqlparser_mysql_trim_right(delete_target_start_ptr, statement_sql + from_pos);
	status = sqlparser_mysql_validate_delete_join_target(
		delete_target_start_ptr,
		delete_target_end_ptr,
		alias_start,
		alias_end,
		relation_name_start,
		relation_name_end,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	source_start_ptr = sqlparser_mysql_trim_left(statement_sql + join_pos + strlen("join"), statement_sql + on_pos);
	source_end_ptr = sqlparser_mysql_trim_right(source_start_ptr, statement_sql + on_pos);
	join_start_ptr = sqlparser_mysql_trim_left(statement_sql + on_pos + strlen("on"), where_pos == (size_t)-1 ? end : statement_sql + where_pos);
	join_end_ptr = sqlparser_mysql_trim_right(join_start_ptr, where_pos == (size_t)-1 ? end : statement_sql + where_pos);
	where_start_ptr = where_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(statement_sql + where_pos + strlen("where"), end);
	where_end_ptr = where_start_ptr == NULL ? NULL : sqlparser_mysql_trim_right(where_start_ptr, end);
	if (target_start_ptr >= target_end_ptr || source_start_ptr >= source_end_ptr || join_start_ptr >= join_end_ptr) {
		return SQLPARSER_STATUS_OK;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, statement_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, statement_sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "DELETE FROM ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, target_start_ptr, (size_t)(target_end_ptr - target_start_ptr), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " USING ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, source_start_ptr, (size_t)(source_end_ptr - source_start_ptr), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " WHERE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_append_join_on_call(&out, join_start_ptr, join_end_ptr, out_error);
	}
	if (status == SQLPARSER_STATUS_OK && where_start_ptr != NULL && where_start_ptr < where_end_ptr) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " AND ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, where_start_ptr, (size_t)(where_end_ptr - where_start_ptr), out_error);
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_dml_extensions(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	size_t len;
	size_t segment_start;
	size_t copy_start;
	int rewritten;
	sqlparser_status_t status;

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
	while (segment_start <= len) {
		size_t statement_end;
		char *statement_sql;
		char *rewritten_sql;

		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_mysql_rewrite_on_duplicate_statement(statement_sql, &rewritten_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_mysql_rewrite_update_join_statement(statement_sql, &rewritten_sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_mysql_rewrite_delete_join_statement(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			size_t leading_end;

			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = (size_t)(sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end) - sql);
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
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
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_identifier_to_backtick(
	const char *sql,
	size_t *index,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_mysql_buffer_append_char(out, '`', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + 1U;
	while (sql[pos] != '\0') {
		char c;

		c = sql[pos];
		if (c == '"') {
			if (sql[pos + 1U] == '"') {
				status = sqlparser_mysql_buffer_append_char(out, '"', out_error);
				pos += 2U;
			} else {
				status = sqlparser_mysql_buffer_append_char(out, '`', out_error);
				pos++;
				*index = pos;
				return status;
			}
		} else if (c == '`') {
			status = sqlparser_mysql_buffer_append_cstr(out, "``", out_error);
			pos++;
		} else {
			status = sqlparser_mysql_buffer_append_char(out, c, out_error);
			pos++;
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated quoted identifier in deparse output");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_mysql_param_to_question(
	const char *sql,
	size_t *index,
	const sqlparser_mysql_state_t *state,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	unsigned long value;
	size_t pos;

	pos = *index + 1U;
	if (state == NULL || state->positional_param_count == 0U ||
	    !isdigit((unsigned char)sql[pos])) {
		return sqlparser_mysql_buffer_append_char(out, sql[(*index)++], out_error);
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

	if (value > 0UL && (size_t)value <= state->positional_param_count) {
		*index = pos;
		return sqlparser_mysql_buffer_append_char(out, '?', out_error);
	}

	return sqlparser_mysql_buffer_append_char(out, sql[(*index)++], out_error);
}

static sqlparser_status_t sqlparser_mysql_rewrite_pg_quotes_to_backticks(
	const char *sql,
	const sqlparser_mysql_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	while (sql[index] != '\0') {
		int copied;

		copied = sqlparser_mysql_copy_single_quoted_or_comment(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_mysql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if (sql[index] == '$') {
			status = sqlparser_mysql_param_to_question(sql, &index, state, &out, out_error);
		} else if (sql[index] == '"') {
			status = sqlparser_mysql_identifier_to_backtick(sql, &index, &out, out_error);
		} else {
			status = sqlparser_mysql_buffer_append_char(&out, sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_limit_count_offset_to_mysql(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	sqlparser_status_t status;
	size_t index;
	size_t copy_start;
	int rewritten;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	rewritten = 0;
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_mysql_skip_quoted_or_comment_span(sql, index);
		if (skipped > index) {
			index = skipped;
			continue;
		}

		if (sqlparser_mysql_ascii_word_equal(sql, index, "limit")) {
			size_t count_start_pos;
			size_t count_end_pos;
			size_t offset_word_pos;
			size_t offset_start_pos;
			size_t offset_end_pos;
			const char *count_start;
			const char *count_end;
			const char *offset_start;
			const char *offset_end;

			count_start_pos = index + 5U;
			while (isspace((unsigned char)sql[count_start_pos])) {
				count_start_pos++;
			}
			count_end_pos = count_start_pos;
			while (sql[count_end_pos] != '\0' && !isspace((unsigned char)sql[count_end_pos])) {
				count_end_pos++;
			}
			offset_word_pos = count_end_pos;
			while (isspace((unsigned char)sql[offset_word_pos])) {
				offset_word_pos++;
			}
			if (!sqlparser_mysql_ascii_word_equal(sql, offset_word_pos, "offset")) {
				index++;
				continue;
			}
			offset_start_pos = offset_word_pos + 6U;
			while (isspace((unsigned char)sql[offset_start_pos])) {
				offset_start_pos++;
			}
			offset_end_pos = offset_start_pos;
			while (sql[offset_end_pos] != '\0' && sql[offset_end_pos] != ';' &&
			       !isspace((unsigned char)sql[offset_end_pos])) {
				offset_end_pos++;
			}

			count_start = sqlparser_mysql_trim_left(sql + count_start_pos, sql + count_end_pos);
			count_end = sqlparser_mysql_trim_right(count_start, sql + count_end_pos);
			offset_start = sqlparser_mysql_trim_left(sql + offset_start_pos, sql + offset_end_pos);
			offset_end = sqlparser_mysql_trim_right(offset_start, sql + offset_end_pos);
			if (!sqlparser_mysql_simple_limit_operand(count_start, count_end) ||
			    !sqlparser_mysql_simple_limit_operand(offset_start, offset_end)) {
				index++;
				continue;
			}

			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
				rewritten = 1;
			}
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, index - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, "LIMIT ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					offset_start,
					(size_t)(offset_end - offset_start),
					out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, ", ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_mem(
					&out,
					count_start,
					(size_t)(count_end - count_start),
					out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			index = offset_end_pos;
			copy_start = index;
			continue;
		}

		index++;
	}

	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}

	status = sqlparser_mysql_buffer_append_cstr(&out, sql + copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_mysql_find_first_top_level_and(
	const char *masked,
	size_t start,
	size_t end)
{
	return sqlparser_mysql_find_top_level_word_between(masked, "and", start, end);
}

static sqlparser_status_t sqlparser_mysql_rewrite_on_conflict_to_duplicate_key(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	char *masked;
	const char *sql;
	size_t len;
	size_t index;
	size_t copy_start;
	int rewritten;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&out, 0, sizeof(out));
	index = 0U;
	copy_start = 0U;
	rewritten = 0;
	while (index < len) {
		size_t on_pos;
		size_t conflict_pos;
		size_t do_pos;
		size_t update_pos;
		size_t set_pos;

		on_pos = sqlparser_mysql_find_top_level_word_between(masked, "on", index, len);
		if (on_pos == (size_t)-1) {
			break;
		}
		conflict_pos = sqlparser_mysql_skip_space(masked, on_pos + strlen("on"));
		if (!sqlparser_mysql_word_at(masked, conflict_pos, "conflict")) {
			index = on_pos + strlen("on");
			continue;
		}
		do_pos = sqlparser_mysql_find_top_level_word_between(
			masked,
			"do",
			conflict_pos + strlen("conflict"),
			len);
		update_pos = do_pos == (size_t)-1 ?
			(size_t)-1 :
			sqlparser_mysql_skip_space(masked, do_pos + strlen("do"));
		set_pos = update_pos == (size_t)-1 || !sqlparser_mysql_word_at(masked, update_pos, "update") ?
			(size_t)-1 :
			sqlparser_mysql_find_top_level_word_between(masked, "set", update_pos + strlen("update"), len);
		if (set_pos == (size_t)-1) {
			index = conflict_pos + strlen("conflict");
			continue;
		}
		if (!rewritten) {
			status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(masked);
				return status;
			}
			rewritten = 1;
		}
		status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, on_pos - copy_start, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, "ON DUPLICATE KEY UPDATE ", out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			free(masked);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		copy_start = set_pos + strlen("set");
		index = copy_start;
	}
	free(masked);
	if (!rewritten) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_update_from_to_join(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	char *masked;
	const char *sql;
	const char *end;
	size_t len;
	size_t start_pos;
	size_t set_pos;
	size_t from_pos;
	size_t where_pos;
	size_t and_pos;
	const char *target_start;
	const char *target_end;
	const char *assign_start;
	const char *assign_end;
	const char *source_start;
	const char *source_end;
	const char *join_start;
	const char *join_end;
	const char *where_start;
	const char *where_end;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	start_pos = (size_t)(sqlparser_mysql_trim_left(sql, sql + len) - sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "update")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	set_pos = sqlparser_mysql_find_top_level_word_between(masked, "set", start_pos + strlen("update"), len);
	from_pos = set_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "from", set_pos + strlen("set"), len);
	where_pos = from_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "where", from_pos + strlen("from"), len);
	if (set_pos == (size_t)-1 || from_pos == (size_t)-1 || where_pos == (size_t)-1) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	end = sqlparser_mysql_trim_right(sql, sql + len);
	and_pos = sqlparser_mysql_find_first_top_level_and(masked, where_pos + strlen("where"), (size_t)(end - sql));
	target_start = sqlparser_mysql_trim_left(sql + start_pos + strlen("update"), sql + set_pos);
	target_end = sqlparser_mysql_trim_right(target_start, sql + set_pos);
	assign_start = sqlparser_mysql_trim_left(sql + set_pos + strlen("set"), sql + from_pos);
	assign_end = sqlparser_mysql_trim_right(assign_start, sql + from_pos);
	source_start = sqlparser_mysql_trim_left(sql + from_pos + strlen("from"), sql + where_pos);
	source_end = sqlparser_mysql_trim_right(source_start, sql + where_pos);
	join_start = sqlparser_mysql_trim_left(sql + where_pos + strlen("where"), and_pos == (size_t)-1 ? end : sql + and_pos);
	join_end = sqlparser_mysql_trim_right(join_start, and_pos == (size_t)-1 ? end : sql + and_pos);
	where_start = and_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(sql + and_pos + strlen("and"), end);
	where_end = where_start == NULL ? NULL : sqlparser_mysql_trim_right(where_start, end);
	free(masked);
	if (target_start >= target_end || assign_start >= assign_end || source_start >= source_end ||
	    join_start >= join_end) {
		return SQLPARSER_STATUS_OK;
	}
	(void)sqlparser_mysql_unwrap_join_on_call(&join_start, &join_end);
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "UPDATE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, target_start, (size_t)(target_end - target_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " JOIN ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, source_start, (size_t)(source_end - source_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " ON ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, join_start, (size_t)(join_end - join_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " SET ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, assign_start, (size_t)(assign_end - assign_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK && where_start != NULL && where_start < where_end) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " WHERE ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, where_start, (size_t)(where_end - where_start), out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_delete_using_to_join(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	char *masked;
	const char *sql;
	const char *end;
	size_t len;
	size_t start_pos;
	size_t using_pos;
	size_t where_pos;
	size_t and_pos;
	const char *target_start;
	const char *target_end;
	const char *source_start;
	const char *source_end;
	const char *join_start;
	const char *join_end;
	const char *where_start;
	const char *where_end;
	const char *alias_start;
	const char *alias_end;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	sql = *io_sql;
	len = strlen(sql);
	masked = NULL;
	status = sqlparser_mysql_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	start_pos = (size_t)(sqlparser_mysql_trim_left(sql, sql + len) - sql);
	if (!sqlparser_mysql_word_at(masked, start_pos, "delete")) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	using_pos = sqlparser_mysql_find_top_level_word_between(masked, "using", start_pos + strlen("delete"), len);
	where_pos = using_pos == (size_t)-1 ? (size_t)-1 : sqlparser_mysql_find_top_level_word_between(masked, "where", using_pos + strlen("using"), len);
	if (using_pos == (size_t)-1 || where_pos == (size_t)-1) {
		free(masked);
		return SQLPARSER_STATUS_OK;
	}
	end = sqlparser_mysql_trim_right(sql, sql + len);
	and_pos = sqlparser_mysql_find_first_top_level_and(masked, where_pos + strlen("where"), (size_t)(end - sql));
	target_start = sqlparser_mysql_trim_left(sql + start_pos + strlen("delete from"), sql + using_pos);
	target_end = sqlparser_mysql_trim_right(target_start, sql + using_pos);
	source_start = sqlparser_mysql_trim_left(sql + using_pos + strlen("using"), sql + where_pos);
	source_end = sqlparser_mysql_trim_right(source_start, sql + where_pos);
	join_start = sqlparser_mysql_trim_left(sql + where_pos + strlen("where"), and_pos == (size_t)-1 ? end : sql + and_pos);
	join_end = sqlparser_mysql_trim_right(join_start, and_pos == (size_t)-1 ? end : sql + and_pos);
	where_start = and_pos == (size_t)-1 ? NULL : sqlparser_mysql_trim_left(sql + and_pos + strlen("and"), end);
	where_end = where_start == NULL ? NULL : sqlparser_mysql_trim_right(where_start, end);
	free(masked);
	if (target_start >= target_end || source_start >= source_end || join_start >= join_end) {
		return SQLPARSER_STATUS_OK;
	}
	(void)sqlparser_mysql_unwrap_join_on_call(&join_start, &join_end);
	alias_start = NULL;
	alias_end = NULL;
	(void)sqlparser_mysql_extract_alias_span(target_start, target_end, &alias_start, &alias_end);
	if (alias_start == NULL || alias_end == NULL) {
		alias_start = target_start;
		alias_end = target_end;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, sql, start_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, "DELETE ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, alias_start, (size_t)(alias_end - alias_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " FROM ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, target_start, (size_t)(target_end - target_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " JOIN ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, source_start, (size_t)(source_end - source_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " ON ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, join_start, (size_t)(join_end - join_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK && where_start != NULL && where_start < where_end) {
		status = sqlparser_mysql_buffer_append_cstr(&out, " WHERE ", out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_mem(&out, where_start, (size_t)(where_end - where_start), out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_internal_use_statement(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	const char *sql;
	const char *start;
	const char *end;
	const char *value_start;
	const char *value_end;
	const char *prefix;
	size_t prefix_len;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	start = sqlparser_mysql_trim_left(sql, sql + strlen(sql));
	end = sqlparser_mysql_trim_right(start, sql + strlen(sql));
	prefix = "SET " SQLPARSER_INTERNAL_CURRENT_DATABASE " TO ";
	prefix_len = strlen(prefix);
	if ((size_t)(end - start) < prefix_len ||
	    strncmp(start, prefix, prefix_len) != 0) {
		return SQLPARSER_STATUS_OK;
	}

	value_start = start + prefix_len;
	value_end = sqlparser_mysql_trim_right(value_start, end);
	if (value_start >= value_end) {
		return SQLPARSER_STATUS_OK;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_mysql_buffer_append_cstr(&out, "USE ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_append_mem(&out, value_start, (size_t)(value_end - value_start), out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}

	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_read_internal_string_arg(
	const char *sql,
	size_t *index,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
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
	pos = sqlparser_mysql_skip_space(sql, *index);
	if (sql[pos] == '\'' || sql[pos] == '"' || sql[pos] == '`') {
		quote = sql[pos];
		pos++;
		memset(&out, 0, sizeof(out));
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					status = sqlparser_mysql_buffer_append_char(&out, quote, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_mysql_buffer_release(&out);
						return status;
					}
					pos += 2U;
					continue;
				}
				pos++;
				*index = pos;
				*out_value = sqlparser_mysql_buffer_take(&out);
				if (*out_value == NULL) {
					*out_value = sqlparser_strdup("");
					if (*out_value == NULL) {
						sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
						return SQLPARSER_STATUS_NO_MEMORY;
					}
				}
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_mysql_buffer_append_char(&out, sql[pos], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_mysql_buffer_release(&out);
				return status;
			}
			pos++;
		}
		sqlparser_mysql_buffer_release(&out);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated internal prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	token_start = pos;
	while (sql[pos] != '\0' && sql[pos] != ',') {
		pos++;
	}
	token_end = pos;
	while (token_end > token_start && isspace((unsigned char)sql[token_end - 1U])) {
		token_end--;
	}
	if (token_start >= token_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal prepared argument");
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

static int sqlparser_mysql_internal_set_prefix(
	const char *sql,
	const char *internal_name,
	size_t *out_pos)
{
	size_t pos;
	size_t len;

	pos = sqlparser_mysql_skip_space(sql, 0U);
	if (!sqlparser_mysql_ascii_word_equal(sql, pos, "set")) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, pos + strlen("set"));
	len = strlen(internal_name);
	if (strncmp(sql + pos, internal_name, len) != 0 ||
	    sqlparser_mysql_is_ident_char((unsigned char)sql[pos + len])) {
		return 0;
	}
	pos = sqlparser_mysql_skip_space(sql, pos + len);
	if (!sqlparser_mysql_ascii_word_equal(sql, pos, "to") && sql[pos] != '=') {
		return 0;
	}
	pos = sql[pos] == '=' ? pos + 1U : pos + strlen("to");
	*out_pos = pos;
	return 1;
}

static sqlparser_status_t sqlparser_mysql_rewrite_internal_prepared_statement(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	static const struct {
		const char *internal_name;
		const char *prefix;
		const char *middle;
		int needs_second_arg;
	} specs[] = {
		{SQLPARSER_INTERNAL_MYSQL_PREPARE, "PREPARE ", " FROM ", 1},
		{SQLPARSER_INTERNAL_MYSQL_EXECUTE, "EXECUTE ", " USING ", 0},
		{SQLPARSER_INTERNAL_MYSQL_DEALLOCATE_PREPARE, "DEALLOCATE PREPARE ", NULL, 0},
		{SQLPARSER_INTERNAL_MYSQL_DROP_PREPARE, "DROP PREPARE ", NULL, 0}
	};
	sqlparser_mysql_buffer_t out;
	const char *sql;
	char *arg0;
	char *arg1;
	size_t index;
	size_t pos;
	size_t spec_index;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	for (spec_index = 0U; spec_index < sizeof(specs) / sizeof(specs[0]); spec_index++) {
		if (!sqlparser_mysql_internal_set_prefix(sql, specs[spec_index].internal_name, &pos)) {
			continue;
		}
		arg0 = NULL;
		arg1 = NULL;
		index = pos;
		status = sqlparser_mysql_read_internal_string_arg(sql, &index, &arg0, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(arg0);
			return status;
		}
		index = sqlparser_mysql_skip_space(sql, index);
		if (sql[index] == ',') {
			index++;
			status = sqlparser_mysql_read_internal_string_arg(sql, &index, &arg1, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				free(arg0);
				free(arg1);
				return status;
			}
			index = sqlparser_mysql_skip_space(sql, index);
		}
		if (sql[index] != '\0') {
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
		status = sqlparser_mysql_buffer_append_cstr(&out, specs[spec_index].prefix, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_append_cstr(&out, arg0, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && arg1 != NULL && specs[spec_index].middle != NULL) {
			status = sqlparser_mysql_buffer_append_cstr(&out, specs[spec_index].middle, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, arg1, out_error);
			}
		}
		free(arg0);
		free(arg1);
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		free(*io_sql);
		*io_sql = sqlparser_mysql_buffer_take(&out);
		if (*io_sql == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_rewrite_internal_use(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
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
		statement_end = sqlparser_mysql_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_mysql_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		original_statement_sql = statement_sql;
		status = sqlparser_mysql_rewrite_internal_use_statement(&statement_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && statement_sql == original_statement_sql) {
			status = sqlparser_mysql_rewrite_internal_prepared_statement(&statement_sql, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			free(statement_sql);
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		if (statement_sql != original_statement_sql) {
			if (!rewritten) {
				status = sqlparser_mysql_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(statement_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = (size_t)(sqlparser_mysql_trim_left(sql + segment_start, sql + statement_end) - sql);
			status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_mysql_buffer_append_cstr(&out, statement_sql, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				free(statement_sql);
				sqlparser_mysql_buffer_release(&out);
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
	status = sqlparser_mysql_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_mysql_buffer_reserve(&out, 0U, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_mysql_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	char *quoted_sql;
	sqlparser_mysql_state_t *mysql_state;
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

	mysql_state = NULL;
	status = sqlparser_mysql_state_new(&mysql_state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_mysql_preprocess_quotes(input_sql, mysql_state, &quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	status = sqlparser_mysql_rewrite_use_statements(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	status = sqlparser_mysql_rewrite_dml_extensions(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	status = sqlparser_mysql_reject_unsupported(quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	status = sqlparser_mysql_rewrite_limit_offset_count(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		sqlparser_mysql_state_destroy(mysql_state);
		return status;
	}

	*out_parser_sql = quoted_sql;
	*out_state = mysql_state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_preprocess_fragment(
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

	return sqlparser_mysql_preprocess_quotes(
		input_sql,
		(sqlparser_mysql_state_t *)state,
		out_parser_sql,
		out_error);
}

static sqlparser_status_t sqlparser_mysql_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *quoted_sql;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect deparse output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;

	quoted_sql = NULL;
	status = sqlparser_mysql_rewrite_pg_quotes_to_backticks(
		core_sql,
		(const sqlparser_mysql_state_t *)state,
		&quoted_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_mysql_rewrite_limit_count_offset_to_mysql(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_on_conflict_to_duplicate_key(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_update_from_to_join(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_delete_using_to_join(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}
	status = sqlparser_mysql_rewrite_internal_use(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}

	*out_sql = quoted_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_clone_state(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_mysql_state_t *source;
	sqlparser_mysql_state_t *clone;
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

	source = (const sqlparser_mysql_state_t *)state;
	status = sqlparser_mysql_state_new(&clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clone->positional_param_count = source->positional_param_count;
	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_dialect_ops_t SQLPARSER_MYSQL_OPS = {
	SQLPARSER_DIALECT_MYSQL,
	"mysql",
	sqlparser_mysql_preprocess,
	sqlparser_mysql_preprocess_fragment,
	sqlparser_mysql_postprocess_deparse,
	sqlparser_mysql_clone_state,
	sqlparser_mysql_state_destroy,
	NULL
};

const sqlparser_dialect_ops_t *sqlparser_dialect_mysql_ops(void)
{
	return &SQLPARSER_MYSQL_OPS;
}
