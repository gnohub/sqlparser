#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"
#include "sqlparser_test_failure.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static void hex(const char *s) { if(s) for(;*s;s++) printf("%02x",(unsigned char)*s); }
static size_t case_number;
static void exercise(const char *fragment, const char *known_value, int must_fail, int dump)
{
    const char *input="INSERT INTO t(a,b) VALUES ('old','keep')";
    unsigned limits, duplicate;
    for(limits=0;limits<4;limits++) for(duplicate=0;duplicate<2;duplicate++) {
        sqlparser_parse_options_t options;
        sqlparser_handle_t *h=NULL;
        sqlparser_query_graph_view_t before;
        sqlparser_graph_dml_t dml;
        sqlparser_patch_t patches[2]; sqlparser_patch_list_t list;
        sqlparser_error_t error, saved_error;
        sqlparser_status_t status, deparse_status;
        char *output=NULL;
        memset(patches,0,sizeof(patches));
        sqlparser_parse_options_default(&options); options.dialect=SQLPARSER_DIALECT_DAMENG;
        options.limits.max_statement_count=1;
        if(limits&1) options.limits.max_sql_bytes=strlen(input);
        if(limits&2) options.limits.max_output_bytes=strlen(input);
        CHECK(sqlparser_parse_with_options(input,&options,&h,&error)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_statement_query_graph(h,0,&before,&error)==SQLPARSER_STATUS_OK);
        patches[0].op=patches[1].op=SQLPARSER_PATCH_REPLACE;
        patches[0].selector="stmt[0].insert_cell[0][0]"; patches[0].sql=fragment;
        patches[1].selector=duplicate?patches[0].selector:"stmt[0].insert_cell[0][1]";
        patches[1].sql="'done'";
        list.items=patches; list.count=2;
        status=sqlparser_apply_patch(h,&list,&error); saved_error=error;
        if(must_fail) CHECK(status!=SQLPARSER_STATUS_OK);
        if(status!=SQLPARSER_STATUS_OK) {
            CHECK(sqlparser_query_graph_dml(&before,&dml,&error)==SQLPARSER_STATUS_INVALID_ARGUMENT);
            CHECK(sqlparser_test_failed_handle(h));
        } else if(known_value) {
            sqlparser_literal_view_t value;
            CHECK(sqlparser_insert_cell_literal(h,0,0,0,&value,&error)==SQLPARSER_STATUS_OK);
            CHECK(value.kind==SQLPARSER_LITERAL_KIND_STRING);
            CHECK(value.string_value && strcmp(value.string_value,duplicate?"done":known_value)==0);
        }
        deparse_status=sqlparser_deparse(h,&output,&error);
        if(dump) {
            printf("%zu|%u|%u|%d|%d|%d|%d|",case_number,limits,duplicate,status,
                saved_error.cursor,saved_error.line,saved_error.column);
            hex(saved_error.message); printf("|%d|",deparse_status); hex(output); puts("");
        }
        sqlparser_string_free(output); sqlparser_handle_destroy(h);
    }
    case_number++;
}
int main(int argc,char **argv)
{
    int dump=argc==2 && strcmp(argv[1],"--dump")==0;
    unsigned byte;
    char sql[4],value[2],large[260];
    static const char *fallback[]={
        "'can''t'","'back\\\\slash'","'line\nfeed'","'tab\there'","'雪'","'\177'","'\377'",
        "N'national'","E'escape'","$tag$dollar$tag$","('grouped')","'x'::text",
        "'x'||'y'","NULL","DEFAULT","1","'x'\n'y'","'a\0hidden'"
    };
    static const char *invalid[]={"", "'unterminated", "'x' trailing", "'x', 'y'", "'x'); SELECT 1; --"};
    exercise("''","",0,dump);
    for(byte=0x20;byte<=0x7e;byte++) if(byte!='\'' && byte!='\\') {
        sql[0]='\'';sql[1]=(char)byte;sql[2]='\'';sql[3]='\0';
        value[0]=(char)byte;value[1]='\0';exercise(sql,value,0,dump);
    }
    exercise("'; -- /*! text */'","; -- /*! text */",0,dump);
    for(byte=0;byte<sizeof(fallback)/sizeof(fallback[0]);byte++) exercise(fallback[byte],NULL,0,dump);
    for(byte=0;byte<sizeof(invalid)/sizeof(invalid[0]);byte++) exercise(invalid[byte],NULL,1,dump);
    large[0]='\'';memset(large+1,'a',256);large[257]='\'';large[258]='\0';
    exercise(large,NULL,0,dump);
    if(!dump) printf("Dameng ASCII string validation: %zu fragments, %zu limit/ordering cases passed\n",case_number,case_number*8);
    return 0;
}
