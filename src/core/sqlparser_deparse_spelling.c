#include <stddef.h>
#include <string.h>

#include "postgres_deparse.h"
#include "sqlparser_internal.h"

typedef struct {
	const char *sql;
	size_t length;
	size_t cursor;
	int locations_match;
	int mysql_lex;
	int oracle_q_quotes;
	int colon_binds;
	int at_binds;
} sqlparser_identifier_resolver_t;

static int sqlparser_ascii_is_space(unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
		c == '\f' || c == '\v';
}

static int sqlparser_ascii_is_identifier_start(unsigned char c)
{
	return (c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		c == '_' ||
		c == '#' ||
		c == '$' ||
		c >= 0x80U;
}

static int sqlparser_ascii_is_identifier_part(unsigned char c)
{
	return sqlparser_ascii_is_identifier_start(c) ||
		(c >= '0' && c <= '9') ||
		c == '$' ||
		c == '#';
}

static unsigned char sqlparser_ascii_lower(unsigned char c)
{
	return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static size_t sqlparser_skip_block_comment(const char *sql, size_t length, size_t position)
{
	size_t depth;

	position += 2U;
	depth = 1U;
	while (position < length && depth > 0U) {
		if (position + 1U < length && sql[position] == '/' && sql[position + 1U] == '*') {
			depth++;
			position += 2U;
		} else if (position + 1U < length && sql[position] == '*' && sql[position + 1U] == '/') {
			depth--;
			position += 2U;
		} else {
			position++;
		}
	}
	return position;
}

static size_t sqlparser_skip_trivia(
	const sqlparser_identifier_resolver_t *resolver,
	size_t position)
{
	for (;;) {
		while (position < resolver->length &&
		       sqlparser_ascii_is_space((unsigned char)resolver->sql[position])) {
			position++;
		}
		if (position + 1U < resolver->length &&
		    resolver->sql[position] == '-' &&
		    resolver->sql[position + 1U] == '-' &&
		    (!resolver->mysql_lex ||
		     position + 2U == resolver->length ||
		     (unsigned char)resolver->sql[position + 2U] <= 0x20U)) {
			position += 2U;
			while (position < resolver->length &&
			       resolver->sql[position] != '\n' &&
			       resolver->sql[position] != '\r') {
				position++;
			}
			continue;
		}
		if (resolver->mysql_lex &&
		    position < resolver->length &&
		    resolver->sql[position] == '#') {
			position++;
			while (position < resolver->length &&
			       resolver->sql[position] != '\n' &&
			       resolver->sql[position] != '\r') {
				position++;
			}
			continue;
		}
		if (position + 1U < resolver->length &&
		    resolver->sql[position] == '/' &&
		    resolver->sql[position + 1U] == '*') {
			position = sqlparser_skip_block_comment(
				resolver->sql,
				resolver->length,
				position);
			continue;
		}
		return position;
	}
}

static size_t sqlparser_skip_quoted_string(
	const char *sql,
	size_t length,
	size_t position,
	int backslash_escapes)
{
	char quote;

	quote = sql[position];
	position++;
	while (position < length) {
		if (sql[position] == quote) {
			if (position + 1U < length && sql[position + 1U] == quote) {
				position += 2U;
				continue;
			}
			return position + 1U;
		}
		if (backslash_escapes && sql[position] == '\\' && position + 1U < length) {
			position += 2U;
		} else {
			position++;
		}
	}
	return length;
}

static size_t sqlparser_skip_oracle_q_quoted(
	const char *sql,
	size_t length,
	size_t position)
{
	size_t q_position;
	size_t cursor;
	char opening;
	char closing;

	q_position = position;
	if (q_position < length &&
	    (sql[q_position] == 'N' || sql[q_position] == 'n')) {
		q_position++;
	}
	if (q_position + 2U >= length ||
	    (sql[q_position] != 'Q' && sql[q_position] != 'q') ||
	    sql[q_position + 1U] != '\'') {
		return position;
	}

	opening = sql[q_position + 2U];
	switch (opening) {
	case '[':
		closing = ']';
		break;
	case '{':
		closing = '}';
		break;
	case '(':
		closing = ')';
		break;
	case '<':
		closing = '>';
		break;
	default:
		closing = opening;
		break;
	}
	cursor = q_position + 3U;
	while (cursor + 1U < length) {
		if (sql[cursor] == closing && sql[cursor + 1U] == '\'') {
			return cursor + 2U;
		}
		cursor++;
	}
	return length;
}

static size_t sqlparser_skip_number(
	const char *sql,
	size_t length,
	size_t position)
{
	size_t cursor;

	cursor = position;
	if (cursor + 2U <= length &&
	    sql[cursor] == '0' &&
	    cursor + 1U < length &&
	    (sql[cursor + 1U] == 'x' || sql[cursor + 1U] == 'X' ||
	     sql[cursor + 1U] == 'b' || sql[cursor + 1U] == 'B')) {
		cursor += 2U;
		while (cursor < length &&
		       ((sql[cursor] >= '0' && sql[cursor] <= '9') ||
		        (sql[cursor] >= 'a' && sql[cursor] <= 'f') ||
		        (sql[cursor] >= 'A' && sql[cursor] <= 'F'))) {
			cursor++;
		}
		return cursor;
	}
	while (cursor < length && sql[cursor] >= '0' && sql[cursor] <= '9') {
		cursor++;
	}
	if (cursor < length && sql[cursor] == '.') {
		cursor++;
		while (cursor < length && sql[cursor] >= '0' && sql[cursor] <= '9') {
			cursor++;
		}
	}
	if (cursor < length && (sql[cursor] == 'e' || sql[cursor] == 'E')) {
		size_t exponent;

		exponent = cursor + 1U;
		if (exponent < length &&
		    (sql[exponent] == '+' || sql[exponent] == '-')) {
			exponent++;
		}
		if (exponent < length &&
		    sql[exponent] >= '0' && sql[exponent] <= '9') {
			cursor = exponent + 1U;
			while (cursor < length &&
			       sql[cursor] >= '0' && sql[cursor] <= '9') {
				cursor++;
			}
		}
	}
	return cursor;
}

static size_t sqlparser_dollar_quote_delimiter_length(
	const char *sql,
	size_t length,
	size_t position)
{
	size_t cursor;

	if (position >= length || sql[position] != '$') {
		return 0U;
	}
	cursor = position + 1U;
	while (cursor < length &&
	       ((sql[cursor] >= 'a' && sql[cursor] <= 'z') ||
	        (sql[cursor] >= 'A' && sql[cursor] <= 'Z') ||
	        (sql[cursor] >= '0' && sql[cursor] <= '9') ||
	        sql[cursor] == '_')) {
		cursor++;
	}
	return cursor < length && sql[cursor] == '$' ? cursor - position + 1U : 0U;
}

static size_t sqlparser_skip_dollar_quoted(
	const char *sql,
	size_t length,
	size_t position,
	size_t delimiter_length)
{
	size_t cursor;

	cursor = position + delimiter_length;
	while (cursor + delimiter_length <= length) {
		if (memcmp(sql + cursor, sql + position, delimiter_length) == 0) {
			return cursor + delimiter_length;
		}
		cursor++;
	}
	return length;
}

static size_t sqlparser_quoted_identifier_end(
	const char *sql,
	size_t length,
	size_t position)
{
	char closing;

	closing = sql[position] == '[' ? ']' : sql[position];
	position++;
	while (position < length) {
		if (sql[position] == closing) {
			if (position + 1U < length && sql[position + 1U] == closing) {
				position += 2U;
				continue;
			}
			return position + 1U;
		}
		position++;
	}
	return length;
}

static size_t sqlparser_identifier_end(
	const char *sql,
	size_t length,
	size_t position)
{
	if (position >= length ||
	    !sqlparser_ascii_is_identifier_start((unsigned char)sql[position])) {
		return position;
	}
	position++;
	while (position < length &&
	       sqlparser_ascii_is_identifier_part((unsigned char)sql[position])) {
		position++;
	}
	return position;
}

static int sqlparser_unquoted_identifier_matches(
	const char *sql,
	size_t start,
	size_t end,
	const char *identifier)
{
	size_t index;
	size_t length;

	for (length = 0U; identifier[length] != '\0'; length++) {
		if (identifier[length] >= 'A' && identifier[length] <= 'Z') {
			return 0;
		}
	}
	if (end - start != length) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (sqlparser_ascii_lower((unsigned char)sql[start + index]) !=
		    (unsigned char)identifier[index]) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_quoted_identifier_matches(
	const char *sql,
	size_t start,
	size_t end,
	const char *identifier)
{
	size_t position;
	size_t identifier_index;
	char closing;

	if (end <= start + 1U ||
	    (sql[start] != '"' && sql[start] != '`' && sql[start] != '[')) {
		return 0;
	}
	closing = sql[start] == '[' ? ']' : sql[start];
	if (sql[end - 1U] != closing) {
		return 0;
	}
	position = start + 1U;
	identifier_index = 0U;
	while (position + 1U < end) {
		if (sql[position] == closing &&
		    position + 1U < end - 1U &&
		    sql[position + 1U] == closing) {
			if (identifier[identifier_index] != closing) {
				return 0;
			}
			identifier_index++;
			position += 2U;
			continue;
		}
		if (identifier[identifier_index] == '\0' ||
		    sql[position] != identifier[identifier_index]) {
			return 0;
		}
		identifier_index++;
		position++;
	}
	return identifier[identifier_index] == '\0';
}

static int sqlparser_source_identifier_at(
	sqlparser_identifier_resolver_t *resolver,
	size_t position,
	const char *identifier,
	const char **resolved,
	size_t *resolved_length,
	size_t *token_end)
{
	size_t end;

	if (position >= resolver->length) {
		return 0;
	}
	if (resolver->sql[position] == '"' ||
	    resolver->sql[position] == '`' ||
	    resolver->sql[position] == '[') {
		end = sqlparser_quoted_identifier_end(resolver->sql, resolver->length, position);
		if (sqlparser_quoted_identifier_matches(
			    resolver->sql,
			    position,
			    end,
			    identifier)) {
			*token_end = end;
			return -1;
		}
		return 0;
	}
	end = sqlparser_identifier_end(resolver->sql, resolver->length, position);
	if (end == position ||
	    !sqlparser_unquoted_identifier_matches(
		    resolver->sql,
		    position,
		    end,
		    identifier)) {
		return 0;
	}

	*resolved = resolver->sql + position;
	*resolved_length = end - position;
	*token_end = end;
	return 1;
}

static int sqlparser_resolve_qualified_identifier(
	sqlparser_identifier_resolver_t *resolver,
	const char *identifier,
	size_t location,
	size_t component_index,
	const char **resolved,
	size_t *resolved_length)
{
	size_t component;
	size_t position;
	size_t token_end;
	int match;

	position = sqlparser_skip_trivia(resolver, location);
	for (component = 0U;; component++) {
		if (component == component_index) {
			match = sqlparser_source_identifier_at(
				resolver,
				position,
				identifier,
				resolved,
				resolved_length,
				&token_end);
			if (match != 0) {
				if (token_end > resolver->cursor) {
					resolver->cursor = token_end;
				}
				return match > 0;
			}
		}

		if (position >= resolver->length) {
			return 0;
		}
		if (resolver->sql[position] == '"' ||
		    resolver->sql[position] == '`' ||
		    resolver->sql[position] == '[') {
			token_end = sqlparser_quoted_identifier_end(
				resolver->sql,
				resolver->length,
				position);
		} else {
			token_end = sqlparser_identifier_end(
				resolver->sql,
				resolver->length,
				position);
		}
		if (token_end == position) {
			return 0;
		}
		position = sqlparser_skip_trivia(resolver, token_end);
		if (position >= resolver->length || resolver->sql[position] != '.') {
			return 0;
		}
		position = sqlparser_skip_trivia(resolver, position + 1U);
	}
}

static int sqlparser_resolve_identifier_forward(
	sqlparser_identifier_resolver_t *resolver,
	const char *identifier,
	size_t location,
	const char **resolved,
	size_t *resolved_length)
{
	size_t delimiter_length;
	size_t position;
	size_t skipped;
	size_t token_end;
	int match;

	position = location > resolver->cursor ? location : resolver->cursor;
	while (position < resolver->length) {
		position = sqlparser_skip_trivia(resolver, position);
		if (position >= resolver->length) {
			break;
		}
		if (resolver->oracle_q_quotes) {
			skipped = sqlparser_skip_oracle_q_quoted(
				resolver->sql,
				resolver->length,
				position);
			if (skipped != position) {
				position = skipped;
				continue;
			}
		}
		if (resolver->colon_binds &&
		    resolver->sql[position] == ':' &&
		    position + 1U < resolver->length) {
			skipped = position + 1U;
			if (sqlparser_ascii_is_identifier_start(
				    (unsigned char)resolver->sql[skipped])) {
				position = sqlparser_identifier_end(
					resolver->sql,
					resolver->length,
					skipped);
				continue;
			}
			if (resolver->sql[skipped] >= '0' &&
			    resolver->sql[skipped] <= '9') {
				do {
					skipped++;
				} while (skipped < resolver->length &&
					 resolver->sql[skipped] >= '0' &&
					 resolver->sql[skipped] <= '9');
				position = skipped;
				continue;
			}
			if (resolver->sql[skipped] == '"') {
				position = sqlparser_quoted_identifier_end(
					resolver->sql,
					resolver->length,
					skipped);
				continue;
			}
		}
		if (resolver->at_binds &&
		    resolver->sql[position] == '@' &&
		    position + 1U < resolver->length) {
			skipped = position + 1U;
			if (resolver->sql[skipped] == '@') {
				skipped++;
			}
			if (skipped < resolver->length &&
			    (resolver->sql[skipped] == '\'' ||
			     resolver->sql[skipped] == '"' ||
			     resolver->sql[skipped] == '`')) {
				if (resolver->sql[skipped] == '\'' ||
				    (resolver->mysql_lex &&
				     resolver->sql[skipped] == '"')) {
					position = sqlparser_skip_quoted_string(
						resolver->sql,
						resolver->length,
						skipped,
						resolver->mysql_lex);
				} else {
					position = sqlparser_quoted_identifier_end(
						resolver->sql,
						resolver->length,
						skipped);
				}
				continue;
			}
			if (skipped < resolver->length &&
			    (sqlparser_ascii_is_identifier_part(
				     (unsigned char)resolver->sql[skipped]) ||
			     (resolver->mysql_lex && resolver->sql[skipped] == '.'))) {
				position = skipped + 1U;
				while (position < resolver->length &&
				       (sqlparser_ascii_is_identifier_part(
					        (unsigned char)resolver->sql[position]) ||
				        (resolver->mysql_lex &&
				         resolver->sql[position] == '.'))) {
					position++;
				}
				continue;
			}
		}
		if (resolver->sql[position] == '\'' ||
		    (resolver->mysql_lex && resolver->sql[position] == '"')) {
			position = sqlparser_skip_quoted_string(
				resolver->sql,
				resolver->length,
				position,
				resolver->mysql_lex);
			continue;
		}
		if ((resolver->sql[position] == 'U' || resolver->sql[position] == 'u') &&
		    position + 2U < resolver->length &&
		    resolver->sql[position + 1U] == '&' &&
		    (resolver->sql[position + 2U] == '\'' ||
		     resolver->sql[position + 2U] == '"')) {
			if (resolver->sql[position + 2U] == '\'') {
				position = sqlparser_skip_quoted_string(
					resolver->sql,
					resolver->length,
					position + 2U,
					0);
			} else {
				position = sqlparser_quoted_identifier_end(
					resolver->sql,
					resolver->length,
					position + 2U);
			}
			continue;
		}
		if ((resolver->sql[position] == 'E' || resolver->sql[position] == 'e') &&
		    position + 1U < resolver->length &&
		    resolver->sql[position + 1U] == '\'') {
			position = sqlparser_skip_quoted_string(
				resolver->sql,
				resolver->length,
				position + 1U,
				1);
			continue;
		}
		if ((resolver->sql[position] == 'N' || resolver->sql[position] == 'n' ||
		     resolver->sql[position] == 'B' || resolver->sql[position] == 'b' ||
		     resolver->sql[position] == 'X' || resolver->sql[position] == 'x') &&
		    position + 1U < resolver->length &&
		    (resolver->sql[position + 1U] == '\'' ||
		     (resolver->mysql_lex && resolver->sql[position + 1U] == '"'))) {
			position = sqlparser_skip_quoted_string(
				resolver->sql,
				resolver->length,
				position + 1U,
				resolver->mysql_lex);
			continue;
		}
		if (resolver->mysql_lex &&
		    resolver->sql[position] == '_') {
			skipped = sqlparser_identifier_end(
				resolver->sql,
				resolver->length,
				position);
			if (skipped < resolver->length &&
			    (resolver->sql[skipped] == '\'' ||
			     resolver->sql[skipped] == '"')) {
				position = sqlparser_skip_quoted_string(
					resolver->sql,
					resolver->length,
					skipped,
					1);
				continue;
			}
		}
		if (resolver->sql[position] >= '0' &&
		    resolver->sql[position] <= '9') {
			position = sqlparser_skip_number(
				resolver->sql,
				resolver->length,
				position);
			continue;
		}
		delimiter_length = sqlparser_dollar_quote_delimiter_length(
			resolver->sql,
			resolver->length,
			position);
		if (delimiter_length > 0U) {
			position = sqlparser_skip_dollar_quoted(
				resolver->sql,
				resolver->length,
				position,
				delimiter_length);
			continue;
		}
		match = sqlparser_source_identifier_at(
			resolver,
			position,
			identifier,
			resolved,
			resolved_length,
			&token_end);
		if (match > 0) {
			resolver->cursor = token_end;
			return 1;
		}
		if (match < 0) {
			resolver->cursor = token_end;
			return -1;
		}
		if (resolver->sql[position] == '"' ||
		    resolver->sql[position] == '`' ||
		    resolver->sql[position] == '[') {
			position = sqlparser_quoted_identifier_end(
				resolver->sql,
				resolver->length,
				position);
		} else {
			token_end = sqlparser_identifier_end(
				resolver->sql,
				resolver->length,
				position);
			position = token_end > position ? token_end : position + 1U;
		}
	}
	return 0;
}

static bool sqlparser_resolve_identifier(
	void *context,
	const char *identifier,
	int location,
	size_t component_index,
	bool search_forward,
	const char **resolved,
	size_t *resolved_length)
{
	sqlparser_identifier_resolver_t *resolver;
	sqlparser_identifier_resolver_t prefix;
	size_t start;
	size_t cursor;

	if (context == NULL || identifier == NULL || resolved == NULL ||
	    resolved_length == NULL) {
		return false;
	}
	*resolved = NULL;
	*resolved_length = 0U;
	resolver = (sqlparser_identifier_resolver_t *)context;
	if (location == SQLPARSER_PROTO_LOCATION_GENERATED) {
		return false;
	}
	if (!resolver->locations_match) {
		int match;

		cursor = resolver->cursor;
		match = sqlparser_resolve_identifier_forward(
			resolver,
			identifier,
			cursor,
			resolved,
			resolved_length);
		if (match > 0) {
			return true;
		}
		if (match < 0 || cursor == 0U) {
			return false;
		}
		prefix = *resolver;
		prefix.length = cursor;
		prefix.cursor = 0U;
		match = sqlparser_resolve_identifier_forward(
			&prefix,
			identifier,
			0U,
			resolved,
			resolved_length);
		if (match <= 0) {
			return false;
		}
		resolver->cursor = prefix.cursor;
		return true;
	}
	if (search_forward) {
		start = location >= 0 ? (size_t)location : resolver->cursor;
		if (start > resolver->length) {
			return false;
		}
		return sqlparser_resolve_identifier_forward(
			resolver,
			identifier,
			start,
			resolved,
			resolved_length) > 0;
	}
	if (location < 0 || (size_t)location >= resolver->length) {
		return false;
	}
	return sqlparser_resolve_qualified_identifier(
		resolver,
		identifier,
		(size_t)location,
		component_index,
		resolved,
		resolved_length) != 0;
}

PgQueryDeparseResult sqlparser_deparse_protobuf_for_handle(
	const sqlparser_handle_t *handle,
	PgQueryProtobuf parse_tree)
{
	PostgresDeparseOpts options;
	sqlparser_identifier_resolver_t resolver;

	memset(&options, 0, sizeof(options));
	if (handle == NULL || handle->parser_sql == NULL) {
		return pg_query_deparse_protobuf_opts(parse_tree, options);
	}

	resolver.locations_match =
		handle->sql_len == handle->parser_sql_len &&
		memcmp(handle->sql, handle->parser_sql, handle->sql_len) == 0;
	resolver.sql = handle->sql;
	resolver.length = handle->sql_len;
	resolver.cursor = 0U;
	resolver.mysql_lex =
		handle->dialect == SQLPARSER_DIALECT_MYSQL ||
		handle->dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL;
	resolver.oracle_q_quotes =
		handle->dialect == SQLPARSER_DIALECT_ORACLE ||
		handle->dialect == SQLPARSER_DIALECT_DAMENG ||
		handle->dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE;
	resolver.colon_binds = resolver.oracle_q_quotes;
	resolver.at_binds =
		resolver.mysql_lex ||
		handle->dialect == SQLPARSER_DIALECT_SQLSERVER ||
		handle->dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
	options.identifier_resolver = sqlparser_resolve_identifier;
	options.identifier_resolver_context = &resolver;
	return pg_query_deparse_protobuf_opts(parse_tree, options);
}
