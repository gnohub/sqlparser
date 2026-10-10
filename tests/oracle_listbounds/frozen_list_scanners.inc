/* Frozen list materialization bodies; only function names
 * and references to the separately frozen lexical helpers are renamed.
 * Unchanged item construction, trimming and cleanup helpers remain shared.
 */

static sqlparser_status_t listbounds_frozen_parse_column_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dialect_multi_insert_column_t **out_columns,
	size_t *out_count,
	int *span_safe,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_column_t *columns;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;
	int name_state = 0;

	if (out_columns == NULL || out_count == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "column list outputs must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_columns = NULL;
	*out_count = 0U;
	columns = NULL;
	count = 0U;
	capacity = 0U;
	item_start = start;
	pos = start;
	while (pos <= end) {
		int at_end;
		size_t skipped;

		at_end = pos == end;
		skipped = !at_end ? listbounds_frozen_skip_quoted_or_comment_span(sql, pos) : pos;
		if (skipped > pos) {
			*span_safe = 0;
			pos = skipped;
			continue;
		}
		if (!at_end && sql[pos] != ',' &&
		    !listbounds_frozen_span_name_byte((unsigned char)sql[pos], 0, &name_state)) *span_safe = 0;
		if (at_end || sql[pos] == ',') {
			size_t item_end;
			sqlparser_dialect_multi_insert_column_t *next;

			if (name_state == 0) *span_safe = 0;
			name_state = 0;
			if (count == capacity) {
				size_t next_capacity;

				next_capacity = capacity == 0U ? 4U : capacity * 2U;
				if (next_capacity < capacity) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				next = (sqlparser_dialect_multi_insert_column_t *)realloc(columns, next_capacity * sizeof(*next));
				if (next == NULL) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				columns = next;
				capacity = next_capacity;
			}
			memset(&columns[count], 0, sizeof(columns[count]));
			item_end = sqlparser_oracle_trim_right(sql, item_start, pos);
			item_start = sqlparser_oracle_trim_left(sql, item_start, item_end);
			if (item_start >= item_end) {
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "column list contains an empty item");
				goto fail;
			}
			if (sqlparser_oracle_trimmed_slice_dup(sql, item_start, item_end, &columns[count].sql, out_error) != SQLPARSER_STATUS_OK ||
			    sqlparser_oracle_identifier_from_sql(sql, item_start, item_end, &columns[count].name, out_error) != SQLPARSER_STATUS_OK) {
				free(columns[count].name);
				free(columns[count].sql);
				goto fail;
			}
			count++;
			item_start = pos + 1U;
		}
		pos++;
	}

	*out_columns = columns;
	*out_count = count;
	return SQLPARSER_STATUS_OK;

fail:
	if (columns != NULL) {
		size_t index;

		for (index = 0U; index < count; index++) {
			free(columns[index].name);
			free(columns[index].sql);
		}
		free(columns);
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t listbounds_frozen_parse_value_list(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_t *multi,
	sqlparser_dialect_multi_insert_value_t **out_values,
	size_t *out_count,
	int *span_safe,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_value_t *values;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;
	size_t depth;

	if (out_values == NULL || out_count == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value list arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_values = NULL;
	*out_count = 0U;
	values = NULL;
	count = 0U;
	capacity = 0U;
	item_start = start;
	pos = start;
	depth = 0U;
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
			sqlparser_dialect_multi_insert_value_t *next;
			uint32_t lexical_flags = 0U;
			int cell_span_safe = 0;

			if (count == capacity) {
				size_t next_capacity;

				next_capacity = capacity == 0U ? 4U : capacity * 2U;
				if (next_capacity < capacity) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				next = (sqlparser_dialect_multi_insert_value_t *)realloc(values, next_capacity * sizeof(*next));
				if (next == NULL) {
					sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
					goto fail;
				}
				values = next;
				capacity = next_capacity;
			}
			if (sqlparser_oracle_parse_value_item_with_span_proof(sql, item_start, pos, state, &values[count],
				multi->oracle_spans_complete ? &lexical_flags : NULL, &cell_span_safe, out_error) != SQLPARSER_STATUS_OK) {
				goto fail;
			}
			if (!cell_span_safe) *span_safe = 0;
			sqlparser_oracle_record_cell_span(multi, sql, item_start, pos, lexical_flags, span_safe);
			count++;
			item_start = pos + 1U;
		}
		pos++;
	}

	*out_values = values;
	*out_count = count;
	return SQLPARSER_STATUS_OK;

fail:
	if (values != NULL) {
		size_t index;

		for (index = 0U; index < count; index++) {
			sqlparser_oracle_value_clear(&values[index]);
		}
		free(values);
	}
	return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t listbounds_frozen_parse_multi_insert_into(
	const char *sql,
	size_t *io_pos,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_t *multi,
	const char *condition_public_sql,
	const char *condition_parser_sql,
	int is_else,
	size_t condition_group_id,
	int *span_safe,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_branch_t branch;
	size_t pos;
	size_t relation_start;
	size_t relation_end;
	size_t columns_open;
	size_t columns_close;
	size_t values_pos;
	size_t values_open;
	size_t values_close;
	sqlparser_status_t status;
	int relation_name_state = 0;

	memset(&branch, 0, sizeof(branch));
	pos = sqlparser_oracle_span_trim_left(sql, *io_pos, end, span_safe);
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "into")) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected INTO");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	pos = sqlparser_oracle_span_trim_left(sql, pos + strlen("into"), end, span_safe);
	relation_start = pos;
	columns_open = end;
	values_pos = end;
	while (pos < end) {
		size_t skipped;

		skipped = listbounds_frozen_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			*span_safe = 0;
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			columns_open = pos;
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(sql, pos, "values")) {
			values_pos = pos;
			break;
		}
		if (!listbounds_frozen_span_name_byte((unsigned char)sql[pos], 1, &relation_name_state))
			*span_safe = 0;
		pos++;
	}
	if (relation_name_state == 0) *span_safe = 0;
	if (columns_open == end && values_pos == end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	relation_end = columns_open != end ? columns_open : values_pos;
	status = sqlparser_oracle_relation_from_sql(sql, relation_start, relation_end, &branch.relation, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (columns_open != end) {
		if (!listbounds_frozen_find_matching_paren(sql, columns_open, end, &columns_close)) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = listbounds_frozen_parse_column_list(
			sql,
			columns_open + 1U,
			columns_close,
			&branch.columns,
			&branch.column_count,
			span_safe,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			return status;
		}
		pos = sqlparser_oracle_span_trim_left(sql, columns_close + 1U, end, span_safe);
	} else {
		pos = values_pos;
	}
	if (!sqlparser_oracle_ascii_word_equal(sql, pos, "values")) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected VALUES");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (pos <= UINT32_MAX) branch.oracle_values_position = (uint32_t)pos;
	else *span_safe = 0;
	pos = sqlparser_oracle_span_trim_left(sql, pos + strlen("values"), end, span_safe);
	if (pos >= end || sql[pos] != '(') {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list expected '('");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	values_open = pos;
	if (!listbounds_frozen_find_matching_paren(sql, values_open, end, &values_close)) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list is not closed");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (multi->oracle_spans_complete && multi->oracle_span_count < UINT32_MAX)
		branch.oracle_span_base = (uint32_t)multi->oracle_span_count;
	status = listbounds_frozen_parse_value_list(
		sql,
		values_open + 1U,
		values_close,
		state,
		multi,
		&branch.cells,
		&branch.cell_count,
		span_safe,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		return status;
	}
	if (condition_public_sql != NULL) {
		branch.condition_public_sql = sqlparser_strdup(condition_public_sql);
		branch.condition_parser_sql = sqlparser_strdup(condition_parser_sql != NULL ? condition_parser_sql : condition_public_sql);
		if (branch.condition_public_sql == NULL || branch.condition_parser_sql == NULL) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		branch.has_condition = 1;
	}
	branch.is_else = is_else;
	branch.condition_group_id = condition_group_id;
	if (condition_public_sql != NULL || is_else || condition_group_id != 0U) {
		multi->oracle_spans_identity = 0;
		*span_safe = 0;
	}
	status = sqlparser_oracle_multi_insert_add_branch(multi, &branch, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		return status;
	}
	*io_pos = values_close + 1U;
	return SQLPARSER_STATUS_OK;
}
