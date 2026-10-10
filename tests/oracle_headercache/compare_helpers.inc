/* Standalone normal field/ownership comparisons; no old harness include. */
static sqlparser_error_t seeded_error(void)
{
    sqlparser_error_t e;
    memset(&e, 0, sizeof(e)); e.code = SQLPARSER_STATUS_UNSUPPORTED;
    e.cursor = 17; e.line = 19; e.column = 23;
    strcpy(e.message, "retained header-cache sentinel");
    return e;
}

static void same_error(const sqlparser_error_t *a, const sqlparser_error_t *b)
{
    CHECK(a->code == b->code && a->cursor == b->cursor &&
          a->line == b->line && a->column == b->column);
    CHECK(memcmp(a->message, b->message, sizeof(a->message)) == 0);
}

static void same_text(const char *a, const char *b)
{
    CHECK((a == NULL) == (b == NULL));
    if (a) CHECK(strcmp(a, b) == 0);
}

static void same_value(const sqlparser_dialect_multi_insert_value_t *a,
                       const sqlparser_dialect_multi_insert_value_t *b)
{
    same_text(a->public_sql, b->public_sql); same_text(a->parser_sql, b->parser_sql);
    CHECK(a->has_bind == b->has_bind && a->bind_kind == b->bind_kind &&
          a->bind_position == b->bind_position && a->has_bind_position == b->has_bind_position &&
          a->has_literal == b->has_literal);
    CHECK(memcmp(a->bind, b->bind, sizeof(a->bind)) == 0);
    CHECK(memcmp(a->bind_sql, b->bind_sql, sizeof(a->bind_sql)) == 0);
    same_text(a->literal_string_value, b->literal_string_value);
    same_text(a->literal_float_value, b->literal_float_value);
    CHECK(a->literal.kind == b->literal.kind && a->literal.integer_value == b->literal.integer_value &&
          a->literal.boolean_value == b->literal.boolean_value &&
          a->literal.quoted_identifier == b->literal.quoted_identifier);
    same_text(a->literal.string_value, b->literal.string_value);
    same_text(a->literal.float_value, b->literal.float_value);
    if (a->literal.string_value) CHECK(a->literal.string_value == a->literal_string_value);
    if (a->literal.float_value) CHECK(a->literal.float_value == a->literal_float_value);
    if (b->literal.string_value) CHECK(b->literal.string_value == b->literal_string_value);
    if (b->literal.float_value) CHECK(b->literal.float_value == b->literal_float_value);
}

static void same_state(const sqlparser_oracle_state_t *a, const sqlparser_oracle_state_t *b)
{
    const sqlparser_dialect_national_literals_t *an = &a->national_literals, *bn = &b->national_literals;
    const sqlparser_dialect_prepared_binds_t *ap = &a->prepared_binds, *bp = &b->prepared_binds;
    size_t i;
    CHECK(a->bind_count == b->bind_count && a->bind_capacity == b->bind_capacity &&
          a->bind_occurrence_count == b->bind_occurrence_count);
    for (i = 0; i < a->bind_count; i++) same_text(a->bind_names[i], b->bind_names[i]);
    CHECK(ap->count == bp->count && ap->capacity == bp->capacity &&
          ap->occurrence_count == bp->occurrence_count && ap->valid == bp->valid);
    for (i = 0; i < ap->count; i++) same_text(ap->names[i], bp->names[i]);
    CHECK(an->count == bn->count && an->capacity == bn->capacity &&
          an->literal_count == bn->literal_count && an->fragment_start == bn->fragment_start &&
          an->fragment_literal_base == bn->fragment_literal_base);
    for (i = 0; i < an->count; i++) {
        same_text(an->items[i].sql, bn->items[i].sql);
        same_text(an->items[i].surface_sql, bn->items[i].surface_sql);
        CHECK(an->items[i].ordinal == bn->items[i].ordinal);
        CHECK(an->items[i].owner == NULL && bn->items[i].owner == NULL);
    }
    CHECK(a->dblink_count == b->dblink_count && a->dblink_capacity == b->dblink_capacity &&
          a->next_dblink_id == b->next_dblink_id);
    for (i = 0; i < a->dblink_count; i++) {
        same_text(a->dblink_relations[i].parser_object_name, b->dblink_relations[i].parser_object_name);
        same_text(a->dblink_relations[i].public_object_name, b->dblink_relations[i].public_object_name);
        same_text(a->dblink_relations[i].public_link_name, b->dblink_relations[i].public_link_name);
        same_text(a->dblink_relations[i].public_object_sql, b->dblink_relations[i].public_object_sql);
        same_text(a->dblink_relations[i].public_link_sql, b->dblink_relations[i].public_link_sql);
    }
    CHECK(a->minuses.count == b->minuses.count && a->minuses.capacity == b->minuses.capacity &&
          a->minuses.except_count == b->minuses.except_count &&
          a->minuses.fragment_start == b->minuses.fragment_start &&
          a->minuses.fragment_except_base == b->minuses.fragment_except_base);
    for (i = 0; i < a->minuses.count; i++) CHECK(a->minuses.items[i].ordinal == b->minuses.items[i].ordinal);
}

static void same_relation(const sqlparser_dialect_multi_insert_relation_t *a,
                          const sqlparser_dialect_multi_insert_relation_t *b)
{
    same_text(a->database_name, b->database_name); same_text(a->schema_name, b->schema_name);
    same_text(a->table_name, b->table_name); same_text(a->link_name, b->link_name);
    same_text(a->link_sql, b->link_sql); same_text(a->sql, b->sql);
}

static void owned_relation(const sqlparser_dialect_multi_insert_relation_t *r)
{
    const char *parts[] = {r->database_name, r->schema_name, r->table_name, r->link_name, r->link_sql, r->sql};
    size_t i, j;
    for (i = 0; i < COUNT(parts); i++) for (j = i + 1U; j < COUNT(parts); j++)
        if (parts[i] && parts[j]) CHECK(parts[i] != parts[j]);
}

static void same_multi(const sqlparser_dialect_multi_insert_t *a, const sqlparser_dialect_multi_insert_t *b)
{
    size_t i, j;
    CHECK(a->branch_count == b->branch_count && a->branch_capacity == b->branch_capacity &&
          a->oracle_span_count == b->oracle_span_count && a->oracle_span_capacity == b->oracle_span_capacity &&
          a->oracle_spans_complete == b->oracle_spans_complete && a->oracle_spans_identity == b->oracle_spans_identity &&
          a->oracle_generic_spans_equivalent == b->oracle_generic_spans_equivalent);
    for (i = 0; i < a->branch_count; i++) {
        const sqlparser_dialect_multi_insert_branch_t *x = &a->branches[i], *y = &b->branches[i];
        CHECK(x->ordinal == y->ordinal && x->column_count == y->column_count && x->cell_count == y->cell_count &&
              x->has_condition == y->has_condition && x->is_else == y->is_else &&
              x->condition_group_id == y->condition_group_id && x->oracle_span_base == y->oracle_span_base &&
              x->oracle_values_position == y->oracle_values_position);
        same_relation(&x->relation, &y->relation); owned_relation(&x->relation); owned_relation(&y->relation);
        same_text(x->condition_public_sql, y->condition_public_sql); same_text(x->condition_parser_sql, y->condition_parser_sql);
        for (j = 0; j < x->column_count; j++) { same_text(x->columns[j].name, y->columns[j].name); same_text(x->columns[j].sql, y->columns[j].sql); }
        for (j = 0; j < x->cell_count; j++) same_value(&x->cells[j], &y->cells[j]);
    }
    for (i = 0; i < a->oracle_span_count; i++) CHECK(
        a->oracle_spans[i].source_start == b->oracle_spans[i].source_start &&
        a->oracle_spans[i].source_length == b->oracle_spans[i].source_length &&
        a->oracle_spans[i].lexical_flags == b->oracle_spans[i].lexical_flags);
}
