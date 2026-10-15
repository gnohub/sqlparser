/* Regression coverage for sparse value index. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "sqlparser_ast_internal.h"

#ifdef SQLPARSER_SPARSE_VALUE_INTERNAL
static size_t test_fallback_calls;
static sqlparser_status_t test_find_statement_node_index_by_node(
    sqlparser_handle_t *, size_t, PgQuery__Node *, size_t *, sqlparser_error_t *);
#define sqlparser_find_statement_node_index_by_node test_find_statement_node_index_by_node
#include "../../src/core/sqlparser_view.c"
#undef sqlparser_find_statement_node_index_by_node
static sqlparser_status_t test_find_statement_node_index_by_node(
    sqlparser_handle_t *h, size_t s, PgQuery__Node *n, size_t *i, sqlparser_error_t *e)
{
    test_fallback_calls++;
    return sqlparser_find_statement_node_index_by_node(h, s, n, i, e);
}
#endif

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "sparse value [%s/%s]:%d: %s\n", test_case, test_phase, __LINE__, #x); exit(1); } } while (0)
static const char *test_case = "initialization", *test_phase = "internal";
static int test_dump;
static size_t test_alternatives, test_fixtures, test_nodes, test_scalars, test_fallbacks;
static size_t test_duplicates, test_grouping, test_tails, test_roots;
static size_t test_corpus, test_overrides, test_statuses, test_failures;
static size_t test_graphs, test_values, test_selectors, test_patches, test_borrowed;

static int test_scalar(const PgQuery__Node *n)
{
    return n && (n->node_case == PG_QUERY__NODE__NODE_A_CONST ||
        n->node_case == PG_QUERY__NODE__NODE_PARAM_REF ||
        n->node_case == PG_QUERY__NODE__NODE_SET_TO_DEFAULT);
}
static void test_text(const char *s)
{
    if (!test_dump) return;
    if (!s) { putchar('~'); return; }
    for (; *s; s++) printf("%02x", (unsigned char)*s);
}
static void test_begin(const char *name, sqlparser_dialect_t dialect, const char *sql)
{
    test_case = name;
    if (test_dump) { printf("CASE|%d|", dialect); test_text(name); putchar('|'); test_text(sql); putchar('\n'); }
}
static void test_set_phase(const char *p)
{
    test_phase = p;
    if (test_dump) printf("PHASE|%s\n", p);
}
static void test_seed(sqlparser_error_t *e) { memset(e, 0xa5, sizeof(*e)); }
static int test_result(const char *op, sqlparser_status_t s, const sqlparser_error_t *e)
{
    test_statuses++; CHECK(e->code == s);
    if (s == SQLPARSER_STATUS_OK) CHECK(e->cursor == 0 && e->line == 0 && e->column == 0 && !e->message[0]);
    else { test_failures++; CHECK(memchr(e->message, 0, sizeof(e->message))); }
    if (test_dump) { printf("STATUS|%s|%d|%d|%d|%d|", op, s, e->cursor, e->line, e->column); test_text(e->message); putchar('\n'); }
    return s == SQLPARSER_STATUS_OK;
}
static sqlparser_handle_t *test_parse(const char *sql, sqlparser_dialect_t d)
{
    sqlparser_parse_options_t o; sqlparser_handle_t *h = NULL;
    sqlparser_error_t e; sqlparser_status_t s;
    sqlparser_parse_options_default(&o); o.dialect = d;
    test_seed(&e); s = sqlparser_parse_with_options(sql, &o, &h, &e);
    if (!test_result("parse", s, &e)) { CHECK(!h); return NULL; }
    CHECK(h && !strcmp(sqlparser_original_sql(h), sql)); return h;
}

#ifdef SQLPARSER_SPARSE_VALUE_INTERNAL
/* Independent full descriptor scan retains eligible Nodes in traversal order.
 * It does not call the production active-oneof or sparse recording helpers. */
typedef struct { PgQuery__Node *nodes[16384]; size_t count, grouping, tails; PgQuery__SelectStmt *tail; } test_reference_t;
static test_reference_t test_reference;
static void test_collect_message(ProtobufCMessage *);
static void test_collect_child(ProtobufCMessage *child)
{
    if (!child) return;
    if (child->descriptor == &pg_query__node__descriptor) {
        PgQuery__Node *n = (PgQuery__Node *)child;
        if (sqlparser_node_is_grouping_wrapper(n)) test_reference.grouping++;
        else if (sqlparser_node_is_mysql_dml_tail_wrapper(n, test_reference.tail)) test_reference.tails++;
        else { CHECK(test_reference.count < COUNT(test_reference.nodes)); test_reference.nodes[test_reference.count++] = n; }
    }
    test_collect_message(child);
}
static void test_collect_message(ProtobufCMessage *m)
{
    unsigned i;
    if (!m || !m->descriptor) return;
    for (i = 0; i < m->descriptor->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = &m->descriptor->fields[i]; char *base = (char *)m;
        if (f->type != PROTOBUF_C_TYPE_MESSAGE) continue;
        if ((f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) && *(int *)(base + f->quantifier_offset) != (int)f->id) continue;
        if (f->label == PROTOBUF_C_LABEL_REPEATED) {
            size_t j, n = *(size_t *)(base + f->quantifier_offset);
            ProtobufCMessage **items = *(ProtobufCMessage ***)(base + f->offset);
            for (j = 0; j < n; j++) test_collect_child(items ? items[j] : NULL);
        } else test_collect_child(*(ProtobufCMessage **)(base + f->offset));
    }
}
static void test_compare_cache(sqlparser_handle_t *h, size_t statement)
{
    sqlparser_graph_build_t build = {0}; sqlparser_error_t e = {0};
    PgQuery__Node *root; size_t i, scalar_count = 0, ordinal = 0, before;
    CHECK(sqlparser_get_statement_node(h, statement, &root, &e) == SQLPARSER_STATUS_OK);
    build.handle = h; build.statement_index = statement; build.statement_node = root;
    CHECK(sqlparser_get_mysql_dml_tail_select(h, statement, root, &build.dml_tail_select, &e) == SQLPARSER_STATUS_OK);
    memset(&test_reference, 0, sizeof(test_reference)); test_reference.tail = build.dml_tail_select;
    test_collect_message(&root->base);
    CHECK(sqlparser_graph_selector_cache_collect(&build, &root->base, NULL, &ordinal, &e) == 0);
    CHECK(ordinal == test_reference.count);
    for (i = 0; i < test_reference.count; i++) if (test_scalar(test_reference.nodes[i])) scalar_count++;
    CHECK(build.node_index_count == scalar_count);
    /* Each retained entry saves its full-walk ordinal before sorting. */
    scalar_count = 0;
    for (i = 0; i < test_reference.count; i++) if (test_scalar(test_reference.nodes[i])) {
        CHECK(build.node_indices[scalar_count].pointer == test_reference.nodes[i]);
        CHECK(build.node_indices[scalar_count].index == i); scalar_count++;
    }
    sqlparser_graph_selector_cache_clear(&build);
    CHECK(build.node_indices == NULL && !build.selector_cache_built && build.node_index_capacity == 0);
    before = test_fallback_calls;
    CHECK(sqlparser_graph_find_cached_value_index(&build, NULL) == SIZE_MAX);
    CHECK(test_fallback_calls == before && !build.selector_cache_built);
    /* Unsupported lookup must use the general walker even before cache build. */
    for (i = 0; i < test_reference.count; i++) if (!test_scalar(test_reference.nodes[i])) {
        size_t expected;
        CHECK(sqlparser_find_statement_node_index_by_node(h, statement, test_reference.nodes[i], &expected, &e) == SQLPARSER_STATUS_OK);
        before = test_fallback_calls;
        CHECK(sqlparser_graph_find_cached_value_index(&build, test_reference.nodes[i]) == expected);
        CHECK(test_fallback_calls == before + 1 && !build.selector_cache_built); test_fallbacks++; break;
    }
    /* A first scalar lookup builds the cache without entering the full walker. */
    for (i = 0; i < test_reference.count; i++) if (test_scalar(test_reference.nodes[i])) {
        size_t first = 0;
        while (test_reference.nodes[first] != test_reference.nodes[i]) first++;
        before = test_fallback_calls;
        CHECK(sqlparser_graph_find_cached_value_index(&build, test_reference.nodes[i]) == first);
        CHECK(test_fallback_calls == before && build.selector_cache_built);
        break;
    }
    CHECK(sqlparser_graph_ensure_selector_cache(&build, &e) == 0);
    CHECK(build.node_index_count == scalar_count);
    before = test_fallback_calls;
    CHECK(sqlparser_graph_find_cached_value_index(&build, NULL) == SIZE_MAX);
    CHECK(test_fallback_calls == before && build.selector_cache_built);
    for (i = 0; i < test_reference.count; i++) {
        size_t first = 0, actual, expected; PgQuery__Node **slot = NULL;
        PgQuery__Node *node = test_reference.nodes[i];
        while (test_reference.nodes[first] != node) first++;
        CHECK(sqlparser_get_statement_node_slot_by_index(h, statement, i, &slot, NULL, &e) == SQLPARSER_STATUS_OK);
        CHECK(slot && *slot == node);
        CHECK(sqlparser_find_statement_node_index_by_node(h, statement, node, &expected, &e) == SQLPARSER_STATUS_OK && expected == first);
        before = test_fallback_calls; actual = sqlparser_graph_find_cached_value_index(&build, node);
        CHECK(actual == first);
        CHECK(test_fallback_calls == before + (test_scalar(node) ? 0U : 1U));
        test_nodes++; test_scalars += test_scalar(node); test_fallbacks += !test_scalar(node); test_duplicates += first != i;
    }
    before = test_fallback_calls;
    CHECK(sqlparser_graph_find_cached_value_index(&build, root) == SIZE_MAX);
    CHECK(test_fallback_calls == before + (test_scalar(root) ? 0U : 1U)); test_roots++;
    {
        PgQuery__Node absent = PG_QUERY__NODE__INIT;
        absent.node_case = PG_QUERY__NODE__NODE_A_CONST;
        before = test_fallback_calls;
        CHECK(sqlparser_graph_find_cached_value_index(&build, &absent) == SIZE_MAX && test_fallback_calls == before);
        absent.node_case = (PgQuery__Node__NodeCase)INT_MAX;
        CHECK(sqlparser_graph_find_cached_value_index(&build, &absent) == SIZE_MAX && test_fallback_calls == before + 1U);
    }
    CHECK(build.node_index_count == scalar_count);
    test_grouping += test_reference.grouping; test_tails += test_reference.tails; test_fixtures++;
    sqlparser_graph_selector_cache_clear(&build);
}
typedef struct { void *items[16384]; size_t count, serial; } test_pool_t;
static void *test_alloc(test_pool_t *p, size_t n)
{
    void *r = calloc(1U, n); CHECK(r && p->count < COUNT(p->items)); p->items[p->count++] = r; return r;
}
static ProtobufCMessage *test_message(test_pool_t *p, const ProtobufCMessageDescriptor *d)
{
    ProtobufCMessage *m = test_alloc(p, d->sizeof_message); CHECK(d->message_init); d->message_init(m); return m;
}
static void test_set_node(PgQuery__Node *n, int c, ProtobufCMessage *m)
{
    CHECK(sizeof(n->node_case) == sizeof(c)); memcpy(&n->node_case, &c, sizeof(c));
    memcpy((char *)n + pg_query__node__descriptor.fields[0].offset, &m, sizeof(m));
}
static ProtobufCMessage *test_fixture(test_pool_t *p, const ProtobufCMessageDescriptor *d, unsigned depth)
{
    ProtobufCMessage *m = test_message(p, d); char *base = (char *)m; unsigned i, used[16], n_used = 0;
    if (d == &pg_query__node__descriptor) {
        static const int cases[] = {PG_QUERY__NODE__NODE_A_CONST, PG_QUERY__NODE__NODE_PARAM_REF,
            PG_QUERY__NODE__NODE_SET_TO_DEFAULT, PG_QUERY__NODE__NODE_COLUMN_REF, PG_QUERY__NODE__NODE_STRING};
        int c = cases[p->serial++ % COUNT(cases)];
        const ProtobufCFieldDescriptor *f = protobuf_c_message_descriptor_get_field(d, c);
        test_set_node((PgQuery__Node *)m, c, test_fixture(p, f->descriptor, 0)); return m;
    }
    for (i = 0; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = &d->fields[i];
        if (f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) {
            unsigned k; for (k = 0; k < n_used && used[k] != f->quantifier_offset; k++) {}
            if (k < n_used) continue;
            CHECK(n_used < COUNT(used)); used[n_used++] = f->quantifier_offset;
            *(int *)(base + f->quantifier_offset) = (int)f->id;
        }
        if (f->type == PROTOBUF_C_TYPE_MESSAGE && depth) {
            if (f->label == PROTOBUF_C_LABEL_REPEATED) {
                ProtobufCMessage **a = test_alloc(p, 4U * sizeof(*a));
                a[0] = test_fixture(p, f->descriptor, depth - 1); a[2] = test_fixture(p, f->descriptor, depth - 1);
                a[3] = a[0]; /* A duplicate after another occupied/null entry. */
                *(ProtobufCMessage ***)(base + f->offset) = a; *(size_t *)(base + f->quantifier_offset) = 4;
            } else *(ProtobufCMessage **)(base + f->offset) = test_fixture(p, f->descriptor, depth - 1);
        }
    }
    return m;
}
static void test_synthetic(sqlparser_handle_t *h, PgQuery__Node *root)
{
    PgQuery__Node *saved = h->ast->stmts[0]->stmt;
    h->ast->stmts[0]->stmt = root; test_compare_cache(h, 0); h->ast->stmts[0]->stmt = saved;
}
static void test_schema(void)
{
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    sqlparser_handle_t *h; sqlparser_error_t e; unsigned i;
    h = test_parse("SELECT 0", SQLPARSER_DIALECT_POSTGRESQL); CHECK(h);
    CHECK(sqlparser_handle_ensure_ast(h, &e) == SQLPARSER_STATUS_OK); CHECK(d->n_fields == 268);
    for (i = 0; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = &d->fields[i]; test_pool_t pool = {0};
        PgQuery__Node node = PG_QUERY__NODE__INIT, root = PG_QUERY__NODE__INIT;
        PgQuery__List list = PG_QUERY__LIST__INIT;
        PgQuery__Node literal = PG_QUERY__NODE__INIT, bind = PG_QUERY__NODE__INIT, def = PG_QUERY__NODE__INIT;
        PgQuery__Node *items[] = {&literal, &node, NULL, &bind, &node, &def, &literal};
        test_case = f->name;
        CHECK(f->type == PROTOBUF_C_TYPE_MESSAGE && (f->flags & PROTOBUF_C_FIELD_FLAG_ONEOF));
        test_set_node(&node, f->id, test_fixture(&pool, f->descriptor, 2));
        literal.node_case = PG_QUERY__NODE__NODE_A_CONST; bind.node_case = PG_QUERY__NODE__NODE_PARAM_REF;
        def.node_case = PG_QUERY__NODE__NODE_SET_TO_DEFAULT;
        root.node_case = PG_QUERY__NODE__NODE_LIST; root.list = &list; list.n_items = COUNT(items); list.items = items;
        test_synthetic(h, &root); test_synthetic(h, &node); /* Alternative used as root is excluded. */
        test_set_node(&node, f->id, NULL); test_synthetic(h, &root); test_synthetic(h, &node);
        while (pool.count) free(pool.items[--pool.count]);
        test_alternatives++;
    }
    {
        const int cases[] = {0, -1, INT_MIN, INT_MAX, 269, 99999};
        PgQuery__Node root = PG_QUERY__NODE__INIT, unknown = PG_QUERY__NODE__INIT, scalar = PG_QUERY__NODE__INIT;
        PgQuery__String inactive = PG_QUERY__STRING__INIT; PgQuery__List list = PG_QUERY__LIST__INIT;
        PgQuery__Node *items[] = {&unknown, &scalar, &unknown};
        test_case = "empty-and-unknown-oneof"; root.node_case = PG_QUERY__NODE__NODE_LIST; root.list = &list;
        list.n_items = COUNT(items); list.items = items; scalar.node_case = PG_QUERY__NODE__NODE_A_CONST;
        inactive.sval = (char *)"inactive";
        for (i = 0; i < COUNT(cases); i++) {
            CHECK(!protobuf_c_message_descriptor_get_field(d, (unsigned)cases[i]));
            test_set_node(&unknown, cases[i], &inactive.base); test_synthetic(h, &root); test_synthetic(h, &unknown);
        }
        for (i = 1; i < d->fields[d->n_fields - 1].id; i++) if (!protobuf_c_message_descriptor_get_field(d, i)) {
            test_set_node(&unknown, i, &inactive.base); test_synthetic(h, &root);
        }
    }
    {
        PgQuery__Node root = PG_QUERY__NODE__INIT, group = PG_QUERY__NODE__INIT, sub = PG_QUERY__NODE__INIT;
        PgQuery__Node literal = PG_QUERY__NODE__INIT, bind = PG_QUERY__NODE__INIT, def = PG_QUERY__NODE__INIT;
        PgQuery__Node column = PG_QUERY__NODE__INIT; PgQuery__List list = PG_QUERY__LIST__INIT;
        PgQuery__AIndirection a = PG_QUERY__A__INDIRECTION__INIT, b = PG_QUERY__A__INDIRECTION__INIT;
        PgQuery__AConst c = PG_QUERY__A__CONST__INIT; PgQuery__ParamRef p = PG_QUERY__PARAM_REF__INIT;
        PgQuery__SetToDefault v = PG_QUERY__SET_TO_DEFAULT__INIT;
        PgQuery__Node *items[] = {&column, &group, &bind, &sub, &def, &literal, &column, &bind};
        test_case = "mixed-grouping-incomplete-scalars";
        root.node_case = PG_QUERY__NODE__NODE_LIST; root.list = &list; list.n_items = COUNT(items); list.items = items;
        group.node_case = sub.node_case = PG_QUERY__NODE__NODE_A_INDIRECTION;
        group.a_indirection = &a; sub.a_indirection = &b; a.arg = &sub; b.arg = &literal;
        literal.node_case = PG_QUERY__NODE__NODE_A_CONST; literal.a_const = &c;
        bind.node_case = PG_QUERY__NODE__NODE_PARAM_REF; bind.param_ref = &p;
        def.node_case = PG_QUERY__NODE__NODE_SET_TO_DEFAULT; def.set_to_default = &v;
        column.node_case = PG_QUERY__NODE__NODE_COLUMN_REF;
        test_synthetic(h, &root);
        c.val_case = PG_QUERY__A__CONST__VAL_SVAL; c.sval = NULL; test_synthetic(h, &root);
        c.val_case = (PgQuery__AConst__ValCase)INT_MAX; test_synthetic(h, &root);
        b.n_indirection = 1; b.indirection = NULL; test_synthetic(h, &root); /* Occupied count with absent array. */
        list.n_items = 7; list.items = NULL; test_synthetic(h, &root);
    }
    sqlparser_handle_destroy(h);
    CHECK(test_alternatives == 268 && test_duplicates && test_grouping && test_scalars && test_fallbacks);
}
static void test_parsed_cache_cases(void)
{
    static const struct { sqlparser_dialect_t d; const char *sql; } cases[] = {
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT (((1))), $1, a + 2, coalesce(a, 'x'), NULL, true FROM t WHERE a IN (3,4) ORDER BY 1 LIMIT 5"},
        {SQLPARSER_DIALECT_POSTGRESQL, "UPDATE t SET a = DEFAULT, b = $1, c = 2 WHERE id = 3 RETURNING 4"},
        {SQLPARSER_DIALECT_POSTGRESQL, "INSERT INTO t(a,b) VALUES (DEFAULT,$1),(2,3) RETURNING 4"},
        {SQLPARSER_DIALECT_MYSQL, "UPDATE t SET a = 1, b = ? WHERE id = 2 ORDER BY a + 3 LIMIT 4"},
        {SQLPARSER_DIALECT_MYSQL, "DELETE FROM t WHERE id = ? ORDER BY a + 1 LIMIT 2"}
    };
    size_t i;
    for (i = 0; i < COUNT(cases); i++) {
        sqlparser_handle_t *h; test_case = cases[i].sql; h = test_parse(cases[i].sql, cases[i].d); CHECK(h);
        test_compare_cache(h, 0); sqlparser_handle_destroy(h);
    }
    CHECK(test_tails >= 6);
}
#endif

static void test_print_selector(const sqlparser_selector_t *s)
{
    if (test_dump) printf("%d|%zu|%zu|%zu|%zu", s->kind, s->statement_index, s->item_index, s->row_index, s->column_index);
}
static int test_graph(sqlparser_handle_t *h, sqlparser_selector_t *first)
{
    size_t statement, n = sqlparser_statement_count(h); sqlparser_error_t e; sqlparser_status_t s;
    if (first) memset(first, 0, sizeof(*first));
    if (test_dump) printf("STATEMENTS|%zu\n", n);
    for (statement = 0; statement < n; statement++) {
        sqlparser_query_graph_view_t g, repeated; size_t i;
        test_seed(&e); s = sqlparser_statement_query_graph(h, statement, &g, &e);
        if (!test_result("graph", s, &e)) return 0;
        test_seed(&e); s = sqlparser_statement_query_graph(h, statement, &repeated, &e);
        if (!test_result("graph-repeat", s, &e)) return 0;
        CHECK(g.handle == h && repeated.handle == h && repeated.generation == g.generation && repeated.value_count == g.value_count);
        if (test_dump) printf("GRAPH|%zu|%lu|%zu|%d|%zu|%zu|%zu|%zu|%zu|%zu|%zu|%d|%zu\n", statement, g.generation, g.root_block_index, g.has_root_block,
            g.block_count, g.relation_count, g.target_count, g.field_count, g.value_count, g.set_count, g.predicate_count, g.has_dml, g.dml_branch_count);
        test_graphs++;
        for (i = 0; i < g.value_count; i++) {
            sqlparser_graph_value_t v, again;
            test_seed(&e); s = sqlparser_query_graph_value_at(&g, i, &v, &e);
            if (!test_result("value", s, &e)) return 0;
            test_seed(&e); s = sqlparser_query_graph_value_at(&repeated, i, &again, &e);
            if (!test_result("borrowed-value-repeat", s, &e)) return 0;
            CHECK(v.kind == again.kind && v.has_selector == again.has_selector);
            CHECK(v.literal.string_value == again.literal.string_value && v.literal.float_value == again.literal.float_value);
            CHECK(!memcmp(&v.selector, &again.selector, sizeof(v.selector)));
            if (test_dump) {
                printf("VALUE|%zu|%zu|%zu|%d|%d|%d|%d|%lld|%d|%d|", statement, i, v.block_index, v.clause, v.kind, v.has_selector,
                    v.literal.kind, v.literal.integer_value, v.literal.boolean_value, v.literal.quoted_identifier);
                test_text(v.literal.string_value); putchar('|'); test_text(v.literal.float_value); putchar('|');
                test_text(v.operator_name); putchar('|'); test_text(v.bind); putchar('|'); test_text(v.bind_sql); putchar('|');
                printf("%d|%d|%d|%zu|%d|", v.has_bind, v.bind_kind, v.has_bind_sql, v.bind_position, v.has_bind_position);
                test_print_selector(&v.selector); putchar('\n');
            }
            if (v.has_selector && v.selector.kind == SQLPARSER_SELECTOR_KIND_VALUE) {
                PgQuery__Node **slot = NULL; size_t ordinal;
                CHECK(sqlparser_get_statement_node_slot_by_index(h, statement, v.selector.item_index, &slot, NULL, &e) == SQLPARSER_STATUS_OK);
                CHECK(slot && *slot && test_scalar(*slot));
                CHECK(sqlparser_find_statement_node_index_by_node(h, statement, *slot, &ordinal, &e) == SQLPARSER_STATUS_OK);
                CHECK(ordinal == v.selector.item_index); test_selectors++;
                if (first && !first->kind) *first = v.selector;
            }
            test_values++; test_borrowed++;
        }
    }
    return 1;
}
static int test_export(sqlparser_handle_t *h, const char *op, int json)
{
    sqlparser_error_t e; sqlparser_status_t s; char *out = NULL, *snapshot;
    test_seed(&e); s = json ? sqlparser_export_view_json(h, 0, &out, &e) : sqlparser_deparse(h, &out, &e);
    if (!test_result(op, s, &e)) { CHECK(!out); return 0; }
    CHECK(out); snapshot = strdup(out); CHECK(snapshot);
    if (test_dump) { printf("%s|", json ? "JSON" : "SQL"); test_text(out); putchar('\n'); }
    CHECK(!strcmp(snapshot, out)); free(snapshot); sqlparser_string_free(out); return 1;
}
static void test_public_case(const char *sql, sqlparser_dialect_t dialect)
{
    static const char *phases[] = {"native-cold", "json-cold", "graph-json-native", "patch-rebuild"}; size_t mode;
    for (mode = 0; mode < COUNT(phases); mode++) {
        sqlparser_handle_t *h; sqlparser_selector_t first;
        test_set_phase(phases[mode]); h = test_parse(sql, dialect); if (!h) continue;
        if (mode == 0) { (void)test_export(h, "native-sql", 0); goto done; }
        if (mode == 1) { (void)test_export(h, "view-json", 1); goto done; }
        if (!test_graph(h, &first)) goto done;
        if (mode == 3 && first.kind) {
            sqlparser_patch_t p = {0}; sqlparser_patch_list_t patches = {&p, 1};
            sqlparser_error_t e; sqlparser_status_t s; char selector[100];
            snprintf(selector, sizeof(selector), "stmt[%zu].value[%zu]", first.statement_index, first.item_index);
            p.op = SQLPARSER_PATCH_REPLACE; p.selector = selector; p.sql = "713";
            test_seed(&e); s = sqlparser_apply_patch(h, &patches, &e);
            if (!test_result("scalar-patch", s, &e)) goto done;
            test_patches++; if (!test_graph(h, NULL)) goto done;
        }
        if (!test_export(h, "view-json", 1)) goto done;
        (void)test_export(h, "native-sql", 0);
    done:
        sqlparser_handle_destroy(h); /* Never reuse after a public failure. */
    }
}
static void test_lifetimes(void)
{
    size_t dialect;
    const char *sql = "SELECT a + 1, 'borrowed 日本' AS label, NULL FROM t WHERE id = 2";
    for (dialect = 0; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        sqlparser_handle_t *h; sqlparser_query_graph_view_t g, fresh;
        sqlparser_graph_value_t borrowed = {0}, v; sqlparser_error_t e; sqlparser_status_t s;
        sqlparser_selector_t old_selector; sqlparser_literal_value_t literal = {0};
        sqlparser_patch_t p[2] = {{0}}; sqlparser_patch_list_t patches = {p, 2}, empty = {NULL, 0};
        char a[100], b[100], *owned_json = NULL, *saved; size_t i, target = SIZE_MAX, source = SIZE_MAX;
        char *input = strdup(sql); CHECK(input);
        test_begin("borrowed-selector-rebuild", (sqlparser_dialect_t)dialect, sql); test_set_phase("borrowed-positive");
        h = test_parse(input, (sqlparser_dialect_t)dialect); CHECK(h); memset(input, '!', strlen(input)); free(input);
        CHECK(!strcmp(sqlparser_original_sql(h), sql)); CHECK(test_graph(h, NULL));
        CHECK(sqlparser_statement_query_graph(h, 0, &g, &e) == SQLPARSER_STATUS_OK);
        for (i = 0; i < g.value_count; i++) {
            CHECK(sqlparser_query_graph_value_at(&g, i, &v, &e) == SQLPARSER_STATUS_OK);
            if (!v.has_selector || v.selector.kind != SQLPARSER_SELECTOR_KIND_VALUE) continue;
            if (v.literal.kind == SQLPARSER_LITERAL_KIND_STRING) { source = i; borrowed = v; }
            else if (target == SIZE_MAX) target = i;
        }
        CHECK(source != SIZE_MAX && target != SIZE_MAX);
        CHECK(sqlparser_query_graph_value_at(&g, target, &v, &e) == SQLPARSER_STATUS_OK); old_selector = v.selector;
        CHECK(sqlparser_apply_patch(h, &empty, &e) == SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_value_at(&g, source, &v, &e) == SQLPARSER_STATUS_OK);
        CHECK(v.literal.string_value == borrowed.literal.string_value && !strcmp(v.literal.string_value, "borrowed 日本"));
        CHECK(sqlparser_export_view_json(h, 0, &owned_json, &e) == SQLPARSER_STATUS_OK); saved = strdup(owned_json); CHECK(saved);
        snprintf(a, sizeof(a), "stmt[0].value[%zu]", borrowed.selector.item_index);
        snprintf(b, sizeof(b), "stmt[0].value[%zu]", old_selector.item_index);
        literal.kind = SQLPARSER_LITERAL_KIND_STRING; literal.string_value = borrowed.literal.string_value;
        p[0].op = p[1].op = SQLPARSER_PATCH_REPLACE; p[0].selector = a; p[0].sql = "'changed'";
        p[1].selector = b; p[1].literal = &literal; /* Borrowed input must be copied before first edit. */
        test_seed(&e); s = sqlparser_apply_patch(h, &patches, &e); CHECK(test_result("borrowed-two-patch", s, &e));
        test_patches += 2;
        test_seed(&e); s = sqlparser_query_graph_value_at(&g, 0, &v, &e);
        CHECK(s == SQLPARSER_STATUS_INVALID_ARGUMENT); CHECK(!test_result("stale-borrowed-graph", s, &e));
        CHECK(test_graph(h, NULL)); CHECK(sqlparser_statement_query_graph(h, 0, &fresh, &e) == SQLPARSER_STATUS_OK);
        CHECK(fresh.generation == g.generation + 1);
        CHECK(sqlparser_query_graph_value_at(&fresh, target, &v, &e) == SQLPARSER_STATUS_OK);
        CHECK(v.has_selector && !memcmp(&old_selector, &v.selector, sizeof(old_selector)));
        CHECK(v.literal.kind == SQLPARSER_LITERAL_KIND_STRING && !strcmp(v.literal.string_value, "borrowed 日本"));
#ifdef SQLPARSER_SPARSE_VALUE_INTERNAL
        test_compare_cache(h, 0);
#endif
        CHECK(test_export(h, "rebuilt-json", 1)); CHECK(test_export(h, "rebuilt-native-sql", 0));
        sqlparser_handle_destroy(h); CHECK(!strcmp(owned_json, saved)); sqlparser_string_free(owned_json); free(saved); test_borrowed += 3;
    }
}
static void test_dedicated(void)
{
    static const struct { sqlparser_dialect_t dialect; const char *sql; } cases[] = {
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT (((1))), $1, a + 2, coalesce(a, 'x'), NULL, true FROM t WHERE a IN (3,4) ORDER BY 1 LIMIT 5"},
        {SQLPARSER_DIALECT_POSTGRESQL, "WITH q AS (SELECT 1 a, $1 b) SELECT q.a, 2 FROM q UNION ALL SELECT 3, 4 ORDER BY 1"},
        {SQLPARSER_DIALECT_POSTGRESQL, "INSERT INTO t(a,b) VALUES (DEFAULT,$1),(2,3) RETURNING 4"},
        {SQLPARSER_DIALECT_POSTGRESQL, "UPDATE t SET a = DEFAULT, b = $1, c = 2 WHERE id = 3 RETURNING 4"},
        {SQLPARSER_DIALECT_MYSQL, "UPDATE t SET a = 1, b = ? WHERE id = 2 ORDER BY a + 3 LIMIT 4"},
        {SQLPARSER_DIALECT_MYSQL, "DELETE FROM t WHERE id = ? ORDER BY a + 1 LIMIT 2"},
        {SQLPARSER_DIALECT_ORACLE, "SELECT :a, a + 2, '日本' FROM t WHERE b = :b UNION ALL SELECT :a, 3, 'x' FROM t"},
        {SQLPARSER_DIALECT_SQLSERVER, "SELECT TOP (3) @a, a + 2, N'日本' FROM t WHERE b = @b ORDER BY 1"},
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT NULL, TRUE, FALSE, -1, 2.5, 'x', $1; VALUES (DEFAULT,1)"},
        {SQLPARSER_DIALECT_POSTGRESQL, ""}, {SQLPARSER_DIALECT_POSTGRESQL, "SELECT FROM"}
    };
    size_t i;
    for (i = 0; i < COUNT(cases); i++) { char name[60]; snprintf(name, sizeof(name), "dedicated-%zu", i); test_begin(name, cases[i].dialect, cases[i].sql); test_public_case(cases[i].sql, cases[i].dialect); }
}
static void test_corpora(const char *directory)
{
    static const char *files[] = {"sql_batch_input.json", "mysql_dialect_input.json", "oracle_dialect_input.json", "sqlserver_dialect_input.json", "dameng_dialect_input.json", "vastbase_oracle_dialect_input.json", "vastbase_mysql_dialect_input.json", "vastbase_postgresql_dialect_input.json", "vastbase_sqlserver_dialect_input.json", "kingbase_oracle_dialect_input.json", "kingbase_mysql_dialect_input.json", "kingbase_postgresql_dialect_input.json", "kingbase_sqlserver_dialect_input.json"};
    static const char *names[] = {"postgresql", "mysql", "oracle", "sqlserver", "dameng", "vastbase-oracle", "vastbase-mysql", "vastbase-postgresql", "vastbase-sqlserver", "kingbase-oracle", "kingbase-mysql", "kingbase-postgresql", "kingbase-sqlserver"};
    size_t f;
    for (f = 0; f < COUNT(files); f++) {
        char path[4096]; json_error_t error; json_t *root, *items, *item; size_t i;
        CHECK(snprintf(path, sizeof(path), "%s/%s", directory, files[f]) < (int)sizeof(path)); root = json_load_file(path, 0, &error); CHECK(root);
        items = json_object_get(root, "items"); CHECK(json_is_array(items));
        json_array_foreach(items, i, item) {
            const char *sql = json_string_value(json_object_get(item, "sql")), *name = json_string_value(json_object_get(item, "name"));
            const char *declared = json_string_value(json_object_get(item, "dialect")); size_t dialect = f;
            CHECK(sql && name);
            if (declared) { for (dialect = 0; dialect < COUNT(names) && strcmp(declared, names[dialect]); dialect++) {} CHECK(dialect < COUNT(names)); test_overrides += dialect != f; }
            test_begin(name, (sqlparser_dialect_t)dialect, sql); test_public_case(sql, (sqlparser_dialect_t)dialect); test_corpus++;
        }
        fprintf(stderr, "CORPUS|%s|%zu\n", files[f], json_array_size(items)); json_decref(root);
    }
    CHECK(test_corpus == 4004 && test_overrides == 7);
}
int main(int argc, char **argv)
{
    int i, targeted = 0, internal_only = 0; const char *directory = "tests/cases";
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump")) test_dump = 1;
        else if (!strcmp(argv[i], "--targeted")) targeted = 1;
        else if (!strcmp(argv[i], "--internal-only")) internal_only = 1;
        else if (!strcmp(argv[i], "--corpora") && i + 1 < argc) directory = argv[++i];
        else return 2;
    }
#ifdef SQLPARSER_SPARSE_VALUE_INTERNAL
    test_schema(); test_parsed_cache_cases();
#else
    CHECK(!internal_only);
#endif
    if (!internal_only) { test_dedicated(); test_lifetimes(); if (!targeted) test_corpora(directory); }
    fprintf(stderr, "SUMMARY alternatives=%zu internal_fixtures=%zu node_checks=%zu scalar_checks=%zu fallback_checks=%zu duplicate_checks=%zu grouping_wrappers=%zu mysql_tail_wrappers=%zu root_exclusions=%zu corpus_inputs=%zu dialect_overrides=%zu graphs=%zu values=%zu selector_checks=%zu patches=%zu borrowed_checks=%zu statuses=%zu recorded_non_success=%zu\n",
        test_alternatives, test_fixtures, test_nodes, test_scalars, test_fallbacks, test_duplicates, test_grouping, test_tails, test_roots,
        test_corpus, test_overrides, test_graphs, test_values, test_selectors, test_patches, test_borrowed, test_statuses, test_failures);
    return 0;
}
