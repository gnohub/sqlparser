/* The reference disables ONLY the new admission call. All old preprocessing
 * algorithms run independently, with their real private state visible here.
 * Public entry points link to these same instrumented Dameng dialect ops.
 * No native parser, wire codec, graph, or patch implementation is replaced. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sqlparser_internal.h"
#include "sqlparser_dialect_dml_result_internal.h"
#include "src/pg_query_observer.h"

static const char *stage = "start";
static size_t cases, proof_calls, proof_successes, allocation_calls;
static int force_reference;
static size_t returning_validations;
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
    if (result) ++proof_successes;
    return result;
}
static sqlparser_status_t returning_validate_probe(sqlparser_dialect_t dialect,
    const char *sql,int allow_return,int allow_plain,sqlparser_error_t *error)
{
    ++returning_validations;
    return sqlparser_dialect_returning_into_validate(dialect,sql,allow_return,allow_plain,error);
}
#define sqlparser_dialect_returning_into_validate returning_validate_probe
#define pg_query_prove_identity_scalar_insert identity_probe
#include "../../src/dialect/sqlparser_dialect_dameng.c"
#undef pg_query_prove_identity_scalar_insert
#undef sqlparser_dialect_returning_into_validate
static int prove(const char *sql,PgQueryIdentityScalarInsertProof *proof)
{return pg_query_prove_identity_scalar_insert(sql,sqlparser_dameng_identity_name,proof);}

#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
static int armed;
static unsigned native_depth, unpack_depth;
static size_t native_entries, parser_calls, unpack_calls, unpack_frees;
static size_t unpack_start, unpack_end;
static int failure_in_unpack;
static size_t fail_at, injected, attempts, live, ledger_end;
static void *ledger[16384];
struct MemoryContextData;
struct MemoryContextData *__real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(struct MemoryContextData *);
struct MemoryContextData *__wrap_pg_query_enter_memory_context(void)
{ ++native_entries; ++native_depth; return __real_pg_query_enter_memory_context(); }
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
    ++unpack_calls;
    if(armed)unpack_start=attempts;
    ++unpack_depth;tree=__real_pg_query__parse_result__unpack(allocator,n,data);--unpack_depth;
    if(armed)unpack_end=attempts;
    return tree;
}
void __real_pg_query__parse_result__free_unpacked(PgQuery__ParseResult *,ProtobufCAllocator *);
void __wrap_pg_query__parse_result__free_unpacked(PgQuery__ParseResult *tree,ProtobufCAllocator *allocator)
{++unpack_frees;__real_pg_query__parse_result__free_unpacked(tree,allocator);}
PgQueryProtobufParseResult __real_sqlparser_parse_protobuf_preserving_identifier_spelling(const char *);
PgQueryProtobufParseResult __wrap_sqlparser_parse_protobuf_preserving_identifier_spelling(const char *sql)
{++parser_calls;return __real_sqlparser_parse_protobuf_preserving_identifier_spelling(sql);}
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
static void multi_equal(const sqlparser_dialect_multi_insert_t *a,const sqlparser_dialect_multi_insert_t *b)
{
    sqlparser_dialect_multi_insert_t x,y;
    CHECK((a==NULL)==(b==NULL));if(!a)return;x=*a;y=*b;
#define TEXT(m) text_equal(a->m,b->m);x.m=y.m=NULL
    TEXT(source_public_sql);TEXT(source_parser_sql);
#undef TEXT
#define CLEAR(m) CHECK((a->m==NULL)==(b->m==NULL));x.m=y.m=NULL
    CLEAR(branches);CLEAR(oracle_spans);CLEAR(oracle_pending_ids);
    CLEAR(oracle_source_provenance.sql);CLEAR(oracle_source_provenance.parser_sql);
    CLEAR(oracle_source_provenance.wire);CLEAR(oracle_source_provenance.state);
#undef CLEAR
    CHECK(!memcmp(&x,&y,sizeof(x)));
    if(a->oracle_span_count)CHECK(!memcmp(a->oracle_spans,b->oracle_spans,a->oracle_span_count*sizeof(*a->oracle_spans)));
    if(a->oracle_pending_count)CHECK(!memcmp(a->oracle_pending_ids,b->oracle_pending_ids,a->oracle_pending_count*sizeof(*a->oracle_pending_ids)));
    for(size_t i=0;i<a->branch_count;i++) {
        const sqlparser_dialect_multi_insert_branch_t *c=&a->branches[i],*d=&b->branches[i];
        sqlparser_dialect_multi_insert_branch_t u=*c,v=*d;
#define TEXT(m) text_equal(c->m,d->m);u.m=v.m=NULL
        TEXT(relation.database_name);TEXT(relation.schema_name);TEXT(relation.table_name);
        TEXT(relation.link_name);TEXT(relation.link_sql);TEXT(relation.sql);
        TEXT(condition_public_sql);TEXT(condition_parser_sql);
#undef TEXT
        CHECK((c->columns==NULL)==(d->columns==NULL));u.columns=v.columns=NULL;
        CHECK((c->cells==NULL)==(d->cells==NULL));u.cells=v.cells=NULL;
        CHECK(!memcmp(&u,&v,sizeof(u)));
        for(size_t j=0;j<c->column_count;j++) {
            text_equal(c->columns[j].name,d->columns[j].name);text_equal(c->columns[j].sql,d->columns[j].sql);
        }
        for(size_t j=0;j<c->cell_count;j++) {
            sqlparser_dialect_multi_insert_value_t m=c->cells[j],n=d->cells[j];
#define TEXT(memb) text_equal(m.memb,n.memb);m.memb=n.memb=NULL
            TEXT(public_sql);TEXT(parser_sql);TEXT(literal_string_value);TEXT(literal_float_value);
            TEXT(literal.string_value);TEXT(literal.float_value);
#undef TEXT
            CHECK(!memcmp(&m,&n,sizeof(m)));
        }
    }
}
static void state_equal(const sqlparser_dameng_state_t *a,const sqlparser_dameng_state_t *b)
{
    sqlparser_dameng_state_t x,y;
    CHECK((a==NULL)==(b==NULL));if(!a)return;memcpy(&x,a,sizeof(x));memcpy(&y,b,sizeof(y));
#define CLEAR(m) CHECK((a->m==NULL)==(b->m==NULL));x.m=y.m=NULL
    CLEAR(bind_names);CLEAR(prepared_binds.names);CLEAR(top_restores);CLEAR(national_literals.items);
    CLEAR(minuses.items);CLEAR(multi_insert);CLEAR(dblink_relations);CLEAR(multi_updates);CLEAR(returning_into.items);
#undef CLEAR
    CHECK(!memcmp(&x,&y,sizeof(x))); /* All 38 flattened fields, including inactive capacities/checkpoints. */
    for(size_t i=0;i<a->bind_count;i++)text_equal(a->bind_names[i],b->bind_names[i]);
    for(size_t i=0;i<a->prepared_binds.count;i++)text_equal(a->prepared_binds.names[i],b->prepared_binds.names[i]);
#define OWNER(m) owner_equal((const ProtobufCMessage *)u.m,(const ProtobufCMessage *)v.m);u.m=v.m=NULL
#define TEXT(m) text_equal(u.m,v.m);u.m=v.m=NULL
    for(size_t i=0;i<a->top_count;i++) {
        sqlparser_dameng_top_restore_t u=a->top_restores[i],v=b->top_restores[i];
        TEXT(clause);OWNER(owner);CHECK(!memcmp(&u,&v,sizeof(u)));
    }
    for(size_t i=0;i<a->national_literals.count;i++) {
        sqlparser_dialect_national_literal_t u=a->national_literals.items[i],v=b->national_literals.items[i];
        TEXT(sql);TEXT(surface_sql);OWNER(owner);CHECK(!memcmp(&u,&v,sizeof(u)));
    }
    for(size_t i=0;i<a->minuses.count;i++) {
        sqlparser_dialect_minus_t u=a->minuses.items[i],v=b->minuses.items[i];
        OWNER(owner);CHECK(!memcmp(&u,&v,sizeof(u)));
    }
    for(size_t i=0;i<a->dblink_count;i++) {
        sqlparser_dameng_dblink_relation_t u=a->dblink_relations[i],v=b->dblink_relations[i];
        TEXT(parser_object_name);TEXT(public_object_name);TEXT(public_link_name);TEXT(public_object_sql);TEXT(public_link_sql);
        OWNER(owner);CHECK(!memcmp(&u,&v,sizeof(u)));
    }
    for(size_t i=0;i<a->multi_update_count;i++) {
        const sqlparser_dameng_multi_update_t *c=&a->multi_updates[i],*d=&b->multi_updates[i];
        sqlparser_dameng_multi_update_t u=*c,v=*d;
        TEXT(target_qualifier_sql);
        CHECK((c->relations==NULL)==(d->relations==NULL));u.relations=v.relations=NULL;
        CHECK((c->links==NULL)==(d->links==NULL));u.links=v.links=NULL;
        CHECK(!memcmp(&u,&v,sizeof(u)));
        for(size_t j=0;j<c->relation_count;j++) {
            text_equal(c->relations[j].qualifier_sql,d->relations[j].qualifier_sql);
            owner_equal((const ProtobufCMessage *)c->relations[j].owner,(const ProtobufCMessage *)d->relations[j].owner);
        }
        for(size_t j=0;j+1U<c->relation_count;j++) {
            CHECK(c->links[j].kind==d->links[j].kind);text_equal(c->links[j].keyword_sql,d->links[j].keyword_sql);
            owner_equal((const ProtobufCMessage *)c->links[j].condition_owner,(const ProtobufCMessage *)d->links[j].condition_owner);
        }
    }
#undef OWNER
#undef TEXT
    for(size_t i=0;i<a->returning_into.count;i++) {
        const sqlparser_dialect_returning_into_item_t *u=&a->returning_into.items[i],*v=&b->returning_into.items[i];
        CHECK(u->statement_index==v->statement_index&&u->pair_count==v->pair_count);
        CHECK(u->keyword_uppercase_mask==v->keyword_uppercase_mask&&u->into_uppercase_mask==v->into_uppercase_mask);
        CHECK(u->uses_return_keyword==v->uses_return_keyword);
    }
    multi_equal(a->multi_insert,b->multi_insert);
}
static void plain_state(const sqlparser_dameng_state_t *s,size_t strings)
{
    sqlparser_dameng_state_t zero={0};zero.national_literals.literal_count=strings;
    CHECK(s&&memcmp(s,&zero,sizeof(zero))==0);
    CHECK(sqlparser_dameng_state_is_plain_insert_strings(s,strings));
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
    sqlparser_status_t ar,br;size_t before=proof_successes,calls=proof_calls,validations=returning_validations;
    sqlparser_parse_options_default(&options);
    if(origins) {
        CHECK(sqlparser_identifier_origin_map_new_identity(strlen(sql),&am,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_identifier_origin_map_new_identity(strlen(sql),&bm,&be)==SQLPARSER_STATUS_OK);
    }
    force_reference=0;
    ar=origins?sqlparser_dameng_preprocess_identifier_origins(sql,&options.limits,&a,&as,am,&ae):
        sqlparser_dameng_preprocess(sql,&options.limits,&a,&as,&ae);
    if(origins)CHECK(proof_calls==calls);
    else if(admitted>=0) {
        if(proof_successes-before!=(size_t)admitted) {
            const char *q=sql;while(q&&*q==' ')++q;
            fprintf(stderr,"admission expected=%d actual=%zu SQL=%.350s\n",admitted,proof_successes-before,q?q:"NULL");
        }
        CHECK(proof_successes-before==(size_t)admitted);
    }
    if(sql) {
        CHECK(proof_calls-calls==(origins?0U:1U));
        CHECK(returning_validations-validations==(proof_successes>before?0U:1U));
    }
    force_reference=1;
    br=origins?sqlparser_dameng_preprocess_identifier_origins(sql,&options.limits,&b,&bs,bm,&be):
        sqlparser_dameng_preprocess(sql,&options.limits,&b,&bs,&be);
    force_reference=0;
    if(ar!=br||memcmp(&ae,&be,sizeof(ae)))fprintf(stderr,"preprocess status %d/%d errors %s/%s\n",ar,br,ae.message,be.message);
    CHECK(ar==br);CHECK(memcmp(&ae,&be,sizeof(ae))==0);text_equal(a,b);state_equal(as,bs);
    if(ar==SQLPARSER_STATUS_OK) {
        CHECK(a&&b&&a!=sql&&b!=sql);
        if(admitted==1&&!origins) {
            PgQueryIdentityScalarInsertProof proof;
            CHECK(prove(sql,&proof));
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
    free(a);free(b);sqlparser_dameng_state_destroy(as);sqlparser_dameng_state_destroy(bs);
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
    CHECK(!b->native_scalar_provenance);
    if (a->native_scalar_provenance) {
        CHECK(a->generation == 0UL);
        CHECK(a->native_scalar_provenance->proof.source_length == a->sql_len);
        CHECK(a->native_scalar_provenance->proof.row_count >= 32U);
    }
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

static const sqlparser_dialect_t dialects[]={SQLPARSER_DIALECT_DAMENG};
static void public_one(const char *sql,int patch,const sqlparser_parse_options_t *custom,sqlparser_dialect_t dialect)
{
    sqlparser_parse_options_t options;sqlparser_handle_t *a=NULL,*b=NULL;
    sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;
    char *owned=sql?copy(sql):NULL;
    sqlparser_parse_options_default(&options);if(custom)options=*custom;options.dialect=dialect;
#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
    size_t native0=native_entries,parser0=parser_calls,unpack0=unpack_calls,free0=unpack_frees;
#endif
    force_reference=0;ar=sqlparser_parse_with_options(owned,&options,&a,&ae);
#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
    size_t anative=native_entries-native0,aparse=parser_calls-parser0,aunpack=unpack_calls-unpack0,afree=unpack_frees-free0;
    native0=native_entries;parser0=parser_calls;unpack0=unpack_calls;free0=unpack_frees;
#endif
    force_reference=1;br=sqlparser_parse_with_options(owned,&options,&b,&be);force_reference=0;
#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
    CHECK(anative<=native_entries-native0&&aparse<=parser_calls-parser0);
    CHECK(aunpack<=unpack_calls-unpack0&&afree<=unpack_frees-free0);
    if(ar==SQLPARSER_STATUS_OK){
        CHECK(anative>=1U&&parser_calls-parser0>=1U&&unpack_calls-unpack0>=1U);
        if(a->native_scalar_provenance)CHECK(aparse==0U&&aunpack==0U&&afree==0U);
    }
#endif
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

static void lifetime_transitions(void)
{
    static const char *replacements[]={"'ordinary'","N'national-中'",":owned_bind","upper('expression')","q'[quote literal]'","''"};
    sqlparser_parse_options_t options;sqlparser_handle_t *a=NULL,*b=NULL,*ac=NULL,*bc=NULL;
    sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;
    char *sql=fixture(33U,4096U,"s.t","a,b,c","'text',1,CURRENT_TIMESTAMP","");
    stage="caller retirement, early independent clone, national/bind/expression/structural transitions";
    sqlparser_parse_options_default(&options);options.dialect=SQLPARSER_DIALECT_DAMENG;
    force_reference=0;CHECK(sqlparser_parse_with_options(sql,&options,&a,&ae)==SQLPARSER_STATUS_OK);
    force_reference=1;CHECK(sqlparser_parse_with_options(sql,&options,&b,&be)==SQLPARSER_STATUS_OK);force_reference=0;
    poison_free(sql);
    force_reference=0;CHECK(sqlparser_handle_clone(a,&ac,&ae)==SQLPARSER_STATUS_OK);
    force_reference=1;CHECK(sqlparser_handle_clone(b,&bc,&be)==SQLPARSER_STATUS_OK);force_reference=0;
    CHECK(ac->sql!=a->sql&&bc->sql!=b->sql&&ac->dialect_state!=a->dialect_state&&bc->dialect_state!=b->dialect_state);
    sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);a=ac;b=bc;
    handle_equal(a,b);state_equal(a->dialect_state,b->dialect_state);
    for(size_t i=0;i<COUNT(replacements)+2U;i++) {
        char *owned=i<COUNT(replacements)?copy(replacements[i]):NULL;
        sqlparser_literal_value_t literal={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value="typed-after-transition"};
        sqlparser_patch_t p[2]={
            {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][0]",.sql=owned},
            {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[1][0]",.literal=&literal}
        };
        if(i==COUNT(replacements))p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_ROW,.selector="stmt[0].insert_row[32]"};
        if(i==COUNT(replacements)+1U)p[0]=(sqlparser_patch_t){.op=SQLPARSER_PATCH_DELETE_COLUMN,.selector="stmt[0].insert_columns",.index=1U};
        sqlparser_patch_list_t list={p,COUNT(p)};
        force_reference=0;ar=sqlparser_apply_patch(a,&list,&ae);
        force_reference=1;br=sqlparser_apply_patch(b,&list,&be);force_reference=0;
        poison_free(owned);CHECK(ar==br&&ar==SQLPARSER_STATUS_OK);CHECK(!memcmp(&ae,&be,sizeof(ae)));
        handle_equal(a,b);state_equal(a->dialect_state,b->dialect_state);++cases;
    }
    sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);
}

static void positives(void)
{
    static const char *cells[]={"'张三李四'","0","2147483647","2147483648","-2147483648","- 7",".5","- .5E+3","100.50","1.e-2","CURRENT_TIMESTAMP","CURRENT_TIME","CURRENT_TIME(6)","SESSION_USER","CURRENT_CATALOG",
        "CURRENT_ROLE","CURRENT_USER","USER","CURRENT_SCHEMA","LOCALTIME","LOCALTIMESTAMP",
        "CURRENT_TIME(0)","CURRENT_TIME(2147483647)","CURRENT_TIMESTAMP(0)","CURRENT_TIMESTAMP(2147483647)",
        "LOCALTIME(0)","LOCALTIME(2147483647)","LOCALTIMESTAMP(0)","LOCALTIMESTAMP(2147483647)"};
    static const char *strings[]={"''","'UTF8 é € 😀'","'@link ? :bind $1 0xAB foo0xCD'",
        "'/* comment */ -- comment ; @link :1 ? \"quoted\" q N nq BEGIN PIVOT PROCEDURE RETURN RETURNING INTO'",
        "'connect by start with connect_by_root nocycle prior minus except limit select top set schema alter session exec sql CURRENT_TIMESTAMP()'"};
    stage="positive metadata, owned SQL, fresh state and public parity";
    for(size_t i=0;i<COUNT(cells);i++) {
        char row[1024];snprintf(row,sizeof(row),"'text',%s,CURRENT_DATE",cells[i]);
        char *s=fixture(32U,4096U,"Db.Sch.Tab","a,b,c",row,"; \t\n");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);free(s);
    }
    for(size_t i=0;i<COUNT(strings);i++) {
        char *s=fixture(32U,4096U,"S.T","a",strings[i],"");
        preprocess_parity(s,1,0);public_parity(s,1,NULL);preprocess_parity(s,1,1);free(s);
    }
    {
        char *s=fixture(32U,4096U,"db.sch.tab","a,b,c","- 1,.5,CURRENT_TIMESTAMP ( 6 )",";\r\n\t ");
        preprocess_parity(s,1,0);public_parity(s,0,NULL);free(s);
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

/* Independent frozen rewrite inventory from the legacy Dameng source;
 * do not reuse the current implementation's predicate or table to generate expectations. */
static const char *const hazards[]={
    "alter","begin","connect","connect_by_root","create","current_timestamp","except","exec","limit",
    "minus","nocycle","pivot","prior","procedure","return","returning","select","set","start"
};
static const char *const pg_keywords[]={
#define PG_KEYWORD(word,token,category,label) word,
#include "src/postgres/include/parser/kwlist.h"
#undef PG_KEYWORD
};
static void name_case(const char *name,int admit,int public_test)
{
    for(size_t parts=1U;parts<=3U;parts++)for(size_t position=0;position<=parts;position++) {
        char table[4000]="",columns[1200];
        for(size_t i=0;i<parts;i++) {
            if(i)strcat(table,".");
            strcat(table,i==position?name:"ordinary_name");
        }
        snprintf(columns,sizeof(columns),"a,%s,c",position==parts?name:"b");
        char *s=fixture(32U,4096U,table,columns,"'text',1,CURRENT_TIMESTAMP","");
        preprocess_parity(s,admit,0);if(public_test)public_parity(s,0,NULL);free(s);
    }
}
static void identifier_hazards(void)
{
    static const char *ordinary_names[]={"a0xAB","foo0X1","foo0xG","foo0XG","prefix0xAB_suffix","identity_foo0xAB","foo0XAB_select"};
    static const char *sigil_names[]={"#tmp","@var","$PARTITION","a$b","[name]","\"name\"","`name`","naïve","0xAB"};
    stage="every Dameng trigger in each one/two/three-part relation and column";
    for(size_t list=0;list<2U;list++) {
        const char *const *words=list?pg_keywords:hazards;
        size_t n=list?COUNT(pg_keywords):COUNT(hazards);
        for(size_t i=0;i<n;i++) {
            char mixed[128],prefix[160],suffix[160],quoted[160];size_t len=strlen(words[i]);CHECK(len<sizeof(mixed));
            for(size_t j=0;j<=len;j++)mixed[j]=(char)(j%2U?toupper((unsigned char)words[i][j]):words[i][j]);
            snprintf(prefix,sizeof(prefix),"prefix_%s",words[i]);snprintf(suffix,sizeof(suffix),"%s_suffix",words[i]);
            name_case(words[i],0,!list);name_case(mixed,0,0);name_case(prefix,1,0);name_case(suffix,1,0);
            snprintf(quoted,sizeof(quoted),"'%s'",words[i]);
            char *s=fixture(32U,4096U,"s.t","a",quoted,"");
            preprocess_parity(s,1,0);free(s);
        }
    }
    for(size_t i=0;i<COUNT(ordinary_names);i++)name_case(ordinary_names[i],1,1);
    for(size_t i=0;i<COUNT(sigil_names);i++)name_case(sigil_names[i],0,1);
    /* Bounded callback: no terminating byte is available or consulted. */
    for(size_t i=0;i<COUNT(hazards);i++) {
        size_t n=strlen(hazards[i]);char *p=malloc(n);CHECK(p);memcpy(p,hazards[i],n);
        CHECK(!sqlparser_dameng_identity_name(p,n));free(p);
    }
}
static void exclusions(void)
{
    static const char *values[]={"'x\\n'","'a''b'","'a'\n'b'","N'x'","n'x'","q'[x]'","Q'{x}'","nq'[x]'","NQ'(x)'","E'x'","U&'x'","$$x$$","\"x\"",
        "'\x80'","'\xc0\x80'","'\xed\xa0\x80'","'\xf4\x90\x80\x80'","'line\nline'","'line\tline'","'\x01'","'unterminated",
        "?","@param","@@version","$1",":bind",":1",":dotted.name","NULL","DEFAULT","TRUE","1+2","CURRENT_TIMESTAMP()","CURRENT_TIMESTAMP(/*trivia*/) ","0xDEAD","(SELECT 1)","{fn ABS(1)}","1::int","CAST(1 AS INT)","abs(1)","q'[unterminated","/*hint*/1","--comment\n1","1/*comment*/",
        "CURRENT_TIMESTAMP(+1)","CURRENT_TIMESTAMP(1.0)","CURRENT_TIMESTAMP( )","CURRENT_TIME(-1)","CURRENT_TIME(2147483648)","CURRENT_TIMESTAMP(-1)","CURRENT_TIMESTAMP(2147483648)",
        "LOCALTIME(-1)","LOCALTIME(2147483648)","LOCALTIMESTAMP(-1)","LOCALTIMESTAMP(2147483648)",
        "CURRENT_DATE(0)","CURRENT_ROLE(0)","CURRENT_USER(0)","USER(0)","CURRENT_SCHEMA(0)","SESSION_USER(0)","CURRENT_CATALOG(0)"};
    static const char *tails[]={";;",";SELECT 2"," RETURN a INTO :bind"," RETURNING *"," -- comment"," /* comment */",
        ",('x',1,2)"," junk"," RETURNING a INTO :bind","@remote",";SET SCHEMA demo",";BEGIN SELECT 1 END",","," )",";\x01",",('x',1)",",('x',1,2,3)"};
    static const char *whole[]={
        "SELECT 'x'", "SELECT TOP 2 a FROM t", "SELECT CURRENT_TIMESTAMP()",
        "SELECT q'[plain]',nq'[national]',N'national',:bind,? FROM t",
        "SELECT * FROM s.t@remote", "SELECT PRIOR a,CONNECT_BY_ROOT b FROM t START WITH a=1 CONNECT BY NOCYCLE PRIOR a=b",
        "SELECT 1 MINUS SELECT 2", "SELECT 1 EXCEPT SELECT 2", "SELECT * FROM t LIMIT 1,2",
        "SET SCHEMA demo", "ALTER SESSION SET CURRENT_SCHEMA=demo", "EXEC SQL PREPARE stmt FROM 'SELECT 1'",
        "EXEC SQL EXECUTE stmt", "EXEC SQL DEALLOCATE PREPARE stmt",
        "INSERT t(a) VALUES ('x')", "INSERT INTO t(a) VALUES ('x') RETURNING a INTO :bind",
        "INSERT INTO t(a) VALUES ('x') RETURN a INTO :bind", "INSERT INTO t(a) VALUES ('x') RETURNING a",
        "INSERT INTO t(a) SELECT a FROM u", "INSERT INTO t DEFAULT VALUES",
        "WITH x AS (SELECT 1) INSERT INTO t(a) VALUES ('x')", "/*+ hint */ INSERT INTO t(a) VALUES ('x')",
        "INSERT ALL INTO t(a) VALUES ('x') INTO u(b) VALUES (N'y') SELECT 1",
        "INSERT FIRST WHEN 1=1 THEN INTO t(a) VALUES (1) ELSE INTO u(b) VALUES (2) SELECT 1",
        "UPDATE a,b SET a.v=b.v WHERE a.k=b.k", "UPDATE a JOIN b ON a.k=b.k SET a.v=b.v",
        "CREATE PROCEDURE p AS BEGIN SELECT 1 END", "BEGIN SELECT 1 END", "SELECT * FROM t PIVOT(a)",
        "INSERT INTO prior(a) VALUES ('unterminated", "INSERT INTO pivot(a) VALUES ('x') RETURNING a",
        "INSERT INTO t(a) VALUES (nq'[unterminated) RETURNING a", "INSERT INTO t(a) VALUES ('x'); BEGIN SELECT 1 END"
    };
    stage="national/quoted literals, hierarchy, links, binds, comments, multi-DML, malformed and error-order fallback";
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
        size_t length=strlen(whole[i]);char *padded=malloc(4096U+length+1U);CHECK(padded);
        memset(padded,' ',4096U);memcpy(padded+4096U,whole[i],length+1U);
        preprocess_parity(padded,0,0);public_parity(padded,0,NULL);free(padded);
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
            sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_DAMENG;
            o.limits.max_sql_bytes=n+delta;public_parity(s,0,&o);
            o.limits.max_sql_bytes=n+1;o.limits.max_output_bytes=n+delta;public_parity(s,0,&o);
        }
        free(s);
    }
    {
        PgQueryIdentityScalarInsertProof p={1U,1U,1U,1U,1},zero={0};
        CHECK(!prove(NULL,&p));CHECK(!memcmp(&p,&zero,sizeof(p)));
        CHECK(!prove("SELECT 1",NULL));
    }
}

static void fragment_and_arguments(void)
{
    static const char *fragments[]={"'plain'","N'national'","nq'[national]'","q'[plain]'","'back\\slash'","'one''two'","?",":bind","CURRENT_TIMESTAMP(6)","CURRENT_TIMESTAMP()","PRIOR a","'unterminated"};
    stage="fragment and invalid-argument routes remain unchanged";
    for(size_t i=0;i<COUNT(fragments);i++) {
        sqlparser_dameng_state_t *a=NULL,*b=NULL;char *as=NULL,*bs=NULL;
        sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;size_t calls=proof_calls;
        CHECK(sqlparser_dameng_state_new(&a,&ae)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_dameng_state_new(&b,&be)==SQLPARSER_STATUS_OK);
        force_reference=0;ar=sqlparser_dameng_preprocess_fragment(fragments[i],a,0U,&as,&ae);
        force_reference=1;br=sqlparser_dameng_preprocess_fragment(fragments[i],b,0U,&bs,&be);force_reference=0;
        CHECK(proof_calls==calls);CHECK(ar==br);CHECK(!memcmp(&ae,&be,sizeof(ae)));
        text_equal(as,bs);state_equal(a,b);free(as);free(bs);
        sqlparser_dameng_state_destroy(a);sqlparser_dameng_state_destroy(b);++cases;
    }
    {
        char *sql=fixture(32U,4096U,"s.t","a,b,c","'text',1,CURRENT_TIMESTAMP","");
        char *a=NULL,*b=NULL;void *as=NULL,*bs=NULL;sqlparser_error_t ae={0},be={0};
        force_reference=0;CHECK(sqlparser_dameng_preprocess(sql,NULL,&a,&as,&ae)==SQLPARSER_STATUS_OK);
        force_reference=1;CHECK(sqlparser_dameng_preprocess(sql,NULL,&b,&bs,&be)==SQLPARSER_STATUS_OK);force_reference=0;
        text_equal(a,b);state_equal(as,bs);CHECK(!memcmp(&ae,&be,sizeof(ae)));
        free(a);free(b);sqlparser_dameng_state_destroy(as);sqlparser_dameng_state_destroy(bs);free(sql);++cases;
    }
    preprocess_parity(NULL,0,0);
    for(int output=0;output<2;output++) {
        char *as=NULL,*bs=NULL;void *a=NULL,*b=NULL;sqlparser_error_t ae={0},be={0};
        sqlparser_status_t ar,br;size_t calls=proof_calls,allocs=allocation_calls;
        force_reference=0;ar=sqlparser_dameng_preprocess("INSERT",NULL,output?&as:NULL,output?NULL:&a,&ae);
        force_reference=1;br=sqlparser_dameng_preprocess("INSERT",NULL,output?&bs:NULL,output?NULL:&b,&be);force_reference=0;
        CHECK(proof_calls==calls&&allocation_calls==allocs);CHECK(ar==br&&ar==SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(!memcmp(&ae,&be,sizeof(ae)));CHECK(!as&&!bs&&!a&&!b);++cases;
    }
}


#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
static void allocation_failures(void)
{
    static const char *rows[]={"'text',100.50,CURRENT_TIMESTAMP", "'@ exec minus nocycle prior return returning into pivot',100.50,CURRENT_TIMESTAMP"};
    size_t total_boundaries=0U;
    for(size_t sample=0;sample<COUNT(rows);sample++) {
        char *sql=fixture(32U,4096U,"s.t","a,b,c",rows[sample],"");size_t boundaries=0U;
        stage="allocation-free whole-source recognition";
        {
            PgQueryIdentityScalarInsertProof p;size_t before=allocation_calls;
            arm(1U);CHECK(prove(sql,&p));armed=0;
            CHECK(attempts==0U&&injected==0U&&allocation_calls==before&&!live);
        }
        stage="identity state and owned-copy allocation boundaries";
        for(size_t at=0;at<=boundaries;at++) {
            char *out=(char *)(uintptr_t)1U;void *state=(void *)(uintptr_t)1U;
            sqlparser_error_t e={0};sqlparser_parse_options_t o;
            sqlparser_status_t status;sqlparser_parse_options_default(&o);force_reference=0;arm(at);
            status=sqlparser_dameng_preprocess(sql,&o.limits,&out,&state,&e);armed=0;
            if(!at){boundaries=attempts;CHECK(boundaries==2U);CHECK(status==SQLPARSER_STATUS_OK);}
            else {
                CHECK(injected==1U);CHECK(status==SQLPARSER_STATUS_NO_MEMORY);CHECK(!out&&!state);CHECK(e.code==SQLPARSER_STATUS_NO_MEMORY);
                char *reference=NULL;void *rs=NULL;sqlparser_error_t re={0};
                force_reference=1;arm(at);
                CHECK(sqlparser_dameng_preprocess(sql,&o.limits,&reference,&rs,&re)==status);
                armed=0;force_reference=0;CHECK(injected==1U&&!reference&&!rs&&!live);
                CHECK(!memcmp(&e,&re,sizeof(e)));
            }
            free(out);sqlparser_dameng_state_destroy(state);CHECK(!live);preprocess_parity(sql,1,0);
        }
        total_boundaries+=boundaries;
        if(sample==0U) {
            stage="public identity parse allocation failures and recovery";boundaries=0U;
            size_t legacy_unpack_start,legacy_unpack_count;
            {
                sqlparser_handle_t *h=NULL;sqlparser_error_t e={0};sqlparser_parse_options_t o;
                sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_DAMENG;
                sqlparser_pg_query_prepare();force_reference=1;arm(0U);
                CHECK(sqlparser_parse_with_options(sql,&o,&h,&e)==SQLPARSER_STATUS_OK);
                armed=0;force_reference=0;
                legacy_unpack_start=unpack_start;legacy_unpack_count=unpack_end-unpack_start;
                CHECK(legacy_unpack_count>0U);sqlparser_handle_destroy(h);CHECK(!live);
            }
            for(size_t at=0;at<=boundaries;at++) {
                sqlparser_handle_t *h=NULL;sqlparser_error_t e={0};sqlparser_parse_options_t o;sqlparser_status_t status;
                sqlparser_parse_options_default(&o);o.dialect=SQLPARSER_DIALECT_DAMENG;
                sqlparser_pg_query_prepare();force_reference=0;arm(at);
                status=sqlparser_parse_with_options(sql,&o,&h,&e);armed=0;
                if(!at){boundaries=attempts;CHECK(status==SQLPARSER_STATUS_OK&&h);}
                else {
                    CHECK(injected==1U);
                    if(status==SQLPARSER_STATUS_OK) {
                        sqlparser_handle_t *ref=NULL;sqlparser_error_t re={0};
                        CHECK(h);
                        if (h->native_scalar_provenance)
                            CHECK(h->native_scalar_provenance->proof.source_length == h->sql_len);
                        force_reference=1;CHECK(sqlparser_parse_with_options(sql,&o,&ref,&re)==SQLPARSER_STATUS_OK);force_reference=0;
                        handle_equal(h,ref);state_equal(h->dialect_state,ref->dialect_state);sqlparser_handle_destroy(ref);
                    } else {
                        CHECK(!h);
                        if(failure_in_unpack) {
                            /* Historical Dameng validation reports failed protobuf
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
                sqlparser_handle_destroy(h);CHECK(!live);public_one(sql,0,NULL,SQLPARSER_DIALECT_DAMENG);
            }
            printf("Dameng identity public allocation boundaries=%zu zero_live=yes\n",boundaries);
        }
        free(sql);
    }
    CHECK(total_boundaries==4U);
    printf("Dameng identity preprocessing allocations: plain=2 keyword-string=2 zero_live=yes\n");
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
    fprintf(stderr,"Dameng identity: positives\n");positives();
    fprintf(stderr,"Dameng identity: hazards\n");identifier_hazards();
    fprintf(stderr,"Dameng identity: fallbacks\n");exclusions();
    fprintf(stderr,"Dameng identity: bounds, fragments and lifetime\n");bounds();fragment_and_arguments();lifetime_transitions();
    if(argc==2){fprintf(stderr,"Dameng identity: actual fixture\n");external_fixture(argv[1]);}
#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
    fprintf(stderr,"Dameng identity: allocation faults\n");allocation_failures();
#endif
    pg_query_exit();printf("Dameng identity preprocessing passed: %zu complete state/SQL/public cases; %zu proof calls\n",cases,proof_calls);return 0;
}
