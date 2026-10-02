/* Differential coverage for descriptor-gated packing and strict wire shortcuts.
 * Descriptor copies (including child references) force the generic runtime. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protobuf/pg_query.pb-c.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const ProtobufCMessageDescriptor *const descriptors[] = {
    &pg_query__node__descriptor, &pg_query__string__descriptor,
    &pg_query__a__const__descriptor, &pg_query__column_ref__descriptor,
    &pg_query__res_target__descriptor, &pg_query__integer__descriptor,
    &pg_query__param_ref__descriptor, &pg_query__float__descriptor,
    &pg_query__boolean__descriptor, &pg_query__bit_string__descriptor
};
static ProtobufCMessageDescriptor generic[COUNT(descriptors)];
static ProtobufCFieldDescriptor *generic_fields[COUNT(descriptors)];
typedef struct { size_t calls, live, fail; } allocation_state;
static void *count_alloc(void *data, size_t size) {
    allocation_state *s = data;
    void *p;
    if (++s->calls == s->fail) return NULL;
    p = malloc(size ? size : 1);
    if (p) s->live++;
    return p;
}
static void count_free(void *data, void *p) {
    allocation_state *s = data;
    if (p) { CHECK(s->live > 0); s->live--; free(p); }
}
static const ProtobufCMessageDescriptor *generic_descriptor(const ProtobufCMessageDescriptor *d) {
    size_t i;
    for (i = 0; i < COUNT(descriptors); i++) if (d == descriptors[i]) return &generic[i];
    return d;
}
static void init_generic(void) {
    size_t i;
    unsigned j;
    for (i = 0; i < COUNT(descriptors); i++) {
        generic[i] = *descriptors[i];
        generic[i].message_init = NULL;
        generic_fields[i] = malloc(descriptors[i]->n_fields * sizeof(*generic_fields[i]));
        CHECK(generic_fields[i]);
        memcpy(generic_fields[i], descriptors[i]->fields, descriptors[i]->n_fields * sizeof(*generic_fields[i]));
        generic[i].fields = generic_fields[i];
    }
    for (i = 0; i < COUNT(descriptors); i++) {
        for (j = 0; j < generic[i].n_fields; j++) {
            ProtobufCFieldDescriptor *f = &generic_fields[i][j];
            if (f->type == PROTOBUF_C_TYPE_MESSAGE) f->descriptor = generic_descriptor(f->descriptor);
        }
    }
}
static void same_encoding(const ProtobufCMessage *a, const ProtobufCMessage *b) {
    size_t n = protobuf_c_message_get_packed_size(a), m = protobuf_c_message_get_packed_size(b);
    uint8_t *wa = malloc(n + 1), *wb = malloc(m + 1), sa[7], sb[7];
    ProtobufCBufferSimple ba = PROTOBUF_C_BUFFER_SIMPLE_INIT(sa);
    ProtobufCBufferSimple bb = PROTOBUF_C_BUFFER_SIMPLE_INIT(sb);
    CHECK(wa && wb && n == m);
    wa[n] = wb[m] = 0xa5;
    CHECK(protobuf_c_message_pack(a, wa) == n && wa[n] == 0xa5);
    CHECK(protobuf_c_message_pack(b, wb) == m && wb[m] == 0xa5);
    CHECK(memcmp(wa, wb, n) == 0);
    CHECK(protobuf_c_message_pack_to_buffer(a, &ba.base) == n);
    CHECK(protobuf_c_message_pack_to_buffer(b, &bb.base) == n);
    CHECK(ba.len == n && bb.len == n);
    CHECK(memcmp(wa, ba.data, n) == 0 && memcmp(wa, bb.data, n) == 0);
    PROTOBUF_C_BUFFER_SIMPLE_CLEAR(&ba);
    PROTOBUF_C_BUFFER_SIMPLE_CLEAR(&bb);
    free(wa); free(wb);
}
static void probe(const ProtobufCMessageDescriptor *d, const uint8_t *wire, size_t n, int fail_allocations) {
    allocation_state sa = {0}, sb = {0};
    ProtobufCAllocator aa = {count_alloc, count_free, &sa}, ab = {count_alloc, count_free, &sb};
    ProtobufCMessage *a = protobuf_c_message_unpack(d, &aa, n, wire);
    ProtobufCMessage *b = protobuf_c_message_unpack(generic_descriptor(d), &ab, n, wire);
    size_t ca, cb, f;
    CHECK((a != NULL) == (b != NULL));
    if (a) {
        CHECK(a->descriptor == d && b->descriptor == generic_descriptor(d));
        same_encoding(a, b);
    }
    protobuf_c_message_free_unpacked(a, &aa);
    protobuf_c_message_free_unpacked(b, &ab);
    CHECK(sa.live == 0 && sb.live == 0);
    ca = sa.calls; cb = sb.calls;
    if (!fail_allocations) return;
    for (f = 1; f <= ca; f++) {
        memset(&sa, 0, sizeof(sa)); sa.fail = f;
        a = protobuf_c_message_unpack(d, &aa, n, wire);
        CHECK(a == NULL && sa.live == 0);
    }
    for (f = 1; f <= cb; f++) {
        memset(&sb, 0, sizeof(sb)); sb.fail = f;
        b = protobuf_c_message_unpack(generic_descriptor(d), &ab, n, wire);
        CHECK(b == NULL && sb.live == 0);
    }
}
static void check_message(ProtobufCMessage *m) {
    ProtobufCMessage *g = malloc(m->descriptor->sizeof_message);
    size_t n;
    uint8_t *wire;
    uint8_t unknown_data[] = {0x81, 0x00};
    ProtobufCMessageUnknownField unknown = {999U, PROTOBUF_C_WIRE_TYPE_VARINT, 2, unknown_data};
    CHECK(g && m->n_unknown_fields == 0);
    memcpy(g, m, m->descriptor->sizeof_message);
    g->descriptor = generic_descriptor(m->descriptor);
    same_encoding(m, g);
    n = protobuf_c_message_get_packed_size(g);
    wire = malloc(n ? n : 1); CHECK(wire);
    CHECK(protobuf_c_message_pack(g, wire) == n);
    probe(m->descriptor, wire, n, 1);
    free(wire);
    /* Unknown fields force the complete generic packing path. */
    m->n_unknown_fields = g->n_unknown_fields = 1;
    m->unknown_fields = g->unknown_fields = &unknown;
    same_encoding(m, g);
    m->n_unknown_fields = 0; m->unknown_fields = NULL;
    free(g);
}
static void field_is(const ProtobufCMessageDescriptor *d, unsigned i, unsigned tag,
                     ProtobufCType type, ProtobufCLabel label, unsigned offset) {
    const ProtobufCFieldDescriptor *f = d->fields + i;
    CHECK(i < d->n_fields && f->id == tag && f->type == type && f->label == label && f->offset == offset);
}
#define FIELD(d, i, tag, type, structure, member) \
    field_is(&pg_query__##d##__descriptor, i, tag, PROTOBUF_C_TYPE_##type, PROTOBUF_C_LABEL_NONE, offsetof(structure, member))
static void test_descriptor_contracts(void) {
    unsigned i;
    CHECK(pg_query__string__descriptor.n_fields == 2);
    FIELD(string, 0, 1, STRING, PgQuery__String, sval);
    FIELD(string, 1, 2, INT32, PgQuery__String, location);
    CHECK(pg_query__integer__descriptor.n_fields == 1);
    FIELD(integer, 0, 1, INT32, PgQuery__Integer, ival);
    CHECK(pg_query__float__descriptor.n_fields == 1);
    FIELD(float, 0, 1, STRING, PgQuery__Float, fval);
    CHECK(pg_query__boolean__descriptor.n_fields == 1);
    FIELD(boolean, 0, 1, BOOL, PgQuery__Boolean, boolval);
    CHECK(pg_query__bit_string__descriptor.n_fields == 1);
    FIELD(bit_string, 0, 1, STRING, PgQuery__BitString, bsval);
    CHECK(pg_query__param_ref__descriptor.n_fields == 2);
    FIELD(param_ref, 0, 1, INT32, PgQuery__ParamRef, number);
    FIELD(param_ref, 1, 2, INT32, PgQuery__ParamRef, location);
    CHECK(pg_query__a__const__descriptor.n_fields == 7);
    for (i = 0; i < 5; i++) {
        const ProtobufCFieldDescriptor *f = &pg_query__a__const__descriptor.fields[i];
        FIELD(a__const, i, i + 1, MESSAGE, PgQuery__AConst, ival);
        CHECK(f->quantifier_offset == offsetof(PgQuery__AConst, val_case));
        CHECK(f->flags == PROTOBUF_C_FIELD_FLAG_ONEOF && f->default_value == NULL);
    }
    FIELD(a__const, 5, 10, BOOL, PgQuery__AConst, isnull);
    FIELD(a__const, 6, 11, INT32, PgQuery__AConst, location);
    CHECK(pg_query__column_ref__descriptor.n_fields == 2);
    field_is(&pg_query__column_ref__descriptor, 0, 1, PROTOBUF_C_TYPE_MESSAGE,
             PROTOBUF_C_LABEL_REPEATED, offsetof(PgQuery__ColumnRef, fields));
    CHECK(pg_query__column_ref__descriptor.fields[0].quantifier_offset == offsetof(PgQuery__ColumnRef, n_fields));
    FIELD(column_ref, 1, 2, INT32, PgQuery__ColumnRef, location);
    CHECK(pg_query__res_target__descriptor.n_fields == 4);
    FIELD(res_target, 0, 1, STRING, PgQuery__ResTarget, name);
    field_is(&pg_query__res_target__descriptor, 1, 2, PROTOBUF_C_TYPE_MESSAGE,
             PROTOBUF_C_LABEL_REPEATED, offsetof(PgQuery__ResTarget, indirection));
    CHECK(pg_query__res_target__descriptor.fields[1].quantifier_offset == offsetof(PgQuery__ResTarget, n_indirection));
    FIELD(res_target, 2, 3, MESSAGE, PgQuery__ResTarget, val);
    FIELD(res_target, 3, 4, INT32, PgQuery__ResTarget, location);
}
static void test_values(void) {
    static const int32_t values[] = {0, 1, -1, 127, 128, 16383, 16384, 2097151, 2097152, 268435455, 268435456, INT32_MAX, INT32_MIN};
    static const size_t lengths[] = {0, 1, 126, 127, 128, 255, 16383, 16384};
    PgQuery__Integer integer = PG_QUERY__INTEGER__INIT;
    PgQuery__Boolean boolean = PG_QUERY__BOOLEAN__INIT;
    PgQuery__ParamRef param = PG_QUERY__PARAM_REF__INIT;
    PgQuery__String string = PG_QUERY__STRING__INIT;
    PgQuery__Float floating = PG_QUERY__FLOAT__INIT;
    PgQuery__BitString bits = PG_QUERY__BIT_STRING__INIT;
    size_t i, j;
    char text[16385]; memset(text, 'x', sizeof(text));
    for (i = 0; i < COUNT(values); i++) {
        integer.ival = values[i]; boolean.boolval = values[i];
        check_message(&integer.base); check_message(&boolean.base);
        for (j = 0; j < COUNT(values); j++) {
            param.number = values[i]; param.location = values[j]; check_message(&param.base);
        }
    }
    string.sval = NULL; floating.fval = NULL; bits.bsval = NULL;
    check_message(&string.base); check_message(&floating.base); check_message(&bits.base);
    for (i = 0; i < COUNT(lengths); i++) {
        memset(text, 'x', sizeof(text)); text[lengths[i]] = '\0';
        string.sval = text; floating.fval = text; bits.bsval = text;
        check_message(&floating.base); check_message(&bits.base);
        for (j = 0; j < COUNT(values); j++) {
            string.location = values[j]; check_message(&string.base);
        }
    }
}
static void test_wrappers(void) {
    PgQuery__String string = PG_QUERY__STRING__INIT;
    PgQuery__Integer integer = PG_QUERY__INTEGER__INIT;
    PgQuery__Float floating = PG_QUERY__FLOAT__INIT;
    PgQuery__Boolean boolean = PG_QUERY__BOOLEAN__INIT;
    PgQuery__BitString bits = PG_QUERY__BIT_STRING__INIT;
    PgQuery__AConst literal = PG_QUERY__A__CONST__INIT;
    PgQuery__Node leaf = PG_QUERY__NODE__INIT;
    PgQuery__ColumnRef column = PG_QUERY__COLUMN_REF__INIT;
    PgQuery__ResTarget target = PG_QUERY__RES_TARGET__INIT;
    ProtobufCMessage *children[] = {&integer.base, &floating.base, &boolean.base, &string.base, &bits.base};
    PgQuery__Node *items[] = {&leaf, &leaf, &leaf};
    unsigned i, j;
    string.sval = "a long value which needs its own allocation"; string.location = -1;
    integer.ival = INT32_MIN; floating.fval = "1.23"; boolean.boolval = 7; bits.bsval = "b1010";
    for (i = 0; i < COUNT(children); i++) {
        literal.val_case = (PgQuery__AConst__ValCase)(i + 1);
        memcpy(&literal.ival, &children[i], sizeof(literal.ival));
        literal.location = -1; literal.isnull = -7;
        check_message(&literal.base);
        literal.ival = NULL; check_message(&literal.base);
    }
    literal.ival = (PgQuery__Integer *)(uintptr_t)1;
    literal.val_case = (PgQuery__AConst__ValCase)999; check_message(&literal.base);
    literal.val_case = PG_QUERY__A__CONST__VAL__NOT_SET; check_message(&literal.base);
    leaf.node_case = PG_QUERY__NODE__NODE_STRING; leaf.string = &string;
    for (i = 0; i <= COUNT(items); i++) {
        column.n_fields = i; column.fields = items; column.location = -1;
        check_message(&column.base);
        target.n_indirection = i; target.indirection = items;
        for (j = 0; j < 2; j++) {
            target.val = j ? &leaf : NULL; target.name = j ? "alias" : NULL; target.location = INT32_MAX;
            check_message(&target.base);
        }
    }
}
static uint32_t random_state = UINT32_C(0x9e3779b9);
static uint32_t random32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void test_wire_edges_and_fuzz(void) {
    static const uint8_t edges[][24] = {
        {0}, {8}, {8, 0x80}, {8, 0x80, 0}, {8, 1, 8, 2}, {16, 2, 8, 1},
        {10, 0}, {10, 3, 'a', 0, 'b'}, {10, 0x80, 0}, {0x8a, 0, 0},
        {10, 0, 18, 0}, {10, 1, 0x80, 18, 0}, {10, 0, 10, 1, 0x80},
        {13, 1, 2, 3, 4}, {9, 1, 2, 3, 4, 5, 6, 7, 8},
        {0xff, 0xff, 0xff, 0xff, 0xff}, {0xfa, 0xff, 0xff, 0xff, 0x7f, 0},
        {10, 0xff, 0xff, 0xff, 0xff, 0x7f},
        {8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
        {8, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
    };
    static const size_t lengths[] = {1, 1, 2, 3, 4, 4, 2, 5, 3, 3, 4, 5, 5, 5, 9, 5, 6, 6, 11, 11};
    uint8_t wire[40];
    size_t d, i, j, n;
    for (d = 0; d < COUNT(descriptors); d++) {
        probe(descriptors[d], NULL, 0, 1);
        for (i = 0; i < COUNT(edges); i++) {
            for (n = 0; n <= lengths[i]; n++) probe(descriptors[d], edges[i], n, 1);
        }
        for (i = 0; i < 12000; i++) {
            n = random32() % sizeof(wire);
            for (j = 0; j < n; j++) wire[j] = (uint8_t)random32();
            if (i % 3 == 0 && n > 1) wire[0] = (uint8_t)(8 + (random32() % 12));
            probe(descriptors[d], wire, n, 0);
        }
    }
    /* Generic boolean parsing intentionally accepts other wire types; the
     * strict shortcut must fall back rather than changing this behavior. */
    {
        ProtobufCMessage *m = protobuf_c_message_unpack(&pg_query__boolean__descriptor, NULL, 5, edges[13]);
        CHECK(m != NULL); protobuf_c_message_free_unpacked(m, NULL);
    }
    /* Embedded NULs are owned copies, including bytes past the NUL. */
    {
        PgQuery__String *m = (PgQuery__String *)protobuf_c_message_unpack(&pg_query__string__descriptor, NULL, 5, edges[7]);
        CHECK(m && memcmp(m->sval, "a\0b\0", 4) == 0 && m->sval != (const char *)edges[7] + 2);
        protobuf_c_message_free_unpacked(&m->base, NULL);
    }
}
int main(void) {
    size_t i;
    init_generic(); test_descriptor_contracts(); test_values(); test_wrappers(); test_wire_edges_and_fuzz();
    for (i = 0; i < COUNT(descriptors); i++) free(generic_fields[i]);
    puts("protobuf leaf/wrapper fast/generic differential and wire fuzz tests passed");
    return 0;
}
