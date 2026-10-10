/* Independent native batch oracle and allocator instrumentation. The included
 * singleton suite supplies ordinary-parser byte comparison and allocation
 * hooks only; its entry points and admission expectations remain unchanged. */
#define main scalar_singleton_test_main
#include "test_scalar_insert_native.c"
#undef main
#define BCOUNT(a) (sizeof(a)/sizeof((a)[0]))

static char *batch_join(const char *const *parts,size_t count,const char *separator)
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
static void batch_offsets(const char *sql,PgQueryProtobuf wire)
{
    PgQuery__ParseResult *tree=pg_query__parse_result__unpack(NULL,wire.len,(const uint8_t *)wire.data);
    size_t position=0U,start=0U,statement=0U;int quoted=0;CHECK(tree);
    for(;;){
        unsigned char ch=(unsigned char)sql[position];
        if(ch=='\'')quoted=!quoted; /* Positive subset has no escaped quote tokens. */
        if((!quoted&&ch==';')||!ch){
            if(statement<tree->n_stmts){
                const PgQuery__RawStmt *raw=tree->stmts[statement++];
                CHECK(raw&&raw->stmt_location>=0&&(size_t)raw->stmt_location==start);
                CHECK(raw->stmt_len>=0&&(size_t)raw->stmt_len==(ch?position-start:0U));
            }
            start=position+1U;
        }
        if(!ch)break;
        ++position;
    }
    CHECK(!quoted&&statement==tree->n_stmts);pg_query__parse_result__free_unpacked(tree,NULL);
}
static void batch_parity(const char *sql,int options,int fast,size_t expected_count)
{
    size_t count=999U;int certified=1;PgQueryProtobufParseResult a,b;observation o={0};
    char *unchanged=malloc(strlen(sql)+1U);CHECK(unchanged);strcpy(unchanged,sql);
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    grammar_calls=native_calls=0U;check_admission=1;
#endif
    a=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_batch(sql,options,&count,&certified);
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    check_admission=0;CHECK(grammar_calls==(fast?0U:1U));grammar_calls=0U;
#endif
    b=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(sql,options,observe,&o);
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    CHECK(grammar_calls==1U);
#endif
    CHECK(!strcmp(sql,unchanged));CHECK((a.error==NULL)==(b.error==NULL));text_equal(a.stderr_buffer,b.stderr_buffer);
    if(a.error){
        text_equal(a.error->message,b.error->message);text_equal(a.error->context,b.error->context);
        CHECK(a.error->cursorpos==b.error->cursorpos&&a.error->lineno==b.error->lineno);
        CHECK(!certified&&!count);
    }else{
        CHECK(o.calls==1U);wire_equal(o.wire,b.parse_tree);
        if(expected_count)CHECK(o.statements==expected_count);
        if(certified)CHECK(count==o.statements);
        if(fast){CHECK(certified);batch_offsets(sql,a.parse_tree);}
    }
    wire_equal(a.parse_tree,b.parse_tree);pg_query_exit();wire_equal(a.parse_tree,b.parse_tree);
    pg_query_free_protobuf_parse_result(a);pg_query_free_protobuf_parse_result(b);free(o.wire.data);free(unchanged);++cases;
}
static void batch_cases(void)
{
    static const char *values[]={"'UTF8 é € 😀; -- /* */',-2147483648,CURRENT_DATE",
        "'x',.5,LOCALTIME(6)","'@ exec cross apply',1.e-2,CURRENT_SCHEMA"};
    stage="mixed statements, absolute UTF8 byte locations and semicolon lengths";
    for(size_t count=2U;count<=9U;count++)for(int terminal=0;terminal<2;terminal++){
        char *parts[9];
        for(size_t i=0U;i<count;i++)parts[i]=fixture(32U+i,4096U,"Db.Sch.Tab","a,b,c",values[i%3U],i+1U==count?(terminal?"; \r\n\t":" \r\n\t"):"");
        char *sql=batch_join((const char *const *)parts,count,"; \r\n\t");batch_parity(sql,0,1,count);
        free(sql);for(size_t i=0U;i<count;i++)free(parts[i]);
    }
    {
        char *a=fixture(32U,4096U,"s.t","a,b,c",values[0],"");const char *parts[]={a,a};char *sql=batch_join(parts,2U,";");
        batch_parity(a,0,0,1U); /* Dedicated batch entry never expands singleton admission. */
        batch_parity(sql,PG_QUERY_DISABLE_BACKSLASH_QUOTE,0,2U);
        batch_parity(sql,PG_QUERY_DISABLE_STANDARD_CONFORMING_STRINGS,0,2U);
        batch_parity(sql,PG_QUERY_DISABLE_ESCAPE_STRING_WARNING,0,2U);
        free(sql);free(a);
    }
    stage="every source rejection is allocation-free before whole-source grammar fallback";
    {
        static const struct {const char *table,*cols,*values,*tail;} misses[]={
            {"s.t","a,b,c","N'x',1,CURRENT_DATE",""},{"\"s\".t","a,b,c","'x',1,CURRENT_DATE",""},
            {"[s].[t]","a,b,c","'x',1,CURRENT_DATE",""},{"s.t","a,b,c","'one''two',1,CURRENT_DATE",""},
            {"s.t","a,b,c","'back\\slash',1,CURRENT_DATE",""},{"s.t","a,b,c","'x',/*c*/1,CURRENT_DATE",""},
            {"s.t","a,b,c","'x',NULL,CURRENT_DATE",""},{"s.t","a,b,c","'x',1,CURRENT_DATE","; --c\n"},
            {"s.t","a,b,c","'x',1",""},{"s.t","a,b,c","'x',1,CURRENT_DATE"," malformed"},
            {"s.t","a,b,c","'unterminated,1,CURRENT_DATE",""}
        };
        char *safe=fixture(32U,4096U,"s.t","a,b,c",values[0],"");
        for(size_t k=0U;k<BCOUNT(misses);k++)for(size_t at=0U;at<3U;at++){
            char *bad=fixture(32U,4096U,misses[k].table,misses[k].cols,misses[k].values,misses[k].tail);
            const char *parts[]={safe,safe,safe};parts[at]=bad;char *sql=batch_join(parts,3U,";\n");
            batch_parity(sql,0,0,0U);free(sql);free(bad);
        }
        static const char *separators[]={";;","; /*c*/ ","; SELECT 1;","; USE db;","; GO\n"," "};
        for(size_t k=0U;k<BCOUNT(separators);k++){
            const char *parts[]={safe,safe};char *sql=batch_join(parts,2U,separators[k]);batch_parity(sql,0,0,0U);free(sql);
        }
        free(safe);
    }
    stage="per-statement 31/32/33 rows and 4095/4096/4097 byte bounds";
    for(size_t rows=31U;rows<=33U;rows++)for(size_t length=4095U;length<=4097U;length++){
        char *base=fixture(rows,0U,"s.t","a,b,c","'x',1,CURRENT_DATE","");size_t pad=length-strlen(base);free(base);
        char *bad=fixture(rows,pad,"s.t","a,b,c","'x',1,CURRENT_DATE","");
        char *safe=fixture(32U,4096U,"s.t","a,b,c","'x',1,CURRENT_DATE","");
        const char *parts[]={safe,bad};char *sql=batch_join(parts,2U,";");
        batch_parity(sql,0,rows>=32U&&length>=4096U,2U);free(sql);
        /* First slice includes its separator in the source-size proof. */
        parts[0]=bad;parts[1]=safe;sql=batch_join(parts,2U,";");
        batch_parity(sql,0,rows>=32U&&length+1U>=4096U,2U);free(sql);free(safe);free(bad);
    }
}
static void batch_descriptor_capacity(void)
{
    static const size_t counts[]={15U,16U,17U};
    char *one=fixture(32U,4096U,"Db.Sch.Tab","a,b,c","'UTF8 é € 😀; -- /* */',1,CURRENT_DATE","");
    char *two=fixture(35U,4096U,"other_schema.other_table","a,b","'plain',1.e-2","");
    char *bad=fixture(32U,4096U,"s.t","a,b,c","'unterminated,1,CURRENT_DATE","");
    const char *parts[17];
    stage="bounded descriptor 15/16/17 capacity, full-source rejection and RawStmt offsets";
    for(size_t k=0U;k<BCOUNT(counts);k++){
        size_t count=counts[k];
        for(size_t i=0U;i<count;i++)parts[i]=(i%2U)?two:one;
        for(int terminal=0;terminal<2;terminal++){
            char *joined=batch_join(parts,count,"; \r\n\t");
            const char *whole[]={joined,terminal?"; \r\n\t":" \r\n\t"};
            char *sql=batch_join(whole,2U,"");batch_parity(sql,0,1,count);free(sql);free(joined);
        }
        {
            char *joined=batch_join(parts,count,";\n");
            const char *whole[]={joined,";; \r\n"};
            char *sql=batch_join(whole,2U,"");batch_parity(sql,0,0,count);free(sql);free(joined);
        }
        parts[count-1U]="SELECT 1";
        char *sql=batch_join(parts,count,";\n");batch_parity(sql,0,0,count);free(sql);
        parts[count-1U]=bad;
        sql=batch_join(parts,count,";\n");batch_parity(sql,0,0,0U);free(sql);
    }
    free(one);free(two);free(bad);
}
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
static void batch_native_oom(size_t expected_count)
{
    char *a=fixture(128U,4096U,"Db.Sch.Tab","a,b,c,d,e,f,g,h,i",
        "'张三李四',1,-2147483648,100.50,CURRENT_TIMESTAMP,LOCALTIME(6),'',2147483647,-1.5e+3","");
    const char *parts[17];CHECK(expected_count<=BCOUNT(parts));
    for(size_t i=0U;i<expected_count;i++)parts[i]=a;
    char *sql=batch_join(parts,expected_count,"; \n");size_t boundaries=0U;
    stage="native allocation boundaries across cached/overflow batches and fresh-call recovery";
    for(size_t at=0U;at<=boundaries;at++){
        PgQueryProtobufParseResult parsed;size_t count=0U;int certified=0;
        pg_query_exit();pg_query_init();native_attempts=injected=grammar_calls=0U;native_depth=0;fail_at=at;armed=1;
        parsed=pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified_batch(sql,0,&count,&certified);
        armed=0;native_depth=0;CHECK(grammar_calls==0U);
        if(!at){boundaries=native_attempts;CHECK(boundaries&&!parsed.error&&parsed.parse_tree.data&&certified&&count==expected_count);}
        else{
            CHECK(injected==1U);
            if(parsed.error)CHECK(strstr(parsed.error->message,"out of memory")!=NULL&&!certified&&!count);
            else{
                PgQueryProtobufParseResult reference=pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(sql,0,NULL,NULL);
                CHECK(!reference.error&&parsed.parse_tree.data&&certified&&count==expected_count);wire_equal(parsed.parse_tree,reference.parse_tree);
                pg_query_free_protobuf_parse_result(reference);
            }
        }
        pg_query_free_protobuf_parse_result(parsed);CHECK(CurrentMemoryContext==TopMemoryContext);
        batch_parity(sql,0,1,expected_count);CHECK(!native_live_bytes&&!native_live_blocks);
    }
    printf("scalar batch native statements=%zu allocation boundaries=%zu zero_live=yes\n",expected_count,boundaries);free(sql);free(a);
}
#endif
int main(int argc,char **argv)
{
    CHECK(argc<=2);batch_cases();batch_descriptor_capacity();
#ifdef SQLPARSER_SIMPLE_INSERT_WRAPPERS
    batch_native_oom(3U);batch_native_oom(16U);batch_native_oom(17U);
#endif
    if(argc==2){
        FILE *f=fopen(argv[1],"rb");long length;char *sql;CHECK(f&&fseek(f,0,SEEK_END)==0);
        length=ftell(f);CHECK(length>0&&length<16L*1024L*1024L&&fseek(f,0,SEEK_SET)==0);
        sql=malloc((size_t)length+1U);CHECK(sql&&fread(sql,1,(size_t)length,f)==(size_t)length&&fclose(f)==0);sql[length]='\0';
        stage="actual five-statement 5000-row fixture";batch_parity(sql,0,1,5U);free(sql);
    }
    pg_query_exit();printf("scalar batch native full-wire/error cases=%zu\n",cases);return 0;
}
