/* Regression coverage for scalar name commit. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "sqlparser_ast_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static const char *case_name = "startup", *phase = "startup";
static sqlparser_dialect_t dialect;
static size_t checkpoints, mutation_entries, setters, patches, clones, fixtures;
static size_t admitted_states, fallback_states, failures, controls;
static int dump;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "scalar commit:%d case=%s dialect=%d phase=%s: %s\n", __LINE__, case_name ? case_name : "(missing)", dialect, phase, #x); exit(1); } } while (0)

typedef struct { char *p; size_t n, cap; } buffer_t;
static void append(buffer_t *b, const char *format, ...)
{
    va_list a, c; int needed;
    va_start(a, format); va_copy(c, a); needed = vsnprintf(NULL, 0, format, c); va_end(c);
    CHECK(needed >= 0);
    if (b->n + (size_t)needed + 1 > b->cap) {
        b->cap = 2 * (b->n + (size_t)needed + 1) + 128;
        b->p = realloc(b->p, b->cap); CHECK(b->p);
    }
    vsnprintf(b->p + b->n, b->cap - b->n, format, a); va_end(a); b->n += (size_t)needed;
}
static void field(buffer_t *b, const char *s)
{ if (s) append(b, "%zu:%s\n", strlen(s), s); else append(b, "NULL\n"); }
static void emit(const buffer_t *b)
{ if (dump) CHECK(fwrite(b->p, 1, b->n, stdout) == b->n); }
static int result(sqlparser_status_t s, sqlparser_error_t *e)
{
    CHECK(e->code == s && memchr(e->message, 0, sizeof(e->message)));
    if (s != SQLPARSER_STATUS_OK) fprintf(stderr, "RESULT case=%s phase=%s status=%d error=%s\n", case_name, phase, s, e->message);
    return s == SQLPARSER_STATUS_OK;
}
static sqlparser_handle_t *parse_options(const char *sql, sqlparser_parse_options_t *o)
{
    sqlparser_handle_t *h = NULL; sqlparser_error_t e = {0};
    CHECK(result(sqlparser_parse_with_options(sql, o, &h, &e), &e) && h);
    return h;
}
static sqlparser_handle_t *parse(const char *sql)
{
    sqlparser_parse_options_t o; sqlparser_parse_options_default(&o); o.dialect = dialect;
    return parse_options(sql, &o);
}
/* No new bulk-name API: this is an old-public-API differential driver. */
static size_t find_name(sqlparser_handle_t *h, size_t stmt, const char *owner, const char *value, size_t occurrence)
{
    sqlparser_error_t e = {0}; size_t n, i;
    CHECK(result(sqlparser_statement_name_count(h, stmt, &n, &e), &e));
    for (i = 0; i < n; i++) {
        sqlparser_name_view_t v;
        CHECK(result(sqlparser_statement_name(h, stmt, i, &v, &e), &e));
        if ((!owner || !strcmp(owner, v.owner_type)) && (!value || !strcmp(value, v.value))) {
            if (!occurrence--) return i;
        }
    }
    fprintf(stderr, "missing name owner=%s value=%s\n", owner ? owner : "*", value ? value : "*");
    CHECK(0); return SIZE_MAX;
}
static int admitted(sqlparser_handle_t *h)
{
    return h && !h->failed && h->ast && h->representation == SQLPARSER_REPRESENTATION_OWNED_AST &&
        h->ast_state_bound && !h->control && h->dialect == SQLPARSER_DIALECT_POSTGRESQL &&
        h->dialect_ops == sqlparser_dialect_postgresql_ops() && !h->dialect_ops->prepare_ast_state;
}
static void snapshot(sqlparser_handle_t *h, buffer_t *b)
{
    sqlparser_error_t e = {0}; sqlparser_bind_occurrence_view_t binds;
    char *sql = NULL, *json = NULL; size_t s, n, i;
    CHECK(!h->failed);
    append(b, "STATE|%lu|%zu|%zu|%u\n", h->generation, sqlparser_statement_count(h), h->identifier_mutation_count, h->patch_batch_flags);
    field(b, sqlparser_original_sql(h));
    CHECK(result(sqlparser_deparse(h, &sql, &e), &e)); field(b, sql); sqlparser_string_free(sql);
    for (s = 0; s < sqlparser_statement_count(h); s++) {
        sqlparser_statement_kind_t kind; const char *node = NULL;
        CHECK(result(sqlparser_statement_kind(h, s, &kind, &e), &e));
        CHECK(result(sqlparser_statement_node_name(h, s, &node, &e), &e));
        CHECK(result(sqlparser_statement_name_count(h, s, &n, &e), &e));
        append(b, "STMT|%zu|%d|%zu\n", s, kind, n); field(b, node);
        for (i = 0; i < n; i++) {
            sqlparser_name_view_t v, again;
            CHECK(result(sqlparser_statement_name(h, s, i, &v, &e), &e));
            CHECK(result(sqlparser_statement_name(h, s, i, &again, &e), &e));
            CHECK(v.value == again.value && v.owner_type == again.owner_type && v.field_name == again.field_name);
            field(b, v.owner_type); field(b, v.field_name); field(b, v.value);
        }
    }
    CHECK(result(sqlparser_handle_bind_occurrences(h, &binds, &e), &e)); append(b, "BINDS|%zu\n", binds.count);
    for (i = 0; i < binds.count; i++) {
        sqlparser_bind_occurrence_t v;
        CHECK(result(sqlparser_bind_occurrence_at(&binds, i, &v, &e), &e));
        append(b, "%zu|%d\n", v.position, v.kind); field(b, v.key); field(b, v.sql);
    }
    CHECK(result(sqlparser_export_view_json(h, 0, &json, &e), &e)); field(b, json); sqlparser_string_free(json);
}
/* Copy raw pointer VALUES without dereferencing cached slots. A genuine fresh
 * traversal must reproduce the complete tuple exactly. Check immediately after
 * commits, before any read-side work could incidentally repair a stale binding. */
static void checkpoint(sqlparser_handle_t *h, const char *label)
{
    size_t n = h->identifier_mutation_count, i;
    sqlparser_identifier_mutation_t *saved = malloc((n + 1) * sizeof(*saved));
    sqlparser_error_t e = {0}; buffer_t before = {0}, after = {0};
    unsigned long generation = h->generation;
    if (!h->ast_state_bound) fprintf(stderr, "COLD phase=%s representation=%d ast=%d mutations=%zu\n", phase, h->representation, h->ast != NULL, n);
    CHECK(saved && !h->failed);
    if (!h->ast_state_bound) { CHECK(n == 0); CHECK(sqlparser_handle_ensure_ast(h, &e) == SQLPARSER_STATUS_OK); }
    if (admitted(h)) admitted_states++; else fallback_states++;
    if (n) memcpy(saved, h->identifier_mutations, n * sizeof(*saved));
    CHECK(sqlparser_handle_rebind_identifier_mutations(h, &e) == SQLPARSER_STATUS_OK);
    CHECK(h->identifier_mutation_count == n && h->generation == generation);
    for (i = 0; i < n; i++) {
        sqlparser_identifier_mutation_t *m = h->identifier_mutations + i, *v = saved + i;
        CHECK(v->raw_statement_index == m->raw_statement_index && v->string_index == m->string_index);
        CHECK(v->slot == m->slot && v->value == m->value && m->slot && m->value);
        CHECK(v->original == m->original && v->spelling == m->spelling && v->relation_group == m->relation_group);
        CHECK(v->source_component_index == m->source_component_index && v->has_source_component == m->has_source_component && v->source_present == m->source_present);
        /* Only the fresh traversal's certified field is dereferenced. */
        CHECK(*m->slot == m->value); mutation_entries++;
    }
    snapshot(h, &before);
    CHECK(sqlparser_handle_rebind_identifier_mutations(h, &e) == SQLPARSER_STATUS_OK);
    snapshot(h, &after); CHECK(before.n == after.n && !memcmp(before.p, after.p, before.n));
    if (dump) printf("CHECK|%s|%s|%d\n", case_name, label, dialect);
    emit(&before);
    if (dump) for (i = 0; i < n; i++) {
        sqlparser_identifier_mutation_t *m = h->identifier_mutations + i;
        printf("MUTATION|%zu|%zu|%zu|%zu|%d|%d|%zu:%s\n", m->raw_statement_index,
            m->string_index, m->relation_group, m->source_component_index, m->has_source_component,
            m->source_present, strlen(m->value), m->value);
    }
    checkpoints++; free(saved); free(before.p); free(after.p);
}
static void rename_at(sqlparser_handle_t *h, size_t stmt, size_t index, const char *value, const char *label)
{
    sqlparser_error_t e = {0}; phase = label;
    CHECK(result(sqlparser_statement_set_name(h, stmt, index, value, &e), &e)); setters++; checkpoint(h, label);
}
static void rename_value(sqlparser_handle_t *h, size_t stmt, const char *owner, const char *old, const char *value, const char *label)
{ rename_at(h, stmt, find_name(h, stmt, owner, old, 0), value, label); }
static void apply(sqlparser_handle_t *h, sqlparser_patch_t *p, size_t n, const char *label)
{
    sqlparser_patch_list_t list = {p, n}; sqlparser_error_t e = {0}; unsigned long g = h->generation;
    phase = label; CHECK(result(sqlparser_apply_patch(h, &list, &e), &e));
    CHECK(h->generation == g + (n != 0)); patches++; checkpoint(h, label);
}
static void name_selector(sqlparser_handle_t *h, size_t s, const char *owner, const char *value, char *out)
{ snprintf(out, 80, "stmt[%zu].name[%zu]", s, find_name(h, s, owner, value, 0)); }
static void clause_selector(sqlparser_handle_t *h, sqlparser_clause_kind_t kind, char *out)
{
    sqlparser_error_t e = {0}; size_t n, i;
    CHECK(result(sqlparser_statement_clause_count(h, 0, &n, &e), &e));
    for (i = 0; i < n; i++) {
        sqlparser_clause_view_t v; CHECK(result(sqlparser_statement_clause(h, 0, i, &v, &e), &e));
        if (v.kind == kind) { snprintf(out, 80, "stmt[0].clause[%zu]", i); return; }
    }
    CHECK(0);
}
static const char *quoted_alias(int original)
{
    int mysql = dialect == SQLPARSER_DIALECT_MYSQL || dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL || dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL;
    int server = dialect == SQLPARSER_DIALECT_SQLSERVER || dialect == SQLPARSER_DIALECT_VASTBASE_SQLSERVER || dialect == SQLPARSER_DIALECT_KINGBASE_SQLSERVER;
    return original ? (mysql ? "`old_alias`" : server ? "[old_alias]" : "\"old_alias\"") :
        (mysql ? "`Quoted Alias`" : server ? "[Quoted Alias]" : "\"Quoted Alias\"");
}
static void dialect_cases(void)
{
    for (dialect = 0; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        sqlparser_handle_t *h, *clone = NULL; sqlparser_error_t e = {0};
        sqlparser_patch_t p[3] = {{0}}; char a[80], b[80], c[80];
        sqlparser_name_view_t borrowed; sqlparser_query_graph_view_t graph;
        size_t alias, expressions; unsigned long g; char *owned_sql = NULL, *saved_sql;
        case_name = "thirteen-dialects-mixed-lifecycle"; phase = "parse";
        h = parse("/* head */ SELECT s.a AS old_alias, s.b AS keep_alias FROM sch.tab s WHERE s.a = 1 ORDER BY s.b; /* tail */");
        alias = find_name(h, 0, "ResTarget", "old_alias", 0);
        CHECK(admitted(h) == (dialect == SQLPARSER_DIALECT_POSTGRESQL));
        checkpoint(h, "initial");
        CHECK(result(sqlparser_statement_query_graph(h, 0, &graph, &e), &e)); g = h->generation;
        rename_at(h, 0, alias, "renamed_alias", "scalar-alias"); CHECK(h->generation == g + 1);
        CHECK(sqlparser_query_graph_expression_count(&graph, &expressions, &e) == SQLPARSER_STATUS_INVALID_ARGUMENT);
        rename_value(h, 0, "RangeVar", "tab", "other_table", "scalar-qualified-table");
        rename_value(h, 0, "RangeVar", "sch", "other_schema", "scalar-qualified-schema");
        name_selector(h, 0, "ResTarget", "renamed_alias", a);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.sql=quoted_alias(0)};
        apply(h, p, 1, "quoted-spelling");
        rename_value(h, 0, "ResTarget", "Quoted Alias", "café_列", "utf8-semantic");
        rename_value(h, 0, "ResTarget", "café_列", "old_alias", "undo-alias");
        name_selector(h, 0, "ResTarget", "old_alias", a);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.sql=quoted_alias(1)};
        apply(h, p, 1, "spelling-only");
        p[0].sql = NULL; p[0].default_sql = "old_alias"; apply(h, p, 1, "spelling-undo");
        /* Borrowed input is passed only while its current owner is live. */
        alias = find_name(h, 0, "ResTarget", "keep_alias", 0);
        CHECK(result(sqlparser_statement_name(h, 0, alias, &borrowed, &e), &e));
        rename_value(h, 0, "ResTarget", "old_alias", borrowed.value, "borrowed-direct");
        name_selector(h, 0, "ResTarget", "keep_alias", a);
        snprintf(b, sizeof(b), "stmt[0].name[%zu]", find_name(h, 0, "ResTarget", "keep_alias", 1));
        CHECK(result(sqlparser_statement_name(h, 0, find_name(h, 0, "ResTarget", "keep_alias", 0), &borrowed, &e), &e));
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="batch_alias"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=b,.default_sql=borrowed.value};
        apply(h, p, 2, "borrowed-batch-duplicate");
        clause_selector(h, SQLPARSER_CLAUSE_KIND_ORDER_BY, c);
        name_selector(h, 0, "ResTarget", "batch_alias", a);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="before_order"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=c,.sql="s.b DESC, s.a"};
        p[2] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="after_order"};
        apply(h, p, 3, "names-order-names-one-batch");
        rename_value(h, 0, "ColumnRef", "b", "order_changed", "rename-before-owner-removal");
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_targets[0]",.sql="s.c AS fresh_alias, s.d AS another_alias"};
        apply(h, p, 1, "replace-select-list");
        rename_value(h, 0, "ResTarget", "fresh_alias", "after_select", "name-after-select");
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].where[0]",.sql="s.e = 2 AND s.f = 3"};
        apply(h, p, 1, "replace-where");
        rename_value(h, 0, "ColumnRef", "e", "where_changed", "name-after-where");
        CHECK(result(sqlparser_deparse(h, &owned_sql, &e), &e)); saved_sql = strdup(owned_sql); CHECK(saved_sql);
        CHECK(result(sqlparser_handle_clone(h, &clone, &e), &e) && clone); clones++;
        CHECK(!clone->ast_state_bound);
        for (alias = 0; alias < clone->identifier_mutation_count; alias++) CHECK(!clone->identifier_mutations[alias].slot && !clone->identifier_mutations[alias].value);
        /* Call old setter on a cold clone without first forcing a read. */
        alias = find_name(h, 0, "ResTarget", NULL, 0);
        sqlparser_handle_destroy(h); h = NULL;
        rename_at(clone, 0, alias, "clone_alias", "cold-clone-after-source-destroy");
        CHECK(!strcmp(owned_sql, saved_sql)); free(saved_sql); sqlparser_string_free(owned_sql);
        apply(clone, NULL, 0, "empty-batch"); sqlparser_handle_destroy(clone);

        case_name = "thirteen-dialects-update-source-selector"; phase = "parse";
        h = parse("/* update */ UPDATE sch.tab SET a = old_col + 1, b = keep_col + 2 WHERE id = 7");
        rename_value(h, 0, "ResTarget", "a", "first_col", "update-name-before");
        rename_value(h, 0, "ColumnRef", "old_col", "changed_col", "update-expression-before");
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE_ASSIGNMENT,.selector="stmt[0].assignment[0]",.sql="fresh_col = incoming_col + 9"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_ASSIGNMENT,.selector="stmt[0].assignment[2]",.name="copy_col",.source_selector="stmt[0].assignment[0]"};
        apply(h, p, 2, "replace-and-copy-update-assignment");
        rename_value(h, 0, "ResTarget", "copy_col", "renamed_copy", "name-after-source-selector");
        clause_selector(h, SQLPARSER_CLAUSE_KIND_SET_LIST, a);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.sql="final_col = final_value + 3, last_col = 4"};
        apply(h, p, 1, "replace-update-list");
        rename_value(h, 0, "ResTarget", "final_col", "after_update", "name-after-update-list");
        sqlparser_handle_destroy(h);
    }
}

/* Direct internal scalar setter checks precede any patch finalization. */
static void internal_name(sqlparser_handle_t *h, size_t stmt, size_t index,
    const char *value, const char *spelling, const char *label)
{
    sqlparser_error_t e = {0}; phase = label;
    CHECK(result(sqlparser_statement_set_name_spelling(h, stmt, index, value, spelling, &e), &e));
    setters++; checkpoint(h, label);
}
static void internal_cases(void)
{
    for (dialect = 0; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        sqlparser_handle_t *h; sqlparser_error_t e = {0}; size_t index, order; char selector[80];
        case_name = "internal-immediate-mixed-commits"; phase = "parse";
        h = parse("SELECT a AS old_alias FROM sch.tab WHERE b = 1 ORDER BY c");
        index = find_name(h, 0, "ResTarget", "old_alias", 0);
        internal_name(h, 0, index, "direct_alias", NULL, "internal-scalar");
        CHECK(h->identifier_mutation_count == 1);
        internal_name(h, 0, index, "old_alias", quoted_alias(1), "internal-spelling-only");
        internal_name(h, 0, index, "old_alias", NULL, "internal-same-value-no-op");
        internal_name(h, 0, index, "temporary_alias", NULL, "internal-before-undo");
        internal_name(h, 0, index, "old_alias", NULL, "internal-undo");
        CHECK(h->identifier_mutation_count == 0);
        index = find_name(h, 0, "RangeVar", "tab", 0);
        internal_name(h, 0, index, "retained_table", NULL, "internal-qualified-table");
        index = find_name(h, 0, "ColumnRef", "c", 0);
        internal_name(h, 0, index, "removed_order", NULL, "internal-order-before-removal");
        clause_selector(h, SQLPARSER_CLAUSE_KIND_ORDER_BY, selector);
        CHECK(sscanf(selector, "stmt[0].clause[%zu]", &order) == 1);
        phase = "internal-replace-order";
        CHECK(result(sqlparser_statement_set_clause_sql_in_place(h, 0, order, "new_order DESC, another_order", &e), &e));
        checkpoint(h, phase); CHECK(h->identifier_mutation_count == 1);
        internal_name(h, 0, find_name(h, 0, "ColumnRef", "new_order", 0), "kept_order", NULL, "internal-name-after-order");
        internal_name(h, 0, find_name(h, 0, "ResTarget", "old_alias", 0), "removed_alias", NULL, "internal-alias-before-removal");
        phase = "internal-replace-select";
        CHECK(result(sqlparser_select_set_targets_sql_in_place(h, 0, 0, "next_col AS next_alias, second_col", &e), &e));
        checkpoint(h, phase);
        internal_name(h, 0, find_name(h, 0, "ResTarget", "next_alias", 0), "retained_alias", NULL, "internal-name-after-select");
        internal_name(h, 0, find_name(h, 0, "ColumnRef", "b", 0), "removed_where", NULL, "internal-where-before-removal");
        phase = "internal-replace-where";
        CHECK(result(sqlparser_statement_set_where_sql_in_place(h, 0, 0, "new_where = 2", &e), &e)); checkpoint(h, phase);
        internal_name(h, 0, find_name(h, 0, "ColumnRef", "new_where", 0), "after_where", NULL, "internal-name-after-where");
        sqlparser_handle_destroy(h);
        h = parse("UPDATE sch.tab SET a = old_value, b = keep_value WHERE id = 1");
        internal_name(h, 0, find_name(h, 0, "RangeVar", "tab", 0), "retained_table", NULL, "internal-update-relation");
        internal_name(h, 0, find_name(h, 0, "ResTarget", "a", 0), "removed_assignment", NULL, "internal-update-before-removal");
        phase = "internal-update-replace";
        CHECK(result(sqlparser_update_set_assignments_sql(h, 0, 0, "fresh_col = fresh_value, another_col = 2", &e), &e)); checkpoint(h, phase);
        internal_name(h, 0, find_name(h, 0, "ResTarget", "fresh_col", 0), "after_assignment", NULL, "internal-update-after-removal");
        sqlparser_handle_destroy(h);
    }
    {
        sqlparser_handle_t *h; sqlparser_dialect_ops_t copy; const sqlparser_dialect_ops_t *original;
        dialect = SQLPARSER_DIALECT_POSTGRESQL; case_name = "copied-canonical-ops-fallback"; phase = "parse";
        h = parse("SELECT a AS old_alias FROM t");
        (void)find_name(h, 0, "ResTarget", "old_alias", 0);
        original = h->dialect_ops; copy = *original; CHECK(!copy.prepare_ast_state);
        h->dialect_ops = &copy; CHECK(!admitted(h));
        internal_name(h, 0, find_name(h, 0, "ResTarget", "old_alias", 0), "copied_ops_alias", NULL, "copied-ops-internal-name");
        h->dialect_ops = original; sqlparser_handle_destroy(h);
    }
}
static void reset_reproduction(void)
{
    sqlparser_handle_t *h; sqlparser_patch_t p; sqlparser_error_t e = {0}; char *out = NULL;
    dialect = SQLPARSER_DIALECT_POSTGRESQL; case_name = "public-alias-reset-reproduction"; phase = "parse";
    h = parse("SELECT a AS old_alias FROM t WHERE b = 1");
    p = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_targets[0]",.sql="c AS fresh_alias"};
    apply(h, &p, 1, "install-select-list");
    rename_value(h, 0, "ResTarget", "fresh_alias", "after_select", "public-rename-alias");
    CHECK(result(sqlparser_deparse(h, &out, &e), &e));
    if (dump) printf("ALIAS_RESET_BEFORE_WHERE|%s\n", out);
    sqlparser_string_free(out); out = NULL;
    p = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].where[0]",.sql="e = 2"};
    apply(h, &p, 1, "replace-only-where");
    CHECK(result(sqlparser_deparse(h, &out, &e), &e));
    if (dump) printf("ALIAS_RESET_REPRO|%s\n", out);
    sqlparser_string_free(out); sqlparser_handle_destroy(h);
}
static void scaling_cases(void)
{
    static const size_t sizes[] = {1, 2, 4, 16, 64}; size_t k, reverse, duplicate, cte;
    dialect = SQLPARSER_DIALECT_POSTGRESQL;
    for (cte = 0; cte < 2; cte++) for (duplicate = 0; duplicate < 2; duplicate++)
    for (reverse = 0; reverse < 2; reverse++) for (k = 0; k < COUNT(sizes); k++) {
        size_t n = sizes[k], i; char label[100], selector[64][80], values[64][32];
        buffer_t sql = {0}; sqlparser_handle_t *h; sqlparser_patch_t p[64]; sqlparser_error_t e = {0};
        snprintf(label, sizeof(label), "scale-%s-%s-%s-%zu", cte ? "cte" : "select", duplicate ? "duplicate" : "unique", reverse ? "reverse" : "forward", n);
        case_name = label; phase = "parse";
        if (cte) append(&sql, "WITH outer_cte AS (");
        append(&sql, "SELECT ");
        for (i = 0; i < n; i++) append(&sql, "%s1 AS %s%zu", i ? ", " : "", "name_", duplicate ? 0 : i);
        if (cte) append(&sql, ") SELECT * FROM outer_cte");
        h = parse(sql.p); free(sql.p); memset(&sql, 0, sizeof(sql));
        for (i = 0; i < n; i++) {
            size_t j = reverse ? n - 1 - i : i; sqlparser_name_view_t v;
            /* ResTarget ordinal is located by owner, preserving duplicates. */
            size_t index = find_name(h, 0, "ResTarget", NULL, j);
            CHECK(result(sqlparser_statement_name(h, 0, index, &v, &e), &e));
            snprintf(values[i], sizeof(values[i]), "changed_%zu", j);
            internal_name(h, 0, index, values[i], NULL, "internal-sequential-rename");
        }
        sqlparser_handle_destroy(h);
        /* Same M writes inside one batch, exercising pending AST_DIRTY state. */
        if (cte) append(&sql, "WITH outer_cte AS (");
        append(&sql, "SELECT ");
        for (i = 0; i < n; i++) append(&sql, "%s1 AS name_%zu", i ? ", " : "", duplicate ? 0 : i);
        if (cte) append(&sql, ") SELECT * FROM outer_cte");
        h = parse(sql.p); free(sql.p); memset(&sql, 0, sizeof(sql));
        for (i = 0; i < n; i++) {
            size_t j = reverse ? n - 1 - i : i, index = find_name(h, 0, "ResTarget", NULL, j);
            snprintf(selector[i], sizeof(selector[i]), "stmt[0].name[%zu]", index);
            p[i] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=selector[i],.default_sql=values[i]};
        }
        apply(h, p, n, "batch-rename"); sqlparser_handle_destroy(h);
    }
}

/* Output-limited commits cannot run the full successful-render snapshot. Their
 * immediate tuple checks still run before the intentionally failing deparse. */
static void limited_binding(sqlparser_handle_t *h)
{
    sqlparser_error_t e = {0}; size_t n = h->identifier_mutation_count, i;
    sqlparser_identifier_mutation_t *v = malloc((n + 1) * sizeof(*v)); CHECK(v && !h->failed);
    if (n) memcpy(v, h->identifier_mutations, n * sizeof(*v));
    CHECK(sqlparser_handle_rebind_identifier_mutations(h, &e) == SQLPARSER_STATUS_OK && h->identifier_mutation_count == n);
    for (i = 0; i < n; i++) {
        sqlparser_identifier_mutation_t *m = h->identifier_mutations + i;
        CHECK(v[i].raw_statement_index == m->raw_statement_index && v[i].string_index == m->string_index);
        CHECK(v[i].slot == m->slot && v[i].value == m->value && m->slot && m->value && *m->slot == m->value);
        mutation_entries++;
    }
    free(v);
}
static void controls_and_limits(void)
{
    sqlparser_handle_t *h; sqlparser_error_t e = {0}; sqlparser_parse_options_t o;
    sqlparser_patch_t p[2] = {{0}}; char a[80], huge[180]; char *out = NULL; size_t i;
    dialect = SQLPARSER_DIALECT_POSTGRESQL; case_name = "small-controls"; phase = "parse";
    h = parse("SELECT 1"); CHECK(result(sqlparser_handle_ensure_ast(h, &e), &e)); checkpoint(h, "zero-names"); controls++;
    sqlparser_handle_destroy(h);
    h = parse("SELECT a AS same_alias FROM t");
    i = find_name(h, 0, "ResTarget", "same_alias", 0);
    { unsigned long g = h->generation; rename_at(h, 0, i, "same_alias", "public-no-op"); CHECK(g + 1 == h->generation); }
    { unsigned long g = h->generation;
      CHECK(sqlparser_statement_set_name(h, 0, i, "", &e) == SQLPARSER_STATUS_INVALID_ARGUMENT);
      if (dump) printf("INVALID|empty-name|%d|%lu|%lu\n", h->failed, g, h->generation);
      controls++; }
    sqlparser_handle_destroy(h);
    case_name = "multiple-statements-binds";
    h = parse("SELECT $1 AS first_alias FROM first_table WHERE a = $2; SELECT $3 AS second_alias FROM second_table");
    rename_value(h, 1, "ResTarget", "second_alias", "second_changed", "second-statement");
    rename_value(h, 0, "ResTarget", "first_alias", "first_changed", "first-statement");
    name_selector(h, 1, "RangeVar", "second_table", a);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].where[0]",.sql="new_col = $4"};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="second_new"};
    apply(h, p, 2, "multi-statement-mixed-binds"); sqlparser_handle_destroy(h);

    case_name = "limits-and-terminal-failure"; memset(huge, 'x', sizeof(huge) - 1); huge[sizeof(huge) - 1] = 0;
    sqlparser_parse_options_default(&o); o.limits.max_sql_bytes = 80;
    h = parse_options("SELECT a AS old_alias FROM t", &o); i = find_name(h, 0, "ResTarget", "old_alias", 0);
    CHECK(result(sqlparser_statement_set_name(h, 0, i, huge, &e), &e));
    checkpoint(h, "semantic-name-with-sql-input-limit");
    if (dump) printf("LIMIT|input-name|%d|%s\n", h->failed, e.message);
    controls++; sqlparser_handle_destroy(h);
    sqlparser_parse_options_default(&o); o.limits.max_output_bytes = 80;
    h = parse_options("SELECT a AS old_alias FROM t", &o);
    name_selector(h, 0, "ResTarget", "old_alias", a);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql=huge};
    { sqlparser_patch_list_t list = {p, 1}; sqlparser_status_t s = sqlparser_apply_patch(h, &list, &e);
      CHECK(s == SQLPARSER_STATUS_OK); limited_binding(h); s = sqlparser_deparse(h, &out, &e);
      if (dump) printf("TERMINAL|output-limit|%d|%d|%s\n", s, h->failed, e.message);
      CHECK(s == SQLPARSER_STATUS_RESOURCE_LIMIT && h->failed && !out); failures++; }
    /* After a terminal error, destruction only: no reads or attempted reuse. */
    sqlparser_handle_destroy(h);
    h = parse("SELECT a AS old_alias FROM t"); name_selector(h, 0, "ResTarget", "old_alias", a);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="successful_first"};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].relation[0]",.sql="a + b"};
    { sqlparser_patch_list_t list = {p, 2}; sqlparser_status_t s = sqlparser_apply_patch(h, &list, &e);
      if (dump) printf("TERMINAL|invalid-second-patch|%d|%d|%s\n", s, h->failed, e.message);
      CHECK(s != SQLPARSER_STATUS_OK && h->failed); failures++; }
    sqlparser_handle_destroy(h);
    /* Exact ordinary deparse limit with no attempted read after failure. */
    sqlparser_parse_options_default(&o); o.limits.max_output_bytes = strlen("SELECT a AS new_alias FROM t");
    h = parse_options("SELECT a AS old_alias FROM t", &o); i = find_name(h, 0, "ResTarget", "old_alias", 0);
    CHECK(result(sqlparser_statement_set_name(h, 0, i, "new_alias", &e), &e));
    limited_binding(h);
    CHECK(result(sqlparser_deparse(h, &out, &e), &e)); CHECK(strlen(out) == o.limits.max_output_bytes);
    if (dump) printf("EXACTLIMIT|%s\n", out);
    controls++; sqlparser_string_free(out); sqlparser_handle_destroy(h);
}
static void fixture_cases(const char *dir)
{
    static const char *files[] = {"sql_batch_input.json", "mysql_dialect_input.json", "oracle_dialect_input.json", "sqlserver_dialect_input.json", "dameng_dialect_input.json", "vastbase_oracle_dialect_input.json", "vastbase_mysql_dialect_input.json", "vastbase_postgresql_dialect_input.json", "vastbase_sqlserver_dialect_input.json", "kingbase_oracle_dialect_input.json", "kingbase_mysql_dialect_input.json", "kingbase_postgresql_dialect_input.json", "kingbase_sqlserver_dialect_input.json"};
    size_t f;
    for (f = 0; f < COUNT(files); f++) {
        char path[4096]; json_error_t je; json_t *root, *items, *item; size_t j, picked = 0;
        snprintf(path, sizeof(path), "%s/%s", dir, files[f]); root = json_load_file(path, 0, &je); CHECK(root);
        items = json_object_get(root, "items"); CHECK(json_is_array(items)); dialect = (sqlparser_dialect_t)f;
        json_array_foreach(items, j, item) {
            const char *sql = json_string_value(json_object_get(item, "sql"));
            const char *declared = json_string_value(json_object_get(item, "dialect"));
            sqlparser_handle_t *h; sqlparser_error_t e = {0}; size_t n, i, selected = SIZE_MAX; char *original = NULL;
            if (!sql || strncmp(sql, "SELECT ", 7) || (declared && strcmp(declared, sqlparser_dialect_name(dialect)))) continue;
            case_name = json_string_value(json_object_get(item, "name")); CHECK(case_name); phase = "fixture-parse";
            h = parse(sql); CHECK(result(sqlparser_statement_name_count(h, 0, &n, &e), &e));
            for (i = 0; i < n; i++) {
                sqlparser_name_view_t v; CHECK(result(sqlparser_statement_name(h, 0, i, &v, &e), &e));
                if (!strcmp(v.owner_type, "ResTarget") || (!strcmp(v.owner_type, "RangeVar") && !strcmp(v.field_name, "relname"))) {
                    selected = i; original = strdup(v.value); CHECK(original); break;
                }
            }
            if (selected != SIZE_MAX) {
                rename_at(h, 0, selected, "m26_fixture_name", "fixture-rename");
                rename_at(h, 0, selected, original, "fixture-undo"); picked++; fixtures++;
            }
            free(original); sqlparser_handle_destroy(h); if (picked == 6) break;
        }
        CHECK(picked == 6); json_decref(root);
    }
}
int main(int argc, char **argv)
{
    const char *dir = NULL; int i;
    for (i = 1; i < argc; i++) if (!strcmp(argv[i], "--dump")) dump = 1; else dir = argv[i];
    dialect_cases(); internal_cases(); reset_reproduction(); scaling_cases(); controls_and_limits(); if (dir) fixture_cases(dir);
    fprintf(stderr, "PASS checkpoints=%zu mutation_entries=%zu setters=%zu patches=%zu clones=%zu fixtures=%zu admitted_states=%zu fallback_states=%zu terminal_failures=%zu controls=%zu\n", checkpoints, mutation_entries, setters, patches, clones, fixtures, admitted_states, fallback_states, failures, controls);
    return 0;
}
