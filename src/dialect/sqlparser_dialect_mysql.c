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

static int sqlparser_mysql_copy_quoted_or_comment(
	const char *sql,
	size_t *index,
	sqlparser_mysql_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;

	quote = sql[*index];
	if (quote == '\'' || quote == '"' || quote == '`') {
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
	const char *sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	index = 0U;
	while (sql[index] != '\0') {
		int copied;

		copied = sqlparser_mysql_copy_quoted_or_comment(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_mysql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if (sqlparser_mysql_ascii_word_equal(sql, index, "limit")) {
			size_t limit_pos;
			size_t first_start_pos;
			size_t comma_pos;
			size_t second_start_pos;
			size_t second_end_pos;
			const char *first_start;
			const char *first_end;
			const char *second_start;
			const char *second_end;

			limit_pos = index;
			first_start_pos = index + 5U;
			while (isspace((unsigned char)sql[first_start_pos])) {
				first_start_pos++;
			}
			comma_pos = first_start_pos;
			while (sql[comma_pos] != '\0' && sql[comma_pos] != ',' && sql[comma_pos] != ';') {
				comma_pos++;
			}
			if (sql[comma_pos] != ',') {
				status = sqlparser_mysql_buffer_append_char(&out, sql[index], out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
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
				status = sqlparser_mysql_buffer_append_char(&out, sql[index], out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				index++;
				continue;
			}

			status = sqlparser_mysql_buffer_append_cstr(&out, "LIMIT ", out_error);
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
			(void)limit_pos;
			continue;
		}

		status = sqlparser_mysql_buffer_append_char(&out, sql[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		index++;
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
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
	const char *sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_mysql_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	index = 0U;
	while (sql[index] != '\0') {
		int copied;

		copied = sqlparser_mysql_copy_quoted_or_comment(sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_mysql_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
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
				status = sqlparser_mysql_buffer_append_char(&out, sql[index], out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
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
				status = sqlparser_mysql_buffer_append_char(&out, sql[index], out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_mysql_buffer_release(&out);
					return status;
				}
				index++;
				continue;
			}

			status = sqlparser_mysql_buffer_append_cstr(&out, "LIMIT ", out_error);
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
			continue;
		}

		status = sqlparser_mysql_buffer_append_char(&out, sql[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_mysql_buffer_release(&out);
			return status;
		}
		index++;
	}

	*out_sql = sqlparser_mysql_buffer_take(&out);
	if (*out_sql == NULL) {
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
	char *limit_sql;
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

	status = sqlparser_mysql_reject_unsupported(quoted_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(quoted_sql);
		return status;
	}

	limit_sql = NULL;
	status = sqlparser_mysql_rewrite_limit_offset_count(quoted_sql, &limit_sql, out_error);
	free(quoted_sql);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*out_parser_sql = limit_sql;
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

	status = sqlparser_mysql_rewrite_limit_count_offset_to_mysql(quoted_sql, out_sql, out_error);
	free(quoted_sql);
	return status;
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
