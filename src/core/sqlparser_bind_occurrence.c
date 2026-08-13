#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "../dialect/sqlparser_dialect_internal.h"
#include "../dialect/sqlparser_dialect_sqlserver_scan.h"
#include "sqlparser_internal.h"

static int sqlparser_bind_is_postgresql(sqlparser_dialect_t dialect)
{
	return dialect == SQLPARSER_DIALECT_POSTGRESQL ||
		dialect == SQLPARSER_DIALECT_VASTBASE_POSTGRESQL;
}

static int sqlparser_bind_oracle_name_start(unsigned char ch)
{
	return isalpha(ch) || ch == '_';
}

static int sqlparser_bind_oracle_name_char(unsigned char ch)
{
	return isalnum(ch) || ch == '_' || ch == '$' || ch == '#';
}

static int sqlparser_bind_boundary_before(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t start)
{
	unsigned char previous;

	if (start == 0U) {
		return 1;
	}
	previous = (unsigned char)sql[start - 1U];
	if (previous == '.' || sqlparser_public_char_is_ident(previous)) {
		return 0;
	}
	if (sqlparser_dialect_is_sqlserver_compatible(dialect) &&
	    previous == '@') {
		return 0;
	}
	return 1;
}

static int sqlparser_bind_boundary_after(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t end)
{
	unsigned char next;

	next = (unsigned char)sql[end];
	if (sqlparser_public_char_is_ident(next) ||
	    (next == '.' &&
	     !sqlparser_dialect_is_sqlserver_compatible(dialect))) {
		return 0;
	}
	if (sqlparser_dialect_is_sqlserver_compatible(dialect) && next == '@') {
		return 0;
	}
	return 1;
}

static int sqlparser_bind_unsigned_value(
	const char *sql,
	size_t start,
	size_t end,
	size_t *out_value)
{
	size_t value;

	value = 0U;
	while (start < end) {
		unsigned int digit;

		digit = (unsigned int)(sql[start] - '0');
		if (value > (SIZE_MAX - digit) / 10U) {
			return 0;
		}
		value = value * 10U + digit;
		start++;
	}
	*out_value = value;
	return 1;
}

static int sqlparser_bind_dollar_token(
	const sqlparser_bind_scanner_t *scanner,
	size_t start,
	sqlparser_bind_token_t *out_token)
{
	size_t end;
	size_t value;
	int public_token;

	if (scanner->sql[start] != '$' ||
	    scanner->sql[start + 1U] < '1' ||
	    scanner->sql[start + 1U] > '9' ||
	    !sqlparser_bind_boundary_before(scanner->dialect, scanner->sql, start)) {
		return 0;
	}
	end = start + 1U;
	while (isdigit((unsigned char)scanner->sql[end])) {
		end++;
	}
	if (sqlparser_public_char_is_ident((unsigned char)scanner->sql[end]) ||
	    end == start + 1U) {
		return 0;
	}
	value = 0U;
	public_token = sqlparser_bind_is_postgresql(scanner->dialect) ||
		scanner->dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE;
	if (!public_token &&
	    (!scanner->allow_markers ||
	     !sqlparser_bind_unsigned_value(
		     scanner->sql, start + 1U, end, &value) ||
	     value < scanner->marker_start ||
	     value >= scanner->marker_end)) {
		return 0;
	}
	out_token->start = start;
	out_token->end = end;
	out_token->key_start = start + 1U;
	out_token->key_length = end - start - 1U;
	out_token->kind = SQLPARSER_BIND_KIND_POSITIONAL;
	return 1;
}

static int sqlparser_bind_colon_token(
	const sqlparser_bind_scanner_t *scanner,
	size_t start,
	sqlparser_bind_token_t *out_token)
{
	const char *sql;
	size_t end;
	sqlparser_bind_kind_t kind;

	if (!sqlparser_dialect_is_oracle_or_dameng_compatible(scanner->dialect)) {
		return 0;
	}
	sql = scanner->sql;
	if (sql[start] != ':' || sql[start + 1U] == ':' ||
	    sql[start + 1U] == '=' ||
	    (start > 0U && sql[start - 1U] == ':') ||
	    !sqlparser_bind_boundary_before(scanner->dialect, sql, start)) {
		return 0;
	}
	end = start + 1U;
	if (isdigit((unsigned char)sql[end])) {
		kind = SQLPARSER_BIND_KIND_POSITIONAL;
		while (isdigit((unsigned char)sql[end])) {
			end++;
		}
	} else if (sqlparser_bind_oracle_name_start((unsigned char)sql[end])) {
		kind = SQLPARSER_BIND_KIND_NAMED;
		for (;;) {
			end++;
			while (sqlparser_bind_oracle_name_char((unsigned char)sql[end])) {
				end++;
			}
			if (sql[end] != '.' ||
			    !sqlparser_bind_oracle_name_start(
				    (unsigned char)sql[end + 1U])) {
				break;
			}
			end++;
		}
	} else {
		return 0;
	}
	if ((kind == SQLPARSER_BIND_KIND_POSITIONAL &&
	     sqlparser_public_char_is_ident((unsigned char)sql[end])) ||
	    (kind != SQLPARSER_BIND_KIND_POSITIONAL &&
	     !sqlparser_bind_boundary_after(scanner->dialect, sql, end))) {
		return 0;
	}
	out_token->start = start;
	out_token->end = end;
	out_token->key_start = start + 1U;
	out_token->key_length = end - start - 1U;
	out_token->kind = kind;
	return 1;
}

static int sqlparser_bind_at_token(
	const sqlparser_bind_scanner_t *scanner,
	size_t start,
	sqlparser_bind_token_t *out_token)
{
	const char *sql;
	size_t end;

	if (!sqlparser_dialect_is_sqlserver_compatible(scanner->dialect)) {
		return 0;
	}
	sql = scanner->sql;
	if (sql[start] != '@' || sql[start + 1U] == '@' ||
	    (!sqlparser_sqlserver_is_ident_start(
		     (unsigned char)sql[start + 1U]) &&
	     !isdigit((unsigned char)sql[start + 1U])) ||
	    !sqlparser_bind_boundary_before(scanner->dialect, sql, start)) {
		return 0;
	}
	end = start + 2U;
	while (sqlparser_sqlserver_is_ident_char((unsigned char)sql[end])) {
		end++;
	}
	if (!sqlparser_bind_boundary_after(scanner->dialect, sql, end)) {
		return 0;
	}
	out_token->start = start;
	out_token->end = end;
	out_token->key_start = start + 1U;
	out_token->key_length = end - start - 1U;
	out_token->kind = SQLPARSER_BIND_KIND_NAMED;
	return 1;
}

static int sqlparser_bind_question_token(
	const sqlparser_bind_scanner_t *scanner,
	size_t start,
	sqlparser_bind_token_t *out_token)
{
	if (scanner->sql[start] != '?' ||
	    sqlparser_bind_is_postgresql(scanner->dialect)) {
		return 0;
	}
	out_token->start = start;
	out_token->end = start + 1U;
	out_token->key_start = 0U;
	out_token->key_length = 0U;
	out_token->kind = SQLPARSER_BIND_KIND_POSITIONAL;
	return 1;
}

static size_t sqlparser_bind_mysql_executable_skip(
	const char *sql,
	size_t index)
{
	char quote;
	size_t pos;

	quote = sql[index];
	if (quote != '\'' && quote != '"' && quote != '`') {
		return sqlparser_public_skip_quoted_or_comment(
			SQLPARSER_DIALECT_MYSQL,
			sql,
			index);
	}
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

static int sqlparser_bind_mysql_executable_body(
	const sqlparser_bind_scanner_t *scanner,
	size_t comment_start,
	size_t comment_end,
	size_t *out_start,
	size_t *out_end)
{
	const char *sql;
	size_t body_end;
	size_t body_start;
	size_t pos;
	size_t statement_end;
	size_t version_start;

	if (!sqlparser_dialect_is_mysql_compatible(scanner->dialect) ||
	    scanner->code_start == SIZE_MAX || comment_start != scanner->code_start ||
	    comment_end - comment_start < 5U) {
		return 0;
	}
	sql = scanner->sql;
	if (sql[comment_start] != '/' || sql[comment_start + 1U] != '*' ||
	    sql[comment_start + 2U] != '!' || sql[comment_end - 2U] != '*' ||
	    sql[comment_end - 1U] != '/') {
		return 0;
	}
	body_start = comment_start + 3U;
	version_start = body_start;
	while (body_start < comment_end - 2U &&
	       isdigit((unsigned char)sql[body_start])) {
		body_start++;
	}
	if (body_start != version_start && body_start - version_start != 5U &&
	    body_start - version_start != 6U) {
		return 0;
	}
	while (body_start < comment_end - 2U &&
	       isspace((unsigned char)sql[body_start])) {
		body_start++;
	}
	body_end = comment_end - 2U;
	while (body_end > body_start && isspace((unsigned char)sql[body_end - 1U])) {
		body_end--;
	}
	if (body_start == body_end) {
		return 0;
	}
	for (pos = body_start; pos < body_end; pos++) {
		size_t skipped;

		skipped = sqlparser_bind_mysql_executable_skip(scanner->sql, pos);
		if (skipped > pos && skipped <= body_end) {
			pos = skipped - 1U;
			continue;
		}
		if (sql[pos] == ';') {
			return 0;
		}
	}
	statement_end = comment_end;
	while (statement_end < scanner->length && sql[statement_end] != ';') {
		if (!isspace((unsigned char)sql[statement_end])) {
			return 0;
		}
		statement_end++;
	}
	*out_start = body_start;
	*out_end = comment_end - 2U;
	return 1;
}

void sqlparser_bind_scanner_init(
	sqlparser_bind_scanner_t *scanner,
	sqlparser_dialect_t dialect,
	const char *sql)
{
	if (scanner == NULL) {
		return;
	}
	memset(scanner, 0, sizeof(*scanner));
	scanner->dialect = dialect;
	scanner->sql = sql;
	scanner->length = sql != NULL ? strlen(sql) : 0U;
}

void sqlparser_bind_scanner_init_markers(
	sqlparser_bind_scanner_t *scanner,
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t marker_start,
	size_t marker_count)
{
	sqlparser_bind_scanner_init(scanner, dialect, sql);
	if (scanner == NULL || marker_count == 0U ||
	    marker_start > SIZE_MAX - marker_count) {
		return;
	}
	scanner->marker_start = marker_start;
	scanner->marker_end = marker_start + marker_count;
	scanner->allow_markers = 1;
}

int sqlparser_bind_scanner_next(
	sqlparser_bind_scanner_t *scanner,
	sqlparser_bind_token_t *out_token)
{
	if (scanner == NULL || out_token == NULL || scanner->sql == NULL) {
		return 0;
	}
	memset(out_token, 0, sizeof(*out_token));
	while (scanner->index < scanner->length) {
		size_t index;
		size_t skipped;

		if (scanner->code_end != 0U && scanner->index >= scanner->code_end) {
			scanner->index = scanner->length >= 2U &&
				scanner->code_end <= scanner->length - 2U ?
				scanner->code_end + 2U :
				scanner->length;
			scanner->code_start = SIZE_MAX;
			scanner->code_end = 0U;
			continue;
		}
		index = scanner->index;
		if (isspace((unsigned char)scanner->sql[index])) {
			scanner->index++;
			if (scanner->code_end == 0U && scanner->code_start == index) {
				scanner->code_start = scanner->index;
			}
			continue;
		}
		if (scanner->code_end == 0U && scanner->sql[index] == ';') {
			scanner->index++;
			scanner->code_start = scanner->index;
			continue;
		}
		skipped = sqlparser_public_skip_quoted_or_comment(
			scanner->dialect,
			scanner->sql,
			index);
		if (skipped != index) {
			if (scanner->code_end == 0U) {
				size_t body_end;
				size_t body_start;

				if (sqlparser_bind_mysql_executable_body(
					    scanner,
					    index,
					    skipped,
					    &body_start,
					    &body_end)) {
					scanner->index = body_start;
					scanner->code_start = body_start;
					scanner->code_end = body_end;
					continue;
				}
				scanner->code_start = SIZE_MAX;
			}
			scanner->index = skipped;
			continue;
		}
		if (sqlparser_bind_question_token(scanner, index, out_token) ||
		    sqlparser_bind_dollar_token(scanner, index, out_token) ||
		    sqlparser_bind_colon_token(scanner, index, out_token) ||
		    sqlparser_bind_at_token(scanner, index, out_token)) {
			scanner->index = out_token->end;
			if (scanner->code_end == 0U) {
				scanner->code_start = SIZE_MAX;
			}
			return 1;
		}
		scanner->index++;
		if (scanner->code_end == 0U) {
			scanner->code_start = SIZE_MAX;
		}
	}
	return 0;
}

int sqlparser_bind_token_exact(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t length,
	size_t start,
	size_t end,
	sqlparser_bind_token_t *out_token)
{
	sqlparser_bind_scanner_t scanner;
	sqlparser_bind_token_t token;
	if (sql == NULL || start >= end) {
		return 0;
	}
	if (end > length || sql[length] != '\0') {
		return 0;
	}
	memset(&scanner, 0, sizeof(scanner));
	scanner.dialect = dialect;
	scanner.sql = sql;
	scanner.length = length;
	scanner.index = start;
	if (!sqlparser_bind_scanner_next(&scanner, &token) ||
	    token.start != start || token.end != end) {
		return 0;
	}
	if (out_token != NULL) {
		*out_token = token;
	}
	return 1;
}
