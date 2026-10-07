/* A deliberately narrow, private protobuf writer for the certified INSERT
 * commit. Only the large VALUES subtree and its ancestors are specialized.
 * The generic writer remains authoritative for unsupported trees and schemas.
 * No AST storage is borrowed by the result or retained after this call. */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_internal.h"

#ifndef SQLPARSER_DISABLE_CERTIFIED_INSERT_PACKER

enum {
    CP_PARSE_VERSION = 1, CP_PARSE_STMTS = 2,
    CP_RAW_STMT = 1, CP_RAW_LOCATION = 2, CP_RAW_LENGTH = 3,
    CP_INSERT_RELATION = 1, CP_INSERT_COLS = 2, CP_INSERT_SELECT = 3,
    CP_INSERT_OVERRIDE = 7,
    CP_SELECT_VALUES = 10, CP_SELECT_LIMIT = 14, CP_SELECT_OP = 17,
    CP_LIST_ITEMS = 1, CP_CONST_LOCATION = 11,
    CP_INTEGER_VALUE = 1, CP_STRING_VALUE = 1
};

typedef struct {
    unsigned id;
    ProtobufCLabel label;
    ProtobufCType type;
    size_t quantifier_offset, offset;
    const void *descriptor, *default_value;
    unsigned flags;
} cp_field;

#define CP_SCALAR(t, f, id, kind, desc) \
    {id, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_##kind, 0, \
     offsetof(t, f), desc, NULL, 0}
#define CP_MESSAGE(t, f, id, desc) CP_SCALAR(t, f, id, MESSAGE, desc)
#define CP_REPEATED(t, f, id, desc) \
    {id, PROTOBUF_C_LABEL_REPEATED, PROTOBUF_C_TYPE_MESSAGE, \
     offsetof(t, n_##f), offsetof(t, f), desc, NULL, 0}
#define CP_ONEOF(t, f, id, q, desc) \
    {id, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_MESSAGE, \
     offsetof(t, q), offsetof(t, f), desc, NULL, PROTOBUF_C_FIELD_FLAG_ONEOF}

/* These are wire/layout guards, not a second schema. A generated-schema
 * change must either match every assumption below or use generic packing. */
static const cp_field cp_parse_fields[] = {
    CP_SCALAR(PgQuery__ParseResult, version, CP_PARSE_VERSION, INT32, NULL),
    CP_REPEATED(PgQuery__ParseResult, stmts, CP_PARSE_STMTS, &pg_query__raw_stmt__descriptor)
};
static const cp_field cp_raw_fields[] = {
    CP_MESSAGE(PgQuery__RawStmt, stmt, CP_RAW_STMT, &pg_query__node__descriptor),
    CP_SCALAR(PgQuery__RawStmt, stmt_location, CP_RAW_LOCATION, INT32, NULL),
    CP_SCALAR(PgQuery__RawStmt, stmt_len, CP_RAW_LENGTH, INT32, NULL)
};
static const cp_field cp_insert_fields[] = {
    CP_MESSAGE(PgQuery__InsertStmt, relation, CP_INSERT_RELATION, &pg_query__range_var__descriptor),
    CP_REPEATED(PgQuery__InsertStmt, cols, CP_INSERT_COLS, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__InsertStmt, select_stmt, CP_INSERT_SELECT, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__InsertStmt, on_conflict_clause, 4, &pg_query__on_conflict_clause__descriptor),
    CP_REPEATED(PgQuery__InsertStmt, returning_list, 5, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__InsertStmt, with_clause, 6, &pg_query__with_clause__descriptor),
    CP_SCALAR(PgQuery__InsertStmt, override, CP_INSERT_OVERRIDE, ENUM, &pg_query__overriding_kind__descriptor)
};
static const cp_field cp_select_fields[] = {
    CP_REPEATED(PgQuery__SelectStmt, distinct_clause, 1, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__SelectStmt, into_clause, 2, &pg_query__into_clause__descriptor),
    CP_REPEATED(PgQuery__SelectStmt, target_list, 3, &pg_query__node__descriptor),
    CP_REPEATED(PgQuery__SelectStmt, from_clause, 4, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__SelectStmt, where_clause, 5, &pg_query__node__descriptor),
    CP_REPEATED(PgQuery__SelectStmt, group_clause, 6, &pg_query__node__descriptor),
    CP_SCALAR(PgQuery__SelectStmt, group_distinct, 7, BOOL, NULL),
    CP_MESSAGE(PgQuery__SelectStmt, having_clause, 8, &pg_query__node__descriptor),
    CP_REPEATED(PgQuery__SelectStmt, window_clause, 9, &pg_query__node__descriptor),
    CP_REPEATED(PgQuery__SelectStmt, values_lists, CP_SELECT_VALUES, &pg_query__node__descriptor),
    CP_REPEATED(PgQuery__SelectStmt, sort_clause, 11, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__SelectStmt, limit_offset, 12, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__SelectStmt, limit_count, 13, &pg_query__node__descriptor),
    CP_SCALAR(PgQuery__SelectStmt, limit_option, CP_SELECT_LIMIT, ENUM, &pg_query__limit_option__descriptor),
    CP_REPEATED(PgQuery__SelectStmt, locking_clause, 15, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__SelectStmt, with_clause, 16, &pg_query__with_clause__descriptor),
    CP_SCALAR(PgQuery__SelectStmt, op, CP_SELECT_OP, ENUM, &pg_query__set_operation__descriptor),
    CP_SCALAR(PgQuery__SelectStmt, all, 18, BOOL, NULL),
    CP_MESSAGE(PgQuery__SelectStmt, larg, 19, &pg_query__select_stmt__descriptor),
    CP_MESSAGE(PgQuery__SelectStmt, rarg, 20, &pg_query__select_stmt__descriptor),
    CP_MESSAGE(PgQuery__SelectStmt, start_with_clause, 21, &pg_query__node__descriptor),
    CP_MESSAGE(PgQuery__SelectStmt, connect_by_clause, 22, &pg_query__node__descriptor),
    CP_SCALAR(PgQuery__SelectStmt, connect_by_no_cycle, 23, BOOL, NULL),
    CP_SCALAR(PgQuery__SelectStmt, connect_by_first, 24, BOOL, NULL),
    CP_SCALAR(PgQuery__SelectStmt, limit_clause_style, 25, ENUM, &pg_query__limit_clause_style__descriptor)
};
static const cp_field cp_list_fields[] = {
    CP_REPEATED(PgQuery__List, items, CP_LIST_ITEMS, &pg_query__node__descriptor)
};
static const cp_field cp_const_fields[] = {
    CP_ONEOF(PgQuery__AConst, ival, PG_QUERY__A__CONST__VAL_IVAL, val_case, &pg_query__integer__descriptor),
    CP_ONEOF(PgQuery__AConst, fval, PG_QUERY__A__CONST__VAL_FVAL, val_case, &pg_query__float__descriptor),
    CP_ONEOF(PgQuery__AConst, boolval, PG_QUERY__A__CONST__VAL_BOOLVAL, val_case, &pg_query__boolean__descriptor),
    CP_ONEOF(PgQuery__AConst, sval, PG_QUERY__A__CONST__VAL_SVAL, val_case, &pg_query__string__descriptor),
    CP_ONEOF(PgQuery__AConst, bsval, PG_QUERY__A__CONST__VAL_BSVAL, val_case, &pg_query__bit_string__descriptor),
    CP_SCALAR(PgQuery__AConst, isnull, 10, BOOL, NULL),
    CP_SCALAR(PgQuery__AConst, location, CP_CONST_LOCATION, INT32, NULL)
};
static const cp_field cp_integer_fields[] = {
    CP_SCALAR(PgQuery__Integer, ival, CP_INTEGER_VALUE, INT32, NULL)
};
static const cp_field cp_string_fields[] = {
    {CP_STRING_VALUE, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_STRING,
     0, offsetof(PgQuery__String, sval), NULL, &protobuf_c_empty_string, 0},
    CP_SCALAR(PgQuery__String, location, 2, INT32, NULL)
};
static const cp_field cp_node_fields[] = {
    CP_ONEOF(PgQuery__Node, insert_stmt, PG_QUERY__NODE__NODE_INSERT_STMT, node_case, &pg_query__insert_stmt__descriptor),
    CP_ONEOF(PgQuery__Node, select_stmt, PG_QUERY__NODE__NODE_SELECT_STMT, node_case, &pg_query__select_stmt__descriptor),
    CP_ONEOF(PgQuery__Node, list, PG_QUERY__NODE__NODE_LIST, node_case, &pg_query__list__descriptor),
    CP_ONEOF(PgQuery__Node, a_const, PG_QUERY__NODE__NODE_A_CONST, node_case, &pg_query__a__const__descriptor)
};
#undef CP_SCALAR
#undef CP_MESSAGE
#undef CP_REPEATED
#undef CP_ONEOF

static int cp_field_matches(const ProtobufCFieldDescriptor *f, const cp_field *e)
{
    return f->id == e->id && f->label == e->label && f->type == e->type &&
        f->quantifier_offset == e->quantifier_offset && f->offset == e->offset &&
        f->descriptor == e->descriptor && f->default_value == e->default_value &&
        f->flags == e->flags;
}

static int cp_schema(const ProtobufCMessageDescriptor *d, size_t size,
    const cp_field *fields, size_t count)
{
    size_t i;
    if (d->magic != PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC ||
        d->sizeof_message != size || d->n_fields != count) return 0;
    for (i = 0U; i < count; i++) {
        if (!cp_field_matches(&d->fields[i], &fields[i])) return 0;
    }
    return 1;
}

static int cp_schemas_match(void)
{
    size_t i;
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    if (PG_QUERY__OVERRIDING_KIND__OVERRIDING_NOT_SET != 1 ||
        PG_QUERY__LIMIT_OPTION__LIMIT_OPTION_DEFAULT != 1 ||
        PG_QUERY__SET_OPERATION__SETOP_NONE != 1 ||
        PG_QUERY__LIMIT_CLAUSE_STYLE__LIMIT_CLAUSE_STYLE_DEFAULT != 0) return 0;
#define CP_SCHEMA(name, type, fields) \
    cp_schema(&pg_query__##name##__descriptor, sizeof(type), fields, sizeof(fields) / sizeof(fields[0]))
    if (!CP_SCHEMA(parse_result, PgQuery__ParseResult, cp_parse_fields) ||
        !CP_SCHEMA(raw_stmt, PgQuery__RawStmt, cp_raw_fields) ||
        !CP_SCHEMA(insert_stmt, PgQuery__InsertStmt, cp_insert_fields) ||
        !CP_SCHEMA(select_stmt, PgQuery__SelectStmt, cp_select_fields) ||
        !CP_SCHEMA(list, PgQuery__List, cp_list_fields) ||
        !CP_SCHEMA(a__const, PgQuery__AConst, cp_const_fields) ||
        !CP_SCHEMA(integer, PgQuery__Integer, cp_integer_fields) ||
        !CP_SCHEMA(string, PgQuery__String, cp_string_fields)) return 0;
#undef CP_SCHEMA
    if (d->magic != PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC ||
        d->sizeof_message != sizeof(PgQuery__Node) || d->n_fields != 268U) return 0;
    /* Every inactive Node arm must remain an inactive oneof. */
    for (i = 0U; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        if (f->id != i + 1U || f->label != PROTOBUF_C_LABEL_NONE ||
            f->type != PROTOBUF_C_TYPE_MESSAGE || f->flags != PROTOBUF_C_FIELD_FLAG_ONEOF ||
            f->quantifier_offset != offsetof(PgQuery__Node, node_case) ||
            f->offset != offsetof(PgQuery__Node, a_const) || f->default_value != NULL) return 0;
    }
    for (i = 0U; i < sizeof(cp_node_fields) / sizeof(cp_node_fields[0]); i++) {
        if (cp_node_fields[i].id == 0U || cp_node_fields[i].id > d->n_fields ||
            !cp_field_matches(&d->fields[cp_node_fields[i].id - 1U], &cp_node_fields[i])) return 0;
    }
    return 1;
}

static int cp_message(const ProtobufCMessage *m, const ProtobufCMessageDescriptor *d)
{
    return m != NULL && m->descriptor == d && m->n_unknown_fields == 0U;
}
#define CP_IS(p, name) cp_message((const ProtobufCMessage *)(p), &pg_query__##name##__descriptor)

static unsigned cp_varint_size(uint32_t value)
{
    if (value < (1U << 7)) return 1U;
    if (value < (1U << 14)) return 2U;
    if (value < (1U << 21)) return 3U;
    if (value < (1U << 28)) return 4U;
    return 5U;
}

/* All size arithmetic is widened before addition, and the whole supported
 * wire is bounded by UINT32_MAX. Oversize is a generic fallback, not a new
 * public resource-limit error. */
static uint64_t cp_envelope(unsigned tag, uint32_t payload)
{
    return (uint64_t)cp_varint_size((tag << 3) | 2U) + cp_varint_size(payload) + payload;
}
static unsigned cp_scalar_size(unsigned tag, uint32_t value)
{
    return value == 0U ? 0U : cp_varint_size(tag << 3) + cp_varint_size(value);
}

static int cp_values_defaults(const PgQuery__SelectStmt *s)
{
    return s->n_distinct_clause == 0U && s->distinct_clause == NULL &&
        s->into_clause == NULL && s->n_target_list == 0U && s->target_list == NULL &&
        s->n_from_clause == 0U && s->from_clause == NULL && s->where_clause == NULL &&
        s->start_with_clause == NULL && s->connect_by_clause == NULL &&
        !s->connect_by_no_cycle && !s->connect_by_first &&
        s->n_group_clause == 0U && s->group_clause == NULL && !s->group_distinct &&
        s->having_clause == NULL && s->n_window_clause == 0U && s->window_clause == NULL &&
        s->n_sort_clause == 0U && s->sort_clause == NULL && s->limit_offset == NULL &&
        s->limit_count == NULL && s->limit_option == PG_QUERY__LIMIT_OPTION__LIMIT_OPTION_DEFAULT &&
        s->n_locking_clause == 0U && s->locking_clause == NULL && s->with_clause == NULL &&
        s->op == PG_QUERY__SET_OPERATION__SETOP_NONE && !s->all && s->larg == NULL && s->rarg == NULL &&
        s->limit_clause_style == PG_QUERY__LIMIT_CLAUSE_STYLE__LIMIT_CLAUSE_STYLE_DEFAULT;
}

typedef struct {
    uint32_t value, constant, node;
    size_t string_length;
    int generic;
} cp_cell_sizes;

static int cp_measure_cell(const PgQuery__Node *node, cp_cell_sizes *sizes)
{
    const PgQuery__AConst *c;
    uint64_t n;
    sizes->string_length = 0U;
    sizes->generic = 0;
    /* These scalar-only mixed cells need no new wire encoder. Retain the
     * exact-descriptor/unknown guards, then delegate their entire Node to the
     * ordinary writer. Only the enclosing row lengths use the private cache. */
    if (CP_IS(node, node) &&
        ((node->node_case == PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION &&
          CP_IS(node->sqlvalue_function, sqlvalue_function)) ||
         (node->node_case == PG_QUERY__NODE__NODE_A_CONST &&
          CP_IS(node->a_const, a__const) && !node->a_const->isnull &&
          node->a_const->location >= 0 &&
          node->a_const->val_case == PG_QUERY__A__CONST__VAL_FVAL &&
          CP_IS(node->a_const->fval, float) && node->a_const->fval->fval != NULL))) {
        size_t generic_size = protobuf_c_message_get_packed_size(&node->base);
        if (generic_size > UINT32_MAX) return 0;
        sizes->node = (uint32_t)generic_size;
        sizes->generic = 1;
        return 1;
    }
    if (!CP_IS(node, node) || node->node_case != PG_QUERY__NODE__NODE_A_CONST ||
        !CP_IS(node->a_const, a__const)) return 0;
    c = node->a_const;
    if (c->isnull || c->location < 0) return 0;
    if (c->val_case == PG_QUERY__A__CONST__VAL_IVAL) {
        if (!CP_IS(c->ival, integer) || c->ival->ival < 0) return 0;
        n = cp_scalar_size(CP_INTEGER_VALUE, (uint32_t)c->ival->ival);
    } else if (c->val_case == PG_QUERY__A__CONST__VAL_SVAL) {
        if (!CP_IS(c->sval, string) || c->sval->location != 0 || c->sval->sval == NULL) return 0;
        sizes->string_length = strlen(c->sval->sval);
        if (sizes->string_length > UINT32_MAX) return 0;
        n = sizes->string_length == 0U ? 0U : cp_envelope(CP_STRING_VALUE, (uint32_t)sizes->string_length);
    } else return 0;
    if (n > UINT32_MAX) return 0;
    sizes->value = (uint32_t)n;
    n = cp_envelope((unsigned)c->val_case, sizes->value) +
        cp_scalar_size(CP_CONST_LOCATION, (uint32_t)c->location);
    if (n > UINT32_MAX) return 0;
    sizes->constant = (uint32_t)n;
    n = cp_envelope(PG_QUERY__NODE__NODE_A_CONST, sizes->constant);
    if (n > UINT32_MAX) return 0;
    sizes->node = (uint32_t)n;
    return 1;
}

typedef struct {
    uint8_t *next, *end;
    int failed;
} cp_writer;

static void cp_write_varint(cp_writer *w, uint32_t value)
{
    do {
        if (w->next == w->end) { w->failed = 1; return; }
        *w->next++ = (uint8_t)((value & 0x7fU) | (value >= 0x80U ? 0x80U : 0U));
        value >>= 7;
    } while (value != 0U);
}
static void cp_write_header(cp_writer *w, unsigned tag, uint32_t payload)
{
    cp_write_varint(w, (tag << 3) | 2U);
    cp_write_varint(w, payload);
}
static void cp_write_scalar(cp_writer *w, unsigned tag, uint32_t value)
{
    if (value != 0U) {
        cp_write_varint(w, tag << 3);
        cp_write_varint(w, value);
    }
}
static void cp_write_generic(cp_writer *w, unsigned tag, const ProtobufCMessage *message)
{
    size_t size = protobuf_c_message_get_packed_size(message), packed;
    if (size > UINT32_MAX) { w->failed = 1; return; }
    cp_write_header(w, tag, (uint32_t)size);
    if (size > (size_t)(w->end - w->next)) { w->failed = 1; return; }
    packed = protobuf_c_message_pack(message, w->next);
    if (packed != size) { w->failed = 1; return; }
    w->next += packed;
}

static void cp_write_cell(cp_writer *w, const PgQuery__Node *node)
{
    const PgQuery__AConst *c;
    cp_cell_sizes sizes;
    /* A second local measurement avoids a per-cell cache. The enclosing row
     * and ancestor lengths were all checked before output allocation. */
    if (!cp_measure_cell(node, &sizes)) { w->failed = 1; return; }
    if (sizes.generic) {
        size_t packed;
        cp_write_header(w, CP_LIST_ITEMS, sizes.node);
        if (sizes.node > (size_t)(w->end - w->next)) { w->failed = 1; return; }
        packed = protobuf_c_message_pack(&node->base, w->next);
        if (packed != sizes.node) { w->failed = 1; return; }
        w->next += packed;
        return;
    }
    c = node->a_const;
    cp_write_header(w, CP_LIST_ITEMS, sizes.node);
    cp_write_header(w, PG_QUERY__NODE__NODE_A_CONST, sizes.constant);
    cp_write_header(w, (unsigned)c->val_case, sizes.value);
    if (c->val_case == PG_QUERY__A__CONST__VAL_IVAL) {
        cp_write_scalar(w, CP_INTEGER_VALUE, (uint32_t)c->ival->ival);
    } else if (sizes.string_length != 0U) {
        cp_write_header(w, CP_STRING_VALUE, (uint32_t)sizes.string_length);
        if (sizes.string_length > (size_t)(w->end - w->next)) { w->failed = 1; return; }
        memcpy(w->next, c->sval->sval, sizes.string_length);
        w->next += sizes.string_length;
    }
    cp_write_scalar(w, CP_CONST_LOCATION, (uint32_t)c->location);
}
#endif

sqlparser_status_t sqlparser_pack_certified_insert(const PgQuery__ParseResult *ast,
    PgQueryProtobuf *out, int *out_handled)
{
#ifndef SQLPARSER_DISABLE_CERTIFIED_INSERT_PACKER
    const PgQuery__RawStmt *raw;
    const PgQuery__InsertStmt *insert;
    const PgQuery__SelectStmt *select;
    uint32_t *row_sizes = NULL;
    uint32_t select_size, select_node_size, insert_size, insert_node_size, raw_size, total_size;
    uint64_t n, prefix_size = 0U;
    size_t row, column, generic_size;
    cp_writer writer;
#endif
    if (out == NULL || out_handled == NULL) return SQLPARSER_STATUS_INVALID_ARGUMENT;
    out->data = NULL;
    out->len = 0U;
    *out_handled = 0;
#ifdef SQLPARSER_DISABLE_CERTIFIED_INSERT_PACKER
    (void)ast;
    return SQLPARSER_STATUS_OK;
#else
    if (!cp_schemas_match() || !CP_IS(ast, parse_result) || ast->version < 0 ||
        ast->n_stmts != 1U || ast->stmts == NULL ||
        !CP_IS(ast->stmts[0], raw_stmt)) return SQLPARSER_STATUS_OK;
    raw = ast->stmts[0];
    if (raw->stmt_location != 0 || raw->stmt_len < 0 || !CP_IS(raw->stmt, node) ||
        raw->stmt->node_case != PG_QUERY__NODE__NODE_INSERT_STMT ||
        !CP_IS(raw->stmt->insert_stmt, insert_stmt)) return SQLPARSER_STATUS_OK;
    insert = raw->stmt->insert_stmt;
    if (insert->relation == NULL || insert->n_cols == 0U || insert->cols == NULL ||
        insert->on_conflict_clause != NULL || insert->n_returning_list != 0U ||
        insert->returning_list != NULL || insert->with_clause != NULL ||
        insert->override != PG_QUERY__OVERRIDING_KIND__OVERRIDING_NOT_SET ||
        !CP_IS(insert->select_stmt, node) ||
        insert->select_stmt->node_case != PG_QUERY__NODE__NODE_SELECT_STMT ||
        !CP_IS(insert->select_stmt->select_stmt, select_stmt)) return SQLPARSER_STATUS_OK;
    select = insert->select_stmt->select_stmt;
    if (!cp_values_defaults(select) || select->n_values_lists == 0U || select->values_lists == NULL ||
        select->n_values_lists > SIZE_MAX / sizeof(*row_sizes) ||
        select->n_values_lists > UINT32_MAX) return SQLPARSER_STATUS_OK;

    generic_size = protobuf_c_message_get_packed_size(&insert->relation->base);
    if (generic_size > UINT32_MAX) return SQLPARSER_STATUS_OK;
    prefix_size = cp_envelope(CP_INSERT_RELATION, (uint32_t)generic_size);
    for (column = 0U; column < insert->n_cols; column++) {
        if (insert->cols[column] == NULL) return SQLPARSER_STATUS_OK;
        generic_size = protobuf_c_message_get_packed_size(&insert->cols[column]->base);
        if (generic_size > UINT32_MAX) return SQLPARSER_STATUS_OK;
        prefix_size += cp_envelope(CP_INSERT_COLS, (uint32_t)generic_size);
        if (prefix_size > UINT32_MAX) return SQLPARSER_STATUS_OK;
    }
    /* Scratch OOM is safe to decline: generic packing needs no such cache. */
    row_sizes = (uint32_t *)malloc(select->n_values_lists * sizeof(*row_sizes));
    if (row_sizes == NULL) return SQLPARSER_STATUS_OK;
    n = 0U;
    for (row = 0U; row < select->n_values_lists; row++) {
        const PgQuery__Node *row_node = select->values_lists[row];
        const PgQuery__List *cells;
        uint64_t row_size = 0U, row_node_size;
        if (!CP_IS(row_node, node) || row_node->node_case != PG_QUERY__NODE__NODE_LIST ||
            !CP_IS(row_node->list, list)) goto fallback;
        cells = row_node->list;
        if (cells->n_items == 0U || cells->items == NULL || cells->n_items != insert->n_cols) goto fallback;
        for (column = 0U; column < cells->n_items; column++) {
            cp_cell_sizes sizes;
            if (!cp_measure_cell(cells->items[column], &sizes)) goto fallback;
            row_size += cp_envelope(CP_LIST_ITEMS, sizes.node);
            if (row_size > UINT32_MAX) goto fallback;
        }
        row_sizes[row] = (uint32_t)row_size;
        row_node_size = cp_envelope(PG_QUERY__NODE__NODE_LIST, (uint32_t)row_size);
        if (row_node_size > UINT32_MAX) goto fallback;
        n += cp_envelope(CP_SELECT_VALUES, (uint32_t)row_node_size);
        if (n > UINT32_MAX) goto fallback;
    }
    n += cp_scalar_size(CP_SELECT_LIMIT, (uint32_t)select->limit_option) +
        cp_scalar_size(CP_SELECT_OP, (uint32_t)select->op);
    if (n > UINT32_MAX) goto fallback;
    select_size = (uint32_t)n;
    n = cp_envelope(PG_QUERY__NODE__NODE_SELECT_STMT, select_size);
    if (n > UINT32_MAX) goto fallback;
    select_node_size = (uint32_t)n;
    n = prefix_size + cp_envelope(CP_INSERT_SELECT, select_node_size) +
        cp_scalar_size(CP_INSERT_OVERRIDE, (uint32_t)insert->override);
    if (n > UINT32_MAX) goto fallback;
    insert_size = (uint32_t)n;
    n = cp_envelope(PG_QUERY__NODE__NODE_INSERT_STMT, insert_size);
    if (n > UINT32_MAX) goto fallback;
    insert_node_size = (uint32_t)n;
    n = cp_envelope(CP_RAW_STMT, insert_node_size) + cp_scalar_size(CP_RAW_LENGTH, (uint32_t)raw->stmt_len);
    if (n > UINT32_MAX) goto fallback;
    raw_size = (uint32_t)n;
    n = cp_scalar_size(CP_PARSE_VERSION, (uint32_t)ast->version) + cp_envelope(CP_PARSE_STMTS, raw_size);
    /* Writer bounds use pointer differences; on 32-bit targets an otherwise
     * representable unsigned wire length can exceed PTRDIFF_MAX. */
    if (n > UINT32_MAX || n > SIZE_MAX || n > PTRDIFF_MAX) goto fallback;
    total_size = (uint32_t)n;

    *out_handled = 1;
    out->data = (char *)malloc(total_size);
    if (out->data == NULL) {
        free(row_sizes);
        return SQLPARSER_STATUS_NO_MEMORY;
    }
    writer.next = (uint8_t *)out->data;
    writer.end = writer.next + total_size;
    writer.failed = 0;
    cp_write_scalar(&writer, CP_PARSE_VERSION, (uint32_t)ast->version);
    cp_write_header(&writer, CP_PARSE_STMTS, raw_size);
    cp_write_header(&writer, CP_RAW_STMT, insert_node_size);
    cp_write_header(&writer, PG_QUERY__NODE__NODE_INSERT_STMT, insert_size);
    cp_write_generic(&writer, CP_INSERT_RELATION, &insert->relation->base);
    for (column = 0U; column < insert->n_cols; column++) {
        cp_write_generic(&writer, CP_INSERT_COLS, &insert->cols[column]->base);
    }
    cp_write_header(&writer, CP_INSERT_SELECT, select_node_size);
    cp_write_header(&writer, PG_QUERY__NODE__NODE_SELECT_STMT, select_size);
    for (row = 0U; row < select->n_values_lists; row++) {
        const PgQuery__List *cells = select->values_lists[row]->list;
        cp_write_header(&writer, CP_SELECT_VALUES,
            (uint32_t)cp_envelope(PG_QUERY__NODE__NODE_LIST, row_sizes[row]));
        cp_write_header(&writer, PG_QUERY__NODE__NODE_LIST, row_sizes[row]);
        for (column = 0U; column < cells->n_items; column++) {
            cp_write_cell(&writer, cells->items[column]);
        }
    }
    cp_write_scalar(&writer, CP_SELECT_LIMIT, (uint32_t)select->limit_option);
    cp_write_scalar(&writer, CP_SELECT_OP, (uint32_t)select->op);
    cp_write_scalar(&writer, CP_INSERT_OVERRIDE, (uint32_t)insert->override);
    cp_write_scalar(&writer, CP_RAW_LENGTH, (uint32_t)raw->stmt_len);
    free(row_sizes);
    if (writer.failed || writer.next != writer.end) {
        free(out->data);
        out->data = NULL;
        return SQLPARSER_STATUS_INTERNAL_ERROR;
    }
    out->len = total_size;
    return SQLPARSER_STATUS_OK;
fallback:
    free(row_sizes);
    return SQLPARSER_STATUS_OK;
#endif
}
