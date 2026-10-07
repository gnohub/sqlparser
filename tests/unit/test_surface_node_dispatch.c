/* Differential oracle: link this test with the generic reference walker,
 * compiled separately with its three public entry points renamed _reference.
 * The disabled-optimization reference can also be checked against a reference build.
 * No test-only production API or implementation inclusion is needed. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/dialect/sqlparser_dialect_ast_surface_internal.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s [case %s]\n", \
    __FILE__, __LINE__, #x, current_case); abort(); } } while (0)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *current_case = "initialization";
static size_t comparisons, traversals, callback_coverage;
void sqlparser_dialect_ast_surface_visit_reference(const PgQuery__ParseResult *,
    const sqlparser_dialect_ast_surface_visitor_t *);
void sqlparser_dialect_ast_surface_visit_roots_reference(ProtobufCMessage *const *,
    size_t, size_t, const sqlparser_dialect_ast_surface_visitor_t *);

#ifdef SQLPARSER_SURFACE_ALLOC_WRAPPERS
static int in_traversal;
static size_t allocation_calls, free_calls;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
char *__real_strdup(const char *);
char *__real_strndup(const char *, size_t);
void *__wrap_malloc(size_t n) { if (in_traversal) allocation_calls++; return __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t s) { if (in_traversal) allocation_calls++; return __real_calloc(n, s); }
void *__wrap_realloc(void *p, size_t n) { if (in_traversal) allocation_calls++; return __real_realloc(p, n); }
void __wrap_free(void *p) { if (in_traversal) free_calls++; __real_free(p); }
char *__wrap_strdup(const char *s) { if (in_traversal) allocation_calls++; return __real_strdup(s); }
char *__wrap_strndup(const char *s, size_t n) { if (in_traversal) allocation_calls++; return __real_strndup(s, n); }
#endif
static void traversal_begin(void)
{
#ifdef SQLPARSER_SURFACE_ALLOC_WRAPPERS
    CHECK(!in_traversal); allocation_calls = free_calls = 0U; in_traversal = 1;
#endif
}
static void traversal_end(void)
{
#ifdef SQLPARSER_SURFACE_ALLOC_WRAPPERS
    in_traversal = 0; CHECK(allocation_calls == 0U && free_calls == 0U);
#endif
    traversals++;
}

enum { STRING, PARAM, RELATION, DERIVED, INSERT_RELATION, JOIN, LIMIT, SETOP, N_CALLBACKS };
typedef struct { size_t statement; uint64_t value; int kind, detail; } Event;
typedef struct {
    Event events[16384]; size_t count; int mutate;
    PgQuery__Node *parent;
    int next_cases[4]; ProtobufCMessage *next_children[4];
    size_t change_count, change_index;
} Log;
static Log logs[2];
static uint64_t string_hash(const char *s)
{
    uint64_t h = UINT64_C(1469598103934665603);
    if (s == NULL) return 0;
    while (*s) h = (h ^ (unsigned char)*s++) * UINT64_C(1099511628211);
    return h;
}
static void set_node(PgQuery__Node *n, int active, ProtobufCMessage *child)
{
    CHECK(sizeof(n->node_case) == sizeof(active));
    memcpy(&n->node_case, &active, sizeof(active));
    memcpy((char *)n + pg_query__node__descriptor.fields[0].offset, &child, sizeof(child));
}
static void record(Log *log, int kind, size_t statement, uint64_t value, int detail)
{
    Event *e;
    CHECK(log->count < COUNT(log->events));
    e = &log->events[log->count++];
    e->kind = kind; e->statement = statement; e->value = value; e->detail = detail;
    callback_coverage |= (size_t)1U << kind;
    if (log->parent != NULL && log->change_index < log->change_count) {
        size_t i = log->change_index++;
        set_node(log->parent, log->next_cases[i], log->next_children[i]);
    }
}
static void on_string(PgQuery__AConst *n, size_t s, void *p)
{
    Log *l = p;
    const char *v = n->val_case == PG_QUERY__A__CONST__VAL_SVAL ? n->sval->sval : n->bsval->bsval;
    record(l, STRING, s, string_hash(v), n->location);
    if (l->mutate) n->location += 7;
}
static void on_param(PgQuery__ParamRef *n, size_t s, void *p)
{
    Log *l = p;
    record(l, PARAM, s, (uint64_t)n->number, n->location);
    if (l->mutate) n->number = 1000 + (int32_t)l->count;
}
static void on_relation(PgQuery__RangeVar *n, size_t s, void *p)
{
    Log *l = p; record(l, RELATION, s, string_hash(n->relname), n->location);
    if (l->mutate) n->location += 11;
}
static void on_derived(PgQuery__RangeSubselect *n, size_t s, void *p)
{
    Log *l = p; record(l, DERIVED, s, (uint64_t)n->lateral, 0);
    if (l->mutate) n->lateral = !n->lateral;
}
static void on_insert(PgQuery__RangeVar *n, size_t s, void *p)
{
    Log *l = p; record(l, INSERT_RELATION, s, string_hash(n->relname), n->location);
    if (l->mutate) n->location += 13;
}
static void on_join(PgQuery__JoinExpr *n, size_t s, void *p)
{
    Log *l = p; record(l, JOIN, s, (uint64_t)n->jointype, n->rtindex);
    if (l->mutate) n->rtindex += 17;
}
static void on_limit(PgQuery__SelectStmt *n, size_t s, void *p)
{
    Log *l = p; record(l, LIMIT, s, (uint64_t)n->limit_clause_style, n->group_distinct);
    if (l->mutate) n->group_distinct = !n->group_distinct;
}
static void on_setop(PgQuery__SelectStmt *n, size_t s, void *p)
{
    Log *l = p; record(l, SETOP, s, (uint64_t)n->op, n->all);
    if (l->mutate) n->all = !n->all;
}
static sqlparser_dialect_ast_surface_visitor_t visitor(Log *l)
{
    sqlparser_dialect_ast_surface_visitor_t v = {0};
    v.context = l; v.string_literal = on_string; v.param_ref = on_param;
    v.relation = on_relation; v.derived_relation = on_derived;
    v.insert_relation = on_insert; v.join = on_join;
    v.select_limit = on_limit; v.set_operation = on_setop;
    return v;
}
static void reset_logs(int mutate)
{
    memset(logs, 0, sizeof(logs)); logs[0].mutate = logs[1].mutate = mutate;
}
static void compare_logs(void)
{
    CHECK(logs[0].count == logs[1].count);
    CHECK(memcmp(logs[0].events, logs[1].events, logs[0].count * sizeof(Event)) == 0);
    CHECK(logs[0].change_index == logs[1].change_index);
}
static void same_bytes(const ProtobufCMessage *a, const ProtobufCMessage *b)
{
    size_t na, nb; unsigned char *wa, *wb;
    if (a == NULL || b == NULL) { CHECK(a == b); return; }
    na = protobuf_c_message_get_packed_size(a); nb = protobuf_c_message_get_packed_size(b);
    CHECK(na == nb); wa = malloc(na + 1U); wb = malloc(nb + 1U); CHECK(wa && wb);
    wa[na] = wb[nb] = 0xa5;
    CHECK(protobuf_c_message_pack(a, wa) == na && wa[na] == 0xa5);
    CHECK(protobuf_c_message_pack(b, wb) == nb && wb[nb] == 0xa5);
    CHECK(memcmp(wa, wb, na) == 0); free(wa); free(wb); comparisons++;
}
static void run_roots(ProtobufCMessage *a, ProtobufCMessage *b, size_t index)
{
    ProtobufCMessage *ra[] = {NULL, a, NULL}, *rb[] = {NULL, b, NULL};
    sqlparser_dialect_ast_surface_visitor_t va = visitor(&logs[0]), vb = visitor(&logs[1]);
    traversal_begin(); sqlparser_dialect_ast_surface_visit_roots_reference(ra, COUNT(ra), index, &va); traversal_end();
    traversal_begin(); sqlparser_dialect_ast_surface_visit_roots(rb, COUNT(rb), index, &vb); traversal_end();
    compare_logs();
    for (size_t i = 0U; i < logs[0].count; i++) CHECK(logs[0].events[i].statement == index);
}
static void run_parse(ProtobufCMessage *a, ProtobufCMessage *b)
{
    PgQuery__RawStmt empty = PG_QUERY__RAW_STMT__INIT;
    PgQuery__RawStmt ar = PG_QUERY__RAW_STMT__INIT, br = PG_QUERY__RAW_STMT__INIT;
    PgQuery__RawStmt *as[] = {NULL, &empty, &ar, NULL}, *bs[] = {NULL, &empty, &br, NULL};
    PgQuery__ParseResult pa = PG_QUERY__PARSE_RESULT__INIT, pb = PG_QUERY__PARSE_RESULT__INIT;
    sqlparser_dialect_ast_surface_visitor_t va = visitor(&logs[0]), vb = visitor(&logs[1]);
    ar.stmt = (PgQuery__Node *)a; br.stmt = (PgQuery__Node *)b;
    pa.n_stmts = COUNT(as); pa.stmts = as; pb.n_stmts = COUNT(bs); pb.stmts = bs;
    traversal_begin(); sqlparser_dialect_ast_surface_visit_reference(&pa, &va); traversal_end();
    traversal_begin(); sqlparser_dialect_ast_surface_visit(&pb, &vb); traversal_end(); compare_logs();
    for (size_t i = 0U; i < logs[0].count; i++) CHECK(logs[0].events[i].statement == 2U);
}
static void exercise(ProtobufCMessage *a, ProtobufCMessage *b)
{
    for (int mutate = 0; mutate <= 1; mutate++) {
        reset_logs(mutate); run_roots(a, b, 73U); same_bytes(a, b);
        reset_logs(mutate); run_parse(a, b); same_bytes(a, b);
    }
}

typedef struct { void *items[8192]; size_t count, serial; } Pool;
static void *pool_alloc(Pool *p, size_t size)
{
    void *v = calloc(1U, size); CHECK(v && p->count < COUNT(p->items)); p->items[p->count++] = v; return v;
}
static void pool_clear(Pool *p)
{
    while (p->count) free(p->items[--p->count]);
    p->serial = 0U;
}
static ProtobufCMessage *new_message(Pool *p, const ProtobufCMessageDescriptor *d)
{
    ProtobufCMessage *m = pool_alloc(p, d->sizeof_message); CHECK(d->message_init != NULL); d->message_init(m); return m;
}
/* Bounded, descriptor-valid fixtures populate every message field (including
 * repeated children) without introducing cycles or allocating during a walk. */
static ProtobufCMessage *nested_message(Pool *p, const ProtobufCMessageDescriptor *d, unsigned depth)
{
    ProtobufCMessage *m = new_message(p, d);
    unsigned char *base = (unsigned char *)m;
    unsigned used_oneof[16]; size_t n_used = 0U;
    p->serial++;
    if (d == &pg_query__param_ref__descriptor) {
        ((PgQuery__ParamRef *)m)->number = (int32_t)p->serial; return m;
    }
    if (d == &pg_query__a__const__descriptor) {
        PgQuery__AConst *v = (PgQuery__AConst *)m;
        PgQuery__String *s = (PgQuery__String *)new_message(p, &pg_query__string__descriptor);
        s->sval = (char *)"surface literal"; v->val_case = PG_QUERY__A__CONST__VAL_SVAL; v->sval = s; return m;
    }
    if (d == &pg_query__node__descriptor) {
        const ProtobufCMessageDescriptor *child = p->serial % 2U ? &pg_query__param_ref__descriptor : &pg_query__a__const__descriptor;
        int active = child == &pg_query__param_ref__descriptor ? PG_QUERY__NODE__NODE_PARAM_REF : PG_QUERY__NODE__NODE_A_CONST;
        set_node((PgQuery__Node *)m, active, nested_message(p, child, 0)); return m;
    }
    if (depth == 0U) return m;
    if (d == &pg_query__select_stmt__descriptor) {
        ((PgQuery__SelectStmt *)m)->op = PG_QUERY__SET_OPERATION__SETOP_NONE;
        ((PgQuery__SelectStmt *)m)->limit_clause_style = PG_QUERY__LIMIT_CLAUSE_STYLE__LIMIT_CLAUSE_STYLE_LIMIT;
    }
    for (unsigned i = 0U; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = &d->fields[i]; ProtobufCMessage *child;
        if (f->type != PROTOBUF_C_TYPE_MESSAGE) continue;
        if (f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) {
            size_t k;
            for (k = 0; k < n_used; k++) if (used_oneof[k] == f->quantifier_offset) break;
            if (k < n_used) continue;
            CHECK(n_used < COUNT(used_oneof)); used_oneof[n_used++] = f->quantifier_offset;
            int active = (int)f->id; memcpy(base + f->quantifier_offset, &active, sizeof(active));
        }
        if (f->label == PROTOBUF_C_LABEL_REPEATED) {
            size_t count = 2U; ProtobufCMessage **items = pool_alloc(p, count * sizeof(*items));
            for (size_t k = 0; k < count; k++) items[k] = nested_message(p, f->descriptor, depth - 1U);
            memcpy(base + f->quantifier_offset, &count, sizeof(count)); memcpy(base + f->offset, &items, sizeof(items));
        } else {
            child = nested_message(p, f->descriptor, depth - 1U); memcpy(base + f->offset, &child, sizeof(child));
        }
    }
    return m;
}
static void all_alternatives(void)
{
    Pool p[2] = {{{0},0,0},{{0},0,0}}; char name[100];
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    CHECK(d->n_fields == 268U);
    for (unsigned i = 0U; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        CHECK(f->type == PROTOBUF_C_TYPE_MESSAGE && (f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF));
        for (unsigned mode = 0U; mode < 4U; mode++) {
            PgQuery__Node n[2] = {PG_QUERY__NODE__INIT, PG_QUERY__NODE__INIT};
            snprintf(name, sizeof(name), "arm %u %s mode %u", f->id, f->name, mode); current_case = name;
            for (size_t k = 0; k < 2U; k++) {
                ProtobufCMessage *child = mode == 0U ? NULL : mode == 1U ? new_message(&p[k], f->descriptor) :
                    mode == 2U ? nested_message(&p[k], f->descriptor, 3U) : nested_message(&p[k], &pg_query__param_ref__descriptor, 0U);
                set_node(&n[k], (int)f->id, child);
            }
            exercise(&n[0].base, &n[1].base);
            if (mode == 3U) CHECK(logs[0].count == 1U && logs[0].events[0].kind == PARAM);
            pool_clear(&p[0]); pool_clear(&p[1]);
        }
    }
    puts("surface Node: 268 arms, NULL/default/nested/actual-descriptor children passed");
}
static void invalid_and_copied_descriptors(void)
{
    static const int invalid[] = {0, -1, INT_MIN, 269, INT_MAX};
    ProtobufCMessageDescriptor copy = pg_query__node__descriptor;
    ProtobufCMessage no_descriptor = {NULL, 0U, NULL};
    current_case = "invalid discriminators, copied descriptors, and null input";
    for (size_t k = 0; k < COUNT(invalid); k++) for (int copied = 0; copied <= 1; copied++) {
        PgQuery__Node n[2] = {PG_QUERY__NODE__INIT, PG_QUERY__NODE__INIT};
        for (size_t i = 0; i < 2U; i++) { set_node(&n[i], invalid[k], (ProtobufCMessage *)(uintptr_t)1U); if (copied) n[i].base.descriptor = &copy; }
        exercise(&n[0].base, &n[1].base); CHECK(logs[0].count == 0U);
    }
    for (unsigned k = 0; k < pg_query__node__descriptor.n_fields; k++) {
        PgQuery__Node n[2] = {PG_QUERY__NODE__INIT, PG_QUERY__NODE__INIT};
        PgQuery__ParamRef p[2] = {PG_QUERY__PARAM_REF__INIT, PG_QUERY__PARAM_REF__INIT};
        for (size_t i = 0; i < 2U; i++) { n[i].base.descriptor = &copy; set_node(&n[i], (int)pg_query__node__descriptor.fields[k].id, &p[i].base); }
        exercise(&n[0].base, &n[1].base); CHECK(logs[0].count == 1U);
    }
    reset_logs(1); run_roots(NULL, NULL, SIZE_MAX); run_roots(&no_descriptor, &no_descriptor, SIZE_MAX); CHECK(logs[0].count == 0U);
    traversal_begin();
    sqlparser_dialect_ast_surface_visit(NULL, NULL); sqlparser_dialect_ast_surface_visit_reference(NULL, NULL);
    sqlparser_dialect_ast_surface_visit_roots(NULL, 0U, 0U, NULL); sqlparser_dialect_ast_surface_visit_roots_reference(NULL, 0U, 0U, NULL);
    { ProtobufCMessage *r[] = {(ProtobufCMessage *)(uintptr_t)1U};
      sqlparser_dialect_ast_surface_visit_roots(r, 1U, 0U, NULL); sqlparser_dialect_ast_surface_visit_roots_reference(r, 1U, 0U, NULL); }
    traversal_end();
    puts("surface Node: invalid/unset poison pointers, copied descriptors, null inputs passed");
}
static void discriminator_changes(void)
{
    char name[100];
    for (int api = 0; api < 2; api++) for (unsigned j = 0U; j < 268U; j++) for (unsigned mode = 0U; mode < 8U; mode++) {
        PgQuery__Node n[2] = {PG_QUERY__NODE__INIT, PG_QUERY__NODE__INIT};
        PgQuery__ParamRef p[2][2] = {{PG_QUERY__PARAM_REF__INIT, PG_QUERY__PARAM_REF__INIT}, {PG_QUERY__PARAM_REF__INIT, PG_QUERY__PARAM_REF__INIT}};
        int initial = (int)pg_query__node__descriptor.fields[j].id;
        int next = mode == 0U ? (j + 1U < 268U ? (int)pg_query__node__descriptor.fields[j + 1U].id : initial) :
            mode == 1U ? (j ? (int)pg_query__node__descriptor.fields[j - 1U].id : initial) :
            mode == 2U ? initial : mode == 3U ? 0 : mode == 4U ? -1 : mode == 5U ? INT_MAX :
            (j + 1U < 268U ? (int)pg_query__node__descriptor.fields[j + 1U].id : initial);
        size_t expected = (mode == 0U || mode == 7U) && j + 1U < 268U ? 2U : 1U;
        snprintf(name, sizeof(name), "parent discriminator arm %u mode %u API %d", j, mode, api); current_case = name; reset_logs(1);
        for (size_t k = 0; k < 2U; k++) {
            p[k][0].number = 11; p[k][1].number = 22; set_node(&n[k], initial, &p[k][0].base);
            logs[k].parent = &n[k]; logs[k].change_count = 1U; logs[k].next_cases[0] = next;
            logs[k].next_children[0] = mode == 6U ? NULL : mode == 7U ? &p[k][0].base : &p[k][1].base;
            if (mode == 3U || mode == 4U || mode == 5U) logs[k].next_children[0] = (ProtobufCMessage *)(uintptr_t)1U;
        }
        if (api == 0) run_roots(&n[0].base, &n[1].base, 901U);
        else run_parse(&n[0].base, &n[1].base);
        CHECK(logs[0].count == expected);
        CHECK(logs[0].events[0].value == 11U);
        if (expected == 2U) CHECK(logs[0].events[1].value == (mode == 7U ? 1001U : 22U));
        same_bytes(&n[0].base, &n[1].base);
        same_bytes(&p[0][0].base, &p[1][0].base); same_bytes(&p[0][1].base, &p[1][1].base);
    }
    puts("surface Node: 4,288 callback-driven later/earlier/same/unknown/null/reused-pointer changes passed");
}
/* A callback runs inside the previously loaded child to completion. Only its
 * final parent case matters when the forward field walk resumes. */
static void multihop_and_midchild_changes(void)
{
    for (int api = 0; api < 2; api++) for (int mode = 0; mode < 5; mode++) {
        PgQuery__Node n[2] = {PG_QUERY__NODE__INIT, PG_QUERY__NODE__INIT};
        PgQuery__ParamRef p[2][4];
        PgQuery__List list[2] = {PG_QUERY__LIST__INIT, PG_QUERY__LIST__INIT};
        PgQuery__Node *items[2][2];
        int initial = mode == 0 ? 1 : 20;
        size_t expected = mode == 1 || mode == 3 ? 2U : 3U;
        current_case = "multi-hop and changes inside a still-running child"; reset_logs(1);
        for (size_t k = 0; k < 2U; k++) {
            for (size_t j = 0; j < 4U; j++) { pg_query__param_ref__init(&p[k][j]); p[k][j].number = (int32_t)(41U + j); }
            items[k][0] = (PgQuery__Node *)&p[k][0]; items[k][1] = (PgQuery__Node *)&p[k][1];
            list[k].n_items = COUNT(items[k]); list[k].items = items[k];
            set_node(&n[k], initial, mode == 0 ? &p[k][0].base : &list[k].base);
            logs[k].parent = &n[k]; logs[k].change_count = mode == 0 ? 3U : 2U;
            if (mode == 0) {
                logs[k].next_cases[0] = 127; logs[k].next_children[0] = &p[k][1].base;
                logs[k].next_cases[1] = 268; logs[k].next_children[1] = &p[k][2].base;
                logs[k].next_cases[2] = 1; logs[k].next_children[2] = &p[k][3].base;
            } else {
                logs[k].next_cases[0] = mode == 2 ? 5 : mode == 4 ? 0 : 200;
                logs[k].next_children[0] = mode == 4 ? (ProtobufCMessage *)(uintptr_t)1U : &p[k][3].base;
                logs[k].next_cases[1] = mode == 1 ? 5 : mode == 3 ? 20 : 200;
                logs[k].next_children[1] = &p[k][2].base;
            }
        }
        if (api == 0) run_roots(&n[0].base, &n[1].base, SIZE_MAX); else run_parse(&n[0].base, &n[1].base);
        CHECK(logs[0].count == expected);
        for (size_t j = 0; j < expected; j++) CHECK(logs[0].events[j].value == 41U + j);
        same_bytes(&n[0].base, &n[1].base); same_bytes(&list[0].base, &list[1].base);
        for (size_t j = 0; j < 4U; j++) same_bytes(&p[0][j].base, &p[1][j].base);
    }
    puts("surface Node: multi-hop and mid-child discriminator mutations passed");
}
/* A foreign message intentionally has different offsets and non-contiguous
 * oneof tags. The exact descriptor gate must leave its generic field walk alone. */
typedef struct {
    ProtobufCMessage base; uint64_t padding[3]; int arm; ProtobufCMessage *child;
    size_t count; ProtobufCMessage **items;
} Foreign;
static void foreign_descriptors(void)
{
    ProtobufCMessageDescriptor descriptor = pg_query__node__descriptor;
    ProtobufCFieldDescriptor fields[3];
    Foreign f[2] = {0};
    PgQuery__ParamRef p[2][3] = {{PG_QUERY__PARAM_REF__INIT, PG_QUERY__PARAM_REF__INIT, PG_QUERY__PARAM_REF__INIT}, {PG_QUERY__PARAM_REF__INIT, PG_QUERY__PARAM_REF__INIT, PG_QUERY__PARAM_REF__INIT}};
    ProtobufCMessage *items[2][2] = {{&p[0][1].base, &p[0][2].base}, {&p[1][1].base, &p[1][2].base}};
    current_case = "foreign descriptor offsets, oneof, repeated fields";
    fields[0] = fields[1] = fields[2] = pg_query__node__descriptor.fields[0];
    fields[0].id = 7U; fields[1].id = 9001U;
    for (size_t k = 0U; k < 2U; k++) { fields[k].offset = offsetof(Foreign, child); fields[k].quantifier_offset = offsetof(Foreign, arm); }
    fields[2].id = 9999U; fields[2].flags = 0U; fields[2].label = PROTOBUF_C_LABEL_REPEATED;
    fields[2].offset = offsetof(Foreign, items); fields[2].quantifier_offset = offsetof(Foreign, count);
    descriptor.n_fields = COUNT(fields); descriptor.fields = fields; descriptor.sizeof_message = sizeof(Foreign);
    for (size_t k = 0U; k < 2U; k++) {
        f[k].base.descriptor = &descriptor; f[k].arm = 9001; f[k].child = &p[k][0].base;
        f[k].count = COUNT(items[k]); f[k].items = items[k];
        for (size_t j = 0U; j < 3U; j++) p[k][j].number = (int32_t)(31U + j);
    }
    exercise(&f[0].base, &f[1].base); CHECK(logs[0].count == 3U);
    puts("surface Node: foreign descriptor generic traversal passed");
}
static PgQuery__ParseResult *parse_fixture(const char *sql, sqlparser_dialect_t dialect)
{
    sqlparser_parse_options_t options; sqlparser_error_t error; sqlparser_handle_t *handle = NULL;
    PgQuery__ParseResult *ast; size_t size; uint8_t *wire;
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    sqlparser_status_t status = sqlparser_parse_with_options(sql, &options, &handle, &error);
    if (status != SQLPARSER_STATUS_OK) fprintf(stderr, "dialect %d parse: %s\nSQL: %s\n", (int)dialect, error.message, sql);
    CHECK(status == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_handle_ensure_ast(handle, &error) == SQLPARSER_STATUS_OK);
    size = protobuf_c_message_get_packed_size(&handle->ast->base); wire = malloc(size + 1U); CHECK(wire);
    CHECK(protobuf_c_message_pack(&handle->ast->base, wire) == size);
    ast = pg_query__parse_result__unpack(NULL, size, wire); CHECK(ast); free(wire); sqlparser_handle_destroy(handle); return ast;
}
static void parsed_fixture(const char *sql, sqlparser_dialect_t dialect, const int *order, size_t order_count)
{
    PgQuery__ParseResult *ast[2] = {parse_fixture(sql, dialect), parse_fixture(sql, dialect)};
    sqlparser_dialect_ast_surface_visitor_t va, vb;
    for (int mutate = 0; mutate <= 1; mutate++) {
        reset_logs(mutate); va = visitor(&logs[0]); vb = visitor(&logs[1]);
        traversal_begin(); sqlparser_dialect_ast_surface_visit_reference(ast[0], &va); traversal_end();
        traversal_begin(); sqlparser_dialect_ast_surface_visit(ast[1], &vb); traversal_end(); compare_logs();
        CHECK(logs[0].count > 0U);
        if (order != NULL) { CHECK(logs[0].count == order_count); for (size_t i = 0; i < order_count; i++) { CHECK(logs[0].events[i].kind == order[i]); CHECK(logs[0].events[i].statement == (i < 9U ? 0U : i < 11U ? 1U : i < 13U ? 2U : 3U)); } }
        for (size_t i = 0U; i < logs[0].count; i++) { CHECK(logs[0].events[i].statement < ast[0]->n_stmts); if (i) CHECK(logs[0].events[i-1U].statement <= logs[0].events[i].statement); }
        same_bytes(&ast[0]->base, &ast[1]->base);
        /* The roots API carries one caller-supplied index through all roots. */
        ProtobufCMessage **roots[2] = {calloc(ast[0]->n_stmts, sizeof(*roots[0])), calloc(ast[1]->n_stmts, sizeof(*roots[1]))}; CHECK(roots[0] && roots[1]);
        for (size_t k = 0; k < 2U; k++) for (size_t i = 0; i < ast[k]->n_stmts; i++) roots[k][i] = (ProtobufCMessage *)ast[k]->stmts[i]->stmt;
        reset_logs(mutate);
        traversal_begin(); sqlparser_dialect_ast_surface_visit_roots_reference(roots[0], ast[0]->n_stmts, SIZE_MAX, &va); traversal_end();
        traversal_begin(); sqlparser_dialect_ast_surface_visit_roots(roots[1], ast[1]->n_stmts, SIZE_MAX, &vb); traversal_end(); compare_logs();
        for (size_t i = 0; i < logs[0].count; i++) CHECK(logs[0].events[i].statement == SIZE_MAX);
        same_bytes(&ast[0]->base, &ast[1]->base); free(roots[0]); free(roots[1]);
    }
    pg_query__parse_result__free_unpacked(ast[0], NULL); pg_query__parse_result__free_unpacked(ast[1], NULL);
}
static void sql_fixtures(void)
{
    static const char *const bind[] = {"$1", "?", ":id", "@id", ":id", ":id", "?", "$1", "@id", ":id", "?", "$1", "@id"};
    static const int exact_order[] = {STRING, PARAM, RELATION, JOIN, DERIVED, STRING, PARAM, LIMIT, PARAM, INSERT_RELATION, STRING, RELATION, PARAM, RELATION, STRING};
    char sql[4096], label[100];
    current_case = "exact typed traversal order and statement indices";
    parsed_fixture("SELECT 'head', $1 FROM a JOIN (SELECT 'inner' AS v) d ON a.id=$2 LIMIT $3; INSERT INTO outbox(v) VALUES ('insert'); UPDATE a SET id=$4; DELETE FROM a WHERE v='delete'", SQLPARSER_DIALECT_POSTGRESQL, exact_order, COUNT(exact_order));
    for (int dialect = SQLPARSER_DIALECT_POSTGRESQL; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        snprintf(label, sizeof(label), "real SQL dialect %d", dialect); current_case = label;
        snprintf(sql, sizeof(sql), "SELECT u.id, 'label' AS label FROM users u JOIN (SELECT id FROM audit WHERE state='ready') a ON u.id=a.id WHERE u.id=%s; INSERT INTO audit(id,state) VALUES (%s,'new'); UPDATE users SET name='changed' WHERE id=%s; DELETE FROM audit WHERE id=%s; SELECT id FROM users UNION ALL SELECT id FROM audit", bind[dialect], bind[dialect], bind[dialect], bind[dialect]);
        parsed_fixture(sql, (sqlparser_dialect_t)dialect, NULL, 0U);
    }
    current_case = "PostgreSQL CTE, limit, view, CTAS, merge, values, and DDL";
    parsed_fixture("WITH q AS (SELECT $1 AS id, 'cte' AS name) SELECT * FROM q LEFT JOIN users u ON q.id=u.id ORDER BY u.id LIMIT $2 OFFSET $3; CREATE VIEW report AS SELECT 'view' AS v FROM users; CREATE TABLE report_copy AS SELECT 'copy' AS v FROM users; MERGE INTO users u USING audit a ON u.id=a.id WHEN MATCHED THEN UPDATE SET name='merge' WHEN NOT MATCHED THEN INSERT (id,name) VALUES (a.id,'new'); SELECT * FROM (VALUES ($4,'value'),($5,'other')) v(id,name); ALTER TABLE users ADD COLUMN extra text", SQLPARSER_DIALECT_POSTGRESQL, NULL, 0U);
    puts("surface Node: exact callback order, mutations, and realistic SQL across all 13 dialects passed");
}
int main(void)
{
    all_alternatives(); invalid_and_copied_descriptors(); discriminator_changes(); multihop_and_midchild_changes(); foreign_descriptors(); sql_fixtures();
    current_case = "coverage"; CHECK(callback_coverage == ((size_t)1U << N_CALLBACKS) - 1U);
    printf("surface Node differential: %zu byte comparisons, %zu traversals, all 8 callback types", comparisons, traversals);
#ifdef SQLPARSER_SURFACE_ALLOC_WRAPPERS
    puts(", zero traversal allocations/frees");
#else
    puts(" (allocation wrappers disabled)");
#endif
    return 0;
}
