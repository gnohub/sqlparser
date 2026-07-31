#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_deparse.h"
#include "sqlparser_identifier_origin_internal.h"
#include "sqlparser_internal.h"
#include "sqlparser_ast_internal.h"
#include "../dialect/sqlparser_dialect_internal.h"

typedef struct {
	const sqlparser_identifier_mutation_t *mutation;
	const char *identifier;
	const char *resolved;
	size_t resolved_length;
	int tagged;
	int registered;
	int consumed;
	int preserve_original;
} sqlparser_generated_identifier_t;

typedef struct sqlparser_identifier_resolver {
	const sqlparser_handle_t *handle;
	const char *sql;
	size_t length;
	size_t cursor;
	int locations_match;
	int mysql_lex;
	int oracle_q_quotes;
	int colon_binds;
	int at_binds;
	int top_keyword;
	int keyword_match;
	int audit_identifiers;
	int audit_failed;
	unsigned char *consumed;
	const sqlparser_identifier_origin_map_t *origins;
	const struct sqlparser_identifier_resolver *origin_source;
	sqlparser_generated_identifier_t *generated_identifiers;
	size_t generated_identifier_count;
	int generated_identifier_error;
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
	return c >= 'A' && c <= 'Z' ?
		(unsigned char)(c + ('a' - 'A')) :
		c;
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

	length = strlen(identifier);
	if (end - start != length) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (sql[start + index] != identifier[index]) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_unquoted_identifier_matches_ascii_ci(
	const char *sql,
	size_t start,
	size_t end,
	const char *identifier)
{
	size_t index;
	size_t length;
	size_t source_length;

	length = strlen(identifier);
	source_length = end - start;
	if (source_length != length &&
	    !(length == 63U && source_length > length)) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (sqlparser_ascii_lower((unsigned char)sql[start + index]) !=
		    sqlparser_ascii_lower((unsigned char)identifier[index])) {
			return 0;
		}
	}
	return 1;
}

static int sqlparser_source_word_is_syntax_keyword(
	const sqlparser_identifier_resolver_t *resolver,
	size_t start,
	size_t end)
{
	return postgres_deparse_keyword_category(
			resolver->sql + start,
			end - start) >= 0 ||
		(resolver->top_keyword &&
		 sqlparser_unquoted_identifier_matches_ascii_ci(
			 resolver->sql,
			 start,
			 end,
			 "top"));
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

static int sqlparser_hex_value(unsigned char c)
{
	if (c >= '0' && c <= '9') {
		return (int)(c - '0');
	}
	if (c >= 'a' && c <= 'f') {
		return (int)(c - 'a') + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return (int)(c - 'A') + 10;
	}
	return -1;
}

static int sqlparser_uamp_hex_codepoint(
	const char *sql,
	size_t end,
	size_t *position,
	size_t digit_count,
	unsigned long *codepoint)
{
	unsigned long value;
	size_t index;

	if (*position > end || digit_count > end - *position) {
		return 0;
	}
	value = 0UL;
	for (index = 0U; index < digit_count; index++) {
		int digit;

		digit = sqlparser_hex_value(
			(unsigned char)sql[*position + index]);
		if (digit < 0) {
			return 0;
		}
		value = (value << 4) | (unsigned long)digit;
	}
	*position += digit_count;
	*codepoint = value;
	return 1;
}

static int sqlparser_identifier_match_byte(
	const char *identifier,
	size_t *identifier_index,
	unsigned char byte,
	int ascii_ci)
{
	unsigned char expected;

	expected = (unsigned char)identifier[*identifier_index];
	if (expected == '\0') {
		return 0;
	}
	if (ascii_ci) {
		expected = sqlparser_ascii_lower(expected);
		byte = sqlparser_ascii_lower(byte);
	}
	if (expected != byte) {
		return 0;
	}
	(*identifier_index)++;
	return 1;
}

static int sqlparser_identifier_match_codepoint(
	const char *identifier,
	size_t *identifier_index,
	unsigned long codepoint,
	int ascii_ci)
{
	unsigned char encoded[4];
	size_t count;
	size_t index;

	if (codepoint <= 0x7FUL) {
		encoded[0] = (unsigned char)codepoint;
		count = 1U;
	} else if (codepoint <= 0x7FFUL) {
		encoded[0] = (unsigned char)(0xC0U | (codepoint >> 6));
		encoded[1] = (unsigned char)(0x80U | (codepoint & 0x3FU));
		count = 2U;
	} else if (codepoint <= 0xFFFFUL) {
		encoded[0] = (unsigned char)(0xE0U | (codepoint >> 12));
		encoded[1] = (unsigned char)(0x80U | ((codepoint >> 6) & 0x3FU));
		encoded[2] = (unsigned char)(0x80U | (codepoint & 0x3FU));
		count = 3U;
	} else if (codepoint <= 0x10FFFFUL) {
		encoded[0] = (unsigned char)(0xF0U | (codepoint >> 18));
		encoded[1] = (unsigned char)(0x80U | ((codepoint >> 12) & 0x3FU));
		encoded[2] = (unsigned char)(0x80U | ((codepoint >> 6) & 0x3FU));
		encoded[3] = (unsigned char)(0x80U | (codepoint & 0x3FU));
		count = 4U;
	} else {
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (!sqlparser_identifier_match_byte(
			    identifier,
			    identifier_index,
			    encoded[index],
			    ascii_ci)) {
			return 0;
		}
	}
	return 1;
}

static size_t sqlparser_uamp_identifier_span_end(
	const sqlparser_identifier_resolver_t *resolver,
	size_t quote_end,
	unsigned char *escape)
{
	size_t keyword_end;
	size_t position;

	*escape = '\\';
	position = sqlparser_skip_trivia(resolver, quote_end);
	keyword_end = sqlparser_identifier_end(
		resolver->sql,
		resolver->length,
		position);
	if (keyword_end - position != 7U ||
	    !sqlparser_unquoted_identifier_matches_ascii_ci(
		    resolver->sql,
		    position,
		    keyword_end,
		    "uescape")) {
		return quote_end;
	}
	position = sqlparser_skip_trivia(resolver, keyword_end);
	if (position + 2U >= resolver->length ||
	    resolver->sql[position] != '\'' ||
	    resolver->sql[position + 2U] != '\'') {
		return quote_end;
	}
	*escape = (unsigned char)resolver->sql[position + 1U];
	return position + 3U;
}

static int sqlparser_uamp_identifier_matches(
	const sqlparser_identifier_resolver_t *resolver,
	size_t start,
	size_t quote_end,
	const char *identifier,
	int ascii_ci,
	size_t *span_end)
{
	unsigned char escape;
	size_t identifier_index;
	size_t position;

	*span_end = sqlparser_uamp_identifier_span_end(
		resolver,
		quote_end,
		&escape);
	identifier_index = 0U;
	position = start + 3U;
	while (position + 1U < quote_end) {
		unsigned char byte;

		byte = (unsigned char)resolver->sql[position++];
		if (byte == '"') {
			if (position + 1U >= quote_end ||
			    resolver->sql[position] != '"') {
				return 0;
			}
			position++;
			if (!sqlparser_identifier_match_byte(
				    identifier,
				    &identifier_index,
				    '"',
				    ascii_ci)) {
				return 0;
			}
			continue;
		}
		if (byte != escape) {
			if (!sqlparser_identifier_match_byte(
				    identifier,
				    &identifier_index,
				    byte,
				    ascii_ci)) {
				return 0;
			}
			continue;
		}
		if (position >= quote_end - 1U) {
			return 0;
		}
		if ((unsigned char)resolver->sql[position] == escape) {
			position++;
			if (!sqlparser_identifier_match_byte(
				    identifier,
				    &identifier_index,
				    escape,
				    ascii_ci)) {
				return 0;
			}
			continue;
		}
		{
			unsigned long codepoint;
			size_t digit_count;

			digit_count = 4U;
			if (resolver->sql[position] == '+') {
				position++;
				digit_count = 6U;
			}
			if (!sqlparser_uamp_hex_codepoint(
				    resolver->sql,
				    quote_end - 1U,
				    &position,
				    digit_count,
				    &codepoint)) {
				return 0;
			}
			if (codepoint >= 0xD800UL && codepoint <= 0xDBFFUL) {
				unsigned long low;

				if (position >= quote_end - 1U ||
				    (unsigned char)resolver->sql[position++] != escape ||
				    !sqlparser_uamp_hex_codepoint(
					    resolver->sql,
					    quote_end - 1U,
					    &position,
					    4U,
					    &low) ||
				    low < 0xDC00UL || low > 0xDFFFUL) {
					return 0;
				}
				codepoint =
					0x10000UL +
					((codepoint - 0xD800UL) << 10) +
					(low - 0xDC00UL);
			}
			if (!sqlparser_identifier_match_codepoint(
				    identifier,
				    &identifier_index,
				    codepoint,
				    ascii_ci)) {
				return 0;
			}
		}
	}
	return identifier[identifier_index] == '\0';
}

static int sqlparser_resolve_identifier_origin(
	sqlparser_identifier_resolver_t *resolver,
	size_t parser_start,
	size_t parser_end,
	const char *identifier,
	const char **resolved,
	size_t *resolved_length);

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
	if ((resolver->sql[position] == 'U' ||
	     resolver->sql[position] == 'u') &&
	    position + 2U < resolver->length &&
	    resolver->sql[position + 1U] == '&' &&
	    resolver->sql[position + 2U] == '"') {
		size_t quote_end;
		size_t span_end;

		if (resolver->keyword_match) {
			return 0;
		}
		quote_end = sqlparser_quoted_identifier_end(
			resolver->sql,
			resolver->length,
			position + 2U);
		if (sqlparser_uamp_identifier_matches(
			    resolver,
			    position,
			    quote_end,
			    identifier,
			    0,
			    &span_end)) {
			*resolved = resolver->sql + position;
			*resolved_length = span_end - position;
			*token_end = span_end;
			return sqlparser_resolve_identifier_origin(
				resolver,
				position,
				span_end,
				identifier,
				resolved,
				resolved_length);
		}
		return 0;
	}
	if (resolver->sql[position] == '"' ||
	    resolver->sql[position] == '`' ||
	    resolver->sql[position] == '[') {
		if (resolver->keyword_match) {
			return 0;
		}
		end = sqlparser_quoted_identifier_end(resolver->sql, resolver->length, position);
		if (sqlparser_quoted_identifier_matches(
			    resolver->sql,
			    position,
			    end,
			    identifier)) {
			*resolved = resolver->sql + position;
			*resolved_length = end - position;
			*token_end = end;
			return sqlparser_resolve_identifier_origin(
				resolver,
				position,
				end,
				identifier,
				resolved,
				resolved_length);
		}
		return 0;
	}
	end = sqlparser_identifier_end(resolver->sql, resolver->length, position);
	if (end == position) {
		return 0;
	}
	if (resolver->keyword_match ?
	    !sqlparser_unquoted_identifier_matches_ascii_ci(
		    resolver->sql,
		    position,
		    end,
		    identifier) :
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
	return sqlparser_resolve_identifier_origin(
		resolver,
		position,
		end,
		identifier,
		resolved,
		resolved_length);
}

static int sqlparser_resolve_identifier_origin(
	sqlparser_identifier_resolver_t *resolver,
	size_t parser_start,
	size_t parser_end,
	const char *identifier,
	const char **resolved,
	size_t *resolved_length)
{
	sqlparser_identifier_origin_t origin;
	sqlparser_identifier_origin_kind_t kind;
	sqlparser_identifier_resolver_t source;
	const char *source_resolved;
	size_t source_end;
	size_t source_resolved_length;

	if (resolver->origins == NULL || resolver->keyword_match) {
		return 1;
	}
	kind = sqlparser_identifier_origin_map_lookup(
		resolver->origins,
		parser_start,
		parser_end - parser_start,
		&origin);
	if (kind == SQLPARSER_IDENTIFIER_ORIGIN_GENERATED) {
		return 1;
	}
	if (kind != SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
	    resolver->origin_source == NULL ||
	    origin.source_offset > resolver->origin_source->length ||
	    origin.source_length >
		    resolver->origin_source->length - origin.source_offset) {
		resolver->audit_failed = 1;
		return 0;
	}
	source = *resolver->origin_source;
	source.cursor = 0U;
	source.keyword_match = 0;
	source.audit_identifiers = 0;
	source.audit_failed = 0;
	source.consumed = NULL;
	source.origins = NULL;
	source.origin_source = NULL;
	source_resolved = NULL;
	source_resolved_length = 0U;
	source_end = origin.source_offset;
	if (sqlparser_source_identifier_at(
		    &source,
		    origin.source_offset,
		    identifier,
		    &source_resolved,
		    &source_resolved_length,
		    &source_end) <= 0 ||
	    source_end < origin.source_offset ||
	    source_end - origin.source_offset != origin.source_length) {
		resolver->audit_failed = 1;
		return 0;
	}
	*resolved = source_resolved;
	*resolved_length = source_resolved_length;
	return 1;
}

static int sqlparser_consume_identifier(
	sqlparser_identifier_resolver_t *resolver,
	size_t start,
	size_t end)
{
	if (resolver->audit_identifiers &&
	    resolver->consumed != NULL &&
	    resolver->consumed[start] != 0U) {
		return 0;
	}
	if (resolver->audit_identifiers && resolver->consumed != NULL) {
		resolver->consumed[start] = 1U;
	}
	if (end > resolver->cursor) {
		resolver->cursor = end;
	}
	return 1;
}

static int sqlparser_source_is_identifier(
	const sqlparser_identifier_resolver_t *resolver,
	size_t position)
{
	if (position >= resolver->length) {
		return 0;
	}
	if (resolver->sql[position] == '$' &&
	    position + 1U < resolver->length &&
	    resolver->sql[position + 1U] >= '0' &&
	    resolver->sql[position + 1U] <= '9') {
		return 0;
	}
	if ((resolver->sql[position] == 'U' ||
	     resolver->sql[position] == 'u') &&
	    position + 2U < resolver->length &&
	    resolver->sql[position + 1U] == '&' &&
	    resolver->sql[position + 2U] == '"') {
		return 1;
	}
	return resolver->sql[position] == '"' ||
		resolver->sql[position] == '`' ||
		resolver->sql[position] == '[' ||
		sqlparser_ascii_is_identifier_start(
			(unsigned char)resolver->sql[position]);
}

static size_t sqlparser_source_identifier_end(
	const sqlparser_identifier_resolver_t *resolver,
	size_t position)
{
	size_t token_end;

	if ((resolver->sql[position] == 'U' ||
	     resolver->sql[position] == 'u') &&
	    position + 2U < resolver->length &&
	    resolver->sql[position + 1U] == '&' &&
	    resolver->sql[position + 2U] == '"') {
		unsigned char escape;

		token_end = sqlparser_quoted_identifier_end(
			resolver->sql,
			resolver->length,
			position + 2U);
		return sqlparser_uamp_identifier_span_end(
			resolver,
			token_end,
			&escape);
	}
	if (resolver->sql[position] == '"' ||
	    resolver->sql[position] == '`' ||
	    resolver->sql[position] == '[') {
		return sqlparser_quoted_identifier_end(
			resolver->sql,
			resolver->length,
			position);
	}
	return sqlparser_identifier_end(
		resolver->sql,
		resolver->length,
		position);
}

static size_t sqlparser_source_string_end(
	const sqlparser_identifier_resolver_t *resolver,
	size_t position)
{
	size_t delimiter_length;
	size_t end;

	if (position >= resolver->length) {
		return position;
	}
	if (resolver->sql[position] == '\'') {
		return sqlparser_skip_quoted_string(
			resolver->sql,
			resolver->length,
			position,
			resolver->mysql_lex);
	}
	if (resolver->mysql_lex && resolver->sql[position] == '"') {
		return sqlparser_skip_quoted_string(
			resolver->sql,
			resolver->length,
			position,
			1);
	}
	if (resolver->oracle_q_quotes) {
		end = sqlparser_skip_oracle_q_quoted(
			resolver->sql,
			resolver->length,
			position);
		if (end != position) {
			return end;
		}
	}
	if ((resolver->sql[position] == 'U' ||
	     resolver->sql[position] == 'u') &&
	    position + 2U < resolver->length &&
	    resolver->sql[position + 1U] == '&' &&
	    resolver->sql[position + 2U] == '\'') {
		return sqlparser_skip_quoted_string(
			resolver->sql,
			resolver->length,
			position + 2U,
			0);
	}
	if ((resolver->sql[position] == 'E' ||
	     resolver->sql[position] == 'e') &&
	    position + 1U < resolver->length &&
	    resolver->sql[position + 1U] == '\'') {
		return sqlparser_skip_quoted_string(
			resolver->sql,
			resolver->length,
			position + 1U,
			1);
	}
	if ((resolver->sql[position] == 'N' ||
	     resolver->sql[position] == 'n' ||
	     resolver->sql[position] == 'B' ||
	     resolver->sql[position] == 'b' ||
	     resolver->sql[position] == 'X' ||
	     resolver->sql[position] == 'x') &&
	    position + 1U < resolver->length &&
	    resolver->sql[position + 1U] == '\'') {
		return sqlparser_skip_quoted_string(
			resolver->sql,
			resolver->length,
			position + 1U,
			resolver->mysql_lex);
	}
	if (resolver->mysql_lex && resolver->sql[position] == '_') {
		end = sqlparser_identifier_end(
			resolver->sql,
			resolver->length,
			position);
		if (end < resolver->length &&
		    (resolver->sql[end] == '\'' ||
		     resolver->sql[end] == '"')) {
			return sqlparser_skip_quoted_string(
				resolver->sql,
				resolver->length,
				end,
				1);
		}
	}
	delimiter_length = sqlparser_dollar_quote_delimiter_length(
		resolver->sql,
		resolver->length,
		position);
	return delimiter_length > 0U ?
		sqlparser_skip_dollar_quoted(
			resolver->sql,
			resolver->length,
			position,
			delimiter_length) :
		position;
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
				return match > 0 &&
					sqlparser_consume_identifier(
						resolver,
						position,
						token_end);
			}
		}

		if (position >= resolver->length) {
			return 0;
		}
		if ((resolver->sql[position] == 'U' ||
		     resolver->sql[position] == 'u') &&
		    position + 2U < resolver->length &&
		    resolver->sql[position + 1U] == '&' &&
		    resolver->sql[position + 2U] == '"') {
			unsigned char escape;

			token_end = sqlparser_quoted_identifier_end(
				resolver->sql,
				resolver->length,
				position + 2U);
			token_end = sqlparser_uamp_identifier_span_end(
				resolver,
				token_end,
				&escape);
		} else if (resolver->sql[position] == '"' ||
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
		if (resolver->keyword_match &&
		    (resolver->sql[position] == ',' ||
		     resolver->sql[position] == ';' ||
		     resolver->sql[position] == '(' ||
		     resolver->sql[position] == ')' ||
		     resolver->sql[position] == '=')) {
			return 0;
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
		    resolver->sql[position + 2U] == '\'') {
			position = sqlparser_skip_quoted_string(
				resolver->sql,
				resolver->length,
				position + 2U,
				0);
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
			if (resolver->audit_identifiers &&
			    resolver->sql[position] != '"' &&
			    resolver->sql[position] != '`' &&
			    resolver->sql[position] != '[' &&
			    sqlparser_source_word_is_syntax_keyword(
				    resolver,
				    position,
				    token_end)) {
				sqlparser_identifier_resolver_t probe;
				const char *next_resolved;
				size_t next_length;

				probe = *resolver;
				probe.cursor = token_end;
				probe.consumed = NULL;
				next_resolved = NULL;
				next_length = 0U;
				if (sqlparser_resolve_identifier_forward(
					    &probe,
					    identifier,
					    token_end,
					    &next_resolved,
					    &next_length) > 0) {
					position = token_end;
					continue;
				}
			}
			return sqlparser_consume_identifier(
				resolver,
				position,
				token_end);
		}
		if (sqlparser_source_is_identifier(resolver, position)) {
			int syntax_keyword;

			token_end = sqlparser_source_identifier_end(
				resolver,
				position);
			syntax_keyword =
				resolver->sql[position] != '"' &&
				resolver->sql[position] != '`' &&
				resolver->sql[position] != '[' &&
				token_end > position &&
				sqlparser_source_word_is_syntax_keyword(
					resolver,
					position,
					token_end);
			if (resolver->audit_identifiers &&
			    !syntax_keyword) {
				return 0;
			}
			if (token_end > position) {
				position = token_end;
				continue;
			}
			if (resolver->audit_identifiers) {
				return 0;
			}
		}
		position++;
	}
	return 0;
}

static int sqlparser_resolve_window_name(
	sqlparser_identifier_resolver_t *resolver,
	const char *identifier,
	size_t location,
	const char **resolved,
	size_t *resolved_length)
{
	size_t position;

	if (!resolver->locations_match) {
		return sqlparser_resolve_identifier_forward(
			resolver,
			identifier,
			resolver->cursor,
			resolved,
			resolved_length);
	}
	if (location >= resolver->length) {
		return 0;
	}
	position = resolver->cursor;
	while (position < location) {
		size_t keyword_end;
		size_t token_end;

		position = sqlparser_skip_trivia(resolver, position);
		if (position >= location) {
			break;
		}
		if (sqlparser_source_identifier_at(
			    resolver,
			    position,
			    identifier,
			    resolved,
			    resolved_length,
			    &token_end) > 0) {
			size_t after;

			after = sqlparser_skip_trivia(resolver, token_end);
			keyword_end = sqlparser_identifier_end(
				resolver->sql,
				resolver->length,
				after);
			if (keyword_end > after &&
			    sqlparser_unquoted_identifier_matches_ascii_ci(
				    resolver->sql,
				    after,
				    keyword_end,
				    "as") &&
			    sqlparser_skip_trivia(resolver, keyword_end) == location) {
				return sqlparser_consume_identifier(
					resolver,
					position,
					token_end);
			}
		}
		if ((resolver->sql[position] == 'U' ||
		     resolver->sql[position] == 'u') &&
		    position + 2U < resolver->length &&
		    resolver->sql[position + 1U] == '&' &&
		    resolver->sql[position + 2U] == '"') {
			unsigned char escape;

			token_end = sqlparser_quoted_identifier_end(
				resolver->sql,
				resolver->length,
				position + 2U);
			position = sqlparser_uamp_identifier_span_end(
				resolver,
				token_end,
				&escape);
		} else if (resolver->sql[position] == '"' ||
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

static int sqlparser_resolve_index_access_method(
	sqlparser_identifier_resolver_t *resolver,
	const char *identifier,
	const char **resolved,
	size_t *resolved_length,
	int *explicit_using)
{
	size_t keyword_end;
	size_t position;
	size_t token_end;

	*explicit_using = 0;
	position = sqlparser_skip_trivia(resolver, resolver->cursor);
	keyword_end = sqlparser_identifier_end(
		resolver->sql,
		resolver->length,
		position);
	if (keyword_end == position ||
	    !sqlparser_unquoted_identifier_matches_ascii_ci(
		    resolver->sql,
		    position,
		    keyword_end,
		    "using")) {
		return 0;
	}
	*explicit_using = 1;
	position = sqlparser_skip_trivia(resolver, keyword_end);
	if (sqlparser_source_identifier_at(
		    resolver,
		    position,
		    identifier,
		    resolved,
		    resolved_length,
		    &token_end) <= 0) {
		return 0;
	}
	return sqlparser_consume_identifier(
		resolver,
		position,
		token_end);
}

static sqlparser_generated_identifier_t *
sqlparser_find_generated_identifier(
	sqlparser_identifier_resolver_t *resolver,
	const char *identifier)
{
	size_t index;

	if (resolver == NULL || identifier == NULL) {
		return NULL;
	}
	for (index = 0U;
	     index < resolver->generated_identifier_count;
	     index++) {
		if (resolver->generated_identifiers[index].identifier ==
		    identifier) {
			return &resolver->generated_identifiers[index];
		}
	}
	return NULL;
}

static void sqlparser_read_generated_identifier(
	void *context,
	size_t identifier_index,
	const char *identifier)
{
	sqlparser_identifier_resolver_t *resolver;

	resolver = (sqlparser_identifier_resolver_t *)context;
	if (resolver == NULL ||
	    identifier_index >= resolver->generated_identifier_count ||
	    identifier == NULL ||
	    !resolver->generated_identifiers[identifier_index].tagged ||
	    resolver->generated_identifiers[identifier_index].registered) {
		if (resolver != NULL) {
			resolver->generated_identifier_error = 1;
		}
		return;
	}
	resolver->generated_identifiers[identifier_index].identifier =
		identifier;
	resolver->generated_identifiers[identifier_index].registered = 1;
}

static bool sqlparser_generated_identifier_probe(
	void *context,
	const char *identifier)
{
	return sqlparser_find_generated_identifier(
		       (sqlparser_identifier_resolver_t *)context,
		       identifier) != NULL;
}

static void sqlparser_consume_generated_identifier(
	sqlparser_identifier_resolver_t *resolver,
	sqlparser_generated_identifier_t *generated,
	int location,
	size_t component_index,
	bool search_forward)
{
	sqlparser_identifier_resolver_t probe;
	const char *resolved;
	size_t resolved_length;
	int explicit_using;
	int matched;

	if (generated == NULL || generated->consumed) {
		return;
	}
	if (generated->mutation == NULL ||
	    generated->mutation->original == NULL) {
		resolver->generated_identifier_error = 1;
		return;
	}
	if (!generated->mutation->source_present) {
		generated->consumed = 1;
		return;
	}
	if (generated->mutation->has_source_component) {
		component_index =
			generated->mutation->source_component_index;
	}
	probe = *resolver;
	probe.audit_identifiers = 1;
	probe.audit_failed = 0;
	probe.keyword_match = 0;
	probe.consumed = NULL;
	probe.generated_identifiers = NULL;
	probe.generated_identifier_count = 0U;
	probe.generated_identifier_error = 0;
	resolved = NULL;
	resolved_length = 0U;
	matched = 0;
	if (!probe.locations_match) {
		matched = sqlparser_resolve_identifier_forward(
			&probe,
			generated->mutation->original,
			probe.cursor,
			&resolved,
			&resolved_length) > 0;
	} else if (component_index ==
		   POSTGRES_DEPARSE_WINDOW_NAME_COMPONENT) {
		matched =
			location >= 0 &&
			sqlparser_resolve_window_name(
				&probe,
				generated->mutation->original,
				(size_t)location,
				&resolved,
				&resolved_length) > 0;
	} else if (component_index ==
		   POSTGRES_DEPARSE_INDEX_ACCESS_METHOD_COMPONENT) {
		matched = sqlparser_resolve_index_access_method(
			&probe,
			generated->mutation->original,
			&resolved,
			&resolved_length,
			&explicit_using) > 0;
	} else if (search_forward || location < 0) {
		size_t start;

		start =
			location >= 0 ? (size_t)location : probe.cursor;
		if (start <= probe.length) {
			matched = sqlparser_resolve_identifier_forward(
				&probe,
				generated->mutation->original,
				start,
				&resolved,
				&resolved_length) > 0;
		}
	} else if ((size_t)location < probe.length) {
		matched = sqlparser_resolve_qualified_identifier(
			&probe,
			generated->mutation->original,
			(size_t)location,
			component_index,
			&resolved,
			&resolved_length) > 0;
	}
	if (!matched) {
		resolver->generated_identifier_error = 1;
		return;
	}
	generated->resolved = resolved;
	generated->resolved_length = resolved_length;
	generated->preserve_original =
		generated->identifier != NULL &&
		strcmp(
			generated->identifier,
			generated->mutation->original) == 0;
	generated->consumed = 1;
	if (probe.cursor > resolver->cursor) {
		resolver->cursor = probe.cursor;
	}
}

static bool sqlparser_resolve_sql_value_function_spelling(
	sqlparser_identifier_resolver_t *resolver,
	const char *keyword,
	int location,
	const char **resolved,
	size_t *resolved_length)
{
	sqlparser_identifier_origin_t origin;
	size_t end;
	size_t start;

	if (resolver == NULL || keyword == NULL || location < 0) {
		return false;
	}
	start = resolver->locations_match ?
		(size_t)location :
		resolver->cursor;
	start = sqlparser_skip_trivia(resolver, start);
	if (start >= resolver->length) {
		return false;
	}
	end = sqlparser_identifier_end(
		resolver->sql,
		resolver->length,
		start);
	if (end == start ||
	    !sqlparser_unquoted_identifier_matches_ascii_ci(
		    resolver->sql,
		    start,
		    end,
		    keyword)) {
		return false;
	}
	*resolved = resolver->sql + start;
	*resolved_length = end - start;
	if (resolver->origins != NULL &&
	    resolver->origin_source != NULL &&
	    sqlparser_identifier_origin_map_lookup(
		    resolver->origins,
		    start,
		    end - start,
		    &origin) == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE &&
	    origin.source_offset <= resolver->origin_source->length &&
	    origin.source_length <=
		    resolver->origin_source->length -
			    origin.source_offset &&
	    sqlparser_unquoted_identifier_matches_ascii_ci(
		    resolver->origin_source->sql,
		    origin.source_offset,
		    origin.source_offset + origin.source_length,
		    keyword)) {
		*resolved =
			resolver->origin_source->sql + origin.source_offset;
		*resolved_length = origin.source_length;
	}
	if (end > resolver->cursor) {
		resolver->cursor = end;
	}
	return true;
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
	bool result;

	if (context == NULL || identifier == NULL || resolved == NULL ||
	    resolved_length == NULL) {
		return false;
	}
	*resolved = NULL;
	*resolved_length = 0U;
	resolver = (sqlparser_identifier_resolver_t *)context;
	if (component_index ==
	    POSTGRES_DEPARSE_SQL_VALUE_FUNCTION_COMPONENT) {
		return sqlparser_resolve_sql_value_function_spelling(
			resolver,
			identifier,
			location,
			resolved,
			resolved_length);
	}
	if (sqlparser_proto_location_is_identifier_spelling(location)) {
		sqlparser_generated_identifier_t *generated;

		if (sqlparser_handle_identifier_spelling(
			    resolver->handle,
			    location,
			    component_index,
			    resolved,
			    resolved_length)) {
			generated = sqlparser_find_generated_identifier(
				resolver,
				identifier);
			if (generated != NULL) {
				generated->consumed = 1;
			}
			return true;
		}
		resolver->generated_identifier_error = 1;
		return false;
	}
	{
		sqlparser_generated_identifier_t *generated;

		generated = sqlparser_find_generated_identifier(
			resolver,
			identifier);
			if (generated != NULL) {
				sqlparser_consume_generated_identifier(
				resolver,
				generated,
				location,
					component_index,
					search_forward);
				if (generated->consumed &&
				    generated->mutation != NULL &&
				    generated->mutation->spelling != NULL) {
					*resolved =
						generated->mutation->spelling;
					*resolved_length =
						strlen(*resolved);
					return true;
				}
				if (generated->consumed &&
			    generated->preserve_original &&
			    generated->resolved != NULL) {
				*resolved = generated->resolved;
				*resolved_length =
					generated->resolved_length;
				return true;
			}
			return false;
		}
	}
	if (location == POSTGRES_DEPARSE_GENERATED_IDENTIFIER_LOCATION) {
		return false;
	}
	if (location == POSTGRES_DEPARSE_SEMANTIC_IDENTIFIER_LOCATION) {
		return false;
	}
	if (location < POSTGRES_DEPARSE_GENERATED_STYLE_BASE) {
		return false;
	}
	if (component_index == POSTGRES_DEPARSE_WINDOW_NAME_COMPONENT) {
		result =
			location >= 0 &&
			sqlparser_resolve_window_name(
				resolver,
				identifier,
				(size_t)location,
				resolved,
				resolved_length) > 0;
		if (resolver->audit_identifiers && !result) {
			resolver->audit_failed = 1;
		}
		return result;
	}
	if (component_index ==
	    POSTGRES_DEPARSE_INDEX_ACCESS_METHOD_COMPONENT) {
		int explicit_using;

		result = sqlparser_resolve_index_access_method(
			resolver,
			identifier,
			resolved,
			resolved_length,
			&explicit_using) > 0;
		if (resolver->audit_identifiers &&
		    !result &&
		    (explicit_using || strcmp(identifier, "btree") != 0)) {
			resolver->audit_failed = 1;
		}
		return result;
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
			result = true;
			goto done;
		}
		if (match < 0 || cursor == 0U || resolver->audit_identifiers) {
			result = false;
			goto done;
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
			result = false;
			goto done;
		}
		resolver->cursor = prefix.cursor;
		result = true;
		goto done;
	}
	if (search_forward) {
		start = location >= 0 ? (size_t)location : resolver->cursor;
		if (start > resolver->length) {
			result = false;
			goto done;
		}
		result = sqlparser_resolve_identifier_forward(
			resolver,
			identifier,
			start,
			resolved,
			resolved_length) > 0;
		goto done;
	}
	if (location < 0 || (size_t)location >= resolver->length) {
		result = false;
		goto done;
	}
	result = sqlparser_resolve_qualified_identifier(
		resolver,
		identifier,
		(size_t)location,
		component_index,
		resolved,
		resolved_length) != 0;

done:
	if (resolver->audit_identifiers && !result) {
		resolver->audit_failed = 1;
	}
	return result;
}

static bool sqlparser_identifier_is_keyword(
	void *context,
	const char *identifier,
	const char *keyword,
	int location,
	bool search_forward)
{
	sqlparser_identifier_resolver_t *resolver;
	sqlparser_identifier_resolver_t probe;
	const char *resolved;
	size_t resolved_length;
	size_t start;
	size_t index;
	bool matched;

	if (context == NULL || identifier == NULL || keyword == NULL) {
		return false;
	}
	resolver = (sqlparser_identifier_resolver_t *)context;
	{
		sqlparser_generated_identifier_t *generated;

		generated = sqlparser_find_generated_identifier(
			resolver,
			identifier);
		if (generated != NULL) {
			sqlparser_consume_generated_identifier(
				resolver,
				generated,
				location,
				0U,
				search_forward);
			return false;
		}
	}
	for (index = 0U;; index++) {
		unsigned char left;
		unsigned char right;

		left = (unsigned char)identifier[index];
		right = (unsigned char)keyword[index];
		if (sqlparser_ascii_lower(left) != sqlparser_ascii_lower(right)) {
			return false;
		}
		if (left == '\0') {
			break;
		}
	}

	if (location == POSTGRES_DEPARSE_GENERATED_IDENTIFIER_LOCATION ||
	    location < POSTGRES_DEPARSE_GENERATED_STYLE_BASE) {
		return false;
	}
	start =
		resolver->locations_match && location >= 0 ?
			(size_t)location :
			resolver->cursor;
	if (start < resolver->cursor) {
		start = resolver->cursor;
	}
	start = sqlparser_skip_trivia(resolver, start);
	if (start < resolver->length &&
	    (resolver->sql[start] == '=' ||
	     resolver->sql[start] == '(' ||
	     resolver->sql[start] == ',')) {
		start = sqlparser_skip_trivia(resolver, start + 1U);
	}
	if (start > resolver->length) {
		return false;
	}
	probe = *resolver;
	probe.cursor = start;
	probe.keyword_match = 1;
	probe.audit_identifiers = 1;
	probe.audit_failed = 0;
	probe.consumed = NULL;
	resolved = NULL;
	resolved_length = 0U;
	matched = search_forward ?
		sqlparser_resolve_identifier_forward(
			&probe,
			identifier,
			start,
			&resolved,
			&resolved_length) > 0 :
		sqlparser_resolve_qualified_identifier(
			&probe,
			identifier,
			start,
			0U,
			&resolved,
			&resolved_length) > 0;
	if (matched && probe.cursor > resolver->cursor) {
		resolver->cursor = probe.cursor;
	}
	return matched;
}

static PostgresDeparseSourceTokenKind sqlparser_source_token_at(
	sqlparser_identifier_resolver_t *resolver,
	const char *identifier,
	size_t position,
	size_t *source_end)
{
	const char *resolved;
	size_t resolved_length;
	size_t string_end;
	size_t token_end;

	position = sqlparser_skip_trivia(resolver, position);
	if (position < resolver->length && resolver->sql[position] == '=') {
		position = sqlparser_skip_trivia(resolver, position + 1U);
	}
	*source_end = position;
	if (position >= resolver->length) {
		return POSTGRES_DEPARSE_SOURCE_TOKEN_NONE;
	}
	string_end = sqlparser_source_string_end(resolver, position);
	if (string_end != position) {
		*source_end = string_end;
		return POSTGRES_DEPARSE_SOURCE_TOKEN_STRING;
	}
	resolved = NULL;
	resolved_length = 0U;
	if (sqlparser_source_identifier_at(
		    resolver,
		    position,
		    identifier,
		    &resolved,
		    &resolved_length,
		    &token_end) > 0) {
		*source_end = token_end;
		return POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MATCH;
	}
	*source_end = sqlparser_identifier_end(
		resolver->sql,
		resolver->length,
		position);
	return sqlparser_source_is_identifier(resolver, position) ?
		POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MISMATCH :
		POSTGRES_DEPARSE_SOURCE_TOKEN_NONE;
}

static PostgresDeparseSourceTokenKind sqlparser_source_token_forward(
	sqlparser_identifier_resolver_t *resolver,
	const char *identifier,
	size_t position,
	size_t *source_end)
{
	int first;

	*source_end = position;
	first = 1;
	while (position < resolver->length) {
		PostgresDeparseSourceTokenKind kind;
		size_t token_end;

		position = sqlparser_skip_trivia(resolver, position);
		if (position >= resolver->length) {
			break;
		}
		if (first &&
		    (resolver->sql[position] == '=' ||
		     resolver->sql[position] == '(' ||
		     resolver->sql[position] == ',')) {
			position++;
			first = 0;
			continue;
		}
		first = 0;
		if (resolver->sql[position] == ',' ||
		    resolver->sql[position] == ';' ||
		    resolver->sql[position] == '(' ||
		    resolver->sql[position] == ')') {
			break;
		}
		kind = sqlparser_source_token_at(
			resolver,
			identifier,
			position,
			&token_end);
		if (kind == POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MATCH ||
		    kind == POSTGRES_DEPARSE_SOURCE_TOKEN_STRING) {
			*source_end = token_end;
			return kind;
		}
		if (kind == POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MISMATCH) {
			if (token_end > position &&
			    resolver->sql[position] != '"' &&
			    resolver->sql[position] != '`' &&
			    resolver->sql[position] != '[' &&
			    sqlparser_unquoted_identifier_matches_ascii_ci(
				    resolver->sql,
				    position,
				    token_end,
				    identifier)) {
				*source_end = token_end;
				return kind;
			}
			if (token_end == position ||
			    !sqlparser_source_word_is_syntax_keyword(
				    resolver,
				    position,
				    token_end)) {
				*source_end = token_end;
				return kind;
			}
			position = token_end;
			continue;
		}
		if (resolver->sql[position] >= '0' &&
		    resolver->sql[position] <= '9') {
			position = sqlparser_skip_number(
				resolver->sql,
				resolver->length,
				position);
		} else {
			position++;
		}
	}
	return POSTGRES_DEPARSE_SOURCE_TOKEN_NONE;
}

static PostgresDeparseSourceTokenKind sqlparser_source_token_probe(
	void *context,
	const char *identifier,
	int location,
	bool search_forward)
{
	sqlparser_identifier_resolver_t probe;
	sqlparser_identifier_resolver_t *resolver;
	size_t start;
	size_t source_end;
	PostgresDeparseSourceTokenKind kind;

	if (context == NULL || identifier == NULL) {
		return POSTGRES_DEPARSE_SOURCE_TOKEN_NONE;
	}
	resolver = (sqlparser_identifier_resolver_t *)context;
	{
		sqlparser_generated_identifier_t *generated;

		generated = sqlparser_find_generated_identifier(
			resolver,
			identifier);
		if (generated != NULL) {
			PostgresDeparseSourceTokenKind generated_kind;

			if (generated->mutation == NULL) {
				resolver->generated_identifier_error = 1;
				return POSTGRES_DEPARSE_SOURCE_TOKEN_NONE;
			}
			if (!generated->mutation->source_present) {
				sqlparser_consume_generated_identifier(
					resolver,
					generated,
					location,
					0U,
					search_forward);
				return POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MISMATCH;
			}
			probe = *resolver;
			probe.audit_identifiers = 1;
			probe.audit_failed = 0;
			probe.consumed = NULL;
			probe.generated_identifiers = NULL;
			probe.generated_identifier_count = 0U;
			start =
				resolver->locations_match && location >= 0 ?
					(size_t)location :
					resolver->cursor;
			if (start < resolver->cursor) {
				start = resolver->cursor;
			}
			if (start > resolver->length) {
				return POSTGRES_DEPARSE_SOURCE_TOKEN_NONE;
			}
			generated_kind = search_forward ?
				sqlparser_source_token_forward(
					&probe,
					generated->mutation->original,
					start,
					&source_end) :
				sqlparser_source_token_at(
					&probe,
					generated->mutation->original,
					start,
					&source_end);
			if (generated_kind ==
			    POSTGRES_DEPARSE_SOURCE_TOKEN_STRING) {
				if (source_end > resolver->cursor) {
					resolver->cursor = source_end;
				}
				generated->consumed = 1;
				return generated_kind;
			}
			sqlparser_consume_generated_identifier(
				resolver,
				generated,
				location,
				0U,
				search_forward);
			return generated_kind ==
				       POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MATCH ?
				generated_kind :
				POSTGRES_DEPARSE_SOURCE_TOKEN_IDENTIFIER_MISMATCH;
		}
	}
	probe = *resolver;
	probe.audit_identifiers = 0;
	probe.audit_failed = 0;
	probe.consumed = NULL;
	start =
		resolver->locations_match && location >= 0 ?
			(size_t)location :
			resolver->cursor;
	if (start < resolver->cursor) {
		start = resolver->cursor;
	}
	if (start > resolver->length) {
		return POSTGRES_DEPARSE_SOURCE_TOKEN_NONE;
	}
	kind = search_forward ?
		sqlparser_source_token_forward(
			&probe,
			identifier,
			start,
			&source_end) :
		sqlparser_source_token_at(
			&probe,
			identifier,
			start,
			&source_end);
	if (kind == POSTGRES_DEPARSE_SOURCE_TOKEN_STRING &&
	    source_end > resolver->cursor) {
		resolver->cursor = source_end;
	}
	return kind;
}

PgQueryProtobufParseResult
sqlparser_parse_protobuf_preserving_identifier_spelling(
	const char *parser_sql)
{
	if (parser_sql == NULL) {
		return pg_query_parse_protobuf(parser_sql);
	}
	return pg_query_parse_protobuf_opts_preserving_identifier_spelling(
		parser_sql,
		PG_QUERY_PARSE_DEFAULT);
}

static void sqlparser_identifier_resolver_init(
	const sqlparser_handle_t *handle,
	sqlparser_identifier_resolver_t *resolver)
{
	memset(resolver, 0, sizeof(*resolver));
	if (handle == NULL) {
		return;
	}
	resolver->handle = handle;
	resolver->locations_match =
		handle->sql != NULL &&
		handle->parser_sql != NULL &&
		handle->sql_len == handle->parser_sql_len &&
		memcmp(handle->sql, handle->parser_sql, handle->sql_len) == 0;
	resolver->sql = handle->sql;
	resolver->length = handle->sql_len;
	resolver->mysql_lex =
		handle->dialect == SQLPARSER_DIALECT_MYSQL ||
		handle->dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL;
	resolver->oracle_q_quotes =
		handle->dialect == SQLPARSER_DIALECT_ORACLE ||
		handle->dialect == SQLPARSER_DIALECT_DAMENG ||
		handle->dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE;
	resolver->colon_binds = resolver->oracle_q_quotes;
	resolver->at_binds =
		resolver->mysql_lex ||
		handle->dialect == SQLPARSER_DIALECT_SQLSERVER ||
		handle->dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
	resolver->top_keyword =
		handle->dialect == SQLPARSER_DIALECT_DAMENG ||
		handle->dialect == SQLPARSER_DIALECT_SQLSERVER ||
		handle->dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER;
}

sqlparser_status_t sqlparser_identifier_origins_for_handle(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t **out_origins,
	sqlparser_error_t *out_error)
{
	sqlparser_handle_t *mutable_handle;
	sqlparser_identifier_origin_map_t *origins;
	char *parser_sql;
	void *dialect_state;
	sqlparser_status_t status;

	if (handle == NULL || out_origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle and identifier origin output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_origins = NULL;
	if (handle->identifier_origins != NULL) {
		*out_origins = handle->identifier_origins;
		return SQLPARSER_STATUS_OK;
	}
	mutable_handle = (sqlparser_handle_t *)handle;
	if (handle->sql_len == handle->parser_sql_len &&
	    memcmp(handle->sql, handle->parser_sql, handle->sql_len) == 0) {
		status = sqlparser_identifier_origin_map_new_identity(
			handle->sql_len,
			&origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		mutable_handle->identifier_origins = origins;
		*out_origins = origins;
		return SQLPARSER_STATUS_OK;
	}

	parser_sql = NULL;
	dialect_state = NULL;
	origins = NULL;
	status = sqlparser_dialect_preprocess_identifier_origins(
		handle->dialect,
		handle->sql,
		&handle->limits,
		&parser_sql,
		&dialect_state,
		&origins,
		out_error);
	if (status == SQLPARSER_STATUS_OK &&
	    (parser_sql == NULL ||
	     strlen(parser_sql) != handle->parser_sql_len ||
	     memcmp(
		     parser_sql,
		     handle->parser_sql,
		     handle->parser_sql_len) != 0 ||
	     sqlparser_identifier_origin_map_output_length(origins) !=
		     handle->parser_sql_len)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"identifier origin replay differs from parser input");
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (handle->dialect_ops != NULL &&
	    handle->dialect_ops->destroy_state != NULL) {
		handle->dialect_ops->destroy_state(dialect_state);
	}
	free(parser_sql);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_identifier_origin_map_destroy(origins);
		return status;
	}
	mutable_handle->identifier_origins = origins;
	*out_origins = origins;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_resolve_relation_component_spelling(
	const sqlparser_handle_t *handle,
	const PgQuery__RangeVar *relation,
	size_t component_index,
	const char *identifier,
	char **out_spelling,
	sqlparser_error_t *out_error)
{
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_identifier_resolver_t resolver;
	sqlparser_identifier_resolver_t source_resolver;
	const char *resolved;
	char *spelling;
	size_t resolved_length;
	sqlparser_status_t status;

	if (handle == NULL || relation == NULL || identifier == NULL ||
	    identifier[0] == '\0' || out_spelling == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"relation identifier spelling input is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_spelling = NULL;
	resolved = NULL;
	resolved_length = 0U;
	if (sqlparser_proto_location_is_identifier_spelling(
		    relation->location)) {
		if (!sqlparser_handle_identifier_spelling(
			    handle,
			    relation->location,
			    component_index,
			    &resolved,
			    &resolved_length)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"relation identifier spelling is unavailable");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	} else {
		if (relation->location < 0 || handle->parser_sql == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"relation identifier source is unavailable");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		origins = NULL;
		status = sqlparser_identifier_origins_for_handle(
			handle,
			&origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return status;
		}
		sqlparser_identifier_resolver_init(handle, &source_resolver);
		sqlparser_identifier_resolver_init(handle, &resolver);
		resolver.sql = handle->parser_sql;
		resolver.length = handle->parser_sql_len;
		resolver.locations_match = 1;
		resolver.mysql_lex = 0;
		resolver.oracle_q_quotes = 0;
		resolver.colon_binds = 0;
		resolver.at_binds = 0;
		resolver.top_keyword = 0;
		resolver.origins = origins;
		resolver.origin_source = &source_resolver;
		if (!sqlparser_resolve_qualified_identifier(
			    &resolver,
			    identifier,
			    (size_t)relation->location,
			    component_index,
			    &resolved,
			    &resolved_length)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"relation identifier spelling could not be resolved");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
	}
	if (resolved_length == SIZE_MAX) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	spelling = (char *)malloc(resolved_length + 1U);
	if (spelling == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	memcpy(spelling, resolved, resolved_length);
	spelling[resolved_length] = '\0';
	*out_spelling = spelling;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_render_default_identifier_spelling(
	const char *identifier,
	char **out_spelling,
	sqlparser_error_t *out_error)
{
	char *spelling;
	size_t identifier_length;
	size_t quote_count;
	size_t index;
	size_t output_length;
	int safe;

	if (identifier == NULL || identifier[0] == '\0' ||
	    out_spelling == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier spelling input is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_spelling = NULL;
	identifier_length = strlen(identifier);
	quote_count = 0U;
	safe =
		(identifier[0] >= 'a' && identifier[0] <= 'z') ||
		identifier[0] == '_';
	for (index = 0U; index < identifier_length; index++) {
		char character;

		character = identifier[index];
		if ((character >= 'a' && character <= 'z') ||
		    (character >= '0' && character <= '9') ||
		    character == '_') {
			continue;
		}
		safe = 0;
		if (character == '"') {
			quote_count++;
		}
	}
	if (safe &&
	    postgres_deparse_keyword_category(
		    identifier,
		    identifier_length) <= 0) {
		spelling = sqlparser_strdup(identifier);
		if (spelling == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		*out_spelling = spelling;
		return SQLPARSER_STATUS_OK;
	}
	if (identifier_length >
	    SIZE_MAX - quote_count - 3U) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	output_length = identifier_length + quote_count + 2U;
	spelling = (char *)malloc(output_length + 1U);
	if (spelling == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	output_length = 0U;
	spelling[output_length++] = '"';
	for (index = 0U; index < identifier_length; index++) {
		if (identifier[index] == '"') {
			spelling[output_length++] = '"';
		}
		spelling[output_length++] = identifier[index];
	}
	spelling[output_length++] = '"';
	spelling[output_length] = '\0';
	*out_spelling = spelling;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_mark_proto_generated_from_handle(
	const sqlparser_handle_t *handle,
	ProtobufCMessage *message,
	sqlparser_error_t *out_error)
{
	sqlparser_generated_source_t source;
	const sqlparser_identifier_origin_map_t *origins;
	sqlparser_status_t status;

	if (handle == NULL || message == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"generated fragment source must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	origins = NULL;
	status = sqlparser_identifier_origins_for_handle(
		handle,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}

	memset(&source, 0, sizeof(source));
	source.public_sql = handle->sql;
	source.origins = origins;
	source.dialect = handle->dialect;
	source.spelling_handle = NULL;
	status = sqlparser_mark_proto_generated_with_fragment_source(
		message,
		handle->parser_sql,
		0U,
		&source,
		out_error);
	return status;
}

static int sqlparser_bytes_contain(
	const char *bytes,
	size_t byte_count,
	const char *needle,
	size_t needle_length)
{
	size_t index;

	if (needle_length == 0U || needle_length > byte_count) {
		return 0;
	}
	for (index = 0U;
	     index <= byte_count - needle_length;
	     index++) {
		if (memcmp(bytes + index, needle, needle_length) == 0) {
			return 1;
		}
	}
	return 0;
}

static int sqlparser_prepare_generated_identifier_tree(
	const sqlparser_handle_t *handle,
	PgQueryProtobuf parse_tree,
	size_t raw_statement_offset,
	sqlparser_identifier_resolver_t *resolver,
	PostgresDeparseOpts *options,
	char *prefix,
	size_t prefix_capacity,
	PgQueryProtobuf *out_tree)
{
	PgQuery__ParseResult *ast;
	char *packed;
	size_t applied;
	size_t counter;
	size_t mutation_index;
	size_t packed_len;
	size_t packed_size;
	size_t prefix_length;

	*out_tree = parse_tree;
	if (handle->identifier_mutation_count == 0U) {
		return 1;
	}
	if (handle->identifier_mutation_count >
	    SIZE_MAX / sizeof(*resolver->generated_identifiers)) {
		return 0;
	}
	resolver->generated_identifiers =
		(sqlparser_generated_identifier_t *)calloc(
			handle->identifier_mutation_count,
			sizeof(*resolver->generated_identifiers));
	if (resolver->generated_identifiers == NULL) {
		return 0;
	}
	resolver->generated_identifier_count =
		handle->identifier_mutation_count;
	for (mutation_index = 0U;
	     mutation_index < handle->identifier_mutation_count;
	     mutation_index++) {
		resolver->generated_identifiers[mutation_index].mutation =
			&handle->identifier_mutations[mutation_index];
	}

	prefix_length = 0U;
	for (counter = 0U;; counter++) {
		int written;

		written = snprintf(
			prefix,
			prefix_capacity,
			"\001sqlparser_generated_%llu_",
			(unsigned long long)counter);
		if (written <= 0 || (size_t)written >= prefix_capacity) {
			return 0;
		}
		prefix_length = (size_t)written;
		if (!sqlparser_bytes_contain(
			    parse_tree.data,
			    parse_tree.len,
			    prefix,
			    prefix_length)) {
			break;
		}
		if (counter == SIZE_MAX) {
			return 0;
		}
	}

	ast = pg_query__parse_result__unpack(
		NULL,
		parse_tree.len,
		(const uint8_t *)parse_tree.data);
	if (ast == NULL) {
		return 0;
	}
	applied = 0U;
	for (mutation_index = 0U;
	     mutation_index < handle->identifier_mutation_count;
	     mutation_index++) {
		const sqlparser_identifier_mutation_t *mutation;
		char identifier_index[32];
		char **slot;
		char *tagged;
		size_t identifier_index_length;
		size_t tagged_length;
		size_t value_length;
		size_t local_statement_index;
		int written;
		sqlparser_error_t error;
		sqlparser_status_t status;

		mutation = &handle->identifier_mutations[mutation_index];
		if (mutation->raw_statement_index < raw_statement_offset) {
			continue;
		}
		local_statement_index =
			mutation->raw_statement_index - raw_statement_offset;
		if (local_statement_index >= ast->n_stmts) {
			continue;
		}
		slot = NULL;
		status = sqlparser_get_raw_statement_string_slot_by_index(
			ast,
			local_statement_index,
			mutation->string_index,
			&slot);
		if (status != SQLPARSER_STATUS_OK || slot == NULL ||
		    *slot == NULL ||
		    (*slot)[0] == '\0' ||
		    (mutation->relation_group == 0U &&
		     mutation->spelling == NULL &&
		     strcmp(*slot, mutation->original) == 0)) {
			continue;
		}
		written = snprintf(
			identifier_index,
			sizeof(identifier_index),
			"%llu",
			(unsigned long long)mutation_index);
		if (written <= 0 ||
		    (size_t)written >= sizeof(identifier_index)) {
			pg_query__parse_result__free_unpacked(ast, NULL);
			return 0;
		}
		identifier_index_length = (size_t)written;
		value_length = strlen(*slot);
		if (prefix_length >
			    SIZE_MAX - identifier_index_length - 2U ||
		    prefix_length + identifier_index_length + 2U >
			    SIZE_MAX - value_length) {
			pg_query__parse_result__free_unpacked(ast, NULL);
			return 0;
		}
		tagged_length =
			prefix_length + identifier_index_length + 1U +
			value_length;
		tagged = (char *)malloc(tagged_length + 1U);
		if (tagged == NULL) {
			pg_query__parse_result__free_unpacked(ast, NULL);
			return 0;
		}
		memcpy(tagged, prefix, prefix_length);
		memcpy(
			tagged + prefix_length,
			identifier_index,
			identifier_index_length);
		tagged[prefix_length + identifier_index_length] = ':';
		memcpy(
			tagged + prefix_length + identifier_index_length + 1U,
			*slot,
			value_length + 1U);
		memset(&error, 0, sizeof(error));
		status = sqlparser_replace_proto_string(
			slot,
			tagged,
			&error);
		free(tagged);
		if (status != SQLPARSER_STATUS_OK) {
			pg_query__parse_result__free_unpacked(ast, NULL);
			return 0;
		}
		resolver->generated_identifiers[mutation_index].tagged = 1;
		applied++;
	}
	if (applied == 0U) {
		pg_query__parse_result__free_unpacked(ast, NULL);
		free(resolver->generated_identifiers);
		resolver->generated_identifiers = NULL;
		resolver->generated_identifier_count = 0U;
		return 1;
	}
	packed_size = pg_query__parse_result__get_packed_size(ast);
	if (packed_size == 0U) {
		pg_query__parse_result__free_unpacked(ast, NULL);
		return 0;
	}
	packed = (char *)malloc(packed_size);
	if (packed == NULL) {
		pg_query__parse_result__free_unpacked(ast, NULL);
		return 0;
	}
	packed_len = pg_query__parse_result__pack(
		ast,
		(uint8_t *)packed);
	pg_query__parse_result__free_unpacked(ast, NULL);
	if (packed_len != packed_size) {
		free(packed);
		return 0;
	}
	out_tree->data = packed;
	out_tree->len = packed_len;
	options->generated_identifier_prefix = prefix;
	options->generated_identifier_prefix_length = prefix_length;
	options->generated_identifier_reader =
		sqlparser_read_generated_identifier;
	options->generated_identifier_probe =
		sqlparser_generated_identifier_probe;
	return 1;
}

sqlparser_status_t sqlparser_validate_ast_identifier_spelling(
	const sqlparser_handle_t *handle,
	sqlparser_error_t *out_error)
{
	const sqlparser_identifier_origin_map_t *origins;
	PostgresDeparseOpts options;
	PgQueryProtobuf effective_tree;
	PgQueryProtobufParseResult reference_parse;
	PgQueryDeparseResult result;
	sqlparser_identifier_resolver_t reference_resolver;
	sqlparser_identifier_resolver_t resolver;
	sqlparser_identifier_resolver_t source_resolver;
	sqlparser_status_t status;
	char generated_identifier_prefix[64];
	size_t generated_index;
	size_t index;

	if (handle == NULL || handle->sql == NULL ||
	    handle->parser_sql == NULL || handle->parse_tree.data == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"handle must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}

	origins = NULL;
	memset(&reference_parse, 0, sizeof(reference_parse));
	status = sqlparser_identifier_origins_for_handle(
		handle,
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	memset(&options, 0, sizeof(options));
	sqlparser_identifier_resolver_init(handle, &source_resolver);
	sqlparser_identifier_resolver_init(handle, &resolver);
	resolver.sql = handle->parser_sql;
	resolver.length = handle->parser_sql_len;
	resolver.cursor = 0U;
	resolver.locations_match = 1;
	resolver.mysql_lex = 0;
	resolver.oracle_q_quotes = 0;
	resolver.colon_binds = 0;
	resolver.at_binds = 0;
	resolver.top_keyword = 0;
	resolver.audit_identifiers = 1;
	resolver.origins = origins;
	resolver.origin_source = &source_resolver;
	if (resolver.length > 0U) {
		resolver.consumed =
			(unsigned char *)calloc(resolver.length, sizeof(*resolver.consumed));
		if (resolver.consumed == NULL) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
	}
	options.identifier_resolver = sqlparser_resolve_identifier;
	options.keyword_matcher = sqlparser_identifier_is_keyword;
	options.source_token_probe = sqlparser_source_token_probe;
	if (handle->generation == 0UL) {
		reference_resolver = resolver;
		reference_resolver.cursor = 0U;
		reference_resolver.audit_failed = 0;
		reference_resolver.consumed = NULL;
		if (reference_resolver.length > 0U) {
			reference_resolver.consumed =
				(unsigned char *)calloc(
					reference_resolver.length,
					sizeof(*reference_resolver.consumed));
			if (reference_resolver.consumed == NULL) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				free(resolver.consumed);
				return SQLPARSER_STATUS_NO_MEMORY;
			}
		}
		reference_parse =
			sqlparser_parse_protobuf_preserving_identifier_spelling(
				handle->parser_sql);
		if (reference_parse.error != NULL ||
		    reference_parse.parse_tree.data == NULL) {
			if (reference_parse.error != NULL) {
				sqlparser_error_from_pg(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					handle->parser_sql,
					reference_parse.error);
			} else {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"failed to rebuild identifier audit AST");
			}
			pg_query_free_protobuf_parse_result(reference_parse);
			free(reference_resolver.consumed);
			free(resolver.consumed);
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		options.identifier_resolver_context =
			&reference_resolver;
		result = pg_query_deparse_protobuf_opts(
			reference_parse.parse_tree,
			options);
		if (result.error != NULL || result.query == NULL ||
		    reference_resolver.audit_failed) {
			if (result.error != NULL) {
				sqlparser_error_from_pg(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					handle->parser_sql,
					result.error);
			} else {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_INTERNAL_ERROR,
					"failed to audit reference AST identifiers");
			}
			pg_query_free_deparse_result(result);
			pg_query_free_protobuf_parse_result(reference_parse);
			free(reference_resolver.consumed);
			free(resolver.consumed);
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		pg_query_free_deparse_result(result);
	} else {
		memset(&reference_resolver, 0, sizeof(reference_resolver));
	}
	options.identifier_resolver_context = &resolver;
	if (!sqlparser_prepare_generated_identifier_tree(
		    handle,
		    handle->parse_tree,
		    0U,
		    &resolver,
		    &options,
		    generated_identifier_prefix,
		    sizeof(generated_identifier_prefix),
		    &effective_tree)) {
		free(resolver.generated_identifiers);
		pg_query_free_protobuf_parse_result(reference_parse);
		free(reference_resolver.consumed);
		free(resolver.consumed);
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"failed to prepare identifier audit AST");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	result = pg_query_deparse_protobuf_opts(effective_tree, options);
	if (effective_tree.data != handle->parse_tree.data) {
		free(effective_tree.data);
	}
	if (result.error != NULL || result.query == NULL) {
		if (result.error != NULL) {
			sqlparser_error_from_pg(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				handle->parser_sql,
				result.error);
		} else {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"failed to deparse identifier audit AST");
		}
		pg_query_free_deparse_result(result);
		pg_query_free_protobuf_parse_result(reference_parse);
		free(reference_resolver.consumed);
		free(resolver.consumed);
		free(resolver.generated_identifiers);
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (options.generated_identifier_prefix_length > 0U &&
	    sqlparser_bytes_contain(
		    result.query,
		    strlen(result.query),
		    generated_identifier_prefix,
		    options.generated_identifier_prefix_length)) {
		resolver.generated_identifier_error = 1;
	}
	for (generated_index = 0U;
	     generated_index < resolver.generated_identifier_count;
	     generated_index++) {
		sqlparser_generated_identifier_t *generated;

		generated =
			&resolver.generated_identifiers[generated_index];
		if (generated->tagged &&
		    (!generated->registered || !generated->consumed)) {
			resolver.generated_identifier_error = 1;
			break;
		}
	}
	pg_query_free_deparse_result(result);
	if (handle->generation == 0UL) {
		for (index = 0U; index < resolver.length; index++) {
			if (reference_resolver.consumed[index] != 0U &&
			    resolver.consumed[index] == 0U) {
				resolver.audit_failed = 1;
				break;
			}
		}
	}
	pg_query_free_protobuf_parse_result(reference_parse);
	free(reference_resolver.consumed);
	free(resolver.consumed);
	free(resolver.generated_identifiers);
	if (resolver.audit_failed || resolver.generated_identifier_error) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"AST identifier spelling differs from original SQL");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	sqlparser_error_clear(out_error);
	return SQLPARSER_STATUS_OK;
}

PgQueryDeparseResult sqlparser_deparse_protobuf_for_handle(
	const sqlparser_handle_t *handle,
	PgQueryProtobuf parse_tree,
	size_t raw_statement_offset,
	size_t source_start,
	size_t source_end)
{
	const sqlparser_identifier_origin_map_t *origins;
	PostgresDeparseOpts options;
	PgQueryProtobuf effective_tree;
	PgQueryDeparseResult result;
	sqlparser_identifier_resolver_t resolver;
	sqlparser_identifier_resolver_t source_resolver;
	sqlparser_error_t error;
	char generated_identifier_prefix[64];
	size_t generated_index;

	memset(&options, 0, sizeof(options));
	if (handle == NULL || handle->parser_sql == NULL) {
		return pg_query_deparse_protobuf_opts(parse_tree, options);
	}
	if (source_start > source_end ||
	    source_end > handle->parser_sql_len) {
		memset(&result, 0, sizeof(result));
		return result;
	}

	origins = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_identifier_origins_for_handle(
		    handle,
		    &origins,
		    &error) != SQLPARSER_STATUS_OK) {
		memset(&result, 0, sizeof(result));
		return result;
	}
	sqlparser_identifier_resolver_init(handle, &source_resolver);
	sqlparser_identifier_resolver_init(handle, &resolver);
	resolver.sql = handle->parser_sql;
	resolver.length = source_end;
	resolver.cursor = source_start;
	resolver.locations_match = 1;
	resolver.mysql_lex = 0;
	resolver.oracle_q_quotes = 0;
	resolver.colon_binds = 0;
	resolver.at_binds = 0;
	resolver.top_keyword = 0;
	resolver.audit_identifiers = handle->generation == 0UL;
	resolver.origins = origins;
	resolver.origin_source = &source_resolver;
	options.identifier_resolver = sqlparser_resolve_identifier;
	options.keyword_matcher = sqlparser_identifier_is_keyword;
	options.source_token_probe = sqlparser_source_token_probe;
	options.identifier_resolver_context = &resolver;
	if (!sqlparser_prepare_generated_identifier_tree(
		    handle,
		    parse_tree,
		    raw_statement_offset,
		    &resolver,
		    &options,
		    generated_identifier_prefix,
		    sizeof(generated_identifier_prefix),
		    &effective_tree)) {
		free(resolver.generated_identifiers);
		memset(&result, 0, sizeof(result));
		return result;
	}
	result = pg_query_deparse_protobuf_opts(effective_tree, options);
	if (effective_tree.data != parse_tree.data) {
		free(effective_tree.data);
	}
	if (result.query != NULL &&
	    options.generated_identifier_prefix_length > 0U &&
	    sqlparser_bytes_contain(
		    result.query,
		    strlen(result.query),
		    generated_identifier_prefix,
		    options.generated_identifier_prefix_length)) {
		resolver.generated_identifier_error = 1;
	}
	for (generated_index = 0U;
	     generated_index < resolver.generated_identifier_count;
	     generated_index++) {
		sqlparser_generated_identifier_t *generated;

		generated =
			&resolver.generated_identifiers[generated_index];
		if (generated->tagged &&
		    (!generated->registered || !generated->consumed)) {
			resolver.generated_identifier_error = 1;
			break;
		}
	}
	if (resolver.generated_identifier_error) {
		pg_query_free_deparse_result(result);
		memset(&result, 0, sizeof(result));
	}
	free(resolver.generated_identifiers);
	return result;
}
