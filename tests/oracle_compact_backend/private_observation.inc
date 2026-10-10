/* Read-only logical adapter. Baseline uses its original legacy representation;
 * only the current implementation includes the real production tagged accessors. There is
 * no token classifier or construction algorithm in this observation layer. */
static int obs_is_compact(const sqlparser_dialect_multi_insert_branch_t *b)
{
#ifdef COMPACT_BACKEND
    REQUIRE(b->cell_storage==SQLPARSER_ORACLE_CELL_STORAGE_LEGACY ||
        b->cell_storage==SQLPARSER_ORACLE_CELL_STORAGE_COMPACT);
    return b->cell_storage==SQLPARSER_ORACLE_CELL_STORAGE_COMPACT;
#else
    (void)b;return 0;
#endif
}
static const void *obs_storage(const sqlparser_dialect_multi_insert_branch_t *b)
{
#ifdef COMPACT_BACKEND
    if(obs_is_compact(b)){REQUIRE(!b->cells);return b->oracle_compact_cells;}
    REQUIRE(!b->oracle_compact_cells);
#endif
    return b->cells;
}
static const char *obs_public(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_public_sql(b,c);
#else
    return b->cells[c].public_sql;
#endif
}
static const char *obs_parser(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_parser_sql(b,c);
#else
    return b->cells[c].parser_sql;
#endif
}
static int obs_has_literal(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_has_literal(b,c);
#else
    return b->cells[c].has_literal;
#endif
}
static const sqlparser_literal_view_t *obs_literal(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_literal(b,c);
#else
    return &b->cells[c].literal;
#endif
}
static const sqlparser_dialect_multi_insert_value_t *obs_legacy(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_legacy(b,c);
#else
    return &b->cells[c];
#endif
}
static int same_text(const char *a,const char *b){return a&&b?!strcmp(a,b):a==b;}
static void compare_cell_logical(const sqlparser_dialect_multi_insert_branch_t *a,
    const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
    const sqlparser_literal_view_t *x=obs_literal(a,c),*y=obs_literal(b,c);
    const sqlparser_dialect_multi_insert_value_t empty={0};
    const sqlparser_dialect_multi_insert_value_t *u=obs_legacy(a,c),*v=obs_legacy(b,c);
    if(!u)u=&empty;
    if(!v)v=&empty;
    REQUIRE(same_text(obs_public(a,c),obs_public(b,c))&&same_text(obs_parser(a,c),obs_parser(b,c)));
    REQUIRE(obs_has_literal(a,c)==obs_has_literal(b,c)&&x->kind==y->kind);
    REQUIRE(same_text(x->string_value,y->string_value)&&same_text(x->float_value,y->float_value));
    REQUIRE(x->integer_value==y->integer_value&&x->boolean_value==y->boolean_value&&x->quoted_identifier==y->quoted_identifier);
    if(x->string_value)REQUIRE(x->string_value!=y->string_value);
    if(x->float_value)REQUIRE(x->float_value!=y->float_value);
    REQUIRE(u->has_bind==v->has_bind&&u->bind_kind==v->bind_kind&&!strcmp(u->bind,v->bind));
    REQUIRE(!strcmp(u->bind_sql,v->bind_sql)&&u->bind_position==v->bind_position&&u->has_bind_position==v->has_bind_position);
}
/* Additional complete logical private records do not replace any public,
 * owned-wire, selector, JSON or graph record in the original producer. */
static void logical_cells(sqlparser_handle_t *h)
{
    const sqlparser_dialect_multi_insert_t *m=multi(h);
    puts("logical_private_cells");
    for(size_t b=0;b<m->branch_count;b++)for(size_t c=0;c<m->branches[b].cell_count;c++){
        const sqlparser_dialect_multi_insert_branch_t *r=&m->branches[b];
        const sqlparser_dialect_multi_insert_value_t empty={0};
        const sqlparser_dialect_multi_insert_value_t *v=obs_legacy(r,c);if(!v)v=&empty;
        printf("branch=%zu cell=%zu has_literal=%d has_bind=%d bind_kind=%d bind_position=%zu has_bind_position=%d\n",
            b,c,obs_has_literal(r,c),v->has_bind,v->bind_kind,v->bind_position,v->has_bind_position);
        text(obs_public(r,c));text(obs_parser(r,c));rec_sqlparser_literal_view_t(obs_literal(r,c));
        text(v->bind);text(v->bind_sql);
    }
}
static void public_cell_sql_record(sqlparser_handle_t *h)
{
    const sqlparser_dialect_multi_insert_t *m=multi(h);size_t count=0;
    puts("public_cell_sql_getters");
    for(size_t b=0;b<m->branch_count;b++)for(size_t c=0;c<m->branches[b].cell_count;c++){
        const sqlparser_dialect_multi_insert_branch_t *r=&m->branches[b];
        const sqlparser_literal_view_t *literal=obs_literal(r,c);char *out=NULL;
        OK(sqlparser_insert_cell_sql(h,0,b,c,&out,&err));REQUIRE(out&&!strcmp(out,obs_public(r,c)));
        REQUIRE(outside(out,obs_public(r,c))&&outside(out,obs_parser(r,c)));
        REQUIRE(outside(out,literal->string_value)&&outside(out,literal->float_value));
        owned_text(h,out);text(out);sqlparser_string_free(out);count++;
    }
    fprintf(stderr,"public_sql_getters cells=%zu independent_owned_outputs=1 graph_materialized=%d\n",count,h->query_graph!=NULL);
}
typedef struct {uintptr_t start,end;} observed_owner_t;
static int owner_compare(const void *a,const void *b)
{
    uintptr_t x=((const observed_owner_t*)a)->start,y=((const observed_owner_t*)b)->start;
    return x<y?-1:x>y;
}
static void note_owner(sqlparser_handle_t *h,observed_owner_t *owners,size_t *count,const char *start,const char *last)
{
    REQUIRE(start&&last);owned_text(h,start);owned_text(h,last);
    owners[*count].start=(uintptr_t)start;
    owners[*count].end=(uintptr_t)last+strlen(last)+1U;
    REQUIRE(owners[*count].end>owners[*count].start);++*count;
}
static void require_storage(sqlparser_handle_t *h,const char *label,size_t compact_branches,size_t legacy_branches)
{
    const sqlparser_dialect_multi_insert_t *m=multi(h);size_t compact=0,legacy=0;
#ifndef COMPACT_BACKEND
    legacy_branches+=compact_branches;compact_branches=0;
#endif
    for(size_t b=0;b<m->branch_count;b++)if(obs_is_compact(&m->branches[b]))compact++;else legacy++;
    REQUIRE(compact==compact_branches&&legacy==legacy_branches);
    fprintf(stderr,"storage_expected %s compact_branches=%zu legacy_branches=%zu verified=1\n",label,compact,legacy);
}
static void observe_storage(sqlparser_handle_t *h,const char *label)
{
    const sqlparser_dialect_multi_insert_t *m=multi(h);
    size_t cells=0,compact=0,legacy=0,compact_branches=0,owners_count=0,text_bytes=0,initial_spans=0,certified_spans=0;
    for(size_t b=0;b<m->branch_count;b++)cells+=m->branches[b].cell_count;
    observed_owner_t *owners=calloc(cells?4*cells:1,sizeof(*owners));REQUIRE(owners);
    for(size_t b=0;b<m->branch_count;b++){
        const sqlparser_dialect_multi_insert_branch_t *r=&m->branches[b];int compact_kind=obs_is_compact(r);
        REQUIRE(!r->cell_count||obs_storage(r));compact_branches+=(size_t)compact_kind;
#ifdef COMPACT_BACKEND
        REQUIRE(sqlparser_oracle_branch_has_cells(r)==(obs_storage(r)!=NULL));
        if(compact_kind)REQUIRE(h->dialect==SQLPARSER_DIALECT_ORACLE ||
            h->dialect==SQLPARSER_DIALECT_VASTBASE_ORACLE ||
            h->dialect==SQLPARSER_DIALECT_KINGBASE_ORACLE);
#endif
        for(size_t c=0;c<r->cell_count;c++){
            const char *pub=obs_public(r,c),*parser=obs_parser(r,c);
            const sqlparser_literal_view_t *literal=obs_literal(r,c);
            const sqlparser_dialect_multi_insert_value_t *v=obs_legacy(r,c);
            REQUIRE(pub&&parser&&pub!=parser);owned_text(h,pub);owned_text(h,parser);
            if(compact_kind){
                const char *last=parser;compact++;
                REQUIRE(!v&&parser==pub+strlen(pub)+1U&&!strcmp(pub,parser));
                if(literal->string_value){REQUIRE(literal->string_value==parser+strlen(parser)+1U);last=literal->string_value;}
                if(literal->float_value){REQUIRE(!literal->string_value&&literal->float_value==parser+strlen(parser)+1U);last=literal->float_value;}
                note_owner(h,owners,&owners_count,pub,last);
            }else{
                legacy++;REQUIRE(v);
                note_owner(h,owners,&owners_count,pub,pub);note_owner(h,owners,&owners_count,parser,parser);
                if(v->literal_string_value){REQUIRE(literal->string_value==v->literal_string_value);note_owner(h,owners,&owners_count,v->literal_string_value,v->literal_string_value);}
                if(v->literal_float_value){REQUIRE(literal->float_value==v->literal_float_value);note_owner(h,owners,&owners_count,v->literal_float_value,v->literal_float_value);}
            }
            size_t ss=0,se=0,vp=0,cs=0,ce=0;
            initial_spans+=(size_t)sqlparser_oracle_multi_insert_initial_cell_span(h,b,c,&ss,&se,&vp,&cs,&ce);
            certified_spans+=(size_t)sqlparser_oracle_multi_insert_certified_cell_span(h,b,c,&cs,&ce);
        }
    }
    qsort(owners,owners_count,sizeof(*owners),owner_compare);
    for(size_t i=0;i<owners_count;i++){
        if(i)REQUIRE(owners[i-1].end<=owners[i].start);
        text_bytes+=(size_t)(owners[i].end-owners[i].start);
    }
    REQUIRE(sizeof(sqlparser_graph_dml_cell_t)==456U);
    size_t compact_size=0;
#ifdef COMPACT_BACKEND
    compact_size=sizeof(sqlparser_oracle_compact_value_t);REQUIRE(compact_size==64U);
#endif
    fprintf(stderr,"storage %s dialect=%d branches=%zu compact_branches=%zu legacy_branches=%zu cells=%zu compact_cells=%zu legacy_cells=%zu compact_record_size=%zu legacy_record_size=%zu live_cell_record_bytes=%zu live_text_owners=%zu live_text_bytes=%zu owner_ranges_disjoint=1 independent_NUL=1 initial_span_hits=%zu certified_span_hits=%zu graph_materialized=%d public_cell_size=%zu allocation_call_count=unmeasured capacity_bytes=unmeasured\n",
        label,h->dialect,m->branch_count,compact_branches,m->branch_count-compact_branches,cells,compact,legacy,
        compact_size,sizeof(sqlparser_dialect_multi_insert_value_t),compact*compact_size+legacy*sizeof(sqlparser_dialect_multi_insert_value_t),
        owners_count,text_bytes,initial_spans,certified_spans,h->query_graph!=NULL,sizeof(sqlparser_graph_dml_cell_t));
    free(owners);
}
