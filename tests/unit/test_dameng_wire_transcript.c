/* This exact source links independently to current implementation and immutable baseline.
 * --record prints complete public graph/accessor/JSON/source/wire transcripts.
 * No runtime capability override or current implementation oracle is used in either binary. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "sqlparser_internal.h"
#include "sqlparser_test_failure.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static sqlparser_error_t error;
static int recording;
static const char *stage = "init";
static size_t cases;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d case=%zu stage=%s %s: %s\n",__FILE__,__LINE__,cases,stage,#x,error.message); abort(); } } while(0)
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

typedef struct { char *text; size_t length, capacity; } buffer;
static void append(buffer *b, const char *format, ...)
{
    va_list a,c; int n;
    va_start(a,format); va_copy(c,a); n=vsnprintf(NULL,0U,format,c); va_end(c); CHECK(n>=0);
    if(b->length+(size_t)n+1U>b->capacity){b->capacity=(b->length+(size_t)n+1U)*2U; b->text=realloc(b->text,b->capacity); CHECK(b->text);}
    CHECK(vsnprintf(b->text+b->length,b->capacity-b->length,format,a)==n);va_end(a);b->length+=(size_t)n;
}
static char *source(size_t rows,size_t columns)
{
    buffer b={0};size_t i,j;
    append(&b,"INSERT INTO TEST_LIB.MIXED(a0");for(j=1U;j<columns;j++)append(&b,",a%zu",j);append(&b,") VALUES ");
    for(i=0U;i<rows;i++){
        append(&b,"%s(",i?",":"");
        for(j=0U;j<columns;j++){
            if(j)append(&b,",");
            if(j==2U)append(&b,"'original-张三'");
            else if(j%4U==0U)append(&b,"%zu",i);
            else if(j%4U==1U)append(&b,"'Ω中'");
            else if(j%4U==2U)append(&b,"100.50");
            else append(&b,"CURRENT_TIMESTAMP");
        }append(&b,")");
    }append(&b,";\n");return b.text;
}
static char *read_file(const char *path)
{
    FILE *f=fopen(path,"rb");char *p;long n;CHECK(f&&fseek(f,0,SEEK_END)==0);n=ftell(f);CHECK(n>=0&&fseek(f,0,SEEK_SET)==0);
    p=malloc((size_t)n+1U);CHECK(p&&fread(p,1U,(size_t)n,f)==(size_t)n&&fclose(f)==0);p[n]='\0';return p;
}
static sqlparser_handle_t *parse_source(const char *sql)
{
    sqlparser_handle_t *h=NULL;sqlparser_parse_options_t o;
    sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_DAMENG;
    /* Complete 45,000-cell JSON is larger than the default output cap. */
    o.limits.max_output_bytes=64U*1024U*1024U;
    CHECK(sqlparser_parse_with_options(sql,&o,&h,&error)==SQLPARSER_STATUS_OK);return h;
}
static void record_handle(sqlparser_handle_t *h)
{
    size_t i;sqlparser_query_graph_view_t g;
    record_text(sqlparser_original_sql(h));record_text(h->parser_sql);
    record_number(h->parse_tree.len);
    if(recording)for(i=0U;i<h->parse_tree.len;i++)printf("%02x",(unsigned char)h->parse_tree.data[i]);
    CHECK(sqlparser_statement_query_graph(h,0U,&g,&error)==SQLPARSER_STATUS_OK);record_graph(h,&g);
}
static void run_source(char *sql,size_t rows,size_t columns,int typed,int extended)
{
    sqlparser_handle_t *h=parse_source(sql),*clone=NULL;
    sqlparser_query_graph_view_t g;sqlparser_graph_dml_cell_t cell;
    sqlparser_patch_t *p=calloc(rows,sizeof(*p));
    sqlparser_literal_value_t *l=calloc(rows,sizeof(*l));
    sqlparser_patch_list_t list={p,rows};size_t i,round;
    CHECK(p&&l);++cases;record_number(cases);record_number(rows);record_number(columns);record_number((unsigned)typed);
    memset(sql,0xa7,strlen(sql));free(sql);
    CHECK(sqlparser_statement_query_graph(h,0U,&g,&error)==SQLPARSER_STATUS_OK);
    /* Record initial state on a separate handle so public inspection cannot
     * materialize the optimized subject's AST before its original patch API. */
    {sqlparser_handle_t *initial=parse_source(sqlparser_original_sql(h));record_handle(initial);sqlparser_handle_destroy(initial);}
    for(i=0U;i<rows;i++){
        char value[80],*selector=NULL;buffer b={0};
        CHECK(sqlparser_query_graph_dml_cell_at(&g,i*columns+2U,&cell,&error)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_selector_format(&cell.selector,&selector,&error)==SQLPARSER_STATUS_OK);
        snprintf(value,sizeof(value),"masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567",i+1U);CHECK(strlen(value)==49U);
        p[i].op=SQLPARSER_PATCH_REPLACE;p[i].selector=selector;
        if(typed){l[i].kind=SQLPARSER_LITERAL_KIND_STRING;l[i].string_value=sqlparser_strdup(value);CHECK(l[i].string_value);p[i].literal=&l[i];}
        else{append(&b,"'%s'",value);p[i].sql=b.text;}
    }
    CHECK(sqlparser_apply_patch(h,&list,&error)==SQLPARSER_STATUS_OK);
    for(i=0U;i<rows;i++){free((char*)p[i].selector);free((char*)p[i].sql);free((char*)l[i].string_value);}
    memset(p,0xa7,rows*sizeof(*p));memset(l,0xa7,rows*sizeof(*l));free(p);free(l);
    if(extended){
        /* Immediate second commit, mixed raw/typed, duplicate selectors. */
        sqlparser_literal_value_t value={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value="second-round"};
        sqlparser_patch_t mixed[3]={{.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][2]",.sql="'first'"},
            {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[1][2]",.literal=&value},
            {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][2]",.sql="''"}};
        sqlparser_patch_list_t next={mixed,COUNT(mixed)};
        CHECK(sqlparser_apply_patch(h,&next,&error)==SQLPARSER_STATUS_OK);
    }
    record_handle(h);
    if(extended){
        CHECK(sqlparser_handle_clone(h,&clone,&error)==SQLPARSER_STATUS_OK);sqlparser_handle_destroy(h);h=clone;
        for(round=0U;round<3U;round++){
            sqlparser_patch_t patch={.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][2]",
                .sql=round==0U?"N'national-中'":round==1U?"upper('mixed')":"'plain-again'"};
            sqlparser_patch_list_t next={&patch,1U};
            CHECK(sqlparser_apply_patch(h,&next,&error)==SQLPARSER_STATUS_OK);record_handle(h);
        }
    }
    sqlparser_handle_destroy(h);
}
static void error_boundaries(void)
{
    static const char *bad[]={"'unterminated","'a', 'b'","'a'; SELECT 1","'a' garbage"};
    size_t i;
    for(i=0U;i<COUNT(bad);i++){
        char *s=source(33U,9U);sqlparser_handle_t *h=parse_source(s);sqlparser_query_graph_view_t g;
        sqlparser_patch_t p={.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][2]",.sql=bad[i]};
        sqlparser_patch_list_t list={&p,1U};sqlparser_status_t status;
        free(s);CHECK(sqlparser_statement_query_graph(h,0U,&g,&error)==SQLPARSER_STATUS_OK);
        status=sqlparser_apply_patch(h,&list,&error);CHECK(status!=SQLPARSER_STATUS_OK);record_status(status);
        record_number(error.code);record_number(error.cursor);record_number(error.line);record_number(error.column);
        record_number((unsigned)sqlparser_test_failed_handle(h));sqlparser_handle_destroy(h);++cases;
    }
}
int main(int argc,char **argv)
{
    static const size_t widths[]={3U,5U,9U,17U};size_t w,typed;const char *fixture=NULL;int i;
    for(i=1;i<argc;i++)if(strcmp(argv[i],"--record")==0)recording=1;else fixture=argv[i];
    stage="independent source wire and complete public accessor parity";
    for(w=0U;w<COUNT(widths);w++)for(typed=0U;typed<2U;typed++)run_source(source(33U,widths[w]),33U,widths[w],(int)typed,1);
    if(fixture)for(typed=0U;typed<2U;typed++)run_source(read_file(fixture),5000U,9U,(int)typed,0);
    error_boundaries();pg_query_exit();
    if(!recording)printf("Dameng independent transcript: %zu cases passed\n",cases);
    return 0;
}
