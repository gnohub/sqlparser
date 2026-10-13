/* Independent scalar scanner frozen from test_surface_scanner_differential.c
 * (e2006f1), including its own quote/dollar/q/comment helpers. */
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
