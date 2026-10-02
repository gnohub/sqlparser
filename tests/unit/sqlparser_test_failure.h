#ifndef SQLPARSER_TEST_FAILURE_H
#define SQLPARSER_TEST_FAILURE_H

#include <stdio.h>
#include "sqlparser/sqlparser.h"

/* A failed apply/deparse is terminal. Exercise rejection without inspecting
 * any borrowed storage invalidated by that operation; destruction is the
 * caller's only supported continuation. */
static int sqlparser_test_failed_handle(sqlparser_handle_t *handle)
{
    sqlparser_error_t error;
    sqlparser_query_graph_view_t graph;
    sqlparser_control_flow_view_t flow;
    sqlparser_patch_list_t empty = {NULL, 0U};
    char *sql = NULL;
    char *view = NULL;
    int valid =
        sqlparser_deparse(handle, &sql, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
        sql == NULL &&
        sqlparser_apply_patch(handle, &empty, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
        sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
        sqlparser_handle_control_flow(handle, &flow, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
        sqlparser_export_view_json(handle, 0, &view, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT &&
        view == NULL;
    sqlparser_string_free(sql);
    sqlparser_string_free(view);
    if (!valid) fprintf(stderr, "failed handle remained usable: %s\n", error.message);
    return valid;
}
#endif
