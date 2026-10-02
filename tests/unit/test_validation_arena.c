/* Validate temporary AST arena lifetime and allocation-failure cleanup. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"
#include "protobuf/pg_query.pb-c.h"
#include "src/pg_query_observer.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
#ifdef SQLPARSER_ARENA_ALLOC_COUNTS
static int custom_unpack;
static size_t calls, fail_at, live, tracked_count;
static void *tracked[1024];
void *__real_malloc(size_t);
void __real_free(void *);
PgQuery__ParseResult *__real_pg_query__parse_result__unpack(ProtobufCAllocator *,size_t,const uint8_t *);
PgQueryProtobufParseResult
__real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
    const char *, int, PgQueryProtobufObserver, void *);
PgQueryProtobufParseResult
__wrap_pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
    const char *sql, int options, PgQueryProtobufObserver observer, void *context)
{
    /* Model serializers that cannot expose a live C tree, e.g. protobuf C++.
     * This deliberately exercises the real arena fallback and its failures. */
    (void)observer;
    return __real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        sql, options, NULL, context);
}
PgQueryProtobufParseResult
__wrap_pg_query_parse_protobuf_opts_preserving_identifier_spelling_certified(
    const char *sql, int options, PgQueryProtobufObserver observer, void *context,
    size_t *statement_count, int *certified)
{
    (void)observer;
    *statement_count = 0U;
    *certified = 0;
    return __real_pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        sql, options, NULL, context);
}
void *__wrap_malloc(size_t size)
{
    void *p;
    if (custom_unpack && ++calls == fail_at) return NULL;
    p=__real_malloc(size);
    if (custom_unpack && p != NULL) {
        CHECK(tracked_count < sizeof(tracked)/sizeof(tracked[0]));
        tracked[tracked_count++]=p; live++;
    }
    return p;
}
void __wrap_free(void *p)
{
    size_t i;
    for(i=0;i<tracked_count;i++) if(p != NULL && tracked[i]==p) {
        tracked[i]=NULL; CHECK(live>0); live--; break;
    }
    __real_free(p);
}
PgQuery__ParseResult *__wrap_pg_query__parse_result__unpack(
    ProtobufCAllocator *allocator,size_t size,const uint8_t *bytes)
{
    int previous=custom_unpack;
    PgQuery__ParseResult *result;
    custom_unpack=allocator!=NULL;
    result=__real_pg_query__parse_result__unpack(allocator,size,bytes);
    custom_unpack=previous;
    return result;
}
static void reset(size_t fail)
{
    CHECK(live==0); calls=0; fail_at=fail; tracked_count=0;
}
#endif
static void exercise(const char *sql, int expect_arena)
{
    sqlparser_parse_options_t options;
    sqlparser_error_t error;
    sqlparser_handle_t *handle=NULL;
    sqlparser_query_graph_view_t graph;
    char *output=NULL;
    size_t i;
    (void)expect_arena;
    sqlparser_parse_options_default(&options); options.dialect=SQLPARSER_DIALECT_MYSQL;
#ifdef SQLPARSER_ARENA_ALLOC_COUNTS
    reset(0);
#endif
    CHECK(sqlparser_parse_with_options(sql,&options,&handle,&error)==SQLPARSER_STATUS_OK);
#ifdef SQLPARSER_ARENA_ALLOC_COUNTS
    {
        size_t count=calls, failure;
        CHECK((count>0)==expect_arena && live==0); /* No validation-arena storage escapes parse. */
        sqlparser_handle_destroy(handle); handle=NULL;
        for(failure=1;failure<=count;failure++) {
            reset(failure);
            CHECK(sqlparser_parse_with_options(sql,&options,&handle,&error)!=SQLPARSER_STATUS_OK);
            CHECK(handle==NULL && live==0);
        }
        reset(0);
        CHECK(sqlparser_parse_with_options(sql,&options,&handle,&error)==SQLPARSER_STATUS_OK);
    }
#endif
    CHECK(sqlparser_statement_query_graph(handle,0,&graph,&error)==SQLPARSER_STATUS_OK);
    CHECK(sqlparser_deparse(handle,&output,&error)==SQLPARSER_STATUS_OK);
    CHECK(strcmp(output,sql)==0);
    sqlparser_string_free(output); sqlparser_handle_destroy(handle);
    for(i=0;i<20;i++) {
        handle=NULL;
#ifdef SQLPARSER_ARENA_ALLOC_COUNTS
        reset(0);
#endif
        CHECK(sqlparser_parse_with_options(sql,&options,&handle,&error)==SQLPARSER_STATUS_OK);
        sqlparser_handle_destroy(handle);
#ifdef SQLPARSER_ARENA_ALLOC_COUNTS
        CHECK(live==0);
#endif
    }
}
int main(void)
{
    char *large=malloc(71000), *rows=malloc(50000), *p;
    size_t i;
    CHECK(large && rows);
    strcpy(large,"SELECT '"); memset(large+8,'x',70000); strcpy(large+70008,"'");
    exercise("SELECT 1",0); exercise(large,1);
    p=rows+sprintf(rows,"INSERT INTO t(id,v) VALUES ");
    for(i=0;i<500;i++) p+=sprintf(p,"%s(%zu,'value-%zu')",i?",":"",i,i);
    exercise(rows,1);
    free(large); free(rows);
#ifdef SQLPARSER_ARENA_ALLOC_COUNTS
    puts("validation arena parse/read/destroy and allocation-failure checks passed");
#else
    puts("validation arena parse/read/destroy checks passed; allocation wrapping unavailable");
#endif
    return 0;
}
