/* Destruction is compared directly, without a pack/unpack round trip: packing
 * would erase invalid oneof cases, inactive pointers and partially built arrays.
 * Recursively copied descriptors provide an independent generic-loop oracle. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protobuf/pg_query.pb-c.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_DESCRIPTORS 512
#define MAX_ALLOCS 128

static struct descriptor_copy {
    const ProtobufCMessageDescriptor *original;
    ProtobufCMessageDescriptor copy;
    ProtobufCFieldDescriptor *fields;
} copies[MAX_DESCRIPTORS];
static size_t n_copies;

static const ProtobufCMessageDescriptor *copy_descriptor(const ProtobufCMessageDescriptor *d) {
    struct descriptor_copy *c;
    size_t i;
    unsigned f;
    for (i = 0; i < n_copies; i++) if (copies[i].original == d) return &copies[i].copy;
    CHECK(n_copies < COUNT(copies));
    c = &copies[n_copies++]; /* Register before following recursive references. */
    c->original = d;
    c->copy = *d;
    c->copy.message_init = NULL;
    c->fields = malloc((d->n_fields ? d->n_fields : 1) * sizeof(*c->fields));
    CHECK(c->fields != NULL);
    if (d->n_fields) memcpy(c->fields, d->fields, d->n_fields * sizeof(*c->fields));
    c->copy.fields = c->fields;
    for (f = 0; f < d->n_fields; f++) {
        if (c->fields[f].type == PROTOBUF_C_TYPE_MESSAGE)
            c->fields[f].descriptor = copy_descriptor(c->fields[f].descriptor);
    }
    return &c->copy;
}

/* Logical allocation IDs are construction-order indices, so allocator callback
 * arguments and ordering compare exactly despite different malloc addresses.
 * Each owned buffer points at its message; message entries point at themselves
 * and record their parent. Every live ancestor must be invalidated before any
 * callback beneath it. No allocation is allowed during destruction. */
typedef struct {
    void *pointer;
    size_t size, owner, parent;
    unsigned depth;
    int live, message;
} allocation;
typedef struct {
    allocation entries[MAX_ALLOCS];
    size_t trace[MAX_ALLOCS], n_allocs, n_frees, live, calls, fail;
    int destroying, unpacking, check_unpack_root;
} allocation_state;

#ifdef SQLPARSER_PROTOBUF_FREE_WRAPPERS
/* Observe the built-in allocator too, so a default-allocator-only shortcut is
 * checked against the same independent generic descriptor/free-order oracle. */
static allocation_state *default_allocator_state;
void __real_free(void *pointer);
static void record_free(void *data, void *pointer);
void __wrap_free(void *pointer) {
    if (default_allocator_state != NULL)
        record_free(default_allocator_state, pointer);
    else
        __real_free(pointer);
}
#endif

static void *record_alloc(void *data, size_t size) {
    allocation_state *s = data;
    allocation *e;
    void *p;
    CHECK(!s->destroying);
    if (++s->calls == s->fail) return NULL;
    CHECK(s->n_allocs < COUNT(s->entries));
    p = malloc(size ? size : 1);
    CHECK(p != NULL);
    e = &s->entries[s->n_allocs++];
    memset(e, 0, sizeof(*e));
    e->pointer = p; e->size = size; e->live = 1;
    s->live++;
    return p;
}
static size_t allocation_id(allocation_state *s, const void *p) {
    size_t i;
    for (i = 0; i < s->n_allocs; i++)
        if (s->entries[i].live && s->entries[i].pointer == p) return i + 1;
    CHECK(0); return 0;
}
static void record_free(void *data, void *p) {
    allocation_state *s = data;
    size_t id = allocation_id(s, p), owner;
    allocation *e = &s->entries[id - 1];
    CHECK(s->destroying || s->unpacking);
    CHECK(s->n_frees < COUNT(s->trace) && s->live > 0);
    for (owner = e->owner; owner; owner = s->entries[owner - 1].parent) {
        allocation *ancestor = &s->entries[owner - 1];
        CHECK(ancestor->live && ancestor->message);
        CHECK(((ProtobufCMessage *)ancestor->pointer)->descriptor == NULL);
    }
    if (s->check_unpack_root)
        CHECK(((ProtobufCMessage *)s->entries[0].pointer)->descriptor == NULL);
    s->trace[s->n_frees++] = id;
    e->live = 0; s->live--;
#ifdef SQLPARSER_PROTOBUF_FREE_WRAPPERS
    __real_free(p);
#else
    free(p);
#endif
}
static void *owned_alloc(allocation_state *s, size_t size, ProtobufCMessage *owner) {
    void *p = record_alloc(s, size);
    s->entries[s->n_allocs - 1].owner = allocation_id(s, owner);
    return p;
}
static ProtobufCMessage *new_message(allocation_state *s, unsigned mode,
                                    const ProtobufCMessageDescriptor *d,
                                    ProtobufCMessage *parent) {
    ProtobufCMessage *m = record_alloc(s, d->sizeof_message);
    allocation *e = &s->entries[s->n_allocs - 1];
    e->owner = s->n_allocs; e->message = 1;
    if (parent) {
        e->parent = allocation_id(s, parent);
        e->depth = s->entries[e->parent - 1].depth + 1;
    }
    protobuf_c_message_init(d, m);
    /* 0: generated, 1: copied, 2/3: alternating actual child descriptors. */
    if (mode == 1 || (mode == 2 && e->depth % 2) || (mode == 3 && !(e->depth % 2)))
        m->descriptor = copy_descriptor(d);
    return m;
}
static void add_unknown(allocation_state *s, ProtobufCMessage *m, unsigned kind) {
    ProtobufCMessageUnknownField *fields;
    if (!kind) return;
    fields = owned_alloc(s, 3 * sizeof(*fields), m);
    memset(fields, 0, 3 * sizeof(*fields));
    m->unknown_fields = fields;
    if (kind == 2) return; /* Allocated before parsing the first unknown field. */
    m->n_unknown_fields = 3;
    fields[0].tag = 4000; fields[0].wire_type = PROTOBUF_C_WIRE_TYPE_VARINT;
    fields[0].len = 1; fields[0].data = owned_alloc(s, 1, m); fields[0].data[0] = 7;
    fields[1].tag = 4001; fields[1].wire_type = PROTOBUF_C_WIRE_TYPE_LENGTH_PREFIXED;
    /* A NULL payload must not produce a free(NULL) callback. */
    fields[2].tag = 4002; fields[2].wire_type = PROTOBUF_C_WIRE_TYPE_LENGTH_PREFIXED;
    fields[2].data = owned_alloc(s, 1, m); /* Non-NULL zero-length payload is owned. */
}
static void set_string(allocation_state *s, PgQuery__String *m, unsigned kind) {
    static const char embedded[] = {'a', '\0', 'b', '\0'};
    if (kind == 0) m->sval = NULL;
    else if (kind == 1) CHECK(m->sval == pg_query__string__descriptor.fields[0].default_value);
    else {
        size_t n = kind == 2 ? 1 : sizeof(embedded);
        m->sval = owned_alloc(s, n, &m->base);
        if (kind == 2) m->sval[0] = '\0';
        else memcpy(m->sval, embedded, n);
    }
}

typedef struct {
    const ProtobufCMessageDescriptor *descriptor;
    uint32_t choice;
    unsigned null_child, unknown, variant;
} fixture;
typedef ProtobufCMessage *(*fixture_builder)(allocation_state *, unsigned, const fixture *);

static ProtobufCMessage *build_scalar(allocation_state *s, unsigned mode, const fixture *f) {
    ProtobufCMessage *m = new_message(s, mode, f->descriptor, NULL);
    if (f->descriptor == &pg_query__string__descriptor) set_string(s, (PgQuery__String *)m, f->variant);
    else ((PgQuery__Integer *)m)->ival = INT32_MIN;
    add_unknown(s, m, f->unknown);
    return m;
}
static ProtobufCMessage *build_oneof(allocation_state *s, unsigned mode, const fixture *f) {
    const ProtobufCFieldDescriptor *selected = protobuf_c_message_descriptor_get_field(f->descriptor, f->choice);
    const ProtobufCFieldDescriptor *first = &f->descriptor->fields[0];
    ProtobufCMessage *root = new_message(s, mode, f->descriptor, NULL), *child;
    memcpy((char *)root + first->quantifier_offset, &f->choice, sizeof(f->choice));
    if (selected && (selected->flags & PROTOBUF_C_FIELD_FLAG_ONEOF)) {
        child = NULL;
        if (!f->null_child) {
            /* Also prove recursion follows the actual child descriptor, rather
             * than assuming its type from the parent field's declaration. */
            const ProtobufCMessageDescriptor *d = f->variant ? &pg_query__string__descriptor : selected->descriptor;
            child = new_message(s, mode, d, root);
            if (d == &pg_query__string__descriptor) set_string(s, (PgQuery__String *)child, 3);
            add_unknown(s, child, f->unknown);
        }
    } else child = (ProtobufCMessage *)(uintptr_t)1; /* Inactive poison, never owned. */
    memcpy((char *)root + first->offset, &child, sizeof(child));
    add_unknown(s, root, f->unknown);
    return root;
}
static ProtobufCMessage *build_list(allocation_state *s, unsigned mode, const fixture *f) {
    PgQuery__Node *root = (PgQuery__Node *)new_message(s, mode, &pg_query__node__descriptor, NULL);
    PgQuery__List *list = (PgQuery__List *)new_message(s, mode, &pg_query__list__descriptor, &root->base);
    PgQuery__Node *node;
    PgQuery__AConst *literal;
    PgQuery__String *string;
    size_t i;
    root->node_case = PG_QUERY__NODE__NODE_LIST; root->list = list;
    if (f->variant != 0) {
        list->items = owned_alloc(s, 5 * sizeof(*list->items), &list->base);
        for (i = 0; i < 5; i++) list->items[i] = (PgQuery__Node *)(uintptr_t)1;
    }
    list->n_items = f->variant == 1 ? 0 : 3;
    if (f->variant >= 2) {
        list->items[1] = NULL;
        node = (PgQuery__Node *)new_message(s, mode, &pg_query__node__descriptor, &list->base);
        list->items[0] = node; node->node_case = PG_QUERY__NODE__NODE_A_CONST;
        literal = (PgQuery__AConst *)new_message(s, mode, &pg_query__a__const__descriptor, &node->base);
        node->a_const = literal; literal->val_case = PG_QUERY__A__CONST__VAL_SVAL;
        string = (PgQuery__String *)new_message(s, mode, &pg_query__string__descriptor, &literal->base);
        literal->sval = string; set_string(s, string, 3);
        add_unknown(s, &string->base, f->unknown);
        node = (PgQuery__Node *)new_message(s, mode, &pg_query__node__descriptor, &list->base);
        list->items[2] = node; node->node_case = PG_QUERY__NODE__NODE_INTEGER;
        node->integer = (PgQuery__Integer *)new_message(s, mode, &pg_query__integer__descriptor, &node->base);
        /* Capacity five, initialized count three: trailing poison is untouched. */
    }
    add_unknown(s, &list->base, f->unknown); add_unknown(s, &root->base, f->unknown);
    return &root->base;
}
static ProtobufCMessage *build_literal_chain(allocation_state *s, unsigned mode, const fixture *f) {
    PgQuery__Node *node = (PgQuery__Node *)new_message(s, mode, &pg_query__node__descriptor, NULL);
    PgQuery__AConst *literal = (PgQuery__AConst *)new_message(s, mode, &pg_query__a__const__descriptor, &node->base);
    const ProtobufCMessageDescriptor *leaf_descriptor = f->choice == PG_QUERY__A__CONST__VAL_IVAL ?
        &pg_query__integer__descriptor : &pg_query__string__descriptor;
    ProtobufCMessage *leaf;
    if ((f->variant & 4U) != 0U)
        leaf_descriptor = leaf_descriptor == &pg_query__integer__descriptor ?
            &pg_query__string__descriptor : &pg_query__integer__descriptor;
    node->node_case = PG_QUERY__NODE__NODE_A_CONST; node->a_const = literal;
    literal->val_case = f->choice;
    literal->isnull = (int)f->null_child; /* Active payloads are owned even when isnull is true. */
    leaf = new_message(s, mode, leaf_descriptor, &literal->base);
    if (f->choice == PG_QUERY__A__CONST__VAL_IVAL) literal->ival = (PgQuery__Integer *)leaf;
    else literal->sval = (PgQuery__String *)leaf;
    if (leaf_descriptor == &pg_query__integer__descriptor) {
        ((PgQuery__Integer *)leaf)->ival = INT32_MIN;
    } else {
        set_string(s, (PgQuery__String *)leaf, f->variant % 4U);
    }
    /* Unknown arrays vary independently at all depths, including allocated
     * arrays whose initialized count is zero. */
    add_unknown(s, &node->base, f->unknown % 3U);
    add_unknown(s, &literal->base, f->unknown / 3U % 3U);
    add_unknown(s, leaf, f->unknown / 9U % 3U);
    return &node->base;
}
static void compare_fixture(fixture_builder build, const fixture *f) {
    allocation_state oracle = {0};
    ProtobufCAllocator allocator = {record_alloc, record_free, &oracle};
    ProtobufCMessage *m = build(&oracle, 1, f);
    size_t i;
    unsigned mode;
    oracle.destroying = 1;
    protobuf_c_message_free_unpacked(m, &allocator);
    CHECK(oracle.live == 0 && oracle.n_frees == oracle.n_allocs && oracle.calls == oracle.n_allocs);
    for (mode = 0; mode < 4; mode++) {
        allocation_state actual = {0}, ordinary = {0};
        allocator.allocator_data = &actual;
        m = build(&actual, mode, f);
        CHECK(actual.n_allocs == oracle.n_allocs);
        for (i = 0; i < actual.n_allocs; i++) CHECK(actual.entries[i].size == oracle.entries[i].size);
        actual.destroying = 1;
        protobuf_c_message_free_unpacked(m, &allocator);
        CHECK(actual.live == 0 && actual.calls == oracle.calls && actual.n_frees == oracle.n_frees);
        CHECK(memcmp(actual.trace, oracle.trace, oracle.n_frees * sizeof(*oracle.trace)) == 0);
        /* The NULL allocator must release these ordinary malloc allocations as
         * well; sanitizers independently check leaks and invalid sentinel frees. */
        m = build(&ordinary, mode, f);
#ifdef SQLPARSER_PROTOBUF_FREE_WRAPPERS
        ordinary.destroying = 1;
        default_allocator_state = &ordinary;
#endif
        protobuf_c_message_free_unpacked(m, NULL);
#ifdef SQLPARSER_PROTOBUF_FREE_WRAPPERS
        default_allocator_state = NULL;
        CHECK(ordinary.live == 0 && ordinary.calls == oracle.calls && ordinary.n_frees == oracle.n_frees);
        CHECK(memcmp(ordinary.trace, oracle.trace, oracle.n_frees * sizeof(*oracle.trace)) == 0);
#endif
    }
}

static void check_field(const ProtobufCMessageDescriptor *d, unsigned i, uint32_t tag,
                        ProtobufCType type, ProtobufCLabel label, size_t offset) {
    const ProtobufCFieldDescriptor *f;
    CHECK(i < d->n_fields); f = &d->fields[i];
    CHECK(f->id == tag && f->type == type && f->label == label && f->offset == offset);
}
static void test_descriptor_contracts(void) {
    const ProtobufCMessageDescriptor *node = &pg_query__node__descriptor;
    const ProtobufCMessageDescriptor *literal = &pg_query__a__const__descriptor;
    const ProtobufCMessageDescriptor *string = &pg_query__string__descriptor;
    const ProtobufCMessageDescriptor *integer = &pg_query__integer__descriptor;
    const ProtobufCMessageDescriptor *list = &pg_query__list__descriptor;
    const ProtobufCMessageDescriptor *children[] = {&pg_query__integer__descriptor,
        &pg_query__float__descriptor, &pg_query__boolean__descriptor,
        &pg_query__string__descriptor, &pg_query__bit_string__descriptor};
    PgQuery__String initialized = PG_QUERY__STRING__INIT;
    unsigned i;
    CHECK(node->sizeof_message == sizeof(PgQuery__Node) && node->n_fields > 0);
    CHECK(sizeof(((PgQuery__Node *)0)->node_case) == sizeof(uint32_t));
    for (i = 0; i < node->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = &node->fields[i];
        check_field(node, i, i + 1, PROTOBUF_C_TYPE_MESSAGE, PROTOBUF_C_LABEL_NONE, offsetof(PgQuery__Node, alias));
        CHECK(f->quantifier_offset == offsetof(PgQuery__Node, node_case));
        CHECK(f->flags == PROTOBUF_C_FIELD_FLAG_ONEOF && f->default_value == NULL && f->descriptor != NULL);
    }
    CHECK(literal->sizeof_message == sizeof(PgQuery__AConst) && literal->n_fields == 7);
    CHECK(sizeof(((PgQuery__AConst *)0)->val_case) == sizeof(uint32_t));
    CHECK(PG_QUERY__A__CONST__VAL_IVAL == 1 && PG_QUERY__A__CONST__VAL_FVAL == 2 &&
          PG_QUERY__A__CONST__VAL_BOOLVAL == 3 && PG_QUERY__A__CONST__VAL_SVAL == 4 && PG_QUERY__A__CONST__VAL_BSVAL == 5);
    for (i = 0; i < 5; i++) {
        const ProtobufCFieldDescriptor *f = &literal->fields[i];
        check_field(literal, i, i + 1, PROTOBUF_C_TYPE_MESSAGE, PROTOBUF_C_LABEL_NONE, offsetof(PgQuery__AConst, ival));
        CHECK(f->quantifier_offset == offsetof(PgQuery__AConst, val_case));
        CHECK(f->flags == PROTOBUF_C_FIELD_FLAG_ONEOF && f->default_value == NULL && f->descriptor == children[i]);
    }
    check_field(literal, 5, 10, PROTOBUF_C_TYPE_BOOL, PROTOBUF_C_LABEL_NONE, offsetof(PgQuery__AConst, isnull));
    check_field(literal, 6, 11, PROTOBUF_C_TYPE_INT32, PROTOBUF_C_LABEL_NONE, offsetof(PgQuery__AConst, location));
    CHECK(!literal->fields[5].flags && !literal->fields[6].flags);
    CHECK(string->sizeof_message == sizeof(PgQuery__String) && string->n_fields == 2);
    check_field(string, 0, 1, PROTOBUF_C_TYPE_STRING, PROTOBUF_C_LABEL_NONE, offsetof(PgQuery__String, sval));
    check_field(string, 1, 2, PROTOBUF_C_TYPE_INT32, PROTOBUF_C_LABEL_NONE, offsetof(PgQuery__String, location));
    CHECK(!string->fields[0].flags && !string->fields[1].flags);
    CHECK(string->fields[0].default_value == initialized.sval && initialized.sval != NULL && initialized.sval[0] == '\0');
    CHECK(integer->sizeof_message == sizeof(PgQuery__Integer) && integer->n_fields == 1);
    check_field(integer, 0, 1, PROTOBUF_C_TYPE_INT32, PROTOBUF_C_LABEL_NONE, offsetof(PgQuery__Integer, ival));
    CHECK(!integer->fields[0].flags && integer->fields[0].default_value == NULL);
    CHECK(list->sizeof_message == sizeof(PgQuery__List) && list->n_fields == 1);
    check_field(list, 0, 1, PROTOBUF_C_TYPE_MESSAGE, PROTOBUF_C_LABEL_REPEATED, offsetof(PgQuery__List, items));
    CHECK(list->fields[0].quantifier_offset == offsetof(PgQuery__List, n_items));
    CHECK(!list->fields[0].flags && list->fields[0].default_value == NULL && list->fields[0].descriptor == node);
}
static void test_direct_free(void) {
    static const uint32_t invalid_node[] = {0, 269, 999, UINT32_MAX};
    static const uint32_t invalid_literal[] = {0, 6, 999, UINT32_MAX};
    fixture f = {0};
    size_t i;
    unsigned unknown, null_child;
    protobuf_c_message_free_unpacked(NULL, NULL);
    { allocation_state s = {0}; ProtobufCAllocator a = {record_alloc, record_free, &s};
      protobuf_c_message_free_unpacked(NULL, &a); CHECK(s.calls == 0 && s.n_frees == 0); }
    for (unknown = 0; unknown < 3; unknown++) {
        f.unknown = unknown; f.descriptor = &pg_query__string__descriptor;
        for (f.variant = 0; f.variant < 4; f.variant++) compare_fixture(build_scalar, &f);
        f.descriptor = &pg_query__integer__descriptor; compare_fixture(build_scalar, &f);
        f.variant = 0;
        for (null_child = 0; null_child < 2; null_child++) {
            f.null_child = null_child; f.descriptor = &pg_query__node__descriptor;
            for (i = 0; i < f.descriptor->n_fields; i++) {
                f.choice = f.descriptor->fields[i].id; compare_fixture(build_oneof, &f);
            }
            f.descriptor = &pg_query__a__const__descriptor;
            for (f.choice = 1; f.choice <= 5; f.choice++) compare_fixture(build_oneof, &f);
        }
        f.null_child = 0; f.descriptor = &pg_query__node__descriptor;
        for (i = 0; i < COUNT(invalid_node); i++) {
            CHECK(protobuf_c_message_descriptor_get_field(f.descriptor, invalid_node[i]) == NULL);
            f.choice = invalid_node[i]; compare_fixture(build_oneof, &f);
        }
        f.descriptor = &pg_query__a__const__descriptor;
        for (i = 0; i < COUNT(invalid_literal); i++) {
            f.choice = invalid_literal[i]; compare_fixture(build_oneof, &f);
        }
        /* Scalar tags are also invalid oneof selectors. */
        f.choice = 10; compare_fixture(build_oneof, &f);
        f.choice = 11; compare_fixture(build_oneof, &f);
        f.variant = 1; f.choice = 1; compare_fixture(build_oneof, &f);
        f.descriptor = &pg_query__node__descriptor; compare_fixture(build_oneof, &f);
        for (f.variant = 0; f.variant < 3; f.variant++) compare_fixture(build_list, &f);
    }
    for (f.unknown = 0; f.unknown < 27; f.unknown++) {
        for (f.null_child = 0; f.null_child < 2; f.null_child++) {
            f.choice = PG_QUERY__A__CONST__VAL_IVAL;
            for (f.variant = 0; f.variant < 8; f.variant++)
                compare_fixture(build_literal_chain, &f);
            f.choice = PG_QUERY__A__CONST__VAL_SVAL;
            for (f.variant = 0; f.variant < 8; f.variant++)
                compare_fixture(build_literal_chain, &f);
        }
    }
}

static void test_unpack_cleanup(void) {
    /* Known String field with wrong type precedes an unknown field: scanning
     * allocates unknown_fields, but parsing fails with n_unknown_fields == 0. */
    static const uint8_t malformed[] = {8, 1, 0xa0, 6, 1};
    unsigned generic;
    for (generic = 0; generic < 2; generic++) {
        const ProtobufCMessageDescriptor *d = generic ? copy_descriptor(&pg_query__string__descriptor) : &pg_query__string__descriptor;
        size_t fail;
        for (fail = 0; fail <= 2; fail++) {
            allocation_state s = {0};
            ProtobufCAllocator a = {record_alloc, record_free, &s};
            s.fail = fail; s.unpacking = 1; s.check_unpack_root = 1;
            CHECK(protobuf_c_message_unpack(d, &a, sizeof(malformed), malformed) == NULL);
            CHECK(s.live == 0 && s.n_allocs == s.n_frees);
            if (!fail) CHECK(s.calls == 2 && s.trace[0] == 2 && s.trace[1] == 1);
        }
        CHECK(protobuf_c_message_unpack(d, NULL, sizeof(malformed), malformed) == NULL);
    }
    /* Exercise fail-each-allocation cleanup of a real nested repeated message,
     * independently of direct fixtures. One failed element leaves a partial
     * array, and unknown fields exist at multiple depths. */
    {
        fixture f = {0};
        allocation_state source = {0};
        ProtobufCAllocator a = {record_alloc, record_free, &source};
        ProtobufCMessage *root;
        uint8_t *wire;
        size_t size;
        f.variant = 2; f.unknown = 1;
        root = build_list(&source, 0, &f);
        /* Sparse arrays and NULL unknown payloads are meaningful destruction
         * fixtures but not valid wire encodings. Complete those entries before
         * packing this separate fail-allocation fixture. */
        {
            PgQuery__List *list = ((PgQuery__Node *)root)->list;
            size_t i;
            list->items[1] = (PgQuery__Node *)new_message(&source, 0, &pg_query__node__descriptor, &list->base);
            for (i = 0; i < source.n_allocs; i++) {
                if (source.entries[i].message) {
                    ProtobufCMessage *m = source.entries[i].pointer;
                    if (m->n_unknown_fields == 3) {
                        ProtobufCMessageUnknownField *u = m->unknown_fields;
                        u[1].data = owned_alloc(&source, 1, m);
                        u[1].data[0] = 0; u[1].len = 1;
                        u[2].data[0] = 0; u[2].len = 1;
                    }
                }
            }
        }
        size = protobuf_c_message_get_packed_size(root);
        wire = malloc(size ? size : 1); CHECK(wire != NULL);
        CHECK(protobuf_c_message_pack(root, wire) == size);
        source.destroying = 1; protobuf_c_message_free_unpacked(root, &a); CHECK(source.live == 0);
        for (generic = 0; generic < 2; generic++) {
            const ProtobufCMessageDescriptor *d = generic ? copy_descriptor(&pg_query__node__descriptor) : &pg_query__node__descriptor;
            size_t fail, calls = 0;
            for (fail = 0; fail <= calls; fail++) {
                allocation_state s = {0};
                a.allocator_data = &s; s.fail = fail; s.unpacking = 1;
                root = protobuf_c_message_unpack(d, &a, size, wire);
                if (!fail) { CHECK(root != NULL); calls = s.calls; }
                else CHECK(root == NULL);
                s.destroying = 1; protobuf_c_message_free_unpacked(root, &a);
                CHECK(s.live == 0 && s.n_frees == s.n_allocs);
            }
        }
        free(wire);
    }
}
int main(void) {
    size_t i;
    copy_descriptor(&pg_query__node__descriptor);
    test_descriptor_contracts(); test_direct_free(); test_unpack_cleanup();
    for (i = 0; i < n_copies; i++) free(copies[i].fields);
    puts("protobuf free allocator-trace equivalence and partial cleanup tests passed");
    return 0;
}
