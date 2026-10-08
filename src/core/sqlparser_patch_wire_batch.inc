static int sqlparser_patch_certify_insert_batch_edits(const sqlparser_handle_t *handle,
    const sqlparser_wire_scalar_batch_t *batch, const sqlparser_surface_source_edits_t *edits,
    int all_raw_plain)
{
    size_t index, source_end = 0U;
    int64_t delta = 0;
    if (!sqlparser_wire_scalar_batch_is_current(batch, handle) || edits->count == 0U || edits->items == NULL) return 0;
    for (index = 0U; index < edits->count; index++) {
        const sqlparser_surface_source_edit_t *edit = &edits->items[index];
        if (edit->source_start < source_end || edit->source_end <= edit->source_start ||
            edit->source_end > handle->sql_len ||
            (int64_t)edit->source_start + delta < 0 || (int64_t)edit->source_start + delta > INT32_MAX ||
            edit->replacement == NULL || edit->replacement_length < 2U || edit->replacement_length > INT32_MAX ||
            (!all_raw_plain && (!sqlparser_patch_plain_ascii_string_sql(edit->replacement) ||
                               strlen(edit->replacement) != edit->replacement_length))) return 0;
        delta += (int64_t)edit->replacement_length - (int64_t)(edit->source_end - edit->source_start);
        if ((int64_t)handle->sql_len + delta < 0 || (int64_t)handle->sql_len + delta > INT32_MAX) return 0;
        source_end = edit->source_end;
    }
    /* Exact per-statement membership, shifted headers and nonzero RawStmt
     * lengths are independently checked by the canonical whole-wire packer. */
    return 1;
}

static sqlparser_status_t sqlparser_patch_try_wire_scalar_batch_strings(
    sqlparser_handle_t *handle, const sqlparser_patch_list_t *patches,
    int *out_handled, sqlparser_error_t *out_error)
{
    const sqlparser_wire_scalar_batch_t *batch = sqlparser_query_graph_wire_scalar_batch(handle);
    sqlparser_surface_source_edits_t edits = {0};
    sqlparser_view_expression_source_cache_t cache = {0};
    sqlparser_handle_t source_view;
    PgQueryProtobuf packed = {0};
    sqlparser_status_t status = SQLPARSER_STATUS_OK;
    char *sql = NULL;
    size_t index, sql_length, statement_count;
    int all_raw_plain = 1;
    *out_handled = 0;
    if (batch == NULL || patches->count == 0U || !sqlparser_wire_scalar_batch_is_current(batch, handle))
        return SQLPARSER_STATUS_OK;
    /* Silent whole-list eligibility only. Payload/selector diagnostics still
     * happen in caller order, through the original renderer and planner. */
    for (index = 0U; index < patches->count; index++) {
        const sqlparser_patch_t *patch = &patches->items[index];
        int raw_plain = 0;
        if (patch->op != SQLPARSER_PATCH_REPLACE || patch->name != NULL || patch->default_sql != NULL ||
            patch->source_selector != NULL || patch->bind != NULL || patch->bool_operator != 0 ||
            (patch->literal != NULL ?
                patch->literal->kind != SQLPARSER_LITERAL_KIND_STRING || patch->sql != NULL :
                patch->sql == NULL || !(raw_plain = sqlparser_patch_plain_ascii_string_sql(patch->sql))))
            return SQLPARSER_STATUS_OK;
        if (patch->literal != NULL || !raw_plain) all_raw_plain = 0;
    }
    sql_length = handle->sql_len;
    for (index = 0U; index < patches->count; index++) {
        const sqlparser_patch_t *patch = &patches->items[index];
        sqlparser_selector_t selector;
        sqlparser_wire_scalar_cell_t cell;
        PgQuery__Node node = PG_QUERY__NODE__INIT;
        PgQuery__AConst constant = PG_QUERY__A__CONST__INIT;
        PgQuery__String string = PG_QUERY__STRING__INIT;
        int supported = 0, preserves = 2;
        if (!sqlparser_patch_parse_insert_cell_selector(patch->selector, &selector) ||
            selector.statement_index >= batch->statement_count ||
            !sqlparser_wire_scalar_batch_certified_cell(batch, selector.statement_index,
                selector.row_index, selector.column_index, &cell) ||
            cell.kind != SQLPARSER_WIRE_SCALAR_STRING) goto done;
        string.sval = (char *)sqlparser_query_graph_wire_scalar_batch_string(handle,
            selector.statement_index, selector.row_index, selector.column_index);
        if (string.sval == NULL) goto done;
        constant.val_case = PG_QUERY__A__CONST__VAL_SVAL; constant.sval = &string; constant.location = cell.location;
        node.node_case = PG_QUERY__NODE__NODE_A_CONST; node.a_const = &constant;
        status = sqlparser_patch_plan_surface_edit(handle, patch, &selector,
            &edits, &cache, NULL, &node, NULL, &sql_length, all_raw_plain, 1, NULL, batch,
            &preserves, &supported, out_error);
        if (status != SQLPARSER_STATUS_OK || !supported || preserves != 2) goto done;
    }
    if (!sqlparser_patch_certify_insert_batch_edits(handle, batch, &edits, all_raw_plain)) goto done;
    memset(&source_view, 0, sizeof(source_view));
    source_view.sql = handle->sql; source_view.sql_len = handle->sql_len; source_view.limits = handle->limits;
    source_view.surface_source_complete = 1; source_view.surface_source_edits = edits;
    status = sqlparser_restore_source_envelope(&source_view, &sql, out_error);
    if (status != SQLPARSER_STATUS_OK) goto done;
    status = sqlparser_wire_scalar_batch_pack_proven_edits(batch, &edits, &packed);
    if (status == SQLPARSER_STATUS_UNSUPPORTED) { status = SQLPARSER_STATUS_OK; goto done; }
    if (status != SQLPARSER_STATUS_OK) {
        sqlparser_error_set_message(out_error, status,
            status == SQLPARSER_STATUS_NO_MEMORY ? "out of memory" : "failed to repack parse tree protobuf");
        goto done;
    }
    statement_count = batch->statement_count;
    /* All caller reads ended. The original graph and certificate may now die;
     * both full-source output buffers and every retained owner count are proved. */
    sqlparser_surface_source_edits_release(&edits);
    status = sqlparser_handle_commit_certified_insert_batch_wire(handle, &sql, &packed, statement_count, out_error);
    if (status == SQLPARSER_STATUS_OK) *out_handled = 1;
done:
    if (status != SQLPARSER_STATUS_OK) *out_handled = 1;
    sqlparser_surface_source_edits_release(&edits);
    free(sql); free(packed.data);
    return status;
}
