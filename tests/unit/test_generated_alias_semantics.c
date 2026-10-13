/* Regression coverage for generated alias semantics. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "sqlparser_internal.h"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define MAX_ALIASES 32
static sqlparser_dialect_t dialect;
static const char *case_name="startup",*phase="startup",*variant="none";
static int dump;
static size_t checks,failures,differences,setters,patches,clones,errors,cases,fragments,views,getter_errors;
#define REQUIRE(x) do {if(!(x)){fprintf(stderr,"FATAL|%s|%d|%s|%s|line=%d|%s\n",case_name,dialect,variant,phase,__LINE__,#x);exit(2);}}while(0)
static void ok(sqlparser_status_t s,const sqlparser_error_t*e)
{if(s)fprintf(stderr,"STATUS|%d|%s\n",s,e->message);REQUIRE(s==SQLPARSER_STATUS_OK&&e->code==s);}
static char*copy(const char*s){char*p=strdup(s);REQUIRE(p);return p;}
static void fail(const char*kind,const char*expected,const char*actual)
{failures++;if(dump)printf("FAIL|%s|%d|%s|%s|%s|expected=%s|actual=%s\n",case_name,dialect,variant,phase,kind,expected?expected:"NULL",actual?actual:"NULL");}
static sqlparser_handle_t*parse_options(const char*sql,sqlparser_parse_options_t*o)
{sqlparser_handle_t*h=NULL;sqlparser_error_t e={0};ok(sqlparser_parse_with_options(sql,o,&h,&e),&e);REQUIRE(h);return h;}
static sqlparser_handle_t*parse(const char*sql)
{sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.dialect=dialect;return parse_options(sql,&o);}
static size_t alias_index(sqlparser_handle_t*h,size_t stmt,size_t occurrence)
{
    sqlparser_error_t e={0};size_t i,n;
    ok(sqlparser_statement_name_count(h,stmt,&n,&e),&e);
    for(i=0;i<n;i++){sqlparser_name_view_t v;ok(sqlparser_statement_name(h,stmt,i,&v,&e),&e);
        if(!strcmp(v.owner_type,"ResTarget")&&!strcmp(v.field_name,"name")&&occurrence--==0)return i;}
    REQUIRE(0);return SIZE_MAX;
}
typedef struct{size_t statement,occurrence;char*value;} alias_t;
typedef struct{sqlparser_handle_t*h;alias_t aliases[MAX_ALIASES];size_t count;const char*variant;} fixture_t;
static void capture(fixture_t*f)
{
    size_t s,i,n;sqlparser_error_t e={0};f->count=0;
    for(s=0;s<sqlparser_statement_count(f->h);s++){size_t ordinal=0;ok(sqlparser_statement_name_count(f->h,s,&n,&e),&e);
        for(i=0;i<n;i++){sqlparser_name_view_t v;ok(sqlparser_statement_name(f->h,s,i,&v,&e),&e);
            if(!strcmp(v.owner_type,"ResTarget")&&!strcmp(v.field_name,"name")){
                REQUIRE(f->count<MAX_ALIASES);f->aliases[f->count++]=(alias_t){s,ordinal++,copy(v.value)};}}}
}
static void expect(fixture_t*f,size_t stmt,size_t ordinal,const char*value)
{size_t i;for(i=0;i<f->count;i++)if(f->aliases[i].statement==stmt&&f->aliases[i].occurrence==ordinal){char*p=copy(value);free(f->aliases[i].value);f->aliases[i].value=p;return;}REQUIRE(0);}
static void close_fixture(fixture_t*f)
{size_t i;sqlparser_handle_destroy(f->h);for(i=0;i<f->count;i++)free(f->aliases[i].value);memset(f,0,sizeof(*f));}
static fixture_t fixture(const char*sql,const char*label)
{fixture_t f={0};f.h=parse(sql);f.variant=label;capture(&f);cases++;return f;}
static void apply(sqlparser_handle_t*h,sqlparser_patch_t*p,size_t n)
{sqlparser_patch_list_t list={p,n};sqlparser_error_t e={0};ok(sqlparser_apply_patch(h,&list,&e),&e);patches++;}
static void replace(sqlparser_handle_t*h,const char*selector,const char*sql)
{sqlparser_patch_t p={.op=SQLPARSER_PATCH_REPLACE,.selector=selector,.sql=sql};apply(h,&p,1);}
static void selector(fixture_t*f,size_t stmt,size_t ordinal,char out[80])
{snprintf(out,80,"stmt[%zu].name[%zu]",stmt,alias_index(f->h,stmt,ordinal));}
/* Protect equal-spelled columns, tables, relation aliases and CTE names from
 * incorrect value-based binding. Snapshot strings have independent lifetimes. */
static char*nonaliases(sqlparser_handle_t*h)
{
    size_t cap=32768,used=0,s,i,n;char*out=malloc(cap);sqlparser_error_t e={0};REQUIRE(out);out[0]=0;
    for(s=0;s<sqlparser_statement_count(h);s++){ok(sqlparser_statement_name_count(h,s,&n,&e),&e);
        for(i=0;i<n;i++){sqlparser_name_view_t v;int k;ok(sqlparser_statement_name(h,s,i,&v,&e),&e);
            if(!strcmp(v.owner_type,"ResTarget")&&!strcmp(v.field_name,"name"))continue;
            k=snprintf(out+used,cap-used,"%zu:%s:%s:%s;",s,v.owner_type,v.field_name,v.value);
            REQUIRE(k>=0&&(size_t)k<cap-used);used+=(size_t)k;}}
    return out;
}
static void rename_alias(fixture_t*f,size_t stmt,size_t ordinal,const char*value)
{
    sqlparser_error_t e={0};char*before=nonaliases(f->h),*after,*owned=copy(value);
    variant=f->variant;ok(sqlparser_statement_set_name(f->h,stmt,alias_index(f->h,stmt,ordinal),value,&e),&e);setters++;
    expect(f,stmt,ordinal,owned);free(owned);after=nonaliases(f->h);
    if(strcmp(before,after))fail("unrelated-name-change",before,after);
    free(before);free(after);
}
static void raw_alias(fixture_t*f,size_t stmt,size_t ordinal,const char*value,const char*spelling)
{char s[80];selector(f,stmt,ordinal,s);replace(f->h,s,spelling);expect(f,stmt,ordinal,value);}
static void alias_oracle(fixture_t*f,sqlparser_handle_t*h,const char*kind)
{
    size_t i;sqlparser_error_t e={0};
    for(i=0;i<f->count;i++){sqlparser_name_view_t v;alias_t*a=f->aliases+i;
        ok(sqlparser_statement_name(h,a->statement,alias_index(h,a->statement,a->occurrence),&v,&e),&e);
        if(strcmp(v.value,a->value))fail(kind,a->value,v.value);}
}
static char*alias_digest(sqlparser_handle_t*h)
{
    size_t i,n,used=0;sqlparser_error_t e={0};char*out=calloc(1,16384);REQUIRE(out);
    ok(sqlparser_statement_name_count(h,0,&n,&e),&e);
    for(i=0;i<n;i++){sqlparser_name_view_t v;int k;ok(sqlparser_statement_name(h,0,i,&v,&e),&e);
        if(strcmp(v.owner_type,"ResTarget")||strcmp(v.field_name,"name"))continue;
        k=snprintf(out+used,16384-used,"%zu:%s;",strlen(v.value),v.value);REQUIRE(k>=0&&(size_t)k<16384-used);used+=(size_t)k;}
    return out;
}
static void getter_error(sqlparser_status_t status,const sqlparser_error_t*e,const char*side)
{
    int mysql_mode=dialect==SQLPARSER_DIALECT_MYSQL||dialect==SQLPARSER_DIALECT_VASTBASE_MYSQL||dialect==SQLPARSER_DIALECT_KINGBASE_MYSQL;
    getter_errors++;
    if(dump)printf("GETTER_ERROR|%s|%d|%s|%s|%s|%d|%s\n",case_name,dialect,variant,phase,side,status,e->message);
    /* Narrow, observed legacy edge only. An unexpected error is a failed
     * correctness assertion, never silently normalized into that exception. */
    if(!mysql_mode||strcmp(phase,"em\"bed")||strcmp(side,"fresh")||
       status!=SQLPARSER_STATUS_PARSE_ERROR||strcmp(e->message,"unterminated quoted identifier in deparse output"))
        fail("unexpected-fragment-error","successful target getter",e->message);
}
static void fragment_oracle(sqlparser_handle_t*h,sqlparser_handle_t*fresh)
{
    size_t s,l,i,n,lists;sqlparser_error_t e={0};
    sqlparser_handle_t *owned_h=NULL,*owned_fresh=NULL;
    /* Getter diagnostics are isolated on clones. If a fragment rendering fails,
     * destroy that owner without reading or attempting to reuse it. */
    ok(sqlparser_handle_clone(h,&owned_h,&e),&e);
    ok(sqlparser_handle_clone(fresh,&owned_fresh,&e),&e);h=owned_h;fresh=owned_fresh;
    for(s=0;s<sqlparser_statement_count(h);s++){
        ok(sqlparser_select_target_list_count(h,s,&lists,&e),&e);
        for(l=0;l<lists;l++){ok(sqlparser_select_target_count(h,s,l,&n,&e),&e);
            for(i=0;i<n;i++){
                char *actual=NULL,*expected=NULL,*wrapped,*a,*b;sqlparser_handle_t*x,*y;
                sqlparser_selector_t sel={0};char selector_text[128],*selected=NULL;
                sqlparser_query_graph_view_t graph;size_t gi;const char*semantic_alias=NULL;
                sqlparser_status_t status=sqlparser_select_target_sql(h,s,l,i,&actual,&e);
                if(status!=SQLPARSER_STATUS_OK){
                    getter_error(status,&e,"current");
                    REQUIRE(!actual);goto done;}
                status=sqlparser_select_target_sql(fresh,s,l,i,&expected,&e);
                if(status!=SQLPARSER_STATUS_OK){
                    getter_error(status,&e,"fresh");
                    REQUIRE(!expected);sqlparser_string_free(actual);goto done;}
                snprintf(selector_text,sizeof(selector_text),"stmt[%zu].select_target[%zu][%zu]",s,l,i);
                ok(sqlparser_selector_parse(selector_text,&sel,&e),&e);
                ok(sqlparser_statement_query_graph(h,s,&graph,&e),&e);
                for(gi=0;gi<graph.target_count;gi++){sqlparser_graph_target_t t;
                    ok(sqlparser_query_graph_target_at(&graph,gi,&t,&e),&e);
                    if(t.has_selector&&t.selector.kind==sel.kind&&t.selector.statement_index==sel.statement_index&&
                       t.selector.item_index==sel.item_index&&t.selector.row_index==sel.row_index&&t.selector.column_index==sel.column_index){
                        semantic_alias=t.output_name;break;}}
                REQUIRE(semantic_alias);
                ok(sqlparser_selector_select_target_sql(h,&sel,&selected,&e),&e);
                if(strcmp(actual,selected))fail("selector-target-getter",actual,selected);
                sqlparser_string_free(selected);fragments++;
                wrapped=malloc(strlen(actual)+8);REQUIRE(wrapped);sprintf(wrapped,"SELECT %s",actual);x=parse(wrapped);free(wrapped);
                wrapped=malloc(strlen(expected)+8);REQUIRE(wrapped);sprintf(wrapped,"SELECT %s",expected);y=parse(wrapped);free(wrapped);
                a=alias_digest(x);b=alias_digest(y);
                /* Every target in this bounded driver has an explicit alias.
                 * Check its current semantic name directly too: the baseline's
                 * whole-statement output is not itself a correctness oracle. */
                {sqlparser_name_view_t root_alias;
                    ok(sqlparser_statement_name(x,0,alias_index(x,0,0),&root_alias,&e),&e);
                    if(strcmp(root_alias.value,semantic_alias))fail("fragment-live-alias",semantic_alias,root_alias.value);}
                if(strcmp(a,b))fail("fragment-roundtrip-alias",b,a);
                {char*actual_names=nonaliases(x),*expected_names=nonaliases(y);
                    if(strcmp(actual_names,expected_names))fail("fragment-roundtrip-unrelated",expected_names,actual_names);
                    free(actual_names);free(expected_names);}
                if(dump)printf("FRAGMENT|%s|%d|%s|%s|%s|%s\n",case_name,dialect,variant,phase,selector_text,actual);
                free(a);free(b);sqlparser_handle_destroy(x);sqlparser_handle_destroy(y);
                sqlparser_string_free(actual);sqlparser_string_free(expected);
            }
        }
    }
done:
    sqlparser_handle_destroy(owned_h);sqlparser_handle_destroy(owned_fresh);
}
static void view_oracle(sqlparser_handle_t*h,sqlparser_handle_t*fresh)
{
    sqlparser_error_t e={0};json_error_t je;char*actual=NULL,*expected=NULL;json_t*a,*b,*sa,*sb;size_t s,i;
    ok(sqlparser_export_view_json(h,0,&actual,&e),&e);ok(sqlparser_export_view_json(fresh,0,&expected,&e),&e);
    a=json_loads(actual,0,&je);b=json_loads(expected,0,&je);REQUIRE(a&&b);
    sa=json_object_get(a,"statements");sb=json_object_get(b,"statements");REQUIRE(json_array_size(sa)==json_array_size(sb));
    for(s=0;s<json_array_size(sa);s++){
        json_t*ta=json_object_get(json_object_get(json_array_get(sa,s),"query_graph"),"targets");
        json_t*tb=json_object_get(json_object_get(json_array_get(sb,s),"query_graph"),"targets");
        REQUIRE(json_array_size(ta)==json_array_size(tb));
        for(i=0;i<json_array_size(ta);i++){
            const char*x=json_string_value(json_object_get(json_array_get(ta,i),"name"));
            const char*y=json_string_value(json_object_get(json_array_get(tb,i),"name"));
            if((x==NULL)!=(y==NULL)||(x&&strcmp(x,y)))fail("view-target-name",y,x);
            views++;
        }
    }
    json_decref(a);json_decref(b);sqlparser_string_free(actual);sqlparser_string_free(expected);
}
static char*checkpoint(fixture_t*f,const char*label)
{
    sqlparser_error_t e={0};char*out=NULL,*names,*round_names;sqlparser_handle_t*fresh=NULL;
    sqlparser_parse_options_t o;sqlparser_status_t status;size_t s,i,n;
    phase=label;variant=f->variant;checks++;alias_oracle(f,f->h,"live-alias");
    /* Borrowed pointers are compared only within one unmutated generation. */
    for(s=0;s<sqlparser_statement_count(f->h);s++){ok(sqlparser_statement_name_count(f->h,s,&n,&e),&e);
        for(i=0;i<n;i++){sqlparser_name_view_t a,b;ok(sqlparser_statement_name(f->h,s,i,&a,&e),&e);ok(sqlparser_statement_name(f->h,s,i,&b,&e),&e);REQUIRE(a.value==b.value&&a.owner_type==b.owner_type&&a.field_name==b.field_name);}}
    ok(sqlparser_deparse(f->h,&out,&e),&e);
    if(dump)printf("SQL|%s|%d|%s|%s|%s\n",case_name,dialect,variant,label,out);
    sqlparser_parse_options_default(&o);o.dialect=dialect;status=sqlparser_parse_with_options(out,&o,&fresh,&e);
    if(status!=SQLPARSER_STATUS_OK){fail("reparse","valid SQL",e.message);REQUIRE(!fresh);return out;}
    alias_oracle(f,fresh,"roundtrip-alias");names=nonaliases(f->h);round_names=nonaliases(fresh);
    if(strcmp(names,round_names))fail("roundtrip-unrelated",names,round_names);
    free(names);free(round_names);fragment_oracle(f->h,fresh);
    /* The exact SQL-output boundary is deliberately too small for View JSON. */
    if(strcmp(f->variant,"exact-limit"))view_oracle(f->h,fresh);
    sqlparser_handle_destroy(fresh);return out;
}
static void check(fixture_t*f,const char*label){char*out=checkpoint(f,label);sqlparser_string_free(out);}
static void pair_check(fixture_t*a,fixture_t*b,const char*label)
{
    char*x=checkpoint(a,label),*y=checkpoint(b,label);
    if(strcmp(x,y)){differences++;if(dump)printf("PAIR_DIFFERENCE|%s|%d|%s|original=%s|generated=%s\n",case_name,dialect,label,x,y);}
    sqlparser_string_free(x);sqlparser_string_free(y);
}
static void pair_rename(fixture_t*a,fixture_t*b,const char*value,const char*label)
{phase=label;rename_alias(a,0,0,value);rename_alias(b,0,0,value);pair_check(a,b,label);}
static void quote_name(char*out,size_t cap,const char*value,int q)
{
    size_t n=0;char open=q==1?'"':q==2?96:q==3?'[':0,close=q==3?']':open;
    if(open)out[n++]=open;
    for(;*value;value++){REQUIRE(n+4<cap);out[n++]=*value;if(open&&*value==close)out[n++]=close;}
    if(close)out[n++]=close;
    out[n]=0;
}
static int mysql(void){return dialect==SQLPARSER_DIALECT_MYSQL||dialect==SQLPARSER_DIALECT_VASTBASE_MYSQL||dialect==SQLPARSER_DIALECT_KINGBASE_MYSQL;}
static int server(void){return dialect==SQLPARSER_DIALECT_SQLSERVER||dialect==SQLPARSER_DIALECT_VASTBASE_SQLSERVER||dialect==SQLPARSER_DIALECT_KINGBASE_SQLSERVER;}
static void order_selector(sqlparser_handle_t*h,char out[80])
{
    size_t i,n;sqlparser_error_t e={0};ok(sqlparser_statement_clause_count(h,0,&n,&e),&e);
    for(i=0;i<n;i++){sqlparser_clause_view_t v;ok(sqlparser_statement_clause(h,0,i,&v,&e),&e);
        if(v.kind==SQLPARSER_CLAUSE_KIND_ORDER_BY){snprintf(out,80,"stmt[0].clause[%zu]",i);return;}}
    REQUIRE(0);
}
static void matrix(void)
{
    int q,as;char label[80],token[300],target[500],sql[1000],raw[300],order[80];
    static const char*values[]={"plain_new","CaseSensitive","select","café_列","two words","em\"bed","em\140bed","em]bed","fresh_alias"};
    for(dialect=0;dialect<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;dialect++)for(q=0;q<4;q++){
        /* MySQL modes treat double quotes as string syntax, not identifier
         * delimiters. The supported native quoted-identifier case is backtick. */
        if((q==1&&mysql())||(q==2&&!mysql())||(q==3&&!server()))continue;
        for(as=0;as<2;as++){fixture_t original,generated;size_t i;
            snprintf(label,sizeof(label),"dialect-matrix-q%d-as%d",q,as);case_name=label;phase="parse";
            quote_name(token,sizeof(token),"fresh_alias",q);snprintf(target,sizeof(target),"c%s%s, protected AS keep_alias",as?" AS ":" ",token);
            snprintf(sql,sizeof(sql),"SELECT %s FROM keep_table WHERE b = 1 ORDER BY protected",target);
            original=fixture(sql,"original");generated=fixture("SELECT a AS old_alias, protected AS keep_alias FROM keep_table WHERE b = 1 ORDER BY protected","generated");
            {char*s=checkpoint(&original,"generation-zero");if(strcmp(s,sql))fail("generation-zero-bytes",sql,s);sqlparser_string_free(s);}
            replace(generated.h,"stmt[0].select_targets[0]",target);expect(&generated,0,0,"fresh_alias");check(&generated,"generated-installed");
            pair_rename(&original,&generated,"fresh_alias","unchanged-name");
            for(i=0;i<COUNT(values);i++)pair_rename(&original,&generated,values[i],values[i]);
            quote_name(raw,sizeof(raw),"Raw Alias",q?q:(mysql()?2:server()?3:1));
            raw_alias(&original,0,0,"Raw Alias",raw);raw_alias(&generated,0,0,"Raw Alias",raw);pair_check(&original,&generated,"explicit-raw-spelling");
            {char*s=checkpoint(&generated,"verify-explicit-spelling");if(!strstr(s,raw))fail("explicit-raw-spelling",raw,s);sqlparser_string_free(s);}
            pair_rename(&original,&generated,"Raw Alias","raw-noop-keeps-spelling");
            {
                const char*raw_values[]={"CaseSensitive","select","café_列",mysql()?"em\140bed":server()?"em]bed":"em\"bed"};
                for(i=0;i<COUNT(raw_values);i++){
                    quote_name(raw,sizeof(raw),raw_values[i],q?q:(mysql()?2:server()?3:1));
                    raw_alias(&original,0,0,raw_values[i],raw);raw_alias(&generated,0,0,raw_values[i],raw);
                    pair_check(&original,&generated,"raw-case-keyword-unicode-escape");
                    {char*s=checkpoint(&generated,"verify-raw-escape");if(!strstr(s,raw))fail("explicit-raw-spelling",raw,s);sqlparser_string_free(s);}
                }
            }
            pair_rename(&original,&generated,"after_raw","semantic-after-raw");
            replace(original.h,"stmt[0].where[0]","e = 2");replace(generated.h,"stmt[0].where[0]","e = 2");pair_check(&original,&generated,"after-where");
            order_selector(original.h,order);replace(original.h,order,"protected DESC");order_selector(generated.h,order);replace(generated.h,order,"protected DESC");pair_check(&original,&generated,"after-order");
            replace(original.h,"stmt[0].select_target[0][1]","next_col AS next_alias");replace(generated.h,"stmt[0].select_target[0][1]","next_col AS next_alias");
            expect(&original,0,1,"next_alias");expect(&generated,0,1,"next_alias");pair_check(&original,&generated,"after-other-select");
            pair_rename(&original,&generated,"fresh_alias","undo-after-structure");close_fixture(&original);close_fixture(&generated);
        }
    }
}
static void source_spelling_cases(void)
{
    for(dialect=0;dialect<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;dialect++){
        const char*sql="/* heading */ SeLeCt  c AS fresh_alias,\n protected AS keep_alias FROM keep_table WHERE b = 1; /* tail */";
        fixture_t a,b;char*out;
        case_name="comments-case-and-source";phase="parse";
        a=fixture(sql,"original");b=fixture("/* heading */ SeLeCt  a AS old_alias,\n protected AS keep_alias FROM keep_table WHERE b = 1; /* tail */","generated");
        out=checkpoint(&a,"generation-zero");if(strcmp(out,sql))fail("generation-zero-bytes",sql,out);sqlparser_string_free(out);
        replace(b.h,"stmt[0].select_target[0][0]","c AS fresh_alias");expect(&b,0,0,"fresh_alias");
        pair_rename(&a,&b,"after_select","semantic-name");
        replace(a.h,"stmt[0].where[0]","e = 2");replace(b.h,"stmt[0].where[0]","e = 2");pair_check(&a,&b,"after-where");
        close_fixture(&a);close_fixture(&b);
    }
}
static size_t owner_index(sqlparser_handle_t*h,const char*owner,size_t ordinal)
{
    sqlparser_error_t e={0};size_t n,i;ok(sqlparser_statement_name_count(h,0,&n,&e),&e);
    for(i=0;i<n;i++){sqlparser_name_view_t v;ok(sqlparser_statement_name(h,0,i,&v,&e),&e);
        if(!strcmp(v.owner_type,owner)&&ordinal--==0)return i;}
    REQUIRE(0);return SIZE_MAX;
}
static void generated_relation_aliases(void)
{
    int quoted;
    for(dialect=0;dialect<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;dialect++)for(quoted=0;quoted<2;quoted++){
        fixture_t a,b;size_t i;sqlparser_error_t e={0};char token[100],name_selector[80],sql[600],initial[600],target[300];
        const char*values[]={"changed_alias","CaseSensitive","select","dup"};
        case_name=quoted?"nested-relation-alias-quoted":"nested-relation-alias-identity";phase="parse";
        quote_name(token,sizeof(token),"dup",quoted?(mysql()?2:server()?3:1):0);
        snprintf(target,sizeof(target),"(SELECT dup.x AS inner_alias FROM inner_table AS %s) AS outer_alias",token);
        snprintf(sql,sizeof(sql),"SELECT %s FROM outer_table AS dup WHERE k = 1",target);
        snprintf(initial,sizeof(initial),"SELECT (SELECT dup.x AS inner_alias FROM old_inner AS %s) AS outer_alias FROM outer_table AS dup WHERE k = 1",token);
        a=fixture(sql,"original");b=fixture(initial,"generated");
        replace(b.h,"stmt[0].select_target[0][0]",target);
        for(i=0;i<COUNT(values);i++){
            sqlparser_name_view_t untouched;
            phase=values[i];ok(sqlparser_statement_set_name(a.h,0,owner_index(a.h,"Alias",0),values[i],&e),&e);setters++;
            ok(sqlparser_statement_set_name(b.h,0,owner_index(b.h,"Alias",0),values[i],&e),&e);setters++;
            ok(sqlparser_statement_name(b.h,0,owner_index(b.h,"Alias",1),&untouched,&e),&e);
            if(strcmp(untouched.value,"dup"))fail("distinct-relation-alias","dup",untouched.value);
            ok(sqlparser_statement_name(b.h,0,owner_index(b.h,"ColumnRef",0),&untouched,&e),&e);
            if(strcmp(untouched.value,"dup"))fail("unrelated-qualified-reference","dup",untouched.value);
            pair_check(&a,&b,values[i]);
        }
        quote_name(token,sizeof(token),"Raw Relation",mysql()?2:server()?3:1);
        snprintf(name_selector,sizeof(name_selector),"stmt[0].name[%zu]",owner_index(a.h,"Alias",0));replace(a.h,name_selector,token);
        snprintf(name_selector,sizeof(name_selector),"stmt[0].name[%zu]",owner_index(b.h,"Alias",0));replace(b.h,name_selector,token);
        pair_check(&a,&b,"raw-relation-alias");
        replace(a.h,"stmt[0].where[0]","after_where = 2");replace(b.h,"stmt[0].where[0]","after_where = 2");pair_check(&a,&b,"relation-alias-after-where");
        {sqlparser_name_view_t v;ok(sqlparser_statement_name(b.h,0,owner_index(b.h,"Alias",0),&v,&e),&e);
            if(strcmp(v.value,"Raw Relation"))fail("retained-relation-alias","Raw Relation",v.value);}
        close_fixture(&a);close_fixture(&b);
    }
}
static void duplicate_cases(void)
{
    static const char*inputs[]={
        "SELECT dup AS dup, protected AS dup FROM dup WHERE dup = 1 ORDER BY protected",
        "SELECT a AS dup FROM keep_table; SELECT b AS dup FROM other_table",
        "WITH dup AS (SELECT a AS dup, b AS dup FROM inner_table) SELECT c AS dup FROM dup",
        "SELECT a AS dup, (SELECT b AS dup FROM inner_table) AS dup FROM keep_table WHERE a = 1"};
    size_t k;
    for(dialect=0;dialect<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;dialect++)for(k=0;k<COUNT(inputs);k++){
        fixture_t f;char label[80],value[80],target[250];size_t i,lists;
        snprintf(label,sizeof(label),"duplicate-owner-identity-%zu",k);case_name=label;phase="parse";f=fixture(inputs[k],"mixed");
        /* Mark existing target lists generated with their public SQL, preserving
         * shape without assuming root/nested alias traversal order. */
        {sqlparser_error_t e={0};ok(sqlparser_select_target_list_count(f.h,0,&lists,&e),&e);
         for(i=0;i<lists;i++){size_t n,j;char all[1500]={0};ok(sqlparser_select_target_count(f.h,0,i,&n,&e),&e);
             for(j=0;j<n;j++){char*t=NULL;ok(sqlparser_select_target_sql(f.h,0,i,j,&t,&e),&e);REQUIRE(strlen(all)+strlen(t)+3<sizeof(all));if(j)strcat(all,", ");strcat(all,t);sqlparser_string_free(t);}
             snprintf(target,sizeof(target),"stmt[0].select_targets[%zu]",i);replace(f.h,target,all);}}
        check(&f,"generated-duplicates");
        for(i=f.count;i>0;i--){alias_t*a=f.aliases+i-1;snprintf(value,sizeof(value),"unique_alias_%zu",i-1);phase="reverse-rename";rename_alias(&f,a->statement,a->occurrence,value);check(&f,"reverse-rename");}
        for(i=0;i<f.count;i++){alias_t*a=f.aliases+i;phase="forward-undo";rename_alias(&f,a->statement,a->occurrence,"dup");check(&f,"forward-undo");}
        close_fixture(&f);
    }
}
static void batch_lifetime_cases(void)
{
    for(dialect=0;dialect<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;dialect++){
        fixture_t f,clone;sqlparser_error_t e={0};sqlparser_patch_t p[4];char a[80],b[80];
        sqlparser_name_view_t borrowed;sqlparser_query_graph_view_t graph;size_t n,index;char*owned,*saved;
        case_name="same-batch-source-and-lifetime";phase="parse";
        f=fixture("SELECT a AS old_alias, b AS keep_alias FROM keep_table WHERE c = 1 ORDER BY b","generated");
        replace(f.h,"stmt[0].select_targets[0]","x AS fresh_alias, b AS keep_alias");expect(&f,0,0,"fresh_alias");
        selector(&f,0,0,a);
        p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="before_structure"};
        p[1]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].where[0]",.sql="after_where = 2"};
        p[2]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="after_structure"};
        apply(f.h,p,3);expect(&f,0,0,"after_structure");check(&f,"name-where-name-batch");
        /* Source reads the alias written earlier in this batch, not an initial
         * AST snapshot. Copies must have independent subsequent name binding. */
        selector(&f,0,0,a);p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="copied_alias"};
        p[1]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_target[0][1]",.source_selector="stmt[0].select_target[0][0]"};
        apply(f.h,p,2);expect(&f,0,0,"copied_alias");expect(&f,0,1,"copied_alias");check(&f,"rename-and-source-copy");
        rename_alias(&f,0,0,"only_first");check(&f,"copied-owner-independent");
        ok(sqlparser_statement_name(f.h,0,alias_index(f.h,0,1),&borrowed,&e),&e);
        rename_alias(&f,0,0,borrowed.value);check(&f,"borrowed-setter-input");
        selector(&f,0,0,a);selector(&f,0,1,b);ok(sqlparser_statement_name(f.h,0,alias_index(f.h,0,0),&borrowed,&e),&e);
        p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=a,.default_sql="batch_first"};
        p[1]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector=b,.default_sql=borrowed.value};
        apply(f.h,p,2);expect(&f,0,0,"batch_first");expect(&f,0,1,"copied_alias");check(&f,"borrowed-batch-input");
        ok(sqlparser_statement_query_graph(f.h,0,&graph,&e),&e);apply(f.h,NULL,0);ok(sqlparser_query_graph_expression_count(&graph,&n,&e),&e);
        rename_alias(&f,0,0,"batch_first");REQUIRE(sqlparser_query_graph_expression_count(&graph,&n,&e)==SQLPARSER_STATUS_INVALID_ARGUMENT);check(&f,"noop-invalidates-borrowed-graph");
        owned=checkpoint(&f,"before-clone");saved=copy(owned);memset(&clone,0,sizeof(clone));clone.variant="clone";
        memset(&e,0,sizeof(e));ok(sqlparser_handle_clone(f.h,&clone.h,&e),&e);clones++;
        clone.count=f.count;for(n=0;n<f.count;n++){clone.aliases[n]=f.aliases[n];clone.aliases[n].value=copy(f.aliases[n].value);}
        index=alias_index(f.h,0,0);close_fixture(&f);
        ok(sqlparser_statement_set_name(clone.h,0,index,"clone_alias",&e),&e);setters++;expect(&clone,0,0,"clone_alias");
        check(&clone,"clone-after-source-destroy");REQUIRE(!strcmp(owned,saved));free(saved);sqlparser_string_free(owned);
        replace(clone.h,"stmt[0].where[0]","clone_where = 3");check(&clone,"clone-after-where");close_fixture(&clone);
    }
}
static void workaround_cases(void)
{
    int q;
    for(dialect=0;dialect<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;dialect++)for(q=0;q<2;q++){
        fixture_t f;char spelling[256],label[80];const char*value=q?"Case café_列":"after_select";
        snprintf(label,sizeof(label),"raw-workaround-%d",q);case_name=label;phase="parse";
        f=fixture("SELECT a AS old_alias FROM keep_table WHERE b = 1 ORDER BY b","raw-workaround");
        replace(f.h,"stmt[0].select_targets[0]","c AS fresh_alias");expect(&f,0,0,"fresh_alias");
        quote_name(spelling,sizeof(spelling),value,q?(mysql()?2:server()?3:1):0);raw_alias(&f,0,0,value,spelling);check(&f,"raw-patch");
        replace(f.h,"stmt[0].where[0]","e = 2");check(&f,"raw-followed-by-where");
        {char order[80];order_selector(f.h,order);replace(f.h,order,"e DESC");}check(&f,"raw-followed-by-order");
        rename_alias(&f,0,0,value);check(&f,"raw-followed-by-noop");close_fixture(&f);
    }
}
static fixture_t clone_fixture(fixture_t*f)
{
    fixture_t c={0};size_t i;sqlparser_error_t e={0};c.variant=f->variant;c.count=f->count;
    ok(sqlparser_handle_clone(f->h,&c.h,&e),&e);clones++;
    for(i=0;i<f->count;i++){c.aliases[i]=f->aliases[i];c.aliases[i].value=copy(f->aliases[i].value);}
    return c;
}
static void quoted_copy_clone_undo(void)
{
    for(dialect=0;dialect<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;dialect++){
        fixture_t a,b,ca,cb;char token[100],target[200],sql[600];size_t ai,bi;
        sqlparser_patch_t p;sqlparser_error_t e={0};
        case_name="quoted-copy-clone-undo";phase="parse";
        quote_name(token,sizeof(token),"fresh_alias",mysql()?2:server()?3:1);
        snprintf(target,sizeof(target),"x AS %s",token);
        snprintf(sql,sizeof(sql),"SELECT %s, %s FROM keep_table WHERE b = 1",target,target);
        a=fixture(sql,"original");b=fixture("SELECT a AS old_alias, b AS keep_alias FROM keep_table WHERE b = 1","generated");
        replace(b.h,"stmt[0].select_target[0][0]",target);expect(&b,0,0,"fresh_alias");
        p=(sqlparser_patch_t){.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_target[0][1]",.source_selector="stmt[0].select_target[0][0]"};
        apply(b.h,&p,1);expect(&b,0,1,"fresh_alias");pair_check(&a,&b,"quoted-source-copy");
        pair_rename(&a,&b,"CaseSensitive","rename-only-first");
        ai=alias_index(a.h,0,0);bi=alias_index(b.h,0,0);ca=clone_fixture(&a);cb=clone_fixture(&b);
        close_fixture(&a);close_fixture(&b);
        /* No clone read occurs before these writes. Undo must retain the initial
         * quote token even after cloned mutation metadata outlives its source. */
        ok(sqlparser_statement_set_name(ca.h,0,ai,"fresh_alias",&e),&e);setters++;expect(&ca,0,0,"fresh_alias");
        ok(sqlparser_statement_set_name(cb.h,0,bi,"fresh_alias",&e),&e);setters++;expect(&cb,0,0,"fresh_alias");
        pair_check(&ca,&cb,"quoted-undo-on-cold-clone");
        replace(ca.h,"stmt[0].where[0]","after_clone = 2");replace(cb.h,"stmt[0].where[0]","after_clone = 2");
        pair_check(&ca,&cb,"quoted-clone-after-where");close_fixture(&ca);close_fixture(&cb);
    }
}
static void error_cases(void)
{
    fixture_t f;sqlparser_parse_options_t o;sqlparser_error_t e={0};sqlparser_status_t s;
    char huge[256],*out=NULL,selector_text[80];size_t index;
    dialect=SQLPARSER_DIALECT_POSTGRESQL;case_name="ordinary-errors-and-limits";phase="parse";
    f=fixture("SELECT a AS old_alias FROM t WHERE b = 1","generated");replace(f.h,"stmt[0].select_targets[0]","c AS fresh_alias");expect(&f,0,0,"fresh_alias");index=alias_index(f.h,0,0);
    s=sqlparser_statement_set_name(f.h,0,index,"",&e);REQUIRE(s==SQLPARSER_STATUS_INVALID_ARGUMENT);errors++;close_fixture(&f);
    f=fixture("SELECT a AS old_alias FROM t","generated");replace(f.h,"stmt[0].select_targets[0]","c AS fresh_alias");expect(&f,0,0,"fresh_alias");
    selector(&f,0,0,selector_text);
    {sqlparser_patch_t p[2]={{.op=SQLPARSER_PATCH_REPLACE,.selector=selector_text,.default_sql="successful_first"},{.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].relation[0]",.sql="a + b"}};
     sqlparser_patch_list_t list={p,2};s=sqlparser_apply_patch(f.h,&list,&e);REQUIRE(s!=SQLPARSER_STATUS_OK);errors++;}
    close_fixture(&f);
    memset(huge,'x',sizeof(huge)-1);huge[sizeof(huge)-1]=0;
    sqlparser_parse_options_default(&o);o.limits.max_output_bytes=100;
    memset(&f,0,sizeof(f));f.variant="output-limit";f.h=parse_options("SELECT a AS old_alias FROM t",&o);capture(&f);
    replace(f.h,"stmt[0].select_targets[0]","c AS fresh_alias");expect(&f,0,0,"fresh_alias");rename_alias(&f,0,0,huge);
    s=sqlparser_deparse(f.h,&out,&e);
    if(s!=SQLPARSER_STATUS_RESOURCE_LIMIT)fail("generated-output-limit","resource limit",out?out:e.message);
    if(dump)printf("LIMIT|output|status=%d|%s\n",s,e.message);
    if(out)sqlparser_string_free(out);
    errors++;close_fixture(&f);
    sqlparser_parse_options_default(&o);o.limits.max_output_bytes=strlen("SELECT c AS exact_alias FROM t");
    memset(&f,0,sizeof(f));f.variant="exact-limit";f.h=parse_options("SELECT a AS old_alias FROM t",&o);capture(&f);
    replace(f.h,"stmt[0].select_targets[0]","c AS fresh_alias");expect(&f,0,0,"fresh_alias");rename_alias(&f,0,0,"exact_alias");check(&f,"exact-output-boundary");close_fixture(&f);
    sqlparser_parse_options_default(&o);o.limits.max_sql_bytes=80;
    memset(&f,0,sizeof(f));f.variant="input-limit";f.h=parse_options("SELECT a AS old_alias FROM t",&o);capture(&f);
    replace(f.h,"stmt[0].select_targets[0]","c AS fresh_alias");expect(&f,0,0,"fresh_alias");rename_alias(&f,0,0,huge);check(&f,"semantic-name-not-sql-input");close_fixture(&f);
}
int main(int argc,char**argv)
{
    int i;for(i=1;i<argc;i++)if(!strcmp(argv[i],"--dump"))dump=1;else REQUIRE(0);
    matrix();source_spelling_cases();generated_relation_aliases();duplicate_cases();batch_lifetime_cases();quoted_copy_clone_undo();workaround_cases();error_cases();
    fprintf(stderr,"%s checks=%zu semantic_failures=%zu pair_differences=%zu setters=%zu patches=%zu clones=%zu error_cases=%zu fixtures=%zu fragments=%zu view_targets=%zu getter_errors=%zu\n",(failures||differences)?"FAIL":"PASS",checks,failures,differences,setters,patches,clones,errors,cases,fragments,views,getter_errors);
    return (failures||differences)?1:0;
}
