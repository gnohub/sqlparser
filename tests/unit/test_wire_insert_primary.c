#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser_wire_insert_internal.h"
static size_t unpacks;
#ifdef SQLPARSER_WIRE_GRAPH_WRAPPERS
PgQuery__ParseResult *__real_pg_query__parse_result__unpack(ProtobufCAllocator *,size_t,const uint8_t *);
PgQuery__ParseResult *__wrap_pg_query__parse_result__unpack(ProtobufCAllocator *a,size_t n,const uint8_t *p) { unpacks++; return __real_pg_query__parse_result__unpack(a,n,p); }
#endif
#define OK(x) do { if (!(x)) { fprintf(stderr,"line %d %s error=%d:%s unpacks=%zu\n",__LINE__,#x,error.code,error.message,unpacks); return 1; } } while(0)
int main(void) {
 size_t rows=5000,i,used=0,n=0;
 char *input=malloc(rows*80U+128U),*expected=malloc(rows*80U+128U),*output=NULL;
 sqlparser_handle_t *h=NULL,*reference=NULL;
 sqlparser_parse_options_t options; sqlparser_error_t error={0};
 sqlparser_query_graph_view_t graph; sqlparser_graph_dml_t dml;
 sqlparser_patch_t *patches=calloc(rows,sizeof(*patches)); sqlparser_patch_list_t list={patches,rows};
 OK(input && expected && patches);
 used=(size_t)sprintf(input,"INSERT INTO t(id, text_col) VALUES "); n=(size_t)sprintf(expected,"INSERT INTO t(id, text_col) VALUES ");
 for(i=1;i<=rows;i++) {
  used+=(size_t)sprintf(input+used,"%s(%zu,'small-secret-%04zu')",i==1?"":",",i,i);
  n+=(size_t)sprintf(expected+n,"%s(%zu,'masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567')",i==1?"":",",i,i);
 }
 sqlparser_parse_options_default(&options);options.dialect=SQLPARSER_DIALECT_MYSQL;
 OK(sqlparser_parse_with_options(input,&options,&h,&error)==SQLPARSER_STATUS_OK);
 OK(h->ast==NULL && unpacks==0);
 OK(sqlparser_statement_query_graph(h,0,&graph,&error)==SQLPARSER_STATUS_OK);
 OK(sqlparser_query_graph_wire_insert(h)!=NULL && h->ast==NULL && unpacks==0);
 OK(sqlparser_query_graph_dml(&graph,&dml,&error)==SQLPARSER_STATUS_OK && dml.rows.count==rows*2U);
 for(i=0;i<dml.rows.count;i++) {
  size_t index;sqlparser_graph_dml_cell_t cell;
  OK(sqlparser_query_graph_span_index_at(&graph,dml.rows,i,&index,&error)==SQLPARSER_STATUS_OK);
  OK(sqlparser_query_graph_dml_cell_at(&graph,index,&cell,&error)==SQLPARSER_STATUS_OK);
  if(cell.column_ordinal==1) {
   char *value=malloc(80U),*selector=NULL; size_t row=cell.row_index;
   OK(value!=NULL);sprintf(value,"'masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567'",row+1);
   OK(sqlparser_selector_format(&cell.selector,&selector,&error)==SQLPARSER_STATUS_OK);
   patches[row].op=SQLPARSER_PATCH_REPLACE;patches[row].selector=selector;patches[row].sql=value;
  }
 }
 OK(sqlparser_apply_patch(h,&list,&error)==SQLPARSER_STATUS_OK && h->ast==NULL && unpacks==0);
 for(i=0;i<rows;i++){free((char*)patches[i].selector);free((char*)patches[i].sql);}free(patches);free(input);
 OK(sqlparser_deparse(h,&output,&error)==SQLPARSER_STATUS_OK && unpacks==0 && strcmp(output,expected)==0);
 OK(sqlparser_parse_with_options(expected,&options,&reference,&error)==SQLPARSER_STATUS_OK);
 OK(h->parse_tree.len==reference->parse_tree.len && !memcmp(h->parse_tree.data,reference->parse_tree.data,h->parse_tree.len));
 printf("primary 5000 rows: actual QueryGraph selectors, wire/output equality, caller free-before-deparse, %zu full AST unpacks passed\n",unpacks);
 free(expected);free(output);sqlparser_handle_destroy(reference);sqlparser_handle_destroy(h);pg_query_exit();return 0;
}
