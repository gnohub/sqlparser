#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
} sqlparser_oracle_buffer_t;

typedef struct {
	char **bind_names;
	size_t bind_count;
	size_t bind_capacity;
	int saw_minus;
} sqlparser_oracle_state_t;

static void sqlparser_oracle_buffer_release(sqlparser_oracle_buffer_t *buffer)
{
	if (buffer == NULL) {
		return;
	}

	free(buffer->data);
	buffer->data = NULL;
	buffer->len = 0U;
	buffer->capacity = 0U;
}

static sqlparser_status_t sqlparser_oracle_buffer_reserve(
	sqlparser_oracle_buffer_t *buffer,
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

static sqlparser_status_t sqlparser_oracle_buffer_append_mem(
	sqlparser_oracle_buffer_t *buffer,
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

	status = sqlparser_oracle_buffer_reserve(buffer, len, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memcpy(buffer->data + buffer->len, data, len);
	buffer->len += len;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_buffer_append_char(
	sqlparser_oracle_buffer_t *buffer,
	char value,
	sqlparser_error_t *out_error)
{
	return sqlparser_oracle_buffer_append_mem(buffer, &value, 1U, out_error);
}

static sqlparser_status_t sqlparser_oracle_buffer_append_cstr(
	sqlparser_oracle_buffer_t *buffer,
	const char *text,
	sqlparser_error_t *out_error)
{
	return sqlparser_oracle_buffer_append_mem(buffer, text, text != NULL ? strlen(text) : 0U, out_error);
}

static char *sqlparser_oracle_buffer_take(sqlparser_oracle_buffer_t *buffer)
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

static sqlparser_status_t sqlparser_oracle_buffer_finish(
	sqlparser_oracle_buffer_t *buffer,
	sqlparser_error_t *out_error)
{
	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	return sqlparser_oracle_buffer_reserve(buffer, 0U, out_error);
}

static sqlparser_status_t sqlparser_oracle_buffer_reserve_input(
	sqlparser_oracle_buffer_t *buffer,
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

static void sqlparser_oracle_state_destroy(void *state)
{
	sqlparser_oracle_state_t *oracle_state;
	size_t index;

	oracle_state = (sqlparser_oracle_state_t *)state;
	if (oracle_state == NULL) {
		return;
	}

	for (index = 0U; index < oracle_state->bind_count; index++) {
		free(oracle_state->bind_names[index]);
	}
	free(oracle_state->bind_names);
	free(oracle_state);
}

static sqlparser_status_t sqlparser_oracle_state_new(
	sqlparser_oracle_state_t **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *state;

	if (out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"out_state must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	state = (sqlparser_oracle_state_t *)calloc(1U, sizeof(*state));
	if (state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_oracle_is_ident_start(unsigned char c)
{
	return isalpha(c) || c == '_';
}

static int sqlparser_oracle_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '$' || c == '#';
}

static int sqlparser_oracle_is_word_boundary(const char *text, size_t pos, size_t len)
{
	unsigned char prev;
	unsigned char next;

	prev = pos == 0U ? 0U : (unsigned char)text[pos - 1U];
	next = text[pos + len] == '\0' ? 0U : (unsigned char)text[pos + len];
	return !sqlparser_oracle_is_ident_char(prev) && !sqlparser_oracle_is_ident_char(next);
}

static int sqlparser_oracle_ascii_word_equal(const char *text, size_t pos, const char *word)
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

	return sqlparser_oracle_is_word_boundary(text, pos, len);
}

static size_t sqlparser_oracle_trim_left(const char *text, size_t start, size_t end)
{
	while (start < end && isspace((unsigned char)text[start])) {
		start++;
	}
	return start;
}

static size_t sqlparser_oracle_trim_right(const char *text, size_t start, size_t end)
{
	(void)start;
	while (end > start && isspace((unsigned char)text[end - 1U])) {
		end--;
	}
	return end;
}

static size_t sqlparser_oracle_q_quote_prefix_len(const char *text)
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

static int sqlparser_oracle_copy_quoted_or_comment(
	const char *sql,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;

	quote = sql[*index];
	if (quote == '\'' || quote == '"') {
		pos = *index;
		if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			return -1;
		}
		pos++;
		while (sql[pos] != '\0') {
			if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					pos++;
					if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) !=
					    SQLPARSER_STATUS_OK) {
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
			if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
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
			if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
				return -1;
			}
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				pos++;
				if (sqlparser_oracle_buffer_append_char(out, sql[pos], out_error) !=
				    SQLPARSER_STATUS_OK) {
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

static sqlparser_status_t sqlparser_oracle_mask_non_code(
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
		size_t q_prefix_len;

		q_prefix_len = sqlparser_oracle_q_quote_prefix_len(masked + index);
		if (q_prefix_len > 0U) {
			char open_delim;
			char close_delim;
			size_t prefix_pos;

			for (prefix_pos = 0U; prefix_pos <= q_prefix_len; prefix_pos++) {
				masked[index + prefix_pos] = ' ';
			}
			index += q_prefix_len + 1U;
			if (index >= len) {
				break;
			}
			open_delim = masked[index];
			close_delim = open_delim;
			if (open_delim == '[') {
				close_delim = ']';
			} else if (open_delim == '{') {
				close_delim = '}';
			} else if (open_delim == '(') {
				close_delim = ')';
			} else if (open_delim == '<') {
				close_delim = '>';
			}
			masked[index] = ' ';
			index++;
			while (index < len) {
				if (masked[index] == close_delim && masked[index + 1U] == '\'') {
					masked[index] = ' ';
					masked[index + 1U] = ' ';
					index++;
					break;
				}
				masked[index] = ' ';
				index++;
			}
		} else if (masked[index] == '\'') {
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

static int sqlparser_oracle_contains_phrase(const char *masked, const char *phrase)
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

	needs_left_boundary = sqlparser_oracle_is_ident_char((unsigned char)phrase[0]);
	needs_right_boundary = sqlparser_oracle_is_ident_char((unsigned char)phrase[last_phrase_pos - 1U]);

	for (pos = 0U; masked[pos] != '\0'; pos++) {
		size_t text_pos;
		size_t phrase_pos;
		int matched;

		if (needs_left_boundary && pos > 0U &&
		    sqlparser_oracle_is_ident_char((unsigned char)masked[pos - 1U])) {
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
		    (!needs_right_boundary || !sqlparser_oracle_is_ident_char((unsigned char)masked[text_pos]))) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_oracle_raw_contains_word_span(const char *sql, const char *word, size_t word_len)
{
	size_t pos;

	if (sql == NULL || word == NULL || word_len == 0U) {
		return 0;
	}

	for (pos = 0U; sql[pos] != '\0'; pos++) {
		size_t index;

		if (pos > 0U && sqlparser_oracle_is_ident_char((unsigned char)sql[pos - 1U])) {
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
		    !sqlparser_oracle_is_ident_char((unsigned char)sql[pos + word_len])) {
			return 1;
		}
	}

	return 0;
}

static int sqlparser_oracle_raw_contains_word(const char *sql, const char *word)
{
	return sqlparser_oracle_raw_contains_word_span(sql, word, word != NULL ? strlen(word) : 0U);
}

static int sqlparser_oracle_raw_may_contain_phrase(const char *sql, const char *phrase)
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
		       !sqlparser_oracle_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		start = pos;
		while (phrase[pos] != '\0' &&
		       sqlparser_oracle_is_ident_char((unsigned char)phrase[pos])) {
			pos++;
		}
		len = pos - start;
		if (len == 0U) {
			continue;
		}

		saw_token = 1;
		if (!sqlparser_oracle_raw_contains_word_span(sql, phrase + start, len)) {
			return 0;
		}
	}

	return saw_token;
}

static int sqlparser_oracle_starts_with_word(const char *masked, const char *word)
{
	size_t pos;

	if (masked == NULL || word == NULL) {
		return 0;
	}

	pos = 0U;
	while (isspace((unsigned char)masked[pos])) {
		pos++;
	}

	return sqlparser_oracle_ascii_word_equal(masked, pos, word);
}

static sqlparser_status_t sqlparser_oracle_reject_unsupported(
	const char *sql,
	sqlparser_error_t *out_error)
{
	static const char *const unsupported_phrases[] = {
		"connect by",
		"connect_by_root",
		"connect_by_iscycle",
		"connect_by_isleaf",
		"insert all",
		"insert first",
		"returning",
		"log errors",
		"pivot",
		"unpivot",
		"match_recognize",
		"model",
		"as of scn",
		"as of timestamp",
		"versions between",
		"create package",
		"create or replace package",
		"create procedure",
		"create or replace procedure",
		"create function",
		"create or replace function",
		"create trigger",
		"create or replace trigger",
		"alter session",
		"alter system",
		"create synonym",
		"create public synonym",
		"drop synonym",
		"database link",
		"explain plan for"
	};
	char *masked;
	sqlparser_status_t status;
	size_t index;
	int needs_mask;

	needs_mask =
		sqlparser_oracle_raw_contains_word(sql, "begin") ||
		sqlparser_oracle_raw_contains_word(sql, "declare") ||
		strstr(sql, "(+)") != NULL ||
		strchr(sql, '@') != NULL;
	for (index = 0U; !needs_mask &&
	     index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_oracle_raw_may_contain_phrase(sql, unsupported_phrases[index])) {
			needs_mask = 1;
		}
	}
	if (!needs_mask) {
		return SQLPARSER_STATUS_OK;
	}

	masked = NULL;
	status = sqlparser_oracle_mask_non_code(sql, &masked, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	if (sqlparser_oracle_starts_with_word(masked, "begin") ||
	    sqlparser_oracle_starts_with_word(masked, "declare")) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported Oracle syntax: PL/SQL block");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (strstr(masked, "(+)") != NULL) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported Oracle syntax: legacy outer join operator (+)");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	if (strchr(masked, '@') != NULL) {
		free(masked);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"unsupported Oracle syntax: database link");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}

	for (index = 0U; index < sizeof(unsupported_phrases) / sizeof(unsupported_phrases[0]); index++) {
		if (sqlparser_oracle_contains_phrase(masked, unsupported_phrases[index])) {
			char message[256];

			(void)snprintf(
				message,
				sizeof(message),
				"unsupported Oracle syntax: %s",
				unsupported_phrases[index]);
			free(masked);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, message);
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}

	free(masked);
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_oracle_session_value_token_end(
	const char *input_sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_oracle_preprocess_alter_session_switch(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	const char *sentinel_name;
	size_t start;
	size_t end;
	size_t pos;
	size_t param_start;
	size_t param_end;
	size_t value_start;
	size_t value_end;
	size_t service_start;
	size_t service_end;
	int has_service;
	int is_generic_session_param;
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

	start = sqlparser_oracle_trim_left(input_sql, 0U, strlen(input_sql));
	end = sqlparser_oracle_trim_right(input_sql, start, strlen(input_sql));
	if (end > start && input_sql[end - 1U] == ';') {
		end--;
		end = sqlparser_oracle_trim_right(input_sql, start, end);
	}
	pos = start;
	if (!sqlparser_oracle_ascii_word_equal(input_sql, pos, "alter")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("alter"), end);
	if (!sqlparser_oracle_ascii_word_equal(input_sql, pos, "session")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("session"), end);
	if (!sqlparser_oracle_ascii_word_equal(input_sql, pos, "set")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("set"), end);
	param_start = pos;
	while (pos < end && sqlparser_oracle_is_ident_char((unsigned char)input_sql[pos])) {
		pos++;
	}
	param_end = pos;
	if (param_start == param_end) {
		return SQLPARSER_STATUS_OK;
	}

	sentinel_name = NULL;
	is_generic_session_param = 0;
	if (param_end - param_start == strlen("current_schema") &&
	    sqlparser_oracle_ascii_word_equal(input_sql, param_start, "current_schema")) {
		sentinel_name = SQLPARSER_INTERNAL_CURRENT_SCHEMA;
	} else if (param_end - param_start == strlen("container") &&
	           sqlparser_oracle_ascii_word_equal(input_sql, param_start, "container")) {
		sentinel_name = SQLPARSER_INTERNAL_CURRENT_DATABASE;
	} else {
		is_generic_session_param = 1;
	}

	pos = sqlparser_oracle_trim_left(input_sql, pos, end);
	if (pos >= end || input_sql[pos] != '=') {
		return SQLPARSER_STATUS_OK;
	}
	pos++;
	value_start = sqlparser_oracle_trim_left(input_sql, pos, end);
	value_end = sqlparser_oracle_session_value_token_end(input_sql, value_start, end, out_error);
	if (value_end == 0U) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (value_start >= value_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET requires a value");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	has_service = 0;
	service_start = 0U;
	service_end = 0U;
	pos = sqlparser_oracle_trim_left(input_sql, value_end, end);
	if (pos < end) {
		if (is_generic_session_param ||
		    strcmp(sentinel_name, SQLPARSER_INTERNAL_CURRENT_DATABASE) != 0 ||
		    !sqlparser_oracle_ascii_word_equal(input_sql, pos, "service")) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unsupported ALTER SESSION SET suffix");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("service"), end);
		if (pos >= end || input_sql[pos] != '=') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET CONTAINER SERVICE requires '='");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos++;
		service_start = sqlparser_oracle_trim_left(input_sql, pos, end);
		service_end = sqlparser_oracle_session_value_token_end(input_sql, service_start, end, out_error);
		if (service_end == 0U) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_PARSE_ERROR;
		}
		if (service_start >= service_end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "ALTER SESSION SET CONTAINER SERVICE requires a value");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		pos = sqlparser_oracle_trim_left(input_sql, service_end, end);
		if (pos != end) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unsupported ALTER SESSION SET CONTAINER suffix");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		has_service = 1;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK && is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_char(&out, '"', out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_cstr(
			&out,
			SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_mem(
			&out,
			input_sql + param_start,
			param_end - param_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_char(&out, '"', out_error);
	}
	if (status == SQLPARSER_STATUS_OK && !is_generic_session_param) {
		status = sqlparser_oracle_buffer_append_cstr(&out, sentinel_name, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_mem(
			&out,
			input_sql + value_start,
			value_end - value_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_service) {
		status = sqlparser_oracle_buffer_append_cstr(&out, ", ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_service) {
		status = sqlparser_oracle_buffer_append_mem(
			&out,
			input_sql + service_start,
			service_end - service_start,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_append_internal_string_literal(
	sqlparser_oracle_buffer_t *out,
	const char *input_sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	size_t pos;
	sqlparser_status_t status;

	status = sqlparser_oracle_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	for (pos = start; pos < end; pos++) {
		if (input_sql[pos] == '\'') {
			status = sqlparser_oracle_buffer_append_cstr(out, "''", out_error);
		} else {
			status = sqlparser_oracle_buffer_append_char(out, input_sql[pos], out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return sqlparser_oracle_buffer_append_char(out, '\'', out_error);
}

static sqlparser_status_t sqlparser_oracle_preprocess_execute_immediate(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t start;
	size_t end;
	size_t pos;
	size_t value_start;
	size_t value_end;
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

	start = sqlparser_oracle_trim_left(input_sql, 0U, strlen(input_sql));
	end = sqlparser_oracle_trim_right(input_sql, start, strlen(input_sql));
	if (end > start && input_sql[end - 1U] == ';') {
		end--;
		end = sqlparser_oracle_trim_right(input_sql, start, end);
	}
	if (!sqlparser_oracle_ascii_word_equal(input_sql, start, "execute")) {
		return SQLPARSER_STATUS_OK;
	}
	pos = sqlparser_oracle_trim_left(input_sql, start + strlen("execute"), end);
	if (!sqlparser_oracle_ascii_word_equal(input_sql, pos, "immediate")) {
		return SQLPARSER_STATUS_OK;
	}
	value_start = sqlparser_oracle_trim_left(input_sql, pos + strlen("immediate"), end);
	value_end = sqlparser_oracle_trim_right(input_sql, value_start, end);
	if (value_start >= value_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "EXECUTE IMMEDIATE requires SQL text");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " TO ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_append_internal_string_literal(&out, input_sql, value_start, value_end, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_oracle_session_value_token_end(
	const char *input_sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t pos;

	if (start >= end) {
		return start;
	}

	quote = input_sql[start];
	if (quote == '\'' || quote == '"') {
		pos = start + 1U;
		while (pos < end) {
			if (input_sql[pos] == quote) {
				if (pos + 1U < end && input_sql[pos + 1U] == quote) {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated ALTER SESSION value");
		return 0U;
	}

	pos = start;
	while (pos < end && !isspace((unsigned char)input_sql[pos])) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_oracle_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	char quote;
	char open_delim;
	char close_delim;
	size_t q_prefix_len;
	size_t pos;

	q_prefix_len = sqlparser_oracle_q_quote_prefix_len(sql + index);
	if (q_prefix_len > 0U) {
		pos = index + q_prefix_len + 1U;
		if (sql[pos] == '\0') {
			return pos;
		}
		open_delim = sql[pos];
		close_delim = open_delim;
		if (open_delim == '[') {
			close_delim = ']';
		} else if (open_delim == '{') {
			close_delim = '}';
		} else if (open_delim == '(') {
			close_delim = ')';
		} else if (open_delim == '<') {
			close_delim = '>';
		}
		pos++;
		while (sql[pos] != '\0') {
			if (sql[pos] == close_delim && sql[pos + 1U] == '\'') {
				return pos + 2U;
			}
			pos++;
		}
		return pos;
	}

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

static size_t sqlparser_oracle_statement_end(const char *sql, size_t start)
{
	size_t index;
	size_t skipped;

	index = start;
	while (sql[index] != '\0') {
		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, index);
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

static sqlparser_status_t sqlparser_oracle_rewrite_alter_session_switches(
	const char *input_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	char *statement_sql;
	char *rewritten_sql;
	sqlparser_status_t status;
	size_t len;
	size_t segment_start;
	size_t statement_end;
	size_t copy_start;
	size_t leading_end;
	int rewritten;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (input_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "SQL input must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	len = strlen(input_sql);
	segment_start = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start < len) {
		statement_end = sqlparser_oracle_statement_end(input_sql, segment_start);
		statement_sql = sqlparser_strndup(input_sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_oracle_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_oracle_preprocess_alter_session_switch(statement_sql, &rewritten_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_oracle_preprocess_execute_immediate(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_oracle_buffer_reserve_input(&out, input_sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = sqlparser_oracle_trim_left(input_sql, segment_start, statement_end);
			status = sqlparser_oracle_buffer_append_mem(&out, input_sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
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
	status = sqlparser_oracle_buffer_append_mem(&out, input_sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_append_pg_string_char(
	sqlparser_oracle_buffer_t *out,
	char value,
	sqlparser_error_t *out_error)
{
	if (value == '\'') {
		return sqlparser_oracle_buffer_append_cstr(out, "''", out_error);
	}

	return sqlparser_oracle_buffer_append_char(out, value, out_error);
}

static sqlparser_status_t sqlparser_oracle_copy_q_string_literal(
	const char *input,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_error_t *out_error)
{
	char open_delim;
	char close_delim;
	size_t prefix_len;
	size_t pos;
	sqlparser_status_t status;

	prefix_len = sqlparser_oracle_q_quote_prefix_len(input + *index);
	if (prefix_len == 0U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"invalid Oracle q-quoted string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	open_delim = input[*index + prefix_len + 1U];
	if (open_delim == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_PARSE_ERROR,
			"unterminated Oracle q-quoted string literal");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	close_delim = open_delim;
	if (open_delim == '[') {
		close_delim = ']';
	} else if (open_delim == '{') {
		close_delim = '}';
	} else if (open_delim == '(') {
		close_delim = ')';
	} else if (open_delim == '<') {
		close_delim = '>';
	}

	status = sqlparser_oracle_buffer_append_char(out, '\'', out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	pos = *index + prefix_len + 2U;
	while (input[pos] != '\0') {
		if (input[pos] == close_delim && input[pos + 1U] == '\'') {
			status = sqlparser_oracle_buffer_append_char(out, '\'', out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			*index = pos + 2U;
			return SQLPARSER_STATUS_OK;
		}

		status = sqlparser_oracle_append_pg_string_char(out, input[pos], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos++;
	}

	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_PARSE_ERROR,
		"unterminated Oracle q-quoted string literal");
	return SQLPARSER_STATUS_PARSE_ERROR;
}

static sqlparser_status_t sqlparser_oracle_state_find_or_add_bind(
	sqlparser_oracle_state_t *state,
	const char *name,
	size_t len,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	char *name_copy;
	char **next;
	size_t index;
	size_t next_capacity;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind state arguments must not be NULL");
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

static sqlparser_status_t sqlparser_oracle_state_append_bind(
	sqlparser_oracle_state_t *state,
	const char *name,
	size_t len,
	size_t *out_param_index,
	sqlparser_error_t *out_error)
{
	char *name_copy;
	char **next;
	size_t next_capacity;

	if (state == NULL || name == NULL || out_param_index == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"bind state arguments must not be NULL");
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

static sqlparser_status_t sqlparser_oracle_append_pg_param(
	sqlparser_oracle_buffer_t *out,
	size_t param_index,
	sqlparser_error_t *out_error)
{
	char text[32];

	(void)snprintf(text, sizeof(text), "$%lu", (unsigned long)param_index);
	return sqlparser_oracle_buffer_append_cstr(out, text, out_error);
}

static sqlparser_status_t sqlparser_oracle_copy_bind_placeholder(
	const char *input,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_oracle_state_t *state,
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
	} else if (sqlparser_oracle_is_ident_start((unsigned char)input[end])) {
		end++;
		while (sqlparser_oracle_is_ident_char((unsigned char)input[end])) {
			end++;
		}
		while (input[end] == '.' &&
		       sqlparser_oracle_is_ident_start((unsigned char)input[end + 1U])) {
			end += 2U;
			while (sqlparser_oracle_is_ident_char((unsigned char)input[end])) {
				end++;
			}
		}
	} else {
		return sqlparser_oracle_buffer_append_char(out, input[(*index)++], out_error);
	}

	status = sqlparser_oracle_state_find_or_add_bind(
		state,
		input + start,
		end - start,
		&param_index,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_oracle_append_pg_param(out, param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	*index = end;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_copy_question_placeholder(
	size_t *index,
	sqlparser_oracle_buffer_t *out,
	sqlparser_oracle_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t param_index;
	sqlparser_status_t status;

	status = sqlparser_oracle_state_append_bind(state, "?", 1U, &param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_oracle_append_pg_param(out, param_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	(*index)++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_preprocess_text(
	const char *input_sql,
	sqlparser_oracle_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	while (input_sql[index] != '\0') {
		int copied;
		size_t q_prefix_len;

		q_prefix_len = sqlparser_oracle_q_quote_prefix_len(input_sql + index);
		if (q_prefix_len > 0U) {
			if (q_prefix_len == 2U) {
				sqlparser_oracle_buffer_release(&out);
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"unsupported Oracle syntax: national q-quoted string literal");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			status = sqlparser_oracle_copy_q_string_literal(input_sql, &index, &out, out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return status;
			}
			continue;
		}

		copied = sqlparser_oracle_copy_quoted_or_comment(input_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if (input_sql[index] == '?') {
			status = sqlparser_oracle_copy_question_placeholder(&index, &out, state, out_error);
		} else if (input_sql[index] == ':' && input_sql[index + 1U] != '=' &&
		    input_sql[index + 1U] != ':' && input_sql[index + 1U] != '\0') {
			status = sqlparser_oracle_copy_bind_placeholder(input_sql, &index, &out, state, out_error);
		} else if (sqlparser_oracle_ascii_word_equal(input_sql, index, "minus")) {
			state->saw_minus = 1;
			status = sqlparser_oracle_buffer_append_cstr(&out, "EXCEPT", out_error);
			index += 5U;
		} else {
			status = sqlparser_oracle_buffer_append_char(&out, input_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_oracle_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_param_to_bind(
	const char *sql,
	size_t *index,
	const sqlparser_oracle_state_t *state,
	sqlparser_oracle_buffer_t *out,
	sqlparser_error_t *out_error)
{
	unsigned long value;
	size_t pos;

	pos = *index + 1U;
	if (!isdigit((unsigned char)sql[pos])) {
		return sqlparser_oracle_buffer_append_char(out, sql[(*index)++], out_error);
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
		return sqlparser_oracle_buffer_append_cstr(out, state->bind_names[value - 1UL], out_error);
	}

	return sqlparser_oracle_buffer_append_char(out, sql[(*index)++], out_error);
}

static size_t sqlparser_oracle_quoted_literal_end(const char *sql, size_t start)
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

static size_t sqlparser_oracle_skip_optional_timestamp_zone(const char *sql, size_t pos)
{
	size_t scan;
	size_t word_pos;

	scan = pos;
	while (isspace((unsigned char)sql[scan])) {
		scan++;
	}

	if (sqlparser_oracle_ascii_word_equal(sql, scan, "without") ||
	    sqlparser_oracle_ascii_word_equal(sql, scan, "with")) {
		word_pos = scan;
		while (sqlparser_oracle_is_ident_char((unsigned char)sql[word_pos])) {
			word_pos++;
		}
		while (isspace((unsigned char)sql[word_pos])) {
			word_pos++;
		}
		if (sqlparser_oracle_ascii_word_equal(sql, word_pos, "time")) {
			while (sqlparser_oracle_is_ident_char((unsigned char)sql[word_pos])) {
				word_pos++;
			}
			while (isspace((unsigned char)sql[word_pos])) {
				word_pos++;
			}
			if (sqlparser_oracle_ascii_word_equal(sql, word_pos, "zone")) {
				while (sqlparser_oracle_is_ident_char((unsigned char)sql[word_pos])) {
					word_pos++;
				}
				return word_pos;
			}
		}
	}

	return pos;
}

static int sqlparser_oracle_copy_cast_literal(
	const char *sql,
	size_t *index,
	sqlparser_oracle_buffer_t *out,
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

	literal_end = sqlparser_oracle_quoted_literal_end(sql, *index);
	if (literal_end == 0U || sql[literal_end] != ':' || sql[literal_end + 1U] != ':') {
		return 0;
	}

	prefix = NULL;
	cast_name_pos = literal_end + 2U;
	cast_end = cast_name_pos;
	if (sqlparser_oracle_ascii_word_equal(sql, cast_name_pos, "date")) {
		prefix = "DATE ";
		cast_end += strlen("date");
	} else if (sqlparser_oracle_ascii_word_equal(sql, cast_name_pos, "timestamp")) {
		prefix = "TIMESTAMP ";
		cast_end += strlen("timestamp");
		cast_end = sqlparser_oracle_skip_optional_timestamp_zone(sql, cast_end);
	}
	if (prefix == NULL) {
		return 0;
	}

	status = sqlparser_oracle_buffer_append_cstr(out, prefix, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}
	status = sqlparser_oracle_buffer_append_mem(out, sql + *index, literal_end - *index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return -1;
	}

	*index = cast_end;
	return 1;
}

static sqlparser_status_t sqlparser_oracle_postprocess_text(
	const char *core_sql,
	const sqlparser_oracle_state_t *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	sqlparser_status_t status;
	size_t index;

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_reserve_input(&out, core_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	index = 0U;
	while (core_sql[index] != '\0') {
		int copied;

		copied = sqlparser_oracle_copy_cast_literal(core_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		copied = sqlparser_oracle_copy_quoted_or_comment(core_sql, &index, &out, out_error);
		if (copied < 0) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		if (copied > 0) {
			continue;
		}

		if (core_sql[index] == '$') {
			status = sqlparser_oracle_param_to_bind(core_sql, &index, state, &out, out_error);
		} else if (state != NULL && state->saw_minus &&
		           sqlparser_oracle_ascii_word_equal(core_sql, index, "except")) {
			status = sqlparser_oracle_buffer_append_cstr(&out, "MINUS", out_error);
			index += 6U;
		} else if (sqlparser_oracle_ascii_word_equal(core_sql, index, "truncate")) {
			status = sqlparser_oracle_buffer_append_cstr(&out, "TRUNCATE TABLE ", out_error);
			index += 8U;
			while (isspace((unsigned char)core_sql[index])) {
				index++;
			}
			if (sqlparser_oracle_ascii_word_equal(core_sql, index, "table")) {
				index += 5U;
				while (isspace((unsigned char)core_sql[index])) {
					index++;
				}
			}
		} else {
			status = sqlparser_oracle_buffer_append_char(&out, core_sql[index], out_error);
			if (status == SQLPARSER_STATUS_OK) {
				index++;
			}
		}

		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
	}

	status = sqlparser_oracle_buffer_finish(&out, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	return SQLPARSER_STATUS_OK;
}

static const char *sqlparser_oracle_find_top_level_comma(const char *start, const char *end)
{
	const char *pos;
	char quote;

	pos = start;
	while (pos < end) {
		if (*pos == '\'' || *pos == '"') {
			quote = *pos++;
			while (pos < end) {
				if (*pos == quote) {
					if (pos + 1 < end && *(pos + 1) == quote) {
						pos += 2;
						continue;
					}
					pos++;
					break;
				}
				pos++;
			}
			continue;
		}
		if (*pos == ',') {
			return pos;
		}
		pos++;
	}
	return NULL;
}

static sqlparser_status_t sqlparser_oracle_buffer_append_session_value(
	sqlparser_oracle_buffer_t *out,
	const char *value_start,
	const char *value_end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	int can_unquote;
	int first_char;
	sqlparser_status_t status;

	if (value_start < value_end && *value_start == '"' && *(value_end - 1) == '"') {
		can_unquote = 1;
		first_char = 1;
		pos = value_start + 1;
		while (pos < value_end - 1) {
			if (*pos == '"' && pos + 1 < value_end - 1 && *(pos + 1) == '"') {
				can_unquote = 0;
				break;
			}
			if (isupper((unsigned char)*pos) ||
			    (first_char && !sqlparser_oracle_is_ident_start((unsigned char)*pos)) ||
			    (!first_char && !sqlparser_oracle_is_ident_char((unsigned char)*pos))) {
				can_unquote = 0;
				break;
			}
			first_char = 0;
			pos++;
		}
		if (can_unquote && !first_char) {
			return sqlparser_oracle_buffer_append_mem(
				out,
				value_start + 1,
				(size_t)(value_end - value_start - 2),
				out_error);
		}
	}

	status = sqlparser_oracle_buffer_append_mem(
		out,
		value_start,
		(size_t)(value_end - value_start),
		out_error);
	return status;
}

static sqlparser_status_t sqlparser_oracle_buffer_append_session_parameter_value(
	sqlparser_oracle_buffer_t *out,
	const char *value_start,
	const char *value_end,
	sqlparser_error_t *out_error)
{
	const char *pos;
	sqlparser_status_t status;

	if (value_start < value_end && *value_start == '"' && *(value_end - 1) == '"') {
		status = sqlparser_oracle_buffer_append_char(out, '\'', out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		pos = value_start + 1;
		while (pos < value_end - 1) {
			if (*pos == '"' && pos + 1 < value_end - 1 && *(pos + 1) == '"') {
				status = sqlparser_oracle_buffer_append_char(out, '"', out_error);
				pos += 2;
			} else if (*pos == '\'') {
				status = sqlparser_oracle_buffer_append_cstr(out, "''", out_error);
				pos++;
			} else {
				status = sqlparser_oracle_buffer_append_char(out, *pos, out_error);
				pos++;
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		return sqlparser_oracle_buffer_append_char(out, '\'', out_error);
	}

	return sqlparser_oracle_buffer_append_session_value(out, value_start, value_end, out_error);
}

static sqlparser_status_t sqlparser_oracle_postprocess_session_switch(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	const char *parameter_name;
	const char *prefix;
	const char *name_start;
	const char *name_end;
	const char *value_start;
	const char *value_end;
	const char *service_start;
	const char *service_end;
	const char *comma;
	size_t start;
	size_t end;
	size_t prefix_len;
	size_t pos;
	size_t parameter_name_len;
	int has_service;
	int is_generic_session_param;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	start = sqlparser_oracle_trim_left(core_sql, 0U, strlen(core_sql));
	end = sqlparser_oracle_trim_right(core_sql, start, strlen(core_sql));
	parameter_name = NULL;
	parameter_name_len = 0U;
	is_generic_session_param = 0;
	prefix = "SET " SQLPARSER_INTERNAL_CURRENT_SCHEMA " TO ";
	prefix_len = strlen(prefix);
	if (end - start >= prefix_len && strncmp(core_sql + start, prefix, prefix_len) == 0) {
		parameter_name = "CURRENT_SCHEMA";
		parameter_name_len = strlen(parameter_name);
		value_start = core_sql + start + prefix_len;
	} else {
		prefix = "SET " SQLPARSER_INTERNAL_CURRENT_DATABASE " TO ";
		prefix_len = strlen(prefix);
		if (end - start >= prefix_len && strncmp(core_sql + start, prefix, prefix_len) == 0) {
			parameter_name = "CONTAINER";
			parameter_name_len = strlen(parameter_name);
			value_start = core_sql + start + prefix_len;
		} else {
			prefix = "SET ";
			prefix_len = strlen(prefix);
			if (end - start < prefix_len || strncmp(core_sql + start, prefix, prefix_len) != 0) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_oracle_trim_left(core_sql, start + prefix_len, end);
			if (pos >= end) {
				return SQLPARSER_STATUS_OK;
			}
			if (core_sql[pos] == '"') {
				name_start = core_sql + pos + 1U;
				pos++;
				while (pos < end) {
					if (core_sql[pos] == '"') {
						if (pos + 1U < end && core_sql[pos + 1U] == '"') {
							pos += 2U;
							continue;
						}
						break;
					}
					pos++;
				}
				if (pos >= end) {
					return SQLPARSER_STATUS_OK;
				}
				name_end = core_sql + pos;
				pos++;
			} else {
				name_start = core_sql + pos;
				while (pos < end && !isspace((unsigned char)core_sql[pos])) {
					pos++;
				}
				name_end = core_sql + pos;
			}
			prefix = SQLPARSER_INTERNAL_ORACLE_SESSION_PARAM_PREFIX;
			prefix_len = strlen(prefix);
			if ((size_t)(name_end - name_start) <= prefix_len ||
			    strncmp(name_start, prefix, prefix_len) != 0) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_oracle_trim_left(core_sql, pos, end);
			if (!sqlparser_oracle_ascii_word_equal(core_sql, pos, "to")) {
				return SQLPARSER_STATUS_OK;
			}
			pos = sqlparser_oracle_trim_left(core_sql, pos + strlen("to"), end);
			if (pos >= end) {
				return SQLPARSER_STATUS_OK;
			}
			parameter_name = name_start + prefix_len;
			parameter_name_len = (size_t)(name_end - parameter_name);
			value_start = core_sql + pos;
			is_generic_session_param = 1;
		}
	}

	value_end = core_sql + end;
	if (value_start >= value_end) {
		return SQLPARSER_STATUS_OK;
	}
	has_service = 0;
	service_start = NULL;
	service_end = NULL;
	if (strcmp(parameter_name, "CONTAINER") == 0) {
		comma = sqlparser_oracle_find_top_level_comma(value_start, value_end);
		if (comma != NULL) {
			value_end = core_sql + sqlparser_oracle_trim_right(core_sql, (size_t)(value_start - core_sql), (size_t)(comma - core_sql));
			service_start = core_sql + sqlparser_oracle_trim_left(core_sql, (size_t)(comma + 1 - core_sql), end);
			service_end = core_sql + end;
			if (service_start < service_end) {
				has_service = 1;
			}
		}
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "ALTER SESSION SET ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_mem(&out, parameter_name, parameter_name_len, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = is_generic_session_param ?
			sqlparser_oracle_buffer_append_session_parameter_value(
				&out,
				value_start,
				value_end,
				out_error) :
			sqlparser_oracle_buffer_append_session_value(
				&out,
				value_start,
				value_end,
				out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_service) {
		status = sqlparser_oracle_buffer_append_cstr(&out, " SERVICE = ", out_error);
	}
	if (status == SQLPARSER_STATUS_OK && has_service) {
		status = sqlparser_oracle_buffer_append_session_value(
			&out,
			service_start,
			service_end,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}

	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_read_internal_string_arg(
	const char *sql,
	size_t *index,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t pos;
	size_t len;
	size_t token_start;
	size_t token_end;
	char quote;
	sqlparser_status_t status;

	if (sql == NULL || index == NULL || out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "internal argument output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = NULL;
	len = strlen(sql);
	pos = sqlparser_oracle_trim_left(sql, *index, len);
	if (sql[pos] == '\'' || sql[pos] == '"') {
		quote = sql[pos];
		pos++;
		memset(&out, 0, sizeof(out));
		while (sql[pos] != '\0') {
			if (sql[pos] == quote) {
				if (sql[pos + 1U] == quote) {
					status = sqlparser_oracle_buffer_append_char(&out, quote, out_error);
					if (status != SQLPARSER_STATUS_OK) {
						sqlparser_oracle_buffer_release(&out);
						return status;
					}
					pos += 2U;
					continue;
				}
				pos++;
				*index = pos;
				*out_value = sqlparser_oracle_buffer_take(&out);
				if (*out_value == NULL) {
					*out_value = sqlparser_strdup("");
					if (*out_value == NULL) {
						sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
						return SQLPARSER_STATUS_NO_MEMORY;
					}
				}
				return SQLPARSER_STATUS_OK;
			}
			status = sqlparser_oracle_buffer_append_char(&out, sql[pos], out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return status;
			}
			pos++;
		}
		sqlparser_oracle_buffer_release(&out);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "unterminated internal Oracle prepared argument");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	token_start = pos;
	while (sql[pos] != '\0' && sql[pos] != ',') {
		pos++;
	}
	token_end = sqlparser_oracle_trim_right(sql, token_start, pos);
	if (token_start >= token_end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "missing internal Oracle prepared argument");
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

static sqlparser_status_t sqlparser_oracle_postprocess_execute_immediate(
	const char *core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	const char *prefix;
	char *value;
	size_t start;
	size_t end;
	size_t prefix_len;
	size_t index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (core_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}

	start = sqlparser_oracle_trim_left(core_sql, 0U, strlen(core_sql));
	end = sqlparser_oracle_trim_right(core_sql, start, strlen(core_sql));
	prefix = "SET " SQLPARSER_INTERNAL_ORACLE_EXECUTE_IMMEDIATE " TO ";
	prefix_len = strlen(prefix);
	if (end - start < prefix_len || strncmp(core_sql + start, prefix, prefix_len) != 0) {
		return SQLPARSER_STATUS_OK;
	}
	index = start + prefix_len;
	value = NULL;
	status = sqlparser_oracle_read_internal_string_arg(core_sql, &index, &value, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(value);
		return status;
	}
	index = sqlparser_oracle_trim_left(core_sql, index, end);
	if (index != end) {
		free(value);
		return SQLPARSER_STATUS_OK;
	}
	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "EXECUTE IMMEDIATE ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, value, out_error);
	}
	free(value);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_rewrite_session_switches(
	char **io_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
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
		return SQLPARSER_STATUS_OK;
	}

	sql = *io_sql;
	len = strlen(sql);
	segment_start = 0U;
	copy_start = 0U;
	rewritten = 0;
	memset(&out, 0, sizeof(out));
	while (segment_start < len) {
		statement_end = sqlparser_oracle_statement_end(sql, segment_start);
		statement_sql = sqlparser_strndup(sql + segment_start, statement_end - segment_start);
		if (statement_sql == NULL) {
			sqlparser_oracle_buffer_release(&out);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		rewritten_sql = NULL;
		status = sqlparser_oracle_postprocess_session_switch(statement_sql, &rewritten_sql, out_error);
		if (status == SQLPARSER_STATUS_OK && rewritten_sql == NULL) {
			status = sqlparser_oracle_postprocess_execute_immediate(statement_sql, &rewritten_sql, out_error);
		}
		free(statement_sql);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
		if (rewritten_sql != NULL) {
			if (!rewritten) {
				status = sqlparser_oracle_buffer_reserve_input(&out, sql, out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(rewritten_sql);
					return status;
				}
				rewritten = 1;
			}
			leading_end = sqlparser_oracle_trim_left(sql, segment_start, statement_end);
			status = sqlparser_oracle_buffer_append_mem(&out, sql + copy_start, leading_end - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, rewritten_sql, out_error);
			}
			free(rewritten_sql);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
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
	status = sqlparser_oracle_buffer_append_mem(&out, sql + copy_start, len - copy_start, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	free(*io_sql);
	*io_sql = sqlparser_oracle_buffer_take(&out);
	if (*io_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *state;
	char *rewritten_sql;
	const char *preprocess_input;
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
	status = sqlparser_oracle_state_new(&state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	rewritten_sql = NULL;
	status = sqlparser_oracle_rewrite_alter_session_switches(input_sql, &rewritten_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(state);
		return status;
	}
	preprocess_input = rewritten_sql != NULL ? rewritten_sql : input_sql;

	status = sqlparser_oracle_reject_unsupported(preprocess_input, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(rewritten_sql);
		sqlparser_oracle_state_destroy(state);
		return status;
	}

	status = sqlparser_oracle_preprocess_text(preprocess_input, state, out_parser_sql, out_error);
	free(rewritten_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(state);
		return status;
	}

	*out_state = state;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_postprocess_deparse(
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
	status = sqlparser_oracle_postprocess_text(
		core_sql,
		(const sqlparser_oracle_state_t *)state,
		&public_sql,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_oracle_rewrite_session_switches(&public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(public_sql);
		return status;
	}
	*out_sql = public_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_preprocess_fragment(
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
			"Oracle dialect state is missing");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}

	return sqlparser_oracle_preprocess_text(
		input_sql,
		(sqlparser_oracle_state_t *)state,
		out_parser_sql,
		out_error);
}

static sqlparser_status_t sqlparser_oracle_clone_state(
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const sqlparser_oracle_state_t *source;
	sqlparser_oracle_state_t *clone;
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

	source = (const sqlparser_oracle_state_t *)state;
	status = sqlparser_oracle_state_new(&clone, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	clone->saw_minus = source->saw_minus;

	for (index = 0U; index < source->bind_count; index++) {
		size_t param_index;

		status = sqlparser_oracle_state_append_bind(
			clone,
			source->bind_names[index],
			strlen(source->bind_names[index]),
			&param_index,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_state_destroy(clone);
			return status;
		}
		(void)param_index;
	}

	*out_state = clone;
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_dialect_ops_t SQLPARSER_ORACLE_OPS = {
	SQLPARSER_DIALECT_ORACLE,
	"oracle",
	sqlparser_oracle_preprocess,
	sqlparser_oracle_preprocess_fragment,
	sqlparser_oracle_postprocess_deparse,
	sqlparser_oracle_clone_state,
	sqlparser_oracle_state_destroy,
	NULL
};

const sqlparser_dialect_ops_t *sqlparser_dialect_oracle_ops(void)
{
	return &SQLPARSER_ORACLE_OPS;
}
