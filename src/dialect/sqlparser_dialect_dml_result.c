#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_dml_result_internal.h"
#include "sqlparser_dialect_dameng_internal.h"
#include "sqlparser_dialect_oracle_internal.h"
#include "sqlparser_dialect_sqlserver_internal.h"

static int sqlparser_returning_into_is_ident_char(unsigned char value)
{
	return isalnum(value) || value == '_' || value == '$' ||
		value == '#' || value >= 0x80U;
}

static size_t sqlparser_returning_into_word_end(
	const char *sql,
	size_t start)
{
	size_t end;

	end = start;
	while (sqlparser_returning_into_is_ident_char(
		       (unsigned char)sql[end])) {
		end++;
	}
	return end;
}

static int sqlparser_returning_into_word_equal(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	size_t index;
	size_t length;

	length = word != NULL ? strlen(word) : 0U;
	if (sql == NULL || word == NULL || end - start != length ||
	    (start > 0U && sqlparser_returning_into_is_ident_char(
				  (unsigned char)sql[start - 1U])) ||
	    sqlparser_returning_into_is_ident_char((unsigned char)sql[end])) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (tolower((unsigned char)sql[start + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_returning_into_word_at(
	const char *sql,
	size_t start,
	const char *word)
{
	size_t index;
	size_t length;

	if (sql == NULL || word == NULL ||
	    (start > 0U && sqlparser_returning_into_is_ident_char(
			  (unsigned char)sql[start - 1U]))) {
		return 0;
	}
	length = strlen(word);
	for (index = 0U; index < length; index++) {
		if (sql[start + index] == '\0' ||
		    tolower((unsigned char)sql[start + index]) !=
			    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return !sqlparser_returning_into_is_ident_char(
		(unsigned char)sql[start + length]);
}

static size_t sqlparser_returning_into_skip_trivia(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t start)
{
	size_t next;

	while (sql[start] != '\0') {
		while (isspace((unsigned char)sql[start])) {
			start++;
		}
		if (!((sql[start] == '-' && sql[start + 1U] == '-') ||
		      (sql[start] == '/' && sql[start + 1U] == '*'))) {
			break;
		}
		next = sqlparser_public_skip_quoted_or_comment(
			dialect, sql, start);
		if (next <= start) {
			break;
		}
		start = next;
	}
	return start;
}

static int sqlparser_returning_into_statement_context(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t keyword_start,
	size_t *out_statement_index)
{
	size_t depth;
	size_t position;
	size_t statement_index;
	int statement_is_dml;
	int statement_word_seen;

	depth = 0U;
	position = 0U;
	statement_index = 0U;
	statement_is_dml = 0;
	statement_word_seen = 0;
	while (position < keyword_start) {
		size_t next;
		size_t word_end;

		next = sqlparser_public_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			position++;
			continue;
		}
		if (sql[position] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			position++;
			continue;
		}
		if (depth == 0U && sql[position] == ';') {
			if (statement_word_seen) {
				statement_index++;
			}
			statement_is_dml = 0;
			statement_word_seen = 0;
			position++;
			continue;
		}
		if (!sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) ||
		    isdigit((unsigned char)sql[position])) {
			position++;
			continue;
		}
		word_end = sqlparser_returning_into_word_end(sql, position);
		if (depth == 0U && !statement_word_seen) {
			statement_word_seen = 1;
			statement_is_dml =
				sqlparser_returning_into_word_equal(
					sql, position, word_end, "insert") ||
				sqlparser_returning_into_word_equal(
					sql, position, word_end, "update") ||
				sqlparser_returning_into_word_equal(
					sql, position, word_end, "delete");
		}
		position = word_end;
	}
	if (depth != 0U || !statement_is_dml) {
		return 0;
	}
	if (out_statement_index != NULL) {
		*out_statement_index = statement_index;
	}
	return 1;
}

static size_t sqlparser_returning_into_bind_end(
	const char *sql,
	size_t start)
{
	size_t end;

	if (sql[start] != ':' || sql[start + 1U] == '\0' ||
	    sql[start + 1U] == ':' || sql[start + 1U] == '=') {
		return start;
	}
	end = start + 1U;
	if (isdigit((unsigned char)sql[end])) {
		while (isdigit((unsigned char)sql[end])) {
			end++;
		}
		return end;
	}
	if (!isalpha((unsigned char)sql[end]) && sql[end] != '_' &&
	    (unsigned char)sql[end] < 0x80U) {
		return start;
	}
	end++;
	while (sqlparser_returning_into_is_ident_char(
		       (unsigned char)sql[end])) {
		end++;
	}
	return end;
}

int sqlparser_dialect_returning_into_receiver_is_bind(
	sqlparser_dialect_t dialect,
	const char *sql)
{
	size_t end;
	size_t skipped_end;
	size_t start;

	if (sql == NULL) {
		return 0;
	}
	start = sqlparser_returning_into_skip_trivia(dialect, sql, 0U);
	end = sqlparser_returning_into_bind_end(sql, start);
	skipped_end = sqlparser_returning_into_skip_trivia(dialect, sql, end);
	return end > start && sql[skipped_end] == '\0';
}

static int sqlparser_returning_into_bulk_collect_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t start)
{
	size_t position;

	if (!sqlparser_returning_into_word_at(sql, start, "bulk")) {
		return 0;
	}
	position = sqlparser_returning_into_skip_trivia(
		dialect, sql, start + strlen("bulk"));
	if (!sqlparser_returning_into_word_at(sql, position, "collect")) {
		return 0;
	}
	position = sqlparser_returning_into_skip_trivia(
		dialect, sql, position + strlen("collect"));
	return sqlparser_returning_into_word_at(sql, position, "into");
}

int sqlparser_dialect_returning_into_clause_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t keyword_start,
	int allow_return_keyword,
	sqlparser_dialect_returning_into_clause_t *out_clause)
{
	sqlparser_dialect_returning_into_clause_t clause;
	size_t depth;
	size_t keyword_length;
	size_t position;
	size_t sink_end;
	size_t sink_start;
	size_t sink_count;
	size_t target_count;
	int found_into;
	int target_has_code;

	memset(&clause, 0, sizeof(clause));
	if (sql == NULL || out_clause == NULL) {
		return 0;
	}
	if (sqlparser_returning_into_word_at(
		    sql, keyword_start, "returning")) {
		keyword_length = strlen("returning");
	} else if (allow_return_keyword &&
		   sqlparser_returning_into_word_at(
			   sql, keyword_start, "return")) {
		keyword_length = strlen("return");
		clause.uses_return_keyword = 1;
	} else {
		return 0;
	}
	if (!sqlparser_returning_into_statement_context(
		    dialect,
		    sql,
		    keyword_start,
		    &clause.statement_index)) {
		return 0;
	}
	clause.keyword_end = keyword_start + keyword_length;
	depth = 0U;
	position = clause.keyword_end;
	found_into = 0;
	target_count = 0U;
	target_has_code = 0;
	while (sql[position] != '\0') {
		size_t next;
		size_t word_end;

		next = sqlparser_public_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			if (!((sql[position] == '-' && sql[position + 1U] == '-') ||
			      (sql[position] == '/' && sql[position + 1U] == '*'))) {
				target_has_code = 1;
			}
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			target_has_code = 1;
			position++;
			continue;
		}
		if (sql[position] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			target_has_code = 1;
			position++;
			continue;
		}
		if (depth == 0U && sql[position] == ';') {
			return 0;
		}
		if (depth == 0U && sql[position] == ',') {
			if (!target_has_code) {
				return 0;
			}
			target_count++;
			target_has_code = 0;
			position++;
			continue;
		}
		if (depth == 0U &&
		    sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) &&
		    !isdigit((unsigned char)sql[position])) {
			if (sqlparser_returning_into_bulk_collect_at(
				    dialect, sql, position)) {
				return 0;
			}
			word_end = sqlparser_returning_into_word_end(
				sql, position);
			if (sqlparser_returning_into_word_equal(
				    sql, position, word_end, "into")) {
				if (!target_has_code) {
					return 0;
				}
				target_count++;
				clause.into_start = position;
				found_into = 1;
				break;
			}
			position = word_end;
			target_has_code = 1;
			continue;
		}
		if (!isspace((unsigned char)sql[position])) {
			target_has_code = 1;
		}
		position++;
	}
	if (!found_into || target_count == 0U) {
		return 0;
	}
	sink_start = sqlparser_returning_into_skip_trivia(
		dialect,
		sql,
		clause.into_start + strlen("into"));
	sink_end = sqlparser_returning_into_bind_end(sql, sink_start);
	if (sink_end == sink_start) {
		return 0;
	}
	sink_count = 0U;
	for (;;) {
		sink_count++;
		position = sqlparser_returning_into_skip_trivia(
			dialect, sql, sink_end);
		if (sql[position] != ',') {
			break;
		}
		sink_start = sqlparser_returning_into_skip_trivia(
			dialect, sql, position + 1U);
		sink_end = sqlparser_returning_into_bind_end(sql, sink_start);
		if (sink_end == sink_start) {
			return 0;
		}
	}
	if ((sql[position] != '\0' && sql[position] != ';') ||
	    sink_count != target_count) {
		return 0;
	}
	clause.pair_count = target_count;
	*out_clause = clause;
	return 1;
}

sqlparser_status_t sqlparser_dialect_returning_into_validate(
	sqlparser_dialect_t dialect,
	const char *sql,
	int allow_return_keyword,
	sqlparser_error_t *out_error)
{
	size_t position;

	if (sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	position = 0U;
	while (sql[position] != '\0') {
		size_t next;
		size_t word_end;

		next = sqlparser_public_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			position = next;
			continue;
		}
		if (sql[position] == ':') {
			next = sqlparser_returning_into_bind_end(sql, position);
			if (next > position) {
				position = next;
				continue;
			}
		}
		if (!sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) ||
		    isdigit((unsigned char)sql[position])) {
			position++;
			continue;
		}
		word_end = sqlparser_returning_into_word_end(sql, position);
		if (sqlparser_returning_into_word_equal(
			    sql, position, word_end, "returning") ||
		    (allow_return_keyword &&
		     sqlparser_returning_into_word_equal(
			     sql, position, word_end, "return"))) {
			sqlparser_dialect_returning_into_clause_t clause;
			size_t depth;

			if (!sqlparser_dialect_returning_into_clause_at(
				    dialect,
				    sql,
				    position,
				    allow_return_keyword,
				    &clause)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"unsupported RETURN/RETURNING ... INTO syntax");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			depth = 0U;
			position = clause.into_start + strlen("into");
			while (sql[position] != '\0') {
				next = sqlparser_public_skip_quoted_or_comment(
					dialect, sql, position);
				if (next > position) {
					position = next;
					continue;
				}
				if (sql[position] == '(') {
					depth++;
				} else if (sql[position] == ')' && depth > 0U) {
					depth--;
				} else if (sql[position] == ';' && depth == 0U) {
					position++;
					break;
				}
				position++;
			}
			continue;
		}
		position = word_end;
	}
	return SQLPARSER_STATUS_OK;
}

void sqlparser_dialect_returning_into_state_clear(
	sqlparser_dialect_returning_into_state_t *state)
{
	if (state == NULL) {
		return;
	}
	free(state->items);
	memset(state, 0, sizeof(*state));
}

sqlparser_status_t sqlparser_dialect_returning_into_state_clone(
	const sqlparser_dialect_returning_into_state_t *source,
	sqlparser_dialect_returning_into_state_t *target,
	sqlparser_error_t *out_error)
{
	if (target == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"RETURNING INTO state clone output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(target, 0, sizeof(*target));
	if (source == NULL || source->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (source->count > SIZE_MAX / sizeof(*target->items)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	target->items = (sqlparser_dialect_returning_into_item_t *)malloc(
		source->count * sizeof(*target->items));
	if (target->items == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memcpy(
		target->items,
		source->items,
		source->count * sizeof(*target->items));
	target->count = source->count;
	target->capacity = source->count;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dialect_returning_into_state_append(
	sqlparser_dialect_returning_into_state_t *state,
	size_t statement_index,
	const char *keyword,
	size_t keyword_length,
	const char *into_keyword,
	size_t pair_count,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_returning_into_item_t *items;
	size_t capacity;
	size_t index;
	uint16_t keyword_uppercase_mask;
	uint8_t into_uppercase_mask;

	if (state == NULL || keyword == NULL || into_keyword == NULL ||
	    pair_count == 0U ||
	    (keyword_length != strlen("return") &&
	     keyword_length != strlen("returning"))) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"RETURNING INTO state input is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	keyword_uppercase_mask = 0U;
	for (index = 0U; index < keyword_length; index++) {
		if (isupper((unsigned char)keyword[index])) {
			keyword_uppercase_mask |= (uint16_t)(1U << index);
		}
	}
	into_uppercase_mask = 0U;
	for (index = 0U; index < strlen("into"); index++) {
		if (isupper((unsigned char)into_keyword[index])) {
			into_uppercase_mask |= (uint8_t)(1U << index);
		}
	}
	for (index = 0U; index < state->count; index++) {
		if (state->items[index].statement_index == statement_index) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_UNSUPPORTED,
				"multiple RETURNING INTO clauses in one statement are unsupported");
			return SQLPARSER_STATUS_UNSUPPORTED;
		}
	}
	if (state->count == state->capacity) {
		capacity = state->capacity == 0U ? 2U : state->capacity * 2U;
		if (capacity < state->capacity ||
		    capacity > SIZE_MAX / sizeof(*items)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		items = (sqlparser_dialect_returning_into_item_t *)realloc(
			state->items, capacity * sizeof(*items));
		if (items == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		state->items = items;
		state->capacity = capacity;
	}
	state->items[state->count].statement_index = statement_index;
	state->items[state->count].pair_count = pair_count;
	state->items[state->count].keyword_uppercase_mask =
		keyword_uppercase_mask;
	state->items[state->count].into_uppercase_mask = into_uppercase_mask;
	state->items[state->count].uses_return_keyword =
		keyword_length == strlen("return");
	state->count++;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_returning_into_statement_span(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t target_statement_index,
	size_t *out_start,
	size_t *out_end)
{
	size_t depth;
	size_t position;
	size_t start;
	size_t statement_index;
	int statement_has_code;

	depth = 0U;
	position = 0U;
	start = 0U;
	statement_index = 0U;
	statement_has_code = 0;
	while (sql[position] != '\0') {
		size_t next;

		next = sqlparser_public_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			if (!((sql[position] == '-' && sql[position + 1U] == '-') ||
			      (sql[position] == '/' && sql[position + 1U] == '*'))) {
				statement_has_code = 1;
			}
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			statement_has_code = 1;
		} else if (sql[position] == ')') {
			if (depth > 0U) {
				depth--;
			}
		} else if (depth == 0U && sql[position] == ';') {
			if (statement_has_code) {
				if (statement_index == target_statement_index) {
					*out_start = start;
					*out_end = position;
					return 1;
				}
				statement_index++;
			}
			start = position + 1U;
			statement_has_code = 0;
		} else if (!isspace((unsigned char)sql[position])) {
			statement_has_code = 1;
		}
		position++;
	}
	if (!statement_has_code || statement_index != target_statement_index) {
		return 0;
	}
	*out_start = start;
	*out_end = position;
	return 1;
}

static int sqlparser_returning_into_deparse_parts(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t statement_start,
	size_t statement_end,
	size_t pair_count,
	size_t *out_keyword_start,
	size_t *out_keyword_end,
	size_t *out_target_end,
	size_t *out_sink_start,
	size_t *out_sink_end)
{
	size_t comma;
	size_t comma_count;
	size_t depth;
	size_t keyword_end;
	size_t keyword_start;
	size_t position;
	size_t sink_count;
	int part_has_code;

	if (pair_count == 0U) {
		return 0;
	}

	keyword_start = SIZE_MAX;
	keyword_end = SIZE_MAX;
	depth = 0U;
	position = statement_start;
	while (position < statement_end) {
		size_t next;
		size_t word_end;

		next = sqlparser_public_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			position++;
			continue;
		}
		if (sql[position] == ')') {
			if (depth > 0U) {
				depth--;
			}
			position++;
			continue;
		}
		if (depth == 0U &&
		    sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) &&
		    !isdigit((unsigned char)sql[position])) {
			word_end = sqlparser_returning_into_word_end(
				sql, position);
			if (sqlparser_returning_into_word_equal(
				    sql, position, word_end, "returning")) {
				keyword_start = position;
				keyword_end = word_end;
				break;
			}
			position = word_end;
			continue;
		}
		position++;
	}
	if (keyword_start == SIZE_MAX) {
		return 0;
	}
	comma = SIZE_MAX;
	comma_count = 0U;
	depth = 0U;
	position = keyword_end;
	part_has_code = 0;
	while (position < statement_end) {
		size_t next;

		next = sqlparser_public_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			if (!((sql[position] == '-' && sql[position + 1U] == '-') ||
			      (sql[position] == '/' && sql[position + 1U] == '*'))) {
				part_has_code = 1;
			}
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			part_has_code = 1;
		} else if (sql[position] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			part_has_code = 1;
		} else if (depth == 0U && sql[position] == ',') {
			if (!part_has_code) {
				return 0;
			}
			comma_count++;
			if (comma_count == pair_count) {
				comma = position;
				break;
			}
			part_has_code = 0;
		} else if (!isspace((unsigned char)sql[position])) {
			part_has_code = 1;
		}
		position++;
	}
	if (comma == SIZE_MAX) {
		return 0;
	}
	*out_target_end = comma;
	while (*out_target_end > keyword_end &&
	       isspace((unsigned char)sql[*out_target_end - 1U])) {
		(*out_target_end)--;
	}
	*out_sink_start = comma + 1U;
	while (*out_sink_start < statement_end &&
	       isspace((unsigned char)sql[*out_sink_start])) {
		(*out_sink_start)++;
	}
	*out_sink_end = statement_end;
	while (*out_sink_end > *out_sink_start &&
	       isspace((unsigned char)sql[*out_sink_end - 1U])) {
		(*out_sink_end)--;
	}
	if (*out_target_end <= keyword_end ||
	    *out_sink_start >= *out_sink_end) {
		return 0;
	}
	depth = 0U;
	position = *out_sink_start;
	sink_count = 0U;
	part_has_code = 0;
	while (position < *out_sink_end) {
		size_t next;

		next = sqlparser_public_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			if (!((sql[position] == '-' && sql[position + 1U] == '-') ||
			      (sql[position] == '/' && sql[position + 1U] == '*'))) {
				part_has_code = 1;
			}
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			part_has_code = 1;
		} else if (sql[position] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			part_has_code = 1;
		} else if (depth == 0U && sql[position] == ',') {
			if (!part_has_code) {
				return 0;
			}
			sink_count++;
			part_has_code = 0;
		} else if (!isspace((unsigned char)sql[position])) {
			part_has_code = 1;
		}
		position++;
	}
	if (depth != 0U || !part_has_code) {
		return 0;
	}
	sink_count++;
	if (sink_count != pair_count) {
		return 0;
	}
	*out_keyword_start = keyword_start;
	*out_keyword_end = keyword_end;
	return 1;
}

static sqlparser_status_t sqlparser_returning_into_postprocess_item(
	sqlparser_dialect_t dialect,
	char **io_sql,
	const sqlparser_dialect_returning_into_item_t *item,
	sqlparser_error_t *out_error)
{
	const char *keyword_base;
	char into_keyword[sizeof("into")];
	char keyword[sizeof("returning")];
	char *output;
	size_t index;
	size_t keyword_end;
	size_t keyword_length;
	size_t keyword_start;
	size_t output_length;
	size_t sink_end;
	size_t sink_start;
	size_t statement_end;
	size_t statement_start;
	size_t suffix_length;
	size_t target_end;
	size_t target_length;
	size_t write;

	if (!sqlparser_returning_into_statement_span(
		    dialect,
		    *io_sql,
		    item->statement_index,
		    &statement_start,
		    &statement_end) ||
	    !sqlparser_returning_into_deparse_parts(
		    dialect,
		    *io_sql,
		    statement_start,
		    statement_end,
		    item->pair_count,
		    &keyword_start,
		    &keyword_end,
		    &target_end,
		    &sink_start,
		    &sink_end)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"RETURNING INTO deparse shape is inconsistent");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	keyword_base = item->uses_return_keyword ? "return" : "returning";
	keyword_length = strlen(keyword_base);
	for (index = 0U; index < keyword_length; index++) {
		keyword[index] =
			(item->keyword_uppercase_mask & (uint16_t)(1U << index)) != 0U ?
				(char)toupper((unsigned char)keyword_base[index]) :
				keyword_base[index];
	}
	for (index = 0U; index < strlen("into"); index++) {
		into_keyword[index] =
			(item->into_uppercase_mask & (uint8_t)(1U << index)) != 0U ?
				(char)toupper((unsigned char)"into"[index]) :
				"into"[index];
	}
	target_length = target_end - keyword_end;
	suffix_length = strlen(*io_sql) - sink_end;
	if (keyword_start > SIZE_MAX - keyword_length ||
	    keyword_start + keyword_length > SIZE_MAX - target_length ||
	    keyword_start + keyword_length + target_length > SIZE_MAX - 6U ||
	    keyword_start + keyword_length + target_length + 6U >
		    SIZE_MAX - (sink_end - sink_start) ||
	    keyword_start + keyword_length + target_length + 6U +
		    (sink_end - sink_start) > SIZE_MAX - suffix_length - 1U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	output_length = keyword_start + keyword_length + target_length +
		6U + (sink_end - sink_start) + suffix_length;
	output = (char *)malloc(output_length + 1U);
	if (output == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	write = 0U;
	memcpy(output + write, *io_sql, keyword_start);
	write += keyword_start;
	memcpy(output + write, keyword, keyword_length);
	write += keyword_length;
	memcpy(output + write, *io_sql + keyword_end, target_length);
	write += target_length;
	output[write++] = ' ';
	memcpy(output + write, into_keyword, strlen("into"));
	write += strlen("into");
	output[write++] = ' ';
	memcpy(output + write, *io_sql + sink_start, sink_end - sink_start);
	write += sink_end - sink_start;
	memcpy(output + write, *io_sql + sink_end, suffix_length);
	write += suffix_length;
	output[write] = '\0';
	free(*io_sql);
	*io_sql = output;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dialect_returning_into_postprocess(
	sqlparser_dialect_t dialect,
	char **io_sql,
	const sqlparser_dialect_returning_into_state_t *state,
	sqlparser_error_t *out_error)
{
	size_t index;
	sqlparser_status_t status;

	if (io_sql == NULL || *io_sql == NULL || state == NULL ||
	    state->count == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	index = state->count;
	while (index > 0U) {
		index--;
		status = sqlparser_returning_into_postprocess_item(
			dialect, io_sql, &state->items[index], out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	return SQLPARSER_STATUS_OK;
}

static const sqlparser_dialect_returning_into_state_t *
sqlparser_dialect_returning_into_state(
	sqlparser_dialect_t dialect,
	const void *state)
{
	const sqlparser_dialect_returning_into_state_t *returning_into;

	returning_into = NULL;
	if (sqlparser_dialect_is_oracle_compatible(dialect)) {
		returning_into = sqlparser_oracle_state_returning_into(state);
	} else if (dialect == SQLPARSER_DIALECT_DAMENG) {
		returning_into = sqlparser_dameng_state_returning_into(state);
	}
	return returning_into != NULL && returning_into->count > 0U ?
		returning_into : NULL;
}

static const sqlparser_dialect_returning_into_item_t *
sqlparser_dialect_returning_into_item(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index)
{
	const sqlparser_dialect_returning_into_state_t *returning_into;
	size_t index;

	returning_into = sqlparser_dialect_returning_into_state(
		dialect, state);
	if (returning_into == NULL) {
		return NULL;
	}
	for (index = 0U; index < returning_into->count; index++) {
		if (returning_into->items[index].statement_index ==
		    statement_index) {
			return &returning_into->items[index];
		}
	}
	return NULL;
}

static sqlparser_dialect_returning_into_item_t *
sqlparser_dialect_returning_into_item_mutable(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index)
{
	sqlparser_dialect_returning_into_state_t *returning_into;
	size_t index;

	returning_into = (sqlparser_dialect_returning_into_state_t *)
		sqlparser_dialect_returning_into_state(dialect, state);
	if (returning_into == NULL) {
		return NULL;
	}
	for (index = 0U; index < returning_into->count; index++) {
		if (returning_into->items[index].statement_index ==
		    statement_index) {
			return &returning_into->items[index];
		}
	}
	return NULL;
}

static int sqlparser_dialect_has_returning_into(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index)
{
	return sqlparser_dialect_returning_into_item(
		       dialect, state, statement_index) != NULL;
}

static const sqlparser_sqlserver_output_state_t *sqlparser_dialect_sqlserver_output_state(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	if (out_local_statement_index != NULL) {
		*out_local_statement_index = statement_index;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		return NULL;
	}
	return sqlparser_sqlserver_state_output(
		state, statement_index, out_local_statement_index);
}

static sqlparser_sqlserver_output_state_t *sqlparser_dialect_sqlserver_output_state_mutable(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	if (out_local_statement_index != NULL) {
		*out_local_statement_index = statement_index;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		return NULL;
	}
	return sqlparser_sqlserver_state_output_mutable(
		state, statement_index, out_local_statement_index);
}

static unsigned int sqlparser_dialect_postgresql_merge_target_reference_kinds(
	const PgQuery__MergeStmt *stmt)
{
	unsigned int kinds;
	size_t index;

	kinds = SQLPARSER_DIALECT_DML_TARGET_REFERENCE_NONE;
	if (stmt == NULL || stmt->merge_when_clauses == NULL) {
		return kinds;
	}
	for (index = 0U; index < stmt->n_merge_when_clauses; index++) {
		PgQuery__Node *when_node;
		PgQuery__MergeWhenClause *when_clause;

		when_node = stmt->merge_when_clauses[index];
		when_clause = when_node != NULL &&
			when_node->node_case == PG_QUERY__NODE__NODE_MERGE_WHEN_CLAUSE ?
			when_node->merge_when_clause : NULL;
		if (when_clause == NULL) {
			continue;
		}
		switch (when_clause->command_type) {
			case PG_QUERY__CMD_TYPE__CMD_INSERT:
			case PG_QUERY__CMD_TYPE__CMD_UPDATE:
				kinds |= SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER;
				break;
			case PG_QUERY__CMD_TYPE__CMD_DELETE:
				kinds |= SQLPARSER_DIALECT_DML_TARGET_REFERENCE_BEFORE;
				break;
			default:
				break;
		}
	}
	return kinds;
}

static int sqlparser_dialect_postgresql_dml_node(
	PgQuery__Node *node,
	sqlparser_dialect_dml_result_dml_t *out_dml)
{
	if (node == NULL || out_dml == NULL) {
		return 0;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	switch (node->node_case) {
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			if (node->insert_stmt != NULL) {
				out_dml->kind = SQLPARSER_GRAPH_DML_INSERT;
				out_dml->target_count = node->insert_stmt->n_returning_list;
				out_dml->message = (ProtobufCMessage *)node->insert_stmt;
				out_dml->target_reference_kinds =
					SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER;
			}
			break;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			if (node->update_stmt != NULL) {
				out_dml->kind = SQLPARSER_GRAPH_DML_UPDATE;
				out_dml->target_count = node->update_stmt->n_returning_list;
				out_dml->message = (ProtobufCMessage *)node->update_stmt;
				out_dml->target_reference_kinds =
					SQLPARSER_DIALECT_DML_TARGET_REFERENCE_AFTER;
			}
			break;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			if (node->delete_stmt != NULL) {
				out_dml->kind = SQLPARSER_GRAPH_DML_DELETE;
				out_dml->target_count = node->delete_stmt->n_returning_list;
				out_dml->message = (ProtobufCMessage *)node->delete_stmt;
				out_dml->target_reference_kinds =
					SQLPARSER_DIALECT_DML_TARGET_REFERENCE_BEFORE;
			}
			break;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			if (node->merge_stmt != NULL) {
				out_dml->kind = SQLPARSER_GRAPH_DML_MERGE;
				out_dml->target_count = node->merge_stmt->n_returning_list;
				out_dml->message = (ProtobufCMessage *)node->merge_stmt;
				out_dml->target_reference_kinds =
					sqlparser_dialect_postgresql_merge_target_reference_kinds(
						node->merge_stmt);
			}
			break;
		default:
			break;
	}
	if (out_dml->message == NULL) {
		return 0;
	}
	out_dml->channel_count = out_dml->target_count > 0U ? 1U : 0U;
	return 1;
}

static PgQuery__WithClause *sqlparser_dialect_postgresql_with_clause(
	PgQuery__Node *statement)
{
	if (statement == NULL) {
		return NULL;
	}
	switch (statement->node_case) {
		case PG_QUERY__NODE__NODE_SELECT_STMT:
			return statement->select_stmt != NULL ?
				statement->select_stmt->with_clause : NULL;
		case PG_QUERY__NODE__NODE_INSERT_STMT:
			return statement->insert_stmt != NULL ?
				statement->insert_stmt->with_clause : NULL;
		case PG_QUERY__NODE__NODE_UPDATE_STMT:
			return statement->update_stmt != NULL ?
				statement->update_stmt->with_clause : NULL;
		case PG_QUERY__NODE__NODE_DELETE_STMT:
			return statement->delete_stmt != NULL ?
				statement->delete_stmt->with_clause : NULL;
		case PG_QUERY__NODE__NODE_MERGE_STMT:
			return statement->merge_stmt != NULL ?
				statement->merge_stmt->with_clause : NULL;
		default:
			return NULL;
	}
}

static PgQuery__Node *sqlparser_dialect_postgresql_statement(
	const sqlparser_handle_t *handle,
	size_t statement_index)
{
	if (handle == NULL ||
	    (!sqlparser_dialect_uses_postgresql_placeholders(handle->dialect) &&
	     !sqlparser_dialect_has_returning_into(
		     handle->dialect,
		     handle->dialect_state,
		     statement_index)) ||
	    handle->ast == NULL || handle->ast->stmts == NULL ||
	    statement_index >= handle->ast->n_stmts ||
	    handle->ast->stmts[statement_index] == NULL) {
		return NULL;
	}
	return handle->ast->stmts[statement_index]->stmt;
}

typedef struct {
	sqlparser_dialect_dml_result_visit_fn visitor;
	void *context;
	size_t count;
} sqlparser_dialect_postgresql_dml_visit_state_t;

static int sqlparser_dialect_postgresql_dml_visit_node(
	sqlparser_dialect_postgresql_dml_visit_state_t *state,
	PgQuery__Node *node,
	PgQuery__CommonTableExpr *cte,
	size_t parent_dml_index,
	int has_parent)
{
	PgQuery__WithClause *with_clause;
	sqlparser_dialect_dml_result_dml_t dml;
	size_t child_parent_dml_index;
	size_t index;
	int child_has_parent;
	int status;

	if (state == NULL || node == NULL) {
		return 0;
	}
	memset(&dml, 0, sizeof(dml));
	child_parent_dml_index = parent_dml_index;
	child_has_parent = has_parent;
	if (sqlparser_dialect_postgresql_dml_node(node, &dml)) {
		dml.cte = cte;
		dml.parent_dml_index = parent_dml_index;
		dml.has_parent = has_parent;
		if (state->visitor != NULL) {
			status = state->visitor(state->count, &dml, state->context);
			if (status != 0) {
				return status;
			}
		}
		child_parent_dml_index = state->count;
		child_has_parent = 1;
		state->count++;
	}
	with_clause = sqlparser_dialect_postgresql_with_clause(node);
	if (with_clause == NULL || with_clause->ctes == NULL) {
		return 0;
	}
	for (index = 0U; index < with_clause->n_ctes; index++) {
		PgQuery__Node *cte_node;
		PgQuery__CommonTableExpr *child_cte;

		cte_node = with_clause->ctes[index];
		child_cte = cte_node != NULL &&
			cte_node->node_case == PG_QUERY__NODE__NODE_COMMON_TABLE_EXPR ?
			cte_node->common_table_expr : NULL;
		if (child_cte == NULL || child_cte->ctequery == NULL) {
			continue;
		}
		status = sqlparser_dialect_postgresql_dml_visit_node(
			state,
			child_cte->ctequery,
			child_cte,
			child_parent_dml_index,
			child_has_parent);
		if (status != 0) {
			return status;
		}
	}
	return 0;
}

int sqlparser_dialect_postgresql_dml_result_visit(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	sqlparser_dialect_dml_result_visit_fn visitor,
	void *context,
	size_t *out_count)
{
	sqlparser_dialect_postgresql_dml_visit_state_t state;
	PgQuery__Node *statement;
	int status;

	if (out_count != NULL) {
		*out_count = 0U;
	}
	statement = sqlparser_dialect_postgresql_statement(handle, statement_index);
	if (statement == NULL) {
		return 0;
	}
	memset(&state, 0, sizeof(state));
	state.visitor = visitor;
	state.context = context;
	status = sqlparser_dialect_postgresql_dml_visit_node(
		&state, statement, NULL, 0U, 0);
	if (out_count != NULL) {
		*out_count = state.count;
	}
	return status;
}

typedef struct {
	size_t target_index;
	sqlparser_dialect_dml_result_dml_t *out_dml;
	int found;
} sqlparser_dialect_postgresql_dml_at_context_t;

static int sqlparser_dialect_postgresql_dml_at_visitor(
	size_t dml_index,
	const sqlparser_dialect_dml_result_dml_t *dml,
	void *context)
{
	sqlparser_dialect_postgresql_dml_at_context_t *find;

	find = (sqlparser_dialect_postgresql_dml_at_context_t *)context;
	if (find == NULL || dml == NULL || dml_index != find->target_index) {
		return 0;
	}
	*find->out_dml = *dml;
	find->found = 1;
	return 1;
}

static int sqlparser_dialect_postgresql_dml_at(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	sqlparser_dialect_dml_result_dml_t *out_dml)
{
	sqlparser_dialect_postgresql_dml_at_context_t find;

	if (out_dml == NULL) {
		return 0;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	memset(&find, 0, sizeof(find));
	find.target_index = dml_index;
	find.out_dml = out_dml;
	(void)sqlparser_dialect_postgresql_dml_result_visit(
		handle,
		statement_index,
		sqlparser_dialect_postgresql_dml_at_visitor,
		&find,
		NULL);
	return find.found;
}

size_t sqlparser_dialect_dml_result_count(
	const sqlparser_handle_t *handle,
	size_t statement_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t local_statement_index;
	size_t count;
	size_t index;

	if (handle == NULL) {
		return 0U;
	}
	if (sqlparser_dialect_has_returning_into(
		    handle->dialect,
		    handle->dialect_state,
		    statement_index)) {
		return 1U;
	}
	if (sqlparser_dialect_uses_postgresql_placeholders(handle->dialect)) {
		count = 0U;
		(void)sqlparser_dialect_postgresql_dml_result_visit(
			handle,
			statement_index,
			NULL,
			NULL,
			&count);
		return count;
	}
	output = sqlparser_dialect_sqlserver_output_state(
		handle->dialect,
		handle->dialect_state,
		statement_index,
		&local_statement_index);
	count = 0U;
	for (index = 0U; index < sqlparser_sqlserver_output_dml_count(output); index++) {
		sqlparser_sqlserver_output_dml_view_t dml;

		if (sqlparser_sqlserver_output_dml_at(output, index, &dml) &&
		    dml.statement_index == local_statement_index) {
			count++;
		}
	}
	return count;
}

static int sqlparser_dialect_dml_global_index(
	const sqlparser_sqlserver_output_state_t *output,
	size_t statement_index,
	size_t dml_index,
	size_t *out_index)
{
	size_t index;
	size_t ordinal;

	ordinal = 0U;
	for (index = 0U; index < sqlparser_sqlserver_output_dml_count(output); index++) {
		sqlparser_sqlserver_output_dml_view_t dml;

		if (!sqlparser_sqlserver_output_dml_at(output, index, &dml) ||
		    dml.statement_index != statement_index) {
			continue;
		}
		if (ordinal == dml_index) {
			if (out_index != NULL) {
				*out_index = index;
			}
			return 1;
		}
		ordinal++;
	}
	return 0;
}

int sqlparser_dialect_dml_result_dml_at(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	sqlparser_dialect_dml_result_dml_t *out_dml)
{
	const sqlparser_dialect_returning_into_item_t *returning_into;
	const sqlparser_sqlserver_output_state_t *output;
	sqlparser_sqlserver_output_dml_view_t dml;
	size_t global_index;
	size_t local_statement_index;

	if (out_dml == NULL) {
		return 0;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	returning_into = handle != NULL ?
		sqlparser_dialect_returning_into_item(
			handle->dialect,
			handle->dialect_state,
			statement_index) : NULL;
	if (dml_index == 0U && returning_into != NULL) {
		if (!sqlparser_dialect_postgresql_dml_at(
			    handle, statement_index, dml_index, out_dml) ||
		    returning_into->pair_count > SIZE_MAX / 2U ||
		    out_dml->target_count != returning_into->pair_count * 2U) {
			return 0;
		}
		out_dml->target_count = returning_into->pair_count;
		out_dml->channel_count = 1U;
		return 1;
	}
	if (handle != NULL &&
	    sqlparser_dialect_uses_postgresql_placeholders(handle->dialect)) {
		return sqlparser_dialect_postgresql_dml_at(
			handle, statement_index, dml_index, out_dml);
	}
	if (handle == NULL) {
		return 0;
	}
	output = sqlparser_dialect_sqlserver_output_state(
		handle->dialect,
		handle->dialect_state,
		statement_index,
		&local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index) ||
	    !sqlparser_sqlserver_output_dml_at(output, global_index, &dml)) {
		return 0;
	}
	out_dml->kind = dml.kind;
	out_dml->channel_count = dml.channel_count;
	out_dml->parent_dml_index = dml.parent_dml_index;
	out_dml->has_parent = dml.has_parent;
	out_dml->source_name = dml.source_name;
	out_dml->source_sql = dml.source_sql;
	out_dml->has_duplicate_target_relation = dml.has_duplicate_target_relation;
	return 1;
}

int sqlparser_dialect_dml_result_channel_at(
	const sqlparser_handle_t *handle,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	sqlparser_dialect_dml_result_channel_t *out_channel)
{
	const sqlparser_dialect_returning_into_item_t *returning_into;
	const sqlparser_sqlserver_output_state_t *output;
	sqlparser_sqlserver_output_channel_view_t channel;
	size_t global_index;
	size_t local_statement_index;

	if (out_channel == NULL) {
		return 0;
	}
	memset(out_channel, 0, sizeof(*out_channel));
	returning_into = handle != NULL ?
		sqlparser_dialect_returning_into_item(
			handle->dialect,
			handle->dialect_state,
			statement_index) : NULL;
	if (dml_index == 0U && channel_index == 0U && returning_into != NULL) {
		out_channel->kind = SQLPARSER_GRAPH_DML_RESULT_SINK;
		out_channel->target_count = returning_into->pair_count;
		out_channel->sink_value_offset = returning_into->pair_count;
		out_channel->sink_value_count = returning_into->pair_count;
		return 1;
	}
	if (handle != NULL &&
	    sqlparser_dialect_uses_postgresql_placeholders(handle->dialect)) {
		sqlparser_dialect_dml_result_dml_t dml;

		memset(&dml, 0, sizeof(dml));
		if (channel_index != 0U ||
		    !sqlparser_dialect_postgresql_dml_at(
			    handle,
			    statement_index,
			    dml_index,
			    &dml) ||
		    dml.target_count == 0U) {
			return 0;
		}
		out_channel->kind = SQLPARSER_GRAPH_DML_RESULT_CLIENT;
		out_channel->target_count = dml.target_count;
		return 1;
	}
	if (handle == NULL) {
		return 0;
	}
	output = sqlparser_dialect_sqlserver_output_state(
		handle->dialect,
		handle->dialect_state,
		statement_index,
		&local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index) ||
	    !sqlparser_sqlserver_output_channel_at(output, global_index, channel_index, &channel)) {
		return 0;
	}
	out_channel->kind = channel.kind;
	out_channel->target_offset = channel.target_offset;
	out_channel->target_count = channel.target_count;
	out_channel->sink_sql = channel.sink_sql;
	out_channel->sink_column_count = channel.sink_column_count;
	return 1;
}

const char *sqlparser_dialect_dml_result_sink_column_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index)) {
		return NULL;
	}
	return sqlparser_sqlserver_output_sink_column_at(
		output,
		global_index,
		channel_index,
		column_index);
}

const char *sqlparser_dialect_dml_result_action_marker_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index)) {
		return NULL;
	}
	return sqlparser_sqlserver_output_action_marker_at(
		output, global_index, channel_index, target_index);
}

sqlparser_status_t sqlparser_dialect_dml_result_preprocess_target_sql(
	sqlparser_dialect_t dialect,
	const void *state,
	const char *public_sql,
	sqlparser_graph_dml_kind_t dml_kind,
	char **out_sql,
	char **out_action_marker,
	sqlparser_error_t *out_error)
{
	(void)state;
	if (sqlparser_dialect_uses_postgresql_placeholders(dialect) ||
	    sqlparser_dialect_returning_into_state(dialect, state) != NULL) {
		if (public_sql == NULL || out_sql == NULL || out_action_marker == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"DML result target SQL and outputs must not be NULL");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		*out_action_marker = NULL;
		*out_sql = sqlparser_strdup(public_sql);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "dialect does not support DML result targets");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return sqlparser_sqlserver_output_preprocess_target_sql(
		public_sql, dml_kind, out_sql, out_action_marker, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_postprocess_target_sql(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *parser_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *action_marker;

	if (sqlparser_dialect_uses_postgresql_placeholders(dialect) ||
	    sqlparser_dialect_returning_into_state(dialect, state) != NULL) {
		if (parser_sql == NULL || out_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"DML result target SQL and output must not be NULL");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		*out_sql = sqlparser_strdup(parser_sql);
		if (*out_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "dialect does not support DML result targets");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	action_marker = sqlparser_dialect_dml_result_action_marker_at(
		dialect, state, statement_index, dml_index, channel_index, target_index);
	return sqlparser_sqlserver_output_postprocess_target_sql(
		parser_sql, action_marker, out_sql, out_error);
}

static sqlparser_status_t sqlparser_dialect_dml_result_mutable_index(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	sqlparser_sqlserver_output_state_t **out_output,
	size_t *out_global_index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state_mutable(
		dialect, state, statement_index, &local_statement_index);
	if (output == NULL ||
	    !sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, out_global_index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_output = output;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dialect_dml_result_adjust_target_count(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	ptrdiff_t delta,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	if (sqlparser_dialect_uses_postgresql_placeholders(dialect)) {
		return SQLPARSER_STATUS_OK;
	}
	if (sqlparser_dialect_returning_into_state(dialect, state) != NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"RETURNING INTO target insertion and deletion are unsupported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_adjust_target_count(
		output, global_index, channel_index, target_index, delta, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_adjust_paired_count(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	ptrdiff_t delta,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_returning_into_item_t *item;
	size_t magnitude;

	item = sqlparser_dialect_returning_into_item_mutable(
		dialect, state, statement_index);
	if (item == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"RETURNING INTO statement index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (delta < 0) {
		magnitude = (size_t)(-(delta + 1)) + 1U;
		if (magnitude >= item->pair_count) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INVALID_ARGUMENT,
				"RETURNING INTO pair count must remain positive");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		item->pair_count -= magnitude;
		return SQLPARSER_STATUS_OK;
	}
	magnitude = (size_t)delta;
	if (magnitude > SIZE_MAX - item->pair_count) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"RETURNING INTO pair count is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	item->pair_count += magnitude;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dialect_dml_result_set_action_marker(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *marker,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	if (sqlparser_dialect_uses_postgresql_placeholders(dialect) ||
	    sqlparser_dialect_returning_into_state(dialect, state) != NULL) {
		return SQLPARSER_STATUS_OK;
	}
	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_action_marker(
		output, global_index, channel_index, target_index, marker, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_set_sink_sql(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	const char *sink_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_sink_sql(
		output, global_index, channel_index, sink_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_set_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_sink_column(
		output, global_index, channel_index, column_index, column_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_insert_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_insert_sink_column(
		output, global_index, channel_index, column_index, column_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_insert_target_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_insert_target_sink_column(
		output, global_index, channel_index, target_index, column_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_delete_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_delete_sink_column(
		output, global_index, channel_index, column_index, out_error);
}
