#ifndef SQLPARSER_DIALECT_MULTI_INSERT_TYPES_H
#define SQLPARSER_DIALECT_MULTI_INSERT_TYPES_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "sqlparser/sqlparser.h"

typedef enum {
	SQLPARSER_DIALECT_MULTI_INSERT_NONE = 0,
	SQLPARSER_DIALECT_MULTI_INSERT_ALL = 1,
	SQLPARSER_DIALECT_MULTI_INSERT_FIRST = 2
} sqlparser_dialect_multi_insert_mode_t;

typedef struct {
	char *database_name;
	char *schema_name;
	char *table_name;
	char *link_name;
	char *link_sql;
	char *sql;
} sqlparser_dialect_multi_insert_relation_t;

typedef struct {
	char *name;
	char *sql;
} sqlparser_dialect_multi_insert_column_t;

typedef struct {
	char *public_sql;
	char *parser_sql;
	int has_bind;
	sqlparser_bind_kind_t bind_kind;
	char bind[SQLPARSER_BIND_TEXT_CAPACITY];
	char bind_sql[SQLPARSER_BIND_SQL_CAPACITY];
	size_t bind_position;
	int has_bind_position;
	int has_literal;
	sqlparser_literal_view_t literal;
	char *literal_string_value;
	char *literal_float_value;
} sqlparser_dialect_multi_insert_value_t;

/* Oracle-only identity cells own one allocation, rooted at public_sql.
 * parser_sql and an optional literal text are distinct NUL-terminated slices. */
typedef struct {
	char *public_sql;
	char *parser_sql;
	sqlparser_literal_view_t literal;
	int has_literal;
} sqlparser_oracle_compact_value_t;

#if UINTPTR_MAX == UINT64_MAX && LONG_MAX == INT64_MAX
_Static_assert(sizeof(sqlparser_oracle_compact_value_t) == 64U,
	"Oracle compact cells must retain the complete 64-byte record");
#endif

enum {
	SQLPARSER_ORACLE_CELL_STORAGE_LEGACY = 0,
	SQLPARSER_ORACLE_CELL_STORAGE_COMPACT = 1
};

typedef struct {
	size_t ordinal;
	sqlparser_dialect_multi_insert_relation_t relation;
	sqlparser_dialect_multi_insert_column_t *columns;
	size_t column_count;
	/* Oracle-only exclusive header text owner; NULL means legacy field owners.
	 * Independent of cells and optional lexical/graph proofs. */
	char *oracle_header_text_block;
	sqlparser_dialect_multi_insert_value_t *cells;
	size_t cell_count;
	/* Required ownership metadata; independent of every optional proof. */
	sqlparser_oracle_compact_value_t *oracle_compact_cells;
	int cell_storage;
	char *condition_public_sql;
	char *condition_parser_sql;
	int has_condition;
	int is_else;
	size_t condition_group_id;
	uint32_t oracle_span_base;
	uint32_t oracle_values_position;
} sqlparser_dialect_multi_insert_branch_t;

typedef struct {
	uint32_t source_start;
	uint32_t source_length;
	uint32_t lexical_flags;
} sqlparser_oracle_cell_span_t;

enum {
	SQLPARSER_ORACLE_CELL_ORDINARY_STRING = 1U,
	SQLPARSER_ORACLE_CELL_IDENTITY = 2U
};

enum {
	SQLPARSER_ORACLE_GRAPH_HEADER_SIMPLE_ASCII_ALL = 1U
};

typedef struct {
	sqlparser_dialect_multi_insert_mode_t mode;
	/* Fresh-parse lexical facts for every owned relation/column header.
	 * Independent of cell spans and source/parser identity; zero is unknown. */
	uint32_t oracle_graph_header_flags;
	sqlparser_dialect_multi_insert_branch_t *branches;
	size_t branch_count;
	/* Private constructor storage; only branch_count entries own values. */
	size_t branch_capacity;
	char *source_public_sql;
	char *source_parser_sql;
	/* Optional constructor facts, allocated only by the Oracle constructor.
	 * IDs are stable indices; no caller, graph or cell-allocation pointers. */
	sqlparser_oracle_cell_span_t *oracle_spans;
	size_t oracle_span_count;
	size_t oracle_span_capacity;
	uint32_t oracle_source_start;
	uint32_t oracle_source_length;
	int oracle_spans_complete;
	int oracle_spans_identity;
	int oracle_outer_identity;
	/* Complete constructor proof of the public statement/cell scanners.
	 * Fresh parse/clone only; every mutation invalidates it. */
	int oracle_generic_spans_equivalent;
	uint32_t *oracle_pending_ids;
	size_t oracle_pending_count;
	size_t oracle_pending_capacity;
	int oracle_pending_disabled;
	/* Oracle-only, issued after parse or a certified branch-string commit.
	 * Kept here so ordinary handles and SELECT state pay no storage cost. */
	struct {
		const char *sql;
		const char *parser_sql;
		const char *wire;
		const void *state;
		size_t sql_length;
		size_t parser_sql_length;
		size_t wire_length;
		unsigned long generation;
	} oracle_source_provenance;
} sqlparser_dialect_multi_insert_t;

#endif
