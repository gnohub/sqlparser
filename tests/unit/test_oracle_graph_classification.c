/* Oracle-family graph classification regression. No production bypass exists.
 * --record emits a stable, complete public-accessor/JSON/patch transcript for
 * byte comparison with the SAME source linked to an immutable baseline library.
 * Default runs portable synthesized fixtures. An optional SQL filename adds the
 * exact external fixture. The count oracle independently parses every cell.
 * Allocation ordinals are implementation-local; reference ordinals need not match. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "sqlparser_identifier_origin_internal.h"
#include "../../src/core/sqlparser_ast_internal.h"
#include "../../src/dialect/sqlparser_dialect_internal.h"
#include "../../src/dialect/sqlparser_dialect_oracle_internal.h"

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static sqlparser_error_t error;
static const char *stage = "init";
static size_t case_number, graph_cell_calls;
static int graph_count_active, recording;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d case=%zu stage=%s: %s: %s\n", __FILE__, __LINE__, case_number, stage, #x, error.message); abort(); } } while(0)

#ifdef SQLPARSER_ORACLE_GRAPH_WRAPPERS
sqlparser_status_t __real_sqlparser_parse_insert_cell_node_sql(const char *, const sqlparser_generated_source_t *, PgQuery__Node **, sqlparser_error_t *);
sqlparser_status_t __wrap_sqlparser_parse_insert_cell_node_sql(const char *s, const sqlparser_generated_source_t *source, PgQuery__Node **node, sqlparser_error_t *e)
{
    if (graph_count_active) ++graph_cell_calls;
    return __real_sqlparser_parse_insert_cell_node_sql(s, source, node, e);
}
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
static void record_number(unsigned long long n)
{ if(recording)printf("%llu;",n); }
static void record_text(const char *s)
{if(recording){if(!s)fputs("NULL;",stdout);else{printf("%zu:",strlen(s));fwrite(s,1U,strlen(s),stdout);fputc(';',stdout);}}}
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
    size_t i,j,n,index,count;int has;char *json=NULL,*sql=NULL;
    sqlparser_bind_occurrence_view_t binds;
    record_sqlparser_query_graph_view_t(g);
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
    CHECK(sqlparser_deparse(h,&sql,&error)==SQLPARSER_STATUS_OK);record_text(sql);free(sql);
    if(recording)fputc('\n',stdout);
}
static sqlparser_handle_t *parse(sqlparser_dialect_t dialect,const char *sql)
{
    sqlparser_handle_t *h=NULL;sqlparser_parse_options_t options;char *owned=sqlparser_strdup(sql);sqlparser_status_t s;
    CHECK(owned);sqlparser_parse_options_default(&options);options.dialect=dialect;
    options.limits.max_sql_bytes=32U*1024U*1024U;options.limits.max_output_bytes=64U*1024U*1024U;
    s=sqlparser_parse_with_options(owned,&options,&h,&error);memset(owned,'x',strlen(owned));free(owned);
    CHECK(s==SQLPARSER_STATUS_OK&&h);return h;
}
/* This oracle always invokes the unmodified generic parser for EVERY source
 * expression. It owns/frees every returned AST; there is no AST/output cache.
 * Its simulation only determines how many of those parses the new loop needs.
 * Cases containing binds use the retained parser placeholder rather than names. */
/* Detect ParamRef semantically in the independently parsed tree, including
 * nested functions and subqueries. Dollar signs inside comments/text are not binds. */
static int contains_parameter(const ProtobufCMessage *message)
{
    unsigned i;
    if(!message)return 0;
    if(message->descriptor==&pg_query__param_ref__descriptor)return 1;
    for(i=0U;i<message->descriptor->n_fields;i++){
        const ProtobufCFieldDescriptor *f=&message->descriptor->fields[i];
        const unsigned char *base=(const unsigned char *)message;
        if(f->type!=PROTOBUF_C_TYPE_MESSAGE)continue;
        if((f->flags&PROTOBUF_C_FIELD_FLAG_ONEOF)&&*(const unsigned *)(base+f->quantifier_offset)!=f->id)continue;
        if(f->label==PROTOBUF_C_LABEL_REPEATED){
            size_t j,n=*(const size_t *)(base+f->quantifier_offset);
            ProtobufCMessage *const *items=*(ProtobufCMessage *const *const *)(base+f->offset);
            for(j=0U;j<n;j++)if(contains_parameter(items[j]))return 1;
        }else if(contains_parameter(*(ProtobufCMessage *const *)(base+f->offset)))return 1;
    }
    return 0;
}
static size_t expected_calls(sqlparser_handle_t *h,size_t *full_count)
{
    const sqlparser_dialect_multi_insert_t *m=sqlparser_oracle_state_multi_insert(h->dialect_state);
    const char *remembered=NULL;size_t b,c,calls=0U;*full_count=0U;CHECK(m);
    for(b=0U;b<m->branch_count;b++)for(c=0U;c<m->branches[b].cell_count;c++){
        const sqlparser_dialect_multi_insert_value_t *v=&m->branches[b].cells[c];PgQuery__Node *node=NULL,*semantic;
        if(v->has_bind||v->has_literal||!v->parser_sql)continue;
        CHECK(sqlparser_parse_insert_cell_node_sql(v->parser_sql,NULL,&node,&error)==SQLPARSER_STATUS_OK);
        if(contains_parameter(&node->base)){sqlparser_free_proto_node(node);continue;}
        semantic=sqlparser_unwrap_grouping_node(node);++*full_count;
        if(!remembered||strcmp(remembered,v->parser_sql)!=0){++calls;if(semantic&&semantic->node_case==PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION&&semantic->sqlvalue_function)remembered=v->parser_sql;}
        else CHECK(semantic&&semantic->node_case==PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION&&semantic->sqlvalue_function);
        sqlparser_free_proto_node(node);
    }
    return calls;
}
static sqlparser_status_t counted_graph(sqlparser_handle_t *h,sqlparser_query_graph_view_t *g,sqlparser_error_t *e)
{
    sqlparser_status_t s;graph_cell_calls=0U;graph_count_active=1;s=sqlparser_statement_query_graph(h,0U,g,e);graph_count_active=0;return s;
}
static void check_count(size_t optimized,size_t full)
{
#ifdef SQLPARSER_ORACLE_GRAPH_WRAPPERS
#ifdef SQLPARSER_ORACLE_GRAPH_BASELINE
    CHECK(graph_cell_calls==full);
#else
    CHECK(graph_cell_calls==optimized);
#endif
#else
    (void)optimized;(void)full;
#endif
    (void)optimized;(void)full;
}
static void lifecycle(sqlparser_dialect_t dialect,const char *sql,int large)
{
    sqlparser_handle_t *h=parse(dialect,sql),*clone=NULL;sqlparser_query_graph_view_t g;size_t optimized,full;
    ++case_number;stage="fresh graph";record_number(dialect);record_text(sql);optimized=expected_calls(h,&full);
    CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);check_count(optimized,full);
    if(large)fprintf(stderr,"actual fixture dialect=%s generic-cell-parses=%zu cached-cell-parses=%zu observed-graph-cell-parses=%zu\n",sqlparser_dialect_name(dialect),full,optimized,graph_cell_calls);
    record_graph(h,&g);
    stage="warm graph";CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);check_count(0U,0U);
    stage="graph rebuild";sqlparser_handle_clear_query_graph(h);CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);check_count(optimized,full);
    if(!large)record_graph(h,&g);
    CHECK(sqlparser_handle_clone(h,&clone,&error)==SQLPARSER_STATUS_OK);sqlparser_handle_destroy(h);h=clone;
    stage="clone after owner destruction";sqlparser_handle_clear_query_graph(h);CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);check_count(optimized,full);
    if(!large)record_graph(h,&g);
    sqlparser_handle_destroy(h);
}
static void patch_modes(sqlparser_dialect_t dialect,const char *sql,size_t column)
{
    char *outputs[2]={NULL,NULL};size_t typed;
    for(typed=0U;typed<2U;typed++){
        sqlparser_handle_t *h=parse(dialect,sql);sqlparser_query_graph_view_t g;
        const sqlparser_dialect_multi_insert_t *m=sqlparser_oracle_state_multi_insert(h->dialect_state);
        sqlparser_patch_t *p;sqlparser_literal_value_t *values;sqlparser_patch_list_t list;size_t b,branches=m->branch_count;
        CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);
        p=calloc(branches,sizeof(*p));values=calloc(branches,sizeof(*values));CHECK(p&&values);
        for(b=0U;b<branches;b++){
            char selector[128];CHECK(column<m->branches[b].cell_count);
            snprintf(selector,sizeof(selector),"stmt[0].insert_cell[%zu][%zu]",b,column);p[b].selector=sqlparser_strdup(selector);CHECK(p[b].selector);p[b].op=SQLPARSER_PATCH_REPLACE;
            if(typed){values[b].kind=SQLPARSER_LITERAL_KIND_STRING;values[b].string_value="masked-'text'-Ω";p[b].literal=&values[b];}
            else p[b].sql="'masked-''text''-Ω'";
        }
        list.items=p;list.count=branches;stage=typed?"typed patch":"raw patch";
        CHECK(sqlparser_apply_patch(h,&list,&error)==SQLPARSER_STATUS_OK);
        for(b=0U;b<branches;b++) free((char *)p[b].selector);
        free(p);
        free(values);
        CHECK(sqlparser_deparse(h,&outputs[typed],&error)==SQLPARSER_STATUS_OK);record_text(outputs[typed]);
        CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);record_graph(h,&g);sqlparser_handle_destroy(h);
    }
    CHECK(strcmp(outputs[0],outputs[1])==0);free(outputs[0]);free(outputs[1]);
}
static void category_mutations(sqlparser_dialect_t dialect)
{
    static const char *cells[]={"CURRENT_DATE","DEFAULT","SrcValue",":Again","abs(-7)","'literal'","CURRENT_TIMESTAMP"};
    sqlparser_handle_t *h=parse(dialect,"INSERT ALL INTO T (C) VALUES (CURRENT_TIMESTAMP) INTO T (C) VALUES (CURRENT_TIMESTAMP) SELECT S.C AS SrcValue FROM SourceTable S");size_t i;
    for(i=0U;i<COUNT(cells);i++){
        sqlparser_patch_t p={0};sqlparser_patch_list_t list;sqlparser_query_graph_view_t g;size_t full,optimized;
        stage="category mutation";p.op=SQLPARSER_PATCH_REPLACE;p.selector="stmt[0].insert_cell[0][0]";p.sql=cells[i];list.items=&p;list.count=1U;
        CHECK(sqlparser_apply_patch(h,&list,&error)==SQLPARSER_STATUS_OK);CHECK(h->generation>0UL);
        optimized=expected_calls(h,&full);CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);check_count(optimized,full);record_graph(h,&g);
    }
    sqlparser_handle_destroy(h);
}
static void malformed_retry(sqlparser_dialect_t dialect)
{
    sqlparser_handle_t *h=parse(dialect,"INSERT ALL INTO T (C) VALUES (CURRENT_TIMESTAMP) INTO T (C) VALUES (CURRENT_TIMESTAMP) SELECT 1 FROM Dual");
    sqlparser_dialect_multi_insert_t *m=(sqlparser_dialect_multi_insert_t *)sqlparser_oracle_state_multi_insert(h->dialect_state);
    sqlparser_query_graph_view_t g;char *saved=m->branches[1].cells[0].parser_sql;sqlparser_status_t status;
    stage="malformed graph retry";m->branches[1].cells[0].parser_sql="CURRENT_TIMESTAMP +";
    status=counted_graph(h,&g,&error);CHECK(status!=SQLPARSER_STATUS_OK&&h->query_graph==NULL);record_status(status);
    m->branches[1].cells[0].parser_sql=saved;CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);check_count(1U,2U);record_graph(h,&g);
    sqlparser_handle_destroy(h);
}
static void cache_hit_public_sql(sqlparser_dialect_t dialect)
{
    sqlparser_handle_t *h=parse(dialect,"INSERT ALL INTO T (C) VALUES (CURRENT_TIMESTAMP) INTO T (C) VALUES (CURRENT_TIMESTAMP) SELECT 1 FROM Dual");
    sqlparser_dialect_multi_insert_t *m=(sqlparser_dialect_multi_insert_t *)sqlparser_oracle_state_multi_insert(h->dialect_state);
    sqlparser_query_graph_view_t g;char *saved=m->branches[1].cells[0].public_sql;
    stage="cache hit still validates public SQL";m->branches[1].cells[0].public_sql=NULL;
    CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_INTERNAL_ERROR&&h->query_graph==NULL);
    CHECK(strcmp(error.message,"multi-insert expression cell SQL is missing")==0);record_text(error.message);
    m->branches[1].cells[0].public_sql="current_timestamp /* distinct public spelling */";
    CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);check_count(1U,2U);record_graph(h,&g);
    m->branches[1].cells[0].public_sql=saved;sqlparser_handle_destroy(h);
}
#ifndef SQLPARSER_ORACLE_GRAPH_BASELINE
static void same_literal(const sqlparser_literal_view_t *a,const sqlparser_literal_view_t *b)
{
    CHECK(a->kind==b->kind&&a->integer_value==b->integer_value&&a->boolean_value==b->boolean_value&&a->quoted_identifier==b->quoted_identifier);
    CHECK((a->string_value==NULL)==(b->string_value==NULL));
    CHECK((a->float_value==NULL)==(b->float_value==NULL));
    if(a->string_value)CHECK(strcmp(a->string_value,b->string_value)==0);
    if(a->float_value)CHECK(strcmp(a->float_value,b->float_value)==0);
}
/* The immutable archive has dangling text on these AConst fallback paths.
 * Instead, EVERY expected literal is parsed separately and its AST kept alive
 * until after graph construction, allocator churn, and exact value comparison. */
static void live_literal_oracle(sqlparser_handle_t *h)
{
    const sqlparser_dialect_multi_insert_t *m=sqlparser_oracle_state_multi_insert(h->dialect_state);
    PgQuery__Node *nodes[32]={0};sqlparser_literal_view_t expected[32];
    sqlparser_query_graph_view_t g;void *churn[512];size_t b,c,n=0U,i;
    CHECK(m);
    for(b=0U;b<m->branch_count;b++)for(c=0U;c<m->branches[b].cell_count;c++){
        const sqlparser_dialect_multi_insert_value_t *v=&m->branches[b].cells[c];PgQuery__Node *semantic;
        CHECK(n<COUNT(nodes)&&v->parser_sql);
        CHECK(sqlparser_parse_insert_cell_node_sql(v->parser_sql,NULL,&nodes[n],&error)==SQLPARSER_STATUS_OK);
        semantic=sqlparser_unwrap_grouping_node(nodes[n]);CHECK(semantic&&semantic->node_case==PG_QUERY__NODE__NODE_A_CONST);
        CHECK(sqlparser_fill_literal_view_from_a_const(semantic->a_const,&expected[n],&error)==SQLPARSER_STATUS_OK);++n;
    }
    sqlparser_handle_clear_query_graph(h);CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);
    for(i=0U;i<COUNT(churn);i++){size_t size=8U+(i%16U)*8U;churn[i]=malloc(size);CHECK(churn[i]);memset(churn[i],0xa5,size);}
    for(i=0U;i<n;i++){
        sqlparser_graph_dml_cell_t cell;CHECK(sqlparser_query_graph_dml_cell_at(&g,i,&cell,&error)==SQLPARSER_STATUS_OK);
        CHECK(cell.kind==SQLPARSER_GRAPH_VALUE_LITERAL);same_literal(&cell.literal,&expected[i]);
    }
    for(i=0U;i<COUNT(churn);i++)free(churn[i]);
    for(i=0U;i<n;i++)sqlparser_free_proto_node(nodes[i]);
    /* The graph keeps its own copy even after the independent oracle dies. */
    {char *json=NULL;CHECK(sqlparser_export_view_json(h,0,&json,&error)==SQLPARSER_STATUS_OK);free(json);}
}
static void literal_ownership_cases(sqlparser_dialect_t dialect)
{
    static const char *cases[]={
        "INSERT ALL INTO T (A,B,C,D) VALUES (q'[SET SCHEMA hidden; can''t]',('grouped string'),(1.25),((123.456e+7))) SELECT q'{session;}' AS C FROM SourceTable",
        "INSERT ALL INTO T (A,B,C,D) VALUES ((('long enough grouped text to exercise independent ownership')),q'{a;b}',((0.0000000125)),(42)) INTO U(A,B,C,D) VALUES (q'!different quoted literal!',('two''quotes'),(1e-12),(7)) SELECT 1 FROM Dual"
    };
    size_t i,round;
    for(i=0U;i<COUNT(cases);i++){
        sqlparser_handle_t *h=parse(dialect,cases[i]),*clone=NULL;
        char *retained[4]={NULL,NULL,NULL,NULL},*snapshots[4]={NULL,NULL,NULL,NULL};
        stage="repaired fragment literal lifetime";live_literal_oracle(h);
        CHECK(sqlparser_handle_clone(h,&clone,&error)==SQLPARSER_STATUS_OK);sqlparser_handle_destroy(h);h=clone;
        live_literal_oracle(h);
        for(round=0U;round<COUNT(retained);round++){
            sqlparser_patch_t p={0};sqlparser_patch_list_t list;sqlparser_literal_value_t value={0};size_t j;
            p.op=SQLPARSER_PATCH_REPLACE;p.selector=round<2U?"stmt[0].insert_cell[0][0]":"stmt[0].insert_cell[0][2]";
            if(round==0U)p.sql="q'[raw replacement; can''t; and more storage]'";
            else if(round==1U){value.kind=SQLPARSER_LITERAL_KIND_STRING;value.string_value="typed replacement with 'quotes'";p.literal=&value;}
            else if(round==2U)p.sql="((999999.0125))";
            else {value.kind=SQLPARSER_LITERAL_KIND_FLOAT;value.float_value="0.0000125";p.literal=&value;}
            list.items=&p;list.count=1U;CHECK(sqlparser_apply_patch(h,&list,&error)==SQLPARSER_STATUS_OK);live_literal_oracle(h);
            CHECK(sqlparser_deparse(h,&retained[round],&error)==SQLPARSER_STATUS_OK);snapshots[round]=sqlparser_strdup(retained[round]);CHECK(snapshots[round]);
            for(j=0U;j<=round;j++)CHECK(strcmp(retained[j],snapshots[j])==0);
        }
        sqlparser_handle_destroy(h);
        for(round=0U;round<COUNT(retained);round++){CHECK(strcmp(retained[round],snapshots[round])==0);free(retained[round]);free(snapshots[round]);}
    }
}
#endif

static void allocation_sweep(sqlparser_dialect_t dialect)
{
#ifdef SQLPARSER_ORACLE_GRAPH_WRAPPERS
    static const char *cases[]={
        "INSERT ALL INTO T (A,B,C) VALUES (CURRENT_TIMESTAMP,1,CURRENT_TIMESTAMP) INTO T (A,B,C) VALUES (CURRENT_TIMESTAMP,abs(-7),CURRENT_TIMESTAMP) SELECT S.C AS Alias FROM SourceTable S WHERE S.C=:B",
#ifndef SQLPARSER_ORACLE_GRAPH_BASELINE
        "INSERT ALL INTO T (A,B,C) VALUES (q'[quoted text]',('grouped text'),(1.25)) INTO T (A,B,C) VALUES (q'{other text}',('different grouped text'),((0.0001))) SELECT S.C AS Alias FROM SourceTable S WHERE S.C=:B"
#endif
    };
    size_t fail,k;int null_error,completed;
    if(recording)return;
    for(k=0U;k<COUNT(cases);k++)for(null_error=0;null_error<2;null_error++){
        completed=0;
        for(fail=1U;fail<16384U;fail++){
            sqlparser_handle_t *h=parse(dialect,cases[k]);
            sqlparser_query_graph_view_t g;sqlparser_status_t status;size_t calls;
            stage="allocation sweep";CHECK(allocation_live==0U);allocation_fail=fail;allocation_calls=0U;allocation_active=1;
            status=counted_graph(h,&g,null_error?NULL:&error);allocation_active=0;calls=allocation_calls;
            if(status!=SQLPARSER_STATUS_OK){CHECK(h->query_graph==NULL);CHECK(counted_graph(h,&g,&error)==SQLPARSER_STATUS_OK);}
            else CHECK(h->query_graph!=NULL);
            sqlparser_handle_destroy(h);CHECK(allocation_live==0U&&native_depth==0U);
            if(calls<fail){CHECK(status==SQLPARSER_STATUS_OK);completed=1;break;}
        }
        CHECK(completed);
    }
#else
    (void)dialect;
#endif
}
static char *read_file(const char *path)
{
    FILE *f=fopen(path,"rb");long size;char *sql;CHECK(f&&fseek(f,0L,SEEK_END)==0);size=ftell(f);CHECK(size>=0&&fseek(f,0L,SEEK_SET)==0);sql=malloc((size_t)size+1U);CHECK(sql&&fread(sql,1U,(size_t)size,f)==(size_t)size);sql[size]='\0';CHECK(fclose(f)==0);return sql;
}
static char *fixture(const char *a,const char *b,const char *select_sql)
{
    const char *format="INSERT ALL INTO T (A,B,C,D,E) VALUES (%s,'literal',%s,%s,%s) INTO T (A,B,C,D,E) VALUES (%s,'literal',%s,%s,%s) %s";
    size_t length=1024U+strlen(a)*4U+strlen(b)*4U+strlen(select_sql);char *sql=malloc(length);int n;
    CHECK(sql);n=snprintf(sql,length,format,a,a,b,a,a,a,b,a,select_sql);CHECK(n>=0&&(size_t)n<length);return sql;
}
int main(int argc,char **argv)
{
    static const sqlparser_dialect_t dialects[]={SQLPARSER_DIALECT_ORACLE,SQLPARSER_DIALECT_KINGBASE_ORACLE,SQLPARSER_DIALECT_VASTBASE_ORACLE};
    static const char *functions[]={"CURRENT_DATE","CURRENT_TIME","CURRENT_TIMESTAMP","LOCALTIME","LOCALTIMESTAMP","CURRENT_ROLE","CURRENT_USER","USER","SESSION_USER","CURRENT_CATALOG","CURRENT_SCHEMA","CURRENT_TIMESTAMP(3)","CURRENT_TIMESTAMP /* precision */ (3)","(CURRENT_TIMESTAMP)","((CURRENT_TIMESTAMP))","current_timestamp","CURRENT_TIMESTAMP /* $123 is comment text */"};
    static const char *negative[]={"DEFAULT","SourceAlias","UnresolvedAlias","abs(-7)","CAST(42 AS INTEGER)","(2+3)","(SELECT 9 FROM Dual)","ROWNUM",":B","coalesce(:B,1)","CURRENT_TIMESTAMP + INTERVAL '1' DAY"};
    static const char *sources[]={
        "INSERT ALL INTO T (C) VALUES (CURRENT_TIMESTAMP) SELECT S.C FROM SourceTable S UNION ALL SELECT S.C FROM OtherTable S",
        "INSERT FIRST WHEN C=1 THEN INTO T (C) VALUES (CURRENT_TIMESTAMP) WHEN C=2 THEN INTO U (C) VALUES (CURRENT_TIMESTAMP) ELSE INTO V (C) VALUES (CURRENT_TIMESTAMP) SELECT C FROM SourceTable",
        "INSERT ALL INTO T (C) VALUES (CURRENT_TIMESTAMP) INTO U (C) VALUES (C) SELECT A.C AS C,B.C AS C FROM A,B",
        /* The branch q-quote variant has invalid borrowed literal text in the
         * frozen archive. literal_ownership_cases tests that repaired path
         * independently against live parsed literals, never baseline garbage. */
        "INSERT ALL INTO T (C) VALUES ('branch literal') INTO U (C) VALUES (CURRENT_TIMESTAMP) SELECT q'{session;}' AS C FROM SourceTable",
        "INSERT ALL INTO T (C) VALUES (CURRENT_TIMESTAMP) SELECT NodeId FROM TreeSource START WITH ParentId IS NULL CONNECT BY PRIOR NodeId=ParentId"
    };
    const char *path=NULL;size_t d,i;char *actual=NULL;
    for(i=1U;i<(size_t)argc;i++){if(strcmp(argv[i],"--record")==0)recording=1;else {CHECK(path==NULL);path=argv[i];}}
    if(path)actual=read_file(path);
    for(d=0U;d<COUNT(dialects);d++){
        for(i=0U;i<COUNT(functions);i++){char *sql=fixture(functions[i],i%2U?"CURRENT_DATE":"CURRENT_TIMESTAMP","SELECT S.C AS SourceAlias FROM SourceTable S");lifecycle(dialects[d],sql,0);free(sql);}
        for(i=0U;i<COUNT(negative);i++){char *sql=fixture("CURRENT_TIMESTAMP",negative[i],"SELECT S.C AS SourceAlias FROM SourceTable S");lifecycle(dialects[d],sql,0);free(sql);}
        for(i=0U;i<COUNT(sources);i++){
            if(dialects[d]==SQLPARSER_DIALECT_VASTBASE_ORACLE&&i+1U==COUNT(sources)){
                sqlparser_handle_t *h=NULL;sqlparser_parse_options_t options;sqlparser_status_t status;
                sqlparser_parse_options_default(&options);options.dialect=dialects[d];
                status=sqlparser_parse_with_options(sources[i],&options,&h,&error);
                CHECK(status==SQLPARSER_STATUS_UNSUPPORTED&&h==NULL);
                CHECK(strcmp(error.message,"hierarchical query is not supported for this dialect")==0);
                record_status(status);
            }else lifecycle(dialects[d],sources[i],0);
        }
        {char *sql=fixture("CURRENT_TIMESTAMP","CURRENT_TIMESTAMP","SELECT S.C AS SourceAlias FROM SourceTable S");patch_modes(dialects[d],sql,1U);free(sql);}
        category_mutations(dialects[d]);malformed_retry(dialects[d]);cache_hit_public_sql(dialects[d]);allocation_sweep(dialects[d]);
#ifndef SQLPARSER_ORACLE_GRAPH_BASELINE
        literal_ownership_cases(dialects[d]);
#endif
        {
            static const char *national_cases[]={
                "INSERT ALL INTO Target (Value) VALUES (1) SELECT N'national' AS Nat FROM SourceName",
                "INSERT ALL INTO Target (Value) VALUES (N'national') SELECT 1 FROM SourceName",
                "INSERT ALL INTO Target (Value) VALUES (nq'{national; text}') SELECT N'national' AS Nat FROM SourceName",
                "INSERT ALL INTO TargetTable (Value) VALUES (q'[branch; text]') SELECT q'[source; text]' AS \"QuotedAlias\", N'national' AS Nat FROM SchemaName.\"SourceName\" WHERE \"Field\"=:BindName"
            };
            size_t n;
            for(n=0U;n<COUNT(national_cases);n++){
                sqlparser_handle_t *h=parse(dialects[d],national_cases[n]);size_t round;
                stage="national literal baseline contract";record_text(national_cases[n]);
                for(round=0U;round<3U;round++){
                    sqlparser_query_graph_view_t g;sqlparser_status_t status;char *text=NULL;
                    record_number(round);record_text("graph");status=counted_graph(h,&g,&error);record_status(status);
                    if(status==SQLPARSER_STATUS_OK){record_text("json");status=sqlparser_export_view_json(h,0,&text,&error);record_status(status);if(status==SQLPARSER_STATUS_OK)record_text(text);free(text);text=NULL;}
                    if(status==SQLPARSER_STATUS_OK){record_text("deparse");status=sqlparser_deparse(h,&text,&error);record_status(status);if(status==SQLPARSER_STATUS_OK)record_text(text);free(text);}
                    if(status!=SQLPARSER_STATUS_OK){CHECK(strcmp(error.message,"national literal AST owner is missing")==0);break;}
                    if(round==0U){sqlparser_handle_t *clone=NULL;CHECK(sqlparser_handle_clone(h,&clone,&error)==SQLPARSER_STATUS_OK);sqlparser_handle_destroy(h);h=clone;}
                    if(round==1U){sqlparser_patch_t p={0};sqlparser_patch_list_t list;p.op=SQLPARSER_PATCH_REPLACE;p.selector="stmt[0].insert_cell[0][0]";p.sql="'replacement'";list.items=&p;list.count=1U;status=sqlparser_apply_patch(h,&list,&error);record_status(status);if(status!=SQLPARSER_STATUS_OK){CHECK(strcmp(error.message,"national literal AST owner is missing")==0);break;}}
                }
                sqlparser_handle_destroy(h);
            }
        }
        if(actual){lifecycle(dialects[d],actual,1);patch_modes(dialects[d],actual,2U);}
    }
    free(actual);if(!recording)puts("PASS: Oracle-family graph classification, independent cell oracle, complete public records, lifecycle, patch modes, retries and allocation ledger");return 0;
}
