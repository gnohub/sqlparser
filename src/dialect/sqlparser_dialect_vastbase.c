#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_dialect_internal.h"
#include "sqlparser_dialect_sqlserver_scan.h"

#define SQLPARSER_VASTBASE_SCAN_DOLLAR_QUOTES 0x01U
#define SQLPARSER_VASTBASE_SCAN_NESTED_COMMENTS 0x02U
#define SQLPARSER_VASTBASE_SCAN_BACKSLASH_ESCAPES 0x04U
#define SQLPARSER_VASTBASE_SCAN_HASH_COMMENTS 0x08U
#define SQLPARSER_VASTBASE_SCAN_ORACLE_Q_QUOTES 0x10U
#define SQLPARSER_VASTBASE_SCAN_SQLSERVER_GO 0x20U
#define SQLPARSER_VASTBASE_SCAN_POSTGRESQL_E_STRINGS 0x40U

static int sqlparser_vastbase_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '$' || c >= 0x80U;
}

static size_t sqlparser_vastbase_skip_space(const char *sql, size_t pos, size_t end)
{
	while (pos < end && isspace((unsigned char)sql[pos])) {
		pos++;
	}
	return pos;
}

static size_t sqlparser_vastbase_trim_right(const char *sql, size_t start, size_t end)
{
	while (end > start && isspace((unsigned char)sql[end - 1U])) {
		end--;
	}
	return end;
}

static int sqlparser_vastbase_word_at(
	const char *sql,
	size_t pos,
	size_t end,
	const char *word)
{
	size_t index;
	size_t len;

	if (sql == NULL || word == NULL || pos >= end) {
		return 0;
	}
	len = strlen(word);
	if (len > end - pos ||
	    (pos > 0U && sqlparser_vastbase_is_ident_char((unsigned char)sql[pos - 1U])) ||
	    (pos + len < end && sqlparser_vastbase_is_ident_char((unsigned char)sql[pos + len]))) {
		return 0;
	}
	for (index = 0U; index < len; index++) {
		if (tolower((unsigned char)sql[pos + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static size_t sqlparser_vastbase_quoted_span_end(
	const char *sql,
	size_t pos,
	size_t end,
	int allow_backslash,
	int *out_closed)
{
	char close;
	char open;

	if (out_closed != NULL) {
		*out_closed = 0;
	}
	open = sql[pos];
	close = open == '[' ? ']' : open;
	pos++;
	while (pos < end) {
		if (allow_backslash &&
		    sql[pos] == '\\' &&
		    open != '[' &&
		    pos + 1U < end) {
			pos += 2U;
			continue;
		}
		if (sql[pos] != close) {
			pos++;
			continue;
		}
		if (pos + 1U < end && sql[pos + 1U] == close) {
			pos += 2U;
			continue;
		}
		if (out_closed != NULL) {
			*out_closed = 1;
		}
		return pos + 1U;
	}
	return end;
}

static size_t sqlparser_vastbase_oracle_q_quoted_span_end(
	const char *sql,
	size_t pos,
	size_t end,
	int *out_closed)
{
	char close;
	char open;
	size_t scan;

	if (out_closed != NULL) {
		*out_closed = 0;
	}
	scan = pos;
	if (scan < end &&
	    (sql[scan] == 'n' || sql[scan] == 'N') &&
	    scan + 1U < end &&
	    (sql[scan + 1U] == 'q' || sql[scan + 1U] == 'Q')) {
		scan++;
	}
	if (scan + 2U >= end ||
	    (sql[scan] != 'q' && sql[scan] != 'Q') ||
	    sql[scan + 1U] != '\'') {
		return pos;
	}
	open = sql[scan + 2U];
	switch (open) {
		case '[':
			close = ']';
			break;
		case '{':
			close = '}';
			break;
		case '(':
			close = ')';
			break;
		case '<':
			close = '>';
			break;
		default:
			close = open;
			break;
	}
	scan += 3U;
	while (scan + 1U < end) {
		if (sql[scan] == close && sql[scan + 1U] == '\'') {
			if (out_closed != NULL) {
				*out_closed = 1;
			}
			return scan + 2U;
		}
		scan++;
	}
	return end;
}

static size_t sqlparser_vastbase_dollar_quoted_span_end(
	const char *sql,
	size_t pos,
	size_t end,
	int *out_closed)
{
	size_t body;
	size_t delimiter_len;
	size_t tag_end;

	if (out_closed != NULL) {
		*out_closed = 0;
	}
	if (pos >= end || sql[pos] != '$') {
		return pos;
	}
	tag_end = pos + 1U;
	while (tag_end < end &&
	       (isalnum((unsigned char)sql[tag_end]) || sql[tag_end] == '_')) {
		tag_end++;
	}
	if (tag_end >= end || sql[tag_end] != '$') {
		return pos;
	}
	delimiter_len = tag_end - pos + 1U;
	body = tag_end + 1U;
	while (body + delimiter_len <= end) {
		if (memcmp(sql + body, sql + pos, delimiter_len) == 0) {
			if (out_closed != NULL) {
				*out_closed = 1;
			}
			return body + delimiter_len;
		}
		body++;
	}
	return end;
}

static size_t sqlparser_vastbase_non_code_end(
	const char *sql,
	size_t pos,
	size_t end,
	unsigned int scan_flags)
{
	size_t depth;
	size_t skipped;

	if (pos >= end) {
		return pos;
	}
	if ((scan_flags & SQLPARSER_VASTBASE_SCAN_ORACLE_Q_QUOTES) != 0U) {
		skipped = sqlparser_vastbase_oracle_q_quoted_span_end(
			sql,
			pos,
			end,
			NULL);
		if (skipped > pos) {
			return skipped;
		}
	}
	if (sql[pos] == '\'' ||
	    sql[pos] == '"' ||
	    (sql[pos] == '`' &&
	     (scan_flags & SQLPARSER_VASTBASE_SCAN_BACKSLASH_ESCAPES) != 0U) ||
	    (sql[pos] == '[' &&
	     (scan_flags & SQLPARSER_VASTBASE_SCAN_SQLSERVER_GO) != 0U)) {
		int allow_backslash;

		allow_backslash =
			(scan_flags & SQLPARSER_VASTBASE_SCAN_BACKSLASH_ESCAPES) != 0U;
		if (!allow_backslash &&
		    (scan_flags & SQLPARSER_VASTBASE_SCAN_POSTGRESQL_E_STRINGS) != 0U &&
		    sql[pos] == '\'' &&
		    pos > 0U &&
		    (sql[pos - 1U] == 'e' || sql[pos - 1U] == 'E') &&
		    (pos == 1U ||
		     !sqlparser_vastbase_is_ident_char(
			     (unsigned char)sql[pos - 2U]))) {
			allow_backslash = 1;
		}
		return sqlparser_vastbase_quoted_span_end(
			sql,
			pos,
			end,
			allow_backslash,
			NULL);
	}
	if (sql[pos] == '-' && pos + 1U < end && sql[pos + 1U] == '-') {
		pos += 2U;
		while (pos < end && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if ((scan_flags & SQLPARSER_VASTBASE_SCAN_HASH_COMMENTS) != 0U &&
	    sql[pos] == '#') {
		pos++;
		while (pos < end && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if (sql[pos] == '/' && pos + 1U < end && sql[pos + 1U] == '*') {
		pos += 2U;
		depth = 1U;
		while (pos + 1U < end) {
			if ((scan_flags & SQLPARSER_VASTBASE_SCAN_NESTED_COMMENTS) != 0U &&
			    sql[pos] == '/' && sql[pos + 1U] == '*') {
				depth++;
				pos += 2U;
				continue;
			}
			if (sql[pos] == '*' && sql[pos + 1U] == '/') {
				depth--;
				pos += 2U;
				if (depth == 0U) {
					return pos;
				}
				continue;
			}
			pos++;
		}
		return end;
	}
	return pos;
}

static int sqlparser_vastbase_sqlserver_go_at(
	const char *sql,
	size_t pos,
	size_t end,
	size_t *out_next)
{
	size_t next;

	if (sql == NULL || pos >= end ||
	    !sqlparser_sqlserver_line_is_go(sql, pos, &next) ||
	    next > end) {
		return 0;
	}
	if (out_next != NULL) {
		*out_next = next;
	}
	return 1;
}

static size_t sqlparser_vastbase_statement_end(
	const char *sql,
	size_t start,
	size_t end,
	unsigned int scan_flags,
	size_t *out_next)
{
	size_t next;
	size_t pos;
	size_t skipped;

	if (out_next != NULL) {
		*out_next = end;
	}
	pos = start;
	while (pos < end) {
		if ((scan_flags & SQLPARSER_VASTBASE_SCAN_DOLLAR_QUOTES) != 0U &&
		    sql[pos] == '$') {
			skipped = sqlparser_vastbase_dollar_quoted_span_end(
				sql,
				pos,
				end,
				NULL);
			if (skipped > pos) {
				pos = skipped;
				continue;
			}
		}
		skipped = sqlparser_vastbase_non_code_end(
			sql,
			pos,
			end,
			scan_flags);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == ';') {
			if (out_next != NULL) {
				*out_next = pos + 1U;
			}
			break;
		}
		if ((scan_flags & SQLPARSER_VASTBASE_SCAN_SQLSERVER_GO) != 0U &&
		    sqlparser_vastbase_sqlserver_go_at(
			    sql,
			    pos,
			    end,
			    &next)) {
			if (out_next != NULL) {
				*out_next = next;
			}
			break;
		}
		pos++;
	}
	return pos;
}

static size_t sqlparser_vastbase_skip_leading_trivia(
	const char *sql,
	size_t start,
	size_t end,
	unsigned int scan_flags)
{
	size_t pos;
	size_t skipped;

	pos = start;
	for (;;) {
		pos = sqlparser_vastbase_skip_space(sql, pos, end);
		if (pos >= end ||
		    !((sql[pos] == '-' && pos + 1U < end && sql[pos + 1U] == '-') ||
		      (sql[pos] == '/' && pos + 1U < end && sql[pos + 1U] == '*') ||
		      ((scan_flags & SQLPARSER_VASTBASE_SCAN_HASH_COMMENTS) != 0U &&
		       sql[pos] == '#'))) {
			return pos;
		}
		skipped = sqlparser_vastbase_non_code_end(
			sql,
			pos,
			end,
			scan_flags);
		if (skipped <= pos) {
			return pos;
		}
		pos = skipped;
	}
}

static size_t sqlparser_vastbase_token_end(
	const char *sql,
	size_t pos,
	size_t end,
	unsigned int scan_flags)
{
	int closed;
	int allow_backslash;
	size_t quote_pos;
	size_t token_end;

	if (pos >= end) {
		return pos;
	}
	if ((scan_flags & SQLPARSER_VASTBASE_SCAN_ORACLE_Q_QUOTES) != 0U) {
		token_end = sqlparser_vastbase_oracle_q_quoted_span_end(
			sql,
			pos,
			end,
			&closed);
		if (token_end > pos) {
			return closed ? token_end : pos;
		}
	}
	if ((scan_flags & SQLPARSER_VASTBASE_SCAN_DOLLAR_QUOTES) != 0U &&
	    sql[pos] == '$') {
		token_end = sqlparser_vastbase_dollar_quoted_span_end(
			sql,
			pos,
			end,
			&closed);
		if (token_end > pos) {
			return closed ? token_end : pos;
		}
	}
	quote_pos = pos;
	if ((scan_flags & SQLPARSER_VASTBASE_SCAN_POSTGRESQL_E_STRINGS) != 0U &&
	    pos + 1U < end &&
	    (sql[pos] == 'e' || sql[pos] == 'E') &&
	    sql[pos + 1U] == '\'') {
		quote_pos++;
	} else if (pos + 1U < end &&
		   (sql[pos] == 'n' || sql[pos] == 'N') &&
		   sql[pos + 1U] == '\'') {
		quote_pos++;
	}
	if (sql[quote_pos] == '\'' ||
	    sql[quote_pos] == '"' ||
	    sql[quote_pos] == '`' ||
	    sql[quote_pos] == '[') {
		if ((sql[quote_pos] == '`' &&
		     (scan_flags & SQLPARSER_VASTBASE_SCAN_BACKSLASH_ESCAPES) == 0U) ||
		    (sql[quote_pos] == '[' &&
		     (scan_flags & SQLPARSER_VASTBASE_SCAN_SQLSERVER_GO) == 0U)) {
			return pos;
		}
		allow_backslash =
			(scan_flags & SQLPARSER_VASTBASE_SCAN_BACKSLASH_ESCAPES) != 0U ||
			(quote_pos > pos &&
			 (scan_flags & SQLPARSER_VASTBASE_SCAN_POSTGRESQL_E_STRINGS) != 0U &&
			 (sql[pos] == 'e' || sql[pos] == 'E'));
		token_end = sqlparser_vastbase_quoted_span_end(
			sql,
			quote_pos,
			end,
			allow_backslash,
			&closed);
		return closed ? token_end : pos;
	}
	if ((sql[pos] == '-' && pos + 1U < end && sql[pos + 1U] == '-') ||
	    (sql[pos] == '/' && pos + 1U < end && sql[pos + 1U] == '*') ||
	    ((scan_flags & SQLPARSER_VASTBASE_SCAN_HASH_COMMENTS) != 0U &&
	     sql[pos] == '#')) {
		return pos;
	}
	while (pos < end &&
	       !isspace((unsigned char)sql[pos]) &&
	       sql[pos] != '=' &&
	       sql[pos] != ',' &&
	       sql[pos] != ';') {
		pos++;
	}
	return pos;
}

static int sqlparser_vastbase_consume_word(
	const char *sql,
	size_t *io_pos,
	size_t end,
	const char *word)
{
	size_t pos;

	pos = sqlparser_vastbase_skip_space(sql, *io_pos, end);
	if (!sqlparser_vastbase_word_at(sql, pos, end, word)) {
		return 0;
	}
	*io_pos = pos + strlen(word);
	return 1;
}

static int sqlparser_vastbase_transaction_options_are_supported(
	const char *sql,
	size_t pos,
	size_t end)
{
	int option_count;

	option_count = 0;
	while (pos < end) {
		if (sqlparser_vastbase_consume_word(sql, &pos, end, "isolation")) {
			if (!sqlparser_vastbase_consume_word(sql, &pos, end, "level")) {
				return 0;
			}
			if (sqlparser_vastbase_consume_word(sql, &pos, end, "serializable")) {
				/* Complete option. */
			} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "repeatable")) {
				if (!sqlparser_vastbase_consume_word(sql, &pos, end, "read")) {
					return 0;
				}
			} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "read")) {
				if (!sqlparser_vastbase_consume_word(sql, &pos, end, "committed") &&
				    !sqlparser_vastbase_consume_word(sql, &pos, end, "uncommitted")) {
					return 0;
				}
			} else {
				return 0;
			}
		} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "read")) {
			if (!sqlparser_vastbase_consume_word(sql, &pos, end, "only") &&
			    !sqlparser_vastbase_consume_word(sql, &pos, end, "write")) {
				return 0;
			}
		} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "not")) {
			if (!sqlparser_vastbase_consume_word(sql, &pos, end, "deferrable")) {
				return 0;
			}
		} else if (!sqlparser_vastbase_consume_word(sql, &pos, end, "deferrable")) {
			return 0;
		}
		option_count++;
		pos = sqlparser_vastbase_skip_space(sql, pos, end);
		if (pos == end) {
			return option_count > 0;
		}
		if (sql[pos] != ',') {
			return 0;
		}
		pos = sqlparser_vastbase_skip_space(sql, pos + 1U, end);
		if (pos == end) {
			return 0;
		}
	}
	return 0;
}

static int sqlparser_vastbase_alter_session_is_supported(
	const char *sql,
	size_t start,
	size_t end,
	unsigned int scan_flags)
{
	size_t pos;
	size_t token_end;

	pos = start;
	if (!sqlparser_vastbase_word_at(sql, pos, end, "alter")) {
		return 0;
	}
	pos = sqlparser_vastbase_skip_space(sql, pos + strlen("alter"), end);
	if (!sqlparser_vastbase_word_at(sql, pos, end, "session")) {
		return 0;
	}
	pos = sqlparser_vastbase_skip_space(sql, pos + strlen("session"), end);
	if (!sqlparser_vastbase_word_at(sql, pos, end, "set")) {
		return 0;
	}
	pos = sqlparser_vastbase_skip_space(sql, pos + strlen("set"), end);
	if (pos >= end) {
		return 0;
	}

	if (sqlparser_vastbase_word_at(sql, pos, end, "current_schema")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("current_schema"), end);
		if (sqlparser_vastbase_word_at(sql, pos, end, "to")) {
			pos = sqlparser_vastbase_skip_space(sql, pos + strlen("to"), end);
		}
		if (pos >= end || sql[pos] == '\'') {
			return 0;
		}
		token_end = sqlparser_vastbase_token_end(
			sql, pos, end, scan_flags);
		return token_end > pos && sqlparser_vastbase_skip_space(sql, token_end, end) == end;
	}
	if (sqlparser_vastbase_word_at(sql, pos, end, "names")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("names"), end);
		token_end = sqlparser_vastbase_token_end(
			sql, pos, end, scan_flags);
		return token_end > pos && sqlparser_vastbase_skip_space(sql, token_end, end) == end;
	}
	if (sqlparser_vastbase_word_at(sql, pos, end, "time")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("time"), end);
		if (!sqlparser_vastbase_word_at(sql, pos, end, "zone")) {
			return 0;
		}
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("zone"), end);
		token_end = sqlparser_vastbase_token_end(
			sql, pos, end, scan_flags);
		return token_end > pos && sqlparser_vastbase_skip_space(sql, token_end, end) == end;
	}
	if (sqlparser_vastbase_word_at(sql, pos, end, "xml")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("xml"), end);
		if (!sqlparser_vastbase_word_at(sql, pos, end, "option")) {
			return 0;
		}
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("option"), end);
		if (sqlparser_vastbase_word_at(sql, pos, end, "document")) {
			pos += strlen("document");
		} else if (sqlparser_vastbase_word_at(sql, pos, end, "content")) {
			pos += strlen("content");
		} else {
			return 0;
		}
		return sqlparser_vastbase_skip_space(sql, pos, end) == end;
	}
	if (sqlparser_vastbase_word_at(sql, pos, end, "role")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("role"), end);
		token_end = sqlparser_vastbase_token_end(
			sql, pos, end, scan_flags);
		if (token_end <= pos) {
			return 0;
		}
		pos = sqlparser_vastbase_skip_space(sql, token_end, end);
		if (!sqlparser_vastbase_word_at(sql, pos, end, "password")) {
			return 0;
		}
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("password"), end);
		token_end = sqlparser_vastbase_token_end(
			sql, pos, end, scan_flags);
		return token_end > pos && sqlparser_vastbase_skip_space(sql, token_end, end) == end;
	}
	if (sqlparser_vastbase_word_at(sql, pos, end, "session")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("session"), end);
		if (sqlparser_vastbase_word_at(sql, pos, end, "authorization")) {
			pos = sqlparser_vastbase_skip_space(sql, pos + strlen("authorization"), end);
			token_end = sqlparser_vastbase_token_end(
				sql, pos, end, scan_flags);
			if (token_end <= pos) {
				return 0;
			}
			if (sqlparser_vastbase_word_at(sql, pos, token_end, "default")) {
				return sqlparser_vastbase_skip_space(sql, token_end, end) == end;
			}
			pos = sqlparser_vastbase_skip_space(sql, token_end, end);
			if (!sqlparser_vastbase_word_at(sql, pos, end, "password")) {
				return 0;
			}
			pos = sqlparser_vastbase_skip_space(sql, pos + strlen("password"), end);
			token_end = sqlparser_vastbase_token_end(
				sql, pos, end, scan_flags);
			return token_end > pos &&
				sqlparser_vastbase_skip_space(sql, token_end, end) == end;
		}
		if (!sqlparser_vastbase_word_at(sql, pos, end, "characteristics")) {
			return 0;
		}
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("characteristics"), end);
		if (!sqlparser_vastbase_word_at(sql, pos, end, "as")) {
			return 0;
		}
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("as"), end);
		if (!sqlparser_vastbase_word_at(sql, pos, end, "transaction")) {
			return 0;
		}
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("transaction"), end);
		return sqlparser_vastbase_transaction_options_are_supported(sql, pos, end);
	}
	if (sqlparser_vastbase_word_at(sql, pos, end, "transaction")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("transaction"), end);
		return sqlparser_vastbase_transaction_options_are_supported(sql, pos, end);
	}

	if (!sqlparser_vastbase_word_at(sql, pos, end, "statement_timeout")) {
		return 0;
	}
	token_end = sqlparser_vastbase_token_end(
		sql, pos, end, scan_flags);
	if (token_end <= pos) {
		return 0;
	}
	pos = sqlparser_vastbase_skip_space(sql, token_end, end);
	if (pos < end && sql[pos] == '=') {
		pos = sqlparser_vastbase_skip_space(sql, pos + 1U, end);
		token_end = sqlparser_vastbase_token_end(
			sql, pos, end, scan_flags);
		return token_end > pos && sqlparser_vastbase_skip_space(sql, token_end, end) == end;
	}
	if (sqlparser_vastbase_word_at(sql, pos, end, "to")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("to"), end);
		token_end = sqlparser_vastbase_token_end(
			sql, pos, end, scan_flags);
		return token_end > pos && sqlparser_vastbase_skip_space(sql, token_end, end) == end;
	}
	if (!sqlparser_vastbase_word_at(sql, pos, end, "from")) {
		return 0;
	}
	pos = sqlparser_vastbase_skip_space(sql, pos + strlen("from"), end);
	if (!sqlparser_vastbase_word_at(sql, pos, end, "current")) {
		return 0;
	}
	return sqlparser_vastbase_skip_space(sql, pos + strlen("current"), end) == end;
}

static sqlparser_status_t sqlparser_vastbase_size_add(
	size_t *value,
	size_t add,
	sqlparser_error_t *out_error)
{
	if (value == NULL || add > (size_t)-1 - *value) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*value += add;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_vastbase_rewrite_session_pass(
	const char *sql,
	char *out,
	size_t *out_len,
	int *out_rewritten,
	unsigned int scan_flags,
	sqlparser_identifier_origin_writer_t *origin_writer,
	sqlparser_error_t *out_error)
{
	static const char prefix_head[] = "SET ";
	static const char prefix_tail[] = " TO '";
	static const char prefix[] = "SET " SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT " TO '";
	size_t copy_start;
	size_t encoded_len;
	size_t end;
	size_t len;
	size_t segment_start;
	size_t source_pos;
	size_t statement_end;
	size_t statement_next;
	size_t statement_start;
	size_t write_pos;
	sqlparser_status_t status;

	len = strlen(sql);
	copy_start = 0U;
	segment_start = 0U;
	*out_len = 0U;
	*out_rewritten = 0;
	while (segment_start < len) {
		statement_end = sqlparser_vastbase_statement_end(
			sql,
			segment_start,
			len,
			scan_flags,
			&statement_next);
		statement_start = sqlparser_vastbase_skip_leading_trivia(
			sql, segment_start, statement_end, scan_flags);
		end = sqlparser_vastbase_trim_right(sql, statement_start, statement_end);
		if (statement_start < end &&
		    sqlparser_vastbase_alter_session_is_supported(
			    sql,
			    statement_start,
			    end,
			    scan_flags)) {
			encoded_len = end - statement_start;
			for (source_pos = statement_start; source_pos < end; source_pos++) {
				if (sql[source_pos] == '\'' &&
				    sqlparser_vastbase_size_add(&encoded_len, 1U, out_error) !=
					    SQLPARSER_STATUS_OK) {
					return SQLPARSER_STATUS_NO_MEMORY;
				}
			}
			write_pos = *out_len;
			status = sqlparser_vastbase_size_add(out_len, statement_start - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_vastbase_size_add(out_len, sizeof(prefix) - 1U, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_vastbase_size_add(out_len, encoded_len, out_error);
			}
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_vastbase_size_add(out_len, 1U, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (origin_writer != NULL) {
				status = sqlparser_identifier_origin_writer_append_input(
					origin_writer,
					copy_start,
					statement_start - copy_start,
					out_error);
				if (status == SQLPARSER_STATUS_OK) {
					status =
						sqlparser_identifier_origin_writer_append_unknown(
							origin_writer,
							sizeof(prefix_head) - 1U,
							out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status =
						sqlparser_identifier_origin_writer_append_generated_identifier(
							origin_writer,
							sizeof(
								SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT) -
								1U,
							out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status =
						sqlparser_identifier_origin_writer_append_unknown(
							origin_writer,
							sizeof(prefix_tail) - 1U,
							out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status =
						sqlparser_identifier_origin_writer_append_unknown(
							origin_writer,
							encoded_len,
							out_error);
				}
				if (status == SQLPARSER_STATUS_OK) {
					status =
						sqlparser_identifier_origin_writer_append_unknown(
							origin_writer,
							1U,
							out_error);
				}
				if (status != SQLPARSER_STATUS_OK) {
					return status;
				}
			}
			if (out != NULL) {
				memcpy(out + write_pos, sql + copy_start, statement_start - copy_start);
				write_pos += statement_start - copy_start;
				memcpy(out + write_pos, prefix, sizeof(prefix) - 1U);
				write_pos += sizeof(prefix) - 1U;
				for (source_pos = statement_start; source_pos < end; source_pos++) {
					out[write_pos++] = sql[source_pos];
					if (sql[source_pos] == '\'') {
						out[write_pos++] = '\'';
					}
				}
				out[write_pos] = '\'';
			}
			copy_start = end;
			*out_rewritten = 1;
		}
		if (statement_next >= len) {
			break;
		}
		segment_start = statement_next;
	}
	status = sqlparser_vastbase_size_add(out_len, len - copy_start, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (origin_writer != NULL) {
		status = sqlparser_identifier_origin_writer_append_input(
			origin_writer,
			copy_start,
			len - copy_start,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
	}
	if (out != NULL) {
		memcpy(out + *out_len - (len - copy_start), sql + copy_start, len - copy_start);
		out[*out_len] = '\0';
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_vastbase_rewrite_session_statements(
	const char *sql,
	char **out_sql,
	unsigned int scan_flags,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	char *rewritten_sql;
	sqlparser_identifier_origin_writer_t origin_writer;
	size_t out_len;
	int rewritten;
	sqlparser_status_t status;

	if (out_sql == NULL || sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL input and output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	status = sqlparser_vastbase_rewrite_session_pass(
		sql,
		NULL,
		&out_len,
		&rewritten,
		scan_flags,
		NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK || !rewritten) {
		return status;
	}
	if (out_len == (size_t)-1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	rewritten_sql = (char *)malloc(out_len + 1U);
	if (rewritten_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memset(&origin_writer, 0, sizeof(origin_writer));
	if (origins != NULL) {
		status = sqlparser_identifier_origin_writer_begin(
			&origin_writer,
			origins,
			strlen(sql),
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			free(rewritten_sql);
			return status;
		}
	}
	status = sqlparser_vastbase_rewrite_session_pass(
		sql,
		rewritten_sql,
		&out_len,
		&rewritten,
		scan_flags,
		origins != NULL ? &origin_writer : NULL,
		out_error);
	if (status == SQLPARSER_STATUS_OK && origins != NULL) {
		status = sqlparser_identifier_origin_writer_commit(
			&origin_writer,
			out_len,
			out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_writer_release(&origin_writer);
		free(rewritten_sql);
		return status;
	}
	*out_sql = rewritten_sql;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_vastbase_internal_session_value(
	const char *sql,
	size_t start,
	size_t end,
	size_t *out_value_start,
	size_t *out_value_end,
	size_t *out_decoded_len)
{
	char quote;
	size_t decoded_len;
	size_t pos;
	size_t prefix_len;

	pos = start;
	if (!sqlparser_vastbase_word_at(sql, pos, end, "set")) {
		return 0;
	}
	pos = sqlparser_vastbase_skip_space(sql, pos + strlen("set"), end);
	prefix_len = strlen(SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT);
	if (pos + prefix_len > end ||
	    strncmp(sql + pos, SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT, prefix_len) != 0 ||
	    (pos + prefix_len < end &&
	     sqlparser_vastbase_is_ident_char((unsigned char)sql[pos + prefix_len]))) {
		return 0;
	}
	pos = sqlparser_vastbase_skip_space(sql, pos + prefix_len, end);
	if (sqlparser_vastbase_word_at(sql, pos, end, "to")) {
		pos = sqlparser_vastbase_skip_space(sql, pos + strlen("to"), end);
	} else if (pos < end && sql[pos] == '=') {
		pos = sqlparser_vastbase_skip_space(sql, pos + 1U, end);
	} else {
		return 0;
	}
	if (pos >= end || (sql[pos] != '\'' && sql[pos] != '"')) {
		return 0;
	}
	quote = sql[pos];
	*out_value_start = ++pos;
	decoded_len = 0U;
	while (pos < end) {
		if (sql[pos] != quote) {
			decoded_len++;
			pos++;
			continue;
		}
		if (pos + 1U < end && sql[pos + 1U] == quote) {
			decoded_len++;
			pos += 2U;
			continue;
		}
		*out_value_end = pos;
		pos = sqlparser_vastbase_skip_space(sql, pos + 1U, end);
		if (pos != end) {
			return 0;
		}
		*out_decoded_len = decoded_len;
		return 1;
	}
	return 0;
}

static sqlparser_status_t sqlparser_vastbase_restore_session_pass(
	const char *sql,
	char *out,
	size_t *out_len,
	int *out_rewritten,
	unsigned int scan_flags,
	sqlparser_error_t *out_error)
{
	char quote;
	size_t copy_start;
	size_t decoded_len;
	size_t end;
	size_t len;
	size_t pos;
	size_t segment_start;
	size_t statement_end;
	size_t statement_next;
	size_t statement_start;
	size_t value_end;
	size_t value_start;
	sqlparser_status_t status;

	len = strlen(sql);
	copy_start = 0U;
	segment_start = 0U;
	*out_len = 0U;
	*out_rewritten = 0;
	while (segment_start < len) {
		statement_end = sqlparser_vastbase_statement_end(
			sql,
			segment_start,
			len,
			scan_flags,
			&statement_next);
		statement_start = sqlparser_vastbase_skip_leading_trivia(
			sql, segment_start, statement_end, scan_flags);
		end = sqlparser_vastbase_trim_right(sql, statement_start, statement_end);
		if (statement_start < end &&
		    sqlparser_vastbase_internal_session_value(
			    sql,
			    statement_start,
			    end,
			    &value_start,
			    &value_end,
			    &decoded_len)) {
			status = sqlparser_vastbase_size_add(out_len, statement_start - copy_start, out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_vastbase_size_add(out_len, decoded_len, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
			if (out != NULL) {
				size_t write_pos;

				quote = sql[value_start - 1U];
				write_pos = *out_len - decoded_len - (statement_start - copy_start);
					memcpy(out + write_pos, sql + copy_start, statement_start - copy_start);
					write_pos += statement_start - copy_start;
					for (pos = value_start; pos < value_end; pos++) {
						out[write_pos++] = sql[pos];
						if (sql[pos] == quote &&
						    pos + 1U < value_end &&
						    sql[pos + 1U] == quote) {
							pos++;
						}
					}
			}
			copy_start = end;
			*out_rewritten = 1;
		}
		if (statement_next >= len) {
			break;
		}
		segment_start = statement_next;
	}
	status = sqlparser_vastbase_size_add(out_len, len - copy_start, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (out != NULL) {
		memcpy(out + *out_len - (len - copy_start), sql + copy_start, len - copy_start);
		out[*out_len] = '\0';
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_vastbase_restore_session_statements(
	const char *sql,
	char **out_sql,
	unsigned int scan_flags,
	sqlparser_error_t *out_error)
{
	char *restored_sql;
	size_t out_len;
	int rewritten;
	sqlparser_status_t status;

	if (out_sql == NULL || sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"SQL input and output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	status = sqlparser_vastbase_restore_session_pass(
		sql, NULL, &out_len, &rewritten, scan_flags, out_error);
	if (status != SQLPARSER_STATUS_OK || !rewritten) {
		return status;
	}
	if (out_len == (size_t)-1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	restored_sql = (char *)malloc(out_len + 1U);
	if (restored_sql == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	status = sqlparser_vastbase_restore_session_pass(
		sql, restored_sql, &out_len, &rewritten, scan_flags, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(restored_sql);
		return status;
	}
	*out_sql = restored_sql;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_vastbase_preprocess_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	unsigned int scan_flags,
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_error_t *out_error)
{
	const char *parser_input;
	char *rewritten_sql;
	sqlparser_status_t status;

	if (input_sql == NULL || out_parser_sql == NULL || out_state == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect preprocess input and output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	*out_state = NULL;
	if (base_ops == NULL || base_ops->preprocess == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase dialect mode is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	rewritten_sql = NULL;
	status = sqlparser_vastbase_rewrite_session_statements(
		input_sql,
		&rewritten_sql,
		scan_flags,
		NULL,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	parser_input = rewritten_sql != NULL ? rewritten_sql : input_sql;
	status = base_ops->preprocess(
		parser_input, limits, out_parser_sql, out_state, out_error);
	free(rewritten_sql);
	return status;
}

typedef sqlparser_status_t (*sqlparser_vastbase_origin_preprocess_fn)(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);

static sqlparser_status_t sqlparser_vastbase_preprocess_origins_delegate(
	sqlparser_vastbase_origin_preprocess_fn base_preprocess,
	unsigned int scan_flags,
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	const char *parser_input;
	char *rewritten_sql;
	sqlparser_status_t status;

	if (out_parser_sql != NULL) {
		*out_parser_sql = NULL;
	}
	if (out_state != NULL) {
		*out_state = NULL;
	}
	if (input_sql == NULL || out_parser_sql == NULL ||
	    out_state == NULL || origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"Vastbase identifier origin arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (base_preprocess == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"Vastbase dialect mode is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	rewritten_sql = NULL;
	status = sqlparser_vastbase_rewrite_session_statements(
		input_sql,
		&rewritten_sql,
		scan_flags,
		origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	parser_input = rewritten_sql != NULL ? rewritten_sql : input_sql;
	status = base_preprocess(
		parser_input,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
	free(rewritten_sql);
	return status;
}

sqlparser_status_t sqlparser_vastbase_oracle_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_preprocess_origins_delegate(
		sqlparser_oracle_preprocess_identifier_origins,
		SQLPARSER_VASTBASE_SCAN_ORACLE_Q_QUOTES,
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
}

sqlparser_status_t sqlparser_vastbase_mysql_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_preprocess_origins_delegate(
		sqlparser_mysql_preprocess_identifier_origins,
		SQLPARSER_VASTBASE_SCAN_BACKSLASH_ESCAPES |
			SQLPARSER_VASTBASE_SCAN_HASH_COMMENTS,
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
}

sqlparser_status_t sqlparser_vastbase_postgresql_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_preprocess_origins_delegate(
		sqlparser_postgresql_preprocess_identifier_origins,
		SQLPARSER_VASTBASE_SCAN_DOLLAR_QUOTES |
			SQLPARSER_VASTBASE_SCAN_NESTED_COMMENTS |
			SQLPARSER_VASTBASE_SCAN_POSTGRESQL_E_STRINGS,
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
}

sqlparser_status_t sqlparser_vastbase_sqlserver_preprocess_identifier_origins(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_preprocess_origins_delegate(
		sqlparser_sqlserver_preprocess_identifier_origins,
		SQLPARSER_VASTBASE_SCAN_NESTED_COMMENTS |
			SQLPARSER_VASTBASE_SCAN_SQLSERVER_GO,
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_preprocess_fragment_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *input_sql,
	void *state,
	size_t statement_index,
	char **out_parser_sql,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->preprocess_fragment == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase SQL fragment is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return base_ops->preprocess_fragment(
		input_sql, state, statement_index, out_parser_sql, out_error);
}

static sqlparser_status_t sqlparser_vastbase_postprocess_deparse_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	unsigned int scan_flags,
	const char *core_sql,
	const void *state,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	char *base_sql;
	char *restored_sql;
	sqlparser_status_t status;

	if (core_sql == NULL || out_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"dialect deparse input and output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_sql = NULL;
	if (base_ops == NULL || base_ops->postprocess_deparse == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase deparse is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	base_sql = NULL;
	status = base_ops->postprocess_deparse(
		core_sql, state, &base_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(base_sql);
		return status;
	}
	if (base_sql == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"Vastbase base dialect returned no SQL");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	restored_sql = NULL;
	status = sqlparser_vastbase_restore_session_statements(
		base_sql, &restored_sql, scan_flags, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		free(base_sql);
		free(restored_sql);
		return status;
	}
	if (restored_sql != NULL) {
		free(base_sql);
		*out_sql = restored_sql;
	} else {
		*out_sql = base_sql;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t sqlparser_vastbase_postprocess_fragment_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *core_sql,
	const void *state,
	size_t statement_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase SQL fragment is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	if (base_ops->postprocess_fragment != NULL) {
		return base_ops->postprocess_fragment(
			core_sql, state, statement_index, out_sql, out_error);
	}
	if (base_ops->postprocess_deparse != NULL) {
		return base_ops->postprocess_deparse(core_sql, state, out_sql, out_error);
	}
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase SQL fragment is not supported");
	return SQLPARSER_STATUS_UNSUPPORTED;
}

static sqlparser_status_t sqlparser_vastbase_clone_state_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	void **out_state,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->clone_state == NULL) {
		if (out_state == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "out_state must not be NULL");
			return SQLPARSER_STATUS_INVALID_ARGUMENT;
		}
		*out_state = NULL;
		return SQLPARSER_STATUS_OK;
	}
	return base_ops->clone_state(state, out_state, out_error);
}

static void sqlparser_vastbase_destroy_state_delegate(const sqlparser_dialect_ops_t *base_ops, void *state)
{
	if (base_ops != NULL && base_ops->destroy_state != NULL) {
		base_ops->destroy_state(state);
	}
}

static sqlparser_status_t sqlparser_vastbase_postprocess_literal_fragment_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *core_sql,
	const void *state,
	size_t statement_index,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->postprocess_literal_fragment == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase literal fragment is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return base_ops->postprocess_literal_fragment(
		core_sql, state, statement_index, literal_index, out_sql, out_error);
}

static const char *sqlparser_vastbase_statement_keyword_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	size_t statement_index,
	const PgQuery__Node *statement)
{
	if (base_ops == NULL || base_ops->statement_keyword == NULL) {
		return NULL;
	}
	return base_ops->statement_keyword(state, statement_index, statement);
}

static sqlparser_graph_insert_mode_t sqlparser_vastbase_insert_mode_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	size_t statement_index,
	sqlparser_graph_insert_mode_t core_mode)
{
	if (base_ops == NULL || base_ops->insert_mode == NULL) {
		return core_mode;
	}
	return base_ops->insert_mode(state, statement_index, core_mode);
}

static const char *sqlparser_vastbase_relation_object_name_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	const char *parser_object_name)
{
	if (base_ops == NULL || base_ops->relation_object_name == NULL) {
		return NULL;
	}
	return base_ops->relation_object_name(state, parser_object_name);
}

static const char *sqlparser_vastbase_relation_link_name_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const void *state,
	const char *parser_object_name)
{
	if (base_ops == NULL || base_ops->relation_link_name == NULL) {
		return NULL;
	}
	return base_ops->relation_link_name(state, parser_object_name);
}

static sqlparser_status_t sqlparser_vastbase_postprocess_control_unit_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	const char *core_sql,
	const void *state,
	size_t statement_index,
	int is_condition,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	if (base_ops == NULL || base_ops->postprocess_control_unit == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Vastbase control SQL is not supported");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return base_ops->postprocess_control_unit(
		core_sql,
		state,
		statement_index,
		is_condition,
		out_sql,
		out_error);
}

static sqlparser_control_state_t *sqlparser_vastbase_take_control_state_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	void *state)
{
	return base_ops != NULL && base_ops->take_control_state != NULL ?
		base_ops->take_control_state(state) : NULL;
}

static const char *sqlparser_vastbase_session_raw_argument(
	const PgQuery__Node *statement)
{
	const PgQuery__Node *arg;
	const PgQuery__VariableSetStmt *set_stmt;

	if (statement == NULL ||
	    statement->node_case != PG_QUERY__NODE__NODE_VARIABLE_SET_STMT ||
	    statement->variable_set_stmt == NULL ||
	    statement->variable_set_stmt->name == NULL ||
	    strcmp(
		    statement->variable_set_stmt->name,
		    SQLPARSER_INTERNAL_VASTBASE_SESSION_STATEMENT) != 0) {
		return NULL;
	}
	set_stmt = statement->variable_set_stmt;
	if (set_stmt->n_args != 1U || set_stmt->args == NULL) {
		return NULL;
	}
	arg = set_stmt->args[0];
	if (arg == NULL ||
	    arg->node_case != PG_QUERY__NODE__NODE_A_CONST ||
	    arg->a_const == NULL ||
	    arg->a_const->val_case != PG_QUERY__A__CONST__VAL_SVAL ||
	    arg->a_const->sval == NULL) {
		return NULL;
	}
	return arg->a_const->sval->sval;
}

static sqlparser_status_t sqlparser_vastbase_session_add_item(
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_graph_session_scope_t scope,
	sqlparser_graph_session_target_kind_t target_kind,
	const char *name,
	size_t name_length,
	size_t *out_item_index,
	sqlparser_error_t *out_error)
{
	return emitter->add_item(
		emitter->context,
		scope,
		target_kind,
		name,
		name != NULL ? name_length : 0U,
		out_item_index,
		out_error);
}

static char *sqlparser_vastbase_session_decode_quoted(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_error_t *out_error)
{
	char close;
	char open;
	char *decoded;
	size_t length;
	size_t pos;

	if (sql == NULL || end - start < 2U) {
		return NULL;
	}
	open = sql[start];
	close = open == '[' ? ']' : open;
	if ((open != '\'' && open != '"' && open != '`' && open != '[') ||
	    sql[end - 1U] != close) {
		return NULL;
	}
	decoded = (char *)malloc(end - start);
	if (decoded == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return NULL;
	}
	length = 0U;
	for (pos = start + 1U; pos < end - 1U; pos++) {
		if (sql[pos] == close &&
		    pos + 1U < end - 1U && sql[pos + 1U] == close) {
			pos++;
		}
		decoded[length++] = sql[pos];
	}
	decoded[length] = '\0';
	return decoded;
}

static int sqlparser_vastbase_session_parse_integer(
	const char *sql,
	size_t start,
	size_t end,
	long long *out_value)
{
	unsigned long long limit;
	unsigned long long value;
	unsigned int digit;
	int negative;

	if (sql == NULL || out_value == NULL || start >= end) {
		return 0;
	}
	negative = 0;
	if (sql[start] == '+' || sql[start] == '-') {
		negative = sql[start] == '-';
		start++;
		if (start == end) {
			return 0;
		}
	}
	value = 0U;
	limit = negative ? (unsigned long long)LLONG_MAX + 1U : (unsigned long long)LLONG_MAX;
	while (start < end) {
		if (!isdigit((unsigned char)sql[start])) {
			return 0;
		}
		digit = (unsigned int)(sql[start] - '0');
		if (value > (limit - digit) / 10U) {
			return 0;
		}
		value = value * 10U + digit;
		start++;
	}
	*out_value = negative ?
		(value == (unsigned long long)LLONG_MAX + 1U ? LLONG_MIN : -(long long)value) :
		(long long)value;
	return 1;
}

static int sqlparser_vastbase_session_span_is_identifier(
	const char *sql,
	size_t start,
	size_t end)
{
	if (sql == NULL || start >= end ||
	    !(isalpha((unsigned char)sql[start]) || sql[start] == '_' ||
	      (unsigned char)sql[start] >= 0x80U)) {
		return 0;
	}
	for (start++; start < end; start++) {
		if (!sqlparser_vastbase_is_ident_char((unsigned char)sql[start])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_vastbase_session_span_is_keyword(
	const char *sql,
	size_t start,
	size_t end)
{
	static const char *const keywords[] = {
		"all", "content", "current", "default", "document", "local",
		"off", "on"
	};
	size_t index;

	for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
		if (sqlparser_vastbase_word_at(sql, start, end, keywords[index]) &&
		    start + strlen(keywords[index]) == end) {
			return 1;
		}
	}
	return 0;
}

static sqlparser_status_t sqlparser_vastbase_session_emit_span(
	const char *sql,
	size_t start,
	size_t end,
	size_t item_index,
	const char *name,
	sqlparser_graph_session_value_kind_t forced_kind,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_session_value_t value;
	char *decoded;
	long long integer_value;

	start = sqlparser_vastbase_skip_space(sql, start, end);
	end = sqlparser_vastbase_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Vastbase session value is empty");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	memset(&value, 0, sizeof(value));
	value.name = name;
	value.name_length = name != NULL ? strlen(name) : 0U;
	decoded = sqlparser_vastbase_session_decode_quoted(
		sql, start, end, out_error);
	if (decoded != NULL) {
		value.kind = sql[start] == '\'' ?
			SQLPARSER_GRAPH_SESSION_VALUE_LITERAL :
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER;
		if (value.kind == SQLPARSER_GRAPH_SESSION_VALUE_LITERAL) {
			value.literal.kind = SQLPARSER_LITERAL_KIND_STRING;
			value.literal.string_value = decoded;
		} else {
			value.text = decoded;
			value.text_length = strlen(decoded);
		}
		if (emitter->add_value(
			    emitter->context,
			    item_index,
			    &value,
			    out_error) != SQLPARSER_STATUS_OK) {
			free(decoded);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		free(decoded);
		return SQLPARSER_STATUS_OK;
	}
	if (out_error != NULL &&
	    (out_error->code == SQLPARSER_STATUS_NO_MEMORY ||
	     out_error->code == SQLPARSER_STATUS_RESOURCE_LIMIT)) {
		return out_error->code;
	}
	sqlparser_error_clear(out_error);
	if (forced_kind == SQLPARSER_GRAPH_SESSION_VALUE_UNKNOWN &&
	    sqlparser_vastbase_session_parse_integer(
		    sql, start, end, &integer_value)) {
		value.kind = SQLPARSER_GRAPH_SESSION_VALUE_LITERAL;
		value.literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
		value.literal.integer_value = integer_value;
	} else {
		value.kind = forced_kind != SQLPARSER_GRAPH_SESSION_VALUE_UNKNOWN ?
			forced_kind :
			(sqlparser_vastbase_session_span_is_keyword(sql, start, end) ?
				SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD :
				(sqlparser_vastbase_session_span_is_identifier(sql, start, end) ?
					SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER :
					SQLPARSER_GRAPH_SESSION_VALUE_EXPRESSION));
		value.text = sql + start;
		value.text_length = end - start;
	}
	return emitter->add_value(
		emitter->context,
		item_index,
		&value,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_session_project_transaction(
	const char *sql,
	size_t pos,
	size_t end,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	const char *value_name;
	size_t item_index;
	size_t value_end;
	size_t value_start;

	if (sqlparser_vastbase_session_add_item(
		    emitter,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_TRANSACTION,
		    NULL,
		    0U,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	while (pos < end) {
		pos = sqlparser_vastbase_skip_space(sql, pos, end);
		if (sqlparser_vastbase_consume_word(sql, &pos, end, "isolation")) {
			if (!sqlparser_vastbase_consume_word(sql, &pos, end, "level")) {
				goto invalid;
			}
			value_name = "isolation_level";
			value_start = sqlparser_vastbase_skip_space(sql, pos, end);
			if (sqlparser_vastbase_consume_word(sql, &pos, end, "serializable")) {
				/* Complete value. */
			} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "repeatable")) {
				if (!sqlparser_vastbase_consume_word(sql, &pos, end, "read")) {
					goto invalid;
				}
			} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "read")) {
				if (!sqlparser_vastbase_consume_word(sql, &pos, end, "committed") &&
				    !sqlparser_vastbase_consume_word(sql, &pos, end, "uncommitted")) {
					goto invalid;
				}
			} else {
				goto invalid;
			}
		} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "read")) {
			value_name = "access_mode";
			value_start = pos - strlen("read");
			if (!sqlparser_vastbase_consume_word(sql, &pos, end, "only") &&
			    !sqlparser_vastbase_consume_word(sql, &pos, end, "write")) {
				goto invalid;
			}
		} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "not")) {
			value_name = "deferrable";
			value_start = pos - strlen("not");
			if (!sqlparser_vastbase_consume_word(sql, &pos, end, "deferrable")) {
				goto invalid;
			}
		} else if (sqlparser_vastbase_consume_word(sql, &pos, end, "deferrable")) {
			value_name = "deferrable";
			value_start = pos - strlen("deferrable");
		} else {
			goto invalid;
		}
		value_end = sqlparser_vastbase_trim_right(sql, value_start, pos);
		if (sqlparser_vastbase_session_emit_span(
			    sql,
			    value_start,
			    value_end,
			    item_index,
			    value_name,
			    SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD,
			    emitter,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		pos = sqlparser_vastbase_skip_space(sql, pos, end);
		if (pos == end) {
			return SQLPARSER_STATUS_OK;
		}
		if (sql[pos] != ',') {
			goto invalid;
		}
		pos++;
	}

invalid:
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Vastbase transaction characteristics are invalid");
	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static sqlparser_status_t sqlparser_vastbase_project_common_session(
	const PgQuery__Node *statement,
	unsigned int scan_flags,
	const sqlparser_dialect_session_emitter_t *emitter,
	int *out_handled,
	sqlparser_error_t *out_error)
{
	const char *raw_sql;
	size_t end;
	size_t item_index;
	size_t name_end;
	size_t name_start;
	size_t pos;
	size_t token_end;
	size_t value_start;

	if (out_handled == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Vastbase session projection state is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	*out_handled = 0;
	raw_sql = sqlparser_vastbase_session_raw_argument(statement);
	if (raw_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	*out_handled = 1;
	if (emitter == NULL || emitter->set_action == NULL ||
	    emitter->add_item == NULL || emitter->add_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "session graph emitter is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	end = strlen(raw_sql);
	pos = sqlparser_vastbase_skip_space(raw_sql, 0U, end);
	if (!sqlparser_vastbase_consume_word(raw_sql, &pos, end, "alter") ||
	    !sqlparser_vastbase_consume_word(raw_sql, &pos, end, "session") ||
	    !sqlparser_vastbase_consume_word(raw_sql, &pos, end, "set")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Vastbase session statement is invalid");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	pos = sqlparser_vastbase_skip_space(raw_sql, pos, end);
	if (sqlparser_vastbase_word_at(raw_sql, pos, end, "current_schema")) {
		pos = sqlparser_vastbase_skip_space(
			raw_sql, pos + strlen("current_schema"), end);
		if (sqlparser_vastbase_word_at(raw_sql, pos, end, "to")) {
			pos = sqlparser_vastbase_skip_space(raw_sql, pos + strlen("to"), end);
		}
		token_end = sqlparser_vastbase_token_end(
			raw_sql, pos, end, scan_flags);
		if (emitter->set_action(
			    emitter->context,
			    SQLPARSER_GRAPH_SESSION_ACTION_SWITCH,
			    out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_vastbase_session_add_item(
			    emitter,
			    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			    SQLPARSER_GRAPH_SESSION_TARGET_SCHEMA,
			    NULL,
			    0U,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return sqlparser_vastbase_session_emit_span(
			raw_sql,
			pos,
			token_end,
			item_index,
			NULL,
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			emitter,
			out_error);
	}
	if (emitter->set_action(
		    emitter->context,
		    SQLPARSER_GRAPH_SESSION_ACTION_SET,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (sqlparser_vastbase_word_at(raw_sql, pos, end, "names")) {
		name_start = pos;
		name_end = pos + strlen("names");
		value_start = sqlparser_vastbase_skip_space(raw_sql, name_end, end);
		token_end = sqlparser_vastbase_token_end(
			raw_sql, value_start, end, scan_flags);
	} else if (sqlparser_vastbase_word_at(raw_sql, pos, end, "time")) {
		name_start = pos;
		pos = sqlparser_vastbase_skip_space(raw_sql, pos + strlen("time"), end);
		if (!sqlparser_vastbase_word_at(raw_sql, pos, end, "zone")) {
			goto invalid;
		}
		name_end = pos + strlen("zone");
		value_start = sqlparser_vastbase_skip_space(raw_sql, name_end, end);
		token_end = sqlparser_vastbase_token_end(
			raw_sql, value_start, end, scan_flags);
	} else if (sqlparser_vastbase_word_at(raw_sql, pos, end, "xml")) {
		name_start = pos;
		pos = sqlparser_vastbase_skip_space(raw_sql, pos + strlen("xml"), end);
		if (!sqlparser_vastbase_word_at(raw_sql, pos, end, "option")) {
			goto invalid;
		}
		name_end = pos + strlen("option");
		value_start = sqlparser_vastbase_skip_space(raw_sql, name_end, end);
		token_end = sqlparser_vastbase_token_end(
			raw_sql, value_start, end, scan_flags);
	} else if (sqlparser_vastbase_word_at(raw_sql, pos, end, "role")) {
		pos = sqlparser_vastbase_skip_space(raw_sql, pos + strlen("role"), end);
		token_end = sqlparser_vastbase_token_end(
			raw_sql, pos, end, scan_flags);
		value_start = pos;
		pos = sqlparser_vastbase_skip_space(raw_sql, token_end, end);
		if (!sqlparser_vastbase_word_at(raw_sql, pos, end, "password")) {
			goto invalid;
		}
		pos = sqlparser_vastbase_skip_space(raw_sql, pos + strlen("password"), end);
		name_end = sqlparser_vastbase_token_end(
			raw_sql, pos, end, scan_flags);
		if (sqlparser_vastbase_session_add_item(
			    emitter,
			    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
			    SQLPARSER_GRAPH_SESSION_TARGET_ROLE,
			    NULL,
			    0U,
			    &item_index,
			    out_error) != SQLPARSER_STATUS_OK ||
		    sqlparser_vastbase_session_emit_span(
			    raw_sql,
			    value_start,
			    token_end,
			    item_index,
			    NULL,
			    SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER,
			    emitter,
			    out_error) != SQLPARSER_STATUS_OK) {
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		return sqlparser_vastbase_session_emit_span(
			raw_sql,
			pos,
			name_end,
			item_index,
			"password",
			SQLPARSER_GRAPH_SESSION_VALUE_UNKNOWN,
			emitter,
			out_error);
	} else if (sqlparser_vastbase_word_at(raw_sql, pos, end, "session")) {
		pos = sqlparser_vastbase_skip_space(raw_sql, pos + strlen("session"), end);
		if (sqlparser_vastbase_word_at(raw_sql, pos, end, "authorization")) {
			pos = sqlparser_vastbase_skip_space(
				raw_sql, pos + strlen("authorization"), end);
			token_end = sqlparser_vastbase_token_end(
				raw_sql, pos, end, scan_flags);
			if (sqlparser_vastbase_session_add_item(
				    emitter,
				    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
				    SQLPARSER_GRAPH_SESSION_TARGET_AUTHORIZATION,
				    NULL,
				    0U,
				    &item_index,
				    out_error) != SQLPARSER_STATUS_OK ||
			    sqlparser_vastbase_session_emit_span(
				    raw_sql,
				    pos,
				    token_end,
				    item_index,
				    NULL,
				    SQLPARSER_GRAPH_SESSION_VALUE_UNKNOWN,
				    emitter,
				    out_error) != SQLPARSER_STATUS_OK) {
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
			}
			pos = sqlparser_vastbase_skip_space(raw_sql, token_end, end);
			if (pos == end) {
				return SQLPARSER_STATUS_OK;
			}
			if (!sqlparser_vastbase_word_at(raw_sql, pos, end, "password")) {
				goto invalid;
			}
			pos = sqlparser_vastbase_skip_space(
				raw_sql, pos + strlen("password"), end);
			token_end = sqlparser_vastbase_token_end(
				raw_sql, pos, end, scan_flags);
			return sqlparser_vastbase_session_emit_span(
				raw_sql,
				pos,
				token_end,
				item_index,
				"password",
				SQLPARSER_GRAPH_SESSION_VALUE_UNKNOWN,
				emitter,
				out_error);
		}
		if (!sqlparser_vastbase_consume_word(
			    raw_sql, &pos, end, "characteristics") ||
		    !sqlparser_vastbase_consume_word(raw_sql, &pos, end, "as") ||
		    !sqlparser_vastbase_consume_word(raw_sql, &pos, end, "transaction")) {
			goto invalid;
		}
		return sqlparser_vastbase_session_project_transaction(
			raw_sql, pos, end, emitter, out_error);
	} else if (sqlparser_vastbase_word_at(raw_sql, pos, end, "transaction")) {
		pos += strlen("transaction");
		return sqlparser_vastbase_session_project_transaction(
			raw_sql, pos, end, emitter, out_error);
	} else if (sqlparser_vastbase_word_at(raw_sql, pos, end, "statement_timeout")) {
		name_start = pos;
		name_end = pos + strlen("statement_timeout");
		pos = sqlparser_vastbase_skip_space(raw_sql, name_end, end);
		if (pos < end && raw_sql[pos] == '=') {
			pos = sqlparser_vastbase_skip_space(raw_sql, pos + 1U, end);
		} else if (sqlparser_vastbase_word_at(raw_sql, pos, end, "to")) {
			pos = sqlparser_vastbase_skip_space(raw_sql, pos + strlen("to"), end);
		} else if (sqlparser_vastbase_word_at(raw_sql, pos, end, "from")) {
			pos = sqlparser_vastbase_skip_space(raw_sql, pos + strlen("from"), end);
		} else {
			goto invalid;
		}
		value_start = pos;
		token_end = sqlparser_vastbase_token_end(
			raw_sql, value_start, end, scan_flags);
	} else {
		goto invalid;
	}
	if (token_end <= value_start ||
	    sqlparser_vastbase_skip_space(raw_sql, token_end, end) != end) {
		goto invalid;
	}
	if (sqlparser_vastbase_session_add_item(
		    emitter,
		    SQLPARSER_GRAPH_SESSION_SCOPE_SESSION,
		    SQLPARSER_GRAPH_SESSION_TARGET_PARAMETER,
		    raw_sql + name_start,
		    name_end - name_start,
		    &item_index,
		    out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	return sqlparser_vastbase_session_emit_span(
		raw_sql,
		value_start,
		token_end,
		item_index,
		NULL,
		SQLPARSER_GRAPH_SESSION_VALUE_UNKNOWN,
		emitter,
		out_error);

invalid:
	sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR, "Vastbase session statement is invalid");
	return SQLPARSER_STATUS_INTERNAL_ERROR;
}

static sqlparser_status_t sqlparser_vastbase_project_session_delegate(
	const sqlparser_dialect_ops_t *base_ops,
	unsigned int scan_flags,
	const sqlparser_handle_t *handle,
	const void *state,
	size_t statement_index,
	const PgQuery__Node *statement,
	const sqlparser_dialect_session_emitter_t *emitter,
	sqlparser_error_t *out_error)
{
	int handled;
	sqlparser_status_t status;

	status = sqlparser_vastbase_project_common_session(
		statement, scan_flags, emitter, &handled, out_error);
	if (status != SQLPARSER_STATUS_OK || handled) {
		return status;
	}
	if (base_ops == NULL || base_ops->project_session == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	return base_ops->project_session(
		handle,
		state,
		statement_index,
		statement,
		emitter,
		out_error);
}

#define SQLPARSER_DEFINE_VASTBASE_PRE_POST(TAG, BASE_OPS_FN, SCAN_FLAGS) \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_preprocess( \
		const char *input_sql, \
		const sqlparser_limits_t *limits, \
		char **out_parser_sql, \
		void **out_state, \
		sqlparser_error_t *out_error) \
		{ \
			return sqlparser_vastbase_preprocess_delegate( \
				BASE_OPS_FN(), \
				SCAN_FLAGS, \
				input_sql, \
			limits, \
			out_parser_sql, \
			out_state, \
			out_error); \
	} \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_postprocess_deparse( \
		const char *core_sql, \
		const void *state, \
		char **out_sql, \
		sqlparser_error_t *out_error) \
		{ \
			return sqlparser_vastbase_postprocess_deparse_delegate( \
				BASE_OPS_FN(), \
				SCAN_FLAGS, \
				core_sql, \
			state, \
			out_sql, \
			out_error); \
	} \
	static const char *sqlparser_vastbase_##TAG##_statement_keyword( \
		const void *state, \
		size_t statement_index, \
		const PgQuery__Node *statement) \
	{ \
		return sqlparser_vastbase_statement_keyword_delegate( \
			BASE_OPS_FN(), \
			state, \
			statement_index, \
			statement); \
	} \
	static sqlparser_graph_insert_mode_t sqlparser_vastbase_##TAG##_insert_mode( \
		const void *state, \
		size_t statement_index, \
		sqlparser_graph_insert_mode_t core_mode) \
	{ \
		return sqlparser_vastbase_insert_mode_delegate( \
			BASE_OPS_FN(), \
			state, \
			statement_index, \
			core_mode); \
	} \
	static const char *sqlparser_vastbase_##TAG##_relation_object_name( \
		const void *state, \
		const char *parser_object_name) \
	{ \
		return sqlparser_vastbase_relation_object_name_delegate( \
			BASE_OPS_FN(), \
			state, \
			parser_object_name); \
	} \
	static const char *sqlparser_vastbase_##TAG##_relation_link_name( \
		const void *state, \
		const char *parser_object_name) \
	{ \
		return sqlparser_vastbase_relation_link_name_delegate( \
			BASE_OPS_FN(), \
			state, \
			parser_object_name); \
	} \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_project_session( \
		const sqlparser_handle_t *handle, \
		const void *state, \
		size_t statement_index, \
		const PgQuery__Node *statement, \
		const sqlparser_dialect_session_emitter_t *emitter, \
		sqlparser_error_t *out_error) \
	{ \
		return sqlparser_vastbase_project_session_delegate( \
			BASE_OPS_FN(), \
			SCAN_FLAGS, \
			handle, \
			state, \
			statement_index, \
			statement, \
			emitter, \
			out_error); \
	}

#define SQLPARSER_DEFINE_VASTBASE_STATEFUL(TAG, BASE_OPS_FN, SCAN_FLAGS) \
	SQLPARSER_DEFINE_VASTBASE_PRE_POST(TAG, BASE_OPS_FN, SCAN_FLAGS) \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_preprocess_fragment( \
		const char *input_sql, \
		void *state, \
		size_t statement_index, \
		char **out_parser_sql, \
		sqlparser_error_t *out_error) \
	{ \
		return sqlparser_vastbase_preprocess_fragment_delegate( \
			BASE_OPS_FN(), \
			input_sql, \
			state, \
			statement_index, \
			out_parser_sql, \
			out_error); \
	} \
	static sqlparser_status_t sqlparser_vastbase_##TAG##_clone_state( \
		const void *state, \
		void **out_state, \
		sqlparser_error_t *out_error) \
	{ \
		return sqlparser_vastbase_clone_state_delegate(BASE_OPS_FN(), state, out_state, out_error); \
	} \
	static void sqlparser_vastbase_##TAG##_destroy_state(void *state) \
	{ \
		sqlparser_vastbase_destroy_state_delegate(BASE_OPS_FN(), state); \
	}

SQLPARSER_DEFINE_VASTBASE_STATEFUL(
	oracle,
	sqlparser_dialect_oracle_ops,
	SQLPARSER_VASTBASE_SCAN_ORACLE_Q_QUOTES)
SQLPARSER_DEFINE_VASTBASE_STATEFUL(
	mysql,
	sqlparser_dialect_mysql_ops,
	SQLPARSER_VASTBASE_SCAN_BACKSLASH_ESCAPES |
		SQLPARSER_VASTBASE_SCAN_HASH_COMMENTS)
SQLPARSER_DEFINE_VASTBASE_STATEFUL(
	postgresql,
	sqlparser_dialect_postgresql_ops,
	SQLPARSER_VASTBASE_SCAN_DOLLAR_QUOTES |
		SQLPARSER_VASTBASE_SCAN_NESTED_COMMENTS |
		SQLPARSER_VASTBASE_SCAN_POSTGRESQL_E_STRINGS)
SQLPARSER_DEFINE_VASTBASE_STATEFUL(
	sqlserver,
	sqlparser_dialect_sqlserver_ops,
	SQLPARSER_VASTBASE_SCAN_NESTED_COMMENTS |
		SQLPARSER_VASTBASE_SCAN_SQLSERVER_GO)

static sqlparser_status_t sqlparser_vastbase_oracle_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_literal_fragment_delegate(
		sqlparser_dialect_oracle_ops(),
		core_sql,
		state,
		statement_index,
		literal_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_mysql_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_literal_fragment_delegate(
		sqlparser_dialect_mysql_ops(),
		core_sql,
		state,
		statement_index,
		literal_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_postgresql_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_literal_fragment_delegate(
		sqlparser_dialect_postgresql_ops(),
		core_sql,
		state,
		statement_index,
		literal_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_sqlserver_postprocess_literal_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	size_t literal_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_literal_fragment_delegate(
		sqlparser_dialect_sqlserver_ops(),
		core_sql,
		state,
		statement_index,
		literal_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_sqlserver_postprocess_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_fragment_delegate(
		sqlparser_dialect_sqlserver_ops(),
		core_sql,
		state,
		statement_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_mysql_postprocess_fragment(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_fragment_delegate(
		sqlparser_dialect_mysql_ops(),
		core_sql,
		state,
		statement_index,
		out_sql,
		out_error);
}

static sqlparser_status_t sqlparser_vastbase_sqlserver_postprocess_control_unit(
	const char *core_sql,
	const void *state,
	size_t statement_index,
	int is_condition,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	return sqlparser_vastbase_postprocess_control_unit_delegate(
		sqlparser_dialect_sqlserver_ops(),
		core_sql,
		state,
		statement_index,
		is_condition,
		out_sql,
		out_error);
}

static sqlparser_control_state_t *sqlparser_vastbase_sqlserver_take_control_state(void *state)
{
	return sqlparser_vastbase_take_control_state_delegate(
		sqlparser_dialect_sqlserver_ops(), state);
}

static const sqlparser_dialect_ops_t SQLPARSER_VASTBASE_ORACLE_OPS = {
	SQLPARSER_DIALECT_VASTBASE_ORACLE,
	"vastbase-oracle",
	sqlparser_vastbase_oracle_preprocess,
	sqlparser_vastbase_oracle_preprocess_fragment,
	sqlparser_vastbase_oracle_postprocess_deparse,
	sqlparser_vastbase_oracle_clone_state,
	sqlparser_vastbase_oracle_destroy_state,
	sqlparser_vastbase_oracle_postprocess_literal_fragment,
	sqlparser_vastbase_oracle_statement_keyword,
	sqlparser_vastbase_oracle_insert_mode,
	sqlparser_vastbase_oracle_relation_object_name,
	sqlparser_vastbase_oracle_relation_link_name,
	NULL,
	NULL,
	NULL,
	sqlparser_vastbase_oracle_project_session
};

static const sqlparser_dialect_ops_t SQLPARSER_VASTBASE_MYSQL_OPS = {
	SQLPARSER_DIALECT_VASTBASE_MYSQL,
	"vastbase-mysql",
	sqlparser_vastbase_mysql_preprocess,
	sqlparser_vastbase_mysql_preprocess_fragment,
	sqlparser_vastbase_mysql_postprocess_deparse,
	sqlparser_vastbase_mysql_clone_state,
	sqlparser_vastbase_mysql_destroy_state,
	sqlparser_vastbase_mysql_postprocess_literal_fragment,
	sqlparser_vastbase_mysql_statement_keyword,
	sqlparser_vastbase_mysql_insert_mode,
	sqlparser_vastbase_mysql_relation_object_name,
	sqlparser_vastbase_mysql_relation_link_name,
	sqlparser_vastbase_mysql_postprocess_fragment,
	NULL,
	NULL,
	sqlparser_vastbase_mysql_project_session
};

static const sqlparser_dialect_ops_t SQLPARSER_VASTBASE_POSTGRESQL_OPS = {
	SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
	"vastbase-postgresql",
	sqlparser_vastbase_postgresql_preprocess,
	sqlparser_vastbase_postgresql_preprocess_fragment,
	sqlparser_vastbase_postgresql_postprocess_deparse,
	sqlparser_vastbase_postgresql_clone_state,
	sqlparser_vastbase_postgresql_destroy_state,
	sqlparser_vastbase_postgresql_postprocess_literal_fragment,
	sqlparser_vastbase_postgresql_statement_keyword,
	sqlparser_vastbase_postgresql_insert_mode,
	sqlparser_vastbase_postgresql_relation_object_name,
	sqlparser_vastbase_postgresql_relation_link_name,
	NULL,
	NULL,
	NULL,
	sqlparser_vastbase_postgresql_project_session
};

static const sqlparser_dialect_ops_t SQLPARSER_VASTBASE_SQLSERVER_OPS = {
	SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
	"vastbase-sqlserver",
	sqlparser_vastbase_sqlserver_preprocess,
	sqlparser_vastbase_sqlserver_preprocess_fragment,
	sqlparser_vastbase_sqlserver_postprocess_deparse,
	sqlparser_vastbase_sqlserver_clone_state,
	sqlparser_vastbase_sqlserver_destroy_state,
	sqlparser_vastbase_sqlserver_postprocess_literal_fragment,
	sqlparser_vastbase_sqlserver_statement_keyword,
	sqlparser_vastbase_sqlserver_insert_mode,
	sqlparser_vastbase_sqlserver_relation_object_name,
	sqlparser_vastbase_sqlserver_relation_link_name,
	sqlparser_vastbase_sqlserver_postprocess_fragment,
	sqlparser_vastbase_sqlserver_postprocess_control_unit,
	sqlparser_vastbase_sqlserver_take_control_state,
	sqlparser_vastbase_sqlserver_project_session
};

const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_oracle_ops(void)
{
	return &SQLPARSER_VASTBASE_ORACLE_OPS;
}

const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_mysql_ops(void)
{
	return &SQLPARSER_VASTBASE_MYSQL_OPS;
}

const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_postgresql_ops(void)
{
	return &SQLPARSER_VASTBASE_POSTGRESQL_OPS;
}

const sqlparser_dialect_ops_t *sqlparser_dialect_vastbase_sqlserver_ops(void)
{
	return &SQLPARSER_VASTBASE_SQLSERVER_OPS;
}
