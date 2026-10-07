/* Batch-local SQL Server shape proof must not survive structural edits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"
static sqlparser_dialect_t dialect;
static sqlparser_error_t error;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line=%d dialect=%d error=%s\n",__LINE__,dialect,error.message);abort();}}while(0)
static const char input[]="INSERT INTO t(id,name) VALUES(1,'one'),(2,'two'),(3,'three')";
static sqlparser_handle_t *parse(const char *sql){sqlparser_parse_options_t o;sqlparser_handle_t *h=NULL;sqlparser_parse_options_default(&o);o.dialect=dialect;CHECK(sqlparser_parse_with_options(sql,&o,&h,&error)==SQLPARSER_STATUS_OK);return h;}
static void cell(sqlparser_handle_t *h,size_t row,size_t col,const char *expected){char *s=NULL;CHECK(sqlparser_insert_cell_sql(h,0,row,col,&s,&error)==SQLPARSER_STATUS_OK);if(strcmp(s,expected))fprintf(stderr,"actual=%s expected=%s\n",s,expected);CHECK(!strcmp(s,expected));sqlparser_string_free(s);}
static void apply(sqlparser_handle_t *h,sqlparser_patch_t *p,size_t n){sqlparser_patch_list_t l={p,n};CHECK(sqlparser_apply_patch(h,&l,&error)==SQLPARSER_STATUS_OK);}
static void roundtrip(sqlparser_handle_t *h){char *s=NULL;CHECK(sqlparser_deparse(h,&s,&error)==SQLPARSER_STATUS_OK);sqlparser_handle_t *again=parse(s);sqlparser_handle_destroy(again);sqlparser_string_free(s);}
int main(void){sqlparser_literal_value_t x={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value="x"},y={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value="y"};sqlparser_dialect_t ds[]={SQLPARSER_DIALECT_SQLSERVER,SQLPARSER_DIALECT_VASTBASE_SQLSERVER,SQLPARSER_DIALECT_KINGBASE_SQLSERVER};
for(size_t d=0;d<3;d++){dialect=ds[d];sqlparser_handle_t *h;sqlparser_patch_t p[5];
/* Repeated cells, then a new public batch on the same handle. */
h=parse(input);p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][1]",.literal=&x};p[1]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[1][1]",.sql="'raw'"};p[2]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][1]",.literal=&y};apply(h,p,3);cell(h,0,1,"'y'");cell(h,1,1,"'raw'");apply(h,p,1);cell(h,0,1,"'x'");roundtrip(h);sqlparser_handle_destroy(h);
/* Materialize two cached string edits, delete row, then resolve shifted row. */
h=parse(input);p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][1]",.literal=&x};p[1]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[1][1]",.literal=&x};p[2]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ROW,.selector="stmt[0].insert_row[0]"};p[3]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][1]",.literal=&y};apply(h,p,4);cell(h,0,0,"2");cell(h,0,1,"'y'");cell(h,1,0,"3");cell(h,1,1,"'three'");roundtrip(h);sqlparser_handle_destroy(h);
/* Column growth after a cached run cannot reuse stale shape validation. */
h=parse(input);p[2]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_INSERT_COLUMN,.selector="stmt[0].insert_columns",.index=1,.name="backup",.literal=&y};p[3]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[2][2]",.literal=&x};apply(h,p,4);cell(h,0,1,"'y'");cell(h,0,2,"'x'");cell(h,2,2,"'x'");roundtrip(h);sqlparser_handle_destroy(h);
/* Source-selector dependency observes earlier edits in the same batch. */
h=parse(input);p[2]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[2][1]",.source_selector="stmt[0].insert_cell[0][1]"};apply(h,p,3);cell(h,2,1,"'x'");roundtrip(h);sqlparser_handle_destroy(h);
/* Bad selector after a cached run remains terminal; views cannot be reused. */
h=parse(input);sqlparser_query_graph_view_t g;CHECK(sqlparser_statement_query_graph(h,0,&g,&error)==SQLPARSER_STATUS_OK);p[2]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[999][1]",.literal=&x};sqlparser_patch_list_t l={p,3};CHECK(sqlparser_apply_patch(h,&l,&error)!=SQLPARSER_STATUS_OK);char *s=NULL;CHECK(sqlparser_deparse(h,&s,&error)!=SQLPARSER_STATUS_OK);CHECK(s==NULL);sqlparser_handle_destroy(h);
/* Expression replacement breaks the immutable string run. */
h=parse(input);p[2]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[2][1]",.sql="LOWER('ABC')"};apply(h,p,3);cell(h,0,1,"'x'");roundtrip(h);sqlparser_handle_destroy(h);
}
puts("SQLSERVER_SHAPE_CACHE_PASSED");return 0;}
