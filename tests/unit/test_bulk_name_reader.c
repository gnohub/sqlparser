/* Regression coverage for bulk name reader. */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include <pthread.h>
#include "sqlparser_ast_internal.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *case_name = "initialization", *phase = "start";
static int dump;
static size_t checks, records, statements, inputs, overrides, parse_rejections;
static size_t reader_rejections, schema_variants, schema_copies, schema_unknown;
static size_t short_reads, exact_reads, oversized_reads, zero_reads, mutations, clones;
static size_t depth_cases, limit_cases, traversal_errors, concurrent_handles, concurrent_reads;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "bulk names:%d case=%s phase=%s: %s\n", __LINE__, case_name, phase, #x); exit(1); } } while (0)

static void seed(sqlparser_error_t *e) { memset(e, 0xa5, sizeof(*e)); }
static void error_ok(sqlparser_status_t s, const sqlparser_error_t *e)
{
    CHECK(e->code == s && memchr(e->message, '\0', sizeof(e->message)));
    if (s == SQLPARSER_STATUS_OK) CHECK(!e->message[0] && !e->cursor && !e->line && !e->column);
}
static int same(const char *a, const char *b)
{ return a == b || (a && b && !strcmp(a, b)); }
static void same_view(const sqlparser_name_view_t *a, const sqlparser_name_view_t *b)
{
    CHECK(a->value == b->value && same(a->owner_type, b->owner_type) && same(a->field_name, b->field_name));
    CHECK(a->value && a->value[0] && a->owner_type && a->field_name);
    records++;
}
static void emit(const char *s)
{ if (!s) fputs("~", stdout); else for (; *s; s++) printf("%02x", (unsigned char)*s); }
static sqlparser_handle_t *parse(const char *sql, sqlparser_dialect_t d)
{
    sqlparser_parse_options_t o;
    sqlparser_error_t e;
    sqlparser_handle_t *h = NULL;
    sqlparser_status_t s;
    sqlparser_parse_options_default(&o); o.dialect = d;
    seed(&e); s = sqlparser_parse_with_options(sql, &o, &h, &e); error_ok(s, &e);
    if (dump) printf("PARSE|%d|%d\n", d, s);
    if (s != SQLPARSER_STATUS_OK) { CHECK(!h); parse_rejections++; return NULL; }
    CHECK(h); return h;
}
static void sentinel(sqlparser_name_view_t *v, size_t n)
{
    size_t i;
    memset(v, 0x5a, n * sizeof(*v));
    for (i = 0; i < n; i++) { v[i].owner_type = "guard-owner"; v[i].field_name = "guard-field"; v[i].value = "guard-value"; }
}
/* Both guards and every unfilled record must retain their complete bytes. */
static void capacity_case(sqlparser_handle_t *h, size_t statement,
    const sqlparser_name_view_t *oracle, size_t total, size_t capacity, int null_buffer)
{
    size_t allocated = capacity + 3U, filled = capacity < total ? capacity : total;
    size_t count = SIZE_MAX, i;
    sqlparser_name_view_t *v = malloc(allocated * sizeof(*v)), *saved = malloc(allocated * sizeof(*saved));
    sqlparser_error_t e;
    sqlparser_status_t s, expected = capacity && capacity < total ? SQLPARSER_STATUS_RESOURCE_LIMIT : SQLPARSER_STATUS_OK;
    unsigned long generation = h->generation;
    size_t mutation_count = h->identifier_mutation_count;
    CHECK(v && saved); sentinel(v, allocated); memcpy(saved, v, allocated * sizeof(*v));
    seed(&e); s = sqlparser_statement_names(h, statement, null_buffer ? NULL : v + 1, capacity, &count, &e);
    CHECK(s == expected && count == total); error_ok(s, &e);
    CHECK(h->generation == generation && h->identifier_mutation_count == mutation_count && !h->failed);
    CHECK(!memcmp(v, saved, sizeof(*v)));
    if (null_buffer) filled = 0;
    for (i = 0; i < filled; i++) same_view(v + 1U + i, oracle + i);
    CHECK(!memcmp(v + 1U + filled, saved + 1U + filled, (allocated - 1U - filled) * sizeof(*v)));
    if (!capacity) zero_reads++; else if (capacity < total) short_reads++;
    else if (capacity == total) exact_reads++; else oversized_reads++;
    checks++; free(saved); free(v);
}
static int inspect(sqlparser_handle_t *h, size_t statement, size_t expected_total)
{
    sqlparser_error_t e;
    sqlparser_status_t s;
    sqlparser_name_view_t *oracle;
    size_t total = SIZE_MAX, i;
    unsigned long generation = h->generation;
    char *source = strdup(sqlparser_original_sql(h));
    CHECK(source);
    seed(&e); s = sqlparser_statement_name_count(h, statement, &total, &e); error_ok(s, &e);
    if (s != SQLPARSER_STATUS_OK) {
        sqlparser_name_view_t v, saved; size_t got = 99;
        sentinel(&v, 1); memcpy(&saved, &v, sizeof(v));
        seed(&e); CHECK(sqlparser_statement_names(h, statement, &v, 1, &got, &e) == s);
        CHECK(!got && !memcmp(&v, &saved, sizeof(v))); error_ok(s, &e);
        reader_rejections++; free(source); return 0;
    }
    if (expected_total != SIZE_MAX) CHECK(total == expected_total);
    oracle = malloc((total + 1U) * sizeof(*oracle)); CHECK(oracle);
    for (i = 0; i < total; i++) {
        seed(&e); s = sqlparser_statement_name(h, statement, i, oracle + i, &e);
        CHECK(s == SQLPARSER_STATUS_OK); error_ok(s, &e);
        if (dump) { printf("NAME|%zu|%zu|", statement, i); emit(oracle[i].owner_type); putchar('|'); emit(oracle[i].field_name); putchar('|'); emit(oracle[i].value); putchar('\n'); }
    }
    capacity_case(h, statement, oracle, total, 0, 1);
    capacity_case(h, statement, oracle, total, 0, 0);
    capacity_case(h, statement, oracle, total, 1, 0);
    if (total > 1) capacity_case(h, statement, oracle, total, total - 1, 0);
    capacity_case(h, statement, oracle, total, total, 0);
    capacity_case(h, statement, oracle, total, total + 3, 0);
    capacity_case(h, statement, oracle, total, total + 3, 0); /* repeated read */
    CHECK(h->generation == generation && !strcmp(source, sqlparser_original_sql(h)));
    statements++; free(source); free(oracle); return 1;
}
/* First operation on a fresh handle is the new positive-capacity API. Its
 * one-record prefix and complete count are checked with the old getters. */
static int cold_read(sqlparser_handle_t *h, size_t statement)
{
    sqlparser_name_view_t v[3], saved[3], old;
    sqlparser_error_t e, old_e;
    sqlparser_status_t s, old_s;
    size_t got = 99, total = 99;
    unsigned long generation = h->generation;
    sentinel(v, COUNT(v)); memcpy(saved, v, sizeof(v));
    seed(&e); s = sqlparser_statement_names(h, statement, v + 1, 1, &got, &e); error_ok(s, &e);
    seed(&old_e); old_s = sqlparser_statement_name_count(h, statement, &total, &old_e); error_ok(old_s, &old_e);
    CHECK(!memcmp(v, saved, sizeof(v[0])) && !memcmp(v + 2, saved + 2, sizeof(v[0])));
    CHECK(h->generation == generation);
    if (old_s != SQLPARSER_STATUS_OK) {
        CHECK(s == old_s && !got && !memcmp(v, saved, sizeof(v))); reader_rejections++; return 0;
    }
    CHECK(got == total && s == (total > 1 ? SQLPARSER_STATUS_RESOURCE_LIMIT : SQLPARSER_STATUS_OK));
    if (total) { CHECK(sqlparser_statement_name(h, statement, 0, &old, &old_e) == SQLPARSER_STATUS_OK); same_view(v + 1, &old); }
    else CHECK(!memcmp(v, saved, sizeof(v)));
    CHECK(!h->failed); checks++; return 1;
}
static void public_case(const char *sql, sqlparser_dialect_t d)
{
    sqlparser_handle_t *h = parse(sql, d);
    size_t i;
    if (!h) return;
    for (i = 0; i < sqlparser_statement_count(h); i++) {
        phase = "cold-and-capacity";
        if (!cold_read(h, i) || !inspect(h, i, SIZE_MAX)) break;
    }
    sqlparser_handle_destroy(h);
}
static void count_cases(void)
{
    static const size_t counts[] = {0,1,2,3,4,5,7,8,9,64,129};
    size_t k;
    phase = "count-boundaries";
    for (k = 0; k < COUNT(counts); k++) {
        char sql[8192], name[80]; size_t i, at = 0, got = 999;
        sqlparser_error_t e;
        sqlparser_handle_t *h;
        snprintf(name, sizeof(name), "exact-count-%zu", counts[k]); case_name = name;
        at += (size_t)snprintf(sql + at, sizeof(sql) - at, "SELECT ");
        if (!counts[k]) at += (size_t)snprintf(sql + at, sizeof(sql) - at, "1");
        for (i = 0; i < counts[k]; i++) at += (size_t)snprintf(sql + at, sizeof(sql) - at, "%sc%zu", i ? "," : "", i);
        CHECK(at < sizeof(sql)); h = parse(sql, SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
        CHECK(cold_read(h, 0) && inspect(h, 0, counts[k])); sqlparser_handle_destroy(h);
        /* A second fresh handle exercises lazy count-only materialization. */
        h = parse(sql, SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
        seed(&e); CHECK(sqlparser_statement_names(h, 0, NULL, 0, &got, &e) == SQLPARSER_STATUS_OK);
        error_ok(SQLPARSER_STATUS_OK, &e); CHECK(got == counts[k]);
        CHECK(inspect(h, 0, counts[k])); sqlparser_handle_destroy(h);
    }
}
static void invalid_call(const sqlparser_handle_t *h, size_t statement,
    size_t capacity, int null_buffer, int null_count)
{
    sqlparser_name_view_t v[3], saved[3]; size_t got = 123;
    sqlparser_error_t e;
    sentinel(v, COUNT(v)); memcpy(saved, v, sizeof(v)); seed(&e);
    CHECK(sqlparser_statement_names(h, statement, null_buffer ? NULL : v + 1,
        capacity, null_count ? NULL : &got, &e) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    error_ok(SQLPARSER_STATUS_INVALID_ARGUMENT, &e);
    CHECK((null_count ? got == 123 : got == 0) && !memcmp(v, saved, sizeof(v))); checks++;
}
static void argument_cases(void)
{
    sqlparser_handle_t *h;
    sqlparser_error_t e;
    sqlparser_patch_t patch = {0}; sqlparser_patch_list_t list = {&patch, 1};
    size_t got;
    case_name = "arguments-and-failed-handle"; phase = "arguments";
    invalid_call(NULL, 0, 0, 1, 0); invalid_call(NULL, 0, 1, 0, 0);
    invalid_call(NULL, 0, SIZE_MAX, 0, 0); invalid_call(NULL, 0, 1, 0, 1);
    h = parse("SELECT a FROM t", SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
    {
        PgQuery__ParseResult *before = h->ast;
        invalid_call(h, 0, 1, 0, 1); CHECK(h->ast == before);
        invalid_call(h, 0, 1, 1, 0); CHECK(h->ast == before);
        invalid_call(h, 0, SIZE_MAX, 0, 0); CHECK(h->ast == before);
        invalid_call(h, 0, SIZE_MAX / sizeof(sqlparser_name_view_t) + 1U, 0, 0); CHECK(h->ast == before);
    }
    invalid_call(h, 1, 1, 0, 0); invalid_call(h, SIZE_MAX, 1, 0, 0);
    invalid_call(h, SIZE_MAX, 0, 1, 0);
    got = 123; CHECK(sqlparser_statement_names(h, 0, NULL, 0, &got, NULL) == SQLPARSER_STATUS_OK && got == 2);
    CHECK(inspect(h, 0, 2));
    /* Ordinary rejected patch produces an actual terminal handle. No state or
     * storage is fabricated, and no invalidated borrowed value is accessed. */
    patch.op = SQLPARSER_PATCH_REPLACE; patch.selector = "stmt[0].name[999999]"; patch.default_sql = "x";
    CHECK(sqlparser_apply_patch(h, &list, &e) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    CHECK(h->failed); invalid_call(h, 0, 1, 0, 0); invalid_call(h, 0, 0, 1, 0);
    CHECK(sqlparser_statement_names(h, 0, NULL, 0, &got, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT && got == 0);
    sqlparser_handle_destroy(h);
}

/* Valid protobuf fixtures adapted from test_name_read_context.c. */
typedef struct { void *items[4096]; size_t count, serial; } pool_t;
static int32_t *reference_location(ProtobufCMessage *message)
{
    unsigned i;
    if (message == NULL || message->descriptor == NULL) return NULL;
    for (i = 0U; i < message->descriptor->n_fields; ++i) {
        const ProtobufCFieldDescriptor *f = &message->descriptor->fields[i];
        if (f->type == PROTOBUF_C_TYPE_INT32 && f->label != PROTOBUF_C_LABEL_REPEATED &&
            strcmp(f->name, "location") == 0) return (int32_t *)((char *)message + f->offset);
    }
    return NULL;
}

static void *pool_alloc(pool_t *pool, size_t size)
{
    void *p = calloc(1U, size);
    CHECK(p != NULL && pool->count < COUNT(pool->items));
    pool->items[pool->count++] = p; return p;
}
static ProtobufCMessage *new_message(pool_t *pool, const ProtobufCMessageDescriptor *d)
{
    ProtobufCMessage *message = pool_alloc(pool, d->sizeof_message);
    CHECK(d->message_init != NULL); d->message_init(message); return message;
}
static char *new_string(pool_t *pool)
{
    char *text = pool_alloc(pool, 40U);
    snprintf(text, 40U, "identifier_%zu", ++pool->serial); return text;
}
static void set_node(PgQuery__Node *node, int active, ProtobufCMessage *child)
{
    CHECK(sizeof(node->node_case) == sizeof(active));
    memcpy(&node->node_case, &active, sizeof(active));
    memcpy((char *)node + pg_query__node__descriptor.fields[0].offset, &child, sizeof(child));
}
static ProtobufCMessage *fixture_message(pool_t *pool, const ProtobufCMessageDescriptor *d, unsigned depth)
{
    ProtobufCMessage *message = new_message(pool, d);
    char *base = (char *)message;
    unsigned used[16], n_used = 0U, i;
    int32_t *location = reference_location(message);
    if (location != NULL) *location = (int32_t)(17U + ++pool->serial);
    if (d == &pg_query__node__descriptor) {
        const ProtobufCMessageDescriptor *child_d;
        int active;
        switch (pool->serial++ % 3U) {
            case 0: child_d = &pg_query__string__descriptor; active = PG_QUERY__NODE__NODE_STRING; break;
            case 1: child_d = &pg_query__range_var__descriptor; active = PG_QUERY__NODE__NODE_RANGE_VAR; break;
            default: child_d = &pg_query__a__const__descriptor; active = PG_QUERY__NODE__NODE_A_CONST; break;
        }
        set_node((PgQuery__Node *)message, active, fixture_message(pool, child_d, 0U));
        return message;
    }
    if (d == &pg_query__a__const__descriptor) {
        PgQuery__AConst *value = (PgQuery__AConst *)message;
        value->val_case = PG_QUERY__A__CONST__VAL_SVAL;
        value->sval = (PgQuery__String *)fixture_message(pool, &pg_query__string__descriptor, 0U);
        return message;
    }
    for (i = 0U; i < d->n_fields; ++i) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        if ((f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U) {
            unsigned k;
            for (k = 0U; k < n_used && used[k] != f->quantifier_offset; ++k) {}
            if (k != n_used) continue;
            CHECK(n_used < COUNT(used)); used[n_used++] = f->quantifier_offset;
            *(int *)(base + f->quantifier_offset) = (int)f->id;
        }
        if (f->type == PROTOBUF_C_TYPE_STRING) {
            if (f->label == PROTOBUF_C_LABEL_REPEATED) {
                char **items = pool_alloc(pool, 4U * sizeof(*items));
                items[0] = new_string(pool); items[1] = NULL;
                items[2] = (char *)""; items[3] = new_string(pool);
                *(char ***)(base + f->offset) = items;
                *(size_t *)(base + f->quantifier_offset) = 4U;
            } else *(char **)(base + f->offset) = new_string(pool);
        } else if (f->type == PROTOBUF_C_TYPE_MESSAGE && depth != 0U) {
            const ProtobufCMessageDescriptor *child_d = f->descriptor;
            if (f->label == PROTOBUF_C_LABEL_REPEATED) {
                ProtobufCMessage **items = pool_alloc(pool, 3U * sizeof(*items));
                items[0] = fixture_message(pool, child_d, depth - 1U);
                items[2] = fixture_message(pool, child_d, depth - 1U);
                *(ProtobufCMessage ***)(base + f->offset) = items;
                *(size_t *)(base + f->quantifier_offset) = 3U;
            } else *(ProtobufCMessage **)(base + f->offset) = fixture_message(pool, child_d, depth - 1U);
        }
    }
    return message;
}

/* Borrow a test-owned generated root only after a real parsed handle has
 * materialized. Restore its owned root before destruction; this is never a
 * serialized, mutated, or deparsed artificial grammar tree. */
static void schema_root(sqlparser_handle_t *h, PgQuery__Node *root)
{
    PgQuery__Node *owned = h->ast->stmts[0]->stmt;
    h->ast->stmts[0]->stmt = root;
    CHECK(inspect(h, 0, SIZE_MAX));
    h->ast->stmts[0]->stmt = owned;
}
static void schema_cases(void)
{
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    sqlparser_handle_t *h;
    sqlparser_error_t e;
    unsigned i;
    phase = "canonical-copied-schema"; case_name = "schema";
    h = parse("SELECT 1", SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
    CHECK(sqlparser_handle_ensure_ast(h, &e) == SQLPARSER_STATUS_OK);
    CHECK(d->n_fields == 268U);
    for (i = 0; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = d->fields + i;
        pool_t pool = {0};
        PgQuery__Node wrapper = PG_QUERY__NODE__INIT;
        ProtobufCMessage *child = fixture_message(&pool, f->descriptor, 2U);
        ProtobufCMessageDescriptor root_copy = *d, child_copy = *child->descriptor;
        ProtobufCFieldDescriptor *root_fields = malloc(d->n_fields * sizeof(*root_fields));
        ProtobufCFieldDescriptor *child_fields = malloc(child_copy.n_fields * sizeof(*child_fields));
        CHECK(root_fields && (child_fields || !child_copy.n_fields));
        case_name = f->name; set_node(&wrapper, (int)f->id, child);
        schema_root(h, &wrapper); schema_variants++;
        memcpy(root_fields, d->fields, d->n_fields * sizeof(*root_fields)); root_copy.fields = root_fields;
        if (child_copy.n_fields) memcpy(child_fields, child_copy.fields, child_copy.n_fields * sizeof(*child_fields));
        child_copy.fields = child_fields; wrapper.base.descriptor = &root_copy; child->descriptor = &child_copy;
        schema_root(h, &wrapper); schema_copies++;
        /* A selected absent child is valid read-only protobuf structure. */
        set_node(&wrapper, (int)f->id, NULL); schema_root(h, &wrapper);
        free(root_fields); free(child_fields);
        while (pool.count) free(pool.items[--pool.count]);
    }
    {
        const int absent[] = {0,-1,INT_MIN,INT_MAX,269,99999};
        PgQuery__Node wrapper = PG_QUERY__NODE__INIT;
        PgQuery__String child = PG_QUERY__STRING__INIT;
        ProtobufCMessageDescriptor copied = *d;
        case_name = "unknown-node-tags"; child.sval = "inactive";
        for (i = 0; i < COUNT(absent); i++) {
            if (protobuf_c_message_descriptor_get_field(d, (uint32_t)absent[i])) continue;
            set_node(&wrapper, absent[i], &child.base);
            schema_root(h, &wrapper); wrapper.base.descriptor = &copied;
            schema_root(h, &wrapper); wrapper.base.descriptor = d; schema_unknown += 2;
        }
    }
    {
        struct unknown_message { ProtobufCMessage base; char *name; size_t n_items; ProtobufCMessage **items; } custom;
        ProtobufCMessageDescriptor desc = pg_query__res_target__descriptor;
        ProtobufCFieldDescriptor fields[2] = {{0}};
        PgQuery__Node wrapper = PG_QUERY__NODE__INIT;
        PgQuery__String atom = PG_QUERY__STRING__INIT;
        ProtobufCMessage *items[3] = {&atom.base, NULL, &atom.base};
        case_name = "unknown-custom-descriptor"; phase = "custom-schema";
        memset(&custom, 0, sizeof(custom)); custom.base.descriptor = &desc;
        custom.name = "custom_alias"; custom.n_items = COUNT(items); custom.items = items; atom.sval = "custom_nested";
        fields[0].name = "name"; fields[0].id = 1; fields[0].type = PROTOBUF_C_TYPE_STRING;
        fields[0].label = PROTOBUF_C_LABEL_OPTIONAL; fields[0].offset = offsetof(struct unknown_message, name);
        fields[1].name = "indirection"; fields[1].id = 2; fields[1].type = PROTOBUF_C_TYPE_MESSAGE;
        fields[1].label = PROTOBUF_C_LABEL_REPEATED; fields[1].offset = offsetof(struct unknown_message, items);
        fields[1].quantifier_offset = offsetof(struct unknown_message, n_items); fields[1].descriptor = &pg_query__string__descriptor;
        desc.n_fields = COUNT(fields); desc.fields = fields; desc.sizeof_message = sizeof(custom);
        /* Neither descriptor address nor fields belong to generated metadata. */
        set_node(&wrapper, PG_QUERY__NODE__NODE_RES_TARGET, &custom.base);
        schema_root(h, &wrapper); schema_unknown++;
        desc.short_name = "UnregisteredMessage"; schema_root(h, &wrapper); schema_unknown++;
    }
    sqlparser_handle_destroy(h);
}
static size_t find_view(const sqlparser_name_view_t *v, size_t count, const char *value)
{ size_t i; for (i = 0; i < count; i++) if (!strcmp(v[i].value, value)) return i; return SIZE_MAX; }
static sqlparser_name_view_t *bulk_all(sqlparser_handle_t *h, size_t statement, size_t *count)
{
    sqlparser_name_view_t *v;
    sqlparser_error_t e;
    CHECK(sqlparser_statement_names(h, statement, NULL, 0, count, &e) == SQLPARSER_STATUS_OK);
    v = malloc((*count + 1U) * sizeof(*v)); CHECK(v);
    CHECK(sqlparser_statement_names(h, statement, v, *count + 1U, count, &e) == SQLPARSER_STATUS_OK);
    return v;
}
static void lifetime_cases(void)
{
    size_t dialect;
    const char *sql = "SELECT Foo AS left_alias, KeepCol AS right_alias FROM MixedTable";
    for (dialect = 0; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        sqlparser_handle_t *h, *clone = NULL;
        sqlparser_name_view_t *v, *again;
        sqlparser_error_t e;
        sqlparser_patch_t patches[2] = {{0}};
        sqlparser_patch_list_t list = {patches, 2}, empty = {NULL, 0};
        sqlparser_query_graph_view_t graph;
        char left_selector[80], right_selector[80], *input = strdup(sql), *output = NULL;
        size_t count, repeated, left, right, i, expression_count;
        unsigned long generation;
        CHECK(input); case_name = "borrowed-patch-clone-lifetimes"; phase = "empty-apply";
        h = parse(input, (sqlparser_dialect_t)dialect); CHECK(h);
        memset(input, '!', strlen(input)); free(input); CHECK(!strcmp(sqlparser_original_sql(h), sql));
        v = bulk_all(h, 0, &count); left = find_view(v, count, "left_alias"); right = find_view(v, count, "right_alias");
        CHECK(left != SIZE_MAX && right != SIZE_MAX);
        CHECK(sqlparser_statement_query_graph(h, 0, &graph, &e) == SQLPARSER_STATUS_OK);
        generation = h->generation;
        CHECK(sqlparser_apply_patch(h, &empty, &e) == SQLPARSER_STATUS_OK && h->generation == generation);
        CHECK(sqlparser_query_graph_expression_count(&graph, &expression_count, &e) == SQLPARSER_STATUS_OK);
        again = bulk_all(h, 0, &repeated); CHECK(count == repeated);
        for (i = 0; i < count; i++) same_view(v + i, again + i);
        free(again);
        snprintf(left_selector, sizeof(left_selector), "stmt[0].name[%zu]", left);
        snprintf(right_selector, sizeof(right_selector), "stmt[0].name[%zu]", right);
        patches[0].op = patches[1].op = SQLPARSER_PATCH_REPLACE;
        patches[0].selector = left_selector; patches[0].default_sql = "changed_alias";
        patches[1].selector = right_selector; patches[1].default_sql = v[left].value;
        phase = "borrowed-input-batch";
        CHECK(sqlparser_apply_patch(h, &list, &e) == SQLPARSER_STATUS_OK && h->generation == generation + 1UL);
        /* v's strings are invalidated. Free only the caller-owned array. */
        free(v); v = NULL;
        CHECK(sqlparser_query_graph_expression_count(&graph, &expression_count, &e) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        v = bulk_all(h, 0, &count);
        CHECK(find_view(v, count, "changed_alias") != SIZE_MAX && find_view(v, count, "left_alias") != SIZE_MAX);
        CHECK(inspect(h, 0, SIZE_MAX)); mutations++;
        generation = h->generation; phase = "nonempty-no-op";
        patches[0].default_sql = v[left].value; list.count = 1;
        CHECK(sqlparser_apply_patch(h, &list, &e) == SQLPARSER_STATUS_OK && h->generation == generation + 1UL);
        free(v); v = NULL; mutations++;
        CHECK(inspect(h, 0, SIZE_MAX));
        phase = "clone-source-destroy";
        CHECK(sqlparser_handle_clone(h, &clone, &e) == SQLPARSER_STATUS_OK && clone);
        v = bulk_all(h, 0, &count); again = bulk_all(clone, 0, &repeated); CHECK(count == repeated);
        for (i = 0; i < count; i++) {
            CHECK(same(v[i].value, again[i].value) && same(v[i].owner_type, again[i].owner_type) && same(v[i].field_name, again[i].field_name));
            CHECK(v[i].value != again[i].value);
        }
        free(v); sqlparser_handle_destroy(h);
        for (i = 0; i < repeated; i++) CHECK(again[i].value && again[i].value[0]);
        CHECK(find_view(again, repeated, "changed_alias") != SIZE_MAX);
        CHECK(inspect(clone, 0, SIZE_MAX)); free(again);
        CHECK(sqlparser_deparse(clone, &output, &e) == SQLPARSER_STATUS_OK);
        sqlparser_handle_destroy(clone); CHECK(output && strstr(output, "changed_alias"));
        sqlparser_string_free(output); clones++;
    }
    {
        sqlparser_handle_t *h = parse("SELECT a,b FROM first_table; SELECT c FROM second_table; SELECT 1", SQLPARSER_DIALECT_POSTGRESQL);
        sqlparser_name_view_t *first, *second; size_t n1, n2, pass, i;
        case_name = "statement-switching"; phase = "repeat-switch"; CHECK(h && sqlparser_statement_count(h) == 3);
        first = bulk_all(h, 0, &n1); second = bulk_all(h, 1, &n2);
        for (pass = 0; pass < 4; pass++) {
            CHECK(inspect(h, 2, 0) && inspect(h, 1, 2) && inspect(h, 0, 3));
            for (i = 0; i < n1; i++) CHECK(first[i].value && first[i].value[0]);
            for (i = 0; i < n2; i++) CHECK(second[i].value && second[i].value[0]);
        }
        CHECK(find_view(first, n1, "first_table") != SIZE_MAX && find_view(second, n2, "second_table") != SIZE_MAX);
        free(first); free(second); sqlparser_handle_destroy(h);
    }
}
static void corpora(const char *directory)
{
    static const char *files[] = {
        "sql_batch_input.json", "mysql_dialect_input.json", "oracle_dialect_input.json",
        "sqlserver_dialect_input.json", "dameng_dialect_input.json", "vastbase_oracle_dialect_input.json",
        "vastbase_mysql_dialect_input.json", "vastbase_postgresql_dialect_input.json",
        "vastbase_sqlserver_dialect_input.json", "kingbase_oracle_dialect_input.json",
        "kingbase_mysql_dialect_input.json", "kingbase_postgresql_dialect_input.json",
        "kingbase_sqlserver_dialect_input.json"
    };
    static const char *names[] = {"postgresql", "mysql", "oracle", "sqlserver", "dameng", "vastbase-oracle", "vastbase-mysql", "vastbase-postgresql", "vastbase-sqlserver", "kingbase-oracle", "kingbase-mysql", "kingbase-postgresql", "kingbase-sqlserver"};
    size_t f;
    for (f = 0; f < COUNT(files); f++) {
        char path[4096]; json_error_t e; json_t *root, *items, *item; size_t i;
        CHECK(snprintf(path, sizeof(path), "%s/%s", directory, files[f]) < (int)sizeof(path));
        root = json_load_file(path, 0, &e); CHECK(root);
        items = json_object_get(root, "items"); CHECK(json_is_array(items));
        json_array_foreach(items, i, item) {
            const char *sql = json_string_value(json_object_get(item, "sql"));
            const char *name = json_string_value(json_object_get(item, "name"));
            const char *declared = json_string_value(json_object_get(item, "dialect"));
            size_t dialect = f;
            CHECK(sql && name);
            if (declared) {
                for (dialect = 0; dialect < COUNT(names) && strcmp(names[dialect], declared); dialect++) {}
                CHECK(dialect < COUNT(names)); overrides += dialect != f;
            }
            case_name = name; phase = "corpus";
            if (dump) { printf("CASE|%zu|%zu|", f, i); emit(name); putchar('\n'); }
            public_case(sql, (sqlparser_dialect_t)dialect); inputs++;
        }
        fprintf(stderr, "CORPUS|%s|%zu\n", files[f], json_array_size(items)); json_decref(root);
    }
    CHECK(inputs == 4004U && overrides == 7U);
}

/* Parent-requested focused boundary additions. Malformed-array rejection uses
 * real initialized objects and NULL arrays, never invalid addresses or OOM. */
static void depth_and_limit_cases(void)
{
    unsigned depth, i;
    sqlparser_handle_t *h;
    sqlparser_error_t e;
    PgQuery__Node nodes[66]; PgQuery__AIndirection groups[65];
    PgQuery__RangeVar leaf = PG_QUERY__RANGE_VAR__INIT;
    case_name = "depth-64-65"; phase = "source-and-ast-depth";
    leaf.relname = "deep_relation";
    for (depth = 64; depth <= 65; depth++) {
        char sql[1024]; size_t at = 0;
        at += (size_t)snprintf(sql + at, sizeof(sql) - at, "SELECT ");
        for (i = 0; i < depth; i++) at += (size_t)snprintf(sql + at, sizeof(sql) - at, "f(");
        at += (size_t)snprintf(sql + at, sizeof(sql) - at, "deep_column");
        for (i = 0; i < depth; i++) sql[at++] = ')';
        sql[at] = '\0'; CHECK(at < sizeof(sql));
        h = parse(sql, SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
        CHECK(cold_read(h, 0) && inspect(h, 0, depth + 1U)); sqlparser_handle_destroy(h); depth_cases++;
        h = parse("SELECT 1", SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
        CHECK(sqlparser_handle_ensure_ast(h, &e) == SQLPARSER_STATUS_OK);
        for (i = 0; i < depth; i++) {
            pg_query__node__init(nodes + i); pg_query__a__indirection__init(groups + i);
            nodes[i].node_case = PG_QUERY__NODE__NODE_A_INDIRECTION; nodes[i].a_indirection = groups + i;
            groups[i].arg = nodes + i + 1U;
        }
        pg_query__node__init(nodes + depth);
        nodes[depth].node_case = PG_QUERY__NODE__NODE_RANGE_VAR; nodes[depth].range_var = &leaf;
        schema_root(h, nodes); sqlparser_handle_destroy(h); depth_cases++;
    }
    {
        const char *sql = "SELECT source_limit_name FROM source_limit_table";
        sqlparser_parse_options_t options;
        sqlparser_parse_options_default(&options); case_name = "source-byte-limit"; phase = "parse-limit";
        for (i = 0; i < 2; i++) {
            sqlparser_status_t status;
            options.limits.max_sql_bytes = strlen(sql) - i; h = NULL; seed(&e);
            status = sqlparser_parse_with_options(sql, &options, &h, &e); error_ok(status, &e);
            CHECK(status == (i ? SQLPARSER_STATUS_RESOURCE_LIMIT : SQLPARSER_STATUS_OK));
            if (h) { CHECK(cold_read(h, 0) && inspect(h, 0, 2)); sqlparser_handle_destroy(h); }
            else { CHECK(i); }
            limit_cases++;
        }
    }
    {
        char sql[2048]; sqlparser_parse_options_t options;
        case_name = "statement-64-65-limit"; phase = "parse-limit";
        sqlparser_parse_options_default(&options); CHECK(options.limits.max_statement_count == 64U);
        for (depth = 64; depth <= 65; depth++) {
            size_t at = 0, j;
            sqlparser_status_t status;
            for (j = 0; j < depth; j++) at += (size_t)snprintf(sql + at, sizeof(sql) - at, "SELECT c%zu;", j);
            CHECK(at < sizeof(sql)); h = NULL; seed(&e);
            status = sqlparser_parse_with_options(sql, &options, &h, &e); error_ok(status, &e);
            CHECK(status == (depth == 64 ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_RESOURCE_LIMIT));
            if (h) {
                CHECK(sqlparser_statement_count(h) == 64U);
                for (j = 0; j < depth; j++) CHECK(cold_read(h, j) && inspect(h, j, 1));
                sqlparser_handle_destroy(h);
            } else { CHECK(depth == 65); }
            limit_cases++;
        }
        /* The explicit larger source limit also admits a 65-statement AST. */
        options.limits.max_statement_count = 65U;
        CHECK(sqlparser_parse_with_options(sql, &options, &h, &e) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_statement_count(h) == 65U);
        for (i = 0; i < 65; i++) CHECK(inspect(h, i, 1));
        sqlparser_handle_destroy(h); limit_cases++;
    }
}
static void traversal_error_cases(void)
{
    PgQuery__Node wrapper = PG_QUERY__NODE__INIT;
    PgQuery__Alias alias = PG_QUERY__ALIAS__INIT;
    PgQuery__Node *owned;
    sqlparser_handle_t *h;
    sqlparser_error_t e;
    size_t capacity, count;
    case_name = "missing-repeated-array"; phase = "ordinary-traversal-rejection";
    h = parse("SELECT retained_name", SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
    CHECK(sqlparser_handle_ensure_ast(h, &e) == SQLPARSER_STATUS_OK);
    owned = h->ast->stmts[0]->stmt;
    alias.aliasname = "valid_prefix"; alias.n_colnames = 1; alias.colnames = NULL;
    wrapper.node_case = PG_QUERY__NODE__NODE_ALIAS; wrapper.alias = &alias;
    h->ast->stmts[0]->stmt = &wrapper;
    for (capacity = 0; capacity <= 2; capacity++) {
        sqlparser_name_view_t v[4], saved[4];
        sentinel(v, COUNT(v)); memcpy(saved, v, sizeof(v)); count = 999; seed(&e);
        CHECK(sqlparser_statement_names(h, 0, capacity ? v + 1 : NULL, capacity, &count, &e) == SQLPARSER_STATUS_INTERNAL_ERROR);
        error_ok(SQLPARSER_STATUS_INTERNAL_ERROR, &e); CHECK(count == 0);
        /* Caller discards every output record on traversal failure; only the
         * outside guards, not any partial prefix, are inspected. */
        CHECK(!memcmp(v, saved, sizeof(v[0])) && !memcmp(v + 3, saved + 3, sizeof(v[0])));
        traversal_errors++;
    }
    h->ast->stmts[0]->stmt = owned;
    CHECK(inspect(h, 0, 1)); sqlparser_handle_destroy(h);
}
typedef struct { pthread_barrier_t *barrier; size_t handles, reads; } concurrent_worker_t;
static void *concurrent_worker(void *arg)
{
    concurrent_worker_t *w = arg;
    size_t dialect, iteration;
    int gate = pthread_barrier_wait(w->barrier);
    CHECK(gate == 0 || gate == PTHREAD_BARRIER_SERIAL_THREAD);
    for (dialect = 0; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        sqlparser_parse_options_t options;
        sqlparser_handle_t *h = NULL;
        sqlparser_error_t e;
        sqlparser_parse_options_default(&options); options.dialect = (sqlparser_dialect_t)dialect;
        CHECK(sqlparser_parse_with_options("SELECT a AS ax,b AS bx FROM thread_table", &options, &h, &e) == SQLPARSER_STATUS_OK && h);
        for (iteration = 0; iteration < 20; iteration++) {
            sqlparser_name_view_t bulk[8], old; size_t count = SIZE_MAX, total = 0, i;
            CHECK(sqlparser_statement_names(h, 0, NULL, 0, &count, &e) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_statement_name_count(h, 0, &total, &e) == SQLPARSER_STATUS_OK && total == 5 && count == total);
            CHECK(sqlparser_statement_names(h, 0, bulk, 1, &count, &e) == SQLPARSER_STATUS_RESOURCE_LIMIT && count == total);
            CHECK(sqlparser_statement_names(h, 0, bulk, COUNT(bulk), &count, &e) == SQLPARSER_STATUS_OK && count == total);
            for (i = 0; i < total; i++) {
                CHECK(sqlparser_statement_name(h, 0, i, &old, &e) == SQLPARSER_STATUS_OK);
                CHECK(old.value == bulk[i].value && same(old.owner_type, bulk[i].owner_type) && same(old.field_name, bulk[i].field_name));
            }
            w->reads += 3U;
        }
        sqlparser_handle_destroy(h); w->handles++;
    }
    return NULL;
}
static void concurrent_cases(void)
{
    pthread_t threads[4]; concurrent_worker_t workers[4] = {{0}};
    pthread_barrier_t barrier;
    size_t i;
    case_name = "distinct-handle-concurrency"; phase = "concurrent-bulk-reads";
    CHECK(pthread_barrier_init(&barrier, NULL, COUNT(threads)) == 0);
    for (i = 0; i < COUNT(threads); i++) {
        workers[i].barrier = &barrier;
        CHECK(pthread_create(threads + i, NULL, concurrent_worker, workers + i) == 0);
    }
    for (i = 0; i < COUNT(threads); i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        concurrent_handles += workers[i].handles; concurrent_reads += workers[i].reads;
    }
    CHECK(pthread_barrier_destroy(&barrier) == 0);
    CHECK(concurrent_handles == 52 && concurrent_reads == 3120);
}

int main(int argc, char **argv)
{
    const char *directory = "tests/cases"; int targeted = 0, i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump")) dump = 1;
        else if (!strcmp(argv[i], "--targeted")) targeted = 1;
        else if (!strcmp(argv[i], "--corpora") && i + 1 < argc) directory = argv[++i];
        else { fprintf(stderr, "usage: %s [--dump] [--targeted] [--corpora path]\n", argv[0]); return 2; }
    }
    count_cases(); argument_cases(); schema_cases(); lifetime_cases();
    depth_and_limit_cases(); traversal_error_cases(); concurrent_cases();
    if (!targeted) corpora(directory);
    CHECK(schema_variants == 268 && schema_copies == 268 && schema_unknown > 0 && clones == 13);
    fprintf(stderr, "SUMMARY checks=%zu records=%zu statements=%zu schema_variants=%zu schema_copies=%zu schema_unknown=%zu corpus_inputs=%zu dialect_overrides=%zu parse_rejections=%zu reader_rejections=%zu zero_reads=%zu short_reads=%zu exact_reads=%zu oversized_reads=%zu mutations=%zu clones=%zu depth_cases=%zu limit_cases=%zu traversal_errors=%zu concurrent_handles=%zu concurrent_reads=%zu\n", checks, records, statements, schema_variants, schema_copies, schema_unknown, inputs, overrides, parse_rejections, reader_rejections, zero_reads, short_reads, exact_reads, oversized_reads, mutations, clones, depth_cases, limit_cases, traversal_errors, concurrent_handles, concurrent_reads);
    return 0;
}
