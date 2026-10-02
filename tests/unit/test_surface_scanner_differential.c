/* Frozen pre-optimization scanner from e2006f1. Keep the reference scalar and
 * independent of the production quote/comment scanners. */
#include <ctype.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/dialect/sqlparser_dialect_internal.h"
#include "sqlparser_internal.h"

static int reference_char_is_ident(unsigned char ch)
{
	return isalnum(ch) || ch == '_' || ch == '$' || ch == '#' ||
		ch >= 0x80U;
}

static int reference_dollar_tag_char_is_ident(unsigned char ch)
{
	return isalnum(ch) || ch == '_' || ch >= 0x80U;
}

static size_t reference_skip_dollar_quote(
	const char *sql,
	size_t index)
{
	size_t body;
	size_t delimiter_length;
	size_t tag_end;

	if (sql == NULL || sql[index] != '$' ||
	    (index > 0U &&
	     (isalnum((unsigned char)sql[index - 1U]) ||
	      sql[index - 1U] == '_' || sql[index - 1U] == '$' ||
	      (unsigned char)sql[index - 1U] >= 0x80U))) {
		return index;
	}
	tag_end = index + 1U;
	while (reference_dollar_tag_char_is_ident(
		       (unsigned char)sql[tag_end])) {
		tag_end++;
	}
	if (sql[tag_end] != '$') {
		return index;
	}
	delimiter_length = tag_end - index + 1U;
	body = tag_end + 1U;
	while (sql[body] != '\0') {
		if (strncmp(sql + body, sql + index, delimiter_length) == 0) {
			return body + delimiter_length;
		}
		body++;
	}
	return index;
}

static size_t reference_skip_oracle_q_quote(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t index)
{
	char close;
	char open;
	size_t pos;

	if (!sqlparser_dialect_is_oracle_or_dameng_compatible(dialect) ||
	    sql == NULL) {
		return index;
	}
	pos = index;
	if ((sql[pos] == 'n' || sql[pos] == 'N') &&
	    (sql[pos + 1U] == 'q' || sql[pos + 1U] == 'Q')) {
		pos++;
	}
	if ((sql[pos] != 'q' && sql[pos] != 'Q') ||
	    sql[pos + 1U] != '\'' || sql[pos + 2U] == '\0') {
		return index;
	}
	open = sql[pos + 2U];
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
	pos += 3U;
	while (sql[pos] != '\0') {
		if (sql[pos] == close && sql[pos + 1U] == '\'') {
			return pos + 2U;
		}
		pos++;
	}
	return pos;
}

static int reference_nested_comments(sqlparser_dialect_t dialect)
{
	return sqlparser_dialect_uses_postgresql_placeholders(dialect) ||
		sqlparser_dialect_is_sqlserver_compatible(dialect) ||
		dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE;
}

static size_t reference_skip_quoted_or_comment(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t index)
{
	char quote;
	size_t depth;
	size_t pos;

	if (sql == NULL || sql[index] == '\0') {
		return index;
	}
	if (sqlparser_dialect_uses_postgresql_placeholders(dialect) ||
	    sqlparser_dialect_is_oracle_compatible(dialect)) {
		pos = reference_skip_dollar_quote(sql, index);
		if (pos != index) {
			return pos;
		}
	}
	pos = reference_skip_oracle_q_quote(dialect, sql, index);
	if (pos != index) {
		return pos;
	}
	if (sql[index] == '-' && sql[index + 1U] == '-') {
		if (sqlparser_dialect_is_mysql_compatible(dialect) &&
		    sql[index + 2U] != '\0' &&
		    !isspace((unsigned char)sql[index + 2U]) &&
		    !iscntrl((unsigned char)sql[index + 2U])) {
			return index;
		}
		pos = index + 2U;
		while (sql[pos] != '\0' && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if (sqlparser_dialect_is_mysql_compatible(dialect) &&
	    sql[index] == '#') {
		pos = index + 1U;
		while (sql[pos] != '\0' && sql[pos] != '\n') {
			pos++;
		}
		return pos;
	}
	if (sql[index] == '/' && sql[index + 1U] == '*') {
		pos = index + 2U;
		depth = 1U;
		while (sql[pos] != '\0' && sql[pos + 1U] != '\0') {
			if (reference_nested_comments(dialect) &&
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
		return sql[pos] == '\0' ? pos : pos + 1U;
	}
	if (sqlparser_dialect_is_sqlserver_compatible(dialect) &&
	    sql[index] == '[') {
		pos = index + 1U;
		while (sql[pos] != '\0') {
			if (sql[pos] == ']') {
				if (sql[pos + 1U] == ']') {
					pos += 2U;
					continue;
				}
				return pos + 1U;
			}
			pos++;
		}
		return pos;
	}
	if (sql[index] != '\'' && sql[index] != '"' &&
	    (!sqlparser_dialect_is_mysql_compatible(dialect) ||
	     sql[index] != '`')) {
		return index;
	}

	quote = sql[index];
	pos = index + 1U;
	while (sql[pos] != '\0') {
		if (sql[pos] == '\\' &&
		    (sqlparser_dialect_is_mysql_compatible(dialect) ||
		     (quote == '\'' && index > 0U &&
		      (sql[index - 1U] == 'e' || sql[index - 1U] == 'E') &&
		      (index == 1U ||
		       !reference_char_is_ident(
			       (unsigned char)sql[index - 2U])))) &&
		    sql[pos + 1U] != '\0') {
			pos += 2U;
			continue;
		}
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

static uint32_t random_state = UINT32_C(0x58d7a31b);
static size_t comparisons;

static uint32_t next_random(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static void compare_input(const char *sql, size_t length)
{
	int dialect;
	size_t index;

	for (dialect = SQLPARSER_DIALECT_POSTGRESQL;
	     dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
		for (index = 0U; index <= length; index++) {
			size_t expected = reference_skip_quoted_or_comment(
				(sqlparser_dialect_t)dialect, sql, index);
			size_t actual = sqlparser_public_skip_quoted_or_comment(
				(sqlparser_dialect_t)dialect, sql, index);

			comparisons++;
			if (actual != expected || actual < index || actual > length) {
				size_t byte;

				fprintf(stderr,
					"surface scanner mismatch: locale=%s dialect=%d "
					"length=%zu index=%zu expected=%zu actual=%zu bytes=",
					setlocale(LC_CTYPE, NULL), dialect,
					length, index, expected, actual);
				for (byte = 0U; byte < length; byte++) {
					fprintf(stderr, "%02x", (unsigned char)sql[byte]);
				}
				fputc('\n', stderr);
				exit(1);
			}
		}
	}
}

/* Each prefix has an exact-sized allocation: sanitizers also exercise the
 * last-byte and trailing-escape lookaheads without spare padding. */
static void compare_prefixes(const char *sql)
{
	size_t length = strlen(sql);
	size_t end;

	for (end = 0U; end <= length; end++) {
		char *prefix = (char *)malloc(end + 1U);

		if (prefix == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memcpy(prefix, sql, end);
		prefix[end] = '\0';
		compare_input(prefix, end);
		free(prefix);
	}
}

static void check_fixed_inputs(void)
{
	static const char *const cases[] = {
		"", "a", " ", "\n", "\r", "\t", "\\", "'", "\"", "`", "[",
		"-", "--", "---", "--x", "-- x\ny", "--\rx\ny", "--\tx\ny",
		"--\vx\ny", "--\fx\ny", "--\001x\ny", "--\177x\ny",
		"--\200x\ny", "--\377x\ny", "#", "#x\ny", "#x\ry", "/", "/*",
		"/**/", "/*/", "/*x", "/*x*", "/*x/", "/*/**/*/tail",
		"/*outer /*inner*/ outer*/ tail", "/* / / * * / /* */ / */x",
		"/* outer /* inner */", "/*'\"`[--#\n */ x",
		"[]", "[abc]tail", "[a]]b]tail", "[a]]", "[a]]]", "[a\\]b]",
		"''", "''''", "'''", "'abc'tail", "'a''b'tail", "'a\\'b'tail",
		"'a\\\\'tail", "'a\\", "'a''", "'a\\\"b'", "'a\nb'",
		"\"\"", "\"a\"\"b\"tail", "\"a\\\"b\"tail", "\"a\\",
		"``", "`a``b`tail", "`a\\`b`tail", "`a\\", "`a\\'b`",
		"E'a\\'b'tail", "e'a\\'b'tail", " E'a\\'b'tail",
		"aE'a\\'b'tail", "1e'a\\'b'tail", "_E'a\\'b'tail",
		"$E'a\\'b'tail", "#e'a\\'b'tail", "\200E'a\\'b'tail",
		"\377e'a\\'b'tail", "-e'a\\'b'tail", "E\"a\\\"b\"tail",
		"N'a\\'b'tail", "n'a''b'tail", "n", "N", "q", "Q", "nq", "NQ",
		"q'", "nq'", "q'[", "q'[a]'tail", "q'{a}'tail", "q'(a)'tail",
		"q'<a>'tail", "q'!a!'tail", "q'aa a'tail", "q'\\a\\'tail",
		"Q'[a]x]''b]'tail", "nq'[a]'tail", "Nq'{a}'tail", "nQ'(a)'tail",
		"NQ'<a>'tail", "xq'[a]'tail", "xnq'[a]'tail", "q'\200a\200'tail",
		"q'\377a\377'tail", "q'\na\n'tail", "q'''tail", "q'['a''b]'tail",
		"$", "$$", "$$$$", "$$a$$tail", "$tag$a$tag$tail",
		"$tag$a$other$b$tag$tail", "$tag$truncated", "$tag", "$1$x$1$",
		"$_$x$_$tail", "$\200$x$\200$tail", "$\377$x$\377$tail",
		"a$$x$$tail", "_$$x$$tail", "1$$x$$tail", "$$$x$$$tail",
		"\200$$x$$tail", "#$$x$$tail", " $$x$$tail", "$a$b$a",
		"SELECT q'[a; /* b */]', E'a\\'b', `c``d`, [e]]f], $$g$$; -- tail\n"
	};
	size_t index;
	int dialect;

	for (dialect = SQLPARSER_DIALECT_POSTGRESQL;
	     dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
		if (sqlparser_public_skip_quoted_or_comment(
			    (sqlparser_dialect_t)dialect, NULL, 17U) != 17U) {
			fprintf(stderr, "NULL scanner input changed\n");
			exit(1);
		}
	}
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		compare_prefixes(cases[index]);
	}
	/* Every nonzero byte as an opener, escape-prefix boundary, dash-comment
	 * boundary, and an Oracle alternative quote delimiter. */
	for (index = 1U; index < 256U; index++) {
		char input[] = "xE'a\\'b' --x body\n q'xbodyx'";
		char opener[] = "x'body'/*comment*/";

		input[0] = (char)index;
		input[11] = (char)index;
		input[21] = (char)index;
		input[26] = (char)index;
		opener[0] = (char)index;
		compare_prefixes(input);
		compare_prefixes(opener);
	}
}

static void check_random_inputs(void)
{
	static const char syntax_bytes[] =
		"abnNqQeE_019$#-'\"`[]{}()<>/\\*!; \t\r\n\001\177\200\377";
	size_t sample;

	for (sample = 0U; sample < 6000U; sample++) {
		size_t length = next_random() % 257U;
		char *sql = (char *)malloc(length + 1U);
		size_t index;

		if (sql == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		for (index = 0U; index < length; index++) {
			uint32_t random = next_random();

			sql[index] = sample % 2U == 0U
				? (char)(1U + random % 255U)
				: syntax_bytes[random % (sizeof(syntax_bytes) - 1U)];
		}
		sql[length] = '\0';
		compare_input(sql, length);
		free(sql);
	}
}

static void check_long_inputs(void)
{
	static const char *const openers[] = {
		"'", "\"", "`", "E'", "[", "/*", "-- ", "#", "$$", "$tag$", "q'[", "NQ'!"
	};
	static const char *const closers[] = {
		"'", "\"", "`", "'", "]", "*/", "\n", "\n", "$$", "$tag$", "]'", "!'"
	};
	size_t fixture;

	for (fixture = 0U; fixture < sizeof(openers) / sizeof(openers[0]); fixture++) {
		size_t prefix_length = strlen(openers[fixture]);
		size_t suffix_length = strlen(closers[fixture]);
		size_t length = prefix_length + 4096U + suffix_length;
		char *sql = (char *)malloc(length + 1U);

		if (sql == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		memcpy(sql, openers[fixture], prefix_length);
		memset(sql + prefix_length, 'x', 4096U);
		memcpy(sql + prefix_length + 4096U, closers[fixture], suffix_length + 1U);
		compare_input(sql, length);
		/* Exercise the same long bodies without their closing delimiters. */
		sql[prefix_length + 4096U] = '\0';
		compare_input(sql, prefix_length + 4096U);
		free(sql);
	}
}

int main(void)
{
	static const char *const locales[] = {
		"C", "C.UTF-8", "en_US.UTF-8", "en_US.utf8", "de_DE.UTF-8", "tr_TR.UTF-8", ""
	};
	size_t locale_index;
	size_t tested_locales = 0U;

	for (locale_index = 0U; locale_index < sizeof(locales) / sizeof(locales[0]); locale_index++) {
		if (setlocale(LC_CTYPE, locales[locale_index]) == NULL) {
			continue;
		}
		tested_locales++;
		random_state = UINT32_C(0x58d7a31b);
		check_fixed_inputs();
		check_random_inputs();
		check_long_inputs();
	}
	printf("surface scanner differential: %zu comparisons across 13 dialects in %zu locale configurations passed\n",
	       comparisons, tested_locales);
	return 0;
}
