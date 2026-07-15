#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "sqlparser_internal.h"
#include "sqlparser_dialect_sqlserver_scan.h"

static char sqlparser_sqlserver_char_at(const char *sql, size_t pos, size_t end)
{
	if (pos >= end) {
		return '\0';
	}
	return sql[pos];
}

sqlparser_status_t sqlparser_sqlserver_error_at(
	const char *sql,
	size_t pos,
	const char *message,
	sqlparser_error_t *out_error)
{
	size_t index;
	int line;
	int column;

	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, message);
	if (out_error == NULL) {
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	out_error->cursor = pos < (size_t)INT_MAX ? (int)pos + 1 : INT_MAX;
	line = 1;
	column = 1;
	for (index = 0U; index < pos && sql[index] != '\0'; index++) {
		if (sql[index] == '\n') {
			line++;
			column = 1;
		} else {
			column++;
		}
	}
	out_error->line = line;
	out_error->column = column;
	return SQLPARSER_STATUS_PARSE_ERROR;
}

int sqlparser_sqlserver_is_ident_start(unsigned char c)
{
	return isalpha(c) || c == '_' || c == '#';
}

int sqlparser_sqlserver_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '$' || c == '#' || c == '@';
}

static int sqlparser_sqlserver_is_word_boundary(const char *text, size_t pos, size_t len)
{
	unsigned char prev;
	unsigned char next;

	prev = pos == 0U ? 0U : (unsigned char)text[pos - 1U];
	next = text[pos + len] == '\0' ? 0U : (unsigned char)text[pos + len];
	return !sqlparser_sqlserver_is_ident_char(prev) &&
	       !sqlparser_sqlserver_is_ident_char(next);
}

int sqlparser_sqlserver_ascii_word_equal(const char *text, size_t pos, const char *word)
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
		if (tolower((unsigned char)text[pos + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}

	return sqlparser_sqlserver_is_word_boundary(text, pos, len);
}

unsigned int sqlparser_sqlserver_candidate_mask(const char *text)
{
	unsigned int mask;
	size_t next;
	size_t pos;

	if (text == NULL) {
		return 0U;
	}
	mask = 0U;
	for (pos = 0U; text[pos] != '\0'; pos++) {
		if (sqlparser_sqlserver_can_copy_quoted_or_comment(text, pos)) {
			next = sqlparser_sqlserver_skip_quoted_or_comment_span(text, pos);
			if (next > pos) {
				pos = next - 1U;
				continue;
			}
		}
		switch (tolower((unsigned char)text[pos])) {
			case 'e':
				if (sqlparser_sqlserver_ascii_word_equal(text, pos, "else")) {
					mask |= SQLPARSER_SQLSERVER_CANDIDATE_CONTROL;
				}
				break;
			case 'i':
				if (sqlparser_sqlserver_ascii_word_equal(text, pos, "if")) {
					mask |= SQLPARSER_SQLSERVER_CANDIDATE_CONTROL;
				} else if (sqlparser_sqlserver_ascii_word_equal(text, pos, "insert")) {
					mask |= SQLPARSER_SQLSERVER_CANDIDATE_INSERT;
				}
				break;
			case 'm':
				if (sqlparser_sqlserver_ascii_word_equal(text, pos, "merge")) {
					mask |= SQLPARSER_SQLSERVER_CANDIDATE_MERGE;
				}
				break;
			case 'o':
				if (sqlparser_sqlserver_ascii_word_equal(text, pos, "output")) {
					mask |= SQLPARSER_SQLSERVER_CANDIDATE_OUTPUT;
				}
				break;
			default:
				break;
		}
	}
	return mask;
}

int sqlparser_sqlserver_line_is_go(const char *text, size_t pos, size_t *out_next)
{
	size_t line_start;

	line_start = pos;
	while (line_start > 0U && text[line_start - 1U] != '\n' && text[line_start - 1U] != '\r') {
		line_start--;
	}
	while (isspace((unsigned char)text[line_start]) &&
	       text[line_start] != '\n' && text[line_start] != '\r') {
		line_start++;
	}
	if (line_start != pos || !sqlparser_sqlserver_ascii_word_equal(text, pos, "go")) {
		return 0;
	}
	pos += 2U;
	while (text[pos] != '\0' && text[pos] != '\n' && text[pos] != '\r') {
		if (!isspace((unsigned char)text[pos]) && !isdigit((unsigned char)text[pos])) {
			return 0;
		}
		pos++;
	}
	while (text[pos] == '\r' || text[pos] == '\n') {
		pos++;
	}
	if (out_next != NULL) {
		*out_next = pos;
	}
	return 1;
}

size_t sqlparser_sqlserver_skip_space(const char *text, size_t pos)
{
	while (isspace((unsigned char)text[pos])) {
		pos++;
	}
	return pos;
}

size_t sqlparser_sqlserver_trim_left(const char *text, size_t start, size_t end)
{
	while (start < end && isspace((unsigned char)text[start])) {
		start++;
	}
	return start;
}

size_t sqlparser_sqlserver_trim_right(const char *text, size_t start, size_t end)
{
	while (end > start && isspace((unsigned char)text[end - 1U])) {
		end--;
	}
	return end;
}

int sqlparser_sqlserver_can_copy_quoted_or_comment(const char *sql, size_t index)
{
	if (sql == NULL || sql[index] == '\0') {
		return 0;
	}
	return sql[index] == '[' ||
	       sql[index] == '\'' ||
	       sql[index] == '"' ||
	       (sql[index] == '-' && sql[index + 1U] == '-') ||
	       (sql[index] == '/' && sql[index + 1U] == '*');
}

static sqlparser_status_t sqlparser_sqlserver_scan_non_code_span(
	const char *sql,
	size_t index,
	size_t end,
	size_t *out_next,
	sqlparser_error_t *out_error)
{
	char current;
	char quote;
	size_t depth;
	size_t pos;

	if (sql == NULL || out_next == NULL || index >= end || sql[index] == '\0') {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server span arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	current = sql[index];
	if (current == '[') {
		pos = index + 1U;
		while (sqlparser_sqlserver_char_at(sql, pos, end) != '\0') {
			if (sql[pos] == ']' &&
			    sqlparser_sqlserver_char_at(sql, pos + 1U, end) == ']') {
				pos += 2U;
				continue;
			}
			if (sql[pos] == ']') {
				*out_next = pos + 1U;
				return SQLPARSER_STATUS_OK;
			}
			pos++;
		}
		*out_next = pos;
		return sqlparser_sqlserver_error_at(
			sql,
			index,
			"unterminated SQL Server bracket-delimited identifier",
			out_error);
	}

	if (current == '\'' || current == '"') {
		quote = current;
		pos = index + 1U;
		while (sqlparser_sqlserver_char_at(sql, pos, end) != '\0') {
			if (sql[pos] == quote &&
			    sqlparser_sqlserver_char_at(sql, pos + 1U, end) == quote) {
				pos += 2U;
				continue;
			}
			if (sql[pos] == quote) {
				*out_next = pos + 1U;
				return SQLPARSER_STATUS_OK;
			}
			pos++;
		}
		*out_next = pos;
		return sqlparser_sqlserver_error_at(
			sql,
			index,
			quote == '\'' ? "unterminated SQL Server string literal" :
			                 "unterminated SQL Server quoted identifier",
			out_error);
	}

	if (current == '-' && sqlparser_sqlserver_char_at(sql, index + 1U, end) == '-') {
		pos = index + 2U;
		while (sqlparser_sqlserver_char_at(sql, pos, end) != '\0' &&
		       sql[pos] != '\r' && sql[pos] != '\n') {
			pos++;
		}
		if (sqlparser_sqlserver_char_at(sql, pos, end) == '\r') {
			pos++;
			if (sqlparser_sqlserver_char_at(sql, pos, end) == '\n') {
				pos++;
			}
		} else if (sqlparser_sqlserver_char_at(sql, pos, end) == '\n') {
			pos++;
		}
		*out_next = pos;
		return SQLPARSER_STATUS_OK;
	}

	if (current == '/' && sqlparser_sqlserver_char_at(sql, index + 1U, end) == '*') {
		depth = 1U;
		pos = index + 2U;
		while (sqlparser_sqlserver_char_at(sql, pos, end) != '\0') {
			if (sql[pos] == '/' &&
			    sqlparser_sqlserver_char_at(sql, pos + 1U, end) == '*') {
				depth++;
				pos += 2U;
				continue;
			}
			if (sql[pos] == '*' &&
			    sqlparser_sqlserver_char_at(sql, pos + 1U, end) == '/') {
				depth--;
				pos += 2U;
				if (depth == 0U) {
					*out_next = pos;
					return SQLPARSER_STATUS_OK;
				}
				continue;
			}
			pos++;
		}
		*out_next = pos;
		return sqlparser_sqlserver_error_at(
			sql,
			index,
			"unterminated SQL Server block comment",
			out_error);
	}

	*out_next = index;
	sqlparser_error_set_message(
		out_error,
		SQLPARSER_STATUS_INVALID_ARGUMENT,
		"SQL Server span does not start with quoted text or a comment");
	return SQLPARSER_STATUS_INVALID_ARGUMENT;
}

sqlparser_status_t sqlparser_sqlserver_quoted_or_comment_span(
	const char *sql,
	size_t index,
	size_t *out_next,
	sqlparser_error_t *out_error)
{
	return sqlparser_sqlserver_scan_non_code_span(
		sql,
		index,
		(size_t)-1,
		out_next,
		out_error);
}

size_t sqlparser_sqlserver_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	size_t next;

	if (!sqlparser_sqlserver_can_copy_quoted_or_comment(sql, index)) {
		return index;
	}
	next = index;
	(void)sqlparser_sqlserver_quoted_or_comment_span(sql, index, &next, NULL);
	return next;
}

static int sqlparser_sqlserver_token_word_start(unsigned char c)
{
	return sqlparser_sqlserver_is_ident_start(c) || c == '@' || c == '$';
}

static int sqlparser_sqlserver_span_word_equal(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	size_t index;
	size_t word_len;

	if (sql == NULL || word == NULL) {
		return 0;
	}
	word_len = strlen(word);
	if (end - start != word_len) {
		return 0;
	}
	for (index = 0U; index < word_len; index++) {
		if (tolower((unsigned char)sql[start + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

int sqlparser_sqlserver_token_word_equal(
	const char *sql,
	const sqlparser_sqlserver_token_t *token,
	const char *word)
{
	return token != NULL && token->kind == SQLPARSER_SQLSERVER_TOKEN_WORD &&
	       sqlparser_sqlserver_span_word_equal(sql, token->start, token->end, word);
}

static int sqlparser_sqlserver_peek_word(
	const char *sql,
	size_t pos,
	size_t end,
	size_t *out_start,
	size_t *out_end)
{
	size_t next;

	while (pos < end && sql[pos] != '\0') {
		while (pos < end && isspace((unsigned char)sql[pos])) {
			pos++;
		}
		if (pos >= end || sql[pos] == '\0') {
			return 0;
		}
		if (!sqlparser_sqlserver_can_copy_quoted_or_comment(sql, pos) ||
		    (sql[pos] != '-' && sql[pos] != '/')) {
			break;
		}
		next = pos;
		if (sqlparser_sqlserver_scan_non_code_span(
			    sql,
			    pos,
			    end,
			    &next,
			    NULL) != SQLPARSER_STATUS_OK) {
			return 0;
		}
		pos = next;
	}

	if (pos >= end ||
	    !sqlparser_sqlserver_token_word_start((unsigned char)sql[pos])) {
		return 0;
	}
	if (out_start != NULL) {
		*out_start = pos;
	}
	pos++;
	while (pos < end &&
	       sqlparser_sqlserver_is_ident_char((unsigned char)sql[pos])) {
		pos++;
	}
	if (out_end != NULL) {
		*out_end = pos;
	}
	return 1;
}

static int sqlparser_sqlserver_begin_opens_block(
	const sqlparser_sqlserver_scanner_t *scanner,
	size_t pos)
{
	size_t first_start;
	size_t first_end;
	size_t second_start;
	size_t second_end;

	if (!sqlparser_sqlserver_peek_word(
		    scanner->sql,
		    pos,
		    scanner->end,
		    &first_start,
		    &first_end)) {
		return 1;
	}
	if (sqlparser_sqlserver_span_word_equal(
		    scanner->sql,
		    first_start,
		    first_end,
		    "tran") ||
	    sqlparser_sqlserver_span_word_equal(
		    scanner->sql,
		    first_start,
		    first_end,
		    "transaction") ||
	    sqlparser_sqlserver_span_word_equal(
		    scanner->sql,
		    first_start,
		    first_end,
		    "dialog")) {
		return 0;
	}
	if (!sqlparser_sqlserver_peek_word(
		    scanner->sql,
		    first_end,
		    scanner->end,
		    &second_start,
		    &second_end)) {
		return 1;
	}
	if (sqlparser_sqlserver_span_word_equal(
		    scanner->sql,
		    first_start,
		    first_end,
		    "distributed") &&
	    (sqlparser_sqlserver_span_word_equal(
		     scanner->sql,
		     second_start,
		     second_end,
		     "tran") ||
	     sqlparser_sqlserver_span_word_equal(
		     scanner->sql,
		     second_start,
		     second_end,
		     "transaction"))) {
		return 0;
	}
	if (sqlparser_sqlserver_span_word_equal(
		    scanner->sql,
		    first_start,
		    first_end,
		    "conversation") &&
	    sqlparser_sqlserver_span_word_equal(
		    scanner->sql,
		    second_start,
		    second_end,
		    "timer")) {
		return 0;
	}
	return 1;
}

static int sqlparser_sqlserver_end_is_conversation(
	const sqlparser_sqlserver_scanner_t *scanner,
	size_t pos)
{
	size_t start;
	size_t end;

	return sqlparser_sqlserver_peek_word(
		       scanner->sql,
		       pos,
		       scanner->end,
		       &start,
		       &end) &&
	       sqlparser_sqlserver_span_word_equal(
		       scanner->sql,
		       start,
		       end,
		       "conversation");
}

sqlparser_status_t sqlparser_sqlserver_scanner_init(
	sqlparser_sqlserver_scanner_t *scanner,
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	if (scanner == NULL || sql == NULL || start > end) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server scanner arguments are invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	memset(scanner, 0, sizeof(*scanner));
	scanner->sql = sql;
	scanner->end = end;
	scanner->pos = start;
	scanner->status = SQLPARSER_STATUS_OK;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_sqlserver_scanner_finish(
	sqlparser_sqlserver_scanner_t *scanner,
	sqlparser_sqlserver_token_t *out_token,
	sqlparser_error_t *out_error)
{
	if (scanner->paren_depth != 0U) {
		scanner->status = sqlparser_sqlserver_error_at(
			scanner->sql,
			scanner->pos,
			"unterminated SQL Server parenthesized expression",
			out_error);
		return scanner->status;
	}
	if (scanner->case_depth != 0U) {
		scanner->status = sqlparser_sqlserver_error_at(
			scanner->sql,
			scanner->pos,
			"unterminated SQL Server CASE expression",
			out_error);
		return scanner->status;
	}
	if (scanner->block_depth != 0U) {
		scanner->status = sqlparser_sqlserver_error_at(
			scanner->sql,
			scanner->pos,
			"unterminated SQL Server BEGIN block",
			out_error);
		return scanner->status;
	}

	memset(out_token, 0, sizeof(*out_token));
	out_token->kind = SQLPARSER_SQLSERVER_TOKEN_EOF;
	out_token->start = scanner->pos;
	out_token->end = scanner->pos;
	scanner->done = 1;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_sqlserver_scanner_next(
	sqlparser_sqlserver_scanner_t *scanner,
	sqlparser_sqlserver_token_t *out_token,
	sqlparser_error_t *out_error)
{
	size_t next;
	size_t pos;

	if (scanner == NULL || out_token == NULL || scanner->sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL Server scanner output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (scanner->status != SQLPARSER_STATUS_OK) {
		return scanner->status;
	}
	if (scanner->done) {
		memset(out_token, 0, sizeof(*out_token));
		out_token->kind = SQLPARSER_SQLSERVER_TOKEN_EOF;
		out_token->start = scanner->pos;
		out_token->end = scanner->pos;
		return SQLPARSER_STATUS_OK;
	}

	pos = scanner->pos;
	while (pos < scanner->end && scanner->sql[pos] != '\0') {
		while (pos < scanner->end &&
		       isspace((unsigned char)scanner->sql[pos])) {
			pos++;
		}
		if (pos >= scanner->end || scanner->sql[pos] == '\0') {
			break;
		}
		if (!((scanner->sql[pos] == '-' &&
		       sqlparser_sqlserver_char_at(scanner->sql, pos + 1U, scanner->end) == '-') ||
		      (scanner->sql[pos] == '/' &&
		       sqlparser_sqlserver_char_at(scanner->sql, pos + 1U, scanner->end) == '*'))) {
			break;
		}
		next = pos;
		scanner->status = sqlparser_sqlserver_scan_non_code_span(
			scanner->sql,
			pos,
			scanner->end,
			&next,
			out_error);
		if (scanner->status != SQLPARSER_STATUS_OK) {
			return scanner->status;
		}
		pos = next;
	}

	scanner->pos = pos;
	if (pos >= scanner->end || scanner->sql[pos] == '\0') {
		return sqlparser_sqlserver_scanner_finish(scanner, out_token, out_error);
	}

	memset(out_token, 0, sizeof(*out_token));
	out_token->start = pos;
	out_token->paren_depth = scanner->paren_depth;
	out_token->block_depth = scanner->block_depth;
	out_token->case_depth = scanner->case_depth;

	if ((scanner->sql[pos] == 'N' || scanner->sql[pos] == 'n') &&
	    sqlparser_sqlserver_char_at(scanner->sql, pos + 1U, scanner->end) == '\'') {
		next = pos + 1U;
		scanner->status = sqlparser_sqlserver_scan_non_code_span(
			scanner->sql,
			pos + 1U,
			scanner->end,
			&next,
			out_error);
		if (scanner->status != SQLPARSER_STATUS_OK) {
			return scanner->status;
		}
		out_token->kind = SQLPARSER_SQLSERVER_TOKEN_STRING;
		out_token->end = next;
		scanner->pos = next;
		return SQLPARSER_STATUS_OK;
	}

	if (scanner->sql[pos] == '\'' || scanner->sql[pos] == '"' ||
	    scanner->sql[pos] == '[') {
		next = pos;
		scanner->status = sqlparser_sqlserver_scan_non_code_span(
			scanner->sql,
			pos,
			scanner->end,
			&next,
			out_error);
		if (scanner->status != SQLPARSER_STATUS_OK) {
			return scanner->status;
		}
		out_token->kind = scanner->sql[pos] == '\'' ?
			SQLPARSER_SQLSERVER_TOKEN_STRING :
			SQLPARSER_SQLSERVER_TOKEN_QUOTED_IDENTIFIER;
		out_token->end = next;
		scanner->pos = next;
		return SQLPARSER_STATUS_OK;
	}

	if (sqlparser_sqlserver_token_word_start((unsigned char)scanner->sql[pos])) {
		next = pos + 1U;
		while (next < scanner->end &&
		       sqlparser_sqlserver_is_ident_char((unsigned char)scanner->sql[next])) {
			next++;
		}
		out_token->kind = SQLPARSER_SQLSERVER_TOKEN_WORD;
		out_token->end = next;
		scanner->pos = next;

		if (next - pos == 4U &&
		    tolower((unsigned char)scanner->sql[pos]) == 'c' &&
		    sqlparser_sqlserver_token_word_equal(scanner->sql, out_token, "case")) {
			scanner->case_depth++;
		} else if (next - pos == 5U &&
		           tolower((unsigned char)scanner->sql[pos]) == 'b' &&
		           sqlparser_sqlserver_token_word_equal(scanner->sql, out_token, "begin")) {
			if (sqlparser_sqlserver_begin_opens_block(scanner, next)) {
				scanner->block_depth++;
			}
		} else if (next - pos == 3U &&
		           tolower((unsigned char)scanner->sql[pos]) == 'e' &&
		           sqlparser_sqlserver_token_word_equal(scanner->sql, out_token, "end")) {
			if (scanner->case_depth > 0U) {
				scanner->case_depth--;
			} else if (!sqlparser_sqlserver_end_is_conversation(scanner, next)) {
				if (scanner->block_depth == 0U) {
					scanner->status = sqlparser_sqlserver_error_at(
						scanner->sql,
						pos,
						"unmatched SQL Server END",
						out_error);
					return scanner->status;
				}
				scanner->block_depth--;
			}
		}
		return SQLPARSER_STATUS_OK;
	}

	out_token->kind = SQLPARSER_SQLSERVER_TOKEN_SYMBOL;
	out_token->symbol = scanner->sql[pos];
	out_token->end = pos + 1U;
	scanner->pos = pos + 1U;
	if (out_token->symbol == '(') {
		scanner->paren_depth++;
	} else if (out_token->symbol == ')') {
		if (scanner->paren_depth == 0U) {
			scanner->status = sqlparser_sqlserver_error_at(
				scanner->sql,
				pos,
				"unmatched SQL Server closing parenthesis",
				out_error);
			return scanner->status;
		}
		scanner->paren_depth--;
	}
	return SQLPARSER_STATUS_OK;
}

size_t sqlparser_sqlserver_statement_end(
	const char *sql,
	size_t start,
	size_t end)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;

	if (sql == NULL) {
		return start;
	}
	if (start >= end || memchr(sql + start, ';', end - start) == NULL) {
		return end;
	}
	if (sqlparser_sqlserver_scanner_init(
		    &scanner,
		    sql,
		    start,
		    end,
		    NULL) != SQLPARSER_STATUS_OK) {
		return end;
	}
	while (sqlparser_sqlserver_scanner_next(&scanner, &token, NULL) == SQLPARSER_STATUS_OK) {
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			return end;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		    token.symbol == ';' &&
		    token.paren_depth == 0U &&
		    token.block_depth == 0U &&
		    token.case_depth == 0U) {
			return token.start;
		}
	}
	return end;
}

int sqlparser_sqlserver_find_matching_paren(
	const char *input,
	size_t open_pos,
	size_t *out_close_pos,
	size_t *out_next_pos)
{
	size_t depth;
	size_t pos;
	size_t skipped;

	if (input == NULL || input[open_pos] != '(') {
		return 0;
	}
	depth = 1U;
	pos = open_pos + 1U;
	while (input[pos] != '\0') {
		if (sqlparser_sqlserver_can_copy_quoted_or_comment(input, pos)) {
			skipped = pos;
			if (sqlparser_sqlserver_quoted_or_comment_span(
				    input,
				    pos,
				    &skipped,
				    NULL) != SQLPARSER_STATUS_OK) {
				return 0;
			}
			pos = skipped;
			continue;
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

int sqlparser_sqlserver_find_top_level_char(
	const char *input,
	size_t start,
	size_t end,
	char target,
	size_t *out_pos)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;

	if (input == NULL || start > end ||
	    sqlparser_sqlserver_scanner_init(
		    &scanner,
		    input,
		    start,
		    end,
		    NULL) != SQLPARSER_STATUS_OK) {
		return 0;
	}
	while (sqlparser_sqlserver_scanner_next(&scanner, &token, NULL) == SQLPARSER_STATUS_OK) {
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			return 0;
		}
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_SYMBOL &&
		    token.symbol == target &&
		    token.paren_depth == 0U &&
		    token.block_depth == 0U &&
		    token.case_depth == 0U) {
			if (out_pos != NULL) {
				*out_pos = token.start;
			}
			return 1;
		}
	}
	return 0;
}

int sqlparser_sqlserver_find_top_level_word(
	const char *input,
	size_t start,
	size_t end,
	const char *word,
	size_t *out_pos)
{
	sqlparser_sqlserver_scanner_t scanner;
	sqlparser_sqlserver_token_t token;

	if (input == NULL || word == NULL || word[0] == '\0' || start > end ||
	    sqlparser_sqlserver_scanner_init(
		    &scanner,
		    input,
		    start,
		    end,
		    NULL) != SQLPARSER_STATUS_OK) {
		return 0;
	}
	while (sqlparser_sqlserver_scanner_next(&scanner, &token, NULL) == SQLPARSER_STATUS_OK) {
		if (token.kind == SQLPARSER_SQLSERVER_TOKEN_EOF) {
			return 0;
		}
		if (token.paren_depth == 0U &&
		    token.block_depth == 0U &&
		    token.case_depth == 0U &&
		    sqlparser_sqlserver_token_word_equal(input, &token, word)) {
			if (out_pos != NULL) {
				*out_pos = token.start;
			}
			return 1;
		}
	}
	return 0;
}
