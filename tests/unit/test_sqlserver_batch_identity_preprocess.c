/* Preprocessing-only batch identity: use the independent legacy-state,
 * parser-wire, graph, error and caller-lifetime oracles from the singleton
 * suite. This translation unit exposes no test hook in production code. */
#define main sqlparser_single_identity_suite_main
#include "test_sqlserver_identity_preprocess.c"
#undef main

static char *join_parts(const char *const *parts, size_t count, const char *separator)
{
    size_t length=0U, used=0U, separator_length=strlen(separator);
    char *sql;
    CHECK(count>0U);
    for(size_t i=0;i<count;i++) {
        size_t n=strlen(parts[i]);CHECK(n<SIZE_MAX-length);length+=n;
        if(i){CHECK(separator_length<SIZE_MAX-length);length+=separator_length;}
    }
    sql=malloc(length+1U);CHECK(sql);
    for(size_t i=0;i<count;i++) {
        size_t n=strlen(parts[i]);
        if(i){memcpy(sql+used,separator,separator_length);used+=separator_length;}
        memcpy(sql+used,parts[i],n);used+=n;
    }
    CHECK(used==length);sql[used]='\0';return sql;
}

static void batch_admission(const char *sql, int expected, size_t statements, size_t strings)
{
    sqlparser_sqlserver_identity_batch_t batch, zero={0};
    memset(&batch,0xa5,sizeof(batch));force_reference=0;
    CHECK(sqlparser_sqlserver_prove_identity_insert_batch(sql,&batch)==expected);
    if(expected) {
        CHECK(batch.source_length==strlen(sql));CHECK(batch.statement_count==statements);
        CHECK(batch.string_count==strings);CHECK(statements>=2U);
    } else CHECK(!memcmp(&batch,&zero,sizeof(batch)));
    if(!proof_string_count_override){
        PgQueryIdentityInsertSequenceProof sequence,empty={0};
        PgQueryIdentityScalarInsertProof single,single_zero={0};
        PgQueryIdentityInsertSequenceKind kind;
        memset(&sequence,0xa5,sizeof(sequence));
        kind=pg_query_prove_identity_insert_sequence(sql,sqlparser_sqlserver_identity_name,&sequence);
        if(expected){
            CHECK(kind==PG_QUERY_IDENTITY_INSERT_BATCH);
            CHECK(sequence.source_length==batch.source_length&&sequence.statement_count==statements&&sequence.string_count==strings);
            CHECK(!memcmp(&sequence.single,&single_zero,sizeof(single_zero)));
        }else if(sqlparser_sqlserver_prove_identity_scalar_insert(sql,&single)){
            CHECK(kind==PG_QUERY_IDENTITY_INSERT_SINGLE);
            CHECK(sequence.source_length==single.source_length&&sequence.statement_count==1U&&sequence.string_count==single.string_count);
            CHECK(!memcmp(&sequence.single,&single,sizeof(single)));
        }else{
            CHECK(kind==PG_QUERY_IDENTITY_INSERT_NONE&&!memcmp(&sequence,&empty,sizeof(empty)));
        }
    }
}

static void prefix_classification_boundaries(void)
{
    static const size_t counts[]={1U,2U,5U,15U,16U,17U};
    char *safe=fixture(32U,4096U,"s.t","a,b","'é 😀; -- /* */',1","");
    const char *parts[17];
    stage="prefix NONE/SINGLE/BATCH classification and exact old slice bounds";
    for(size_t k=0U;k<COUNT(counts);k++){
        size_t count=counts[k];for(size_t i=0U;i<count;i++)parts[i]=safe;
        for(int terminal=0;terminal<2;terminal++){
            char *joined=join_parts(parts,count,"; \r\n\t");
            const char *all[]={joined,terminal?"; \r\n\t":" \r\n\t"};
            char *sql=join_parts(all,2U,"");batch_admission(sql,count>1U,count,32U*count);
            free(sql);free(joined);
        }
    }
    for(size_t length=4095U;length<=4097U;length++){
        char *base=fixture(32U,0U,"s.t","a","'x'","");size_t padding=length-strlen(base);free(base);
        char *edge=fixture(32U,padding,"s.t","a","'x'","");
        const char *first[]={edge,safe};char *sql=join_parts(first,2U,";");
        batch_admission(sql,length+1U>=4096U,2U,64U);free(sql);
        const char *last[]={safe,edge};sql=join_parts(last,2U,";");
        batch_admission(sql,length>=4096U,2U,64U);free(sql);free(edge);
    }
    {
        char spaces[5001];memset(spaces,' ',5000U);spaces[5000]=0;
        char *small=fixture(32U,0U,"s.t","a","'x'",";");
        const char *padded_parts[]={small,spaces};char *padded=join_parts(padded_parts,2U,"");
        batch_admission(padded,0,0U,0U); /* SINGLE may count padding after ';'. */
        const char *batch_parts[]={safe,padded};char *sql=join_parts(batch_parts,2U,";");
        batch_admission(sql,0,0U,0U); /* BATCH may not count that final padding. */
        free(sql);
        const char *first_parts[]={padded,safe};sql=join_parts(first_parts,2U,"");
        batch_admission(sql,0,0U,0U); /* Nor padding after the first ';'. */
        free(sql);free(padded);free(small);
        small=fixture(32U,0U,"s.t","a","'x'","");
        const char *inside_parts[]={small,spaces};padded=join_parts(inside_parts,2U,"");
        const char *last_inside[]={safe,padded};sql=join_parts(last_inside,2U,";");
        batch_admission(sql,1,2U,64U); /* No final ';': whitespace is in the slice. */
        free(sql);
        const char *first_inside[]={padded,safe};sql=join_parts(first_inside,2U,";");
        batch_admission(sql,1,2U,64U); /* Whitespace before first ';' also counts. */
        free(sql);free(padded);free(small);
    }
    {
        PgQueryIdentityInsertSequenceProof p,z={0};
        memset(&p,0xa5,sizeof(p));CHECK(pg_query_prove_identity_insert_sequence(NULL,sqlparser_sqlserver_identity_name,&p)==PG_QUERY_IDENTITY_INSERT_NONE);CHECK(!memcmp(&p,&z,sizeof(p)));
        memset(&p,0xa5,sizeof(p));CHECK(pg_query_prove_identity_insert_sequence(safe,NULL,&p)==PG_QUERY_IDENTITY_INSERT_NONE);CHECK(!memcmp(&p,&z,sizeof(p)));
        CHECK(pg_query_prove_identity_insert_sequence(safe,sqlparser_sqlserver_identity_name,NULL)==PG_QUERY_IDENTITY_INSERT_NONE);
    }
    free(safe);
}

static void batch_preprocess(const char *sql, int expected, size_t statements, size_t strings)
{
    char *a=NULL,*b=NULL;void *as=NULL,*bs=NULL;
    sqlparser_parse_options_t o;sqlparser_error_t ae={0},be={0};
    PgQueryIdentityScalarInsertProof proof,zero={0};sqlparser_status_t ar,br;
    sqlparser_identity_insert_batch_proof_t batch_proof, batch_zero={0};
    batch_admission(sql,expected,statements,strings);
    sqlparser_parse_options_default(&o);memset(&proof,0xa5,sizeof(proof));memset(&batch_proof,0xa5,sizeof(batch_proof));
    force_reference=0;
    ar=sqlparser_sqlserver_preprocess_validation_proof(sql,&o.limits,&a,&as,&proof,&batch_proof,&ae);
    /* Batch metadata never becomes singleton/native graph provenance. */
    CHECK(!memcmp(&proof,&zero,sizeof(proof)));
    if(ar==SQLPARSER_STATUS_OK&&expected) {
        CHECK(batch_proof.source_length==strlen(sql)&&batch_proof.statement_count==statements&&batch_proof.string_count==strings);
    } else CHECK(!memcmp(&batch_proof,&batch_zero,sizeof(batch_proof)));
    force_reference=1;br=sqlparser_sqlserver_preprocess(sql,&o.limits,&b,&bs,&be);force_reference=0;
    CHECK(ar==br);CHECK(!memcmp(&ae,&be,sizeof(ae)));text_equal(a,b);state_equal(as,bs);
    if(ar==SQLPARSER_STATUS_OK) {
        CHECK(a!=sql&&b!=sql);
        if(expected) {
            sqlparser_sqlserver_state_t plain={0};
            plain.literal_count=strings;plain.table_source_count=statements;
            /* Both assertions independently confirm the legacy counters and
             * every other state/checkpoint byte for the admitted grammar. */
            CHECK(!memcmp(as,&plain,sizeof(plain)));CHECK(!memcmp(bs,&plain,sizeof(plain)));
            CHECK(!strcmp(sql,a));
        }
    } else CHECK(!a&&!b&&!as&&!bs);
    free(a);free(b);sqlparser_sqlserver_state_destroy(as);sqlparser_sqlserver_state_destroy(bs);++cases;
}

static void batch_positives(void)
{
    static const char *values[]={"'x'","'é; 😀',1","'@ exec cross apply ; -- /* */',2,CURRENT_DATE",
        "'a','b',.5,- 7,CURRENT_TIME(6)","1,2,CURRENT_TIMESTAMP"};
    static const char *columns[]={"a","a,b","a,b,c","a,b,c,d,e","a,b,c"};
    static const char *tables[]={"t","s.t","db.s.t","OtherSchema.OtherTable","other_table"};
    static const size_t strings_per_row[]={1U,1U,1U,2U,0U};
    static const char *separators[]={";","; \n\t",";\r\n"};
    stage="variable-width, target, row-count and statement-count batches";
    for(size_t count=2U;count<=9U;count++) {
        char *parts[9];size_t strings=0U;
        for(size_t i=0;i<count;i++) {
            size_t which=i%COUNT(values), rows=32U+i;
            parts[i]=fixture(rows,4096U,tables[which],columns[which],values[which],
                i+1U==count&&count%2U?"; \n\t":"");
            strings+=rows*strings_per_row[which];
        }
        char *sql=join_parts((const char *const *)parts,count,separators[count%COUNT(separators)]);
        batch_preprocess(sql,1,count,strings);public_parity(sql,1,NULL);
        if(count==2U)preprocess_parity(sql,-1,1);
        free(sql);for(size_t i=0;i<count;i++)free(parts[i]);
    }
    {
        char *numeric=fixture(32U,4096U,"s.t","a,b,c","1,2,CURRENT_TIMESTAMP","");
        const char *parts[]={numeric,numeric};char *sql=join_parts(parts,2U,";");
        batch_preprocess(sql,1,2U,0U);public_parity(sql,0,NULL);free(sql);free(numeric);
    }
}

static void batch_hazards(void)
{
    char *safe=fixture(32U,4096U,"safe_schema.safe_table","a,b,c","'safe',1,CURRENT_TIMESTAMP","");
    stage="all hazard names, mixed case, token boundaries and quoted words in later statements";
    for(size_t list=0;list<2U;list++) {
        const char *const *words=list?pg_keywords:hazards;
        size_t n=list?COUNT(pg_keywords):COUNT(hazards);
        for(size_t i=0;i<n;i++) {
            char mixed[128],prefixed[160],suffixed[160],quoted[160];size_t len=strlen(words[i]);
            CHECK(len<sizeof(mixed));
            for(size_t j=0;j<=len;j++)mixed[j]=(char)(j%2U?toupper((unsigned char)words[i][j]):words[i][j]);
            snprintf(prefixed,sizeof(prefixed),"prefix_%s",words[i]);
            snprintf(suffixed,sizeof(suffixed),"%s_suffix",words[i]);
            const char *names[]={words[i],mixed,prefixed,suffixed};
            for(size_t variant=0;variant<COUNT(names);variant++)for(size_t position=0;position<4U;position++) {
                char table[512],cols[512];
                snprintf(table,sizeof(table),"%s.%s.%s",position==0U?names[variant]:"db",
                    position==1U?names[variant]:"sch",position==2U?names[variant]:"tab");
                snprintf(cols,sizeof(cols),"a,%s,c",position==3U?names[variant]:"b");
                char *other=fixture(32U,4096U,table,cols,"'text',1,CURRENT_DATE","");
                const char *parts[]={safe,other};char *sql=join_parts(parts,2U,";\n");
                batch_preprocess(sql,variant>=2U,2U,64U);free(sql);free(other);
            }
            snprintf(quoted,sizeof(quoted),"'%s'",words[i]);
            char *other=fixture(32U,4096U,"s.t","a",quoted,"");
            const char *parts[]={safe,other};char *sql=join_parts(parts,2U,";\n");
            batch_preprocess(sql,1,2U,64U);free(sql);free(other);
        }
    }
    free(safe);
}

static void batch_fallbacks_and_limits(void)
{
    static const char *suffixes[]={"SELECT 1","BEGIN SELECT 1 END","USE db","GO\n",";","-- comment\n",
        "/* comment */","INSERT INTO t(a) VALUES ('small')","INSERT INTO t(a) OUTPUT inserted.a VALUES ('x')",
        "INSERT INTO [t](a) VALUES ('x')","INSERT INTO t(a) VALUES (N'x')","INSERT INTO t(a) VALUES ('unterminated"};
    static const char *values[]={"'a''b'","'back\\slash'","N'x'","'line\nline'","'\x80'","NULL",
        "DEFAULT","@x","0xAB","?","1+2","(SELECT 1)","'unterminated"};
    static const char *names[]={"a0xAB","a0XG","#tmp","@var","[name]","\"name\"","naïve"};
    char *safe=fixture(32U,4096U,"s.t","a","'safe'","");
    stage="mixed, empty, commented, rewritten, malformed and unsupported batches";
    for(size_t i=0;i<COUNT(suffixes);i++) {
        const char *parts[]={safe,suffixes[i]};char *sql=join_parts(parts,2U,"; ");
        sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.limits.max_statement_count=1U;
        batch_preprocess(sql,0,0,0);public_parity(sql,0,NULL);public_parity(sql,0,&o);free(sql);
        {
            const char *last[]={safe,safe,suffixes[i]};
            const char *middle[]={safe,suffixes[i],safe};
            sql=join_parts(last,3U,"; ");batch_preprocess(sql,0,0,0);public_parity(sql,0,NULL);free(sql);
            sql=join_parts(middle,3U,"; ");batch_preprocess(sql,0,0,0);public_parity(sql,0,NULL);free(sql);
        }
    }
    for(size_t i=0;i<COUNT(values);i++) {
        char *other=fixture(32U,4096U,"s.t","a",values[i],"");
        const char *parts[]={safe,other};char *sql=join_parts(parts,2U,";");
        batch_preprocess(sql,0,0,0);public_parity(sql,0,NULL);free(sql);
        {
            const char *later[]={safe,safe,safe,safe,safe,safe,safe,safe,other};
            sql=join_parts(later,COUNT(later),";");batch_preprocess(sql,0,0,0);free(sql);
        }
        free(other);
    }
    for(size_t i=0;i<COUNT(names);i++) {
        char *other=fixture(32U,4096U,names[i],"a","'other'","");
        const char *parts[]={safe,other};char *sql=join_parts(parts,2U,";");
        batch_preprocess(sql,0,0,0);public_parity(sql,0,NULL);free(sql);free(other);
    }
    for(size_t rows=31U;rows<=33U;rows++) {
        char *other=fixture(rows,4096U,"s.t","a","'other'","");
        const char *parts[]={safe,other};char *sql=join_parts(parts,2U,";");
        batch_preprocess(sql,rows>=32U,2U,32U+rows);public_parity(sql,0,NULL);free(sql);free(other);
    }
    for(size_t size=4095U;size<=4097U;size++) {
        char *base=fixture(32U,0U,"s.t","a","'other'","");size_t padding=size-strlen(base);free(base);
        char *other=fixture(32U,padding,"s.t","a","'other'","");
        const char *parts[]={safe,other};char *sql=join_parts(parts,2U,";");
        batch_preprocess(sql,size>=4096U,2U,64U);free(sql);free(other);
    }
    {
        const char *parts[]={safe,safe};char *sql=join_parts(parts,2U,";\n");
        sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);size_t n=strlen(sql);
        for(int d=-1;d<=1;d++) {
            o.limits.max_sql_bytes=n+d;public_parity(sql,0,&o);
            o.limits.max_sql_bytes=n+1U;o.limits.max_output_bytes=n+d;public_parity(sql,0,&o);
        }
        sqlparser_parse_options_default(&o);
        for(size_t limit=1U;limit<=3U;limit++){o.limits.max_statement_count=limit;public_parity(sql,0,&o);}
        proof_string_count_override=SIZE_MAX;batch_admission(sql,0,0,0);proof_string_count_override=0U;
        free(sql);
    }
    /* Rejections must not read NULL or perform scratch allocations for a
     * SELECT, and no stale or partial metadata may be published. */
    batch_admission(NULL,0,0,0);batch_admission("",0,0,0);batch_admission("I",0,0,0);
    batch_admission("INSERT",0,0,0);batch_admission("SELECT 1",0,0,0);
    CHECK(!sqlparser_sqlserver_prove_identity_insert_batch("SELECT 1",NULL));
    batch_admission(safe,0,0,0);free(safe);
}

#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
static void batch_allocation_failures(void)
{
    char *small=fixture(32U,4096U,"s.t","a","'small'","");
    char *large=fixture(33U,16384U,"other_schema.other_table","a,b","'@ exec cross apply','large'","");
    const char *parts[]={small,large,small};char *sql=join_parts(parts,3U,";\n");
    size_t scratch_boundaries=0U,preprocess_boundaries[2]={0U,0U};
    stage="non-INSERT and singleton misses allocate no batch scratch";
    {
        sqlparser_sqlserver_identity_batch_t batch;
        arm(1U);CHECK(!sqlparser_sqlserver_prove_identity_insert_batch("SELECT 1",&batch));
        CHECK(!sqlparser_sqlserver_prove_identity_insert_batch(small,&batch));armed=0;
        CHECK(!attempts&&!injected&&!live);
    }
    {
        PgQueryIdentityInsertSequenceProof sequence;
        arm(1U);CHECK(pg_query_prove_identity_insert_sequence(sql,sqlparser_sqlserver_identity_name,&sequence)==PG_QUERY_IDENTITY_INSERT_BATCH);armed=0;
        CHECK(sequence.statement_count==3U&&!attempts&&!injected&&!live);
    }
    stage="every optional scratch growth failure discards its partial batch proof";
    for(size_t at=0U;at<=scratch_boundaries;at++) {
        sqlparser_sqlserver_identity_batch_t batch,zero={0};int result;
        memset(&batch,0xa5,sizeof(batch));arm(at);
        result=sqlparser_sqlserver_prove_identity_insert_batch(sql,&batch);armed=0;
        if(!at){scratch_boundaries=attempts;CHECK(scratch_boundaries==2U&&result);}
        else{CHECK(injected==1U&&!result);CHECK(!memcmp(&batch,&zero,sizeof(batch)));}
        CHECK(!live);
    }
    stage="batch state/copy/raw-mask OOM and optional allocation fallback preserve cleanup";
    for(int legacy=0;legacy<2;legacy++)for(size_t at=0U;at<=preprocess_boundaries[legacy];at++) {
        char *out=NULL,*reference=NULL;void *state=NULL,*reference_state=NULL;
        sqlparser_parse_options_t o;sqlparser_error_t e={0},re={0};sqlparser_status_t result;
        PgQueryIdentityScalarInsertProof proof,zero={0};sqlparser_parse_options_default(&o);
        memset(&proof,0xa5,sizeof(proof));force_reference=0;force_prefix_reference=legacy;arm(at);
        result=sqlparser_sqlserver_preprocess_validation_proof(sql,&o.limits,&out,&state,&proof,NULL,&e);armed=0;
        force_prefix_reference=0;
        CHECK(!memcmp(&proof,&zero,sizeof(proof)));
        if(!at){preprocess_boundaries[legacy]=attempts;CHECK(preprocess_boundaries[legacy]==(legacy?5U:3U)&&result==SQLPARSER_STATUS_OK);}
        else {
            CHECK(injected==1U);
            /* Retain every old optional-scratch failure under an explicit
             * prefix-disabled reference. The current implementation has only the three
             * required state/copy/RAW-mask allocations; compare actual
             * boundaries independently, never equate changed OOM ordinals. */
            CHECK(result==((legacy&&(at==2U||at==3U))?SQLPARSER_STATUS_OK:SQLPARSER_STATUS_NO_MEMORY));
        }
        if(result==SQLPARSER_STATUS_OK) {
            force_reference=1;
            CHECK(sqlparser_sqlserver_preprocess(sql,&o.limits,&reference,&reference_state,&re)==SQLPARSER_STATUS_OK);
            force_reference=0;text_equal(out,reference);state_equal(state,reference_state);
            CHECK(!memcmp(&e,&re,sizeof(e)));
        } else {
            CHECK(result==SQLPARSER_STATUS_NO_MEMORY&&e.code==result);CHECK(!out&&!state);
            CHECK(!strcmp(e.message,"out of memory"));
        }
        free(out);free(reference);sqlparser_sqlserver_state_destroy(state);
        sqlparser_sqlserver_state_destroy(reference_state);CHECK(!live);
    }
    free(sql);free(small);free(large);
    printf("SQLServer batch identity: old scratch growth boundaries=%zu candidate preprocess boundaries=%zu old preprocess boundaries=%zu zero_live=yes\n",
        scratch_boundaries,preprocess_boundaries[0],preprocess_boundaries[1]);
}
#endif

static void batch_external_fixture(const char *path)
{
    FILE *f=fopen(path,"rb");long n;char *sql;sqlparser_sqlserver_identity_batch_t batch;
    CHECK(f);CHECK(!fseek(f,0,SEEK_END));n=ftell(f);CHECK(n>0&&n<16L*1024L*1024L);
    CHECK(!fseek(f,0,SEEK_SET));sql=malloc((size_t)n+1U);CHECK(sql);
    CHECK(fread(sql,1,(size_t)n,f)==(size_t)n);CHECK(!fclose(f));sql[n]='\0';CHECK(strlen(sql)==(size_t)n);
    stage="supplied original nine-column multi-INSERT fixture";
    CHECK(sqlparser_sqlserver_prove_identity_insert_batch(sql,&batch));
    batch_preprocess(sql,1,batch.statement_count,batch.string_count);public_parity(sql,1,NULL);free(sql);
}

int main(int argc,char **argv)
{
    CHECK(argc<=2);
    prefix_classification_boundaries();
    fprintf(stderr,"SQLServer batch identity: positives\n");batch_positives();
    fprintf(stderr,"SQLServer batch identity: hazard inventory\n");batch_hazards();
    fprintf(stderr,"SQLServer batch identity: fallbacks and limits\n");batch_fallbacks_and_limits();
    if(argc==2)batch_external_fixture(argv[1]);
#ifdef SQLPARSER_IDENTITY_ALLOC_WRAPPERS
    fprintf(stderr,"SQLServer batch identity: allocation faults\n");batch_allocation_failures();
#endif
    pg_query_exit();printf("SQLServer batch identity passed: %zu full state/parser/SQL/error cases\n",cases);return 0;
}
