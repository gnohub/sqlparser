/* Independent reference statement and expression scanners.
 * Only static linkage and function names/internal references differ.
 * Do not call optimized
 * statement, generic multi, expression-source or top-level scanners. */

static int spancert_frozen_sqlparser_public_char_is_ident(unsigned char ch)
{
	return isalnum(ch) || ch == '_' || ch == '$' || ch == '#' ||
		ch >= 0x80U;
}

static int spancert_frozen_sqlparser_public_dollar_tag_char_is_ident(unsigned char ch)
{
	return isalnum(ch) || ch == '_' || ch >= 0x80U;
}

static size_t spancert_frozen_sqlparser_public_skip_dollar_quote(
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
	while (spancert_frozen_sqlparser_public_dollar_tag_char_is_ident(
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

static size_t spancert_frozen_sqlparser_public_skip_oracle_q_quote(
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

static int spancert_frozen_sqlparser_public_nested_comments(sqlparser_dialect_t dialect)
{
	return sqlparser_dialect_uses_postgresql_placeholders(dialect) ||
		sqlparser_dialect_is_sqlserver_compatible(dialect) ||
		dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE;
}

static size_t spancert_frozen_sqlparser_public_skip_quoted_or_comment(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t index)
{
	char quote;
	char stops[3];
	int backslash_escapes;
	int nested_comments;
	size_t depth;
	size_t pos;

	if (sql == NULL) {
		return index;
	}
	/* Most callers visit every source byte. Only query dialect capabilities
	 * after a byte that can actually open that dialect's quote or comment. */
	switch (sql[index]) {
		case '$':
			if (sqlparser_dialect_uses_postgresql_placeholders(dialect) ||
			    sqlparser_dialect_is_oracle_compatible(dialect)) {
				return spancert_frozen_sqlparser_public_skip_dollar_quote(sql, index);
			}
			return index;
		case 'q':
		case 'Q':
		case 'n':
		case 'N':
			return spancert_frozen_sqlparser_public_skip_oracle_q_quote(dialect, sql, index);
		case '-':
			if (sql[index + 1U] != '-') {
				return index;
			}
			if (sqlparser_dialect_is_mysql_compatible(dialect) &&
			    sql[index + 2U] != '\0' &&
			    !isspace((unsigned char)sql[index + 2U]) &&
			    !iscntrl((unsigned char)sql[index + 2U])) {
				return index;
			}
			pos = index + 2U;
			return pos + strcspn(sql + pos, "\n");
		case '#':
			if (!sqlparser_dialect_is_mysql_compatible(dialect)) {
				return index;
			}
			pos = index + 1U;
			return pos + strcspn(sql + pos, "\n");
		case '/':
			if (sql[index + 1U] != '*') {
				return index;
			}
			pos = index + 2U;
			depth = 1U;
			nested_comments = spancert_frozen_sqlparser_public_nested_comments(dialect);
			for (;;) {
				pos += strcspn(sql + pos, nested_comments ? "/*" : "*");
				if (sql[pos] == '\0') {
					return pos;
				}
				if (sql[pos + 1U] == '\0') {
					return pos + 1U;
				}
				if (nested_comments && sql[pos] == '/' && sql[pos + 1U] == '*') {
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
		case '[':
			if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
				return index;
			}
			pos = index + 1U;
			for (;;) {
				pos += strcspn(sql + pos, "]");
				if (sql[pos] == '\0') {
					return pos;
				}
				if (sql[pos + 1U] != ']') {
					return pos + 1U;
				}
				pos += 2U;
			}
		case '`':
			if (!sqlparser_dialect_is_mysql_compatible(dialect)) {
				return index;
			}
			/* fall through */
		case '\'':
		case '"':
			break;
		default:
			return index;
	}

	quote = sql[index];
	backslash_escapes = sqlparser_dialect_is_mysql_compatible(dialect) ||
		(quote == '\'' && index > 0U &&
		 (sql[index - 1U] == 'e' || sql[index - 1U] == 'E') &&
		 (index == 1U ||
		  !spancert_frozen_sqlparser_public_char_is_ident((unsigned char)sql[index - 2U])));
	stops[0] = quote;
	stops[1] = backslash_escapes ? '\\' : '\0';
	stops[2] = '\0';
	pos = index + 1U;
	for (;;) {
		pos += strcspn(sql + pos, stops);
		if (sql[pos] == '\0') {
			return pos;
		}
		if (sql[pos] == '\\' && backslash_escapes) {
			if (sql[pos + 1U] == '\0') {
				return pos + 1U;
			}
			pos += 2U;
			continue;
		}
		if (sql[pos + 1U] != quote) {
			return pos + 1U;
		}
		pos += 2U;
	}
}

static int spancert_frozen_sqlparser_public_comment_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t pos)
{
	if (sql[pos] == '/' && sql[pos + 1U] == '*') {
		return 1;
	}
	if (sql[pos] == '-' && sql[pos + 1U] == '-') {
		return !sqlparser_dialect_is_mysql_compatible(dialect) ||
			sql[pos + 2U] == '\0' ||
			isspace((unsigned char)sql[pos + 2U]) ||
			iscntrl((unsigned char)sql[pos + 2U]);
	}
	return sqlparser_dialect_is_mysql_compatible(dialect) &&
		sql[pos] == '#';
}

static size_t spancert_frozen_sqlparser_public_skip_trivia(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t pos)
{
	size_t skipped;

	while (sql[pos] != '\0') {
		if (isspace((unsigned char)sql[pos])) {
			pos++;
			continue;
		}
		if (!spancert_frozen_sqlparser_public_comment_at(dialect, sql, pos)) {
			break;
		}
		skipped = spancert_frozen_sqlparser_public_skip_quoted_or_comment(
			dialect,
			sql,
			pos);
		if (skipped == pos) {
			break;
		}
		pos = skipped;
	}
	return pos;
}

static size_t spancert_frozen_sqlparser_public_skip_space(const char *sql, size_t pos)
{
	while (sql[pos] != '\0' && isspace((unsigned char)sql[pos])) {
		pos++;
	}
	return pos;
}

static int spancert_frozen_sqlparser_public_sqlserver_go_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t pos,
	size_t *out_after)
{
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect) ||
	    sql == NULL) {
		return 0;
	}
	return sqlparser_sqlserver_line_is_go(sql, pos, out_after);
}
