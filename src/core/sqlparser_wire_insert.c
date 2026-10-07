/* Private source-and-wire certificate for one ordinary two-column INSERT.
 * The certificate owns only a descriptor and four bytes per row. No protobuf
 * objects are unpacked, and every eligibility miss is silent and read-only.
 * The narrow schema guards deliberately mirror the certified AST packer. */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_wire_insert_internal.h"
#include "../dialect/sqlparser_dialect_internal.h"

enum {
    WI_PARSE_VERSION = 1, WI_PARSE_STMTS = 2,
    WI_RAW_STMT = 1, WI_RAW_LOCATION = 2, WI_RAW_LENGTH = 3,
    WI_INSERT_RELATION = 1, WI_INSERT_COLS = 2, WI_INSERT_SELECT = 3,
    WI_INSERT_OVERRIDE = 7,
    WI_SELECT_VALUES = 10, WI_SELECT_LIMIT = 14, WI_SELECT_OP = 17,
    WI_LIST_ITEMS = 1, WI_CONST_LOCATION = 11,
    WI_INTEGER_VALUE = 1, WI_STRING_VALUE = 1
};

typedef struct {
    unsigned id;
    ProtobufCLabel label;
    ProtobufCType type;
    size_t quantifier_offset, offset;
    const void *descriptor, *default_value;
    unsigned flags;
} wi_field;

#define WI_SCALAR(t, f, id, kind, desc) \
    {id, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_##kind, 0, \
     offsetof(t, f), desc, NULL, 0}
#define WI_MESSAGE(t, f, id, desc) WI_SCALAR(t, f, id, MESSAGE, desc)
#define WI_REPEATED(t, f, id, desc) \
    {id, PROTOBUF_C_LABEL_REPEATED, PROTOBUF_C_TYPE_MESSAGE, \
     offsetof(t, n_##f), offsetof(t, f), desc, NULL, 0}
#define WI_ONEOF(t, f, id, q, desc) \
    {id, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_MESSAGE, \
     offsetof(t, q), offsetof(t, f), desc, NULL, PROTOBUF_C_FIELD_FLAG_ONEOF}

/* These are wire/layout guards, not a second schema. A generated-schema
 * change must either match every assumption below or use generic packing. */
static const wi_field wi_parse_fields[] = {
    WI_SCALAR(PgQuery__ParseResult, version, WI_PARSE_VERSION, INT32, NULL),
    WI_REPEATED(PgQuery__ParseResult, stmts, WI_PARSE_STMTS, &pg_query__raw_stmt__descriptor)
};
static const wi_field wi_raw_fields[] = {
    WI_MESSAGE(PgQuery__RawStmt, stmt, WI_RAW_STMT, &pg_query__node__descriptor),
    WI_SCALAR(PgQuery__RawStmt, stmt_location, WI_RAW_LOCATION, INT32, NULL),
    WI_SCALAR(PgQuery__RawStmt, stmt_len, WI_RAW_LENGTH, INT32, NULL)
};
static const wi_field wi_insert_fields[] = {
    WI_MESSAGE(PgQuery__InsertStmt, relation, WI_INSERT_RELATION, &pg_query__range_var__descriptor),
    WI_REPEATED(PgQuery__InsertStmt, cols, WI_INSERT_COLS, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__InsertStmt, select_stmt, WI_INSERT_SELECT, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__InsertStmt, on_conflict_clause, 4, &pg_query__on_conflict_clause__descriptor),
    WI_REPEATED(PgQuery__InsertStmt, returning_list, 5, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__InsertStmt, with_clause, 6, &pg_query__with_clause__descriptor),
    WI_SCALAR(PgQuery__InsertStmt, override, WI_INSERT_OVERRIDE, ENUM, &pg_query__overriding_kind__descriptor)
};
static const wi_field wi_select_fields[] = {
    WI_REPEATED(PgQuery__SelectStmt, distinct_clause, 1, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__SelectStmt, into_clause, 2, &pg_query__into_clause__descriptor),
    WI_REPEATED(PgQuery__SelectStmt, target_list, 3, &pg_query__node__descriptor),
    WI_REPEATED(PgQuery__SelectStmt, from_clause, 4, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__SelectStmt, where_clause, 5, &pg_query__node__descriptor),
    WI_REPEATED(PgQuery__SelectStmt, group_clause, 6, &pg_query__node__descriptor),
    WI_SCALAR(PgQuery__SelectStmt, group_distinct, 7, BOOL, NULL),
    WI_MESSAGE(PgQuery__SelectStmt, having_clause, 8, &pg_query__node__descriptor),
    WI_REPEATED(PgQuery__SelectStmt, window_clause, 9, &pg_query__node__descriptor),
    WI_REPEATED(PgQuery__SelectStmt, values_lists, WI_SELECT_VALUES, &pg_query__node__descriptor),
    WI_REPEATED(PgQuery__SelectStmt, sort_clause, 11, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__SelectStmt, limit_offset, 12, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__SelectStmt, limit_count, 13, &pg_query__node__descriptor),
    WI_SCALAR(PgQuery__SelectStmt, limit_option, WI_SELECT_LIMIT, ENUM, &pg_query__limit_option__descriptor),
    WI_REPEATED(PgQuery__SelectStmt, locking_clause, 15, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__SelectStmt, with_clause, 16, &pg_query__with_clause__descriptor),
    WI_SCALAR(PgQuery__SelectStmt, op, WI_SELECT_OP, ENUM, &pg_query__set_operation__descriptor),
    WI_SCALAR(PgQuery__SelectStmt, all, 18, BOOL, NULL),
    WI_MESSAGE(PgQuery__SelectStmt, larg, 19, &pg_query__select_stmt__descriptor),
    WI_MESSAGE(PgQuery__SelectStmt, rarg, 20, &pg_query__select_stmt__descriptor),
    WI_MESSAGE(PgQuery__SelectStmt, start_with_clause, 21, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__SelectStmt, connect_by_clause, 22, &pg_query__node__descriptor),
    WI_SCALAR(PgQuery__SelectStmt, connect_by_no_cycle, 23, BOOL, NULL),
    WI_SCALAR(PgQuery__SelectStmt, connect_by_first, 24, BOOL, NULL),
    WI_SCALAR(PgQuery__SelectStmt, limit_clause_style, 25, ENUM, &pg_query__limit_clause_style__descriptor)
};
static const wi_field wi_list_fields[] = {
    WI_REPEATED(PgQuery__List, items, WI_LIST_ITEMS, &pg_query__node__descriptor)
};
static const wi_field wi_const_fields[] = {
    WI_ONEOF(PgQuery__AConst, ival, PG_QUERY__A__CONST__VAL_IVAL, val_case, &pg_query__integer__descriptor),
    WI_ONEOF(PgQuery__AConst, fval, PG_QUERY__A__CONST__VAL_FVAL, val_case, &pg_query__float__descriptor),
    WI_ONEOF(PgQuery__AConst, boolval, PG_QUERY__A__CONST__VAL_BOOLVAL, val_case, &pg_query__boolean__descriptor),
    WI_ONEOF(PgQuery__AConst, sval, PG_QUERY__A__CONST__VAL_SVAL, val_case, &pg_query__string__descriptor),
    WI_ONEOF(PgQuery__AConst, bsval, PG_QUERY__A__CONST__VAL_BSVAL, val_case, &pg_query__bit_string__descriptor),
    WI_SCALAR(PgQuery__AConst, isnull, 10, BOOL, NULL),
    WI_SCALAR(PgQuery__AConst, location, WI_CONST_LOCATION, INT32, NULL)
};
static const wi_field wi_integer_fields[] = {
    WI_SCALAR(PgQuery__Integer, ival, WI_INTEGER_VALUE, INT32, NULL)
};
static const wi_field wi_string_fields[] = {
    {WI_STRING_VALUE, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_STRING,
     0, offsetof(PgQuery__String, sval), NULL, &protobuf_c_empty_string, 0},
    WI_SCALAR(PgQuery__String, location, 2, INT32, NULL)
};
static const wi_field wi_node_fields[] = {
    WI_ONEOF(PgQuery__Node, insert_stmt, PG_QUERY__NODE__NODE_INSERT_STMT, node_case, &pg_query__insert_stmt__descriptor),
    WI_ONEOF(PgQuery__Node, select_stmt, PG_QUERY__NODE__NODE_SELECT_STMT, node_case, &pg_query__select_stmt__descriptor),
    WI_ONEOF(PgQuery__Node, list, PG_QUERY__NODE__NODE_LIST, node_case, &pg_query__list__descriptor),
    WI_ONEOF(PgQuery__Node, res_target, PG_QUERY__NODE__NODE_RES_TARGET, node_case, &pg_query__res_target__descriptor),
    WI_ONEOF(PgQuery__Node, a_const, PG_QUERY__NODE__NODE_A_CONST, node_case, &pg_query__a__const__descriptor)
};
static const wi_field wi_range_fields[] = {
    {1, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_STRING, 0,
     offsetof(PgQuery__RangeVar, catalogname), NULL, &protobuf_c_empty_string, 0},
    {2, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_STRING, 0,
     offsetof(PgQuery__RangeVar, schemaname), NULL, &protobuf_c_empty_string, 0},
    {3, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_STRING, 0,
     offsetof(PgQuery__RangeVar, relname), NULL, &protobuf_c_empty_string, 0},
    WI_SCALAR(PgQuery__RangeVar, inh, 4, BOOL, NULL),
    {5, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_STRING, 0,
     offsetof(PgQuery__RangeVar, relpersistence), NULL, &protobuf_c_empty_string, 0},
    WI_MESSAGE(PgQuery__RangeVar, alias, 6, &pg_query__alias__descriptor),
    WI_SCALAR(PgQuery__RangeVar, location, 7, INT32, NULL)
};
static const wi_field wi_target_fields[] = {
    {1, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_STRING, 0,
     offsetof(PgQuery__ResTarget, name), NULL, &protobuf_c_empty_string, 0},
    WI_REPEATED(PgQuery__ResTarget, indirection, 2, &pg_query__node__descriptor),
    WI_MESSAGE(PgQuery__ResTarget, val, 3, &pg_query__node__descriptor),
    WI_SCALAR(PgQuery__ResTarget, location, 4, INT32, NULL)
};
#undef WI_SCALAR
#undef WI_MESSAGE
#undef WI_REPEATED
#undef WI_ONEOF

static int wi_field_matches(const ProtobufCFieldDescriptor *f, const wi_field *e)
{
    return f->id == e->id && f->label == e->label && f->type == e->type &&
        f->quantifier_offset == e->quantifier_offset && f->offset == e->offset &&
        f->descriptor == e->descriptor && f->default_value == e->default_value &&
        f->flags == e->flags;
}

static int wi_schema(const ProtobufCMessageDescriptor *d, size_t size,
    const wi_field *fields, size_t count)
{
    size_t i;
    if (d->magic != PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC ||
        d->sizeof_message != size || d->n_fields != count) return 0;
    for (i = 0U; i < count; i++) {
        if (!wi_field_matches(&d->fields[i], &fields[i])) return 0;
    }
    return 1;
}

static int wi_schemas_match(void)
{
    size_t i;
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    if (PG_QUERY__NODE__NODE_LIST < 16 || PG_QUERY__NODE__NODE_LIST > 2047 ||
        PG_QUERY__NODE__NODE_A_CONST < 16 || PG_QUERY__NODE__NODE_A_CONST > 2047 ||
        PG_QUERY__A__CONST__VAL_IVAL != 1 || PG_QUERY__A__CONST__VAL_SVAL != 4 ||
        PG_QUERY__OVERRIDING_KIND__OVERRIDING_NOT_SET != 1 ||
        PG_QUERY__LIMIT_OPTION__LIMIT_OPTION_DEFAULT != 1 ||
        PG_QUERY__SET_OPERATION__SETOP_NONE != 1 ||
        PG_QUERY__LIMIT_CLAUSE_STYLE__LIMIT_CLAUSE_STYLE_DEFAULT != 0) return 0;
#define WI_SCHEMA(name, type, fields) \
    wi_schema(&pg_query__##name##__descriptor, sizeof(type), fields, sizeof(fields) / sizeof(fields[0]))
    if (!WI_SCHEMA(parse_result, PgQuery__ParseResult, wi_parse_fields) ||
        !WI_SCHEMA(raw_stmt, PgQuery__RawStmt, wi_raw_fields) ||
        !WI_SCHEMA(insert_stmt, PgQuery__InsertStmt, wi_insert_fields) ||
        !WI_SCHEMA(select_stmt, PgQuery__SelectStmt, wi_select_fields) ||
        !WI_SCHEMA(list, PgQuery__List, wi_list_fields) ||
        !WI_SCHEMA(a__const, PgQuery__AConst, wi_const_fields) ||
        !WI_SCHEMA(integer, PgQuery__Integer, wi_integer_fields) ||
        !WI_SCHEMA(string, PgQuery__String, wi_string_fields) ||
        !WI_SCHEMA(range_var, PgQuery__RangeVar, wi_range_fields) ||
        !WI_SCHEMA(res_target, PgQuery__ResTarget, wi_target_fields)) return 0;
#undef WI_SCHEMA
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
    for (i = 0U; i < sizeof(wi_node_fields) / sizeof(wi_node_fields[0]); i++) {
        if (wi_node_fields[i].id == 0U || wi_node_fields[i].id > d->n_fields ||
            !wi_field_matches(&d->fields[wi_node_fields[i].id - 1U], &wi_node_fields[i])) return 0;
    }
    return 1;
}

/* Readers accept only the minimal, ordered encoding produced by protobuf-c.
 * Taking an exact child slice makes unknown, duplicate and misplaced fields
 * fail at that message's end, rather than accidentally accepting a prefix. */
typedef struct {
    const uint8_t *next, *end;
} wi_reader;

static int wi_varint_long(wi_reader *r, uint32_t *out)
{
    uint32_t value = 0U;
    unsigned shift;
    for (shift = 0U; shift <= 28U; shift += 7U) {
        uint8_t b;
        if (r->next == r->end) return 0;
        b = *r->next++;
        if (shift == 28U && (b & 0xf0U) != 0U) return 0;
        value |= (uint32_t)(b & 0x7fU) << shift;
        if ((b & 0x80U) == 0U) {
            if (shift != 0U && b == 0U) return 0;
            *out = value;
            return 1;
        }
    }
    return 0;
}

/* Most keys and child lengths fit in one byte. Keep that bounded case small
 * enough to inline, while the shared reader retains the exact failure cursor
 * and canonical/overflow checks for every multi-byte encoding. */
static inline int wi_varint(wi_reader *r, uint32_t *out)
{
    if (r->next == r->end) return 0;
    if (*r->next < 0x80U) {
        *out = *r->next++;
        return 1;
    }
    return wi_varint_long(r, out);
}

static int wi_key_is(const wi_reader *r, unsigned field, unsigned type)
{
    wi_reader copy = *r;
    uint32_t key;
    return wi_varint(&copy, &key) && key == ((field << 3) | type);
}

static int wi_message(wi_reader *r, unsigned field, wi_reader *child)
{
    uint32_t key, length;
    if (!wi_varint(r, &key) || key != ((field << 3) | 2U) ||
        !wi_varint(r, &length) || (size_t)length > (size_t)(r->end - r->next)) return 0;
    child->next = r->next;
    child->end = child->next + length;
    r->next = child->end;
    return 1;
}

static int wi_scalar(wi_reader *r, unsigned field, uint32_t *out)
{
    uint32_t key;
    /* Explicit defaults are noncanonical for these proto3 scalar fields. */
    return wi_varint(r, &key) && key == (field << 3) &&
        wi_varint(r, out) && *out != 0U && *out <= INT32_MAX;
}

static int wi_optional_scalar(wi_reader *r, unsigned field, uint32_t *out)
{
    *out = 0U;
    return !wi_key_is(r, field, 0U) || wi_scalar(r, field, out);
}

static int wi_enum(wi_reader *r, unsigned field, uint32_t expected)
{
    uint32_t value;
    return wi_scalar(r, field, &value) && value == expected;
}

static int wi_text(wi_reader *r, unsigned field, const char **text, uint32_t *length)
{
    wi_reader bytes;
    if (!wi_message(r, field, &bytes) || bytes.next == bytes.end) return 0;
    *text = (const char *)bytes.next;
    *length = (uint32_t)(bytes.end - bytes.next);
    return 1;
}

static int wi_cell(wi_reader *list, unsigned column, sqlparser_wire_insert_cell_t *cell)
{
    wi_reader node, constant, value;
    uint32_t number;
    unsigned kind = column == 0U ? PG_QUERY__A__CONST__VAL_IVAL : PG_QUERY__A__CONST__VAL_SVAL;
    memset(cell, 0, sizeof(*cell));
    if (!wi_message(list, WI_LIST_ITEMS, &node) ||
        !wi_message(&node, PG_QUERY__NODE__NODE_A_CONST, &constant) || node.next != node.end ||
        !wi_message(&constant, kind, &value)) return 0;
    if (column == 0U) {
        if (!wi_optional_scalar(&value, WI_INTEGER_VALUE, &number)) return 0;
        cell->integer = (int32_t)number;
    } else {
        cell->text = (const char *)value.next;
        if (value.next != value.end && !wi_text(&value, WI_STRING_VALUE, &cell->text, &cell->length)) return 0;
    }
    if (value.next != value.end || !wi_scalar(&constant, WI_CONST_LOCATION, &number) ||
        constant.next != constant.end) return 0;
    cell->location = (int32_t)number;
    return 1;
}

static int wi_row(wi_reader *values, sqlparser_wire_insert_cell_t cells[2])
{
    wi_reader node, list;
    return wi_message(values, WI_SELECT_VALUES, &node) &&
        wi_message(&node, PG_QUERY__NODE__NODE_LIST, &list) && node.next == node.end &&
        wi_cell(&list, 0U, &cells[0]) && wi_cell(&list, 1U, &cells[1]) && list.next == list.end;
}

int sqlparser_wire_insert_row(const sqlparser_wire_insert_t *insert, size_t row,
    sqlparser_wire_insert_cell_t cells[2])
{
    wi_reader reader;
    if (insert == NULL || cells == NULL || row >= insert->row_count || insert->wire == NULL ||
        insert->row_offsets == NULL || insert->wire_length > PTRDIFF_MAX ||
        insert->row_offsets[row] >= insert->wire_length) return 0;
    reader.next = (const uint8_t *)insert->wire + insert->row_offsets[row];
    reader.end = (const uint8_t *)insert->wire + insert->wire_length;
    return wi_row(&reader, cells);
}

/* The public handle owns immutable canonical bytes for the certificate's
 * entire lifetime. Subsequent consumers need only values, not a second proof
 * of each tag, default and nested-message boundary. These helpers retain the
 * outer row bound and never read beyond it, even at a length/varint edge. */
#if defined(_MSC_VER)
#define WI_INLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define WI_INLINE static inline __attribute__((always_inline))
#else
#define WI_INLINE static inline
#endif

WI_INLINE int wi_certified_u32(const uint8_t **cursor, const uint8_t *end, uint32_t *out)
{
    const uint8_t *p = *cursor;
    uint32_t value;
    unsigned shift;
    uint8_t byte;
    if (p == end) return 0;
    byte = *p++;
    value = byte & 0x7fU;
    if (byte < 0x80U) { *cursor = p; *out = value; return 1; }
    for (shift = 7U; shift <= 28U; shift += 7U) {
        if (p == end) return 0;
        byte = *p++;
        if (shift == 28U && (byte & 0xf0U) != 0U) return 0;
        value |= (uint32_t)(byte & 0x7fU) << shift;
        if (byte < 0x80U) { *cursor = p; *out = value; return 1; }
    }
    return 0;
}

/* Skip an already-proved tag and its canonical length. There is no need to
 * decode either numeric value or recreate a nested reader for this envelope. */
WI_INLINE int wi_certified_envelope(const uint8_t **cursor, const uint8_t *end, size_t tag_bytes)
{
    const uint8_t *p = *cursor;
    unsigned count;
    if ((size_t)(end - p) <= tag_bytes) return 0;
    p += tag_bytes;
    for (count = 0U; count < 5U; count++) {
        if (p == end) return 0;
        if (*p++ < 0x80U) { *cursor = p; return 1; }
    }
    return 0;
}

WI_INLINE int wi_certified_cell(const uint8_t **cursor, const uint8_t *end,
    unsigned column, sqlparser_wire_insert_cell_t *cell)
{
    const uint8_t *p = *cursor;
    uint32_t length, value;
    /* List.items tag=1; Node.a_const is a two-byte message tag. Its exact
     * generated ID and oneof descriptor were checked before certification. */
    if (!wi_certified_envelope(&p, end, 1U) ||
        !wi_certified_envelope(&p, end, 2U) || p == end) return 0;
    p++; /* Proved AConst integer/string arm tag, both one byte. */
    if (!wi_certified_u32(&p, end, &length)) return 0;
    if (column == 0U) {
        if (length != 0U) {
            if (p == end) return 0;
            p++; /* Integer.ival tag. Zero has an empty wrapper. */
            if (!wi_certified_u32(&p, end, &value) || value > INT32_MAX) return 0;
            cell->integer = (int32_t)value;
        }
    } else {
        cell->text = (const char *)p;
        if (length != 0U) {
            if (p == end) return 0;
            p++; /* String.sval tag. Empty text has an empty wrapper. */
            if (!wi_certified_u32(&p, end, &value) || value > (size_t)(end - p)) return 0;
            cell->text = (const char *)p;
            cell->length = value;
            p += value;
        }
    }
    if (p == end) return 0;
    p++; /* AConst.location tag, proved present and positive. */
    if (!wi_certified_u32(&p, end, &value) || value > INT32_MAX) return 0;
    cell->location = (int32_t)value;
    *cursor = p;
    return 1;
}
#undef WI_INLINE

int sqlparser_wire_insert_certified_row(const sqlparser_wire_insert_t *insert, size_t row,
    sqlparser_wire_insert_cell_t cells[2])
{
#ifdef SQLPARSER_WIRE_INSERT_DEFENSIVE_ROWS
    return sqlparser_wire_insert_row(insert, row, cells);
#else
    const uint8_t *p, *end;
    uint32_t length;
    if (insert == NULL || cells == NULL || row >= insert->row_count || insert->wire == NULL ||
        insert->row_offsets == NULL || insert->wire_length > PTRDIFF_MAX ||
        insert->row_offsets[row] >= insert->wire_length) return 0;
    p = (const uint8_t *)insert->wire + insert->row_offsets[row];
    end = (const uint8_t *)insert->wire + insert->wire_length;
    p++; /* Proved SelectStmt.values_lists tag. */
    if (!wi_certified_u32(&p, end, &length) || length > (size_t)(end - p)) return 0;
    end = p + length;
    memset(cells, 0, sizeof(*cells) * 2U);
    if (!wi_certified_envelope(&p, end, 2U) ||
        !wi_certified_cell(&p, end, 0U, &cells[0]) ||
        !wi_certified_cell(&p, end, 1U, &cells[1])) return 0;
    return p == end;
#endif
}

static void wi_gap(const char *sql, size_t length, size_t *pos)
{
    while (*pos < length) {
        unsigned char c = (unsigned char)sql[*pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != '\v') break;
        ++*pos;
    }
}

static int wi_identifier_char(unsigned char c, int first)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
        (!first && c >= '0' && c <= '9');
}

static int wi_word(const char *sql, size_t length, size_t *pos, const char *word, size_t word_length)
{
    size_t i = 0U;
    wi_gap(sql, length, pos);
    if (*pos == length || !wi_identifier_char((unsigned char)sql[*pos], 1)) return 0;
    while (*pos < length && wi_identifier_char((unsigned char)sql[*pos], 0)) {
        unsigned char c = (unsigned char)sql[(*pos)++];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if (i == word_length || c != (unsigned char)word[i++]) return 0;
    }
    return i == word_length;
}

static int wi_punctuation(const char *sql, size_t length, size_t *pos, char punctuation)
{
    wi_gap(sql, length, pos);
    if (*pos == length || sql[*pos] != punctuation) return 0;
    ++*pos;
    return 1;
}

static int wi_source_cell(const char *sql, size_t length, size_t *pos,
    const sqlparser_wire_insert_cell_t *cell, unsigned column)
{
    wi_gap(sql, length, pos);
    if (*pos == length || cell->location != (int32_t)*pos) return 0;
    if (column == 0U) {
        uint32_t number = 0U;
        if (sql[*pos] < '0' || sql[*pos] > '9') return 0;
        do {
            unsigned digit = (unsigned)(sql[(*pos)++] - '0');
            if (number > ((uint32_t)INT32_MAX - digit) / 10U) return 0;
            number = number * 10U + digit;
        } while (*pos < length && sql[*pos] >= '0' && sql[*pos] <= '9');
        return number == (uint32_t)cell->integer;
    } else {
        size_t start;
        if (sql[(*pos)++] != '\'') return 0;
        start = *pos;
        while (*pos < length && sql[*pos] != '\'') {
            unsigned char c = (unsigned char)sql[(*pos)++];
            if (c < 0x20U || c > 0x7eU || c == '\\') return 0;
        }
        if (*pos == length || *pos - start != cell->length ||
            memcmp(sql + start, cell->text, cell->length) != 0) return 0;
        ++*pos;
        return 1;
    }
}

static int wi_name(wi_reader *message, unsigned name_field, unsigned location_field,
    sqlparser_wire_insert_t *insert, unsigned index,
    const char *sql, size_t length, size_t *pos)
{
    uint32_t location;
    if (!wi_text(message, name_field, &insert->names[index], &insert->name_lengths[index])) return 0;
    if (index == 0U) {
        const char *persistence;
        uint32_t persistence_length;
        if (!wi_enum(message, 4U, 1U) || !wi_text(message, 5U, &persistence, &persistence_length) ||
            persistence_length != 1U || persistence[0] != 'p') return 0;
    }
    if (!wi_scalar(message, location_field, &location) || message->next != message->end) return 0;
    wi_gap(sql, length, pos);
    if (location != *pos || !wi_word(sql, length, pos, insert->names[index], insert->name_lengths[index])) return 0;
    if ((size_t)insert->name_lengths[index] >= SIZE_MAX - insert->text_bytes) return 0;
    insert->text_bytes += (size_t)insert->name_lengths[index] + 1U;
    return 1;
}

sqlparser_wire_insert_t *sqlparser_wire_insert_certify(const sqlparser_handle_t *handle)
{
    sqlparser_wire_insert_t description = {0}, *result;
    wi_reader parse, raw, node, insert, relation, column, target, select_node, values, rows;
    sqlparser_wire_insert_cell_t cells[2];
    const char *sql;
    const uint8_t *base;
    size_t length, pos = 0U, row;
    uint64_t footprint, budget, structural_per_row, structural_budget;
    unsigned index;

    if (handle == NULL || handle->dialect != SQLPARSER_DIALECT_MYSQL || handle->failed ||
        handle->sql == NULL || handle->parser_sql == NULL || handle->ast != NULL ||
        (handle->generation != 0UL && !handle->surface_source_complete) ||
        handle->current_sql != NULL || handle->current_parser_sql != NULL || handle->control != NULL ||
        handle->statement_count != 1U || handle->sql_len != handle->parser_sql_len ||
        handle->sql_len < 32U * 6U || handle->sql_len > INT32_MAX || handle->sql_len > PTRDIFF_MAX ||
        handle->parse_tree.data == NULL || handle->parse_tree.len == 0U ||
        handle->parse_tree.len > UINT32_MAX || handle->parse_tree.len > PTRDIFF_MAX ||
        handle->patch_batch_flags != 0U || handle->surface_source_edits.count != 0U ||
        handle->surface_source_edits.items != NULL || handle->identifier_mutation_count != 0U ||
        handle->identifier_mutations != NULL || handle->identifier_mutation_capacity != 0U ||
        handle->identifier_spelling_count != 0U || handle->identifier_spellings != NULL ||
        handle->identifier_spelling_capacity != 0U || handle->identifier_spelling_status != SQLPARSER_STATUS_OK) return NULL;
    sql = handle->sql;
    length = handle->sql_len;
    if (memcmp(sql, handle->parser_sql, length + 1U) != 0 || sql[length] != '\0') return NULL;
    description.wire = handle->parse_tree.data;
    description.wire_length = handle->parse_tree.len;
    base = (const uint8_t *)description.wire;
    parse.next = base;
    parse.end = base + description.wire_length;
    if (!wi_optional_scalar(&parse, WI_PARSE_VERSION, &description.version) ||
        !wi_message(&parse, WI_PARSE_STMTS, &raw) || parse.next != parse.end ||
        !wi_message(&raw, WI_RAW_STMT, &node) ||
        !wi_optional_scalar(&raw, WI_RAW_LENGTH, &description.raw_length) || raw.next != raw.end ||
        !wi_message(&node, PG_QUERY__NODE__NODE_INSERT_STMT, &insert) || node.next != node.end ||
        !wi_word(sql, length, &pos, "insert", 6U) || !wi_word(sql, length, &pos, "into", 4U)) return NULL;
    description.prefix_offset = (uint32_t)(insert.next - base);
    if (!wi_message(&insert, WI_INSERT_RELATION, &relation) ||
        !wi_name(&relation, 3U, 7U, &description, 0U, sql, length, &pos) ||
        !wi_punctuation(sql, length, &pos, '(')) return NULL;
    for (index = 1U; index <= 2U; index++) {
        if (!wi_message(&insert, WI_INSERT_COLS, &column) ||
            !wi_message(&column, PG_QUERY__NODE__NODE_RES_TARGET, &target) || column.next != column.end ||
            !wi_name(&target, 1U, 4U, &description, index, sql, length, &pos) ||
            !wi_punctuation(sql, length, &pos, index == 1U ? ',' : ')')) return NULL;
    }
    description.prefix_length = (uint32_t)(insert.next - base) - description.prefix_offset;
    if (!wi_message(&insert, WI_INSERT_SELECT, &select_node) ||
        !wi_enum(&insert, WI_INSERT_OVERRIDE, 1U) || insert.next != insert.end ||
        !wi_message(&select_node, PG_QUERY__NODE__NODE_SELECT_STMT, &values) || select_node.next != select_node.end ||
        !wi_word(sql, length, &pos, "values", 6U)) return NULL;
    rows = values;
    while (wi_key_is(&values, WI_SELECT_VALUES, 2U)) {
        /* A strict per-string admission ceiling prevents rescanning a whole
         * long-string generation only to decline the aggregate slab budget.
         * It still admits the primary and graph-borrowed selector strings. */
        if (!wi_row(&values, cells) || cells[1].length > 32U ||
            (description.row_count != 0U && !wi_punctuation(sql, length, &pos, ',')) ||
            !wi_punctuation(sql, length, &pos, '(') ||
            !wi_source_cell(sql, length, &pos, &cells[0], 0U) ||
            !wi_punctuation(sql, length, &pos, ',') ||
            !wi_source_cell(sql, length, &pos, &cells[1], 1U) ||
            !wi_punctuation(sql, length, &pos, ')')) return NULL;
        if (description.row_count == UINT32_MAX ||
            (size_t)cells[1].length >= SIZE_MAX - description.text_bytes) return NULL;
        description.row_count++;
        description.text_bytes += (size_t)cells[1].length + 1U;
    }
    if (description.row_count < 32U || !wi_enum(&values, WI_SELECT_LIMIT, 1U) ||
        !wi_enum(&values, WI_SELECT_OP, 1U) || values.next != values.end) return NULL;
    wi_gap(sql, length, &pos);
    if (pos < length && sql[pos] == ';') {
        if (description.raw_length != pos) return NULL;
        pos++;
        wi_gap(sql, length, &pos);
    } else if (description.raw_length != 0U) return NULL;
    if (pos != length || !sqlparser_mysql_state_is_plain_insert_strings(handle->dialect_state, description.row_count) ||
        !wi_schemas_match()) return NULL;

    /* Keep retained certificate + owned graph text within 16 bytes per cell.
     * This explicit eligibility bound caps the extra storage even if a later
     * generic reader materializes the full AST while this graph is retained. */
    footprint = (uint64_t)description.text_bytes +
        (uint64_t)description.row_count * sizeof(uint32_t) + sizeof(description);
    budget = (uint64_t)description.row_count * 32U;
    /* Also bound retained extras to one twelfth of the native row's known
     * structural allocations. The byte-per-cell cap alone is insufficient
     * as a portable AST-relative bound when pointers are narrower. */
    structural_per_row = 3U * (uint64_t)sizeof(PgQuery__Node) +
        2U * (uint64_t)sizeof(PgQuery__AConst) + sizeof(PgQuery__Integer) +
        sizeof(PgQuery__String) + sizeof(PgQuery__List) + 3U * (uint64_t)sizeof(void *);
    if (structural_per_row == 0U || (uint64_t)description.row_count > UINT64_MAX / structural_per_row) return NULL;
    structural_budget = (uint64_t)description.row_count * structural_per_row / 12U;
    if (footprint > budget || footprint > structural_budget ||
        description.row_count > SIZE_MAX / sizeof(uint32_t)) return NULL;
    result = (sqlparser_wire_insert_t *)malloc(sizeof(*result));
    if (result == NULL) return NULL;
    *result = description;
    result->row_offsets = (uint32_t *)malloc(description.row_count * sizeof(uint32_t));
    if (result->row_offsets == NULL) { free(result); return NULL; }
    for (row = 0U; row < description.row_count; row++) {
        wi_reader ignored;
        result->row_offsets[row] = (uint32_t)(rows.next - base);
        /* The complete immutable row was validated in the first pass. */
        if (!wi_message(&rows, WI_SELECT_VALUES, &ignored)) {
            sqlparser_wire_insert_destroy(result);
            return NULL;
        }
    }
    return result;
}

void sqlparser_wire_insert_destroy(sqlparser_wire_insert_t *insert)
{
    if (insert != NULL) {
        free(insert->row_offsets);
        free(insert);
    }
}

static unsigned wi_varint_size(uint32_t value)
{
    if (value < (1U << 7)) return 1U;
    if (value < (1U << 14)) return 2U;
    if (value < (1U << 21)) return 3U;
    if (value < (1U << 28)) return 4U;
    return 5U;
}

static uint64_t wi_envelope(unsigned tag, uint32_t payload)
{
    return (uint64_t)wi_varint_size((tag << 3) | 2U) + wi_varint_size(payload) + payload;
}

static unsigned wi_scalar_size(unsigned tag, uint32_t value)
{
    return value == 0U ? 0U : wi_varint_size(tag << 3) + wi_varint_size(value);
}

typedef struct {
    uint32_t value, constant, node;
} wi_cell_sizes;

static int wi_measure_cell(const sqlparser_wire_insert_cell_t *cell,
    unsigned column, wi_cell_sizes *sizes)
{
    uint64_t n;
    unsigned kind = column == 0U ? PG_QUERY__A__CONST__VAL_IVAL : PG_QUERY__A__CONST__VAL_SVAL;
    if (cell->location < 0) return 0;
    if (column == 0U) {
        if (cell->integer < 0) return 0;
        n = wi_scalar_size(WI_INTEGER_VALUE, (uint32_t)cell->integer);
    } else {
        n = cell->length == 0U ? 0U : wi_envelope(WI_STRING_VALUE, cell->length);
    }
    if (n > UINT32_MAX) return 0;
    sizes->value = (uint32_t)n;
    n = wi_envelope(kind, sizes->value) + wi_scalar_size(WI_CONST_LOCATION, (uint32_t)cell->location);
    if (n > UINT32_MAX) return 0;
    sizes->constant = (uint32_t)n;
    n = wi_envelope(PG_QUERY__NODE__NODE_A_CONST, sizes->constant);
    if (n > UINT32_MAX) return 0;
    sizes->node = (uint32_t)n;
    return 1;
}

static int wi_edits(const sqlparser_surface_source_edits_t *edits, int replacements_proven)
{
    size_t index, previous_end = 0U;
    if (edits == NULL || (edits->count != 0U && edits->items == NULL)) return 0;
    for (index = 0U; index < edits->count; index++) {
        const sqlparser_surface_source_edit_t *edit = &edits->items[index];
        size_t p;
        if (edit->source_start < previous_end || edit->source_end <= edit->source_start ||
            edit->source_end > INT32_MAX || edit->replacement == NULL ||
            edit->replacement_length < 2U || edit->replacement_length > INT32_MAX ||
            edit->replacement[0] != '\'' || edit->replacement[edit->replacement_length - 1U] != '\'') return 0;
        /* The private proven entry reuses the patch planner's proof of this
         * exact immutable owned token. Interval, length and quote-envelope
         * checks still run; only the redundant interior-byte walk is omitted. */
        if (!replacements_proven) {
            for (p = 1U; p + 1U < edit->replacement_length; p++) {
                unsigned char c = (unsigned char)edit->replacement[p];
                if (c < 0x20U || c > 0x7eU || c == '\'' || c == '\\') return 0;
            }
        }
        previous_end = edit->source_end;
    }
    return 1;
}

/* Read original borrowed cells, substitute only exact second-cell spans, and
 * shift both locations before incorporating this row's replacement delta.
 * The resulting text still borrows either the wire or an immutable edit. */
static int wi_effective_row(const sqlparser_wire_insert_t *insert, size_t row,
    const sqlparser_surface_source_edits_t *edits, size_t *edit_index,
    int64_t *delta, sqlparser_wire_insert_cell_t cells[2])
{
    unsigned column;
    size_t old_string_location;
    if (!sqlparser_wire_insert_certified_row(insert, row, cells)) return 0;
    old_string_location = (size_t)cells[1].location;
    for (column = 0U; column < 2U; column++) {
        int64_t location = (int64_t)cells[column].location + *delta;
        if (location < 0 || location > INT32_MAX) return 0;
        cells[column].location = (int32_t)location;
    }
    if (*edit_index < edits->count) {
        const sqlparser_surface_source_edit_t *edit = &edits->items[*edit_index];
        if (edit->source_start < old_string_location) return 0;
        if (edit->source_start == old_string_location) {
            uint64_t end = (uint64_t)old_string_location + cells[1].length + 2U;
            if (end != edit->source_end) return 0;
            *delta += (int64_t)edit->replacement_length - ((int64_t)cells[1].length + 2);
            cells[1].text = edit->replacement + 1U;
            cells[1].length = (uint32_t)(edit->replacement_length - 2U);
            ++*edit_index;
        }
    }
    return (uint64_t)(uint32_t)cells[1].location + cells[1].length + 2U <= INT32_MAX;
}

typedef struct {
    uint8_t *next, *end;
    int failed;
} wi_writer;

static void wi_write_varint(wi_writer *w, uint32_t value)
{
    do {
        if (w->next == w->end) { w->failed = 1; return; }
        *w->next++ = (uint8_t)((value & 0x7fU) | (value >= 0x80U ? 0x80U : 0U));
        value >>= 7;
    } while (value != 0U);
}

static void wi_write_header(wi_writer *w, unsigned tag, uint32_t payload)
{
    wi_write_varint(w, (tag << 3) | 2U);
    wi_write_varint(w, payload);
}

static void wi_write_scalar(wi_writer *w, unsigned tag, uint32_t value)
{
    if (value != 0U) {
        wi_write_varint(w, tag << 3);
        wi_write_varint(w, value);
    }
}

static void wi_write_bytes(wi_writer *w, const char *bytes, size_t length)
{
    if (length > (size_t)(w->end - w->next)) { w->failed = 1; return; }
    if (length != 0U) memcpy(w->next, bytes, length);
    w->next += length;
}

static void wi_write_cell(wi_writer *w, const sqlparser_wire_insert_cell_t *cell, unsigned column)
{
    wi_cell_sizes sizes;
    unsigned kind = column == 0U ? PG_QUERY__A__CONST__VAL_IVAL : PG_QUERY__A__CONST__VAL_SVAL;
    if (!wi_measure_cell(cell, column, &sizes)) { w->failed = 1; return; }
    wi_write_header(w, WI_LIST_ITEMS, sizes.node);
    wi_write_header(w, PG_QUERY__NODE__NODE_A_CONST, sizes.constant);
    wi_write_header(w, kind, sizes.value);
    if (column == 0U) {
        wi_write_scalar(w, WI_INTEGER_VALUE, (uint32_t)cell->integer);
    } else if (cell->length != 0U) {
        wi_write_header(w, WI_STRING_VALUE, cell->length);
        wi_write_bytes(w, cell->text, cell->length);
    }
    wi_write_scalar(w, WI_CONST_LOCATION, (uint32_t)cell->location);
}

static sqlparser_status_t wi_pack(const sqlparser_wire_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out, int replacements_proven)
{
    uint32_t *row_sizes = NULL;
    uint32_t select_size, select_node_size, insert_size, insert_node_size, raw_size, total_size, raw_length;
    uint64_t n;
    int64_t delta = 0;
    size_t row, edit_index = 0U;
    wi_writer writer;

    if (out == NULL) return SQLPARSER_STATUS_INVALID_ARGUMENT;
    out->data = NULL;
    out->len = 0U;
    if (insert == NULL || edits == NULL) return SQLPARSER_STATUS_INVALID_ARGUMENT;
    if (insert->wire == NULL || insert->wire_length > UINT32_MAX || insert->wire_length > PTRDIFF_MAX ||
        insert->row_count < 32U || insert->row_count > UINT32_MAX || insert->row_offsets == NULL ||
        insert->row_count > SIZE_MAX / sizeof(*row_sizes) || insert->version > INT32_MAX ||
        insert->raw_length > INT32_MAX || insert->prefix_offset > insert->wire_length ||
        insert->prefix_length > insert->wire_length - insert->prefix_offset ||
        !wi_edits(edits, replacements_proven)) return SQLPARSER_STATUS_UNSUPPORTED;
    /* Scratch OOM is an eligibility miss; generic packing needs no cache. */
    row_sizes = (uint32_t *)malloc(insert->row_count * sizeof(*row_sizes));
    if (row_sizes == NULL) return SQLPARSER_STATUS_UNSUPPORTED;
    n = 0U;
    for (row = 0U; row < insert->row_count; row++) {
        sqlparser_wire_insert_cell_t cells[2];
        uint64_t row_size = 0U, row_node_size;
        unsigned column;
        if (!wi_effective_row(insert, row, edits, &edit_index, &delta, cells)) goto unsupported;
        for (column = 0U; column < 2U; column++) {
            wi_cell_sizes sizes;
            if (!wi_measure_cell(&cells[column], column, &sizes)) goto unsupported;
            row_size += wi_envelope(WI_LIST_ITEMS, sizes.node);
        }
        if (row_size > UINT32_MAX) goto unsupported;
        row_sizes[row] = (uint32_t)row_size;
        row_node_size = wi_envelope(PG_QUERY__NODE__NODE_LIST, row_sizes[row]);
        if (row_node_size > UINT32_MAX) goto unsupported;
        n += wi_envelope(WI_SELECT_VALUES, (uint32_t)row_node_size);
        if (n > UINT32_MAX) goto unsupported;
    }
    if (edit_index != edits->count) goto unsupported;
    raw_length = insert->raw_length;
    if (raw_length != 0U) {
        int64_t shifted_length = (int64_t)raw_length + delta;
        if (shifted_length <= 0 || shifted_length > INT32_MAX) goto unsupported;
        raw_length = (uint32_t)shifted_length;
    }
    n += wi_scalar_size(WI_SELECT_LIMIT, 1U) + wi_scalar_size(WI_SELECT_OP, 1U);
    if (n > UINT32_MAX) goto unsupported;
    select_size = (uint32_t)n;
    n = wi_envelope(PG_QUERY__NODE__NODE_SELECT_STMT, select_size);
    if (n > UINT32_MAX) goto unsupported;
    select_node_size = (uint32_t)n;
    n = insert->prefix_length + wi_envelope(WI_INSERT_SELECT, select_node_size) +
        wi_scalar_size(WI_INSERT_OVERRIDE, 1U);
    if (n > UINT32_MAX) goto unsupported;
    insert_size = (uint32_t)n;
    n = wi_envelope(PG_QUERY__NODE__NODE_INSERT_STMT, insert_size);
    if (n > UINT32_MAX) goto unsupported;
    insert_node_size = (uint32_t)n;
    n = wi_envelope(WI_RAW_STMT, insert_node_size) + wi_scalar_size(WI_RAW_LENGTH, raw_length);
    if (n > UINT32_MAX) goto unsupported;
    raw_size = (uint32_t)n;
    n = wi_scalar_size(WI_PARSE_VERSION, insert->version) + wi_envelope(WI_PARSE_STMTS, raw_size);
    if (n > UINT32_MAX || n > SIZE_MAX || n > PTRDIFF_MAX) goto unsupported;
    total_size = (uint32_t)n;
    out->data = (char *)malloc(total_size);
    if (out->data == NULL) {
        free(row_sizes);
        return SQLPARSER_STATUS_NO_MEMORY;
    }
    writer.next = (uint8_t *)out->data;
    writer.end = writer.next + total_size;
    writer.failed = 0;
    wi_write_scalar(&writer, WI_PARSE_VERSION, insert->version);
    wi_write_header(&writer, WI_PARSE_STMTS, raw_size);
    wi_write_header(&writer, WI_RAW_STMT, insert_node_size);
    wi_write_header(&writer, PG_QUERY__NODE__NODE_INSERT_STMT, insert_size);
    wi_write_bytes(&writer, insert->wire + insert->prefix_offset, insert->prefix_length);
    wi_write_header(&writer, WI_INSERT_SELECT, select_node_size);
    wi_write_header(&writer, PG_QUERY__NODE__NODE_SELECT_STMT, select_size);
    edit_index = 0U;
    delta = 0;
    for (row = 0U; row < insert->row_count; row++) {
        sqlparser_wire_insert_cell_t cells[2];
        if (!wi_effective_row(insert, row, edits, &edit_index, &delta, cells)) {
            writer.failed = 1;
            break;
        }
        wi_write_header(&writer, WI_SELECT_VALUES,
            (uint32_t)wi_envelope(PG_QUERY__NODE__NODE_LIST, row_sizes[row]));
        wi_write_header(&writer, PG_QUERY__NODE__NODE_LIST, row_sizes[row]);
        wi_write_cell(&writer, &cells[0], 0U);
        wi_write_cell(&writer, &cells[1], 1U);
    }
    wi_write_scalar(&writer, WI_SELECT_LIMIT, 1U);
    wi_write_scalar(&writer, WI_SELECT_OP, 1U);
    wi_write_scalar(&writer, WI_INSERT_OVERRIDE, 1U);
    wi_write_scalar(&writer, WI_RAW_LENGTH, raw_length);
    free(row_sizes);
    if (writer.failed || writer.next != writer.end || edit_index != edits->count) {
        free(out->data);
        out->data = NULL;
        return SQLPARSER_STATUS_INTERNAL_ERROR;
    }
    out->len = total_size;
    return SQLPARSER_STATUS_OK;
unsupported:
    free(row_sizes);
    return SQLPARSER_STATUS_UNSUPPORTED;
}

sqlparser_status_t sqlparser_wire_insert_pack(const sqlparser_wire_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out)
{
    return wi_pack(insert, edits, out, 0);
}

sqlparser_status_t sqlparser_wire_insert_pack_proven_edits(const sqlparser_wire_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out)
{
    return wi_pack(insert, edits, out, 1);
}

/* General scalar VALUES certificates intentionally live separately from the
 * narrow two-column codec. Grammar validation already happened: this pass
 * proves only the immutable source/wire facts needed for graph and packing. */
static int wsi_schemas_match(void)
{
    static const wi_field float_fields[] = {
        {1, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_STRING, 0,
         offsetof(PgQuery__Float, fval), NULL, &protobuf_c_empty_string, 0}
    };
    static const wi_field function_fields[] = {
        {1, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_MESSAGE, 0,
         offsetof(PgQuery__SQLValueFunction, xpr), &pg_query__node__descriptor, NULL, 0},
        {2, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_ENUM, 0,
         offsetof(PgQuery__SQLValueFunction, op), &pg_query__sqlvalue_function_op__descriptor, NULL, 0},
        {3, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_UINT32, 0,
         offsetof(PgQuery__SQLValueFunction, type), NULL, NULL, 0},
        {4, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_INT32, 0,
         offsetof(PgQuery__SQLValueFunction, typmod), NULL, NULL, 0},
        {5, PROTOBUF_C_LABEL_NONE, PROTOBUF_C_TYPE_INT32, 0,
         offsetof(PgQuery__SQLValueFunction, location), NULL, NULL, 0}
    };
    const ProtobufCFieldDescriptor *f;
    if (!wi_schemas_match() || PG_QUERY__A__CONST__VAL_FVAL != 2 ||
        PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION < 16 || PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION > 2047 ||
        (unsigned)PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION > pg_query__node__descriptor.n_fields ||
        (PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION & 15) == (PG_QUERY__NODE__NODE_A_CONST & 15) ||
        PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIMESTAMP != 4 ||
        !wi_schema(&pg_query__float__descriptor, sizeof(PgQuery__Float),
            float_fields, sizeof(float_fields) / sizeof(float_fields[0])) ||
        !wi_schema(&pg_query__sqlvalue_function__descriptor, sizeof(PgQuery__SQLValueFunction),
            function_fields, sizeof(function_fields) / sizeof(function_fields[0]))) return 0;
    f = &pg_query__node__descriptor.fields[PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION - 1U];
    return f->descriptor == &pg_query__sqlvalue_function__descriptor;
}

/* typmod=-1 has the canonical ten-byte int32 encoding. Keep the exact native
 * bytes so signed conversion and noncanonical encodings cannot slip through. */
static const uint8_t wsi_typmod_default[] = {
    0x20U, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0x01U
};

static const char *wsi_function_word(unsigned op)
{
    switch (op) {
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_DATE: return "current_date";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIME: return "current_time";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIMESTAMP: return "current_timestamp";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_LOCALTIME: return "localtime";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_LOCALTIMESTAMP: return "localtimestamp";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_ROLE: return "current_role";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_USER: return "current_user";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_USER: return "user";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_SESSION_USER: return "session_user";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_CATALOG: return "current_catalog";
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_SCHEMA: return "current_schema";
    default: return NULL; /* Explicit precision and new enum kinds fall back. */
    }
}

static int wsi_cell(wi_reader *list, sqlparser_wire_scalar_cell_t *cell)
{
    wi_reader node, body, value;
    uint32_t number;
    unsigned kind;
    memset(cell, 0, sizeof(*cell));
    if (!wi_message(list, WI_LIST_ITEMS, &node)) return 0;
    if (wi_key_is(&node, PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION, 2U)) {
        const char *word;
        if (!wi_message(&node, PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION, &body) || node.next != node.end ||
            !wi_scalar(&body, 2U, &number) || (word = wsi_function_word(number)) == NULL ||
            (size_t)(body.end - body.next) < sizeof(wsi_typmod_default) ||
            memcmp(body.next, wsi_typmod_default, sizeof(wsi_typmod_default)) != 0) return 0;
        cell->integer = (int32_t)number; /* Function enum, not a literal value. */
        body.next += sizeof(wsi_typmod_default);
        if (!wi_scalar(&body, 5U, &number) || body.next != body.end) return 0;
        cell->kind = SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION;
        cell->location = (int32_t)number;
        cell->length = (uint32_t)strlen(word);
        return 1;
    }
    if (!wi_message(&node, PG_QUERY__NODE__NODE_A_CONST, &body) || node.next != node.end) return 0;
    if (wi_key_is(&body, PG_QUERY__A__CONST__VAL_IVAL, 2U)) kind = PG_QUERY__A__CONST__VAL_IVAL;
    else if (wi_key_is(&body, PG_QUERY__A__CONST__VAL_SVAL, 2U)) kind = PG_QUERY__A__CONST__VAL_SVAL;
    else if (wi_key_is(&body, PG_QUERY__A__CONST__VAL_FVAL, 2U)) kind = PG_QUERY__A__CONST__VAL_FVAL;
    else return 0;
    if (!wi_message(&body, kind, &value)) return 0;
    if (kind == PG_QUERY__A__CONST__VAL_IVAL) {
        if (!wi_optional_scalar(&value, WI_INTEGER_VALUE, &number)) return 0;
        cell->integer = (int32_t)number;
        cell->kind = SQLPARSER_WIRE_SCALAR_INTEGER;
    } else {
        cell->text = (const char *)value.next;
        if (value.next != value.end && !wi_text(&value, 1U, &cell->text, &cell->length)) return 0;
        cell->kind = kind == PG_QUERY__A__CONST__VAL_SVAL ? SQLPARSER_WIRE_SCALAR_STRING : SQLPARSER_WIRE_SCALAR_FLOAT;
        if (cell->kind == SQLPARSER_WIRE_SCALAR_FLOAT && cell->length == 0U) return 0;
    }
    if (value.next != value.end || !wi_scalar(&body, WI_CONST_LOCATION, &number) || body.next != body.end) return 0;
    cell->location = (int32_t)number;
    return 1;
}

static int wsi_row_reader(const sqlparser_wire_scalar_insert_t *insert, size_t row, wi_reader *list)
{
    wi_reader values, node;
    if (insert == NULL || insert->wire == NULL || insert->row_offsets == NULL ||
        row >= insert->row_count || insert->wire_length > PTRDIFF_MAX ||
        insert->row_offsets[row] >= insert->wire_length) return 0;
    values.next = (const uint8_t *)insert->wire + insert->row_offsets[row];
    values.end = (const uint8_t *)insert->wire + insert->wire_length;
    return wi_message(&values, WI_SELECT_VALUES, &node) &&
        wi_message(&node, PG_QUERY__NODE__NODE_LIST, list) && node.next == node.end;
}

static int wsi_source_text(const sqlparser_wire_scalar_insert_t *insert, sqlparser_wire_scalar_cell_t *cell)
{
    if (cell->kind != SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION) return 1;
    if ((size_t)cell->location > insert->sql_length ||
        cell->length > insert->sql_length - (size_t)cell->location) return 0;
    cell->text = insert->sql + cell->location;
    return 1;
}

int sqlparser_wire_scalar_insert_row(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, sqlparser_wire_scalar_cell_t *cells)
{
    wi_reader list;
    size_t column;
    if (cells == NULL || !wsi_row_reader(insert, row, &list)) return 0;
    for (column = 0U; column < insert->column_count; column++)
        if (!wsi_cell(&list, &cells[column]) || !wsi_source_text(insert, &cells[column])) return 0;
    return list.next == list.end;
}

int sqlparser_wire_scalar_insert_cell(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, size_t column, sqlparser_wire_scalar_cell_t *cell)
{
    wi_reader list;
    size_t index;
    if (cell == NULL || insert == NULL || column >= insert->column_count ||
        !wsi_row_reader(insert, row, &list)) return 0;
    for (index = 0U; index <= column; index++) if (!wsi_cell(&list, cell)) return 0;
    return wsi_source_text(insert, cell);
}


/* Proof-reuse decoder. The descriptor is valid only while its owned canonical
 * source and wire remain immutable. Keep the complete row envelope and every
 * cursor/payload bound, but do not re-prove nested tags or signed typmod bytes.
 * Defensive callers and certification continue to use wsi_cell above. */
#ifndef SQLPARSER_WIRE_SCALAR_INSERT_DEFENSIVE_ROWS
#if defined(_MSC_VER)
#define WSI_INLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define WSI_INLINE static inline __attribute__((always_inline))
#else
#define WSI_INLINE static inline
#endif

WSI_INLINE uint32_t wsi_function_length(unsigned op)
{
    switch (op) {
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_DATE: return 12U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIME: return 12U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_TIMESTAMP: return 17U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_LOCALTIME: return 9U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_LOCALTIMESTAMP: return 14U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_ROLE: return 12U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_USER: return 12U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_USER: return 4U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_SESSION_USER: return 12U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_CATALOG: return 15U;
    case PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_CURRENT_SCHEMA: return 14U;
    default: return 0U;
    }
}

WSI_INLINE int wsi_certified_cell(const uint8_t **cursor, const uint8_t *end,
    sqlparser_wire_scalar_cell_t *cell)
{
    const uint8_t *p = *cursor;
    uint32_t length, value;
    unsigned arm;
    int is_function;
    memset(cell, 0, sizeof(*cell));
    if (!wi_certified_envelope(&p, end, 1U) || p == end) return 0;
    /* Both admitted Node arms have two-byte tags with distinct first bytes. */
    is_function = *p == (uint8_t)((((PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION << 3) | 2U) & 0x7fU) | 0x80U);
    if (!wi_certified_envelope(&p, end, 2U) || p == end) return 0;
    if (is_function) {
        p++; /* SQLValueFunction.op, a proved positive enum. */
        if (!wi_certified_u32(&p, end, &value) || value > INT32_MAX ||
            (size_t)(end - p) <= sizeof(wsi_typmod_default)) return 0;
        cell->kind = SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION;
        cell->integer = (int32_t)value;
        cell->length = wsi_function_length(value);
        p += sizeof(wsi_typmod_default); /* Canonical signed typmod=-1. */
    } else {
        arm = *p++;
        if (!wi_certified_u32(&p, end, &length)) return 0;
        if (arm == ((PG_QUERY__A__CONST__VAL_IVAL << 3) | 2U)) {
            cell->kind = SQLPARSER_WIRE_SCALAR_INTEGER;
            if (length != 0U) {
                if (p == end) return 0;
                p++; /* Integer.ival; zero has an empty wrapper. */
                if (!wi_certified_u32(&p, end, &value) || value > INT32_MAX) return 0;
                cell->integer = (int32_t)value;
            }
        } else {
            cell->kind = arm == ((PG_QUERY__A__CONST__VAL_SVAL << 3) | 2U) ?
                SQLPARSER_WIRE_SCALAR_STRING : SQLPARSER_WIRE_SCALAR_FLOAT;
            cell->text = (const char *)p;
            if (length != 0U) {
                if (p == end) return 0;
                p++; /* String.sval or Float.fval, both field one. */
                if (!wi_certified_u32(&p, end, &value) || value > (size_t)(end - p)) return 0;
                cell->text = (const char *)p;
                cell->length = value;
                p += value;
            }
        }
    }
    if (p == end) return 0;
    p++; /* AConst.location or SQLValueFunction.location. */
    if (!wi_certified_u32(&p, end, &value) || value > INT32_MAX) return 0;
    cell->location = (int32_t)value;
    *cursor = p;
    return 1;
}
#undef WSI_INLINE

static int wsi_certified_row_reader(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, wi_reader *list)
{
    const uint8_t *p, *end;
    uint32_t length;
    if (insert == NULL || row >= insert->row_count || insert->wire == NULL ||
        insert->row_offsets == NULL || insert->wire_length > PTRDIFF_MAX ||
        insert->row_offsets[row] >= insert->wire_length) return 0;
    p = (const uint8_t *)insert->wire + insert->row_offsets[row];
    end = (const uint8_t *)insert->wire + insert->wire_length;
    p++; /* Proved SelectStmt.values_lists tag. */
    if (!wi_certified_u32(&p, end, &length) || length > (size_t)(end - p)) return 0;
    end = p + length;
    if (!wi_certified_envelope(&p, end, 2U)) return 0;
    list->next = p;
    list->end = end;
    return 1;
}

#endif /* !SQLPARSER_WIRE_SCALAR_INSERT_DEFENSIVE_ROWS */

static int wsi_read_cell(wi_reader *list, sqlparser_wire_scalar_cell_t *cell, int certified)
{
#ifdef SQLPARSER_WIRE_SCALAR_INSERT_DEFENSIVE_ROWS
    (void)certified;
    return wsi_cell(list, cell);
#else
    return certified ? wsi_certified_cell(&list->next, list->end, cell) : wsi_cell(list, cell);
#endif
}

static int wsi_read_row(const sqlparser_wire_scalar_insert_t *insert, size_t row,
    wi_reader *list, int certified)
{
#ifdef SQLPARSER_WIRE_SCALAR_INSERT_DEFENSIVE_ROWS
    (void)certified;
    return wsi_row_reader(insert, row, list);
#else
    return certified ? wsi_certified_row_reader(insert, row, list) : wsi_row_reader(insert, row, list);
#endif
}

int sqlparser_wire_scalar_insert_certified_row(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, sqlparser_wire_scalar_cell_t *cells)
{
    wi_reader list;
    size_t column;
    if (cells == NULL || !wsi_read_row(insert, row, &list, 1)) return 0;
    for (column = 0U; column < insert->column_count; column++)
        if (!wsi_read_cell(&list, &cells[column], 1) || !wsi_source_text(insert, &cells[column])) return 0;
    return list.next == list.end;
}

int sqlparser_wire_scalar_insert_certified_cell(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, size_t column, sqlparser_wire_scalar_cell_t *cell)
{
    wi_reader list;
    size_t index;
    if (cell == NULL || insert == NULL || column >= insert->column_count ||
        !wsi_read_row(insert, row, &list, 1)) return 0;
    for (index = 0U; index <= column; index++) if (!wsi_read_cell(&list, cell, 1)) return 0;
    return wsi_source_text(insert, cell);
}

static int wsi_source_cell(const char *sql, size_t length, size_t *pos,
    const sqlparser_wire_scalar_cell_t *cell)
{
    size_t start;
    wi_gap(sql, length, pos);
    if (*pos == length || cell->location != (int32_t)*pos) return 0;
    if (cell->kind == SQLPARSER_WIRE_SCALAR_INTEGER) {
        uint32_t value = 0U;
        if (sql[*pos] < '0' || sql[*pos] > '9') return 0;
        do {
            unsigned digit = (unsigned)(sql[(*pos)++] - '0');
            if (value > ((uint32_t)INT32_MAX - digit) / 10U) return 0;
            value = value * 10U + digit;
        } while (*pos < length && sql[*pos] >= '0' && sql[*pos] <= '9');
        return value == (uint32_t)cell->integer;
    }
    if (cell->kind == SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION)
        return wi_word(sql, length, pos, wsi_function_word((unsigned)cell->integer), cell->length);
    start = *pos;
    if (cell->kind == SQLPARSER_WIRE_SCALAR_STRING) {
        if (sql[(*pos)++] != '\'') return 0;
        start = *pos;
        while (*pos < length && sql[*pos] != '\'') {
            unsigned char c = (unsigned char)sql[(*pos)++];
            /* Grammar validates encoding; these bytes need no unescaping. */
            if (c < 0x20U || c == 0x7fU || c == '\\') return 0;
        }
        if (*pos == length || *pos - start != cell->length ||
            memcmp(sql + start, cell->text, cell->length) != 0) return 0;
        ++*pos;
        return 1;
    }
    if (cell->kind != SQLPARSER_WIRE_SCALAR_FLOAT || cell->length > length - *pos ||
        memcmp(sql + *pos, cell->text, cell->length) != 0) return 0;
    /* Preserve precisely the native Float spelling. Signs/parentheses or
     * lexical variants outside one simple numeric token use generic code. */
    while (*pos < start + cell->length) {
        unsigned char c = (unsigned char)sql[(*pos)++];
        if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) return 0;
    }
    return sql[start] >= '0' && sql[start] <= '9';
}

/* The public parser retains original unquoted identifier spelling. Keywords
 * are case-insensitive, but graph names must match these exact owned bytes. */
static int wsi_name_word(const char *sql, size_t length, size_t *pos,
    const char *name, size_t name_length)
{
    size_t start;
    wi_gap(sql, length, pos);
    start = *pos;
    if (start == length || !wi_identifier_char((unsigned char)sql[start], 1)) return 0;
    while (*pos < length && wi_identifier_char((unsigned char)sql[*pos], 0)) ++*pos;
    return *pos - start == name_length && memcmp(sql + start, name, name_length) == 0;
}

static int wsi_add_text(sqlparser_wire_scalar_insert_t *d, size_t length)
{
    if (length >= SIZE_MAX - d->text_bytes) return 0;
    d->text_bytes += length + 1U;
    return 1;
}

void sqlparser_wire_scalar_insert_destroy(sqlparser_wire_scalar_insert_t *insert)
{
    if (insert != NULL) {
        free(insert->names);
        free(insert->row_offsets);
        free(insert);
    }
}

sqlparser_wire_scalar_insert_t *sqlparser_wire_scalar_insert_certify(const sqlparser_handle_t *handle)
{
    sqlparser_wire_scalar_insert_t description = {0}, *result = NULL;
    wi_reader parse, raw, node, insert, relation, columns, column, target, select_node, values, rows;
    const char *sql;
    const uint8_t *base;
    size_t length, pos = 0U, index, row;
    uint32_t location;
    if (!sqlparser_dialect_supports_plain_scalar_insert(handle) || handle->failed ||
        handle->sql == NULL || handle->parser_sql == NULL || handle->ast != NULL ||
        (handle->generation != 0UL && !handle->surface_source_complete) ||
        handle->current_sql != NULL || handle->current_parser_sql != NULL || handle->control != NULL ||
        handle->statement_count != 1U || handle->sql_len != handle->parser_sql_len ||
        handle->sql_len < 192U || handle->sql_len > INT32_MAX || handle->sql_len > PTRDIFF_MAX ||
        handle->parse_tree.data == NULL || handle->parse_tree.len == 0U ||
        handle->parse_tree.len > UINT32_MAX || handle->parse_tree.len > PTRDIFF_MAX ||
        handle->patch_batch_flags != 0U || handle->surface_source_edits.count != 0U ||
        handle->surface_source_edits.items != NULL || handle->identifier_mutation_count != 0U ||
        handle->identifier_mutations != NULL || handle->identifier_mutation_capacity != 0U ||
        handle->identifier_spelling_count != 0U || handle->identifier_spellings != NULL ||
        handle->identifier_spelling_capacity != 0U || handle->identifier_spelling_status != SQLPARSER_STATUS_OK) return NULL;
    sql = handle->sql;
    length = handle->sql_len;
    if (memcmp(sql, handle->parser_sql, length + 1U) != 0 || sql[length] != '\0') return NULL;
    description.wire = handle->parse_tree.data;
    description.wire_length = handle->parse_tree.len;
    description.sql = sql;
    description.sql_length = length;
    base = (const uint8_t *)description.wire;
    parse.next = base;
    parse.end = base + description.wire_length;
    if (!wi_optional_scalar(&parse, WI_PARSE_VERSION, &description.version) ||
        !wi_message(&parse, WI_PARSE_STMTS, &raw) || parse.next != parse.end ||
        !wi_message(&raw, WI_RAW_STMT, &node) ||
        !wi_optional_scalar(&raw, WI_RAW_LENGTH, &description.raw_length) || raw.next != raw.end ||
        !wi_message(&node, PG_QUERY__NODE__NODE_INSERT_STMT, &insert) || node.next != node.end ||
        !wi_word(sql, length, &pos, "insert", 6U) || !wi_word(sql, length, &pos, "into", 4U)) return NULL;
    description.prefix_offset = (uint32_t)(insert.next - base);
    if (!wi_message(&insert, WI_INSERT_RELATION, &relation)) return NULL;
    columns = insert;
    while (wi_key_is(&insert, WI_INSERT_COLS, 2U)) {
        if (!wi_message(&insert, WI_INSERT_COLS, &column)) return NULL;
        description.column_count++;
    }
    if (description.column_count == 0U || description.column_count > (SIZE_MAX / sizeof(*description.names)) - 3U) return NULL;
    description.prefix_length = (uint32_t)(insert.next - base) - description.prefix_offset;
    if (!wi_message(&insert, WI_INSERT_SELECT, &select_node) || !wi_enum(&insert, WI_INSERT_OVERRIDE, 1U) ||
        insert.next != insert.end || !wi_message(&select_node, PG_QUERY__NODE__NODE_SELECT_STMT, &values) ||
        select_node.next != select_node.end) return NULL;
    description.names = calloc(description.column_count + 3U, sizeof(*description.names));
    if (description.names == NULL) return NULL;
    for (index = 0U; index < 3U; index++) {
        sqlparser_wire_scalar_name_t *name = &description.names[index];
        if (wi_key_is(&relation, (unsigned)index + 1U, 2U) &&
            !wi_text(&relation, (unsigned)index + 1U, &name->text, &name->length)) goto miss;
    }
    if (description.names[2].text == NULL ||
        (description.names[0].text != NULL && description.names[1].text == NULL) ||
        !wi_enum(&relation, 4U, 1U) || !wi_message(&relation, 5U, &node) ||
        node.end - node.next != 1 || *node.next != 'p' ||
        !wi_scalar(&relation, 7U, &location) || relation.next != relation.end) goto miss;
    wi_gap(sql, length, &pos);
    if (location != pos) goto miss;
    for (index = 0U; index < 3U; index++) {
        sqlparser_wire_scalar_name_t *name = &description.names[index];
        if (name->text == NULL) continue;
        if (!wsi_name_word(sql, length, &pos, name->text, name->length) || !wsi_add_text(&description, name->length) ||
            (index < 2U && !wi_punctuation(sql, length, &pos, '.'))) goto miss;
    }
    if (!wi_punctuation(sql, length, &pos, '(')) goto miss;
    for (index = 0U; index < description.column_count; index++) {
        sqlparser_wire_scalar_name_t *name = &description.names[index + 3U];
        if (!wi_message(&columns, WI_INSERT_COLS, &column) ||
            !wi_message(&column, PG_QUERY__NODE__NODE_RES_TARGET, &target) || column.next != column.end ||
            !wi_text(&target, 1U, &name->text, &name->length) ||
            !wi_scalar(&target, 4U, &location) || target.next != target.end) goto miss;
        wi_gap(sql, length, &pos);
        if (location != pos || !wsi_name_word(sql, length, &pos, name->text, name->length) ||
            !wsi_add_text(&description, name->length) ||
            !wi_punctuation(sql, length, &pos, index + 1U == description.column_count ? ')' : ',')) goto miss;
    }
    if (!wi_word(sql, length, &pos, "values", 6U)) goto miss;
    rows = values;
    while (wi_key_is(&values, WI_SELECT_VALUES, 2U)) {
        wi_reader list;
        if (!wi_message(&values, WI_SELECT_VALUES, &node) ||
            !wi_message(&node, PG_QUERY__NODE__NODE_LIST, &list) || node.next != node.end ||
            (description.row_count != 0U && !wi_punctuation(sql, length, &pos, ',')) ||
            !wi_punctuation(sql, length, &pos, '(')) goto miss;
        for (index = 0U; index < description.column_count; index++) {
            sqlparser_wire_scalar_cell_t cell;
            if (!wsi_cell(&list, &cell) || !wsi_source_cell(sql, length, &pos, &cell) ||
                !wi_punctuation(sql, length, &pos, index + 1U == description.column_count ? ')' : ',')) goto miss;
            if (cell.kind != SQLPARSER_WIRE_SCALAR_INTEGER && !wsi_add_text(&description, cell.length)) goto miss;
            if (cell.kind == SQLPARSER_WIRE_SCALAR_STRING) description.string_count++;
        }
        if (list.next != list.end || description.row_count == UINT32_MAX) goto miss;
        description.row_count++;
    }
    if (description.row_count < 32U || !wi_enum(&values, WI_SELECT_LIMIT, 1U) ||
        !wi_enum(&values, WI_SELECT_OP, 1U) || values.next != values.end) goto miss;
    wi_gap(sql, length, &pos);
    if (pos < length && sql[pos] == ';') {
        if (description.raw_length != pos) goto miss;
        pos++;
        wi_gap(sql, length, &pos);
    } else if (description.raw_length != 0U) goto miss;
    if (pos != length || !sqlparser_dialect_state_is_plain_insert_strings(handle, description.string_count) ||
        !wsi_schemas_match()) goto miss;
    /* Certificate + graph-owned text are bounded by 32 bytes per cell.
     * Long-string and large-name outliers keep the generic implementation. */
    if (description.row_count > SIZE_MAX / description.column_count ||
        description.row_count > SIZE_MAX / sizeof(*description.row_offsets)) goto miss;
    {
        uint64_t cells = (uint64_t)description.row_count * description.column_count;
        uint64_t retained = (uint64_t)description.text_bytes + sizeof(description) +
            (uint64_t)description.row_count * sizeof(*description.row_offsets) +
            (uint64_t)(description.column_count + 3U) * sizeof(*description.names);
        if (cells > UINT64_MAX / 32U || retained > cells * 32U) goto miss;
    }
    result = malloc(sizeof(*result));
    if (result == NULL) goto miss;
    *result = description;
    result->row_offsets = malloc(description.row_count * sizeof(*result->row_offsets));
    if (result->row_offsets == NULL) { free(result); goto miss; }
    for (row = 0U; row < result->row_count; row++) {
        result->row_offsets[row] = (uint32_t)(rows.next - base);
        if (!wi_message(&rows, WI_SELECT_VALUES, &node)) {
            sqlparser_wire_scalar_insert_destroy(result);
            return NULL;
        }
    }
    return result;
miss:
    free(description.names);
    return NULL;
}

/* Initial native attestation is deliberately separate from strict admission. */
sqlparser_wire_scalar_insert_t *sqlparser_wire_scalar_insert_from_native(const sqlparser_handle_t *handle)
{
    sqlparser_wire_scalar_insert_t description = {0}, *result = NULL;
    wi_reader parse, raw, node, insert, relation, columns, column, target, select_node, values;
    const sqlparser_native_scalar_provenance_t *provenance;
    const PgQueryNativeScalarInsertProof *proof;
    const char *sql;
    const uint8_t *base;
    size_t length, pos = 0U, index, row;
    uint32_t location;
    /* This entry is not a verifier for caller-supplied or mutated protobuf.
     * Only the owned immutable output of the exact initial native constructor
     * and canonical serializer may reuse its semantic/source attestation. */
    if (handle == NULL || handle->dialect_ops == NULL ||
        !handle->dialect_ops->plain_scalar_native_validation || handle->generation != 0UL ||
        (provenance = handle->native_scalar_provenance) == NULL ||
        provenance->sql != handle->sql || handle->parser_sql != handle->sql ||
        provenance->wire != handle->parse_tree.data ||
        provenance->wire_length != handle->parse_tree.len) return NULL;
    proof = &provenance->proof;
    if (proof->source_length != handle->sql_len || proof->row_count < 32U ||
        proof->row_count > UINT32_MAX || proof->column_count == 0U ||
        proof->statement_length < 0 || proof->row_count > SIZE_MAX / proof->column_count ||
        proof->row_count > SIZE_MAX / sizeof(*description.row_offsets) ||
        proof->column_count > (SIZE_MAX / sizeof(*description.names)) - 3U ||
        proof->row_count * proof->column_count > proof->source_length ||
        proof->string_count > proof->row_count * proof->column_count ||
        proof->text_bytes < proof->string_count || proof->text_bytes > proof->source_length ||
        !wsi_schemas_match()) return NULL;
    if (!sqlparser_dialect_supports_plain_scalar_insert(handle) || handle->failed ||
        handle->sql == NULL || handle->parser_sql == NULL || handle->ast != NULL ||
        (handle->generation != 0UL && !handle->surface_source_complete) ||
        handle->current_sql != NULL || handle->current_parser_sql != NULL || handle->control != NULL ||
        handle->statement_count != 1U || handle->sql_len != handle->parser_sql_len ||
        handle->sql_len < 192U || handle->sql_len > INT32_MAX || handle->sql_len > PTRDIFF_MAX ||
        handle->parse_tree.data == NULL || handle->parse_tree.len == 0U ||
        handle->parse_tree.len > UINT32_MAX || handle->parse_tree.len > PTRDIFF_MAX ||
        handle->patch_batch_flags != 0U || handle->surface_source_edits.count != 0U ||
        handle->surface_source_edits.items != NULL || handle->identifier_mutation_count != 0U ||
        handle->identifier_mutations != NULL || handle->identifier_mutation_capacity != 0U ||
        handle->identifier_spelling_count != 0U || handle->identifier_spellings != NULL ||
        handle->identifier_spelling_capacity != 0U || handle->identifier_spelling_status != SQLPARSER_STATUS_OK) return NULL;
    sql = handle->sql;
    length = handle->sql_len;
    if (sql[length] != '\0') return NULL;
    description.wire = handle->parse_tree.data;
    description.wire_length = handle->parse_tree.len;
    description.sql = sql;
    description.sql_length = length;
    base = (const uint8_t *)description.wire;
    parse.next = base;
    parse.end = base + description.wire_length;
    if (!wi_optional_scalar(&parse, WI_PARSE_VERSION, &description.version) ||
        !wi_message(&parse, WI_PARSE_STMTS, &raw) || parse.next != parse.end ||
        !wi_message(&raw, WI_RAW_STMT, &node) ||
        !wi_optional_scalar(&raw, WI_RAW_LENGTH, &description.raw_length) || raw.next != raw.end ||
        !wi_message(&node, PG_QUERY__NODE__NODE_INSERT_STMT, &insert) || node.next != node.end ||
        !wi_word(sql, length, &pos, "insert", 6U) || !wi_word(sql, length, &pos, "into", 4U)) return NULL;
    description.prefix_offset = (uint32_t)(insert.next - base);
    if (!wi_message(&insert, WI_INSERT_RELATION, &relation)) return NULL;
    columns = insert;
    while (wi_key_is(&insert, WI_INSERT_COLS, 2U)) {
        if (!wi_message(&insert, WI_INSERT_COLS, &column)) return NULL;
        description.column_count++;
    }
    if (description.column_count != proof->column_count ||
        description.raw_length != (uint32_t)proof->statement_length) return NULL;
    description.prefix_length = (uint32_t)(insert.next - base) - description.prefix_offset;
    if (!wi_message(&insert, WI_INSERT_SELECT, &select_node) || !wi_enum(&insert, WI_INSERT_OVERRIDE, 1U) ||
        insert.next != insert.end || !wi_message(&select_node, PG_QUERY__NODE__NODE_SELECT_STMT, &values) ||
        select_node.next != select_node.end) return NULL;
    description.names = calloc(description.column_count + 3U, sizeof(*description.names));
    if (description.names == NULL) return NULL;
    for (index = 0U; index < 3U; index++) {
        sqlparser_wire_scalar_name_t *name = &description.names[index];
        if (wi_key_is(&relation, (unsigned)index + 1U, 2U) &&
            !wi_text(&relation, (unsigned)index + 1U, &name->text, &name->length)) goto miss;
    }
    if (description.names[2].text == NULL ||
        (description.names[0].text != NULL && description.names[1].text == NULL) ||
        !wi_enum(&relation, 4U, 1U) || !wi_message(&relation, 5U, &node) ||
        node.end - node.next != 1 || *node.next != 'p' ||
        !wi_scalar(&relation, 7U, &location) || relation.next != relation.end) goto miss;
    wi_gap(sql, length, &pos);
    if (location != pos) goto miss;
    for (index = 0U; index < 3U; index++) {
        sqlparser_wire_scalar_name_t *name = &description.names[index];
        if (name->text == NULL) continue;
        if (!wsi_name_word(sql, length, &pos, name->text, name->length) || !wsi_add_text(&description, name->length) ||
            (index < 2U && !wi_punctuation(sql, length, &pos, '.'))) goto miss;
    }
    if (!wi_punctuation(sql, length, &pos, '(')) goto miss;
    for (index = 0U; index < description.column_count; index++) {
        sqlparser_wire_scalar_name_t *name = &description.names[index + 3U];
        if (!wi_message(&columns, WI_INSERT_COLS, &column) ||
            !wi_message(&column, PG_QUERY__NODE__NODE_RES_TARGET, &target) || column.next != column.end ||
            !wi_text(&target, 1U, &name->text, &name->length) ||
            !wi_scalar(&target, 4U, &location) || target.next != target.end) goto miss;
        wi_gap(sql, length, &pos);
        if (location != pos || !wsi_name_word(sql, length, &pos, name->text, name->length) ||
            !wsi_add_text(&description, name->length) ||
            !wi_punctuation(sql, length, &pos, index + 1U == description.column_count ? ')' : ',')) goto miss;
    }
    if (!wi_word(sql, length, &pos, "values", 6U)) goto miss;
    /* Text and string totals were accumulated while the native recognizer
     * proved every cell. Retain the graph storage budget, including the small
     * optional provenance record, before allocating the index. */
    if (description.text_bytes > proof->text_bytes ||
        !sqlparser_dialect_state_is_plain_insert_strings(handle, proof->string_count)) goto miss;
    description.row_count = proof->row_count;
    description.string_count = proof->string_count;
    description.text_bytes = proof->text_bytes;
    {
        uint64_t cells = (uint64_t)description.row_count * description.column_count;
        uint64_t retained = (uint64_t)description.text_bytes + sizeof(description) + sizeof(*provenance) +
            (uint64_t)description.row_count * sizeof(*description.row_offsets) +
            (uint64_t)(description.column_count + 3U) * sizeof(*description.names);
        if (cells > UINT64_MAX / 32U || retained > cells * 32U) goto miss;
    }
    result = malloc(sizeof(*result));
    if (result == NULL) goto miss;
    *result = description;
    result->row_offsets = malloc(description.row_count * sizeof(*result->row_offsets));
    if (result->row_offsets == NULL) { free(result); goto miss; }
    for (row = 0U; row < result->row_count; row++) {
        wi_reader list;
        result->row_offsets[row] = (uint32_t)(values.next - base);
        /* Exact outer tags, canonical bounded lengths, one List per row,
         * exact row count, and the final SelectStmt tail remain mandatory.
         * Per-cell tags/values/source agreement were proved by construction;
         * graph materialization still uses the bounded certified decoder. */
        if (!wi_message(&values, WI_SELECT_VALUES, &node) ||
            !wi_message(&node, PG_QUERY__NODE__NODE_LIST, &list) || node.next != node.end ||
            list.next == list.end) {
            sqlparser_wire_scalar_insert_destroy(result);
            return NULL;
        }
    }
    if (!wi_enum(&values, WI_SELECT_LIMIT, 1U) ||
        !wi_enum(&values, WI_SELECT_OP, 1U) || values.next != values.end) {
        sqlparser_wire_scalar_insert_destroy(result);
        return NULL;
    }
    return result;
miss:
    free(description.names);
    return NULL;
}

static unsigned wsi_constant_kind(const sqlparser_wire_scalar_cell_t *cell)
{
    return cell->kind == SQLPARSER_WIRE_SCALAR_INTEGER ? PG_QUERY__A__CONST__VAL_IVAL :
        cell->kind == SQLPARSER_WIRE_SCALAR_STRING ? PG_QUERY__A__CONST__VAL_SVAL : PG_QUERY__A__CONST__VAL_FVAL;
}

static int wsi_measure_cell(const sqlparser_wire_scalar_cell_t *cell, wi_cell_sizes *sizes)
{
    uint64_t n;
    if (cell->location < 0) return 0;
    if (cell->kind == SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION) {
        if (wsi_function_word((unsigned)cell->integer) == NULL) return 0;
        n = wi_scalar_size(2U, (uint32_t)cell->integer) + sizeof(wsi_typmod_default) +
            wi_scalar_size(5U, (uint32_t)cell->location);
        sizes->value = 0U;
        sizes->constant = (uint32_t)n;
        n = wi_envelope(PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION, sizes->constant);
    } else {
        if (cell->kind == SQLPARSER_WIRE_SCALAR_INTEGER) {
            if (cell->integer < 0) return 0;
            n = wi_scalar_size(1U, (uint32_t)cell->integer);
        } else if (cell->kind == SQLPARSER_WIRE_SCALAR_STRING || cell->kind == SQLPARSER_WIRE_SCALAR_FLOAT) {
            n = cell->length == 0U ? 0U : wi_envelope(1U, cell->length);
        } else return 0;
        if (n > UINT32_MAX) return 0;
        sizes->value = (uint32_t)n;
        n = wi_envelope(wsi_constant_kind(cell), sizes->value) + wi_scalar_size(WI_CONST_LOCATION, (uint32_t)cell->location);
        if (n > UINT32_MAX) return 0;
        sizes->constant = (uint32_t)n;
        n = wi_envelope(PG_QUERY__NODE__NODE_A_CONST, sizes->constant);
    }
    if (n > UINT32_MAX) return 0;
    sizes->node = (uint32_t)n;
    return 1;
}

static int wsi_effective_cell(sqlparser_wire_scalar_cell_t *cell,
    const sqlparser_surface_source_edits_t *edits, size_t *edit_index, int64_t *delta)
{
    size_t old_location = (size_t)cell->location;
    int64_t location = (int64_t)cell->location + *delta;
    if (location < 0 || location > INT32_MAX) return 0;
    cell->location = (int32_t)location;
    if (*edit_index < edits->count) {
        const sqlparser_surface_source_edit_t *edit = &edits->items[*edit_index];
        if (edit->source_start < old_location) return 0;
        if (edit->source_start == old_location) {
            if (cell->kind != SQLPARSER_WIRE_SCALAR_STRING ||
                (uint64_t)old_location + cell->length + 2U != edit->source_end) return 0;
            *delta += (int64_t)edit->replacement_length - ((int64_t)cell->length + 2);
            cell->text = edit->replacement + 1U;
            cell->length = (uint32_t)(edit->replacement_length - 2U);
            ++*edit_index;
        }
    }
    return cell->kind != SQLPARSER_WIRE_SCALAR_STRING ||
        (uint64_t)(uint32_t)cell->location + cell->length + 2U <= INT32_MAX;
}

static void wsi_write_cell(wi_writer *w, const sqlparser_wire_scalar_cell_t *cell, const wi_cell_sizes *sizes)
{
    wi_write_header(w, WI_LIST_ITEMS, sizes->node);
    if (cell->kind == SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION) {
        wi_write_header(w, PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION, sizes->constant);
        wi_write_scalar(w, 2U, (uint32_t)cell->integer);
        wi_write_bytes(w, (const char *)wsi_typmod_default, sizeof(wsi_typmod_default));
        wi_write_scalar(w, 5U, (uint32_t)cell->location);
    } else {
        wi_write_header(w, PG_QUERY__NODE__NODE_A_CONST, sizes->constant);
        wi_write_header(w, wsi_constant_kind(cell), sizes->value);
        if (cell->kind == SQLPARSER_WIRE_SCALAR_INTEGER) wi_write_scalar(w, 1U, (uint32_t)cell->integer);
        else if (cell->length != 0U) {
            wi_write_header(w, 1U, cell->length);
            wi_write_bytes(w, cell->text, cell->length);
        }
        wi_write_scalar(w, WI_CONST_LOCATION, (uint32_t)cell->location);
    }
}

static sqlparser_status_t wsi_pack(const sqlparser_wire_scalar_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out, int replacements_proven)
{
    uint32_t *row_sizes = NULL;
    uint32_t select_size, select_node_size, insert_size, insert_node_size, raw_size, total_size, raw_length;
    uint64_t n;
    int64_t delta = 0;
    size_t row, column, edit_index = 0U;
    wi_writer writer;
    if (out == NULL) return SQLPARSER_STATUS_INVALID_ARGUMENT;
    out->data = NULL;
    out->len = 0U;
    if (insert == NULL || edits == NULL) return SQLPARSER_STATUS_INVALID_ARGUMENT;
    if (insert->wire == NULL || insert->wire_length > UINT32_MAX || insert->wire_length > PTRDIFF_MAX ||
        insert->row_count < 32U || insert->row_count > UINT32_MAX || insert->row_offsets == NULL ||
        insert->column_count == 0U || insert->column_count > SIZE_MAX / insert->row_count ||
        insert->row_count > SIZE_MAX / sizeof(*row_sizes) || insert->version > INT32_MAX ||
        insert->raw_length > INT32_MAX || insert->prefix_offset > insert->wire_length ||
        insert->prefix_length > insert->wire_length - insert->prefix_offset ||
        !wi_edits(edits, replacements_proven)) return SQLPARSER_STATUS_UNSUPPORTED;
    row_sizes = malloc(insert->row_count * sizeof(*row_sizes));
    if (row_sizes == NULL) return SQLPARSER_STATUS_UNSUPPORTED;
    n = 0U;
    for (row = 0U; row < insert->row_count; row++) {
        wi_reader list;
        uint64_t row_size = 0U, row_node_size;
        if (!wsi_read_row(insert, row, &list, replacements_proven)) goto unsupported;
        for (column = 0U; column < insert->column_count; column++) {
            sqlparser_wire_scalar_cell_t cell;
            wi_cell_sizes sizes;
            if (!wsi_read_cell(&list, &cell, replacements_proven) || !wsi_effective_cell(&cell, edits, &edit_index, &delta) ||
                !wsi_measure_cell(&cell, &sizes)) goto unsupported;
            row_size += wi_envelope(WI_LIST_ITEMS, sizes.node);
            if (row_size > UINT32_MAX) goto unsupported;
        }
        if (list.next != list.end) goto unsupported;
        row_sizes[row] = (uint32_t)row_size;
        row_node_size = wi_envelope(PG_QUERY__NODE__NODE_LIST, row_sizes[row]);
        if (row_node_size > UINT32_MAX) goto unsupported;
        n += wi_envelope(WI_SELECT_VALUES, (uint32_t)row_node_size);
        if (n > UINT32_MAX) goto unsupported;
    }
    if (edit_index != edits->count || (int64_t)insert->sql_length + delta < 0 ||
        (int64_t)insert->sql_length + delta > INT32_MAX) goto unsupported;
    raw_length = insert->raw_length;
    if (raw_length != 0U) {
        int64_t shifted_length = (int64_t)raw_length + delta;
        if (shifted_length <= 0 || shifted_length > INT32_MAX) goto unsupported;
        raw_length = (uint32_t)shifted_length;
    }
    n += wi_scalar_size(WI_SELECT_LIMIT, 1U) + wi_scalar_size(WI_SELECT_OP, 1U);
    if (n > UINT32_MAX) goto unsupported;
    select_size = (uint32_t)n;
    n = wi_envelope(PG_QUERY__NODE__NODE_SELECT_STMT, select_size);
    if (n > UINT32_MAX) goto unsupported;
    select_node_size = (uint32_t)n;
    n = insert->prefix_length + wi_envelope(WI_INSERT_SELECT, select_node_size) + wi_scalar_size(WI_INSERT_OVERRIDE, 1U);
    if (n > UINT32_MAX) goto unsupported;
    insert_size = (uint32_t)n;
    n = wi_envelope(PG_QUERY__NODE__NODE_INSERT_STMT, insert_size);
    if (n > UINT32_MAX) goto unsupported;
    insert_node_size = (uint32_t)n;
    n = wi_envelope(WI_RAW_STMT, insert_node_size) + wi_scalar_size(WI_RAW_LENGTH, raw_length);
    if (n > UINT32_MAX) goto unsupported;
    raw_size = (uint32_t)n;
    n = wi_scalar_size(WI_PARSE_VERSION, insert->version) + wi_envelope(WI_PARSE_STMTS, raw_size);
    if (n > UINT32_MAX || n > SIZE_MAX || n > PTRDIFF_MAX) goto unsupported;
    total_size = (uint32_t)n;
    out->data = malloc(total_size);
    if (out->data == NULL) { free(row_sizes); return SQLPARSER_STATUS_NO_MEMORY; }
    writer.next = (uint8_t *)out->data;
    writer.end = writer.next + total_size;
    writer.failed = 0;
    wi_write_scalar(&writer, WI_PARSE_VERSION, insert->version);
    wi_write_header(&writer, WI_PARSE_STMTS, raw_size);
    wi_write_header(&writer, WI_RAW_STMT, insert_node_size);
    wi_write_header(&writer, PG_QUERY__NODE__NODE_INSERT_STMT, insert_size);
    wi_write_bytes(&writer, insert->wire + insert->prefix_offset, insert->prefix_length);
    wi_write_header(&writer, WI_INSERT_SELECT, select_node_size);
    wi_write_header(&writer, PG_QUERY__NODE__NODE_SELECT_STMT, select_size);
    edit_index = 0U;
    delta = 0;
    for (row = 0U; row < insert->row_count; row++) {
        wi_reader list;
        if (!wsi_read_row(insert, row, &list, replacements_proven)) { writer.failed = 1; break; }
        wi_write_header(&writer, WI_SELECT_VALUES, (uint32_t)wi_envelope(PG_QUERY__NODE__NODE_LIST, row_sizes[row]));
        wi_write_header(&writer, PG_QUERY__NODE__NODE_LIST, row_sizes[row]);
        for (column = 0U; column < insert->column_count; column++) {
            sqlparser_wire_scalar_cell_t cell;
            wi_cell_sizes sizes;
            if (!wsi_read_cell(&list, &cell, replacements_proven) || !wsi_effective_cell(&cell, edits, &edit_index, &delta) ||
                !wsi_measure_cell(&cell, &sizes)) { writer.failed = 1; break; }
            wsi_write_cell(&writer, &cell, &sizes);
        }
        if (writer.failed || list.next != list.end) { writer.failed = 1; break; }
    }
    wi_write_scalar(&writer, WI_SELECT_LIMIT, 1U);
    wi_write_scalar(&writer, WI_SELECT_OP, 1U);
    wi_write_scalar(&writer, WI_INSERT_OVERRIDE, 1U);
    wi_write_scalar(&writer, WI_RAW_LENGTH, raw_length);
    free(row_sizes);
    if (writer.failed || writer.next != writer.end || edit_index != edits->count) {
        free(out->data);
        out->data = NULL;
        return SQLPARSER_STATUS_INTERNAL_ERROR;
    }
    out->len = total_size;
    return SQLPARSER_STATUS_OK;
unsupported:
    free(row_sizes);
    return SQLPARSER_STATUS_UNSUPPORTED;
}

sqlparser_status_t sqlparser_wire_scalar_insert_pack(const sqlparser_wire_scalar_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out)
{
    return wsi_pack(insert, edits, out, 0);
}

sqlparser_status_t sqlparser_wire_scalar_insert_pack_proven_edits(const sqlparser_wire_scalar_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out)
{
    return wsi_pack(insert, edits, out, 1);
}
