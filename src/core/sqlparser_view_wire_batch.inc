const sqlparser_wire_scalar_batch_t *sqlparser_query_graph_wire_scalar_batch(const sqlparser_handle_t *handle)
{
    const sqlparser_query_graph_cache_t *cache;
    if (handle == NULL || handle->failed || (cache = handle->query_graph) == NULL ||
        cache->statement_count < 2U || cache->generation != handle->generation || handle->query_graph_generation != handle->generation ||
        !sqlparser_wire_scalar_batch_is_current(cache->wire_scalar_batch, handle)) return NULL;
    return cache->wire_scalar_batch;
}

const char *sqlparser_query_graph_wire_scalar_batch_string(const sqlparser_handle_t *handle,
    size_t statement, size_t row, size_t column)
{
    const sqlparser_wire_scalar_batch_t *batch = sqlparser_query_graph_wire_scalar_batch(handle);
    const sqlparser_wire_scalar_batch_statement_t *descriptor;
    const sqlparser_graph_dml_cell_cache_t *cell;
    if (batch == NULL || statement >= batch->statement_count) return NULL;
    descriptor = &batch->statements[statement];
    if (row >= descriptor->insert.row_count || column >= descriptor->insert.column_count) return NULL;
    cell = &handle->query_graph->dml_cells[descriptor->cell_offset + row * descriptor->insert.column_count + column];
    return cell->kind == SQLPARSER_GRAPH_VALUE_LITERAL && cell->payload.literal.kind == SQLPARSER_LITERAL_KIND_STRING ?
        cell->payload.literal.string_value : NULL;
}

static sqlparser_status_t sqlparser_query_graph_try_wire_scalar_batch(
    sqlparser_handle_t *handle, sqlparser_query_graph_cache_t **out_cache,
    sqlparser_error_t *out_error)
{
    sqlparser_wire_scalar_batch_t *batch;
    sqlparser_query_graph_cache_t *cache;
    sqlparser_wire_scalar_cell_t *cells = NULL;
    size_t s, row, column, maximum_columns = 0U, index_count, index_base = 0U;
    char *text;
    *out_cache = NULL;
#ifdef SQLPARSER_DISABLE_WIRE_SCALAR_INSERT_GRAPH
    (void)handle; (void)out_error;
    return SQLPARSER_STATUS_OK;
#endif
    batch = sqlparser_wire_scalar_batch_certify(handle);
    if (batch == NULL) return SQLPARSER_STATUS_OK;
    if (batch->cell_count > SIZE_MAX / sizeof(*cache->dml_cells) ||
        batch->column_count > SIZE_MAX / sizeof(*cache->dml_columns) ||
        batch->statement_count > SIZE_MAX / sizeof(*cache->statements) ||
        batch->statement_count > SIZE_MAX / sizeof(*cache->blocks) ||
        batch->statement_count > SIZE_MAX / sizeof(*cache->relations) ||
        batch->statement_count > SIZE_MAX / sizeof(*cache->dml) ||
        batch->cell_count > SIZE_MAX - batch->column_count ||
        batch->cell_count + batch->column_count > SIZE_MAX - batch->statement_count) goto miss;
    index_count = batch->cell_count + batch->column_count + batch->statement_count;
    if (index_count > SIZE_MAX / sizeof(*cache->index_pool)) goto miss;
    for (s = 0U; s < batch->statement_count; s++)
        if (maximum_columns < batch->statements[s].insert.column_count)
            maximum_columns = batch->statements[s].insert.column_count;
    if (maximum_columns > SIZE_MAX / sizeof(*cells)) goto miss;
    cache = calloc(1U, sizeof(*cache));
    if (cache == NULL) { sqlparser_wire_scalar_batch_destroy(batch); goto no_memory; }
    cache->wire_scalar_batch = batch;
    cache->generation = handle->generation; cache->statement_count = batch->statement_count;
    cache->statements = calloc(batch->statement_count, sizeof(*cache->statements));
    cache->blocks = calloc(batch->statement_count, sizeof(*cache->blocks));
    cache->relations = calloc(batch->statement_count, sizeof(*cache->relations));
    cache->dml = calloc(batch->statement_count, sizeof(*cache->dml));
    cache->dml_columns = calloc(batch->column_count, sizeof(*cache->dml_columns));
    cache->dml_cells = calloc(batch->cell_count, sizeof(*cache->dml_cells));
    cache->index_pool = malloc(index_count * sizeof(*cache->index_pool));
    cache->value_text = malloc(batch->text_bytes);
    cells = malloc(maximum_columns * sizeof(*cells));
    if (cache->statements == NULL || cache->blocks == NULL || cache->relations == NULL || cache->dml == NULL ||
        cache->dml_columns == NULL || cache->dml_cells == NULL || cache->index_pool == NULL ||
        cache->value_text == NULL || cells == NULL) {
        sqlparser_query_graph_cache_release(cache); free(cells); goto no_memory;
    }
    cache->block_count = cache->block_capacity = batch->statement_count;
    cache->relation_count = cache->relation_capacity = batch->statement_count;
    cache->dml_count = cache->dml_capacity = batch->statement_count;
    cache->dml_column_count = cache->dml_column_capacity = batch->column_count;
    cache->dml_cell_count = cache->dml_cell_capacity = batch->cell_count;
    cache->index_pool_count = cache->index_pool_capacity = index_count;
    cache->value_text_length = cache->value_text_capacity = batch->text_bytes;
    text = cache->value_text;
    for (s = 0U; s < batch->statement_count; s++) {
        const sqlparser_wire_scalar_batch_statement_t *descriptor = &batch->statements[s];
        const sqlparser_wire_scalar_insert_t *insert = &descriptor->insert;
        sqlparser_statement_graph_t *statement = &cache->statements[s];
        size_t columns = insert->column_count, count = insert->row_count * columns;
        statement->has_root_block = 1;
        statement->block_offset = statement->relation_offset = statement->dml_offset = s;
        statement->block_count = statement->relation_count = statement->dml_count = 1U;
        statement->dml_column_offset = descriptor->column_offset;
        statement->dml_cell_offset = descriptor->cell_offset;
        statement->dml_column_count = columns; statement->dml_cell_count = count;
        cache->blocks[s].statement_index = s;
        cache->blocks[s].kind = SQLPARSER_GRAPH_BLOCK_SELECT;
        cache->blocks[s].relations.offset = index_base + columns + count;
        cache->blocks[s].relations.count = 1U;
        cache->relations[s].statement_index = s;
        cache->relations[s].kind = SQLPARSER_GRAPH_REL_BASE;
        cache->relations[s].selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
        cache->relations[s].selector.statement_index = s;
        cache->relations[s].has_selector = 1;
        cache->dml[s].statement_index = s;
        cache->dml[s].kind = SQLPARSER_GRAPH_DML_INSERT;
        cache->dml[s].insert_mode = SQLPARSER_GRAPH_INSERT_MODE_VALUES;
        cache->dml[s].has_target_relation = 1;
        cache->dml[s].target_columns.offset = index_base;
        cache->dml[s].target_columns.count = columns;
        cache->dml[s].rows.offset = index_base + columns;
        cache->dml[s].rows.count = count;
        for (column = 0U; column < columns + 3U; column++) {
            const sqlparser_wire_scalar_name_t *name = &insert->names[column];
            if (name->text == NULL) continue;
            if (column == 0U) cache->relations[s].database_name = text;
            else if (column == 1U) cache->relations[s].schema_name = text;
            else if (column == 2U) cache->relations[s].object_name = text;
            else {
                sqlparser_graph_dml_column_t *target = &cache->dml_columns[descriptor->column_offset + column - 3U];
                target->column_name = text; target->index = target->ordinal = column - 3U;
                target->statement_index = s;
            }
            memcpy(text, name->text, name->length); text += name->length; *text++ = '\0';
        }
        for (column = 0U; column < columns; column++) cache->index_pool[index_base + column] = column;
        for (row = 0U; row < insert->row_count; row++) {
            if (!sqlparser_wire_scalar_insert_certified_row(insert, row, cells)) {
                sqlparser_query_graph_cache_release(cache); free(cells);
                sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR,
                    "certified scalar INSERT batch wire changed during graph construction");
                return SQLPARSER_STATUS_INTERNAL_ERROR;
            }
            for (column = 0U; column < columns; column++) {
                size_t local = row * columns + column;
                sqlparser_graph_dml_cell_cache_t *cell = &cache->dml_cells[descriptor->cell_offset + local];
                cell->row_index = row; cell->column_ordinal = column;
                cell->kind = (uint8_t)SQLPARSER_GRAPH_VALUE_LITERAL;
                cell->selector_kind = (uint8_t)SQLPARSER_SELECTOR_KIND_INSERT_CELL;
                cell->flags = SQLPARSER_GRAPH_DML_CELL_HAS_SELECTOR;
                if (cells[column].kind == SQLPARSER_WIRE_SCALAR_INTEGER) {
                    cell->payload.literal.kind = SQLPARSER_LITERAL_KIND_INTEGER;
                    cell->payload.literal.integer_value = cells[column].integer;
                } else {
                    if (cells[column].kind == SQLPARSER_WIRE_SCALAR_STRING) {
                        cell->payload.literal.kind = SQLPARSER_LITERAL_KIND_STRING;
                        cell->payload.literal.string_value = text;
                    } else if (cells[column].kind == SQLPARSER_WIRE_SCALAR_FLOAT) {
                        cell->payload.literal.kind = SQLPARSER_LITERAL_KIND_FLOAT;
                        cell->payload.literal.float_value = text;
                    } else {
                        cell->kind = (uint8_t)SQLPARSER_GRAPH_VALUE_EXPRESSION;
                        cell->flags |= SQLPARSER_GRAPH_DML_CELL_HAS_EXPRESSION_SQL;
                        cell->payload.expression_sql_offset = (size_t)(text - cache->value_text);
                    }
                    memcpy(text, cells[column].text, cells[column].length);
                    text += cells[column].length; *text++ = '\0';
                }
                cache->index_pool[index_base + columns + local] = local;
            }
        }
        cache->index_pool[index_base + columns + count] = 0U;
        index_base += columns + count + 1U;
    }
    free(cells);
    if ((size_t)(text - cache->value_text) != batch->text_bytes || index_base != index_count) {
        sqlparser_query_graph_cache_release(cache);
        sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INTERNAL_ERROR,
            "certified scalar INSERT batch graph size mismatch");
        return SQLPARSER_STATUS_INTERNAL_ERROR;
    }
    *out_cache = cache;
    return SQLPARSER_STATUS_OK;
miss:
    sqlparser_wire_scalar_batch_destroy(batch);
    return SQLPARSER_STATUS_OK;
no_memory:
    sqlparser_error_set_message(out_error, SQLPARSER_STATUS_NO_MEMORY, "out of memory");
    return SQLPARSER_STATUS_NO_MEMORY;
}
