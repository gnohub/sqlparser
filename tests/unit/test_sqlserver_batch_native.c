/* SQLServer batch-native route isolation and full ordinary-grammar oracle.
 * Reuse test-only instrumentation; no production test hooks are exported. */
#define SQLPARSER_VALIDATION_TEST_MAIN sqlserver_singleton_validation_main
#include "test_sqlserver_validation_proof.c"
#undef SQLPARSER_VALIDATION_TEST_MAIN
#ifdef SQLPARSER_VALIDATION_PROOF_WRAPPERS
static char *bvp_join(const char *const *parts,size_t count,const char *separator)
{
    size_t length=1U,used=0U;char *sql;
    for(size_t i=0U;i<count;i++)length+=strlen(parts[i])+(i?strlen(separator):0U);
    sql=malloc(length);CHECK(sql);
    for(size_t i=0U;i<count;i++){
        if(i){memcpy(sql+used,separator,strlen(separator));used+=strlen(separator);}
        memcpy(sql+used,parts[i],strlen(parts[i]));used+=strlen(parts[i]);
    }
    sql[used]='\0';return sql;
}
static void bvp_pair(const char *sql,sqlparser_dialect_t dialect,int eligible,int mode,
    const sqlparser_parse_options_t *custom,size_t count)
{
    sqlparser_parse_options_t o;sqlparser_handle_t *a=NULL,*b=NULL;sqlparser_error_t ae={0},be={0};
    sqlparser_status_t ar,br;vp_routes_t r;char *owned=copy(sql);
    sqlparser_parse_options_default(&o);if(custom)o=*custom;o.dialect=dialect;
    stage="batch native/ordinary routes, exact bytes/state/errors, caller lifetime and all graph cells";
    vp_mode=mode;vp_start();ar=sqlparser_parse_with_options(owned,&o,&a,&ae);r=vp_stop();vp_mode=VP_NORMAL;
    force_reference=1;br=sqlparser_parse_with_options(owned,&o,&b,&be);force_reference=0;poison_free(owned);
    CHECK(ar==br&&!memcmp(&ae,&be,sizeof(ae)));
    CHECK(!r.simple_constructor&&!r.scalar_constructor&&!r.native_entry&&!r.ordinary_entry);
    CHECK(r.batch_entry==(size_t)eligible&&r.batch_constructor==(size_t)eligible);
    if(eligible){
        CHECK(!r.grammar&&r.certified_calls==1U);
        CHECK(r.certified_hits==(size_t)(mode==VP_NORMAL));
        CHECK(r.unpack==(size_t)(mode!=VP_NORMAL)&&r.unpack_free==r.unpack);
    }
    if(ar==SQLPARSER_STATUS_OK){
        CHECK(a&&b&&!a->native_scalar_provenance&&!b->native_scalar_provenance);
        CHECK(!a->dialect_ops->plain_scalar_native_validation&&a->statement_count==count);
        CHECK(a->parse_tree.len==b->parse_tree.len&&!memcmp(a->parse_tree.data,b->parse_tree.data,a->parse_tree.len));
        state_equal(a->dialect_state,b->dialect_state);
        CHECK(sqlparser_handle_ensure_ast(b,&be)==SQLPARSER_STATUS_OK);
        pg_query_exit();handle_equal(a,b);
    }else CHECK(!a&&!b);
    sqlparser_handle_destroy(a);sqlparser_handle_destroy(b);++cases;
}
static void bvp_routes(void)
{
    char *one=fixture(32U,4096U,"Db.Sch.Tab","a,b,c","'UTF8 é € 😀; -- /* */',-2147483648,CURRENT_TIME(6)","");
    char *two=fixture(35U,4096U,"other_schema.other_table","a,b","'plain',1.e-2","; \r\n");
    const char *parts[]={one,two};char *sql=bvp_join(parts,2U,"; \r\n\t");
    for(size_t d=0U;d<COUNT(dialects);d++){
        bvp_pair(sql,dialects[d],1,VP_NORMAL,NULL,2U);
        bvp_pair(sql,dialects[d],1,VP_FORCE_OBSERVED,NULL,2U);
        bvp_pair(sql,dialects[d],1,VP_NO_CERTIFICATE_BACKEND,NULL,2U);
        for(vp_bad_proof=1;vp_bad_proof<=3;vp_bad_proof++)bvp_pair(sql,dialects[d],0,VP_NORMAL,NULL,2U);
        vp_bad_proof=0;vp_copy_owner=1;bvp_pair(sql,dialects[d],0,VP_NORMAL,NULL,2U);vp_copy_owner=0;
        for(size_t limit=1U;limit<=3U;limit++){
            sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.limits.max_statement_count=limit;
            bvp_pair(sql,dialects[d],1,VP_NORMAL,&o,2U);
        }
        for(int delta=-1;delta<=1;delta++){
            sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.limits.max_sql_bytes=strlen(sql)+delta;
            bvp_pair(sql,dialects[d],delta>=0,VP_NORMAL,&o,2U);
        }
        for(int delta=-1;delta<=1;delta++){
            sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.limits.max_output_bytes=strlen(sql)+delta;
            bvp_pair(sql,dialects[d],1,VP_NORMAL,&o,2U);
        }
        {
            sqlparser_handle_t *a=NULL,*b=NULL;sqlparser_parse_options_t o;
            sqlparser_parse_options_default(&o);o.dialect=dialects[d];o.limits.max_statement_count=1U;
            sqlparser_status_t ar=sqlparser_parse_with_options(sql,&o,&a,NULL);
            force_reference=1;sqlparser_status_t br=sqlparser_parse_with_options(sql,&o,&b,NULL);force_reference=0;
            CHECK(ar==br&&!a&&!b);
        }
        /* Reparse must not acquire the initial-only batch capability. */
        sqlparser_handle_t *h=vp_parse(sql,dialects[d]);sqlparser_error_t e={0};char *owned=copy(sql);vp_routes_t r;
        vp_start();CHECK(sqlparser_handle_reparse_destructive(h,&owned,&e)==SQLPARSER_STATUS_OK);r=vp_stop();
        CHECK(!owned&&h->generation==1UL&&!h->native_scalar_provenance&&r.grammar==1U&&!r.batch_entry&&!r.batch_constructor);
        sqlparser_handle_destroy(h);
    }
    public_parity(sql,1,NULL); /* Clone and raw/typed patches retain independent state/wire parity. */
    free(sql);free(one);free(two);
}
static void bvp_fallbacks(void)
{
    static const struct {const char *table,*columns,*values,*tail;} inputs[]={
        {"s.t","a,b,c","N'national',1,CURRENT_DATE",""},
        {"[s].[t]","[a],b,c","'plain',1,CURRENT_DATE",""},
        {"s.t","a,b,c","'one''two',1,CURRENT_DATE",""},
        {"s.t","a,b,c","'plain',/*c*/1,CURRENT_DATE",""},
        {"s.t","a,b,c","'plain',1,CURRENT_DATE","; -- trailing\n"},
        {"s.t","a,b,c","'plain',1,CURRENT_DATE",";;"},
        {"s.t","a,b,c","'plain',1,CURRENT_DATE"," malformed"},
        {"s.a0xAB","a,b,c","'plain',1,CURRENT_DATE",""},
        {"s.t","a,top,c","'plain',1,CURRENT_DATE",""}
    };
    char *safe=fixture(32U,4096U,"s.t","a,b,c","'plain',1,CURRENT_DATE","");
    for(size_t d=0U;d<COUNT(dialects);d++)for(size_t i=0U;i<COUNT(inputs);i++)for(size_t at=0U;at<3U;at++){
        char *bad=fixture(32U,4096U,inputs[i].table,inputs[i].columns,inputs[i].values,inputs[i].tail);
        const char *parts[]={safe,safe,safe};parts[at]=bad;char *sql=bvp_join(parts,3U,";\n");
        bvp_pair(sql,dialects[d],0,VP_NORMAL,NULL,3U);free(sql);free(bad);
    }
    free(safe);
}
static void bvp_writer_faults(void)
{
    char *one=fixture(128U,0U,"s.t","a,b,c,d,e,f,g,h,i",
        "'abcdefghijklmnopqrstuvwxyz0123456789',1,2147483648,1.25,CURRENT_DATE,CURRENT_TIME,CURRENT_TIMESTAMP,USER,'UTF8 é € 😀'","");
    const char *parts[]={one,one,one};char *sql=bvp_join(parts,3U,";\n");
    for(size_t d=0U;d<COUNT(dialects);d++)for(int kind=VP_FAIL_CACHE;kind<=VP_FAIL_OUTPUT;kind++){
        size_t boundaries=0U;stage="batch certified writer cache/output faults and recovery";
        for(size_t at=0U;at<=boundaries;at++){
            sqlparser_parse_options_t o;sqlparser_handle_t *h=NULL;sqlparser_error_t e={0};sqlparser_status_t status;vp_routes_t r;
            sqlparser_parse_options_default(&o);o.dialect=dialects[d];sqlparser_pg_query_prepare();
            vp_arm(kind,at);vp_start();status=sqlparser_parse_with_options(sql,&o,&h,&e);r=vp_stop();vp_disarm();
            CHECK(!r.grammar&&!r.simple_constructor&&!r.scalar_constructor&&!r.native_entry&&r.batch_constructor==1U&&r.batch_entry==1U);
            CHECK(r.certified_calls==1U&&!r.unpack&&!r.unpack_free);
            if(!at){CHECK(status==SQLPARSER_STATUS_OK&&h&&!vp_injected&&r.certified_hits==1U);boundaries=kind==VP_FAIL_CACHE?vp_cache_calls:vp_output_calls;CHECK(boundaries);}
            else{CHECK(vp_injected==1U&&status==SQLPARSER_STATUS_NO_MEMORY&&e.code==status&&!h&&!r.certified_hits);CHECK(!strcmp(e.message,"out of memory"));}
            sqlparser_handle_destroy(h);CHECK(!vp_live_blocks&&!vp_live_bytes);bvp_pair(sql,dialects[d],1,VP_NORMAL,NULL,3U);
        }
        printf("SQLServer batch writer fault dialect=%d target=%d boundaries=%zu zero_live=yes\n",(int)dialects[d],kind,boundaries);
    }
    free(sql);free(one);
}
static void bvp_actual(const char *path)
{
    FILE *f=fopen(path,"rb");long length;char *sql;CHECK(f&&fseek(f,0,SEEK_END)==0);
    length=ftell(f);CHECK(length>0&&length<16L*1024L*1024L&&fseek(f,0,SEEK_SET)==0);
    sql=malloc((size_t)length+1U);CHECK(sql&&fread(sql,1,(size_t)length,f)==(size_t)length&&fclose(f)==0);sql[length]='\0';
    for(size_t d=0U;d<COUNT(dialects);d++)bvp_pair(sql,dialects[d],1,VP_NORMAL,NULL,5U);
    free(sql);
}
#endif
int main(int argc,char **argv)
{
    CHECK(argc<=2);
#ifdef SQLPARSER_VALIDATION_PROOF_WRAPPERS
    bvp_routes();bvp_fallbacks();bvp_writer_faults();vp_error_precedence();
    if(argc==2)bvp_actual(argv[1]);
    pg_query_exit();printf("SQLServer batch-native routes, full-wire/state/graph/errors/faults: %zu cases\n",cases);
#else
    (void)argv;puts("SKIP: requires GNU function instrumentation and wrappers");
#endif
    return 0;
}
