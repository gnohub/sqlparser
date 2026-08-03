#ifndef SQLPARSER_IDENTIFIER_ORIGIN_INTERNAL_H
#define SQLPARSER_IDENTIFIER_ORIGIN_INTERNAL_H

#include <stddef.h>

#include "sqlparser/sqlparser.h"

typedef enum {
	SQLPARSER_IDENTIFIER_ORIGIN_UNKNOWN = 0,
	SQLPARSER_IDENTIFIER_ORIGIN_SOURCE = 1,
	SQLPARSER_IDENTIFIER_ORIGIN_GENERATED = 2
} sqlparser_identifier_origin_kind_t;

typedef struct {
	sqlparser_identifier_origin_kind_t kind;
	size_t source_offset;
	size_t source_length;
} sqlparser_identifier_origin_t;

typedef struct sqlparser_identifier_origin_map sqlparser_identifier_origin_map_t;
typedef struct sqlparser_identifier_origin_run sqlparser_identifier_origin_run_t;

typedef struct {
	sqlparser_identifier_origin_map_t *map;
	sqlparser_identifier_origin_run_t *runs;
	size_t run_count;
	size_t run_capacity;
	size_t output_length;
} sqlparser_identifier_origin_writer_t;

sqlparser_status_t sqlparser_identifier_origin_map_new_identity(
	size_t input_length,
	sqlparser_identifier_origin_map_t **out_map,
	sqlparser_error_t *out_error);

void sqlparser_identifier_origin_map_destroy(
	sqlparser_identifier_origin_map_t *map);

sqlparser_status_t sqlparser_dialect_preprocess_identifier_origins(
	sqlparser_dialect_t dialect,
	const char *input_sql,
	const sqlparser_limits_t *limits,
	char **out_parser_sql,
	void **out_state,
	sqlparser_identifier_origin_map_t **out_origins,
	sqlparser_error_t *out_error);

size_t sqlparser_identifier_origin_map_output_length(
	const sqlparser_identifier_origin_map_t *map);

/*
 * SOURCE proves byte provenance only. Callers validating identifier spelling
 * must also require the returned source span to be one complete raw identifier
 * token before accepting it.
 */
sqlparser_identifier_origin_kind_t sqlparser_identifier_origin_map_lookup(
	const sqlparser_identifier_origin_map_t *map,
	size_t parser_offset,
	size_t parser_length,
	sqlparser_identifier_origin_t *out_origin);

sqlparser_status_t sqlparser_identifier_origin_writer_begin(
	sqlparser_identifier_origin_writer_t *writer,
	sqlparser_identifier_origin_map_t *map,
	size_t input_length,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_identifier_origin_writer_append_input(
	sqlparser_identifier_origin_writer_t *writer,
	size_t input_offset,
	size_t length,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_identifier_origin_writer_append_unknown(
	sqlparser_identifier_origin_writer_t *writer,
	size_t length,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_identifier_origin_writer_append_generated_identifier(
	sqlparser_identifier_origin_writer_t *writer,
	size_t length,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_identifier_origin_writer_append_source_identifier(
	sqlparser_identifier_origin_writer_t *writer,
	size_t input_offset,
	size_t input_length,
	size_t output_length,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_identifier_origin_writer_append_map(
	sqlparser_identifier_origin_writer_t *writer,
	const sqlparser_identifier_origin_map_t *input_map,
	size_t input_offset,
	sqlparser_error_t *out_error);

sqlparser_status_t sqlparser_identifier_origin_writer_commit(
	sqlparser_identifier_origin_writer_t *writer,
	size_t output_length,
	sqlparser_error_t *out_error);

void sqlparser_identifier_origin_writer_release(
	sqlparser_identifier_origin_writer_t *writer);

#endif
