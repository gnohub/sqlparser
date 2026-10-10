/* Exact native node/text parity and allocator-derived capacity boundaries. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "pg_query.h"
#include "src/pg_query_internal.h"
#include "src/pg_query_observer.h"
#include "parser/scansup.h"
#include "src/pg_query_simple_insert.inc"
#include "src/pg_query_scalar_insert.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);abort(); } } while (0)
static size_t checks;

static void compare_cell(PgQueryScalarInsertKind kind, bool negative,
                         const char *text, size_t length, Size capacity)
{
    PgQueryScalarInsertCell cell = {0};
    A_Const *a,*b,copy_a,copy_b;
    char *at,*bt;
    Size request;
    cell.kind=kind;cell.negative=negative;cell.text.length=length;
    cell.location=20317;cell.integer=-103;
    request=pg_query_scalar_insert_inline_text_bytes(&cell,capacity);
    a=(A_Const *)pg_query_scalar_insert_make_cell(text,&cell,capacity);
    b=(A_Const *)pg_query_scalar_insert_make_cell(text,&cell,0U);
    CHECK(IsA(a,A_Const)&&IsA(b,A_Const));copy_a=*a;copy_b=*b;
    if(kind==PG_QUERY_SCALAR_INTEGER) {CHECK(!request);CHECK(!memcmp(&copy_a,&copy_b,sizeof(copy_a)));return;}
    if(kind==PG_QUERY_SCALAR_FLOAT) {at=a->val.fval.fval;bt=b->val.fval.fval;copy_a.val.fval.fval=copy_b.val.fval.fval=NULL;}
    else {at=a->val.sval.sval;bt=b->val.sval.sval;copy_a.val.sval.sval=copy_b.val.sval.sval=NULL;CHECK(a->val.sval.location==0);}
    CHECK(!memcmp(&copy_a,&copy_b,sizeof(copy_a)));CHECK(!strcmp(at,bt));
    CHECK(at[length+(kind==PG_QUERY_SCALAR_FLOAT&&negative)]=='\0');
    if(request) {CHECK(at==(char *)a+sizeof(*a));CHECK(GetMemoryChunkSpace(a)==GetMemoryChunkSpace(b));}
    else CHECK(at!=(char *)a+sizeof(*a));
    /* No separate free of at/bt: every object belongs to the parse context. */
}

int main(void)
{
    MemoryContext ctx=pg_query_enter_memory_context();
    MemoryContextData other={0};
    Size capacity=pg_query_allocset_same_class_capacity(ctx,sizeof(A_Const));
    Size before=ctx->mem_allocated;
    PgQueryScalarInsertCell cell={0};
    other.type=T_GenerationContext;
    CHECK(!pg_query_allocset_same_class_capacity(NULL,1U));
    CHECK(!pg_query_allocset_same_class_capacity(&other,1U));
    CHECK(!pg_query_allocset_same_class_capacity(ctx,0U));
    CHECK(!pg_query_allocset_same_class_capacity(ctx,SIZE_MAX));
    CHECK(!pg_query_allocset_same_class_capacity(ctx,MaxAllocSize));
    CHECK(capacity>=sizeof(A_Const)&&capacity<=MaxAllocSize);
    for(Size n=1U;n<=512U;++n) {
        Size c=pg_query_allocset_same_class_capacity(ctx,n);
        CHECK(c>=n&&c<=MaxAllocSize);
    }
    CHECK(ctx->mem_allocated==before);
    cell.kind=PG_QUERY_SCALAR_STRING;
    CHECK(!pg_query_scalar_insert_inline_text_bytes(&cell,0U));
    CHECK(!pg_query_scalar_insert_inline_text_bytes(&cell,sizeof(A_Const)));
    cell.text.length=SIZE_MAX;CHECK(!pg_query_scalar_insert_inline_text_bytes(&cell,capacity));
    cell.text.length=MaxAllocSize;CHECK(!pg_query_scalar_insert_inline_text_bytes(&cell,capacity));
    cell.text.length=0U;CHECK(!pg_query_scalar_insert_inline_text_bytes(&cell,SIZE_MAX));
    if(capacity>sizeof(A_Const)) {
        Size slack=capacity-sizeof(A_Const);
        char *text=malloc(slack+257U);CHECK(text);memset(text,'x',slack+256U);text[slack+256U]=0;
        for(size_t n=0;n<=slack+1U;++n) {
            cell.text.length=n;
            CHECK((pg_query_scalar_insert_inline_text_bytes(&cell,capacity)!=0U)==(n<slack));
            compare_cell(PG_QUERY_SCALAR_STRING,false,text,n,capacity);
            /* Numeric bytes are already certified before this layout helper. */
            compare_cell(PG_QUERY_SCALAR_FLOAT,false,text,n,capacity);
        }
        free(text);
    }
    compare_cell(PG_QUERY_SCALAR_FLOAT,true,"1.5",3U,capacity);
    compare_cell(PG_QUERY_SCALAR_STRING,false,"张三李四",strlen("张三李四"),capacity);
    compare_cell(PG_QUERY_SCALAR_INTEGER,false,"",0U,capacity);
    pg_query_exit_memory_context(ctx);pg_query_exit();
    printf("MySQL inline cell layout: %zu checks; actual AConst=%zu class=%zu; fields/text/NUL, class-boundary fallback, allocator guards passed\n",checks,sizeof(A_Const),capacity);
    return 0;
}
