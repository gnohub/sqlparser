/* Frozen varint3 body; only function name changed. Valid buffer/index contract retained. */
static size_t scangates_frozen_oracle_scanner(const char *sql, size_t index)
{
	char quote;
	char open_delim;
	char close_delim;
	size_t q_prefix_len;
	size_t pos;

	pos = sqlparser_oracle_skip_dollar_quoted_span(sql, index);
	if (pos != index) {
		return pos;
	}

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
