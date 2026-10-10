/* Independent reference statement and expression scanners.
 * Only static linkage and function names/internal references differ.
 * Do not call optimized
 * statement, generic multi, expression-source or top-level scanners. */

static int spancert_frozen_sqlparser_oracle_multi_insert_commit_owner(const sqlparser_handle_t *handle)
{
	const sqlparser_dialect_ops_t *owner;
	if (handle == NULL) return 0;
	if (handle->dialect == SQLPARSER_DIALECT_ORACLE) {
		owner = sqlparser_dialect_oracle_ops();
	} else if (handle->dialect == SQLPARSER_DIALECT_KINGBASE_ORACLE) {
		owner = sqlparser_dialect_kingbase_oracle_ops();
	} else if (handle->dialect == SQLPARSER_DIALECT_VASTBASE_ORACLE) {
		owner = sqlparser_dialect_vastbase_oracle_ops();
	} else {
		return 0;
	}
	return handle->dialect_ops == owner &&
		handle->dialect_ops == sqlparser_dialect_get_ops(handle->dialect);
}

static const sqlparser_dialect_multi_insert_t *
spancert_frozen_sqlparser_oracle_owned_multi_insert(const sqlparser_handle_t *handle)
{
    if (!spancert_frozen_sqlparser_oracle_multi_insert_commit_owner(handle) ||
        handle->dialect_state == NULL) return NULL;
    return sqlparser_oracle_state_multi_insert(handle->dialect_state);
}

static int spancert_frozen_sqlparser_oracle_multi_insert_source_is_current(const sqlparser_handle_t *handle)
{
	const sqlparser_dialect_multi_insert_t *multi = spancert_frozen_sqlparser_oracle_owned_multi_insert(handle);
	if (multi == NULL) return 0;
	return multi->oracle_source_provenance.sql != NULL &&
		multi->oracle_source_provenance.sql == handle->sql &&
		multi->oracle_source_provenance.parser_sql == handle->parser_sql &&
		multi->oracle_source_provenance.wire != NULL &&
		multi->oracle_source_provenance.wire == handle->parse_tree.data &&
		multi->oracle_source_provenance.state == handle->dialect_state &&
		multi->oracle_source_provenance.sql_length == handle->sql_len &&
		multi->oracle_source_provenance.parser_sql_length == handle->parser_sql_len &&
		multi->oracle_source_provenance.wire_length == handle->parse_tree.len &&
		multi->oracle_source_provenance.generation == handle->generation &&
		handle->surface_source_edits.count == 0U &&
		(handle->patch_batch_flags &
		 (SQLPARSER_PATCH_BATCH_AST_DIRTY | SQLPARSER_PATCH_BATCH_MULTI_INSERT_DIRTY)) == 0U;
}

static int spancert_frozen_sqlparser_oracle_multi_insert_certified_cell_span(
	const sqlparser_handle_t *handle, size_t branch_index, size_t column_index,
	size_t *out_start, size_t *out_end)
{
	const sqlparser_dialect_multi_insert_t *multi;
	const sqlparser_dialect_multi_insert_branch_t *branch;
	const sqlparser_oracle_cell_span_t *span;
	size_t id;
	if (out_start == NULL || out_end == NULL ||
	    !spancert_frozen_sqlparser_oracle_multi_insert_source_is_current(handle)) return 0;
	multi = spancert_frozen_sqlparser_oracle_owned_multi_insert(handle);
	if (!multi->oracle_spans_complete || !multi->oracle_spans_identity ||
	    !multi->oracle_outer_identity || multi->oracle_spans == NULL ||
	    multi->branches == NULL || branch_index >= multi->branch_count ||
	    multi->oracle_source_start > handle->sql_len) return 0;
	branch = &multi->branches[branch_index];
	if (branch->ordinal != branch_index || branch->cells == NULL ||
	    column_index >= branch->cell_count ||
	    column_index > SIZE_MAX - branch->oracle_span_base) return 0;
	id = branch->oracle_span_base + column_index;
	if (id >= multi->oracle_span_count) return 0;
	span = &multi->oracle_spans[id];
	if ((span->lexical_flags & SQLPARSER_ORACLE_CELL_IDENTITY) == 0U ||
	    span->source_length == 0U || span->source_start >= multi->oracle_source_start ||
	    span->source_length > multi->oracle_source_start - span->source_start) return 0;
	*out_start = span->source_start;
	*out_end = (size_t)span->source_start + span->source_length;
	return 1;
}
