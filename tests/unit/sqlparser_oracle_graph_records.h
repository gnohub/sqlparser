/* Explicit public-field serialization. Do not serialize pointers or padding. */
static void record_sqlparser_literal_view_t(const sqlparser_literal_view_t *v)
{
    record_number((unsigned long long)v->kind);
    record_text(v->string_value);
    record_text(v->float_value);
    record_number((unsigned long long)v->integer_value);
    record_number((unsigned long long)v->boolean_value);
    record_number((unsigned long long)v->quoted_identifier);
}
static void record_sqlparser_selector_t(const sqlparser_selector_t *v)
{
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->item_index);
    record_number((unsigned long long)v->row_index);
    record_number((unsigned long long)v->column_index);
}
static void record_sqlparser_index_span_t(const sqlparser_index_span_t *v)
{
    record_number((unsigned long long)v->offset);
    record_number((unsigned long long)v->count);
}
static void record_sqlparser_target_path_entry_t(const sqlparser_target_path_entry_t *v)
{
    record_text(v->kind);
    record_text(v->name);
    record_number((unsigned long long)v->has_name);
    record_number((unsigned long long)v->name_truncated);
    record_number((unsigned long long)v->arg_index);
}
static void record_sqlparser_query_graph_view_t(const sqlparser_query_graph_view_t *v)
{
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->generation);
    record_number((unsigned long long)v->root_block_index);
    record_number((unsigned long long)v->has_root_block);
    record_number((unsigned long long)v->block_count);
    record_number((unsigned long long)v->relation_count);
    record_number((unsigned long long)v->target_count);
    record_number((unsigned long long)v->field_count);
    record_number((unsigned long long)v->value_count);
    record_number((unsigned long long)v->set_count);
    record_number((unsigned long long)v->predicate_count);
    record_number((unsigned long long)v->has_dml);
    record_number((unsigned long long)v->dml_branch_count);
}
static void record_sqlparser_graph_block_t(const sqlparser_graph_block_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->kind);
    record_sqlparser_index_span_t(&v->relations);
    record_sqlparser_index_span_t(&v->targets);
    record_sqlparser_index_span_t(&v->predicates);
}
static void record_sqlparser_graph_relation_t(const sqlparser_graph_relation_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->block_index);
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->quoted_identifier);
    record_text(v->database_name);
    record_text(v->schema_name);
    record_text(v->object_name);
    record_text(v->alias_name);
    record_text(v->link_name);
    record_number((unsigned long long)v->source_block_index);
    record_number((unsigned long long)v->has_source_block);
    record_number((unsigned long long)v->database_quoted_identifier);
    record_number((unsigned long long)v->schema_quoted_identifier);
    record_number((unsigned long long)v->link_quoted_identifier);
    record_number((unsigned long long)v->ddl_role);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
    record_number((unsigned long long)v->alias_quoted_identifier);
}
static void record_sqlparser_graph_target_t(const sqlparser_graph_target_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->block_index);
    record_number((unsigned long long)v->ordinal);
    record_number((unsigned long long)v->kind);
    record_text(v->output_name);
    record_number((unsigned long long)v->field_index);
    record_number((unsigned long long)v->has_field);
    record_number((unsigned long long)v->value_index);
    record_number((unsigned long long)v->has_value);
    record_sqlparser_index_span_t(&v->star_relations);
    record_number((unsigned long long)v->source_block_index);
    record_number((unsigned long long)v->has_source_block);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
    record_sqlparser_selector_t(&v->target_list_selector);
    record_number((unsigned long long)v->has_target_list_selector);
    record_number((unsigned long long)v->sink_value_index);
    record_number((unsigned long long)v->has_sink_value);
    record_number((unsigned long long)v->output_quoted_identifier);
}
static void record_sqlparser_graph_field_t(const sqlparser_graph_field_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->block_index);
    record_number((unsigned long long)v->clause);
    record_number((unsigned long long)v->relation_index);
    record_number((unsigned long long)v->has_relation);
    record_number((unsigned long long)v->quoted_identifier);
    record_sqlparser_index_span_t(&v->candidate_relations);
    record_text(v->column_name);
    record_number((unsigned long long)v->target_index);
    record_number((unsigned long long)v->has_target);
    { size_t j; for (j=0U; j < sizeof(v->target_path)/sizeof(v->target_path[0]); ++j) record_sqlparser_target_path_entry_t(&v->target_path[j]); }
    record_number((unsigned long long)v->target_path_count);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
    record_number((unsigned long long)v->pseudo);
    record_number((unsigned long long)v->prior);
}
static void record_sqlparser_graph_like_escape_t(const sqlparser_graph_like_escape_t *v)
{
    record_number((unsigned long long)v->kind);
    record_sqlparser_literal_view_t(&v->literal);
    record_text(v->bind);
    record_number((unsigned long long)v->has_bind);
    record_number((unsigned long long)v->bind_kind);
    record_text(v->bind_sql);
    record_number((unsigned long long)v->has_bind_sql);
    record_number((unsigned long long)v->bind_position);
    record_number((unsigned long long)v->has_bind_position);
}
static void record_sqlparser_graph_value_t(const sqlparser_graph_value_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->block_index);
    record_number((unsigned long long)v->clause);
    record_text(v->operator_name);
    record_number((unsigned long long)v->operator_kind);
    record_number((unsigned long long)v->field_index);
    record_number((unsigned long long)v->has_field);
    record_number((unsigned long long)v->source_field_index);
    record_number((unsigned long long)v->has_source_field);
    record_number((unsigned long long)v->field_match_kind);
    record_number((unsigned long long)v->kind);
    record_sqlparser_literal_view_t(&v->literal);
    record_text(v->bind);
    record_number((unsigned long long)v->has_bind);
    record_number((unsigned long long)v->bind_kind);
    record_text(v->bind_sql);
    record_number((unsigned long long)v->has_bind_sql);
    record_number((unsigned long long)v->bind_position);
    record_number((unsigned long long)v->has_bind_position);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
    record_sqlparser_graph_like_escape_t(&v->like_escape);
}
static void record_sqlparser_graph_expression_t(const sqlparser_graph_expression_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->block_index);
    record_number((unsigned long long)v->clause);
    record_number((unsigned long long)v->kind);
    record_text(v->sql);
    record_text(v->name);
    record_sqlparser_index_span_t(&v->arguments);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
    record_sqlparser_selector_t(&v->argument_list_selector);
    record_number((unsigned long long)v->has_argument_list_selector);
}
static void record_sqlparser_graph_expression_argument_t(const sqlparser_graph_expression_argument_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->expression_index);
    record_number((unsigned long long)v->ordinal);
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->value_index);
    record_number((unsigned long long)v->has_value);
    record_number((unsigned long long)v->field_index);
    record_number((unsigned long long)v->has_field);
    record_number((unsigned long long)v->nested_expression_index);
    record_number((unsigned long long)v->has_nested_expression);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
}
static void record_sqlparser_graph_set_t(const sqlparser_graph_set_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->result_block_index);
    record_sqlparser_index_span_t(&v->branch_blocks);
}
static void record_sqlparser_graph_predicate_t(const sqlparser_graph_predicate_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->block_index);
    record_number((unsigned long long)v->clause);
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->bool_operator);
    record_text(v->operator_name);
    record_number((unsigned long long)v->operator_kind);
    record_number((unsigned long long)v->left_field_index);
    record_number((unsigned long long)v->has_left_field);
    record_number((unsigned long long)v->right_field_index);
    record_number((unsigned long long)v->has_right_field);
    record_number((unsigned long long)v->value_index);
    record_number((unsigned long long)v->has_value);
    record_sqlparser_index_span_t(&v->children);
    record_number((unsigned long long)v->nocycle);
}
static void record_sqlparser_graph_session_t(const sqlparser_graph_session_t *v)
{
    record_number((unsigned long long)v->action);
    record_number((unsigned long long)v->item_count);
}
static void record_sqlparser_graph_session_item_t(const sqlparser_graph_session_item_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->scope);
    record_number((unsigned long long)v->target_kind);
    record_text(v->name);
    record_number((unsigned long long)v->value_offset);
    record_number((unsigned long long)v->value_count);
}
static void record_sqlparser_graph_session_value_t(const sqlparser_graph_session_value_t *v)
{
    record_number((unsigned long long)v->index);
    record_text(v->name);
    record_number((unsigned long long)v->kind);
    record_text(v->text);
    record_sqlparser_literal_view_t(&v->literal);
    record_text(v->bind_key);
    record_number((unsigned long long)v->bind_kind);
    record_text(v->bind_sql);
    record_number((unsigned long long)v->bind_position);
    record_number((unsigned long long)v->has_bind_position);
}
static void record_sqlparser_graph_dml_t(const sqlparser_graph_dml_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->insert_mode);
    record_number((unsigned long long)v->target_relation_index);
    record_number((unsigned long long)v->has_target_relation);
    record_sqlparser_index_span_t(&v->target_columns);
    record_sqlparser_index_span_t(&v->rows);
    record_sqlparser_index_span_t(&v->assignments);
    record_sqlparser_index_span_t(&v->delete_targets);
    record_sqlparser_index_span_t(&v->branches);
    record_number((unsigned long long)v->source_block_index);
    record_number((unsigned long long)v->has_source_block);
}
static void record_sqlparser_graph_dml_result_t(const sqlparser_graph_dml_result_t *v)
{
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->block_index);
    record_number((unsigned long long)v->sink_relation_index);
    record_number((unsigned long long)v->has_sink_relation);
    record_sqlparser_index_span_t(&v->sink_columns);
    record_sqlparser_index_span_t(&v->references);
}
static void record_sqlparser_graph_dml_reference_t(const sqlparser_graph_dml_reference_t *v)
{
    record_number((unsigned long long)v->target_index);
    record_number((unsigned long long)v->field_index);
    record_number((unsigned long long)v->has_field);
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->relation_index);
}
static void record_sqlparser_graph_dml_branch_t(const sqlparser_graph_dml_branch_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->dml_index);
    record_number((unsigned long long)v->ordinal);
    record_number((unsigned long long)v->branch_kind);
    record_number((unsigned long long)v->target_relation_index);
    record_number((unsigned long long)v->has_target_relation);
    record_sqlparser_index_span_t(&v->target_columns);
    record_sqlparser_index_span_t(&v->rows);
    record_number((unsigned long long)v->condition_block_index);
    record_number((unsigned long long)v->has_condition_block);
    record_sqlparser_selector_t(&v->condition_selector);
    record_number((unsigned long long)v->has_condition_selector);
    record_sqlparser_selector_t(&v->delete_condition_selector);
    record_number((unsigned long long)v->has_delete_condition_selector);
}
static void record_sqlparser_graph_dml_column_t(const sqlparser_graph_dml_column_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->dml_index);
    record_number((unsigned long long)v->ordinal);
    record_text(v->column_name);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
    record_number((unsigned long long)v->quoted_identifier);
}
static void record_sqlparser_graph_dml_cell_t(const sqlparser_graph_dml_cell_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->dml_index);
    record_number((unsigned long long)v->row_index);
    record_number((unsigned long long)v->column_ordinal);
    record_number((unsigned long long)v->kind);
    record_number((unsigned long long)v->source_target_index);
    record_number((unsigned long long)v->has_source_target);
    record_number((unsigned long long)v->source_field_index);
    record_number((unsigned long long)v->has_source_field);
    record_sqlparser_literal_view_t(&v->literal);
    record_text(v->bind);
    record_number((unsigned long long)v->has_bind);
    record_number((unsigned long long)v->bind_kind);
    record_text(v->bind_sql);
    record_number((unsigned long long)v->has_bind_sql);
    record_number((unsigned long long)v->bind_position);
    record_number((unsigned long long)v->has_bind_position);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
}
static void record_sqlparser_graph_dml_assignment_t(const sqlparser_graph_dml_assignment_t *v)
{
    record_number((unsigned long long)v->index);
    record_number((unsigned long long)v->statement_index);
    record_number((unsigned long long)v->dml_index);
    record_number((unsigned long long)v->target_field_index);
    record_number((unsigned long long)v->value_kind);
    record_number((unsigned long long)v->source_target_index);
    record_number((unsigned long long)v->has_source_target);
    record_number((unsigned long long)v->source_field_index);
    record_number((unsigned long long)v->has_source_field);
    record_sqlparser_index_span_t(&v->rhs_fields);
    record_sqlparser_index_span_t(&v->rhs_values);
    record_sqlparser_index_span_t(&v->rhs_blocks);
    record_sqlparser_literal_view_t(&v->literal);
    record_text(v->bind);
    record_number((unsigned long long)v->has_bind);
    record_number((unsigned long long)v->bind_kind);
    record_text(v->bind_sql);
    record_number((unsigned long long)v->has_bind_sql);
    record_number((unsigned long long)v->bind_position);
    record_number((unsigned long long)v->has_bind_position);
    record_sqlparser_selector_t(&v->selector);
    record_number((unsigned long long)v->has_selector);
}
static void record_sqlparser_bind_occurrence_t(const sqlparser_bind_occurrence_t *v)
{
    record_number((unsigned long long)v->position);
    record_number((unsigned long long)v->kind);
    record_text(v->key);
    record_text(v->sql);
}
