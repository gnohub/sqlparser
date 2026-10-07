/* Validation regression: reuse the independent identity/state/guard
 * suite, then isolate ordinary-grammar writer certification and its allocator
 * boundaries. This is correctness instrumentation, never a benchmark.
 *
 * The included production parser is compiled with -finstrument-functions so
 * its two private constructors can be counted without a production test hook.
 * All exported parser bodies are unchanged. The archive supplies the ordinary
 * grammar, converter and remaining implementation. */
#define main sqlparser_identity_reference_suite_main
#include "test_sqlserver_identity_preprocess.c"
#undef main

#ifdef SQLPARSER_VALIDATION_PROOF_WRAPPERS
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wclobbered"
#endif
#include "../../vendor/libpg_query/src/pg_query_parse.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "sqlparser_wire_insert_internal.h"

typedef struct {
    size_t grammar, simple_constructor, scalar_constructor, ordinary_entry, native_entry;
    size_t certified_calls, certified_hits, unpack, unpack_free, observed;
    size_t control_take, bind_reset, owner_reset, strict_graph, native_graph, native_graph_hits;
} vp_routes_t;
static vp_routes_t vp_routes;
static int vp_count_routes, vp_converter_depth, vp_native_depth;
enum { VP_NORMAL, VP_FORCE_OBSERVED, VP_NO_CERTIFICATE_BACKEND };
static int vp_mode, vp_copy_owner, vp_bad_proof;
static sqlparser_validation_preprocess_fn vp_base_preprocess;

void __cyg_profile_func_enter(void *, void *) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *, void *) __attribute__((no_instrument_function));
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
void __cyg_profile_func_enter(void *fn, void *caller)
{
    (void)caller;
    if (!vp_count_routes) return;
    if (fn == (void *)pg_query_try_simple_insert) ++vp_routes.simple_constructor;
    if (fn == (void *)pg_query_try_scalar_insert) ++vp_routes.scalar_constructor;
    if (fn == (void *)pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_ordinary)
        ++vp_routes.ordinary_entry;
    if (fn == (void *)pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_native)
        ++vp_routes.native_entry;
    if (fn == (void *)sqlparser_sqlserver_take_control_state) ++vp_routes.control_take;
    if (fn == (void *)sqlparser_sqlserver_bind_ast_state) ++vp_routes.bind_reset;
    if (fn == (void *)sqlparser_sqlserver_clear_ast_owners) ++vp_routes.owner_reset;
}
void __cyg_profile_func_exit(void *fn, void *caller) { (void)fn; (void)caller; }
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

List *__real_raw_parser_with_options(const char *, RawParseMode, bool);
List *__wrap_raw_parser_with_options(const char *sql, RawParseMode mode, bool preserve)
{
    if (vp_count_routes) ++vp_routes.grammar;
    return __real_raw_parser_with_options(sql, mode, preserve);
}
MemoryContext __real_pg_query_enter_memory_context(void);
void __real_pg_query_exit_memory_context(MemoryContext);
MemoryContext __wrap_pg_query_enter_memory_context(void)
{ ++vp_native_depth; return __real_pg_query_enter_memory_context(); }
void __wrap_pg_query_exit_memory_context(MemoryContext ctx)
{ __real_pg_query_exit_memory_context(ctx); CHECK(vp_native_depth > 0); --vp_native_depth; }
PgQuery__ParseResult *__real_pg_query__parse_result__unpack(ProtobufCAllocator *, size_t, const uint8_t *);
PgQuery__ParseResult *__wrap_pg_query__parse_result__unpack(ProtobufCAllocator *a, size_t n, const uint8_t *p)
{ if (vp_count_routes) ++vp_routes.unpack; return __real_pg_query__parse_result__unpack(a,n,p); }
void __real_pg_query__parse_result__free_unpacked(PgQuery__ParseResult *, ProtobufCAllocator *);
void __wrap_pg_query__parse_result__free_unpacked(PgQuery__ParseResult *p, ProtobufCAllocator *a)
{ if (vp_count_routes) ++vp_routes.unpack_free; __real_pg_query__parse_result__free_unpacked(p,a); }
static void vp_observe(const PgQuery__ParseResult *tree, void *context)
{ (void)context; CHECK(tree != NULL); if (vp_count_routes) ++vp_routes.observed; }
PgQueryProtobufParseResult __real_sqlparser_parse_protobuf_preserving_identifier_spelling(const char *);
PgQueryProtobufParseResult __wrap_sqlparser_parse_protobuf_preserving_identifier_spelling(const char *sql)
{
    /* The reused reference disables identity preprocessing as before, and
     * also explicitly bypasses all direct writers with a real observer. */
    if (force_reference) return pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        sql, PG_QUERY_PARSE_DEFAULT, vp_observe, NULL);
    return __real_sqlparser_parse_protobuf_preserving_identifier_spelling(sql);
}
PgQueryProtobuf __real_pg_query_nodes_to_protobuf_observed(const void *, PgQueryProtobufObserver, void *);
PgQueryProtobuf __real_pg_query_nodes_to_protobuf_certified(const void *, PgQueryProtobufObserver, void *, size_t *, int *);
PgQueryProtobuf __wrap_pg_query_nodes_to_protobuf_observed(const void *tree, PgQueryProtobufObserver observer, void *context)
{
    PgQueryProtobuf result; ++vp_converter_depth;
    result = __real_pg_query_nodes_to_protobuf_observed(tree,observer,context);
    --vp_converter_depth; return result;
}
PgQueryProtobuf __wrap_pg_query_nodes_to_protobuf_certified(const void *tree, PgQueryProtobufObserver observer,
    void *context, size_t *count, int *certified)
{
    PgQueryProtobuf result; ++vp_converter_depth;
    if (vp_count_routes) ++vp_routes.certified_calls;
    if (vp_mode != VP_NORMAL) {
        *count = 0U; *certified = 0;
        /* NO_CERTIFICATE_BACKEND matches the C++ entry's exact fallback
         * contract. It is deliberately labelled emulation, not a C++ run. */
        result = __real_pg_query_nodes_to_protobuf_observed(tree,
            vp_mode == VP_FORCE_OBSERVED ? vp_observe : observer, context);
    } else result = __real_pg_query_nodes_to_protobuf_certified(tree,observer,context,count,certified);
    if (vp_count_routes && *certified) ++vp_routes.certified_hits;
    --vp_converter_depth; return result;
}

const sqlparser_dialect_ops_t *__real_sqlparser_dialect_get_ops(sqlparser_dialect_t);
const sqlparser_dialect_ops_t *__wrap_sqlparser_dialect_get_ops(sqlparser_dialect_t d)
{
    static sqlparser_dialect_ops_t copies[SQLPARSER_DIALECT_KINGBASE_SQLSERVER+1];
    const sqlparser_dialect_ops_t *ops = __real_sqlparser_dialect_get_ops(d);
    if (!vp_copy_owner || ops == NULL) return ops;
    CHECK(d >= SQLPARSER_DIALECT_POSTGRESQL && d <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER);
    copies[d] = *ops; return &copies[d];
}
static sqlparser_status_t vp_corrupt_proof(const char *sql, const sqlparser_limits_t *limits,
    char **parser_sql, void **state, PgQueryIdentityScalarInsertProof *proof, sqlparser_error_t *error)
{
    sqlparser_status_t status = vp_base_preprocess(sql,limits,parser_sql,state,proof,error);
    if (status == SQLPARSER_STATUS_OK && proof->row_count) {
        if (vp_bad_proof == 1) ++proof->source_length;
        else ++proof->string_count;
    }
    return status;
}
sqlparser_validation_preprocess_fn __real_sqlparser_dialect_validation_preprocessor(sqlparser_dialect_t, const sqlparser_dialect_ops_t *);
sqlparser_validation_preprocess_fn __wrap_sqlparser_dialect_validation_preprocessor(sqlparser_dialect_t d, const sqlparser_dialect_ops_t *ops)
{
    sqlparser_validation_preprocess_fn fn = __real_sqlparser_dialect_validation_preprocessor(d,ops);
    if (fn != NULL && vp_bad_proof) { vp_base_preprocess=fn; return vp_corrupt_proof; }
    return fn;
}
sqlparser_wire_scalar_insert_t *__real_sqlparser_wire_scalar_insert_certify(const sqlparser_handle_t *);
sqlparser_wire_scalar_insert_t *__wrap_sqlparser_wire_scalar_insert_certify(const sqlparser_handle_t *h)
{ if (vp_count_routes) ++vp_routes.strict_graph; return __real_sqlparser_wire_scalar_insert_certify(h); }
sqlparser_wire_scalar_insert_t *__real_sqlparser_wire_scalar_insert_from_native(const sqlparser_handle_t *);
sqlparser_wire_scalar_insert_t *__wrap_sqlparser_wire_scalar_insert_from_native(const sqlparser_handle_t *h)
{
    sqlparser_wire_scalar_insert_t *p=__real_sqlparser_wire_scalar_insert_from_native(h);
    if (vp_count_routes) { ++vp_routes.native_graph; if(p)++vp_routes.native_graph_hits; }
    return p;
}

/* These hooks target exactly the direct writer's native-context cache and
 * output boundaries. The ledger remains active through later handle teardown
 * and does NOT exclude allocations just because native_depth is nonzero. */
enum { VP_FAIL_CACHE=1, VP_FAIL_OUTPUT=2 };
static int vp_armed, vp_target;
static size_t vp_fail_at, vp_cache_calls, vp_output_calls, vp_injected, vp_live_blocks, vp_live_bytes;
static struct { void *pointer; size_t bytes; } vp_ledger[64];
static size_t vp_slot(void *p)
{
    for(size_t i=0U;p&&i<COUNT(vp_ledger);++i) if(vp_ledger[i].pointer==p)return i;
    return COUNT(vp_ledger);
}
static void vp_forget(void *p)
{
    size_t i=vp_slot(p); if(i==COUNT(vp_ledger))return;
    CHECK(vp_live_blocks>0U && vp_live_bytes>=vp_ledger[i].bytes);
    --vp_live_blocks;vp_live_bytes-=vp_ledger[i].bytes;vp_ledger[i].pointer=NULL;vp_ledger[i].bytes=0U;
}
static void vp_remember(void *p,size_t n)
{
    if(!p)return;
    CHECK(vp_slot(p)==COUNT(vp_ledger));
    for(size_t i=0U;i<COUNT(vp_ledger);++i)if(!vp_ledger[i].pointer){
        vp_ledger[i].pointer=p;vp_ledger[i].bytes=n;++vp_live_blocks;vp_live_bytes+=n;return;
    }
    CHECK(0);
}
void *__real_realloc(void *,size_t);
void __real_free(void *);
void *__real_pg_query_protobuf_alloc_output(size_t);
void *__wrap_realloc(void *p,size_t n)
{
    void *q;size_t old=vp_slot(p);int tracking=vp_armed&&vp_converter_depth>0;
    if(tracking){
        CHECK(vp_native_depth>0);++vp_cache_calls;
        if(vp_target==VP_FAIL_CACHE&&vp_cache_calls==vp_fail_at){++vp_injected;return NULL;}
    }
    q=__real_realloc(p,n);
    if(q||!n){
        if(old<COUNT(vp_ledger)){
            CHECK(vp_live_blocks>0U);--vp_live_blocks;vp_live_bytes-=vp_ledger[old].bytes;
            vp_ledger[old].pointer=NULL;vp_ledger[old].bytes=0U;
        }
        if(tracking||old<COUNT(vp_ledger))vp_remember(q,n);
    }
    return q;
}
void *__wrap_pg_query_protobuf_alloc_output(size_t n)
{
    void *p;
    if(vp_armed){
        CHECK(vp_converter_depth>0&&vp_native_depth>0);++vp_output_calls;
        if(vp_target==VP_FAIL_OUTPUT&&vp_output_calls==vp_fail_at){++vp_injected;return NULL;}
    }
    p=__real_pg_query_protobuf_alloc_output(n);if(vp_armed)vp_remember(p,n);return p;
}
void __wrap_free(void *p){vp_forget(p);__real_free(p);}
static void vp_arm(int target,size_t at)
{
    CHECK(!vp_armed&&!vp_converter_depth&&!vp_native_depth&&!vp_live_blocks&&!vp_live_bytes);
    vp_armed=1;vp_target=target;vp_fail_at=at;vp_cache_calls=vp_output_calls=vp_injected=0U;
}
static void vp_disarm(void)
{ vp_armed=0;CHECK(!vp_converter_depth&&!vp_native_depth); }
static void vp_start(void){CHECK(!vp_count_routes);memset(&vp_routes,0,sizeof(vp_routes));vp_count_routes=1;}
static vp_routes_t vp_stop(void){vp_count_routes=0;CHECK(!vp_converter_depth&&!vp_native_depth);return vp_routes;}
static void vp_initial_routes(vp_routes_t r,int eligible,int mode)
{
    CHECK(r.grammar==1U&&r.simple_constructor==0U&&r.scalar_constructor==0U&&r.native_entry==0U);
    CHECK(r.ordinary_entry==(size_t)eligible&&r.certified_calls==(size_t)eligible);
    CHECK(r.certified_hits==(size_t)(eligible&&mode==VP_NORMAL));
    CHECK(r.unpack==(size_t)(!eligible||mode!=VP_NORMAL));
    CHECK(r.unpack_free==r.unpack);
    CHECK(r.control_take==1U&&r.bind_reset==1U&&r.owner_reset==1U);
}
static sqlparser_handle_t *vp_parse(const char *sql,sqlparser_dialect_t dialect)
{
    sqlparser_parse_options_t o;sqlparser_handle_t *h=NULL;sqlparser_error_t e={0};
    sqlparser_parse_options_default(&o);o.dialect=dialect;
    CHECK(sqlparser_parse_with_options(sql,&o,&h,&e)==SQLPARSER_STATUS_OK);CHECK(h);return h;
}
static void vp_pair(const char *sql,sqlparser_dialect_t dialect,int eligible,int mode)
{
    char *owned=copy(sql);sqlparser_handle_t *a,*b;sqlparser_error_t e={0};vp_routes_t routes;
    stage="fresh ordinary grammar, canonical bytes, full state and forced AST graph parity";
    vp_mode=mode;vp_start();a=vp_parse(owned,dialect);routes=vp_stop();vp_mode=VP_NORMAL;
    vp_initial_routes(routes,eligible,mode);CHECK(!a->ast&&!a->native_scalar_provenance);
    force_reference=1;b=vp_parse(owned,dialect);force_reference=0;poison_free(owned);
    CHECK(!b->native_scalar_provenance);state_equal(a->dialect_state,b->dialect_state);
    CHECK(a->parse_tree.len==b->parse_tree.len&&!memcmp(a->parse_tree.data,b->parse_tree.data,a->parse_tree.len));
    CHECK(sqlparser_handle_ensure_ast(b,&e)==SQLPARSER_STATUS_OK); /* Independent graph route. */
    pg_query_exit();handle_equal(a,b);sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);++cases;
}
static void vp_route_boundaries(void)
{
    static const char *values[]={"'UTF8 é € 😀',0,CURRENT_DATE","'x',2147483647,CURRENT_TIME",
        "'x',2147483648,CURRENT_TIMESTAMP","'x',-2147483648,LOCALTIME","'x',- 7,LOCALTIMESTAMP",
        "'x',.5,SESSION_USER","'x',- .5E+3,CURRENT_SCHEMA","'x',100.50,USER","'x',1.e-2,CURRENT_ROLE",
        "'x',1,CURRENT_TIME(0)","'x',1,CURRENT_TIME(6)","'x',1,CURRENT_TIME(2147483647)",
        "'x',1,CURRENT_TIMESTAMP(0)","'x',1,CURRENT_TIMESTAMP(2147483647)",
        "'x',1,LOCALTIME(2147483647)","'x',1,LOCALTIMESTAMP(2147483647)"};
    for(size_t d=0U;d<COUNT(dialects);++d){
        for(size_t r=31U;r<=32U;++r){char *s=fixture(r,4096U,"Db.Sch.Tab","a,b,c","'x',1,CURRENT_DATE","");vp_pair(s,dialects[d],r==32U,VP_NORMAL);free(s);}
        for(size_t n=4095U;n<=4096U;++n){
            char *base=fixture(32U,0U,"s.t","a,b,c","'x',1,CURRENT_DATE","");size_t pad=n-strlen(base);free(base);
            char *s=fixture(32U,pad,"s.t","a,b,c","'x',1,CURRENT_DATE","");CHECK(strlen(s)==n);vp_pair(s,dialects[d],n==4096U,VP_NORMAL);free(s);
        }
        for(size_t i=0U;i<COUNT(values);++i){
            char *s=fixture(32U,4096U,"Db.Sch.Tab","a,b,c",values[i],"; \t\n");
            vp_pair(s,dialects[d],1,VP_NORMAL);free(s);
        }
        {
            char *s=fixture(128U,0U,"s.t","a,b,c","'abcdefghijklmnopqrstuvwxyz0123456789',1,CURRENT_DATE","");
            vp_pair(s,dialects[d],1,VP_FORCE_OBSERVED);vp_pair(s,dialects[d],1,VP_NO_CERTIFICATE_BACKEND);
            for(vp_bad_proof=1;vp_bad_proof<=2;++vp_bad_proof)vp_pair(s,dialects[d],0,VP_NORMAL);
            vp_bad_proof=0;vp_copy_owner=1;vp_pair(s,dialects[d],0,VP_NORMAL);vp_copy_owner=0;free(s);
        }
    }
}
static void vp_exact_owners(void)
{
    stage="private validation callback rejects copied or mismatched registered owners";
    for(size_t i=0U;i<COUNT(dialects);++i){
        const sqlparser_dialect_ops_t *ops=__real_sqlparser_dialect_get_ops(dialects[i]);sqlparser_dialect_ops_t copy_ops=*ops;
        CHECK(__real_sqlparser_dialect_validation_preprocessor(dialects[i],ops)!=NULL);
        CHECK(__real_sqlparser_dialect_validation_preprocessor(dialects[i],&copy_ops)==NULL);
        CHECK(__real_sqlparser_dialect_validation_preprocessor(dialects[i],sqlparser_dialect_mysql_ops())==NULL);
        CHECK(__real_sqlparser_dialect_validation_preprocessor(SQLPARSER_DIALECT_MYSQL,ops)==NULL);
        CHECK(__real_sqlparser_dialect_validation_preprocessor(dialects[i],NULL)==NULL);++cases;
    }
}
static void vp_graph_and_reparse(void)
{
    char *sql=fixture(128U,0U,"s.t","a,b,c","'abcdefghijklmnopqrstuvwxyz0123456789',1,CURRENT_DATE","");
    for(size_t d=0U;d<COUNT(dialects);++d){
        sqlparser_handle_t *h=vp_parse(sql,dialects[d]),*ref;sqlparser_query_graph_view_t graph;sqlparser_error_t e={0};vp_routes_t r;
        stage="strict graph certificate remains mandatory with native graph provenance absent";
        vp_start();CHECK(sqlparser_statement_query_graph(h,0U,&graph,&e)==SQLPARSER_STATUS_OK);r=vp_stop();
        CHECK(r.strict_graph==1U&&r.native_graph==1U&&r.native_graph_hits==0U&&r.unpack==0U);
        CHECK(!h->native_scalar_provenance&&!h->ast&&sqlparser_query_graph_wire_scalar_insert(h));
        stage="destructive reparse retains ordinary unpack route and consumes owned source";
        char *owned=copy(sql);vp_start();CHECK(sqlparser_handle_reparse_destructive(h,&owned,&e)==SQLPARSER_STATUS_OK);r=vp_stop();
        CHECK(owned==NULL&&h->generation==1UL&&!h->native_scalar_provenance);
        CHECK(r.grammar==1U&&r.ordinary_entry==0U&&r.certified_calls==0U&&r.unpack==1U&&r.unpack_free==1U);
        CHECK(r.simple_constructor==0U&&r.scalar_constructor==0U&&r.native_entry==0U);
        force_reference=1;ref=vp_parse(sql,dialects[d]);force_reference=0;
        state_equal(h->dialect_state,ref->dialect_state);handle_equal(h,ref);sqlparser_handle_destroy(h);sqlparser_handle_destroy(ref);++cases;
    }
    free(sql);
}
static void vp_writer_misses(void)
{
    static const char *sql[]={"SELECT 1+2","SELECT a FROM t AS renamed","WITH x AS (SELECT 1) SELECT a FROM x",
        "SELECT 1 INTO q","MERGE INTO t USING s ON t.a=s.a WHEN MATCHED THEN DELETE",
        "SELECT a FROM t START WITH a=1 CONNECT BY PRIOR a=b","SELECT )"};
    stage="ordinary certified entry preserves writer misses and native syntax errors";
    for(size_t i=0U;i<COUNT(sql);++i){
        size_t count=999U;int certified=1;PgQueryProtobufParseResult a,b;vp_routes_t r;
        vp_start();a=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_ordinary(sql[i],PG_QUERY_PARSE_DEFAULT,&count,&certified);r=vp_stop();
        b=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(sql[i],PG_QUERY_PARSE_DEFAULT,vp_observe,NULL);
        CHECK(r.grammar==1U&&!r.simple_constructor&&!r.scalar_constructor&&!r.native_entry&&r.ordinary_entry==1U);
        CHECK(!certified&&count==0U);CHECK((a.error==NULL)==(b.error==NULL));
        if(a.error){text_equal(a.error->message,b.error->message);CHECK(a.error->cursorpos==b.error->cursorpos);}
        CHECK(a.parse_tree.len==b.parse_tree.len&&!memcmp(a.parse_tree.data,b.parse_tree.data,a.parse_tree.len));
        pg_query_free_protobuf_parse_result(a);pg_query_free_protobuf_parse_result(b);++cases;
    }
}
static void vp_wrapper_and_errors(void)
{
    static const char *sql[]={"USE db_one; SELECT 1","SELECT [a] FROM [s].[t]","SELECT TOP(1) a FROM t",
        "SELECT a FROM t CONNECT BY a=b START WITH a=1","IF 1=1 BEGIN SELECT 1 END ELSE SELECT 2",
        "SELECT 1; SELECT 2","SELECT )","SELECT PRIOR a FROM t","SELECT a FROM t START WITH a=1"};
    stage="outer rewrites, control, hierarchy, statement limits and optional-error fallback";
    for(size_t d=0U;d<COUNT(dialects);++d)for(size_t i=0U;i<COUNT(sql);++i){
        sqlparser_parse_options_t o;sqlparser_handle_t *a=NULL,*b=NULL;sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;vp_routes_t r;
        sqlparser_parse_options_default(&o);o.dialect=dialects[d];o.limits.max_statement_count=1U;
        vp_start();ar=sqlparser_parse_with_options(sql[i],&o,&a,&ae);r=vp_stop();
        force_reference=1;br=sqlparser_parse_with_options(sql[i],&o,&b,&be);force_reference=0;
        CHECK(ar==br&&!memcmp(&ae,&be,sizeof(ae)));CHECK(r.ordinary_entry==0U&&!r.simple_constructor&&!r.scalar_constructor);
        if(ar==SQLPARSER_STATUS_OK){state_equal(a->dialect_state,b->dialect_state);handle_equal(a,b);}else CHECK(!a&&!b);
        sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);a=b=NULL;
        ar=sqlparser_parse_with_options(sql[i],&o,&a,NULL);force_reference=1;br=sqlparser_parse_with_options(sql[i],&o,&b,NULL);force_reference=0;
        CHECK(ar==br);sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);++cases;
    }
}
static void vp_allocator_sweeps(void)
{
    char *sql=fixture(128U,0U,"s.t","a,b,c,d,e,f,g,h,i",
        "'abcdefghijklmnopqrstuvwxyz0123456789',1,2147483648,1.25,CURRENT_DATE,CURRENT_TIME,CURRENT_TIMESTAMP,USER,'UTF8 é € 😀'","");
    for(size_t d=0U;d<COUNT(dialects);++d)for(int kind=VP_FAIL_CACHE;kind<=VP_FAIL_OUTPUT;++kind){
        size_t boundaries=0U;
        stage="all native-context direct-wire cache/output allocation failures and recovery";
        for(size_t at=0U;at<=boundaries;++at){
            sqlparser_parse_options_t o;sqlparser_handle_t *h=NULL;sqlparser_error_t e={0};sqlparser_status_t status;vp_routes_t r;
            sqlparser_parse_options_default(&o);o.dialect=dialects[d];sqlparser_pg_query_prepare();
            vp_arm(kind,at);vp_start();status=sqlparser_parse_with_options(sql,&o,&h,&e);r=vp_stop();vp_disarm();
            CHECK(r.grammar==1U&&!r.simple_constructor&&!r.scalar_constructor&&r.ordinary_entry==1U&&r.certified_calls==1U);
            CHECK(r.unpack==0U&&r.unpack_free==0U&&!r.native_entry);
            if(at==0U){
                CHECK(status==SQLPARSER_STATUS_OK&&h&&!vp_injected&&r.certified_hits==1U);
                CHECK(vp_cache_calls>=2U&&vp_output_calls==1U&&vp_live_blocks==1U&&vp_live_bytes==h->parse_tree.len);
                CHECK(vp_slot(h->parse_tree.data)<COUNT(vp_ledger)&&!h->native_scalar_provenance);
                boundaries=kind==VP_FAIL_CACHE?vp_cache_calls:vp_output_calls;
            }else{
                CHECK(vp_injected==1U&&status==SQLPARSER_STATUS_NO_MEMORY&&e.code==status&&h==NULL&&!r.certified_hits);
                CHECK(!strcmp(e.message,"out of memory"));
            }
            sqlparser_handle_destroy(h);CHECK(vp_live_blocks==0U&&vp_live_bytes==0U);
            vp_pair(sql,dialects[d],1,VP_NORMAL);++cases;
        }
        printf("SQLServer validation native-writer faults dialect=%d target=%d boundaries=%zu zero_live=yes\n",(int)dialects[d],kind,boundaries);
    }
    free(sql);
}
static void vp_error_precedence(void)
{
    char *sql=fixture(128U,0U,"s.t","a,b,c",
        "'abcdefghijklmnopqrstuvwxyz0123456789',1,CURRENT_DATE","; SELECT 1 FROM");
    stage="native parse error precedes converter output-allocation failure";
    for(size_t d=0U;d<COUNT(dialects);++d){
        sqlparser_parse_options_t o;sqlparser_handle_t *a=NULL,*b=NULL;
        sqlparser_error_t ae={0},be={0};sqlparser_status_t ar,br;vp_routes_t r;
        sqlparser_parse_options_default(&o);o.dialect=dialects[d];
        force_reference=1;br=sqlparser_parse_with_options(sql,&o,&b,&be);force_reference=0;
        CHECK(br==SQLPARSER_STATUS_PARSE_ERROR&&b==NULL);
        vp_arm(VP_FAIL_OUTPUT,1U);vp_start();
        ar=sqlparser_parse_with_options(sql,&o,&a,&ae);r=vp_stop();vp_disarm();
        CHECK(ar==br&&a==NULL&&!memcmp(&ae,&be,sizeof(ae)));
        if(vp_output_calls!=1U||vp_injected!=1U||vp_live_blocks||vp_live_bytes)
            fprintf(stderr,"error precedence dialect=%d grammar=%zu output=%zu injected=%zu live=%zu bytes=%zu\n",
                (int)dialects[d],r.grammar,vp_output_calls,vp_injected,vp_live_blocks,vp_live_bytes);
        CHECK(vp_output_calls==1U&&vp_injected==1U&&!vp_live_blocks&&!vp_live_bytes);
        CHECK(r.grammar==1U&&!r.simple_constructor&&!r.scalar_constructor&&!r.ordinary_entry&&!r.unpack);
        ++cases;
    }
    free(sql);
}
#endif

int main(int argc,char **argv)
{
#ifdef SQLPARSER_VALIDATION_PROOF_WRAPPERS
    vp_exact_owners();vp_route_boundaries();vp_graph_and_reparse();vp_writer_misses();vp_wrapper_and_errors();vp_allocator_sweeps();vp_error_precedence();
    puts("SQLServer validation targeted routes, observed/AST parity, ownership and native-writer allocation sweeps passed");
    puts("Backend check: no-certificate C++ contract emulated; this is not an actual C++ backend build");
#else
    puts("SKIP: SQLServer validation route/failure checks require GNU function instrumentation and linker wrappers");
#endif
    /* Preserve the complete previous guard inventory, numeric/UTF8 surfaces,
     * source/resource boundaries, origin/fragment paths and owned-input cases. */
    return sqlparser_identity_reference_suite_main(argc,argv);
}
