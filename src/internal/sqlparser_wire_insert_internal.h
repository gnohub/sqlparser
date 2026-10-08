#ifndef SQLPARSER_WIRE_INSERT_INTERNAL_H
#define SQLPARSER_WIRE_INSERT_INTERNAL_H
#include "sqlparser_internal.h"

/* Private immutable description of a tightly certified, two-column VALUES
 * INSERT. Text slices borrow the handle's canonical owned wire. */
typedef struct {
    const char *text;
    uint32_t length;
    int32_t integer;
    int32_t location;
} sqlparser_wire_insert_cell_t;

typedef struct sqlparser_wire_insert {
    const char *wire;
    size_t wire_length;
    size_t row_count;
    size_t text_bytes; /* NUL-terminated table, columns, and all string cells. */
    uint32_t version;
    uint32_t raw_length;
    uint32_t prefix_offset;
    uint32_t prefix_length;
    const char *names[3]; /* table, column 0, column 1 */
    uint32_t name_lengths[3];
    uint32_t *row_offsets;
} sqlparser_wire_insert_t;

/* Silent, allocation-failure-safe eligibility miss. On success owns only its
 * descriptor and row index. No source/wire/handle state is changed. */
sqlparser_wire_insert_t *sqlparser_wire_insert_certify(const sqlparser_handle_t *handle);
void sqlparser_wire_insert_destroy(sqlparser_wire_insert_t *insert);
int sqlparser_wire_insert_row(const sqlparser_wire_insert_t *insert, size_t row,
    sqlparser_wire_insert_cell_t cells[2]);
/* This accessor accepts only a successful live immutable certificate. It
 * retains a row envelope bound, but does not revalidate proved inner tags. */
int sqlparser_wire_insert_certified_row(const sqlparser_wire_insert_t *insert, size_t row,
    sqlparser_wire_insert_cell_t cells[2]);
/* Defensive entry validates every replacement token as ordinary ASCII.
 * Computes fresh locations and canonical independently owned wire.
 * Failure never mutates old state. */
sqlparser_status_t sqlparser_wire_insert_pack(const sqlparser_wire_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out);
/* Private proof-reuse entry: the exact owned edits must have passed
 * sqlparser_patch_certify_insert_edits and remained immutable since then.
 * It skips only interior-byte validation, retaining every other pack guard. */
sqlparser_status_t sqlparser_wire_insert_pack_proven_edits(const sqlparser_wire_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out);

/* Graph integration owns descriptor and all graph-visible NUL strings. */
const sqlparser_wire_insert_t *sqlparser_query_graph_wire_insert(const sqlparser_handle_t *handle);
const char *sqlparser_query_graph_wire_string(const sqlparser_handle_t *handle, size_t row);
sqlparser_status_t sqlparser_handle_commit_certified_insert_wire(
    sqlparser_handle_t *handle, char **owned_sql, PgQueryProtobuf *owned_wire,
    sqlparser_error_t *out_error);

/* Separate mixed-scalar certificate. The two-column codec above deliberately
 * keeps its original admission and storage contract. Names borrow immutable
 * wire; SQL value-function spelling borrows the owned source. */
typedef enum {
    SQLPARSER_WIRE_SCALAR_INTEGER = 1,
    SQLPARSER_WIRE_SCALAR_STRING = 2,
    SQLPARSER_WIRE_SCALAR_FLOAT = 3,
    SQLPARSER_WIRE_SCALAR_VALUE_FUNCTION = 4
} sqlparser_wire_scalar_kind_t;

typedef struct {
    const char *text;
    uint32_t length;
    int32_t integer, location;
    sqlparser_wire_scalar_kind_t kind;
} sqlparser_wire_scalar_cell_t;

typedef struct {
    const char *text;
    uint32_t length;
} sqlparser_wire_scalar_name_t;

typedef struct sqlparser_wire_scalar_insert {
    const char *wire, *sql;
    size_t wire_length, sql_length, row_count, column_count;
    size_t text_bytes, string_count;
    uint32_t version, raw_length, prefix_offset, prefix_length;
    uint32_t *row_offsets;
    /* catalog, schema, relation, followed by the explicit target columns */
    sqlparser_wire_scalar_name_t *names;
} sqlparser_wire_scalar_insert_t;

sqlparser_wire_scalar_insert_t *sqlparser_wire_scalar_insert_certify(const sqlparser_handle_t *handle);
/* Only exact immutable initial-native provenance can enter this constructor.
 * The independent strict certifier above never consumes provenance. */
sqlparser_wire_scalar_insert_t *sqlparser_wire_scalar_insert_from_native(const sqlparser_handle_t *handle);
void sqlparser_wire_scalar_insert_destroy(sqlparser_wire_scalar_insert_t *insert);
int sqlparser_wire_scalar_insert_row(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, sqlparser_wire_scalar_cell_t *cells);
int sqlparser_wire_scalar_insert_cell(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, size_t column, sqlparser_wire_scalar_cell_t *cell);
/* Proof-reuse accessors: only live successful immutable certificates. All
 * outer row, varint and payload bounds remain enforced. */
int sqlparser_wire_scalar_insert_certified_row(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, sqlparser_wire_scalar_cell_t *cells);
int sqlparser_wire_scalar_insert_certified_cell(const sqlparser_wire_scalar_insert_t *insert,
    size_t row, size_t column, sqlparser_wire_scalar_cell_t *cell);
sqlparser_status_t sqlparser_wire_scalar_insert_pack(const sqlparser_wire_scalar_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out);
/* The live certificate and exact owned edit tokens must remain immutable
 * after sqlparser_patch_certify_insert_edits; reuses both independent proofs. */
sqlparser_status_t sqlparser_wire_scalar_insert_pack_proven_edits(const sqlparser_wire_scalar_insert_t *insert,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out);
const sqlparser_wire_scalar_insert_t *sqlparser_query_graph_wire_scalar_insert(const sqlparser_handle_t *handle);
const char *sqlparser_query_graph_wire_scalar_string(const sqlparser_handle_t *handle, size_t row, size_t column);

/* Whole-owner batch certificate. Every embedded statement still addresses the
 * full original source and full ParseResult wire, never a sliced fake handle. */
typedef struct {
    sqlparser_wire_scalar_insert_t insert;
    uint32_t raw_location, relation_location;
    uint32_t raw_wire_offset, raw_wire_length;
    uint32_t *column_locations;
    size_t cell_offset, column_offset, row_offset;
} sqlparser_wire_scalar_batch_statement_t;

typedef struct sqlparser_wire_scalar_batch {
    const sqlparser_handle_t *owner;
    const sqlparser_dialect_ops_t *ops;
    sqlparser_dialect_t dialect;
    unsigned long generation;
    const char *sql, *parser_sql, *wire;
    size_t sql_length, parser_sql_length, wire_length, statement_count;
    size_t row_count, column_count, cell_count, string_count, text_bytes;
    uint32_t version;
    sqlparser_wire_scalar_batch_statement_t *statements;
} sqlparser_wire_scalar_batch_t;

sqlparser_wire_scalar_batch_t *sqlparser_wire_scalar_batch_certify(const sqlparser_handle_t *handle);
void sqlparser_wire_scalar_batch_destroy(sqlparser_wire_scalar_batch_t *batch);
int sqlparser_wire_scalar_batch_is_current(const sqlparser_wire_scalar_batch_t *batch,
    const sqlparser_handle_t *handle);
int sqlparser_wire_scalar_batch_certified_cell(const sqlparser_wire_scalar_batch_t *batch,
    size_t statement, size_t row, size_t column, sqlparser_wire_scalar_cell_t *cell);
sqlparser_status_t sqlparser_wire_scalar_batch_pack_proven_edits(const sqlparser_wire_scalar_batch_t *batch,
    const sqlparser_surface_source_edits_t *edits, PgQueryProtobuf *out);
const sqlparser_wire_scalar_batch_t *sqlparser_query_graph_wire_scalar_batch(const sqlparser_handle_t *handle);
const char *sqlparser_query_graph_wire_scalar_batch_string(const sqlparser_handle_t *handle,
    size_t statement, size_t row, size_t column);
sqlparser_status_t sqlparser_handle_commit_certified_insert_batch_wire(
    sqlparser_handle_t *handle, char **owned_sql, PgQueryProtobuf *owned_wire,
    size_t statement_count, sqlparser_error_t *out_error);
#endif
