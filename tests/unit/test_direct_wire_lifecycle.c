/* Direct-wire certificates, cache/output failure cleanup and caller ownership. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
#include "src/pg_query_observer.h"

static sqlparser_error_t error;
static const char *stage;
static size_t failure_index;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s stage=%s failure=%zu error=%s\n",__FILE__,__LINE__,#x,stage?stage:"",failure_index,error.message); abort(); } } while (0)
enum { ROWS=128, FAIL_CACHE=1, FAIL_OUTPUT=2 };
#ifdef SQLPARSER_DIRECT_WIRE_FAILURE_WRAPPERS
static int armed, target, converter_depth;
static size_t cache_calls, output_calls, injected;
void *__real_realloc(void *,size_t);
void *__real_pg_query_protobuf_alloc_output(size_t);
PgQueryProtobuf __real_pg_query_nodes_to_protobuf_observed(const void *,PgQueryProtobufObserver,void *);
PgQueryProtobuf __real_pg_query_nodes_to_protobuf_certified(const void *,PgQueryProtobufObserver,void *,size_t *,int *);
void *__wrap_realloc(void *p,size_t size) {
    if(armed && converter_depth>0) {
        cache_calls++;
        if(target==FAIL_CACHE && cache_calls==failure_index){injected++;return NULL;}
    }
    return __real_realloc(p,size);
}
void *__wrap_pg_query_protobuf_alloc_output(size_t size) {
    if(armed) {
        CHECK(converter_depth>0);
        output_calls++;
        if(target==FAIL_OUTPUT && output_calls==failure_index){injected++;return NULL;}
    }
    return __real_pg_query_protobuf_alloc_output(size);
}
PgQueryProtobuf __wrap_pg_query_nodes_to_protobuf_observed(const void *tree,PgQueryProtobufObserver observer,void *context) {
    PgQueryProtobuf result;converter_depth++;
    result=__real_pg_query_nodes_to_protobuf_observed(tree,observer,context);
    converter_depth--;return result;
}
PgQueryProtobuf __wrap_pg_query_nodes_to_protobuf_certified(const void *tree,PgQueryProtobufObserver observer,void *context,size_t *count,int *certified) {
    PgQueryProtobuf result;converter_depth++;
    result=__real_pg_query_nodes_to_protobuf_certified(tree,observer,context,count,certified);
    converter_depth--;return result;
}
static void arm(int kind,size_t at) {
    CHECK(converter_depth==0);target=kind;failure_index=at;
    cache_calls=output_calls=injected=0U;armed=1;
}
static void disarm(void){armed=0;CHECK(converter_depth==0);}
#endif
static char *copy_text(const char *s) {
    char *p=malloc(strlen(s)+1U);CHECK(p!=NULL);strcpy(p,s);return p;
}
static char *make_sql(void) {
    size_t cap=ROWS*100U+128U,used=0U;
    char *s=malloc(cap);CHECK(s!=NULL);
    used+=(size_t)snprintf(s+used,cap-used,"INSERT INTO t(id,v) VALUES ");
    for(size_t i=0;i<ROWS;i++)used+=(size_t)snprintf(s+used,cap-used,
        "%s(%zu,'old-%04zu-abcdefghijklmnopqrstuvwxyz')",i?",":"",i,i);
    CHECK(used<cap && used>=4096U);return s;
}
static sqlparser_status_t parse_status(const char *sql,sqlparser_dialect_t dialect,sqlparser_handle_t **h) {
    sqlparser_parse_options_t options;sqlparser_parse_options_default(&options);options.dialect=dialect;
    *h=NULL;return sqlparser_parse_with_options(sql,&options,h,&error);
}
static sqlparser_handle_t *parse(const char *sql,sqlparser_dialect_t dialect) {
    sqlparser_handle_t *h=NULL;CHECK(parse_status(sql,dialect,&h)==SQLPARSER_STATUS_OK);CHECK(h!=NULL);return h;
}
static void decode_check(sqlparser_handle_t *h,const char *expected) {
    char *out=NULL;sqlparser_query_graph_view_t graph;sqlparser_graph_dml_t dml;
    /* Packed data must survive complete parser-context teardown. */
    pg_query_exit();
    CHECK(sqlparser_statement_query_graph(h,0U,&graph,&error)==SQLPARSER_STATUS_OK);
    CHECK(sqlparser_query_graph_dml(&graph,&dml,&error)==SQLPARSER_STATUS_OK);
    CHECK(dml.rows.count==ROWS*2U);
    CHECK(sqlparser_deparse(h,&out,&error)==SQLPARSER_STATUS_OK);
    CHECK(strcmp(out,expected)==0);sqlparser_string_free(out);
}
typedef struct {
    sqlparser_patch_t *items;
    sqlparser_literal_value_t *literals;
    char *selectors[2],*values[2];
} owned_batch;
static owned_batch make_batch(size_t round,int typed) {
    owned_batch b={0};char text[96];
    b.items=calloc(2U,sizeof(*b.items));b.literals=calloc(2U,sizeof(*b.literals));CHECK(b.items&&b.literals);
    for(size_t i=0;i<2U;i++) {
        snprintf(text,sizeof(text),"stmt[0].insert_cell[%zu][1]",i?(size_t)ROWS-1U:0U);
        b.selectors[i]=copy_text(text);
        snprintf(text,sizeof(text),typed?"round-%zu-%s":"'round-%zu-%s'",round,i?"right":"left");
        b.values[i]=copy_text(text);
        b.items[i].op=SQLPARSER_PATCH_REPLACE;b.items[i].selector=b.selectors[i];
        if(typed){b.literals[i].kind=SQLPARSER_LITERAL_KIND_STRING;b.literals[i].string_value=b.values[i];b.items[i].literal=&b.literals[i];}
        else b.items[i].sql=b.values[i];
    }
    return b;
}
static void free_batch(owned_batch *b) {
    for(size_t i=0;i<2U;i++){free(b->selectors[i]);free(b->values[i]);}
    free(b->literals);free(b->items);memset(b,0,sizeof(*b));
}
static sqlparser_status_t apply_batch(sqlparser_handle_t *h,owned_batch *b) {
    sqlparser_patch_list_t list={b->items,2U};return sqlparser_apply_patch(h,&list,&error);
}
typedef struct {size_t calls,count;} observation;
static void observe(const PgQuery__ParseResult *tree,void *context) {
    observation *o=context;o->calls++;o->count=tree->n_stmts;
}
static void certificate_and_fallback(void) {
    static const struct {const char *sql;int expected;size_t count;} cases[]={
        {"INSERT INTO t(a,b) VALUES(1,'x'),(2,NULL)",1,1},
        {"SELECT 1; UPDATE t SET a=2; DELETE FROM t",1,3},
        {"SELECT 1+2",0,1},{"SELECT a FROM t AS renamed",0,1},
        {"WITH x AS (SELECT 1) SELECT a FROM x",0,1},
        {"SELECT 1 INTO q",0,1},
        {"MERGE INTO t USING s ON t.a=s.a WHEN MATCHED THEN DELETE",0,1}
    };
    stage="certificate and typed-visitor fallback";
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        observation actual_observed={0},reference_observed={0};size_t count=0U;int certified=0;
        PgQueryProtobufParseResult actual=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(
            cases[i].sql,PG_QUERY_PARSE_DEFAULT,observe,&actual_observed,&count,&certified);
        PgQueryProtobufParseResult reference=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
            cases[i].sql,PG_QUERY_PARSE_DEFAULT,observe,&reference_observed);
        CHECK(actual.error==NULL && reference.error==NULL);
        CHECK(certified==cases[i].expected);
        CHECK(actual.parse_tree.data && reference.parse_tree.data);
        CHECK(actual.parse_tree.len==reference.parse_tree.len);
        CHECK(memcmp(actual.parse_tree.data,reference.parse_tree.data,actual.parse_tree.len)==0);
        CHECK(reference_observed.calls==1U);
        if(certified){CHECK(count==cases[i].count && actual_observed.calls==0U);}
        else CHECK(actual_observed.calls==1U && actual_observed.count==cases[i].count);
        pg_query_exit();
        CHECK(memcmp(actual.parse_tree.data,reference.parse_tree.data,actual.parse_tree.len)==0);
        pg_query_free_protobuf_parse_result(actual);pg_query_free_protobuf_parse_result(reference);
    }
}
static void repeated_owned_inputs(const char *sql) {
    sqlparser_handle_t *h=parse(sql,SQLPARSER_DIALECT_MYSQL);
    char *outputs[3]={0},*copies[3]={0};
    stage="three successes with immediate caller frees";
    for(size_t round=0;round<3U;round++) {
        sqlparser_query_graph_view_t old,fresh;sqlparser_graph_dml_t dml;sqlparser_literal_view_t value;
        owned_batch b=make_batch(round,(int)(round%2U));char expected[48];
        CHECK(sqlparser_statement_query_graph(h,0U,&old,&error)==SQLPARSER_STATUS_OK);
        CHECK(apply_batch(h,&b)==SQLPARSER_STATUS_OK);
        free_batch(&b); /* Every patch object/string is gone before deparse or graph. */
        pg_query_exit();
        CHECK(sqlparser_deparse(h,&outputs[round],&error)==SQLPARSER_STATUS_OK);
        copies[round]=copy_text(outputs[round]);
        CHECK(sqlparser_query_graph_dml(&old,&dml,&error)==SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(sqlparser_statement_query_graph(h,0U,&fresh,&error)==SQLPARSER_STATUS_OK);
        CHECK(fresh.generation==old.generation+1UL);
        for(size_t side=0;side<2U;side++) {
            snprintf(expected,sizeof(expected),"round-%zu-%s",round,side?"right":"left");
            CHECK(sqlparser_insert_cell_literal(h,0U,side?ROWS-1U:0U,1U,&value,&error)==SQLPARSER_STATUS_OK);
            CHECK(value.kind==SQLPARSER_LITERAL_KIND_STRING && strcmp(value.string_value,expected)==0);
        }
        for(size_t old_round=0;old_round<=round;old_round++)CHECK(strcmp(outputs[old_round],copies[old_round])==0);
    }
    sqlparser_handle_destroy(h);pg_query_exit();
    for(size_t i=0;i<3U;i++){CHECK(strcmp(outputs[i],copies[i])==0);sqlparser_string_free(outputs[i]);free(copies[i]);}
}
#ifdef SQLPARSER_DIRECT_WIRE_FAILURE_WRAPPERS
static void parse_failures(const char *sql) {
    for(int dialect=SQLPARSER_DIALECT_POSTGRESQL;dialect<=SQLPARSER_DIALECT_MYSQL;dialect++) {
        for(int kind=FAIL_CACHE;kind<=FAIL_OUTPUT;kind++) {
            sqlparser_handle_t *h=NULL;size_t count;
            stage=kind==FAIL_CACHE?"parse cache realloc failure":"parse packed-output failure";
            arm(kind,0U);CHECK(parse_status(sql,(sqlparser_dialect_t)dialect,&h)==SQLPARSER_STATUS_OK);disarm();
            CHECK(cache_calls>=2U && output_calls==1U && injected==0U);
            count=kind==FAIL_CACHE?cache_calls:output_calls;sqlparser_handle_destroy(h);
            for(size_t at=1U;at<=count;at++) {
                sqlparser_status_t status;
                arm(kind,at);status=parse_status(sql,(sqlparser_dialect_t)dialect,&h);disarm();
                CHECK(injected==1U && status==SQLPARSER_STATUS_NO_MEMORY && error.code==SQLPARSER_STATUS_NO_MEMORY && h==NULL);
                h=parse(sql,(sqlparser_dialect_t)dialect);decode_check(h,sql);sqlparser_handle_destroy(h);
            }
            printf("direct wire parse failure sweep dialect=%d target=%d boundaries=%zu\n",dialect,kind,count);
        }
    }
}
static void patch_failures(const char *sql) {
    for(int typed=0;typed<2;typed++)for(int kind=FAIL_CACHE;kind<=FAIL_OUTPUT;kind++) {
        size_t count=0U;
        stage=kind==FAIL_CACHE?"apply cache realloc failure":"apply packed-output failure";
        for(size_t at=0U;at<=count;at++) {
            sqlparser_handle_t *h=parse(sql,SQLPARSER_DIALECT_MYSQL);sqlparser_query_graph_view_t graph;
            sqlparser_status_t status;owned_batch b=make_batch(9U,typed);
            CHECK(sqlparser_statement_query_graph(h,0U,&graph,&error)==SQLPARSER_STATUS_OK);
            arm(kind,at);status=apply_batch(h,&b);disarm();free_batch(&b);
            if(at==0U) {
                CHECK(status==SQLPARSER_STATUS_OK && injected==0U && cache_calls>=2U && output_calls==1U);
                count=kind==FAIL_CACHE?cache_calls:output_calls;
            } else CHECK(status==SQLPARSER_STATUS_NO_MEMORY && error.code==SQLPARSER_STATUS_NO_MEMORY && injected==1U);
            /* Failure is terminal: no view/read/deparse attempt on this handle. */
            sqlparser_handle_destroy(h);
            h=parse(sql,SQLPARSER_DIALECT_MYSQL);decode_check(h,sql);sqlparser_handle_destroy(h);
        }
        printf("direct wire apply failure sweep typed=%d target=%d boundaries=%zu\n",typed,kind,count);
    }
}
static void syntax_precedence(const char *sql) {
    char *invalid=malloc(strlen(sql)+16U);sqlparser_handle_t *h=NULL;sqlparser_status_t status;
    CHECK(invalid!=NULL);sprintf(invalid,"%s; SELECT )",sql);
    stage="syntax error precedes output allocation failure";
    arm(FAIL_OUTPUT,1U);status=parse_status(invalid,SQLPARSER_DIALECT_MYSQL,&h);disarm();
    CHECK(output_calls==1U && injected==1U);
    CHECK(status==SQLPARSER_STATUS_PARSE_ERROR && error.code==SQLPARSER_STATUS_PARSE_ERROR && h==NULL);
    free(invalid);h=parse(sql,SQLPARSER_DIALECT_MYSQL);sqlparser_handle_destroy(h);
}
#endif
int main(void) {
    char *sql=make_sql();certificate_and_fallback();repeated_owned_inputs(sql);
#ifdef SQLPARSER_DIRECT_WIRE_FAILURE_WRAPPERS
    parse_failures(sql);patch_failures(sql);syntax_precedence(sql);
#else
    puts("SKIP: direct-wire allocation failure sweeps need GNU linker wrappers");
#endif
    free(sql);pg_query_exit();
    puts("direct wire lifecycle passed: certified/fallback parity, cache/output failures, repeated owned inputs and lazy decode");
    return 0;
}
