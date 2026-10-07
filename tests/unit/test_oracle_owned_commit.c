/* Oracle-owned branch String->String commit contract.
 * The expected SQL is assembled independently, then freshly parsed without
 * applying patches. --record is byte-compared to this same public caller
 * linked against an immutable pre-commit library; no production bypass exists.
 * Default fixtures are portable and general in branch/column count. Optional:
 * --fixture SQL --golden-dir DIR verifies the existing 5000-row raw/typed
 * pipeline goldens (oracle_raw.sql, oracle_typed.sql, and family equivalents).
 * --alloc runs the implementation-owned allocation-failure ledger on apply.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "sqlparser_internal.h"
#include "sqlparser_identifier_origin_internal.h"
#include "sqlparser_test_failure.h"
#include "../../src/core/sqlparser_ast_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"
#include "../../src/dialect/sqlparser_dialect_oracle_internal.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static sqlparser_error_t error;
static const char *stage = "init";
static size_t case_number, reparse_calls, commit_entries, commit_handled;
static int count_active, recording;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d case=%zu stage=%s: %s: %s\n", __FILE__, __LINE__, case_number, stage, #x, error.message); abort(); } } while (0)

#ifdef SQLPARSER_ORACLE_COMMIT_WRAPPERS
sqlparser_status_t __real_sqlparser_handle_reparse_destructive(sqlparser_handle_t *, char **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_handle_reparse_destructive(sqlparser_handle_t *h, char **sql, sqlparser_error_t *e)
{
    if (count_active) ++reparse_calls;
    return __real_sqlparser_handle_reparse_destructive(h, sql, e);
}
#ifndef SQLPARSER_ORACLE_COMMIT_BASELINE
/* Observational only: call through unchanged. No certificate override. */
sqlparser_status_t __real_sqlparser_oracle_try_commit_multi_insert_strings(sqlparser_handle_t *, const sqlparser_surface_source_edits_t *, char **, int *, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_oracle_try_commit_multi_insert_strings(sqlparser_handle_t *h, const sqlparser_surface_source_edits_t *edits, char **sql, int *handled, sqlparser_error_t *e)
{
    sqlparser_status_t status;
    if (count_active) ++commit_entries;
    status = __real_sqlparser_oracle_try_commit_multi_insert_strings(h, edits, sql, handled, e);
    if (count_active && status == SQLPARSER_STATUS_OK && *handled) ++commit_handled;
    return status;
}
#endif
/* Context-local PostgreSQL OOM can longjmp/abort in the unchanged parser.
 * Sweep all implementation-owned allocations, excluding only that native context. */
static int allocation_active;
static unsigned native_depth;
static size_t allocation_calls, allocation_fail, allocation_live, ledger_end;
static void *ledger[32768];
struct MemoryContextData;
struct MemoryContextData *__real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(struct MemoryContextData *);
struct MemoryContextData *__wrap_pg_query_enter_memory_context(void)
{ ++native_depth; return __real_pg_query_enter_memory_context(); }
void __wrap_pg_query_exit_memory_context(struct MemoryContextData *c)
{ __real_pg_query_exit_memory_context(c); CHECK(native_depth); --native_depth; }
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static size_t slot(void *p)
{ size_t i; if (p && allocation_live) for(i=0U;i<ledger_end;i++) if(ledger[i]==p) return i; return COUNT(ledger); }
static void track(void *p)
{ size_t i; if(!p)return; for(i=0U;i<ledger_end;i++) if(!ledger[i])break; CHECK(i<COUNT(ledger));ledger[i]=p;++allocation_live;if(i==ledger_end)++ledger_end; }
static int reject_allocation(void)
{ return allocation_active && !native_depth && ++allocation_calls == allocation_fail; }
void *__wrap_malloc(size_t n)
{void *p;if(reject_allocation())return NULL;p=__real_malloc(n);if(allocation_active&&!native_depth)track(p);return p;}
void *__wrap_calloc(size_t n,size_t s)
{void *p;if(reject_allocation())return NULL;p=__real_calloc(n,s);if(allocation_active&&!native_depth)track(p);return p;}
void *__wrap_realloc(void *p,size_t n)
{size_t i=slot(p);void *q;if(reject_allocation())return NULL;q=__real_realloc(p,n);if(q||!n){if(i<COUNT(ledger)){ledger[i]=q;if(!q)--allocation_live;}else if(allocation_active&&!native_depth)track(q);}return q;}
void __wrap_free(void *p)
{size_t i=slot(p);if(i<COUNT(ledger)){ledger[i]=NULL;--allocation_live;}__real_free(p);}

#endif

typedef struct { char *data; size_t length, capacity; } text_buffer;
static text_buffer *record_sink;
static void append_bytes(text_buffer *b, const char *s, size_t n)
{
    size_t required = b->length + n + 1U, capacity;
    CHECK(required > b->length);
    if (required > b->capacity) {
        char *p;
        capacity = b->capacity ? b->capacity : 256U;
        while (capacity < required) { CHECK(capacity <= (size_t)-1 / 2U); capacity *= 2U; }
        p = realloc(b->data, capacity); CHECK(p); b->data = p; b->capacity = capacity;
    }
    if (n) memcpy(b->data + b->length, s, n);
    b->length += n; b->data[b->length] = '\0';
}
static void append(text_buffer *b, const char *s) { CHECK(s); append_bytes(b, s, strlen(s)); }
static void append_format(text_buffer *b, const char *format, ...)
{
    char small[256]; int n; va_list ap;
    va_start(ap, format); n = vsnprintf(small, sizeof(small), format, ap); va_end(ap);
    CHECK(n >= 0 && (size_t)n < sizeof(small)); append_bytes(b, small, (size_t)n);
}
static void record_number(unsigned long long n)
{ CHECK(record_sink); append_format(record_sink, "%llu;", n); }
static void record_text(const char *s)
{
    CHECK(record_sink);
    if (!s) append(record_sink, "NULL;");
    else { append_format(record_sink, "%zu:", strlen(s)); append(record_sink, s); append(record_sink, ";"); }
}
#include "sqlparser_oracle_graph_records.h"
static void record_status(sqlparser_status_t s)
{record_number((unsigned long long)s);if(s!=SQLPARSER_STATUS_OK)record_text(error.message);}
static void record_selector(sqlparser_handle_t *h, const sqlparser_selector_t *s)
{
    char *text=NULL;sqlparser_selector_t parsed;
    (void)h;
    CHECK(sqlparser_selector_format(s,&text,&error)==SQLPARSER_STATUS_OK);
    record_text(text);CHECK(sqlparser_selector_parse(text,&parsed,&error)==SQLPARSER_STATUS_OK);
    CHECK(parsed.kind==s->kind&&parsed.statement_index==s->statement_index&&parsed.item_index==s->item_index&&parsed.row_index==s->row_index&&parsed.column_index==s->column_index);
    record_sqlparser_selector_t(&parsed);free(text);
}
static void record_span(const sqlparser_query_graph_view_t *g,sqlparser_index_span_t s)
{
    size_t i,n=0;record_sqlparser_index_span_t(&s);
    for(i=0U;i<s.count;i++){CHECK(sqlparser_query_graph_span_index_at(g,s,i,&n,&error)==SQLPARSER_STATUS_OK);record_number(n);}
    CHECK(sqlparser_query_graph_span_index_at(g,s,s.count,&n,&error)==SQLPARSER_STATUS_INVALID_ARGUMENT);
}
#define SELECTOR(v,m) do{if((v).has_##m)record_selector(h,&(v).m);}while(0)
#define SPAN(v,m) record_span(g,(v).m)
#define AT(type,name,idx,v) do{memset(&(v),0,sizeof(v));CHECK(sqlparser_query_graph_##name##_at(g,idx,&(v),&error)==SQLPARSER_STATUS_OK);record_##type(&(v));}while(0)
static void record_column(sqlparser_handle_t *h,const sqlparser_query_graph_view_t *g,size_t n)
{sqlparser_graph_dml_column_t v;AT(sqlparser_graph_dml_column_t,dml_column,n,v);SELECTOR(v,selector);}
static void record_cell(sqlparser_handle_t *h,const sqlparser_query_graph_view_t *g,size_t n)
{
    sqlparser_graph_dml_cell_t v;AT(sqlparser_graph_dml_cell_t,dml_cell,n,v);SELECTOR(v,selector);
    if(v.has_source_target){sqlparser_graph_target_t t;AT(sqlparser_graph_target_t,target,v.source_target_index,t);}
    if(v.has_source_field){sqlparser_graph_field_t f;AT(sqlparser_graph_field_t,field,v.source_field_index,f);}
    /* INSERT_CELL is not a generic LITERAL selector. Its complete embedded
     * literal is serialized above; do not assume unsupported selector APIs. */
}
static void record_graph(sqlparser_handle_t *h,const sqlparser_query_graph_view_t *g)
{
    size_t i,j,n,index,count;int has;char *json=NULL;
    sqlparser_bind_occurrence_view_t binds;
    { sqlparser_query_graph_view_t normalized = *g; normalized.generation = 0UL; record_sqlparser_query_graph_view_t(&normalized); }
    for(i=0U;i<g->block_count;i++){sqlparser_graph_block_t v;AT(sqlparser_graph_block_t,block,i,v);SPAN(v,relations);SPAN(v,targets);SPAN(v,predicates);}
    for(i=0U;i<g->relation_count;i++){sqlparser_graph_relation_t v;AT(sqlparser_graph_relation_t,relation,i,v);SELECTOR(v,selector);}
    for(i=0U;i<g->target_count;i++){sqlparser_graph_target_t v;AT(sqlparser_graph_target_t,target,i,v);SPAN(v,star_relations);SELECTOR(v,selector);SELECTOR(v,target_list_selector);}
    for(i=0U;i<g->field_count;i++){sqlparser_graph_field_t v;AT(sqlparser_graph_field_t,field,i,v);SPAN(v,candidate_relations);SELECTOR(v,selector);}
    for(i=0U;i<g->value_count;i++){sqlparser_graph_value_t v;AT(sqlparser_graph_value_t,value,i,v);SELECTOR(v,selector);}
    CHECK(sqlparser_query_graph_expression_count(g,&count,&error)==SQLPARSER_STATUS_OK);record_number(count);
    for(i=0U;i<count;i++){sqlparser_graph_expression_t v;AT(sqlparser_graph_expression_t,expression,i,v);SPAN(v,arguments);SELECTOR(v,selector);SELECTOR(v,argument_list_selector);}
    CHECK(sqlparser_query_graph_expression_argument_count(g,&count,&error)==SQLPARSER_STATUS_OK);record_number(count);
    for(i=0U;i<count;i++){sqlparser_graph_expression_argument_t v;AT(sqlparser_graph_expression_argument_t,expression_argument,i,v);SELECTOR(v,selector);}
    for(i=0U;i<g->set_count;i++){sqlparser_graph_set_t v;AT(sqlparser_graph_set_t,set,i,v);SPAN(v,branch_blocks);}
    for(i=0U;i<g->predicate_count;i++){sqlparser_graph_predicate_t v;AT(sqlparser_graph_predicate_t,predicate,i,v);SPAN(v,children);n=0;has=0;CHECK(sqlparser_query_graph_predicate_right_expression(g,i,&n,&has,&error)==SQLPARSER_STATUS_OK);record_number(n);record_number((unsigned)has);}
    {sqlparser_graph_session_t v;sqlparser_status_t s=sqlparser_query_graph_session(g,&v,&error);record_status(s);if(s==SQLPARSER_STATUS_OK){record_sqlparser_graph_session_t(&v);for(i=0;i<v.item_count;i++){sqlparser_graph_session_item_t it;AT(sqlparser_graph_session_item_t,session_item,i,it);for(j=0;j<it.value_count;j++){sqlparser_graph_session_value_t val;AT(sqlparser_graph_session_value_t,session_value,it.value_offset+j,val);}}}}
    CHECK(sqlparser_query_graph_dml_count(g,&count,&error)==SQLPARSER_STATUS_OK);record_number(count);
    {sqlparser_graph_dml_t v;sqlparser_status_t s=sqlparser_query_graph_dml(g,&v,&error);record_status(s);if(s==SQLPARSER_STATUS_OK)record_sqlparser_graph_dml_t(&v);}
    for(i=0U;i<count;i++){
        sqlparser_graph_dml_t v;AT(sqlparser_graph_dml_t,dml,i,v);
        SPAN(v,target_columns);SPAN(v,rows);SPAN(v,assignments);SPAN(v,delete_targets);SPAN(v,branches);
        for(j=0U;j<v.target_columns.count;j++){CHECK(sqlparser_query_graph_span_index_at(g,v.target_columns,j,&index,&error)==SQLPARSER_STATUS_OK);record_column(h,g,index);}
        for(j=0U;j<v.rows.count;j++){CHECK(sqlparser_query_graph_span_index_at(g,v.rows,j,&index,&error)==SQLPARSER_STATUS_OK);record_cell(h,g,index);}
        for(j=0U;j<v.assignments.count;j++){sqlparser_graph_dml_assignment_t a;CHECK(sqlparser_query_graph_span_index_at(g,v.assignments,j,&index,&error)==SQLPARSER_STATUS_OK);AT(sqlparser_graph_dml_assignment_t,dml_assignment,index,a);SPAN(a,rhs_fields);SPAN(a,rhs_values);SPAN(a,rhs_blocks);SELECTOR(a,selector);}
        n=0;has=0;CHECK(sqlparser_query_graph_dml_parent(g,i,&n,&has,&error)==SQLPARSER_STATUS_OK);record_number(n);record_number((unsigned)has);
        CHECK(sqlparser_query_graph_dml_result_count(g,i,&n,&error)==SQLPARSER_STATUS_OK);record_number(n);
        for(j=0U;j<n;j++){size_t k;sqlparser_graph_dml_result_t r;CHECK(sqlparser_query_graph_dml_result_at(g,i,j,&r,&error)==SQLPARSER_STATUS_OK);record_sqlparser_graph_dml_result_t(&r);SPAN(r,sink_columns);SPAN(r,references);for(k=0U;k<r.references.count;k++){sqlparser_graph_dml_reference_t ref;CHECK(sqlparser_query_graph_span_index_at(g,r.references,k,&index,&error)==SQLPARSER_STATUS_OK);AT(sqlparser_graph_dml_reference_t,dml_reference,index,ref);}}
    }
    for(i=0U;i<g->dml_branch_count;i++){
        sqlparser_graph_dml_branch_t b;sqlparser_graph_merge_action_kind_t action;sqlparser_graph_merge_match_kind_t match;sqlparser_index_span_t assignments;
        AT(sqlparser_graph_dml_branch_t,dml_branch,i,b);SPAN(b,target_columns);SPAN(b,rows);SELECTOR(b,condition_selector);SELECTOR(b,delete_condition_selector);
        for(j=0U;j<b.target_columns.count;j++){CHECK(sqlparser_query_graph_span_index_at(g,b.target_columns,j,&index,&error)==SQLPARSER_STATUS_OK);record_column(h,g,index);}
        for(j=0U;j<b.rows.count;j++){CHECK(sqlparser_query_graph_span_index_at(g,b.rows,j,&index,&error)==SQLPARSER_STATUS_OK);record_cell(h,g,index);}
        record_status(sqlparser_query_graph_merge_branch_detail(g,i,&action,&match,&assignments,&error));
    }
    CHECK(sqlparser_handle_bind_occurrences(h,&binds,&error)==SQLPARSER_STATUS_OK);record_number(binds.count);
    for(i=0U;i<binds.count;i++){sqlparser_bind_occurrence_t v;CHECK(sqlparser_bind_occurrence_at(&binds,i,&v,&error)==SQLPARSER_STATUS_OK);record_sqlparser_bind_occurrence_t(&v);}
    CHECK(sqlparser_export_view_json(h,0,&json,&error)==SQLPARSER_STATUS_OK);record_text(json);free(json);

}
static char *copy_text(const char *s)
{ char *p = sqlparser_strdup(s); CHECK(!s || p); return p; }
static void same_text(const char *a, const char *b)
{
    if ((!a != !b) || (a && strcmp(a, b))) {
        fprintf(stderr, "actual=%.500s\nexpected=%.500s\n", a ? a : "NULL", b ? b : "NULL");
        CHECK(0);
    }
}
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb"); long n; char *p;
    CHECK(f && fseek(f, 0L, SEEK_END) == 0); n = ftell(f);
    CHECK(n >= 0 && fseek(f, 0L, SEEK_SET) == 0);
    p = malloc((size_t)n + 1U); CHECK(p);
    CHECK(fread(p, 1U, (size_t)n, f) == (size_t)n); p[n] = '\0'; CHECK(fclose(f) == 0); return p;
}
static sqlparser_handle_t *parse(sqlparser_dialect_t dialect, const char *sql)
{
    sqlparser_parse_options_t options; sqlparser_handle_t *h = NULL;
    char *owned = copy_text(sql); sqlparser_status_t status;
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    options.limits.max_sql_bytes = 32U * 1024U * 1024U;
    options.limits.max_output_bytes = 64U * 1024U * 1024U;
    status = sqlparser_parse_with_options(owned, &options, &h, &error);
    memset(owned, 'x', strlen(owned)); free(owned);
    CHECK(status == SQLPARSER_STATUS_OK && h); return h;
}
static const sqlparser_dialect_multi_insert_t *multi(sqlparser_handle_t *h)
{
    const sqlparser_dialect_multi_insert_t *m = sqlparser_oracle_state_multi_insert(h->dialect_state);
    CHECK(m); return m;
}
static void record_state(sqlparser_handle_t *h)
{
    const sqlparser_dialect_multi_insert_t *m = multi(h); size_t i, j, count;
    record_text(h->sql); record_text(h->parser_sql); record_number(m->mode); record_number(m->branch_count);
    record_text(m->source_public_sql); record_text(m->source_parser_sql);
    for (i = 0U; i < m->branch_count; i++) {
        const sqlparser_dialect_multi_insert_branch_t *b = &m->branches[i];
        record_number(b->ordinal); record_number(b->has_condition); record_number(b->is_else);
        record_number(b->condition_group_id); record_text(b->condition_public_sql); record_text(b->condition_parser_sql);
        record_text(b->relation.database_name); record_text(b->relation.schema_name); record_text(b->relation.table_name);
        record_text(b->relation.link_name); record_text(b->relation.link_sql); record_text(b->relation.sql);
        record_number(b->column_count); record_number(b->cell_count);
        for (j = 0U; j < b->column_count; j++) { record_text(b->columns[j].name); record_text(b->columns[j].sql); }
        for (j = 0U; j < b->cell_count; j++) {
            const sqlparser_dialect_multi_insert_value_t *v = &b->cells[j];
            char *cell = NULL;
            record_text(v->public_sql); record_text(v->parser_sql); record_number(v->has_bind);
            record_number(v->bind_kind); record_text(v->bind); record_text(v->bind_sql);
            record_number(v->bind_position); record_number(v->has_bind_position); record_number(v->has_literal);
            record_sqlparser_literal_view_t(&v->literal);
            record_text(v->literal_string_value); record_text(v->literal_float_value);
            CHECK(sqlparser_insert_cell_sql(h, 0U, i, j, &cell, &error) == SQLPARSER_STATUS_OK);
            record_text(cell); free(cell);
        }
    }
    CHECK(sqlparser_statement_literal_count(h, 0U, &count, &error) == SQLPARSER_STATUS_OK);
    record_number(count);
    for (i = 0U; i < count; i++) {
        sqlparser_literal_view_t v;
        CHECK(sqlparser_statement_literal(h, 0U, i, &v, &error) == SQLPARSER_STATUS_OK);
        record_number(i); record_sqlparser_literal_view_t(&v);
    }
}
/* Reuse the every-span origin test against a separately preprocessed map.
 * Both reference maps are tied to the expected edited source, never old bytes. */
static void compare_origins(sqlparser_handle_t *h, sqlparser_handle_t *reference)
{
    const sqlparser_identifier_origin_map_t *actual, *fresh;
    sqlparser_identifier_origin_map_t *independent = NULL;
    char *parser_sql = NULL; void *state = NULL; size_t offset, length;
    CHECK(sqlparser_identifier_origins_for_handle(h, &actual, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_identifier_origins_for_handle(reference, &fresh, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_dialect_preprocess_identifier_origins(reference->dialect, reference->sql,
        &reference->limits, &parser_sql, &state, &independent, &error) == SQLPARSER_STATUS_OK);
    same_text(h->parser_sql, parser_sql);
    CHECK(sqlparser_identifier_origin_map_output_length(actual) == sqlparser_identifier_origin_map_output_length(independent));
    CHECK(sqlparser_identifier_origin_map_output_length(fresh) == sqlparser_identifier_origin_map_output_length(independent));
    for (offset = 0U; offset <= h->parser_sql_len + 1U; offset++) {
        for (length = 0U; length <= h->parser_sql_len + 1U - offset; length++) {
            sqlparser_identifier_origin_t a, b, c;
            sqlparser_identifier_origin_kind_t ka = sqlparser_identifier_origin_map_lookup(actual, offset, length, &a);
            sqlparser_identifier_origin_kind_t kb = sqlparser_identifier_origin_map_lookup(fresh, offset, length, &b);
            sqlparser_identifier_origin_kind_t kc = sqlparser_identifier_origin_map_lookup(independent, offset, length, &c);
            CHECK(ka == kb && kb == kc && a.kind == b.kind && b.kind == c.kind);
            CHECK(a.source_offset == b.source_offset && b.source_offset == c.source_offset);
            CHECK(a.source_length == b.source_length && b.source_length == c.source_length);
        }
    }
    if (state && h->dialect_ops->destroy_state) h->dialect_ops->destroy_state(state);
    sqlparser_identifier_origin_map_destroy(independent); free(parser_sql);
}
static char *graph_record(sqlparser_handle_t *h, int state)
{
    text_buffer b = {0}; sqlparser_query_graph_view_t g; size_t count, i;
    CHECK(!record_sink); record_sink = &b;
    CHECK(sqlparser_statement_query_graph(h, 0U, &g, &error) == SQLPARSER_STATUS_OK);
    record_graph(h, &g);
    CHECK(sqlparser_statement_literal_count(h, 0U, &count, &error) == SQLPARSER_STATUS_OK);
    record_number(count);
    for (i = 0U; i < count; i++) {
        sqlparser_literal_view_t v;
        CHECK(sqlparser_statement_literal(h, 0U, i, &v, &error) == SQLPARSER_STATUS_OK);
        record_number(i); record_sqlparser_literal_view_t(&v);
    }
    if (state) record_state(h);
    record_sink = NULL; return b.data;
}
static void emit(const char *label, sqlparser_handle_t *h, const char *output)
{
    char *record;
    if (!recording) return;
    record = graph_record(h, 0);
    printf("case=%zu;label=%s;dialect=%s;generation=%lu;output=%zu:%s;graph=%s\n",
        case_number, label, sqlparser_dialect_name(h->dialect), h->generation, strlen(output), output, record);
    free(record);
}
/* Compare to the expected source, not parse(actual), not a second patch route. */
static char *verify(sqlparser_handle_t *h, const char *expected_sql, int state)
{
    sqlparser_handle_t *reference = parse(h->dialect, expected_sql);
    char *actual = NULL, *expected = NULL, *a, *b;
    CHECK(sqlparser_deparse(h, &actual, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(reference, &expected, &error) == SQLPARSER_STATUS_OK);
    same_text(actual, expected);
    a = graph_record(h, state); b = graph_record(reference, state);
    same_text(a, b); free(a); free(b);
    if (state) compare_origins(h, reference);
    emit(stage, h, actual);
    free(expected); sqlparser_handle_destroy(reference); return actual;
}
typedef struct {
    char *parser_sql, *source_public, *source_parser, *wire;
    char *parser_address; void *state_address;
    char *wire_address; size_t wire_length;
} retained_source;
static retained_source retain(sqlparser_handle_t *h)
{
    const sqlparser_dialect_multi_insert_t *m = multi(h); retained_source r;
    memset(&r, 0, sizeof(r));
    r.parser_sql = copy_text(h->parser_sql); r.source_public = copy_text(m->source_public_sql);
    r.source_parser = copy_text(m->source_parser_sql); r.wire_length = h->parse_tree.len;
    r.wire = malloc(r.wire_length ? r.wire_length : 1U); CHECK(r.wire);
    if (r.wire_length) memcpy(r.wire, h->parse_tree.data, r.wire_length);
    r.parser_address = h->parser_sql;
    r.state_address = h->dialect_state; r.wire_address = h->parse_tree.data; return r;
}
static void verify_retained(sqlparser_handle_t *h, retained_source *r)
{
    const sqlparser_dialect_multi_insert_t *m = multi(h);
    same_text(h->parser_sql, r->parser_sql); same_text(m->source_public_sql, r->source_public);
    same_text(m->source_parser_sql, r->source_parser);
    CHECK(h->parse_tree.len == r->wire_length);
    CHECK(!r->wire_length || !memcmp(h->parse_tree.data, r->wire, r->wire_length));
#ifndef SQLPARSER_ORACLE_COMMIT_BASELINE
    /* Public nonempty apply deliberately clears the AST after committing. */
    CHECK(h->parser_sql == r->parser_address && h->ast == NULL);
    CHECK(h->dialect_state == r->state_address && h->parse_tree.data == r->wire_address);
#endif
}
static void release_retained(retained_source *r)
{ free(r->parser_sql); free(r->source_public); free(r->source_parser); free(r->wire); memset(r, 0, sizeof(*r)); }
static sqlparser_status_t counted_apply(sqlparser_handle_t *h, const sqlparser_patch_t *items, size_t count, sqlparser_error_t *e)
{
    sqlparser_patch_list_t list = {items, count}; sqlparser_status_t s;
    reparse_calls = commit_entries = commit_handled = 0U; count_active = 1; s = sqlparser_apply_patch(h, &list, e); count_active = 0; return s;
}
/* admitted=1 is an explicit producer contract, never inferred from timing. */
static void verify_route(int admitted)
{
#ifdef SQLPARSER_ORACLE_COMMIT_WRAPPERS
#ifdef SQLPARSER_ORACLE_COMMIT_BASELINE
    (void)admitted; CHECK(reparse_calls > 0U);
#else
    if (admitted && reparse_calls) fprintf(stderr, "route reparse=%zu commit_entries=%zu commit_handled=%zu\n", reparse_calls, commit_entries, commit_handled);
    if (admitted) { CHECK(reparse_calls == 0U); CHECK(commit_entries > 0U && commit_entries == commit_handled); }
    else CHECK(reparse_calls > 0U);
#endif
#else
    (void)admitted;
#endif
}
static void stale_graph(const sqlparser_query_graph_view_t *g)
{
    sqlparser_graph_dml_t dml; sqlparser_graph_dml_cell_t cell;
    CHECK(sqlparser_query_graph_dml(g, &dml, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
    CHECK(sqlparser_query_graph_dml_cell_at(g, 0U, &cell, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
}
static char *quoted(const char *value)
{
    text_buffer b = {0}; const char *p;
    append(&b, "'");
    for (p = value; *p; p++) { append_bytes(&b, p, 1U); if (*p == '\'') append(&b, "'"); }
    append(&b, "'"); return b.data;
}
/* Generated model owns decoded values and renders them independently. */
typedef struct { size_t branches, columns, strings; char **values; } fixture_model;
static fixture_model make_model(size_t branches, size_t columns)
{
    fixture_model m; size_t b, c;
    m.branches = branches; m.columns = columns; m.strings = columns >= 3U ? columns - 2U : columns;
    m.values = calloc(branches * columns, sizeof(*m.values)); CHECK(m.values);
    for (b = 0U; b < branches; b++) for (c = 0U; c < m.strings; c++) {
        char text[128]; CHECK(snprintf(text, sizeof(text), "old-%zu-%zu-张三-'Ω'", b, c) > 0);
        m.values[b * columns + c] = copy_text(text);
    }
    return m;
}
static void set_model(fixture_model *m, size_t b, size_t c, const char *value)
{
    CHECK(b < m->branches && c < m->strings);
    free(m->values[b * m->columns + c]); m->values[b * m->columns + c] = copy_text(value);
}
static char *render_model(const fixture_model *m)
{
    text_buffer out = {0}; size_t b, c;
    append(&out, "/* branch-owned test */ INSERT ALL");
    for (b = 0U; b < m->branches; b++) {
        append_format(&out, " INTO App.Target%zu (", b % 3U);
        for (c = 0U; c < m->columns; c++) append_format(&out, "%sC%zu", c ? ", " : "", c);
        append(&out, ") VALUES (");
        for (c = 0U; c < m->columns; c++) {
            if (c) append(&out, ", ");
            if (c < m->strings) { char *q = quoted(m->values[b * m->columns + c]); append(&out, q); free(q); }
            else if (c == m->columns - 1U) append(&out, "CURRENT_TIMESTAMP");
            else append_format(&out, "%zu", b + 1U);
        }
        append(&out, ")");
    }
    append(&out, " SELECT S.C AS SourceAlias, 'source-Ω' AS SourceText FROM SourceTable S WHERE S.C = 7;");
    return out.data;
}
static void free_model(fixture_model *m)
{
    size_t i; for (i = 0U; i < m->branches * m->columns; i++) free(m->values[i]); free(m->values);
}
static sqlparser_graph_dml_cell_t branch_cell(const sqlparser_query_graph_view_t *g, size_t row, size_t column)
{
    sqlparser_graph_dml_t dml; sqlparser_graph_dml_branch_t branch; sqlparser_graph_dml_cell_t cell;
    size_t index;
    CHECK(sqlparser_query_graph_dml(g, &dml, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_span_index_at(g, dml.branches, row, &index, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_branch_at(g, index, &branch, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_span_index_at(g, branch.rows, column, &index, &error) == SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml_cell_at(g, index, &cell, &error) == SQLPARSER_STATUS_OK);
    CHECK(cell.has_selector && cell.column_ordinal == column); return cell;
}
static char *cell_selector(const sqlparser_query_graph_view_t *g, size_t b, size_t c)
{
    sqlparser_graph_dml_cell_t cell = branch_cell(g, b, c); char *selector = NULL;
    CHECK(sqlparser_selector_format(&cell.selector, &selector, &error) == SQLPARSER_STATUS_OK); return selector;
}
static void generated_rounds(sqlparser_dialect_t dialect, size_t branches, size_t columns)
{
    static const char *changes[] = {"", "x", "larger-'escaped'-汉字-Ω-abcdefghijklmnopqrstuvwxyz", "''", "same"};
    fixture_model model = make_model(branches, columns);
    char *input = render_model(&model), *outputs[5] = {0}, *copies[5] = {0};
    sqlparser_handle_t *h = parse(dialect, input); size_t round, i, count = branches * model.strings;
    ++case_number; stage = "general string batch";
    for (round = 0U; round < 5U; round++) {
        sqlparser_query_graph_view_t old, fresh; sqlparser_graph_dml_t dml;
        sqlparser_patch_t *p = calloc(count + 2U, sizeof(*p));
        sqlparser_literal_value_t *lit = calloc(count + 2U, sizeof(*lit));
        char **values = calloc(count + 2U, sizeof(*values));
        size_t *order = malloc(count * sizeof(*order)); unsigned long generation = h->generation;
        retained_source retained; char *expected; size_t n = count;
        CHECK(p && lit && values && order);
        CHECK(sqlparser_statement_query_graph(h, 0U, &old, &error) == SQLPARSER_STATUS_OK);
        retained = retain(h);
        for (i = 0U; i < count; i++) order[i] = i;
        if (round == 1U) for (i = 0U; i < count / 2U; i++) { size_t t = order[i]; order[i] = order[count - 1U - i]; order[count - 1U - i] = t; }
        if (round == 2U) { unsigned long random = 0x31f2UL; for (i = count; i > 1U; i--) { size_t k, t; random = random * 1664525UL + 1013904223UL; k = random % i; t = order[i - 1U]; order[i - 1U] = order[k]; order[k] = t; } }
        for (i = 0U; i < count; i++) {
            size_t row = order[i] / model.strings, column = order[i] % model.strings;
            const char *value = round == 4U ? model.values[row * columns + column] : changes[(round + i) % COUNT(changes)];
            values[i] = round % 2U ? quoted(value) : copy_text(value);
            p[i].op = SQLPARSER_PATCH_REPLACE; p[i].selector = cell_selector(&old, row, column);
            if (round % 2U) p[i].sql = values[i];
            else { lit[i].kind = SQLPARSER_LITERAL_KIND_STRING; lit[i].string_value = values[i]; p[i].literal = &lit[i]; }
            if (round != 4U) set_model(&model, row, column, value);
        }
        if (round == 3U) {
            /* Ordered duplicate modifies then restores the same target. */
            p[n].op = p[n + 1U].op = SQLPARSER_PATCH_REPLACE;
            p[n].selector = cell_selector(&old, 0U, 0U); p[n + 1U].selector = cell_selector(&old, 0U, 0U);
            values[n] = quoted("temporary-'value'"); values[n + 1U] = quoted(model.values[0]);
            p[n].sql = values[n]; p[n + 1U].sql = values[n + 1U]; n += 2U;
        }
        stage = round == 4U ? "nonempty no-op" : round == 3U ? "ordered duplicates" : "general string batch";
        CHECK(counted_apply(h, p, n, &error) == SQLPARSER_STATUS_OK);
        verify_route(1); CHECK(h->generation == generation + 1UL); stale_graph(&old);
        verify_retained(h, &retained); release_retained(&retained);
        /* Free and overwrite all caller patch data before any output access. */
        for (i = 0U; i < n; i++) { free((char *)p[i].selector); memset(values[i], 'x', strlen(values[i])); free(values[i]); }
        free(p); free(lit); free(values); free(order);
        expected = render_model(&model); outputs[round] = verify(h, expected, 1); copies[round] = copy_text(outputs[round]); free(expected);
        CHECK(sqlparser_statement_query_graph(h, 0U, &fresh, &error) == SQLPARSER_STATUS_OK);
        CHECK(fresh.generation == generation + 1UL);
        CHECK(counted_apply(h, NULL, 0U, &error) == SQLPARSER_STATUS_OK && reparse_calls == 0U);
        CHECK(h->generation == generation + 1UL && sqlparser_query_graph_dml(&fresh, &dml, &error) == SQLPARSER_STATUS_OK);
        for (i = 0U; i <= round; i++) same_text(outputs[i], copies[i]);
        if (round == 2U) {
            sqlparser_handle_t *clone = NULL;
            stage = "clone after original destruction";
            CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK); sqlparser_handle_destroy(h); h = clone;
            expected = render_model(&model); { char *out = verify(h, expected, 1); free(out); }
            sqlparser_handle_clear_query_graph(h);
            sqlparser_identifier_origin_map_destroy(h->identifier_origins); h->identifier_origins = NULL;
            { char *out = verify(h, expected, 1); free(out); } free(expected);
        }
    }
    sqlparser_handle_destroy(h);
    for (i = 0U; i < COUNT(outputs); i++) { same_text(outputs[i], copies[i]); free(outputs[i]); free(copies[i]); }
    free(input); free_model(&model);
}
static void borrowed_inputs(sqlparser_dialect_t dialect)
{
    const char *input = "INSERT ALL INTO T (A, B, C, D) VALUES ('borrowed-''Ω''', 'stmt[0].insert_cell[0][2]', 'old', '''raw Ω''') SELECT 1 FROM Dual";
    const char *expected = "INSERT ALL INTO T (A, B, C, D) VALUES ('changed', 'stmt[0].insert_cell[0][2]', 'borrowed-''Ω''', 'raw Ω') SELECT 1 FROM Dual";
    sqlparser_handle_t *h = parse(dialect, input); sqlparser_query_graph_view_t g;
    sqlparser_graph_dml_cell_t value, selector, raw; sqlparser_literal_value_t v = {0}; sqlparser_patch_t p[3] = {{0}};
    char *out; retained_source retained;
    ++case_number; stage = "graph-borrowed inputs";
    CHECK(sqlparser_statement_query_graph(h, 0U, &g, &error) == SQLPARSER_STATUS_OK);
    retained = retain(h); value = branch_cell(&g, 0U, 0U); selector = branch_cell(&g, 0U, 1U); raw = branch_cell(&g, 0U, 3U);
    CHECK(value.literal.kind == SQLPARSER_LITERAL_KIND_STRING && selector.literal.kind == SQLPARSER_LITERAL_KIND_STRING && raw.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
    p[0].op = p[1].op = p[2].op = SQLPARSER_PATCH_REPLACE;
    p[0].selector = "stmt[0].insert_cell[0][0]"; p[0].sql = "'changed'";
    v.kind = SQLPARSER_LITERAL_KIND_STRING; v.string_value = value.literal.string_value;
    p[1].selector = selector.literal.string_value; p[1].literal = &v;
    p[2].selector = "stmt[0].insert_cell[0][3]"; p[2].sql = raw.literal.string_value;
    CHECK(counted_apply(h, p, COUNT(p), &error) == SQLPARSER_STATUS_OK); verify_route(1); stale_graph(&g);
    verify_retained(h, &retained); release_retained(&retained);
    out = verify(h, expected, 1); sqlparser_handle_destroy(h); same_text(out, expected); free(out);
}
static void apply_expected(sqlparser_handle_t *h, const sqlparser_patch_t *p, size_t n, const char *expected, int route)
{
    char *out; sqlparser_query_graph_view_t g; unsigned long generation = h->generation;
    CHECK(sqlparser_statement_query_graph(h, 0U, &g, &error) == SQLPARSER_STATUS_OK);
    CHECK(counted_apply(h, p, n, &error) == SQLPARSER_STATUS_OK);
    if (route >= 0) verify_route(route);
    CHECK(h->generation == generation + 1UL); stale_graph(&g); out = verify(h, expected, 0); free(out);
}
static void following_generic_edits(sqlparser_dialect_t dialect)
{
    sqlparser_handle_t *h;
    sqlparser_patch_t p[3] = {{0}};
    ++case_number; stage = "string then structural then expression";
    h = parse(dialect, "INSERT ALL INTO T (A, B) VALUES ('old', 'other') INTO U (A) VALUES ('second') SELECT S.C AS AliasName FROM SourceTable S");
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'first'"};
    apply_expected(h, p, 1U, "INSERT ALL INTO T (A, B) VALUES ('first', 'other') INTO U (A) VALUES ('second') SELECT S.C AS AliasName FROM SourceTable S", 1);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN, .selector="stmt[0].insert_branch_columns[0]", .index=1U, .name="Added", .default_sql="'new'"};
    apply_expected(h, p, 1U, "INSERT ALL INTO T (A, Added, B) VALUES ('first', 'new', 'other') INTO U (A) VALUES ('second') SELECT S.C AS AliasName FROM SourceTable S", -1);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .sql="upper('mixed')"};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[1][0]", .sql="'mixed string'"};
    apply_expected(h, p, 2U, "INSERT ALL INTO T (A, Added, B) VALUES ('first', upper('mixed'), 'other') INTO U (A) VALUES ('mixed string') SELECT S.C AS AliasName FROM SourceTable S", -1);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].select_target[0][0]", .sql="S.Other AS NewAlias"};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'after source'"};
    apply_expected(h, p, 2U, "INSERT ALL INTO T (A, Added, B) VALUES ('after source', upper('mixed'), 'other') INTO U (A) VALUES ('mixed string') SELECT S.Other AS NewAlias FROM SourceTable S", -1);
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[1][0]", .sql="'final Ω'"};
    apply_expected(h, p, 1U, "INSERT ALL INTO T (A, Added, B) VALUES ('after source', upper('mixed'), 'other') INTO U (A) VALUES ('final Ω') SELECT S.Other AS NewAlias FROM SourceTable S", -1);
    {
        sqlparser_query_graph_view_t graph; size_t index; char *selector = NULL;
        stage = "source identifier after owned and generic commits";
        CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
        for (index = 0U; index < graph.relation_count; index++) {
            sqlparser_graph_relation_t relation;
            CHECK(sqlparser_query_graph_relation_at(&graph, index, &relation, &error) == SQLPARSER_STATUS_OK);
            if (relation.object_name && !strcmp(relation.object_name, "SourceTable")) {
                CHECK(relation.has_selector && !selector);
                CHECK(sqlparser_selector_format(&relation.selector, &selector, &error) == SQLPARSER_STATUS_OK);
            }
        }
        CHECK(selector);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector=selector, .sql="OtherSource"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'after identifier'"};
        apply_expected(h, p, 2U, "INSERT ALL INTO T (A, Added, B) VALUES ('after identifier', upper('mixed'), 'other') INTO U (A) VALUES ('final Ω') SELECT S.Other AS NewAlias FROM OtherSource S", -1);
        free(selector);
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][2]", .sql="'final other'"};
        apply_expected(h, p, 1U, "INSERT ALL INTO T (A, Added, B) VALUES ('after identifier', upper('mixed'), 'final other') INTO U (A) VALUES ('final Ω') SELECT S.Other AS NewAlias FROM OtherSource S", -1);
    }
    sqlparser_handle_destroy(h);
    ++case_number; stage = "source-selector dependency";
    h = parse(dialect, "INSERT ALL INTO T (A, B) VALUES ('old', 'other') SELECT 1 FROM Dual");
    p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'changed'"};
    p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][1]", .source_selector="stmt[0].insert_cell[0][0]"};
    apply_expected(h, p, 2U, "INSERT ALL INTO T (A, B) VALUES ('changed', 'changed') SELECT 1 FROM Dual", -1);
    sqlparser_handle_destroy(h);
}
static void trailing_comment_fallback(sqlparser_dialect_t dialect)
{
    fixture_model model = make_model(1U, 1U); text_buffer input = {0}, expected = {0};
    char *body = render_model(&model); sqlparser_handle_t *h; sqlparser_patch_t p = {0};
    ++case_number; stage = "trailing-comment planner fallback";
    append(&input, body); append(&input, " /* tail */"); free(body);
    set_model(&model, 0U, 0U, "replacement"); body = render_model(&model);
    append(&expected, body); append(&expected, " /* tail */"); free(body);
    h = parse(dialect, input.data);
    p = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'replacement'"};
    apply_expected(h, &p, 1U, expected.data, 0);
#ifndef SQLPARSER_ORACLE_COMMIT_BASELINE
    CHECK(commit_entries == 0U && commit_handled == 0U);
#endif
    sqlparser_handle_destroy(h); free(input.data); free(expected.data); free_model(&model);
}
/* These unusual fragments are accepted by the unchanged whole-statement path.
 * Retain only its status/output contract, never use borrowed decoded literal
 * text as an oracle and never introduce a permissive decoder for the test. */
static void unusual_raw_status(sqlparser_dialect_t dialect)
{
    static const char *raw[] = {"'x'; SELECT 1", "'x' +"}; size_t i;
    for (i = 0U; i < COUNT(raw); i++) {
        sqlparser_handle_t *h = parse(dialect, "INSERT ALL INTO T (A, B) VALUES ('old', 'other') SELECT 1 FROM Dual");
        sqlparser_patch_t p = {.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql=raw[i]};
        sqlparser_status_t status; char *out = NULL;
        ++case_number; stage = "unusual raw generic status";
        status = counted_apply(h, &p, 1U, &error);
        if (recording) printf("case=%zu;raw=%zu;dialect=%s;apply=%d;error=%s;\n", case_number, i, sqlparser_dialect_name(dialect), status, status == SQLPARSER_STATUS_OK ? "" : error.message);
        CHECK(status == SQLPARSER_STATUS_OK);
#ifndef SQLPARSER_ORACLE_COMMIT_BASELINE
        CHECK(commit_handled == 0U);
#endif
        status = sqlparser_deparse(h, &out, &error);
        if (recording) printf("deparse=%d;output=%s;error=%s;\n", status, out ? out : "NULL", status == SQLPARSER_STATUS_OK ? "" : error.message);
        if (status == SQLPARSER_STATUS_OK) {
            sqlparser_query_graph_view_t g;
            status = sqlparser_statement_query_graph(h, 0U, &g, &error);
            if (recording) printf("graph=%d;error=%s;\n", status, status == SQLPARSER_STATUS_OK ? "" : error.message);
            if (status == SQLPARSER_STATUS_OK) emit(stage, h, out);
        }
        free(out); sqlparser_handle_destroy(h);
    }
}
static void generic_boundaries(sqlparser_dialect_t dialect)
{
    static const struct { const char *before, *after, *source, *mode; } cases[] = {
        {"q'[old-Ω]'", "'ordinary'", "SELECT 1 FROM Dual", "ALL"},
        {"'old'", "q'{new-Ω}'", "SELECT 1 FROM Dual", "ALL"},
        {"'old'", "'changed'", "SELECT :BindName AS C FROM Dual", "ALL"},
        {":BranchBind", "'changed'", "SELECT 1 FROM Dual", "ALL"},
        {"'old'", "'line\ntext'", "SELECT 1 FROM Dual", "ALL"},
        {"'old'", "'slash\\text'", "SELECT 1 FROM Dual", "ALL"},
        {"'old'", "upper('text')", "SELECT 1 FROM Dual", "ALL"},
        {"'old'", "'changed'", "SELECT 1 FROM Dual", "FIRST WHEN 1 = 1 THEN"},
        {"'old'", "'changed'", "SELECT 1 FROM Dual", "ALL WHEN 1 = 1 THEN"},
        {"'old'", "'changed'", "SELECT S.C FROM SourceTable@RemoteLink S", "ALL"},
        {"'old'", "'changed'", "SELECT 1 FROM Dual MINUS SELECT 2 FROM Dual", "ALL"}
    };
    size_t i;
    for (i = 0U; i < COUNT(cases); i++) {
        text_buffer input = {0}, expected = {0}; sqlparser_handle_t *h; sqlparser_patch_t p = {0};
        ++case_number; stage = "generic admission boundary";
        append_format(&input, "INSERT %s INTO T (A) VALUES (%s) ", cases[i].mode, cases[i].before); append(&input, cases[i].source);
        append_format(&expected, "INSERT %s INTO T (A) VALUES (%s) ", cases[i].mode, cases[i].after); append(&expected, cases[i].source);
        h = parse(dialect, input.data);
        {
            const sqlparser_identifier_origin_map_t *origins;
            sqlparser_query_graph_view_t graph; sqlparser_handle_t *clone = NULL; char *before;
            CHECK(sqlparser_statement_query_graph(h, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_identifier_origins_for_handle(h, &origins, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
            sqlparser_handle_destroy(h); h = clone;
            before = verify(h, input.data, 0); free(before);
            sqlparser_handle_clear_query_graph(h);
            sqlparser_identifier_origin_map_destroy(h->identifier_origins); h->identifier_origins = NULL;
            before = verify(h, input.data, 0); free(before);
        }
        p.op = SQLPARSER_PATCH_REPLACE; p.selector = "stmt[0].insert_cell[0][0]"; p.sql = cases[i].after;
        apply_expected(h, &p, 1U, expected.data, -1);
        sqlparser_handle_destroy(h); free(input.data); free(expected.data);
    }
}
/* Preserve the existing national-owner error rather than widening this change
 * into a repair. Same caller/status/output transcript is checked at baseline. */
static void national_boundary(sqlparser_dialect_t dialect)
{
    static const char *inputs[] = {
        "INSERT ALL INTO T (A) VALUES (N'old') SELECT 1 FROM Dual",
        "INSERT ALL INTO T (A) VALUES ('old') SELECT N'source' AS C FROM Dual"
    };
    size_t i;
    for (i = 0U; i < COUNT(inputs); i++) {
        sqlparser_handle_t *h = parse(dialect, inputs[i]); sqlparser_patch_t p = {0}; sqlparser_status_t status; char *out = NULL;
        ++case_number; stage = "national generic status";
        {
            const sqlparser_identifier_origin_map_t *origins; sqlparser_handle_t *clone = NULL;
            CHECK(sqlparser_identifier_origins_for_handle(h, &origins, &error) == SQLPARSER_STATUS_OK);
            CHECK(sqlparser_handle_clone(h, &clone, &error) == SQLPARSER_STATUS_OK);
            sqlparser_handle_destroy(h); h = clone;
            CHECK(sqlparser_identifier_origins_for_handle(h, &origins, &error) == SQLPARSER_STATUS_OK);
        }
        p.op = SQLPARSER_PATCH_REPLACE; p.selector = "stmt[0].insert_cell[0][0]"; p.sql = "'replacement'";
        status = counted_apply(h, &p, 1U, &error);
        if (recording) printf("case=%zu;national=%zu;dialect=%s;apply=%d;error=%s;\n", case_number, i, sqlparser_dialect_name(dialect), status, status == SQLPARSER_STATUS_OK ? "" : error.message);
        if (status == SQLPARSER_STATUS_OK) {
            status = sqlparser_deparse(h, &out, &error);
            if (recording) printf("deparse=%d;output=%s;error=%s;\n", status, out ? out : "NULL", status == SQLPARSER_STATUS_OK ? "" : error.message);
            if (status == SQLPARSER_STATUS_OK) {
                sqlparser_query_graph_view_t g;
                status = sqlparser_statement_query_graph(h, 0U, &g, &error);
                if (recording) printf("graph=%d;error=%s;\n", status, status == SQLPARSER_STATUS_OK ? "" : error.message);
                if (status == SQLPARSER_STATUS_OK) emit(stage, h, out);
            }
        }
        if (status != SQLPARSER_STATUS_OK) CHECK(!strcmp(error.message, "national literal AST owner is missing"));
        free(out); sqlparser_handle_destroy(h);
    }
}
static void error_boundaries(sqlparser_dialect_t dialect)
{
    const char *input = "INSERT ALL INTO T (A, B) VALUES ('old', 'other') SELECT 1 FROM Dual";
    static const char *invalid[] = {"'unterminated", "((", "bad(", "("};
    size_t i, position; int null_error;
    for (i = 0U; i < COUNT(invalid) + 3U; i++) for (position = 0U; position < 3U; position++) for (null_error = 0; null_error < 2; null_error++) {
        sqlparser_handle_t *h = parse(dialect, input); sqlparser_query_graph_view_t g;
        sqlparser_patch_t p[3] = {{0}}; sqlparser_status_t status; size_t j; char large[256];
        sqlparser_status_t wanted = i < COUNT(invalid) ? SQLPARSER_STATUS_PARSE_ERROR :
            i == COUNT(invalid) ? SQLPARSER_STATUS_INVALID_ARGUMENT : SQLPARSER_STATUS_RESOURCE_LIMIT;
        ++case_number; stage = "invalid order and limits";
        CHECK(sqlparser_statement_query_graph(h, 0U, &g, &error) == SQLPARSER_STATUS_OK);
        for (j = 0U; j < COUNT(p); j++) p[j] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'valid'"};
        if (i < COUNT(invalid)) p[position].sql = invalid[i];
        else if (i == COUNT(invalid)) p[position].selector = "stmt[99].insert_cell[0][0]";
        else if (i == COUNT(invalid) + 1U) {
            memset(large, 'x', sizeof(large)); large[0] = large[sizeof(large)-2U] = '\''; large[sizeof(large)-1U] = '\0';
            h->limits.max_sql_bytes = 128U; p[position].sql = large;
        } else { h->limits.max_output_bytes = 12U; }
        status = counted_apply(h, p, COUNT(p), null_error ? NULL : &error);
        if (status != wanted) fprintf(stderr, "error case kind=%zu position=%zu null=%d got=%d expected=%d\n", i, position, null_error, status, wanted);
        CHECK(status == wanted);
        if (recording && !null_error) printf("case=%zu;error-kind=%zu;position=%zu;status=%d;message=%s;\n", case_number, i, position, status, error.message);
        if (!null_error) CHECK(error.code == wanted);
        stale_graph(&g); CHECK(sqlparser_test_failed_handle(h)); sqlparser_handle_destroy(h);
    }
    /* Selector parsing precedes raw parsing within an item; prior invalid raw
     * fragments must not be hidden by later invalid selectors or repairs. */
    for (i = 0U; i < 2U; i++) {
        sqlparser_handle_t *h = parse(dialect, input); sqlparser_patch_t p[2] = {{0}}; sqlparser_status_t status;
        ++case_number; stage = "first error precedence";
        p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'unterminated"};
        p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[99].insert_cell[0][0]", .sql="'valid'"};
        if (i) { sqlparser_patch_t swap = p[0]; p[0] = p[1]; p[1] = swap; }
        status = counted_apply(h, p, 2U, &error);
        CHECK(status == (i ? SQLPARSER_STATUS_INVALID_ARGUMENT : SQLPARSER_STATUS_PARSE_ERROR));
        if (recording) printf("case=%zu;precedence=%zu;status=%d;message=%s;\n", case_number, i, status, error.message);
        CHECK(sqlparser_test_failed_handle(h)); sqlparser_handle_destroy(h);
    }
}
static void allocation_sweep(sqlparser_dialect_t dialect)
{
#ifdef SQLPARSER_ORACLE_COMMIT_WRAPPERS
    const char *input = "INSERT ALL INTO T (A, B) VALUES ('old', 'other') INTO U (A) VALUES ('second') SELECT C FROM SourceTable";
    const char *expected = "INSERT ALL INTO T (A, B) VALUES ('long-''Ω''-replacement', 'other') INTO U (A) VALUES ('') SELECT C FROM SourceTable";
    size_t fail; int null_error, warm;
    for (warm = 0; warm < 2; warm++) for (null_error = 0; null_error < 2; null_error++) {
        int complete = 0;
        for (fail = 1U; fail < 32768U; fail++) {
            sqlparser_handle_t *h = parse(dialect, input); sqlparser_patch_t p[2] = {{0}};
            sqlparser_query_graph_view_t g; sqlparser_status_t status; size_t calls; char *out;
            stage = "allocation failure apply ledger";
            p[0] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[0][0]", .sql="'long-''Ω''-replacement'"};
            p[1] = (sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE, .selector="stmt[0].insert_cell[1][0]", .sql="''"};
            if (warm) { CHECK(counted_apply(h, p, 2U, &error) == SQLPARSER_STATUS_OK); }
            CHECK(sqlparser_statement_query_graph(h, 0U, &g, &error) == SQLPARSER_STATUS_OK);
            CHECK(!allocation_live && !native_depth); allocation_fail = fail; allocation_calls = 0U; allocation_active = 1;
            status = counted_apply(h, p, 2U, null_error ? NULL : &error);
            allocation_active = 0; calls = allocation_calls;
            if (status == SQLPARSER_STATUS_OK) { out = verify(h, expected, 1); free(out); }
            else CHECK(sqlparser_test_failed_handle(h));
            sqlparser_handle_destroy(h);
            if (allocation_live || native_depth) fprintf(stderr, "allocation ledger dialect=%s warm=%d null=%d fail=%zu calls=%zu status=%d live=%zu native_depth=%u\n", sqlparser_dialect_name(dialect), warm, null_error, fail, calls, status, allocation_live, native_depth);
            CHECK(allocation_live == 0U && native_depth == 0U);
            if (calls < fail) {
                CHECK(status == SQLPARSER_STATUS_OK); complete = 1;
                fprintf(stderr, "allocation sweep dialect=%s warm=%d null=%d checked=%zu calls=%zu clean\n", sqlparser_dialect_name(dialect), warm, null_error, fail, calls);
                break;
            }
        }
        CHECK(complete);
    }
#else
    (void)dialect;
#endif
}
static void replacement(char *value, size_t i)
{
    CHECK(snprintf(value, 80U, "masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567", i + 1U) == 49);
}
/* Same independent byte edit as the existing public actual_pipeline_modes.c:
 * all 5000 old names, all other source bytes retained, no patch API oracle. */
static char *fixture_expected(const char *source, size_t rows)
{
    const char *needle = "'张三李四'", *p = source, *q; text_buffer out = {0}; size_t i = 0U;
    while ((q = strstr(p, needle)) != NULL) {
        char value[80], *literal; CHECK(i < rows); replacement(value, i++); literal = quoted(value);
        append_bytes(&out, p, (size_t)(q - p)); append(&out, literal); free(literal); p = q + strlen(needle);
    }
    append(&out, p); CHECK(i == rows); return out.data;
}
static void actual_fixture(sqlparser_dialect_t dialect, const char *source, const char *golden_dir)
{
    size_t typed, row; const size_t rows = 5000U;
    char *expected = fixture_expected(source, rows);
    for (typed = 0U; typed < 2U; typed++) {
        sqlparser_handle_t *h = parse(dialect, source); sqlparser_query_graph_view_t g;
        sqlparser_patch_t *p = calloc(rows, sizeof(*p)); sqlparser_literal_value_t *lit = calloc(rows, sizeof(*lit));
        char **values = calloc(rows, sizeof(*values)); char *out; retained_source retained;
        ++case_number; stage = typed ? "actual fixture typed" : "actual fixture raw";
        CHECK(p && lit && values); CHECK(sqlparser_statement_query_graph(h, 0U, &g, &error) == SQLPARSER_STATUS_OK);
        CHECK(multi(h)->branch_count == rows); retained = retain(h);
        for (row = 0U; row < rows; row++) {
            char value[80]; sqlparser_graph_dml_cell_t cell = branch_cell(&g, row, 2U);
            CHECK(multi(h)->branches[row].cell_count == 9U);
            CHECK(cell.literal.kind == SQLPARSER_LITERAL_KIND_STRING); same_text(cell.literal.string_value, "张三李四");
            replacement(value, row); p[row].op = SQLPARSER_PATCH_REPLACE; p[row].selector = cell_selector(&g, row, 2U);
            values[row] = typed ? copy_text(value) : quoted(value);
            if (typed) { lit[row].kind = SQLPARSER_LITERAL_KIND_STRING; lit[row].string_value = values[row]; p[row].literal = &lit[row]; }
            else p[row].sql = values[row];
        }
        CHECK(counted_apply(h, p, rows, &error) == SQLPARSER_STATUS_OK); verify_route(1); stale_graph(&g);
        verify_retained(h, &retained); release_retained(&retained);
        for (row = 0U; row < rows; row++) { free((char *)p[row].selector); memset(values[row], 'x', strlen(values[row])); free(values[row]); }
        free(values); free(lit); free(p);
        out = verify(h, expected, 1);
        if (golden_dir) {
            text_buffer path = {0}; char *gold;
            append(&path, golden_dir); append_format(&path, "/%s_%s.sql", sqlparser_dialect_name(dialect), typed ? "typed" : "raw");
            gold = read_file(path.data); same_text(out, gold); free(gold); free(path.data);
        }
        sqlparser_handle_destroy(h); free(out);
    }
    free(expected);
}
int main(int argc, char **argv)
{
    static const sqlparser_dialect_t dialects[] = {SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE};
    static const size_t shapes[][2] = {{1U, 1U}, {2U, 3U}, {7U, 5U}, {19U, 11U}};
    const char *fixture_path = NULL, *golden_dir = NULL; char *source = NULL; int alloc = 0; size_t i, d;
    for (i = 1U; i < (size_t)argc; i++) {
        if (!strcmp(argv[i], "--record")) recording = 1;
        else if (!strcmp(argv[i], "--alloc")) alloc = 1;
        else if (!strcmp(argv[i], "--fixture")) { CHECK(++i < (size_t)argc); fixture_path = argv[i]; }
        else if (!strcmp(argv[i], "--golden-dir")) { CHECK(++i < (size_t)argc); golden_dir = argv[i]; }
        else CHECK(0);
    }
    CHECK(!golden_dir || fixture_path); CHECK(!(recording && alloc));
    if (fixture_path) source = read_file(fixture_path);
    for (d = 0U; d < COUNT(dialects); d++) {
        for (i = 0U; i < COUNT(shapes); i++) generated_rounds(dialects[d], shapes[i][0], shapes[i][1]);
        borrowed_inputs(dialects[d]); following_generic_edits(dialects[d]); trailing_comment_fallback(dialects[d]);
        unusual_raw_status(dialects[d]); generic_boundaries(dialects[d]);
        national_boundary(dialects[d]); error_boundaries(dialects[d]);
        if (alloc) allocation_sweep(dialects[d]);
        if (source) actual_fixture(dialects[d], source, golden_dir);
    }
    free(source);
    if (!recording) puts("PASS: Oracle-owned branch commit, independent expected SQL/fresh parse, complete graph records, state/origins, lifetimes, generic boundaries and first errors");
    return 0;
}
