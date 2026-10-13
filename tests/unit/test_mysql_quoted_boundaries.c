/* Regression coverage for mysql quoted boundaries. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"

#define CAP 8192
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_NAMES 32
#define MAX_TARGETS 8
static sqlparser_dialect_t dialect;
static const char *case_name = "startup", *path = "startup", *phase = "startup";
static const char *filter_case, *filter_path;
static int filter_dialect = -1, dump, national_diagnostic;
static size_t checks, failures, fixtures, not_admitted, reads, mutations, copies, reparses;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FATAL line=%d expression=%s\n", __LINE__, #x); exit(2); } } while (0)

typedef struct {
    const char *aliases[MAX_NAMES], *fields[MAX_NAMES], *strings[MAX_NAMES];
    size_t alias_count, field_count, string_count, binds;
} oracle_t;
typedef struct { size_t list, index; char text[CAP]; oracle_t oracle; } target_t;
typedef struct {
    char sql[CAP];
    oracle_t oracle;
    target_t targets[MAX_TARGETS]; size_t target_count;
} fixture_t;

static void format(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap; int n; va_start(ap, fmt); n = vsnprintf(out, cap, fmt, ap); va_end(ap);
    REQUIRE(n >= 0 && (size_t)n < cap);
}
static int mysql(void)
{ return dialect == SQLPARSER_DIALECT_MYSQL || dialect == SQLPARSER_DIALECT_VASTBASE_MYSQL || dialect == SQLPARSER_DIALECT_KINGBASE_MYSQL; }
static void quote(char *out, size_t cap, const char *name)
{
    char q = mysql() ? '`' : '"'; size_t n = 0;
    out[n++] = q;
    for (; *name; name++) { REQUIRE(n + 4 < cap); out[n++] = *name; if (*name == q) out[n++] = q; }
    out[n++] = q; out[n] = 0;
}
static void fail(const char *what, const char *expected, const char *actual)
{
    failures++;
    printf("FAIL|%d|%s|%s|%s|%s|expected=%s|actual=%s\n", dialect, case_name, path, phase, what, expected ? expected : "(null)", actual ? actual : "(null)");
}
static int success(sqlparser_status_t status, const sqlparser_error_t *error, const char *what)
{
    checks++;
    if (status != SQLPARSER_STATUS_OK) { char detail[512]; format(detail, sizeof(detail), "status=%d:%s", status, error->message); fail(what, "status=0", detail); return 0; }
    return 1;
}
static void equal(const char *what, const char *expected, const char *actual)
{ checks++; if (!actual || strcmp(expected, actual)) fail(what, expected, actual); }
static void number(const char *what, size_t expected, size_t actual)
{ char a[40], b[40]; format(a, sizeof(a), "%zu", expected); format(b, sizeof(b), "%zu", actual); equal(what, a, b); }
static void add_alias(oracle_t *o, const char *s) { REQUIRE(o->alias_count < MAX_NAMES); o->aliases[o->alias_count++] = s; }
static void add_field(oracle_t *o, const char *s) { REQUIRE(o->field_count < MAX_NAMES); o->fields[o->field_count++] = s; }
static void add_string(oracle_t *o, const char *s) { REQUIRE(o->string_count < MAX_NAMES); o->strings[o->string_count++] = s; }
static void merge(oracle_t *o, const oracle_t *from)
{
    size_t i; for (i=0;i<from->alias_count;i++) add_alias(o,from->aliases[i]);
    for(i=0;i<from->field_count;i++) add_field(o,from->fields[i]);
    for(i=0;i<from->string_count;i++) add_string(o,from->strings[i]);
    o->binds += from->binds;
}
static sqlparser_handle_t *parse_options(const char *sql, size_t output_limit, int initial)
{
    sqlparser_parse_options_t options; sqlparser_error_t error = {0}; sqlparser_handle_t *h = NULL; sqlparser_status_t status;
    sqlparser_parse_options_default(&options); options.dialect=dialect;
    if(output_limit) options.limits.max_output_bytes=output_limit;
    status=sqlparser_parse_with_options(sql,&options,&h,&error);
    if(status != SQLPARSER_STATUS_OK) {
        if(initial) { not_admitted++; printf("NOT_ADMITTED|%d|%s|%s|%d|%s|%s\n",dialect,case_name,path,status,error.message,sql); }
        success(status,&error,initial?"input-parse":"output-reparse"); sqlparser_handle_destroy(h); return NULL;
    }
    REQUIRE(h); if(!initial) reparses++; return h;
}
static void match_multiset(const char *what, const char *const *expected, size_t en, const char *const *actual, size_t an)
{
    size_t i,j; unsigned char used[MAX_NAMES]={0}; number(what,en,an);
    for(i=0;i<en;i++) { for(j=0;j<an;j++) if(!used[j] && !strcmp(expected[i],actual[j])) break;
        if(j==an) fail(what,expected[i],"missing intended value"); else used[j]=1; }
}
/* All semantic expectations originate in explicit fixture strings. No output
 * produced by either library is used to infer the intended identifier value. */
static int semantics(sqlparser_handle_t *h, const oracle_t *o)
{
    sqlparser_error_t error={0}; size_t n,i,na=0,nf=0,ns=0;
    const char *aliases[MAX_NAMES],*fields[MAX_NAMES],*strings[MAX_NAMES];
    sqlparser_bind_occurrence_view_t binds;
    if(!success(sqlparser_statement_name_count(h,0,&n,&error),&error,"name-count")) return 0;
    for(i=0;i<n;i++) { sqlparser_name_view_t v,again;
        if(!success(sqlparser_statement_name(h,0,i,&v,&error),&error,"name-read"))return 0;
        if(!success(sqlparser_statement_name(h,0,i,&again,&error),&error,"name-read-again"))return 0;
        equal("name-repeat",v.value,again.value);
        if(!strcmp(v.owner_type,"ResTarget") && !strcmp(v.field_name,"name")) { REQUIRE(na<MAX_NAMES); aliases[na++]=v.value; }
        if(!strcmp(v.owner_type,"ColumnRef") && !strcmp(v.field_name,"fields")) { REQUIRE(nf<MAX_NAMES); fields[nf++]=v.value; }
        if(dump) printf("NAME|%d|%s|%s|%s|%s|%s|%s\n",dialect,case_name,path,phase,v.owner_type,v.field_name,v.value);
    }
    match_multiset("alias-names",o->aliases,o->alias_count,aliases,na);
    match_multiset("field-names",o->fields,o->field_count,fields,nf);
    if(!success(sqlparser_statement_literal_count(h,0,&n,&error),&error,"literal-count"))return 0;
    for(i=0;i<n;i++) { sqlparser_literal_view_t v;
        if(!success(sqlparser_statement_literal(h,0,i,&v,&error),&error,"literal-read"))return 0;
        if(v.kind==SQLPARSER_LITERAL_KIND_STRING) { REQUIRE(ns<MAX_NAMES); strings[ns++]=v.string_value; }
    }
    match_multiset("string-values",o->strings,o->string_count,strings,ns);
    if(!success(sqlparser_handle_bind_occurrences(h,&binds,&error),&error,"bind-list"))return 0;
    number("bind-count",o->binds,binds.count);
    for(i=0;i<binds.count;i++) { sqlparser_bind_occurrence_t b;
        if(!success(sqlparser_bind_occurrence_at(&binds,i,&b,&error),&error,"bind-item"))return 0;
        number("bind-occurrence-order",i+1,b.position);
        if(mysql()) { number("bind-kind",SQLPARSER_BIND_KIND_POSITIONAL,b.kind); equal("bind-sql","?",b.sql); checks++;if(b.key)fail("bind-key","NULL",b.key); }
        if(dump)printf("BIND|%d|%s|%s|%s|%zu|%d|%s|%s\n",dialect,case_name,path,phase,b.position,b.kind,b.key?b.key:"NULL",b.sql);
    }
    return 1;
}
static int roundtrip(const char *sql,const oracle_t *o)
{
    sqlparser_handle_t *h=parse_options(sql,0,0); int result;
    if(!h)return 0;
    result=semantics(h,o); sqlparser_handle_destroy(h); return result;
}
static int render(sqlparser_handle_t *h, const fixture_t *f, int kind)
{
    sqlparser_error_t error={0}; char *out=NULL,*again=NULL,*saved=NULL,wrapped[CAP+16]; size_t i,count;
    phase="live-semantics"; if(!semantics(h,&f->oracle))return 0;
    for(i=0;i<(kind?f->target_count:1);i++) {
        const target_t *t=kind?&f->targets[i]:NULL; sqlparser_status_t status;
        const char *expected=kind?t->text:f->sql;
        phase=kind?"fragment-render":"whole-render";
        if(kind==1) status=sqlparser_select_target_sql(h,0,t->list,t->index,&out,&error);
        else if(kind==2) { sqlparser_selector_t selector; char selector_text[100];
            format(selector_text,sizeof(selector_text),"stmt[0].select_target[%zu][%zu]",t->list,t->index);
            if(!success(sqlparser_selector_parse(selector_text,&selector,&error),&error,"selector-parse"))return 0;
            status=sqlparser_selector_select_target_sql(h,&selector,&out,&error);
        } else status=sqlparser_deparse(h,&out,&error);
        if(!success(status,&error,kind?"target-getter":"whole-getter")) { sqlparser_string_free(out);return 0; }
        reads++; equal(kind?"exact-target":"exact-whole",expected,out);
        if(dump)printf("SQL|%d|%s|%s|%s|%zu|%s\n",dialect,case_name,path,phase,i,out);
        saved=strdup(out);REQUIRE(saved);
        /* Repetition exercises cached state without mutation. */
        if(kind) status=sqlparser_select_target_sql(h,0,t->list,t->index,&again,&error);
        else status=sqlparser_deparse(h,&again,&error);
        if(!success(status,&error,"repeat-getter")) { free(saved);sqlparser_string_free(out);sqlparser_string_free(again);return 0; }
        reads++; equal("repeat-output",out,again);sqlparser_string_free(again);again=NULL;
        phase="reparse-semantics";
        if(kind) {format(wrapped,sizeof(wrapped),"SELECT %s",out);roundtrip(wrapped,&t->oracle);}
        else roundtrip(out,&f->oracle);
        phase="retained-output";equal("owned-output-after-other-reads",saved,out);
        free(saved);sqlparser_string_free(out);out=NULL;
    }
    phase="read-counts";
    if(!success(sqlparser_select_target_list_count(h,0,&count,&error),&error,"target-list-count"))return 0;
    number("target-list-count",(!strcmp(case_name,"nested-duplicate"))?2:1,count);
    return 1;
}
static size_t find_alias(sqlparser_handle_t *h,const char *name)
{
    size_t n,i;sqlparser_error_t e={0};
    if(!success(sqlparser_statement_name_count(h,0,&n,&e),&e,"rename-name-count"))return SIZE_MAX;
    for(i=0;i<n;i++){sqlparser_name_view_t v;if(!success(sqlparser_statement_name(h,0,i,&v,&e),&e,"rename-name-read"))return SIZE_MAX;
        if(!strcmp(v.owner_type,"ResTarget")&&!strcmp(v.field_name,"name")&&!strcmp(v.value,name))return i;}
    fail("rename-source","old_alias","missing");return SIZE_MAX;
}
/* These exact 15 fixture paths are independently retained in a strict,
 * separately failing diagnostic mode. No defective spelling is accepted by
 * either mode. All other national-literal paths remain in the main suite. */
static int national_diagnostic_path(const char *p)
{
    int oracle_family=dialect==SQLPARSER_DIALECT_ORACLE||dialect==SQLPARSER_DIALECT_DAMENG||dialect==SQLPARSER_DIALECT_VASTBASE_ORACLE||dialect==SQLPARSER_DIALECT_KINGBASE_ORACLE;
    int sqlserver_family=dialect==SQLPARSER_DIALECT_SQLSERVER||dialect==SQLPARSER_DIALECT_VASTBASE_SQLSERVER||dialect==SQLPARSER_DIALECT_KINGBASE_SQLSERVER;
    if(strcmp(case_name,"adjacent-national-literals"))return 0;
    return (oracle_family&&(!strcmp(p,"direct")||!strcmp(p,"selector")||!strcmp(p,"relation-mutation")))||
        (sqlserver_family&&!strcmp(p,"relation-mutation"));
}
static int wanted(const char *p)
{
    return national_diagnostic_path(p)==national_diagnostic &&
        (!filter_case||strstr(case_name,filter_case)) &&
        (!filter_path||!strcmp(p,filter_path)) &&
        (filter_dialect<0||filter_dialect==(int)dialect);
}
static void ordinary(const fixture_t *f)
{
    static const char *paths[]={"whole","direct","selector","relation-mutation"}; size_t p;
    for(p=0;p<COUNT(paths);p++) { sqlparser_handle_t *h; sqlparser_error_t e={0};fixture_t changed;
        path=paths[p];if(!wanted(path))continue;fixtures++;phase="input-parse";h=parse_options(f->sql,0,1);if(!h)continue;
        if(p==3) { changed=*f; const char *suffix=strstr(f->sql," FROM t");
            /* All ordinary generated fixtures have a final FROM t. */
            REQUIRE(suffix && !strcmp(suffix," FROM t"));
            format(changed.sql,sizeof(changed.sql),"%.*s FROM renamed_table",(int)(suffix-f->sql),f->sql);
            phase="relation-mutation";mutations++;
            if(!success(sqlparser_statement_set_relation_name(h,0,0,NULL,"renamed_table",&e),&e,"relation-setter")){sqlparser_handle_destroy(h);continue;}
            render(h,&changed,1); /* Check identifier fragments after unrelated edit. */
            /* Independent whole getter on a fresh mutation owner avoids reads
             * after any preceding deparse error. */
            sqlparser_handle_destroy(h);h=parse_options(f->sql,0,1);if(!h)continue;
            if(success(sqlparser_statement_set_relation_name(h,0,0,NULL,"renamed_table",&e),&e,"relation-setter"))render(h,&changed,0);
        } else render(h,f,(int)p);
        sqlparser_handle_destroy(h);
    }
}
static void single(const char *name,int field)
{
    fixture_t f={0};char token[1024];quote(token,sizeof(token),name);
    format(f.targets[0].text,sizeof(f.targets[0].text),field?"%s AS keep_alias":"c AS %s",token);
    add_alias(&f.targets[0].oracle,field?"keep_alias":name);add_field(&f.targets[0].oracle,field?name:"c");
    f.target_count=1;merge(&f.oracle,&f.targets[0].oracle);format(f.sql,sizeof(f.sql),"SELECT %s FROM t",f.targets[0].text);ordinary(&f);
}
static void rename_and_replace(const char *name)
{
    fixture_t f={0};char token[1024];int kind;
    quote(token,sizeof(token),name);format(f.targets[0].text,sizeof(f.targets[0].text),"c AS %s",token);
    f.target_count=1;add_alias(&f.targets[0].oracle,name);add_field(&f.targets[0].oracle,"c");merge(&f.oracle,&f.targets[0].oracle);
    format(f.sql,sizeof(f.sql),"SELECT %s FROM t",f.targets[0].text);
    for(kind=0;kind<3;kind++){sqlparser_handle_t*h;sqlparser_error_t e={0};sqlparser_status_t s;char*owned=NULL,*snapshot=NULL;size_t index;
        path=kind==0?"alias-setter":kind==1?"target-setter":"patch-replace";if(!wanted(path))continue;fixtures++;
        h=parse_options("SELECT c AS old_alias FROM t",0,1);if(!h)continue;
        phase="output-before-mutation";
        if(!success(sqlparser_deparse(h,&owned,&e),&e,"owned-before")){sqlparser_handle_destroy(h);continue;}
        snapshot=strdup(owned);REQUIRE(snapshot);phase="mutation";mutations++;
        if(kind==0){index=find_alias(h,"old_alias");if(index==SIZE_MAX){free(snapshot);sqlparser_string_free(owned);sqlparser_handle_destroy(h);continue;}s=sqlparser_statement_set_name(h,0,index,name,&e);}
        else if(kind==1)s=sqlparser_select_set_target_sql(h,0,0,0,f.targets[0].text,&e);
        else{sqlparser_patch_t patch={.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_target[0][0]",.sql=f.targets[0].text};sqlparser_patch_list_t list={&patch,1};s=sqlparser_apply_patch(h,&list,&e);}
        if(success(s,&e,"ordinary-mutation")) { /* Separate owners for whole and getter paths are already covered elsewhere. */
            if(render(h,&f,0))render(h,&f,2);
        }
        sqlparser_handle_destroy(h);phase="destroyed-owner";equal("owned-sql-lifetime",snapshot,owned);free(snapshot);sqlparser_string_free(owned);
    }
}
static void mixed(const char *name,int national,int binds)
{
    fixture_t f={0};char token[1024];const char*value="lit $1 $2 ? -- # /* x */ \" ` ; , ()";quote(token,sizeof(token),name);
    format(f.targets[0].text,sizeof(f.targets[0].text),binds?"? AS first_bind":"%s'lit $1 $2 ? -- # /* x */ \" ` ; , ()' AS first_literal",national?"N":"");
    add_alias(&f.targets[0].oracle,binds?"first_bind":"first_literal");if(binds)f.targets[0].oracle.binds=1;else add_string(&f.targets[0].oracle,value);
    format(f.targets[1].text,sizeof(f.targets[1].text),"%s AS %s",token,token);f.targets[1].index=1;add_field(&f.targets[1].oracle,name);add_alias(&f.targets[1].oracle,name);
    format(f.targets[2].text,sizeof(f.targets[2].text),binds?"? AS last_bind":"%s'tail' AS last_literal",national?"N":"");
    f.targets[2].index=2;add_alias(&f.targets[2].oracle,binds?"last_bind":"last_literal");if(binds)f.targets[2].oracle.binds=1;else add_string(&f.targets[2].oracle,"tail");
    f.target_count=3;for(size_t i=0;i<3;i++)merge(&f.oracle,&f.targets[i].oracle);
    format(f.sql,sizeof(f.sql),"SELECT %s, %s, %s FROM t",f.targets[0].text,f.targets[1].text,f.targets[2].text);ordinary(&f);
}
static void nested(void)
{
    fixture_t f={0};char token[1024];const char*name="dup $1 \" -- ` ?";quote(token,sizeof(token),name);case_name="nested-duplicate";
    format(f.targets[0].text,sizeof(f.targets[0].text),"(SELECT %s AS %s FROM inner_t) AS outer_alias",token,token);
    add_alias(&f.targets[0].oracle,"outer_alias");add_alias(&f.targets[0].oracle,name);add_field(&f.targets[0].oracle,name);
    format(f.targets[1].text,sizeof(f.targets[1].text),"%s AS %s",token,token);f.targets[1].index=1;add_alias(&f.targets[1].oracle,name);add_field(&f.targets[1].oracle,name);
    f.targets[2]=f.targets[1];f.targets[2].list=1;f.targets[2].index=0;
    f.target_count=3;merge(&f.oracle,&f.targets[0].oracle);merge(&f.oracle,&f.targets[1].oracle);
    format(f.sql,sizeof(f.sql),"SELECT %s, %s FROM t",f.targets[0].text,f.targets[1].text);
    /* The ordinary relation mutation intentionally expects the simple outer
     * relation ordinal, so nested cases use only unmutated independent owners. */
    for(int k=0;k<3;k++){path=k==0?"whole":k==1?"direct":"selector";if(!wanted(path))continue;fixtures++;sqlparser_handle_t*h=parse_options(f.sql,0,1);if(h){render(h,&f,k);sqlparser_handle_destroy(h);}}
}
static void copy_lifecycle(void)
{
    const char*name="copy $2 \" /* ` ?";fixture_t f={0};char token[1024],input[CAP];sqlparser_handle_t*h;sqlparser_error_t e={0};char*owned=NULL,*snapshot=NULL;
    quote(token,sizeof(token),name);case_name="copy-lifetime";path="source-selector";if(!wanted(path))return;fixtures++;
    format(f.targets[0].text,sizeof(f.targets[0].text),"%s AS %s",token,token);add_alias(&f.targets[0].oracle,name);add_field(&f.targets[0].oracle,name);
    f.targets[1]=f.targets[0];f.targets[1].index=1;f.target_count=2;merge(&f.oracle,&f.targets[0].oracle);merge(&f.oracle,&f.targets[1].oracle);
    format(input,sizeof(input),"SELECT %s, spare AS old_alias FROM t",f.targets[0].text);
    format(f.sql,sizeof(f.sql),"SELECT %s, %s FROM t",f.targets[0].text,f.targets[1].text);
    h=parse_options(input,0,1);if(!h)return;
    sqlparser_patch_t patches[]={ {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].select_target[0][1]",.source_selector="stmt[0].select_target[0][0]"} };
    sqlparser_patch_list_t list={patches,COUNT(patches)};copies++;phase="copy-target";
    if(!success(sqlparser_apply_patch(h,&list,&e),&e,"source-selector-copy"))goto done;
    if(!render(h,&f,0) || !render(h,&f,2))goto done;
    if(!success(sqlparser_select_target_sql(h,0,0,1,&owned,&e),&e,"allocated-copied-fragment"))goto done;
    snapshot=strdup(owned);REQUIRE(snapshot);
    phase="remove-original";mutations++;
    if(!success(sqlparser_select_delete_target(h,0,0,0,&e),&e,"delete-source-target"))goto done;
    f.target_count=1;f.oracle=f.targets[0].oracle;format(f.sql,sizeof(f.sql),"SELECT %s FROM t",f.targets[0].text);
    if(render(h,&f,0))render(h,&f,1);
 done:sqlparser_handle_destroy(h);if(snapshot){phase="destroyed-owner";equal("copied-output-lifetime",snapshot,owned);}free(snapshot);sqlparser_string_free(owned);
}
static void limits(void)
{
    const char*name="limit $2 \" ` ?";char token[1024],target[1200],sql[1400];sqlparser_error_t e={0};
    quote(token,sizeof(token),name);format(target,sizeof(target),"c AS %s",token);format(sql,sizeof(sql),"SELECT %s FROM t",target);case_name="output-limit";
    for(int short_by=0;short_by<2;short_by++){sqlparser_handle_t*h;char*out=NULL;sqlparser_status_t s;size_t limit=strlen(sql)-(size_t)short_by;
        path=short_by?"one-byte-short":"exact-limit";if(!wanted(path))continue;fixtures++;phase="limit-parse";h=parse_options("SELECT c AS old_alias FROM t",limit,1);if(!h)continue;
        phase="limit-mutation";mutations++;size_t index=find_alias(h,"old_alias");if(index==SIZE_MAX){sqlparser_handle_destroy(h);continue;}
        if(!success(sqlparser_statement_set_name(h,0,index,name,&e),&e,"limit-alias-setter")){sqlparser_handle_destroy(h);continue;}
        phase="limit-deparse";s=sqlparser_deparse(h,&out,&e);number("output-limit-status",short_by?SQLPARSER_STATUS_RESOURCE_LIMIT:SQLPARSER_STATUS_OK,s);
        if(!short_by&&s==SQLPARSER_STATUS_OK)equal("exact-limit-output",sql,out);
        if(short_by){checks++;if(out)fail("limit-null-output","NULL",out);}
        printf("LIMIT|%d|%s|%zu|%d|%s\n",dialect,path,limit,s,e.message);sqlparser_string_free(out);sqlparser_handle_destroy(h);
    }
}
int main(int argc,char**argv)
{
    static const struct {const char*label,*value;} names[]={
        {"double-quote","em\"bed"},{"single-quote","em'bed"},{"doubled-backtick","em`bed"},
        {"dollar-one","cash$1"},{"dollar-two","cash$2"},{"question","what?now"},
        {"dash-comment","dash-- tail"},{"hash-comment","hash# tail"},{"block-comment","open/*body*/close"},
        {"unclosed-block-looking","open/*tail"},{"punctuation","par(en),semi;tail"},
        {"backslash","path\\name"},{"backslash-quote","path\\\"name"},{"trailing-backslash","path\\"},
        {"unicode","café_列"},{"combined","mix`\"'$1?$2--#/*(),;\\列"}
    };
    for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--dump"))dump=1;else if(!strcmp(argv[i],"--national-diagnostic"))national_diagnostic=1;else if(!strcmp(argv[i],"--case")&&i+1<argc)filter_case=argv[++i];else if(!strcmp(argv[i],"--path")&&i+1<argc)filter_path=argv[++i];else if(!strcmp(argv[i],"--dialect")&&i+1<argc)filter_dialect=atoi(argv[++i]);else REQUIRE(0);}
    for(int d=0;d<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER;d++){dialect=(sqlparser_dialect_t)d;
        for(size_t i=0;i<COUNT(names);i++){char label[128];format(label,sizeof(label),"alias-%s",names[i].label);case_name=label;single(names[i].value,0);rename_and_replace(names[i].value);
            format(label,sizeof(label),"field-%s",names[i].label);single(names[i].value,1);}
        case_name="adjacent-literals";mixed("a'\"$1 /* `",0,0);
        /* PostgreSQL-family national literals are not asserted as supported. */
        if(d!=SQLPARSER_DIALECT_POSTGRESQL&&d!=SQLPARSER_DIALECT_VASTBASE_POSTGRESQL&&d!=SQLPARSER_DIALECT_KINGBASE_POSTGRESQL){case_name="adjacent-national-literals";mixed("a'\"$2 /* `",1,0);}
        if(mysql()){case_name="actual-binds-before-after";mixed("protect $1 $2 ? -- \" `",0,1);}
        nested();copy_lifecycle();limits();
    }
    printf("MODE|%s\n",national_diagnostic?"inherited-national-literal-diagnostic":"quoted-boundary-regression");
    printf("SUMMARY|fixtures=%zu|checks=%zu|failures=%zu|not_admitted=%zu|reads=%zu|mutations=%zu|copies=%zu|reparses=%zu\n",fixtures,checks,failures,not_admitted,reads,mutations,copies,reparses);
    return failures?1:0;
}
