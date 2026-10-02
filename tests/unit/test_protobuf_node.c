/* Differential tests for the bundled pg_query.Node oneof fast path. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protobuf/pg_query.pb-c.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
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
static ProtobufCMessageDescriptor generic_node;
static void same_encoding(ProtobufCMessage *fast, ProtobufCMessage *generic) {
    size_t n = protobuf_c_message_get_packed_size(fast);
    size_t m = protobuf_c_message_get_packed_size(generic);
    uint8_t *a, *b;
    uint8_t scratch_a[32], scratch_b[32];
    ProtobufCBufferSimple ba = PROTOBUF_C_BUFFER_SIMPLE_INIT(scratch_a);
    ProtobufCBufferSimple bb = PROTOBUF_C_BUFFER_SIMPLE_INIT(scratch_b);
    CHECK(n == m);
    a = malloc(n ? n : 1); b = malloc(n ? n : 1);
    CHECK(a && b);
    CHECK(protobuf_c_message_pack(fast, a) == n);
    CHECK(protobuf_c_message_pack(generic, b) == n);
    CHECK(memcmp(a, b, n) == 0);
    CHECK(protobuf_c_message_pack_to_buffer(fast, &ba.base) == n);
    CHECK(protobuf_c_message_pack_to_buffer(generic, &bb.base) == n);
    CHECK(ba.len == n && bb.len == n);
    CHECK(memcmp(a, ba.data, n) == 0 && memcmp(a, bb.data, n) == 0);
    PROTOBUF_C_BUFFER_SIMPLE_CLEAR(&ba);
    PROTOBUF_C_BUFFER_SIMPLE_CLEAR(&bb);
    free(a); free(b);
}
static void probe(const uint8_t *bytes, size_t size, int expected) {
    allocation_state sa = {0}, sb = {0};
    ProtobufCAllocator aa = {count_alloc, count_free, &sa};
    ProtobufCAllocator ab = {count_alloc, count_free, &sb};
    ProtobufCMessage *a = protobuf_c_message_unpack(&pg_query__node__descriptor, &aa, size, bytes);
    ProtobufCMessage *b = protobuf_c_message_unpack(&generic_node, &ab, size, bytes);
    size_t calls_a, calls_b, f;
    CHECK((a != NULL) == (b != NULL));
    if (expected >= 0) CHECK((a != NULL) == expected);
    if (a) {
        CHECK(((PgQuery__Node *)a)->node_case == ((PgQuery__Node *)b)->node_case);
        same_encoding(a, b);
    }
    protobuf_c_message_free_unpacked(a, &aa);
    protobuf_c_message_free_unpacked(b, &ab);
    CHECK(sa.live == 0 && sb.live == 0);
    calls_a = sa.calls; calls_b = sb.calls;
    /* Fail each allocation in turn. Allocation counts may improve; both paths
     * must return failure safely and release all earlier allocations. */
    for (f = 1; f <= calls_a; f++) {
        memset(&sa, 0, sizeof(sa)); sa.fail = f;
        a = protobuf_c_message_unpack(&pg_query__node__descriptor, &aa, size, bytes);
        CHECK(a == NULL);
        CHECK(sa.live == 0);
    }
    for (f = 1; f <= calls_b; f++) {
        memset(&sb, 0, sizeof(sb)); sb.fail = f;
        b = protobuf_c_message_unpack(&generic_node, &ab, size, bytes);
        CHECK(b == NULL);
        CHECK(sb.live == 0);
    }
}
static size_t varint(uint32_t value, uint8_t *out) {
    size_t n = 0;
    do { out[n++] = (uint8_t)((value & 127U) | (value > 127U ? 128U : 0U)); value >>= 7; } while (value);
    return n;
}
static void test_all_alternatives(void) {
    unsigned i;
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    CHECK(d->n_fields > 0);
    for (i = 0; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = d->fields + i;
        const ProtobufCMessageDescriptor *child_desc = f->descriptor;
        PgQuery__Node node = PG_QUERY__NODE__INIT;
        ProtobufCMessage *child;
        uint8_t *wire;
        size_t n;
        CHECK(f->type == PROTOBUF_C_TYPE_MESSAGE);
        CHECK(f->label == PROTOBUF_C_LABEL_NONE || f->label == PROTOBUF_C_LABEL_OPTIONAL);
        CHECK(f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF);
        CHECK(f->quantifier_offset == d->fields[0].quantifier_offset);
        CHECK(f->offset == d->fields[0].offset);
        CHECK(f->default_value == NULL);
        child = malloc(child_desc->sizeof_message); CHECK(child);
        protobuf_c_message_init(child_desc, child);
        node.node_case = (PgQuery__Node__NodeCase)f->id;
        memcpy((char *)&node + f->offset, &child, sizeof(child));
        n = protobuf_c_message_get_packed_size(&node.base);
        wire = malloc(n ? n : 1); CHECK(wire);
        CHECK(protobuf_c_message_pack(&node.base, wire) == n);
        node.base.descriptor = &generic_node;
        { PgQuery__Node fast = node; fast.base.descriptor = d; same_encoding(&fast.base, &node.base); }
        probe(wire, n, 1);
        free(wire);
        protobuf_c_message_free_unpacked(child, NULL);
        /* An active oneof with a NULL message retains existing omission rules. */
        child = NULL; memcpy((char *)&node + f->offset, &child, sizeof(child));
        { PgQuery__Node fast = node; fast.base.descriptor = d; same_encoding(&fast.base, &node.base); }
    }
}
static void test_wire_edges(void) {
    uint8_t wire[512] = {0}; size_t n, j;
    uint32_t first = pg_query__node__descriptor.fields[0].id;
    uint32_t last = pg_query__node__descriptor.fields[pg_query__node__descriptor.n_fields - 1].id;
    static const uint8_t malformed[][5] = {
        {0x80}, {0x0a}, {0x0a, 0x02, 0x08}, {0x0a, 0x01, 0x80}, {0x0f}, {0x00}
    };
    static const size_t lengths[] = {1,1,3,3,1,1};
    probe(wire, 0, 1);
    for (j=0; j<sizeof(lengths)/sizeof(lengths[0]); j++) probe(malformed[j], lengths[j], 0);
    /* Unknown varint, fixed64, bytes, fixed32 fields survive in wire order. */
    n = varint(4000U << 3, wire); wire[n++] = 123;
    n += varint((4001U << 3)|1U, wire+n); memset(wire+n, 42, 8); n+=8;
    n += varint((4002U << 3)|2U, wire+n); wire[n++]=3; memcpy(wire+n,"abc",3); n+=3;
    n += varint((4003U << 3)|5U, wire+n); memset(wire+n, 24, 4); n+=4;
    probe(wire, n, 1);
    n += varint((first << 3)|2U, wire+n); wire[n++]=0;
    probe(wire, n, 1);
    /* Duplicate alternatives, more than one scanned-member slab. */
    for (j=0;j<24;j++) { n += varint((((j&1U)?first:last)<<3)|2U,wire+n); wire[n++]=0; }
    probe(wire,n,1);
    /* Neither order is allowed to hide an invalid earlier/later message. */
    n = varint((first<<3)|2U,wire); wire[n++]=1; wire[n++]=0x80;
    n += varint((first<<3)|2U,wire+n); wire[n++]=0; probe(wire,n,0);
    n = varint((first<<3)|2U,wire); wire[n++]=0;
    n += varint((last<<3)|2U,wire+n); wire[n++]=1; wire[n++]=0x80; probe(wire,n,0);
    /* Wrong wire type for a known message. */
    n=varint(first<<3,wire); wire[n++]=1; probe(wire,n,0);
}
static void test_invalid_discriminator(void) {
    unsigned i;
    uint32_t cases[] = {0U, 269U, 100000U, UINT32_MAX};
    uint8_t unknown_data[] = {7};
    ProtobufCMessageUnknownField unknown = {4000U, PROTOBUF_C_WIRE_TYPE_VARINT, 1, unknown_data};
    for (i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        PgQuery__Node fast=PG_QUERY__NODE__INIT, generic;
        fast.node_case=(PgQuery__Node__NodeCase)cases[i];
        fast.alias=(PgQuery__Alias *)(uintptr_t)1; /* Inactive: never read/freed. */
        fast.base.n_unknown_fields=1; fast.base.unknown_fields=&unknown;
        generic=fast; generic.base.descriptor=&generic_node;
        same_encoding(&fast.base,&generic.base);
        /* A separately allocated invalid node exercises free without touching
         * the inactive union; unknown fields were checked via probe above. */
        { allocation_state state={0}; ProtobufCAllocator a={count_alloc,count_free,&state};
          PgQuery__Node *p=count_alloc(&state,sizeof(*p)); CHECK(p); *p=fast;
          p->base.n_unknown_fields=0; p->base.unknown_fields=NULL;
          protobuf_c_message_free_unpacked(&p->base,&a); CHECK(state.live==0); }
    }
}
static void test_nested_and_generic(void) {
    PgQuery__String value = PG_QUERY__STRING__INIT;
    PgQuery__AConst literal = PG_QUERY__A__CONST__INIT;
    PgQuery__Node leaf = PG_QUERY__NODE__INIT, root = PG_QUERY__NODE__INIT;
    PgQuery__Node *items[] = {&leaf, &leaf};
    PgQuery__List list = PG_QUERY__LIST__INIT;
    uint8_t bytes[128]; size_t n;
    /* This is an actual nested Node -> List -> Node -> AConst -> String. */
    value.sval = "owned string";
    literal.val_case = PG_QUERY__A__CONST__VAL_SVAL; literal.sval = &value;
    leaf.node_case = PG_QUERY__NODE__NODE_A_CONST; leaf.a_const = &literal;
    list.n_items = 2; list.items = items;
    root.node_case = PG_QUERY__NODE__NODE_LIST; root.list = &list;
    n = protobuf_c_message_get_packed_size(&root.base); CHECK(n <= sizeof(bytes));
    CHECK(protobuf_c_message_pack(&root.base, bytes) == n); probe(bytes,n,1);
    /* A synthetic ordinary descriptor ensures required-field validation still
     * runs, even though no generated PgQuery message currently uses proto2. */
    {
        typedef struct { ProtobufCMessage base; int32_t value; } required_message;
        static const ProtobufCFieldDescriptor fields[] = {{
            .name="value", .id=1, .label=PROTOBUF_C_LABEL_REQUIRED,
            .type=PROTOBUF_C_TYPE_INT32, .offset=offsetof(required_message,value)
        }};
        static const ProtobufCIntRange ranges[] = {{1,0},{0,1}};
        static const ProtobufCMessageDescriptor desc = {
            .magic=PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC, .name="test.Required",
            .short_name="Required", .sizeof_message=sizeof(required_message),
            .n_fields=1, .fields=fields, .n_field_ranges=1, .field_ranges=ranges
        };
        const uint8_t valid[]={8,42};
        ProtobufCMessage *message;
        CHECK(protobuf_c_message_unpack(&desc,NULL,0,valid)==NULL);
        message=protobuf_c_message_unpack(&desc,NULL,sizeof(valid),valid);
        CHECK(message != NULL && ((required_message *)message)->value==42);
        protobuf_c_message_free_unpacked(message,NULL);
    }
}
int main(void) {
    generic_node=pg_query__node__descriptor;
    generic_node.message_init=NULL; /* Force generic initialization and loops. */
    test_all_alternatives(); test_wire_edges(); test_invalid_discriminator(); test_nested_and_generic();
    puts("protobuf Node fast/generic differential tests passed");
    return 0;
}
