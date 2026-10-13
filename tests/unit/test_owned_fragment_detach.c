/* Regression coverage for owned fragment detach. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_ast_internal.h"
#include "sqlparser_proto_clone.h"
#include "sqlparser_test_failure.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *case_name = "initialization", *phase = "start";
static int dump, only_observe;
static sqlparser_dialect_t dialect;
static size_t cases, successes, failures, admission_cases, outputs, clones;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "owned fragment:%d case=%s dialect=%d phase=%s: %s\n", __LINE__, case_name, dialect, phase, #x); exit(1); } } while (0)

#ifdef SQLPARSER_M24_OBSERVE
static size_t observed_clones, observed_parses, observed_marks;
static int observing;
sqlparser_status_t __real_sqlparser_clone_proto_node(const PgQuery__Node *, PgQuery__Node **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_clone_proto_node(const PgQuery__Node *n, PgQuery__Node **out, sqlparser_error_t *e)
{
    sqlparser_status_t s = __real_sqlparser_clone_proto_node(n, out, e);
    if (observing && s == SQLPARSER_STATUS_OK) observed_clones++;
    return s;
}
PgQueryOwnedParseResult __real_sqlparser_parse_owned_preserving_identifier_spelling(const char *);
PgQueryOwnedParseResult __wrap_sqlparser_parse_owned_preserving_identifier_spelling(const char *sql)
{
    PgQueryOwnedParseResult r = __real_sqlparser_parse_owned_preserving_identifier_spelling(sql);
    if (observing && r.error == NULL && r.parse_tree != NULL) observed_parses++;
    return r;
}
sqlparser_status_t __real_sqlparser_mark_proto_generated_with_fragment_source(ProtobufCMessage *, const char *, size_t, const sqlparser_generated_source_t *, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_mark_proto_generated_with_fragment_source(ProtobufCMessage *n, const char *sql, size_t offset, const sqlparser_generated_source_t *source, sqlparser_error_t *e)
{
    sqlparser_status_t s = __real_sqlparser_mark_proto_generated_with_fragment_source(n, sql, offset, source, e);
    if (observing && s == SQLPARSER_STATUS_OK) observed_marks++;
    return s;
}
sqlparser_status_t __real_sqlparser_mark_proto_nodes_generated_with_fragment_source(PgQuery__Node **, size_t, const char *, size_t, const sqlparser_generated_source_t *, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_mark_proto_nodes_generated_with_fragment_source(PgQuery__Node **n, size_t count, const char *sql, size_t offset, const sqlparser_generated_source_t *source, sqlparser_error_t *e)
{
    sqlparser_status_t s = __real_sqlparser_mark_proto_nodes_generated_with_fragment_source(n, count, sql, offset, source, e);
    if (observing && s == SQLPARSER_STATUS_OK) observed_marks += count;
    return s;
}
static void observe_start(void) { observed_clones = observed_parses = observed_marks = 0; observing = 1; }
static void observe_end(const char *operation)
{
    observing = 0;
    fprintf(stderr, "OBS|%s|%s|%d|%zu|%zu|%zu\n", case_name, operation, dialect, observed_clones, observed_parses, observed_marks);
}
#else
static void observe_start(void) {}
static void observe_end(const char *operation) { (void)operation; }
#endif

static void text(const char *s)
{
    if (!dump) return;
    if (s == NULL) { putchar('-'); return; }
    for (; *s; s++) printf("%02x", (unsigned char)*s);
}
static void string_record(const char *label, const char *s)
{ if (dump) { printf("%s|", label); text(s); putchar('\n'); } }
static int result(const char *operation, sqlparser_status_t s, const sqlparser_error_t *e)
{
    CHECK(e->code == s);
    CHECK(memchr(e->message, '\0', sizeof(e->message)) != NULL);
    if (s == SQLPARSER_STATUS_OK) CHECK(!e->cursor && !e->line && !e->column && !e->message[0]);
    if (dump) {
        printf("RESULT|%s|%d|%d|%d|%d|", operation, s, e->cursor, e->line, e->column);
        text(e->message); putchar('\n');
    }
    return s == SQLPARSER_STATUS_OK;
}
static char *copy_text(const char *s) { char *p = strdup(s); CHECK(p); return p; }
static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_parse_options_t o;
    sqlparser_error_t e;
    sqlparser_handle_t *h = NULL;
    sqlparser_status_t s;
    sqlparser_parse_options_default(&o); o.dialect = dialect;
    s = sqlparser_parse_with_options(sql, &o, &h, &e);
    if (!result("parse", s, &e)) { CHECK(!h); return NULL; }
    CHECK(h); return h;
}
static void same_encoding(const ProtobufCMessage *a, const ProtobufCMessage *b)
{
    size_t n = protobuf_c_message_get_packed_size(a), m = protobuf_c_message_get_packed_size(b);
    uint8_t *x = malloc(n ? n : 1), *y = malloc(m ? m : 1);
    CHECK(x && y && n == m);
    CHECK(protobuf_c_message_pack(a, x) == n && protobuf_c_message_pack(b, y) == m);
    CHECK(!memcmp(x, y, n)); free(x); free(y);
}
static void root_admission_one(PgQuery__Node *root, sqlparser_status_t expected)
{
    PgQuery__Node *copy = NULL;
    sqlparser_error_t old_error = {0};
    sqlparser_status_t old_status = sqlparser_clone_proto_node(root, &copy, &old_error);
    CHECK(old_status == expected);
#ifdef SQLPARSER_M24_CANDIDATE
    {
        sqlparser_error_t new_error = {0};
        sqlparser_status_t new_status = sqlparser_validate_owned_wrapper_node(root, &new_error);
        CHECK(new_status == old_status);
        CHECK(new_error.code == old_error.code && new_error.cursor == old_error.cursor &&
            new_error.line == old_error.line && new_error.column == old_error.column &&
            !strcmp(new_error.message, old_error.message));
        CHECK(sqlparser_validate_owned_wrapper_node(root, NULL) == old_status);
    }
#endif
    if (copy) { CHECK(copy != root); same_encoding(&root->base, &copy->base); sqlparser_free_proto_node(copy); }
    admission_cases++;
    if (dump) { printf("ADMISSION|%zu|%d|", admission_cases, old_status); text(old_error.message); putchar('\n'); }
}
static void root_admission(void)
{
    const ProtobufCMessageDescriptor *d = &pg_query__node__descriptor;
    unsigned i;
    CHECK(d->n_fields == 268U);
    for (i = 0; i < d->n_fields; i++) {
        const ProtobufCFieldDescriptor *f = d->fields + i;
        const ProtobufCMessageDescriptor *child_d = f->descriptor;
        PgQuery__Node root = PG_QUERY__NODE__INIT;
        ProtobufCMessage *child = malloc(child_d->sizeof_message);
        CHECK(child && f->type == PROTOBUF_C_TYPE_MESSAGE);
        protobuf_c_message_init(child_d, child);
        root.node_case = (PgQuery__Node__NodeCase)f->id;
        memcpy((uint8_t *)&root + f->offset, &child, sizeof(child));
        root_admission_one(&root, SQLPARSER_STATUS_OK);
        protobuf_c_message_free_unpacked(child, NULL);
    }
    {
        PgQuery__Node root = PG_QUERY__NODE__INIT;
        uint8_t data[] = {1};
        ProtobufCMessageUnknownField unknown = {9999, PROTOBUF_C_WIRE_TYPE_VARINT, sizeof(data), data};
        root_admission_one(&root, SQLPARSER_STATUS_INTERNAL_ERROR);
        root.node_case = PG_QUERY__NODE__NODE_STRING;
        root_admission_one(&root, SQLPARSER_STATUS_INTERNAL_ERROR);
        root.node_case = (PgQuery__Node__NodeCase)9999;
        root_admission_one(&root, SQLPARSER_STATUS_INTERNAL_ERROR);
        root.base.n_unknown_fields = 1; root.base.unknown_fields = &unknown;
        root_admission_one(&root, SQLPARSER_STATUS_OK);
        root.node_case = PG_QUERY__NODE__NODE__NOT_SET;
        root_admission_one(&root, SQLPARSER_STATUS_OK);
        root.node_case = PG_QUERY__NODE__NODE_STRING;
        root_admission_one(&root, SQLPARSER_STATUS_OK);
        root_admission_one(NULL, SQLPARSER_STATUS_INVALID_ARGUMENT);
    }
}

static int graph_read(sqlparser_handle_t *h, sqlparser_query_graph_view_t *saved)
{
    sqlparser_error_t e;
    sqlparser_query_graph_view_t g;
    sqlparser_bind_occurrence_view_t binds;
    sqlparser_status_t s;
    size_t i, count;
    char *json = NULL;
    s = sqlparser_statement_query_graph(h, 0, &g, &e);
    if (!result("graph", s, &e)) return 0;
    if (saved) *saved = g;
    if (dump) printf("GRAPH|%lu|%zu|%zu|%zu|%zu|%zu|%zu|%zu|%d\n", g.generation,
        g.block_count, g.relation_count, g.target_count, g.field_count, g.value_count,
        g.set_count, g.predicate_count, g.has_dml);
    s = sqlparser_query_graph_expression_count(&g, &count, &e);
    if (!result("expressions", s, &e)) return 0;
    for (i = 0; i < count; i++) {
        sqlparser_graph_expression_t x;
        s = sqlparser_query_graph_expression_at(&g, i, &x, &e);
        if (!result("expression", s, &e)) return 0;
        if (dump) printf("EXPR|%zu|%d|%d|%zu|%zu\n", x.index, x.clause, x.kind, x.arguments.offset, x.arguments.count);
        string_record("EXPRSQL", x.sql); string_record("EXPRNAME", x.name);
    }
    s = sqlparser_handle_bind_occurrences(h, &binds, &e);
    if (!result("binds", s, &e)) return 0;
    for (i = 0; i < binds.count; i++) {
        sqlparser_bind_occurrence_t b;
        s = sqlparser_bind_occurrence_at(&binds, i, &b, &e);
        if (!result("bind", s, &e)) return 0;
        if (dump) printf("BIND|%zu|%d\n", b.position, b.kind);
        string_record("BINDKEY", b.key); string_record("BINDSQL", b.sql);
    }
    s = sqlparser_statement_name_count(h, 0, &count, &e);
    if (!result("names", s, &e)) return 0;
    for (i = 0; i < count; i++) {
        sqlparser_name_view_t n;
        s = sqlparser_statement_name(h, 0, i, &n, &e);
        if (!result("name", s, &e)) return 0;
        string_record("OWNER", n.owner_type); string_record("FIELD", n.field_name); string_record("NAME", n.value);
    }
    s = sqlparser_export_view_json(h, 0, &json, &e);
    if (!result("view", s, &e)) { CHECK(!json); return 0; }
    CHECK(json); string_record("VIEW", json); sqlparser_string_free(json);
    return 1;
}
static void stale_graph(sqlparser_query_graph_view_t *g)
{
    sqlparser_error_t e; size_t count = 999;
    sqlparser_status_t s = sqlparser_query_graph_expression_count(g, &count, &e);
    CHECK(s == SQLPARSER_STATUS_INVALID_ARGUMENT);
    (void)result("stale-graph", s, &e);
}

enum route { WHERE, SELECT_LIST, ASSIGN_VALUE, UPDATE_LIST, ORDER_LIST, SESSION, TARGET_ONE, ASSIGN_ONE, INSERT_CELL, ROUTE_COUNT };
static const char *route_names[] = {"where", "select-list", "assignment-value", "update-list", "order-list", "session-value", "single-target", "single-assignment", "insert-cell-control"};
static const char *base_sql(enum route r)
{
    if (r == SESSION) return "/* head */ SET search_path TO 'old'; /* tail */";
    if (r == ASSIGN_VALUE || r == UPDATE_LIST || r == ASSIGN_ONE)
        return "/* head */ UPDATE MixedTable SET a = 1, b = UPPER('old') WHERE id = 7; /* tail */";
    if (r == INSERT_CELL) return "/* head */ INSERT INTO MixedTable(a,b) VALUES (1,UPPER('old')); /* tail */";
    return "/* head */ SELECT a, UPPER('old') AS OldAlias FROM MixedTable WHERE a = 1 ORDER BY a; /* tail */";
}
static int make_patch(sqlparser_handle_t *h, enum route r, const char *sql, sqlparser_patch_t *p, char *selector, size_t capacity)
{
    sqlparser_error_t e; sqlparser_status_t s; size_t count, i;
    memset(p, 0, sizeof(*p)); p->op = SQLPARSER_PATCH_REPLACE; p->sql = sql;
    if (r == WHERE) snprintf(selector, capacity, "stmt[0].where[0]");
    else if (r == ASSIGN_VALUE || r == ASSIGN_ONE) {
        snprintf(selector, capacity, "stmt[0].assignment[0]");
        if (r == ASSIGN_ONE) p->op = SQLPARSER_PATCH_REPLACE_ASSIGNMENT;
    } else if (r == SESSION) snprintf(selector, capacity, "stmt[0].value[0]");
    else if (r == TARGET_ONE) snprintf(selector, capacity, "stmt[0].select_target[0][0]");
    else if (r == INSERT_CELL) snprintf(selector, capacity, "stmt[0].insert_cell[0][0]");
    else {
        sqlparser_clause_kind_t wanted = r == SELECT_LIST ? SQLPARSER_CLAUSE_KIND_SELECT_LIST :
            r == UPDATE_LIST ? SQLPARSER_CLAUSE_KIND_SET_LIST : SQLPARSER_CLAUSE_KIND_ORDER_BY;
        s = sqlparser_statement_clause_count(h, 0, &count, &e);
        if (!result("find-clause-count", s, &e)) return 0;
        for (i = 0; i < count; i++) {
            sqlparser_clause_view_t v;
            s = sqlparser_statement_clause(h, 0, i, &v, &e);
            if (!result("find-clause", s, &e)) return 0;
            if (v.kind == wanted) { snprintf(selector, capacity, "stmt[0].clause[%zu]", i); break; }
        }
        CHECK(i < count);
    }
    p->selector = selector; return 1;
}
static int apply(sqlparser_handle_t *h, sqlparser_patch_t *p, size_t count, const char *label)
{
    sqlparser_patch_list_t list = {p, count};
    sqlparser_error_t e; sqlparser_status_t s;
    observe_start(); s = sqlparser_apply_patch(h, &list, &e); observe_end(label);
    return result(label, s, &e);
}
/* All returned SQL allocations are retained across later reads, edits, and
 * destruction. A separate parse records independent semantic/deparse output;
 * existing round-trip/source-rebase errors are retained rather than hidden. */
static int output_pair(sqlparser_handle_t *h, char **out, char **snapshot)
{
    sqlparser_error_t e; sqlparser_status_t s; char *second = NULL;
    observe_start(); s = sqlparser_deparse(h, out, &e); observe_end("deparse");
    if (!result("deparse", s, &e)) { CHECK(!*out); CHECK(sqlparser_test_failed_handle(h)); return 0; }
    CHECK(*out); *snapshot = copy_text(*out); outputs++;
    string_record("SQL", *out);
    s = sqlparser_deparse(h, &second, &e);
    if (!result("deparse-again", s, &e)) { CHECK(!second); return 0; }
    CHECK(second && second != *out && !strcmp(second, *out)); outputs++;
    sqlparser_string_free(second);
    return 1;
}
static int fragment_renders(sqlparser_handle_t *h)
{
    sqlparser_error_t e;
    sqlparser_status_t s;
    size_t i, count;
    s = sqlparser_statement_clause_count(h, 0, &count, &e);
    if (!result("render-clause-count", s, &e)) return 0;
    for (i = 0; i < count; i++) {
        char *first = NULL, *second = NULL;
        observe_start(); s = sqlparser_statement_clause_sql(h, 0, i, &first, &e); observe_end("fragment-render");
        if (!result("fragment-render", s, &e)) { CHECK(!first); return 0; }
        string_record("FRAGMENT-RENDER", first);
        s = sqlparser_statement_clause_sql(h, 0, i, &second, &e);
        if (!result("fragment-render-again", s, &e)) { sqlparser_string_free(first); CHECK(!second); return 0; }
        CHECK((first == NULL) == (second == NULL));
        if (first) CHECK(first != second && !strcmp(first, second));
        sqlparser_string_free(first); sqlparser_string_free(second);
    }
    /* Session fragment rendering is reached via the graph publicly. Also
     * exercise the unchanged private renderer directly on its live owned root. */
    CHECK(sqlparser_handle_ensure_ast(h, &e) == SQLPARSER_STATUS_OK);
    if (h->ast && h->ast->n_stmts && h->ast->stmts[0]->stmt->node_case == PG_QUERY__NODE__NODE_VARIABLE_SET_STMT) {
        PgQuery__VariableSetStmt *stmt = h->ast->stmts[0]->stmt->variable_set_stmt;
        for (i = 0; i < stmt->n_args; i++) {
            char *sql = NULL;
            observe_start(); s = sqlparser_render_variable_set_arg_node_sql(h, stmt->args[i], &sql, &e); observe_end("session-render");
            if (!result("session-render", s, &e)) { CHECK(!sql); return 0; }
            string_record("SESSION-RENDER", sql); sqlparser_string_free(sql);
        }
    }
    return 1;
}

static void private_fragments(void)
{
    typedef sqlparser_status_t (*parser_t)(const char *, const sqlparser_generated_source_t *, PgQuery__Node **, sqlparser_error_t *);
    static const parser_t parsers[] = {sqlparser_parse_where_node_sql, sqlparser_parse_select_target_node_sql,
        sqlparser_parse_update_assignment_node_sql, sqlparser_parse_variable_set_arg_node_sql};
    static const char *const inputs[] = {"0", "'日本''quote'", "", " ", "/* comment */", "1; SELECT 2", "1, 2", "DEFAULT", NULL};
    size_t i, j;
    static const struct { int status, cursor, line, column; const char *message; } baseline[4][9] = {
        {
            {0, 0, 0, 0, ""},
            {0, 0, 0, 0, ""},
            {1, 0, 0, 0, "WHERE SQL must not be NULL or empty"},
            {3, 43, 1, 43, "syntax error at end of input"},
            {3, 55, 1, 55, "syntax error at end of input"},
            {4, 0, 0, 0, "wrapped WHERE parse tree is invalid"},
            {3, 43, 1, 43, "syntax error at or near \",\""},
            {0, 0, 0, 0, ""},
            {1, 0, 0, 0, "WHERE SQL must not be NULL or empty"},
        },
        {
            {0, 0, 0, 0, ""},
            {0, 0, 0, 0, ""},
            {1, 0, 0, 0, "select target SQL must not be NULL or empty"},
            {4, 0, 0, 0, "wrapped SELECT parse tree does not contain target list"},
            {4, 0, 0, 0, "wrapped SELECT parse tree does not contain target list"},
            {4, 0, 0, 0, "wrapped SELECT parse tree is invalid"},
            {5, 0, 0, 0, "select target SQL must contain exactly one target"},
            {0, 0, 0, 0, ""},
            {1, 0, 0, 0, "select target SQL must not be NULL or empty"},
        },
        {
            {0, 0, 0, 0, ""},
            {0, 0, 0, 0, ""},
            {1, 0, 0, 0, "assignment SQL must not be NULL or empty"},
            {3, 57, 1, 57, "syntax error at end of input"},
            {3, 69, 1, 69, "syntax error at end of input"},
            {4, 0, 0, 0, "wrapped update parse tree is invalid"},
            {3, 59, 1, 59, "syntax error at or near \"2\""},
            {0, 0, 0, 0, ""},
            {1, 0, 0, 0, "assignment SQL must not be NULL or empty"},
        },
        {
            {0, 0, 0, 0, ""},
            {0, 0, 0, 0, ""},
            {1, 0, 0, 0, "SET argument SQL must not be NULL or empty"},
            {3, 40, 1, 40, "syntax error at end of input"},
            {3, 52, 1, 52, "syntax error at end of input"},
            {4, 0, 0, 0, "wrapped SET parse tree is invalid"},
            {4, 0, 0, 0, "wrapped SET parse tree does not contain one argument"},
            {4, 0, 0, 0, "wrapped SET parse tree does not contain one argument"},
            {1, 0, 0, 0, "SET argument SQL must not be NULL or empty"},
        },
    };
    phase = "private-shape"; case_name = "private-wrapper-shapes";
    for (i = 0; i < COUNT(parsers); i++) for (j = 0; j < COUNT(inputs); j++) {
        PgQuery__Node *node = NULL;
        sqlparser_error_t e = {0};
        sqlparser_status_t status = parsers[i](inputs[j], NULL, &node, &e);
        if (dump) printf("PRIVATE|%zu|%zu\n", i, j);
        (void)result("private-wrapper", status, &e);
        CHECK((int)status == baseline[i][j].status && e.cursor == baseline[i][j].cursor &&
            e.line == baseline[i][j].line && e.column == baseline[i][j].column &&
            !strcmp(e.message, baseline[i][j].message));
        if (status == SQLPARSER_STATUS_OK) {
            PgQuery__Node *copy = NULL;
            CHECK(node && sqlparser_clone_proto_node(node, &copy, &e) == SQLPARSER_STATUS_OK);
            same_encoding(&node->base, &copy->base);
            sqlparser_free_proto_node(node); sqlparser_free_proto_node(copy);
        } else CHECK(node == NULL);
    }
}
static void single_target_insert(void)
{
    sqlparser_error_t e; sqlparser_status_t status;
    static const char *const text[] = {"a AS NewAlias", "a AS FirstAlias, b AS SecondAlias"};
    size_t i;
    for (i = 0; i < COUNT(text); i++) {
        sqlparser_handle_t *h = parse(base_sql(SELECT_LIST));
        CHECK(h); case_name = "single-target-insertion-guard"; phase = "insert-target";
        status = sqlparser_select_insert_target_sql(h, 0, 0, 1, text[i], &e);
        if (dump) printf("SINGLE-TARGET-INSERT|%zu\n", i);
        (void)result("insert-target", status, &e);
        CHECK(status == (i ? SQLPARSER_STATUS_UNSUPPORTED : SQLPARSER_STATUS_OK));
        if (i) CHECK(sqlparser_test_failed_handle(h)); else CHECK(graph_read(h, NULL));
        sqlparser_handle_destroy(h);
    }
}

static void run_case(const char *name, enum route r, const char *replacement, int warm, int required)
{
    sqlparser_handle_t *h, *fresh;
    sqlparser_query_graph_view_t old = {0};
    sqlparser_patch_t p;
    char selector[96], *input = copy_text(base_sql(r)), *fragment = replacement ? copy_text(replacement) : NULL;
    char *out[2] = {NULL, NULL}, *snap[2] = {NULL, NULL};
    sqlparser_error_t e; sqlparser_status_t s; size_t round;
    int have_old = 0;
    case_name = name; phase = "parse"; cases++;
    if (dump) { printf("CASE|%zu|%d|%d|%d|", cases, dialect, r, warm); text(name); putchar('\n'); }
    string_record("INPUT", input); string_record("FRAGMENT", fragment);
    h = parse(input);
    if (!h) { CHECK(!required); goto done; }
    CHECK(!strcmp(sqlparser_original_sql(h), input));
    if (warm) { phase = "warm-before"; if (!graph_read(h, &old)) { CHECK(!required); goto destroy; } have_old = 1; }
    if (!make_patch(h, r, fragment, &p, selector, sizeof(selector))) { CHECK(!required); goto destroy; }
    for (round = 0; round < 2; round++) {
        phase = round ? "repeat-apply" : "raw-apply";
        if (dump) printf("ROUND|%zu\n", round);
        if (!apply(h, &p, 1, phase)) {
            failures++; CHECK(!required); CHECK(sqlparser_test_failed_handle(h));
            if (have_old) stale_graph(&old);
            goto destroy;
        }
        successes++;
        if (have_old) stale_graph(&old);
        CHECK(!strcmp(input, base_sql(r)));
        string_record("CURRENT-SOURCE", sqlparser_original_sql(h));
        CHECK(!replacement || !strcmp(fragment, replacement));
        phase = "post-cold";
        if (!graph_read(h, &old)) { CHECK(!required); goto destroy; }
        have_old = 1; phase = "post-warm";
        if (!graph_read(h, NULL)) { CHECK(!required); goto destroy; }
        if (!fragment_renders(h)) { CHECK(!required); goto destroy; }
        if (!output_pair(h, &out[round], &snap[round])) { CHECK(!required); goto destroy; }
        if (round == 0) {
            phase = "independent-reparse"; fresh = parse(out[0]);
            if (fresh) { (void)graph_read(fresh, NULL); sqlparser_handle_destroy(fresh); }
        }
        for (size_t j = 0; j <= round; j++) CHECK(!strcmp(out[j], snap[j]));
    }
    /* Whole-tree clones remain independent after detached nodes have been
     * incorporated into a live AST and through its destruction. */
    phase = "clone-survival";
    s = sqlparser_handle_ensure_ast(h, &e); CHECK(s == SQLPARSER_STATUS_OK);
    if (h->ast) {
        ProtobufCMessage *copy = sqlparser_clone_proto_message(&h->ast->base);
        ProtobufCMessage *other = sqlparser_clone_proto_message(&h->ast->base);
        CHECK(copy && other && copy != other); same_encoding(copy, other);
        same_encoding(copy, &h->ast->base); clones++;
        sqlparser_handle_destroy(h); h = NULL;
        same_encoding(copy, other); protobuf_c_message_free_unpacked(copy, NULL); protobuf_c_message_free_unpacked(other, NULL);
    }
 destroy:
    sqlparser_handle_destroy(h);
 done:
    for (round = 0; round < 2; round++) if (out[round]) {
        CHECK(snap[round] && !strcmp(out[round], snap[round]));
        sqlparser_string_free(out[round]); free(snap[round]);
    }
    CHECK(!strcmp(input, base_sql(r))); free(input); free(fragment);
}

static const char *fragments[6][13] = {
    {"0 = 0", "COALESCE(a, (SELECT MAX(id) FROM NestedTable)) > 2 AND b = 'café 日本 ''quote'''", "a = 2 /* fragment */", "", "   ", "/* only comment */", "a =", "TRUE; SELECT 2", "a = $2", "a = :NamedBind", "a = ?", "a = @NamedBind", "a = :2"},
    {"0 AS ZeroAlias, b AS OtherAlias, 3 AS ThirdAlias", "COALESCE(a, (SELECT MAX(id) FROM NestedTable)) AS \"日本Alias\", 'café ''quote''' AS TextAlias", "a /* fragment */, b", "", "   ", "/* only comment */", "a,", "a; SELECT 2", "$2 AS BoundAlias", ":NamedBind AS BoundAlias", "? AS BoundAlias", "@NamedBind AS BoundAlias", ":2 AS BoundAlias"},
    {"0", "COALESCE((SELECT MAX(id) FROM NestedTable), LENGTH('café 日本 ''quote'''))", "2 /* fragment */", "", "   ", "/* only comment */", "(", "1; SELECT 2", "$2", ":NamedBind", "?", "@NamedBind", ":2"},
    {"a = 0, b = 2, c = 3", "a = COALESCE((SELECT MAX(id) FROM NestedTable), 2), b = 'café 日本 ''quote'''", "a = 2 /* fragment */, b = 3", "", "   ", "/* only comment */", "a =", "a = 1; SELECT 2", "a = $2, b = $1", "a = :NamedBind, b = :OtherBind", "a = ?, b = ?", "a = @NamedBind, b = @OtherBind", "a = :2, b = :1"},
    {"a DESC, b ASC, id DESC", "COALESCE(a, (SELECT MAX(id) FROM NestedTable)) DESC, LENGTH('café 日本 ''quote''') ASC", "a /* fragment */ DESC, b ASC", "", "   ", "/* only comment */", "a,", "a; SELECT 2", "$2 DESC", ":NamedBind DESC", "? DESC", "@NamedBind DESC", ":2 DESC"},
    {"0", "'café 日本 ''quote'''", "next_schema /* fragment */", "", "   ", "/* only comment */", "(", "next_schema; SELECT 2", "$2", ":NamedBind", "?", "@NamedBind", ":2"}
};

static void borrowed_and_source(void)
{
    sqlparser_handle_t *h;
    sqlparser_query_graph_view_t g;
    sqlparser_graph_expression_t x;
    sqlparser_patch_t p[2] = {{0}};
    sqlparser_error_t e; sqlparser_status_t s;
    char *before = NULL, *snap = NULL, *after = NULL, *after_snap = NULL;
    case_name = "borrowed-and-source-selector"; phase = "parse"; cases++;
    if (dump) printf("CASE|%zu|%d|borrowed\n", cases, dialect);
    h = parse("/* head */ UPDATE t SET a = UPPER('original 日本'), b = 0, c = 0; SELECT a FROM t WHERE a = UPPER('borrowed 日本'); /* tail */"); CHECK(h);
    CHECK(graph_read(h, &g));
    s = sqlparser_statement_query_graph(h, 1, &g, &e); CHECK(s == SQLPARSER_STATUS_OK);
    s = sqlparser_query_graph_expression_at(&g, 0, &x, &e); CHECK(s == SQLPARSER_STATUS_OK && x.sql);
    string_record("BORROWED", x.sql);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[1]", .sql=x.sql};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ASSIGNMENT, .selector="stmt[0].assignment[2]", .name="copied", .source_selector="stmt[0].assignment[0]"};
    CHECK(apply(h, p, 2, "borrowed-source-batch")); stale_graph(&g);
    CHECK(graph_read(h, &g)); CHECK(output_pair(h, &before, &snap));
    /* Change the source after copying it. Destinations must remain independent. */
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[0]", .sql="LOWER('changed')"};
    CHECK(apply(h, p, 1, "source-change")); stale_graph(&g);
    CHECK(output_pair(h, &after, &after_snap)); CHECK(strstr(after, "UPPER('original 日本')"));
    CHECK(!strcmp(before, snap));
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ASSIGNMENT, .selector="stmt[0].assignment[4]", .name="generated_copy", .source_selector="stmt[0].assignment[0]"};
    CHECK(apply(h, p, 1, "copy-detached-source"));
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[0]", .sql="LENGTH('last')"};
    CHECK(apply(h, p, 1, "change-detached-source"));
    {
        char *sql = NULL;
        s = sqlparser_deparse(h, &sql, &e); CHECK(s == SQLPARSER_STATUS_OK && sql);
        string_record("COPY-SURVIVES", sql);
        CHECK(strstr(sql, "LOWER('changed')") && strstr(sql, "UPPER('original 日本')"));
        sqlparser_string_free(sql);
    }
    CHECK(graph_read(h, &g));
    s = sqlparser_statement_query_graph(h, 1, &g, &e); CHECK(s == SQLPARSER_STATUS_OK);
    s = sqlparser_query_graph_expression_at(&g, 0, &x, &e); CHECK(s == SQLPARSER_STATUS_OK && x.sql);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].assignment[1]", .sql=x.sql};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[999].assignment[0]", .sql="1"};
    CHECK(!apply(h, p, 2, "terminal-after-borrow")); stale_graph(&g); CHECK(sqlparser_test_failed_handle(h));
    sqlparser_handle_destroy(h);
    CHECK(!strcmp(before, snap) && !strcmp(after, after_snap));
    sqlparser_string_free(before); sqlparser_string_free(after); free(snap); free(after_snap);
}
static void long_cases(void)
{
    char payload[8192], quoted[8300], target[8400], where[8400], assignment[8400], order[8400];
    memset(payload, 'x', sizeof(payload) - 1); payload[sizeof(payload) - 1] = 0;
    snprintf(quoted, sizeof(quoted), "'%s日本''quoted'", payload);
    snprintf(target, sizeof(target), "%s AS LongAlias, 2 AS SecondAlias", quoted);
    snprintf(where, sizeof(where), "b = %s", quoted);
    snprintf(assignment, sizeof(assignment), "a = %s, b = 2", quoted);
    snprintf(order, sizeof(order), "LENGTH(%s) DESC, a ASC", quoted);
    run_case("long-where", WHERE, where, 1, 1);
    run_case("long-target", SELECT_LIST, target, 1, 1);
    run_case("long-value", ASSIGN_VALUE, quoted, 1, 1);
    run_case("long-update-list", UPDATE_LIST, assignment, 1, 1);
    run_case("long-order", ORDER_LIST, order, 1, 1);
    run_case("long-session", SESSION, quoted, 1, 1);
}
int main(int argc, char **argv)
{
    int d, warm; size_t f; enum route r;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump")) dump = 1;
        else if (!strcmp(argv[i], "--observe-only")) only_observe = 1;
        else CHECK(0);
    }
    if (!only_observe) { root_admission(); private_fragments(); single_target_insert(); }
    for (d = 0; d <= 12; d++) {
        dialect = (sqlparser_dialect_t)d;
        if (only_observe && d != 0) continue;
        for (r = WHERE; r <= SESSION; r++) for (f = 0; f < (only_observe ? 1U : COUNT(fragments[0])); f++) for (warm = 0; warm < 2; warm++) {
            char name[100]; snprintf(name, sizeof(name), "%s-fragment-%zu-%s", route_names[r], f, warm ? "warm" : "cold");
            run_case(name, r, fragments[r][f], warm, f == 0 && d == 0);
        }
        if (only_observe) { borrowed_and_source(); continue; }
        run_case("single-target", TARGET_ONE, "a + 1 AS NewAlias", 1, 0);
        run_case("single-target-expands-list", TARGET_ONE, "a AS FirstAlias, b AS SecondAlias", 1, 0);
        run_case("single-assignment", ASSIGN_ONE, "a = COALESCE(b, 3)", 1, 0);
        run_case("single-assignment-reject-list", ASSIGN_ONE, "a = 2, b = 3", 1, 0);
        run_case("insert-detach-control", INSERT_CELL, "LENGTH('日本') + 2", 1, 0);
        run_case("null-where", WHERE, NULL, 1, 0);
        run_case("null-target-list", SELECT_LIST, NULL, 1, 0);
        run_case("null-assignment", ASSIGN_VALUE, NULL, 1, 0);
        run_case("null-update-list", UPDATE_LIST, NULL, 1, 0);
        run_case("null-order", ORDER_LIST, NULL, 1, 0);
        run_case("null-session", SESSION, NULL, 1, 0);
        run_case("national-target", SELECT_LIST, "N'café 日本' AS TextAlias", 1, 0);
        run_case("national-assignment", ASSIGN_VALUE, "N'café 日本'", 1, 0);
        {
            const char *quoted = d == 1 || d == 6 || d == 10 ? "`Mixed``Name`" :
                d == 3 || d == 8 || d == 12 ? "[Mixed]]Name]" : "\"Mixed\"\"Name\"";
            char text[256];
            snprintf(text, sizeof(text), "%s = 2", quoted); run_case("dialect-quoted-where", WHERE, text, 1, 0);
            snprintf(text, sizeof(text), "%s AS %s, 2 AS OtherAlias", quoted, quoted); run_case("dialect-quoted-targets", SELECT_LIST, text, 1, 0);
            snprintf(text, sizeof(text), "%s = 2, b = 3", quoted); run_case("dialect-quoted-update", UPDATE_LIST, text, 1, 0);
            snprintf(text, sizeof(text), "%s DESC, b ASC", quoted); run_case("dialect-quoted-order", ORDER_LIST, text, 1, 0);
        }
        if (d == 0) { borrowed_and_source(); long_cases(); }
    }
    fprintf(stderr, "PASS cases=%zu successful_applies=%zu expected_failures=%zu admission_cases=%zu independent_outputs=%zu surviving_clones=%zu\n", cases, successes, failures, admission_cases, outputs, clones);
    return 0;
}
