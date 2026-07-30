#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser_identifier_origin_internal.h"
#include "sqlparser_internal.h"
#include "../dialect/sqlparser_dialect_internal.h"

typedef enum {
	SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_LINEAR = 1,
	SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_IDENTIFIER = 2,
	SQLPARSER_IDENTIFIER_ORIGIN_RUN_GENERATED = 3
} sqlparser_identifier_origin_run_kind_t;

struct sqlparser_identifier_origin_run {
	sqlparser_identifier_origin_run_kind_t kind;
	size_t output_offset;
	size_t output_length;
	size_t source_offset;
	size_t source_length;
};

struct sqlparser_identifier_origin_map {
	sqlparser_identifier_origin_run_t *runs;
	size_t run_count;
	size_t output_length;
};

typedef sqlparser_status_t (*sqlparser_identifier_origin_preprocess_fn)(
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t *origins,
	sqlparser_error_t *out_error);

static sqlparser_identifier_origin_preprocess_fn
sqlparser_identifier_origin_preprocess_for_dialect(sqlparser_dialect_t dialect)
{
	switch (dialect) {
		case SQLPARSER_DIALECT_POSTGRESQL:
			return sqlparser_postgresql_preprocess_identifier_origins;
		case SQLPARSER_DIALECT_MYSQL:
			return sqlparser_mysql_preprocess_identifier_origins;
		case SQLPARSER_DIALECT_ORACLE:
			return sqlparser_oracle_preprocess_identifier_origins;
		case SQLPARSER_DIALECT_SQLSERVER:
			return sqlparser_sqlserver_preprocess_identifier_origins;
		case SQLPARSER_DIALECT_DAMENG:
			return sqlparser_dameng_preprocess_identifier_origins;
		case SQLPARSER_DIALECT_VASTBASE_ORACLE:
			return sqlparser_vastbase_oracle_preprocess_identifier_origins;
		case SQLPARSER_DIALECT_VASTBASE_MYSQL:
			return sqlparser_vastbase_mysql_preprocess_identifier_origins;
		case SQLPARSER_DIALECT_VASTBASE_POSTGRESQL:
			return sqlparser_vastbase_postgresql_preprocess_identifier_origins;
		case SQLPARSER_DIALECT_VASTBASE_SQLSERVER:
			return sqlparser_vastbase_sqlserver_preprocess_identifier_origins;
		default:
			return NULL;
	}
}

static int sqlparser_identifier_origin_size_add(
	size_t left,
	size_t right,
	size_t *out_value)
{
	if (right > SIZE_MAX - left) {
		return 0;
	}
	*out_value = left + right;
	return 1;
}

static sqlparser_status_t sqlparser_identifier_origin_reserve_runs(
	sqlparser_identifier_origin_run_t **runs,
	size_t *capacity,
	size_t required,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_run_t *next;
	size_t next_capacity;

	if (required <= *capacity) {
		return SQLPARSER_STATUS_OK;
	}
	next_capacity = *capacity == 0U ? 8U : *capacity;
	while (next_capacity < required) {
		if (next_capacity > SIZE_MAX / 2U) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"identifier origin map is too large");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		next_capacity *= 2U;
	}
	if (next_capacity > SIZE_MAX / sizeof(*next)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"identifier origin map is too large");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	next = (sqlparser_identifier_origin_run_t *)realloc(
		*runs,
		next_capacity * sizeof(*next));
	if (next == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	*runs = next;
	*capacity = next_capacity;
	return SQLPARSER_STATUS_OK;
}

static int sqlparser_identifier_origin_run_can_merge(
	const sqlparser_identifier_origin_run_t *left,
	const sqlparser_identifier_origin_run_t *right)
{
	size_t left_output_end;
	size_t left_source_end;

	if (left->kind != SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_LINEAR ||
	    right->kind != SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_LINEAR ||
	    !sqlparser_identifier_origin_size_add(
		    left->output_offset,
		    left->output_length,
		    &left_output_end) ||
	    left_output_end != right->output_offset) {
		return 0;
	}
	if (!sqlparser_identifier_origin_size_add(
		    left->source_offset,
		    left->source_length,
		    &left_source_end)) {
		return 0;
	}
	return left_source_end == right->source_offset;
}

static sqlparser_status_t sqlparser_identifier_origin_writer_append_run(
	sqlparser_identifier_origin_writer_t *writer,
	const sqlparser_identifier_origin_run_t *run,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_run_t *previous;
	sqlparser_status_t status;
	size_t merged_output_length;
	size_t merged_source_length;

	if (run->output_length == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	if (writer->run_count > 0U) {
		previous = &writer->runs[writer->run_count - 1U];
		if (sqlparser_identifier_origin_run_can_merge(previous, run)) {
			if (!sqlparser_identifier_origin_size_add(
				    previous->output_length,
				    run->output_length,
				    &merged_output_length)) {
				sqlparser_error_set_message(
					out_error,
					SQLPARSER_STATUS_NO_MEMORY,
					"identifier origin map is too large");
				return SQLPARSER_STATUS_NO_MEMORY;
			}
			previous->output_length = merged_output_length;
			if (previous->kind ==
			    SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_LINEAR) {
				if (!sqlparser_identifier_origin_size_add(
					    previous->source_length,
					    run->source_length,
					    &merged_source_length)) {
					sqlparser_error_set_message(
						out_error,
						SQLPARSER_STATUS_NO_MEMORY,
						"identifier origin map is too large");
					return SQLPARSER_STATUS_NO_MEMORY;
				}
				previous->source_length = merged_source_length;
			}
			return SQLPARSER_STATUS_OK;
		}
	}
	status = sqlparser_identifier_origin_reserve_runs(
		&writer->runs,
		&writer->run_capacity,
		writer->run_count + 1U,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	writer->runs[writer->run_count++] = *run;
	return SQLPARSER_STATUS_OK;
}

static size_t sqlparser_identifier_origin_first_run(
	const sqlparser_identifier_origin_map_t *map,
	size_t offset)
{
	size_t left;
	size_t right;

	left = 0U;
	right = map->run_count;
	while (left < right) {
		size_t middle;
		size_t run_end;

		middle = left + (right - left) / 2U;
		if (!sqlparser_identifier_origin_size_add(
			    map->runs[middle].output_offset,
			    map->runs[middle].output_length,
			    &run_end) ||
		    run_end <= offset) {
			left = middle + 1U;
		} else {
			right = middle;
		}
	}
	return left;
}

sqlparser_status_t sqlparser_identifier_origin_map_new_identity(
	size_t input_length,
	sqlparser_identifier_origin_map_t **out_map,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_map_t *map;

	if (out_map == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin map output must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	*out_map = NULL;
	map = (sqlparser_identifier_origin_map_t *)calloc(1U, sizeof(*map));
	if (map == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_NO_MEMORY,
			"out of memory");
		return SQLPARSER_STATUS_NO_MEMORY;
	}
	map->output_length = input_length;
	if (input_length > 0U) {
		map->runs =
			(sqlparser_identifier_origin_run_t *)malloc(sizeof(*map->runs));
		if (map->runs == NULL) {
			free(map);
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_NO_MEMORY,
				"out of memory");
			return SQLPARSER_STATUS_NO_MEMORY;
		}
		map->runs[0].kind =
			SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_LINEAR;
		map->runs[0].output_offset = 0U;
		map->runs[0].output_length = input_length;
		map->runs[0].source_offset = 0U;
		map->runs[0].source_length = input_length;
		map->run_count = 1U;
	}
	*out_map = map;
	return SQLPARSER_STATUS_OK;
}

void sqlparser_identifier_origin_map_destroy(
	sqlparser_identifier_origin_map_t *map)
{
	if (map == NULL) {
		return;
	}
	free(map->runs);
	free(map);
}

sqlparser_status_t sqlparser_dialect_preprocess_identifier_origins(
	sqlparser_dialect_t dialect,
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t **out_origins,
	sqlparser_error_t *out_error)
{
	const sqlparser_dialect_ops_t *ops;
	sqlparser_identifier_origin_preprocess_fn preprocess;
	sqlparser_identifier_origin_map_t *origins;
	sqlparser_status_t status;

	if (out_parser_sql != NULL) {
		*out_parser_sql = NULL;
	}
	if (out_state != NULL) {
		*out_state = NULL;
	}
	if (out_origins != NULL) {
		*out_origins = NULL;
	}
	if (input_sql == NULL || out_parser_sql == NULL ||
	    out_state == NULL || out_origins == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin preprocess arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	preprocess =
		sqlparser_identifier_origin_preprocess_for_dialect(dialect);
	if (preprocess == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_UNSUPPORTED,
			"identifier origin preprocess is not implemented for dialect");
		return SQLPARSER_STATUS_UNSUPPORTED;
	}
	ops = sqlparser_dialect_get_ops(dialect);
	origins = NULL;
	status = sqlparser_identifier_origin_map_new_identity(
		strlen(input_sql),
		&origins,
		out_error);
	if (status != SQLPARSER_STATUS_OK) {
		return status;
	}
	status = preprocess(
		input_sql,
		limits,
		out_parser_sql,
		out_state,
		origins,
		out_error);
	if (status == SQLPARSER_STATUS_OK &&
	    (*out_parser_sql == NULL ||
	     sqlparser_identifier_origin_map_output_length(origins) !=
		     strlen(*out_parser_sql))) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"identifier origin output length does not match parser SQL");
		status = SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	if (status != SQLPARSER_STATUS_OK) {
		free(*out_parser_sql);
		*out_parser_sql = NULL;
		if (*out_state != NULL && ops != NULL &&
		    ops->destroy_state != NULL) {
			ops->destroy_state(*out_state);
		}
		*out_state = NULL;
		sqlparser_identifier_origin_map_destroy(origins);
		return status;
	}
	*out_origins = origins;
	return SQLPARSER_STATUS_OK;
}

size_t sqlparser_identifier_origin_map_output_length(
	const sqlparser_identifier_origin_map_t *map)
{
	return map != NULL ? map->output_length : 0U;
}

sqlparser_identifier_origin_kind_t sqlparser_identifier_origin_map_lookup(
	const sqlparser_identifier_origin_map_t *map,
	size_t parser_offset,
	size_t parser_length,
	sqlparser_identifier_origin_t *out_origin)
{
	const sqlparser_identifier_origin_run_t *run;
	sqlparser_identifier_origin_kind_t kind;
	size_t parser_end;
	size_t position;
	size_t run_index;
	size_t source_offset;
	size_t source_end;

	if (out_origin != NULL) {
		memset(out_origin, 0, sizeof(*out_origin));
	}
	if (map == NULL || parser_length == 0U ||
	    !sqlparser_identifier_origin_size_add(
		    parser_offset,
		    parser_length,
		    &parser_end) ||
	    parser_end > map->output_length) {
		return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
	}
	run_index = sqlparser_identifier_origin_first_run(map, parser_offset);
	if (run_index >= map->run_count) {
		return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
	}
	run = &map->runs[run_index];
	if (run->output_offset > parser_offset) {
		return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
	}
	if (run->kind ==
		    SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_IDENTIFIER ||
	    run->kind == SQLPARSER_IDENTIFIER_ORIGIN_RUN_GENERATED) {
		size_t run_end;
		sqlparser_identifier_origin_kind_t exact_kind;

		if (run->output_offset != parser_offset ||
		    !sqlparser_identifier_origin_size_add(
			    run->output_offset,
			    run->output_length,
			    &run_end) ||
		    run_end != parser_end) {
			return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
		}
		exact_kind =
			run->kind ==
					SQLPARSER_IDENTIFIER_ORIGIN_RUN_GENERATED ?
				SQLPARSER_IDENTIFIER_ORIGIN_GENERATED :
				SQLPARSER_IDENTIFIER_ORIGIN_SOURCE;
		if (out_origin != NULL) {
			out_origin->kind = exact_kind;
			if (exact_kind == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE) {
				out_origin->source_offset = run->source_offset;
				out_origin->source_length = run->source_length;
			}
		}
		return exact_kind;
	}

	kind = SQLPARSER_IDENTIFIER_ORIGIN_SOURCE;
	position = parser_offset;
	source_offset = 0U;
	source_end = 0U;
	while (position < parser_end) {
		size_t overlap_end;
		size_t run_end;

		if (run_index >= map->run_count) {
			return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
		}
		run = &map->runs[run_index];
		if (run->output_offset > position ||
		    !sqlparser_identifier_origin_size_add(
			    run->output_offset,
			    run->output_length,
			    &run_end) ||
		    run_end <= position ||
		    run->kind !=
			    SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_LINEAR) {
			return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
		}
		overlap_end = run_end < parser_end ? run_end : parser_end;
		if (kind == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE) {
			size_t overlap_source;

			if (!sqlparser_identifier_origin_size_add(
				    run->source_offset,
				    position - run->output_offset,
				    &overlap_source)) {
				return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
			}
			if (position == parser_offset) {
				source_offset = overlap_source;
			} else if (overlap_source != source_end) {
				return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
			}
			if (!sqlparser_identifier_origin_size_add(
				    overlap_source,
				    overlap_end - position,
				    &source_end)) {
				return SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN;
			}
		}
		position = overlap_end;
		run_index++;
	}
	if (out_origin != NULL) {
		out_origin->kind = kind;
		if (kind == SQLPARSER_IDENTIFIER_ORIGIN_SOURCE) {
			out_origin->source_offset = source_offset;
			out_origin->source_length = source_end - source_offset;
		}
	}
	return kind;
}

sqlparser_status_t sqlparser_identifier_origin_writer_begin(
	sqlparser_identifier_origin_writer_t *writer,
	sqlparser_identifier_origin_map_t *map,
	size_t input_length,
	sqlparser_error_t *out_error)
{
	if (writer == NULL || map == NULL) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin writer arguments must not be NULL");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (map->output_length != input_length) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"identifier origin input length does not match SQL");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	memset(writer, 0, sizeof(*writer));
	writer->map = map;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_identifier_origin_writer_append_input(
	sqlparser_identifier_origin_writer_t *writer,
	size_t input_offset,
	size_t length,
	sqlparser_error_t *out_error)
{
	const sqlparser_identifier_origin_map_t *map;
	size_t input_end;
	size_t output_end;
	size_t run_index;
	sqlparser_status_t status;

	if (writer == NULL || writer->map == NULL ||
	    !sqlparser_identifier_origin_size_add(
		    input_offset,
		    length,
		    &input_end) ||
	    input_end > writer->map->output_length ||
	    !sqlparser_identifier_origin_size_add(
		    writer->output_length,
		    length,
		    &output_end)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin input range is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	if (length == 0U) {
		return SQLPARSER_STATUS_OK;
	}
	map = writer->map;
	run_index = sqlparser_identifier_origin_first_run(map, input_offset);
	while (run_index < map->run_count) {
		const sqlparser_identifier_origin_run_t *input_run;
		sqlparser_identifier_origin_run_t output_run;
		size_t input_run_end;
		size_t overlap_start;
		size_t overlap_end;

		input_run = &map->runs[run_index];
		if (input_run->output_offset >= input_end) {
			break;
		}
		if (!sqlparser_identifier_origin_size_add(
			    input_run->output_offset,
			    input_run->output_length,
			    &input_run_end)) {
			sqlparser_error_set_message(
				out_error,
				SQLPARSER_STATUS_INTERNAL_ERROR,
				"identifier origin map is invalid");
			return SQLPARSER_STATUS_INTERNAL_ERROR;
		}
		overlap_start = input_run->output_offset > input_offset ?
			input_run->output_offset :
			input_offset;
		overlap_end = input_run_end < input_end ?
			input_run_end :
			input_end;
		if (overlap_start < overlap_end) {
			memset(&output_run, 0, sizeof(output_run));
			output_run.kind = input_run->kind;
			output_run.output_offset =
				writer->output_length + overlap_start - input_offset;
			output_run.output_length = overlap_end - overlap_start;
			if (input_run->kind ==
			    SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_LINEAR) {
				output_run.source_offset =
					input_run->source_offset +
					overlap_start -
					input_run->output_offset;
				output_run.source_length =
					output_run.output_length;
			} else if (input_run->kind ==
					   SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_IDENTIFIER ||
				   input_run->kind ==
					   SQLPARSER_IDENTIFIER_ORIGIN_RUN_GENERATED) {
				if (overlap_start != input_run->output_offset ||
				    overlap_end != input_run_end) {
					run_index++;
					continue;
				}
				output_run.source_offset =
					input_run->source_offset;
				output_run.source_length =
					input_run->source_length;
			}
			status = sqlparser_identifier_origin_writer_append_run(
				writer,
				&output_run,
				out_error);
			if (status != SQLPARSER_STATUS_OK) {
				return status;
			}
		}
		run_index++;
	}
	writer->output_length = output_end;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_identifier_origin_writer_append_unknown(
	sqlparser_identifier_origin_writer_t *writer,
	size_t length,
	sqlparser_error_t *out_error)
{
	size_t output_end;

	if (writer == NULL || writer->map == NULL ||
	    !sqlparser_identifier_origin_size_add(
		    writer->output_length,
		    length,
		    &output_end)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"identifier origin output range is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	writer->output_length = output_end;
	return SQLPARSER_STATUS_OK;
}

sqlparser_status_t sqlparser_identifier_origin_writer_append_generated_identifier(
	sqlparser_identifier_origin_writer_t *writer,
	size_t length,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_run_t run;
	sqlparser_status_t status;
	size_t output_end;

	if (writer == NULL || writer->map == NULL ||
	    !sqlparser_identifier_origin_size_add(
		    writer->output_length,
		    length,
		    &output_end)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"generated identifier origin range is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	memset(&run, 0, sizeof(run));
	run.kind = SQLPARSER_IDENTIFIER_ORIGIN_RUN_GENERATED;
	run.output_offset = writer->output_length;
	run.output_length = length;
	status = sqlparser_identifier_origin_writer_append_run(
		writer,
		&run,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		writer->output_length = output_end;
	}
	return status;
}

sqlparser_status_t sqlparser_identifier_origin_writer_append_source_identifier(
	sqlparser_identifier_origin_writer_t *writer,
	size_t input_offset,
	size_t input_length,
	size_t output_length,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_run_t run;
	sqlparser_identifier_origin_t origin;
	sqlparser_identifier_origin_kind_t kind;
	sqlparser_status_t status;
	size_t output_end;

	if (writer == NULL || writer->map == NULL ||
	    !sqlparser_identifier_origin_size_add(
		    writer->output_length,
		    output_length,
		    &output_end)) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INVALID_ARGUMENT,
			"source identifier origin range is invalid");
		return SQLPARSER_STATUS_INVALID_ARGUMENT;
	}
	kind = sqlparser_identifier_origin_map_lookup(
		writer->map,
		input_offset,
		input_length,
		&origin);
	if (kind == SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN) {
		writer->output_length = output_end;
		return SQLPARSER_STATUS_OK;
	}
	if (kind == SQLPARSER_IDENTIFIER_ORIGIN_GENERATED) {
		return sqlparser_identifier_origin_writer_append_generated_identifier(
			writer,
			output_length,
			out_error);
	}
	memset(&run, 0, sizeof(run));
	run.kind = SQLPARSER_IDENTIFIER_ORIGIN_RUN_SOURCE_IDENTIFIER;
	run.output_offset = writer->output_length;
	run.output_length = output_length;
	run.source_offset = origin.source_offset;
	run.source_length = origin.source_length;
	status = sqlparser_identifier_origin_writer_append_run(
		writer,
		&run,
		out_error);
	if (status == SQLPARSER_STATUS_OK) {
		writer->output_length = output_end;
	}
	return status;
}

sqlparser_status_t sqlparser_identifier_origin_writer_commit(
	sqlparser_identifier_origin_writer_t *writer,
	size_t output_length,
	sqlparser_error_t *out_error)
{
	sqlparser_identifier_origin_map_t *map;

	if (writer == NULL || writer->map == NULL ||
	    writer->output_length != output_length) {
		sqlparser_error_set_message(
			out_error,
			SQLPARSER_STATUS_INTERNAL_ERROR,
			"identifier origin output length does not match parser SQL");
		return SQLPARSER_STATUS_INTERNAL_ERROR;
	}
	map = writer->map;
	free(map->runs);
	map->runs = writer->runs;
	map->run_count = writer->run_count;
	map->output_length = writer->output_length;
	memset(writer, 0, sizeof(*writer));
	return SQLPARSER_STATUS_OK;
}

void sqlparser_identifier_origin_writer_release(
	sqlparser_identifier_origin_writer_t *writer)
{
	if (writer == NULL) {
		return;
	}
	free(writer->runs);
	memset(writer, 0, sizeof(*writer));
}
