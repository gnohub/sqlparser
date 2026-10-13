/* Regression coverage for compact authority. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_internal.h"

static sqlparser_error_t error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "compact:%d %s: %s\n", __LINE__, #x, error.message); exit(1); } } while (0)
#define OK(x) CHECK((x) == SQLPARSER_STATUS_OK)
static char *fixture(void) {
    char *s = malloc(24000); size_t at = 0;
    CHECK(s); at += sprintf(s+at,"INSERT INTO data_t(aa,bb,cc,dd) VALUES ");
    for (size_t i=0;i<96;i++) at += sprintf(s+at,"%s(%zu,'initial-long-value-abcdefghijklmnopqrstuvwxyz',12.5,CURRENT_TIMESTAMP)",i?",\n":"",i);
    strcpy(s+at,";  "); return s;
}
static sqlparser_handle_t *parse(const char *s) {
    sqlparser_parse_options_t o; sqlparser_handle_t *h=NULL;
    sqlparser_parse_options_default(&o); o.dialect=SQLPARSER_DIALECT_MYSQL;
    OK(sqlparser_parse_with_options(s,&o,&h,&error)); CHECK(h); return h;
}
static void compact(sqlparser_handle_t *h) {
    CHECK(h->representation == SQLPARSER_REPRESENTATION_COMPACT_INSERT);
    CHECK(h->compact_insert && h->parse_tree.data == NULL && h->ast == NULL);
    CHECK(h->compact_generation == h->generation);
}
static sqlparser_graph_dml_cell_t cell(sqlparser_query_graph_view_t *g,size_t row,size_t col) {
    sqlparser_graph_dml_cell_t c; OK(sqlparser_query_graph_dml_cell_at(g,row*4+col,&c,&error)); return c;
}
static void apply(sqlparser_handle_t *h,sqlparser_patch_t *p,size_t n) {
    sqlparser_patch_list_t list={p,n}; OK(sqlparser_apply_patch(h,&list,&error));
}
static void expect(sqlparser_handle_t *h,size_t row,const char *s) {
    sqlparser_literal_view_t l; OK(sqlparser_insert_cell_literal(h,0,row,1,&l,&error)); CHECK(l.kind==SQLPARSER_LITERAL_KIND_STRING && !strcmp(l.string_value,s));
}
static void boundaries(void) {
    for (size_t rows=31; rows<=33; ++rows) for (size_t length=4095; length<=4097; ++length) {
        char input[4098];size_t n=(size_t)sprintf(input,"INSERT INTO edge_t(aa,bb) VALUES ");
        for(size_t i=0;i<rows;i++)n+=(size_t)sprintf(input+n,"%s(%zu,'edge')",i?",":"",i);
        input[n++]=';';while(n<length)input[n++]=' ';input[n]=0;
        sqlparser_handle_t *h=parse(input);char *output=NULL;
        CHECK((h->compact_insert!=NULL)==(rows>=32 && length>=4096));
        OK(sqlparser_deparse(h,&output,&error));CHECK(!strcmp(input,output));
        sqlparser_string_free(output);sqlparser_handle_destroy(h);
    }
}
static void cold_and_limits(const char *input) {
    sqlparser_handle_t *h=parse(input);sqlparser_patch_t p[2]={{0}};size_t rows;
    compact(h);p[0].op=SQLPARSER_PATCH_REPLACE;p[0].selector="stmt[0].insert_cell[0][1]";p[0].sql="'cold'";
    apply(h,p,1);expect(h,0,"cold");CHECK(h->representation!=SQLPARSER_REPRESENTATION_EMPTY);sqlparser_handle_destroy(h);
    h=parse(input);p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ROW,.selector="stmt[0].insert_row[1]"};
    apply(h,p,1);OK(sqlparser_insert_row_count(h,0,&rows,&error));CHECK(rows==95);sqlparser_handle_destroy(h);
    /* An invalid oversized intermediate edit cannot be hidden by a later one. */
    h=parse(input);h->limits.max_sql_bytes=strlen(input)+1;h->limits.max_output_bytes=strlen(input)+1;
    char larger[16000];memset(larger,'z',sizeof(larger));larger[0]='\'';larger[15998]='\'';larger[15999]=0;
    p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][1]",.sql=larger};
    p[1]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=p[0].selector,.sql="'small'"};
    {sqlparser_patch_list_t list={p,2};CHECK(sqlparser_apply_patch(h,&list,&error)!=SQLPARSER_STATUS_OK);CHECK(h->failed);}sqlparser_handle_destroy(h);
}

static void ordered_compact_edits(const char *input) {
    sqlparser_handle_t *h=parse(input);sqlparser_query_graph_view_t g;
    sqlparser_patch_t patches[96]={{0}};char selectors[96][48];char *out=NULL;
    OK(sqlparser_statement_query_graph(h,0,&g,&error));
    patches[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[95][1]",.sql="'first'"};
    patches[1]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][1]",.sql="''"};
    patches[2]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=patches[0].selector,.sql="'last-expanded-value-012345678901234567890123456789012345678901234567890123456789'"};
    apply(h,patches,3);OK(sqlparser_statement_query_graph(h,0,&g,&error));
    CHECK(!strcmp(cell(&g,95,1).literal.string_value,"last-expanded-value-012345678901234567890123456789012345678901234567890123456789"));
    CHECK(!strcmp(cell(&g,0,1).literal.string_value,""));
    sqlparser_handle_destroy(h);h=parse(input);OK(sqlparser_statement_query_graph(h,0,&g,&error));
    /* Shrink every string until this valid authority is below initial admission
     * size. It must remain fully readable and promotable without re-recognition. */
    for(size_t i=0;i<96;i++) {
        sprintf(selectors[i],"stmt[0].insert_cell[%zu][1]",i);
        patches[i]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=selectors[i],.sql="''"};
    }
    apply(h,patches,96);compact(h);CHECK(h->sql_len<4096);
    OK(sqlparser_statement_query_graph(h,0,&g,&error));
    for(size_t i=0;i<96;i++)CHECK(!strcmp(cell(&g,i,1).literal.string_value,""));
    OK(sqlparser_deparse(h,&out,&error));CHECK(strlen(out)==h->sql_len);sqlparser_string_free(out);
    expect(h,95,"");sqlparser_handle_destroy(h);
    h=parse(input);OK(sqlparser_statement_query_graph(h,0,&g,&error));
    patches[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=selectors[0],.sql="'temporary'"};
    patches[1]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=selectors[0],.sql="'initial-long-value-abcdefghijklmnopqrstuvwxyz'"};
    apply(h,patches,2);CHECK(h->generation==1);
    OK(sqlparser_deparse(h,&out,&error));CHECK(!strcmp(out,input));sqlparser_string_free(out);sqlparser_handle_destroy(h);
}

int main(void) {
    char *s=fixture(),*original=strdup(s),*out=NULL,*old=NULL;
    PgQueryMysqlOwnedScalarInsertPlan plan={{0}};PgQueryCompactScalarInsert *cp=NULL;size_t sc=0;
    int proved=pg_query_prove_mysql_owned_scalar_insert(s,strlen(s),&plan,&sc);
    int taken=pg_query_take_mysql_compact_insert(s,strlen(s),&plan,&cp);
    CHECK(proved == 1 && taken == 1); free(cp);
    sqlparser_handle_t *h=parse(s); sqlparser_query_graph_view_t g,g2;
    sqlparser_graph_dml_cell_t c; sqlparser_graph_dml_t d;
    sqlparser_patch_t p[3]={{0}}; sqlparser_literal_value_t lit={0};
    const char *name=NULL; size_t count;
    compact(h); memset(s,'x',strlen(s)); free(s);
    OK(sqlparser_deparse(h,&out,&error)); CHECK(!strcmp(out,original)); old=out; out=NULL;
    OK(sqlparser_statement_query_graph(h,0,&g,&error)); compact(h);
    c=cell(&g,0,1); CHECK(!strcmp(c.literal.string_value,"initial-long-value-abcdefghijklmnopqrstuvwxyz"));
    apply(h,NULL,0); compact(h); OK(sqlparser_query_graph_dml(&g,&d,&error));
    p[0].op=p[1].op=SQLPARSER_PATCH_REPLACE;
    p[0].selector="stmt[0].insert_cell[0][1]"; p[0].sql="'changed'";
    p[1].selector="stmt[0].insert_cell[1][1]"; lit.kind=SQLPARSER_LITERAL_KIND_STRING; lit.string_value=c.literal.string_value; p[1].literal=&lit;
    apply(h,p,2); compact(h); CHECK(h->generation==1);
    CHECK(sqlparser_query_graph_dml(&g,&d,&error)==SQLPARSER_STATUS_INVALID_ARGUMENT);
    OK(sqlparser_statement_query_graph(h,0,&g2,&error)); compact(h);
    c=cell(&g2,0,1); CHECK(!strcmp(c.literal.string_value,"changed"));
    p[0].sql="'again'"; p[1].literal=NULL; p[1].sql="'second'";
    apply(h,p,2); compact(h); CHECK(h->generation==2);
    OK(sqlparser_statement_query_graph(h,0,&g,&error)); c=cell(&g,0,1);
    OK(sqlparser_statement_node_name(h,0,&name,&error)); CHECK(!strcmp(name,"InsertStmt"));
    CHECK(h->compact_insert==NULL && h->ast!=NULL && h->parse_tree.data==NULL);
    CHECK(!strcmp(c.literal.string_value,"again")); OK(sqlparser_query_graph_dml(&g,&d,&error));
    OK(sqlparser_statement_literal_count(h,0,&count,&error)); CHECK(count==96*3);
    memset(p,0,sizeof(p)); p[0].op=p[1].op=SQLPARSER_PATCH_REPLACE;
    p[0].selector="stmt[0].insert_cell[0][1]";lit.string_value=c.literal.string_value;p[0].literal=&lit;
    p[1].selector="stmt[0].insert_cell[1][1]";p[1].source_selector=p[0].selector;
    apply(h,p,2); expect(h,0,"again");expect(h,1,"again");
    p[0].literal=NULL;p[0].sql="'sequential'";apply(h,p,2);expect(h,1,"sequential");
    OK(sqlparser_deparse(h,&out,&error)); sqlparser_handle_destroy(h); CHECK(!strcmp(old,original));
    h=parse(out); expect(h,1,"sequential"); sqlparser_handle_destroy(h); sqlparser_string_free(out); sqlparser_string_free(old);
    h=parse(original); memset(p,0,sizeof(p));p[0].op=SQLPARSER_PATCH_REPLACE;p[0].selector="stmt[0].insert_cell[0][1]";p[0].sql="UPPER('expr')";
    apply(h,p,1); CHECK(h->compact_insert==NULL);OK(sqlparser_deparse(h,&out,&error));CHECK(strstr(out,"UPPER")||strstr(out,"upper"));sqlparser_string_free(out);sqlparser_handle_destroy(h);
    h=parse(original); p[0].sql="'bad'";p[0].selector="stmt[0].insert_cell[999][1]";
    {sqlparser_patch_list_t list={p,1}; CHECK(sqlparser_apply_patch(h,&list,&error)!=SQLPARSER_STATUS_OK);CHECK(h->failed);CHECK(sqlparser_deparse(h,&out,&error)!=SQLPARSER_STATUS_OK);CHECK(out==NULL);}sqlparser_handle_destroy(h);
    boundaries(); cold_and_limits(original); ordered_compact_edits(original);
    free(original); puts("compact authority ordinary transitions passed (9 admission boundaries plus lifecycle/promotion/limits)");return 0;
}
