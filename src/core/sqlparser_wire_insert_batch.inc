/* Separate complete-batch strict certificate. Reuses the existing bounded
 * source/wire readers; no dialect preprocessing/native provenance is minted. */
static int wsb_add(size_t *value, size_t add)
{
    if (add > SIZE_MAX - *value) return 0;
    *value += add;
    return 1;
}

void sqlparser_wire_scalar_batch_destroy(sqlparser_wire_scalar_batch_t *batch)
{
    size_t statement;
    if (batch == NULL) return;
    if (batch->statements != NULL) for (statement = 0U; statement < batch->statement_count; statement++) {
        free(batch->statements[statement].insert.names);
        free(batch->statements[statement].insert.row_offsets);
        free(batch->statements[statement].column_locations);
    }
    free(batch->statements);
    free(batch);
}

int sqlparser_wire_scalar_batch_is_current(const sqlparser_wire_scalar_batch_t *batch,
    const sqlparser_handle_t *handle)
{
    return batch != NULL && handle != NULL && batch->owner == handle && !handle->failed &&
        batch->ops == handle->dialect_ops && batch->dialect == handle->dialect &&
        batch->generation == handle->generation && batch->sql == handle->sql &&
        batch->sql_length == handle->sql_len && batch->parser_sql == handle->parser_sql &&
        batch->parser_sql_length == handle->parser_sql_len && batch->wire == handle->parse_tree.data &&
        batch->wire_length == handle->parse_tree.len && batch->statement_count == handle->statement_count &&
        handle->ast == NULL && handle->control == NULL && handle->current_sql == NULL &&
        handle->current_parser_sql == NULL && handle->patch_batch_flags == 0U &&
        handle->surface_source_edits.count == 0U && handle->surface_source_edits.items == NULL &&
        handle->identifier_mutation_count == 0U && handle->identifier_mutations == NULL &&
        handle->identifier_mutation_capacity == 0U && handle->identifier_spelling_count == 0U &&
        handle->identifier_spellings == NULL && handle->identifier_spelling_capacity == 0U &&
        handle->identifier_spelling_status == SQLPARSER_STATUS_OK &&
        (handle->generation == 0UL || handle->surface_source_complete) &&
        sqlparser_dialect_state_is_plain_insert_batch_strings(handle, batch->statement_count, batch->string_count);
}

sqlparser_wire_scalar_batch_t *sqlparser_wire_scalar_batch_certify(const sqlparser_handle_t *handle)
{
    sqlparser_wire_scalar_batch_t *batch;
    wi_reader parse;
    const uint8_t *base;
    const char *sql;
    size_t length, position = 0U, expected_raw_start = 0U, s, retained;
    if (handle == NULL || !sqlparser_dialect_is_sqlserver_compatible(handle->dialect) ||
        !sqlparser_dialect_supports_plain_scalar_insert(handle) || handle->failed ||
        handle->ast != NULL || handle->sql == NULL || handle->parser_sql == NULL ||
        handle->statement_count < 2U || handle->sql_len != handle->parser_sql_len ||
        handle->sql_len < 192U || handle->sql_len > INT32_MAX || handle->sql_len > PTRDIFF_MAX ||
        handle->parse_tree.data == NULL || handle->parse_tree.len == 0U ||
        handle->parse_tree.len > UINT32_MAX || handle->parse_tree.len > PTRDIFF_MAX ||
        handle->statement_count > handle->parse_tree.len / 2U ||
        handle->statement_count > SIZE_MAX / sizeof(*batch->statements)) return NULL;
    sql = handle->sql;
    length = handle->sql_len;
    {
        size_t prefix = 0U;
        if (!wi_word(sql, length, &prefix, "insert", 6U)) return NULL;
    }
    if (sql[length] != '\0' || memcmp(sql, handle->parser_sql, length + 1U) != 0 || !wsi_schemas_match()) return NULL;
    batch = calloc(1U, sizeof(*batch));
    if (batch == NULL) return NULL;
    batch->owner = handle; batch->ops = handle->dialect_ops; batch->dialect = handle->dialect;
    batch->generation = handle->generation; batch->sql = sql; batch->sql_length = length;
    batch->parser_sql = handle->parser_sql; batch->parser_sql_length = handle->parser_sql_len;
    batch->wire = handle->parse_tree.data; batch->wire_length = handle->parse_tree.len;
    batch->statement_count = handle->statement_count;
    batch->statements = calloc(batch->statement_count, sizeof(*batch->statements));
    if (batch->statements == NULL) goto miss;
    base = (const uint8_t *)batch->wire;
    parse.next = base; parse.end = base + batch->wire_length;
    if (!wi_optional_scalar(&parse, WI_PARSE_VERSION, &batch->version) || batch->version > INT32_MAX) goto miss;
    retained = sizeof(*batch);
    if (!wsb_add(&retained, batch->statement_count * sizeof(*batch->statements))) goto miss;
    for (s = 0U; s < batch->statement_count; s++) {
        sqlparser_wire_scalar_batch_statement_t *statement = &batch->statements[s];
        sqlparser_wire_scalar_insert_t *d = &statement->insert;
        wi_reader raw, node, insert, relation, columns, column, target, select_node, values, rows;
        const uint8_t *raw_start = parse.next;
        size_t index, row, count;
        uint32_t location;
        d->wire = batch->wire; d->wire_length = batch->wire_length;
        d->sql = sql; d->sql_length = length; d->version = batch->version;
        statement->cell_offset = batch->cell_count;
        statement->column_offset = batch->column_count;
        statement->row_offset = batch->row_count;
        if (!wi_message(&parse, WI_PARSE_STMTS, &raw) ||
            !wi_message(&raw, WI_RAW_STMT, &node) ||
            !wi_optional_scalar(&raw, WI_RAW_LOCATION, &statement->raw_location) ||
            !wi_optional_scalar(&raw, WI_RAW_LENGTH, &d->raw_length) || raw.next != raw.end ||
            statement->raw_location != expected_raw_start || d->raw_length > INT32_MAX ||
            !wi_message(&node, PG_QUERY__NODE__NODE_INSERT_STMT, &insert) || node.next != node.end ||
            !wi_word(sql, length, &position, "insert", 6U) || !wi_word(sql, length, &position, "into", 4U)) goto miss;
        statement->raw_wire_offset = (uint32_t)(raw_start - base);
        statement->raw_wire_length = (uint32_t)(parse.next - raw_start);
        d->prefix_offset = (uint32_t)(insert.next - base);
        if (!wi_message(&insert, WI_INSERT_RELATION, &relation)) goto miss;
        columns = insert;
        while (wi_key_is(&insert, WI_INSERT_COLS, 2U)) {
            if (!wi_message(&insert, WI_INSERT_COLS, &column) || d->column_count == SIZE_MAX) goto miss;
            d->column_count++;
        }
        if (d->column_count == 0U || d->column_count > SIZE_MAX / sizeof(*d->names) - 3U ||
            d->column_count > SIZE_MAX / sizeof(*statement->column_locations)) goto miss;
        d->prefix_length = (uint32_t)(insert.next - base) - d->prefix_offset;
        if (!wi_message(&insert, WI_INSERT_SELECT, &select_node) || !wi_enum(&insert, WI_INSERT_OVERRIDE, 1U) ||
            insert.next != insert.end || !wi_message(&select_node, PG_QUERY__NODE__NODE_SELECT_STMT, &values) ||
            select_node.next != select_node.end) goto miss;
        d->names = calloc(d->column_count + 3U, sizeof(*d->names));
        statement->column_locations = malloc(d->column_count * sizeof(*statement->column_locations));
        if (d->names == NULL || statement->column_locations == NULL) goto miss;
        for (index = 0U; index < 3U; index++) {
            sqlparser_wire_scalar_name_t *name = &d->names[index];
            if (wi_key_is(&relation, (unsigned)index + 1U, 2U) &&
                !wi_text(&relation, (unsigned)index + 1U, &name->text, &name->length)) goto miss;
        }
        if (d->names[2].text == NULL || (d->names[0].text != NULL && d->names[1].text == NULL) ||
            !wi_enum(&relation, 4U, 1U) || !wi_message(&relation, 5U, &node) ||
            node.end - node.next != 1 || *node.next != 'p' ||
            !wi_scalar(&relation, 7U, &location) || relation.next != relation.end) goto miss;
        wi_gap(sql, length, &position);
        if (location != position) goto miss;
        statement->relation_location = location;
        for (index = 0U; index < 3U; index++) {
            sqlparser_wire_scalar_name_t *name = &d->names[index];
            if (name->text == NULL) continue;
            if (!wsi_name_word(sql, length, &position, name->text, name->length) || !wsi_add_text(d, name->length) ||
                (index < 2U && !wi_punctuation(sql, length, &position, '.'))) goto miss;
        }
        if (!wi_punctuation(sql, length, &position, '(')) goto miss;
        for (index = 0U; index < d->column_count; index++) {
            sqlparser_wire_scalar_name_t *name = &d->names[index + 3U];
            if (!wi_message(&columns, WI_INSERT_COLS, &column) ||
                !wi_message(&column, PG_QUERY__NODE__NODE_RES_TARGET, &target) || column.next != column.end ||
                !wi_text(&target, 1U, &name->text, &name->length) ||
                !wi_scalar(&target, 4U, &location) || target.next != target.end) goto miss;
            wi_gap(sql, length, &position);
            if (location != position || !wsi_name_word(sql, length, &position, name->text, name->length) ||
                !wsi_add_text(d, name->length) ||
                !wi_punctuation(sql, length, &position, index + 1U == d->column_count ? ')' : ',')) goto miss;
            statement->column_locations[index] = location;
        }
        if (!wi_word(sql, length, &position, "values", 6U)) goto miss;
        rows = values;
        while (wi_key_is(&values, WI_SELECT_VALUES, 2U)) {
            wi_reader list;
            if (!wi_message(&values, WI_SELECT_VALUES, &node) ||
                !wi_message(&node, PG_QUERY__NODE__NODE_LIST, &list) || node.next != node.end ||
                (d->row_count != 0U && !wi_punctuation(sql, length, &position, ',')) ||
                !wi_punctuation(sql, length, &position, '(')) goto miss;
            for (index = 0U; index < d->column_count; index++) {
                sqlparser_wire_scalar_cell_t cell;
                if (!wsi_cell(&list, &cell) || !wsi_source_cell(sql, length, &position, &cell) ||
                    !wi_punctuation(sql, length, &position, index + 1U == d->column_count ? ')' : ',')) goto miss;
                if (cell.kind != SQLPARSER_WIRE_SCALAR_INTEGER && !wsi_add_text(d, cell.length)) goto miss;
                if (cell.kind == SQLPARSER_WIRE_SCALAR_STRING && !wsb_add(&d->string_count, 1U)) goto miss;
            }
            if (list.next != list.end || d->row_count == UINT32_MAX) goto miss;
            d->row_count++;
        }
        if (d->row_count < 32U || !wi_enum(&values, WI_SELECT_LIMIT, 1U) ||
            !wi_enum(&values, WI_SELECT_OP, 1U) || values.next != values.end) goto miss;
        wi_gap(sql, length, &position);
        if (position < length && sql[position] == ';') {
            if (d->raw_length != position - expected_raw_start) goto miss;
            expected_raw_start = ++position;
            wi_gap(sql, length, &position);
        } else if (d->raw_length != 0U || s + 1U != batch->statement_count || position != length) goto miss;
        if (d->row_count > SIZE_MAX / d->column_count || d->row_count > SIZE_MAX / sizeof(*d->row_offsets)) goto miss;
        count = d->row_count * d->column_count;
        if (!wsb_add(&batch->cell_count, count) || !wsb_add(&batch->row_count, d->row_count) ||
            !wsb_add(&batch->column_count, d->column_count) || !wsb_add(&batch->string_count, d->string_count) ||
            !wsb_add(&batch->text_bytes, d->text_bytes) ||
            !wsb_add(&retained, d->text_bytes) ||
            !wsb_add(&retained, (d->column_count + 3U) * sizeof(*d->names)) ||
            !wsb_add(&retained, d->column_count * sizeof(*statement->column_locations)) ||
            !wsb_add(&retained, d->row_count * sizeof(*d->row_offsets))) goto miss;
        d->row_offsets = malloc(d->row_count * sizeof(*d->row_offsets));
        if (d->row_offsets == NULL) goto miss;
        for (row = 0U; row < d->row_count; row++) {
            d->row_offsets[row] = (uint32_t)(rows.next - base);
            if (!wi_message(&rows, WI_SELECT_VALUES, &node)) goto miss;
        }
    }
    if (position != length || parse.next != parse.end || batch->cell_count > SIZE_MAX / 32U ||
        retained > batch->cell_count * 32U || !sqlparser_wire_scalar_batch_is_current(batch, handle)) goto miss;
    return batch;
miss:
    sqlparser_wire_scalar_batch_destroy(batch);
    return NULL;
}

int sqlparser_wire_scalar_batch_certified_cell(const sqlparser_wire_scalar_batch_t *batch,
    size_t statement, size_t row, size_t column, sqlparser_wire_scalar_cell_t *cell)
{
    return batch != NULL && batch->statements != NULL && statement < batch->statement_count &&
        sqlparser_wire_scalar_insert_certified_cell(&batch->statements[statement].insert, row, column, cell);
}

typedef struct {
    uint32_t relation, prefix, select, select_node, insert, insert_node, raw;
    uint32_t raw_location, raw_length;
    int64_t prefix_delta;
} wsb_sizes;

static int wsb_location(uint32_t old, int64_t delta, uint32_t *out)
{
    int64_t shifted = (int64_t)old + delta;
    if (old > INT32_MAX || shifted < 0 || shifted > INT32_MAX) return 0;
    *out = (uint32_t)shifted;
    return 1;
}

/* Header names keep their payload bytes; every absolute source location and
 * enclosing length is freshly measured/written, even for unpatched statements. */
static int wsb_measure_prefix(const sqlparser_wire_scalar_batch_statement_t *statement,
    int64_t delta, uint32_t *relation_size, uint32_t *prefix_size)
{
    const sqlparser_wire_scalar_insert_t *d = &statement->insert;
    uint64_t size = 0U, prefix;
    uint32_t location;
    size_t index;
    for (index = 0U; index < 3U; index++) if (d->names[index].text != NULL)
        size += wi_envelope((unsigned)index + 1U, d->names[index].length);
    if (!wsb_location(statement->relation_location, delta, &location)) return 0;
    size += wi_scalar_size(4U, 1U) + wi_envelope(5U, 1U) + wi_scalar_size(7U, location);
    if (size > UINT32_MAX) return 0;
    *relation_size = (uint32_t)size;
    prefix = wi_envelope(WI_INSERT_RELATION, *relation_size);
    for (index = 0U; index < d->column_count; index++) {
        if (!wsb_location(statement->column_locations[index], delta, &location)) return 0;
        size = wi_envelope(1U, d->names[index + 3U].length) + wi_scalar_size(4U, location);
        if (size > UINT32_MAX) return 0;
        size = wi_envelope(PG_QUERY__NODE__NODE_RES_TARGET, (uint32_t)size);
        if (size > UINT32_MAX) return 0;
        prefix += wi_envelope(WI_INSERT_COLS, (uint32_t)size);
        if (prefix > UINT32_MAX) return 0;
    }
    *prefix_size = (uint32_t)prefix;
    return 1;
}

static int wsb_write_prefix(wi_writer *writer, const sqlparser_wire_scalar_batch_statement_t *statement,
    const wsb_sizes *sizes)
{
    const sqlparser_wire_scalar_insert_t *d = &statement->insert;
    uint32_t location;
    size_t index;
    wi_write_header(writer, WI_INSERT_RELATION, sizes->relation);
    for (index = 0U; index < 3U; index++) if (d->names[index].text != NULL) {
        wi_write_header(writer, (unsigned)index + 1U, d->names[index].length);
        wi_write_bytes(writer, d->names[index].text, d->names[index].length);
    }
    wi_write_scalar(writer, 4U, 1U);
    wi_write_header(writer, 5U, 1U); wi_write_bytes(writer, "p", 1U);
    if (!wsb_location(statement->relation_location, sizes->prefix_delta, &location)) return 0;
    wi_write_scalar(writer, 7U, location);
    for (index = 0U; index < d->column_count; index++) {
        uint64_t target, node;
        if (!wsb_location(statement->column_locations[index], sizes->prefix_delta, &location)) return 0;
        target = wi_envelope(1U, d->names[index + 3U].length) + wi_scalar_size(4U, location);
        if (target > UINT32_MAX) return 0;
        node = wi_envelope(PG_QUERY__NODE__NODE_RES_TARGET, (uint32_t)target);
        if (node > UINT32_MAX) return 0;
        wi_write_header(writer, WI_INSERT_COLS, (uint32_t)node);
        wi_write_header(writer, PG_QUERY__NODE__NODE_RES_TARGET, (uint32_t)target);
        wi_write_header(writer, 1U, d->names[index + 3U].length);
        wi_write_bytes(writer, d->names[index + 3U].text, d->names[index + 3U].length);
        wi_write_scalar(writer, 4U, location);
    }
    return !writer->failed;
}

sqlparser_status_t sqlparser_wire_scalar_batch_pack_proven_edits(const sqlparser_wire_scalar_batch_t *batch,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out)
{
    wsb_sizes *statements = NULL;
    uint32_t *row_sizes = NULL;
    uint64_t total;
    size_t s, row, column, edit_index = 0U;
    int64_t delta = 0;
    wi_writer writer;
    if (out == NULL) return SQLPARSER_STATUS_INVALID_ARGUMENT;
    out->data = NULL; out->len = 0U;
    if (batch == NULL || edits == NULL) return SQLPARSER_STATUS_INVALID_ARGUMENT;
    if (!sqlparser_wire_scalar_batch_is_current(batch, batch->owner) || !wi_edits(edits, 1) ||
        batch->statement_count > SIZE_MAX / sizeof(*statements) ||
        batch->row_count > SIZE_MAX / sizeof(*row_sizes)) return SQLPARSER_STATUS_UNSUPPORTED;
    statements = calloc(batch->statement_count, sizeof(*statements));
    row_sizes = malloc(batch->row_count * sizeof(*row_sizes));
    if (statements == NULL || row_sizes == NULL) goto unsupported;
    total = wi_scalar_size(WI_PARSE_VERSION, batch->version);
    for (s = 0U; s < batch->statement_count; s++) {
        const sqlparser_wire_scalar_batch_statement_t *statement = &batch->statements[s];
        const sqlparser_wire_scalar_insert_t *d = &statement->insert;
        wsb_sizes *sizes = &statements[s];
        uint64_t n = 0U;
        sizes->prefix_delta = delta;
        if (!wsb_location(statement->raw_location, delta, &sizes->raw_location) ||
            !wsb_measure_prefix(statement, delta, &sizes->relation, &sizes->prefix)) goto unsupported;
        for (row = 0U; row < d->row_count; row++) {
            wi_reader list;
            uint64_t row_size = 0U, node;
            if (!wsi_read_row(d, row, &list, 1)) goto unsupported;
            for (column = 0U; column < d->column_count; column++) {
                sqlparser_wire_scalar_cell_t cell;
                wi_cell_sizes cell_sizes;
                if (!wsi_read_cell(&list, &cell, 1) || !wsi_effective_cell(&cell, edits, &edit_index, &delta) ||
                    !wsi_measure_cell(&cell, &cell_sizes)) goto unsupported;
                row_size += wi_envelope(WI_LIST_ITEMS, cell_sizes.node);
                if (row_size > UINT32_MAX) goto unsupported;
            }
            if (list.next != list.end) goto unsupported;
            row_sizes[statement->row_offset + row] = (uint32_t)row_size;
            node = wi_envelope(PG_QUERY__NODE__NODE_LIST, (uint32_t)row_size);
            if (node > UINT32_MAX) goto unsupported;
            n += wi_envelope(WI_SELECT_VALUES, (uint32_t)node);
            if (n > UINT32_MAX) goto unsupported;
        }
        if ((int64_t)batch->sql_length + delta < 0 || (int64_t)batch->sql_length + delta > INT32_MAX) goto unsupported;
        sizes->raw_length = d->raw_length;
        if (sizes->raw_length != 0U) {
            int64_t shifted = (int64_t)d->raw_length + delta - sizes->prefix_delta;
            if (shifted <= 0 || shifted > INT32_MAX) goto unsupported;
            sizes->raw_length = (uint32_t)shifted;
        }
        n += wi_scalar_size(WI_SELECT_LIMIT, 1U) + wi_scalar_size(WI_SELECT_OP, 1U);
        if (n > UINT32_MAX) goto unsupported;
        sizes->select = (uint32_t)n;
        n = wi_envelope(PG_QUERY__NODE__NODE_SELECT_STMT, sizes->select);
        if (n > UINT32_MAX) goto unsupported;
        sizes->select_node = (uint32_t)n;
        n = sizes->prefix + wi_envelope(WI_INSERT_SELECT, sizes->select_node) + wi_scalar_size(WI_INSERT_OVERRIDE, 1U);
        if (n > UINT32_MAX) goto unsupported;
        sizes->insert = (uint32_t)n;
        n = wi_envelope(PG_QUERY__NODE__NODE_INSERT_STMT, sizes->insert);
        if (n > UINT32_MAX) goto unsupported;
        sizes->insert_node = (uint32_t)n;
        n = wi_envelope(WI_RAW_STMT, sizes->insert_node) +
            wi_scalar_size(WI_RAW_LOCATION, sizes->raw_location) + wi_scalar_size(WI_RAW_LENGTH, sizes->raw_length);
        if (n > UINT32_MAX) goto unsupported;
        sizes->raw = (uint32_t)n;
        total += wi_envelope(WI_PARSE_STMTS, sizes->raw);
        if (total > UINT32_MAX || total > SIZE_MAX || total > PTRDIFF_MAX) goto unsupported;
    }
    if (edit_index != edits->count) goto unsupported;
    out->data = malloc((size_t)total);
    if (out->data == NULL) { free(statements); free(row_sizes); return SQLPARSER_STATUS_NO_MEMORY; }
    writer.next = (uint8_t *)out->data; writer.end = writer.next + (size_t)total; writer.failed = 0;
    wi_write_scalar(&writer, WI_PARSE_VERSION, batch->version);
    edit_index = 0U; delta = 0;
    for (s = 0U; s < batch->statement_count; s++) {
        const sqlparser_wire_scalar_batch_statement_t *statement = &batch->statements[s];
        const sqlparser_wire_scalar_insert_t *d = &statement->insert;
        const wsb_sizes *sizes = &statements[s];
        if (delta != sizes->prefix_delta) { writer.failed = 1; break; }
        wi_write_header(&writer, WI_PARSE_STMTS, sizes->raw);
        wi_write_header(&writer, WI_RAW_STMT, sizes->insert_node);
        wi_write_header(&writer, PG_QUERY__NODE__NODE_INSERT_STMT, sizes->insert);
        if (!wsb_write_prefix(&writer, statement, sizes)) { writer.failed = 1; break; }
        wi_write_header(&writer, WI_INSERT_SELECT, sizes->select_node);
        wi_write_header(&writer, PG_QUERY__NODE__NODE_SELECT_STMT, sizes->select);
        for (row = 0U; row < d->row_count; row++) {
            wi_reader list;
            uint32_t row_size = row_sizes[statement->row_offset + row];
            if (!wsi_read_row(d, row, &list, 1)) { writer.failed = 1; break; }
            wi_write_header(&writer, WI_SELECT_VALUES, (uint32_t)wi_envelope(PG_QUERY__NODE__NODE_LIST, row_size));
            wi_write_header(&writer, PG_QUERY__NODE__NODE_LIST, row_size);
            for (column = 0U; column < d->column_count; column++) {
                sqlparser_wire_scalar_cell_t cell;
                wi_cell_sizes cell_sizes;
                if (!wsi_read_cell(&list, &cell, 1) || !wsi_effective_cell(&cell, edits, &edit_index, &delta) ||
                    !wsi_measure_cell(&cell, &cell_sizes)) { writer.failed = 1; break; }
                wsi_write_cell(&writer, &cell, &cell_sizes);
            }
            if (writer.failed || list.next != list.end) { writer.failed = 1; break; }
        }
        if (writer.failed) break;
        wi_write_scalar(&writer, WI_SELECT_LIMIT, 1U);
        wi_write_scalar(&writer, WI_SELECT_OP, 1U);
        wi_write_scalar(&writer, WI_INSERT_OVERRIDE, 1U);
        wi_write_scalar(&writer, WI_RAW_LOCATION, sizes->raw_location);
        wi_write_scalar(&writer, WI_RAW_LENGTH, sizes->raw_length);
    }
    free(statements); free(row_sizes);
    if (writer.failed || writer.next != writer.end || edit_index != edits->count) {
        free(out->data); out->data = NULL;
        return SQLPARSER_STATUS_INTERNAL_ERROR;
    }
    out->len = (size_t)total;
    return SQLPARSER_STATUS_OK;
unsupported:
    free(statements); free(row_sizes);
    return SQLPARSER_STATUS_UNSUPPORTED;
}
