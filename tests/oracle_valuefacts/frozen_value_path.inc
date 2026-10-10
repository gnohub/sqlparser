/* Frozen listbounds baseline value path. Only the listed private names are
 * renamed. Ordinary allocation, decoder, classification and error behavior
 * are retained verbatim. Shared dependencies outside this closure are unchanged.
 */

static sqlparser_status_t valuefacts_frozen_buffer_reserve_input(
	sqlparser_oracle_buffer_t *buffer,
	const char *input,
	sqlparser_error_t *out_error)
{
	char *next;
	size_t input_len;
	size_t required;

	if (buffer == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"buffer must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	input_len = input != NULL ? strlen(input) : 0U;
	if (input_len == (size_t)-1) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	required = input_len + 1U;
	if (required <= buffer->capacity) {
		return SQLPARSER_STATUS_OK;
	}

	next = (char *)realloc(buffer->data, required);
	if (next == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}

	buffer->data = next;
	buffer->capacity = required;
	buffer->data[buffer->len] = '\0';
	return SQLPARSER_STATUS_OK;
}

static int valuefacts_frozen_text_is_integer_literal(const char *text)
{
	size_t pos;

	if (text == NULL || text[0] == '\0') {
		return 0;
	}
	pos = (text[0] == '+' || text[0] == '-') ? 1U : 0U;
	if (!isdigit((unsigned char)text[pos])) {
		return 0;
	}
	while (isdigit((unsigned char)text[pos])) {
		pos++;
	}
	return text[pos] == '\0';
}

static int valuefacts_frozen_text_is_float_literal(const char *text)
{
	size_t pos;
	int saw_digit;
	int saw_dot;

	if (text == NULL || text[0] == '\0') {
		return 0;
	}
	pos = (text[0] == '+' || text[0] == '-') ? 1U : 0U;
	saw_digit = 0;
	saw_dot = 0;
	while (text[pos] != '\0') {
		if (isdigit((unsigned char)text[pos])) {
			saw_digit = 1;
			pos++;
			continue;
		}
		if (text[pos] == '.' && !saw_dot) {
			saw_dot = 1;
			pos++;
			continue;
		}
		return 0;
	}
	return saw_digit && saw_dot;
}

static int valuefacts_frozen_ascii_text_equal(const char *left, const char *right)
{
	size_t index;

	if (left == NULL || right == NULL) {
		return 0;
	}
	for (index = 0U; left[index] != '\0' && right[index] != '\0'; index++) {
		if (tolower((unsigned char)left[index]) != tolower((unsigned char)right[index])) {
			return 0;
		}
	}
	return left[index] == '\0' && right[index] == '\0';
}

static sqlparser_status_t valuefacts_frozen_unquote_string_literal(
	const char *text,
	char **out_value,
	uint32_t *out_lexical_flags,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t len;
	size_t pos;
	int ordinary = 1;

	if (out_value == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "literal output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_value = NULL;
	len = text != NULL ? strlen(text) : 0U;
	if (len < 2U || text[0] != '\'' || text[len - 1U] != '\'') {
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	memset(&out, 0, sizeof(out));
	pos = 1U;
	while (pos + 1U < len) {
		char value = text[pos];
		int doubled = value == '\'' && pos + 1U < len - 1U && text[pos + 1U] == '\'';
		/* Record exactly the legacy decoder's fact: a doubled quote bypasses
		 * the ordinary-character check, but every other quote/control does not. */
		if (!doubled && (value == '\'' || value == '\\' ||
		    (unsigned char)value < 32U || (unsigned char)value == 127U))
			ordinary = 0;
		/* This local buffer starts empty and only grows by decoded bytes.
		 * Reserve at exactly the old append_char capacity crossings, including
		 * its initial 256-byte allocation and subsequent doubling. Preserve the
		 * same byte/NUL stores and failure cleanup; empty strings still finish
		 * below and allocate there. No ownership or allocation schedule changes. */
		if (out.capacity == 0U || out.len >= out.capacity - 1U) {
			if (sqlparser_oracle_buffer_reserve(&out, 1U, out_error) != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_buffer_release(&out);
				return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
			}
		}
		out.data[out.len++] = value;
		out.data[out.len] = '\0';
		pos += doubled ? 2U : 1U;
	}
	if (sqlparser_oracle_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_value = sqlparser_oracle_buffer_take(&out);
	if (*out_value != NULL && ordinary && out_lexical_flags != NULL)
		*out_lexical_flags |= SQLPARSER_ORACLE_CELL_ORDINARY_STRING;
	return *out_value != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t valuefacts_frozen_value_fill_literal(
	sqlparser_dialect_multi_insert_value_t *value,
	uint32_t *out_lexical_flags,
	sqlparser_error_t *out_error)
{
	long long integer_value;
	char *endptr;

	if (value == NULL || value->public_sql == NULL) {
		return SQLPARSER_STATUS_OK;
	}
	if (valuefacts_frozen_ascii_text_equal(value->public_sql, "null")) {
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_NULL;
		return SQLPARSER_STATUS_OK;
	}
	if (value->public_sql[0] == '\'') {
		if (valuefacts_frozen_unquote_string_literal(value->public_sql, &value->literal_string_value, out_lexical_flags, out_error) != SQLPARSER_STATUS_OK) {
			return SQLPARSER_STATUS_OK;
		}
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_STRING;
		value->literal.string_value = value->literal_string_value;
		return SQLPARSER_STATUS_OK;
	}
	if (valuefacts_frozen_text_is_integer_literal(value->public_sql)) {
		endptr = NULL;
		integer_value = strtoll(value->public_sql, &endptr, 10);
		if (endptr != NULL && *endptr == '\0') {
			value->has_literal = 1;
			value->literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
			value->literal.integer_value = integer_value;
		}
		return SQLPARSER_STATUS_OK;
	}
	if (valuefacts_frozen_text_is_float_literal(value->public_sql)) {
		value->literal_float_value = sqlparser_strdup(value->public_sql);
		if (value->literal_float_value == NULL) {
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		value->has_literal = 1;
		value->literal.kind = SQLPARSER_LITERAL_KIND_FLOAT;
		value->literal.float_value = value->literal_float_value;
	}
	return SQLPARSER_STATUS_OK;
}

/* A complete-token proof for preprocessing identity, not a replacement for
 * literal classification. Unknown syntax stays with preprocess_text, including
 * prefixed/alternative strings, binds, comments and compound expressions. */
static int valuefacts_frozen_value_identity_kind(const char *text, size_t *out_length)
{
	static const char *const sqlvalue_keywords[] = {
		"current_date", "current_time", "current_timestamp",
		"localtime", "localtimestamp", "current_role", "current_user",
		"session_user", "user", "current_catalog", "current_schema"
	};
	size_t pos, index;
	int saw_digit, saw_dot;

	if (text == NULL || text[0] == '\0') return 0;
	if (text[0] == '\'') {
		for (pos = 1U; text[pos] != '\0'; pos++) {
			unsigned char c = (unsigned char)text[pos];
			if (c < 32U || c == 127U || c == '\\') return 0;
			if (c != '\'') continue;
			if (text[pos + 1U] == '\'') {
				pos++;
				continue;
			}
			if (text[pos + 1U] != '\0') return 0;
			*out_length = pos + 1U;
			return 1; /* Exactly one ordinary String token. */
		}
		return 0;
	}
	if ((text[0] >= '0' && text[0] <= '9') ||
	    text[0] == '+' || text[0] == '-' || text[0] == '.') {
		pos = (text[0] == '+' || text[0] == '-') ? 1U : 0U;
		saw_digit = saw_dot = 0;
		for (; text[pos] != '\0'; pos++) {
			if (text[pos] >= '0' && text[pos] <= '9') saw_digit = 1;
			else if (text[pos] == '.' && !saw_dot) saw_dot = 1;
			else return 0;
		}
		if (!saw_digit) return 0;
		*out_length = pos;
		return 2;
	}
	if (valuefacts_frozen_ascii_text_equal(text, "null")) {
		*out_length = 4U;
		return 2;
	}
	/* These are the complete bare SQLValueFunction token alternatives in the
 * native grammar. Precision arguments and other function forms still use the
 * legacy path. None contains a rewrite keyword, bind, quote or DB-link. */
	for (index = 0U; index < sizeof(sqlvalue_keywords) / sizeof(sqlvalue_keywords[0]); index++) {
		if (valuefacts_frozen_ascii_text_equal(text, sqlvalue_keywords[index])) {
			*out_length = strlen(sqlvalue_keywords[index]);
			return 2;
		}
	}
	return 0;
}

static sqlparser_status_t valuefacts_frozen_preprocess_value_text(
	const char *input_sql,
	sqlparser_oracle_state_t *state,
	char **out_sql,
	int *out_span_safe,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	sqlparser_status_t status;
	size_t length = 0U;
	int identity_kind = valuefacts_frozen_value_identity_kind(input_sql, &length);

	if (out_span_safe != NULL) *out_span_safe = identity_kind != 0;
	if (identity_kind == 0)
		return sqlparser_oracle_preprocess_text(input_sql, state, out_sql, out_error);
	memset(&out, 0, sizeof(out));
	/* Preserve the legacy first allocation (realloc(NULL, strlen + 1)), its
 * error, and its position before any state update. The complete identity
 * proof establishes that the old append/finish calls cannot allocate again. */
	status = valuefacts_frozen_buffer_reserve_input(&out, input_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) return status;
	memcpy(out.data, input_sql, length + 1U);
	out.len = length;
	if (identity_kind == 1) state->national_literals.literal_count++;
	*out_sql = sqlparser_oracle_buffer_take(&out);
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t valuefacts_frozen_parse_value_item_with_span_proof(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_value_t *out_value,
	uint32_t *out_lexical_flags,
	int *out_span_safe,
	sqlparser_error_t *out_error)
{
	sqlparser_bind_token_t bind_token;
	const char *key_start;
	size_t key_len;
	size_t sql_len;
	sqlparser_bind_kind_t bind_kind;
	size_t occurrence_before;
	sqlparser_status_t status;

	if (out_value == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	if (out_lexical_flags != NULL) *out_lexical_flags = 0U;
	status = sqlparser_oracle_trimmed_slice_dup(sql, start, end, &out_value->public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	occurrence_before = state->bind_occurrence_count;
	status = valuefacts_frozen_preprocess_value_text(out_value->public_sql, state, &out_value->parser_sql, out_span_safe, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_value_clear(out_value);
		return status;
	}
	sql_len = strlen(out_value->public_sql);
	if (sqlparser_bind_token_exact(
		    SQLPARSER_DIALECT_ORACLE,
		    out_value->public_sql,
		    sql_len,
		    0U,
		    sql_len,
		    &bind_token) &&
	    state->bind_occurrence_count == occurrence_before + 1U) {
		bind_kind = bind_token.kind;
		key_start = bind_token.key_length > 0U ?
			out_value->public_sql + bind_token.key_start :
			NULL;
		key_len = bind_token.key_length;
		out_value->has_bind = 1;
		out_value->bind_kind = bind_kind;
		if (bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && key_start == NULL) {
			(void)snprintf(out_value->bind, sizeof(out_value->bind), "%lu", (unsigned long)(occurrence_before + 1U));
		} else {
			if (key_len >= sizeof(out_value->bind)) {
				sqlparser_oracle_value_clear(out_value);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind key is too long");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			memcpy(out_value->bind, key_start, key_len);
			out_value->bind[key_len] = '\0';
		}
		if (sql_len >= sizeof(out_value->bind_sql)) {
			sqlparser_oracle_value_clear(out_value);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		memcpy(out_value->bind_sql, out_value->public_sql, sql_len);
		out_value->bind_sql[sql_len] = '\0';
		out_value->bind_position = occurrence_before + 1U;
		out_value->has_bind_position = 1;
	} else {
		status = valuefacts_frozen_value_fill_literal(out_value, out_lexical_flags, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_value_clear(out_value);
			return status;
		}
	}
	if (out_lexical_flags != NULL && strcmp(out_value->public_sql, out_value->parser_sql) == 0)
		*out_lexical_flags |= SQLPARSER_ORACLE_CELL_IDENTITY;
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t valuefacts_frozen_parse_value_item(
	const char *sql, size_t start, size_t end, sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_value_t *out_value,
	uint32_t *out_lexical_flags, sqlparser_error_t *out_error)
{
	return valuefacts_frozen_parse_value_item_with_span_proof(
		sql, start, end, state, out_value, out_lexical_flags, NULL, out_error);
}
