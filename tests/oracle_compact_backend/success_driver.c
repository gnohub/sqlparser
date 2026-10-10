/* Independent ordinary-success and legal-fallback verification only.
 * No allocator hooks, injected failures, failure enumeration or sanitizers.
 * Compile each executable with its own tree's private headers and library. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <locale.h>
#include "sqlparser/sqlparser.h"
#include "sqlparser_internal.h"
#include "sqlparser_dialect_multi_insert_internal.h"
#include "sqlparser_dialect_oracle_internal.h"
static sqlparser_error_t err;
static const char *stage="init";
#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FAIL %s line %d: %s; %s\n",stage,__LINE__,#x,err.message);exit(1);} } while(0)
#define OK(x) do { sqlparser_status_t call_status=(x); REQUIRE(call_status==SQLPARSER_STATUS_OK); } while(0)
static void text(const char *s){ if(!s){puts("NULL");return;} printf("%zu:",strlen(s));fwrite(s,1,strlen(s),stdout);putchar('\n'); }
static void selector(const sqlparser_selector_t *s){printf("%d,%zu,%zu,%zu,%zu\n",s->kind,s->statement_index,s->item_index,s->row_index,s->column_index);}
#include "record_fields.inc"
static void selector_check(const sqlparser_selector_t *s){char *v=NULL;sqlparser_selector_t p;OK(sqlparser_selector_format(s,&v,&err));text(v);OK(sqlparser_selector_parse(v,&p,&err));char *u=NULL;OK(sqlparser_selector_format(&p,&u,&err));REQUIRE(!strcmp(v,u));sqlparser_string_free(v);sqlparser_string_free(u);}
static const sqlparser_dialect_multi_insert_t *multi(sqlparser_handle_t *h){const sqlparser_dialect_multi_insert_t*m=sqlparser_dialect_state_multi_insert(h->dialect,h->dialect_state);REQUIRE(m);return m;}
/* Read-only observations go to stderr, outside the compared public record.
 * A true getter proves eligibility, not a runtime count of skipped calls. */
static void header_state(sqlparser_handle_t *h,const char *label,int expected)
{
#ifdef HEADER_CLASSIFICATION
 const sqlparser_dialect_multi_insert_t *m=multi(h);
 int got=sqlparser_oracle_multi_insert_graph_headers_simple(h,m);
 unsigned flag=m->oracle_graph_header_flags;
 if(expected>=0){REQUIRE(flag==(expected?SQLPARSER_ORACLE_GRAPH_HEADER_SIMPLE_ASCII_ALL:0U));REQUIRE(got==expected);}
 fprintf(stderr,"header %s dialect=%d flag=%u getter=%d expected=%d graph=%d source_current=%d generic_equivalent=%d eligibility_only=1\n",label,h->dialect,flag,got,expected,h->query_graph!=NULL,sqlparser_oracle_multi_insert_source_is_current(h),m->oracle_generic_spans_equivalent);
#else
 (void)h;(void)label;(void)expected;
#endif
}
static void wire_record(const sqlparser_handle_t *h)
{
 const unsigned char *p=(const unsigned char *)h->parse_tree.data;
 printf("owned_wire_bytes=%zu\n",(size_t)h->parse_tree.len);
 for(size_t i=0;i<(size_t)h->parse_tree.len;i++)printf("%02x",p[i]);
 putchar('\n');
}
static int outside(const char*p,const char*s){if(!p||!s)return 1;return (uintptr_t)p<(uintptr_t)s||(uintptr_t)p>(uintptr_t)s+strlen(s);}
static void owned_text(sqlparser_handle_t*h,const char*p){REQUIRE(outside(p,h->sql));REQUIRE(outside(p,h->parser_sql));if(p) REQUIRE(p[strlen(p)]=='\0');}
#include "private_observation.inc"
static void ownership(sqlparser_handle_t *h,const char*label){
 const sqlparser_dialect_multi_insert_t*m=multi(h);
 for(size_t b=0;b<m->branch_count;b++){
  const sqlparser_dialect_multi_insert_branch_t*r=&m->branches[b];if(b&&r->cell_count&&m->branches[b-1].cell_count)REQUIRE(obs_storage(r)!=obs_storage(&m->branches[b-1]));
  owned_text(h,r->relation.sql);owned_text(h,r->relation.database_name);owned_text(h,r->relation.schema_name);owned_text(h,r->relation.table_name);owned_text(h,r->relation.link_name);owned_text(h,r->relation.link_sql);
  for(size_t c=0;c<r->column_count;c++){owned_text(h,r->columns[c].name);owned_text(h,r->columns[c].sql);}
 }
 observe_storage(h,label);
 fprintf(stderr,"ownership %s branches=%zu baseline_owned=1 independent_NUL=1 ast=%d graph=%d span_complete=%d generic_equivalent=%d\n",label,m->branch_count,h->ast!=NULL,h->query_graph!=NULL,m->oracle_spans_complete,m->oracle_generic_spans_equivalent);
}
static sqlparser_handle_t *parse(const char *s,int dialect,int json){sqlparser_handle_t*h=NULL;sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.dialect=dialect;if(json)o.limits.max_output_bytes=128U*1024U*1024U;OK(sqlparser_parse_with_options(s,&o,&h,&err));REQUIRE(h);const sqlparser_dialect_multi_insert_t*m=multi(h);for(size_t i=0;i<m->branch_count;i++){const sqlparser_dialect_multi_insert_branch_t*b=&m->branches[i];REQUIRE(outside(b->relation.sql,s));REQUIRE(outside(b->relation.database_name,s));REQUIRE(outside(b->relation.schema_name,s));REQUIRE(outside(b->relation.table_name,s));REQUIRE(outside(b->relation.link_name,s));REQUIRE(outside(b->relation.link_sql,s));for(size_t c=0;c<b->column_count;c++){REQUIRE(outside(b->columns[c].name,s));REQUIRE(outside(b->columns[c].sql,s));}}return h;}
#include "capture_graph.inc"
static void public_capture(sqlparser_handle_t*h,const char*label){puts(label);printf("dialect=%d statements=%zu\n",sqlparser_handle_dialect(h),sqlparser_statement_count(h));text(sqlparser_original_sql(h));text(h->parser_sql);wire_record(h);capture_graph(h);logical_cells(h);public_cell_sql_record(h);observe_storage(h,label);}
static char *deparse(sqlparser_handle_t*h){char*out=NULL;OK(sqlparser_deparse(h,&out,&err));REQUIRE(out);text(out);return out;}
static char *read_original(const char*path){FILE*f=fopen(path,"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long n=ftell(f);REQUIRE(n>0);rewind(f);char*all=calloc((size_t)n+1,1);REQUIRE(all);REQUIRE(fread(all,1,(size_t)n,f)==(size_t)n);REQUIRE(!fclose(f));const char*mark="-- BEGIN_SQL INSERT_ALL_5000 bytes=";char*s=strstr(all,mark);REQUIRE(s);char*end;unsigned long declared=strtoul(s+strlen(mark),&end,10);s=strchr(end,'\n');REQUIRE(s);s++;end=strstr(s,"-- END_SQL INSERT_ALL_5000");REQUIRE(end&&(size_t)(end-s)==declared);char*r=calloc(declared+1,1);REQUIRE(r);memcpy(r,s,declared);free(all);return r;}
static char *expected(const char*sql){const char*needle="'张三李四'",*p=sql,*q;size_t old=strlen(needle),newlen=51,n=strlen(sql),row=0;char*out=malloc(n+5000*(newlen-old)+1),*o=out;REQUIRE(out);while((q=strstr(p,needle))){memcpy(o,p,(size_t)(q-p));o+=q-p;int written=sprintf(o,"'masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567'",++row);REQUIRE(written==51);o+=written;p=q+old;}strcpy(o,p);REQUIRE(row==5000);return out;}
static void original(const char*path,int typed){stage=typed?"original typed":"original raw";char*sql=read_original(path),*want=expected(sql);sqlparser_handle_t*h=parse(sql,SQLPARSER_DIALECT_ORACLE,0);ownership(h,"original.parse");header_state(h,"original.parse",1);const sqlparser_dialect_multi_insert_t*m=multi(h);REQUIRE(m->branch_count==5000);
 const void*state=h->dialect_state;const void*root=m;const void**cells=calloc(5000,sizeof(*cells));REQUIRE(cells);for(size_t i=0;i<5000;i++)cells[i]=obs_storage(&m->branches[i]);
 require_storage(h,"original.initial",5000,0);
 public_capture(h,"original.initial");sqlparser_query_graph_view_t g;sqlparser_graph_dml_t d;OK(sqlparser_statement_query_graph(h,0,&g,&err));OK(sqlparser_query_graph_dml(&g,&d,&err));sqlparser_patch_t*p=calloc(5000,sizeof(*p));sqlparser_literal_value_t*l=calloc(5000,sizeof(*l));char(*values)[80]=calloc(5000,sizeof(*values));REQUIRE(p&&l&&values);size_t count=0;
 for(size_t b=0;b<d.branches.count;b++){size_t bi;sqlparser_graph_dml_branch_t br;OK(sqlparser_query_graph_span_index_at(&g,d.branches,b,&bi,&err));OK(sqlparser_query_graph_dml_branch_at(&g,bi,&br,&err));REQUIRE(br.rows.count==9);for(size_t c=0;c<br.rows.count;c++){size_t ci;sqlparser_graph_dml_cell_t cell;OK(sqlparser_query_graph_span_index_at(&g,br.rows,c,&ci,&err));OK(sqlparser_query_graph_dml_cell_at(&g,ci,&cell,&err));count++;if(c!=2)continue;REQUIRE(cell.has_selector);char*sel=NULL;OK(sqlparser_selector_format(&cell.selector,&sel,&err));p[b].op=SQLPARSER_PATCH_REPLACE;p[b].selector=sel;snprintf(values[b],80,"'masked-%08zu-abcdefghijklmnopqrstuvwxyz1234567'",b+1);REQUIRE(strlen(values[b])==51);if(typed){values[b][50]=0;l[b].kind=SQLPARSER_LITERAL_KIND_STRING;l[b].string_value=values[b]+1;p[b].literal=&l[b];}else p[b].sql=values[b];}}
 REQUIRE(count==45000);sqlparser_patch_list_t list={p,5000};OK(sqlparser_apply_patch(h,&list,&err));REQUIRE(h->dialect_state==state&&multi(h)==root);m=multi(h);for(size_t i=0;i<5000;i++)REQUIRE(cells[i]==obs_storage(&m->branches[i]));
 require_storage(h,"original.commit",5000,0);
 ownership(h,"original.commit");header_state(h,"original.commit",0);public_capture(h,"original.patched");char*out=deparse(h);REQUIRE(!strcmp(out,want));fprintf(stderr,"PASS %s input=%zu output=%zu cells=%zu patches=5000 replacement_payload_bytes=49 replacement_sql_bytes=51 state_root_actual_storage_stable=1\n",stage,strlen(sql),strlen(out),count);for(size_t b=0;b<5000;b++)sqlparser_string_free((char*)p[b].selector);free(p);free(l);free(values);free(cells);sqlparser_handle_destroy(h);REQUIRE(!strcmp(out,want));sqlparser_string_free(out);free(sql);free(want);}
#include "success_matrix.inc"
int main(int argc,char**argv){REQUIRE(setlocale(LC_CTYPE,"C"));REQUIRE(argc>=2);if(!strcmp(argv[1],"matrix"))matrix();else if(!strcmp(argv[1],"json")){REQUIRE(argc==3);original_json(argv[2]);}else{REQUIRE(argc==4);original(argv[2],!strcmp(argv[3],"typed"));}return 0;}
