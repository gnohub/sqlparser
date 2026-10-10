/* Compare the real view TU with the frozen varint3 statement scanner. A
 * call-through probe proves GO helper calls are unchanged for SQLServer and
 * zero for every other dialect. No helper result is replaced by the test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sqlparser_internal.h"
static size_t go_calls;
static int scangates_probe_go(sqlparser_dialect_t dialect, const char *sql, size_t pos, size_t *after)
{ ++go_calls; return sqlparser_public_sqlserver_go_at(dialect,sql,pos,after); }
#define sqlparser_public_sqlserver_go_at scangates_probe_go
#include "../../src/core/sqlparser_view.c"
#include "../oracle_scangates/frozen_statement_span.inc"
#undef sqlparser_public_sqlserver_go_at
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Statement scanner gate line %d: %s\n",__LINE__,#x); abort(); } } while(0)
static size_t cases, skipped_go_calls, retained_go_calls;
static void compare(const sqlparser_handle_t *h, const char *sql, int current, size_t statement, int null_outputs)
{
    size_t as = 117, ae = 219, bs = 117, be = 219, baseline_calls;
    int a,b;
    go_calls=0;
    b=scangates_frozen_statement_span(h,sql,current,statement,null_outputs&1?NULL:&bs,null_outputs&2?NULL:&be);
    baseline_calls=go_calls;go_calls=0;
    a=sqlparser_view_public_statement_span_in_sql(h,sql,current,statement,null_outputs&1?NULL:&as,null_outputs&2?NULL:&ae);
    CHECK(a==b && as==bs && ae==be);
    if(h && sqlparser_dialect_is_sqlserver_compatible(h->dialect)) {CHECK(go_calls==baseline_calls);retained_go_calls+=go_calls;}
    else {CHECK(go_calls==0);skipped_go_calls+=baseline_calls;}
    ++cases;
}
static uint32_t random_state=UINT32_C(0x6193245a);
static uint32_t next_random(void)
{ random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static void check_text(sqlparser_handle_t *h,const char *text)
{size_t i;h->sql_len=strlen(text);for(i=0;i<8;++i)compare(h,text,0,i,0);}
int main(void)
{
    static const char *const texts[]={
      "", " ", ";;;", "select 1", "select 1; select 2;", "GO", "go\n", " \tGo\r\nSELECT 2", "GO 2\nSELECT 1", "GO 0\nSELECT 1",
      "SELECT 1\nGO\nSELECT 2", "SELECT 1\r\nGo \t\r\nSELECT 2", "SELECT 1;\nGO\n;GO\nSELECT 2;\nGO",
      "GO --comment\nSELECT 2", "GO /*comment*/\nSELECT 2", "gopher;go_value; aGO; GOx", "SELECT GO FROM x;",
      "SELECT 'GO;\nGO', \"GO\", [GO], `GO`; SELECT 2", "SELECT 'a'';GO';SELECT 3", "/* GO\n */ SELECT 1; -- GO\nSELECT 2",
      "/*outer /* GO */ inner*/ SELECT 1;", "SELECT $$GO;$$; SELECT $tag$GO;$tag$; SELECT 2", "SELECT q'[GO;]', nq'{GO;}';SELECT 2",
      "SELECT q'(GO;)',q'<GO;>',q'!GO;!';GO\n", "SELECT '$tag$'; SELECT 2 -- GO", "SELECT 'unterminated; GO", "/*unterminated GO;",
      "GO\rSELECT 1\rGO\rSELECT 2", "SELECT 1\nGO -1\nSELECT 2", "SELECT 1\nGO 9999999999999999999999999\nSELECT 2",
      "SELECT 1\nGO+\nSELECT 2", "\001\177Ω中;SELECT 2"
    };
    static const unsigned char alphabet[]="aGgOo_9$'\"[]qN/*-; \n\r\t";
    sqlparser_handle_t h;int d;size_t i,j,n;char buffer[98];
    memset(&h,0,sizeof(h));
    for(d=-1;d<=13;++d){
      h.dialect=(sqlparser_dialect_t)d;
      for(i=0;i<sizeof(texts)/sizeof(texts[0]);++i)check_text(&h,texts[i]);
      for(i=0;i<1200;++i){n=next_random()%97U;for(j=0;j<n;++j)buffer[j]=(char)alphabet[next_random()%(sizeof(alphabet)-1U)];buffer[n]='\0';check_text(&h,buffer);}
      for(i=0;i<4;++i){compare(NULL,"GO",0,0,(int)i);compare(&h,NULL,0,0,(int)i);compare(&h,"GO",0,0,(int)i);}
      {
        const char *sql="  SELECT 1;   SELECT 2  ";sqlparser_control_state_t control;sqlparser_control_unit_t units[2];
        memset(&control,0,sizeof(control));memset(units,0,sizeof(units));
        control.units=units;control.unit_count=2;h.control=&control;h.sql_len=strlen(sql);
        units[0].source_offset=0;units[0].source_length=12;units[0].current_offset=0;units[0].current_length=12;
        units[1].source_offset=12;units[1].source_length=h.sql_len-12;units[1].current_offset=12;units[1].current_length=h.sql_len-12;
        for(i=0;i<4;++i){compare(&h,sql,0,i,0);compare(&h,sql,1,i,0);}
        units[0].source_offset=h.sql_len+1;compare(&h,sql,0,0,0);
        units[0].source_offset=0;units[0].source_length=h.sql_len+1;compare(&h,sql,0,0,0);
        units[0].current_offset=SIZE_MAX;units[0].current_length=1;compare(&h,sql,1,0,0);
        h.control=NULL;
      }
    }
    CHECK(skipped_go_calls>0 && retained_go_calls>0);
    printf("Statement GO gate: %zu frozen comparisons; skipped %zu non-SQLServer calls; retained %zu SQLServer calls exactly\n",cases,skipped_go_calls,retained_go_calls);
    return 0;
}
