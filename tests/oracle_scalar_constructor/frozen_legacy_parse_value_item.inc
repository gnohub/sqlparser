/* Frozen reference constructor; only function name changed.
 * Independent legacy oracle: never calls any new scalar admission helper. */
static sqlparser_status_t frozen_legacy_parse_value_item(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_value_t *out_value,
	uint32_t *out_lexical_flags,
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
	status = sqlparser_oracle_preprocess_text(out_value->public_sql, state, &out_value->parser_sql, out_error);
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
		status = sqlparser_oracle_value_fill_literal(out_value, out_lexical_flags, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_value_clear(out_value);
			return status;
		}
	}
	if (out_lexical_flags != NULL && strcmp(out_value->public_sql, out_value->parser_sql) == 0)
		*out_lexical_flags |= SQLPARSER_ORACLE_CELL_IDENTITY;
	return SQLPARSER_STATUS_OK;
}
