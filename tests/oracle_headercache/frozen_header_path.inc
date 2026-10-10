/* Constructor-gates header/root path: complete original scans and ordinary
 * constructors, with only private definition/call names changed. The frozen
 * root and INTO entry never use the current implementation header cache.
 */

static int headercache_frozen_list_bounds_append(
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

static int headercache_frozen_list_bounds_usable(
	const sqlparser_oracle_list_bounds_t *bounds,
	int kind, size_t start, size_t close)
{
	return bounds != NULL && bounds->complete && bounds->kind == kind &&
		bounds->start == start && bounds->close == close &&
		bounds->count != 0U &&
		bounds->count <= SQLPARSER_ORACLE_LIST_BOUND_CAPACITY &&
		bounds->ends[bounds->count - 1U] == close;
}

static int headercache_frozen_find_matching_paren_with_bounds(
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

		skipped = sqlparser_oracle_quote_or_comment_may_start((unsigned char)sql[pos]) ?
			sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
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
				if (collect && headercache_frozen_list_bounds_append(bounds, pos)) {
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
				if (name_state == 0 || !headercache_frozen_list_bounds_append(bounds, pos))
					collect = 0;
				name_state = 0;
			} else if (!sqlparser_oracle_span_name_byte((unsigned char)sql[pos], 0, &name_state)) {
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
				collect = headercache_frozen_list_bounds_append(bounds, pos);
			}
		}
		pos++;
	}
	if (bounds != NULL) bounds->count = 0U;
	return 0;
}

static sqlparser_status_t headercache_frozen_parse_column_list_with_bounds(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_dialect_multi_insert_column_t **out_columns,
	size_t *out_count,
	int *span_safe,
	const sqlparser_oracle_list_bounds_t *bounds,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_column_t *columns;
	size_t count;
	size_t capacity;
	size_t item_start;
	size_t pos;
	size_t boundary_index = 0U;
	int use_bounds;
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
	use_bounds = headercache_frozen_list_bounds_usable(
		bounds, SQLPARSER_ORACLE_LIST_COLUMNS, start, end);
	while (pos <= end) {
		int at_end;
		size_t skipped;

		if (use_bounds) {
			pos = bounds->ends[boundary_index++];
			at_end = pos == end;
		} else {
			at_end = pos == end;
			skipped = !at_end ? sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
			if (skipped > pos) {
				*span_safe = 0;
				pos = skipped;
				continue;
			}
			if (!at_end && sql[pos] != ',' &&
			    !sqlparser_oracle_span_name_byte((unsigned char)sql[pos], 0, &name_state)) *span_safe = 0;
		}
		if (at_end || sql[pos] == ',') {
			size_t item_end;
			sqlparser_dialect_multi_insert_column_t *next;

			/* A cached column list proved every original name check safe.
			 * Never promote an already-false caller proof. */
			if (!use_bounds && name_state == 0) *span_safe = 0;
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

static sqlparser_status_t headercache_frozen_parse_multi_insert_into(
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

		skipped = sqlparser_oracle_quote_or_comment_may_start((unsigned char)sql[pos]) ?
			sqlparser_oracle_skip_quoted_or_comment_span(sql, pos) : pos;
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
		if (!sqlparser_oracle_span_name_byte((unsigned char)sql[pos], 1, &relation_name_state))
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
		if (!headercache_frozen_find_matching_paren_with_bounds(sql, columns_open, end, &columns_close,
			SQLPARSER_ORACLE_LIST_COLUMNS, &list_bounds)) {
			sqlparser_oracle_multi_insert_branch_clear(&branch);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT column list is not closed");
			return SQLPARSER_STATUS_PARSE_ERROR;
		}
		status = headercache_frozen_parse_column_list_with_bounds(
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
	if (!headercache_frozen_find_matching_paren_with_bounds(sql, values_open, end, &values_close,
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

static sqlparser_status_t headercache_frozen_parse_multi_insert(
	const char *input_sql,
	sqlparser_oracle_state_t *state,
	char **out_parser_sql,
	int nested_block_comments,
	sqlparser_error_t *out_error)
{
	sqlparser_dialect_multi_insert_t *multi;
	sqlparser_dialect_multi_insert_mode_t mode;
	sqlparser_oracle_buffer_t parser;
	size_t len;
	size_t end;
	size_t pos;
	size_t condition_group_id;
	sqlparser_status_t status;
	int span_safe;

	if (out_parser_sql == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "multi insert parser arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_parser_sql = NULL;
	if (!sqlparser_oracle_is_multi_insert_start(input_sql, &mode, nested_block_comments)) {
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	span_safe = mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL;
	len = strlen(input_sql);
	pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, 0U, len, nested_block_comments, &span_safe);
	end = sqlparser_oracle_span_trim_right(input_sql, pos, len, &span_safe);
	if (end > pos && input_sql[end - 1U] == ';') {
		end = sqlparser_oracle_span_trim_right(input_sql, pos, end - 1U, &span_safe);
	}
	pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(
		input_sql, pos + strlen("insert"), end, nested_block_comments, &span_safe);
	pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(
		input_sql,
		pos + (mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL ? strlen("all") : strlen("first")),
		end,
		nested_block_comments, &span_safe);

	multi = (sqlparser_dialect_multi_insert_t *)calloc(1U, sizeof(*multi));
	if (multi == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	multi->mode = mode;
	multi->oracle_spans_complete = len <= UINT32_MAX;
	multi->oracle_spans_identity = mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL;
	multi->oracle_outer_identity = 1;
	condition_group_id = 0U;

	while (pos < end) {
		pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, pos, end, nested_block_comments, &span_safe);
		if (pos >= end) {
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "select")) {
			break;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "when")) {
			size_t condition_start;
			size_t then_pos;
			char *condition_public;
			char *condition_parser;

			condition_start = sqlparser_oracle_skip_leading_trivia_with_span_proof(
				input_sql, pos + strlen("when"), end, nested_block_comments, &span_safe);
			if (!sqlparser_oracle_find_top_level_word(input_sql, condition_start, end, "then", &then_pos)) {
				sqlparser_oracle_multi_insert_destroy(multi);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT WHEN is missing THEN");
				return SQLPARSER_STATUS_PARSE_ERROR;
			}
			condition_public = NULL;
			condition_parser = NULL;
			status = sqlparser_oracle_trimmed_slice_dup(
				input_sql,
				condition_start,
				then_pos,
				&condition_public,
				out_error);
			if (status == SQLPARSER_STATUS_OK) {
				status = sqlparser_oracle_preprocess_text(condition_public, state, &condition_parser, out_error);
			}
			if (status != SQLPARSER_STATUS_OK) {
				free(condition_public);
				free(condition_parser);
				sqlparser_oracle_multi_insert_destroy(multi);
				return status;
			}
			pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(
				input_sql, then_pos + strlen("then"), end, nested_block_comments, &span_safe);
			condition_group_id++;
			do {
				status = headercache_frozen_parse_multi_insert_into(
					input_sql,
					&pos,
					end,
					state,
					multi,
					condition_public,
					condition_parser,
					0,
					condition_group_id,
					&span_safe,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					free(condition_public);
					free(condition_parser);
					sqlparser_oracle_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, pos, end, nested_block_comments, &span_safe);
			} while (pos < end &&
			         sqlparser_oracle_ascii_word_equal(input_sql, pos, "into"));
			free(condition_public);
			free(condition_parser);
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "else")) {
			pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(
				input_sql, pos + strlen("else"), end, nested_block_comments, &span_safe);
			condition_group_id++;
			while (pos < end && sqlparser_oracle_ascii_word_equal(input_sql, pos, "into")) {
				status = headercache_frozen_parse_multi_insert_into(
					input_sql,
					&pos,
					end,
					state,
					multi,
					NULL,
					NULL,
					1,
					condition_group_id,
					&span_safe,
					out_error);
				if (status != SQLPARSER_STATUS_OK) {
					sqlparser_oracle_multi_insert_destroy(multi);
					return status;
				}
				pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, pos, end, nested_block_comments, &span_safe);
			}
			continue;
		}
		if (sqlparser_oracle_ascii_word_equal(input_sql, pos, "into")) {
			status = headercache_frozen_parse_multi_insert_into(
				input_sql,
				&pos,
				end,
				state,
				multi,
				NULL,
				NULL,
				0,
				0U,
				&span_safe,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				sqlparser_oracle_multi_insert_destroy(multi);
				return status;
			}
			continue;
		}
		sqlparser_oracle_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT expected INTO, WHEN, ELSE, or SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}

	pos = sqlparser_oracle_skip_leading_trivia_with_span_proof(input_sql, pos, end, nested_block_comments, &span_safe);
	if (multi->branch_count == 0U || pos >= end || !sqlparser_oracle_ascii_word_equal(input_sql, pos, "select")) {
		sqlparser_oracle_multi_insert_destroy(multi);
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_PARSE_ERROR, "Oracle multi-table INSERT requires branches and a source SELECT");
		return SQLPARSER_STATUS_PARSE_ERROR;
	}
	status = sqlparser_oracle_trimmed_slice_dup(input_sql, pos, end, &multi->source_public_sql, out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_preprocess_text_with_span_proof(multi->source_public_sql, state, &multi->source_parser_sql, &span_safe, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_multi_insert_destroy(multi);
		return status;
	}
	if (multi->oracle_spans_complete) {
		/* Full constructor input was bounded before any uint32 conversion. */
		multi->oracle_source_start = (uint32_t)pos;
		multi->oracle_source_length = (uint32_t)(end - pos);
		if (strcmp(multi->source_public_sql, multi->source_parser_sql) != 0)
			multi->oracle_spans_identity = 0;
	}

	memset(&parser, 0, sizeof(parser));
	status = sqlparser_oracle_buffer_append_cstr(&parser, "INSERT INTO sqlparser_oracle_multi_insert_source ", out_error);
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_append_cstr(&parser, multi->source_parser_sql, out_error);
	}
	if (status == SQLPARSER_STATUS_OK) {
		status = sqlparser_oracle_buffer_finish(&parser, out_error);
	}
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_buffer_release(&parser);
		sqlparser_oracle_multi_insert_destroy(multi);
		return status;
	}

	/* Publish only a complete proof. Optional span OOM never changes parsing. */
	multi->oracle_generic_spans_equivalent = span_safe &&
		multi->oracle_spans_complete && multi->oracle_spans_identity &&
		sqlparser_oracle_span_ascii_ctype();
	sqlparser_oracle_multi_insert_destroy(state->multi_insert);
	state->multi_insert = multi;
	*out_parser_sql = sqlparser_oracle_buffer_take(&parser);
	return *out_parser_sql != NULL ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_NO_MEMORY;
}
