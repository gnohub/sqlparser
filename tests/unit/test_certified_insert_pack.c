/* The certified commit's private writer is checked against the complete generic
 * protobuf implementation, including fields that must force its fallback. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
#include "sqlparser_test_failure.h"
#endif

static sqlparser_error_t error;
static const char *stage;
static size_t cases, fault_index;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s stage=%s case=%zu fault=%zu error=%s\n", __FILE__, __LINE__, #x, stage ? stage : "", cases, fault_index, error.message); abort(); } } while (0)

#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
/* Every allocation made inside the helper has both boundaries guarded. This
 * covers its private scratch as well as the exact-sized caller-owned output. */
enum { GUARD = 32, MAX_LIVE = 16 };
static struct { unsigned char *base, *data; size_t size; } live[MAX_LIVE];
static size_t helper_depth, allocation_calls, failures, helper_calls;
static size_t guarded_outputs, pack_calls, pack_fault_at, pack_failures;
static int armed, last_handled;
static sqlparser_status_t last_status;
void *__real_malloc(size_t);
void __real_free(void *);
size_t __real_protobuf_c_message_pack(const ProtobufCMessage *, uint8_t *);
sqlparser_status_t __real_sqlparser_pack_certified_insert(const PgQuery__ParseResult *, PgQueryProtobuf *, int *);
static void check_guards(size_t index)
{
    size_t i;
    for (i = 0; i < GUARD; ++i) {
        CHECK(live[index].base[i] == 0xa5);
        CHECK(live[index].data[live[index].size + i] == 0x5a);
    }
}
void *__wrap_malloc(size_t size)
{
    unsigned char *p;
    size_t index;
    if (helper_depth == 0U) return __real_malloc(size);
    ++allocation_calls;
    if (armed && allocation_calls == fault_index) { ++failures; return NULL; }
    for (index = 0; index < MAX_LIVE && live[index].base != NULL; ++index) {}
    CHECK(index < MAX_LIVE && size <= SIZE_MAX - 2U * GUARD);
    p = __real_malloc(size + 2U * GUARD);
    if (p == NULL) return NULL;
    memset(p, 0xa5, GUARD); memset(p + GUARD, 0xc3, size);
    memset(p + GUARD + size, 0x5a, GUARD);
    live[index].base = p; live[index].data = p + GUARD; live[index].size = size;
    return p + GUARD;
}
void __wrap_free(void *p)
{
    size_t index;
    for (index = 0; index < MAX_LIVE; ++index) if (live[index].data == p && p != NULL) {
        check_guards(index); __real_free(live[index].base);
        memset(&live[index], 0, sizeof(live[index])); return;
    }
    __real_free(p);
}
size_t __wrap_protobuf_c_message_pack(const ProtobufCMessage *message, uint8_t *out)
{
    size_t actual = __real_protobuf_c_message_pack(message, out);
    if (helper_depth != 0U && ++pack_calls == pack_fault_at) {
        ++pack_failures; return actual + 1U;
    }
    return actual;
}
sqlparser_status_t __wrap_sqlparser_pack_certified_insert(
    const PgQuery__ParseResult *ast, PgQueryProtobuf *out, int *handled)
{
    sqlparser_status_t status;
    size_t index;
    ++helper_calls; ++helper_depth;
    status = __real_sqlparser_pack_certified_insert(ast, out, handled);
    --helper_depth; last_handled = *handled; last_status = status;
    for (index = 0; index < MAX_LIVE; ++index) if (live[index].base != NULL) {
        check_guards(index);
        if (out->data == (char *)live[index].data) {
            CHECK(out->len == live[index].size); ++guarded_outputs;
        }
    }
    return status;
}
static void assert_no_live(void)
{
    size_t i; CHECK(helper_depth == 0U);
    for (i = 0; i < MAX_LIVE; ++i) CHECK(live[i].base == NULL);
}
static void arm(size_t failure)
{
    assert_no_live(); allocation_calls = failures = 0U; fault_index = failure; armed = 1;
}
static void disarm(void) { armed = 0; }
#endif

static const char base_sql[] =
    "INSERT INTO t(a,b,c) VALUES (0,'left',2147483647),(128,'right',16384);";
/* Production deliberately dispatches bulk INSERTs only; the direct helper
 * also accepts tiny fixtures so its complete wire grammar can be checked. */
static char *bulk_sql(void)
{
    size_t used = 0U, i;
    char *sql = malloc(2048U); CHECK(sql != NULL);
    used += (size_t)snprintf(sql + used, 2048U - used, "INSERT INTO t(a,b,c) VALUES ");
    for (i = 0U; i < 32U; ++i)
        used += (size_t)snprintf(sql + used, 2048U - used, "%s(%zu,'old',2147483647)", i ? "," : "", i);
    CHECK(used + 2U <= 2048U); sql[used++] = ';'; sql[used] = '\0';
    return sql;
}
static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_t options;
    sqlparser_parse_options_default(&options); options.dialect = SQLPARSER_DIALECT_MYSQL;
    CHECK(sqlparser_parse_with_options(sql, &options, &h, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_ensure_ast(h, &error) == SQLPARSER_STATUS_OK);
    return h;
}
static PgQuery__InsertStmt *insert_of(PgQuery__ParseResult *ast) { return ast->stmts[0]->stmt->insert_stmt; }
static PgQuery__SelectStmt *select_of(PgQuery__ParseResult *ast) { return insert_of(ast)->select_stmt->select_stmt; }
static PgQuery__List *row_of(PgQuery__ParseResult *ast, size_t row) { return select_of(ast)->values_lists[row]->list; }
static PgQuery__AConst *cell_of(PgQuery__ParseResult *ast, size_t row, size_t col) { return row_of(ast, row)->items[col]->a_const; }
static PgQueryProtobuf generic(const PgQuery__ParseResult *ast)
{
    PgQueryProtobuf out = {0};
    unsigned char *storage;
    size_t i;
    out.len = protobuf_c_message_get_packed_size(&ast->base);
    storage = malloc(out.len + 32U); CHECK(storage != NULL);
    memset(storage, 0x6d, out.len + 32U);
    CHECK(protobuf_c_message_pack(&ast->base, storage + 16U) == out.len);
    for (i = 0; i < 16U; ++i) CHECK(storage[i] == 0x6d && storage[16U + out.len + i] == 0x6d);
    out.data = malloc(out.len ? out.len : 1U); CHECK(out.data != NULL);
    memcpy(out.data, storage + 16U, out.len); free(storage);
    return out;
}
static void same(const PgQueryProtobuf *a, const PgQueryProtobuf *b)
{
    CHECK(a->len == b->len && a->data != NULL && b->data != NULL);
    CHECK(memcmp(a->data, b->data, a->len) == 0);
}
static void parity(PgQuery__ParseResult *ast, int expected_handled)
{
    PgQueryProtobuf before = generic(ast), after, out = {99U, (char *)(uintptr_t)1U};
    int handled = -1;
    ++cases;
    CHECK(sqlparser_pack_certified_insert(ast, &out, &handled) == SQLPARSER_STATUS_OK);
    CHECK(handled == expected_handled);
    if (handled) { same(&before, &out); free(out.data); }
    else CHECK(out.len == 0U && out.data == NULL);
    /* Unsupported inputs as well as successes must leave every source byte. */
    after = generic(ast); same(&before, &after);
    free(before.data); free(after.data);
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
    assert_no_live();
#endif
}
static void reject_malformed(PgQuery__ParseResult *ast)
{
    PgQueryProtobuf out = {99U, (char *)(uintptr_t)1U};
    int handled = -1;
    ++cases;
    CHECK(sqlparser_pack_certified_insert(ast, &out, &handled) == SQLPARSER_STATUS_OK);
    CHECK(!handled && out.len == 0U && out.data == NULL);
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
    assert_no_live();
#endif
}

/* Include every specialized instance, not merely one representative Node: a
 * guard accidentally omitted on later rows or a different union arm is caught. */
static size_t messages(PgQuery__ParseResult *ast, ProtobufCMessage **out)
{
    PgQuery__InsertStmt *insert = insert_of(ast);
    PgQuery__SelectStmt *select = select_of(ast);
    size_t n = 0U, r, c;
    out[n++] = &ast->base; out[n++] = &ast->stmts[0]->base;
    out[n++] = &ast->stmts[0]->stmt->base; out[n++] = &insert->base;
    out[n++] = &insert->select_stmt->base; out[n++] = &select->base;
    for (r = 0; r < select->n_values_lists; ++r) {
        PgQuery__List *row = row_of(ast, r);
        out[n++] = &select->values_lists[r]->base; out[n++] = &row->base;
        for (c = 0; c < row->n_items; ++c) {
            PgQuery__AConst *cell = cell_of(ast, r, c);
            out[n++] = &row->items[c]->base; out[n++] = &cell->base;
            out[n++] = cell->val_case == PG_QUERY__A__CONST__VAL_IVAL ? &cell->ival->base : &cell->sval->base;
        }
    }
    return n;
}
static ProtobufCMessageUnknownField *new_unknown(void)
{
    ProtobufCMessageUnknownField *u = malloc(sizeof(*u));
    CHECK(u != NULL); memset(u, 0, sizeof(*u));
    u->tag = 19000U; u->wire_type = PROTOBUF_C_WIRE_TYPE_VARINT; u->len = 2U;
    u->data = malloc(u->len); CHECK(u->data != NULL);
    u->data[0] = 0x81; u->data[1] = 0x01;
    return u;
}
static void unknown_and_descriptor_guards(void)
{
    sqlparser_handle_t *h = parse(base_sql);
    ProtobufCMessage *all[512];
    char *bulk = bulk_sql();
    size_t count = messages(h->ast, all), i;
    stage = "unknown fields and copied specialized descriptors";
    parity(h->ast, 1);
    for (i = 0; i < count; ++i) {
        ProtobufCMessage *m = all[i];
        const ProtobufCMessageDescriptor *original = m->descriptor;
        ProtobufCMessageDescriptor copied = *original;
        m->n_unknown_fields = 1U; m->unknown_fields = new_unknown();
        parity(h->ast, 0);
        free(m->unknown_fields->data); free(m->unknown_fields);
        m->n_unknown_fields = 0U; m->unknown_fields = NULL;
        m->descriptor = &copied; parity(h->ast, 0); m->descriptor = original;
    }
    sqlparser_handle_destroy(h);

    /* Exercise the actual commit fallback, with separately owned unknowns at
     * each specialized level; compare the entire wire including their tags. */
    stage = "commit fallback preserves unknown fields";
    for (i = 0; i < count; ++i) {
        PgQueryProtobuf expected;
        char *owned = sqlparser_strdup(bulk);
        size_t actual_count, index;
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
        size_t before_calls = helper_calls;
#endif
        h = parse(bulk); CHECK(owned != NULL);
        actual_count = messages(h->ast, all); CHECK(actual_count >= count && actual_count <= 512U);
        index = i < 6U ? i : actual_count - (count - 6U) + (i - 6U);
        all[index]->n_unknown_fields = 1U; all[index]->unknown_fields = new_unknown();
        expected = generic(h->ast);
        CHECK(sqlparser_handle_commit_certified_insert_strings(h, &owned, &error) == SQLPARSER_STATUS_OK);
        CHECK(owned == NULL && h->ast == NULL && h->patch_batch_flags == 0U);
        CHECK(error.code == SQLPARSER_STATUS_OK && error.message[0] == '\0');
        same(&expected, &h->parse_tree);
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
        CHECK(helper_calls == before_calls + 1U && last_status == SQLPARSER_STATUS_OK && !last_handled);
#endif
        free(expected.data); sqlparser_handle_destroy(h);
    }
    free(bulk);
}

static void generic_children(void)
{
    sqlparser_handle_t *h = parse(base_sql);
    PgQuery__InsertStmt *insert = insert_of(h->ast);
    ProtobufCMessage *child[8];
    size_t i, n = 0U;
    stage = "generic relation and column fields remain lossless";
    child[n++] = &insert->relation->base;
    for (i = 0; i < insert->n_cols; ++i) {
        child[n++] = &insert->cols[i]->base;
        child[n++] = &insert->cols[i]->res_target->base;
    }
    for (i = 0; i < n; ++i) {
        const ProtobufCMessageDescriptor *original = child[i]->descriptor;
        ProtobufCMessageDescriptor copied = *original;
        child[i]->n_unknown_fields = 1U; child[i]->unknown_fields = new_unknown();
        parity(h->ast, 1);
        child[i]->descriptor = &copied; parity(h->ast, 1); child[i]->descriptor = original;
        free(child[i]->unknown_fields->data); free(child[i]->unknown_fields);
        child[i]->n_unknown_fields = 0U; child[i]->unknown_fields = NULL;
    }
    sqlparser_handle_destroy(h);
}

static void scalar_and_prefix_boundaries(void)
{
    static const int32_t locations[] = {0, 1, 127, 128, 16383, 16384, 2097151, 2097152, 268435455, 268435456, INT32_MAX};
    sqlparser_handle_t *h = parse("INSERT INTO t(a) VALUES ('x')");
    PgQuery__AConst *cell = cell_of(h->ast, 0U, 0U);
    char *original = cell->sval->sval, *text = malloc(16401U);
    size_t i, n;
    unsigned int masks[16] = {0};
    ProtobufCMessage *all[16];
    size_t count = messages(h->ast, all);
    stage = "every nested string and message length prefix";
    CHECK(text != NULL && count <= 16U);
    CHECK(h->ast->stmts[0]->stmt_len == 0);
    CHECK(insert_of(h->ast)->override == 1 && select_of(h->ast)->limit_option == 1 && select_of(h->ast)->op == 1);
    CHECK(cell->sval->location == 0);
    memset(text, 'x', 16400U); text[16400U] = '\0'; cell->sval->sval = text;
    /* The complete neighborhoods cross every enclosing message's prefix
     * transition as well as the String payload's own 127/128 and 16383/16384. */
    for (n = 0; n <= 16400U; ++n) {
        if (n > 160U && n < 16200U) continue;
        text[n] = '\0'; parity(h->ast, 1);
        for (i = 0; i < count; ++i) {
            size_t size = protobuf_c_message_get_packed_size(all[i]);
            if (size < 128U) masks[i] |= 1U; else masks[i] |= 2U;
            if (size < 16384U) masks[i] |= 4U; else masks[i] |= 8U;
        }
        text[n] = 'x';
    }
    for (i = 0; i < count; ++i) CHECK(masks[i] == 15U);
    cell->sval->sval = original; free(text);
    stage = "positive shifted locations and statement lengths";
    for (i = 0; i < sizeof(locations) / sizeof(locations[0]); ++i) {
        cell->location = locations[i]; h->ast->stmts[0]->stmt_len = locations[i];
        h->ast->version = locations[i]; parity(h->ast, 1);
    }
    sqlparser_handle_destroy(h);
    h = parse(base_sql); stage = "integer zero and all varint transitions";
    for (i = 0; i < sizeof(locations) / sizeof(locations[0]); ++i) {
        cell_of(h->ast, 0U, 0U)->ival->ival = locations[i]; parity(h->ast, 1);
    }
    CHECK(h->ast->stmts[0]->stmt_len == (int32_t)(strlen(base_sql) - 1U));
    sqlparser_handle_destroy(h);
}

/* Probe every serialized non-VALUES field of the wrapper messages, rather
 * than a short hand-picked subset that could miss a future schema addition. */
static void omitted_fields(PgQuery__ParseResult *ast, ProtobufCMessage *message)
{
    const ProtobufCMessageDescriptor *d = message->descriptor;
    PgQuery__Node *child = row_of(ast, 0U)->items[0];
    PgQuery__Node *children[1] = {child};
    unsigned int i;
    for (i = 0; i < d->n_fields; ++i) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        unsigned char *field = (unsigned char *)message + f->offset;
        unsigned char saved[sizeof(void *) > sizeof(int) ? sizeof(void *) : sizeof(int)];
        size_t width;
        if (message == &insert_of(ast)->base && (strcmp(f->name, "relation") == 0 || strcmp(f->name, "cols") == 0 || strcmp(f->name, "select_stmt") == 0)) continue;
        if (message == &select_of(ast)->base && strcmp(f->name, "values_lists") == 0) continue;
        if (f->label == PROTOBUF_C_LABEL_REPEATED) {
            size_t one = 1U, old_count;
            void *ptr = children;
            unsigned char *quantifier = (unsigned char *)message + f->quantifier_offset;
            memcpy(&old_count, quantifier, sizeof(old_count)); CHECK(old_count == 0U);
            memcpy(saved, field, sizeof(ptr)); memcpy(field, &ptr, sizeof(ptr)); memcpy(quantifier, &one, sizeof(one));
            parity(ast, 0);
            memcpy(field, saved, sizeof(ptr)); memcpy(quantifier, &old_count, sizeof(old_count));
            continue;
        }
        if (f->type == PROTOBUF_C_TYPE_MESSAGE) {
            void *ptr = child; width = sizeof(ptr); memcpy(saved, field, width); memcpy(field, &ptr, width);
        } else {
            int value; width = sizeof(value); memcpy(saved, field, width); memcpy(&value, field, width);
            value = value == 0 ? 1 : 0; memcpy(field, &value, width);
        }
        parity(ast, 0); memcpy(field, saved, width);
    }
}
#define REJECT_FIELD(object, field, value) do { \
    unsigned char saved_field[sizeof((object)->field)]; \
    memcpy(saved_field, &(object)->field, sizeof(saved_field)); (object)->field = (value); \
    parity(h->ast, 0); memcpy(&(object)->field, saved_field, sizeof(saved_field)); \
} while (0)
#define MALFORM_FIELD(object, field, value) do { \
    unsigned char saved_field[sizeof((object)->field)]; \
    memcpy(saved_field, &(object)->field, sizeof(saved_field)); (object)->field = (value); \
    reject_malformed(h->ast); memcpy(&(object)->field, saved_field, sizeof(saved_field)); \
} while (0)
static void shape_and_defaults(void)
{
    sqlparser_handle_t *h = parse(base_sql);
    PgQuery__RawStmt *raw = h->ast->stmts[0];
    PgQuery__InsertStmt *insert = insert_of(h->ast);
    PgQuery__SelectStmt *select = select_of(h->ast);
    PgQuery__List *row = row_of(h->ast, 1U);
    PgQuery__AConst *integer = cell_of(h->ast, 0U, 0U), *string = cell_of(h->ast, 1U, 1U);
    stage = "all wrapper fields and default enum guards";
    omitted_fields(h->ast, &insert->base); omitted_fields(h->ast, &select->base);
    REJECT_FIELD(h->ast, version, -1); REJECT_FIELD(raw, stmt_len, -1);
    REJECT_FIELD(raw, stmt_location, -1); REJECT_FIELD(raw, stmt_location, 1);
    REJECT_FIELD(integer, isnull, 1); REJECT_FIELD(string, isnull, 1);
    REJECT_FIELD(integer, location, -1); REJECT_FIELD(string, location, -1);
    REJECT_FIELD(integer->ival, ival, -1); REJECT_FIELD(string->sval, location, 1);
    REJECT_FIELD(string->sval, location, -1);
    REJECT_FIELD(integer, val_case, PG_QUERY__A__CONST__VAL__NOT_SET);
    REJECT_FIELD(h->ast, n_stmts, 0U); REJECT_FIELD(select, n_values_lists, 0U);
    REJECT_FIELD(insert, n_cols, 0U); REJECT_FIELD(insert, n_cols, 2U);
    REJECT_FIELD(row, n_items, 2U);
    REJECT_FIELD(row, n_items, 0U);
    stage = "null, absent, mixed and malformed wrapper shapes";
    reject_malformed(NULL);
    MALFORM_FIELD(h->ast, stmts, NULL); MALFORM_FIELD(raw, stmt, NULL);
    MALFORM_FIELD(h->ast, n_stmts, 2U);
#if SIZE_MAX > UINT32_MAX
    MALFORM_FIELD(select, n_values_lists, (size_t)UINT32_MAX + 1U);
#endif
    MALFORM_FIELD(insert, relation, NULL); MALFORM_FIELD(insert, cols, NULL);
    MALFORM_FIELD(raw->stmt, insert_stmt, NULL); MALFORM_FIELD(insert, select_stmt, NULL);
    MALFORM_FIELD(insert->select_stmt, select_stmt, NULL); MALFORM_FIELD(select, values_lists, NULL);
    MALFORM_FIELD(select->values_lists[1], list, NULL); MALFORM_FIELD(row, items, NULL);
    MALFORM_FIELD(row->items[1], a_const, NULL); MALFORM_FIELD(integer, ival, NULL); MALFORM_FIELD(string, sval, NULL);
    MALFORM_FIELD(string->sval, sval, NULL);
    MALFORM_FIELD(raw->stmt, node_case, PG_QUERY__NODE__NODE_SELECT_STMT);
    MALFORM_FIELD(insert->select_stmt, node_case, PG_QUERY__NODE__NODE_INSERT_STMT);
    MALFORM_FIELD(select->values_lists[1], node_case, PG_QUERY__NODE__NODE_A_CONST);
    MALFORM_FIELD(row->items[1], node_case, PG_QUERY__NODE__NODE_LIST);
    {
        PgQuery__Node *saved = row->items[1]; row->items[1] = NULL; reject_malformed(h->ast); row->items[1] = saved;
        saved = select->values_lists[1]; select->values_lists[1] = NULL; reject_malformed(h->ast); select->values_lists[1] = saved;
    }
    parity(h->ast, 1); sqlparser_handle_destroy(h);
}

static void mixed_generic_cells(void)
{
    static const char *const inputs[] = {
        "INSERT INTO t(a,b,c) VALUES ('x',1.5,CURRENT_TIMESTAMP)",
        "INSERT INTO t(a,b,c) VALUES ('x',-1.5,CURRENT_TIMESTAMP(6))",
        "INSERT INTO t(a,b,c) VALUES ('x',1e1000,CURRENT_TIME(0))",
        "INSERT INTO t(a,b,c) VALUES ('x',100.50,LOCALTIMESTAMP(3))",
        "INSERT INTO t(a,b,c) VALUES ('x',0.000,CURRENT_DATE)"
    };
    sqlparser_handle_t *h;
    ProtobufCMessage *children[5];
    size_t i;
    stage = "mixed Float and SQLValueFunction cells use generic encoding";
    for (i = 0U; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        h = parse(inputs[i]); parity(h->ast, 1); sqlparser_handle_destroy(h);
    }
    h = parse(inputs[0]);
    children[0] = &row_of(h->ast, 0U)->items[1]->base;
    children[1] = &cell_of(h->ast, 0U, 1U)->base;
    children[2] = &cell_of(h->ast, 0U, 1U)->fval->base;
    children[3] = &row_of(h->ast, 0U)->items[2]->base;
    children[4] = &row_of(h->ast, 0U)->items[2]->sqlvalue_function->base;
    stage = "mixed generic cell descriptor and unknown-field guards";
    for (i = 0U; i < sizeof(children) / sizeof(children[0]); i++) {
        ProtobufCMessage *m = children[i];
        const ProtobufCMessageDescriptor *original = m->descriptor;
        ProtobufCMessageDescriptor copy = *original;
        m->descriptor = &copy; parity(h->ast, 0); m->descriptor = original;
        m->n_unknown_fields = 1U; m->unknown_fields = new_unknown();
        parity(h->ast, 0);
        free(m->unknown_fields->data); free(m->unknown_fields);
        m->n_unknown_fields = 0U; m->unknown_fields = NULL;
    }
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
    {
        size_t count, at;
        stage = "mixed generic cell pack mismatch and allocation failure cleanup";
        pack_calls = pack_failures = 0U; parity(h->ast, 1); count = pack_calls;
        CHECK(count == 1U + insert_of(h->ast)->n_cols + 2U);
        for (at = 1U; at <= count; at++) {
            PgQueryProtobuf out = {0}, before = generic(h->ast), after;
            int handled = -1;
            pack_calls = pack_failures = 0U; pack_fault_at = at;
            CHECK(sqlparser_pack_certified_insert(h->ast, &out, &handled) == SQLPARSER_STATUS_INTERNAL_ERROR);
            pack_fault_at = 0U;
            CHECK(pack_failures == 1U && handled && out.data == NULL && out.len == 0U);
            assert_no_live(); after = generic(h->ast); same(&before, &after);
            free(before.data); free(after.data);
        }
        for (at = 1U; at <= 2U; at++) {
            PgQueryProtobuf out = {0};
            int handled = -1;
            sqlparser_status_t status;
            arm(at); status = sqlparser_pack_certified_insert(h->ast, &out, &handled); disarm();
            CHECK(failures == 1U && out.data == NULL && out.len == 0U);
            if (at == 1U) CHECK(status == SQLPARSER_STATUS_OK && !handled);
            else CHECK(status == SQLPARSER_STATUS_NO_MEMORY && handled);
            assert_no_live(); parity(h->ast, 1);
        }
    }
#endif
    sqlparser_handle_destroy(h);
    {
        enum { ROWS = 64 };
        const size_t capacity = 160U * ROWS + 256U;
        char *sql = malloc(capacity);
        size_t used = 0U;
        CHECK(sql != NULL);
        used += (size_t)snprintf(sql + used, capacity - used,
            "INSERT INTO TEST_LIB.TEACHER_STATISTICS(a,b,c,d,e,f,g,h,i) VALUES ");
        for (i = 0U; i < ROWS; i++) {
            int n = snprintf(sql + used, capacity - used,
                "%s('202505','T%zu','张三李四','13800138000',20,100.50,0,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)",
                i ? "," : "", i + 1001U);
            CHECK(n > 0 && (size_t)n < capacity - used); used += (size_t)n;
        }
        stage = "nine-column mixed complete native wire parity";
        h = parse(sql); parity(h->ast, 1);
        sqlparser_handle_destroy(h); free(sql);
    }
}

static void unsupported_sql(void)
{
    static const char *inputs[] = {
        "INSERT INTO t VALUES ('x')",
        "INSERT INTO t(a,b) VALUES ('x',NULL)",
        "INSERT INTO t(a,b) VALUES ('x',TRUE)",
        "INSERT INTO t(a,b) VALUES ('x',-1)",
        "INSERT INTO t(a,b) VALUES ('x',1+2)",
        "INSERT INTO t(a,b) VALUES ('x',?)",
        "INSERT INTO t(a,b) SELECT 1,2",
        "INSERT INTO t(a) VALUES ('x'); SELECT 1",
        "SELECT 1"
    };
    size_t i; stage = "generic fallback shapes";
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        sqlparser_handle_t *h = parse(inputs[i]); parity(h->ast, 0); sqlparser_handle_destroy(h);
    }
}
static char *many_sql(size_t rows, size_t columns)
{
    size_t cap = rows * columns * 40U + columns * 20U + 100U, used = 0U, r, c;
    char *sql = malloc(cap); CHECK(sql != NULL);
    used += (size_t)snprintf(sql + used, cap - used, "INSERT INTO t(");
    for (c = 0U; c < columns; ++c) used += (size_t)snprintf(sql + used, cap - used, "%sc%zu", c ? "," : "", c);
    used += (size_t)snprintf(sql + used, cap - used, ") VALUES ");
    for (r = 0U; r < rows; ++r) {
        used += (size_t)snprintf(sql + used, cap - used, "%s(", r ? "," : "");
        for (c = 0U; c < columns; ++c) {
            if (c % 2U) used += (size_t)snprintf(sql + used, cap - used, ",%zu", r * columns + c);
            else used += (size_t)snprintf(sql + used, cap - used, "%s'row%zu-col%zu-xxxxxxxxxxxxxxxx'", c ? "," : "", r, c);
        }
        used += (size_t)snprintf(sql + used, cap - used, ")");
    }
    CHECK(used < cap); return sql;
}
static void row_column_matrix(void)
{
    static const size_t rows[] = {1U, 2U, 17U, 128U};
    static const size_t columns[] = {1U, 2U, 17U};
    size_t r, c;
    stage = "one and many rows and columns";
    for (r = 0U; r < sizeof(rows) / sizeof(rows[0]); ++r) for (c = 0U; c < sizeof(columns) / sizeof(columns[0]); ++c) {
        char *sql = many_sql(rows[r], columns[c]);
        sqlparser_handle_t *h = parse(sql);
        parity(h->ast, 1); free(sql); sqlparser_handle_destroy(h);
    }
    {
        char *sql = many_sql(5000U, 2U);
        sqlparser_handle_t *h = parse(sql);
        stage = "primary 5000-row complete wire parity";
        parity(h->ast, 1); free(sql); sqlparser_handle_destroy(h);
    }
}
static void compare_pair(sqlparser_handle_t *actual, sqlparser_handle_t *reference)
{
    sqlparser_query_graph_view_t ag, rg;
    sqlparser_graph_dml_t ad, rd;
    sqlparser_bind_occurrence_view_t ab, rb;
    char *a = NULL, *b = NULL;
    if (actual->parse_tree.len != reference->parse_tree.len) fprintf(stderr, "wire lengths actual=%zu reference=%zu SQL-length=%zu\n", actual->parse_tree.len, reference->parse_tree.len, strlen(sqlparser_original_sql(actual)));
    same(&actual->parse_tree, &reference->parse_tree);
    CHECK(sqlparser_deparse(actual, &a, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(reference, &b, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(a, b) == 0); sqlparser_string_free(a); sqlparser_string_free(b);
    CHECK(sqlparser_export_view_json(actual, 0U, &a, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_export_view_json(reference, 0U, &b, &error) == SQLPARSER_STATUS_OK);
    CHECK(strcmp(a, b) == 0); sqlparser_string_free(a); sqlparser_string_free(b);
    CHECK(sqlparser_statement_query_graph(actual, 0U, &ag, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_statement_query_graph(reference, 0U, &rg, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(&ag, &ad, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(&rg, &rd, &error) == SQLPARSER_STATUS_OK);
    CHECK(ad.rows.count == rd.rows.count && ad.target_columns.count == rd.target_columns.count);
    CHECK(sqlparser_handle_bind_occurrences(actual, &ab, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_bind_occurrences(reference, &rb, &error) == SQLPARSER_STATUS_OK);
    CHECK(ab.count == 0U && rb.count == 0U);
}
static void compare_handle(sqlparser_handle_t *actual)
{
    sqlparser_handle_t *reference = parse(sqlparser_original_sql(actual));
    compare_pair(actual, reference); sqlparser_handle_destroy(reference);
}
static sqlparser_status_t apply_strings(sqlparser_handle_t *h, const char *text)
{
    sqlparser_literal_value_t values[2] = {{0}};
    sqlparser_patch_t patches[2] = {{0}};
    sqlparser_patch_list_t list = {patches, 2U};
    size_t i;
    for (i = 0; i < 2U; ++i) {
        values[i].kind = SQLPARSER_LITERAL_KIND_STRING; values[i].string_value = text;
        patches[i].op = SQLPARSER_PATCH_REPLACE; patches[i].literal = &values[i];
        patches[i].selector = i ? "stmt[0].insert_cell[1][1]" : "stmt[0].insert_cell[0][1]";
    }
    return sqlparser_apply_patch(h, &list, &error);
}
static void small_commit_fallback(void)
{
    sqlparser_handle_t *h = parse(base_sql);
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
    size_t before_calls = helper_calls;
#endif
    stage = "small certified commits retain generic packing";
    CHECK(apply_strings(h, "small-change") == SQLPARSER_STATUS_OK);
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
    CHECK(helper_calls == before_calls);
#endif
    compare_handle(h); sqlparser_handle_destroy(h);
}
static void repeated_apply(void)
{
    static const size_t lengths[] = {0U, 127U, 128U, 16383U, 16384U, 1U};
    char *bulk = bulk_sql();
    sqlparser_handle_t *h = parse(bulk);
    free(bulk);
    size_t round;
    stage = "repeated apply graph SQL bind deparse and later mutations";
    for (round = 0; round < sizeof(lengths) / sizeof(lengths[0]); ++round) {
        char *text = malloc(lengths[round] + 1U);
        sqlparser_query_graph_view_t stale;
        sqlparser_graph_dml_t dml;
        unsigned long generation = h->generation;
        CHECK(text != NULL); memset(text, 'q', lengths[round]); text[lengths[round]] = '\0';
        CHECK(sqlparser_statement_query_graph(h, 0U, &stale, &error) == SQLPARSER_STATUS_OK);
        CHECK(apply_strings(h, text) == SQLPARSER_STATUS_OK);
        memset(text, 0xa7, lengths[round]); free(text); pg_query_exit();
        CHECK(h->generation == generation + 1UL && h->ast == NULL && h->patch_batch_flags == 0U);
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
        CHECK(last_status == SQLPARSER_STATUS_OK && last_handled);
#endif
        CHECK(sqlparser_query_graph_dml(&stale, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        compare_handle(h);
    }
    {
        /* Generic non-string edits may retain locations from their input
         * surface, so use the same independent pre-edit parse/mutation oracle
         * as the differential harness instead of reparsing its final SQL. */
        sqlparser_handle_t *reference = parse(sqlparser_original_sql(h));
        sqlparser_patch_t patch = {0};
        sqlparser_patch_list_t list = {&patch, 1U};
        stage = "following integer mutation";
        patch.op = SQLPARSER_PATCH_REPLACE; patch.selector = "stmt[0].insert_cell[0][0]"; patch.sql = "42";
        CHECK(sqlparser_apply_patch(h, &list, &error) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_apply_patch(reference, &list, &error) == SQLPARSER_STATUS_OK);
        compare_pair(h, reference); sqlparser_handle_destroy(reference);
    }
    sqlparser_handle_destroy(h);
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
    assert_no_live();
#endif
}
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
static void pack_length_failures(void)
{
    char *bulk = bulk_sql();
    sqlparser_handle_t *h = parse(base_sql);
    size_t count, at;
    stage = "generic child pack-length mismatch cleanup";
    pack_calls = pack_failures = 0U; parity(h->ast, 1); count = pack_calls;
    CHECK(count == 1U + insert_of(h->ast)->n_cols);
    for (at = 1U; at <= count; ++at) {
        PgQueryProtobuf out = {0}, before = generic(h->ast), after;
        sqlparser_status_t status;
        int handled = -1;
        pack_calls = pack_failures = 0U; pack_fault_at = at;
        status = sqlparser_pack_certified_insert(h->ast, &out, &handled); pack_fault_at = 0U;
        CHECK(pack_failures == 1U && status == SQLPARSER_STATUS_INTERNAL_ERROR && handled);
        CHECK(out.data == NULL && out.len == 0U); assert_no_live();
        after = generic(h->ast); same(&before, &after); free(before.data); free(after.data);
    }
    sqlparser_handle_destroy(h);
    for (at = 1U; at <= count; ++at) {
        sqlparser_status_t status;
        sqlparser_query_graph_view_t stale;
        sqlparser_graph_dml_t dml;
        h = parse(bulk);
        CHECK(sqlparser_statement_query_graph(h, 0U, &stale, &error) == SQLPARSER_STATUS_OK);
        pack_calls = pack_failures = 0U; pack_fault_at = at;
        status = apply_strings(h, "faulted-child-pack"); pack_fault_at = 0U;
        CHECK(pack_failures == 1U && status == SQLPARSER_STATUS_INTERNAL_ERROR && last_handled);
        CHECK(error.code == SQLPARSER_STATUS_INTERNAL_ERROR && strcmp(error.message, "failed to repack parse tree protobuf") == 0);
        CHECK(sqlparser_query_graph_dml(&stale, &dml, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(sqlparser_test_failed_handle(h)); sqlparser_handle_destroy(h); assert_no_live();
        h = parse(bulk); CHECK(apply_strings(h, "next-handle") == SQLPARSER_STATUS_OK);
        compare_handle(h); sqlparser_handle_destroy(h); assert_no_live();
    }
    free(bulk);
}
static void allocation_failures(void)
{
    char *bulk = bulk_sql();
    sqlparser_handle_t *h = parse(base_sql);
    size_t count, at;
    stage = "direct helper scratch and output OOM";
    arm(0U); parity(h->ast, 1); disarm(); count = allocation_calls;
    CHECK(count == 2U);
    for (at = 1U; at <= count; ++at) {
        PgQueryProtobuf out = {0}, before = generic(h->ast), after;
        int handled = -1;
        sqlparser_status_t status;
        arm(at); status = sqlparser_pack_certified_insert(h->ast, &out, &handled); disarm();
        CHECK(failures == 1U && out.data == NULL && out.len == 0U);
        if (at == 1U) CHECK(status == SQLPARSER_STATUS_OK && !handled);
        else CHECK(status == SQLPARSER_STATUS_NO_MEMORY && handled);
        assert_no_live(); after = generic(h->ast); same(&before, &after);
        free(before.data); free(after.data); parity(h->ast, 1);
    }
    sqlparser_handle_destroy(h);
    stage = "commit scratch fallback and terminal output OOM destroy";
    for (at = 1U; at <= count; ++at) {
        sqlparser_status_t status;
        sqlparser_query_graph_view_t stale;
        sqlparser_graph_dml_t dml;
        h = parse(bulk);
        CHECK(sqlparser_statement_query_graph(h, 0U, &stale, &error) == SQLPARSER_STATUS_OK);
        arm(at); status = apply_strings(h, "changed-longer"); disarm();
        CHECK(failures == 1U && sqlparser_query_graph_dml(&stale, &dml, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        if (at == 1U) {
            CHECK(status == SQLPARSER_STATUS_OK && !last_handled);
            CHECK(error.code == SQLPARSER_STATUS_OK && error.message[0] == '\0');
            compare_handle(h);
            CHECK(apply_strings(h, "fresh-success") == SQLPARSER_STATUS_OK); compare_handle(h);
        } else {
            CHECK(status == SQLPARSER_STATUS_NO_MEMORY && error.code == SQLPARSER_STATUS_NO_MEMORY);
            CHECK(last_handled && sqlparser_test_failed_handle(h));
        }
        sqlparser_handle_destroy(h); assert_no_live();
        /* Failure is terminal only for that handle; no scratch/output survives
         * destroy and a new handle must immediately support another commit. */
        h = parse(bulk); CHECK(apply_strings(h, "retry") == SQLPARSER_STATUS_OK);
        compare_handle(h); sqlparser_handle_destroy(h); assert_no_live();
    }
    free(bulk);
}
#endif
int main(void)
{
    unknown_and_descriptor_guards(); generic_children(); scalar_and_prefix_boundaries();
    shape_and_defaults(); mixed_generic_cells(); unsupported_sql(); row_column_matrix(); small_commit_fallback(); repeated_apply();
#ifdef SQLPARSER_CERTIFIED_PACK_WRAPPERS
    allocation_failures(); pack_length_failures(); assert_no_live(); CHECK(helper_calls > 0U && guarded_outputs > 0U);
    printf("certified INSERT pack parity: %zu cases, guarded allocations, scratch/output OOM, pack mismatch and fallback preservation passed\n", cases);
#else
    printf("certified INSERT pack parity: %zu cases passed (GNU allocation guards/injection unavailable)\n", cases);
#endif
    pg_query_exit(); return 0;
}
