/* Independent reference statement and expression scanners.
 * Only static linkage and function names/internal references differ.
 * Do not call optimized
 * statement, generic multi, expression-source or top-level scanners. */

static int spancert_frozen_sqlparser_view_insert_cell_source_span(
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
		} else if (!spancert_frozen_sqlparser_view_public_statement_span_in_sql(
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
				if (spancert_frozen_sqlparser_public_comment_at(
					    handle->dialect, handle->sql, pos)) {
					break;
				}
				skipped = spancert_frozen_sqlparser_public_skip_quoted_or_comment(
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
			spancert_frozen_sqlparser_oracle_multi_insert_certified_cell_span(
				handle, row_index, column_index, out_start, out_end);
		if (!source_status) source_status = spancert_frozen_sqlparser_view_multi_insert_cell_source_span(
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
		source_status = spancert_frozen_sqlparser_view_expression_source_span(
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
		    !spancert_frozen_sqlparser_view_public_statement_span_in_sql(
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
		skipped = spancert_frozen_sqlparser_view_public_quote_or_comment_byte(
			(unsigned char)handle->sql[pos]) ?
			spancert_frozen_sqlparser_public_skip_quoted_or_comment(
				handle->dialect, handle->sql, pos) : pos;
		if (skipped == pos) {
			pos++;
			continue;
		}
		if ((!allow_comments && spancert_frozen_sqlparser_public_comment_at(
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
