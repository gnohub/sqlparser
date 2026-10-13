/* Regression coverage for owned authority. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"
static sqlparser_error_t error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"owned:%d %s: %s\n",__LINE__,#x,error.message); exit(1); } } while(0)
#define OK(x) CHECK((x)==SQLPARSER_STATUS_OK)
static sqlparser_handle_t *parse(const char *s, sqlparser_dialect_t d) {
    sqlparser_handle_t *h=NULL; sqlparser_parse_options_t o;
    sqlparser_parse_options_default(&o);o.dialect=d;
    OK(sqlparser_parse_with_options(s,&o,&h,&error));return h;
}
static void owned(sqlparser_handle_t *h) {
    CHECK(h && h->representation==SQLPARSER_REPRESENTATION_OWNED_AST);
    CHECK(h->ast && !h->parse_tree.data && !h->compact_insert);
}
static void apply(sqlparser_handle_t *h,sqlparser_patch_t *p,size_t n) {
    sqlparser_patch_list_t l={p,n};OK(sqlparser_apply_patch(h,&l,&error));owned(h);
}
static void *parse_thread(void *unused) {
    (void)unused;
    return parse("SELECT aa, 'first', 'second' FROM data_t WHERE aa = 7",SQLPARSER_DIALECT_POSTGRESQL);
}
static void transfer(void) {
    pthread_t t;void *result;char *sql=NULL,*json=NULL,*again=NULL;
    CHECK(!pthread_create(&t,NULL,parse_thread,NULL));CHECK(!pthread_join(t,&result));
    sqlparser_handle_t *h=result,*clone=NULL;owned(h);
    sqlparser_query_graph_view_t g;OK(sqlparser_statement_query_graph(h,0,&g,&error));owned(h);
    PgQuery__ParseResult *identity=h->ast;
    apply(h,NULL,0);CHECK(h->generation==0 && h->ast==identity);
    OK(sqlparser_export_view_json(h,0,&json,&error));free(json);owned(h);
    sqlparser_patch_t p[2]={{.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_target[0][1]",.sql="'modified-longer'"},
        {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_target[0][2]",.source_selector="stmt[0].select_target[0][1]"}};
    apply(h,p,2);CHECK(h->generation==1);owned(h);
    sqlparser_graph_dml_t dml;CHECK(sqlparser_query_graph_dml(&g,&dml,&error)==SQLPARSER_STATUS_INVALID_ARGUMENT);
    OK(sqlparser_deparse(h,&sql,&error));CHECK(strstr(sql,"modified-longer"));owned(h);
    OK(sqlparser_handle_clone(h,&clone,&error));owned(clone);CHECK(clone->ast!=h->ast);
    sqlparser_handle_destroy(h);OK(sqlparser_deparse(clone,&again,&error));CHECK(!strcmp(sql,again));free(again);
    p[0].selector="stmt[0].select_target[0][1]";p[0].sql="upper('changed')";apply(clone,p,1);owned(clone);
    OK(sqlparser_export_view_json(clone,0,&json,&error));free(json);
    OK(sqlparser_deparse(clone,&again,&error));owned(clone);CHECK(strstr(again,"changed"));
    sqlparser_handle_destroy(clone);CHECK(strstr(sql,"modified-longer"));free(sql);free(again);
}
int main(void) {
    const char *cases[]={
        "SELECT aa, sum(bb) OVER (PARTITION BY cc) FROM data_t WHERE aa IN (SELECT aa FROM other_t)",
        "WITH c AS (SELECT aa FROM data_t) SELECT * FROM c UNION ALL SELECT aa FROM other_t",
        "UPDATE data_t SET aa = 1, bb = 'value' WHERE cc = 2 RETURNING aa",
        "DELETE FROM data_t WHERE aa = 1 RETURNING aa",
        "INSERT INTO data_t (aa,bb) VALUES (1,'first'),(2,'second')",
        "MERGE INTO data_t t USING other_t s ON t.aa=s.aa WHEN MATCHED THEN UPDATE SET bb=s.bb WHEN NOT MATCHED THEN INSERT (aa) VALUES (s.aa)",
        "CREATE TABLE data_t (aa integer DEFAULT 1, bb text, CONSTRAINT pk PRIMARY KEY (aa))",
        "ALTER TABLE data_t ADD COLUMN cc integer DEFAULT 2",
        "CREATE INDEX ix_data ON data_t (aa)",
        "CREATE VIEW data_v AS SELECT aa FROM data_t WHERE aa > 1",
        "CREATE TABLE data_copy AS SELECT aa FROM data_t",
        "DROP TABLE data_t; TRUNCATE other_t; SET search_path TO public; BEGIN; COMMIT;"
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        char *input=strdup(cases[i]),*sql=NULL,*json=NULL;
        sqlparser_handle_t *h=parse(input,SQLPARSER_DIALECT_POSTGRESQL);
        if (i == 4U) CHECK(h->parse_tree.data && !h->compact_insert);
        else owned(h);
        memset(input,'x',strlen(input));free(input);
        OK(sqlparser_deparse(h,&sql,&error));CHECK(!strcmp(sql,cases[i]));free(sql);
        OK(sqlparser_export_view_json(h,0,&json,&error));free(json);
        if (i == 4U) CHECK(h->parse_tree.data && !h->compact_insert);
        else owned(h);
        sqlparser_handle_destroy(h);
    }
    for(int dialect=0;dialect<=12;dialect++) {
        sqlparser_handle_t *h=parse("SELECT aa FROM data_t WHERE aa = 1",(sqlparser_dialect_t)dialect);owned(h);
        sqlparser_patch_t p={.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_target[0][0]",.sql="2 + 3"};
        apply(h,&p,1);char *s=NULL;OK(sqlparser_deparse(h,&s,&error));CHECK(strstr(s,"2 + 3"));free(s);owned(h);sqlparser_handle_destroy(h);
    }
    transfer();
    puts("owned authority: 12 statement families, 13 dialect expression edits and cross-thread clone/lifecycle passed");return 0;
}
