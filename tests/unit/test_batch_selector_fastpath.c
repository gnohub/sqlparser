/* Successful batch-selector shortcut must preserve the generic parser's
 * diagnostics, numeric boundaries, and sequential patch error priority. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"
#include "sqlparser_test_failure.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s case %zu: %s\n",__FILE__,__LINE__,#x,number,error.message); exit(1); } } while (0)
static size_t number;
static void hex(const char *p) { if(p) for(;*p;p++) printf("%02x",(unsigned char)*p); }
static void exercise(const char *selector, int dump)
{
    const char *input="INSERT INTO t(a,b) VALUES ('old','keep'),('two','three')";
    unsigned typed, bad_first;
    for(typed=0;typed<2;typed++) for(bad_first=0;bad_first<2;bad_first++) {
        sqlparser_handle_t *h=NULL;
        sqlparser_parse_options_t options;
        sqlparser_query_graph_view_t graph;
        sqlparser_selector_t parsed;
        sqlparser_patch_t patches[2];
        sqlparser_literal_value_t literal;
        sqlparser_patch_list_t list;
        sqlparser_error_t error, parsed_error, saved;
        sqlparser_status_t status, parsed_status;
        char *out=NULL;
        memset(patches,0,sizeof(patches)); memset(&literal,0,sizeof(literal));
        sqlparser_parse_options_default(&options); options.dialect=SQLPARSER_DIALECT_MYSQL;
        CHECK(sqlparser_parse_with_options(input,&options,&h,&error)==SQLPARSER_STATUS_OK);
        CHECK(sqlparser_statement_query_graph(h,0,&graph,&error)==SQLPARSER_STATUS_OK);
        patches[0].op=patches[1].op=SQLPARSER_PATCH_REPLACE;
        patches[0].selector=bad_first?"stmt[0].insert_cell[0][999]":"stmt[0].insert_cell[0][0]";
        patches[1].selector=selector;
        if(typed) {literal.kind=SQLPARSER_LITERAL_KIND_STRING;literal.string_value="fresh";patches[0].literal=patches[1].literal=&literal;}
        else patches[0].sql=patches[1].sql="'fresh'";
        list.items=patches;list.count=2;
        parsed_status=sqlparser_selector_parse(selector,&parsed,&parsed_error);
        status=sqlparser_apply_patch(h,&list,&error);saved=error;
        if(!bad_first && selector && selector[0] && parsed_status!=SQLPARSER_STATUS_OK) {
            CHECK(status==parsed_status);CHECK(strcmp(saved.message,parsed_error.message)==0);
        }
        if(!bad_first && parsed_status==SQLPARSER_STATUS_OK && parsed.kind==SQLPARSER_SELECTOR_KIND_INSERT_CELL && parsed.statement_index==0 && parsed.row_index<2 && parsed.column_index<2) CHECK(status==SQLPARSER_STATUS_OK);
        if(bad_first) CHECK(status==SQLPARSER_STATUS_INVALID_ARGUMENT);
        if(status==SQLPARSER_STATUS_OK) CHECK(sqlparser_deparse(h,&out,&error)==SQLPARSER_STATUS_OK);
        else CHECK(sqlparser_test_failed_handle(h));
        CHECK(strcmp(input,"INSERT INTO t(a,b) VALUES ('old','keep'),('two','three')")==0);
        if(dump) {printf("%zu|%u|%u|%d|%d|%d|%d|",number,typed,bad_first,status,saved.cursor,saved.line,saved.column);hex(saved.message);printf("|");hex(out);puts("");}
        sqlparser_string_free(out);sqlparser_handle_destroy(h);
    }
    number++;
}
int main(int argc,char **argv)
{
    static const char *cases[]={NULL,"","s","stmt","stmt[","stmt[]","stmt[-1].insert_cell[0][1]","stmt[+0].insert_cell[0][1]","stmt[0].insert_cell[0][1]","stmt[000].insert_cell[0001][0001]","stmt[0].insert_cell[1][0]","stmt[0].insert_cell[2][0]","stmt[1].insert_cell[0][0]","stmt[0].insert_cell[0]","stmt[0].insert_cell[0][]","stmt[0].insert_cell[0][-1]","stmt[0].insert_cell[0][1]x","stmt[0].insert_cell[0][1] ","stmt[0].insert_cellx[0][1]","stmt[0].insert_cell[0][1][2]","stmt[0].insert_cell[0][1","stmt[0].insert_cell[0.0][1]","stmt[0].insert_cell[999999999999999999999999999][1]","stmt[0].insert_cell[0][999999999999999999999999999]","stmt[0].insert_cell[\xff][1]"};
    char text[180],digits[40];size_t i,j;int dump=argc==2 && strcmp(argv[1],"--dump")==0;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++) exercise(cases[i],dump);
    snprintf(digits,sizeof(digits),"%zu",SIZE_MAX);
    snprintf(text,sizeof(text),"stmt[%s].insert_cell[0][1]",digits);exercise(text,dump);
    snprintf(text,sizeof(text),"stmt[0].insert_cell[%s][1]",digits);exercise(text,dump);
    snprintf(text,sizeof(text),"stmt[0].insert_cell[0][%s]",digits);exercise(text,dump);
    /* Every truncated canonical prefix and every byte replacement around its
     * punctuation reaches either an exact success or the same generic error. */
    for(i=0;i<strlen("stmt[0].insert_cell[1][1]");i++) {
        strcpy(text,"stmt[0].insert_cell[1][1]");text[i]='\0';exercise(text,dump);
        for(j=1;j<256;j+=17) {strcpy(text,"stmt[0].insert_cell[1][1]");text[i]=(char)j;exercise(text,dump);}
    }
    if(!dump) printf("Batch selector success/fallback: %zu cases passed\n",number);
    return 0;
}
