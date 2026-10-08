/* The reference disables ONLY the new admission call. All old preprocessing
 * algorithms run independently, with their real private state visible here.
 * Public entry points link to these same instrumented MySQL dialect ops.
 * No native parser, wire codec, graph, or patch implementation is replaced. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sqlparser_internal.h"
#include "src/pg_query_observer.h"

static const char *stage = "start";
static size_t cases, proof_calls, proof_successes, allocation_calls;
static int force_reference;
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d stage=%s case=%zu %s\n",__FILE__,__LINE__,stage,cases,#x); abort(); } } while (0)

static int identity_probe(const char *sql, PgQueryIdentityScalarInsertProof *proof)
{
    int result;
    size_t allocations = allocation_calls;
    ++proof_calls;
    if (force_reference) { memset(proof,0,sizeof(*proof)); return 0; }
    result = pg_query_prove_mysql_identity_scalar_insert(sql,proof);
    CHECK(allocation_calls == allocations); /* Whole-source proof is allocation-free. */
    if (result) ++proof_successes;
    return result;
}
static size_t owned_plan_calls, owned_plan_successes;
static int owned_identity_probe(const char *sql, size_t length,
    PgQueryMysqlOwnedScalarInsertPlan *plan, size_t *strings)
{
    int result;
    size_t allocations = allocation_calls;
    ++owned_plan_calls;
    if (force_reference) { memset(plan, 0, sizeof(*plan)); *strings = 0U; return 0; }
    result = pg_query_prove_mysql_owned_scalar_insert(sql, length, plan, strings);
    CHECK(allocation_calls == allocations);
    if (result) ++owned_plan_successes;
    return result;
}
#define pg_query_prove_mysql_owned_scalar_insert owned_identity_probe
#define pg_query_prove_mysql_identity_scalar_insert identity_probe
#include "../../src/dialect/sqlparser_dialect_mysql.c"
#undef pg_query_prove_mysql_identity_scalar_insert
#undef pg_query_prove_mysql_owned_scalar_insert

#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
static int armed;
static unsigned native_depth;
static size_t fail_at, fail_second, injected, attempts, live, ledger_end;
static void *ledger[16384];
struct MemoryContextData;
struct MemoryContextData *__real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(struct MemoryContextData *);
struct MemoryContextData *__wrap_pg_query_enter_memory_context(void)
{ ++native_depth; return __real_pg_query_enter_memory_context(); }
void __wrap_pg_query_exit_memory_context(struct MemoryContextData *c)
{ __real_pg_query_exit_memory_context(c); CHECK(native_depth); --native_depth; }
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void __real_free(void *);
static size_t slot(void *p)
{
    if(p&&live)for(size_t i=0;i<ledger_end;i++)if(ledger[i]==p)return i;
    return COUNT(ledger);
}
static void remember(void *p)
{
    size_t i;
    if(!p)return;
    for(i=0;i<ledger_end&&ledger[i];i++);
    CHECK(i<COUNT(ledger));ledger[i]=p;++live;if(i==ledger_end)++ledger_end;
}
static int reject(void)
{
    ++allocation_calls;
    if(!armed||native_depth)return 0;
    if(++attempts!=fail_at && attempts!=fail_second)return 0;
    ++injected;return 1;
}
void *__wrap_malloc(size_t n)
{ void *p;if(reject())return NULL;p=__real_malloc(n);if(armed&&!native_depth)remember(p);return p; }
void *__wrap_calloc(size_t n,size_t s)
{ void *p;if(reject())return NULL;p=__real_calloc(n,s);if(armed&&!native_depth)remember(p);return p; }
void *__wrap_realloc(void *p,size_t n)
{
    size_t i=slot(p);void *q;if(reject())return NULL;q=__real_realloc(p,n);
    if(q||!n){if(i<COUNT(ledger)){ledger[i]=q;if(!q)--live;}else if(armed&&!native_depth)remember(q);}
    return q;
}
void __wrap_free(void *p)
{ size_t i=slot(p);if(i<COUNT(ledger)){ledger[i]=NULL;--live;}__real_free(p); }
static void arm(size_t at)
{ CHECK(!live&&!native_depth);fail_at=at;fail_second=0U;injected=attempts=0;armed=1; }
#endif

static void text_equal(const char *a,const char *b)
{ CHECK((a==NULL)==(b==NULL));if(a)CHECK(strcmp(a,b)==0); }
static void state_equal(const sqlparser_mysql_state_t *a,const sqlparser_mysql_state_t *b)
{
    sqlparser_mysql_state_t x,y;
    CHECK((a==NULL)==(b==NULL));if(!a)return;memcpy(&x,a,sizeof(x));memcpy(&y,b,sizeof(y));
#define CLEAR(member) x.member=y.member=NULL
    CLEAR(national_literals.items);CLEAR(dml_modifiers);CLEAR(create_column_restores);
    CLEAR(create_table_restores);CLEAR(on_duplicate_restores);CLEAR(index_hints);
    CLEAR(partition_restores);CLEAR(join_restores);CLEAR(limit_restores);CLEAR(dml_tails);
    CLEAR(dml_shapes);CLEAR(executable_comments);CLEAR(lock_in_share_statements);
#undef CLEAR
    CHECK(memcmp(&x,&y,sizeof(x))==0); /* Every scalar field, including capacities. */
#define EQ(member) CHECK(a->member==b->member)
#define TXT(member) text_equal(a->member,b->member)
    for(size_t i=0;i<a->national_literals.count;i++) {
        EQ(national_literals.items[i].ordinal);CHECK((a->national_literals.items[i].owner==NULL)==(b->national_literals.items[i].owner==NULL));
        TXT(national_literals.items[i].sql);TXT(national_literals.items[i].surface_sql);
    }
    for(size_t i=0;i<a->dml_modifier_count;i++) {
        EQ(dml_modifiers[i].statement_index);EQ(dml_modifiers[i].kind);
        EQ(dml_modifiers[i].flags);EQ(dml_modifiers[i].insert_mode_override);
    }
    for(size_t i=0;i<a->create_column_restore_count;i++) {
        EQ(create_column_restores[i].statement_index);EQ(create_column_restores[i].column_ordinal);
        TXT(create_column_restores[i].segment_sql);
    }
    for(size_t i=0;i<a->create_table_restore_count;i++) {
        EQ(create_table_restores[i].statement_index);TXT(create_table_restores[i].options_sql);
    }
    for(size_t i=0;i<a->on_duplicate_restore_count;i++) {
        EQ(on_duplicate_restores[i].statement_index);EQ(on_duplicate_restores[i].kind);
        TXT(on_duplicate_restores[i].alias_name);TXT(on_duplicate_restores[i].alias_columns_sql);
    }
    for(size_t i=0;i<a->index_hint_count;i++) {
        EQ(index_hints[i].statement_index);EQ(index_hints[i].relation_index);
        EQ(index_hints[i].relation_location);EQ(index_hints[i].surface_order);
        CHECK((a->index_hints[i].owner==NULL)==(b->index_hints[i].owner==NULL));TXT(index_hints[i].fragment_sql);
    }
    for(size_t i=0;i<a->partition_restore_count;i++) {
        EQ(partition_restores[i].statement_index);EQ(partition_restores[i].relation_index);
        EQ(partition_restores[i].relation_location);CHECK((a->partition_restores[i].owner==NULL)==(b->partition_restores[i].owner==NULL));
        TXT(partition_restores[i].fragment_sql);
    }
    for(size_t i=0;i<a->join_restore_count;i++) {
        EQ(join_restores[i].statement_index);EQ(join_restores[i].join_index);
        CHECK((a->join_restores[i].owner==NULL)==(b->join_restores[i].owner==NULL));TXT(join_restores[i].keyword_sql);
    }
    for(size_t i=0;i<a->limit_restore_count;i++) {
        EQ(limit_restores[i].limit_ordinal);CHECK((a->limit_restores[i].owner==NULL)==(b->limit_restores[i].owner==NULL));
    }
    for(size_t i=0;i<a->dml_tail_count;i++)EQ(dml_tails[i].statement_index);
    for(size_t i=0;i<a->dml_shape_count;i++) {EQ(dml_shapes[i].statement_index);EQ(dml_shapes[i].flags);}
    for(size_t i=0;i<a->executable_comment_count;i++) {
        EQ(executable_comments[i].statement_index);EQ(executable_comments[i].body_offset);
        EQ(executable_comments[i].body_length);TXT(executable_comments[i].surface_sql);
    }
    for(size_t i=0;i<a->lock_in_share_count;i++)EQ(lock_in_share_statements[i]);
#undef EQ
#undef TXT
}

static char *fixture(size_t rows,size_t padding,const char *table,const char *columns,const char *values,const char *tail)
{
    size_t capacity=padding+strlen(table)+strlen(columns)+strlen(tail)+256U+rows*(strlen(values)+4U);
    char *sql=malloc(capacity);size_t used=padding;CHECK(sql);memset(sql,' ',padding);
    used+=(size_t)snprintf(sql+used,capacity-used,"INSERT INTO %s(%s) VALUES ",table,columns);
    for(size_t i=0;i<rows;i++)used+=(size_t)snprintf(sql+used,capacity-used,"%s(%s)",i?",":"",values);
    CHECK(used+strlen(tail)<capacity);strcpy(sql+used,tail);return sql;
}

static void preprocess_parity(const char *sql,int admitted,int origins)
{
    char *a=NULL,*b=NULL;void *as=NULL,*bs=NULL;
    sqlparser_error_t ae={0},be={0};sqlparser_limits_t limits;
    sqlparser_identifier_origin_map_t *am=NULL,*bm=NULL;
    sqlparser_status_t ar,br;size_t before=proof_successes,calls=proof_calls;
    sqlparser_parse_options_t options;sqlparser_parse_options_default(&options);limits=options.limits;
    if(origins) {
        CHECK(sqlparser_identifier_origin_map_new_identity(strlen(sql),&am,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_identifier_origin_map_new_identity(strlen(sql),&bm,&be)==SQLPARSER_STATUS_OK);
    }
    force_reference=0;ar=sqlparser_mysql_preprocess_internal(sql,&limits,&a,&as,am,&ae);
    if(origins)CHECK(proof_calls==calls);
    else if(admitted>=0) { if(proof_successes-before!=(size_t)admitted) { const char *q=sql; while(*q==' ')++q; fprintf(stderr,"admission expected=%d actual=%zu SQL=%.350s\n",admitted,proof_successes-before,q); fprintf(stderr,"TAIL=%.120s\n",sql+ (strlen(sql)>120?strlen(sql)-120:0)); } CHECK(proof_successes-before==(size_t)admitted); }
    force_reference=1;br=sqlparser_mysql_preprocess_internal(sql,&limits,&b,&bs,bm,&be);force_reference=0;
    CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);text_equal(a,b);state_equal(as,bs);
    if(ar==SQLPARSER_STATUS_OK) {
        CHECK(a&&b&&a!=sql&&b!=sql);
        if(admitted==1&&!origins) {
            PgQueryIdentityScalarInsertProof proof;
            CHECK(pg_query_prove_mysql_identity_scalar_insert(sql,&proof));
            CHECK(proof.source_length==strlen(sql));CHECK(proof.row_count>=32U&&proof.column_count);
            CHECK(sqlparser_mysql_state_is_plain_insert_strings(as,proof.string_count));
            CHECK(memcmp(sql,a,proof.source_length+1U)==0);
        }
        if(origins) {
            CHECK(sqlparser_identifier_origin_map_output_length(am)==sqlparser_identifier_origin_map_output_length(bm));
            for(size_t i=0;i<strlen(a);i++) {
                sqlparser_identifier_origin_t x={0},y={0};
                CHECK(sqlparser_identifier_origin_map_lookup(am,i,1U,&x)==sqlparser_identifier_origin_map_lookup(bm,i,1U,&y));
                CHECK(memcmp(&x,&y,sizeof(x))==0);
            }
        }
    } else CHECK(!a&&!b&&!as&&!bs);
    free(a);free(b);sqlparser_mysql_state_destroy(as);sqlparser_mysql_state_destroy(bs);
    sqlparser_identifier_origin_map_destroy(am);sqlparser_identifier_origin_map_destroy(bm);++cases;
}

static void graph_equal(sqlparser_handle_t *a,sqlparser_handle_t *b)
{
    sqlparser_query_graph_view_t x,y;sqlparser_error_t ae={0},be={0};
    sqlparser_status_t ar=sqlparser_statement_query_graph(a,0U,&x,&ae);
    sqlparser_status_t br=sqlparser_statement_query_graph(b,0U,&y,&be);
    CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);if(ar!=SQLPARSER_STATUS_OK)return;
    {
        sqlparser_query_graph_view_t xx=x,yy=y;
        xx.handle=yy.handle=NULL;xx.generation=yy.generation=0;
        CHECK(memcmp(&xx,&yy,sizeof(xx))==0);
    }
    for(size_t i=0;i<x.block_count;i++) {
        sqlparser_graph_block_t u,v;
        CHECK(sqlparser_query_graph_block_at(&x,i,&u,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_block_at(&y,i,&v,&be)==SQLPARSER_STATUS_OK);
        CHECK(!memcmp(&u,&v,sizeof(u)));
    }
    for(size_t i=0;i<x.relation_count;i++) {
        sqlparser_graph_relation_t u,v;
        CHECK(sqlparser_query_graph_relation_at(&x,i,&u,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_relation_at(&y,i,&v,&be)==SQLPARSER_STATUS_OK);
#define REL(m) text_equal(u.m,v.m);u.m=v.m=NULL
        REL(database_name);REL(schema_name);REL(object_name);REL(alias_name);REL(link_name);
#undef REL
        CHECK(!memcmp(&u,&v,sizeof(u)));
    }
    if(x.has_dml) {
        sqlparser_graph_dml_t u,v;
        CHECK(sqlparser_query_graph_dml(&x,&u,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_query_graph_dml(&y,&v,&be)==SQLPARSER_STATUS_OK);
        CHECK(!memcmp(&u,&v,sizeof(u)));
        for(size_t i=0;i<u.target_columns.count;i++) {
            size_t ui,vi;sqlparser_graph_dml_column_t uc,vc;
            CHECK(sqlparser_query_graph_span_index_at(&x,u.target_columns,i,&ui,&ae)==SQLPARSER_STATUS_OK);
            CHECK(sqlparser_query_graph_span_index_at(&y,v.target_columns,i,&vi,&be)==SQLPARSER_STATUS_OK);
            CHECK(ui==vi);
            CHECK(sqlparser_query_graph_dml_column_at(&x,ui,&uc,&ae)==SQLPARSER_STATUS_OK);
            CHECK(sqlparser_query_graph_dml_column_at(&y,vi,&vc,&be)==SQLPARSER_STATUS_OK);
            text_equal(uc.column_name,vc.column_name);uc.column_name=vc.column_name=NULL;
            CHECK(!memcmp(&uc,&vc,sizeof(uc)));
        }
        for(size_t i=0;i<u.rows.count;i++) {
            size_t ui,vi;sqlparser_graph_dml_cell_t uc,vc;
            CHECK(sqlparser_query_graph_span_index_at(&x,u.rows,i,&ui,&ae)==SQLPARSER_STATUS_OK);
            CHECK(sqlparser_query_graph_span_index_at(&y,v.rows,i,&vi,&be)==SQLPARSER_STATUS_OK);
            CHECK(ui==vi);
            CHECK(sqlparser_query_graph_dml_cell_at(&x,ui,&uc,&ae)==SQLPARSER_STATUS_OK);
            CHECK(sqlparser_query_graph_dml_cell_at(&y,vi,&vc,&be)==SQLPARSER_STATUS_OK);
            text_equal(uc.literal.string_value,vc.literal.string_value);
            text_equal(uc.literal.float_value,vc.literal.float_value);
            uc.literal.string_value=vc.literal.string_value=NULL;
            uc.literal.float_value=vc.literal.float_value=NULL;
            CHECK(!memcmp(&uc,&vc,sizeof(uc)));
        }
    }
    /* Small-case JSON is another complete structure/value oracle; large
     * cases use every public cell above without constructing huge JSON. */
    if(a->sql_len<16384U&&a->limits.max_output_bytes>=4U*1024U*1024U) {
        char *aj=NULL,*bj=NULL;
        sqlparser_status_t ar=sqlparser_export_view_json(a,0U,&aj,&ae);
        sqlparser_status_t br=sqlparser_export_view_json(b,0U,&bj,&be);
        CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);
        text_equal(aj,bj);sqlparser_string_free(aj);sqlparser_string_free(bj);
    }
}
static void handle_equal(sqlparser_handle_t *a,sqlparser_handle_t *b)
{
    char *as=NULL,*bs=NULL;sqlparser_error_t ae={0},be={0};
    text_equal(a->parser_sql,b->parser_sql);text_equal(a->sql,b->sql);
    CHECK(a->parse_tree.len==b->parse_tree.len);
    CHECK(memcmp(a->parse_tree.data,b->parse_tree.data,a->parse_tree.len)==0);
    CHECK(a->statement_count==b->statement_count);
    {
        sqlparser_status_t ar=sqlparser_deparse(a,&as,&ae);
        sqlparser_status_t br=sqlparser_deparse(b,&bs,&be);
        CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);
        text_equal(as,bs);sqlparser_string_free(as);sqlparser_string_free(bs);
        if(ar!=SQLPARSER_STATUS_OK){CHECK(a->failed==b->failed);return;}
    }
    graph_equal(a,b);
}
static void public_parity(const char *sql,int patch,sqlparser_parse_options_t *custom)
{
    sqlparser_parse_options_t options;sqlparser_handle_t *a=NULL,*b=NULL;
    sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;
    sqlparser_parse_options_default(&options);options.dialect=SQLPARSER_DIALECT_MYSQL;
    if(custom)options=*custom;
    force_reference=0;ar=sqlparser_parse_with_options(sql,&options,&a,&ae);
    force_reference=1;br=sqlparser_parse_with_options(sql,&options,&b,&be);force_reference=0;
    CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);
    if(ar==SQLPARSER_STATUS_OK) {
        CHECK(a&&b);state_equal(a->dialect_state,b->dialect_state);handle_equal(a,b);
        if(patch) {
            sqlparser_patch_t p={0};sqlparser_patch_list_t list={0};
            sqlparser_literal_value_t literal={0};
            p.op=SQLPARSER_PATCH_REPLACE;p.selector="stmt[0].insert_cell[0][0]";
            if(cases%2U){literal.kind=SQLPARSER_LITERAL_KIND_STRING;literal.string_value="identity-new-value";p.literal=&literal;}
            else p.sql="'identity-new-value'";
            list.items=&p;list.count=1U;
            force_reference=0;ar=sqlparser_apply_patch(a,&list,&ae);
            force_reference=1;br=sqlparser_apply_patch(b,&list,&be);force_reference=0;
            CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);CHECK(ar==SQLPARSER_STATUS_OK);
            handle_equal(a,b);
        }
    } else CHECK(!a&&!b);
    sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);++cases;
}

static void positives(void)
{
    static const char *cells[]={"'张三李四'","0","2147483647","2147483648","-2147483648","- 7",".5","- .5E+3","100.50","1.e-2","CURRENT_TIMESTAMP","CURRENT_TIME(6)","SESSION_USER","CURRENT_CATALOG"};
    static const char *strings[]={"''","'UTF8 é € 😀'","'auto_increment unsigned zerofill straight_join'","'/*! SELECT */ # -- ? `quoted` \"string\" ; SET names ON DUPLICATE KEY UPDATE LOCK IN SHARE MODE USE INDEX straight_join auto_increment'"};
    stage="positive metadata, SQL, state and public parity";
    for(size_t i=0;i<COUNT(cells);i++) {
        char values[1024];snprintf(values,sizeof(values),"'text',%s,CURRENT_DATE",cells[i]);
        char *s=fixture(32U,4096U,"Db.Sch.Tab","a,b,c",values,"; \t\n");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);free(s);
    }
    for(size_t i=0;i<COUNT(strings);i++) {
        char *s=fixture(32U,4096U,"S.T","a",strings[i],"");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);preprocess_parity(s,1,1);free(s);
    }
    {
        static const char *names[]={"outfile","dumpfile","duplicate","engine","charset","use","ignore","now","uuid","last_insert_id","OutFiLe","DuMpFiLe","auto_increment_suffix","prefix_auto_increment","unsigned_suffix","prefix_unsigned","zerofill_suffix","prefix_zerofill","straight_join_suffix","prefix_straight_join"};
        for(size_t i=0;i<COUNT(names);i++)for(size_t position=0;position<4U;position++) {
            char table[256],columns[128];
            snprintf(table,sizeof(table),"%s.%s.%s",position==0?names[i]:"db",position==1?names[i]:"sch",position==2?names[i]:"tab");
            snprintf(columns,sizeof(columns),"a,%s,c",position==3?names[i]:"b");
            char *s=fixture(32U,4096U,table,columns,"'text',1,CURRENT_TIMESTAMP","");
            preprocess_parity(s,1,0);public_parity(s,0,NULL);free(s);
        }
    }
    {
        char name[1025];memset(name,'Q',sizeof(name)-1U);name[sizeof(name)-1U]='\0';
        memcpy(name,"auto_increment_",15U);
        memcpy(name+sizeof(name)-15U,"_straight_join",14U);
        name[sizeof(name)-1U]='\0';
        char *s=fixture(32U,4096U,name,"a,b,c","'text',1,CURRENT_TIMESTAMP","");
        preprocess_parity(s,1,0);public_parity(s,0,NULL);free(s);
        s=fixture(32U,4096U,"s.t",name,"'text'","");
        preprocess_parity(s,1,0);public_parity(s,0,NULL);free(s);
    }
    for(size_t n=1;n<=19U;n++) {
        char columns[256]="",values[1024]="";
        for(size_t i=0;i<n;i++) {
            size_t at=strlen(columns);snprintf(columns+at,sizeof(columns)-at,"%sc%zu",i?",":"",i);
            if(i)strcat(values,",");
            strcat(values,cells[i%COUNT(cells)]);
        }
        char *s=fixture(32U,4096U,"S.T",columns,values,"");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);free(s);
    }
    {
        char *s=fixture(5000U,0U,"TEST_LIB.TEACHER_STATISTICS",
            "STAT_DATE,TEACHER_ID,TEACHER_NAME_ENCRYPT,PHONE_ENCRYPT,TOTAL_TEACHING,TOTAL_HOURS,CHECK_STATUS,CREATE_TIME,UPDATE_TIME",
            "'202505','T1001','张三李四','13800138000',20,100.50,0,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP","");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);free(s);
    }
}

static void exclusions(void)
{
    static const char *words[]={"auto_increment","unsigned","zerofill","straight_join","AuTo_InCrEmEnT","UnSiGnEd","ZeRoFiLl","StRaIgHt_JoIn"};
    static const char *values[]={"'x\\n'","'a''b'","'a'\n'b'","N'x'","E'x'","U&'x'","$$x$$","\"x\"","'\x80'","'\xc0\x80'","'\xed\xa0\x80'","'\xf4\x90\x80\x80'","'line\nline'","'unterminated","?","@param","NULL","DEFAULT","TRUE","1+2","CURRENT_TIMESTAMP()"};
    static const char *tails[]={";;",";SET names utf8"," ON DUPLICATE KEY UPDATE a=VALUES(a)"," RETURNING *"," -- comment"," /* comment */",",('x',1,2)"," junk"};
    static const char *whole[]={
        "SELECT 'x'", "SET names utf8", "USE example_db", "PREPARE x FROM 'SELECT ?'", "EXECUTE x USING @v",
        "INSERT IGNORE INTO t(a) VALUES ('x')", "REPLACE INTO t(a) VALUES ('x')",
        "INSERT INTO t(a) VALUES ('x') ON DUPLICATE KEY UPDATE a=VALUES(a)",
        "SELECT a FROM t USE INDEX (a)", "SELECT a FROM t PARTITION (p)",
        "SELECT a FROM t STRAIGHT_JOIN u ON t.a=u.a", "SELECT a FROM t LOCK IN SHARE MODE",
        "SELECT a FROM t LIMIT 1,2", "CREATE TABLE t(a INT UNSIGNED) ENGINE=innodb",
        "/*!40101 SET NAMES utf8 */", "# comment\nSELECT 'x'", "SELECT `x` FROM `t`"
    };
    stage="MySQL-only names, lexical fallbacks, session/DML/extensions";
    for(size_t i=0;i<COUNT(words);i++)for(size_t position=0;position<4U;position++) {
        char table[256],columns[128];
        snprintf(table,sizeof(table),"%s.%s.%s",position==0?words[i]:"db",position==1?words[i]:"sch",position==2?words[i]:"tab");
        snprintf(columns,sizeof(columns),"a,%s,c",position==3?words[i]:"b");
        char *s=fixture(32U,4096U,table,columns,"'x',1,CURRENT_TIMESTAMP","");
        preprocess_parity(s,0,0);public_parity(s,0,NULL);free(s);
    }
    for(size_t i=0;i<COUNT(values);i++) {
        char row[256];snprintf(row,sizeof(row),"%s,1,CURRENT_DATE",values[i]);
        char *s=fixture(32U,4096U,"s.t","a,b,c",row,"");
        preprocess_parity(s,0,0);public_parity(s,0,NULL);free(s);
    }
    for(size_t i=0;i<COUNT(tails);i++) {
        char *s=fixture(32U,4096U,"s.t","a,b,c","'x',1,CURRENT_TIMESTAMP",tails[i]);
        /* A complete additional scalar row remains valid and eligible. */
        preprocess_parity(s,i==6U,0);public_parity(s,0,NULL);free(s);
    }
    for(size_t i=0;i<COUNT(whole);i++) {preprocess_parity(whole[i],0,0);preprocess_parity(whole[i],0,1);}
    {
        char *s=fixture(32U,4096U,"`s`.`t`","a,b,c","'x',1,CURRENT_TIMESTAMP","");
        preprocess_parity(s,0,0);public_parity(s,0,NULL);free(s);
    }
}

static void bounds(void)
{
    stage="source/row admission and public resource/error limits";
    for(size_t rows=31U;rows<=33U;rows++) {
        char *s=fixture(rows,4096U,"s.t","a,b,c","'x',1,CURRENT_TIMESTAMP","");
        preprocess_parity(s,rows>=32U,0);public_parity(s,0,NULL);free(s);
    }
    for(size_t n=4095U;n<=4097U;n++) {
        char *base=fixture(32U,0U,"s.t","a,b,c","'x',1,CURRENT_TIMESTAMP","");
        size_t padding=n-strlen(base);free(base);
        char *s=fixture(32U,padding,"s.t","a,b,c","'x',1,CURRENT_TIMESTAMP","");
        preprocess_parity(s,n>=4096U,0);public_parity(s,0,NULL);
        for(int delta=-1;delta<=1;delta++) {
            sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_MYSQL;
            o.limits.max_sql_bytes=n+delta;public_parity(s,0,&o);
            o.limits.max_sql_bytes=n+1;o.limits.max_output_bytes=n+delta;public_parity(s,0,&o);
        }
        free(s);
    }
    {
        PgQueryIdentityScalarInsertProof p={1U,1U,1U,1U,1},zero={0};
        CHECK(!pg_query_prove_mysql_identity_scalar_insert(NULL,&p));CHECK(!memcmp(&p,&zero,sizeof(p)));
        CHECK(!pg_query_prove_mysql_identity_scalar_insert("SELECT 1",NULL));
    }
}

static void fragment_and_arguments(void)
{
    static const char *fragments[]={"'plain'","N'national'","'back\\slash'","'one''two'","?","CURRENT_TIMESTAMP(6)"};
    stage="fragment and invalid-argument routes remain unchanged";
    for(size_t i=0;i<COUNT(fragments);i++) {
        sqlparser_mysql_state_t *a=NULL,*b=NULL;char *as=NULL,*bs=NULL;
        sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;size_t calls=proof_calls;
        CHECK(sqlparser_mysql_state_new(&a,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_mysql_state_new(&b,&be)==SQLPARSER_STATUS_OK);
        force_reference=0;ar=sqlparser_mysql_preprocess_fragment(fragments[i],a,0U,&as,&ae);
        force_reference=1;br=sqlparser_mysql_preprocess_fragment(fragments[i],b,0U,&bs,&be);force_reference=0;
        CHECK(proof_calls==calls);CHECK(ar==br);CHECK(!memcmp(&ae,&be,sizeof(ae)));
        text_equal(as,bs);state_equal(a,b);free(as);free(bs);
        sqlparser_mysql_state_destroy(a);sqlparser_mysql_state_destroy(b);++cases;
    }
    preprocess_parity(NULL,0,0);
    for(int output=0;output<2;output++) {
        char *as=NULL,*bs=NULL;void *a=NULL,*b=NULL;sqlparser_error_t ae={0},be={0};
        sqlparser_status_t ar,br;size_t calls=proof_calls,allocs=allocation_calls;
        force_reference=0;ar=sqlparser_mysql_preprocess("INSERT",NULL,output?&as:NULL,output?NULL:&a,&ae);
        force_reference=1;br=sqlparser_mysql_preprocess("INSERT",NULL,output?&bs:NULL,output?NULL:&b,&be);force_reference=0;
        CHECK(proof_calls==calls&&allocation_calls==allocs);CHECK(ar==br&&ar==SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(!memcmp(&ae,&be,sizeof(ae)));CHECK(!as&&!bs&&!a&&!b);++cases;
    }
}

static void owned_plan_contract(void)
{
    static const char *rows[] = {
        "'owned plan UTF8 张三李四 literal long enough',100.50,CURRENT_TIMESTAMP",
        "'owned plan literal long enough',- 7,CURRENT_TIME(6)",
        "'auto_increment unsigned zerofill straight_join',1,CURRENT_DATE"
    };
    PgQueryMysqlOwnedScalarInsertPlan plan, zero = {{0}};
    sqlparser_parse_options_t options;
    sqlparser_parse_options_default(&options);
    stage = "owned plan success, pointer/length binding, consumption and fallbacks";
    for (size_t i=0U;i<COUNT(rows);++i) {
        char *sql=fixture(128U,0U,"s.t","a,b,c",rows[i],"; ");
        char *out=NULL,*reference=NULL; void *state=NULL,*reference_state=NULL;
        sqlparser_error_t e={0}, re={0}; size_t calls=owned_plan_calls, strings=0U;
        CHECK(strlen(sql)>=4096U);
        CHECK(sqlparser_mysql_preprocess_with_native_plan(sql,strlen(sql),&options.limits,
            &out,&state,&plan,&e)==SQLPARSER_STATUS_OK);
        CHECK(owned_plan_calls==calls+1U && memcmp(&plan,&zero,sizeof(plan))!=0);
        CHECK(out!=sql);text_equal(out,sql);
        force_reference=1;
        CHECK(sqlparser_mysql_preprocess(sql,&options.limits,&reference,&reference_state,&re)==SQLPARSER_STATUS_OK);
        force_reference=0;state_equal(state,reference_state);text_equal(out,reference);
        /* Caller bytes are not the plan owner, and may be destroyed now. */
        memset(sql,0xa7,strlen(sql));free(sql);
        for (int mode=0;mode<6;++mode) {
            PgQueryProtobufParseResult actual, expected;
            PgQueryNativeScalarInsertProof proof={0}, empty={0};
            size_t count=0U;int certified=0;
            int parse_options=mode==4?PG_QUERY_DISABLE_STANDARD_CONFORMING_STRINGS:PG_QUERY_PARSE_DEFAULT;
            if(mode!=3) CHECK(pg_query_prove_mysql_owned_scalar_insert(out,strlen(out),&plan,&strings));
            if(mode==2) memset(&plan,0,sizeof(plan));
            actual=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan(
                mode==1?reference:out, strlen(out)+(mode==5), parse_options,
                NULL,NULL,&count,&certified,&proof,&plan);
            CHECK(!memcmp(&plan,&zero,sizeof(plan)));
            expected=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
                out,parse_options,NULL,NULL);
            CHECK(!actual.error&&!expected.error&&actual.parse_tree.data&&expected.parse_tree.data);
            CHECK(actual.parse_tree.len==expected.parse_tree.len);
            CHECK(!memcmp(actual.parse_tree.data,expected.parse_tree.data,actual.parse_tree.len));
            if(mode==4) CHECK(!memcmp(&proof,&empty,sizeof(proof)));
            pg_query_free_protobuf_parse_result(actual);pg_query_free_protobuf_parse_result(expected);
        }
        memset(&plan,0xa7,sizeof(plan));
        CHECK(!pg_query_prove_mysql_owned_scalar_insert(out,strlen(out)+1U,&plan,&strings));
        CHECK(!memcmp(&plan,&zero,sizeof(plan))&&strings==0U);
        memset(&plan,0xa7,sizeof(plan));
        CHECK(!pg_query_prove_mysql_owned_scalar_insert(NULL,0U,&plan,&strings));
        CHECK(!memcmp(&plan,&zero,sizeof(plan)));
        public_parity(out,1,NULL);
        free(out);free(reference);sqlparser_mysql_state_destroy(state);sqlparser_mysql_state_destroy(reference_state);
    }
    {
        char *sql=fixture(128U,0U,"t","a,b","1,'simple constructor retains no new native graph proof'","");
        PgQueryNativeScalarInsertProof proof={0},empty={0};size_t strings=0,count=0;int certified=0;
        CHECK(pg_query_prove_mysql_owned_scalar_insert(sql,strlen(sql),&plan,&strings));
        PgQueryProtobufParseResult parsed=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native_plan(
            sql,strlen(sql),0,NULL,NULL,&count,&certified,&proof,&plan);
        CHECK(!parsed.error&&certified&&count==1U&&!memcmp(&proof,&empty,sizeof(proof)));
        CHECK(!memcmp(&plan,&zero,sizeof(plan)));pg_query_free_protobuf_parse_result(parsed);
        public_parity(sql,0,NULL);free(sql);
    }
    {
        char *sql=fixture(128U,0U,"unsigned","a,b,c",rows[0],"");
        char *out=NULL;void *state=NULL;sqlparser_error_t e={0};
        memset(&plan,0xa7,sizeof(plan));
        CHECK(sqlparser_mysql_preprocess_with_native_plan(sql,strlen(sql),&options.limits,
            &out,&state,&plan,&e)!=SQLPARSER_STATUS_OK);
        CHECK(!memcmp(&plan,&zero,sizeof(plan))&&!out&&!state);public_parity(sql,0,NULL);free(sql);
    }
    for(int mode=0;mode<3;++mode) {
        char *out=NULL;void *state=NULL;sqlparser_error_t e={0};size_t calls=owned_plan_calls;
        memset(&plan,0xa7,sizeof(plan));
        CHECK(sqlparser_mysql_preprocess_with_native_plan(NULL,0U,&options.limits,
            mode==0?NULL:&out,mode==1?NULL:&state,&plan,&e)!=SQLPARSER_STATUS_OK);
        CHECK(!memcmp(&plan,&zero,sizeof(plan))&&!out&&!state&&owned_plan_calls==calls);
    }
    ++cases;
}

#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
static void allocation_failures(void)
{
    char *sql=fixture(128U,0U,"s.t","a,b,c","'owned plan literal long enough for admission',100.50,CURRENT_TIMESTAMP","");
    size_t boundaries=0;
    stage="every identity preprocessing allocation failure";
    for(size_t at=0;at<=boundaries;at++) {
        char *out=NULL;void *state=NULL;sqlparser_error_t e={0};sqlparser_parse_options_t o;
        sqlparser_status_t status;sqlparser_parse_options_default(&o);force_reference=0;arm(at);
        status=sqlparser_mysql_preprocess(sql,&o.limits,&out,&state,&e);armed=0;
        if(!at){boundaries=attempts;CHECK(boundaries==2U);CHECK(status==SQLPARSER_STATUS_OK);}
        else {CHECK(injected==1U);CHECK(status==SQLPARSER_STATUS_NO_MEMORY);CHECK(!out&&!state);CHECK(e.code==SQLPARSER_STATUS_NO_MEMORY);}
        free(out);sqlparser_mysql_state_destroy(state);CHECK(!live);preprocess_parity(sql,1,0);
    }
    stage="owned preprocessing required and optional allocation failures";
    for(size_t at=0U;at<=3U;++at) {
        PgQueryMysqlOwnedScalarInsertPlan plan,zero={{0}};
        char *out=NULL;void *state=NULL;sqlparser_error_t e={0};sqlparser_parse_options_t o;
        size_t calls=owned_plan_calls;sqlparser_parse_options_default(&o);
        memset(&plan,0xa7,sizeof(plan));arm(at==3U?2U:at);
        if(at==3U)fail_second=3U;
        sqlparser_status_t status=sqlparser_mysql_preprocess_with_native_plan(
            sql,strlen(sql),&o.limits,&out,&state,&plan,&e);armed=0;
        if(at==0U) CHECK(status==SQLPARSER_STATUS_OK&&memcmp(&plan,&zero,sizeof(plan)));
        else {
            CHECK(!memcmp(&plan,&zero,sizeof(plan))&&owned_plan_calls==calls);
            CHECK(injected==(at==3U?2U:1U));
            if(at==2U) {CHECK(status==SQLPARSER_STATUS_OK&&out&&state);text_equal(out,sql);}
            else CHECK(status==SQLPARSER_STATUS_NO_MEMORY&&!out&&!state&&e.code==status);
        }
        memset(&plan,0,sizeof(plan));free(out);sqlparser_mysql_state_destroy(state);CHECK(!live);
    }
    stage="public identity parse allocation failures and recovery";boundaries=0;
    for(size_t at=0;at<=boundaries;at++) {
        sqlparser_handle_t *h=NULL;sqlparser_error_t e={0};sqlparser_parse_options_t o;sqlparser_status_t status;
        sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_MYSQL;
        sqlparser_pg_query_prepare();force_reference=0;arm(at);
        status=sqlparser_parse_with_options(sql,&o,&h,&e);armed=0;
        if(!at){boundaries=attempts;CHECK(status==SQLPARSER_STATUS_OK&&h);}
        else {
            CHECK(injected==1U);
            if(status==SQLPARSER_STATUS_OK) {
                sqlparser_query_graph_view_t graph; char *rendered=NULL;
                /* Native graph provenance is an optional acceleration: its
                 * allocation failure must preserve the strict fallback. */
                CHECK(h && (at == 2U || h->native_scalar_provenance==NULL));
                CHECK(sqlparser_statement_query_graph(h,0U,&graph,&e)==SQLPARSER_STATUS_OK);
                CHECK(sqlparser_deparse(h,&rendered,&e)==SQLPARSER_STATUS_OK);
                text_equal(rendered,sql);sqlparser_string_free(rendered);
            } else {
                CHECK(status==SQLPARSER_STATUS_NO_MEMORY);CHECK(!h);CHECK(e.code==SQLPARSER_STATUS_NO_MEMORY);
            }
        }
        sqlparser_handle_destroy(h);CHECK(!live);public_parity(sql,0,NULL);
    }
    printf("identity allocation boundaries: preprocess=2 public_parse=%zu zero_live=yes\n",boundaries);free(sql);
}
#endif

int main(void)
{
    positives();exclusions();bounds();fragment_and_arguments();owned_plan_contract();
#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
    allocation_failures();
#endif
    pg_query_exit();printf("MySQL identity preprocessing passed: %zu complete state/SQL/public cases\n",cases);return 0;
}
