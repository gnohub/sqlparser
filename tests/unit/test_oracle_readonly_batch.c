/* Public API regression checks for Oracle batched string replacement. */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlparser/sqlparser.h"

static sqlparser_error_t err;
static const char *stage = "init";
static size_t emitted, cases_run;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL case=%s line=%d condition=%s error=%d:%s\n", stage, __LINE__, #x, (int)err.code, err.message); exit(1); } } while (0)
#define OK(x) do { sqlparser_status_t s_ = (x); REQUIRE(s_ == SQLPARSER_STATUS_OK); } while (0)
/* Never silently truncate an equivalence record. Whole default suite must fit. */
static void record_bytes(const void *p, size_t n)
{
    REQUIRE(n <= 1024U * 1024U - emitted);
    REQUIRE(fwrite(p, 1, n, stdout) == n);
    emitted += n;
}
static void recordf(const char *format, ...)
{
    char buffer[2048];
    va_list args;
    int n;
    va_start(args, format); n = vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    REQUIRE(n >= 0 && (size_t)n < sizeof(buffer));
    record_bytes(buffer, (size_t)n);
}
static void text(const char *s)
{
    if (s == NULL) { recordf("NULL;"); return; }
    recordf("%zu:", strlen(s)); record_bytes(s, strlen(s)); recordf(";");
}
static void selector(const sqlparser_selector_t *s)
{
    recordf("%d,%zu,%zu,%zu,%zu;", (int)s->kind, s->statement_index,
        s->item_index, s->row_index, s->column_index);
}
static void error_record(const char *what, sqlparser_status_t status, const sqlparser_error_t *e)
{
    recordf("%s status=%d", what, (int)status);
    if (e == NULL) { recordf(" error=NULL\n"); return; }
    recordf(" error=%d,%d,%d,%d,", (int)e->code, e->cursor, e->line, e->column);
    /* Every defined error field, including all message bytes; no struct padding. */
    for (size_t i = 0; i < sizeof(e->message); i++) recordf("%02x", (unsigned char)e->message[i]);
    recordf("\n");
}

/* Full public graph-field serializers, adapted from ordinary_sources/record_fields.inc.
 * Each record preserves declaration field order; labels are omitted for size. */
static void rec_sqlparser_index_span_t(const sqlparser_index_span_t *v) { recordf("sqlparser_index_span_t{");
recordf("%lld,", (long long)v->offset);
recordf("%lld,", (long long)v->count);
recordf("}\n");
}
static void rec_sqlparser_literal_view_t(const sqlparser_literal_view_t *v) { recordf("sqlparser_literal_view_t{");
recordf("%lld,", (long long)v->kind);
text(v->string_value);
text(v->float_value);
recordf("%lld,", (long long)v->integer_value);
recordf("%lld,", (long long)v->boolean_value);
recordf("%lld,", (long long)v->quoted_identifier);
recordf("}\n");
}
static void rec_sqlparser_graph_like_escape_t(const sqlparser_graph_like_escape_t *v) { recordf("sqlparser_graph_like_escape_t{");
recordf("%lld,", (long long)v->kind);
rec_sqlparser_literal_view_t(&v->literal);
text(v->bind);
recordf("%lld,", (long long)v->has_bind);
recordf("%lld,", (long long)v->bind_kind);
text(v->bind_sql);
recordf("%lld,", (long long)v->has_bind_sql);
recordf("%lld,", (long long)v->bind_position);
recordf("%lld,", (long long)v->has_bind_position);
recordf("}\n");
}
static void rec_sqlparser_target_path_entry_t(const sqlparser_target_path_entry_t *v) { recordf("sqlparser_target_path_entry_t{");
text(v->kind);
text(v->name);
recordf("%lld,", (long long)v->has_name);
recordf("%lld,", (long long)v->name_truncated);
recordf("%lld,", (long long)v->arg_index);
recordf("}\n");
}
static void rec_sqlparser_relation_view_t(const sqlparser_relation_view_t *v) { recordf("sqlparser_relation_view_t{");
text(v->database_name);
text(v->schema_name);
text(v->table_name);
text(v->alias_name);
text(v->link_name);
recordf("}\n");
}
static void rec_sqlparser_name_view_t(const sqlparser_name_view_t *v) { recordf("sqlparser_name_view_t{");
text(v->owner_type);
text(v->field_name);
text(v->value);
recordf("}\n");
}
static void rec_sqlparser_graph_block_t(const sqlparser_graph_block_t *v) { recordf("sqlparser_graph_block_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->kind);
rec_sqlparser_index_span_t(&v->relations);
rec_sqlparser_index_span_t(&v->targets);
rec_sqlparser_index_span_t(&v->predicates);
recordf("}\n");
}
static void rec_sqlparser_graph_relation_t(const sqlparser_graph_relation_t *v) { recordf("sqlparser_graph_relation_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->block_index);
recordf("%lld,", (long long)v->kind);
recordf("%lld,", (long long)v->quoted_identifier);
text(v->database_name);
text(v->schema_name);
text(v->object_name);
text(v->alias_name);
text(v->link_name);
recordf("%lld,", (long long)v->source_block_index);
recordf("%lld,", (long long)v->has_source_block);
recordf("%lld,", (long long)v->database_quoted_identifier);
recordf("%lld,", (long long)v->schema_quoted_identifier);
recordf("%lld,", (long long)v->link_quoted_identifier);
recordf("%lld,", (long long)v->ddl_role);
selector(&v->selector);
recordf("%lld,", (long long)v->has_selector);
recordf("%lld,", (long long)v->alias_quoted_identifier);
recordf("}\n");
}
static void rec_sqlparser_graph_target_t(const sqlparser_graph_target_t *v) { recordf("sqlparser_graph_target_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->block_index);
recordf("%lld,", (long long)v->ordinal);
recordf("%lld,", (long long)v->kind);
text(v->output_name);
recordf("%lld,", (long long)v->field_index);
recordf("%lld,", (long long)v->has_field);
recordf("%lld,", (long long)v->value_index);
recordf("%lld,", (long long)v->has_value);
rec_sqlparser_index_span_t(&v->star_relations);
recordf("%lld,", (long long)v->source_block_index);
recordf("%lld,", (long long)v->has_source_block);
selector(&v->selector);
recordf("%lld,", (long long)v->has_selector);
selector(&v->target_list_selector);
recordf("%lld,", (long long)v->has_target_list_selector);
recordf("%lld,", (long long)v->sink_value_index);
recordf("%lld,", (long long)v->has_sink_value);
recordf("%lld,", (long long)v->output_quoted_identifier);
recordf("}\n");
}
static void rec_sqlparser_graph_field_t(const sqlparser_graph_field_t *v) { recordf("sqlparser_graph_field_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->block_index);
recordf("%lld,", (long long)v->clause);
recordf("%lld,", (long long)v->relation_index);
recordf("%lld,", (long long)v->has_relation);
recordf("%lld,", (long long)v->quoted_identifier);
rec_sqlparser_index_span_t(&v->candidate_relations);
text(v->column_name);
recordf("%lld,", (long long)v->target_index);
recordf("%lld,", (long long)v->has_target);
for(size_t j=0;j<SQLPARSER_TARGET_PATH_CAPACITY;j++) rec_sqlparser_target_path_entry_t(&v->target_path[j]);
recordf("%lld,", (long long)v->target_path_count);
selector(&v->selector);
recordf("%lld,", (long long)v->has_selector);
recordf("%lld,", (long long)v->pseudo);
recordf("%lld,", (long long)v->prior);
recordf("}\n");
}
static void rec_sqlparser_graph_value_t(const sqlparser_graph_value_t *v) { recordf("sqlparser_graph_value_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->block_index);
recordf("%lld,", (long long)v->clause);
text(v->operator_name);
recordf("%lld,", (long long)v->operator_kind);
recordf("%lld,", (long long)v->field_index);
recordf("%lld,", (long long)v->has_field);
recordf("%lld,", (long long)v->source_field_index);
recordf("%lld,", (long long)v->has_source_field);
recordf("%lld,", (long long)v->field_match_kind);
recordf("%lld,", (long long)v->kind);
rec_sqlparser_literal_view_t(&v->literal);
text(v->bind);
recordf("%lld,", (long long)v->has_bind);
recordf("%lld,", (long long)v->bind_kind);
text(v->bind_sql);
recordf("%lld,", (long long)v->has_bind_sql);
recordf("%lld,", (long long)v->bind_position);
recordf("%lld,", (long long)v->has_bind_position);
selector(&v->selector);
recordf("%lld,", (long long)v->has_selector);
rec_sqlparser_graph_like_escape_t(&v->like_escape);
recordf("}\n");
}
static void rec_sqlparser_graph_expression_t(const sqlparser_graph_expression_t *v) { recordf("sqlparser_graph_expression_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->block_index);
recordf("%lld,", (long long)v->clause);
recordf("%lld,", (long long)v->kind);
text(v->sql);
text(v->name);
rec_sqlparser_index_span_t(&v->arguments);
selector(&v->selector);
recordf("%lld,", (long long)v->has_selector);
selector(&v->argument_list_selector);
recordf("%lld,", (long long)v->has_argument_list_selector);
recordf("}\n");
}
static void rec_sqlparser_graph_expression_argument_t(const sqlparser_graph_expression_argument_t *v) { recordf("sqlparser_graph_expression_argument_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->expression_index);
recordf("%lld,", (long long)v->ordinal);
recordf("%lld,", (long long)v->kind);
recordf("%lld,", (long long)v->value_index);
recordf("%lld,", (long long)v->has_value);
recordf("%lld,", (long long)v->field_index);
recordf("%lld,", (long long)v->has_field);
recordf("%lld,", (long long)v->nested_expression_index);
recordf("%lld,", (long long)v->has_nested_expression);
selector(&v->selector);
recordf("%lld,", (long long)v->has_selector);
recordf("}\n");
}
static void rec_sqlparser_graph_set_t(const sqlparser_graph_set_t *v) { recordf("sqlparser_graph_set_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->kind);
recordf("%lld,", (long long)v->result_block_index);
rec_sqlparser_index_span_t(&v->branch_blocks);
recordf("}\n");
}
static void rec_sqlparser_graph_predicate_t(const sqlparser_graph_predicate_t *v) { recordf("sqlparser_graph_predicate_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->block_index);
recordf("%lld,", (long long)v->clause);
recordf("%lld,", (long long)v->kind);
recordf("%lld,", (long long)v->bool_operator);
text(v->operator_name);
recordf("%lld,", (long long)v->operator_kind);
recordf("%lld,", (long long)v->left_field_index);
recordf("%lld,", (long long)v->has_left_field);
recordf("%lld,", (long long)v->right_field_index);
recordf("%lld,", (long long)v->has_right_field);
recordf("%lld,", (long long)v->value_index);
recordf("%lld,", (long long)v->has_value);
rec_sqlparser_index_span_t(&v->children);
recordf("%lld,", (long long)v->nocycle);
recordf("}\n");
}
static void rec_sqlparser_graph_dml_t(const sqlparser_graph_dml_t *v) { recordf("sqlparser_graph_dml_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->kind);
recordf("%lld,", (long long)v->insert_mode);
recordf("%lld,", (long long)v->target_relation_index);
recordf("%lld,", (long long)v->has_target_relation);
rec_sqlparser_index_span_t(&v->target_columns);
rec_sqlparser_index_span_t(&v->rows);
rec_sqlparser_index_span_t(&v->assignments);
rec_sqlparser_index_span_t(&v->delete_targets);
rec_sqlparser_index_span_t(&v->branches);
recordf("%lld,", (long long)v->source_block_index);
recordf("%lld,", (long long)v->has_source_block);
recordf("}\n");
}
static void rec_sqlparser_graph_dml_branch_t(const sqlparser_graph_dml_branch_t *v) { recordf("sqlparser_graph_dml_branch_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->dml_index);
recordf("%lld,", (long long)v->ordinal);
recordf("%lld,", (long long)v->branch_kind);
recordf("%lld,", (long long)v->target_relation_index);
recordf("%lld,", (long long)v->has_target_relation);
rec_sqlparser_index_span_t(&v->target_columns);
rec_sqlparser_index_span_t(&v->rows);
recordf("%lld,", (long long)v->condition_block_index);
recordf("%lld,", (long long)v->has_condition_block);
selector(&v->condition_selector);
recordf("%lld,", (long long)v->has_condition_selector);
selector(&v->delete_condition_selector);
recordf("%lld,", (long long)v->has_delete_condition_selector);
recordf("}\n");
}
static void rec_sqlparser_graph_dml_column_t(const sqlparser_graph_dml_column_t *v) { recordf("sqlparser_graph_dml_column_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->dml_index);
recordf("%lld,", (long long)v->ordinal);
text(v->column_name);
selector(&v->selector);
recordf("%lld,", (long long)v->has_selector);
recordf("%lld,", (long long)v->quoted_identifier);
recordf("}\n");
}
static void rec_sqlparser_graph_dml_cell_t(const sqlparser_graph_dml_cell_t *v) { recordf("sqlparser_graph_dml_cell_t{");
recordf("%lld,", (long long)v->index);
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->dml_index);
recordf("%lld,", (long long)v->row_index);
recordf("%lld,", (long long)v->column_ordinal);
recordf("%lld,", (long long)v->kind);
recordf("%lld,", (long long)v->source_target_index);
recordf("%lld,", (long long)v->has_source_target);
recordf("%lld,", (long long)v->source_field_index);
recordf("%lld,", (long long)v->has_source_field);
rec_sqlparser_literal_view_t(&v->literal);
text(v->bind);
recordf("%lld,", (long long)v->has_bind);
recordf("%lld,", (long long)v->bind_kind);
text(v->bind_sql);
recordf("%lld,", (long long)v->has_bind_sql);
recordf("%lld,", (long long)v->bind_position);
recordf("%lld,", (long long)v->has_bind_position);
selector(&v->selector);
recordf("%lld,", (long long)v->has_selector);
recordf("}\n");
}
static void rec_sqlparser_query_graph_view_t(const sqlparser_query_graph_view_t *v) { recordf("sqlparser_query_graph_view_t{");
recordf("%lld,", (long long)v->statement_index);
recordf("%lld,", (long long)v->generation);
recordf("%lld,", (long long)v->root_block_index);
recordf("%lld,", (long long)v->has_root_block);
recordf("%lld,", (long long)v->block_count);
recordf("%lld,", (long long)v->relation_count);
recordf("%lld,", (long long)v->target_count);
recordf("%lld,", (long long)v->field_count);
recordf("%lld,", (long long)v->value_count);
recordf("%lld,", (long long)v->set_count);
recordf("%lld,", (long long)v->predicate_count);
recordf("%lld,", (long long)v->has_dml);
recordf("%lld,", (long long)v->dml_branch_count);
recordf("}\n");
}


static void selector_check(const sqlparser_selector_t *s)
{
    char *formatted = NULL, *again = NULL;
    sqlparser_selector_t parsed;
    OK(sqlparser_selector_format(s, &formatted, &err));
    OK(sqlparser_selector_parse(formatted, &parsed, &err));
    OK(sqlparser_selector_format(&parsed, &again, &err));
    REQUIRE(strcmp(formatted, again) == 0);
    text(formatted);
    sqlparser_string_free(formatted); sqlparser_string_free(again);
}

/* All public graph fields, span mappings and usable selectors for these
 * ordinary INSERT ALL/FIRST inputs. No instrumentation or mutation. */
static void span_record(const sqlparser_query_graph_view_t *g, sqlparser_index_span_t s)
{
    for (size_t i=0; i<s.count; i++) {
        size_t index; OK(sqlparser_query_graph_span_index_at(g,s,i,&index,&err));
        recordf("span_index=%zu\n",index);
    }
}
static void capture_graph(sqlparser_handle_t *h)
{
    sqlparser_query_graph_view_t g;
    sqlparser_graph_dml_t d;
    size_t count;
    OK(sqlparser_statement_query_graph(h,0,&g,&err));
    rec_sqlparser_query_graph_view_t(&g);
    OK(sqlparser_query_graph_dml(&g,&d,&err));
    rec_sqlparser_graph_dml_t(&d);
    span_record(&g,d.target_columns); span_record(&g,d.rows);
    span_record(&g,d.assignments); span_record(&g,d.delete_targets);
    span_record(&g,d.branches);
#define LOOP(member,type,api) for(size_t i=0;i<g.member;i++){type v;OK(api(&g,i,&v,&err));rec_##type(&v);
#define SEL(name) if(v.has_##name)selector_check(&v.name)
    LOOP(block_count,sqlparser_graph_block_t,sqlparser_query_graph_block_at)
        span_record(&g,v.relations);span_record(&g,v.targets);span_record(&g,v.predicates);}
    LOOP(relation_count,sqlparser_graph_relation_t,sqlparser_query_graph_relation_at) SEL(selector);}
    LOOP(target_count,sqlparser_graph_target_t,sqlparser_query_graph_target_at)
        SEL(selector);SEL(target_list_selector);span_record(&g,v.star_relations);}
    LOOP(field_count,sqlparser_graph_field_t,sqlparser_query_graph_field_at)
        SEL(selector);span_record(&g,v.candidate_relations);}
    LOOP(value_count,sqlparser_graph_value_t,sqlparser_query_graph_value_at) SEL(selector);}
    LOOP(set_count,sqlparser_graph_set_t,sqlparser_query_graph_set_at)span_record(&g,v.branch_blocks);}
    LOOP(predicate_count,sqlparser_graph_predicate_t,sqlparser_query_graph_predicate_at)span_record(&g,v.children);}
#undef LOOP
    OK(sqlparser_query_graph_expression_count(&g,&count,&err));recordf("expressions=%zu\n",count);
    for(size_t i=0;i<count;i++) {
        sqlparser_graph_expression_t v;OK(sqlparser_query_graph_expression_at(&g,i,&v,&err));
        rec_sqlparser_graph_expression_t(&v);SEL(selector);SEL(argument_list_selector);span_record(&g,v.arguments);
    }
    OK(sqlparser_query_graph_expression_argument_count(&g,&count,&err));recordf("expression_arguments=%zu\n",count);
    for(size_t i=0;i<count;i++) {
        sqlparser_graph_expression_argument_t v;OK(sqlparser_query_graph_expression_argument_at(&g,i,&v,&err));
        rec_sqlparser_graph_expression_argument_t(&v);SEL(selector);
    }
#undef SEL
    for(size_t b=0;b<d.branches.count;b++) {
        size_t bi;sqlparser_graph_dml_branch_t r;
        OK(sqlparser_query_graph_span_index_at(&g,d.branches,b,&bi,&err));
        OK(sqlparser_query_graph_dml_branch_at(&g,bi,&r,&err));rec_sqlparser_graph_dml_branch_t(&r);
        if(r.has_condition_selector)selector_check(&r.condition_selector);
        if(r.has_delete_condition_selector)selector_check(&r.delete_condition_selector);
        span_record(&g,r.target_columns);span_record(&g,r.rows);
        for(size_t c=0;c<r.target_columns.count;c++) {
            size_t ci;sqlparser_graph_dml_column_t col;
            OK(sqlparser_query_graph_span_index_at(&g,r.target_columns,c,&ci,&err));
            OK(sqlparser_query_graph_dml_column_at(&g,ci,&col,&err));rec_sqlparser_graph_dml_column_t(&col);
            if(col.has_selector)selector_check(&col.selector);
        }
        for(size_t c=0;c<r.rows.count;c++) {
            size_t ci;sqlparser_graph_dml_cell_t cell;
            OK(sqlparser_query_graph_span_index_at(&g,r.rows,c,&ci,&err));
            OK(sqlparser_query_graph_dml_cell_at(&g,ci,&cell,&err));rec_sqlparser_graph_dml_cell_t(&cell);
            if(cell.has_selector)selector_check(&cell.selector);
        }
    }
}


static const char input[] = "INSERT ALL INTO T(A,B) VALUES ('a','b') INTO U(A,B) VALUES ('c','d') SELECT 1 FROM Dual";
static const char borrowed_input[] = "INSERT ALL INTO T(A,B,C,D,E) VALUES ('old','''raw payload''','typed ''Ω''','stmt[0].insert_cell[0][0]','sink') SELECT 1 FROM Dual";
static const char mixed_input[] = "INSERT ALL INTO T (A, B) VALUES ('Added', 'borrowed value') INTO U (A) VALUES ('second') SELECT 1 FROM Dual";
static const char nonstring_input[] = "INSERT ALL INTO T(A,B) VALUES ('a',7) INTO U(A,B) VALUES ('c','d') SELECT 1 FROM Dual";
static const char noncompact_input[] = "INSERT ALL INTO T(A,B) VALUES (q'[old-Ω]','b') INTO U(A,B) VALUES ('c','d') SELECT 1 FROM Dual";
static const char *selectors[] = {
    "stmt[0].insert_cell[0][0]", "stmt[0].insert_cell[0][1]",
    "stmt[0].insert_cell[1][0]", "stmt[0].insert_cell[1][1]"
};

typedef enum {
    RAW_ORDER, RAW_REVERSE, TYPED_VALUES, DUPLICATE, SAME, SAME_DUPLICATE, UNDO,
    EMPTY_BATCH, COLD, SECOND_APPLY, BORROWED_HIT, BORROWED_LATE_MISS,
    TYPED_SLASH, TYPED_CONTROL, RAW_COMPLEX, EXPRESSION, NONSTRING, NONCOMPACT,
    MIXED_OP, SOURCE_COPY, LEADING_ZERO, BAD_SELECTOR, LATE_BAD_SELECTOR,
    NULL_TYPED, BAD_THEN_NULL, NULL_THEN_BAD, BOTH_PAYLOADS,
    LIMIT_LARGE, LIMIT_LARGE_SMALL, LIMIT_SMALL_LARGE, LIMIT_LARGE_BAD,
    LIMIT_BAD_LARGE, LIMIT_NULL_LARGE, LIMIT_LARGE_NULL,
    ERROR_NULL_OUTPUT, SUCCESS_NULL_OUTPUT
} Mode;

typedef struct {
    const char *name;
    Mode mode;
    const char *bad_selector;
    int output_limit;
    int expected; /* 0 = OK, -1 = any non-OK, >0 = exact public status */
} Case;
static const Case cases[] = {
    {"raw_source_order", RAW_ORDER, NULL, 0, 0},
    {"raw_reverse_order", RAW_REVERSE, NULL, 0, 0},
    {"typed_quote_empty_utf8", TYPED_VALUES, NULL, 0, 0},
    {"duplicate_last_wins", DUPLICATE, NULL, 0, 0},
    {"same_value", SAME, NULL, 0, 0},
    {"duplicate_same_value", SAME_DUPLICATE, NULL, 0, 0},
    {"modify_then_undo", UNDO, NULL, 0, 0},
    {"empty_batch_keeps_view", EMPTY_BATCH, NULL, 0, 0},
    {"cold_graph_fallback", COLD, NULL, 0, 0},
    {"second_apply_fallback", SECOND_APPLY, NULL, 0, 0},
    {"borrowed_raw_typed_selector", BORROWED_HIT, NULL, 0, 0},
    {"borrowed_late_miss", BORROWED_LATE_MISS, NULL, 0, 0},
    {"typed_backslash_late_miss", TYPED_SLASH, NULL, 0, 0},
    {"typed_control_late_miss", TYPED_CONTROL, NULL, 0, 0},
    {"raw_q_quote_fallback", RAW_COMPLEX, NULL, 0, 0},
    {"raw_expression_fallback", EXPRESSION, NULL, 0, 0},
    {"nonstring_target_fallback", NONSTRING, NULL, 0, 0},
    {"noncompact_branch_fallback", NONCOMPACT, NULL, 0, 0},
    {"mixed_insert_column_fallback", MIXED_OP, NULL, 0, 0},
    {"source_selector_fallback", SOURCE_COPY, NULL, 0, 0},
    {"leading_zero_selector", LEADING_ZERO, NULL, 0, 0},
    {"invalid_trailing", BAD_SELECTOR, "stmt[0].insert_cell[0][1]tail", 0, -1},
    {"invalid_overflow", BAD_SELECTOR, "stmt[0].insert_cell[18446744073709551616][0]", 0, -1},
    {"invalid_branch", BAD_SELECTOR, "stmt[0].insert_cell[99][0]", 0, -1},
    {"invalid_column", BAD_SELECTOR, "stmt[0].insert_cell[0][99]", 0, -1},
    {"invalid_statement", BAD_SELECTOR, "stmt[1].insert_cell[0][0]", 0, -1},
    {"invalid_missing_bracket", BAD_SELECTOR, "stmt[0].insert_cell[0][1", 0, -1},
    {"invalid_null_selector", BAD_SELECTOR, NULL, 0, -1},
    {"invalid_empty_selector", BAD_SELECTOR, "", 0, -1},
    {"valid_then_invalid", LATE_BAD_SELECTOR, "stmt[0].insert_cell[0][1]tail", 0, -1},
    {"typed_null_is_invalid", NULL_TYPED, NULL, 0, SQLPARSER_STATUS_INVALID_ARGUMENT},
    {"invalid_then_typed_null", BAD_THEN_NULL, "stmt[0].insert_cell[0][1]tail", 0, -1},
    {"typed_null_then_invalid", NULL_THEN_BAD, "stmt[0].insert_cell[0][1]tail", 0, -1},
    {"raw_and_typed_invalid", BOTH_PAYLOADS, NULL, 0, -1},
    {"sql_limit_large", LIMIT_LARGE, NULL, 0, SQLPARSER_STATUS_RESOURCE_LIMIT},
    {"sql_limit_large_then_small", LIMIT_LARGE_SMALL, NULL, 0, SQLPARSER_STATUS_RESOURCE_LIMIT},
    {"sql_limit_small_then_large", LIMIT_SMALL_LARGE, NULL, 0, SQLPARSER_STATUS_RESOURCE_LIMIT},
    {"sql_limit_large_then_invalid", LIMIT_LARGE_BAD, "stmt[0].insert_cell[0][1]tail", 0, -1},
    {"sql_limit_invalid_then_large", LIMIT_BAD_LARGE, "stmt[0].insert_cell[0][1]tail", 0, -1},
    {"sql_limit_null_then_large", LIMIT_NULL_LARGE, NULL, 0, -1},
    {"sql_limit_large_then_null", LIMIT_LARGE_NULL, NULL, 0, -1},
    {"output_limit_large", LIMIT_LARGE, NULL, 1, SQLPARSER_STATUS_RESOURCE_LIMIT},
    {"output_limit_large_then_small", LIMIT_LARGE_SMALL, NULL, 1, SQLPARSER_STATUS_RESOURCE_LIMIT},
    {"output_limit_small_then_large", LIMIT_SMALL_LARGE, NULL, 1, SQLPARSER_STATUS_RESOURCE_LIMIT},
    {"output_limit_large_then_invalid", LIMIT_LARGE_BAD, "stmt[0].insert_cell[0][1]tail", 1, -1},
    {"output_limit_invalid_then_large", LIMIT_BAD_LARGE, "stmt[0].insert_cell[0][1]tail", 1, -1},
    {"output_limit_null_then_large", LIMIT_NULL_LARGE, NULL, 1, -1},
    {"output_limit_large_then_null", LIMIT_LARGE_NULL, NULL, 1, -1},
    {"out_error_null_failure", ERROR_NULL_OUTPUT, NULL, 0, SQLPARSER_STATUS_INVALID_ARGUMENT},
    {"out_error_null_success", SUCCESS_NULL_OUTPUT, NULL, 0, 0}
};

static sqlparser_graph_dml_cell_t graph_cell(const sqlparser_query_graph_view_t *g, size_t branch, size_t column)
{
    sqlparser_graph_dml_t d;
    sqlparser_graph_dml_branch_t b;
    sqlparser_graph_dml_cell_t c;
    size_t bi, ci;
    OK(sqlparser_query_graph_dml(g, &d, &err));
    OK(sqlparser_query_graph_span_index_at(g, d.branches, branch, &bi, &err));
    OK(sqlparser_query_graph_dml_branch_at(g, bi, &b, &err));
    OK(sqlparser_query_graph_span_index_at(g, b.rows, column, &ci, &err));
    OK(sqlparser_query_graph_dml_cell_at(g, ci, &c, &err));
    return c;
}
static sqlparser_patch_t raw(const char *s, const char *sql)
{
    sqlparser_patch_t p = {0}; p.op = SQLPARSER_PATCH_REPLACE; p.selector = s; p.sql = sql; return p;
}
static sqlparser_patch_t typed(const char *s, const sqlparser_literal_value_t *value)
{
    sqlparser_patch_t p = {0}; p.op = SQLPARSER_PATCH_REPLACE; p.selector = s; p.literal = value; return p;
}
static sqlparser_status_t apply_record(sqlparser_handle_t *h, sqlparser_patch_t *p, size_t count, int no_error)
{
    sqlparser_patch_list_t list = {p, count};
    sqlparser_status_t result;
    memset(&err, 0, sizeof(err));
    result = sqlparser_apply_patch(h, &list, no_error ? NULL : &err);
    error_record("apply", result, no_error ? NULL : &err);
    return result;
}
static void stale_or_live(const sqlparser_query_graph_view_t *g, int live)
{
    sqlparser_graph_dml_t d;
    sqlparser_status_t s;
    memset(&err, 0, sizeof(err)); s = sqlparser_query_graph_dml(g, &d, &err);
    error_record("old_view", s, &err);
    REQUIRE(s == (live ? SQLPARSER_STATUS_OK : SQLPARSER_STATUS_INVALID_ARGUMENT));
}
static void check_value(sqlparser_handle_t *h, size_t b, size_t c, const char *wanted)
{
    sqlparser_query_graph_view_t graph;
    sqlparser_graph_dml_cell_t cell;
    /* Oracle INSERT ALL is a source-query insert. The ordinary VALUES-only
     * insert_cell_literal getter is unsupported here; use its public graph. */
    OK(sqlparser_statement_query_graph(h, 0, &graph, &err));
    cell = graph_cell(&graph, b, c);
    REQUIRE(cell.literal.kind == SQLPARSER_LITERAL_KIND_STRING);
    REQUIRE(cell.literal.string_value != NULL && strcmp(cell.literal.string_value, wanted) == 0);
}
static char *snapshot(sqlparser_handle_t *h)
{
    char *sql = NULL, *json = NULL;
    sqlparser_query_graph_view_t g;
    sqlparser_graph_dml_t d;
    sqlparser_statement_kind_t kind;
    const char *node = NULL;
    OK(sqlparser_deparse(h, &sql, &err)); REQUIRE(sql != NULL);
    recordf("sql="); text(sql); recordf("\noriginal="); text(sqlparser_original_sql(h));
    recordf("\ndialect=%d statements=%zu\n", sqlparser_handle_dialect(h), sqlparser_statement_count(h));
    OK(sqlparser_statement_kind(h, 0, &kind, &err));
    OK(sqlparser_statement_node_name(h, 0, &node, &err));
    recordf("statement=%d,", (int)kind); text(node); recordf("\n");
    capture_graph(h);
    OK(sqlparser_statement_query_graph(h, 0, &g, &err));
    OK(sqlparser_query_graph_dml(&g, &d, &err));
    for (size_t b = 0; b < d.branches.count; b++) {
        size_t bi; sqlparser_graph_dml_branch_t branch;
        OK(sqlparser_query_graph_span_index_at(&g, d.branches, b, &bi, &err));
        OK(sqlparser_query_graph_dml_branch_at(&g, bi, &branch, &err));
        for (size_t c = 0; c < branch.rows.count; c++) {
            char *cell_sql = NULL;
            OK(sqlparser_insert_cell_sql(h, 0, b, c, &cell_sql, &err));
            recordf("cell_sql=%zu,%zu,", b, c); text(cell_sql); recordf("\n");
            sqlparser_string_free(cell_sql);
        }
    }
    OK(sqlparser_export_view_json(h, 0, &json, &err)); REQUIRE(json != NULL);
    recordf("json="); text(json); recordf("\n");
    sqlparser_string_free(json);
    return sql;
}

static void run_case(sqlparser_dialect_t dialect, const Case *t)
{
    sqlparser_handle_t *h = NULL;
    sqlparser_parse_options_t options;
    sqlparser_query_graph_view_t before, after;
    sqlparser_patch_t p[8] = {{0}};
    sqlparser_literal_value_t values[8] = {{0}};
    const char *source = input;
    char large[601], *saved_output = NULL;
    const char *wanted_cell = "changed";
    size_t wanted_branch = 0, wanted_column = 0, count = 1;
    int graph_first = t->mode != COLD;
    int no_error = t->mode == ERROR_NULL_OUTPUT || t->mode == SUCCESS_NULL_OUTPUT;
    int limited = t->mode >= LIMIT_LARGE && t->mode <= LIMIT_LARGE_NULL;
    sqlparser_status_t status;
    stage = t->name;
    recordf("CASE dialect=%d name=%s\n", (int)dialect, t->name);
    memset(large, 'L', sizeof(large) - 1); large[sizeof(large) - 1] = 0;
    for (size_t i = 0; i < 8; i++) values[i].kind = SQLPARSER_LITERAL_KIND_STRING;
    values[0].string_value = "changed";
    values[1].string_value = NULL;
    values[2].string_value = large;
    p[0] = typed(selectors[0], &values[0]);
    if (t->mode == BORROWED_HIT || t->mode == BORROWED_LATE_MISS) source = borrowed_input;
    if (t->mode == MIXED_OP) source = mixed_input;
    if (t->mode == NONSTRING) source = nonstring_input;
    if (t->mode == NONCOMPACT) source = noncompact_input;
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    if (limited) {
        if (t->output_limit) options.limits.max_output_bytes = 256;
        else options.limits.max_sql_bytes = 256;
    }
    memset(&err, 0, sizeof(err));
    OK(sqlparser_parse_with_options(source, &options, &h, &err)); REQUIRE(h != NULL);
    if (graph_first) {
        OK(sqlparser_statement_query_graph(h, 0, &before, &err));
        REQUIRE(before.generation == 0UL);
    }
    switch (t->mode) {
    case RAW_ORDER: case RAW_REVERSE:
        count = 4;
        for (size_t i = 0; i < count; i++) p[i] = raw(selectors[t->mode == RAW_ORDER ? i : 3-i], "'raw ASCII'");
        wanted_cell = "raw ASCII"; break;
    case TYPED_VALUES:
        values[0].string_value = "O'Brien"; values[3].string_value = ""; values[4].string_value = "Ω中文";
        p[1] = typed(selectors[1], &values[3]); p[2] = typed(selectors[3], &values[4]); count = 3;
        wanted_cell = "O'Brien"; break;
    case DUPLICATE:
        p[0] = raw(selectors[0], "'first'"); p[1] = raw(selectors[0], "'last'");
        p[2] = raw(selectors[3], "'tail'"); count = 3; wanted_cell = "last"; break;
    case SAME: p[0] = raw(selectors[0], "'a'"); wanted_cell = "a"; break;
    case SAME_DUPLICATE: p[0] = raw(selectors[0], "'a'"); p[1] = p[0]; count = 2; wanted_cell = "a"; break;
    case UNDO: p[0] = raw(selectors[0], "'temporary'"); p[1] = raw(selectors[0], "'a'"); count = 2; wanted_cell = "a"; break;
    case EMPTY_BATCH: count = 0; wanted_cell = "a"; break;
    case COLD: p[0] = raw(selectors[0], "'changed'"); break;
    case SECOND_APPLY:
        REQUIRE(apply_record(h, p, count, 0) == SQLPARSER_STATUS_OK);
        stale_or_live(&before, 0);
        OK(sqlparser_statement_query_graph(h, 0, &before, &err)); REQUIRE(before.generation == 1UL);
        values[0].string_value = "second"; wanted_cell = "second"; break;
    case BORROWED_HIT: case BORROWED_LATE_MISS: {
        sqlparser_graph_dml_cell_t r = graph_cell(&before, 0, 1);
        sqlparser_graph_dml_cell_t v = graph_cell(&before, 0, 2);
        sqlparser_graph_dml_cell_t s = graph_cell(&before, 0, 3);
        REQUIRE(r.literal.kind == SQLPARSER_LITERAL_KIND_STRING && !strcmp(r.literal.string_value, "'raw payload'"));
        REQUIRE(v.literal.kind == SQLPARSER_LITERAL_KIND_STRING && !strcmp(v.literal.string_value, "typed 'Ω'"));
        REQUIRE(s.literal.kind == SQLPARSER_LITERAL_KIND_STRING && !strcmp(s.literal.string_value, selectors[0]));
        values[0].string_value = v.literal.string_value;
        p[0] = raw("stmt[0].insert_cell[0][1]", "'overwritten raw source'");
        p[1] = raw("stmt[0].insert_cell[0][4]", r.literal.string_value);
        p[2] = raw("stmt[0].insert_cell[0][2]", "'overwritten typed source'");
        p[3] = typed(s.literal.string_value, &values[0]);
        p[4] = raw("stmt[0].insert_cell[0][3]", "'overwritten selector source'");
        count = 5; wanted_cell = "typed 'Ω'";
        if (t->mode == BORROWED_LATE_MISS) {
            values[3].string_value = "late\\backslash";
            p[5] = typed("stmt[0].insert_cell[0][2]", &values[3]); count = 6;
        }
        break;
    }
    case TYPED_SLASH: values[0].string_value = "late\\backslash"; wanted_cell = values[0].string_value; break;
    case TYPED_CONTROL: values[0].string_value = "line\nwith\ttab"; wanted_cell = values[0].string_value; break;
    case RAW_COMPLEX: p[0] = raw(selectors[0], "q'{new-Ω}'"); wanted_cell = "new-Ω"; break;
    case EXPRESSION: p[0] = raw(selectors[0], "upper('text')"); wanted_cell = NULL; break;
    case NONSTRING: p[0] = typed(selectors[1], &values[0]); wanted_column = 1; break;
    case NONCOMPACT: break;
    case MIXED_OP: {
        sqlparser_graph_dml_cell_t n = graph_cell(&before, 0, 0), v = graph_cell(&before, 0, 1);
        values[3].string_value = v.literal.string_value;
        p[1].op = SQLPARSER_PATCH_INSERT_COLUMN;
        p[1].selector = "stmt[0].insert_branch_columns[0]"; p[1].index = 1;
        p[1].name = n.literal.string_value; p[1].literal = &values[3]; count = 2;
        break;
    }
    case SOURCE_COPY:
        p[1].op = SQLPARSER_PATCH_REPLACE; p[1].selector = selectors[1];
        p[1].source_selector = selectors[0]; count = 2; break;
    case LEADING_ZERO: p[0].selector = "stmt[00].insert_cell[000][00]"; break;
    case BAD_SELECTOR: p[0].selector = t->bad_selector; break;
    case LATE_BAD_SELECTOR: p[1] = typed(t->bad_selector, &values[0]); count = 2; break;
    case NULL_TYPED: case ERROR_NULL_OUTPUT: p[0] = typed(selectors[0], &values[1]); break;
    case BAD_THEN_NULL: p[0].selector = t->bad_selector; p[1] = typed(selectors[1], &values[1]); count = 2; break;
    case NULL_THEN_BAD: p[0] = typed(selectors[0], &values[1]); p[1] = typed(t->bad_selector, &values[0]); count = 2; break;
    case BOTH_PAYLOADS: p[0].sql = "'raw'"; break;
    case LIMIT_LARGE: p[0] = typed(selectors[0], &values[2]); break;
    case LIMIT_LARGE_SMALL: p[0] = typed(selectors[0], &values[2]); p[1] = typed(selectors[0], &values[0]); count = 2; break;
    case LIMIT_SMALL_LARGE: p[1] = typed(selectors[0], &values[2]); count = 2; break;
    case LIMIT_LARGE_BAD: p[0] = typed(selectors[0], &values[2]); p[1] = typed(t->bad_selector, &values[0]); count = 2; break;
    case LIMIT_BAD_LARGE: p[0].selector = t->bad_selector; p[1] = typed(selectors[1], &values[2]); count = 2; break;
    case LIMIT_NULL_LARGE: p[0] = typed(selectors[0], &values[1]); p[1] = typed(selectors[1], &values[2]); count = 2; break;
    case LIMIT_LARGE_NULL: p[0] = typed(selectors[0], &values[2]); p[1] = typed(selectors[1], &values[1]); count = 2; break;
    case SUCCESS_NULL_OUTPUT: break;
    }
    status = apply_record(h, p, count, no_error);
    if (t->expected == -1) REQUIRE(status != SQLPARSER_STATUS_OK);
    else REQUIRE(status == (sqlparser_status_t)t->expected);
    if (status != SQLPARSER_STATUS_OK) {
        /* Failed apply is terminal. Do not use getters to observe a partial prefix. */
        sqlparser_handle_destroy(h); cases_run++; return;
    }
    if (graph_first) stale_or_live(&before, count == 0);
    OK(sqlparser_statement_query_graph(h, 0, &after, &err));
    REQUIRE(after.generation == (graph_first ? before.generation : 0UL) + (count ? 1UL : 0UL));
    if (wanted_cell != NULL) check_value(h, wanted_branch, wanted_column, wanted_cell);
    if (t->mode == TYPED_VALUES) { check_value(h, 0, 1, ""); check_value(h, 1, 1, "Ω中文"); }
    if (t->mode == BORROWED_HIT || t->mode == BORROWED_LATE_MISS) {
        check_value(h, 0, 1, "overwritten raw source"); check_value(h, 0, 4, "raw payload");
        check_value(h, 0, 2, t->mode == BORROWED_HIT ? "overwritten typed source" : "late\\backslash");
        check_value(h, 0, 3, "overwritten selector source");
    }
    if (t->mode == SOURCE_COPY) check_value(h, 0, 1, "changed");
    if (t->mode == RAW_ORDER || t->mode == RAW_REVERSE)
        for (size_t b = 0; b < 2; b++) for (size_t c = 0; c < 2; c++) check_value(h, b, c, "raw ASCII");
    if (t->mode == DUPLICATE) check_value(h, 1, 1, "tail");
    if (t->mode == MIXED_OP) { check_value(h, 0, 1, "borrowed value"); check_value(h, 0, 2, "borrowed value"); }
    saved_output = snapshot(h);
    {
        size_t n = strlen(saved_output);
        char *copy = malloc(n + 1); REQUIRE(copy != NULL); memcpy(copy, saved_output, n + 1);
        if (t->mode == SAME || t->mode == SAME_DUPLICATE || t->mode == UNDO || t->mode == EMPTY_BATCH)
            REQUIRE(strcmp(saved_output, input) == 0);
        sqlparser_handle_destroy(h);
        REQUIRE(memcmp(copy, saved_output, n + 1) == 0);
        free(copy); sqlparser_string_free(saved_output);
    }
    cases_run++;
}

int main(int argc, char **argv)
{
    const sqlparser_dialect_t dialects[] = {SQLPARSER_DIALECT_ORACLE, SQLPARSER_DIALECT_KINGBASE_ORACLE, SQLPARSER_DIALECT_VASTBASE_ORACLE};
    const char *only_case = NULL;
    int only_dialect = -1, list = 0;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--case=", 7)) only_case = argv[i] + 7;
        else if (!strncmp(argv[i], "--dialect=", 10)) only_dialect = atoi(argv[i] + 10);
        else if (!strcmp(argv[i], "--list")) list = 1;
        else { fprintf(stderr, "Usage: %s [--case=NAME] [--dialect=2|9|5] [--list]\n", argv[0]); return 2; }
    }
    for (size_t c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
        if (only_case && strcmp(only_case, cases[c].name)) continue;
        if (list) { recordf("%s expected=%d\n", cases[c].name, cases[c].expected); cases_run++; continue; }
        for (size_t d = 0; d < sizeof(dialects)/sizeof(dialects[0]); d++) {
            if (only_dialect >= 0 && only_dialect != (int)dialects[d]) continue;
            run_case(dialects[d], &cases[c]);
        }
    }
    REQUIRE(cases_run > 0);
    fprintf(stderr, "PASS ordinary_batch cases=%zu output_bytes=%zu no_fault_injection=1\n", cases_run, emitted);
    return 0;
}
