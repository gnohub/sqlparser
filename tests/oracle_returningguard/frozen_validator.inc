/* Frozen reference validator and all private helpers.
 * Renames only; never include the current implementation implementation here. */
static int frozen_sqlparser_returning_into_is_ident_char(unsigned char value)
{
	return isalnum(value) || value == '_' || value == '$' ||
		value == '#' || value >= 0x80U;
}

static size_t frozen_sqlparser_returning_into_word_end(
	const char *sql,
	size_t start)
{
	size_t end;

	end = start;
	while (frozen_sqlparser_returning_into_is_ident_char(
		       (unsigned char)sql[end])) {
		end++;
	}
	return end;
}

static int frozen_sqlparser_returning_into_word_equal(
	const char *sql,
	size_t start,
	size_t end,
	const char *word)
{
	size_t index;
	size_t length;

	length = word != NULL ? strlen(word) : 0U;
	if (sql == NULL || word == NULL || end - start != length ||
	    (start > 0U && frozen_sqlparser_returning_into_is_ident_char(
				  (unsigned char)sql[start - 1U])) ||
	    frozen_sqlparser_returning_into_is_ident_char((unsigned char)sql[end])) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (tolower((unsigned char)sql[start + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static int frozen_sqlparser_returning_into_word_at(
	const char *sql,
	size_t start,
	const char *word)
{
	size_t index;
	size_t length;

	if (sql == NULL || word == NULL ||
	    (start > 0U && frozen_sqlparser_returning_into_is_ident_char(
			  (unsigned char)sql[start - 1U]))) {
		return 0;
	}
	length = strlen(word);
	for (index = 0U; index < length; index++) {
		if (sql[start + index] == '\0' ||
		    tolower((unsigned char)sql[start + index]) !=
			    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return !frozen_sqlparser_returning_into_is_ident_char(
		(unsigned char)sql[start + length]);
}

static size_t frozen_sqlparser_returning_into_skip_trivia(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t start)
{
	size_t next;

	while (sql[start] != '\0') {
		while (isspace((unsigned char)sql[start])) {
			start++;
		}
		if (!((sql[start] == '-' && sql[start + 1U] == '-') ||
		      (sql[start] == '/' && sql[start + 1U] == '*'))) {
			break;
		}
		next = frozen_skip_quoted_or_comment(
			dialect, sql, start);
		if (next <= start) {
			break;
		}
		start = next;
	}
	return start;
}

static int frozen_sqlparser_returning_into_statement_context(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t keyword_start,
	size_t *out_statement_index)
{
	size_t depth;
	size_t position;
	size_t statement_index;
	int statement_is_dml;
	int statement_word_seen;

	depth = 0U;
	position = 0U;
	statement_index = 0U;
	statement_is_dml = 0;
	statement_word_seen = 0;
	while (position < keyword_start) {
		size_t next;
		size_t word_end;

		next = frozen_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			position++;
			continue;
		}
		if (sql[position] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			position++;
			continue;
		}
		if (depth == 0U && sql[position] == ';') {
			if (statement_word_seen) {
				statement_index++;
			}
			statement_is_dml = 0;
			statement_word_seen = 0;
			position++;
			continue;
		}
		if (!frozen_sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) ||
		    isdigit((unsigned char)sql[position])) {
			position++;
			continue;
		}
		word_end = frozen_sqlparser_returning_into_word_end(sql, position);
		if (depth == 0U && !statement_word_seen) {
			statement_word_seen = 1;
			statement_is_dml =
				frozen_sqlparser_returning_into_word_equal(
					sql, position, word_end, "insert") ||
				frozen_sqlparser_returning_into_word_equal(
					sql, position, word_end, "update") ||
				frozen_sqlparser_returning_into_word_equal(
					sql, position, word_end, "delete");
		}
		position = word_end;
	}
	if (depth != 0U || !statement_is_dml) {
		return 0;
	}
	if (out_statement_index != NULL) {
		*out_statement_index = statement_index;
	}
	return 1;
}

static size_t frozen_sqlparser_returning_into_bind_end(
	const char *sql,
	size_t start)
{
	size_t end;

	if (sql[start] != ':' || sql[start + 1U] == '\0' ||
	    sql[start + 1U] == ':' || sql[start + 1U] == '=') {
		return start;
	}
	end = start + 1U;
	if (isdigit((unsigned char)sql[end])) {
		while (isdigit((unsigned char)sql[end])) {
			end++;
		}
		return end;
	}
	if (!isalpha((unsigned char)sql[end]) && sql[end] != '_' &&
	    (unsigned char)sql[end] < 0x80U) {
		return start;
	}
	end++;
	while (frozen_sqlparser_returning_into_is_ident_char(
		       (unsigned char)sql[end])) {
		end++;
	}
	return end;
}

int frozen_sqlparser_dialect_returning_into_receiver_is_bind(
	sqlparser_dialect_t dialect,
	const char *sql)
{
	size_t end;
	size_t skipped_end;
	size_t start;

	if (sql == NULL) {
		return 0;
	}
	start = frozen_sqlparser_returning_into_skip_trivia(dialect, sql, 0U);
	end = frozen_sqlparser_returning_into_bind_end(sql, start);
	skipped_end = frozen_sqlparser_returning_into_skip_trivia(dialect, sql, end);
	return end > start && sql[skipped_end] == '\0';
}

static int frozen_sqlparser_returning_into_bulk_collect_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t start)
{
	size_t position;

	if (!frozen_sqlparser_returning_into_word_at(sql, start, "bulk")) {
		return 0;
	}
	position = frozen_sqlparser_returning_into_skip_trivia(
		dialect, sql, start + strlen("bulk"));
	if (!frozen_sqlparser_returning_into_word_at(sql, position, "collect")) {
		return 0;
	}
	position = frozen_sqlparser_returning_into_skip_trivia(
		dialect, sql, position + strlen("collect"));
	return frozen_sqlparser_returning_into_word_at(sql, position, "into");
}

int frozen_sqlparser_dialect_returning_into_clause_at(
	sqlparser_dialect_t dialect,
	const char *sql,
	size_t keyword_start,
	int allow_return_keyword,
	sqlparser_dialect_returning_into_clause_t *out_clause)
{
	sqlparser_dialect_returning_into_clause_t clause;
	size_t depth;
	size_t keyword_length;
	size_t position;
	size_t sink_end;
	size_t sink_start;
	size_t sink_count;
	size_t target_count;
	int found_into;
	int target_has_code;

	memset(&clause, 0, sizeof(clause));
	if (sql == NULL || out_clause == NULL) {
		return 0;
	}
	if (frozen_sqlparser_returning_into_word_at(
		    sql, keyword_start, "returning")) {
		keyword_length = strlen("returning");
	} else if (allow_return_keyword &&
		   frozen_sqlparser_returning_into_word_at(
			   sql, keyword_start, "return")) {
		keyword_length = strlen("return");
		clause.uses_return_keyword = 1;
	} else {
		return 0;
	}
	if (!frozen_sqlparser_returning_into_statement_context(
		    dialect,
		    sql,
		    keyword_start,
		    &clause.statement_index)) {
		return 0;
	}
	clause.keyword_end = keyword_start + keyword_length;
	depth = 0U;
	position = clause.keyword_end;
	found_into = 0;
	target_count = 0U;
	target_has_code = 0;
	while (sql[position] != '\0') {
		size_t next;
		size_t word_end;

		next = frozen_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			if (!((sql[position] == '-' && sql[position + 1U] == '-') ||
			      (sql[position] == '/' && sql[position + 1U] == '*'))) {
				target_has_code = 1;
			}
			position = next;
			continue;
		}
		if (sql[position] == '(') {
			depth++;
			target_has_code = 1;
			position++;
			continue;
		}
		if (sql[position] == ')') {
			if (depth == 0U) {
				return 0;
			}
			depth--;
			target_has_code = 1;
			position++;
			continue;
		}
		if (depth == 0U && sql[position] == ';') {
			return 0;
		}
		if (depth == 0U && sql[position] == ',') {
			if (!target_has_code) {
				return 0;
			}
			target_count++;
			target_has_code = 0;
			position++;
			continue;
		}
		if (depth == 0U &&
		    frozen_sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) &&
		    !isdigit((unsigned char)sql[position])) {
			if (frozen_sqlparser_returning_into_bulk_collect_at(
				    dialect, sql, position)) {
				return 0;
			}
			word_end = frozen_sqlparser_returning_into_word_end(
				sql, position);
			if (frozen_sqlparser_returning_into_word_equal(
				    sql, position, word_end, "into")) {
				if (!target_has_code) {
					return 0;
				}
				target_count++;
				clause.into_start = position;
				found_into = 1;
				break;
			}
			position = word_end;
			target_has_code = 1;
			continue;
		}
		if (!isspace((unsigned char)sql[position])) {
			target_has_code = 1;
		}
		position++;
	}
	if (!found_into || target_count == 0U) {
		return 0;
	}
	sink_start = frozen_sqlparser_returning_into_skip_trivia(
		dialect,
		sql,
		clause.into_start + strlen("into"));
	sink_end = frozen_sqlparser_returning_into_bind_end(sql, sink_start);
	if (sink_end == sink_start) {
		return 0;
	}
	sink_count = 0U;
	for (;;) {
		sink_count++;
		position = frozen_sqlparser_returning_into_skip_trivia(
			dialect, sql, sink_end);
		if (sql[position] != ',') {
			break;
		}
		sink_start = frozen_sqlparser_returning_into_skip_trivia(
			dialect, sql, position + 1U);
		sink_end = frozen_sqlparser_returning_into_bind_end(sql, sink_start);
		if (sink_end == sink_start) {
			return 0;
		}
	}
	if ((sql[position] != '\0' && sql[position] != ';') ||
	    sink_count != target_count) {
		return 0;
	}
	clause.pair_count = target_count;
	*out_clause = clause;
	return 1;
}

sqlparser_status_t frozen_sqlparser_dialect_returning_into_validate(
	sqlparser_dialect_t dialect,
	const char *sql,
	int allow_return_keyword,
	int allow_plain_returning,
	sqlparser_error_t *out_error)
{
	size_t position;

	if (sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	position = 0U;
	while (sql[position] != '\0') {
		size_t next;
		size_t word_end;

		next = frozen_skip_quoted_or_comment(
			dialect, sql, position);
		if (next > position) {
			position = next;
			continue;
		}
		if (sql[position] == ':') {
			next = frozen_sqlparser_returning_into_bind_end(sql, position);
			if (next > position) {
				position = next;
				continue;
			}
		}
		if (!frozen_sqlparser_returning_into_is_ident_char(
			    (unsigned char)sql[position]) ||
		    isdigit((unsigned char)sql[position])) {
			position++;
			continue;
		}
		word_end = frozen_sqlparser_returning_into_word_end(sql, position);
		if (frozen_sqlparser_returning_into_word_equal(
			    sql, position, word_end, "returning") ||
		    (allow_return_keyword &&
		     frozen_sqlparser_returning_into_word_equal(
			     sql, position, word_end, "return"))) {
			sqlparser_dialect_returning_into_clause_t clause;
			size_t depth;

			if (!frozen_sqlparser_dialect_returning_into_clause_at(
				    dialect,
				    sql,
				    position,
				    allow_return_keyword,
				    &clause)) {
				if (allow_plain_returning &&
				    frozen_sqlparser_returning_into_word_equal(
					    sql,
					    position,
					    word_end,
					    "returning")) {
					position = word_end;
					continue;
				}
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_UNSUPPORTED,
					"unsupported RETURN/RETURNING ... INTO syntax");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			depth = 0U;
			position = clause.into_start + strlen("into");
			while (sql[position] != '\0') {
				next = frozen_skip_quoted_or_comment(
					dialect, sql, position);
				if (next > position) {
					position = next;
					continue;
				}
				if (sql[position] == '(') {
					depth++;
				} else if (sql[position] == ')' && depth > 0U) {
					depth--;
				} else if (sql[position] == ';' && depth == 0U) {
					position++;
					break;
				}
				position++;
			}
			continue;
		}
		position = word_end;
	}
	return SQLPARSER_STATUS_OK;
}
