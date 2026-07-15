#ifndef SQLPARSER_DIALECT_DML_RESULT_INTERNAL_H
#define SQLPARSER_DIALECT_DML_RESULT_INTERNAL_H

#include "sqlparser_dialect_internal.h"

typedef struct {
	sqlparser_graph_dml_kind_t kind;
	size_t channel_count;
	size_t parent_dml_index;
	int has_parent;
	const char *source_name;
	int has_duplicate_target_relation;
} sqlparser_dialect_dml_result_dml_t;

typedef struct {
	sqlparser_graph_dml_result_kind_t kind;
	size_t target_offset;
	size_t target_count;
	const char *sink_sql;
	size_t sink_column_count;
} sqlparser_dialect_dml_result_channel_t;

size_t sqlparser_dialect_dml_result_count(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index);

int sqlparser_dialect_dml_result_dml_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	sqlparser_dialect_dml_result_dml_t *out_dml);

int sqlparser_dialect_dml_result_channel_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	sqlparser_dialect_dml_result_channel_t *out_channel);

const char *sqlparser_dialect_dml_result_sink_column_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index);

const char *sqlparser_dialect_dml_result_action_marker_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index);

sqlparser_status_t sqlparser_dialect_dml_result_preprocess_target_sql(
	sqlparser_dialect_t dialect,
	const void *state,
	const char *public_sql,
	sqlparser_graph_dml_kind_t dml_kind,
	char **out_sql,
	char **out_action_marker,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_dialect_dml_result_postprocess_target_sql(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *parser_sql,
	char **out_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_dialect_dml_result_adjust_target_count(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	ptrdiff_t delta,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_dialect_dml_result_set_action_marker(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *marker,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_dialect_dml_result_set_sink_sql(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	const char *sink_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_dialect_dml_result_set_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_dialect_dml_result_insert_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_dialect_dml_result_delete_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	sqlparser_error_t *out_error);

#endif
