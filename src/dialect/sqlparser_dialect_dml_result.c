#include <string.h>

#include "sqlparser_dialect_dml_result_internal.h"
#include "sqlparser_dialect_sqlserver_internal.h"

static const sqlparser_sqlserver_output_state_t *sqlparser_dialect_sqlserver_output_state(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	if (out_local_statement_index != NULL) {
		*out_local_statement_index = statement_index;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		return NULL;
	}
	return sqlparser_sqlserver_state_output(
		state, statement_index, out_local_statement_index);
}

static sqlparser_sqlserver_output_state_t *sqlparser_dialect_sqlserver_output_state_mutable(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t *out_local_statement_index)
{
	if (out_local_statement_index != NULL) {
		*out_local_statement_index = statement_index;
	}
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		return NULL;
	}
	return sqlparser_sqlserver_state_output_mutable(
		state, statement_index, out_local_statement_index);
}

size_t sqlparser_dialect_dml_result_count(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t local_statement_index;
	size_t count;
	size_t index;

	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	count = 0U;
	for (index = 0U; index < sqlparser_sqlserver_output_dml_count(output); index++) {
		sqlparser_sqlserver_output_dml_view_t dml;

		if (sqlparser_sqlserver_output_dml_at(output, index, &dml) &&
		    dml.statement_index == local_statement_index) {
			count++;
		}
	}
	return count;
}

static int sqlparser_dialect_dml_global_index(
	const sqlparser_sqlserver_output_state_t *output,
	size_t statement_index,
	size_t dml_index,
	size_t *out_index)
{
	size_t index;
	size_t ordinal;

	ordinal = 0U;
	for (index = 0U; index < sqlparser_sqlserver_output_dml_count(output); index++) {
		sqlparser_sqlserver_output_dml_view_t dml;

		if (!sqlparser_sqlserver_output_dml_at(output, index, &dml) ||
		    dml.statement_index != statement_index) {
			continue;
		}
		if (ordinal == dml_index) {
			if (out_index != NULL) {
				*out_index = index;
			}
			return 1;
		}
		ordinal++;
	}
	return 0;
}

int sqlparser_dialect_dml_result_dml_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	sqlparser_dialect_dml_result_dml_t *out_dml)
{
	const sqlparser_sqlserver_output_state_t *output;
	sqlparser_sqlserver_output_dml_view_t dml;
	size_t global_index;
	size_t local_statement_index;

	if (out_dml == NULL) {
		return 0;
	}
	memset(out_dml, 0, sizeof(*out_dml));
	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index) ||
	    !sqlparser_sqlserver_output_dml_at(output, global_index, &dml)) {
		return 0;
	}
	out_dml->kind = dml.kind;
	out_dml->channel_count = dml.channel_count;
	out_dml->parent_dml_index = dml.parent_dml_index;
	out_dml->has_parent = dml.has_parent;
	out_dml->source_name = dml.source_name;
	out_dml->source_sql = dml.source_sql;
	out_dml->has_duplicate_target_relation = dml.has_duplicate_target_relation;
	return 1;
}

int sqlparser_dialect_dml_result_channel_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	sqlparser_dialect_dml_result_channel_t *out_channel)
{
	const sqlparser_sqlserver_output_state_t *output;
	sqlparser_sqlserver_output_channel_view_t channel;
	size_t global_index;
	size_t local_statement_index;

	if (out_channel == NULL) {
		return 0;
	}
	memset(out_channel, 0, sizeof(*out_channel));
	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index) ||
	    !sqlparser_sqlserver_output_channel_at(output, global_index, channel_index, &channel)) {
		return 0;
	}
	out_channel->kind = channel.kind;
	out_channel->target_offset = channel.target_offset;
	out_channel->target_count = channel.target_count;
	out_channel->sink_sql = channel.sink_sql;
	out_channel->sink_column_count = channel.sink_column_count;
	return 1;
}

const char *sqlparser_dialect_dml_result_sink_column_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index)) {
		return NULL;
	}
	return sqlparser_sqlserver_output_sink_column_at(
		output,
		global_index,
		channel_index,
		column_index);
}

const char *sqlparser_dialect_dml_result_action_marker_at(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index)
{
	const sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state(
		dialect, state, statement_index, &local_statement_index);
	if (!sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, &global_index)) {
		return NULL;
	}
	return sqlparser_sqlserver_output_action_marker_at(
		output, global_index, channel_index, target_index);
}

sqlparser_status_t sqlparser_dialect_dml_result_preprocess_target_sql(
	sqlparser_dialect_t dialect,
	const void *state,
	const char *public_sql,
	sqlparser_graph_dml_kind_t dml_kind,
	char **out_sql,
	char **out_action_marker,
	sqlparser_error_t *out_error)
{
	(void)state;
	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "dialect does not support DML result targets");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	return sqlparser_sqlserver_output_preprocess_target_sql(
		public_sql, dml_kind, out_sql, out_action_marker, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_postprocess_target_sql(
	sqlparser_dialect_t dialect,
	const void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *parser_sql,
	char **out_sql,
	sqlparser_error_t *out_error)
{
	const char *action_marker;

	if (!sqlparser_dialect_is_sqlserver_compatible(dialect)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_UNSUPPORTED, "dialect does not support DML result targets");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	action_marker = sqlparser_dialect_dml_result_action_marker_at(
		dialect, state, statement_index, dml_index, channel_index, target_index);
	return sqlparser_sqlserver_output_postprocess_target_sql(
		parser_sql, action_marker, out_sql, out_error);
}

static sqlparser_status_t sqlparser_dialect_dml_result_mutable_index(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	sqlparser_sqlserver_output_state_t **out_output,
	size_t *out_global_index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t local_statement_index;

	output = sqlparser_dialect_sqlserver_output_state_mutable(
		dialect, state, statement_index, &local_statement_index);
	if (output == NULL ||
	    !sqlparser_dialect_dml_global_index(output, local_statement_index, dml_index, out_global_index)) {
		sqlparser_error_set_message(out_error, SQLPARSER_STATUS_INVALID_ARGUMENT, "DML result index is out of range");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_output = output;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_dialect_dml_result_adjust_target_count(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	ptrdiff_t delta,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_adjust_target_count(
		output, global_index, channel_index, target_index, delta, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_set_action_marker(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t target_index,
	const char *marker,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_action_marker(
		output, global_index, channel_index, target_index, marker, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_set_sink_sql(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	const char *sink_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_sink_sql(
		output, global_index, channel_index, sink_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_set_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_set_sink_column(
		output, global_index, channel_index, column_index, column_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_insert_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	const char *column_sql,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_insert_sink_column(
		output, global_index, channel_index, column_index, column_sql, out_error);
}

sqlparser_status_t sqlparser_dialect_dml_result_delete_sink_column(
	sqlparser_dialect_t dialect,
	void *state,
	size_t statement_index,
	size_t dml_index,
	size_t channel_index,
	size_t column_index,
	sqlparser_error_t *out_error)
{
	sqlparser_sqlserver_output_state_t *output;
	size_t global_index;
	sqlparser_status_t status;

	status = sqlparser_dialect_dml_result_mutable_index(
		dialect, state, statement_index, dml_index, &output, &global_index, out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	return sqlparser_sqlserver_output_delete_sink_column(
		output, global_index, channel_index, column_index, out_error);
}
