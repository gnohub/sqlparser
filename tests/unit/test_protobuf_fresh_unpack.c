/* The strict leaf scanner may skip replacement dispatch only while generated
 * defaults are still fresh. Observe every allocator callback, including each
 * failure boundary, to guard allocation order and oneof publication timing. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protobuf/pg_query.pb-c.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    void *blocks[4];
    size_t calls, allocated, freed, fail;
    int destroying;
} allocation_state;
static int integer_fixture;
static size_t text_allocation_size = 4;
#ifdef SQLPARSER_FRESH_DEFAULT_WRAPPERS
static allocation_state *default_state;
void *__real_malloc(size_t size);
void __real_free(void *pointer);
static void *record_alloc(void *data, size_t size);
static void record_free(void *data, void *pointer);
void *__wrap_malloc(size_t size) {
    return default_state != NULL ? record_alloc(default_state, size) : __real_malloc(size);
}
void __wrap_free(void *pointer) {
    if (default_state != NULL) record_free(default_state, pointer);
    else __real_free(pointer);
}
#endif

static void check_pending_parents(const allocation_state *s, size_t count) {
    if (count >= 1) {
        const PgQuery__Node *node = s->blocks[0];
        ProtobufCMessage *child;
        memcpy(&child, &node->a_const, sizeof(child));
        CHECK(node->base.descriptor == &pg_query__node__descriptor);
        CHECK(node->node_case == PG_QUERY__NODE__NODE__NOT_SET && child == NULL);
        CHECK(node->base.n_unknown_fields == 0 && node->base.unknown_fields == NULL);
    }
    if (count >= 2) {
        const PgQuery__AConst *literal = s->blocks[1];
        ProtobufCMessage *child;
        memcpy(&child, &literal->sval, sizeof(child));
        CHECK(literal->base.descriptor == &pg_query__a__const__descriptor);
        CHECK(literal->val_case == PG_QUERY__A__CONST__VAL__NOT_SET && child == NULL);
        CHECK(literal->isnull == 0 && literal->location == 0);
    }
    if (count >= 3) {
        if (integer_fixture) {
            const PgQuery__Integer *integer = s->blocks[2];
            CHECK(integer->base.descriptor == &pg_query__integer__descriptor && integer->ival == 0);
            return;
        }
        const PgQuery__String *string = s->blocks[2];
        CHECK(string->base.descriptor == &pg_query__string__descriptor);
        CHECK(string->sval == pg_query__string__descriptor.fields[0].default_value);
        CHECK(string->location == 0);
    }
}

static void *record_alloc(void *data, size_t size) {
    allocation_state *s = data;
    const size_t expected[] = {sizeof(PgQuery__Node), sizeof(PgQuery__AConst),
        integer_fixture ? sizeof(PgQuery__Integer) : sizeof(PgQuery__String), text_allocation_size};
    void *p;
    CHECK(!s->destroying && s->freed == 0 && s->calls < 4);
    CHECK(size == expected[s->calls]);
    check_pending_parents(s, s->allocated);
    if (++s->calls == s->fail) return NULL;
#ifdef SQLPARSER_FRESH_DEFAULT_WRAPPERS
    p = __real_malloc(size);
#else
    p = malloc(size);
#endif
    CHECK(p);
    memset(p, 0xa5, size);
    s->blocks[s->allocated++] = p;
    return p;
}

static void record_free(void *data, void *pointer) {
    allocation_state *s = data;
    size_t id;
    CHECK(s->freed < s->allocated);
    id = s->allocated - s->freed;
    CHECK(id > 0 && pointer == s->blocks[id - 1]);
    if (id <= 3) CHECK(((ProtobufCMessage *)pointer)->descriptor == NULL);
    if (s->destroying) {
        size_t i;
        /* Successful parent destruction invalidates every ancestor before
         * any child callback, without clearing the selected discriminators. */
        for (i = 0; i < id && i < 3; i++)
            CHECK(((ProtobufCMessage *)s->blocks[i])->descriptor == NULL);
        if (id >= 1) CHECK(((PgQuery__Node *)s->blocks[0])->node_case == PG_QUERY__NODE__NODE_A_CONST);
        if (id >= 2) CHECK(((PgQuery__AConst *)s->blocks[1])->val_case ==
            (integer_fixture ? PG_QUERY__A__CONST__VAL_IVAL : PG_QUERY__A__CONST__VAL_SVAL));
    } else {
        /* A child allocation failure unwinds while its parents are still
         * pending: no discriminator or child pointer may be published early. */
        if (id > 1) check_pending_parents(s, id - 1);
        if (id == 1) CHECK(((PgQuery__Node *)pointer)->node_case == PG_QUERY__NODE__NODE__NOT_SET);
        if (id == 2) CHECK(((PgQuery__AConst *)pointer)->val_case == PG_QUERY__A__CONST__VAL__NOT_SET);
    }
    s->freed++;
#ifdef SQLPARSER_FRESH_DEFAULT_WRAPPERS
    __real_free(pointer);
#else
    free(pointer);
#endif
}

int main(void) {
    static const uint8_t wire[] = {
        0xe2, 0x10, 13, /* Node.a_const */
        34, 7, 10, 3, 'a', 0, 'b', 16, 7, /* AConst.sval -> String */
        80, 1, 88, 9 /* AConst.isnull and location */
    };
    static const uint8_t string_wire[] = {0xe2, 0x10, 9, 34, 5, 10, 3, 'a', 0, 'b', 88, 9};
    static const uint8_t integer_wire[] = {0xe2, 0x10, 6, 10, 2, 8, 42, 88, 9};
    static const uint8_t empty_string[] = {0xe2, 0x10, 4, 34, 0, 88, 9};
    static const uint8_t present_empty_string[] = {0xe2, 0x10, 6, 34, 2, 10, 0, 88, 9};
    static const uint8_t zero_integer[] = {0xe2, 0x10, 4, 10, 0, 88, 9};
    const uint8_t *wires[] = {wire, wire, string_wire, integer_wire, empty_string, present_empty_string, zero_integer};
    const size_t lengths[] = {sizeof(wire), sizeof(wire), sizeof(string_wire), sizeof(integer_wire),
        sizeof(empty_string), sizeof(present_empty_string), sizeof(zero_integer)};
    size_t mode, fail;
    size_t modes = 1;
#ifdef SQLPARSER_FRESH_DEFAULT_WRAPPERS
    modes = sizeof(wires) / sizeof(wires[0]);
#endif
    for (mode = 0; mode < modes; mode++) {
      size_t expected_calls = mode == 3 || mode == 4 || mode == 6 ? 3 : 4;
      integer_fixture = mode == 3 || mode == 6;
      text_allocation_size = mode == 5 ? 1 : 4;
      for (fail = 0; fail <= expected_calls; fail++) {
        allocation_state state = {0};
        ProtobufCAllocator allocator = {record_alloc, record_free, &state};
        PgQuery__Node *node;
        state.fail = fail;
#ifdef SQLPARSER_FRESH_DEFAULT_WRAPPERS
        if (mode != 0) default_state = &state;
#endif
        node = (PgQuery__Node *)protobuf_c_message_unpack(&pg_query__node__descriptor,
            mode == 0 ? &allocator : NULL, lengths[mode], wires[mode]);
        if (fail) {
            CHECK(node == NULL && state.calls == fail && state.allocated == fail - 1);
        } else {
            PgQuery__AConst *literal;
            PgQuery__String *string;
            CHECK(node && state.calls == expected_calls && state.freed == 0);
            CHECK(node->node_case == PG_QUERY__NODE__NODE_A_CONST);
            literal = node->a_const;
            CHECK(literal == state.blocks[1] && literal->val_case ==
                (integer_fixture ? PG_QUERY__A__CONST__VAL_IVAL : PG_QUERY__A__CONST__VAL_SVAL));
            CHECK(literal->isnull == (mode <= 1) && literal->location == 9);
            if (integer_fixture) {
                CHECK(literal->ival == state.blocks[2] && literal->ival->ival == (mode == 3 ? 42 : 0));
            } else {
                string = literal->sval;
                CHECK(string == state.blocks[2] && string->location == (mode <= 1 ? 7 : 0));
                if (mode == 4) CHECK(string->sval == pg_query__string__descriptor.fields[0].default_value);
                else {
                    CHECK(string->sval == state.blocks[3]);
                    CHECK(memcmp(string->sval, mode == 5 ? "" : "a\0b\0", text_allocation_size) == 0);
                }
            }
            state.destroying = 1;
            protobuf_c_message_free_unpacked(&node->base, mode == 0 ? &allocator : NULL);
        }
#ifdef SQLPARSER_FRESH_DEFAULT_WRAPPERS
        default_state = NULL;
#endif
        CHECK(state.allocated == state.freed);
      }
    }
    puts("protobuf fresh-member allocator order, presence publication, embedded NUL and OOM tests passed");
    return 0;
}
