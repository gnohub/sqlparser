#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sqlparser/sqlparser.h"
typedef struct {
    const char *name, *sql, *expected_one, *expected_few;
    size_t few;
    const char *selectors[3];
    const char *strings[3];
    long long integers[3];
} fixture_t;
static const fixture_t fixtures[]={
    {"select","SELECT id, name FROM t WHERE id = 1 AND name = 'old'",
     "SELECT id, name FROM t WHERE id = 2 AND name = 'old'",
     "SELECT id, name FROM t WHERE id = 2 AND name = 'new'",2,
     {"stmt[0].where_literal[0]","stmt[0].where_literal[1]",NULL},
     {NULL,"new",NULL},{2,0,0}},
    {"select_join","SELECT a.id, b.name FROM t a JOIN u b ON a.id=b.id WHERE a.score=1 AND b.name='old' LIMIT 20",
     "SELECT a.id, b.name FROM t a JOIN u b ON a.id=b.id WHERE a.score=2 AND b.name='old' LIMIT 20",
     "SELECT a.id, b.name FROM t a JOIN u b ON a.id=b.id WHERE a.score=2 AND b.name='new' LIMIT 20",2,
     {"stmt[0].where_literal[0]","stmt[0].where_literal[1]",NULL},
     {NULL,"new",NULL},{2,0,0}},
    {"update","UPDATE t SET name='old', score=1 WHERE id=7",
     "UPDATE t SET name='new', score=1 WHERE id=7",
     "UPDATE t SET name='new', score=2 WHERE id=8",3,
     {"stmt[0].assignment[0]","stmt[0].assignment[1]","stmt[0].where_literal[0]"},
     {"new",NULL,NULL},{0,2,8}},
    {"delete","DELETE FROM t WHERE id=1 AND name='old'",
     "DELETE FROM t WHERE id=2 AND name='old'",
     "DELETE FROM t WHERE id=2 AND name='new'",2,
     {"stmt[0].where_literal[0]","stmt[0].where_literal[1]",NULL},
     {NULL,"new",NULL},{2,0,0}}
};
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000.0+t.tv_nsec/1000000.0; }
static void check(sqlparser_status_t s,sqlparser_error_t *e) { if(s!=SQLPARSER_STATUS_OK) { fprintf(stderr,"status=%d %s\n",s,e->message); exit(1); } }
int main(int argc,char **argv)
{
    const fixture_t *f=NULL;
    size_t i,count;
    int repetitions=argc>3?atoi(argv[3]):201, warmups=argc>4?atoi(argv[4]):20, run;
    sqlparser_parse_options_t options;
    sqlparser_error_t error;
    sqlparser_handle_t *expected_handle=NULL;
    char *expected_view=NULL;
    const char *expected;
    if(argc<3 || repetitions<1 || warmups<0) return 2;
    for(i=0;i<sizeof(fixtures)/sizeof(fixtures[0]);i++) if(strcmp(fixtures[i].name,argv[1])==0) f=&fixtures[i];
    if(!f) return 2;
    count=(size_t)strtoul(argv[2],NULL,10);
    if(count!=0 && count!=1 && count!=f->few) return 2;
    expected=count==0?f->sql:(count==1?f->expected_one:f->expected_few);
    sqlparser_parse_options_default(&options); options.dialect=SQLPARSER_DIALECT_MYSQL;
    check(sqlparser_parse_with_options(expected,&options,&expected_handle,&error),&error);
    check(sqlparser_export_view_json(expected_handle,0,&expected_view,&error),&error);
    sqlparser_handle_destroy(expected_handle);
    puts("dialect,workload,patches,run,parse_ms,graph_ms,construct_ms,apply_ms,deparse_ms,total_ms");
    for(run=-warmups;run<repetitions;run++) {
        sqlparser_handle_t *h=NULL,*verified=NULL;
        sqlparser_query_graph_view_t graph;
        sqlparser_patch_t *patches=NULL;
        sqlparser_literal_value_t *values=NULL;
        sqlparser_patch_list_t list;
        char *output=NULL,*view=NULL;
        double t[6];
        t[0]=now_ms();
        check(sqlparser_parse_with_options(f->sql,&options,&h,&error),&error);
        t[1]=now_ms();
        check(sqlparser_statement_query_graph(h,0,&graph,&error),&error);
        t[2]=now_ms();
        if(count) {
            patches=calloc(count,sizeof(*patches)); values=calloc(count,sizeof(*values));
            if(!patches || !values) return 1;
        }
        for(i=0;i<count;i++) {
            patches[i].op=SQLPARSER_PATCH_REPLACE; patches[i].selector=f->selectors[i];
            values[i].kind=f->strings[i]?SQLPARSER_LITERAL_KIND_STRING:SQLPARSER_LITERAL_KIND_INTEGER;
            values[i].string_value=f->strings[i]; values[i].integer_value=f->integers[i];
            patches[i].literal=&values[i];
        }
        list.items=patches;list.count=count;
        t[3]=now_ms(); check(sqlparser_apply_patch(h,&list,&error),&error);
        t[4]=now_ms(); check(sqlparser_deparse(h,&output,&error),&error); t[5]=now_ms();
        /* Verify the actual retrieved SQL, independently of the mutated AST. */
        if(count==0 && strcmp(output,f->sql)!=0) return 1;
        check(sqlparser_parse_with_options(output,&options,&verified,&error),&error);
        check(sqlparser_export_view_json(verified,0,&view,&error),&error);
        if(strcmp(view,expected_view)!=0) { fprintf(stderr,"semantic output mismatch: %s\n",output); return 1; }
        sqlparser_string_free(view);sqlparser_string_free(output);
        sqlparser_handle_destroy(verified);sqlparser_handle_destroy(h);free(patches);free(values);
        if(run>=0) printf("mysql,%s,%zu,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",f->name,count,run,
            t[1]-t[0],t[2]-t[1],t[3]-t[2],t[4]-t[3],t[5]-t[4],t[5]-t[0]);
    }
    sqlparser_string_free(expected_view);return 0;
}
