/* Private compact construction from the exact owned source proof. Admission
 * remains the existing complete scalar recognizer; this consumer cannot widen
 * its syntax or replace its native fallback. Only ordinary malloc/free are
 * used, so neither PostgreSQL initialization nor a memory context is needed. */

static bool
pg_query_compact_scalar_span(size_t input_length, PgQuerySimpleInsertSlice span)
{
    return span.start <= input_length && span.length <= input_length - span.start &&
        span.start <= UINT32_MAX && span.length <= UINT32_MAX;
}

static bool
pg_query_compact_scalar_add_text(size_t length, size_t *total)
{
    if (length >= SIZE_MAX - *total)
        return false;
    *total += length + 1U;
    return true;
}

int
pg_query_take_mysql_compact_insert(const char *owned_input, size_t input_length,
    PgQueryMysqlOwnedScalarInsertPlan *plan, PgQueryCompactScalarInsert **out)
{
    PgQueryMysqlOwnedScalarInsertPlanData data = {0};
    const PgQueryScalarInsertSource *source = &data.source;
    PgQueryCompactScalarInsert *compact;
    PgQuerySimpleInsertSlice name;
    size_t name_count, cell_count, bytes, pos, i, row, column;
    size_t strings = 0U, text_bytes = 0U;

    if (out == NULL)
        return 0;
    *out = NULL;
    if (owned_input == NULL || plan == NULL)
        return 0;
    memcpy(&data, plan->opaque, sizeof(data));
    if (data.valid != PG_QUERY_MYSQL_OWNED_PLAN_VALID ||
        data.owner != owned_input || data.length != input_length ||
        source->length != input_length ||
        !pg_query_simple_insert_source_size(input_length) ||
        !source->graph_compatible || source->rows < 32U ||
        source->columns == 0U || source->relation_parts == 0U ||
        source->relation_parts > 3U || source->statement_start != 0U ||
        source->statement_length < 0 ||
        (size_t) source->statement_length > input_length ||
        source->columns_start > input_length || source->values_start > input_length ||
        !pg_query_simple_insert_rows_size(source->rows) ||
        !pg_query_simple_insert_rows_size(source->columns))
        return 0;

    /* Check the complete single-allocation layout before doing arithmetic.
     * Both trailing arrays have uint32_t alignment, also present in the header. */
    if (source->columns > SIZE_MAX - 3U ||
        source->rows > SIZE_MAX / source->columns)
        return 0;
    name_count = source->columns + 3U;
    cell_count = source->rows * source->columns;
    if (name_count > (SIZE_MAX - sizeof(*compact)) / sizeof(*compact->names))
        return 0;
    bytes = sizeof(*compact) + name_count * sizeof(*compact->names);
    if (cell_count > (SIZE_MAX - bytes) / sizeof(*compact->cells) ||
        source->strings > cell_count)
        return 0;
    bytes += cell_count * sizeof(*compact->cells);
    compact = malloc(bytes);
    if (compact == NULL)
        return -1;
    memset(compact, 0, sizeof(*compact));
    compact->sql = owned_input;
    compact->sql_length = input_length;
    compact->row_count = source->rows;
    compact->column_count = source->columns;
    compact->string_count = source->strings;
    compact->text_bytes = source->graph_text_bytes;
    compact->raw_length = (uint32_t) source->statement_length;
    compact->names = (PgQueryCompactScalarName *) (compact + 1);
    compact->cells = (PgQueryCompactScalarCell *) (compact->names + name_count);
    memset(compact->names, 0, 3U * sizeof(*compact->names));

    /* Repeat only allocation-free lexical routines already used by the proof.
     * Any unexpected reconstruction failure leaves the old plan available. */
    pos = 0U;
    if (!pg_query_simple_insert_word(owned_input, input_length, &pos, "insert", 6U) ||
        !pg_query_simple_insert_word(owned_input, input_length, &pos, "into", 4U))
        goto miss;
    for (i = 0U; i < source->relation_parts; ++i)
    {
        size_t index = 3U - source->relation_parts + i;
        if ((i != 0U &&
             !pg_query_simple_insert_punctuation(owned_input, input_length, &pos, '.')) ||
            !pg_query_simple_insert_name(owned_input, input_length, &pos, &name) ||
            name.start != source->relation[i].start ||
            name.length != source->relation[i].length ||
            !pg_query_compact_scalar_span(input_length, name) || name.length == 0U ||
            !pg_query_compact_scalar_add_text(name.length, &text_bytes))
            goto miss;
        compact->names[index].offset = (uint32_t) name.start;
        compact->names[index].length = (uint32_t) name.length;
    }
    if (!pg_query_simple_insert_punctuation(owned_input, input_length, &pos, '(') ||
        pos != source->columns_start)
        goto miss;
    for (column = 0U; column < source->columns; ++column)
    {
        if (!pg_query_simple_insert_name(owned_input, input_length, &pos, &name) ||
            !pg_query_simple_insert_punctuation(owned_input, input_length, &pos,
                column + 1U == source->columns ? ')' : ',') ||
            !pg_query_compact_scalar_span(input_length, name) || name.length == 0U ||
            !pg_query_compact_scalar_add_text(name.length, &text_bytes))
            goto miss;
        compact->names[column + 3U].offset = (uint32_t) name.start;
        compact->names[column + 3U].length = (uint32_t) name.length;
    }
    if (!pg_query_simple_insert_word(owned_input, input_length, &pos, "values", 6U) ||
        pos != source->values_start)
        goto miss;
    for (row = 0U; row < source->rows; ++row)
    {
        if ((row != 0U &&
             !pg_query_simple_insert_punctuation(owned_input, input_length, &pos, ',')) ||
            !pg_query_simple_insert_punctuation(owned_input, input_length, &pos, '('))
            goto miss;
        for (column = 0U; column < source->columns; ++column)
        {
            PgQueryScalarInsertCell cell = {0};
            PgQueryCompactScalarCell *out_cell =
                &compact->cells[row * source->columns + column];
            if (!pg_query_scalar_insert_cell(owned_input, input_length, &pos, &cell) ||
                !pg_query_simple_insert_punctuation(owned_input, input_length, &pos,
                    column + 1U == source->columns ? ')' : ',') ||
                !pg_query_compact_scalar_span(input_length, cell.text) ||
                cell.location < 0 || (size_t) cell.location >= input_length)
                goto miss;
            out_cell->text_offset = (uint32_t) cell.text.start;
            out_cell->text_length = (uint32_t) cell.text.length;
            out_cell->location = (int32_t) cell.location;
            out_cell->integer = 0;
            switch (cell.kind)
            {
                case PG_QUERY_SCALAR_INTEGER:
                case PG_QUERY_SCALAR_FLOAT:
                    if (cell.negative || cell.text.length == 0U ||
                        !pg_query_scalar_insert_digit(owned_input[cell.text.start]))
                        goto miss;
                    out_cell->kind = cell.kind == PG_QUERY_SCALAR_INTEGER ?
                        PG_QUERY_COMPACT_SCALAR_INTEGER : PG_QUERY_COMPACT_SCALAR_FLOAT;
                    if (cell.kind == PG_QUERY_SCALAR_INTEGER)
                        out_cell->integer = cell.integer;
                    break;
                case PG_QUERY_SCALAR_STRING:
                    out_cell->kind = PG_QUERY_COMPACT_SCALAR_STRING;
                    ++strings;
                    break;
                case PG_QUERY_SCALAR_SQL_VALUE:
                    if (cell.typmod != -1)
                        goto miss;
                    out_cell->kind = PG_QUERY_COMPACT_SCALAR_VALUE_FUNCTION;
                    break;
                default:
                    goto miss;
            }
            if (cell.kind != PG_QUERY_SCALAR_INTEGER &&
                !pg_query_compact_scalar_add_text(cell.text.length, &text_bytes))
                goto miss;
        }
    }
    pg_query_simple_insert_gap(owned_input, input_length, &pos);
    if (pos < input_length && owned_input[pos] == ';')
    {
        if (pos != (size_t) source->statement_length)
            goto miss;
        ++pos;
        pg_query_simple_insert_gap(owned_input, input_length, &pos);
    }
    else if (source->statement_length != 0)
        goto miss;
    if (pos != input_length || strings != source->strings ||
        text_bytes != source->graph_text_bytes)
        goto miss;

    /* Publish only after every field and borrowed span is complete. */
    memset(plan, 0, sizeof(*plan));
    *out = compact;
    return 1;

miss:
    free(compact);
    return 0;
}
