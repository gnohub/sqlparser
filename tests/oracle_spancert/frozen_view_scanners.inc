/* Independent reference statement and expression scanners.
 * Only static linkage and function names/internal references differ.
 * Do not call optimized
 * statement, generic multi, expression-source or top-level scanners. */

static int spancert_frozen_sqlparser_view_public_word_at(
	const char *sql,
	size_t pos,
	size_t end,
	const char *word)
{
	size_t index;
	size_t length;

	if (sql == NULL || word == NULL || pos >= end) {
		return 0;
	}
	length = strlen(word);
	if (length > end - pos ||
	    (pos > 0U &&
	     spancert_frozen_sqlparser_public_char_is_ident(
		     (unsigned char)sql[pos - 1U])) ||
	    (pos + length < end &&
	     spancert_frozen_sqlparser_public_char_is_ident(
		     (unsigned char)sql[pos + length]))) {
		return 0;
	}
	for (index = 0U; index < length; index++) {
		if (tolower((unsigned char)sql[pos + index]) !=
		    tolower((unsigned char)word[index])) {
			return 0;
		}
	}
	return 1;
}

static inline int spancert_frozen_sqlparser_view_public_quote_or_comment_byte(unsigned char ch)
{
	switch (ch) {
		case '$': case 'q': case 'Q': case 'n': case 'N':
		case '-': case '#': case '/': case '[': case '`':
		case '\'': case '"':
			return 1;
		default:
			return 0;
	}
}

static int spancert_frozen_sqlparser_view_public_statement_span_in_sql(
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
		start = spancert_frozen_sqlparser_public_skip_trivia(
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
		pos = spancert_frozen_sqlparser_public_skip_trivia(
			handle->dialect,
			sql,
			pos);
		for (;;) {
			if (sql[pos] == ';') {
				pos++;
			} else if (sqlserver_go && spancert_frozen_sqlparser_public_sqlserver_go_at(
					   handle->dialect,
					   sql,
					   pos,
					   &after_go)) {
				pos = after_go;
			} else {
				break;
			}
			pos = spancert_frozen_sqlparser_public_skip_trivia(
				handle->dialect,
				sql,
				pos);
		}
		if (sql[pos] == '\0') {
			break;
		}
		start = pos;
		while (sql[pos] != '\0' && sql[pos] != ';') {
			if (sqlserver_go && spancert_frozen_sqlparser_public_sqlserver_go_at(
				    handle->dialect,
				    sql,
				    pos,
				    &after_go)) {
				break;
			}
			skipped = spancert_frozen_sqlparser_view_public_quote_or_comment_byte(
				(unsigned char)sql[pos]) ?
				spancert_frozen_sqlparser_public_skip_quoted_or_comment(
					handle->dialect, sql, pos) : pos;
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
		} else if (sqlserver_go && spancert_frozen_sqlparser_public_sqlserver_go_at(
				   handle->dialect,
				   sql,
				   pos,
				   &after_go)) {
			pos = after_go;
		}
	}
	return 0;
}

static int spancert_frozen_sqlparser_view_multi_insert_cell_source_span(
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
	    !spancert_frozen_sqlparser_view_public_word_at(
		    sql,
		    source_start,
		    statement_end,
		    "select")) {
		return 0;
	}

	pos = spancert_frozen_sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		statement_start);
	if (!spancert_frozen_sqlparser_view_public_word_at(
		    sql,
		    pos,
		    source_start,
		    "insert")) {
		return 0;
	}
	pos = spancert_frozen_sqlparser_public_skip_trivia(
		handle->dialect,
		sql,
		pos + strlen("insert"));
	if (multi_insert->mode == SQLPARSER_DIALECT_MULTI_INSERT_ALL &&
	    spancert_frozen_sqlparser_view_public_word_at(
		    sql,
		    pos,
		    source_start,
		    "all")) {
		pos += strlen("all");
	} else if (multi_insert->mode ==
			   SQLPARSER_DIALECT_MULTI_INSERT_FIRST &&
		   spancert_frozen_sqlparser_view_public_word_at(
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
		skipped = spancert_frozen_sqlparser_view_public_quote_or_comment_byte(
			(unsigned char)sql[pos]) ?
			spancert_frozen_sqlparser_public_skip_quoted_or_comment(
				handle->dialect, sql, pos) : pos;
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
		    !spancert_frozen_sqlparser_view_public_word_at(
			    sql,
			    pos,
			    source_start,
			    "values")) {
			pos++;
			continue;
		}
		values_position = pos;
		values_open = spancert_frozen_sqlparser_public_skip_trivia(
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
		cell_start = spancert_frozen_sqlparser_public_skip_space(sql, values_open + 1U);
		paren_depth = 0U;
		bracket_depth = 0U;
		brace_depth = 0U;
		values_close = source_start;
		for (pos = cell_start; pos < source_start; pos++) {
			skipped = spancert_frozen_sqlparser_view_public_quote_or_comment_byte(
				(unsigned char)sql[pos]) ?
				spancert_frozen_sqlparser_public_skip_quoted_or_comment(
					handle->dialect, sql, pos) : pos;
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
			cell_start = spancert_frozen_sqlparser_public_skip_space(sql, pos + 1U);
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

static int spancert_frozen_sqlparser_view_node_source_location(
	const PgQuery__Node *node,
	size_t *out_location)
{
	const ProtobufCMessageDescriptor *descriptor;
	const ProtobufCFieldDescriptor *fields;
	unsigned field_count;
	const ProtobufCMessage *message;
	uint8_t *base;
	unsigned index;

	if (node == NULL || out_location == NULL) {
		return 0;
	}
	message = (const ProtobufCMessage *)node;
	descriptor = message->descriptor;
	if (descriptor == NULL) {
		return 0;
	}
	fields = descriptor->fields;
	field_count = descriptor->n_fields;
	if (descriptor == &pg_query__node__descriptor) {
		fields = protobuf_c_message_descriptor_get_field(
			descriptor, ((const PgQuery__Node *)message)->node_case);
		field_count = fields != NULL ? 1U : 0U;
	}
	base = (uint8_t *)message;
	for (index = 0U; index < field_count; index++) {
		const ProtobufCFieldDescriptor *field;
		ProtobufCMessage *child;
		int32_t *location;

		field = &fields[index];
		if (field->type != PROTOBUF_C_TYPE_MESSAGE ||
		    field->label == PROTOBUF_C_LABEL_REPEATED ||
		    ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		     *(const int *)(base + field->quantifier_offset) !=
			     (int)field->id)) {
			continue;
		}
		child = *(ProtobufCMessage *const *)(base + field->offset);
		location = sqlparser_proto_location_slot(child);
		if (location != NULL && *location >= 0) {
			*out_location = (size_t)*location;
			return 1;
		}
		if (child != NULL &&
		    child->descriptor == &pg_query__type_cast__descriptor &&
		    spancert_frozen_sqlparser_view_node_source_location(
			    ((const PgQuery__TypeCast *)child)->arg,
			    out_location)) {
			return 1;
		}
	}
	return 0;
}

static int spancert_frozen_sqlparser_view_source_delimiter(
	const sqlparser_identifier_origin_map_t *origins,
	size_t parser_offset,
	size_t *out_start,
	size_t *out_end)
{
	sqlparser_identifier_origin_t origin;

	if (out_start == NULL || out_end == NULL ||
	    sqlparser_identifier_origin_map_lookup(
		    origins,
		    parser_offset,
		    1U,
		    &origin) != SQLPARSER_IDENTIFIER_ORIGIN_SOURCE ||
	    origin.source_length == 0U ||
	    origin.source_offset > (size_t)-1 - origin.source_length) {
		return 0;
	}
	*out_start = origin.source_offset;
	*out_end = origin.source_offset + origin.source_length;
	return 1;
}

static int spancert_frozen_sqlparser_view_expression_source_span_between(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t *origins,
	const sqlparser_surface_source_edits_t *surface_edits,
	size_t parser_left_delimiter,
	size_t parser_right_delimiter,
	size_t *out_start,
	size_t *out_end)
{
	size_t edit_index;
	size_t left_end;
	size_t left_start;
	size_t right_end;
	size_t right_start;
	size_t source_end;
	size_t source_start;

	if (!spancert_frozen_sqlparser_view_source_delimiter(
		    origins,
		    parser_left_delimiter,
		    &left_start,
		    &left_end) ||
	    !spancert_frozen_sqlparser_view_source_delimiter(
		    origins,
		    parser_right_delimiter,
		    &right_start,
		    &right_end) ||
	    left_end > right_start ||
	    right_end > handle->sql_len) {
		return 0;
	}
	source_start = spancert_frozen_sqlparser_public_skip_space(
		handle->sql,
		left_end);
	source_end = right_start;
	while (source_end > source_start &&
	       isspace((unsigned char)handle->sql[source_end - 1U])) {
		source_end--;
	}
	if (source_start >= source_end) {
		return 0;
	}
	if (surface_edits != NULL && surface_edits->count > 0U &&
	    surface_edits->items[surface_edits->count - 1U].source_end >= source_start) {
		for (edit_index = 0U;
		     edit_index < surface_edits->count && surface_edits->items[edit_index].source_start <= source_end;
		     edit_index++) {
			const sqlparser_surface_source_edit_t *edit;

			edit = &surface_edits->items[edit_index];
			if (edit->replacement == NULL ||
			    edit->source_start > edit->source_end ||
			    edit->source_end > handle->sql_len) {
				return 0;
			}
			if (edit->source_start == edit->source_end) {
				if (edit->source_start >= source_start &&
				    edit->source_start <= source_end) {
					return 0;
				}
			} else if (edit->source_start < source_end &&
				   source_start < edit->source_end) {
				return 0;
			}
		}
	}
	*out_start = source_start;
	*out_end = source_end;
	return 1;
}

static int spancert_frozen_sqlparser_view_expression_source_span_in_values(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t *origins,
	const sqlparser_surface_source_edits_t *surface_edits,
	size_t scan_start,
	size_t location,
	size_t *out_resume,
	size_t *out_start,
	size_t *out_end)
{
	const char *parser_sql;
	size_t brace_depth;
	size_t bracket_depth;
	size_t cell_left_delimiter;
	size_t paren_depth;
	size_t pos;
	size_t skipped;

	parser_sql = handle->parser_sql;
	pos = spancert_frozen_sqlparser_public_skip_trivia(
		SQLPARSER_DIALECT_POSTGRESQL,
		parser_sql,
		scan_start);
	if (parser_sql[pos] != '(' && parser_sql[pos] != ',') {
		return 0;
	}
	for (;;) {
		cell_left_delimiter = pos;
		paren_depth = 0U;
		bracket_depth = 0U;
		brace_depth = 0U;
		for (pos++; parser_sql[pos] != '\0'; pos++) {
			skipped = spancert_frozen_sqlparser_public_skip_quoted_or_comment(
				SQLPARSER_DIALECT_POSTGRESQL,
				parser_sql,
				pos);
			if (skipped != pos) {
				pos = skipped - 1U;
				continue;
			}
			if (parser_sql[pos] == '(') {
				paren_depth++;
				continue;
			}
			if (parser_sql[pos] == '[') {
				bracket_depth++;
				continue;
			}
			if (parser_sql[pos] == '{') {
				brace_depth++;
				continue;
			}
			if (parser_sql[pos] == ')' && paren_depth > 0U) {
				paren_depth--;
				continue;
			}
			if (parser_sql[pos] == ']' && bracket_depth > 0U) {
				bracket_depth--;
				continue;
			}
			if (parser_sql[pos] == '}' && brace_depth > 0U) {
				brace_depth--;
				continue;
			}
			if ((parser_sql[pos] != ',' && parser_sql[pos] != ')') ||
			    paren_depth != 0U ||
			    bracket_depth != 0U ||
			    brace_depth != 0U) {
				continue;
			}
			if (location > cell_left_delimiter && location < pos) {
				if (out_resume != NULL) {
					*out_resume = cell_left_delimiter;
				}
				return spancert_frozen_sqlparser_view_expression_source_span_between(
					handle,
					origins,
					surface_edits,
					cell_left_delimiter,
					pos,
					out_start,
					out_end);
			}
			if (parser_sql[pos] == ',') {
				cell_left_delimiter = pos;
				continue;
			}
			pos = spancert_frozen_sqlparser_public_skip_trivia(
				SQLPARSER_DIALECT_POSTGRESQL,
				parser_sql,
				pos + 1U);
			if (parser_sql[pos] != ',') {
				return 0;
			}
			pos = spancert_frozen_sqlparser_public_skip_trivia(
				SQLPARSER_DIALECT_POSTGRESQL,
				parser_sql,
				pos + 1U);
			if (parser_sql[pos] != '(') {
				return 0;
			}
			break;
		}
		if (parser_sql[pos] == '\0') {
			return 0;
		}
	}
}

static int spancert_frozen_sqlparser_view_expression_source_span(
	const sqlparser_handle_t *handle,
	const sqlparser_identifier_origin_map_t **origins,
	sqlparser_view_expression_source_cache_t *cache,
	PgQuery__Node *value_node,
	const sqlparser_surface_source_edits_t *surface_edits,
	size_t *out_start,
	size_t *out_end,
	sqlparser_error_t *out_error)
{
	const char *parser_sql;
	size_t location;
	size_t pos;
	size_t resume;
	size_t search_position;
	size_t skipped;
	sqlparser_status_t status;

	if (out_start == NULL || out_end == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"expression source span must not be NULL");
		return -1;
	}
	*out_start = 0U;
	*out_end = 0U;
	if (handle == NULL || origins == NULL || value_node == NULL ||
	    !spancert_frozen_sqlparser_view_node_source_location(value_node, &location) ||
	    location >= handle->parser_sql_len) {
		return 0;
	}
	if (*origins == NULL) {
		status = sqlparser_identifier_origins_for_handle(
			handle,
			origins,
			out_error);
		if (status != SQLPARSER_STATUS_OK) {
			return -1;
		}
	}
	parser_sql = handle->parser_sql;
	search_position = 0U;
	if (cache != NULL &&
	    cache->valid == 1 &&
	    location >= cache->last_location) {
		int source_status;

		resume = cache->resume;
		source_status = spancert_frozen_sqlparser_view_expression_source_span_in_values(
			handle,
			*origins,
			surface_edits,
			resume,
			location,
			&resume,
			out_start,
			out_end);
		if (source_status != 0) {
			cache->resume = resume;
			cache->last_location = location;
			return source_status;
		}
		search_position = cache->search_position;
		cache->valid = 0;
	} else if (cache != NULL) {
		cache->valid = 0;
	}
	for (pos = search_position;
	     parser_sql[pos] != '\0';
	     pos++) {
		int source_status;

		skipped = spancert_frozen_sqlparser_public_skip_quoted_or_comment(
			SQLPARSER_DIALECT_POSTGRESQL,
			parser_sql,
			pos);
		if (skipped != pos) {
			pos = skipped - 1U;
			continue;
		}
		if (!spancert_frozen_sqlparser_view_public_word_at(
			    parser_sql,
			    pos,
			    handle->parser_sql_len,
			    "values")) {
			continue;
		}
		source_status = spancert_frozen_sqlparser_view_expression_source_span_in_values(
			handle,
			*origins,
			surface_edits,
			pos + strlen("values"),
			location,
			&resume,
			out_start,
			out_end);
		if (source_status != 0) {
			if (cache != NULL) {
				cache->resume = resume;
				cache->search_position =
					pos + strlen("values");
				cache->last_location = location;
				cache->valid = 1;
			}
			return source_status;
		}
	}
	return 0;
}
