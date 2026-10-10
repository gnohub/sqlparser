/* Ordinary complete successes only. These cases use the real library and
 * public mutation APIs, plus ordinary internal clone/reparse lifetime entry
 * points. No private mutation, wrapper, injected failure, or failure main. */
#define main unchanged_success_driver_main
#include "success_driver.c"
#undef main
#include "sqlparser_ast_internal.h"

static sqlparser_graph_dml_cell_t public_cell(sqlparser_handle_t *h,size_t b,size_t c)
{
    sqlparser_query_graph_view_t g;sqlparser_graph_dml_t d;sqlparser_graph_dml_branch_t branch;
    sqlparser_graph_dml_cell_t cell;size_t bi,ci;
    OK(sqlparser_statement_query_graph(h,0,&g,&err));OK(sqlparser_query_graph_dml(&g,&d,&err));
    OK(sqlparser_query_graph_span_index_at(&g,d.branches,b,&bi,&err));
    OK(sqlparser_query_graph_dml_branch_at(&g,bi,&branch,&err));
    OK(sqlparser_query_graph_span_index_at(&g,branch.rows,c,&ci,&err));
    OK(sqlparser_query_graph_dml_cell_at(&g,ci,&cell,&err));return cell;
}
static void origins(sqlparser_handle_t *h,const char *label)
{
    const sqlparser_identifier_origin_map_t *a=NULL,*b=NULL;
    OK(sqlparser_identifier_origins_for_handle(h,&a,&err));REQUIRE(a);
    OK(sqlparser_identifier_origins_for_handle(h,&b,&err));REQUIRE(a==b);
    size_t n=sqlparser_identifier_origin_map_output_length(a);REQUIRE(n==h->parser_sql_len);
    printf("origins %s output_length=%zu\n",label,n);
    /* Every valid span, including empty spans, in the small source SELECT.
     * No out-of-bounds or corrupted origin objects are introduced. */
    for(size_t start=0;start<=n;start++)for(size_t count=0;count<=n-start;count++){
        sqlparser_identifier_origin_t v;
        sqlparser_identifier_origin_kind_t k=sqlparser_identifier_origin_map_lookup(a,start,count,&v);
        printf("origin=%zu,%zu,%d,%d,%zu,%zu\n",start,count,k,v.kind,v.source_offset,v.source_length);
    }
    fprintf(stderr,"origins %s valid_spans=%zu warm_same_map=1 private_writes=0\n",label,(n+1U)*(n+2U)/2U);
}
static void assert_output(sqlparser_handle_t *h,const char *wanted,const char *label)
{
    char *out=deparse(h);REQUIRE(!strcmp(out,wanted));sqlparser_string_free(out);
    snapshot(h,label);origins(h,label);
}
static void patch_expected(sqlparser_handle_t *h,sqlparser_patch_t *patches,size_t count,
    const char *wanted,const char *label,size_t compact_branches,size_t legacy_branches)
{
    unsigned long generation=h->generation;
    apply(h,patches,count);REQUIRE(h->generation==generation+1UL);
    require_storage(h,label,compact_branches,legacy_branches);assert_output(h,wanted,label);
}

/* Interleave BIND/DEFAULT/expression/grouped literal fixtures with ordinary
 * identity branches. The bind-free pair tests a compact/legacy string commit. */
static void mixed_storage(int dialect)
{
    static const char input[]="INSERT ALL INTO T (A, B, C, D) VALUES ('compact', 1.25, NULL, CURRENT_TIMESTAMP) "
        "INTO U (A, B, C, D) VALUES ('legacy', DEFAULT, ('grouped string'), coalesce(1,2)) SELECT 1 FROM Dual";
    static const char wanted[]="INSERT ALL INTO T (A, B, C, D) VALUES ('raw changed', 1.25, NULL, CURRENT_TIMESTAMP) "
        "INTO U (A, B, C, D) VALUES ('typed changed', DEFAULT, ('grouped string'), coalesce(1,2)) SELECT 1 FROM Dual";
    stage="ordinary-mixed-storage-hot";sqlparser_handle_t *h=parse(input,dialect,0),*clone=NULL;
    require_storage(h,"mixed.hot.initial",1,1);snapshot(h,"mixed.hot.initial");origins(h,"mixed.hot.initial");
    const void *state=h->dialect_state,*root=multi(h);
    const void *owner0=obs_storage(&multi(h)->branches[0]),*owner1=obs_storage(&multi(h)->branches[1]);
    sqlparser_literal_value_t value={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value="typed changed"};
    sqlparser_patch_t p[]={
        {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][0]",.sql="'raw changed'"},
        {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[1][0]",.literal=&value}};
    patch_expected(h,p,2,wanted,"mixed.hot.commit",1,1);
    REQUIRE(h->dialect_state==state&&multi(h)==root);
    REQUIRE(obs_storage(&multi(h)->branches[0])==owner0&&obs_storage(&multi(h)->branches[1])==owner1);
    deep_clone(h,&clone);sqlparser_handle_destroy(h);h=clone;
    assert_output(h,wanted,"mixed.hot.clone.owner_destroyed");sqlparser_handle_destroy(h);

    static const char binds[]="INSERT ALL INTO T (A, B) VALUES ('compact', 7) "
        "INTO U (A, B) VALUES (:B, 'legacy') SELECT 1 FROM Dual";
    static const char bound_wanted[]="INSERT ALL INTO T (A, B) VALUES ('changed', 7) "
        "INTO U (A, B) VALUES (:B, 'legacy') SELECT 1 FROM Dual";
    stage="ordinary-mixed-storage-bind-fallback";h=parse(binds,dialect,0);
    require_storage(h,"mixed.bind.initial",1,1);snapshot(h,"mixed.bind.initial");origins(h,"mixed.bind.initial");
    p[0].sql="'changed'";patch_expected(h,p,1,bound_wanted,"mixed.bind.generic_commit",0,2);
    deep_clone(h,&clone);sqlparser_handle_destroy(h);h=clone;
    assert_output(h,bound_wanted,"mixed.bind.clone.owner_destroyed");sqlparser_handle_destroy(h);
    fprintf(stderr,"PASS mixed_storage dialect=%d successful_hot_and_generic=1\n",dialect);
}

/* Borrow all user inputs before mutation. A raw expression forces the generic
 * route and retires the compact cells whose decoded strings provide the later
 * selector, typed literal and raw expression inputs. */
static void borrowed_generic(int dialect)
{
    static const char input[]="INSERT ALL INTO T (A, B, C, D) VALUES ('borrowed-''Ω''', 'stmt[0].insert_cell[0][2]', 'old', 'upper(''text'')') SELECT 1 FROM Dual";
    static const char wanted[]="INSERT ALL INTO T (A, B, C, D) VALUES (upper('text'), 'stmt[0].insert_cell[0][2]', 'borrowed-''Ω''', upper('text')) SELECT 1 FROM Dual";
    stage="ordinary-borrowed-generic";sqlparser_handle_t *h=parse(input,dialect,0),*clone=NULL;
    require_storage(h,"borrowed.generic.initial",1,0);snapshot(h,"borrowed.generic.initial");
    sqlparser_graph_dml_cell_t a=public_cell(h,0,0),s=public_cell(h,0,1),r=public_cell(h,0,3);
    REQUIRE(a.literal.string_value&&s.literal.string_value&&r.literal.string_value);
    sqlparser_literal_value_t value={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value=a.literal.string_value};
    sqlparser_patch_t p[]={
        {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][0]",.sql=r.literal.string_value},
        {.op=SQLPARSER_PATCH_REPLACE,.selector=s.literal.string_value,.literal=&value},
        {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][3]",.sql=r.literal.string_value}};
    patch_expected(h,p,3,wanted,"borrowed.generic.commit",0,1);
    deep_clone(h,&clone);sqlparser_handle_destroy(h);h=clone;
    assert_output(h,wanted,"borrowed.generic.clone.owner_destroyed");sqlparser_handle_destroy(h);
    fprintf(stderr,"PASS borrowed_generic dialect=%d raw_and_typed_inputs_consumed=1\n",dialect);
}

/* Exact successful indexed mutations already used by
 * test_dialect_surface_state.c, retaining their expected SQL literals. */
static void indexed_replacements(int dialect)
{
    static const char input[]="INSERT ALL INTO t1 (c1, c2, c3) VALUES (1, 2, 3) INTO t2 (c1, c2, c3) VALUES (4, 5, 6) SELECT 1 FROM dual";
    static const char *wanted[]={
        "INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, 2, 3) INTO t2 (c1, c2, c3) VALUES (4, 5, 6) SELECT 1 FROM dual",
        "INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, LOWER('indexed'), 3) INTO t2 (c1, c2, c3) VALUES (4, 5, 6) SELECT 1 FROM dual",
        "INSERT ALL INTO t1 (c1, c2, c3) VALUES (2147483648, LOWER('indexed'), :indexed_name) INTO t2 (c1, c2, c3) VALUES (4, 5, 6) SELECT 1 FROM dual"};
    stage="ordinary-indexed-replacements";sqlparser_handle_t *h=parse(input,dialect,0);
    require_storage(h,"indexed.initial",2,0);snapshot(h,"indexed.initial");
    unsigned long gen=h->generation;
    sqlparser_literal_value_t integer={.kind=SQLPARSER_LITERAL_KIND_INTEGER,.integer_value=2147483648LL};
    OK(sqlparser_insert_set_cell_literal(h,0,0,0,&integer,&err));REQUIRE(h->generation==gen+1UL);
    require_storage(h,"indexed.typed.generic",0,2);assert_output(h,wanted[0],"indexed.typed.generic");
    gen=h->generation;OK(sqlparser_insert_set_cell_sql(h,0,0,1,"LOWER('indexed')",&err));REQUIRE(h->generation==gen+1UL);
    require_storage(h,"indexed.raw.generic",0,2);assert_output(h,wanted[1],"indexed.raw.generic");
    sqlparser_bind_value_t bind={.kind=SQLPARSER_BIND_KIND_NAMED,.key="indexed_name"};
    gen=h->generation;OK(sqlparser_insert_set_cell_bind(h,0,0,2,&bind,&err));REQUIRE(h->generation==gen+1UL);
    assert_output(h,wanted[2],"indexed.bind.generic");sqlparser_handle_destroy(h);
    fprintf(stderr,"PASS indexed_replacements dialect=%d typed_raw_bind=1\n",dialect);
}

static void borrowed_column(int dialect)
{
    static const char input[]="INSERT ALL INTO T (A, B) VALUES ('Added', 'borrowed value') INTO U (A) VALUES ('second') SELECT 1 FROM Dual";
    static const char wanted[]="INSERT ALL INTO T (A, Added, B) VALUES ('changed', 'borrowed value', 'borrowed value') INTO U (A) VALUES ('second') SELECT 1 FROM Dual";
    stage="ordinary-borrowed-column";sqlparser_handle_t *h=parse(input,dialect,0);
    require_storage(h,"column.borrowed.initial",2,0);snapshot(h,"column.borrowed.initial");
    sqlparser_graph_dml_cell_t name=public_cell(h,0,0),literal=public_cell(h,0,1);
    sqlparser_literal_value_t v={.kind=SQLPARSER_LITERAL_KIND_STRING,.string_value=literal.literal.string_value};
    sqlparser_patch_t p[]={
        {.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][0]",.sql="'changed'"},
        {.op=SQLPARSER_PATCH_INSERT_COLUMN,.selector="stmt[0].insert_branch_columns[0]",.index=1,.name=name.literal.string_value,.literal=&v}};
    patch_expected(h,p,2,wanted,"column.borrowed.commit",0,2);sqlparser_handle_destroy(h);
    fprintf(stderr,"PASS borrowed_column dialect=%d borrowed_name_and_literal=1\n",dialect);
}

/* Exact successful omitted-column fixture from test_core_api.c. All names
 * are filled in one legal batch; no invalid intermediate graph is requested. */
static void name_only(int dialect)
{
    static const char input[]="INSERT ALL INTO t1 VALUES (1, 'a') INTO t2 VALUES (2, 'b', 'x') SELECT source_id FROM src";
    static const char wanted[]="INSERT ALL INTO t1 (ID, NOTE) VALUES (1, 'a') INTO t2 (CODE, LABEL, EXTRA) VALUES (2, 'b', 'x') SELECT source_id FROM src";
    stage="ordinary-name-only-columns";sqlparser_handle_t *h=parse(input,dialect,0);
    require_storage(h,"name_only.initial",2,0);snapshot(h,"name_only.initial");
    sqlparser_patch_t p[]={
        {.op=SQLPARSER_PATCH_INSERT_COLUMN,.selector="stmt[0].insert_branch_columns[0]",.index=0,.name="ID"},
        {.op=SQLPARSER_PATCH_INSERT_COLUMN,.selector="stmt[0].insert_branch_columns[0]",.index=1,.name="NOTE"},
        {.op=SQLPARSER_PATCH_INSERT_COLUMN,.selector="stmt[0].insert_branch_columns[1]",.index=0,.name="CODE"},
        {.op=SQLPARSER_PATCH_INSERT_COLUMN,.selector="stmt[0].insert_branch_columns[1]",.index=1,.name="LABEL"},
        {.op=SQLPARSER_PATCH_INSERT_COLUMN,.selector="stmt[0].insert_branch_columns[1]",.index=2,.name="EXTRA"}};
    patch_expected(h,p,5,wanted,"name_only.commit",0,2);sqlparser_handle_destroy(h);
    fprintf(stderr,"PASS name_only dialect=%d positive_omitted_list_batch=1\n",dialect);
}

/* Accepted generic boundaries from test_oracle_owned_commit.c; use only
 * ordinary API calls. In particular, never clear or forge an origin cache. */
static void generic_boundaries(int dialect)
{
    static const struct {const char *before,*after,*source,*mode;int compact;} cases[]={
        {"q'[old-Ω]'","'ordinary'","SELECT 1 FROM Dual","ALL",0},
        {"'old'","q'{new-Ω}'","SELECT 1 FROM Dual","ALL",1},
        {"'old'","'changed'","SELECT :BindName AS C FROM Dual","ALL",1},
        {":BranchBind","'changed'","SELECT 1 FROM Dual","ALL",0},
        {"'old'","'line\ntext'","SELECT 1 FROM Dual","ALL",1},
        {"'old'","'slash\\text'","SELECT 1 FROM Dual","ALL",1},
        {"'old'","upper('text')","SELECT 1 FROM Dual","ALL",1},
        {"'old'","'changed'","SELECT 1 FROM Dual","FIRST WHEN 1 = 1 THEN",0},
        {"'old'","'changed'","SELECT 1 FROM Dual","ALL WHEN 1 = 1 THEN",0},
        {"'old'","'changed'","SELECT S.C FROM SourceTable@RemoteLink S","ALL",1},
        {"'old'","'changed'","SELECT 1 FROM Dual MINUS SELECT 2 FROM Dual","ALL",1}};
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        char input[512],wanted[512],label[80];
        snprintf(input,sizeof(input),"INSERT %s INTO T (A) VALUES (%s) %s",cases[i].mode,cases[i].before,cases[i].source);
        snprintf(wanted,sizeof(wanted),"INSERT %s INTO T (A) VALUES (%s) %s",cases[i].mode,cases[i].after,cases[i].source);
        snprintf(label,sizeof(label),"generic.boundary.%zu",i);stage=label;
        sqlparser_handle_t *h=parse(input,dialect,0),*clone=NULL;
        require_storage(h,label,(size_t)cases[i].compact,(size_t)!cases[i].compact);snapshot(h,label);origins(h,label);
        sqlparser_patch_t p={.op=SQLPARSER_PATCH_REPLACE,.selector="stmt[0].insert_cell[0][0]",.sql=cases[i].after};
        patch_expected(h,&p,1,wanted,label,0,1);
        deep_clone(h,&clone);sqlparser_handle_destroy(h);h=clone;assert_output(h,wanted,"boundary.clone.owner_destroyed");
        char *owned=deparse(h);unsigned long gen=h->generation;
        OK(sqlparser_handle_reparse_destructive(h,&owned,&err));REQUIRE(!owned&&h->generation==gen+1UL);
        require_storage(h,"boundary.destructive_reparse",0,1);assert_output(h,wanted,"boundary.destructive_reparse");
        sqlparser_handle_destroy(h);
    }
    fprintf(stderr,"PASS generic_boundaries dialect=%d cases=11 clone_destroy_reparse_origins=1\n",dialect);
}

/* Fixed positive spellings from valuefacts/cases.h and the successful graph
 * classification grammar loop. Expected tags are fixture expectations only;
 * no baseline token classification is replaced by current implementation logic. */
static void scalar_eligibility(int dialect)
{
    static const struct {const char *sql;int compact;} values[]={
        {"''",1},{"'Ω中'",1},{"''''",1},{"'a''b'",1},
        {"0",1},{"-0",1},{"+42",1},{"0001",1},{"2147483648",1},
        {"9223372036854775807",1},{"-9223372036854775808",1},
        {"9223372036854775808",1},{"-9223372036854775809",1},
        {"9999999999999999999999999999999999999999999",1},
        {"0.0",1},{"1.",1},{".5",1},{"+.5",1},{"-.5",1},{"000.010",1},{"NuLl",1},
        {"CURRENT_DATE",1},{"CURRENT_TIME",1},{"CURRENT_TIMESTAMP",1},{"LOCALTIME",1},
        {"LOCALTIMESTAMP",1},{"CURRENT_ROLE",1},{"CURRENT_USER",1},{"SESSION_USER",1},
        {"USER",1},{"CURRENT_CATALOG",1},{"CURRENT_SCHEMA",1},{"CuRrEnT_DaTe",1},
        {"CURRENT_TIMESTAMP(3)",0},{"CURRENT_TIMESTAMP /* precision */ (3)",0},
        {"(CURRENT_TIMESTAMP)",0},{"DEFAULT",0},{"SourceAlias",0},{":B",0},
        {"coalesce(:B,1)",0},{"(2+3)",0},{"q'[x''y]'",0},{"1e3",0},
        {"'a\\b'",0},{"'a\nb'",0},{"1/*tail*/",0}};
    for(size_t i=0;i<sizeof(values)/sizeof(values[0]);i++){
        char input[512],label[80];snprintf(label,sizeof(label),"scalar.eligibility.%zu",i);stage=label;
        snprintf(input,sizeof(input),"INSERT ALL INTO T (A,B) VALUES ('keep',%s) SELECT S.C AS SourceAlias FROM SourceTable S",values[i].sql);
        sqlparser_handle_t *h=parse(input,dialect,0),*clone=NULL;
        require_storage(h,label,(size_t)values[i].compact,(size_t)!values[i].compact);snapshot(h,label);
        deep_clone(h,&clone);sqlparser_handle_destroy(h);h=clone;
        snapshot(h,"scalar.clone.owner_destroyed");sqlparser_handle_destroy(h);
    }
    fprintf(stderr,"PASS scalar_eligibility dialect=%d cases=%zu\n",dialect,sizeof(values)/sizeof(values[0]));
}

int main(void)
{
    static const int dialects[]={SQLPARSER_DIALECT_ORACLE,SQLPARSER_DIALECT_VASTBASE_ORACLE,
        SQLPARSER_DIALECT_KINGBASE_ORACLE};
    REQUIRE(setlocale(LC_CTYPE,"C"));
    for(size_t i=0;i<sizeof(dialects)/sizeof(dialects[0]);i++){
        mixed_storage(dialects[i]);borrowed_generic(dialects[i]);indexed_replacements(dialects[i]);
        borrowed_column(dialects[i]);name_only(dialects[i]);generic_boundaries(dialects[i]);scalar_eligibility(dialects[i]);
    }
    fprintf(stderr,"PASS compact_lifecycle dialects=3 generic_boundaries=33 private_mutations=0\n");return 0;
}
