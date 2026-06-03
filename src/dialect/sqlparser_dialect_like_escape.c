#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
} sqlparser_like_escape_buffer_t;

static void sqlparser_like_escape_buffer_release(sqlparser_like_escape_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}
	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
}

static sqlparser_status_t sqlparser_like_escape_buffer_reserve(
	sqlparser_like_escape_buffer_t *buffer,
	size_t extra,
	sqlparser_error_t *out_error)
{
	size_t needed;
	size_t capacity;
	char *data;

	if (buffer->len > ((size_t)-1) - extra - 1U) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	needed = buffer->len + extra + 1U;
	if (needed <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}

	capacity = buffer->capacity != 0U ? buffer->capacity : 128U;
	while (capacity < needed) {
		if (capacity > ((size_t)-1) / 2U) {
			capacity = needed;
			break;
		}
		capacity *= 2U;
	}

	data = (char *)realloc(buffer->data, capacity);
	if (data == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	buffer->data = data;
	buffer->capacity = capacity;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_like_escape_buffer_append_mem(
	sqlparser_like_escape_buffer_t *buffer,
	const char *text,
	size_t len,
	sqlparser_error_t *out_error)
{
	sqlparser_status_t status;

	if (len == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_like_escape_buffer_reserve(buffer, len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memcpy(buffer->data + buffer->len, text, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_like_escape_buffer_append_cstr(
	sqlparser_like_escape_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	return sqlparser_like_escape_buffer_append_mem(buffer, text, strlen(text), out_error);
}

static int sqlparser_like_escape_ascii_equal(char left, char right)
{
	return tolower((unsigned char)left) == tolower((unsigned char)right);
}

static int sqlparser_like_escape_match_token(
	const char *sql,
	size_t offset,
	const char *token)
{
	size_t index;

	for (index = 0U; token[index] != '\0'; index++) {
		if (sql[offset + index] == '\0' ||
		    !sqlparser_like_escape_ascii_equal(sql[offset + index], token[index])) {
			return 0;
		}
	}
	return 1;
}

static size_t sqlparser_like_escape_skip_single_quote(const char *sql, size_t offset)
{
	size_t index;

	index = offset + 1U;
	while (sql[index] != '\0') {
		if (sql[index] == '\'') {
			if (sql[index + 1U] == '\'') {
				index += 2U;
				continue;
			}
			return index + 1U;
		}
		index++;
	}
	return index;
}

static size_t sqlparser_like_escape_skip_repeated_quote(
	const char *sql,
	size_t offset,
	char quote)
{
	size_t index;

	index = offset + 1U;
	while (sql[index] != '\0') {
		if (sql[index] == quote) {
			if (sql[index + 1U] == quote) {
				index += 2U;
				continue;
			}
			return index + 1U;
		}
		index++;
	}
	return index;
}

static size_t sqlparser_like_escape_skip_bracket_identifier(const char *sql, size_t offset)
{
	size_t index;

	index = offset + 1U;
	while (sql[index] != '\0') {
		if (sql[index] == ']') {
			if (sql[index + 1U] == ']') {
				index += 2U;
				continue;
			}
			return index + 1U;
		}
		index++;
	}
	return index;
}

static size_t sqlparser_like_escape_skip_line_comment(const char *sql, size_t offset)
{
	size_t index;

	index = offset + 2U;
	while (sql[index] != '\0' && sql[index] != '\n') {
		index++;
	}
	return index;
}

static size_t sqlparser_like_escape_skip_block_comment(const char *sql, size_t offset)
{
	size_t index;

	index = offset + 2U;
	while (sql[index] != '\0') {
		if (sql[index] == '*' && sql[index + 1U] == '/') {
			return index + 2U;
		}
		index++;
	}
	return index;
}

static size_t sqlparser_like_escape_skip_quoted_or_comment(const char *sql, size_t offset)
{
	switch (sql[offset]) {
	case '\'':
		return sqlparser_like_escape_skip_single_quote(sql, offset);
	case '"':
		return sqlparser_like_escape_skip_repeated_quote(sql, offset, '"');
	case '`':
		return sqlparser_like_escape_skip_repeated_quote(sql, offset, '`');
	case '[':
		return sqlparser_like_escape_skip_bracket_identifier(sql, offset);
	case '-':
		if (sql[offset + 1U] == '-') {
			return sqlparser_like_escape_skip_line_comment(sql, offset);
		}
		break;
	case '/':
		if (sql[offset + 1U] == '*') {
			return sqlparser_like_escape_skip_block_comment(sql, offset);
		}
		break;
	default:
		break;
	}
	return offset;
}

static int sqlparser_like_escape_previous_word_is_like(const char *sql, size_t offset)
{
	size_t end;
	size_t start;
	size_t len;

	end = offset;
	while (end > 0U && isspace((unsigned char)sql[end - 1U])) {
		end--;
	}
	start = end;
	while (start > 0U && isalpha((unsigned char)sql[start - 1U])) {
		start--;
	}
	len = end - start;
	if (len == 4U &&
	    sqlparser_like_escape_match_token(sql, start, "like")) {
		return 1;
	}
	if (len == 5U &&
	    sqlparser_like_escape_match_token(sql, start, "ilike")) {
		return 1;
	}
	return 0;
}

static void sqlparser_like_escape_trim_slice(const char *sql, size_t *start, size_t *len)
{
	size_t end;

	while (*len > 0U && isspace((unsigned char)sql[*start])) {
		(*start)++;
		(*len)--;
	}
	end = *start + *len;
	while (*len > 0U && isspace((unsigned char)sql[end - 1U])) {
		end--;
		(*len)--;
	}
}

static int sqlparser_like_escape_parse_call(
	const char *sql,
	size_t open_offset,
	size_t *out_pattern_start,
	size_t *out_pattern_len,
	size_t *out_escape_start,
	size_t *out_escape_len,
	size_t *out_end)
{
	size_t index;
	size_t comma_offset;
	size_t skipped;
	int depth;

	index = open_offset + 1U;
	comma_offset = (size_t)-1;
	depth = 0;
	while (sql[index] != '\0') {
		skipped = sqlparser_like_escape_skip_quoted_or_comment(sql, index);
		if (skipped != index) {
			index = skipped;
			continue;
		}
		if (sql[index] == '(') {
			depth++;
			index++;
			continue;
		}
		if (sql[index] == ')') {
			if (depth == 0) {
				size_t pattern_start;
				size_t pattern_len;
				size_t escape_start;
				size_t escape_len;

				if (comma_offset == (size_t)-1) {
					return 0;
				}
				pattern_start = open_offset + 1U;
				pattern_len = comma_offset - pattern_start;
				escape_start = comma_offset + 1U;
				escape_len = index - escape_start;
				sqlparser_like_escape_trim_slice(sql, &pattern_start, &pattern_len);
				sqlparser_like_escape_trim_slice(sql, &escape_start, &escape_len);
				if (pattern_len == 0U || escape_len == 0U) {
					return 0;
				}
				*out_pattern_start = pattern_start;
				*out_pattern_len = pattern_len;
				*out_escape_start = escape_start;
				*out_escape_len = escape_len;
				*out_end = index + 1U;
				return 1;
			}
			depth--;
			index++;
			continue;
		}
		if (sql[index] == ',' && depth == 0) {
			if (comma_offset != (size_t)-1) {
				return 0;
			}
			comma_offset = index;
			index++;
			continue;
		}
		index++;
	}
	return 0;
}

sqlparser_status_t sqlparser_dialect_rewrite_like_escape(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	static const char marker[] = "pg_catalog.like_escape";
	sqlparser_like_escape_buffer_t out;
	const char *sql;
	size_t index;
	size_t last;
	int changed;

	if (io_sql == NULL || *io_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL text must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	sql = *io_sql;
	memset(&out, 0, sizeof(out));
	index = 0U;
	last = 0U;
	changed = 0;
	while (sql[index] != '\0') {
		size_t skipped;

		skipped = sqlparser_like_escape_skip_quoted_or_comment(sql, index);
		if (skipped != index) {
			index = skipped;
			continue;
		}

		if (sqlparser_like_escape_match_token(sql, index, marker) &&
		    sql[index + sizeof(marker) - 1U] == '(' &&
		    sqlparser_like_escape_previous_word_is_like(sql, index)) {
			size_t pattern_start;
			size_t pattern_len;
			size_t escape_start;
			size_t escape_len;
			size_t call_end;
			sqlparser_status_t status;

			if (!sqlparser_like_escape_parse_call(
				    sql,
				    index + sizeof(marker) - 1U,
				    &pattern_start,
				    &pattern_len,
				    &escape_start,
				    &escape_len,
				    &call_end)) {
				index++;
				continue;
			}
			status = sqlparser_like_escape_buffer_append_mem(&out, sql + last, index - last, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_like_escape_buffer_append_mem(&out, sql + pattern_start, pattern_len, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_like_escape_buffer_append_cstr(&out, " ESCAPE ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_like_escape_buffer_append_mem(&out, sql + escape_start, escape_len, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_like_escape_buffer_release(&out);
				return status;
			}
			changed = 1;
			index = call_end;
			last = call_end;
			continue;
		}
		index++;
	}

	if (!changed) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_like_escape_buffer_append_mem(&out, sql + last, strlen(sql + last), out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_like_escape_buffer_release(&out);
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	free(*io_sql);
	*io_sql = out.data;
	return SQLPARSER_STATUS_OK;
}
