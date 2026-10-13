/* Regression coverage for national literal fidelity. */
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"
#define CAP 8192
#define MAX_TARGETS 20
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"HARNESS_ERROR|%d|%s\n",__LINE__,#x);exit(2);} } while(0)
static sqlparser_dialect_t dialect;
static const char *label="startup", *route="startup", *phase="startup";
static const char *filter; static int only_dialect=-1, dump;
static size_t checks,failures,fixtures,reads,mutations,copies,limits_checked;
static void fmt(char *out,size_t cap,const char *s,...)
{va_list v;int n;va_start(v,s);n=vsnprintf(out,cap,s,v);va_end(v);REQUIRE(n>=0&&(size_t)n<cap);}
static void fail(const char *what,const char *expected,const char *actual)
{failures++;printf("FAIL|%d|%s|%s|%s|%s|expected=%s|actual=%s\n",dialect,label,route,phase,what,expected?expected:"NULL",actual?actual:"NULL");}
static void eq(const char *what,const char *expected,const char *actual)
{checks++;if(!actual||strcmp(expected,actual))fail(what,expected,actual);}
static void num(const char *what,size_t expected,size_t actual)
{char a[40],b[40];fmt(a,sizeof a,"%zu",expected);fmt(b,sizeof b,"%zu",actual);eq(what,a,b);}
static int ok(sqlparser_status_t s,sqlparser_error_t *e,const char *what)
{checks++;if(s){char b[512];fmt(b,sizeof b,"%d:%s",s,e->message);fail(what,"0",b);return 0;}return 1;}
static int mysql(void){return dialect==1||dialect==6||dialect==10;}
static int pg(void){return dialect==0||dialect==7||dialect==11;}
static int server(void){return dialect==3||dialect==8||dialect==12;}
static int wanted(void){return (!filter||strstr(label,filter))&&(only_dialect<0||(int)dialect==only_dialect);}
static sqlparser_handle_t *parse(const char *sql,size_t limit)
{
    sqlparser_handle_t *h=NULL;sqlparser_parse_options_t o;sqlparser_error_t e={0};
    sqlparser_parse_options_default(&o);o.dialect=dialect;if(limit)o.limits.max_output_bytes=limit;
    if(!ok(sqlparser_parse_with_options(sql,&o,&h,&e),&e,"parse")){sqlparser_handle_destroy(h);return NULL;}
    REQUIRE(h);return h;
}
static void quote(char *out,size_t cap,const char *value,int bracket)
{
    size_t n=0;char a=bracket?'[':mysql()?'`':'"', b=bracket?']':a;out[n++]=a;
    for(;*value;value++){REQUIRE(n+4<cap);out[n++]=*value;if(*value==b)out[n++]=b;}
    out[n++]=b;out[n]=0;
}
/* Fixture SQL supplies the oracle. Normalize insignificant spacing/case only;
 * preserve ordinary versus national string identity, decoded quoted identifier
 * bytes, all string bytes, punctuation, and token boundaries. The test permits
 * standard double/bracket identifier canonicalization but never content changes.
 * The fixture subset deliberately contains no backslash string escapes, dollar
 * quotes, q-quotes, E strings, or numeric spellings needing dialect decoding. */
static void addchar(char *out,size_t *n,char c){REQUIRE(*n+2<CAP);out[(*n)++]=c;out[*n]=0;}
static int canonical(const char *s,char *out)
{
    size_t i=0,n=0;out[0]=0;
    while(s[i]){
        unsigned char c=(unsigned char)s[i];
        if(isspace(c)){i++;continue;}
        if(s[i]=='-'&&s[i+1]=='-'){i+=2;while(s[i]&&s[i]!='\n')i++;continue;}
        if(s[i]=='/'&&s[i+1]=='*'){i+=2;while(s[i]&&!(s[i]=='*'&&s[i+1]=='/'))i++;if(!s[i])return 0;i+=2;continue;}
        if(s[i]=='\''||((s[i]=='N'||s[i]=='n')&&s[i+1]=='\'')){
            char type='S';int closed=0;if(s[i]!='\''){type='N';i++;}i++;addchar(out,&n,type);addchar(out,&n,':');
            while(s[i]){if(s[i]=='\''){if(s[i+1]=='\''){addchar(out,&n,'\'');i+=2;continue;}i++;closed=1;break;}addchar(out,&n,s[i++]);}
            if(!closed)return 0;
            addchar(out,&n,'|');continue;
        }
        if(s[i]=='"'||s[i]=='`'||s[i]=='['){char close=s[i]=='['?']':s[i];int closed=0;i++;addchar(out,&n,'Q');addchar(out,&n,':');
            while(s[i]){if(s[i]==close){if(s[i+1]==close){addchar(out,&n,close);i+=2;continue;}i++;closed=1;break;}addchar(out,&n,s[i++]);}
            if(!closed)return 0;
            addchar(out,&n,'|');continue;
        }
        if(isalnum(c)||c=='_'||c>=128){while(s[i]&&(isalnum((unsigned char)s[i])||s[i]=='_'||(unsigned char)s[i]>=128)){addchar(out,&n,(char)tolower((unsigned char)s[i++]));}addchar(out,&n,'|');continue;}
        addchar(out,&n,s[i++]);addchar(out,&n,'|');
    }
    return 1;
}
static void sql_eq(const char *what,const char *expected,const char *actual)
{char a[CAP],b[CAP];checks++;REQUIRE(canonical(expected,a));if(!actual||!canonical(actual,b)||strcmp(a,b))fail(what,expected,actual);}

typedef enum {TARGET,WHERE,ASSIGNMENT} kind_t;
typedef struct {size_t stmt,list,index;kind_t kind;char sql[CAP];} part_t;
typedef struct {char sql[CAP];part_t parts[MAX_TARGETS];size_t nparts,statements,binds;int rename;} fixture_t;
static void part(fixture_t *f,size_t stmt,size_t list,size_t index,kind_t kind,const char *sql)
{part_t *p;REQUIRE(f->nparts<MAX_TARGETS);p=&f->parts[f->nparts++];p->stmt=stmt;p->list=list;p->index=index;p->kind=kind;fmt(p->sql,sizeof p->sql,"%s",sql);}
static sqlparser_status_t getter(sqlparser_handle_t*h,const part_t*p,int selector,char **out,sqlparser_error_t *e)
{
    sqlparser_selector_t s;char text[128];
    if(!selector){if(p->kind==WHERE)return sqlparser_statement_where_sql(h,p->stmt,p->index,out,e);
        if(p->kind==ASSIGNMENT)return sqlparser_update_assignment_sql(h,p->stmt,p->index,out,e);
        return sqlparser_select_target_sql(h,p->stmt,p->list,p->index,out,e);}
    if(p->kind==WHERE)fmt(text,sizeof text,"stmt[%zu].where[%zu]",p->stmt,p->index);
    else if(p->kind==ASSIGNMENT)fmt(text,sizeof text,"stmt[%zu].assignment[%zu]",p->stmt,p->index);
    else fmt(text,sizeof text,"stmt[%zu].select_target[%zu][%zu]",p->stmt,p->list,p->index);
    if(sqlparser_selector_parse(text,&s,e))return e->code;
    if(p->kind==WHERE)return sqlparser_selector_where_sql(h,&s,out,e);
    if(p->kind==ASSIGNMENT)return sqlparser_selector_update_assignment_sql(h,&s,out,e);
    return sqlparser_selector_select_target_sql(h,&s,out,e);
}
/* Check live borrowed literals only before any mutation. Copies survive reads;
 * borrowed pointers are neither freed nor inspected after mutation/destruction. */
static int borrowed(sqlparser_handle_t*h,size_t statements,size_t binds)
{
    sqlparser_error_t e={0};sqlparser_bind_occurrence_view_t bv;size_t stmt,n,i;
    num("statement-count",statements,sqlparser_statement_count(h));
    for(stmt=0;stmt<statements;stmt++){
        if(!ok(sqlparser_statement_literal_count(h,stmt,&n,&e),&e,"literal-count"))return 0;
        for(i=0;i<n;i++){sqlparser_literal_view_t v,w;char *saved=NULL;
            if(!ok(sqlparser_statement_literal(h,stmt,i,&v,&e),&e,"literal-view"))return 0;
            if(v.kind==SQLPARSER_LITERAL_KIND_STRING){REQUIRE(v.string_value);saved=strdup(v.string_value);REQUIRE(saved);}
            if(!ok(sqlparser_statement_literal(h,stmt,i,&w,&e),&e,"literal-repeat")){free(saved);return 0;}
            num("literal-kind-repeat",v.kind,w.kind);
            if(saved){eq("borrowed-value-after-read",saved,v.string_value);eq("repeated-value",saved,w.string_value);free(saved);}
        }
    }
    if(!ok(sqlparser_handle_bind_occurrences(h,&bv,&e),&e,"bind-occurrences"))return 0;
    num("external-bind-count",binds,bv.count);
    for(i=0;i<bv.count;i++){sqlparser_bind_occurrence_t b;if(!ok(sqlparser_bind_occurrence_at(&bv,i,&b,&e),&e,"bind-at"))return 0;num("bind-position",i+1,b.position);}
    return 1;
}
static int render_whole(sqlparser_handle_t*h,const char *expected,int exact)
{
    sqlparser_error_t e={0};char *a=NULL,*b=NULL;int result=0;
    phase="whole";if(!ok(sqlparser_deparse(h,&a,&e),&e,"whole-getter"))goto done;reads++;
    if(exact)eq("whole-source-fidelity",expected,a);else sql_eq("whole-intended-tokens",expected,a);
    if(dump)printf("SQL|%d|%s|%s|whole|%s\n",dialect,label,route,a);
    if(!ok(sqlparser_deparse(h,&b,&e),&e,"whole-repeat"))goto done;
    reads++;eq("whole-repeat",a,b);result=1;
 done:sqlparser_string_free(a);sqlparser_string_free(b);return result;
}
static int render_parts(sqlparser_handle_t*h,const fixture_t*f,int selector)
{
    sqlparser_error_t e={0};size_t i;char *owned=NULL,*a=NULL,*b=NULL,*snapshot=NULL;int result=0;
    for(i=0;i<f->nparts;i++){
        char phasebuf[64];fmt(phasebuf,sizeof phasebuf,"part-%zu-stmt-%zu-list-%zu-index-%zu",i,f->parts[i].stmt,f->parts[i].list,f->parts[i].index);phase=phasebuf;
        if(!ok(getter(h,&f->parts[i],selector,&a,&e),&e,"fragment-getter"))goto done;
        reads++;
        sql_eq("fragment-intended-tokens",f->parts[i].sql,a);
        if(dump)printf("SQL|%d|%s|%s|%zu|%s\n",dialect,label,route,i,a);
        if(!ok(getter(h,&f->parts[i],selector,&b,&e),&e,"fragment-repeat"))goto done;
        reads++;eq("fragment-repeat",a,b);
        if(!owned){owned=a;a=NULL;snapshot=strdup(owned);REQUIRE(snapshot);}else eq("owned-after-other-reads",snapshot,owned);
        sqlparser_string_free(a);a=NULL;sqlparser_string_free(b);b=NULL;
    }
    result=1;
 done:phase="parts-cleanup";sqlparser_string_free(a);sqlparser_string_free(b);sqlparser_string_free(owned);free(snapshot);return result;
}
static int rename_table(sqlparser_handle_t*h,size_t statement)
{
    size_t n,i;sqlparser_error_t e={0};phase="relation-edit";
    if(!ok(sqlparser_statement_relation_count(h,statement,&n,&e),&e,"relation-count"))return 0;
    for(i=0;i<n;i++){sqlparser_relation_view_t v;if(!ok(sqlparser_statement_relation(h,statement,i,&v,&e),&e,"relation-view"))return 0;
        if(v.table_name&&!strcmp(v.table_name,"t")){mutations++;return ok(sqlparser_statement_set_relation_name(h,statement,i,NULL,"renamed_table",&e),&e,"relation-rename");}}
    fail("relation-locate","table t","not found");return 0;
}
static void ordinary(const fixture_t*f)
{
    static const char *routes[]={"whole","direct","selector","relation-whole","relation-direct","relation-selector"};
    if(!wanted())return;
    for(int k=0;k<(f->rename?6:3);k++){
        sqlparser_handle_t*h;fixture_t changed;char *where;size_t before=failures;route=routes[k];phase="parse";fixtures++;
        h=parse(f->sql,0);if(!h)goto report;
        if(!borrowed(h,f->statements,f->binds))goto done;
        changed=*f;
        if(k>=3){where=strstr(f->sql," FROM t");REQUIRE(where&&(where[7]==0||where[7]==' '||where[7]==';'));
            fmt(changed.sql,sizeof changed.sql,"%.*s FROM renamed_table%s",(int)(where-f->sql),f->sql,where+7);
            if(!rename_table(h,0))goto done;
        }
        if(k%3==0)render_whole(h,changed.sql,k<3);
        else if(render_parts(h,&changed,k%3==2))render_whole(h,changed.sql,k<3);
 done:sqlparser_handle_destroy(h);
 report:printf("FIXTURE|%d|%s|%s|%s|failures=%zu\n",dialect,label,route,failures==before?"PASS":"FAIL",failures-before);
    }
}
static void basic(const char *name,int national,int bracket,int bind)
{
    fixture_t f={.statements=1,.rename=1};char id[1024],a[1200],b[1400],c[1200],d[1200];
    const char *p=national?"N":"";quote(id,sizeof id,name,bracket);
    fmt(a,sizeof a,"%s'same' AS first_value",p);fmt(b,sizeof b,"c AS %s",id);
    fmt(c,sizeof c,"'same' AS ordinary_value");fmt(d,sizeof d,"%s'same' AS later_value",p);
    fmt(f.sql,sizeof f.sql,"SELECT %s, %s, %s, %s FROM t",a,b,c,d);
    part(&f,0,0,0,TARGET,a);part(&f,0,0,1,TARGET,b);part(&f,0,0,2,TARGET,c);part(&f,0,0,3,TARGET,d);
    if(bind){const char *b1=mysql()?"?":server()?"@first_bind":pg()?"$1":":first_bind";
        const char *b2=mysql()?"?":server()?"@last_bind":pg()?"$2":":last_bind";char e[256],g[256];
        fmt(e,sizeof e,"%s AS before_bind",b1);fmt(g,sizeof g,"%s AS after_bind",b2);
        fmt(f.sql,sizeof f.sql,"SELECT %s, %s, %s, %s, %s, %s FROM t",e,a,b,c,d,g);
        f.nparts=0;part(&f,0,0,0,TARGET,e);part(&f,0,0,1,TARGET,a);part(&f,0,0,2,TARGET,b);part(&f,0,0,3,TARGET,c);part(&f,0,0,4,TARGET,d);part(&f,0,0,5,TARGET,g);f.binds=2;
    }
    ordinary(&f);
}
static void composite(void)
{
    fixture_t f={.statements=1,.rename=1};
    label="mixed-later-escaped-unicode";
    fmt(f.sql,sizeof f.sql,"SELECT 'same' AS plain_first, N'same' AS national_second, 'O''Reilly列' AS plain_third, N'O''Reilly列' AS national_fourth, N'Ω' AS national_fifth FROM t");
    part(&f,0,0,0,TARGET,"'same' AS plain_first");part(&f,0,0,1,TARGET,"N'same' AS national_second");part(&f,0,0,2,TARGET,"'O''Reilly列' AS plain_third");part(&f,0,0,3,TARGET,"N'O''Reilly列' AS national_fourth");part(&f,0,0,4,TARGET,"N'Ω' AS national_fifth");ordinary(&f);
    memset(&f,0,sizeof f);f.statements=1;f.rename=1;label="nested-expression";
    fmt(f.sql,sizeof f.sql,"SELECT N'head' AS head_value, COALESCE('same', N'same', N'尾') AS nested_value, N'tail' AS tail_value FROM t");
    part(&f,0,0,0,TARGET,"N'head' AS head_value");part(&f,0,0,1,TARGET,"COALESCE('same', N'same', N'尾') AS nested_value");part(&f,0,0,2,TARGET,"N'tail' AS tail_value");ordinary(&f);
    memset(&f,0,sizeof f);f.statements=1;f.rename=1;label="scalar-subquery";
    fmt(f.sql,sizeof f.sql,"SELECT N'head' AS head_value, (SELECT COALESCE('same', N'same') AS inner_value FROM inner_t) AS nested_value, N'tail' AS tail_value FROM t");
    part(&f,0,0,0,TARGET,"N'head' AS head_value");part(&f,0,0,1,TARGET,"(SELECT COALESCE('same', N'same') AS inner_value FROM inner_t) AS nested_value");part(&f,0,0,2,TARGET,"N'tail' AS tail_value");part(&f,0,1,0,TARGET,"COALESCE('same', N'same') AS inner_value");ordinary(&f);
    memset(&f,0,sizeof f);f.statements=1;label="cte-fragments";
    fmt(f.sql,sizeof f.sql,"WITH q AS (SELECT 'same' AS plain_cte, N'same' AS national_cte FROM cte_t) SELECT N'outer' AS outer_value, 'same' AS plain_outer, N'same' AS national_outer FROM q");
    part(&f,0,0,0,TARGET,"N'outer' AS outer_value");part(&f,0,0,1,TARGET,"'same' AS plain_outer");part(&f,0,0,2,TARGET,"N'same' AS national_outer");part(&f,0,1,0,TARGET,"'same' AS plain_cte");part(&f,0,1,1,TARGET,"N'same' AS national_cte");ordinary(&f);
    memset(&f,0,sizeof f);f.statements=1;f.rename=1;label="set-operation";
    fmt(f.sql,sizeof f.sql,"SELECT 'same' AS left_plain, N'same' AS left_national FROM t UNION ALL SELECT N'same' AS right_national, 'same' AS right_plain FROM other_t");
    part(&f,0,0,0,TARGET,"'same' AS left_plain");part(&f,0,0,1,TARGET,"N'same' AS left_national");part(&f,0,1,0,TARGET,"N'same' AS right_national");part(&f,0,1,1,TARGET,"'same' AS right_plain");ordinary(&f);
    memset(&f,0,sizeof f);f.statements=2;f.rename=1;label="multiple-statements";
    fmt(f.sql,sizeof f.sql,"SELECT N'same' AS first_national, 'same' AS first_plain FROM t; SELECT 'same' AS second_plain, N'same' AS second_national, N'尾' AS last_national FROM second_t");
    part(&f,0,0,0,TARGET,"N'same' AS first_national");part(&f,0,0,1,TARGET,"'same' AS first_plain");part(&f,1,0,0,TARGET,"'same' AS second_plain");part(&f,1,0,1,TARGET,"N'same' AS second_national");part(&f,1,0,2,TARGET,"N'尾' AS last_national");ordinary(&f);
    memset(&f,0,sizeof f);f.statements=1;f.rename=1;label="comments-between-literals";
    fmt(f.sql,sizeof f.sql,"SELECT N'head' AS first_value, /* N'fake' @x $2 \" */ 'same' AS ordinary_value, -- N'fake' :x\n N'same' AS later_value FROM t");
    part(&f,0,0,0,TARGET,"N'head' AS first_value");part(&f,0,0,1,TARGET,"'same' AS ordinary_value");part(&f,0,0,2,TARGET,"N'same' AS later_value");ordinary(&f);
}
static void nonselect(void)
{
    fixture_t f={.statements=1};label="update-assignment-and-where";
    fmt(f.sql,sizeof f.sql,"UPDATE t SET a = 'same', b = N'same', c = N'尾' WHERE d = 'same' AND e = N'same'");
    part(&f,0,0,0,ASSIGNMENT,"'same'");part(&f,0,0,1,ASSIGNMENT,"N'same'");part(&f,0,0,2,ASSIGNMENT,"N'尾'");part(&f,0,0,0,WHERE,"d = 'same' AND e = N'same'");ordinary(&f);
    memset(&f,0,sizeof f);f.statements=1;label="delete-where";
    fmt(f.sql,sizeof f.sql,"DELETE FROM t WHERE d = 'same' AND e = N'same' AND f = N'尾'");part(&f,0,0,0,WHERE,"d = 'same' AND e = N'same' AND f = N'尾'");ordinary(&f);
}
static int find_literal(sqlparser_handle_t*h,const char *value,size_t *out)
{
    sqlparser_error_t e={0};size_t i,n;if(!ok(sqlparser_statement_literal_count(h,0,&n,&e),&e,"find-literal-count"))return 0;
    for(i=0;i<n;i++){sqlparser_literal_view_t v;if(!ok(sqlparser_statement_literal(h,0,i,&v,&e),&e,"find-literal-read"))return 0;if(v.kind==SQLPARSER_LITERAL_KIND_STRING&&!strcmp(v.string_value,value)){*out=i;return 1;}}
    fail("find-literal",value,"not found");return 0;
}
static void edits(void)
{
    const char *input="SELECT 'donor列' AS donor_value, N'keep' AS keep_value, 'old' AS edited_value, N'tail' AS tail_value FROM t";
    static const char *names[]={"raw-national-replacement","raw-ordinary-replacement","typed-ordinary-replacement","borrowed-typed-replacement","insert-national-target","delete-ordinary-target","source-selector-copy"};
    for(size_t k=0;k<COUNT(names);k++){
        sqlparser_handle_t*h;sqlparser_error_t e={0};sqlparser_status_t s;fixture_t f={.statements=1};sqlparser_literal_value_t v={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value="typed列"};char *owned=NULL,*snapshot=NULL;size_t idx=0,before=failures;
        label=names[k];route="mutation-lifecycle";phase="parse";if(!wanted())continue;fixtures++;h=parse(input,0);if(!h)goto report;
        if(!ok(sqlparser_deparse(h,&owned,&e),&e,"owned-before-mutation"))goto done;
        snapshot=strdup(owned);REQUIRE(snapshot);
        part(&f,0,0,0,TARGET,"'donor列' AS donor_value");part(&f,0,0,1,TARGET,"N'keep' AS keep_value");part(&f,0,0,2,TARGET,"'old' AS edited_value");part(&f,0,0,3,TARGET,"N'tail' AS tail_value");
        phase="mutation";mutations++;
        if(k<2){const char *raw=k==0?"N'new列' AS edited_value":"'new列' AS keep_value";size_t target=k==0?2:1;s=sqlparser_select_set_target_sql(h,0,0,target,raw,&e);fmt(f.parts[target].sql,sizeof f.parts[target].sql,"%s",raw);}
        else if(k<4){if(!find_literal(h,"old",&idx))goto done;
            if(k==3){size_t donor;sqlparser_literal_view_t borrowed_value;if(!find_literal(h,"donor列",&donor))goto done;if(!ok(sqlparser_statement_literal(h,0,donor,&borrowed_value,&e),&e,"borrowed-source"))goto done;v.string_value=borrowed_value.string_value;}
            s=sqlparser_statement_set_literal(h,0,idx,&v,&e);fmt(f.parts[2].sql,sizeof f.parts[2].sql,k==3?"'donor列' AS edited_value":"'typed列' AS edited_value");}
        else if(k==4){s=sqlparser_select_insert_target_sql(h,0,0,2,"N'inserted列' AS inserted_value",&e);f.parts[4]=f.parts[3];f.parts[4].index=4;f.parts[3]=f.parts[2];f.parts[3].index=3;fmt(f.parts[2].sql,sizeof f.parts[2].sql,"N'inserted列' AS inserted_value");f.nparts=5;}
        else if(k==5){s=sqlparser_select_delete_target(h,0,0,0,&e);for(size_t i=0;i<3;i++){f.parts[i]=f.parts[i+1];f.parts[i].index=i;}f.nparts=3;}
        else{sqlparser_patch_t p={.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_target[0][2]",.source_selector="stmt[0].select_target[0][1]"};sqlparser_patch_list_t list={&p,1};s=sqlparser_apply_patch(h,&list,&e);fmt(f.parts[2].sql,sizeof f.parts[2].sql,"N'keep' AS keep_value");copies++;}
        if(!ok(s,&e,"edit"))goto done;
        fmt(f.sql,sizeof f.sql,"SELECT ");for(size_t i=0;i<f.nparts;i++){size_t n=strlen(f.sql);fmt(f.sql+n,sizeof f.sql-n,"%s%s",i?", ":"",f.parts[i].sql);}strcat(f.sql," FROM t");
        if(!borrowed(h,1,0)||!render_whole(h,f.sql,0)||!render_parts(h,&f,0)||!render_parts(h,&f,1))goto done;
        if(k==6){phase="delete-original-copy-source";mutations++;if(!ok(sqlparser_select_delete_target(h,0,0,1,&e),&e,"delete-original"))goto done;
            f.parts[1]=f.parts[2];f.parts[1].index=1;f.parts[2]=f.parts[3];f.parts[2].index=2;f.nparts=3;
            fmt(f.sql,sizeof f.sql,"SELECT 'donor列' AS donor_value, N'keep' AS keep_value, N'tail' AS tail_value FROM t");
            if(!render_whole(h,f.sql,0)||!render_parts(h,&f,0)||!render_parts(h,&f,1))goto done;}
 done:sqlparser_handle_destroy(h);phase="destroyed-owner";if(snapshot)eq("owned-output-lifetime",snapshot,owned);free(snapshot);sqlparser_string_free(owned);
 report:printf("FIXTURE|%d|%s|%s|%s|failures=%zu\n",dialect,label,route,failures==before?"PASS":"FAIL",failures-before);
    }
}
static void output_limits(void)
{
    const char *input="SELECT N'head' AS first_value, N'tail' AS last_value FROM t";
    const char *whole="SELECT N'head' AS first_value, N'tail' AS last_value FROM renamed_table";
    const char *fragment="N'tail' AS last_value";
    label="output-limit";if(!wanted())return;
    for(int kind=0;kind<3;kind++)for(int short_by=0;short_by<2;short_by++){
        sqlparser_handle_t*h;sqlparser_error_t e={0};sqlparser_status_t s;char *out=NULL;char name[80];size_t cap=strlen(kind?fragment:whole)-(size_t)short_by,before=failures;
        fmt(name,sizeof name,"%s-%s",kind==0?"whole":kind==1?"direct":"selector",short_by?"short":"exact");route=name;phase="limit-parse";fixtures++;h=parse(input,cap);if(!h)goto report;
        if(kind==0&&!rename_table(h,0))goto done;
        phase="limit-render";limits_checked++;
        if(kind){part_t p={.index=1};s=getter(h,&p,kind==2,&out,&e);}else s=sqlparser_deparse(h,&out,&e);
        num("limit-status",short_by?SQLPARSER_STATUS_RESOURCE_LIMIT:SQLPARSER_STATUS_OK,s);
        if(s==SQLPARSER_STATUS_OK)eq("limit-exact-output",kind?fragment:whole,out);
        else{checks++;if(out)fail("limit-null-output","NULL",out);}
        /* No read or mutation follows any failed rendering operation. */
 done:sqlparser_string_free(out);sqlparser_handle_destroy(h);
 report:printf("FIXTURE|%d|%s|%s|%s|failures=%zu\n",dialect,label,route,failures==before?"PASS":"FAIL",failures-before);
    }
}
int main(int argc,char **argv)
{
    static const struct{const char *name,*value;} names[]={
        {"apostrophe","a'b"},{"double-quote","a\"b"},{"escaped-quote-unicode","a'\"尾"},
        {"dollar-bind","x$1$2"},{"named-bind","x@first_bind:other"},{"system-looking","@@ROWCOUNT"},
        {"type-looking","BOOLEAN NVARCHAR(MAX)"},{"comment-looking","x'--/* N'fake' */?"},
        {"bracket-looking","a]]b"}
    };
    for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--dump"))dump=1;else if(!strcmp(argv[i],"--case")&&i+1<argc)filter=argv[++i];else if(!strcmp(argv[i],"--dialect")&&i+1<argc)only_dialect=atoi(argv[++i]);else REQUIRE(0);}
    for(int d=0;d<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;d++){
        dialect=(sqlparser_dialect_t)d;
        label="ordinary-negative-control";basic("a'\"$1@x--/*N'fake'*/",0,0,0);
        if(pg())continue; /* This suite does not widen PostgreSQL N-surface scope. */
        for(size_t i=0;i<COUNT(names);i++){char name[128];fmt(name,sizeof name,"quoted-%s",names[i].name);label=name;basic(names[i].value,1,0,0);
            if(server()){fmt(name,sizeof name,"bracket-%s",names[i].name);basic(names[i].value,1,1,0);}}
        label="external-binds-protected-identifiers";basic("a'\"$1$2@first_bind:other?--/*N'fake'*/",1,0,1);
        composite();nonselect();edits();output_limits();
    }
    printf("SUMMARY|fixtures=%zu|checks=%zu|failures=%zu|reads=%zu|mutations=%zu|copies=%zu|limits=%zu\n",fixtures,checks,failures,reads,mutations,copies,limits_checked);
    return failures?1:0;
}
