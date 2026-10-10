/* Independent reference Oracle list scanners.
 * Source: src/dialect/sqlparser_dialect_oracle.c
 * These six helper bodies differ only in the listbounds_frozen_ name prefix.
 * Never regenerate these from the current implementation; they are the semantic baseline.
 */
static int listbounds_frozen_is_ident_char(unsigned char c)
{
	return isalnum(c) || c == '_' || c == '$' || c == '#';
}

static size_t listbounds_frozen_q_quote_prefix_len(const char *text)
{
	if (text == NULL) {
		return 0U;
	}
	if ((text[0] == 'q' || text[0] == 'Q') && text[1] == '\'') {
		return 1U;
	}
	if ((text[0] == 'n' || text[0] == 'N') &&
	    (text[1] == 'q' || text[1] == 'Q') &&
	    text[2] == '\'') {
		return 2U;
	}

	return 0U;
}

static size_t listbounds_frozen_skip_dollar_quoted_span(
	const char *sql,
	size_t index)
{
	size_t delimiter_end;
	size_t delimiter_length;
	size_t pos;

	if (sql[index] != '$' ||
	    (index > 0U &&
	     listbounds_frozen_is_ident_char((unsigned char)sql[index - 1U]))) {
		return index;
	}
	delimiter_end = index + 1U;
	while (isalnum((unsigned char)sql[delimiter_end]) ||
	       sql[delimiter_end] == '_') {
		delimiter_end++;
	}
	if (sql[delimiter_end] != '$') {
		return index;
	}
	delimiter_length = delimiter_end - index + 1U;
	pos = delimiter_end + 1U;
	while (sql[pos] != '\0') {
		if (strncmp(sql + pos, sql + index, delimiter_length) == 0) {
			return pos + delimiter_length;
		}
		pos++;
	}
	return pos;
}

static size_t listbounds_frozen_skip_quoted_or_comment_span(const char *sql, size_t index)
{
	char quote;
	char open_delim;
	char close_delim;
	size_t q_prefix_len;
	size_t pos;

	/* The helper immediately declines every other leading byte. */
	if (sql[index] == '$') {
		pos = listbounds_frozen_skip_dollar_quoted_span(sql, index);
		if (pos != index) {
			return pos;
		}
	}

	q_prefix_len = listbounds_frozen_q_quote_prefix_len(sql + index);
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

static int listbounds_frozen_span_name_byte(unsigned char ch, int dotted, int *state)
{
	if (ch >= 0x80U) return 0;
	if (isspace(ch)) {
		if (*state == 1) *state = 2;
		return 1;
	}
	if (dotted && ch == '.') {
		if (*state == 0) return 0;
		*state = 0;
		return 1;
	}
	if (*state == 2) return 0;
	if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' ||
	    (*state == 1 && ch >= '0' && ch <= '9')) {
		*state = 1;
		return 1;
	}
	return 0;
}

static int listbounds_frozen_find_matching_paren(
	const char *sql,
	size_t open_pos,
	size_t end,
	size_t *out_close)
{
	size_t pos;
	size_t depth;

	if (sql == NULL || out_close == NULL || open_pos >= end || sql[open_pos] != '(') {
		return 0;
	}
	pos = open_pos + 1U;
	depth = 1U;
	while (pos < end) {
		size_t skipped;

		skipped = listbounds_frozen_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')') {
			depth--;
			if (depth == 0U) {
				*out_close = pos;
				return 1;
			}
		}
		pos++;
	}
	return 0;
}

#ifndef LISTBOUNDS_FROZEN_HELPERS_ONLY
/* The two loops below preserve the baseline delimiter scanner operations and
 * their order. Allocation, identifier/value parsing, and object construction
 * are replaced by recording each visited delimiter. Column proof updates are
 * retained. In particular, columns DO NOT track parentheses. These are byte
 * scanner references, not a corrected SQL grammar or public-parser oracle. */
#define LISTBOUNDS_REFERENCE_CAPACITY 512U
typedef struct {
	size_t ends[LISTBOUNDS_REFERENCE_CAPACITY];
	size_t count;
	int span_safe;
} listbounds_frozen_delimiters_t;

static void listbounds_frozen_column_delimiters(
	const char *sql, size_t start, size_t end,
	listbounds_frozen_delimiters_t *out)
{
	size_t pos = start;
	int name_state = 0;
	out->count = 0U;
	out->span_safe = 1;
	while (pos <= end) {
		int at_end;
		size_t skipped;

		at_end = pos == end;
		skipped = !at_end ? listbounds_frozen_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			out->span_safe = 0;
			pos = skipped;
			continue;
		}
		if (!at_end && sql[pos] != ',' &&
		    !listbounds_frozen_span_name_byte((unsigned char)sql[pos], 0, &name_state)) out->span_safe = 0;
		if (at_end || sql[pos] == ',') {
			if (name_state == 0) out->span_safe = 0;
			name_state = 0;
			assert(out->count < LISTBOUNDS_REFERENCE_CAPACITY);
			out->ends[out->count++] = pos;
		}
		pos++;
	}
}

static void listbounds_frozen_value_delimiters(
	const char *sql, size_t start, size_t end,
	listbounds_frozen_delimiters_t *out)
{
	size_t pos = start;
	size_t depth = 0U;
	out->count = 0U;
	out->span_safe = 0;
	while (pos <= end) {
		int at_end;
		size_t skipped;

		at_end = pos == end;
		skipped = !at_end ? listbounds_frozen_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (!at_end && sql[pos] == '(') {
			depth++;
			pos++;
			continue;
		}
		if (!at_end && sql[pos] == ')') {
			if (depth > 0U) {
				depth--;
			}
			pos++;
			continue;
		}
		if (at_end || (depth == 0U && sql[pos] == ',')) {
			assert(out->count < LISTBOUNDS_REFERENCE_CAPACITY);
			out->ends[out->count++] = pos;
		}
		pos++;
	}
}
#endif
