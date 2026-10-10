static void legacy_public(sqlparser_handle_t*h){sqlparser_statement_kind_t kind;const char*node=NULL;size_t n=0;OK(sqlparser_statement_kind(h,0,&kind,&err));printf("statement_kind=%d\n",kind);OK(sqlparser_statement_node_name(h,0,&node,&err));text(node);OK(sqlparser_statement_relation_count(h,0,&n,&err));printf("relation_count=%zu\n",n);for(size_t i=0;i<n;i++){sqlparser_relation_view_t r;OK(sqlparser_statement_relation(h,0,i,&r,&err));rec_sqlparser_relation_view_t(&r);}OK(sqlparser_statement_name_count(h,0,&n,&err));printf("name_count=%zu\n",n);for(size_t i=0;i<n;i++){sqlparser_name_view_t v;OK(sqlparser_statement_name(h,0,i,&v,&err));rec_sqlparser_name_view_t(&v);}}
static void shadow_json(const char*sql,int dialect,const char*label,int preload){sqlparser_handle_t*h=parse(sql,dialect,1);REQUIRE(!h->query_graph);REQUIRE(!h->ast);puts(label);if(preload){capture_graph(h);fprintf(stderr,"json %s preload_graph=1 graph_before=%d ast_before=%d\n",label,h->query_graph!=NULL,h->ast!=NULL);}else fprintf(stderr,"json %s preload_graph=0 graph_before=0 ast_before=0\n",label);const void*cache=h->query_graph;char*j=NULL;OK(sqlparser_export_view_json(h,0,&j,&err));REQUIRE(j);text(j);fprintf(stderr,"json %s bytes=%zu graph_after=%d ast_after=%d same_preloaded_cache=%d max_output=134217728\n",label,strlen(j),h->query_graph!=NULL,h->ast!=NULL,preload&&cache==h->query_graph);if(preload)REQUIRE(cache==h->query_graph);sqlparser_string_free(j);wire_record(h);legacy_public(h);sqlparser_handle_destroy(h);}


static int family_expected(int dialect,int expected){return dialect==SQLPARSER_DIALECT_DAMENG?0:expected;}
static void snapshot(sqlparser_handle_t *h,const char *label)
{
    REQUIRE(!h->query_graph);
    public_capture(h,label);
    const void *cache=h->query_graph;REQUIRE(cache);
    sqlparser_query_graph_view_t g;sqlparser_graph_dml_t d;
    OK(sqlparser_statement_query_graph(h,0,&g,&err));REQUIRE(h->query_graph==cache);
    OK(sqlparser_query_graph_dml(&g,&d,&err));
    size_t columns=0,cells=0;
    for(size_t b=0;b<d.branches.count;b++) {
        size_t bi;sqlparser_graph_dml_branch_t br;
        OK(sqlparser_query_graph_span_index_at(&g,d.branches,b,&bi,&err));
        OK(sqlparser_query_graph_dml_branch_at(&g,bi,&br,&err));
        columns+=br.target_columns.count;cells+=br.rows.count;
    }
    fprintf(stderr,"graph %s dialect=%d branches=%zu columns=%zu cells=%zu fresh_then_warm_same_cache=1 no_runtime_bypass_counter=1\n",label,h->dialect,d.branches.count,columns,cells);
    char *s=deparse(h);
    shadow_json(s,h->dialect,label,0);shadow_json(s,h->dialect,label,1);
    sqlparser_string_free(s);
}
static void deep_clone(sqlparser_handle_t *h,sqlparser_handle_t **out)
{
    OK(sqlparser_handle_clone(h,out,&err));REQUIRE(*out&&!(*out)->query_graph);
    const sqlparser_dialect_multi_insert_t *a=multi(h),*b=multi(*out);
    REQUIRE(a!=b&&a->branches!=b->branches&&a->branch_count==b->branch_count);
    for(size_t i=0;i<a->branch_count;i++) {
        const sqlparser_dialect_multi_insert_branch_t *x=&a->branches[i],*y=&b->branches[i];
        REQUIRE((!x->column_count||x->columns!=y->columns)&&obs_storage(x)!=obs_storage(y));
        REQUIRE(!obs_is_compact(y));
        REQUIRE(x->column_count==y->column_count&&x->cell_count==y->cell_count);
#define DISTINCT(field) do{if(x->relation.field){REQUIRE(y->relation.field&&x->relation.field!=y->relation.field&&!strcmp(x->relation.field,y->relation.field));}else REQUIRE(!y->relation.field);}while(0)
        DISTINCT(database_name);DISTINCT(schema_name);DISTINCT(table_name);DISTINCT(link_name);DISTINCT(link_sql);DISTINCT(sql);
#undef DISTINCT
        for(size_t c=0;c<x->column_count;c++) {
            REQUIRE(x->columns[c].name!=y->columns[c].name&&!strcmp(x->columns[c].name,y->columns[c].name));
            REQUIRE(x->columns[c].sql!=y->columns[c].sql&&!strcmp(x->columns[c].sql,y->columns[c].sql));
        }
        for(size_t c=0;c<x->cell_count;c++) {
            REQUIRE(obs_public(x,c)!=obs_public(y,c)&&!strcmp(obs_public(x,c),obs_public(y,c)));
            REQUIRE(obs_parser(x,c)!=obs_parser(y,c)&&!strcmp(obs_parser(x,c),obs_parser(y,c)));
            compare_cell_logical(x,y,c);
        }
    }
    require_storage(*out,"clone.before_owner_destroy",0,b->branch_count);
    observe_storage(*out,"clone.before_owner_destroy");
    header_state(*out,"clone.before_owner_destroy",0);
}
static void apply(sqlparser_handle_t *h,sqlparser_patch_t *p,size_t n)
{
    sqlparser_patch_list_t list={p,n};OK(sqlparser_apply_patch(h,&list,&err));
    REQUIRE(!h->query_graph);header_state(h,"successful_public_patch",0);
}
static const char *lifecycle_sql=
    "insert all into t(a,b,c) values ('one','old',1) "
    "into u(x,y,z) values ('two','other',2) select 1 from dual";
static void lifecycle(int dialect)
{
    stage="normal-lifecycle";
    sqlparser_handle_t *h=parse(lifecycle_sql,dialect,0),*clone=NULL;
    header_state(h,"lifecycle.initial",family_expected(dialect,1));
    snapshot(h,"lifecycle.initial.fresh");deep_clone(h,&clone);
    sqlparser_handle_destroy(h);h=clone;
    header_state(h,"lifecycle.clone.owner_destroyed",0);
    snapshot(h,"lifecycle.clone.after_owner_destroy.fresh");sqlparser_handle_destroy(h);

    h=parse(lifecycle_sql,dialect,0);header_state(h,"lifecycle.before_commit",family_expected(dialect,1));
    sqlparser_literal_value_t literal={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value="typed $123 'kept'"};
    sqlparser_patch_t p[2]={
        {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][1]",.sql="'raw-replaced'"},
        {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[1][1]",.literal=&literal}
    };
    apply(h,p,2);snapshot(h,"lifecycle.after_raw_typed_commit.fresh");
    deep_clone(h,&clone);sqlparser_handle_destroy(h);h=clone;
    header_state(h,"lifecycle.clone_after_commit.owner_destroyed",0);
    snapshot(h,"lifecycle.clone_after_commit.owner_destroyed.fresh");
    char *out=deparse(h);sqlparser_handle_destroy(h);
    h=parse(out,dialect,0);header_state(h,"lifecycle.new_parse_deparse_output",family_expected(dialect,1));
    snapshot(h,"lifecycle.new_parse_deparse_output.fresh");
    sqlparser_handle_destroy(h);sqlparser_string_free(out);

    h=parse(lifecycle_sql,dialect,0);header_state(h,"lifecycle.before_column_insert",family_expected(dialect,1));
    sqlparser_patch_t ins={.op=SQLPARSER_PATCH_INSERT_COLUMN,.selector="stmt[0].insert_branch_columns[1]",.index=1,.name="\"Added Column\"",.literal=&literal};
    apply(h,&ins,1);REQUIRE(multi(h)->branches[0].column_count==3&&multi(h)->branches[1].column_count==4);
    REQUIRE(!strcmp(multi(h)->branches[1].columns[1].name,"Added Column"));
    snapshot(h,"lifecycle.after_quoted_column_insert.fresh");
    out=deparse(h);sqlparser_handle_destroy(h);h=parse(out,dialect,0);
    header_state(h,"lifecycle.new_parse_quoted_output",0);snapshot(h,"lifecycle.new_parse_quoted_output.fresh");
    sqlparser_handle_destroy(h);sqlparser_string_free(out);

    h=parse(lifecycle_sql,dialect,0);header_state(h,"lifecycle.before_source_target_replace",family_expected(dialect,1));
    sqlparser_query_graph_view_t g;OK(sqlparser_statement_query_graph(h,0,&g,&err));
    char *target_selector=NULL;
    for(size_t i=0;i<g.target_count;i++) {
        sqlparser_graph_target_t t;OK(sqlparser_query_graph_target_at(&g,i,&t,&err));
        if(t.has_selector&&t.selector.kind==SQLPARSER_SELECTOR_KIND_SELECT_TARGET) {
            OK(sqlparser_selector_format(&t.selector,&target_selector,&err));break;
        }
    }
    REQUIRE(target_selector);sqlparser_patch_t target={.op=SQLPARSER_PATCH_REPLACE,.selector=target_selector,.sql="2"};
    apply(h,&target,1);sqlparser_string_free(target_selector);
    snapshot(h,"lifecycle.after_public_source_target_replace.fresh");sqlparser_handle_destroy(h);
    fprintf(stderr,"PASS lifecycle dialect=%d normal_clone=2 public_patch_batches=3 fresh_snapshots=8\n",dialect);
}
static char *column_sql(size_t columns)
{
    char *sql=calloc(8192,1),*p=sql;REQUIRE(sql);
    p+=sprintf(p,"insert all ");
    for(size_t branch=0;branch<2;branch++) {
        p+=sprintf(p,"into %s(",branch?"u":"t");
        for(size_t c=0;c<columns;c++)p+=sprintf(p,"%sc%zu",c?",":"",c+1);
        p+=sprintf(p,") values (");
        for(size_t c=0;c<columns;c++)p+=sprintf(p,"%s%zu",c?",":"",c+1);
        p+=sprintf(p,") ");
    }
    strcpy(p,"select 1 from dual");return sql;
}
static void matrix(void)
{
    static const struct {const char *name,*sql;int simple;} cases[]={
        {"same-ascii-header", "insert all into t(a,b) values ('x',1) into t(a,b) values ('y',2) select 1 from dual",1},
        {"different-ascii-headers", "insert all into t(a,b) values ('x',1) into u(c,d) values ('y',2) into t(a,b) values ('z',3) select 1 from dual",1},
        {"qualified-relation-parts", "insert all into t(a) values (1) into s.t(b) values (2) into db.s.t(c) values (3) select 1 from dual",1},
        {"ascii-underscore-digit", "insert all into _t9(_a1,B2) values (1,2) into _t9(_a1,B2) values (3,4) select 1 from dual",1},
        {"ascii-space-tab-newline", "insert all into s \t.\n t ( a ,\tb\n) values (1,2) into s.t(a,b) values (3,4) select 1 from dual",1},
        {"ascii-cr-formfeed-vtab", "insert all into s\r.\f t\v(\ra\f,\vb\r) values (1,2) select 1 from dual",1},
        {"ascii-keyword-names", "insert all into t(timestamp,key) values (1,2) select 1 from dual",1},
        {"quoted-relation-first", "insert all into \"Quoted\"(a,b) values (1,2) into t(a,b) values (3,4) select 1 from dual",0},
        {"quoted-relation-last", "insert all into t(a,b) values (1,2) into \"Quoted\"(a,b) values (3,4) select 1 from dual",0},
        {"quoted-columns", "insert all into t(\"A\",\"b\") values (1,2) into t(a,b) values (3,4) select 1 from dual",0},
        {"escaped-delimited-names", "insert all into \"T\"\"Q\"(\"A\"\"B\",c) values (1,2) select 1 from dual",0},
        {"quoted-qualified-relation", "insert all into \"Db\".\"Schema\".\"Table\"(a,b) values (1,2) select 1 from dual",0},
        {"relation-comment", "insert all into t/*header*/(a,b) values (1,2) into u(a,b) values (3,4) select 1 from dual",0},
        {"column-comment", "insert all into t(a/*column*/,b) values (1,2) select 1 from dual",0},
        {"header-line-comment", "insert all into t-- header\n(a,b) values (1,2) select 1 from dual",0},
        {"database-link", "insert all into s.t@remote_link(a,b) values (1,2) select 1 from dual",0},
        {"nonascii-relation-column", "insert all into 表名(字段,名字) values (1,2) select 1 from dual",0},
        {"quoted-nonascii", "insert all into \"表名\"(\"字段\",\"名字\") values (1,2) select 1 from dual",0},
        {"dollar-hash-identifiers", "insert all into t$(_a#,b$) values (1,2) select 1 from dual",0},
        {"no-column-list", "insert all into t values (1,2) into u(a,b) values (3,4) select 1 from dual",0},
        {"first-when-else", "insert first when src>0 then into t(a,b) values (src,1) into u(c,d) values (src,2) else into t(a,b) values (src,3) select s.x as src from source_table s",0},
        {"all-when-else", "insert all when src>0 then into t(a,b) values (src,1) else into u(c,d) values (src,2) select s.x as src from source_table s",0},
        {"values-parser-public-rewrite", "insert all into t(a,b,c) values (q'[$123]',:named,coalesce(:p,1)) into u(x,y,z) values ('中文',?,CURRENT_TIMESTAMP) select 1 from dual",1},
        {"values-comments-quotes", "insert all into t(a,b) values ('a''b' /* value */,q'[quoted]') into t(a,b) values ('x','y') select 1 from dual",1},
        {"source-minus-rewrite", "insert all into t(a) values (1) into u(b) values (2) select 1 from dual minus select 2 from dual",1}
    };
    static const int dialects[]={SQLPARSER_DIALECT_ORACLE,SQLPARSER_DIALECT_VASTBASE_ORACLE,SQLPARSER_DIALECT_KINGBASE_ORACLE,SQLPARSER_DIALECT_DAMENG};
    for(size_t d=0;d<sizeof(dialects)/sizeof(dialects[0]);d++) {
        for(size_t c=0;c<sizeof(cases)/sizeof(cases[0]);c++) {
            stage=cases[c].name;sqlparser_handle_t *h=parse(cases[c].sql,dialects[d],0);
            header_state(h,cases[c].name,family_expected(dialects[d],cases[c].simple));snapshot(h,cases[c].name);
            header_state(h,"after_warm_graph",family_expected(dialects[d],cases[c].simple));sqlparser_handle_destroy(h);
            fprintf(stderr,"PASS matrix %s dialect=%d\n",cases[c].name,dialects[d]);
        }
        for(size_t n=31;n<=33;n++) {
            char label[32];snprintf(label,sizeof(label),"column-boundary-%zu",n);stage=label;
            char *s=column_sql(n);sqlparser_handle_t *h=parse(s,dialects[d],0);
            header_state(h,label,family_expected(dialects[d],n<=32));snapshot(h,label);
            sqlparser_handle_destroy(h);free(s);fprintf(stderr,"PASS matrix %s dialect=%d\n",label,dialects[d]);
        }
        lifecycle(dialects[d]);
    }
    fprintf(stderr,"PASS matrix cases=112 lifecycle_dialects=4\n");
}
static void original_json(const char *path)
{
    char *s=read_original(path),*t=expected(s);
    shadow_json(s,SQLPARSER_DIALECT_ORACLE,"original.initial.shadow.fresh",0);
    shadow_json(s,SQLPARSER_DIALECT_ORACLE,"original.initial.shadow.preloaded",1);
    shadow_json(t,SQLPARSER_DIALECT_ORACLE,"original.patched.shadow.fresh",0);
    shadow_json(t,SQLPARSER_DIALECT_ORACLE,"original.patched.shadow.preloaded",1);
    free(s);free(t);
}
