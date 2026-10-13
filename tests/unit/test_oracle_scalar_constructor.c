/* Oracle scalar constructor parity, admission and allocation checks. */
#define main scalar_owned_commit_helpers_main
#include "test_oracle_owned_commit.c"
#undef main
#include "../../src/dialect/sqlparser_dialect_oracle.c"
/* Frozen verbatim reference constructor; only function name changed.
 * Independent legacy oracle: never calls any new scalar admission helper. */
static sqlparser_status_t frozen_legacy_parse_value_item(
	const char *sql,
	size_t start,
	size_t end,
	sqlparser_oracle_state_t *state,
	sqlparser_dialect_multi_insert_value_t *out_value,
	uint32_t *out_lexical_flags,
	sqlparser_error_t *out_error)
{
	sqlparser_bind_token_t bind_token;
	const char *key_start;
	size_t key_len;
	size_t sql_len;
	sqlparser_bind_kind_t bind_kind;
	size_t occurrence_before;
	sqlparser_status_t status;

	if (out_value == NULL || state == NULL) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "value arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(out_value, 0, sizeof(*out_value));
	if (out_lexical_flags != NULL) *out_lexical_flags = 0U;
	status = sqlparser_oracle_trimmed_slice_dup(sql, start, end, &out_value->public_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	occurrence_before = state->bind_occurrence_count;
	status = sqlparser_oracle_preprocess_text(out_value->public_sql, state, &out_value->parser_sql, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		sqlparser_oracle_value_clear(out_value);
		return status;
	}
	sql_len = strlen(out_value->public_sql);
	if (sqlparser_bind_token_exact(
		    SQLPARSER_DIALECT_ORACLE,
		    out_value->public_sql,
		    sql_len,
		    0U,
		    sql_len,
		    &bind_token) &&
	    state->bind_occurrence_count == occurrence_before + 1U) {
		bind_kind = bind_token.kind;
		key_start = bind_token.key_length > 0U ?
			out_value->public_sql + bind_token.key_start :
			NULL;
		key_len = bind_token.key_length;
		out_value->has_bind = 1;
		out_value->bind_kind = bind_kind;
		if (bind_kind == SQLPARSER_BIND_KIND_POSITIONAL && key_start == NULL) {
			(void)snprintf(out_value->bind, sizeof(out_value->bind), "%lu", (unsigned long)(occurrence_before + 1U));
		} else {
			if (key_len >= sizeof(out_value->bind)) {
				sqlparser_oracle_value_clear(out_value);
				sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind key is too long");
				return SQLPARSER_STATUS_RESOURCE_LIMIT;
			}
			memcpy(out_value->bind, key_start, key_len);
			out_value->bind[key_len] = '\0';
		}
		if (sql_len >= sizeof(out_value->bind_sql)) {
			sqlparser_oracle_value_clear(out_value);
			sqlparser_error_set_message(out_error, SQLPARSER_STATUS_RESOURCE_LIMIT, "bind SQL is too long");
			return SQLPARSER_STATUS_RESOURCE_LIMIT;
		}
		memcpy(out_value->bind_sql, out_value->public_sql, sql_len);
		out_value->bind_sql[sql_len] = '\0';
		out_value->bind_position = occurrence_before + 1U;
		out_value->has_bind_position = 1;
	} else {
		status = sqlparser_oracle_value_fill_literal(out_value, out_lexical_flags, out_error);
		if (status != SQLPARSER_STATUS_OK) {
			sqlparser_oracle_value_clear(out_value);
			return status;
		}
	}
	if (out_lexical_flags != NULL && strcmp(out_value->public_sql, out_value->parser_sql) == 0)
		*out_lexical_flags |= SQLPARSER_ORACLE_CELL_IDENTITY;
	return SQLPARSER_STATUS_OK;
}
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
/* GNU linker wrappers. Observe the exact ordinal, type and sizes; no timing.
 * PostgreSQL memory-context allocations retain the established exclusion,
 * since its native OOM path can longjmp or abort outside sqlparser ownership. */
typedef struct { void *pointer; size_t identity, bytes; } scalar_slot;
typedef struct { int kind; size_t count, size, old_identity; int success; } scalar_event;
static scalar_slot scalar_ledger[65536];
static scalar_event scalar_events[65536];
static size_t scalar_live, scalar_end, scalar_calls, scalar_fail;
static int scalar_active;
static unsigned scalar_native_depth;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
struct MemoryContextData;
struct MemoryContextData *__real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(struct MemoryContextData *);
struct MemoryContextData *__wrap_pg_query_enter_memory_context(void)
{ ++scalar_native_depth; return __real_pg_query_enter_memory_context(); }
void __wrap_pg_query_exit_memory_context(struct MemoryContextData *p)
{ __real_pg_query_exit_memory_context(p); CHECK(scalar_native_depth); --scalar_native_depth; }
static size_t scalar_find(void *p)
{
    size_t i;
    if (p && scalar_live) for (i = 0; i < scalar_end; ++i)
        if (scalar_ledger[i].pointer == p) return i;
    return COUNT(scalar_ledger);
}
static void scalar_track(void *p, size_t identity, size_t bytes)
{
    size_t i;
    if (!p) return;
    CHECK(scalar_find(p) == COUNT(scalar_ledger));
    for (i = 0; i < scalar_end && scalar_ledger[i].pointer; ++i) {}
    CHECK(i < COUNT(scalar_ledger));
    scalar_ledger[i] = (scalar_slot){p, identity, bytes}; ++scalar_live;
    if (i == scalar_end) ++scalar_end;
}
static scalar_event *scalar_begin_event(int kind, size_t count, size_t size, void *old)
{
    scalar_event *e; size_t slot;
    if (!scalar_active || scalar_native_depth) return NULL;
    CHECK(scalar_calls < COUNT(scalar_events));
    e = &scalar_events[scalar_calls++]; slot = scalar_find(old);
    *e = (scalar_event){kind, count, size,
        old ? (slot < COUNT(scalar_ledger) ? scalar_ledger[slot].identity : (size_t)-1) : 0, 0};
    return e;
}
void *__wrap_malloc(size_t n)
{
    scalar_event *e = scalar_begin_event(1, 1, n, NULL); void *p;
    if (e && scalar_calls == scalar_fail) return NULL;
    p = __real_malloc(n);
    if (e) { e->success = p != NULL; scalar_track(p, scalar_calls, n); }
    return p;
}
void *__wrap_calloc(size_t n, size_t size)
{
    scalar_event *e = scalar_begin_event(2, n, size, NULL); void *p;
    if (e && scalar_calls == scalar_fail) return NULL;
    p = __real_calloc(n, size);
    if (e) { e->success = p != NULL; scalar_track(p, scalar_calls, n * size); }
    return p;
}
void *__wrap_realloc(void *p, size_t n)
{
    size_t slot = scalar_find(p);
    scalar_event *e = scalar_begin_event(3, 1, n, p); void *q;
    if (e && scalar_calls == scalar_fail) return NULL;
    q = __real_realloc(p, n);
    if (e) e->success = q != NULL;
    if (q || !n) {
        if (slot < COUNT(scalar_ledger)) {
            scalar_ledger[slot].pointer = q;
            scalar_ledger[slot].bytes = n;
            if (!q) --scalar_live;
        } else if (e) scalar_track(q, scalar_calls, n);
    }
    return q;
}
void __wrap_free(void *p)
{
    size_t slot = scalar_find(p);
    if (slot < COUNT(scalar_ledger)) {
        scalar_ledger[slot].pointer = NULL; --scalar_live;
    }
    __real_free(p);
}
static void scalar_start(size_t fail)
{
    CHECK(!scalar_live && !scalar_native_depth && !scalar_active);
    scalar_calls = scalar_end = 0; scalar_fail = fail; scalar_active = 1;
}
static void scalar_stop(void)
{ CHECK(scalar_active && !scalar_native_depth); scalar_active = 0; }
static void scalar_record_allocations(void)
{
    size_t i; record_number(scalar_calls);
    for (i = 0; i < scalar_calls; ++i) {
        const scalar_event *e = &scalar_events[i];
        record_number(e->kind); record_number(e->count); record_number(e->size);
        record_number(e->old_identity); record_number(e->success);
    }
}
static void scalar_record_residue(void)
{
    size_t i; record_number(scalar_live);
    for (i = 0; i < scalar_end; ++i) if (scalar_ledger[i].pointer) {
        record_number(scalar_ledger[i].identity); record_number(scalar_ledger[i].bytes);
    }
}
static void scalar_reclaim_recorded_residue(void)
{
    size_t i;
    /* Legacy constructor OOM has a known leak. Observe and compare its exact
     * allocation identity/size first, then reclaim only test-ledger residues
     * so the next independent trial and sanitizer run remain useful. Never
     * use this for ordinary primitive/lifetime checks, which require zero. */
    CHECK(!scalar_active && !scalar_native_depth);
    for (i = 0; i < scalar_end; ++i) if (scalar_ledger[i].pointer) {
        __real_free(scalar_ledger[i].pointer); scalar_ledger[i].pointer = NULL; --scalar_live;
    }
    CHECK(!scalar_live);
}

static void scalar_begin_record(const char *kind, size_t dialect, size_t fixture,
    size_t phase, size_t fail, int null_error, text_buffer *b)
{
    CHECK(record_sink == NULL); memset(b, 0, sizeof(*b)); record_sink = b;
    record_text(kind); record_number(dialect); record_number(fixture);
    record_number(phase); record_number(fail); record_number(null_error);
}
static void scalar_end_record(text_buffer *b)
{
    size_t i;
    CHECK(record_sink == b); record_sink = NULL;
    /* Hex means one physical line per record even for newline/control SQL.
     * A byte comparator retains every error byte and complete state field. */
    for (i = 0; i < b->length; ++i) printf("%02x", (unsigned char)b->data[i]);
    putchar('\n'); free(b->data); memset(b, 0, sizeof(*b));
}
static void scalar_record_bytes(const void *data, size_t length)
{
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = data; size_t i;
    record_number(data != NULL); record_number(length);
    for (i = 0; i < length; ++i) {
        char pair[2] = {hex[p[i] >> 4], hex[p[i] & 15]};
        append_bytes(record_sink, pair, 2);
    }
}
static void scalar_record_origins(sqlparser_handle_t *h)
{
    const sqlparser_identifier_origin_map_t *map = NULL, *cached = NULL;
    sqlparser_error_t e = {0}; sqlparser_status_t status;
    size_t offset, length;
    status = sqlparser_identifier_origins_for_handle(h, &map, &e);
    scalar_record_error(status, &e); if (status != SQLPARSER_STATUS_OK) return;
    record_number(sqlparser_identifier_origin_map_output_length(map));
    for (offset = 0; offset <= h->parser_sql_len + 1; ++offset) {
        /* Exhaust all ranges for short fixtures. Large boundary fixtures use
         * every start with 0/1/2/full/out-of-bounds lengths plus full suffix.
         * No quadratic work is hidden in the larger width fixtures. */
        size_t limit = h->parser_sql_len <= 768 ? h->parser_sql_len + 1 - offset : 2;
        for (length = 0; length <= limit; ++length) {
            sqlparser_identifier_origin_t origin = {0};
            record_number(offset); record_number(length);
            record_number(sqlparser_identifier_origin_map_lookup(map, offset, length, &origin));
            record_number(origin.kind); record_number(origin.source_offset); record_number(origin.source_length);
        }
        if (h->parser_sql_len > 768) {
            sqlparser_identifier_origin_t origin = {0};
            length = offset <= h->parser_sql_len ? h->parser_sql_len - offset : 0;
            record_number(offset); record_number(length);
            record_number(sqlparser_identifier_origin_map_lookup(map, offset, length, &origin));
            record_number(origin.kind); record_number(origin.source_offset); record_number(origin.source_length);
        }
    }
    memset(&e, 0, sizeof(e));
    status = sqlparser_identifier_origins_for_handle(h, &cached, &e);
    scalar_record_error(status, &e); record_number(cached == map);
}
static void scalar_record_handle(sqlparser_handle_t *h, int origins, int graph)
{
    char *text = NULL; size_t i, j, count; sqlparser_error_t e = {0};
    sqlparser_status_t status;
    record_number(h != NULL); if (!h) return;
    record_text(h->sql); record_text(h->parser_sql); record_text(h->current_sql);
    record_text(h->current_parser_sql); record_number(h->sql_len); record_number(h->parser_sql_len);
    record_number(h->generation); record_number(h->statement_count);
    scalar_record_bytes(h->parse_tree.data, h->parse_tree.len);
    scalar_record_state(h->dialect_state, h);
    status = sqlparser_deparse(h, &text, &e); scalar_record_error(status, &e);
    record_text(text); free(text);
    if (graph) for (i = 0; i < sqlparser_statement_count(h); ++i) {
        sqlparser_query_graph_view_t g;
        memset(&e, 0, sizeof(e)); memset(&g, 0, sizeof(g));
        status = sqlparser_statement_query_graph(h, i, &g, &e);
        scalar_record_error(status, &e);
        if (status == SQLPARSER_STATUS_OK) record_graph(h, &g);
        count = 0; memset(&e, 0, sizeof(e));
        status = sqlparser_statement_literal_count(h, i, &count, &e);
        scalar_record_error(status, &e); record_number(count);
        if (status == SQLPARSER_STATUS_OK) for (j = 0; j < count; ++j) {
            sqlparser_literal_view_t literal; memset(&literal, 0, sizeof(literal));
            memset(&e, 0, sizeof(e));
            status = sqlparser_statement_literal(h, i, j, &literal, &e);
            scalar_record_error(status, &e);
            if (status == SQLPARSER_STATUS_OK) record_sqlparser_literal_view_t(&literal);
        }
    }
    if (origins) scalar_record_origins(h);
    scalar_record_state(h->dialect_state, h);
    if (h->ast) {
        size_t n = protobuf_c_message_get_packed_size((ProtobufCMessage *)h->ast);
        unsigned char *wire = malloc(n ? n : 1); CHECK(wire);
        CHECK(protobuf_c_message_pack((ProtobufCMessage *)h->ast, wire) == n);
        scalar_record_bytes(wire, n); free(wire);
    } else scalar_record_bytes(NULL, 0);
}

static const char *const scalar_tokens[] = {
    "''", "'a'", "''''", "'O''Reilly'", "'张三 Ω 😀'", "'-- :bind @db MINUS N''x'''",
    "0", "+0", "-0", "0000123", "+123", "-123", "2147483647", "2147483648",
    "-2147483648", "-2147483649", "9223372036854775807", "9223372036854775808",
    "-9223372036854775808", "-9223372036854775809", "999999999999999999999999999999999",
    ".0", "0.", "1.25", "+.5", "-.5", "-0.", "+000.001", "999999999999.999999999",
    "NULL", "null", "NuLl", "current_date", "CURRENT_TIME", "current_timestamp",
    "localtime", "LOCALtimestamp", "current_role", "CURRENT_USER", "session_user", "user",
    "current_catalog", "CURRENT_SCHEMA", "  'trimmed' \t\r\n", "\t -12.5 \n",
    "N'national'", "n'张三'", "q'[O'Reilly]'", "Q'{Ω :q MINUS}'", "nq'<national>'",
    "$$dollar :d MINUS$$", "$tag$quoted N'x'$tag$", ":named", ":1", "?", "$1",
    "CURRENT_TIMESTAMP(3)", "localtime(0)", "current_time (6)", "current_date_extra",
    "current_userx", "user_name", "NULLIF(1, 2)", "NULLx", "xNULL", "NULL::text",
    "1e2", "1E-2", "0x12", "1_000", "1..2", ".", "+", "-", "++1", "--1",
    "1 + 2", "- 1", "(+1)", "f('before', N'after', :p)", "'a' || 'b'",
    "'slash\\x'", "'new\nline'", "'tab\ttext'", "'del\177text'", "'control\001text'",
    "'a' 'b'", "'a'b'", "'unterminated", "q'[unterminated'", "'a'/*tail*/",
    "/*head*/'a'", "1 --tail\n", "/* unterminated", "'a';", "", " \t\r\n",
    "(SELECT N'before' FROM T@L MINUS SELECT 'after' FROM U@M)",
    "(SELECT :b FROM T EXCEPT SELECT :c FROM U)", "CONNECT_BY_ROOT x", "PRIOR x",
    "'utf8-\xc3\xa9'", "'invalid-utf8-\xff'", "current_schema/*x*/", "USER()"
};
static const char *const scalar_seeds[] = {
    "",
    "SELECT 'ordinary', N'national', :before FROM T@remote MINUS SELECT 'other', N'n2', :after FROM U@other EXCEPT SELECT 'third', N'n3', :last FROM V",
    "UPDATE T SET C = :before RETURNING C INTO :out; SELECT PRIOR id, CONNECT_BY_ROOT name FROM Tree START WITH parent IS NULL CONNECT BY PRIOR id = parent"
};
static const char *const scalar_tail[] = {"'after'", "N'after-national'", ":after_bind",
    "(SELECT N'tail' FROM AfterTable@AfterLink MINUS SELECT 'last' FROM LastTable EXCEPT SELECT 'end' FROM EndTable)"};
static char *scalar_boundary_tokens[20];
static size_t scalar_token_count(void)
{ return COUNT(scalar_tokens) + COUNT(scalar_boundary_tokens); }
static const char *scalar_token(size_t i)
{ return i < COUNT(scalar_tokens) ? scalar_tokens[i] : scalar_boundary_tokens[i - COUNT(scalar_tokens)]; }
static void scalar_init_boundaries(void)
{
    static const size_t lengths[] = {127,128,129,255,256,257,511,512,513,1024};
    size_t i, kind, j;
    for (kind = 0; kind < 2; ++kind) for (i = 0; i < COUNT(lengths); ++i) {
        text_buffer b = {0}; append(&b, "'");
        for (j = 0; j < lengths[i]; ++j)
            append(&b, kind && j % 11 == 0 ? "''" : "a");
        append(&b, "'"); scalar_boundary_tokens[kind * COUNT(lengths) + i] = b.data;
    }
}
static void scalar_seed_error(sqlparser_error_t *e, int mode)
{
    memset(e, 0, sizeof(*e));
    if (mode == 2) {
        e->code = SQLPARSER_STATUS_UNSUPPORTED; e->cursor = -17;
        e->line = 31; e->column = 47; strcpy(e->message, "preexisting-error-must-not-be-sanitized");
    }
}

static sqlparser_oracle_state_t *scalar_make_seed(size_t seed)
{
    sqlparser_oracle_state_t *s = NULL; char *sql = NULL; sqlparser_error_t e = {0};
    CHECK(sqlparser_oracle_state_new(&s, &e) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_oracle_preprocess_text(scalar_seeds[seed < COUNT(scalar_seeds) ? seed : 1], s, &sql, &e) == SQLPARSER_STATUS_OK);
    free(sql);
    if (seed >= COUNT(scalar_seeds))
        s->national_literals.literal_count = seed == COUNT(scalar_seeds) ? SIZE_MAX - 1 : SIZE_MAX;
    /* Nonzero fragment positions reveal mistaken resets as well as counts. */
    sqlparser_dialect_national_literals_begin_fragment(&s->national_literals);
    sqlparser_dialect_minuses_begin_fragment(&s->minuses);
    return s;
}
typedef sqlparser_status_t (*scalar_constructor_fn)(const char *, size_t, size_t,
    sqlparser_oracle_state_t *, sqlparser_dialect_multi_insert_value_t *, uint32_t *, sqlparser_error_t *);

static char *scalar_primitive_once(scalar_constructor_fn constructor, size_t token, size_t seed,
    size_t fail, int null_error, size_t *calls)
{
    sqlparser_oracle_state_t *s = scalar_make_seed(seed);
    sqlparser_dialect_multi_insert_value_t value; sqlparser_error_t e = {0};
    sqlparser_status_t status; uint32_t flags = UINT32_MAX; size_t i;
    text_buffer b = {0}; char *input = copy_text(scalar_token(token));
    scalar_seed_error(&e, null_error);
    CHECK(!record_sink); record_sink = &b;
    scalar_record_state(s, NULL);
    scalar_start(fail);
    status = constructor(input, 0, strlen(input), s, &value, &flags, null_error == 1 ? NULL : &e);
    scalar_stop(); *calls = scalar_calls;
    /* Capture the exact result/error before any further API can clear it. */
    scalar_record_error(status, null_error == 1 ? NULL : &e);
    scalar_record_allocations(); record_number(flags); scalar_record_value(&value);
    scalar_record_state(s, NULL);
    memset(input, 'x', strlen(input)); free(input);
    if (status == SQLPARSER_STATUS_OK && value.public_sql && value.parser_sql) {
        char *saved = copy_text(value.parser_sql);
        CHECK(value.public_sql != value.parser_sql);
        if (*value.public_sql) value.public_sql[0] ^= 1;
        CHECK(strcmp(value.parser_sql, saved) == 0); free(saved);
    }
    sqlparser_oracle_value_clear(&value);
    /* Continue the exact existing state even after failure: state mutations
     * before swallowed/propagated OOM must agree with the frozen constructor. */
    for (i = 0; i < COUNT(scalar_tail); ++i) {
        memset(&e, 0, sizeof(e)); flags = 0;
        status = constructor(scalar_tail[i], 0, strlen(scalar_tail[i]), s, &value, &flags, &e);
        scalar_record_error(status, &e); record_number(flags); scalar_record_value(&value);
        scalar_record_state(s, NULL); sqlparser_oracle_value_clear(&value);
    }
    {
        void *clone = NULL;
        memset(&e, 0, sizeof(e)); status = sqlparser_oracle_clone_state(s, &clone, &e);
        scalar_record_error(status, &e); scalar_record_state(clone, NULL);
        sqlparser_oracle_state_destroy(s); s = NULL;
        scalar_record_state(clone, NULL); sqlparser_oracle_state_destroy(clone);
    }
    CHECK(!scalar_live && !scalar_native_depth); record_sink = NULL;
    return b.data;
}
static void scalar_primitive(int allocations)
{
    size_t token, seed, fail, calls, legacy_calls; int null_error;
    for (token = 0; token < scalar_token_count(); ++token)
    for (seed = 0; seed < COUNT(scalar_seeds) + 2; ++seed)
    for (null_error = 0; null_error < 3; ++null_error) {
        size_t stop = allocations ? 65535 : 0;
        for (fail = 0; fail <= stop; ++fail) {
            char *actual = scalar_primitive_once(sqlparser_oracle_parse_value_item, token, seed, fail, null_error, &calls);
            char *legacy = scalar_primitive_once(frozen_legacy_parse_value_item, token, seed, fail, null_error, &legacy_calls);
            CHECK(calls == legacy_calls); same_text(actual, legacy);
            free(actual); free(legacy);
            if (!allocations || (fail && calls < fail)) break;
        }
        CHECK(!allocations || fail <= stop);
    }
}

static const char *const scalar_sql[] = {
    "INSERT ALL INTO T(A,B,C,D,E,F,G,H,I) VALUES ('old','O''Reilly','张三',-1,+2,.5,1.,NULL,CURRENT_DATE) INTO U(A) VALUES ('tail') SELECT 1 FROM Dual",
    "INSERT ALL INTO T(A,B,C,D,E,F) VALUES (:before, N'national', 'middle', q'[q-string]', 'after', :last) INTO U(A,B) VALUES ('tail', N'last-national') SELECT N'source', :source FROM Src@Link MINUS SELECT 'other', :other FROM OtherSrc EXCEPT SELECT 'end', :end FROM EndSrc",
    "INSERT FIRST WHEN :b = 1 THEN INTO T(A,B,C) VALUES (N'before', 'cell', :after) WHEN x = 'x' THEN INTO U(A) VALUES ('u') ELSE INTO V(A,B) VALUES ('v', CURRENT_USER) SELECT x FROM S@Remote",
    "INSERT ALL INTO T(A,B) VALUES ('first', N'next') SELECT PRIOR id, CONNECT_BY_ROOT name FROM Tree START WITH parent IS NULL CONNECT BY PRIOR id = parent",
    "INSERT ALL INTO T(A,B) VALUES (1, 'before') SELECT 1 FROM Dual; UPDATE U SET C='after' RETURNING C INTO :out",
    "UPDATE U SET C=N'before' RETURNING C INTO :out; INSERT ALL INTO T(A,B) VALUES ('after', :in) SELECT 1 FROM Dual",
    "INSERT ALL INTO T VALUES ('one') INTO U VALUES ('two', 2, NULL) INTO V VALUES (CURRENT_SCHEMA, 'three') SELECT 1 FROM Dual",
    "INSERT ALL INTO T(A,B) VALUES ('x', .) SELECT 1 FROM Dual",
    "INSERT ALL INTO T(A,B) VALUES ('x', 'unterminated) SELECT 1 FROM Dual",
    "INSERT ALL INTO T(A,B) VALUES ('x', CURRENT_TIMESTAMP(3)) SELECT 1 FROM Dual",
    "INSERT ALL INTO \"MixedCase\"@\"Remote\"(\"Col\",B) VALUES ('x', :b) SELECT 1 FROM Dual"
};
static const sqlparser_dialect_t scalar_dialects[] = {
    SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE
};
static sqlparser_handle_t *scalar_parse_owned(sqlparser_dialect_t dialect, const char *sql,
    sqlparser_status_t *status, sqlparser_error_t *e, int null_error, size_t fail, int trace)
{
    sqlparser_parse_options_t options; sqlparser_handle_t *h = NULL;
    char *owned = copy_text(sql);
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    options.limits.max_sql_bytes = 8U * 1024U * 1024U;
    options.limits.max_output_bytes = 32U * 1024U * 1024U;
    if (trace) scalar_start(fail);
    *status = sqlparser_parse_with_options(owned, &options, &h, null_error == 1 ? NULL : e);
    if (trace) scalar_stop();
    memset(owned, 'x', strlen(owned)); free(owned);
    return h;
}
static void scalar_handle_case(sqlparser_dialect_t dialect, const char *sql, size_t fixture, int mutate)
{
    sqlparser_error_t e = {0}; sqlparser_status_t status;
    sqlparser_handle_t *h; text_buffer b; size_t round;
    h = scalar_parse_owned(dialect, sql, &status, &e, 0, 0, 1);
    scalar_begin_record("constructor", dialect, fixture, 0, 0, 0, &b);
    scalar_record_error(status, &e); scalar_record_allocations();
    if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 1, 1);
    else record_number(h != NULL);
    scalar_end_record(&b);
    if (status == SQLPARSER_STATUS_OK && h) {
        sqlparser_handle_t *clone = NULL;
        memset(&e, 0, sizeof(e)); status = sqlparser_handle_clone(h, &clone, &e);
        scalar_begin_record("clone", dialect, fixture, 0, 0, 0, &b);
        scalar_record_error(status, &e); sqlparser_handle_destroy(h); h = clone;
        if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 1, 1);
        else record_number(h != NULL);
        scalar_end_record(&b);
    }
    if (status == SQLPARSER_STATUS_OK && h && mutate) for (round = 0; round < 7; ++round) {
        sqlparser_patch_t patches[2] = {{0}}; sqlparser_literal_value_t literal = {0};
        sqlparser_patch_list_t list = {patches, round == 4 ? 2 : 1};
        char *raw = copy_text(round == 1 ? "N'national-after'" : round == 2 ? ":changed_bind" :
            round == 3 ? "CURRENT_TIMESTAMP" : "'raw-''Ω'''");
        char *decoded = copy_text("typed-张三-'quoted'");
        patches[0].op = SQLPARSER_PATCH_REPLACE; patches[0].selector = "stmt[0].insert_cell[0][0]";
        if (round == 0 || round == 5) {
            literal.kind = SQLPARSER_LITERAL_KIND_STRING; literal.string_value = decoded;
            patches[0].literal = &literal;
        } else patches[0].sql = raw;
        patches[1] = patches[0]; patches[1].sql = "'duplicate-final'"; patches[1].literal = NULL;
        memset(&e, 0, sizeof(e));
        status = round == 6 ? sqlparser_oracle_multi_insert_insert_column_sql(
            h, 0, 0, 0, "ExtraColumn", "'new-column-cell'", &e) : sqlparser_apply_patch(h, &list, &e);
        memset(raw, 'x', strlen(raw)); memset(decoded, 'x', strlen(decoded)); free(raw); free(decoded);
        scalar_begin_record("patch", dialect, fixture, round, 0, 0, &b);
        scalar_record_error(status, &e);
        if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 1, 1);
        else record_number(h != NULL);
        scalar_end_record(&b);
        if (status != SQLPARSER_STATUS_OK) break;
        if (round == 2) {
            sqlparser_handle_t *clone = NULL;
            memset(&e, 0, sizeof(e)); status = sqlparser_handle_clone(h, &clone, &e);
            scalar_begin_record("patched-clone", dialect, fixture, round, 0, 0, &b);
            scalar_record_error(status, &e); sqlparser_handle_destroy(h); h = clone;
            if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 1, 1);
            else record_number(h != NULL);
            scalar_end_record(&b); if (status != SQLPARSER_STATUS_OK || !h) break;
        }
    }
    sqlparser_handle_destroy(h); CHECK(!scalar_live && !scalar_native_depth);
}
static char *scalar_width_fixture(size_t width)
{
    text_buffer b = {0}; size_t row, col;
    static const char *const values[] = {"'ordinary'", "-2147483649", "+.5", "NULL",
        "CURRENT_TIMESTAMP", "N'national'", ":bind", "q'[q]'", "'Ω''张三'"};
    append(&b, "INSERT ALL ");
    for (row = 0; row < 3; ++row) {
        size_t n = row == 1 ? 1 : row == 2 ? width + 1 : width;
        append_format(&b, "INTO T%zu VALUES (", row);
        for (col = 0; col < n; ++col) {
            if (col) append(&b, ", ");
            append(&b, values[(row + col) % COUNT(values)]);
        }
        append(&b, ") ");
    }
    append(&b, "SELECT N'source', :src FROM SourceTable@Remote MINUS SELECT 'end', :end FROM OtherTable");
    return b.data;
}
static void scalar_handles(void)
{
    size_t d, i; static const size_t widths[] = {1,2,3,8,9,10,31,32,33,63,64,65};
    for (d = 0; d < COUNT(scalar_dialects); ++d) {
        for (i = 0; i < COUNT(scalar_sql); ++i)
            scalar_handle_case(scalar_dialects[d], scalar_sql[i], i, i < 4 || i == 6);
        for (i = 0; i < COUNT(widths); ++i) {
            char *sql = scalar_width_fixture(widths[i]);
            scalar_handle_case(scalar_dialects[d], sql, 100 + i, 1); free(sql);
        }
        for (i = 0; i < scalar_token_count(); ++i) {
            text_buffer b = {0}; append(&b, "INSERT ALL INTO T VALUES (N'before', ");
            append(&b, scalar_token(i)); append(&b, ", 'after', N'last', :tail) SELECT 1 FROM Dual");
            scalar_handle_case(scalar_dialects[d], b.data, 1000 + i, 0); free(b.data);
        }
    }
}
static void scalar_constructor_oom(void)
{
    size_t d, fixture, fail, calls; int cloning, null_error;
    /* Full trace/success payload on every ordinal. Optional-proof and swallowed
     * failures remain successful records with their precise state and error. */
    for (d = 0; d < COUNT(scalar_dialects); ++d)
    for (fixture = 0; fixture < 3; ++fixture)
    for (cloning = 0; cloning < 2; ++cloning)
    for (null_error = 0; null_error < 3; ++null_error) {
        sqlparser_error_t e = {0}; sqlparser_status_t status;
        sqlparser_handle_t *source = NULL;
        if (cloning) {
            source = scalar_parse_owned(scalar_dialects[d], scalar_sql[fixture], &status, &e, 0, 0, 0);
            CHECK(status == SQLPARSER_STATUS_OK && source);
            /* The legacy mixed national-literal fixtures can legitimately
             * reject AST owner binding. Clone their original parsed state;
             * the plain fixture separately covers an AST-backed source. */
            if (fixture == 0)
                CHECK(sqlparser_handle_ensure_ast(source, &e) == SQLPARSER_STATUS_OK);
        }
        for (fail = 0; fail < 65536; ++fail) {
            text_buffer b; sqlparser_handle_t *h = NULL;
            scalar_seed_error(&e, null_error);
            if (cloning) {
                scalar_start(fail); status = sqlparser_handle_clone(source, &h, null_error == 1 ? NULL : &e); scalar_stop();
            } else h = scalar_parse_owned(scalar_dialects[d], scalar_sql[fixture], &status, &e, null_error, fail, 1);
            calls = scalar_calls;
            scalar_begin_record(cloning ? "clone-oom" : "constructor-oom", scalar_dialects[d], fixture, 0, fail, null_error, &b);
            scalar_record_error(status, null_error == 1 ? NULL : &e); scalar_record_allocations();
            if (status == SQLPARSER_STATUS_OK) scalar_record_handle(h, 0, 1);
            else record_number(h != NULL);
            sqlparser_handle_destroy(h);
            scalar_record_residue(); scalar_end_record(&b);
            if (scalar_live) fprintf(stderr, "legacy-residue dialect=%d fixture=%zu clone=%d error-mode=%d fail=%zu live=%zu\n",
                scalar_dialects[d], fixture, cloning, null_error, fail, scalar_live);
            scalar_reclaim_recorded_residue(); CHECK(!scalar_native_depth);
            if (fail && calls < fail) { CHECK(status == SQLPARSER_STATUS_OK); break; }
        }
        CHECK(fail < 65536); sqlparser_handle_destroy(source);
    }
}
static void scalar_admission(void)
{
#ifdef SCALAR_OPTIMIZED
    static const char *const strings[] = {"''", "''''", "'a''b'", "'张三 Ω 😀'", "'NULL :bind @remote MINUS'"};
    static const char *const scalars[] = {"NULL", "nUlL", "0", "+0", "-0", "0001", "+123", "-123",
        "9223372036854775808", "-9223372036854775809", ".5", "1.", "+.5", "-0.00",
        "current_date", "CURRENT_TIME", "current_timestamp", "localtime", "LOCALtimestamp",
        "current_role", "CURRENT_USER", "session_user", "user", "current_catalog", "CURRENT_SCHEMA"};
    static const char *const fallback[] = {NULL, "", " ", "SYSTEM_USER", "system_user", "CURRENT_TIMESTAMP(3)",
        "CURRENT_TIME (6)", "LOCALTIME(0)", "CURRENT_DATE_extra", "NULLx", "user_name", "USER()",
        "N'x'", "q'[x]'", "nq'{x}'", "$$x$$", "$t$x$t$", "E'x'", "B'01'", "U&'x'",
        ":bind", "?", "$1", "1e2", "1E-2", "0x1", "1_000", ".", "+", "-", "1..0",
        "1+2", "- 1", "(+1)", "'x' || 'y'", "'x'/*comment*/", "/*comment*/1", "--1",
        "'slash\\x'", "'new\nline'", "'tab\ttext'", "'del\177text'", "'control\001text'",
        "'a' 'b'", "'a'b'", "'unterminated", "'x';", "'x' ", " 'x'"};
    size_t i, n, checks = 0;
    for (i = 0; i < COUNT(strings); ++i) {
        n = SIZE_MAX; CHECK(sqlparser_oracle_value_identity_kind(strings[i], &n) == 1);
        CHECK(n == strlen(strings[i])); ++checks;
    }
    for (i = 0; i < COUNT(scalars); ++i) {
        char *suffix; size_t length = strlen(scalars[i]);
        n = SIZE_MAX; CHECK(sqlparser_oracle_value_identity_kind(scalars[i], &n) == 2);
        CHECK(n == length); ++checks;
        suffix = malloc(length + 2); CHECK(suffix); memcpy(suffix, scalars[i], length);
        suffix[length] = 'x'; suffix[length + 1] = 0;
        CHECK(sqlparser_oracle_value_identity_kind(suffix, &n) == 0); ++checks; free(suffix);
    }
    for (i = 0; i < COUNT(fallback); ++i) {
        n = SIZE_MAX; CHECK(sqlparser_oracle_value_identity_kind(fallback[i], &n) == 0); ++checks;
    }
    for (i = 0; i < COUNT(scalar_boundary_tokens); ++i) {
        n = SIZE_MAX; CHECK(sqlparser_oracle_value_identity_kind(scalar_boundary_tokens[i], &n) == 1);
        CHECK(n == strlen(scalar_boundary_tokens[i])); ++checks;
    }
    printf("optimized-admission-checks=%zu\n", checks);
#else
    fputs("--admission requires SCALAR_OPTIMIZED build\n", stderr); exit(2);
#endif
}
int main(int argc, char **argv)
{
    size_t i;
    if (argc > 2) { fputs("usage: test_oracle_scalar_constructor [--primitive|--primitive-oom|--handles|--constructor-oom|--admission]\n", stderr); return 2; }
    scalar_init_boundaries();
    if (argc == 1) { scalar_admission(); scalar_primitive(0); }
    else if (!strcmp(argv[1], "--primitive")) scalar_primitive(0);
    else if (!strcmp(argv[1], "--primitive-oom")) scalar_primitive(1);
    else if (!strcmp(argv[1], "--handles")) scalar_handles();
    else if (!strcmp(argv[1], "--constructor-oom")) scalar_constructor_oom();
    else if (!strcmp(argv[1], "--admission")) scalar_admission();
    else return 2;
    for (i = 0; i < COUNT(scalar_boundary_tokens); ++i) free(scalar_boundary_tokens[i]);
    CHECK(!record_sink && !scalar_live && !scalar_active && !scalar_native_depth);
    puts("PASS: Oracle scalar constructor parity checks");
    return 0;
}
