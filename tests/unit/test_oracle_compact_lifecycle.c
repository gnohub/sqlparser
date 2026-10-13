#define COMPACT_BACKEND 1
/* Helpers for compact INSERT storage and lifecycle regression checks. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <locale.h>
#include "sqlparser/sqlparser.h"
#include "sqlparser_internal.h"
#include "sqlparser_dialect_multi_insert_internal.h"
#include "sqlparser_dialect_oracle_internal.h"
static sqlparser_error_t err;
static const char *stage="init";
#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FAIL %s line %d: %s; %s\n",stage,__LINE__,#x,err.message);exit(1);} } while(0)
#define OK(x) do { sqlparser_status_t call_status=(x); REQUIRE(call_status==SQLPARSER_STATUS_OK); } while(0)
static void text(const char *s){ if(!s){puts("NULL");return;} printf("%zu:",strlen(s));fwrite(s,1,strlen(s),stdout);putchar('\n'); }
static void selector(const sqlparser_selector_t *s){printf("%d,%zu,%zu,%zu,%zu\n",s->kind,s->statement_index,s->item_index,s->row_index,s->column_index);}
/* Generated only from matching unchanged public API declarations. No raw padding/pointers. */
static void rec_sqlparser_index_span_t(const sqlparser_index_span_t *v) { puts("sqlparser_index_span_t");
fputs("offset=", stdout);
printf("%lld\n", (long long)v->offset);
fputs("count=", stdout);
printf("%lld\n", (long long)v->count);
}
static void rec_sqlparser_literal_view_t(const sqlparser_literal_view_t *v) { puts("sqlparser_literal_view_t");
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("string_value=", stdout);
text(v->string_value);
fputs("float_value=", stdout);
text(v->float_value);
fputs("integer_value=", stdout);
printf("%lld\n", (long long)v->integer_value);
fputs("boolean_value=", stdout);
printf("%lld\n", (long long)v->boolean_value);
fputs("quoted_identifier=", stdout);
printf("%lld\n", (long long)v->quoted_identifier);
}
static void rec_sqlparser_graph_like_escape_t(const sqlparser_graph_like_escape_t *v) { puts("sqlparser_graph_like_escape_t");
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("literal=", stdout);
rec_sqlparser_literal_view_t(&v->literal);
fputs("bind=", stdout);
text(v->bind);
fputs("has_bind=", stdout);
printf("%lld\n", (long long)v->has_bind);
fputs("bind_kind=", stdout);
printf("%lld\n", (long long)v->bind_kind);
fputs("bind_sql=", stdout);
text(v->bind_sql);
fputs("has_bind_sql=", stdout);
printf("%lld\n", (long long)v->has_bind_sql);
fputs("bind_position=", stdout);
printf("%lld\n", (long long)v->bind_position);
fputs("has_bind_position=", stdout);
printf("%lld\n", (long long)v->has_bind_position);
}
static void rec_sqlparser_target_path_entry_t(const sqlparser_target_path_entry_t *v) { puts("sqlparser_target_path_entry_t");
fputs("kind=", stdout);
text(v->kind);
fputs("name=", stdout);
text(v->name);
fputs("has_name=", stdout);
printf("%lld\n", (long long)v->has_name);
fputs("name_truncated=", stdout);
printf("%lld\n", (long long)v->name_truncated);
fputs("arg_index=", stdout);
printf("%lld\n", (long long)v->arg_index);
}
static void rec_sqlparser_relation_view_t(const sqlparser_relation_view_t *v) { puts("sqlparser_relation_view_t");
fputs("database_name=", stdout);
text(v->database_name);
fputs("schema_name=", stdout);
text(v->schema_name);
fputs("table_name=", stdout);
text(v->table_name);
fputs("alias_name=", stdout);
text(v->alias_name);
fputs("link_name=", stdout);
text(v->link_name);
}
static void rec_sqlparser_name_view_t(const sqlparser_name_view_t *v) { puts("sqlparser_name_view_t");
fputs("owner_type=", stdout);
text(v->owner_type);
fputs("field_name=", stdout);
text(v->field_name);
fputs("value=", stdout);
text(v->value);
}
static void rec_sqlparser_graph_block_t(const sqlparser_graph_block_t *v) { puts("sqlparser_graph_block_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("relations=", stdout);
rec_sqlparser_index_span_t(&v->relations);
fputs("targets=", stdout);
rec_sqlparser_index_span_t(&v->targets);
fputs("predicates=", stdout);
rec_sqlparser_index_span_t(&v->predicates);
}
static void rec_sqlparser_graph_relation_t(const sqlparser_graph_relation_t *v) { puts("sqlparser_graph_relation_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("block_index=", stdout);
printf("%lld\n", (long long)v->block_index);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("quoted_identifier=", stdout);
printf("%lld\n", (long long)v->quoted_identifier);
fputs("database_name=", stdout);
text(v->database_name);
fputs("schema_name=", stdout);
text(v->schema_name);
fputs("object_name=", stdout);
text(v->object_name);
fputs("alias_name=", stdout);
text(v->alias_name);
fputs("link_name=", stdout);
text(v->link_name);
fputs("source_block_index=", stdout);
printf("%lld\n", (long long)v->source_block_index);
fputs("has_source_block=", stdout);
printf("%lld\n", (long long)v->has_source_block);
fputs("database_quoted_identifier=", stdout);
printf("%lld\n", (long long)v->database_quoted_identifier);
fputs("schema_quoted_identifier=", stdout);
printf("%lld\n", (long long)v->schema_quoted_identifier);
fputs("link_quoted_identifier=", stdout);
printf("%lld\n", (long long)v->link_quoted_identifier);
fputs("ddl_role=", stdout);
printf("%lld\n", (long long)v->ddl_role);
fputs("selector=", stdout);
selector(&v->selector);
fputs("has_selector=", stdout);
printf("%lld\n", (long long)v->has_selector);
fputs("alias_quoted_identifier=", stdout);
printf("%lld\n", (long long)v->alias_quoted_identifier);
}
static void rec_sqlparser_graph_target_t(const sqlparser_graph_target_t *v) { puts("sqlparser_graph_target_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("block_index=", stdout);
printf("%lld\n", (long long)v->block_index);
fputs("ordinal=", stdout);
printf("%lld\n", (long long)v->ordinal);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("output_name=", stdout);
text(v->output_name);
fputs("field_index=", stdout);
printf("%lld\n", (long long)v->field_index);
fputs("has_field=", stdout);
printf("%lld\n", (long long)v->has_field);
fputs("value_index=", stdout);
printf("%lld\n", (long long)v->value_index);
fputs("has_value=", stdout);
printf("%lld\n", (long long)v->has_value);
fputs("star_relations=", stdout);
rec_sqlparser_index_span_t(&v->star_relations);
fputs("source_block_index=", stdout);
printf("%lld\n", (long long)v->source_block_index);
fputs("has_source_block=", stdout);
printf("%lld\n", (long long)v->has_source_block);
fputs("selector=", stdout);
selector(&v->selector);
fputs("has_selector=", stdout);
printf("%lld\n", (long long)v->has_selector);
fputs("target_list_selector=", stdout);
selector(&v->target_list_selector);
fputs("has_target_list_selector=", stdout);
printf("%lld\n", (long long)v->has_target_list_selector);
fputs("sink_value_index=", stdout);
printf("%lld\n", (long long)v->sink_value_index);
fputs("has_sink_value=", stdout);
printf("%lld\n", (long long)v->has_sink_value);
fputs("output_quoted_identifier=", stdout);
printf("%lld\n", (long long)v->output_quoted_identifier);
}
static void rec_sqlparser_graph_field_t(const sqlparser_graph_field_t *v) { puts("sqlparser_graph_field_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("block_index=", stdout);
printf("%lld\n", (long long)v->block_index);
fputs("clause=", stdout);
printf("%lld\n", (long long)v->clause);
fputs("relation_index=", stdout);
printf("%lld\n", (long long)v->relation_index);
fputs("has_relation=", stdout);
printf("%lld\n", (long long)v->has_relation);
fputs("quoted_identifier=", stdout);
printf("%lld\n", (long long)v->quoted_identifier);
fputs("candidate_relations=", stdout);
rec_sqlparser_index_span_t(&v->candidate_relations);
fputs("column_name=", stdout);
text(v->column_name);
fputs("target_index=", stdout);
printf("%lld\n", (long long)v->target_index);
fputs("has_target=", stdout);
printf("%lld\n", (long long)v->has_target);
fputs("target_path=", stdout);
for(size_t j=0;j<SQLPARSER_TARGET_PATH_CAPACITY;j++) rec_sqlparser_target_path_entry_t(&v->target_path[j]);
fputs("target_path_count=", stdout);
printf("%lld\n", (long long)v->target_path_count);
fputs("selector=", stdout);
selector(&v->selector);
fputs("has_selector=", stdout);
printf("%lld\n", (long long)v->has_selector);
fputs("pseudo=", stdout);
printf("%lld\n", (long long)v->pseudo);
fputs("prior=", stdout);
printf("%lld\n", (long long)v->prior);
}
static void rec_sqlparser_graph_value_t(const sqlparser_graph_value_t *v) { puts("sqlparser_graph_value_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("block_index=", stdout);
printf("%lld\n", (long long)v->block_index);
fputs("clause=", stdout);
printf("%lld\n", (long long)v->clause);
fputs("operator_name=", stdout);
text(v->operator_name);
fputs("operator_kind=", stdout);
printf("%lld\n", (long long)v->operator_kind);
fputs("field_index=", stdout);
printf("%lld\n", (long long)v->field_index);
fputs("has_field=", stdout);
printf("%lld\n", (long long)v->has_field);
fputs("source_field_index=", stdout);
printf("%lld\n", (long long)v->source_field_index);
fputs("has_source_field=", stdout);
printf("%lld\n", (long long)v->has_source_field);
fputs("field_match_kind=", stdout);
printf("%lld\n", (long long)v->field_match_kind);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("literal=", stdout);
rec_sqlparser_literal_view_t(&v->literal);
fputs("bind=", stdout);
text(v->bind);
fputs("has_bind=", stdout);
printf("%lld\n", (long long)v->has_bind);
fputs("bind_kind=", stdout);
printf("%lld\n", (long long)v->bind_kind);
fputs("bind_sql=", stdout);
text(v->bind_sql);
fputs("has_bind_sql=", stdout);
printf("%lld\n", (long long)v->has_bind_sql);
fputs("bind_position=", stdout);
printf("%lld\n", (long long)v->bind_position);
fputs("has_bind_position=", stdout);
printf("%lld\n", (long long)v->has_bind_position);
fputs("selector=", stdout);
selector(&v->selector);
fputs("has_selector=", stdout);
printf("%lld\n", (long long)v->has_selector);
fputs("like_escape=", stdout);
rec_sqlparser_graph_like_escape_t(&v->like_escape);
}
static void rec_sqlparser_graph_expression_t(const sqlparser_graph_expression_t *v) { puts("sqlparser_graph_expression_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("block_index=", stdout);
printf("%lld\n", (long long)v->block_index);
fputs("clause=", stdout);
printf("%lld\n", (long long)v->clause);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("sql=", stdout);
text(v->sql);
fputs("name=", stdout);
text(v->name);
fputs("arguments=", stdout);
rec_sqlparser_index_span_t(&v->arguments);
fputs("selector=", stdout);
selector(&v->selector);
fputs("has_selector=", stdout);
printf("%lld\n", (long long)v->has_selector);
fputs("argument_list_selector=", stdout);
selector(&v->argument_list_selector);
fputs("has_argument_list_selector=", stdout);
printf("%lld\n", (long long)v->has_argument_list_selector);
}
static void rec_sqlparser_graph_expression_argument_t(const sqlparser_graph_expression_argument_t *v) { puts("sqlparser_graph_expression_argument_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("expression_index=", stdout);
printf("%lld\n", (long long)v->expression_index);
fputs("ordinal=", stdout);
printf("%lld\n", (long long)v->ordinal);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("value_index=", stdout);
printf("%lld\n", (long long)v->value_index);
fputs("has_value=", stdout);
printf("%lld\n", (long long)v->has_value);
fputs("field_index=", stdout);
printf("%lld\n", (long long)v->field_index);
fputs("has_field=", stdout);
printf("%lld\n", (long long)v->has_field);
fputs("nested_expression_index=", stdout);
printf("%lld\n", (long long)v->nested_expression_index);
fputs("has_nested_expression=", stdout);
printf("%lld\n", (long long)v->has_nested_expression);
fputs("selector=", stdout);
selector(&v->selector);
fputs("has_selector=", stdout);
printf("%lld\n", (long long)v->has_selector);
}
static void rec_sqlparser_graph_set_t(const sqlparser_graph_set_t *v) { puts("sqlparser_graph_set_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("result_block_index=", stdout);
printf("%lld\n", (long long)v->result_block_index);
fputs("branch_blocks=", stdout);
rec_sqlparser_index_span_t(&v->branch_blocks);
}
static void rec_sqlparser_graph_predicate_t(const sqlparser_graph_predicate_t *v) { puts("sqlparser_graph_predicate_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("block_index=", stdout);
printf("%lld\n", (long long)v->block_index);
fputs("clause=", stdout);
printf("%lld\n", (long long)v->clause);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("bool_operator=", stdout);
printf("%lld\n", (long long)v->bool_operator);
fputs("operator_name=", stdout);
text(v->operator_name);
fputs("operator_kind=", stdout);
printf("%lld\n", (long long)v->operator_kind);
fputs("left_field_index=", stdout);
printf("%lld\n", (long long)v->left_field_index);
fputs("has_left_field=", stdout);
printf("%lld\n", (long long)v->has_left_field);
fputs("right_field_index=", stdout);
printf("%lld\n", (long long)v->right_field_index);
fputs("has_right_field=", stdout);
printf("%lld\n", (long long)v->has_right_field);
fputs("value_index=", stdout);
printf("%lld\n", (long long)v->value_index);
fputs("has_value=", stdout);
printf("%lld\n", (long long)v->has_value);
fputs("children=", stdout);
rec_sqlparser_index_span_t(&v->children);
fputs("nocycle=", stdout);
printf("%lld\n", (long long)v->nocycle);
}
static void rec_sqlparser_graph_dml_t(const sqlparser_graph_dml_t *v) { puts("sqlparser_graph_dml_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("insert_mode=", stdout);
printf("%lld\n", (long long)v->insert_mode);
fputs("target_relation_index=", stdout);
printf("%lld\n", (long long)v->target_relation_index);
fputs("has_target_relation=", stdout);
printf("%lld\n", (long long)v->has_target_relation);
fputs("target_columns=", stdout);
rec_sqlparser_index_span_t(&v->target_columns);
fputs("rows=", stdout);
rec_sqlparser_index_span_t(&v->rows);
fputs("assignments=", stdout);
rec_sqlparser_index_span_t(&v->assignments);
fputs("delete_targets=", stdout);
rec_sqlparser_index_span_t(&v->delete_targets);
fputs("branches=", stdout);
rec_sqlparser_index_span_t(&v->branches);
fputs("source_block_index=", stdout);
printf("%lld\n", (long long)v->source_block_index);
fputs("has_source_block=", stdout);
printf("%lld\n", (long long)v->has_source_block);
}
static void rec_sqlparser_graph_dml_branch_t(const sqlparser_graph_dml_branch_t *v) { puts("sqlparser_graph_dml_branch_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("dml_index=", stdout);
printf("%lld\n", (long long)v->dml_index);
fputs("ordinal=", stdout);
printf("%lld\n", (long long)v->ordinal);
fputs("branch_kind=", stdout);
printf("%lld\n", (long long)v->branch_kind);
fputs("target_relation_index=", stdout);
printf("%lld\n", (long long)v->target_relation_index);
fputs("has_target_relation=", stdout);
printf("%lld\n", (long long)v->has_target_relation);
fputs("target_columns=", stdout);
rec_sqlparser_index_span_t(&v->target_columns);
fputs("rows=", stdout);
rec_sqlparser_index_span_t(&v->rows);
fputs("condition_block_index=", stdout);
printf("%lld\n", (long long)v->condition_block_index);
fputs("has_condition_block=", stdout);
printf("%lld\n", (long long)v->has_condition_block);
fputs("condition_selector=", stdout);
selector(&v->condition_selector);
fputs("has_condition_selector=", stdout);
printf("%lld\n", (long long)v->has_condition_selector);
fputs("delete_condition_selector=", stdout);
selector(&v->delete_condition_selector);
fputs("has_delete_condition_selector=", stdout);
printf("%lld\n", (long long)v->has_delete_condition_selector);
}
static void rec_sqlparser_graph_dml_column_t(const sqlparser_graph_dml_column_t *v) { puts("sqlparser_graph_dml_column_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("dml_index=", stdout);
printf("%lld\n", (long long)v->dml_index);
fputs("ordinal=", stdout);
printf("%lld\n", (long long)v->ordinal);
fputs("column_name=", stdout);
text(v->column_name);
fputs("selector=", stdout);
selector(&v->selector);
fputs("has_selector=", stdout);
printf("%lld\n", (long long)v->has_selector);
fputs("quoted_identifier=", stdout);
printf("%lld\n", (long long)v->quoted_identifier);
}
static void rec_sqlparser_graph_dml_cell_t(const sqlparser_graph_dml_cell_t *v) { puts("sqlparser_graph_dml_cell_t");
fputs("index=", stdout);
printf("%lld\n", (long long)v->index);
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("dml_index=", stdout);
printf("%lld\n", (long long)v->dml_index);
fputs("row_index=", stdout);
printf("%lld\n", (long long)v->row_index);
fputs("column_ordinal=", stdout);
printf("%lld\n", (long long)v->column_ordinal);
fputs("kind=", stdout);
printf("%lld\n", (long long)v->kind);
fputs("source_target_index=", stdout);
printf("%lld\n", (long long)v->source_target_index);
fputs("has_source_target=", stdout);
printf("%lld\n", (long long)v->has_source_target);
fputs("source_field_index=", stdout);
printf("%lld\n", (long long)v->source_field_index);
fputs("has_source_field=", stdout);
printf("%lld\n", (long long)v->has_source_field);
fputs("literal=", stdout);
rec_sqlparser_literal_view_t(&v->literal);
fputs("bind=", stdout);
text(v->bind);
fputs("has_bind=", stdout);
printf("%lld\n", (long long)v->has_bind);
fputs("bind_kind=", stdout);
printf("%lld\n", (long long)v->bind_kind);
fputs("bind_sql=", stdout);
text(v->bind_sql);
fputs("has_bind_sql=", stdout);
printf("%lld\n", (long long)v->has_bind_sql);
fputs("bind_position=", stdout);
printf("%lld\n", (long long)v->bind_position);
fputs("has_bind_position=", stdout);
printf("%lld\n", (long long)v->has_bind_position);
fputs("selector=", stdout);
selector(&v->selector);
fputs("has_selector=", stdout);
printf("%lld\n", (long long)v->has_selector);
}
static void rec_sqlparser_query_graph_view_t(const sqlparser_query_graph_view_t *v) { puts("sqlparser_query_graph_view_t");
fputs("statement_index=", stdout);
printf("%lld\n", (long long)v->statement_index);
fputs("generation=", stdout);
printf("%lld\n", (long long)v->generation);
fputs("root_block_index=", stdout);
printf("%lld\n", (long long)v->root_block_index);
fputs("has_root_block=", stdout);
printf("%lld\n", (long long)v->has_root_block);
fputs("block_count=", stdout);
printf("%lld\n", (long long)v->block_count);
fputs("relation_count=", stdout);
printf("%lld\n", (long long)v->relation_count);
fputs("target_count=", stdout);
printf("%lld\n", (long long)v->target_count);
fputs("field_count=", stdout);
printf("%lld\n", (long long)v->field_count);
fputs("value_count=", stdout);
printf("%lld\n", (long long)v->value_count);
fputs("set_count=", stdout);
printf("%lld\n", (long long)v->set_count);
fputs("predicate_count=", stdout);
printf("%lld\n", (long long)v->predicate_count);
fputs("has_dml=", stdout);
printf("%lld\n", (long long)v->has_dml);
fputs("dml_branch_count=", stdout);
printf("%lld\n", (long long)v->dml_branch_count);
}
static void selector_check(const sqlparser_selector_t *s){char *v=NULL;sqlparser_selector_t p;OK(sqlparser_selector_format(s,&v,&err));text(v);OK(sqlparser_selector_parse(v,&p,&err));char *u=NULL;OK(sqlparser_selector_format(&p,&u,&err));REQUIRE(!strcmp(v,u));sqlparser_string_free(v);sqlparser_string_free(u);}
static const sqlparser_dialect_multi_insert_t *multi(sqlparser_handle_t *h){const sqlparser_dialect_multi_insert_t*m=sqlparser_dialect_state_multi_insert(h->dialect,h->dialect_state);REQUIRE(m);return m;}
/* Read-only observations go to stderr, outside the compared public record.
 * A true getter proves eligibility, not a runtime count of skipped calls. */
static void header_state(sqlparser_handle_t *h,const char *label,int expected)
{
#ifdef HEADER_CLASSIFICATION
 const sqlparser_dialect_multi_insert_t *m=multi(h);
 int got=sqlparser_oracle_multi_insert_graph_headers_simple(h,m);
 unsigned flag=m->oracle_graph_header_flags;
 if(expected>=0){REQUIRE(flag==(expected?SQLPARSER_ORACLE_GRAPH_HEADER_SIMPLE_ASCII_ALL:0U));REQUIRE(got==expected);}
 fprintf(stderr,"header %s dialect=%d flag=%u getter=%d expected=%d graph=%d source_current=%d generic_equivalent=%d eligibility_only=1\n",label,h->dialect,flag,got,expected,h->query_graph!=NULL,sqlparser_oracle_multi_insert_source_is_current(h),m->oracle_generic_spans_equivalent);
#else
 (void)h;(void)label;(void)expected;
#endif
}
static void wire_record(const sqlparser_handle_t *h)
{
 const unsigned char *p=(const unsigned char *)h->parse_tree.data;
 printf("owned_wire_bytes=%zu\n",(size_t)h->parse_tree.len);
 for(size_t i=0;i<(size_t)h->parse_tree.len;i++)printf("%02x",p[i]);
 putchar('\n');
}
static int outside(const char*p,const char*s){if(!p||!s)return 1;return (uintptr_t)p<(uintptr_t)s||(uintptr_t)p>(uintptr_t)s+strlen(s);}
static void owned_text(sqlparser_handle_t*h,const char*p){REQUIRE(outside(p,h->sql));REQUIRE(outside(p,h->parser_sql));if(p) REQUIRE(p[strlen(p)]=='\0');}
/* Read-only logical adapter. Baseline uses its original legacy representation;
 * only the current implementation includes the real production tagged accessors. There is
 * no token classifier or construction algorithm in this observation layer. */
static int obs_is_compact(const sqlparser_dialect_multi_insert_branch_t *b)
{
#ifdef COMPACT_BACKEND
    REQUIRE(b->cell_storage==SQLPARSER_ORACLE_CELL_STORAGE_LEGACY ||
        b->cell_storage==SQLPARSER_ORACLE_CELL_STORAGE_COMPACT);
    return b->cell_storage==SQLPARSER_ORACLE_CELL_STORAGE_COMPACT;
#else
    (void)b;return 0;
#endif
}
static const void *obs_storage(const sqlparser_dialect_multi_insert_branch_t *b)
{
#ifdef COMPACT_BACKEND
    if(obs_is_compact(b)){REQUIRE(!b->cells);return b->oracle_compact_cells;}
    REQUIRE(!b->oracle_compact_cells);
#endif
    return b->cells;
}
static const char *obs_public(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_public_sql(b,c);
#else
    return b->cells[c].public_sql;
#endif
}
static const char *obs_parser(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_parser_sql(b,c);
#else
    return b->cells[c].parser_sql;
#endif
}
static int obs_has_literal(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_has_literal(b,c);
#else
    return b->cells[c].has_literal;
#endif
}
static const sqlparser_literal_view_t *obs_literal(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_literal(b,c);
#else
    return &b->cells[c].literal;
#endif
}
static const sqlparser_dialect_multi_insert_value_t *obs_legacy(const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
#ifdef COMPACT_BACKEND
    return sqlparser_oracle_cell_legacy(b,c);
#else
    return &b->cells[c];
#endif
}
static int same_text(const char *a,const char *b){return a&&b?!strcmp(a,b):a==b;}
static void compare_cell_logical(const sqlparser_dialect_multi_insert_branch_t *a,
    const sqlparser_dialect_multi_insert_branch_t *b,size_t c)
{
    const sqlparser_literal_view_t *x=obs_literal(a,c),*y=obs_literal(b,c);
    const sqlparser_dialect_multi_insert_value_t empty={0};
    const sqlparser_dialect_multi_insert_value_t *u=obs_legacy(a,c),*v=obs_legacy(b,c);
    if(!u)u=&empty;
    if(!v)v=&empty;
    REQUIRE(same_text(obs_public(a,c),obs_public(b,c))&&same_text(obs_parser(a,c),obs_parser(b,c)));
    REQUIRE(obs_has_literal(a,c)==obs_has_literal(b,c)&&x->kind==y->kind);
    REQUIRE(same_text(x->string_value,y->string_value)&&same_text(x->float_value,y->float_value));
    REQUIRE(x->integer_value==y->integer_value&&x->boolean_value==y->boolean_value&&x->quoted_identifier==y->quoted_identifier);
    if(x->string_value)REQUIRE(x->string_value!=y->string_value);
    if(x->float_value)REQUIRE(x->float_value!=y->float_value);
    REQUIRE(u->has_bind==v->has_bind&&u->bind_kind==v->bind_kind&&!strcmp(u->bind,v->bind));
    REQUIRE(!strcmp(u->bind_sql,v->bind_sql)&&u->bind_position==v->bind_position&&u->has_bind_position==v->has_bind_position);
}
/* Additional complete logical private records do not replace any public,
 * owned-wire, selector, JSON or graph record in the original producer. */
static void logical_cells(sqlparser_handle_t *h)
{
    const sqlparser_dialect_multi_insert_t *m=multi(h);
    puts("logical_private_cells");
    for(size_t b=0;b<m->branch_count;b++)for(size_t c=0;c<m->branches[b].cell_count;c++){
        const sqlparser_dialect_multi_insert_branch_t *r=&m->branches[b];
        const sqlparser_dialect_multi_insert_value_t empty={0};
        const sqlparser_dialect_multi_insert_value_t *v=obs_legacy(r,c);if(!v)v=&empty;
        printf("branch=%zu cell=%zu has_literal=%d has_bind=%d bind_kind=%d bind_position=%zu has_bind_position=%d\n",
            b,c,obs_has_literal(r,c),v->has_bind,v->bind_kind,v->bind_position,v->has_bind_position);
        text(obs_public(r,c));text(obs_parser(r,c));rec_sqlparser_literal_view_t(obs_literal(r,c));
        text(v->bind);text(v->bind_sql);
    }
}
static void public_cell_sql_record(sqlparser_handle_t *h)
{
    const sqlparser_dialect_multi_insert_t *m=multi(h);size_t count=0;
    puts("public_cell_sql_getters");
    for(size_t b=0;b<m->branch_count;b++)for(size_t c=0;c<m->branches[b].cell_count;c++){
        const sqlparser_dialect_multi_insert_branch_t *r=&m->branches[b];
        const sqlparser_literal_view_t *literal=obs_literal(r,c);char *out=NULL;
        OK(sqlparser_insert_cell_sql(h,0,b,c,&out,&err));REQUIRE(out&&!strcmp(out,obs_public(r,c)));
        REQUIRE(outside(out,obs_public(r,c))&&outside(out,obs_parser(r,c)));
        REQUIRE(outside(out,literal->string_value)&&outside(out,literal->float_value));
        owned_text(h,out);text(out);sqlparser_string_free(out);count++;
    }
    fprintf(stderr,"public_sql_getters cells=%zu independent_owned_outputs=1 graph_materialized=%d\n",count,h->query_graph!=NULL);
}
typedef struct {uintptr_t start,end;} observed_owner_t;
static int owner_compare(const void *a,const void *b)
{
    uintptr_t x=((const observed_owner_t*)a)->start,y=((const observed_owner_t*)b)->start;
    return x<y?-1:x>y;
}
static void note_owner(sqlparser_handle_t *h,observed_owner_t *owners,size_t *count,const char *start,const char *last)
{
    REQUIRE(start&&last);owned_text(h,start);owned_text(h,last);
    owners[*count].start=(uintptr_t)start;
    owners[*count].end=(uintptr_t)last+strlen(last)+1U;
    REQUIRE(owners[*count].end>owners[*count].start);++*count;
}
static void require_storage(sqlparser_handle_t *h,const char *label,size_t compact_branches,size_t legacy_branches)
{
    const sqlparser_dialect_multi_insert_t *m=multi(h);size_t compact=0,legacy=0;
#ifndef COMPACT_BACKEND
    legacy_branches+=compact_branches;compact_branches=0;
#endif
    for(size_t b=0;b<m->branch_count;b++)if(obs_is_compact(&m->branches[b]))compact++;else legacy++;
    REQUIRE(compact==compact_branches&&legacy==legacy_branches);
    fprintf(stderr,"storage_expected %s compact_branches=%zu legacy_branches=%zu verified=1\n",label,compact,legacy);
}
static void observe_storage(sqlparser_handle_t *h,const char *label)
{
    const sqlparser_dialect_multi_insert_t *m=multi(h);
    size_t cells=0,compact=0,legacy=0,compact_branches=0,owners_count=0,text_bytes=0,initial_spans=0,certified_spans=0;
    for(size_t b=0;b<m->branch_count;b++)cells+=m->branches[b].cell_count;
    observed_owner_t *owners=calloc(cells?4*cells:1,sizeof(*owners));REQUIRE(owners);
    for(size_t b=0;b<m->branch_count;b++){
        const sqlparser_dialect_multi_insert_branch_t *r=&m->branches[b];int compact_kind=obs_is_compact(r);
        REQUIRE(!r->cell_count||obs_storage(r));compact_branches+=(size_t)compact_kind;
#ifdef COMPACT_BACKEND
        REQUIRE(sqlparser_oracle_branch_has_cells(r)==(obs_storage(r)!=NULL));
        if(compact_kind)REQUIRE(h->dialect==SQLPARSER_DIALECT_ORACLE ||
            h->dialect==SQLPARSER_DIALECT_VASTBASE_ORACLE ||
            h->dialect==SQLPARSER_DIALECT_KINGBASE_ORACLE);
#endif
        for(size_t c=0;c<r->cell_count;c++){
            const char *pub=obs_public(r,c),*parser=obs_parser(r,c);
            const sqlparser_literal_view_t *literal=obs_literal(r,c);
            const sqlparser_dialect_multi_insert_value_t *v=obs_legacy(r,c);
            REQUIRE(pub&&parser&&pub!=parser);owned_text(h,pub);owned_text(h,parser);
            if(compact_kind){
                const char *last=parser;compact++;
                REQUIRE(!v&&parser==pub+strlen(pub)+1U&&!strcmp(pub,parser));
                if(literal->string_value){REQUIRE(literal->string_value==parser+strlen(parser)+1U);last=literal->string_value;}
                if(literal->float_value){REQUIRE(!literal->string_value&&literal->float_value==parser+strlen(parser)+1U);last=literal->float_value;}
                note_owner(h,owners,&owners_count,pub,last);
            }else{
                legacy++;REQUIRE(v);
                note_owner(h,owners,&owners_count,pub,pub);note_owner(h,owners,&owners_count,parser,parser);
                if(v->literal_string_value){REQUIRE(literal->string_value==v->literal_string_value);note_owner(h,owners,&owners_count,v->literal_string_value,v->literal_string_value);}
                if(v->literal_float_value){REQUIRE(literal->float_value==v->literal_float_value);note_owner(h,owners,&owners_count,v->literal_float_value,v->literal_float_value);}
            }
            size_t ss=0,se=0,vp=0,cs=0,ce=0;
            initial_spans+=(size_t)sqlparser_oracle_multi_insert_initial_cell_span(h,b,c,&ss,&se,&vp,&cs,&ce);
            certified_spans+=(size_t)sqlparser_oracle_multi_insert_certified_cell_span(h,b,c,&cs,&ce);
        }
    }
    qsort(owners,owners_count,sizeof(*owners),owner_compare);
    for(size_t i=0;i<owners_count;i++){
        if(i)REQUIRE(owners[i-1].end<=owners[i].start);
        text_bytes+=(size_t)(owners[i].end-owners[i].start);
    }
    REQUIRE(sizeof(sqlparser_graph_dml_cell_t)==456U);
    size_t compact_size=0;
#ifdef COMPACT_BACKEND
    compact_size=sizeof(sqlparser_oracle_compact_value_t);REQUIRE(compact_size==64U);
#endif
    fprintf(stderr,"storage %s dialect=%d branches=%zu compact_branches=%zu legacy_branches=%zu cells=%zu compact_cells=%zu legacy_cells=%zu compact_record_size=%zu legacy_record_size=%zu live_cell_record_bytes=%zu live_text_owners=%zu live_text_bytes=%zu owner_ranges_disjoint=1 independent_NUL=1 initial_span_hits=%zu certified_span_hits=%zu graph_materialized=%d public_cell_size=%zu allocation_call_count=unmeasured capacity_bytes=unmeasured\n",
        label,h->dialect,m->branch_count,compact_branches,m->branch_count-compact_branches,cells,compact,legacy,
        compact_size,sizeof(sqlparser_dialect_multi_insert_value_t),compact*compact_size+legacy*sizeof(sqlparser_dialect_multi_insert_value_t),
        owners_count,text_bytes,initial_spans,certified_spans,h->query_graph!=NULL,sizeof(sqlparser_graph_dml_cell_t));
    free(owners);
}
static sqlparser_handle_t *parse(const char *s,int dialect,int json){sqlparser_handle_t*h=NULL;sqlparser_parse_options_t o;sqlparser_parse_options_default(&o);o.dialect=dialect;if(json)o.limits.max_output_bytes=128U*1024U*1024U;OK(sqlparser_parse_with_options(s,&o,&h,&err));REQUIRE(h);const sqlparser_dialect_multi_insert_t*m=multi(h);for(size_t i=0;i<m->branch_count;i++){const sqlparser_dialect_multi_insert_branch_t*b=&m->branches[i];REQUIRE(outside(b->relation.sql,s));REQUIRE(outside(b->relation.database_name,s));REQUIRE(outside(b->relation.schema_name,s));REQUIRE(outside(b->relation.table_name,s));REQUIRE(outside(b->relation.link_name,s));REQUIRE(outside(b->relation.link_sql,s));for(size_t c=0;c<b->column_count;c++){REQUIRE(outside(b->columns[c].name,s));REQUIRE(outside(b->columns[c].sql,s));}}return h;}
/* All public graph fields, span mappings and usable selectors for these
 * ordinary INSERT ALL/FIRST inputs. No instrumentation or mutation. */
static void span_record(const sqlparser_query_graph_view_t *g, sqlparser_index_span_t s)
{
    for (size_t i=0; i<s.count; i++) {
        size_t index; OK(sqlparser_query_graph_span_index_at(g,s,i,&index,&err));
        printf("span_index=%zu\n",index);
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
    OK(sqlparser_query_graph_expression_count(&g,&count,&err));printf("expressions=%zu\n",count);
    for(size_t i=0;i<count;i++) {
        sqlparser_graph_expression_t v;OK(sqlparser_query_graph_expression_at(&g,i,&v,&err));
        rec_sqlparser_graph_expression_t(&v);SEL(selector);SEL(argument_list_selector);span_record(&g,v.arguments);
    }
    OK(sqlparser_query_graph_expression_argument_count(&g,&count,&err));printf("expression_arguments=%zu\n",count);
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
static void public_capture(sqlparser_handle_t*h,const char*label){puts(label);printf("dialect=%d statements=%zu\n",sqlparser_handle_dialect(h),sqlparser_statement_count(h));text(sqlparser_original_sql(h));text(h->parser_sql);wire_record(h);capture_graph(h);logical_cells(h);public_cell_sql_record(h);observe_storage(h,label);}
static char *deparse(sqlparser_handle_t*h){char*out=NULL;OK(sqlparser_deparse(h,&out,&err));REQUIRE(out);text(out);return out;}

static void legacy_public(sqlparser_handle_t*h){sqlparser_statement_kind_t kind;const char*node=NULL;size_t n=0;OK(sqlparser_statement_kind(h,0,&kind,&err));printf("statement_kind=%d\n",kind);OK(sqlparser_statement_node_name(h,0,&node,&err));text(node);OK(sqlparser_statement_relation_count(h,0,&n,&err));printf("relation_count=%zu\n",n);for(size_t i=0;i<n;i++){sqlparser_relation_view_t r;OK(sqlparser_statement_relation(h,0,i,&r,&err));rec_sqlparser_relation_view_t(&r);}OK(sqlparser_statement_name_count(h,0,&n,&err));printf("name_count=%zu\n",n);for(size_t i=0;i<n;i++){sqlparser_name_view_t v;OK(sqlparser_statement_name(h,0,i,&v,&err));rec_sqlparser_name_view_t(&v);}}
static void shadow_json(const char*sql,int dialect,const char*label,int preload){sqlparser_handle_t*h=parse(sql,dialect,1);REQUIRE(!h->query_graph);REQUIRE(!h->ast);puts(label);if(preload){capture_graph(h);fprintf(stderr,"json %s preload_graph=1 graph_before=%d ast_before=%d\n",label,h->query_graph!=NULL,h->ast!=NULL);}else fprintf(stderr,"json %s preload_graph=0 graph_before=0 ast_before=0\n",label);const void*cache=h->query_graph;char*j=NULL;OK(sqlparser_export_view_json(h,0,&j,&err));REQUIRE(j);text(j);fprintf(stderr,"json %s bytes=%zu graph_after=%d ast_after=%d same_preloaded_cache=%d max_output=134217728\n",label,strlen(j),h->query_graph!=NULL,h->ast!=NULL,preload&&cache==h->query_graph);if(preload)REQUIRE(cache==h->query_graph);sqlparser_string_free(j);wire_record(h);legacy_public(h);sqlparser_handle_destroy(h);}


static void snapshot(sqlparser_handle_t *h,const char *label)
{
    REQUIRE(!h->query_graph);
    public_capture(h,label);
    const void *cache=h->query_graph;REQUIRE(cache);
    sqlparser_query_graph_view_t g;sqlparser_graph_dml_t d;
    OK(sqlparser_statement_query_graph(h,0,&g,&err));REQUIRE(h->query_graph==cache);
    OK(sqlparser_query_graph_dml(&g,&d,&err));
    size_t columns=0,cells=0;
    for(size_t b=0;b<d.branches.count;b++) {
        size_t bi;sqlparser_graph_dml_branch_t br;
        OK(sqlparser_query_graph_span_index_at(&g,d.branches,b,&bi,&err));
        OK(sqlparser_query_graph_dml_branch_at(&g,bi,&br,&err));
        columns+=br.target_columns.count;cells+=br.rows.count;
    }
    fprintf(stderr,"graph %s dialect=%d branches=%zu columns=%zu cells=%zu fresh_then_warm_same_cache=1 no_runtime_bypass_counter=1\n",label,h->dialect,d.branches.count,columns,cells);
    char *s=deparse(h);
    shadow_json(s,h->dialect,label,0);shadow_json(s,h->dialect,label,1);
    sqlparser_string_free(s);
}
static void deep_clone(sqlparser_handle_t *h,sqlparser_handle_t **out)
{
    OK(sqlparser_handle_clone(h,out,&err));REQUIRE(*out&&!(*out)->query_graph);
    const sqlparser_dialect_multi_insert_t *a=multi(h),*b=multi(*out);
    REQUIRE(a!=b&&a->branches!=b->branches&&a->branch_count==b->branch_count);
    for(size_t i=0;i<a->branch_count;i++) {
        const sqlparser_dialect_multi_insert_branch_t *x=&a->branches[i],*y=&b->branches[i];
        REQUIRE((!x->column_count||x->columns!=y->columns)&&obs_storage(x)!=obs_storage(y));
        REQUIRE(!obs_is_compact(y));
        REQUIRE(x->column_count==y->column_count&&x->cell_count==y->cell_count);
#define DISTINCT(field) do{if(x->relation.field){REQUIRE(y->relation.field&&x->relation.field!=y->relation.field&&!strcmp(x->relation.field,y->relation.field));}else REQUIRE(!y->relation.field);}while(0)
        DISTINCT(database_name);DISTINCT(schema_name);DISTINCT(table_name);DISTINCT(link_name);DISTINCT(link_sql);DISTINCT(sql);
#undef DISTINCT
        for(size_t c=0;c<x->column_count;c++) {
            REQUIRE(x->columns[c].name!=y->columns[c].name&&!strcmp(x->columns[c].name,y->columns[c].name));
            REQUIRE(x->columns[c].sql!=y->columns[c].sql&&!strcmp(x->columns[c].sql,y->columns[c].sql));
        }
        for(size_t c=0;c<x->cell_count;c++) {
            REQUIRE(obs_public(x,c)!=obs_public(y,c)&&!strcmp(obs_public(x,c),obs_public(y,c)));
            REQUIRE(obs_parser(x,c)!=obs_parser(y,c)&&!strcmp(obs_parser(x,c),obs_parser(y,c)));
            compare_cell_logical(x,y,c);
        }
    }
    require_storage(*out,"clone.before_owner_destroy",0,b->branch_count);
    observe_storage(*out,"clone.before_owner_destroy");
    header_state(*out,"clone.before_owner_destroy",0);
}
static void apply(sqlparser_handle_t *h,sqlparser_patch_t *p,size_t n)
{
    sqlparser_patch_list_t list={p,n};OK(sqlparser_apply_patch(h,&list,&err));
    REQUIRE(!h->query_graph);header_state(h,"successful_public_patch",0);
}

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
