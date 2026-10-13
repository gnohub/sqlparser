/* Regression coverage for spelling gc dispatch. */
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL|%s|line=%d|%s\n", label, __LINE__, #x); exit(1); } checks++; } while (0)
static const char *label = "init";
static size_t checks, cases, canonical_cases, copied_cases, reachable_variants, leaf_variants;
typedef struct { void *items[4096]; size_t count; } Arena;
static void *owned(Arena *a, size_t n)
{ void *p = calloc(1, n); REQUIRE(p && a->count < COUNT(a->items)); a->items[a->count++] = p; return p; }
static ProtobufCMessage *message(Arena *a, const ProtobufCMessageDescriptor *d)
{ ProtobufCMessage *m = owned(a, d->sizeof_message); REQUIRE(d->message_init); d->message_init(m); REQUIRE(m->descriptor == d); return m; }
static void arena_close(Arena *a)
{ while (a->count) free(a->items[--a->count]); }
static int32_t tagged(size_t group, unsigned part)
{ return (int32_t)((int64_t)SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_BASE + ((int64_t)group << SQLPARSER_PROTO_IDENTIFIER_SPELLING_COMPONENT_BITS) + part); }
static void store_case(ProtobufCMessage *m, const ProtobufCFieldDescriptor *f)
{ if (f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) { int v = (int)f->id; memcpy((char *)m + f->quantifier_offset, &v, sizeof(v)); } }
static void store_child(Arena *a, ProtobufCMessage *m, const ProtobufCFieldDescriptor *f, ProtobufCMessage *child)
{
    store_case(m, f);
    if (f->label == PROTOBUF_C_LABEL_REPEATED) {
        size_t n = 1; ProtobufCMessage **items = owned(a, sizeof(*items)); items[0] = child;
        memcpy((char *)m + f->quantifier_offset, &n, sizeof(n));
        memcpy((char *)m + f->offset, &items, sizeof(items));
    } else memcpy((char *)m + f->offset, &child, sizeof(child));
}

/* A schema-only shortest-path oracle. It independently finds a legitimate
 * initialized descendant with an exact, singular INT32 location field.
 * Distances strictly decrease during construction, so cycles never recurse.
 */
typedef struct { const ProtobufCMessageDescriptor *d; const ProtobufCFieldDescriptor *via; unsigned distance; } Schema;
static Schema schema[512];
static size_t schema_count;
static size_t schema_index(const ProtobufCMessageDescriptor *d)
{ size_t i; for (i = 0; i < schema_count; i++) if (schema[i].d == d) return i; REQUIRE(schema_count < COUNT(schema)); schema[schema_count] = (Schema){d, NULL, UINT_MAX}; return schema_count++; }
static int is_location(const ProtobufCFieldDescriptor *f)
{ return f->type == PROTOBUF_C_TYPE_INT32 && f->label != PROTOBUF_C_LABEL_REPEATED && !strcmp(f->name, "location"); }
static void build_schema(void)
{
    size_t i; int changed;
    schema_index(&pg_query__node__descriptor);
    for (i = 0; i < schema_count; i++) {
        unsigned j; const ProtobufCMessageDescriptor *d = schema[i].d;
        for (j = 0; j < d->n_fields; j++) {
            const ProtobufCFieldDescriptor *f = d->fields + j;
            if (is_location(f)) { schema[i].via = f; schema[i].distance = 0; }
            if (f->type == PROTOBUF_C_TYPE_MESSAGE) schema_index(f->descriptor);
        }
    }
    do {
        changed = 0;
        for (i = 0; i < schema_count; i++) {
            unsigned j; const ProtobufCMessageDescriptor *d = schema[i].d;
            for (j = 0; j < d->n_fields; j++) {
                const ProtobufCFieldDescriptor *f = d->fields + j; size_t ci;
                if (f->type != PROTOBUF_C_TYPE_MESSAGE) continue;
                ci = schema_index(f->descriptor);
                if (schema[ci].distance != UINT_MAX && schema[ci].distance + 1 < schema[i].distance) {
                    schema[i].distance = schema[ci].distance + 1; schema[i].via = f; changed = 1;
                }
            }
        }
    } while (changed);
}
static ProtobufCMessage *path_message(Arena *a, const ProtobufCMessageDescriptor *d, int32_t location)
{
    ProtobufCMessage *m = message(a, d); Schema *s = schema + schema_index(d);
    if (!s->via) return m;
    if (is_location(s->via)) {
        store_case(m, s->via); memcpy((char *)m + s->via->offset, &location, sizeof(location));
    } else store_child(a, m, s->via, path_message(a, s->via->descriptor, location));
    return m;
}

typedef struct {
    Arena arena; sqlparser_handle_t handle; PgQuery__ParseResult *root; PgQuery__RawStmt *raw;
} Fixture;
static void groups(Fixture *f, size_t n, size_t capacity)
{
    size_t i, j; sqlparser_handle_t *h = &f->handle;
    REQUIRE(n <= 63 && capacity >= n);
    h->identifier_spellings = calloc(capacity, sizeof(*h->identifier_spellings)); REQUIRE(h->identifier_spellings);
    h->identifier_spelling_count = n; h->identifier_spelling_capacity = capacity;
    h->identifier_spelling_free_group = n; h->identifier_spelling_build_group = SIZE_MAX;
    for (i = 0; i < n; i++) {
        h->identifier_spellings[i].part_count = 16;
        h->identifier_spellings[i].alias_style = (uint8_t)(1 + i % 2);
        for (j = 0; j < 16; j++) { char text[48]; snprintf(text, sizeof(text), "group_%zu_part_%zu", i, j); h->identifier_spellings[i].parts[j] = strdup(text); REQUIRE(h->identifier_spellings[i].parts[j]); }
    }
}
static void fixture_init(Fixture *f, size_t n, size_t capacity)
{
    memset(f, 0, sizeof(*f)); f->root = (PgQuery__ParseResult *)message(&f->arena, &pg_query__parse_result__descriptor);
    f->raw = (PgQuery__RawStmt *)message(&f->arena, &pg_query__raw_stmt__descriptor);
    f->root->n_stmts = 1; f->root->stmts = owned(&f->arena, sizeof(*f->root->stmts)); f->root->stmts[0] = f->raw;
    f->handle.ast = f->root; groups(f, n, capacity);
}
static void fixture_close(Fixture *f)
{
    size_t i, j; for (i = 0; i < f->handle.identifier_spelling_count; i++) for (j = 0; j < f->handle.identifier_spellings[i].part_count; j++) free(f->handle.identifier_spellings[i].parts[j]);
    free(f->handle.identifier_spellings); arena_close(&f->arena);
}
static void check_state(Fixture *f, uint64_t mask)
{
    sqlparser_handle_t *h = &f->handle; size_t expected_count = 0, first_free, i, j;
    for (i = 0; i < 63; i++) if (mask & (UINT64_C(1) << i)) expected_count = i + 1;
    first_free = expected_count; for (i = 0; i < expected_count; i++) if (!(mask & (UINT64_C(1) << i))) { first_free = i; break; }
    REQUIRE(h->identifier_spelling_count == expected_count); REQUIRE(h->identifier_spelling_free_group == first_free);
    if (!expected_count) { REQUIRE(!h->identifier_spellings); REQUIRE(h->identifier_spelling_capacity == 0); }
    else REQUIRE(h->identifier_spellings && h->identifier_spelling_capacity >= expected_count);
    for (i = 0; i < expected_count; i++) {
        sqlparser_identifier_spelling_t *g = h->identifier_spellings + i;
        if (!(mask & (UINT64_C(1) << i))) {
            sqlparser_identifier_spelling_t zero = {0}; REQUIRE(!memcmp(g, &zero, sizeof(zero)));
        } else {
            REQUIRE(g->part_count == 16); REQUIRE(g->alias_style == 1 + i % 2);
            for (j = 0; j < 16; j++) { char text[48]; snprintf(text, sizeof(text), "group_%zu_part_%zu", i, j); REQUIRE(g->parts[j] && !strcmp(g->parts[j], text)); }
        }
    }
    printf("STATE|%s|mask=%016" PRIx64 "|count=%zu|capacity=%zu|first_free=%zu\n", label, mask, h->identifier_spelling_count, h->identifier_spelling_capacity, h->identifier_spelling_free_group);
    cases++;
}
static void sweep(Fixture *f, uint64_t mask)
{ sqlparser_handle_sweep_identifier_spellings(&f->handle); check_state(f, mask); }
static PgQuery__Node *aexpr(Fixture *f, int32_t loc)
{
    PgQuery__Node *n = (PgQuery__Node *)message(&f->arena, &pg_query__node__descriptor);
    n->node_case = PG_QUERY__NODE__NODE_A_EXPR;
    n->a_expr = (PgQuery__AExpr *)message(&f->arena, &pg_query__a__expr__descriptor); n->a_expr->location = loc; return n;
}
static void variant_matrix(void)
{
    const ProtobufCMessageDescriptor *nd = &pg_query__node__descriptor;
    unsigned i; int copied, null_active; char name[180];
    REQUIRE(nd->n_fields == 268);
    for (copied = 0; copied < 2; copied++) for (i = 0; i < nd->n_fields; i++) for (null_active = 0; null_active < 2; null_active++) {
        Fixture f; ProtobufCMessageDescriptor copied_descriptor = *nd; const ProtobufCFieldDescriptor *field = nd->fields + i;
        PgQuery__Node *n; ProtobufCMessage *child = NULL; uint64_t expected = 0;
        snprintf(name, sizeof(name), "%s-variant-%03u-%s-%s", copied ? "copied" : "canonical", field->id, field->name, null_active ? "null" : "live"); label = name;
        fixture_init(&f, 4, 16); n = (PgQuery__Node *)message(&f.arena, nd); REQUIRE(field->type == PROTOBUF_C_TYPE_MESSAGE && (field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF));
        if (!null_active) {
            child = path_message(&f.arena, field->descriptor, tagged(2, 15));
            if (schema[schema_index(field->descriptor)].via) expected = UINT64_C(1) << 2;
            if (!copied) { if (expected) reachable_variants++; else leaf_variants++; }
        }
        store_child(&f.arena, &n->base, field, child); if (copied) n->base.descriptor = &copied_descriptor;
        f.raw->stmt = n; sweep(&f, expected); sweep(&f, expected);
        if (copied) copied_cases++; else canonical_cases++;
        fixture_close(&f);
    }
}
static void empty_unknown_cases(void)
{
    const int discriminants[] = {0, -1, 269, INT_MAX}; size_t i; int copied; char name[80];
    for (copied = 0; copied < 2; copied++) for (i = 0; i < COUNT(discriminants); i++) {
        Fixture f; PgQuery__Node *n; ProtobufCMessageDescriptor cd = pg_query__node__descriptor;
        snprintf(name, sizeof(name), "%s-unknown-%d-inactive-live-child", copied ? "copied" : "canonical", discriminants[i]); label = name;
        fixture_init(&f, 4, 4); n = aexpr(&f, tagged(1, 0)); n->node_case = (PgQuery__Node__NodeCase)discriminants[i];
        REQUIRE(!protobuf_c_message_descriptor_get_field(&pg_query__node__descriptor, (unsigned)discriminants[i]));
        if (copied) n->base.descriptor = &cd;
        f.raw->stmt = n; sweep(&f, 0); fixture_close(&f);
    }
}

/* A legitimate extended Node message: initialized embedded Node plus an extra
 * field. The descriptor remains a separate live object with sorted field IDs.
 * This catches accidental use of the fast path by descriptor name or shape.
 */
typedef struct { PgQuery__Node node; int32_t location; } ExtendedNode;
static void custom_node_cases(void)
{
    int active; char name[80];
    for (active = 0; active < 3; active++) {
        Fixture f; ExtendedNode *n; ProtobufCMessageDescriptor cd = pg_query__node__descriptor;
        ProtobufCFieldDescriptor fields[269]; unsigned names[269]; ProtobufCIntRange ranges[3]; unsigned i;
        snprintf(name, sizeof(name), "custom-node-extra-location-active-%d", active); label = name; fixture_init(&f, 8, 32);
        memcpy(fields, cd.fields, 268 * sizeof(*fields));
        fields[268] = (ProtobufCFieldDescriptor){.name="location", .id=1001, .label=PROTOBUF_C_LABEL_NONE, .type=PROTOBUF_C_TYPE_INT32, .offset=offsetof(ExtendedNode, location)};
        for (i=0;i<269;i++) names[i]=i;
        for (i=1;i<269;i++) { unsigned j=i, value=names[i]; while(j && strcmp(fields[names[j-1]].name,fields[value].name)>0) {names[j]=names[j-1];j--;} names[j]=value; }
        REQUIRE(fields[0].id == 1 && fields[267].id == 268);
        ranges[0]=(ProtobufCIntRange){1,0}; ranges[1]=(ProtobufCIntRange){1001,268}; ranges[2]=(ProtobufCIntRange){0,269};
        cd.fields=fields; cd.n_fields=269; cd.sizeof_message=sizeof(*n); cd.fields_sorted_by_name=names; cd.field_ranges=ranges; cd.n_field_ranges=2;
        n=owned(&f.arena,sizeof(*n)); pg_query__node__init(&n->node); n->node.base.descriptor=&cd; n->location=tagged(5,15);
        if(active==1) { PgQuery__Node *live=aexpr(&f,tagged(2,0)); n->node.node_case=live->node_case; n->node.a_expr=live->a_expr; }
        if(active==2) { PgQuery__Node *live=aexpr(&f,tagged(2,0)); n->node.node_case=(PgQuery__Node__NodeCase)999; n->node.a_expr=live->a_expr; }
        f.raw->stmt=&n->node; sweep(&f, (UINT64_C(1)<<5) | (active==1 ? UINT64_C(1)<<2 : 0)); fixture_close(&f);
    }
}

/* A valid custom ParseResult extension permits testing scalar classifications
 * without placing a mismatched child in any generated-message field.
 */
typedef struct {
    PgQuery__ParseResult root;
    union { int32_t i32; uint32_t u32; int64_t i64; uint64_t u64; int32_t *items; } value;
    size_t count;
    int case_id;
} ExtendedRoot;
static void scalar_case(const char *name, ProtobufCType type, ProtobufCLabel field_label, int oneof, int active, int32_t value, int expected)
{
    Fixture f; ExtendedRoot root = {0}; ProtobufCMessageDescriptor cd = pg_query__parse_result__descriptor;
    ProtobufCFieldDescriptor fields[3]; unsigned names[3]={2,1,0}; ProtobufCIntRange ranges[2]={{1,0},{0,3}};
    int32_t repeated[2]={value,value}; uint64_t mask=expected ? UINT64_C(1)<<2 : 0;
    fixture_init(&f,4,16); pg_query__parse_result__init(&root.root); memcpy(fields, cd.fields, 2*sizeof(*fields));
    fields[2]=(ProtobufCFieldDescriptor){.name=name,.id=3,.label=field_label,.type=type,.offset=offsetof(ExtendedRoot,value),.quantifier_offset=field_label==PROTOBUF_C_LABEL_REPEATED?offsetof(ExtendedRoot,count):offsetof(ExtendedRoot,case_id),.flags=oneof?PROTOBUF_C_FIELD_FLAG_ONEOF:0};
    if (type==PROTOBUF_C_TYPE_ENUM) fields[2].descriptor=&pg_query__a__expr__kind__descriptor;
    { unsigned i; for(i=0;i<3;i++)names[i]=i; for(i=1;i<3;i++){unsigned j=i,v=names[i];while(j&&strcmp(fields[names[j-1]].name,fields[v].name)>0){names[j]=names[j-1];j--;}names[j]=v;} }
    cd.fields=fields;cd.n_fields=3;cd.sizeof_message=sizeof(root);cd.fields_sorted_by_name=names;cd.n_field_ranges=1;cd.field_ranges=ranges;
    root.root.base.descriptor=&cd;root.case_id=active?3:0;
    if(field_label==PROTOBUF_C_LABEL_REPEATED){root.value.items=repeated;root.count=2;}
    else if(type==PROTOBUF_C_TYPE_INT64 || type==PROTOBUF_C_TYPE_UINT64 || type==PROTOBUF_C_TYPE_SINT64 || type==PROTOBUF_C_TYPE_SFIXED64 || type==PROTOBUF_C_TYPE_FIXED64) root.value.i64=value;
    else root.value.i32=value;
    f.handle.ast=&root.root;sweep(&f,mask);fixture_close(&f);
}
static void location_cases(void)
{
    static const struct { const char *name; ProtobufCType type; ProtobufCLabel field_label; int oneof,active,expected; } tests[]={
        {"location",PROTOBUF_C_TYPE_INT32,PROTOBUF_C_LABEL_NONE,0,1,1},
        {"location",PROTOBUF_C_TYPE_INT32,PROTOBUF_C_LABEL_OPTIONAL,0,1,1},
        {"location",PROTOBUF_C_TYPE_INT32,PROTOBUF_C_LABEL_OPTIONAL,1,1,1},
        {"location",PROTOBUF_C_TYPE_INT32,PROTOBUF_C_LABEL_OPTIONAL,1,0,0},
        {"stmt_location",PROTOBUF_C_TYPE_INT32,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"end_location",PROTOBUF_C_TYPE_INT32,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"Location",PROTOBUF_C_TYPE_INT32,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"location",PROTOBUF_C_TYPE_INT64,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"location",PROTOBUF_C_TYPE_UINT32,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"location",PROTOBUF_C_TYPE_UINT64,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"location",PROTOBUF_C_TYPE_SINT32,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"location",PROTOBUF_C_TYPE_SFIXED32,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"location",PROTOBUF_C_TYPE_FIXED32,PROTOBUF_C_LABEL_NONE,0,1,0},
        {"location",PROTOBUF_C_TYPE_INT32,PROTOBUF_C_LABEL_REPEATED,0,1,0}
    };
    static const int32_t sentinels[]={0,1,INT32_MAX,-1,SQLPARSER_PROTO_LOCATION_GENERATED,SQLPARSER_PROTO_LOCATION_GENERATED_STYLE_BASE,SQLPARSER_PROTO_LOCATION_SQL_VALUE_CASE_BASE,SQLPARSER_PROTO_LOCATION_SQL_VALUE_CASE_LAST,SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_LAST,SQLPARSER_PROTO_LOCATION_GENERATED_SPELLING_LAST+1};
    size_t i; char name[120];
    for(i=0;i<COUNT(tests);i++){snprintf(name,sizeof(name),"location-field-filter-%zu-%s",i,tests[i].name);label=name;scalar_case(tests[i].name,tests[i].type,tests[i].field_label,tests[i].oneof,tests[i].active,tagged(2,15),tests[i].expected);}
    for(i=0;i<COUNT(sentinels);i++){Fixture f;snprintf(name,sizeof(name),"canonical-location-sentinel-%" PRId32,sentinels[i]);label=name;fixture_init(&f,4,16);f.raw->stmt=aexpr(&f,sentinels[i]);sweep(&f,0);fixture_close(&f);}
    for(i=0;i<4;i++){Fixture f;snprintf(name,sizeof(name),"canonical-tagged-group-%zu-component-15",i);label=name;fixture_init(&f,4,16);f.raw->stmt=aexpr(&f,tagged(i,15));sweep(&f,UINT64_C(1)<<i);fixture_close(&f);}
    {Fixture f;label="canonical-tagged-one-past-group-count";fixture_init(&f,4,16);f.raw->stmt=aexpr(&f,tagged(4,0));sweep(&f,0);fixture_close(&f);}
}
static void shared_mutation_and_holes(void)
{
    Fixture f; PgQuery__Node *list, *a, *b; PgQuery__Node **items; sqlparser_identifier_mutation_t *mutation;
    label="shared-repeated-and-mutation-roots";fixture_init(&f,12,32);a=aexpr(&f,tagged(1,0));b=aexpr(&f,tagged(5,15));
    list=(PgQuery__Node *)message(&f.arena,&pg_query__node__descriptor);list->node_case=PG_QUERY__NODE__NODE_LIST;
    list->list=(PgQuery__List *)message(&f.arena,&pg_query__list__descriptor);items=owned(&f.arena,4*sizeof(*items));items[0]=a;items[1]=a;items[2]=b;items[3]=b;list->list->items=items;list->list->n_items=4;f.raw->stmt=list;
    mutation=owned(&f.arena,4*sizeof(*mutation));mutation[0].original_alias_location=tagged(8,0);mutation[1].original_alias_location=tagged(8,15);mutation[2].original_alias_location=tagged(12,0);mutation[3].original_alias_location=SQLPARSER_PROTO_LOCATION_GENERATED;
    f.handle.identifier_mutations=mutation;f.handle.identifier_mutation_count=4;
    sweep(&f,(UINT64_C(1)<<1)|(UINT64_C(1)<<5)|(UINT64_C(1)<<8));
    label="shared-first-reference-removed";items[0]=b;sweep(&f,(UINT64_C(1)<<1)|(UINT64_C(1)<<5)|(UINT64_C(1)<<8));
    label="shared-last-reference-removed";items[1]=b;sweep(&f,(UINT64_C(1)<<5)|(UINT64_C(1)<<8));
    label="immutable-mutation-root-only";list->list->n_items=0;sweep(&f,UINT64_C(1)<<8);
    label="immutable-first-root-removed";mutation[0].original_alias_location=-1;sweep(&f,UINT64_C(1)<<8);
    label="immutable-final-root-removed";mutation[1].original_alias_location=-1;sweep(&f,0);fixture_close(&f);
    label="no-holes-and-no-shrink";fixture_init(&f,1,2);f.raw->stmt=aexpr(&f,tagged(0,0));sweep(&f,1);fixture_close(&f);
}
static void deep_cases(void)
{
    static const size_t depths[]={1,63,64,65,66,128,256,512,1024};size_t d;char name[100];
    for(d=0;d<COUNT(depths);d++){Fixture f;PgQuery__Node *n;size_t i;snprintf(name,sizeof(name),"canonical-aexpr-nesting-%zu",depths[d]);label=name;fixture_init(&f,4,16);n=aexpr(&f,tagged(2,15));for(i=0;i<depths[d];i++){PgQuery__Node *parent=aexpr(&f,-1);parent->a_expr->lexpr=n;n=parent;}f.raw->stmt=n;sweep(&f,UINT64_C(1)<<2);fixture_close(&f);}
}
static void skip_cases(void)
{
    Fixture f;label="sweep-build-in-progress-skips";fixture_init(&f,4,16);f.handle.identifier_spelling_build_group=2;sqlparser_handle_sweep_identifier_spellings(&f.handle);check_state(&f,15);fixture_close(&f);
    label="sweep-null-ast-skips";fixture_init(&f,4,16);f.handle.ast=NULL;sqlparser_handle_sweep_identifier_spellings(&f.handle);check_state(&f,15);fixture_close(&f);
    label="sweep-null-handle-and-empty";sqlparser_handle_sweep_identifier_spellings(NULL);memset(&f,0,sizeof(f));sqlparser_handle_sweep_identifier_spellings(&f.handle);check_state(&f,0);
}
int main(void)
{
    build_schema();variant_matrix();empty_unknown_cases();custom_node_cases();location_cases();shared_mutation_and_holes();deep_cases();skip_cases();
    printf("SUMMARY|cases=%zu|assertions=%zu|canonical_variant_cases=%zu|copied_variant_cases=%zu|reachable_variants=%zu|non_location_variants=%zu|schema_descriptors=%zu\n",cases,checks,canonical_cases,copied_cases,reachable_variants,leaf_variants,schema_count);
    return 0;
}
