#ifndef SQLPARSER_DIALECT_SQLSERVER_OUTPUT_H
#define SQLPARSER_DIALECT_SQLSERVER_OUTPUT_H

#include "sqlparser_dialect_dml_result_internal.h"
#include "sqlparser_identifier_origin_internal.h"

typedef struct sqlparser_sqlserver_output_state sqlparser_sqlserver_output_state_t;

typedef struct {
	sqlparser_graph_dml_kind_t kind;
	size_t statement_index;
	size_t channel_count;
	size_t parent_dml_index;
	int has_parent;
	const char *source_name;
	int has_duplicate_target_relation;
} sqlparser_sqlserver_output_dml_view_t;

typedef struct {
	sqlparser_graph_dml_result_kind_t kind;
	size_t target_offset;
	size_t target_count;
	const char *sink_sql;
	size_t sink_column_count;
} sqlparser_sqlserver_output_channel_view_t;

sqlparser_status_t sqlparser_sqlserver_output_preprocess(
	char **io_sql,
	const sqlparser_limits_t *limits,
	unsigned int candidates,
	sqlparser_sqlserver_output_state_t **out_state,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_preprocess_identifier_origins(
	char **io_sql,
	const sqlparser_limits_t *limits,
	unsigned int candidates,
	sqlparser_sqlserver_output_state_t **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_postprocess(
	char **io_sql,
	const sqlparser_sqlserver_output_state_t *state,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_clone(
	const sqlparser_sqlserver_output_state_t *state,
	sqlparser_sqlserver_output_state_t **out_state,
	sqlparser_error_t *out_error);

void sqlparser_sqlserver_output_destroy(sqlparser_sqlserver_output_state_t *state);

size_t sqlparser_sqlserver_output_dml_count(const sqlparser_sqlserver_output_state_t *state);

int sqlparser_sqlserver_output_dml_at(
	const sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	sqlparser_sqlserver_output_dml_view_t *out_dml);

int sqlparser_sqlserver_output_channel_at(
	const sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	sqlparser_sqlserver_output_channel_view_t *out_channel);

const char *sqlparser_sqlserver_output_sink_column_at(
	const sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t column_index);

const char *sqlparser_sqlserver_output_action_marker_at(
	const sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t target_index);

sqlparser_status_t sqlparser_sqlserver_output_adjust_target_count(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	ptrdiff_t delta,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_set_action_marker(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *marker,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_set_sink_sql(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	const char *sink_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_set_sink_column(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_insert_sink_column(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_delete_sink_column(
	sqlparser_sqlserver_output_state_t *state,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_preprocess_target_sql(
	const char *public_sql,
	sqlparser_graph_dml_kind_t dml_kind,
	char **out_sql,
	char **out_action_marker,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_sqlserver_output_postprocess_target_sql(
	const char *parser_sql,
	const char *action_marker,
	char **out_sql,
	sqlparser_error_t *out_error);

#endif
