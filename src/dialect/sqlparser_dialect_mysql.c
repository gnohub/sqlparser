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
		"update ignore",
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
		status = sqlparser_mysql_preprocess_quotes(slice, &quoted_name, out_error);
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
	int has_digit;

	has_digit = 0;
	for (pos = start; pos < end; pos++) {
		if (isdigit((unsigned char)*pos)) {
			has_digit = 1;
			continue;
		}
		if (isspace((unsigned char)*pos)) {
			continue;
		}
		return 0;
	}

	return has_digit;
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

static sqlparser_status_t sqlparser_mysql_rewrite_pg_quotes_to_backticks(
	const char *sql,
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

		if (sql[index] == '"') {
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

	status = sqlparser_mysql_preprocess_quotes(input_sql, &quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_mysql_rewrite_use_statements(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}

	status = sqlparser_mysql_reject_unsupported(quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}

	status = sqlparser_mysql_rewrite_limit_offset_count(&quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}

	*out_parser_sql = quoted_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_mysql_preprocess_fragment(
	const char *input_sql,
	void *state,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	(void)state;

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

	return sqlparser_mysql_preprocess_quotes(input_sql, out_parser_sql, out_error);
}

static sqlparser_status_t sqlparser_mysql_postprocess_deparse(
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *quoted_sql;
	sqlparser_status_t status;

	(void)state;

	if (out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect deparse output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;

	quoted_sql = NULL;
	status = sqlparser_mysql_rewrite_pg_quotes_to_backticks(core_sql, &quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_mysql_rewrite_limit_count_offset_to_mysql(&quoted_sql, out_error);
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

static const sqlparser_dialect_ops_t SQLPARSER_MYSQL_OPS = {
	SQLPARSER_DIALECT_MYSQL,
	"mysql",
	sqlparser_mysql_preprocess,
	sqlparser_mysql_preprocess_fragment,
	sqlparser_mysql_postprocess_deparse,
	NULL,
	NULL,
	NULL
};

const sqlparser_dialect_ops_t *sqlparser_dialect_mysql_ops(void)
{
	return &SQLPARSER_MYSQL_OPS;
}
