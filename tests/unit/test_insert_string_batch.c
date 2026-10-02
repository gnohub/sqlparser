#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"
#include "sqlparser_test_failure.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s (dialect=%d, case=%zu): %s\n", __FILE__, __LINE__, #x, dialect, test, error.message); exit(1); } } while (0)
int main(int argc, char **argv)
{
    int dump = argc == 2 && strcmp(argv[1], "--dump") == 0;
    /* Compare batch semantics with the existing single-patch path, including
     * repeated selectors, a non-string transition and escaped SQL literals. */
    static const char *values[][4] = {
        {"'alpha'", "'before'", "''", "'can''t'"},
        {"'alpha'", "'before'", "'Ω'", "'back\\slash'"},
        {"'alpha'", "UPPER('mixed')", "UPPER('after')", "'last'"},
        {"'old-a'", "'old-b'", "'old-a'", "'old-b'"},
        {"'alpha'", "'before'", "'Ω'", "'last'"}
    };
    const char *sql="/*head*/ INSERT INTO t(id, a, b) VALUES (1, 'old-a', 'old-b'); /*tail*/";
    sqlparser_error_t error;
    int dialect, prebuild;
    size_t test;
    for (dialect=SQLPARSER_DIALECT_POSTGRESQL; dialect<=SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++) {
        for (prebuild=0; prebuild<2; prebuild++) {
        for (test=0; test<sizeof(values)/sizeof(values[0]); test++) {
            sqlparser_parse_options_t options;
            sqlparser_handle_t *batch=NULL, *single=NULL;
            sqlparser_patch_t patches[4];
            sqlparser_query_graph_view_t before_graph;
            sqlparser_patch_list_t list;
            char *a=NULL,*b=NULL,*av=NULL,*bv=NULL;
            size_t i;
            memset(&error,0,sizeof(error)); memset(patches,0,sizeof(patches));
            sqlparser_parse_options_default(&options); options.dialect=(sqlparser_dialect_t)dialect;
            CHECK(sqlparser_parse_with_options(sql,&options,&batch,&error)==SQLPARSER_STATUS_OK);
            CHECK(sqlparser_parse_with_options(sql,&options,&single,&error)==SQLPARSER_STATUS_OK);
            if (prebuild) CHECK(sqlparser_statement_query_graph(batch,0,&before_graph,&error)==SQLPARSER_STATUS_OK);
            for(i=0;i<4;i++) {
                patches[i].op=SQLPARSER_PATCH_REPLACE;
                patches[i].selector=(i&1) ? "stmt[0].insert_cell[0][2]" : "stmt[0].insert_cell[0][1]";
                patches[i].sql=values[test][i];
            }
            list.items=patches; list.count=4;
            CHECK(sqlparser_apply_patch(batch,&list,&error)==SQLPARSER_STATUS_OK);
            if (prebuild && !dump) {
                sqlparser_graph_dml_t previous_dml;
                sqlparser_status_t graph_status=sqlparser_query_graph_dml(&before_graph,&previous_dml,&error);
                CHECK(graph_status==SQLPARSER_STATUS_INVALID_ARGUMENT);
            }
            for(i=0;i<4;i++) { list.items=&patches[i]; list.count=1; CHECK(sqlparser_apply_patch(single,&list,&error)==SQLPARSER_STATUS_OK); }
            CHECK(sqlparser_deparse(batch,&a,&error)==SQLPARSER_STATUS_OK);
            CHECK(sqlparser_deparse(single,&b,&error)==SQLPARSER_STATUS_OK);
            /* Sequential AST fallback may normalize a space before the column
             * list. The string-only batch must preserve the original envelope. */
            if (dump) printf("dialect=%d case=%zu batch=[%s] sequential=[%s]\n",dialect,test,a,b);
            CHECK(strstr(b,"/*head*/") == NULL || strstr(a,"/*head*/") != NULL);
            CHECK(strstr(b,"/*tail*/") == NULL || strstr(a,"/*tail*/") != NULL);
            if (!dump && (test == 0 || test == 3 || test == 4)) {
                CHECK(strstr(a,"/*head*/") != NULL && strstr(a,"/*tail*/") != NULL);
                char expected[256];
                snprintf(expected,sizeof(expected),
                    "/*head*/ INSERT INTO t(id, a, b) VALUES (1, %s, %s); /*tail*/",
                    values[test][2],values[test][3]);
                CHECK(strcmp(a,expected)==0);
            }
            CHECK(sqlparser_export_view_json(batch,0,&av,&error)==SQLPARSER_STATUS_OK);
            CHECK(sqlparser_export_view_json(single,0,&bv,&error)==SQLPARSER_STATUS_OK);
            CHECK(strcmp(av,bv)==0);
            if (dialect==SQLPARSER_DIALECT_MYSQL && prebuild && test!=1 && test!=2) {
                sqlparser_handle_t *reference=NULL;
                sqlparser_patch_t follow[2];
                char *c=NULL,*d=NULL;
                CHECK(strcmp(sqlparser_original_sql(batch),a)==0);
                CHECK(sqlparser_parse_with_options(a,&options,&reference,&error)==SQLPARSER_STATUS_OK);
                memset(follow,0,sizeof(follow));
                follow[0].op=SQLPARSER_PATCH_REPLACE;
                follow[0].selector="stmt[0].insert_cell[0][2]";
                follow[0].source_selector="stmt[0].insert_cell[0][1]";
                follow[1].op=SQLPARSER_PATCH_INSERT_COLUMN;
                follow[1].selector="stmt[0].insert_columns";
                follow[1].index=3; follow[1].name="extra"; follow[1].default_sql="'after'";
                list.items=follow; list.count=2;
                CHECK(sqlparser_apply_patch(batch,&list,&error)==SQLPARSER_STATUS_OK);
                CHECK(sqlparser_apply_patch(reference,&list,&error)==SQLPARSER_STATUS_OK);
                CHECK(sqlparser_deparse(batch,&c,&error)==SQLPARSER_STATUS_OK);
                CHECK(sqlparser_deparse(reference,&d,&error)==SQLPARSER_STATUS_OK);
                CHECK(strcmp(c,d)==0);
                sqlparser_string_free(c); sqlparser_string_free(d); sqlparser_handle_destroy(reference);
            }
            sqlparser_string_free(av); sqlparser_string_free(bv);
            sqlparser_string_free(a); sqlparser_string_free(b);
            sqlparser_handle_destroy(batch); sqlparser_handle_destroy(single);
        }
        /* A malformed final string poisons the handle after preceding edits. */
        {
            sqlparser_parse_options_t options;
            sqlparser_handle_t *handle=NULL;
            sqlparser_query_graph_view_t graph;
            sqlparser_graph_dml_t dml;
            sqlparser_patch_t patches[2]; sqlparser_patch_list_t list;
            char *output=NULL;
            memset(patches,0,sizeof(patches));
            sqlparser_parse_options_default(&options); options.dialect=(sqlparser_dialect_t)dialect;
            CHECK(sqlparser_parse_with_options(sql,&options,&handle,&error)==SQLPARSER_STATUS_OK);
            if (prebuild) CHECK(sqlparser_statement_query_graph(handle,0,&graph,&error)==SQLPARSER_STATUS_OK);
            patches[0].op=patches[1].op=SQLPARSER_PATCH_REPLACE;
            patches[0].selector="stmt[0].insert_cell[0][1]"; patches[0].sql="'changed'";
            patches[1].selector="stmt[0].insert_cell[0][2]"; patches[1].sql="'unterminated";
            list.items=patches; list.count=2;
            CHECK(sqlparser_apply_patch(handle,&list,&error)!=SQLPARSER_STATUS_OK);
            if (prebuild) CHECK(sqlparser_query_graph_dml(&graph,&dml,&error)==SQLPARSER_STATUS_INVALID_ARGUMENT);
            CHECK(sqlparser_test_failed_handle(handle));
            sqlparser_handle_destroy(handle); handle=NULL;
            CHECK(sqlparser_parse_with_options(sql,&options,&handle,&error)==SQLPARSER_STATUS_OK);
            if (prebuild) CHECK(sqlparser_statement_query_graph(handle,0,&graph,&error)==SQLPARSER_STATUS_OK);
            /* A valid later replacement of the same span must not hide an
             * invalid earlier fragment during deduplication. */
            patches[0].sql="'unterminated";
            patches[1].selector=patches[0].selector; patches[1].sql="'valid'";
            CHECK(sqlparser_apply_patch(handle,&list,&error)!=SQLPARSER_STATUS_OK);
            if (prebuild) CHECK(sqlparser_query_graph_dml(&graph,&dml,&error)==SQLPARSER_STATUS_INVALID_ARGUMENT);
            CHECK(sqlparser_test_failed_handle(handle));
            sqlparser_string_free(output); sqlparser_handle_destroy(handle);
        }
        }
    }
    puts("INSERT SQL-string batch equivalence and terminal failure passed across 13 dialects");
    return 0;
}
