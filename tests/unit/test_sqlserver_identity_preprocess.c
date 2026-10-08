/* The reference disables ONLY the new admission call. All old preprocessing
 * algorithms run independently, with their real private state visible here.
 * Public entry points link to these same instrumented SQLServer dialect ops.
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
static const char *complete_proof_source;
static size_t proof_string_count_override;
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d stage=%s case=%zu %s\n",__FILE__,__LINE__,stage,cases,#x); abort(); } } while (0)

static int identity_probe(const char *sql, PgQueryIdentityScalarInsertNamePredicate predicate,
    PgQueryIdentityScalarInsertProof *proof)
{
    int result;
    size_t allocations = allocation_calls;
    ++proof_calls;
    if (force_reference) { if(proof)memset(proof,0,sizeof(*proof)); return 0; }
    result = pg_query_prove_identity_scalar_insert(sql,predicate,proof);
    CHECK(allocation_calls == allocations); /* Whole-source proof is allocation-free. */
    if (result && proof_string_count_override) proof->string_count=proof_string_count_override;
    /* Batch preprocessing also calls the same recognizer on scratch slices.
     * This original suite counts only complete-source singleton admission. */
    if (result && (!complete_proof_source || complete_proof_source == sql)) ++proof_successes;
    return result;
}
#define pg_query_prove_identity_scalar_insert identity_probe
#include "../../src/dialect/sqlparser_dialect_sqlserver.c"
#undef pg_query_prove_identity_scalar_insert
/* Include unchanged OUTPUT implementation so opaque nested capacities and
 * owned payloads are compared too, without test-only production hooks. */
#include "../../src/dialect/sqlparser_dialect_sqlserver_output.c"

#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
static int armed;
static unsigned native_depth, unpack_depth;
static size_t unpack_start, unpack_end;
static int failure_in_unpack;
static size_t fail_at, injected, attempts, live, ledger_end;
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
    if(++attempts!=fail_at)return 0;
    ++injected;failure_in_unpack=unpack_depth!=0U;return 1;
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
{ CHECK(!live&&!native_depth&&!unpack_depth);fail_at=at;injected=attempts=0;unpack_start=unpack_end=0U;failure_in_unpack=0;armed=1; }
PgQuery__ParseResult *__real_pg_query__parse_result__unpack(ProtobufCAllocator *,size_t,const uint8_t *);
PgQuery__ParseResult *__wrap_pg_query__parse_result__unpack(ProtobufCAllocator *allocator,size_t n,const uint8_t *data)
{
    PgQuery__ParseResult *tree;
    if(armed)unpack_start=attempts;
    ++unpack_depth;tree=__real_pg_query__parse_result__unpack(allocator,n,data);--unpack_depth;
    if(armed)unpack_end=attempts;
    return tree;
}
#endif

static void text_equal(const char *a,const char *b)
{ CHECK((a==NULL)==(b==NULL));if(a)CHECK(strcmp(a,b)==0); }

static void owner_equal(const ProtobufCMessage *a,const ProtobufCMessage *b)
{
    size_t n;unsigned char *x,*y;
    CHECK((a==NULL)==(b==NULL));if(!a)return;
    CHECK(a->descriptor==b->descriptor);n=protobuf_c_message_get_packed_size(a);
    CHECK(n==protobuf_c_message_get_packed_size(b));x=malloc(n?n:1U);y=malloc(n?n:1U);CHECK(x&&y);
    CHECK(protobuf_c_message_pack(a,x)==n);CHECK(protobuf_c_message_pack(b,y)==n);
    CHECK(!memcmp(x,y,n));free(x);free(y);
}
static void output_equal(const sqlparser_sqlserver_output_state_t *a,const sqlparser_sqlserver_output_state_t *b)
{
    sqlparser_sqlserver_output_state_t x,y;
    CHECK((a==NULL)==(b==NULL));if(!a)return;x=*a;y=*b;x.dmls=y.dmls=NULL;
    CHECK(!memcmp(&x,&y,sizeof(x)));CHECK((a->dmls==NULL)==(b->dmls==NULL));
    for(size_t i=0;i<a->dml_count;i++) {
        sqlparser_sqlserver_output_dml_t u=a->dmls[i],v=b->dmls[i];
#define TEXT(m) text_equal(u.m,v.m);u.m=v.m=NULL
        TEXT(top_sql);TEXT(source_name);TEXT(source_sql);
#undef TEXT
        CHECK((u.channels==NULL)==(v.channels==NULL));u.channels=v.channels=NULL;
        CHECK(!memcmp(&u,&v,sizeof(u)));
        for(size_t j=0;j<a->dmls[i].channel_count;j++) {
            const sqlparser_sqlserver_output_channel_t *c=&a->dmls[i].channels[j],*d=&b->dmls[i].channels[j];
            sqlparser_sqlserver_output_channel_t m=*c,n=*d;
            text_equal(c->sink_sql,d->sink_sql);m.sink_sql=n.sink_sql=NULL;
            CHECK((c->sink_columns==NULL)==(d->sink_columns==NULL));m.sink_columns=n.sink_columns=NULL;
            CHECK((c->actions==NULL)==(d->actions==NULL));m.actions=n.actions=NULL;
            CHECK(!memcmp(&m,&n,sizeof(m)));
            for(size_t k=0;k<c->sink_column_count;k++)text_equal(c->sink_columns[k],d->sink_columns[k]);
            for(size_t k=0;k<c->action_count;k++) {
                CHECK(c->actions[k].target_index==d->actions[k].target_index);
                text_equal(c->actions[k].marker,d->actions[k].marker);
            }
        }
    }
}
static void flow_equal(const sqlparser_control_state_t *a,const sqlparser_control_state_t *b)
{
    sqlparser_control_state_t x,y;
    CHECK((a==NULL)==(b==NULL));if(!a)return;x=*a;y=*b;
#define CLEAR(m) CHECK((a->m==NULL)==(b->m==NULL));x.m=y.m=NULL
    CLEAR(nodes);CLEAR(branches);CLEAR(items);CLEAR(index_pool);CLEAR(units);
#undef CLEAR
    CHECK(!memcmp(&x,&y,sizeof(x)));
#define BYTES(m,n) if(a->n)CHECK(!memcmp(a->m,b->m,a->n*sizeof(*a->m)))
    BYTES(nodes,node_count);BYTES(branches,branch_count);BYTES(items,item_count);
    BYTES(index_pool,index_count);BYTES(units,unit_count);
#undef BYTES
}
static void state_equal(const sqlparser_sqlserver_state_t *a,const sqlparser_sqlserver_state_t *b)
{
    sqlparser_sqlserver_state_t x,y;
    CHECK((a==NULL)==(b==NULL));if(!a)return;memcpy(&x,a,sizeof(x));memcpy(&y,b,sizeof(y));
#define CLEAR(m) CHECK((a->m==NULL)==(b->m==NULL));x.m=y.m=NULL
    CLEAR(params);CLEAR(unicode_restores);CLEAR(top_restores);CLEAR(bare_bit_restores);
    CLEAR(table_hints);CLEAR(query_hints);CLEAR(json_suffixes);CLEAR(json_suffix_ordinals);
    CLEAR(cast_restores);CLEAR(odbc_fn_restores);CLEAR(save_restores);CLEAR(drop_user_ordinals);
    CLEAR(output_state);CLEAR(control);
#undef CLEAR
    CHECK(!memcmp(&x,&y,sizeof(x))); /* All counters, capacities and checkpoint bytes. */
#define ARRAY(field,count,type,body) \
    for(size_t i=0;i<a->count;i++){type u=a->field[i],v=b->field[i];body;}
#define EQ(m) CHECK(u.m==v.m)
#define TXT(m) text_equal(u.m,v.m)
#define OWNER owner_equal(u.owner,v.owner)
    ARRAY(params,param_count,sqlparser_sqlserver_param_t,TXT(name);EQ(new_number));
    ARRAY(unicode_restores,unicode_count,sqlparser_sqlserver_unicode_restore_t,TXT(literal);EQ(ordinal);OWNER);
    ARRAY(top_restores,top_count,sqlparser_sqlserver_top_restore_t,TXT(suffix);EQ(select_ordinal);EQ(generated_wrapper);OWNER);
    ARRAY(bare_bit_restores,bare_bit_count,sqlparser_sqlserver_ordinal_restore_t,EQ(ordinal);OWNER);
    ARRAY(table_hints,table_hint_count,sqlparser_sqlserver_table_hint_t,TXT(sql);EQ(anchor);EQ(source_ordinal);OWNER);
    ARRAY(cast_restores,cast_restore_count,sqlparser_sqlserver_cast_restore_t,TXT(tail);EQ(ordinal);EQ(kind);OWNER);
    ARRAY(odbc_fn_restores,odbc_fn_count,sqlparser_sqlserver_odbc_fn_restore_t,TXT(surface);EQ(ordinal);EQ(parser_offset);EQ(prefix_length);OWNER);
    ARRAY(save_restores,save_restore_count,sqlparser_sqlserver_save_restore_t,TXT(prefix);EQ(statement_ordinal));
#undef ARRAY
#undef EQ
#undef TXT
#undef OWNER
    for(size_t i=0;i<a->query_hint_count;i++)text_equal(a->query_hints[i],b->query_hints[i]);
    for(size_t i=0;i<a->json_suffix_count;i++)text_equal(a->json_suffixes[i],b->json_suffixes[i]);
    if(a->json_suffix_ordinal_count)CHECK(!memcmp(a->json_suffix_ordinals,b->json_suffix_ordinals,a->json_suffix_ordinal_count*sizeof(size_t)));
    if(a->drop_user_count)CHECK(!memcmp(a->drop_user_ordinals,b->drop_user_ordinals,a->drop_user_count*sizeof(size_t)));
    output_equal(a->output_state,b->output_state);
    if(a->control) {
        CHECK(a->control->unit_count==b->control->unit_count);
        flow_equal(a->control->flow,b->control->flow);
        CHECK(a->control->unit_count>0U);
        CHECK(a->control->unit_states[0]==a&&b->control->unit_states[0]==b);
        for(size_t i=1U;i<a->control->unit_count;i++)state_equal(a->control->unit_states[i],b->control->unit_states[i]);
    }
}
static void plain_state(const sqlparser_sqlserver_state_t *s,size_t strings)
{
    sqlparser_sqlserver_state_t zero={0};
    zero.literal_count=strings;zero.table_source_count=1U;
    CHECK(s&&memcmp(s,&zero,sizeof(zero))==0); /* Independent exhaustive fresh-zero assertion. */
    CHECK(sqlparser_sqlserver_state_is_plain_insert_strings(s,strings));
}
static char *fixture(size_t rows,size_t padding,const char *table,const char *columns,const char *values,const char *tail)
{
    size_t capacity=padding+strlen(table)+strlen(columns)+strlen(tail)+256U+rows*(strlen(values)+4U);
    char *sql=malloc(capacity);size_t used=padding;CHECK(sql);memset(sql,' ',padding);
    used+=(size_t)snprintf(sql+used,capacity-used,"INSERT INTO %s(%s) VALUES ",table,columns);
    for(size_t i=0;i<rows;i++)used+=(size_t)snprintf(sql+used,capacity-used,"%s(%s)",i?",":"",values);
    CHECK(used+strlen(tail)<capacity);strcpy(sql+used,tail);return sql;
}
static char *copy(const char *s)
{char *p=malloc(strlen(s)+1U);CHECK(p);strcpy(p,s);return p;}
static void poison_free(char *s)
{if(s){memset(s,0xa7,strlen(s));free(s);}}
static void preprocess_parity(const char *sql,int admitted,int origins)
{
    char *a=NULL,*b=NULL;void *as=NULL,*bs=NULL;
    sqlparser_error_t ae={0},be={0};sqlparser_parse_options_t options;
    sqlparser_identifier_origin_map_t *am=NULL,*bm=NULL;
    sqlparser_status_t ar,br;size_t before=proof_successes,calls=proof_calls;
    sqlparser_parse_options_default(&options);
    if(origins) {
        CHECK(sqlparser_identifier_origin_map_new_identity(strlen(sql),&am,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_identifier_origin_map_new_identity(strlen(sql),&bm,&be)==SQLPARSER_STATUS_OK);
    }
    force_reference=0;
    complete_proof_source=sql;
    ar=origins?sqlparser_sqlserver_preprocess_identifier_origins(sql,&options.limits,&a,&as,am,&ae):
        sqlparser_sqlserver_preprocess(sql,&options.limits,&a,&as,&ae);
    complete_proof_source=NULL;
    if(origins)CHECK(proof_calls==calls);
    else if(admitted>=0) {
        if(proof_successes-before!=(size_t)admitted) {
            const char *q=sql;while(q&&*q==' ')++q;
            fprintf(stderr,"admission expected=%d actual=%zu SQL=%.350s\n",admitted,proof_successes-before,q?q:"NULL");
        }
        CHECK(proof_successes-before==(size_t)admitted);
    }
    force_reference=1;
    br=origins?sqlparser_sqlserver_preprocess_identifier_origins(sql,&options.limits,&b,&bs,bm,&be):
        sqlparser_sqlserver_preprocess(sql,&options.limits,&b,&bs,&be);
    force_reference=0;
    if(ar!=br||memcmp(&ae,&be,sizeof(ae)))fprintf(stderr,"preprocess status %d/%d errors %s/%s\n",ar,br,ae.message,be.message);
    CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);text_equal(a,b);state_equal(as,bs);
    if(ar==SQLPARSER_STATUS_OK) {
        CHECK(a&&b&&a!=sql&&b!=sql);
        if(admitted==1&&!origins) {
            PgQueryIdentityScalarInsertProof proof;
            CHECK(sqlparser_sqlserver_prove_identity_scalar_insert(sql,&proof));
            CHECK(proof.source_length==strlen(sql));CHECK(proof.row_count>=32U&&proof.column_count);
            plain_state(as,proof.string_count);plain_state(bs,proof.string_count);
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
    free(a);free(b);sqlparser_sqlserver_state_destroy(as);sqlparser_sqlserver_state_destroy(bs);
    sqlparser_identifier_origin_map_destroy(am);sqlparser_identifier_origin_map_destroy(bm);++cases;
}
static void graph_equal(sqlparser_handle_t *a,sqlparser_handle_t *b,size_t statement)
{
    sqlparser_query_graph_view_t x,y;sqlparser_error_t ae={0},be={0};
    sqlparser_status_t ar=sqlparser_statement_query_graph(a,statement,&x,&ae);
    sqlparser_status_t br=sqlparser_statement_query_graph(b,statement,&y,&be);
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
        sqlparser_status_t ar=sqlparser_export_view_json(a,statement,&aj,&ae);
        sqlparser_status_t br=sqlparser_export_view_json(b,statement,&bj,&be);
        CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);
        text_equal(aj,bj);sqlparser_string_free(aj);sqlparser_string_free(bj);
    }
}
static void handle_equal(sqlparser_handle_t *a,sqlparser_handle_t *b)
{
    char *as=NULL,*bs=NULL;sqlparser_error_t ae={0},be={0};
    text_equal(a->parser_sql,b->parser_sql);text_equal(a->sql,b->sql);
    CHECK(!a->native_scalar_provenance&&!b->native_scalar_provenance);
    CHECK(!a->dialect_ops->plain_scalar_native_validation&&!b->dialect_ops->plain_scalar_native_validation);
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
    for(size_t i=0;i<a->statement_count;i++)graph_equal(a,b,i);
}

static const sqlparser_dialect_t dialects[]={SQLPARSER_DIALECT_SQLSERVER,
    SQLPARSER_DIALECT_VASTBASE_SQLSERVER,SQLPARSER_DIALECT_KINGBASE_SQLSERVER};
static void public_one(const char *sql,int patch,const sqlparser_parse_options_t *custom,sqlparser_dialect_t dialect)
{
    sqlparser_parse_options_t options;sqlparser_handle_t *a=NULL,*b=NULL;
    sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;
    char *owned=sql?copy(sql):NULL;
    sqlparser_parse_options_default(&options);if(custom)options=*custom;options.dialect=dialect;
    force_reference=0;ar=sqlparser_parse_with_options(owned,&options,&a,&ae);
    force_reference=1;br=sqlparser_parse_with_options(owned,&options,&b,&be);force_reference=0;
    poison_free(owned); /* Neither path may borrow the caller's source. */
    CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);
    if(ar==SQLPARSER_STATUS_OK) {
        CHECK(a&&b);state_equal(a->dialect_state,b->dialect_state);handle_equal(a,b);
        if(patch&&!a->failed&&!b->failed) {
            sqlparser_patch_t p={0};sqlparser_patch_list_t list={0};
            sqlparser_literal_value_t literal={0};char *raw=copy("'identity-new-value'");
            p.op=SQLPARSER_PATCH_REPLACE;p.selector="stmt[0].insert_cell[0][0]";
            if(cases%2U){literal.kind=SQLPARSER_LITERAL_KIND_STRING;literal.string_value="identity-new-value";p.literal=&literal;}
            else p.sql=raw; /* Exercise original string API, with no persistent caller storage. */
            list.items=&p;list.count=1U;
            force_reference=0;ar=sqlparser_apply_patch(a,&list,&ae);
            force_reference=1;br=sqlparser_apply_patch(b,&list,&be);force_reference=0;
            poison_free(raw);
            CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);CHECK(ar==SQLPARSER_STATUS_OK);
            handle_equal(a,b);state_equal(a->dialect_state,b->dialect_state);
            {
                sqlparser_handle_t *ac=NULL,*bc=NULL;
                force_reference=0;ar=sqlparser_handle_clone(a,&ac,&ae);
                force_reference=1;br=sqlparser_handle_clone(b,&bc,&be);force_reference=0;
                CHECK(ar==br&&ar==SQLPARSER_STATUS_OK);CHECK(!memcmp(&ae,&be,sizeof(ae)));
                sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);a=ac;b=bc;
                handle_equal(a,b);state_equal(a->dialect_state,b->dialect_state);
            }
        }
    } else CHECK(!a&&!b);
    sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);++cases;
}
static void public_parity(const char *sql,int patch,const sqlparser_parse_options_t *custom)
{for(size_t i=0;i<COUNT(dialects);i++)public_one(sql,patch,custom,dialects[i]);}

static void positives(void)
{
    static const char *cells[]={"'张三李四'","0","2147483647","2147483648","-2147483648","- 7",".5","- .5E+3","100.50","1.e-2","CURRENT_TIMESTAMP","CURRENT_TIME","CURRENT_TIME(6)","SESSION_USER","CURRENT_CATALOG",
        "CURRENT_ROLE","CURRENT_USER","USER","CURRENT_SCHEMA","LOCALTIME","LOCALTIMESTAMP",
        "CURRENT_TIME(0)","CURRENT_TIME(2147483647)","CURRENT_TIMESTAMP(0)","CURRENT_TIMESTAMP(2147483647)",
        "LOCALTIME(0)","LOCALTIME(2147483647)","LOCALTIMESTAMP(0)","LOCALTIMESTAMP(2147483647)"};
    static const char *strings[]={"''","'UTF8 é € 😀'","'@param @@version ? :bind $1 0xAB foo0xCD'",
        "'/* comment */ -- comment ; [quoted] #temp \"quoted\" GO USE OUTPUT INTO TOP exec execute from join update declare pivot'",
        "'cross apply outer apply for xml create procedure create function create trigger begin try table variable by source'"};
    stage="positive metadata, owned SQL, fresh state and all-alias public parity";
    for(size_t i=0;i<COUNT(cells);i++) {
        char row[1024];snprintf(row,sizeof(row),"'text',%s,CURRENT_DATE",cells[i]);
        char *s=fixture(32U,4096U,"Db.Sch.Tab","a,b,c",row,"; \t\n");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);free(s);
    }
    for(size_t i=0;i<COUNT(strings);i++) {
        char *s=fixture(32U,4096U,"S.T","a",strings[i],"");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);preprocess_parity(s,1,1);free(s);
    }
    for(size_t n=1;n<=19U;n++) {
        char columns[256]="",values[1024]="";
        for(size_t i=0;i<n;i++) {
            size_t at=strlen(columns);snprintf(columns+at,sizeof(columns)-at,"%sc%zu",i?",":"",i);
            if(i)strcat(values,",");
            strcat(values,cells[i%COUNT(cells)]);
        }
        char *s=fixture(33U,4096U,"Db.Sch.Tab",columns,values,"");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);free(s);
    }
    {
        char *s=fixture(5000U,0U,"TEST_LIB.TEACHER_STATISTICS",
            "STAT_DATE,TEACHER_ID,TEACHER_NAME_ENCRYPT,PHONE_ENCRYPT,TOTAL_TEACHING,TOTAL_HOURS,CHECK_STATUS,CREATE_TIME,UPDATE_TIME",
            "'202505','T1001','张三李四','13800138000',20,100.50,0,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP","");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);free(s);
    }
    {
        char name[1025];memset(name,'Q',sizeof(name)-1U);name[sizeof(name)-1U]='\0';
        memcpy(name,"prefix_identity_",16U);
        char *s=fixture(32U,4096U,name,"a,b,c","'text',1,CURRENT_TIMESTAMP","");
        preprocess_parity(s,1,0);public_parity(s,0,NULL);free(s);
    }
}

/* Independent frozen rewrite inventory from the legacy SQLServer source;
 * do not reuse the optimized predicate or table to generate expectations. */
static const char *const hazards[]={
    "add","all","allow_encrypted_value_modifications","alter","ansi_defaults","ansi_null_dflt_off",
    "ansi_null_dflt_on","ansi_nulls","ansi_padding","ansi_warnings","application","apply",
    "approx_count_distinct","arithabort","arithignore","as","asymmetric","authorization",
    "auto","auto_drop","avg","begin","bit","break",
    "by","case","cast","certificate","check","checkpoint",
    "checksum_agg","collection","columns","commit","committed","compatibility_level",
    "compute","concat_null_yields_null","constraint","context_info","continue","conversation",
    "convert","cookie","count","count_big","create","cross",
    "cursor_close_on_commit","database","datefirst","dateformat","dbcc","deadlock_priority",
    "deallocate","declare","default","default_language","default_schema","delete",
    "deleted","deny","dialog","distinct","distributed","dmy",
    "double","drop","dym","else","end","entry",
    "except","exec","execute","exists","external","fetch",
    "fips_flagger","fmtonly","fn","for","forceplan","foreign",
    "from","full","fullscan","function","generated","go",
    "grant","group","grouping","grouping_id","having","high",
    "identity","identity_insert","if","implicit_transactions","increment","incremental",
    "index","inner","insert","inserted","intermediate","intersect",
    "into","isolation","isolation_level","join","json","key",
    "kill","language","left","level","limit","lock_timeout",
    "login","low","max","maxdop","mdy","member",
    "merge","min","myd","name","no","nocount",
    "noexec","norecompute","noreset","normal","not","null",
    "numeric_roundabort","object","object_id","off","offset","offsets",
    "on","open","opendatasource","openjson","openquery","openrowset",
    "openxml","option","order","outer","output","pagecount",
    "param","parse","parseonly","partition","password","path",
    "percent","persist_sample_percent","pg_catalog","pivot","precision","primary",
    "print","procedure","provider","query_governor_cost_limit","quoted_identifier","raiserror",
    "read","read_only","reconfigure","references","remote_proc_transactions","rename",
    "repeatable","resample","reset","result_set_caching","return","returning",
    "revert","revoke","right","role","rollback","rowcount",
    "sample","save","savepoint","schema","scheme","select",
    "serializable","set","setuser","showplan_all","showplan_text","showplan_xml",
    "shutdown","sid","snapshot","source","sp_execute","sp_executesql",
    "sp_prepare","sp_prepexec","sp_set_session_context","sp_unprepare","sqlserver","start",
    "state","statement","statistics","stats_stream","stdev","stdevp",
    "string_agg","sum","synonym","sys","table","target",
    "textsize","throw","ties","time","timer","to",
    "top","tran","transaction","transfer","trigger","truncate",
    "try","try_cast","try_convert","try_parse","type","uncommitted",
    "union","unique","unpivot","update","use","user",
    "using","value","values","var","variable","varp",
    "waitfor","where","while","with","without","xact_abort",
    "xml","ydm","ymd","zone",
};
static const char *const pg_keywords[]={
#define PG_KEYWORD(word,token,category,label) word,
#include "src/postgres/include/parser/kwlist.h"
#undef PG_KEYWORD
};
static void name_case(const char *name,int admit,int public_test)
{
    for(size_t position=0;position<4U;position++) {
        char table[1200],columns[1200];
        snprintf(table,sizeof(table),"%s.%s.%s",position==0?name:"db",position==1?name:"sch",position==2?name:"tab");
        snprintf(columns,sizeof(columns),"a,%s,c",position==3?name:"b");
        char *s=fixture(32U,4096U,table,columns,"'text',1,CURRENT_TIMESTAMP","");
        preprocess_parity(s,admit,0);if(public_test)public_parity(s,0,NULL);free(s);
    }
}
static void identifier_hazards(void)
{
    static const char *binary_names[]={"foo0xAB","foo0X1","foo0xG","foo0XG","0xAB", "prefix0xAB_suffix","identity_foo0xAB","foo0XAB_select"};
    static const char *sigil_names[]={"#tmp","##tmp","@var","$PARTITION","a$b","[name]","\"name\"","`name`","naïve"};
    stage="every SQLServer and PG keyword in each relation component and column";
    for(size_t list=0;list<2U;list++) {
        const char *const *words=list?pg_keywords:hazards;
        size_t n=list?COUNT(pg_keywords):COUNT(hazards);
        for(size_t i=0;i<n;i++) {
            char mixed[128],prefix[160],suffix[160],quoted[160];size_t len=strlen(words[i]);CHECK(len<sizeof(mixed));
            for(size_t j=0;j<=len;j++)mixed[j]=(char)(j%2U?toupper((unsigned char)words[i][j]):words[i][j]);
            snprintf(prefix,sizeof(prefix),"prefix_%s",words[i]);snprintf(suffix,sizeof(suffix),"%s_suffix",words[i]);
            name_case(words[i],0,0);name_case(mixed,0,0);name_case(prefix,1,0);name_case(suffix,1,0);
            snprintf(quoted,sizeof(quoted),"'%s'",words[i]);
            char *s=fixture(32U,4096U,"s.t","a",quoted,"");
            preprocess_parity(s,1,0);free(s);
        }
    }
    for(size_t i=0;i<COUNT(binary_names);i++)name_case(binary_names[i],0,1);
    for(size_t i=0;i<COUNT(sigil_names);i++)name_case(sigil_names[i],0,1);
    /* Byte-loop rewrite triggers and dangerous words with no token boundary. */
    name_case("bit",0,1);name_case("nchar",0,1);name_case("try_convert",0,1);
    name_case("prefix_bit_suffix",1,1);name_case("prefix_top_suffix",1,1);
}
static void exclusions(void)
{
    static const char *values[]={"'x\\n'","'a''b'","'a'\n'b'","N'x'","E'x'","U&'x'","$$x$$","\"x\"",
        "'\x80'","'\xc0\x80'","'\xed\xa0\x80'","'\xf4\x90\x80\x80'","'line\nline'","'line\tline'","'\x01'","'unterminated",
        "?","@param","@@version","$1",":bind","NULL","DEFAULT","TRUE","1+2","CURRENT_TIMESTAMP()","0xDEAD","(SELECT 1)","{fn ABS(1)}",
        "CURRENT_TIME(-1)","CURRENT_TIME(2147483648)","CURRENT_TIMESTAMP(-1)","CURRENT_TIMESTAMP(2147483648)",
        "LOCALTIME(-1)","LOCALTIME(2147483648)","LOCALTIMESTAMP(-1)","LOCALTIMESTAMP(2147483648)",
        "CURRENT_DATE(0)","CURRENT_ROLE(0)","CURRENT_USER(0)","USER(0)","CURRENT_SCHEMA(0)","SESSION_USER(0)","CURRENT_CATALOG(0)"};
    static const char *tails[]={";;",";SELECT 2"," OUTPUT inserted.a"," RETURNING *"," -- comment"," /* comment */",
        ",('x',1,2)"," junk"," OPTION(RECOMPILE)","\nGO\n",";USE example_db",";BEGIN SELECT 1 END",","," )",";\x01"};
    static const char *whole[]={
        "SELECT 'x'","SELECT TOP (2) a FROM t WITH (NOLOCK) OPTION (RECOMPILE)","SELECT CAST(1 AS BIT)",
        "SELECT TRY_CAST(N'1' AS INT),CONVERT(VARCHAR(20),GETDATE(),120),TRY_CONVERT(INT,'1'),PARSE('1' AS INT)",
        "SELECT 0xAB, @@version, @v, ?", "SELECT {fn ABS(-1)}", "SELECT a FROM t FOR JSON PATH",
        "SET NOCOUNT ON", "USE example_db", "PREPARE x AS SELECT 1", "EXEC sp_executesql N'SELECT @x',N'@x int',@x=1",
        "EXEC sp_prepare @h OUTPUT,N'@x int',N'SELECT @x'", "EXEC sp_execute @h,1", "EXEC sp_unprepare @h",
        "INSERT t(a) VALUES ('x')", "INSERT INTO t(a) OUTPUT inserted.a VALUES ('x')",
        "INSERT INTO t(a) OUTPUT inserted.a INTO sink(a) VALUES ('x')", "INSERT INTO t WITH (TABLOCK)(a) VALUES ('x')",
        "INSERT INTO t(a) SELECT a FROM u", "INSERT INTO t DEFAULT VALUES", "INSERT TOP (1) INTO t(a) VALUES ('x')",
        "CREATE TABLE t(a INT IDENTITY(1,1),b BIT,c NVARCHAR(40))", "DROP USER test_user", "RENAME OBJECT s.t TO other_t",
        "SAVE TRANSACTION point_one", "IF 1=1 BEGIN SELECT 1 END ELSE SELECT 2", "WHILE 1=0 BEGIN SELECT 1 END",
        "BEGIN TRY SELECT 1 END TRY BEGIN CATCH SELECT 2 END CATCH", "SELECT 1\nGO\nSELECT 2", "SELECT [a] FROM [s].[t]"
    };
    stage="lexical, control, bracket, sigil, session, DDL, OUTPUT, prepared and malformed fallback";
    for(size_t i=0;i<COUNT(values);i++) {
        char row[256];snprintf(row,sizeof(row),"%s,1,CURRENT_DATE",values[i]);
        char *s=fixture(32U,4096U,"s.t","a,b,c",row,"");
        preprocess_parity(s,0,0);public_parity(s,0,NULL);free(s);
    }
    for(size_t i=0;i<COUNT(tails);i++) {
        char *s=fixture(32U,4096U,"s.t","a,b,c","'x',1,CURRENT_TIMESTAMP",tails[i]);
        preprocess_parity(s,i==6U,0);public_parity(s,0,NULL);free(s);
    }
    for(size_t i=0;i<COUNT(whole);i++) {
        preprocess_parity(whole[i],0,0);preprocess_parity(whole[i],0,1);public_parity(whole[i],0,NULL);
    }
    {
        char *s=fixture(32U,4096U,"s.t","a,b,c","'x',1,CURRENT_TIMESTAMP","");
        char *into=strstr(s,"INTO ");CHECK(into);memmove(into,into+5U,strlen(into+5U)+1U);
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
            sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_SQLSERVER;
            o.limits.max_sql_bytes=n+delta;public_parity(s,0,&o);
            o.limits.max_sql_bytes=n+1;o.limits.max_output_bytes=n+delta;public_parity(s,0,&o);
        }
        free(s);
    }
    {
        PgQueryIdentityScalarInsertProof p={1U,1U,1U,1U,1},zero={0};
        CHECK(!sqlparser_sqlserver_prove_identity_scalar_insert(NULL,&p));CHECK(!memcmp(&p,&zero,sizeof(p)));
        CHECK(!sqlparser_sqlserver_prove_identity_scalar_insert("SELECT 1",NULL));
    }
}

static void fragment_and_arguments(void)
{
    static const char *fragments[]={"'plain'","N'national'","'back\\slash'","'one''two'","?","CURRENT_TIMESTAMP(6)"};
    stage="fragment and invalid-argument routes remain unchanged";
    for(size_t i=0;i<COUNT(fragments);i++) {
        sqlparser_sqlserver_state_t *a=NULL,*b=NULL;char *as=NULL,*bs=NULL;
        sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;size_t calls=proof_calls;
        CHECK(sqlparser_sqlserver_state_new(&a,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_sqlserver_state_new(&b,&be)==SQLPARSER_STATUS_OK);
        force_reference=0;ar=sqlparser_sqlserver_preprocess_fragment(fragments[i],a,0U,&as,&ae);
        force_reference=1;br=sqlparser_sqlserver_preprocess_fragment(fragments[i],b,0U,&bs,&be);force_reference=0;
        CHECK(proof_calls==calls);CHECK(ar==br);CHECK(!memcmp(&ae,&be,sizeof(ae)));
        text_equal(as,bs);state_equal(a,b);free(as);free(bs);
        sqlparser_sqlserver_state_destroy(a);sqlparser_sqlserver_state_destroy(b);++cases;
    }
    preprocess_parity(NULL,0,0);
    for(int output=0;output<2;output++) {
        char *as=NULL,*bs=NULL;void *a=NULL,*b=NULL;sqlparser_error_t ae={0},be={0};
        sqlparser_status_t ar,br;size_t calls=proof_calls,allocs=allocation_calls;
        force_reference=0;ar=sqlparser_sqlserver_preprocess("INSERT",NULL,output?&as:NULL,output?NULL:&a,&ae);
        force_reference=1;br=sqlparser_sqlserver_preprocess("INSERT",NULL,output?&bs:NULL,output?NULL:&b,&be);force_reference=0;
        CHECK(proof_calls==calls&&allocation_calls==allocs);CHECK(ar==br&&ar==SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(!memcmp(&ae,&be,sizeof(ae)));CHECK(!as&&!bs&&!a&&!b);++cases;
    }
}


#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
static void allocation_failures(void)
{
    static const char *rows[]={"'text',100.50,CURRENT_TIMESTAMP", "'@ exec use output top cross apply',100.50,CURRENT_TIMESTAMP"};
    size_t total_boundaries=0U;
    for(size_t sample=0;sample<COUNT(rows);sample++) {
        char *sql=fixture(32U,4096U,"s.t","a,b,c",rows[sample],"");size_t boundaries=0U;
        stage="allocation-free whole-source recognition";
        {
            PgQueryIdentityScalarInsertProof p;size_t before=allocation_calls;
            arm(1U);CHECK(sqlparser_sqlserver_prove_identity_scalar_insert(sql,&p));armed=0;
            CHECK(attempts==0U&&injected==0U&&allocation_calls==before&&!live);
        }
        stage="identity state/copy/unsupported-mask allocation boundaries";
        for(size_t at=0;at<=boundaries;at++) {
            char *out=NULL;void *state=NULL;sqlparser_error_t e={0};sqlparser_parse_options_t o;
            sqlparser_status_t status;sqlparser_parse_options_default(&o);force_reference=0;arm(at);
            status=sqlparser_sqlserver_preprocess(sql,&o.limits,&out,&state,&e);armed=0;
            if(!at){boundaries=attempts;CHECK(boundaries==2U+sample);CHECK(status==SQLPARSER_STATUS_OK);}
            else {CHECK(injected==1U);CHECK(status==SQLPARSER_STATUS_NO_MEMORY);CHECK(!out&&!state);CHECK(e.code==SQLPARSER_STATUS_NO_MEMORY);}
            free(out);sqlparser_sqlserver_state_destroy(state);CHECK(!live);preprocess_parity(sql,1,0);
        }
        total_boundaries+=boundaries;
        if(sample==0U) {
            stage="public identity parse allocation failures and recovery";boundaries=0U;
            size_t legacy_unpack_start,legacy_unpack_count;
            {
                sqlparser_handle_t *h=NULL;sqlparser_error_t e={0};sqlparser_parse_options_t o;
                sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_SQLSERVER;
                sqlparser_pg_query_prepare();force_reference=1;arm(0U);
                CHECK(sqlparser_parse_with_options(sql,&o,&h,&e)==SQLPARSER_STATUS_OK);
                armed=0;force_reference=0;
                legacy_unpack_start=unpack_start;legacy_unpack_count=unpack_end-unpack_start;
                CHECK(legacy_unpack_count>0U);sqlparser_handle_destroy(h);CHECK(!live);
            }
            for(size_t at=0;at<=boundaries;at++) {
                sqlparser_handle_t *h=NULL;sqlparser_error_t e={0};sqlparser_parse_options_t o;sqlparser_status_t status;
                sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_SQLSERVER;
                sqlparser_pg_query_prepare();force_reference=0;arm(at);
                status=sqlparser_parse_with_options(sql,&o,&h,&e);armed=0;
                if(!at){boundaries=attempts;CHECK(status==SQLPARSER_STATUS_OK&&h);}
                else {
                    CHECK(injected==1U);
                    if(status==SQLPARSER_STATUS_OK) {
                        sqlparser_handle_t *ref=NULL;sqlparser_error_t re={0};
                        CHECK(h&&!h->native_scalar_provenance);
                        force_reference=1;CHECK(sqlparser_parse_with_options(sql,&o,&ref,&re)==SQLPARSER_STATUS_OK);force_reference=0;
                        handle_equal(h,ref);state_equal(h->dialect_state,ref->dialect_state);sqlparser_handle_destroy(ref);
                    } else {
                        CHECK(!h);
                        if(failure_in_unpack) {
                            /* Historical SQLServer validation reports failed protobuf
                             * unpack as INTERNAL_ERROR. Pair the precise allocation
                             * with the independent legacy route, not a broad waiver. */
                            sqlparser_handle_t *ref=NULL;sqlparser_error_t re={0};sqlparser_status_t rs;
                            size_t relative=at-unpack_start;CHECK(relative>0U&&relative<=legacy_unpack_count);
                            CHECK(status==SQLPARSER_STATUS_INTERNAL_ERROR&&e.code==status);
                            CHECK(strcmp(e.message,"failed to unpack parse tree protobuf")==0);CHECK(!live);
                            force_reference=1;arm(legacy_unpack_start+relative);
                            rs=sqlparser_parse_with_options(sql,&o,&ref,&re);armed=0;force_reference=0;
                            CHECK(injected==1U&&failure_in_unpack&&rs==status&&!ref);
                            CHECK(!memcmp(&e,&re,sizeof(e)));CHECK(!live);
                        } else {CHECK(status==SQLPARSER_STATUS_NO_MEMORY&&e.code==status);}

                    }
                }
                sqlparser_handle_destroy(h);CHECK(!live);public_one(sql,0,NULL,SQLPARSER_DIALECT_SQLSERVER);
            }
            printf("SQLServer identity public allocation boundaries=%zu zero_live=yes\n",boundaries);
        }
        free(sql);
    }
    CHECK(total_boundaries==5U);
    printf("SQLServer identity preprocessing allocations: plain=2 raw-mask=3 zero_live=yes\n");
}
#endif
static void external_fixture(const char *path)
{
    FILE *f=fopen(path,"rb");long length;char *sql;
    CHECK(f);CHECK(fseek(f,0,SEEK_END)==0);length=ftell(f);CHECK(length>0&&length<16L*1024L*1024L);
    CHECK(fseek(f,0,SEEK_SET)==0);sql=malloc((size_t)length+1U);CHECK(sql);
    CHECK(fread(sql,1,(size_t)length,f)==(size_t)length);CHECK(fclose(f)==0);sql[length]='\0';
    CHECK(strlen(sql)==(size_t)length);stage="optional supplied actual fixture";
    preprocess_parity(sql,1,0);public_parity(sql,1,NULL);free(sql);
}
int main(int argc,char **argv)
{
    CHECK(argc<=2);
    fprintf(stderr,"SQLServer identity: positives\n");positives();
    fprintf(stderr,"SQLServer identity: hazards\n");identifier_hazards();
    fprintf(stderr,"SQLServer identity: fallbacks\n");exclusions();
    fprintf(stderr,"SQLServer identity: bounds and fragments\n");bounds();fragment_and_arguments();
    if(argc==2){fprintf(stderr,"SQLServer identity: actual fixture\n");external_fixture(argv[1]);}
#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
    fprintf(stderr,"SQLServer identity: allocation faults\n");allocation_failures();
#endif
    pg_query_exit();printf("SQLServer identity preprocessing passed: %zu complete state/SQL/public cases; %zu proof calls\n",cases,proof_calls);return 0;
}
