/* Frozen accepted statementend constructor paths with complete collector
 * timing. Only function/call names change. The lexical closure is separately
 * frozen. Source SHA-256: b201312887bf893929daa45c3dff380595267ccf2d8163997af15804739928fc
 */

static int constructorgates_frozen_list_bounds_append(
	sqlparser_oracle_list_bounds_t *bounds, size_t end)
{
	if (bounds->count >= SQLPARSER_ORACLE_LIST_BOUND_CAPACITY ||
	    end < bounds->start ||
	    (bounds->count != 0U && end <= bounds->ends[bounds->count - 1U])) {
		bounds->count = 0U;
		return 0;
	}
	bounds->ends[bounds->count++] = end;
	return 1;
}

static int constructorgates_frozen_find_matching_paren_with_bounds(
	const char *sql,
	size_t open_pos,
	size_t end,
	size_t *out_close,
	int kind,
	sqlparser_oracle_list_bounds_t *bounds)
{
	size_t pos;
	size_t depth;
	int collect;
	int name_state = 0;

	if (bounds != NULL) {
		bounds->complete = 0;
		bounds->count = 0U;
		bounds->start = 0U;
		bounds->close = 0U;
		bounds->kind = kind;
	}
	if (sql == NULL || out_close == NULL || open_pos >= end || sql[open_pos] != '(') {
		return 0;
	}
	collect = bounds != NULL &&
		(kind == SQLPARSER_ORACLE_LIST_COLUMNS || kind == SQLPARSER_ORACLE_LIST_VALUES);
	pos = open_pos + 1U;
	if (bounds != NULL) bounds->start = pos;
	depth = 1U;
	while (pos < end) {
		size_t skipped;

		skipped = listbounds_frozen_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			/* Keep every original helper jump, even when it crosses end.
			 * Only ordinary quoted VALUES text is admitted by this cache. */
			if (collect && (kind == SQLPARSER_ORACLE_LIST_COLUMNS ||
			    sql[pos] != '\'' || skipped > end)) {
				collect = 0;
				bounds->count = 0U;
			}
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			depth++;
		} else if (sql[pos] == ')') {
			depth--;
			if (depth == 0U) {
				*out_close = pos;
				if (collect && kind == SQLPARSER_ORACLE_LIST_COLUMNS && name_state == 0) {
					collect = 0;
					bounds->count = 0U;
				}
				if (collect && constructorgates_frozen_list_bounds_append(bounds, pos)) {
					bounds->close = pos;
					bounds->complete = 1;
				}
				return 1;
			}
		}
		if (collect && kind == SQLPARSER_ORACLE_LIST_COLUMNS) {
			/* The legacy column splitter has no parenthesis-depth condition.
			 * Admit only wholly safe names, so its proof would stay unchanged. */
			if (sql[pos] == ',') {
				if (name_state == 0 || !constructorgates_frozen_list_bounds_append(bounds, pos))
					collect = 0;
				name_state = 0;
			} else if (!listbounds_frozen_span_name_byte((unsigned char)sql[pos], 0, &name_state)) {
				collect = 0;
			}
			if (!collect) bounds->count = 0U;
		} else if (collect) {
			unsigned char ch = (unsigned char)sql[pos];
			/* Complex quote/comment forms and locale-sensitive unquoted text
			 * retain the complete original second scan. Round depth is shared. */
			if (ch >= 0x80U || ch == '$' || ch == '[' || ch == ']' ||
			    ch == '{' || ch == '}' || ch == '\\' || ch == '`' || ch == '"') {
				collect = 0;
				bounds->count = 0U;
			} else if (ch == ',' && depth == 1U) {
				collect = constructorgates_frozen_list_bounds_append(bounds, pos);
			}
		}
		pos++;
	}
	if (bounds != NULL) bounds->count = 0U;
	return 0;
}

static sqlparser_status_t constructorgates_frozen_identifier_from_sql(
	const char *sql,
	size_t start,
	size_t end,
	char **out_name,
	sqlparser_error_t *out_error)
{
	sqlparser_oracle_buffer_t out;
	size_t pos;

	if (out_name == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "identifier output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_name = NULL;
	start = sqlparser_oracle_trim_left(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "identifier must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (sql[start] != '"') {
		return sqlparser_oracle_trimmed_slice_dup(sql, start, end, out_name, out_error);
	}

	memset(&out, 0, sizeof(out));
	pos = start + 1U;
	while (pos < end) {
		if (sql[pos] == '"') {
			if (pos + 1U < end && sql[pos + 1U] == '"') {
				if (sqlparser_oracle_buffer_append_char(&out, '"', out_error) != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_buffer_release(&out);
					return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
				}
				pos += 2U;
				continue;
			}
			break;
		}
		if (sqlparser_oracle_buffer_append_char(&out, sql[pos], out_error) != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_buffer_release(&out);
			return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
		}
		pos++;
	}
	if (sqlparser_oracle_buffer_finish(&out, out_error) != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&out);
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}
	*out_name = sqlparser_oracle_buffer_take(&out);
	return *out_name != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}

static sqlparser_status_t constructorgates_frozen_relation_from_sql(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dialect_multi_insert_relation_t *relation,
	sqlparser_error_t *out_error)
{
	size_t part_start[3];
	size_t part_end[3];
	size_t count;
	size_t pos;
	size_t object_end;
	size_t link_start;
	size_t link_end;
	int has_link;
	sqlparser_status_t status;

	if (relation == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "relation must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(relation, 0, sizeof(*relation));
	start = sqlparser_oracle_trim_left(sql, start, end);
	end = sqlparser_oracle_trim_right(sql, start, end);
	if (start >= end) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "INSERT target relation must not be empty");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (sqlparser_oracle_trimmed_slice_dup(sql, start, end, &relation->sql, out_error) != SQLPARSER_STATUS_OK) {
		return out_error != NULL ? out_error->code : SQLPARSER_STATUS_NO_MEMORY;
	}

	object_end = end;
	link_start = 0U;
	link_end = 0U;
	has_link = 0;
	count = 0U;
	part_start[count] = start;
	pos = start;
	while (pos < end) {
		size_t skipped;

		skipped = listbounds_frozen_skip_quoted_or_comment_span(sql, pos);
		if (skipped > pos) {
			pos = skipped;
			continue;
		}
		if (sql[pos] == '@') {
			object_end = sqlparser_oracle_trim_right(sql, start, pos);
			link_start = sqlparser_oracle_trim_left(sql, pos + 1U, end);
			link_end = sqlparser_oracle_identifier_token_end(sql, link_start);
			if (object_end <= start || link_end <= link_start ||
			    link_end != end ||
			    sqlparser_oracle_public_identifier_is_empty(
				    sql, link_start, link_end)) {
				sqlparser_oracle_relation_clear(relation);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "invalid Oracle database link name");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			has_link = 1;
			break;
		}
		if (sql[pos] == '.') {
			if (count >= 2U) {
				sqlparser_oracle_relation_clear(relation);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "Oracle relation has too many name parts");
				return SQLPARSER_STATUS_UNSUPPORTED;
			}
			part_end[count] = pos;
			count++;
			part_start[count] = pos + 1U;
		}
		pos++;
	}
	part_end[count] = object_end;
	count++;

	status = SQLPARSER_STATUS_OK;
	if (count == 1U) {
		status = constructorgates_frozen_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->table_name, out_error);
	} else if (count == 2U) {
		status = constructorgates_frozen_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->schema_name, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = constructorgates_frozen_identifier_from_sql(
				sql, part_start[1], part_end[1],
				&relation->table_name, out_error);
		}
	} else {
		status = constructorgates_frozen_identifier_from_sql(
			sql, part_start[0], part_end[0],
			&relation->database_name, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			status = constructorgates_frozen_identifier_from_sql(
				sql, part_start[1], part_end[1],
				&relation->schema_name, out_error);
		}
		if (status == SQLPARSER_STATUS_OK) {
			status = constructorgates_frozen_identifier_from_sql(
				sql, part_start[2], part_end[2],
				&relation->table_name, out_error);
		}
	}
	if (status == SQLPARSER_STATUS_OK && has_link) {
		status = sqlparser_oracle_identifier_unquote(
			sql, link_start, link_end, &relation->link_name, out_error);
		if (status == SQLPARSER_STATUS_OK) {
			relation->link_sql = sqlparser_strndup(
				sql + link_start, link_end - link_start);
			if (relation->link_sql == NULL) {
				sqlparser_error_set_message(
					out_error, SQLPARSER_STATUS_NO_MEMORY,
					"out of memory");
				status = SQLPARSER_STATUS_NO_MEMORY;
			}
		}
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_relation_clear(relation);
		return status;
	}
	return SQLPARSER_STATUS_OK;
}

static sqlparser_status_t constructorgates_frozen_parse_multi_insert_into(
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
	sqlparser_oracle_list_bounds_t list_bounds;

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
	status = constructorgates_frozen_relation_from_sql(sql, relation_start, relation_end, &branch.relation, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	if (columns_open != end) {
		if (!constructorgates_frozen_find_matching_paren_with_bounds(sql, columns_open, end, &columns_close,
			SQLPARSER_ORACLE_LIST_COLUMNS, &list_bounds)) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = sqlparser_oracle_parse_column_list_with_bounds(
			sql,
			columns_open + 1U,
			columns_close,
			&branch.columns,
			&branch.column_count,
			span_safe,
			&list_bounds,
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
	if (!constructorgates_frozen_find_matching_paren_with_bounds(sql, values_open, end, &values_close,
		SQLPARSER_ORACLE_LIST_VALUES, &list_bounds)) {
		sqlparser_oracle_multi_insert_branch_clear(&branch);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT VALUES list is not closed");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	if (multi->oracle_spans_complete && multi->oracle_span_count < UINT32_MAX)
		branch.oracle_span_base = (uint32_t)multi->oracle_span_count;
	status = sqlparser_oracle_parse_value_list_with_bounds(
		sql,
		values_open + 1U,
		values_close,
		state,
		multi,
		&branch.cells,
		&branch.cell_count,
		span_safe,
		&list_bounds,
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
