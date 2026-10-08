/* Deterministic field-by-field Oracle state, including pointer relationships.
 * Owner pointers become preorder positions in the complete protobuf AST.
 * No padding bytes or process addresses enter a transcript. */
static const ProtobufCMessage *scalar_owner_root;
static size_t scalar_owner_walk(const ProtobufCMessage *m, const void *wanted, size_t *ordinal)
{
    size_t i, j, result;
    const unsigned char *base = (const unsigned char *)m;
    if (!m) return 0;
    ++*ordinal;
    if (m == wanted) return *ordinal;
    for (i = 0; i < m->descriptor->n_fields; ++i) {
        const ProtobufCFieldDescriptor *f = &m->descriptor->fields[i];
        if (f->type != PROTOBUF_C_TYPE_MESSAGE) continue;
        if ((f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) &&
            *(const int *)(base + f->quantifier_offset) != (int)f->id) continue;
        if (f->label == PROTOBUF_C_LABEL_REPEATED) {
            size_t n = *(const size_t *)(base + f->quantifier_offset);
            ProtobufCMessage *const *items = *(ProtobufCMessage *const *const *)(base + f->offset);
            for (j = 0; j < n; ++j)
                if ((result = scalar_owner_walk(items[j], wanted, ordinal))) return result;
        } else if ((result = scalar_owner_walk(
            *(ProtobufCMessage *const *)(base + f->offset), wanted, ordinal))) return result;
    }
    return 0;
}
static void scalar_record_owner(const void *owner)
{
    size_t ordinal = 0, identity = 0;
    if (owner) identity = scalar_owner_walk(scalar_owner_root, owner, &ordinal);
    record_number(owner != NULL); record_number(identity);
    /* A detached owner is explicitly visible as nonnull/zero, never silently
     * equated to NULL. Pre-AST state and clone-reset owners are legitimate. */
}
static void scalar_record_error(sqlparser_status_t status, const sqlparser_error_t *e)
{
    record_number(status); record_number(e != NULL);
    if (e) {
        record_number(e->code); record_number((unsigned)e->cursor);
        record_number((unsigned)e->line); record_number((unsigned)e->column);
        record_text(e->message); /* Also on OK: unquote may swallow NO_MEMORY. */
    }
}
static void scalar_record_value(const sqlparser_dialect_multi_insert_value_t *v)
{
    record_text(v->public_sql); record_text(v->parser_sql);
    record_number(v->has_bind); record_number(v->bind_kind);
    record_text(v->bind); record_text(v->bind_sql);
    record_number(v->bind_position); record_number(v->has_bind_position);
    record_number(v->has_literal); record_sqlparser_literal_view_t(&v->literal);
    record_text(v->literal_string_value); record_text(v->literal_float_value);
    record_number(v->public_sql && v->public_sql == v->parser_sql);
    record_number(v->literal.string_value && v->literal.string_value == v->literal_string_value);
    record_number(v->literal.float_value && v->literal.float_value == v->literal_float_value);
}
static void scalar_record_multi(const sqlparser_dialect_multi_insert_t *m,
    const sqlparser_handle_t *h, const sqlparser_oracle_state_t *state)
{
    size_t i, j;
    record_number(m != NULL); if (!m) return;
    record_number(m->mode); record_number(m->branch_count);
    record_text(m->source_public_sql); record_text(m->source_parser_sql);
    for (i = 0; i < m->branch_count; ++i) {
        const sqlparser_dialect_multi_insert_branch_t *b = &m->branches[i];
        record_number(b->ordinal);
        record_text(b->relation.database_name); record_text(b->relation.schema_name);
        record_text(b->relation.table_name); record_text(b->relation.link_name);
        record_text(b->relation.link_sql); record_text(b->relation.sql);
        record_number(b->column_count); record_number(b->cell_count);
        record_text(b->condition_public_sql); record_text(b->condition_parser_sql);
        record_number(b->has_condition); record_number(b->is_else);
        record_number(b->condition_group_id); record_number(b->oracle_span_base);
        for (j = 0; j < b->column_count; ++j) {
            record_text(b->columns[j].name); record_text(b->columns[j].sql);
        }
        for (j = 0; j < b->cell_count; ++j) scalar_record_value(&b->cells[j]);
    }
    record_number(m->oracle_spans != NULL); record_number(m->oracle_span_count);
    record_number(m->oracle_span_capacity); record_number(m->oracle_source_start);
    record_number(m->oracle_source_length); record_number(m->oracle_spans_complete);
    record_number(m->oracle_spans_identity); record_number(m->oracle_outer_identity);
    for (i = 0; i < m->oracle_span_count; ++i) {
        record_number(m->oracle_spans[i].source_start);
        record_number(m->oracle_spans[i].source_length);
        record_number(m->oracle_spans[i].lexical_flags);
    }
    record_number(m->oracle_pending_ids != NULL); record_number(m->oracle_pending_count);
    record_number(m->oracle_pending_capacity); record_number(m->oracle_pending_disabled);
    for (i = 0; i < m->oracle_pending_count; ++i) record_number(m->oracle_pending_ids[i]);
    record_number(m->oracle_source_provenance.sql != NULL);
    record_number(m->oracle_source_provenance.parser_sql != NULL);
    record_number(m->oracle_source_provenance.wire != NULL);
    record_number(m->oracle_source_provenance.state != NULL);
    record_number(m->oracle_source_provenance.sql_length);
    record_number(m->oracle_source_provenance.parser_sql_length);
    record_number(m->oracle_source_provenance.wire_length);
    record_number(m->oracle_source_provenance.generation);
    record_number(h && m->oracle_source_provenance.sql == h->sql);
    record_number(h && m->oracle_source_provenance.parser_sql == h->parser_sql);
    record_number(h && m->oracle_source_provenance.wire == h->parse_tree.data);
    record_number(m->oracle_source_provenance.state == state);
}
static void scalar_record_state(const sqlparser_oracle_state_t *s, const sqlparser_handle_t *h)
{
    size_t i;
    scalar_owner_root = h ? (const ProtobufCMessage *)h->ast : NULL;
    record_number(s != NULL); if (!s) return;
    record_number(s->bind_names != NULL); record_number(s->bind_count);
    record_number(s->bind_capacity); record_number(s->bind_occurrence_count);
    for (i = 0; i < s->bind_count; ++i) record_text(s->bind_names[i]);
    record_number(s->prepared_binds.names != NULL);
    record_number(s->prepared_binds.count); record_number(s->prepared_binds.capacity);
    record_number(s->prepared_binds.occurrence_count); record_number(s->prepared_binds.valid);
    for (i = 0; i < s->prepared_binds.count; ++i) record_text(s->prepared_binds.names[i]);
    record_number(s->national_literals.items != NULL);
    record_number(s->national_literals.count); record_number(s->national_literals.capacity);
    record_number(s->national_literals.literal_count); record_number(s->national_literals.fragment_start);
    record_number(s->national_literals.fragment_literal_base);
    for (i = 0; i < s->national_literals.count; ++i) {
        record_text(s->national_literals.items[i].sql);
        record_text(s->national_literals.items[i].surface_sql);
        record_number(s->national_literals.items[i].ordinal);
        scalar_record_owner(s->national_literals.items[i].owner);
    }
    record_number(s->dblink_relations != NULL); record_number(s->dblink_count);
    record_number(s->dblink_capacity); record_number(s->next_dblink_id);
    for (i = 0; i < s->dblink_count; ++i) {
        const sqlparser_oracle_dblink_relation_t *d = &s->dblink_relations[i];
        record_text(d->parser_object_name); record_text(d->public_object_name);
        record_text(d->public_link_name); record_text(d->public_object_sql);
        record_text(d->public_link_sql); scalar_record_owner(d->owner);
    }
    record_number(s->minuses.items != NULL); record_number(s->minuses.count);
    record_number(s->minuses.capacity); record_number(s->minuses.except_count);
    record_number(s->minuses.fragment_start); record_number(s->minuses.fragment_except_base);
    for (i = 0; i < s->minuses.count; ++i) {
        record_number(s->minuses.items[i].ordinal); scalar_record_owner(s->minuses.items[i].owner);
    }
    scalar_record_multi(s->multi_insert, h, s);
    record_number(s->returning_into.items != NULL); record_number(s->returning_into.count);
    record_number(s->returning_into.capacity);
    for (i = 0; i < s->returning_into.count; ++i) {
        const sqlparser_dialect_returning_into_item_t *r = &s->returning_into.items[i];
        record_number(r->statement_index); record_number(r->pair_count);
        record_number(r->keyword_uppercase_mask); record_number(r->into_uppercase_mask);
        record_number(r->uses_return_keyword);
    }
}
