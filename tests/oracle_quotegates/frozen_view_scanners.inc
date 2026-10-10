/* Frozen accepted scangates bodies. Only function linkage and the three
 * names/internal references differ; never use the current implementation byte gate here. */

static int quotegates_frozen_sqlparser_view_public_statement_span_in_sql(
	const sqlparser_handle_t *handle,
	const char *sql,
	int use_current_control_offsets,
	size_t statement_index,
	size_t *out_start,
	size_t *out_end)
{
	size_t after_go;
	size_t current;
	size_t end;
	size_t pos;
	size_t skipped;
	size_t start;
	int sqlserver_go;

	if (handle == NULL || sql == NULL || out_start == NULL ||
	    out_end == NULL) {
		return 0;
	}
	if (handle->control != NULL) {
		const sqlparser_control_unit_t *unit;

		if (statement_index >= handle->control->unit_count) {
			return 0;
		}
		unit = &handle->control->units[statement_index];
		if (use_current_control_offsets) {
			start = unit->current_offset;
			if (unit->current_length > (size_t)-1 - start) {
				return 0;
			}
			end = start + unit->current_length;
		} else {
			if (unit->source_offset > handle->sql_len ||
			    unit->source_length > handle->sql_len - unit->source_offset) {
				return 0;
			}
			start = unit->source_offset;
			end = start + unit->source_length;
		}
		start = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			start);
		while (end > start && isspace((unsigned char)sql[end - 1U])) {
			end--;
		}
		*out_start = start;
		*out_end = end;
		return start < end;
	}

	/* Incompatible dialects make every GO probe a side-effect-free miss. */
	sqlserver_go = sqlparser_dialect_is_sqlserver_compatible(handle->dialect);
	current = 0U;
	pos = 0U;
	while (sql[pos] != '\0') {
		pos = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			pos);
		for (;;) {
			if (sql[pos] == ';') {
				pos++;
			} else if (sqlserver_go && sqlparser_public_sqlserver_go_at(
					   handle->dialect,
					   sql,
					   pos,
					   &after_go)) {
				pos = after_go;
			} else {
				break;
			}
			pos = sqlparser_public_skip_trivia(
				handle->dialect,
				sql,
				pos);
		}
		if (sql[pos] == '\0') {
			break;
		}
		start = pos;
		while (sql[pos] != '\0' && sql[pos] != ';') {
			if (sqlserver_go && sqlparser_public_sqlserver_go_at(
				    handle->dialect,
				    sql,
				    pos,
				    &after_go)) {
				break;
			}
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				sql,
				pos);
			pos = skipped != pos ? skipped : pos + 1U;
		}
		end = pos;
		while (end > start && isspace((unsigned char)sql[end - 1U])) {
			end--;
		}
		if (start < end) {
			if (current == statement_index) {
				*out_start = start;
				*out_end = end;
				return 1;
			}
			current++;
		}
		if (sql[pos] == ';') {
			pos++;
		} else if (sqlserver_go && sqlparser_public_sqlserver_go_at(
				   handle->dialect,
				   sql,
				   pos,
				   &after_go)) {
			pos = after_go;
		}
	}
	return 0;
}

static int quotegates_frozen_sqlparser_view_multi_insert_cell_source_span(
	const sqlparser_handle_t *handle,
	const sqlparser_dialect_multi_insert_t *multi_insert,
	sqlparser_view_expression_source_cache_t *cache,
	size_t statement_start,
	size_t statement_end,
	size_t target_branch,
	size_t target_column,
	size_t *out_start,
	size_t *out_end)
{
	const sqlparser_dialect_multi_insert_branch_t *branch;
	const sqlparser_dialect_multi_insert_value_t *value;
	const char *sql;
	size_t brace_depth;
	size_t bracket_depth;
	size_t branch_index;
	size_t cell_end;
	size_t cell_index;
	size_t cell_start;
	size_t paren_depth;
	size_t pos;
	size_t skipped;
	size_t source_length;
	size_t source_start;
	size_t target_end;
	size_t target_start;
	size_t values_close;
	size_t values_open;
	size_t values_position;
	size_t target_values_position;
	int target_found;
	int validated;

	if (handle == NULL || multi_insert == NULL ||
	    multi_insert->branches == NULL ||
	    target_branch >= multi_insert->branch_count ||
	    multi_insert->source_public_sql == NULL ||
	    statement_start >= statement_end) {
		return 0;
	}
	sql = handle->sql;
	source_length = strlen(multi_insert->source_public_sql);
	if (source_length == 0U ||
	    source_length > statement_end - statement_start) {
		return 0;
	}
	source_start = statement_end - source_length;
	if (source_start <= statement_start ||
	    memcmp(
		    sql + source_start,
		    multi_insert->source_public_sql,
		    source_length) != 0 ||
	    !sqlparser_view_public_word_at(
		    sql,
		    source_start,
		    statement_end,
		    "select")) {
		return 0;
	}

	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		statement_start);
	if (!sqlparser_view_public_word_at(
		    sql,
		    pos,
		    source_start,
		    "insert")) {
		return 0;
	}
	pos = sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		pos + strlen("insert"));
	if (multi_insert->mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL &&
	    sqlparser_view_public_word_at(
		    sql,
		    pos,
		    source_start,
		    "all")) {
		pos += strlen("all");
	} else if (multi_insert->mode ==
			   SQLPARSER_DIALECT_MULTI_INSERT_FIRST &&
		   sqlparser_view_public_word_at(
			   sql,
			   pos,
			   source_start,
			   "first")) {
		pos += strlen("first");
	} else {
		return 0;
	}

	branch_index = 0U;
	validated = cache != NULL && cache->valid == 2;
	if (validated && target_branch >= cache->multi_branch_index) {
		branch_index = cache->multi_branch_index;
		pos = cache->resume;
	}
	paren_depth = 0U;
	target_end = 0U;
	target_found = 0;
	target_start = 0U;
	target_values_position = 0U;
	while (pos < source_start) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			sql,
			pos);
		if (skipped != pos) {
			if (skipped > source_start) {
				return 0;
			}
			pos = skipped;
			continue;
		}
		if (sql[pos] == '(') {
			paren_depth++;
			pos++;
			continue;
		}
		if (sql[pos] == ')') {
			if (paren_depth == 0U) {
				return 0;
			}
			paren_depth--;
			pos++;
			continue;
		}
		if (paren_depth != 0U ||
		    !sqlparser_view_public_word_at(
			    sql,
			    pos,
			    source_start,
			    "values")) {
			pos++;
			continue;
		}
		values_position = pos;
		values_open = sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			pos + strlen("values"));
		if (values_open >= source_start || sql[values_open] != '(') {
			pos += strlen("values");
			continue;
		}
		if (branch_index >= multi_insert->branch_count ||
		    (branch = &multi_insert->branches[branch_index])->ordinal !=
			    branch_index ||
		    branch->cells == NULL || branch->cell_count == 0U) {
			return 0;
		}

		cell_index = 0U;
		cell_start = sqlparser_public_skip_space(sql, values_open + 1U);
		paren_depth = 0U;
		bracket_depth = 0U;
		brace_depth = 0U;
		values_close = source_start;
		for (pos = cell_start; pos < source_start; pos++) {
			skipped = sqlparser_public_skip_quoted_or_comment(
				handle->dialect,
				sql,
				pos);
			if (skipped != pos) {
				if (skipped > source_start) {
					return 0;
				}
				pos = skipped - 1U;
				continue;
			}
			if (sql[pos] == '(') {
				paren_depth++;
				continue;
			}
			if (sql[pos] == '[') {
				bracket_depth++;
				continue;
			}
			if (sql[pos] == '{') {
				brace_depth++;
				continue;
			}
			if (sql[pos] == ')' && paren_depth > 0U) {
				paren_depth--;
				continue;
			}
			if (sql[pos] == ']' && bracket_depth > 0U) {
				bracket_depth--;
				continue;
			}
			if (sql[pos] == '}' && brace_depth > 0U) {
				brace_depth--;
				continue;
			}
			if ((sql[pos] != ',' && sql[pos] != ')') ||
			    paren_depth != 0U || bracket_depth != 0U ||
			    brace_depth != 0U) {
				continue;
			}
			cell_end = pos;
			while (cell_end > cell_start &&
			       isspace((unsigned char)sql[cell_end - 1U])) {
				cell_end--;
			}
			if (cell_end <= cell_start ||
			    cell_index >= branch->cell_count ||
			    (value = &branch->cells[cell_index])->public_sql == NULL ||
			    cell_end - cell_start != strlen(value->public_sql) ||
			    memcmp(
				    sql + cell_start,
				    value->public_sql,
				    cell_end - cell_start) != 0) {
				return 0;
			}
			if (branch_index == target_branch &&
			    cell_index == target_column) {
				target_start = cell_start;
				target_end = cell_end;
				target_found = 1;
				target_values_position = values_position;
			}
			cell_index++;
			if (sql[pos] == ')') {
				values_close = pos;
				break;
			}
			cell_start = sqlparser_public_skip_space(sql, pos + 1U);
			pos = cell_start - 1U;
		}
		if (values_close == source_start ||
		    cell_index != branch->cell_count) {
			return 0;
		}
		if (validated && target_found) {
			cache->resume = target_values_position;
			cache->multi_branch_index = target_branch;
			*out_start = target_start;
			*out_end = target_end;
			return 1;
		}
		branch_index++;
		paren_depth = 0U;
		pos = values_close + 1U;
	}
	if (paren_depth != 0U || branch_index != multi_insert->branch_count ||
	    !target_found) {
		return 0;
	}
	*out_start = target_start;
	*out_end = target_end;
	if (cache != NULL) {
		cache->valid = 2;
		cache->search_position = statement_start;
		cache->statement_end = statement_end;
		cache->multi_branch_index = target_branch;
		cache->resume = target_values_position;
	}
	return 1;
}

static int quotegates_frozen_sqlparser_view_insert_cell_source_span(
	sqlparser_handle_t *handle,
	const sqlparser_surface_source_edits_t *surface_edits,
	sqlparser_view_expression_source_cache_t *cache,
	int allow_comments,
	size_t statement_index,
	size_t row_index,
	size_t column_index,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	const sqlparser_dialect_multi_insert_t *multi_insert;
	const sqlparser_identifier_origin_map_t *origins;
	PgQuery__InsertStmt *insert_stmt;
	PgQuery__Node *row_node;
	PgQuery__Node *statement;
	PgQuery__Node *value_node;
	PgQuery__SelectStmt *values_stmt;
	sqlparser_graph_insert_mode_t insert_mode;
	sqlparser_status_t status;
	size_t left_delimiter;
	size_t pos;
	size_t right_delimiter;
	size_t skipped;
	size_t statement_end;
	size_t statement_start;
	int source_status;

	if (out_start == NULL || out_end == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"insert cell source span must not be NULL");
		return -1;
	}
	*out_start = 0U;
	*out_end = 0U;
	if (handle == NULL || handle->sql == NULL) {
		return 0;
	}
	multi_insert = sqlparser_dialect_is_oracle_or_dameng_compatible(handle->dialect) ?
		sqlparser_dialect_state_multi_insert(
			handle->dialect,
			handle->dialect_state) : NULL;
	if (multi_insert != NULL) {
		if (statement_index != 0U) return 0;
		if (cache != NULL && cache->valid == 2) {
			statement_start = cache->search_position;
			statement_end = cache->statement_end;
		} else if (!quotegates_frozen_sqlparser_view_public_statement_span_in_sql(
			    handle,
			    handle->sql,
			    0,
			    statement_index,
			    &statement_start,
			    &statement_end)) {
			return 0;
		}
		if (handle->dialect == SQLPARSER_DIALECT_DAMENG && (cache == NULL || cache->valid != 2)) {
			for (pos = statement_start; pos < statement_end;) {
				if (sqlparser_public_comment_at(
					    handle->dialect, handle->sql, pos)) {
					break;
				}
				skipped = sqlparser_public_skip_quoted_or_comment(
					handle->dialect, handle->sql, pos);
				pos = skipped > pos ? skipped : pos + 1U;
			}
			if (pos == statement_end) {
				return 0;
			}
		}
		/* The first generic scan validates every branch/cell, including its
		 * bracket/brace tokenization. Only then may current constructor spans
		 * replace repeated scans; identity alone does not prove that validation.
		 * Keep the common delimiter and comment checks below for every cell. */
		source_status = cache != NULL && cache->valid == 2 &&
			sqlparser_oracle_multi_insert_certified_cell_span(
				handle, row_index, column_index, out_start, out_end);
		if (!source_status) source_status = quotegates_frozen_sqlparser_view_multi_insert_cell_source_span(
			handle,
			multi_insert,
			cache,
			statement_start,
			statement_end,
			row_index,
			column_index,
			out_start,
			out_end);
		if (source_status <= 0) {
			return source_status;
		}
	} else {
		status = sqlparser_get_statement_node(
			handle,
			statement_index,
			&statement,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
		if (statement->node_case != PG_QUERY__NODE__NODE_INSERT_STMT ||
		    (insert_stmt = statement->insert_stmt) == NULL ||
		    sqlparser_insert_source_from_stmt(insert_stmt) !=
			    SQLPARSER_INSERT_SOURCE_VALUES ||
		    insert_stmt->select_stmt == NULL ||
		    (values_stmt = insert_stmt->select_stmt->select_stmt) == NULL) {
			return 0;
		}
		insert_mode = SQLPARSER_GRAPH_INSERT_MODE_VALUES;
		if (handle->dialect_ops != NULL &&
		    handle->dialect_ops->insert_mode != NULL) {
			insert_mode = handle->dialect_ops->insert_mode(
				handle->dialect_state,
				statement_index,
				insert_mode);
		}
		if (insert_mode != SQLPARSER_GRAPH_INSERT_MODE_VALUES &&
		    insert_mode != SQLPARSER_GRAPH_INSERT_MODE_REPLACE_VALUES) {
			return 0;
		}
		if (values_stmt->values_lists == NULL ||
		    row_index >= values_stmt->n_values_lists ||
		    (row_node = values_stmt->values_lists[row_index]) == NULL ||
		    row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
		    row_node->list == NULL || row_node->list->items == NULL ||
		    column_index >= row_node->list->n_items ||
		    (value_node = row_node->list->items[column_index]) == NULL) {
			return 0;
		}
		statement_start = cache != NULL && cache->valid == 1 ? cache->statement_start : 0U;
		statement_end = cache != NULL && cache->valid == 1 ? cache->statement_end : 0U;
		origins = NULL;
		source_status = sqlparser_view_expression_source_span(
			handle,
			&origins,
			cache,
			value_node,
			surface_edits,
			out_start,
			out_end,
			out_error);
		if (source_status <= 0) {
			return source_status;
		}
		if ((statement_start >= statement_end || *out_start < statement_start || *out_end > statement_end) &&
		    !quotegates_frozen_sqlparser_view_public_statement_span_in_sql(
			    handle,
			    handle->sql,
			    0,
			    statement_index,
			    &statement_start,
			    &statement_end)) {
			*out_start = 0U;
			*out_end = 0U;
			return 0;
		}
		if (cache != NULL && cache->valid == 1) {
			cache->statement_start = statement_start;
			cache->statement_end = statement_end;
		}
	}
	if (*out_start < statement_start || *out_end > statement_end ||
	    *out_start >= *out_end) {
		*out_start = 0U;
		*out_end = 0U;
		return 0;
	}
	left_delimiter = *out_start;
	while (left_delimiter > statement_start &&
	       isspace((unsigned char)handle->sql[left_delimiter - 1U])) {
		left_delimiter--;
	}
	right_delimiter = *out_end;
	while (right_delimiter < statement_end &&
	       isspace((unsigned char)handle->sql[right_delimiter])) {
		right_delimiter++;
	}
	if (left_delimiter == statement_start ||
	    (handle->sql[left_delimiter - 1U] != '(' &&
	     handle->sql[left_delimiter - 1U] != ',') ||
	    right_delimiter >= statement_end ||
	    (handle->sql[right_delimiter] != ',' &&
	     handle->sql[right_delimiter] != ')')) {
		*out_start = 0U;
		*out_end = 0U;
		return 0;
	}
	for (pos = *out_start; pos < *out_end;) {
		skipped = sqlparser_public_skip_quoted_or_comment(
			handle->dialect,
			handle->sql,
			pos);
		if (skipped == pos) {
			pos++;
			continue;
		}
		if ((!allow_comments && sqlparser_public_comment_at(
			     handle->dialect, handle->sql, pos)) ||
		    skipped > *out_end) {
			*out_start = 0U;
			*out_end = 0U;
			return 0;
		}
		pos = skipped;
	}
	return 1;
}
