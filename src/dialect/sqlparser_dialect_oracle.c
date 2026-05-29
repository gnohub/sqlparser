#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"
#include "sqlparser_dialect_oracle_internal.h"

typedef struct {
	char *data;
	size_t len;
	size_t capacity;
} sqlparser_oracle_buffer_t;

typedef struct {
	char **bind_names;
	size_t bind_count;
	size_t bind_capacity;
	size_t bind_occurrence_count;
	int saw_minus;
	sqlparser_oracle_multi_insert_t *multi_insert;
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

static void sqlparser_oracle_relation_clear(sqlparser_oracle_relation_t *relation)
{
	if (relation == NULL) {
		return;
	}
	free(relation->database_name);
	free(relation->schema_name);
	free(relation->table_name);
	free(relation->sql);
	memset(relation, 0, sizeof(*relation));
}

static void sqlparser_oracle_column_clear(sqlparser_oracle_column_t *column)
{
	if (column == NULL) {
		return;
	}
	free(column->name);
	free(column->sql);
	memset(column, 0, sizeof(*column));
}

static void sqlparser_oracle_value_clear(sqlparser_oracle_value_t *value)
{
	if (value == NULL) {
		return;
	}
	free(value->public_sql);
	free(value->parser_sql);
	free(value->literal_string_value);
	free(value->literal_float_value);
	memset(value, 0, sizeof(*value));
}

static void sqlparser_oracle_multi_insert_branch_clear(sqlparser_oracle_multi_insert_branch_t *branch)
{
	size_t index;

	if (branch == NULL) {
		return;
	}
	sqlparser_oracle_relation_clear(&branch->relation);
	for (index = 0U; index < branch->column_count; index++) {
		sqlparser_oracle_column_clear(&branch->columns[index]);
	}
	free(branch->columns);
	for (index = 0U; index < branch->cell_count; index++) {
		sqlparser_oracle_value_clear(&branch->cells[index]);
	}
	free(branch->cells);
	free(branch->condition_public_sql);
	free(branch->condition_parser_sql);
	memset(branch, 0, sizeof(*branch));
}

static void sqlparser_oracle_multi_insert_destroy(sqlparser_oracle_multi_insert_t *multi)
{
	size_t branch_index;

	if (multi == NULL) {
		return;
	}
	for (branch_index = 0U; branch_index < multi->branch_count; branch_index++) {
		sqlparser_oracle_multi_insert_branch_clear(&multi->branches[branch_index]);
	}
	free(multi->branches);
	free(multi->source_public_sql);
	free(multi->source_parser_sql);
	free(multi);
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
	sqlparser_oracle_multi_insert_destroy(oracle_state->multi_insert);
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

int sqlparser_oracle_state_has_multi_insert(const void *state)
{
	const sqlparser_oracle_state_t *oracle_state;

	oracle_state = (const sqlparser_oracle_state_t *)state;
	return oracle_state != NULL &&
		oracle_state->multi_insert != NULL &&
		oracle_state->multi_insert->mode != SQLPARSER_ORACLE_MULTI_INSERT_NONE;
}

const sqlparser_oracle_multi_insert_t *sqlparser_oracle_state_multi_insert(const void *state)
{
	const sqlparser_oracle_state_t *oracle_state;

	oracle_state = (const sqlparser_oracle_state_t *)state;
	return oracle_state != NULL ? oracle_state->multi_insert : NULL;
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

	state->bind_occurrence_count++;
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

	state->bind_occurrence_count++;
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

static sqlparser_status_t sqlparser_oracle_trimmed_slice_dup(
	const char *sql,
	size_t start,
	size_t end,
	char **out_text,
	sqlparser_error_t *out_error)
{
	if (out_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "slice output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_text = NULL;
	start = sqlparser_oracle_trim_left(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	*out_text = sqlparser_strndup(sql + start, end - start);
	if (*out_text == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_oracle_find_matching_paren(
	const char *sql,
	size_t open_pos,
	size_t end,
	size_t *out_close)
{
	size_t pos;
	size_t depth;

	if (sql == NULL || out_close == NULL || open_pos >= end || sql[open_pos] != '(') {
		return 0;
	}
	pos = open_pos + 1U;
	depth = 1U;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')') {
			depth--;
			if (depth == 0U) {
				*out_close = pos;
				return 1;
			}
		}
		pos++;
	}
	return 0;
}

static int sqlparser_oracle_find_top_level_word(
	const char *sql,
	size_t start,
	size_t end,
	const char *word,
	size_t *out_pos)
{
	size_t pos;
	size_t depth;

	if (sql == NULL || word == NULL || out_pos == NULL) {
		return 0;
	}
	pos = start;
	depth = 0U;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (sql[pos] == ')') {
			if (depth > 0U) {
				depth--;
			}
			pos++;
			continue;
		}
		if (depth == 0U && sqlparser_oracle_ascii_word_equal(sql, pos, word)) {
			*out_pos = pos;
			return 1;
		}
		pos++;
	}
	return 0;
}

static sqlparser_status_t sqlparser_oracle_identifier_from_sql(
	const char *sql,
	size_t start,
	size_t end,
	char **out_name,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t pos;

	if (out_name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "identifier output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_name = NULL;
	start = sqlparser_oracle_trim_left(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "identifier must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (sql[start] != '"') {
		return sqlparser_oracle_trimmed_slice_dup(sql, start, end, out_name, out_error);
	}

	memset(&out, 0, sizeof(out));
	pos = start + 1U;
	while (pos < end) {
		if (sql[pos] == '"') {
			if (pos + 1U < end && sql[pos + 1U] == '"') {
				if (sqlparser_oracle_buffer_append_char(&out, '"', out_error) != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_buffer_release(&out);
					return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
				}
				pos += 2U;
				continue;
			}
			break;
		}
		if (sqlparser_oracle_buffer_append_char(&out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_oracle_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_name = sqlparser_oracle_buffer_take(&out);
	return *out_name != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_oracle_relation_from_sql(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_relation_t *relation,
	sqlparser_error_t *out_error)
{
	size_t part_start[3];
	size_t part_end[3];
	size_t count;
	size_t pos;

	if (relation == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(relation, 0, sizeof(*relation));
	start = sqlparser_oracle_trim_left(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "INSERT target relation must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (sqlparser_oracle_trimmed_slice_dup(sql, start, end, &relation->sql, out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}

	count = 0U;
	part_start[count] = start;
	pos = start;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '.') {
			if (count >= 2U) {
				sqlparser_oracle_relation_clear(relation);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Oracle relation has too many name parts");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			part_end[count] = pos;
			count++;
			part_start[count] = pos + 1U;
		}
		pos++;
	}
	part_end[count] = end;
	count++;

	if (count == 1U) {
		return sqlparser_oracle_identifier_from_sql(sql, part_start[0], part_end[0], &relation->table_name, out_error);
	}
	if (count == 2U) {
		if (sqlparser_oracle_identifier_from_sql(sql, part_start[0], part_end[0], &relation->schema_name, out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_oracle_identifier_from_sql(sql, part_start[1], part_end[1], &relation->table_name, out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_relation_clear(relation);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_oracle_identifier_from_sql(sql, part_start[0], part_end[0], &relation->database_name, out_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_oracle_identifier_from_sql(sql, part_start[1], part_end[1], &relation->schema_name, out_error) != SQLPARSER_STATUS_OK ||
	    sqlparser_oracle_identifier_from_sql(sql, part_start[2], part_end[2], &relation->table_name, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_relation_clear(relation);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_parse_column_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_column_t **out_columns,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_column_t *columns;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;

	if (out_columns == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "column list outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_columns = NULL;
	*out_count = 0U;
	columns = NULL;
	count = 0U;
	capacity = 0U;
	item_start = start;
	pos = start;
	while (pos <= end) {
		int at_end;
		size_t skipped;

		at_end = pos == end;
		skipped = !at_end ? sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (at_end || sql[pos] == ',') {
			size_t item_end;
			sqlparser_oracle_column_t *next;

			if (count == capacity) {
				size_t next_capacity;

				next_capacity = capacity == 0U ? 4U : capacity * 2U;
				if (next_capacity < capacity) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				next = (sqlparser_oracle_column_t *)realloc(columns, next_capacity * sizeof(*next));
				if (next == NULL) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				columns = next;
				capacity = next_capacity;
			}
			memset(&columns[count], 0, sizeof(columns[count]));
			item_end = sqlparser_oracle_trim_right(sql, item_start, pos);
			item_start = sqlparser_oracle_trim_left(sql, item_start, item_end);
			if (item_start >= item_end) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "column list contains an empty item");
				goto fail;
			}
			if (sqlparser_oracle_trimmed_slice_dup(sql, item_start, item_end, &columns[count].sql, out_error) != SQLPARSER_STATUS_OK ||
			    sqlparser_oracle_identifier_from_sql(sql, item_start, item_end, &columns[count].name, out_error) != SQLPARSER_STATUS_OK) {
				goto fail;
			}
			count++;
			item_start = pos + 1U;
		}
		pos++;
	}

	*out_columns = columns;
	*out_count = count;
	return SQLPARSER_STATUS_OK;

fail:
	if (columns != NULL) {
		size_t index;

		for (index = 0U; index < count; index++) {
			free(columns[index].name);
			free(columns[index].sql);
		}
		free(columns);
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
}

static int sqlparser_oracle_public_bind_token(
	const char *sql,
	sqlparser_bind_kind_t *out_kind,
	const char **out_key_start,
	size_t *out_key_len,
	const char **out_sql_start,
	size_t *out_sql_len)
{
	size_t len;

	if (sql == NULL || out_kind == NULL || out_key_start == NULL || out_key_len == NULL ||
	    out_sql_start == NULL || out_sql_len == NULL) {
		return 0;
	}
	len = strlen(sql);
	if (len == 1U && sql[0] == '?') {
		*out_kind = SQLPARSER_BIND_KIND_POSITIONAL;
		*out_key_start = NULL;
		*out_key_len = 0U;
		*out_sql_start = sql;
		*out_sql_len = 1U;
		return 1;
	}
	if (len > 1U && sql[0] == ':' && sql[1] != '=' && sql[1] != ':') {
		size_t pos;

		pos = 1U;
		if (isdigit((unsigned char)sql[pos])) {
			while (isdigit((unsigned char)sql[pos])) {
				pos++;
			}
			if (pos == len) {
				*out_kind = SQLPARSER_BIND_KIND_POSITIONAL;
				*out_key_start = sql + 1U;
				*out_key_len = len - 1U;
				*out_sql_start = sql;
				*out_sql_len = len;
				return 1;
			}
		} else if (sqlparser_oracle_is_ident_start((unsigned char)sql[pos])) {
			pos++;
			while (sqlparser_oracle_is_ident_char((unsigned char)sql[pos])) {
				pos++;
			}
			if (pos == len) {
				*out_kind = SQLPARSER_BIND_KIND_NAMED;
				*out_key_start = sql + 1U;
				*out_key_len = len - 1U;
				*out_sql_start = sql;
				*out_sql_len = len;
				return 1;
			}
		}
	}
	return 0;
}

static int sqlparser_oracle_text_is_integer_literal(const char *text)
{
	size_t pos;

	if (text == NULL || text[0] == '\0') {
		return 0;
	}
	pos = (text[0] == '+' || text[0] == '-') ? 1U : 0U;
	if (!isdigit((unsigned char)text[pos])) {
		return 0;
	}
	while (isdigit((unsigned char)text[pos])) {
		pos++;
	}
	return text[pos] == '\0';
}

static int sqlparser_oracle_text_is_float_literal(const char *text)
{
	size_t pos;
	int saw_digit;
	int saw_dot;

	if (text == NULL || text[0] == '\0') {
		return 0;
	}
	pos = (text[0] == '+' || text[0] == '-') ? 1U : 0U;
	saw_digit = 0;
	saw_dot = 0;
	while (text[pos] != '\0') {
		if (isdigit((unsigned char)text[pos])) {
			saw_digit = 1;
			pos++;
			continue;
		}
		if (text[pos] == '.' && !saw_dot) {
			saw_dot = 1;
			pos++;
			continue;
		}
		return 0;
	}
	return saw_digit && saw_dot;
}

static int sqlparser_oracle_ascii_text_equal(const char *left, const char *right)
{
	size_t index;

	if (left == NULL || right == NULL) {
		return 0;
	}
	for (index = 0U; left[index] != '\0' && right[index] != '\0'; index++) {
		if (tolower((unsigned char)left[index]) != tolower((unsigned char)right[index])) {
			return 0;
		}
	}
	return left[index] == '\0' && right[index] == '\0';
}

static sqlparser_status_t sqlparser_oracle_unquote_string_literal(
	const char *text,
	char **out_value,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t len;
	size_t pos;

	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = NULL;
	len = text != NULL ? strlen(text) : 0U;
	if (len < 2U || text[0] != '\'' || text[len - 1U] != '\'') {
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	memset(&out, 0, sizeof(out));
	pos = 1U;
	while (pos + 1U < len) {
		if (text[pos] == '\'' && pos + 1U < len - 1U && text[pos + 1U] == '\'') {
			if (sqlparser_oracle_buffer_append_char(&out, '\'', out_error) != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
			pos += 2U;
			continue;
		}
		if (sqlparser_oracle_buffer_append_char(&out, text[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_oracle_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_value = sqlparser_oracle_buffer_take(&out);
	return *out_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_oracle_value_fill_literal(
	sqlparser_oracle_value_t *value,
	sqlparser_error_t *out_error)
{
	long long integer_value;
	char *endptr;

	if (value == NULL || value->public_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_oracle_ascii_text_equal(value->public_sql, "null")) {
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_NULL;
		return SQLPARSER_STATUS_OK;
	}
	if (value->public_sql[0] == '\'') {
		if (sqlparser_oracle_unquote_string_literal(value->public_sql, &value->literal_string_value, out_error) != SQLPARSER_STATUS_OK) {
			return SQLPARSER_STATUS_OK;
		}
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_STRING;
		value->literal.string_value = value->literal_string_value;
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_oracle_text_is_integer_literal(value->public_sql)) {
		endptr = NULL;
		integer_value = strtoll(value->public_sql, &endptr, 10);
		if (endptr != NULL && *endptr == '\0') {
			value->has_literal = 1;
			value->literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
			value->literal.integer_value = integer_value;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_oracle_text_is_float_literal(value->public_sql)) {
		value->literal_float_value = sqlparser_strdup(value->public_sql);
		if (value->literal_float_value == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_FLOAT;
		value->literal.float_value = value->literal_float_value;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_parse_value_item(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_oracle_value_t *out_value,
	sqlparser_error_t *out_error)
{
	const char *key_start;
	const char *sql_start;
	size_t key_len;
	size_t sql_len;
	sqlparser_bind_kind_t bind_kind;
	size_t bind_before;
	size_t occurrence_before;
	sqlparser_status_t status;

	if (out_value == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	status = sqlparser_oracle_trimmed_slice_dup(sql, start, end, &out_value->public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	bind_before = state->bind_count;
	occurrence_before = state->bind_occurrence_count;
	status = sqlparser_oracle_preprocess_text(out_value->public_sql, state, &out_value->parser_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_value_clear(out_value);
		return status;
	}
	if (sqlparser_oracle_public_bind_token(
		    out_value->public_sql,
		    &bind_kind,
		    &key_start,
		    &key_len,
		    &sql_start,
		    &sql_len) &&
	    state->bind_occurrence_count == occurrence_before + 1U) {
		out_value->has_bind = 1;
		out_value->bind_kind = bind_kind;
		if (bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && key_start == NULL) {
			(void)snprintf(out_value->bind, sizeof(out_value->bind), "%lu", (unsigned long)(occurrence_before + 1U));
		} else {
			if (key_len >= sizeof(out_value->bind)) {
				sqlparser_oracle_value_clear(out_value);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind key is too long");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			memcpy(out_value->bind, key_start, key_len);
			out_value->bind[key_len] = '\0';
		}
		if (sql_len >= sizeof(out_value->bind_sql)) {
			sqlparser_oracle_value_clear(out_value);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		memcpy(out_value->bind_sql, sql_start, sql_len);
		out_value->bind_sql[sql_len] = '\0';
		out_value->bind_position = occurrence_before + 1U;
		out_value->has_bind_position = 1;
		(void)bind_before;
	} else {
		status = sqlparser_oracle_value_fill_literal(out_value, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_value_clear(out_value);
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_parse_value_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_oracle_value_t **out_values,
	size_t *out_count,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_value_t *values;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;
	size_t depth;

	if (out_values == NULL || out_count == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value list arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_values = NULL;
	*out_count = 0U;
	values = NULL;
	count = 0U;
	capacity = 0U;
	item_start = start;
	pos = start;
	depth = 0U;
	while (pos <= end) {
		int at_end;
		size_t skipped;

		at_end = pos == end;
		skipped = !at_end ? sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (!at_end && sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (!at_end && sql[pos] == ')') {
			if (depth > 0U) {
				depth--;
			}
			pos++;
			continue;
		}
		if (at_end || (depth == 0U && sql[pos] == ',')) {
			sqlparser_oracle_value_t *next;

			if (count == capacity) {
				size_t next_capacity;

				next_capacity = capacity == 0U ? 4U : capacity * 2U;
				if (next_capacity < capacity) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				next = (sqlparser_oracle_value_t *)realloc(values, next_capacity * sizeof(*next));
				if (next == NULL) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				values = next;
				capacity = next_capacity;
			}
			if (sqlparser_oracle_parse_value_item(sql, item_start, pos, state, &values[count], out_error) != SQLPARSER_STATUS_OK) {
				goto fail;
			}
			count++;
			item_start = pos + 1U;
		}
		pos++;
	}

	*out_values = values;
	*out_count = count;
	return SQLPARSER_STATUS_OK;

fail:
	if (values != NULL) {
		size_t index;

		for (index = 0U; index < count; index++) {
			sqlparser_oracle_value_clear(&values[index]);
		}
		free(values);
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t sqlparser_oracle_multi_insert_add_branch(
	sqlparser_oracle_multi_insert_t *multi,
	const sqlparser_oracle_multi_insert_branch_t *source,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_multi_insert_branch_t *next;

	if (multi == NULL || source == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert branch arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (multi->branch_count == SIZE_MAX) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "too many INSERT branches");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}
	next = (sqlparser_oracle_multi_insert_branch_t *)realloc(
		multi->branches,
		(multi->branch_count + 1U) * sizeof(*next));
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	multi->branches = next;
	multi->branches[multi->branch_count] = *source;
	multi->branches[multi->branch_count].ordinal = multi->branch_count;
	multi->branch_count++;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_parse_multi_insert_into(
	const char *sql,
	size_t *io_pos,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_oracle_multi_insert_t *multi,
	const char *condition_public_sql,
	const char *condition_parser_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_multi_insert_branch_t branch;
	size_t pos;
	size_t relation_start;
	size_t relation_end;
	size_t columns_open;
	size_t columns_close;
	size_t values_pos;
	size_t values_open;
	size_t values_close;
	sqlparser_status_t status;

	memset(&branch, 0, sizeof(branch));
	pos = sqlparser_oracle_trim_left(sql, *io_pos, end);
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "into")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected INTO");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_oracle_trim_left(sql, pos + strlen("into"), end);
	relation_start = pos;
	columns_open = end;
	values_pos = end;
	while (pos < end) {
		size_t skipped;

		skipped = sqlparser_oracle_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			columns_open = pos;
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(sql, pos, "values")) {
			values_pos = pos;
			break;
		}
		pos++;
	}
	if (columns_open == end && values_pos == end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	relation_end = columns_open != end ? columns_open : values_pos;
	status = sqlparser_oracle_relation_from_sql(sql, relation_start, relation_end, &branch.relation, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (columns_open != end) {
		if (!sqlparser_oracle_find_matching_paren(sql, columns_open, end, &columns_close)) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_oracle_parse_column_list(
			sql,
			columns_open + 1U,
			columns_close,
			&branch.columns,
			&branch.column_count,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			return status;
		}
		pos = sqlparser_oracle_trim_left(sql, columns_close + 1U, end);
	} else {
		pos = values_pos;
	}
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "values")) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_oracle_trim_left(sql, pos + strlen("values"), end);
	if (pos >= end || sql[pos] != '(') {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list expected '('");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	values_open = pos;
	if (!sqlparser_oracle_find_matching_paren(sql, values_open, end, &values_close)) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list is not closed");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_oracle_parse_value_list(
		sql,
		values_open + 1U,
		values_close,
		state,
		&branch.cells,
		&branch.cell_count,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		return status;
	}
	if (condition_public_sql != NULL) {
		branch.condition_public_sql = sqlparser_strdup(condition_public_sql);
		branch.condition_parser_sql = sqlparser_strdup(condition_parser_sql != NULL ? condition_parser_sql : condition_public_sql);
		if (branch.condition_public_sql == NULL || branch.condition_parser_sql == NULL) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		branch.has_condition = 1;
	}
	status = sqlparser_oracle_multi_insert_add_branch(multi, &branch, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		return status;
	}
	*io_pos = values_close + 1U;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_oracle_is_multi_insert_start(
	const char *sql,
	sqlparser_oracle_multi_insert_mode_t *out_mode)
{
	size_t len;
	size_t pos;

	if (out_mode != NULL) {
		*out_mode = SQLPARSER_ORACLE_MULTI_INSERT_NONE;
	}
	if (sql == NULL) {
		return 0;
	}
	len = strlen(sql);
	pos = sqlparser_oracle_trim_left(sql, 0U, len);
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "insert")) {
		return 0;
	}
	pos = sqlparser_oracle_trim_left(sql, pos + strlen("insert"), len);
	if (sqlparser_oracle_ascii_word_equal(sql, pos, "all")) {
		if (out_mode != NULL) {
			*out_mode = SQLPARSER_ORACLE_MULTI_INSERT_ALL;
		}
		return 1;
	}
	if (sqlparser_oracle_ascii_word_equal(sql, pos, "first")) {
		if (out_mode != NULL) {
			*out_mode = SQLPARSER_ORACLE_MULTI_INSERT_FIRST;
		}
		return 1;
	}
	return 0;
}

static sqlparser_status_t sqlparser_oracle_parse_multi_insert(
	const char *input_sql,
	sqlparser_oracle_state_t *state,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_multi_insert_t *multi;
	sqlparser_oracle_multi_insert_mode_t mode;
	sqlparser_oracle_buffer_t parser;
	size_t len;
	size_t end;
	size_t pos;
	sqlparser_status_t status;

	if (out_parser_sql == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert parser arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (!sqlparser_oracle_is_multi_insert_start(input_sql, &mode)) {
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	len = strlen(input_sql);
	pos = sqlparser_oracle_trim_left(input_sql, 0U, len);
	end = sqlparser_oracle_trim_right(input_sql, pos, len);
	if (end > pos && input_sql[end - 1U] == ';') {
		end = sqlparser_oracle_trim_right(input_sql, pos, end - 1U);
	}
	pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("insert"), end);
	pos = sqlparser_oracle_trim_left(
		input_sql,
		pos + (mode == SQLPARSER_ORACLE_MULTI_INSERT_ALL ? strlen("all") : strlen("first")),
		end);

	multi = (sqlparser_oracle_multi_insert_t *)calloc(1U, sizeof(*multi));
	if (multi == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	multi->mode = mode;

	while (pos < end) {
		pos = sqlparser_oracle_trim_left(input_sql, pos, end);
		if (pos >= end) {
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "select")) {
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "when")) {
			size_t condition_start;
			size_t then_pos;
			char *condition_public;
			char *condition_parser;

			condition_start = sqlparser_oracle_trim_left(input_sql, pos + strlen("when"), end);
			if (!sqlparser_oracle_find_top_level_word(input_sql, condition_start, end, "then", &then_pos)) {
				sqlparser_oracle_multi_insert_destroy(multi);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT WHEN is missing THEN");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			condition_public = NULL;
			condition_parser = NULL;
			status = sqlparser_oracle_trimmed_slice_dup(
				input_sql,
				condition_start,
				then_pos,
				&condition_public,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_preprocess_text(condition_public, state, &condition_parser, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				free(condition_public);
				free(condition_parser);
				sqlparser_oracle_multi_insert_destroy(multi);
				return status;
			}
			pos = sqlparser_oracle_trim_left(input_sql, then_pos + strlen("then"), end);
			do {
				status = sqlparser_oracle_parse_multi_insert_into(
					input_sql,
					&pos,
					end,
					state,
					multi,
					condition_public,
					condition_parser,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(condition_public);
					free(condition_parser);
					sqlparser_oracle_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_oracle_trim_left(input_sql, pos, end);
			} while (pos < end &&
			         sqlparser_oracle_ascii_word_equal(input_sql, pos, "into"));
			free(condition_public);
			free(condition_parser);
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "else")) {
			pos = sqlparser_oracle_trim_left(input_sql, pos + strlen("else"), end);
			while (pos < end && sqlparser_oracle_ascii_word_equal(input_sql, pos, "into")) {
				status = sqlparser_oracle_parse_multi_insert_into(
					input_sql,
					&pos,
					end,
					state,
					multi,
					NULL,
					NULL,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_oracle_trim_left(input_sql, pos, end);
			}
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "into")) {
			status = sqlparser_oracle_parse_multi_insert_into(
				input_sql,
				&pos,
				end,
				state,
				multi,
				NULL,
				NULL,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_multi_insert_destroy(multi);
				return status;
			}
			continue;
		}
		sqlparser_oracle_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected INTO, WHEN, ELSE, or SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	pos = sqlparser_oracle_trim_left(input_sql, pos, end);
	if (multi->branch_count == 0U || pos >= end || !sqlparser_oracle_ascii_word_equal(input_sql, pos, "select")) {
		sqlparser_oracle_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT requires branches and a source SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_oracle_trimmed_slice_dup(input_sql, pos, end, &multi->source_public_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_preprocess_text(multi->source_public_sql, state, &multi->source_parser_sql, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_destroy(multi);
		return status;
	}

	memset(&parser, 0, sizeof(parser));
	status = sqlparser_oracle_buffer_append_cstr(&parser, "INSERT INTO sqlparser_oracle_multi_insert_source ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&parser, multi->source_parser_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&parser, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&parser);
		sqlparser_oracle_multi_insert_destroy(multi);
		return status;
	}

	sqlparser_oracle_multi_insert_destroy(state->multi_insert);
	state->multi_insert = multi;
	*out_parser_sql = sqlparser_oracle_buffer_take(&parser);
	return *out_parser_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
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

static sqlparser_status_t sqlparser_oracle_render_multi_insert(
	const sqlparser_oracle_multi_insert_t *multi,
	const char *postprocessed_core_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t source_pos;
	size_t core_len;
	size_t branch_index;
	sqlparser_status_t status;

	if (multi == NULL || out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert render arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	core_len = postprocessed_core_sql != NULL ? strlen(postprocessed_core_sql) : 0U;
	if (postprocessed_core_sql == NULL ||
	    !sqlparser_oracle_find_top_level_word(postprocessed_core_sql, 0U, core_len, "select", &source_pos)) {
		postprocessed_core_sql = multi->source_public_sql;
		source_pos = 0U;
	}

	memset(&out, 0, sizeof(out));
	status = sqlparser_oracle_buffer_append_cstr(&out, "INSERT ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(
			&out,
			multi->mode == SQLPARSER_ORACLE_MULTI_INSERT_FIRST ? "FIRST" : "ALL",
			out_error);
	}
	for (branch_index = 0U; status == SQLPARSER_STATUS_OK && branch_index < multi->branch_count; branch_index++) {
		const sqlparser_oracle_multi_insert_branch_t *branch;
		size_t index;

		branch = &multi->branches[branch_index];
		status = sqlparser_oracle_buffer_append_char(&out, ' ', out_error);
		if (status == SQLPARSER_STATUS_OK && branch->has_condition) {
			status = sqlparser_oracle_buffer_append_cstr(&out, "WHEN ", out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, branch->condition_public_sql, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, " THEN ", out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_cstr(&out, "INTO ", out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_cstr(&out, branch->relation.sql, out_error);
		}
		if (status == SQLPARSER_STATUS_OK && branch->column_count > 0U) {
			status = sqlparser_oracle_buffer_append_cstr(&out, " (", out_error);
			for (index = 0U; status == SQLPARSER_STATUS_OK && index < branch->column_count; index++) {
				if (index > 0U) {
					status = sqlparser_oracle_buffer_append_cstr(&out, ", ", out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status = sqlparser_oracle_buffer_append_cstr(&out, branch->columns[index].sql, out_error);
				}
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_char(&out, ')', out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_cstr(&out, " VALUES (", out_error);
		}
		for (index = 0U; status == SQLPARSER_STATUS_OK && index < branch->cell_count; index++) {
			if (index > 0U) {
				status = sqlparser_oracle_buffer_append_cstr(&out, ", ", out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_buffer_append_cstr(&out, branch->cells[index].public_sql, out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_char(&out, ')', out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_char(&out, ' ', out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&out, postprocessed_core_sql + source_pos, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&out, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return status;
	}
	*out_sql = sqlparser_oracle_buffer_take(&out);
	return *out_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
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

	if (sqlparser_oracle_is_multi_insert_start(preprocess_input, NULL)) {
		status = sqlparser_oracle_parse_multi_insert(preprocess_input, state, out_parser_sql, out_error);
		free(rewritten_sql);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_state_destroy(state);
			return status;
		}
		*out_state = state;
		return SQLPARSER_STATUS_OK;
	}

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

	if (sqlparser_oracle_state_has_multi_insert(state)) {
		char *multi_sql;

		multi_sql = NULL;
		status = sqlparser_oracle_render_multi_insert(
			sqlparser_oracle_state_multi_insert(state),
			public_sql,
			&multi_sql,
			out_error);
		free(public_sql);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		public_sql = multi_sql;
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

static sqlparser_status_t sqlparser_oracle_value_clone(
	const sqlparser_oracle_value_t *source,
	sqlparser_oracle_value_t *target,
	sqlparser_error_t *out_error)
{
	if (source == NULL || target == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value clone arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(target, 0, sizeof(*target));
	*target = *source;
	target->public_sql = sqlparser_strdup(source->public_sql);
	target->parser_sql = sqlparser_strdup(source->parser_sql);
	target->literal_string_value = sqlparser_strdup(source->literal_string_value);
	target->literal_float_value = sqlparser_strdup(source->literal_float_value);
	if ((source->public_sql != NULL && target->public_sql == NULL) ||
	    (source->parser_sql != NULL && target->parser_sql == NULL) ||
	    (source->literal_string_value != NULL && target->literal_string_value == NULL) ||
	    (source->literal_float_value != NULL && target->literal_float_value == NULL)) {
		sqlparser_oracle_value_clear(target);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (target->literal.kind == SQLPARSER_LITERAL_KIND_STRING) {
		target->literal.string_value = target->literal_string_value;
	} else if (target->literal.kind == SQLPARSER_LITERAL_KIND_FLOAT) {
		target->literal.float_value = target->literal_float_value;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_multi_insert_clone(
	const sqlparser_oracle_multi_insert_t *source,
	sqlparser_oracle_multi_insert_t **out_clone,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_multi_insert_t *clone;
	size_t branch_index;

	if (out_clone == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert clone output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_clone = NULL;
	if (source == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	clone = (sqlparser_oracle_multi_insert_t *)calloc(1U, sizeof(*clone));
	if (clone == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	clone->mode = source->mode;
	clone->source_public_sql = sqlparser_strdup(source->source_public_sql);
	clone->source_parser_sql = sqlparser_strdup(source->source_parser_sql);
	if ((source->source_public_sql != NULL && clone->source_public_sql == NULL) ||
	    (source->source_parser_sql != NULL && clone->source_parser_sql == NULL)) {
		sqlparser_oracle_multi_insert_destroy(clone);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	if (source->branch_count > 0U) {
		clone->branches = (sqlparser_oracle_multi_insert_branch_t *)calloc(source->branch_count, sizeof(*clone->branches));
		if (clone->branches == NULL) {
			sqlparser_oracle_multi_insert_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		clone->branch_count = source->branch_count;
	}
	for (branch_index = 0U; branch_index < source->branch_count; branch_index++) {
		const sqlparser_oracle_multi_insert_branch_t *src_branch;
		sqlparser_oracle_multi_insert_branch_t *dst_branch;
		size_t index;

		src_branch = &source->branches[branch_index];
		dst_branch = &clone->branches[branch_index];
		dst_branch->ordinal = src_branch->ordinal;
		dst_branch->relation.database_name = sqlparser_strdup(src_branch->relation.database_name);
		dst_branch->relation.schema_name = sqlparser_strdup(src_branch->relation.schema_name);
		dst_branch->relation.table_name = sqlparser_strdup(src_branch->relation.table_name);
		dst_branch->relation.sql = sqlparser_strdup(src_branch->relation.sql);
		if ((src_branch->relation.database_name != NULL && dst_branch->relation.database_name == NULL) ||
		    (src_branch->relation.schema_name != NULL && dst_branch->relation.schema_name == NULL) ||
		    (src_branch->relation.table_name != NULL && dst_branch->relation.table_name == NULL) ||
		    (src_branch->relation.sql != NULL && dst_branch->relation.sql == NULL)) {
			sqlparser_oracle_multi_insert_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		if (src_branch->column_count > 0U) {
			dst_branch->columns = (sqlparser_oracle_column_t *)calloc(src_branch->column_count, sizeof(*dst_branch->columns));
			if (dst_branch->columns == NULL) {
				sqlparser_oracle_multi_insert_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			dst_branch->column_count = src_branch->column_count;
			for (index = 0U; index < src_branch->column_count; index++) {
				dst_branch->columns[index].name = sqlparser_strdup(src_branch->columns[index].name);
				dst_branch->columns[index].sql = sqlparser_strdup(src_branch->columns[index].sql);
				if ((src_branch->columns[index].name != NULL && dst_branch->columns[index].name == NULL) ||
				    (src_branch->columns[index].sql != NULL && dst_branch->columns[index].sql == NULL)) {
					sqlparser_oracle_multi_insert_destroy(clone);
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					return SQLPARSER_STATUS_NO_MEMORY;
				}
			}
		}
		if (src_branch->cell_count > 0U) {
			dst_branch->cells = (sqlparser_oracle_value_t *)calloc(src_branch->cell_count, sizeof(*dst_branch->cells));
			if (dst_branch->cells == NULL) {
				sqlparser_oracle_multi_insert_destroy(clone);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			dst_branch->cell_count = src_branch->cell_count;
			for (index = 0U; index < src_branch->cell_count; index++) {
				if (sqlparser_oracle_value_clone(&src_branch->cells[index], &dst_branch->cells[index], out_error) != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_multi_insert_destroy(clone);
					return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
				}
			}
		}
		dst_branch->has_condition = src_branch->has_condition;
		dst_branch->condition_public_sql = sqlparser_strdup(src_branch->condition_public_sql);
		dst_branch->condition_parser_sql = sqlparser_strdup(src_branch->condition_parser_sql);
		if ((src_branch->condition_public_sql != NULL && dst_branch->condition_public_sql == NULL) ||
		    (src_branch->condition_parser_sql != NULL && dst_branch->condition_parser_sql == NULL)) {
			sqlparser_oracle_multi_insert_destroy(clone);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	*out_clone = clone;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_oracle_bind_key_is_digits(const char *key)
{
	size_t index;

	if (key == NULL || key[0] == '\0') {
		return 0;
	}
	for (index = 0U; key[index] != '\0'; index++) {
		if (!isdigit((unsigned char)key[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_oracle_bind_key_is_identifier(const char *key)
{
	size_t index;

	if (key == NULL || key[0] == '\0') {
		return 0;
	}
	if (!sqlparser_oracle_is_ident_start((unsigned char)key[0])) {
		return 0;
	}
	for (index = 1U; key[index] != '\0'; index++) {
		if (!sqlparser_oracle_is_ident_char((unsigned char)key[index])) {
			return 0;
		}
	}
	return 1;
}

static sqlparser_status_t sqlparser_oracle_render_bind_value(
	const sqlparser_bind_value_t *bind,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char buffer[SQLPARSER_BIND_SQL_CAPACITY];

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind SQL output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (bind == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (bind->kind == SQLPARSER_BIND_KIND_POSITIONAL) {
		if (bind->key == NULL || bind->key[0] == '\0') {
			*out_sql = sqlparser_strdup("?");
		} else {
			if (!sqlparser_oracle_bind_key_is_digits(bind->key)) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "positional bind key must be numeric");
				return SQLPARSER_STATUS_INVALID_ARGUMENT;
			}
			if (snprintf(buffer, sizeof(buffer), ":%s", bind->key) >= (int)sizeof(buffer)) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			*out_sql = sqlparser_strdup(buffer);
		}
	} else if (bind->kind == SQLPARSER_BIND_KIND_NAMED) {
		if (!sqlparser_oracle_bind_key_is_identifier(bind->key)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "named bind key is invalid");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		if (snprintf(buffer, sizeof(buffer), ":%s", bind->key) >= (int)sizeof(buffer)) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		*out_sql = sqlparser_strdup(buffer);
	} else {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "bind kind must be positional or named");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_render_literal_value(
	const sqlparser_literal_value_t *value,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	char buffer[64];
	const char *text;
	size_t index;
	sqlparser_status_t status;

	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal SQL output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (value->kind == SQLPARSER_LITERAL_KIND_NULL) {
		*out_sql = sqlparser_strdup("NULL");
	} else if (value->kind == SQLPARSER_LITERAL_KIND_INTEGER) {
		(void)snprintf(buffer, sizeof(buffer), "%lld", value->integer_value);
		*out_sql = sqlparser_strdup(buffer);
	} else if (value->kind == SQLPARSER_LITERAL_KIND_FLOAT) {
		if (value->float_value == NULL || value->float_value[0] == '\0') {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "float literal value must not be empty");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		*out_sql = sqlparser_strdup(value->float_value);
	} else if (value->kind == SQLPARSER_LITERAL_KIND_BOOLEAN) {
		*out_sql = sqlparser_strdup(value->boolean_value ? "TRUE" : "FALSE");
	} else if (value->kind == SQLPARSER_LITERAL_KIND_STRING) {
		text = value->string_value != NULL ? value->string_value : "";
		memset(&out, 0, sizeof(out));
		status = sqlparser_oracle_buffer_append_char(&out, '\'', out_error);
		for (index = 0U; status == SQLPARSER_STATUS_OK && text[index] != '\0'; index++) {
			if (text[index] == '\'') {
				status = sqlparser_oracle_buffer_append_cstr(&out, "''", out_error);
			} else {
				status = sqlparser_oracle_buffer_append_char(&out, text[index], out_error);
			}
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_append_char(&out, '\'', out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = sqlparser_oracle_buffer_finish(&out, out_error);
		}
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return status;
		}
		*out_sql = sqlparser_oracle_buffer_take(&out);
	} else {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "literal kind is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_oracle_multi_insert_replace_cell_public_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *candidate;
	sqlparser_handle_t *replacement;
	sqlparser_oracle_state_t *state;
	sqlparser_oracle_multi_insert_t *multi;
	sqlparser_oracle_multi_insert_branch_t *branch;
	sqlparser_parse_options_t options;
	char *public_sql;
	sqlparser_status_t status;

	if (handle == NULL || sql_text == NULL || sql_text[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert cell replacement arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (statement_index != 0U || !sqlparser_oracle_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not an Oracle multi-table INSERT cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	candidate = NULL;
	replacement = NULL;
	public_sql = NULL;
	status = sqlparser_handle_clone(handle, &candidate, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state = (sqlparser_oracle_state_t *)candidate->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (column_index >= branch->cell_count) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "cell index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	free(branch->cells[column_index].public_sql);
	branch->cells[column_index].public_sql = sqlparser_strdup(sql_text);
	if (branch->cells[column_index].public_sql == NULL) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_deparse(candidate, &public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(candidate);
		return status;
	}
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_ORACLE;
	options.limits = handle->limits;
	status = sqlparser_parse_with_options(public_sql, &options, &replacement, out_error);
	free(public_sql);
	sqlparser_handle_destroy(candidate);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	sqlparser_handle_replace_contents(handle, replacement);
	sqlparser_handle_destroy(replacement);
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_oracle_multi_insert_set_cell_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *sql_text,
	sqlparser_error_t *out_error)
{
	sqlparser_error_clear(out_error);
	return sqlparser_oracle_multi_insert_replace_cell_public_sql(
		handle,
		statement_index,
		branch_index,
		column_index,
		sql_text,
		out_error);
}

sqlparser_status_t sqlparser_oracle_multi_insert_cell_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_oracle_state_t *state;
	const sqlparser_oracle_multi_insert_t *multi;
	const sqlparser_oracle_multi_insert_branch_t *branch;

	sqlparser_error_clear(out_error);
	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || statement_index != 0U || !sqlparser_oracle_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not an Oracle multi-table INSERT cell");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	state = (const sqlparser_oracle_state_t *)handle->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (column_index >= branch->cell_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "cell index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = sqlparser_strdup(branch->cells[column_index].public_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_oracle_multi_insert_condition_sql(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const sqlparser_oracle_state_t *state;
	const sqlparser_oracle_multi_insert_t *multi;
	const sqlparser_oracle_multi_insert_branch_t *branch;

	sqlparser_error_clear(out_error);
	if (out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_sql must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (handle == NULL || statement_index != 0U || !sqlparser_oracle_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not an Oracle multi-table INSERT condition");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	state = (const sqlparser_oracle_state_t *)handle->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (!branch->has_condition || branch->condition_public_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch has no condition");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = sqlparser_strdup(branch->condition_public_sql);
	if (*out_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_oracle_multi_insert_set_cell_literal(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const sqlparser_literal_value_t *value,
	sqlparser_error_t *out_error)
{
	char *literal_sql;
	sqlparser_status_t status;

	literal_sql = NULL;
	sqlparser_error_clear(out_error);
	status = sqlparser_oracle_render_literal_value(value, &literal_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_oracle_multi_insert_replace_cell_public_sql(
		handle,
		statement_index,
		branch_index,
		column_index,
		literal_sql,
		out_error);
	free(literal_sql);
	return status;
}

sqlparser_status_t sqlparser_oracle_multi_insert_set_cell_bind(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const sqlparser_bind_value_t *bind,
	sqlparser_error_t *out_error)
{
	char *bind_sql;
	sqlparser_status_t status;

	bind_sql = NULL;
	sqlparser_error_clear(out_error);
	status = sqlparser_oracle_render_bind_value(bind, &bind_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = sqlparser_oracle_multi_insert_replace_cell_public_sql(
		handle,
		statement_index,
		branch_index,
		column_index,
		bind_sql,
		out_error);
	free(bind_sql);
	return status;
}

sqlparser_status_t sqlparser_oracle_multi_insert_insert_column_sql(
	sqlparser_handle_t *handle,
	size_t statement_index,
	size_t branch_index,
	size_t column_index,
	const char *column_sql,
	const char *cell_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *candidate;
	sqlparser_handle_t *replacement;
	sqlparser_oracle_state_t *state;
	sqlparser_oracle_multi_insert_t *multi;
	sqlparser_oracle_multi_insert_branch_t *branch;
	sqlparser_oracle_column_t new_column;
	sqlparser_oracle_value_t new_cell;
	sqlparser_oracle_column_t *next_columns;
	sqlparser_oracle_value_t *next_cells;
	sqlparser_parse_options_t options;
	char *public_sql;
	size_t column_insert_index;
	size_t cell_insert_index;
	size_t index;
	sqlparser_status_t status;

	sqlparser_error_clear(out_error);
	if (handle == NULL || column_sql == NULL || column_sql[0] == '\0' ||
	    cell_sql == NULL || cell_sql[0] == '\0') {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert column insertion arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (statement_index != 0U || !sqlparser_oracle_state_has_multi_insert(handle->dialect_state)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "selector is not an Oracle multi-table INSERT branch");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(&new_column, 0, sizeof(new_column));
	memset(&new_cell, 0, sizeof(new_cell));
	candidate = NULL;
	replacement = NULL;
	next_columns = NULL;
	next_cells = NULL;
	public_sql = NULL;

	status = sqlparser_handle_clone(handle, &candidate, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	state = (sqlparser_oracle_state_t *)candidate->dialect_state;
	multi = state != NULL ? state->multi_insert : NULL;
	if (multi == NULL || branch_index >= multi->branch_count) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "branch index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	branch = &multi->branches[branch_index];
	if (branch->column_count == SIZE_MAX || branch->cell_count == SIZE_MAX) {
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "multi insert branch is too large");
		return SQLPARSER_STATUS_RESOURCE_LIMIT;
	}

	status = sqlparser_oracle_trimmed_slice_dup(column_sql, 0U, strlen(column_sql), &new_column.sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_identifier_from_sql(
			column_sql,
			0U,
			strlen(column_sql),
			&new_column.name,
			out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_parse_value_item(
			cell_sql,
			0U,
			strlen(cell_sql),
			state,
			&new_cell,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_column_clear(&new_column);
		sqlparser_oracle_value_clear(&new_cell);
		sqlparser_handle_destroy(candidate);
		return status;
	}

	next_columns = (sqlparser_oracle_column_t *)calloc(branch->column_count + 1U, sizeof(*next_columns));
	next_cells = (sqlparser_oracle_value_t *)calloc(branch->cell_count + 1U, sizeof(*next_cells));
	if (next_columns == NULL || next_cells == NULL) {
		free(next_columns);
		free(next_cells);
		sqlparser_oracle_column_clear(&new_column);
		sqlparser_oracle_value_clear(&new_cell);
		sqlparser_handle_destroy(candidate);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	column_insert_index = column_index > branch->column_count ? branch->column_count : column_index;
	cell_insert_index = column_index > branch->cell_count ? branch->cell_count : column_index;
	for (index = 0U; index < column_insert_index; index++) {
		next_columns[index] = branch->columns[index];
	}
	next_columns[column_insert_index] = new_column;
	memset(&new_column, 0, sizeof(new_column));
	for (index = column_insert_index; index < branch->column_count; index++) {
		next_columns[index + 1U] = branch->columns[index];
	}
	for (index = 0U; index < cell_insert_index; index++) {
		next_cells[index] = branch->cells[index];
	}
	next_cells[cell_insert_index] = new_cell;
	memset(&new_cell, 0, sizeof(new_cell));
	for (index = cell_insert_index; index < branch->cell_count; index++) {
		next_cells[index + 1U] = branch->cells[index];
	}
	free(branch->columns);
	free(branch->cells);
	branch->columns = next_columns;
	branch->column_count++;
	branch->cells = next_cells;
	branch->cell_count++;
	next_columns = NULL;
	next_cells = NULL;

	status = sqlparser_deparse(candidate, &public_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_ORACLE;
		options.limits = handle->limits;
		status = sqlparser_parse_with_options(public_sql, &options, &replacement, out_error);
	}
	free(public_sql);
	sqlparser_handle_destroy(candidate);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	sqlparser_handle_replace_contents(handle, replacement);
	sqlparser_handle_destroy(replacement);
	return SQLPARSER_STATUS_OK;
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
	clone->bind_occurrence_count = source->bind_occurrence_count;

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

	status = sqlparser_oracle_multi_insert_clone(source->multi_insert, &clone->multi_insert, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_state_destroy(clone);
		return status;
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
