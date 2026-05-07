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

		if (input_sql[index] == ':' && input_sql[index + 1U] != '=' &&
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

static sqlparser_status_t sqlparser_oracle_preprocess(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_state_t *state;
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

	status = sqlparser_oracle_reject_unsupported(input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	state = NULL;
	status = sqlparser_oracle_state_new(&state, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	status = sqlparser_oracle_preprocess_text(input_sql, state, out_parser_sql, out_error);
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

	return sqlparser_oracle_postprocess_text(
		core_sql,
		(const sqlparser_oracle_state_t *)state,
		out_sql,
		out_error);
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

		status = sqlparser_oracle_state_find_or_add_bind(
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

static const char *sqlparser_oracle_summary_keyword(const char *core_keyword, const void *state)
{
	const sqlparser_oracle_state_t *oracle_state;

	oracle_state = (const sqlparser_oracle_state_t *)state;
	if (oracle_state != NULL &&
	    oracle_state->saw_minus &&
	    core_keyword != NULL &&
	    strcmp(core_keyword, "except") == 0) {
		return "minus";
	}

	return core_keyword;
}

static const sqlparser_dialect_ops_t SQLPARSER_ORACLE_OPS = {
	SQLPARSER_DIALECT_ORACLE,
	"oracle",
	sqlparser_oracle_preprocess,
	sqlparser_oracle_preprocess_fragment,
	sqlparser_oracle_postprocess_deparse,
	sqlparser_oracle_summary_keyword,
	sqlparser_oracle_clone_state,
	sqlparser_oracle_state_destroy
};

const sqlparser_dialect_ops_t *sqlparser_dialect_oracle_ops(void)
{
	return &SQLPARSER_ORACLE_OPS;
}
