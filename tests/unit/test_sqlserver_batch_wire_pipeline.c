/* Complete SQLServer-family batch wire/graph/owned-commit regression.
 *
 * The singleton suite supplies only shared ordinary-parser wrappers, allocation
 * accounting, and comparison helpers. Its main is not run. This suite forces
 * the exported batch certifier to miss on its reference handles, so the graph
 * oracle cannot accidentally exercise the new implementation. Exact wire is
 * independently produced by the observed native parser, with a non-NULL
 * observer disabling native recognizers and direct writers.
 *
 * The two optional command-line fixtures are the actual supplied 5 x 1000 and
 * single-5000 inputs, in that order. They are semantic checks, not benchmarks.
 * Suggested target: copy test_sqlserver_wire_pipeline's Makefile recipe, use
 * this source, and add -Wl,--wrap=sqlparser_wire_scalar_batch_certify. Keep
 * tests/unit/test_sqlserver_wire_pipeline.c as an explicit prerequisite.
 */
#define main sqlparser_singleton_wire_suite_main
#include "test_sqlserver_wire_pipeline.c"
#undef main

#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
static size_t suppressed_batch_certificates;
sqlparser_wire_scalar_batch_t *__real_sqlparser_wire_scalar_batch_certify(const sqlparser_handle_t *);
sqlparser_wire_scalar_batch_t *__wrap_sqlparser_wire_scalar_batch_certify(const sqlparser_handle_t *h)
{
    if (force_legacy) { ++suppressed_batch_certificates; return NULL; }
    return __real_sqlparser_wire_scalar_batch_certify(h);
}
#endif

typedef struct { size_t rows, columns; const char *target; } batch_shape;
typedef struct { size_t statement, row, column; const char *value; } batch_change;
static const batch_shape diverse_shapes[] = {
    {33U, 9U, "warehouse.FirstTable"}, {35U, 3U, "OtherSchema.second_table"},
    {32U, 2U, "third_table"}, {65U, 13U, "database_name.schema_name.final_table"}
};

static void batch_original(buffer *b, size_t statement, size_t row, size_t column)
{
    static const unsigned numbers[] = {0U, 127U, 128U, 16383U, 16384U, 2147483647U};
    static const char *floats[] = {"100.50", "1.25e+03", "2147483648", "1E-12", "0001.250"};
    if (column == 0U) append(b, "'é;中-%zu-%zu'", statement, row);
    else if (column == 2U || column == 5U || column == 11U) append(b, "'keep-%zu-%zu'", statement, row);
    else if (column == 3U || column == 8U) append(b, "%s", floats[row % COUNT(floats)]);
    else if (column == 4U || column == 7U || column == 9U || column == 10U)
        append(b, "%s", functions_sql[(row + column) % COUNT(functions_sql)]);
    else append(b, "%u", numbers[row % COUNT(numbers)]);
}

/* Expected source is regenerated from shape + caller-ordered logical changes;
 * it never consults a production certificate, location map, or packed wire. */
static char *batch_source(const batch_shape *shapes, size_t count,
    size_t padding, size_t between, int final_semicolon,
    const batch_change *changes, size_t change_count)
{
    buffer b = {0}; size_t s, r, c, i;
    while (padding--) append(&b, " ");
    for (s = 0U; s < count; ++s) {
        if (s) { append(&b, ";\r\n\t "); for (i = 0U; i < between; ++i) append(&b, " "); }
        append(&b, "INSERT INTO %s(", shapes[s].target);
        for (c = 0U; c < shapes[s].columns; ++c) append(&b, "%sc%zu", c ? "," : "", c);
        append(&b, ") VALUES\n");
        for (r = 0U; r < shapes[s].rows; ++r) {
            append(&b, "%s(", r ? ",\n" : "");
            for (c = 0U; c < shapes[s].columns; ++c) {
                const char *replacement = NULL;
                if (c) append(&b, ",");
                for (i = 0U; i < change_count; ++i)
                    if (changes[i].statement == s && changes[i].row == r && changes[i].column == c)
                        replacement = changes[i].value;
                if (replacement) append(&b, "'%s'", replacement);
                else batch_original(&b, s, r, c);
            }
            append(&b, ")");
        }
    }
    append(&b, final_semicolon ? "; \n\t" : " \n\t"); return b.data;
}

typedef struct { const char *sql; size_t statements, calls; } batch_native_context;
static void batch_native_observer(const PgQuery__ParseResult *tree, void *context)
{
    batch_native_context *x = context;
    size_t statement = 0U, start = 0U, position = 0U;
    int quoted = 0;
    CHECK(tree && tree->n_stmts == x->statements); ++x->calls;
    /* Native RawStmt starts are the byte after the preceding semicolon,
     * including whitespace. Final unterminated statement length stays zero. */
    for (;;) {
        unsigned char ch = (unsigned char)x->sql[position];
        if (ch == '\'') {
            if (quoted && x->sql[position + 1U] == '\'') { position += 2U; continue; }
            quoted = !quoted;
        }
        if ((!quoted && ch == ';') || !ch) {
            if (statement < tree->n_stmts) {
                const PgQuery__RawStmt *raw = tree->stmts[statement];
                CHECK(raw && raw->stmt_location >= 0 && (size_t)raw->stmt_location == start);
                CHECK(raw->stmt_len >= 0 && (size_t)raw->stmt_len == (ch ? position - start : 0U));
                ++statement;
            }
            start = position + 1U;
        }
        if (!ch) break;
        ++position;
    }
    CHECK(!quoted && statement == x->statements);
}

static void batch_native_wire(sqlparser_handle_t *h, const char *expected, size_t count)
{
    batch_native_context context = {expected, count, 0U};
    PgQueryProtobufParseResult native = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        expected, PG_QUERY_PARSE_DEFAULT, batch_native_observer, &context);
    CHECK(native.error == NULL && context.calls == 1U && native.parse_tree.data);
    CHECK(sqlparser_statement_count(h) == count && h->parse_tree.len == native.parse_tree.len);
    CHECK(memcmp(h->parse_tree.data, native.parse_tree.data, h->parse_tree.len) == 0);
    pg_query_free_protobuf_parse_result(native);
}

static sqlparser_handle_t *batch_reference(const char *source, size_t count)
{
    sqlparser_handle_t *h;
    sqlparser_query_graph_view_t graph;
    force_legacy = 1;
    h = parse(source);
#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
    CHECK(sqlparser_wire_scalar_batch_certify(h) == NULL);
#endif
    /* Force the exported certificate boundary to miss before asking for AST.
     * This is a public graph request through the complete generic graph path. */
    CHECK(sqlparser_statement_query_graph(h, count - 1U, &graph, &error) == SQLPARSER_STATUS_OK);
    force_legacy = 0;
    CHECK(h->ast && !sqlparser_query_graph_wire_scalar_batch(h));
    CHECK(!sqlparser_query_graph_wire_scalar_insert(h));
    batch_native_wire(h, source, count);
    return h;
}

static void batch_graph_metadata(const sqlparser_query_graph_view_t *ag,
    const sqlparser_query_graph_view_t *bg, size_t rows, size_t columns, int large)
{
    sqlparser_query_graph_view_t av = *ag, bv = *bg;
    sqlparser_graph_dml_t ad, bd;
    size_t i, ai, bi, ac, bc;
    av.handle = bv.handle = NULL; av.generation = bv.generation = 0UL;
    CHECK(memcmp(&av, &bv, sizeof(av)) == 0);
    CHECK(ag->statement_index == bg->statement_index);
    for (i = 0U; i < ag->block_count; ++i) {
        sqlparser_graph_block_t a, b;
        CHECK(sqlparser_query_graph_block_at(ag, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_block_at(bg, i, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        same_span(ag, a.relations, bg, b.relations); same_span(ag, a.targets, bg, b.targets);
        same_span(ag, a.predicates, bg, b.predicates);
    }
    for (i = 0U; i < ag->relation_count; ++i) {
        sqlparser_graph_relation_t a, b;
        CHECK(sqlparser_query_graph_relation_at(ag, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_relation_at(bg, i, &b, &error) == SQLPARSER_STATUS_OK);
#define BATCH_REL_TEXT(m) same_text(a.m, b.m); a.m = b.m = NULL
        BATCH_REL_TEXT(database_name); BATCH_REL_TEXT(schema_name); BATCH_REL_TEXT(object_name);
        BATCH_REL_TEXT(alias_name); BATCH_REL_TEXT(link_name);
#undef BATCH_REL_TEXT
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    CHECK(sqlparser_query_graph_dml_count(ag, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_count(bg, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == 1U && ac == bc);
    CHECK(sqlparser_query_graph_dml(ag, &ad, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(bg, &bd, &error) == SQLPARSER_STATUS_OK);
    CHECK(memcmp(&ad, &bd, sizeof(ad)) == 0);
    CHECK(ad.target_columns.count == columns && ad.rows.count == rows * columns);
    same_span(ag, ad.target_columns, bg, bd.target_columns); same_span(ag, ad.rows, bg, bd.rows);
    same_span(ag, ad.assignments, bg, bd.assignments); same_span(ag, ad.delete_targets, bg, bd.delete_targets);
    same_span(ag, ad.branches, bg, bd.branches);
    for (i = 0U; i < columns; ++i) {
        sqlparser_graph_dml_column_t a, b;
        CHECK(sqlparser_query_graph_span_index_at(ag, ad.target_columns, i, &ai, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_span_index_at(bg, bd.target_columns, i, &bi, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_column_at(ag, ai, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_column_at(bg, bi, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a.column_name, b.column_name); a.column_name = b.column_name = NULL;
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    for (i = 0U; i < rows * columns; ++i) {
        sqlparser_graph_dml_cell_t a, b;
        sqlparser_selector_t selector;
        char expected[96], *formatted = NULL;
        CHECK(sqlparser_query_graph_span_index_at(ag, ad.rows, i, &ai, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_span_index_at(bg, bd.rows, i, &bi, &error) == SQLPARSER_STATUS_OK);
        CHECK(ai == i && bi == i);
        CHECK(sqlparser_query_graph_dml_cell_at(ag, ai, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_cell_at(bg, bi, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(a.index == i && a.row_index == i / columns && a.column_ordinal == i % columns);
        CHECK(a.has_selector && a.selector.kind == SQLPARSER_SELECTOR_KIND_INSERT_CELL);
        CHECK(sqlparser_selector_format(&a.selector, &formatted, &error) == SQLPARSER_STATUS_OK);
        snprintf(expected, sizeof(expected), "stmt[%zu].insert_cell[%zu][%zu]", ag->statement_index, i / columns, i % columns);
        CHECK(strcmp(formatted, expected) == 0);
        CHECK(sqlparser_selector_parse(formatted, &selector, &error) == SQLPARSER_STATUS_OK);
        CHECK(memcmp(&selector, &a.selector, sizeof(selector)) == 0);
        sqlparser_string_free(formatted);
        same_literal(&a.literal, &b.literal); CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
    CHECK(sqlparser_query_graph_dml_result_count(ag, 0U, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_result_count(bg, 0U, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc && ac == 0U);
    {
        int ah, bh;
        CHECK(sqlparser_query_graph_dml_parent(ag, 0U, &ai, &ah, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml_parent(bg, 0U, &bi, &bh, &error) == SQLPARSER_STATUS_OK);
        CHECK(ai == bi && ah == bh);
    }
    CHECK(sqlparser_query_graph_expression_count(ag, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_count(bg, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc);
    for (i = 0U; i < ac; ++i) {
        sqlparser_graph_expression_t a, b;
        if (large && i && i != ac / 2U && i + 1U != ac) continue;
        CHECK(sqlparser_query_graph_expression_at(ag, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_expression_at(bg, i, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a.sql, b.sql); same_text(a.name, b.name); a.sql = b.sql = a.name = b.name = NULL;
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        same_span(ag, a.arguments, bg, b.arguments);
    }
    CHECK(sqlparser_query_graph_expression_argument_count(ag, &ac, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_expression_argument_count(bg, &bc, &error) == SQLPARSER_STATUS_OK);
    CHECK(ac == bc);
    for (i = 0U; i < ac; ++i) {
        sqlparser_graph_expression_argument_t a, b;
        CHECK(sqlparser_query_graph_expression_argument_at(ag, i, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_expression_argument_at(bg, i, &b, &error) == SQLPARSER_STATUS_OK);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
    }
}

static void batch_graph_parity(sqlparser_handle_t *h, sqlparser_handle_t *ref,
    const batch_shape *shapes, size_t count, int large)
{
    size_t s;
    CHECK(sqlparser_statement_count(h) == count && sqlparser_statement_count(ref) == count);
    /* Ask for the last statement first. Graph construction must nevertheless
     * materialize complete pools with correct bases for every earlier one. */
    for (s = count; s-- > 0U;) {
        sqlparser_query_graph_view_t a, b;
        CHECK(sqlparser_statement_query_graph(h, s, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_statement_query_graph(ref, s, &b, &error) == SQLPARSER_STATUS_OK);
        batch_graph_metadata(&a, &b, shapes[s].rows, shapes[s].columns, large);
    }
}

static void batch_assert_fast(sqlparser_handle_t *h, const batch_shape *shapes, size_t count)
{
    const sqlparser_wire_scalar_batch_t *cert = sqlparser_query_graph_wire_scalar_batch(h);
    sqlparser_wire_scalar_batch_t *strict;
    size_t s, r, c, rows = 0U, columns = 0U, cells = 0U;
    CHECK(count > 1U && cert && !h->ast && !h->native_scalar_provenance);
    CHECK(cert->statement_count == count && sqlparser_wire_scalar_batch_is_current(cert, h));
    CHECK(!h->dialect_ops->plain_scalar_native_validation);
    CHECK(sqlparser_dialect_state_is_plain_insert_batch_strings(h, count, cert->string_count));
    CHECK(!sqlparser_dialect_state_is_plain_insert_batch_strings(h, count - 1U, cert->string_count));
    CHECK(!sqlparser_dialect_state_is_plain_insert_batch_strings(h, count + 1U, cert->string_count));
    CHECK(!sqlparser_dialect_state_is_plain_insert_batch_strings(h, count, cert->string_count + 1U));
    strict = sqlparser_wire_scalar_batch_certify(h); CHECK(strict);
    CHECK(sqlparser_wire_scalar_batch_is_current(strict, h));
    for (s = 0U; s < count; ++s) {
        const sqlparser_wire_scalar_batch_statement_t *statement = &cert->statements[s];
        CHECK(statement->insert.row_count == shapes[s].rows && statement->insert.column_count == shapes[s].columns);
        CHECK(statement->cell_offset == cells && statement->column_offset == columns && statement->row_offset == rows);
        CHECK(statement->insert.sql == cert->sql && statement->insert.wire == cert->wire);
        CHECK(statement->insert.sql_length == cert->sql_length && statement->insert.wire_length == cert->wire_length);
        for (r = 0U; r < shapes[s].rows; ++r) for (c = 0U; c < shapes[s].columns; ++c) {
            sqlparser_wire_scalar_cell_t a, b;
            CHECK(sqlparser_wire_scalar_batch_certified_cell(cert, s, r, c, &a));
            CHECK(sqlparser_wire_scalar_batch_certified_cell(strict, s, r, c, &b));
            CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        }
        rows += shapes[s].rows; columns += shapes[s].columns; cells += shapes[s].rows * shapes[s].columns;
    }
    CHECK(cert->row_count == rows && cert->column_count == columns && cert->cell_count == cells);
    {
        sqlparser_wire_scalar_cell_t cell;
        CHECK(!sqlparser_wire_scalar_batch_certified_cell(cert, count, 0U, 0U, &cell));
        CHECK(!sqlparser_wire_scalar_batch_certified_cell(cert, 0U, shapes[0].rows, 0U, &cell));
        CHECK(!sqlparser_wire_scalar_batch_certified_cell(cert, 0U, 0U, shapes[0].columns, &cell));
    }
    sqlparser_wire_scalar_batch_destroy(strict);
}

static owned_batch batch_changes(const batch_change *changes, size_t count, int typed)
{
    owned_batch result = {0}; size_t i;
    result.count = count;
    result.patches = calloc(count, sizeof(*result.patches)); result.literals = calloc(count, sizeof(*result.literals));
    CHECK(result.patches && result.literals);
    for (i = 0U; i < count; ++i) {
        buffer selector = {0}, token = {0};
        append(&selector, "stmt[%zu].insert_cell[%zu][%zu]", changes[i].statement, changes[i].row, changes[i].column);
        result.patches[i].op = SQLPARSER_PATCH_REPLACE; result.patches[i].selector = selector.data;
        if (typed) {
            result.literals[i].kind = SQLPARSER_LITERAL_KIND_STRING;
            result.literals[i].string_value = copy(changes[i].value);
            result.patches[i].literal = &result.literals[i];
        } else {
            append(&token, "'%s'", changes[i].value); result.patches[i].sql = token.data;
        }
    }
    return result;
}

static void batch_positive(int typed, int trailing, size_t padding, size_t between, int late_only)
{
    batch_change changes[] = {
        {3U, 64U, 0U, "late-tail"}, {0U, 0U, 0U, "early-growth-abcdefghijklmnopqrstuvwxyz0123456789"},
        {0U, 32U, 0U, ""}, {0U, 1U, 2U, "x"}, {2U, 17U, 0U, "third"}
    };
    size_t count = COUNT(diverse_shapes), n = late_only ? 1U : COUNT(changes), round;
    char *source = batch_source(diverse_shapes, count, padding, between, trailing, NULL, 0U);
    char *expected = batch_source(diverse_shapes, count, padding, between, trailing, changes, n);
    sqlparser_handle_t *h = parse(source), *ref = batch_reference(source, count);
    sqlparser_query_graph_view_t stale[COUNT(diverse_shapes)];
    sqlparser_wire_scalar_batch_t *old_certificate;
    char *retained = NULL;
    stage = "heterogeneous full batch graph, exact native locations, owned repeated commit";
    batch_graph_parity(h, ref, diverse_shapes, count, 0); batch_assert_fast(h, diverse_shapes, count);
    batch_native_wire(h, source, count); sqlparser_handle_destroy(ref);
    for (round = 0U; round < 2U; ++round) {
        size_t s, before = unpacks, reparse_before = destructive_reparses, fragments = raw_fragment_parses;
        unsigned long generation = h->generation;
        owned_batch p = batch_changes(changes, n, typed);
        sqlparser_patch_list_t list = {p.patches, p.count};
        old_certificate = sqlparser_wire_scalar_batch_certify(h); CHECK(old_certificate);
        for (s = 0U; s < count; ++s)
            CHECK(sqlparser_statement_query_graph(h, s, &stale[s], &error) == SQLPARSER_STATUS_OK);
        memset(&error, 0x5a, sizeof(error));
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(error.code == SQLPARSER_STATUS_OK && error.message[0] == '\0');
        CHECK(h->generation == generation + 1UL && !h->ast && !h->native_scalar_provenance);
        CHECK(!sqlparser_wire_scalar_batch_is_current(old_certificate, h));
        sqlparser_wire_scalar_batch_destroy(old_certificate); release_batch(&p);
        for (s = 0U; s < count; ++s) {
            sqlparser_graph_dml_t dml;
            CHECK(sqlparser_query_graph_dml(&stale[s], &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        }
        {
            char *out = NULL;
            CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK);
            same_text(out, expected); same_text(sqlparser_original_sql(h), expected);
            if (!round) retained = out; else { same_text(retained, expected); sqlparser_string_free(out); }
        }
        NO_UNPACKS(before); NO_REPARSE(reparse_before); NO_FRAGMENT(fragments);
        batch_native_wire(h, expected, count);
        ref = batch_reference(expected, count);
        batch_graph_parity(h, ref, diverse_shapes, count, 0); batch_assert_fast(h, diverse_shapes, count);
        sqlparser_handle_destroy(ref); ++cases;
    }
    poison_free(source); free(expected); sqlparser_handle_destroy(h);
    CHECK(retained && strstr(retained, "late-tail")); sqlparser_string_free(retained);
}

static void batch_same_views(sqlparser_handle_t *h, sqlparser_handle_t *ref)
{
    char *a = NULL, *b = NULL;
    CHECK(sqlparser_statement_count(h) == sqlparser_statement_count(ref));
    CHECK(sqlparser_export_view_json(h, 0, &a, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_export_view_json(ref, 0, &b, &error) == SQLPARSER_STATUS_OK);
    same_text(a, b); sqlparser_string_free(a); sqlparser_string_free(b);
}

static void batch_shape_matrix(int typed)
{
    static const size_t widths[] = {1U, 2U, 4U, 7U, 17U, 9U};
    static const char *targets[] = {"one", "s.two", "db.sch.three", "OtherSchema.Four"};
    size_t count, s;
    stage = "independent targets, one-to-seventeen columns and variable batch counts";
    for (count = 2U; count <= COUNT(widths); ++count) {
        batch_shape shapes[COUNT(widths)];
        batch_change changes[2]; char *source, *expected, *out = NULL;
        sqlparser_handle_t *h, *ref;
        owned_batch p; sqlparser_patch_list_t list; size_t before;
        for (s = 0U; s < count; ++s)
            shapes[s] = (batch_shape){96U + s, widths[s], targets[s % COUNT(targets)]};
        changes[0] = (batch_change){count - 1U, shapes[count - 1U].rows - 1U, 0U, "last"};
        changes[1] = (batch_change){0U, 0U, 0U, "first-expanded"};
        source = batch_source(shapes, count, 0U, 0U, count % 2U, NULL, 0U);
        expected = batch_source(shapes, count, 0U, 0U, count % 2U, changes, COUNT(changes));
        h = parse(source); ref = batch_reference(source, count);
        batch_graph_parity(h, ref, shapes, count, 0); batch_assert_fast(h, shapes, count);
        sqlparser_handle_destroy(ref);
        p = batch_changes(changes, COUNT(changes), typed); list = (sqlparser_patch_list_t){p.patches, p.count};
        before = destructive_reparses;
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK); NO_REPARSE(before); release_batch(&p);
        CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK); same_text(out, expected);
        batch_native_wire(h, expected, count);
        ref = batch_reference(expected, count); batch_graph_parity(h, ref, shapes, count, 0);
        sqlparser_handle_destroy(ref); sqlparser_handle_destroy(h);
        sqlparser_string_free(out); free(source); free(expected); ++cases;
    }
}

static void batch_validation_source_proof_stays_zero(void)
{
    static const batch_shape shapes[] = {{65U, 9U, "s.a"}, {65U, 9U, "s.b"}};
    char *source = batch_source(shapes, COUNT(shapes), 0U, 0U, 1, NULL, 0U), *parser_sql = NULL;
    void *state = NULL;
    const sqlparser_dialect_ops_t *ops = sqlparser_dialect_get_ops(dialect);
    sqlparser_validation_preprocess_fn preprocess = sqlparser_dialect_validation_preprocessor(dialect, ops);
    sqlparser_parse_options_t options;
    PgQueryIdentityScalarInsertProof proof, zero = {0};
    stage = "batch source preprocessing cannot mint singleton native validation proof";
    CHECK(ops && preprocess && !ops->plain_scalar_native_validation);
    sqlparser_parse_options_default(&options); options.dialect = dialect; memset(&proof, 0xa5, sizeof(proof));
    CHECK(preprocess(source, &options.limits, &parser_sql, &state, &proof, NULL, &error) == SQLPARSER_STATUS_OK);
    CHECK(!memcmp(&proof, &zero, sizeof(proof))); same_text(parser_sql, source);
    free(parser_sql); ops->destroy_state(state); free(source); ++cases;
}

static sqlparser_status_t batch_patch_parity(const char *source,
    const sqlparser_patch_list_t *list, size_t sql_limit, size_t output_limit)
{
    sqlparser_handle_t *h = parse(source), *ref;
    sqlparser_query_graph_view_t graph;
    sqlparser_status_t a, b;
    sqlparser_error_t ae, be;
    char *at = NULL, *bt = NULL;
    force_legacy = 1; ref = parse(source);
    CHECK(sqlparser_statement_query_graph(ref, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    force_legacy = 0;
    CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
    if (sql_limit) h->limits.max_sql_bytes = ref->limits.max_sql_bytes = sql_limit;
    if (output_limit) h->limits.max_output_bytes = ref->limits.max_output_bytes = output_limit;
    a = sqlparser_apply_patch(h, list, &ae); b = legacy_apply(ref, list, &be);
    same_error(a, &ae, b, &be); CHECK(h->failed == ref->failed);
    if (!h->failed) {
        sqlparser_status_t ad = sqlparser_deparse(h, &at, &ae), bd = sqlparser_deparse(ref, &bt, &be);
        same_error(ad, &ae, bd, &be);
        if (ad == SQLPARSER_STATUS_OK) {
            /* Formatting can differ on a source-preserving commit. The entire
             * public statement graph is the semantic fallback oracle. */
            h->limits.max_output_bytes = ref->limits.max_output_bytes = SIZE_MAX;
            batch_same_views(h, ref);
        }
    }
    sqlparser_string_free(at); sqlparser_string_free(bt);
    sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref); ++cases; return a;
}

static void batch_ordered_errors_and_fallbacks(void)
{
    static const char *bad_selectors[] = {"broken", "stmt[4].insert_cell[0][0]",
        "stmt[3].insert_cell[65][0]", "stmt[1].insert_cell[0][3]", "stmt[9999999999999999999999].insert_cell[0][0]"};
    static const char *fragments[] = {"'unterminated", "'valid' trailing", "'a', 'b'", "'a'; SELECT 1",
        "upper('mixed')", "'can''t'", "'Ω中'", "'back\\slash'", "'line\nline'", "N'national-中'",
        "42", "CURRENT_TIMESTAMP(3)", " 'padded' ", "'x' /* trailing comment */"};
    static const char *typed_values[] = {"can't", "Ω中", "line\nline", "back\\slash", ""};
    char *source = batch_source(diverse_shapes, COUNT(diverse_shapes), 0U, 0U, 1, NULL, 0U);
    size_t i, order, limit, length = strlen(source);
    stage = "caller-order errors, reverse and duplicate selectors, replacement fallbacks";
    for (i = 0U; i < COUNT(bad_selectors) + COUNT(fragments); ++i)
    for (order = 0U; order < 2U; ++order) {
        sqlparser_patch_t p[] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[64][0]", .sql="'valid'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'valid-too'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[64][0]", .sql="'last-wins'"}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        if (i < COUNT(bad_selectors)) { p[1].selector = bad_selectors[i]; p[0].sql = "'unterminated"; }
        else p[1].sql = fragments[i - COUNT(bad_selectors)];
        if (order) { sqlparser_patch_t swap = p[0]; p[0] = p[1]; p[1] = swap; }
        batch_patch_parity(source, &list, 0U, 0U);
    }
    for (i = 0U; i < COUNT(typed_values); ++i) {
        sqlparser_literal_value_t literal = {.kind=SQLPARSER_LITERAL_KIND_STRING, .string_value=typed_values[i]};
        sqlparser_patch_t p[] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[64][0]", .literal=&literal},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0001].insert_cell[0000][0000]", .literal=&literal},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[64][0]", .literal=&literal}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        CHECK(batch_patch_parity(source, &list, 0U, 0U) == SQLPARSER_STATUS_OK);
    }
    stage = "source and output limits retain first-error precedence";
    for (limit = 0U; limit < 2U; ++limit) for (order = 0U; order < 2U; ++order)
    for (i = 0U; i < 3U; ++i) {
        buffer huge = {0}; size_t n = length + i - 1U;
        sqlparser_patch_t p[] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[64][0]"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="''"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[64][0]", .sql="''"}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        append(&huge, "'"); while (huge.length + 1U < n) append(&huge, "x"); append(&huge, "'");
        p[0].sql = huge.data;
        if (order) { sqlparser_patch_t swap = p[0]; p[0] = p[1]; p[1] = swap; }
        batch_patch_parity(source, &list, limit ? length * 4U : length, limit ? length : length * 4U);
        p[1].selector = "invalid-selector";
        batch_patch_parity(source, &list, limit ? length * 4U : length, limit ? length : length * 4U);
        free(huge.data);
    }
    stage = "structural overlap and source-selector dependencies preserve sequential semantics";
    for (i = 0U; i < 4U; ++i) {
        sqlparser_patch_t p[] = {
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[1][0]", .sql="'before'"},
            {.op=SQLPARSER_PATCH_DELETE_ROW, .selector="stmt[0].insert_row[0]"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'shifted'"},
            {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[1].insert_cell[0][0]", .source_selector="stmt[0].insert_cell[0][0]"}
        };
        sqlparser_patch_list_t list = {p, COUNT(p)};
        if (i == 1U) p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_COLUMN, .selector="stmt[0].insert_columns", .index=0U};
        if (i == 2U) p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_columns", .index=1U, .name="added", .default_sql="42"};
        if (i == 3U) p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[1][0]", .sql="'duplicate'"};
        CHECK(batch_patch_parity(source, &list, 0U, 0U) == SQLPARSER_STATUS_OK);
    }
    free(source);
}

static void batch_source_fallbacks(void)
{
    static const char *tails[] = {
        "SELECT 'keep';", "BEGIN SELECT 1 END;", "USE db;", ";", "-- trailing\n", "/* trailing */",
        "INSERT INTO small_table(a) VALUES ('small');",
        "INSERT INTO t(a) OUTPUT inserted.a VALUES ('x');", "INSERT INTO [t](a) VALUES ('x');",
        "INSERT INTO t(a) VALUES (N'x');", "INSERT INTO t(a) VALUES ('can''t');",
        "INSERT INTO t(a) VALUES (NULL);", "INSERT INTO t(a) VALUES (DEFAULT);",
        "INSERT INTO t(a) VALUES (CURRENT_TIMESTAMP(2));", "INSERT INTO t(a) VALUES (UPPER('x'));",
        "INSERT INTO t(a) VALUES (@p);", "INSERT INTO t(a) VALUES ('unterminated"
    };
    char *safe = batch_source(diverse_shapes, 2U, 0U, 0U, 1, NULL, 0U);
    size_t i, limit;
    stage = "mixed, control, comments, empty, rewritten and unsupported statements miss complete proof";
    for (i = 0U; i < COUNT(tails); ++i) for (limit = 0U; limit < 2U; ++limit) {
        buffer source = {0}; sqlparser_handle_t *h = NULL, *ref = NULL;
        sqlparser_parse_options_t options; sqlparser_status_t a, b; sqlparser_error_t ae, be;
        append(&source, "%s%s", safe, tails[i]);
        sqlparser_parse_options_default(&options); options.dialect = dialect;
        if (limit) options.limits.max_statement_count = 1U;
        a = sqlparser_parse_with_options(source.data, &options, &h, &ae);
        force_legacy = 1; b = sqlparser_parse_with_options(source.data, &options, &ref, &be); force_legacy = 0;
        same_error(a, &ae, b, &be);
        if (a == SQLPARSER_STATUS_OK) {
            CHECK(!sqlparser_wire_scalar_batch_certify(h));
            same_wire(h, ref);
            if (sqlparser_statement_count(h) && !h->control) {
                sqlparser_patch_t p = {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'fallback'"};
                sqlparser_patch_list_t list = {&p, 1U};
                a = sqlparser_apply_patch(h, &list, &ae); b = legacy_apply(ref, &list, &be);
                same_error(a, &ae, b, &be); CHECK(h->failed == ref->failed);
                if (a == SQLPARSER_STATUS_OK) batch_same_views(h, ref);
            }
        } else CHECK(!h && !ref);
        sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref); free(source.data); ++cases;
    }
    free(safe);
}

static void batch_varint_boundaries(int typed)
{
    static const size_t boundaries[] = {127U, 128U, 16383U, 16384U};
    static const batch_shape shapes[] = {{32U, 3U, "a"}, {32U, 2U, "s.b"}};
    size_t i, direction;
    stage = "location and nested message varints cross 128 and 16384 in both directions";
    for (i = 0U; i < COUNT(boundaries); ++i) for (direction = 0U; direction < 2U; ++direction) {
        size_t padding, next_cell;
        char *value = malloc(boundaries[i] + 1U), *source, *expected, *out = NULL;
        batch_change initial = {0U, 0U, 0U, NULL}, change = {0U, 0U, 0U, NULL};
        sqlparser_handle_t *h;
        sqlparser_query_graph_view_t graph;
        owned_batch p; sqlparser_patch_list_t list;
        size_t before;
        CHECK(value); memset(value, 'x', boundaries[i]); value[boundaries[i]] = '\0';
        /* Shrinking long original payloads can exceed the retained-text budget.
         * Use a modest original for the shrink direction; its source location
         * and later headers still cross the requested boundary. */
        initial.value = direction ? "12345678901234567890" : "";
        change.value = direction ? "" : value;
        {
            char *unpadded = batch_source(shapes, COUNT(shapes), 0U, 0U, (int)direction, &initial, 1U);
            const char *values_start = strstr(unpadded, " VALUES\n(");
            CHECK(values_start);
            /* The untouched integer immediately after the edited token is
             * placed on the boundary. Shrink moves it below; growth above. */
            next_cell = (size_t)(values_start - unpadded) + strlen(" VALUES\n('") + strlen(initial.value) + 2U;
            CHECK(next_cell < boundaries[i]); padding = boundaries[i] - next_cell;
            free(unpadded);
        }
        source = batch_source(shapes, COUNT(shapes), padding, 0U, (int)direction, &initial, 1U);
        expected = batch_source(shapes, COUNT(shapes), padding, 0U, (int)direction, &change, 1U);
        h = parse(source);
        CHECK(sqlparser_statement_query_graph(h, 1U, &graph, &error) == SQLPARSER_STATUS_OK);
        batch_assert_fast(h, shapes, COUNT(shapes));
        p = batch_changes(&change, 1U, typed); list = (sqlparser_patch_list_t){p.patches, p.count};
        before = destructive_reparses;
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK); NO_REPARSE(before);
        release_batch(&p);
        CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK); same_text(out, expected);
        batch_native_wire(h, expected, COUNT(shapes));
        sqlparser_string_free(out); sqlparser_handle_destroy(h); free(source); free(expected); free(value); ++cases;
    }
}

static void batch_borrowed_and_clone(void)
{
    size_t count = COUNT(diverse_shapes), before;
    char *source = batch_source(diverse_shapes, count, 7U, 3U, 0, NULL, 0U), *expected;
    sqlparser_handle_t *h = parse(source), *clone = NULL, *ref;
    sqlparser_query_graph_view_t graph, old_graph;
    sqlparser_graph_dml_cell_t borrowed;
    sqlparser_literal_value_t literal = {0};
    sqlparser_patch_t p[3] = {{0}};
    sqlparser_patch_list_t list = {p, COUNT(p)};
    sqlparser_wire_scalar_batch_t *certificate;
    batch_change changes[3];
    char *snapshot;
    stage = "graph-borrowed replacement snapshot, stale graph and independent clone owners";
    CHECK(sqlparser_statement_query_graph(h, 3U, &graph, &error) == SQLPARSER_STATUS_OK);
    old_graph = graph; batch_assert_fast(h, diverse_shapes, count);
    CHECK(sqlparser_query_graph_dml_cell_at(&graph, 2U, &borrowed, &error) == SQLPARSER_STATUS_OK);
    CHECK(borrowed.literal.string_value);
    snapshot = copy(borrowed.literal.string_value);
    literal.kind = SQLPARSER_LITERAL_KIND_STRING; literal.string_value = borrowed.literal.string_value;
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[3].insert_cell[64][0]", .literal=&literal};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .literal=&literal};
    p[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[1].insert_cell[2][0]", .literal=&literal};
    changes[0] = (batch_change){3U, 64U, 0U, snapshot};
    changes[1] = (batch_change){0U, 0U, 0U, snapshot};
    changes[2] = (batch_change){1U, 2U, 0U, snapshot};
    expected = batch_source(diverse_shapes, count, 7U, 3U, 0, changes, COUNT(changes));
    certificate = sqlparser_wire_scalar_batch_certify(h); CHECK(certificate);
    CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
    CHECK(clone && !sqlparser_wire_scalar_batch_is_current(certificate, clone));
    before = destructive_reparses;
    CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK); NO_REPARSE(before);
    CHECK(!sqlparser_wire_scalar_batch_is_current(certificate, h));
    sqlparser_wire_scalar_batch_destroy(certificate);
    memset(&literal, 0xa7, sizeof(literal)); memset(p, 0xa7, sizeof(p));
    {
        sqlparser_graph_dml_t dml; char *out = NULL;
        CHECK(sqlparser_query_graph_dml(&old_graph, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK); same_text(out, expected);
        sqlparser_string_free(out);
    }
    batch_native_wire(h, expected, count);
    ref = batch_reference(expected, count); batch_graph_parity(h, ref, diverse_shapes, count, 0);
    sqlparser_handle_destroy(ref);
    /* Clone must keep its own original source after the original commits. */
    same_text(sqlparser_original_sql(clone), source);
    {
        batch_change change = {2U, 0U, 0U, "clone-only"};
        owned_batch cp = batch_changes(&change, 1U, 1);
        sqlparser_patch_list_t cl = {cp.patches, cp.count};
        char *clone_expected = batch_source(diverse_shapes, count, 7U, 3U, 0, &change, 1U);
        CHECK(sqlparser_apply_patch(clone, &cl, &error) == SQLPARSER_STATUS_OK); release_batch(&cp);
        ref = batch_reference(clone_expected, count);
        batch_graph_parity(clone, ref, diverse_shapes, count, 0);
        sqlparser_handle_destroy(ref); free(clone_expected);
    }
    sqlparser_handle_destroy(clone);
    batch_native_wire(h, expected, count);
    sqlparser_handle_destroy(h); free(source); free(expected); free(snapshot); ++cases;
}

static void batch_deep_expression_parity(void)
{
    const size_t count = COUNT(diverse_shapes);
    const batch_change changes[] = {{0U, 0U, 0U, "expanded-prefix-before-later-functions"}, {3U, 1U, 2U, ""}};
    char *source = batch_source(diverse_shapes, count, 7U, 13U, 1, NULL, 0U);
    char *expected = batch_source(diverse_shapes, count, 7U, 13U, 1, changes, COUNT(changes));
    sqlparser_handle_t *h = parse(source), *ref;
    sqlparser_query_graph_view_t graph;
    owned_batch edits = batch_changes(changes, COUNT(changes), 1);
    sqlparser_patch_list_t list = {edits.patches, edits.count};
    size_t s, row, column, before;
    stage = "later-statement SQLValueFunction text, full fast graph JSON and cell SQL";
    CHECK(sqlparser_statement_query_graph(h, count - 1U, &graph, &error) == SQLPARSER_STATUS_OK);
    batch_assert_fast(h, diverse_shapes, count);
    before = destructive_reparses;
    CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK); NO_REPARSE(before);
    release_batch(&edits);
    ref = batch_reference(expected, count);
    batch_graph_parity(h, ref, diverse_shapes, count, 0);
    batch_assert_fast(h, diverse_shapes, count);
    /* JSON reads the graph's own expression_sql storage. Do this before cell
     * SQL accessors, which are allowed to materialize an ordinary AST. */
    for (s = count; s-- > 0U;) {
        char *a = NULL, *b = NULL;
        CHECK(sqlparser_export_view_json(h, s, &a, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_export_view_json(ref, s, &b, &error) == SQLPARSER_STATUS_OK);
        same_text(a, b); sqlparser_string_free(a); sqlparser_string_free(b);
    }
    for (s = 0U; s < count; s++) for (row = 0U; row < diverse_shapes[s].rows; row++)
        for (column = 0U; column < diverse_shapes[s].columns; column++) {
            char *a = NULL, *b = NULL;
            CHECK(sqlparser_insert_cell_sql(h, s, row, column, &a, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_insert_cell_sql(ref, s, row, column, &b, &error) == SQLPARSER_STATUS_OK);
            same_text(a, b); sqlparser_string_free(a); sqlparser_string_free(b);
        }
    sqlparser_handle_destroy(h); sqlparser_handle_destroy(ref); free(source); free(expected); ++cases;
}

static void batch_certificate_guards(void)
{
    char *source = batch_source(diverse_shapes, COUNT(diverse_shapes), 0U, 0U, 1, NULL, 0U);
    sqlparser_handle_t *h = parse(source);
    sqlparser_wire_scalar_batch_t *cert = sqlparser_wire_scalar_batch_certify(h);
    sqlparser_wire_scalar_cell_t cell;
    size_t i, original_length = h->parse_tree.len;
    stage = "whole-owner certificate rejects stale, foreign, partial and unknown wire";
    CHECK(cert && sqlparser_wire_scalar_batch_is_current(cert, h));
    CHECK(sqlparser_wire_scalar_batch_certified_cell(cert, 3U, 64U, 0U, &cell));
    {
        size_t offset = (size_t)(cell.text - h->parse_tree.data);
        char saved = h->parse_tree.data[offset];
        h->parse_tree.data[offset] ^= 0x20;
        CHECK(!sqlparser_wire_scalar_batch_certify(h));
        h->parse_tree.data[offset] = saved;
    }
    for (i = 0U; i < cert->statement_count; ++i) {
        size_t bounds[] = {cert->statements[i].raw_wire_offset,
            cert->statements[i].raw_wire_offset + cert->statements[i].raw_wire_length - 1U};
        size_t k;
        for (k = 0U; k < COUNT(bounds); ++k) {
            h->parse_tree.len = bounds[k];
            CHECK(!sqlparser_wire_scalar_batch_is_current(cert, h));
            CHECK(!sqlparser_wire_scalar_batch_certify(h));
            h->parse_tree.len = original_length;
        }
    }
    {
        char *old = h->parse_tree.data, *unknown = malloc(original_length + 3U);
        CHECK(unknown); memcpy(unknown, old, original_length);
        unknown[original_length] = (char)0x98; unknown[original_length + 1U] = 0x06;
        unknown[original_length + 2U] = 0x01; /* Unknown scalar field 99. */
        h->parse_tree.data = unknown; h->parse_tree.len = original_length + 3U;
        CHECK(!sqlparser_wire_scalar_batch_is_current(cert, h));
        CHECK(!sqlparser_wire_scalar_batch_certify(h));
        h->parse_tree.data = old; h->parse_tree.len = original_length; free(unknown);
    }
    {
        const sqlparser_dialect_ops_t *ops = h->dialect_ops;
        sqlparser_dialect_ops_t foreign = *ops;
        h->dialect_ops = &foreign;
        CHECK(!sqlparser_wire_scalar_batch_is_current(cert, h)); CHECK(!sqlparser_wire_scalar_batch_certify(h));
        h->dialect_ops = ops;
    }
    {
        char *old_parser = h->parser_sql, *new_parser = copy(h->parser_sql);
        sqlparser_wire_scalar_batch_t *fresh;
        h->parser_sql = new_parser;
        CHECK(!sqlparser_wire_scalar_batch_is_current(cert, h));
        fresh = sqlparser_wire_scalar_batch_certify(h); CHECK(fresh);
        sqlparser_wire_scalar_batch_destroy(fresh);
        h->parser_sql = old_parser; free(new_parser);
        CHECK(sqlparser_wire_scalar_batch_is_current(cert, h));
    }
    ++h->generation;
    CHECK(!sqlparser_wire_scalar_batch_is_current(cert, h)); CHECK(!sqlparser_wire_scalar_batch_certify(h));
    --h->generation;
    CHECK(sqlparser_wire_scalar_batch_is_current(cert, h));
    {
        size_t saved = h->statement_count;
        h->statement_count = saved - 1U;
        CHECK(!sqlparser_wire_scalar_batch_certify(h)); h->statement_count = saved;
    }
    sqlparser_wire_scalar_batch_destroy(cert);
    CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK);
    CHECK(!sqlparser_wire_scalar_batch_certify(h));
    sqlparser_handle_destroy(h); free(source); ++cases;
}

static void batch_allocation_boundaries(void)
{
#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
    static const batch_shape shapes[] = {{32U, 3U, "s.a"}, {33U, 2U, "s.b"}};
    static const batch_change changes[] = {{1U, 32U, 0U, "last-expanded"}, {0U, 0U, 0U, ""}, {0U, 31U, 2U, "x"}};
    char *source = batch_source(shapes, COUNT(shapes), 0U, 0U, 1, NULL, 0U);
    char *expected = batch_source(shapes, COUNT(shapes), 0U, 0U, 1, changes, COUNT(changes));
    size_t operation;
    sqlparser_pg_query_prepare();
    for (operation = 0U; operation < 4U; ++operation) {
        size_t boundaries = 0U;
        for (failure_index = 0U; failure_index <= boundaries; ++failure_index) {
            sqlparser_handle_t *h = parse(source);
            sqlparser_wire_scalar_batch_t *cert = NULL;
            sqlparser_query_graph_view_t graph;
            sqlparser_status_t status = SQLPARSER_STATUS_OK;
            owned_batch p = {0}; sqlparser_patch_list_t list = {0};
            char *out = NULL;
            size_t reparse_before = destructive_reparses;
            stage = operation == 0U ? "batch certificate exhaustive optional allocation misses" :
                operation == 1U ? "complete graph exhaustive outer allocation misses" :
                "batch owned commit exhaustive outer allocation ownership ledger";
            CHECK(live_allocations == 0U && native_depth == 0U); ledger_end = 0U;
            if (operation >= 2U) {
                CHECK(sqlparser_statement_query_graph(h, 1U, &graph, &error) == SQLPARSER_STATUS_OK);
                batch_assert_fast(h, shapes, COUNT(shapes));
                p = batch_changes(changes, COUNT(changes), operation == 3U);
                list = (sqlparser_patch_list_t){p.patches, p.count};
            }
            allocation_calls = allocation_failures = 0U; allocation_armed = 1;
            if (!operation) cert = sqlparser_wire_scalar_batch_certify(h);
            else if (operation == 1U) status = sqlparser_statement_query_graph(h, 1U, &graph, &error);
            else status = sqlparser_apply_patch(h, &list, &error);
            allocation_armed = 0;
            if (operation >= 2U) release_batch(&p);
            if (!failure_index) {
                CHECK(status == SQLPARSER_STATUS_OK && allocation_failures == 0U);
                boundaries = allocation_calls; CHECK(boundaries > 0U && boundaries < 4096U);
                if (!operation) CHECK(cert);
                if (operation >= 2U) NO_REPARSE(reparse_before);
            } else CHECK(allocation_failures == 1U);
            if (!operation) {
                if (failure_index) CHECK(!cert);
                CHECK(!h->failed && !h->native_scalar_provenance);
                sqlparser_wire_scalar_batch_destroy(cert);
                cert = sqlparser_wire_scalar_batch_certify(h); CHECK(cert);
            } else if (operation == 1U) {
                CHECK(!h->failed);
                if (status != SQLPARSER_STATUS_OK) {
                    CHECK(!h->query_graph);
                    CHECK(sqlparser_statement_query_graph(h, 1U, &graph, &error) == SQLPARSER_STATUS_OK);
                }
                CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK); same_text(out, source);
            } else if (status == SQLPARSER_STATUS_OK) {
                CHECK(!h->failed && h->generation == 1UL && !h->native_scalar_provenance);
                CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK); same_text(out, expected);
                batch_native_wire(h, expected, COUNT(shapes));
            } else {
                CHECK(status == SQLPARSER_STATUS_NO_MEMORY && error.code == SQLPARSER_STATUS_NO_MEMORY);
                CHECK(h->failed && !h->query_graph && !h->ast && !h->parse_tree.data && !h->native_scalar_provenance);
            }
            sqlparser_wire_scalar_batch_destroy(cert); sqlparser_string_free(out); sqlparser_handle_destroy(h);
            CHECK(live_allocations == 0U && native_depth == 0U); ++cases;
        }
        printf("SQLSERVER_BATCH_WIRE_OOM dialect=%s operation=%zu boundaries=%zu live=0\n",
            families[family_index].name, operation, boundaries);
    }
    failure_index = 0U; free(source); free(expected);
#endif
}

static char *batch_supplied_expected(const char *source, char **values)
{
    const char *cursor = source, *match, *needle = "'张三李四'";
    size_t row = 0U; buffer out = {0};
    while ((match = strstr(cursor, needle)) != NULL) {
        CHECK(row < 5000U);
        append(&out, "%.*s'%s'", (int)(match - cursor), cursor, values[row++]);
        cursor = match + strlen(needle);
    }
    CHECK(row == 5000U); append(&out, "%s", cursor); return out.data;
}

static void batch_supplied_protocol(const char *path, size_t statement_count, int typed)
{
    batch_shape shapes[5];
    char *source = read_file(path), *expected, *out = NULL;
    char **values = calloc(5000U, sizeof(*values));
    sqlparser_handle_t *h = parse(source), *ref;
    owned_batch p = {0}; sqlparser_patch_list_t list;
    size_t s, r, c, seen = 0U, traversed = 0U, before, unpack_before, fragment_before;
    stage = statement_count == 5U ? "actual supplied 5x1000 correctness protocol" : "actual supplied single5000 correctness protocol";
    CHECK(values && (statement_count == 1U || statement_count == 5U));
    CHECK(sqlparser_statement_count(h) == statement_count);
    p.count = 5000U; p.patches = calloc(p.count, sizeof(*p.patches)); p.literals = calloc(p.count, sizeof(*p.literals));
    CHECK(p.patches && p.literals);
    for (r = 0U; r < 5000U; ++r) {
        char text[80]; snprintf(text, sizeof(text), "masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567", r + 1U);
        CHECK(strlen(text) == 49U); values[r] = copy(text);
    }
    expected = batch_supplied_expected(source, values);
    for (s = 0U; s < statement_count; ++s) {
        sqlparser_query_graph_view_t graph; sqlparser_graph_dml_t dml;
        shapes[s] = (batch_shape){5000U / statement_count, 9U, "TEST_LIB.TEACHER_STATISTICS"};
        CHECK(sqlparser_statement_query_graph(h, s, &graph, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
        CHECK(dml.rows.count == shapes[s].rows * 9U && dml.target_columns.count == 9U);
        /* Exactly 45,000 public cell visits and 5,000 selectors feed one apply. */
        for (r = 0U; r < shapes[s].rows; ++r) for (c = 0U; c < 9U; ++c) {
            sqlparser_graph_dml_cell_t cell; size_t index; char *selector = NULL;
            CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, r * 9U + c, &index, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error) == SQLPARSER_STATUS_OK); ++traversed;
            CHECK(cell.index == r * 9U + c && cell.row_index == r && cell.column_ordinal == c);
            if (c != 2U) continue;
            CHECK(cell.has_selector && cell.selector.statement_index == s);
            CHECK(sqlparser_selector_format(&cell.selector, &selector, &error) == SQLPARSER_STATUS_OK);
            p.patches[seen].op = SQLPARSER_PATCH_REPLACE; p.patches[seen].selector = selector;
            if (typed) {
                p.literals[seen].kind = SQLPARSER_LITERAL_KIND_STRING;
                p.literals[seen].string_value = copy(values[seen]); p.patches[seen].literal = &p.literals[seen];
            } else {
                buffer token = {0}; append(&token, "'%s'", values[seen]); p.patches[seen].sql = token.data;
            }
            ++seen;
        }
    }
    CHECK(traversed == 45000U && seen == 5000U);
    if (statement_count > 1U) batch_assert_fast(h, shapes, statement_count); else assert_fast(h, 5000U, 9U);
    before = destructive_reparses; unpack_before = unpacks; fragment_before = raw_fragment_parses;
    list = (sqlparser_patch_list_t){p.patches, p.count};
    CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(h, &out, &error) == SQLPARSER_STATUS_OK);
    NO_REPARSE(before); NO_UNPACKS(unpack_before); NO_FRAGMENT(fragment_before);
    release_batch(&p); same_text(out, expected);
    batch_native_wire(h, expected, statement_count);
    ref = batch_reference(expected, statement_count); batch_graph_parity(h, ref, shapes, statement_count, 1);
    sqlparser_handle_destroy(ref); sqlparser_handle_destroy(h); sqlparser_string_free(out);
    for (r = 0U; r < 5000U; ++r) free(values[r]);
    free(values); free(source); free(expected); ++cases;
    printf("SQLSERVER_BATCH_WIRE_SUPPLIED dialect=%s statements=%zu patches=5000 cells=45000 typed=%d semantic=passed\n",
        families[family_index].name, statement_count, typed);
}

int main(int argc, char **argv)
{
    size_t d; int typed, trailing;
    CHECK(argc == 1 || argc == 3);
    for (d = 0U; d < COUNT(families); ++d) {
        family_index = d; dialect = families[d].dialect;
        for (typed = 0; typed < 2; ++typed) {
            for (trailing = 0; trailing < 2; ++trailing) {
                batch_positive(typed, trailing, trailing ? 121U : 0U, trailing ? 16300U : 0U, 0);
                batch_positive(typed, trailing, 3U, 5U, 1);
            }
            batch_varint_boundaries(typed);
            batch_shape_matrix(typed);
            if (argc == 3) {
                batch_supplied_protocol(argv[1], 5U, typed);
                batch_supplied_protocol(argv[2], 1U, typed);
            }
        }
        batch_ordered_errors_and_fallbacks(); batch_source_fallbacks();
        batch_validation_source_proof_stays_zero();
        batch_borrowed_and_clone(); batch_deep_expression_parity(); batch_certificate_guards(); batch_allocation_boundaries();
    }
#ifdef SQLPARSER_SQLSERVER_WIRE_WRAPPERS
    CHECK(suppressed_batch_certificates > 0U && legacy_applies > 0U);
    CHECK(live_allocations == 0U && native_depth == 0U);
#endif
    if (argc == 1) printf("SQLSERVER_BATCH_WIRE_SUPPLIED not_run: supply actual 5x1000 and single5000 paths\n");
    printf("SQLSERVER_BATCH_WIRE_PIPELINE_PASSED cases=%zu\n", cases); return 0;
}
