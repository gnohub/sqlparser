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
