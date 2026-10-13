#ifndef SQLPARSER_TEST_TREE_PARITY_H
#define SQLPARSER_TEST_TREE_PARITY_H
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sqlparser_wire_insert_internal.h"

/* Compare the authoritative representation without promoting the tested handle.
 * Compact cells are checked against an independently parsed protobuf tree. */
static int sqlparser_test_text_span(const char *source, size_t length, const char *value, int fold)
{
    size_t i;
    if (value == NULL) return length == 0U;
    if (strlen(value) != length) return 0;
    for (i = 0U; i < length; ++i)
        if (fold ? tolower((unsigned char)source[i]) != tolower((unsigned char)value[i]) :
            source[i] != value[i]) return 0;
    return 1;
}

static int sqlparser_test_compact_tree(const sqlparser_handle_t *h, const PgQuery__ParseResult *tree)
{
    const PgQueryCompactScalarInsert *c = h->compact_insert;
    const PgQuery__InsertStmt *insert;
    const PgQuery__SelectStmt *values;
    const char *names[3];
    size_t r, col;
    if (!c || !tree || tree->n_stmts != 1U || !tree->stmts[0] ||
        tree->stmts[0]->stmt->node_case != PG_QUERY__NODE__NODE_INSERT_STMT ||
        tree->stmts[0]->stmt_len != (int32_t)c->raw_length) return 0;
    insert = tree->stmts[0]->stmt->insert_stmt;
    if (!insert || !insert->relation || insert->n_cols != c->column_count ||
        !insert->select_stmt || insert->select_stmt->node_case != PG_QUERY__NODE__NODE_SELECT_STMT) return 0;
    values = insert->select_stmt->select_stmt;
    if (!values || values->n_values_lists != c->row_count) return 0;
    names[0] = insert->relation->catalogname; names[1] = insert->relation->schemaname; names[2] = insert->relation->relname;
    for (col = 0U; col < c->column_count + 3U; ++col) {
        const char *name = col < 3U ? names[col] : insert->cols[col-3U]->res_target->name;
        if (!sqlparser_test_text_span(c->sql+c->names[col].offset,c->names[col].length,name,0)) return 0;
    }
    for (r = 0U; r < c->row_count; ++r) {
        const PgQuery__List *row = values->values_lists[r]->list;
        if (!row || row->n_items != c->column_count) return 0;
        for (col = 0U; col < c->column_count; ++col) {
            const PgQueryCompactScalarCell *cell = &c->cells[r*c->column_count+col];
            const PgQuery__Node *node = row->items[col];
            const PgQuery__AConst *value;
            if (cell->kind == PG_QUERY_COMPACT_SCALAR_VALUE_FUNCTION) {
                static const struct { const char *name; PgQuery__SQLValueFunctionOp op; } functions[] = {
#define VALUE_FUNCTION(name,op) {name,PG_QUERY__SQLVALUE_FUNCTION_OP__SVFOP_##op}
                    VALUE_FUNCTION("current_timestamp",CURRENT_TIMESTAMP), VALUE_FUNCTION("current_date",CURRENT_DATE),
                    VALUE_FUNCTION("current_time",CURRENT_TIME), VALUE_FUNCTION("localtimestamp",LOCALTIMESTAMP),
                    VALUE_FUNCTION("localtime",LOCALTIME), VALUE_FUNCTION("current_role",CURRENT_ROLE),
                    VALUE_FUNCTION("current_user",CURRENT_USER), VALUE_FUNCTION("session_user",SESSION_USER),
                    VALUE_FUNCTION("user",USER), VALUE_FUNCTION("current_catalog",CURRENT_CATALOG),
                    VALUE_FUNCTION("current_schema",CURRENT_SCHEMA)
#undef VALUE_FUNCTION
                };
                size_t i;
                if (node->node_case != PG_QUERY__NODE__NODE_SQLVALUE_FUNCTION || !node->sqlvalue_function ||
                    node->sqlvalue_function->location != cell->location || node->sqlvalue_function->typmod != -1) return 0;
                for (i=0U;i<sizeof(functions)/sizeof(functions[0]);++i)
                    if (node->sqlvalue_function->op == functions[i].op &&
                        sqlparser_test_text_span(c->sql+cell->text_offset,cell->text_length,functions[i].name,1)) break;
                if (i==sizeof(functions)/sizeof(functions[0])) return 0;
                continue;
            }
            if (node->node_case != PG_QUERY__NODE__NODE_A_CONST || !(value=node->a_const) ||
                value->isnull || value->location != cell->location) return 0;
            if (cell->kind == PG_QUERY_COMPACT_SCALAR_INTEGER) {
                if (value->val_case != PG_QUERY__A__CONST__VAL_IVAL || !value->ival || value->ival->ival != cell->integer) return 0;
            } else {
                const char *text;
                if (cell->kind == PG_QUERY_COMPACT_SCALAR_STRING) {
                    if (value->val_case != PG_QUERY__A__CONST__VAL_SVAL || !value->sval) return 0;
                    text=value->sval->sval;
                } else {
                    if (cell->kind != PG_QUERY_COMPACT_SCALAR_FLOAT || value->val_case != PG_QUERY__A__CONST__VAL_FVAL || !value->fval) return 0;
                    text=value->fval->fval;
                }
                if (!sqlparser_test_text_span(c->sql+cell->text_offset,cell->text_length,text,0)) return 0;
            }
        }
    }
    return 1;
}

static int sqlparser_test_matches_wire(const sqlparser_handle_t *h, PgQueryProtobuf expected)
{
    int equal;
    if (h->compact_insert) {
        PgQuery__ParseResult *tree=pg_query__parse_result__unpack(NULL,expected.len,(const uint8_t*)expected.data);
        if (!tree) return 0;
        equal=sqlparser_test_compact_tree(h,tree);
        pg_query__parse_result__free_unpacked(tree,NULL);
        return equal;
    }
    if (h->representation == SQLPARSER_REPRESENTATION_OWNED_AST && h->ast) {
        size_t length=pg_query__parse_result__get_packed_size(h->ast);
        char *packed=malloc(length);
        if (!packed) return 0;
        equal=pg_query__parse_result__pack(h->ast,(uint8_t*)packed)==length &&
            length==expected.len && !memcmp(packed,expected.data,length);
        free(packed);return equal;
    }
    return h->parse_tree.len==expected.len && h->parse_tree.data &&
        !memcmp(h->parse_tree.data,expected.data,expected.len);
}
static int sqlparser_test_same_tree(const sqlparser_handle_t *a, const sqlparser_handle_t *b)
{
    if (b->representation == SQLPARSER_REPRESENTATION_OWNED_AST && b->ast) {
        PgQueryProtobuf wire = {0};
        int equal;
        wire.len = pg_query__parse_result__get_packed_size(b->ast);
        wire.data = malloc(wire.len);
        if (!wire.data) return 0;
        equal = pg_query__parse_result__pack(b->ast,(uint8_t *)wire.data) == wire.len &&
            sqlparser_test_matches_wire(a,wire);
        free(wire.data);return equal;
    }
    if (b->compact_insert) {
        PgQueryProtobufParseResult parsed = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
            b->parser_sql, PG_QUERY_PARSE_DEFAULT, NULL, NULL);
        int equal = !parsed.error && parsed.parse_tree.data &&
            sqlparser_test_matches_wire(a,parsed.parse_tree) && sqlparser_test_matches_wire(b,parsed.parse_tree);
        pg_query_free_protobuf_parse_result(parsed);return equal;
    }
    return sqlparser_test_matches_wire(a,b->parse_tree);
}

/* Private codec suites require a wire fixture regardless of public admission.
 * Public pipeline tests never call this adapter. */
static int sqlparser_test_force_wire(sqlparser_handle_t *h)
{
    PgQueryProtobufParseResult parsed;
    if (h->parse_tree.data != NULL) return 1;
    parsed = pg_query_parse_protobuf_opts_preserving_identifier_spelling_observed(
        h->parser_sql, PG_QUERY_PARSE_DEFAULT, NULL, NULL);
    if (parsed.error || !parsed.parse_tree.data) {
        pg_query_free_protobuf_parse_result(parsed);return 0;
    }
    if (!sqlparser_test_matches_wire(h,parsed.parse_tree)) {
        pg_query_free_protobuf_parse_result(parsed);return 0;
    }
    sqlparser_handle_clear_ast(h);
    free(h->compact_insert);h->compact_insert=NULL;
    h->parse_tree=parsed.parse_tree;parsed.parse_tree.data=NULL;parsed.parse_tree.len=0U;
    h->representation=SQLPARSER_REPRESENTATION_LEGACY_WIRE;
    pg_query_free_protobuf_parse_result(parsed);return 1;
}
#endif
