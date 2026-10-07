#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"
#include "sqlparser_internal.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		goto fail; \
	} \
} while (0)

/* Compare the complete object, including unused message bytes and padding. */
static int bytes_are(const void *object, size_t size, unsigned char value)
{
	const unsigned char *bytes = (const unsigned char *)object;
	size_t index;

	for (index = 0U; index < size; index++) {
		if (bytes[index] != value) return 0;
	}
	return 1;
}

static int error_matches(const sqlparser_error_t *error,
	sqlparser_status_t code, const char *message)
{
	sqlparser_error_t expected;

	memset(&expected, 0, sizeof(expected));
	expected.code = code;
	if (message != NULL) {
		(void)snprintf(expected.message, sizeof(expected.message), "%s", message);
	}
	return memcmp(error, &expected, sizeof(expected)) == 0;
}

static int check_bytes_and_alignment(void)
{
	static const unsigned char patterns[] = {0xA5U, 0x3CU, 0xFFU};
	const size_t alignment = _Alignof(sqlparser_error_t);
	const size_t guard = 16U * alignment;
	const size_t size = sizeof(sqlparser_error_t) + 3U * guard;
	unsigned char *storage = NULL;
	size_t offset;
	size_t pattern;

	storage = (unsigned char *)malloc(size);
	CHECK(storage != NULL);
	sqlparser_error_clear(NULL);
	for (offset = 0U; offset < guard; offset += alignment) {
		for (pattern = 0U; pattern < sizeof(patterns); pattern++) {
			unsigned char *bytes = storage + guard + offset;
			sqlparser_error_t *error = (sqlparser_error_t *)(void *)bytes;
			size_t prefix = guard + offset;

			memset(storage, patterns[pattern], size);
			sqlparser_error_clear(error);
			CHECK(bytes_are(storage, prefix, patterns[pattern]));
			CHECK(bytes_are(error, sizeof(*error), 0U));
			CHECK(bytes_are(bytes + sizeof(*error),
				size - prefix - sizeof(*error), patterns[pattern]));
			/* Clearing an already clear object must retain the same bytes. */
			sqlparser_error_clear(error);
			CHECK(bytes_are(error, sizeof(*error), 0U));
			CHECK(bytes_are(storage, prefix, patterns[pattern]));
			CHECK(bytes_are(bytes + sizeof(*error),
				size - prefix - sizeof(*error), patterns[pattern]));
		}
	}
	free(storage);
	return 0;
fail:
	free(storage);
	return 1;
}

#define EXPECT(call, code, message) do { \
	memset(&error, 0xA5, sizeof(error)); \
	CHECK((call) == (code)); \
	CHECK(error_matches(&error, (code), (message))); \
} while (0)

static int check_graph_errors(sqlparser_dialect_t dialect)
{
	sqlparser_handle_t *handle = NULL;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_query_graph_view_t stale;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t cell;
	sqlparser_graph_dml_column_t column;
	sqlparser_graph_relation_t relation;
	sqlparser_index_span_t bad_span;
	sqlparser_patch_t patch;
	sqlparser_patch_list_t patches;
	size_t index = SIZE_MAX;

	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	EXPECT(sqlparser_parse_with_options("INSERT INTO t(a,b) VALUES (1,'text')",
		&options, &handle, &error), SQLPARSER_STATUS_OK, NULL);
	EXPECT(sqlparser_statement_query_graph(handle, 0U, &graph, &error),
		SQLPARSER_STATUS_OK, NULL);
	EXPECT(sqlparser_query_graph_dml(&graph, &dml, &error), SQLPARSER_STATUS_OK, NULL);
	EXPECT(sqlparser_query_graph_span_index_at(&graph, dml.rows, 0U, &index, &error),
		SQLPARSER_STATUS_OK, NULL);
	EXPECT(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, &error),
		SQLPARSER_STATUS_OK, NULL);
	CHECK(cell.literal.integer_value == 1LL);
	EXPECT(sqlparser_query_graph_relation_at(&graph, dml.target_relation_index,
		&relation, &error), SQLPARSER_STATUS_OK, NULL);
	EXPECT(sqlparser_query_graph_dml_column_at(&graph, 0U, &column, &error),
		SQLPARSER_STATUS_OK, NULL);

	/* Output-argument failures retain precedence even with an invalid graph. */
	EXPECT(sqlparser_statement_query_graph(NULL, SIZE_MAX, NULL, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "out_graph must not be NULL");
	EXPECT(sqlparser_query_graph_span_index_at(NULL, dml.rows, SIZE_MAX, NULL, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "out_index must not be NULL");
	EXPECT(sqlparser_query_graph_dml_cell_at(NULL, SIZE_MAX, NULL, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "out_cell must not be NULL");
	EXPECT(sqlparser_query_graph_relation_at(NULL, SIZE_MAX, NULL, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "out_relation must not be NULL");
	EXPECT(sqlparser_query_graph_dml_column_at(NULL, SIZE_MAX, NULL, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "out_column must not be NULL");

	memset(&cell, 0xA5, sizeof(cell));
	EXPECT(sqlparser_query_graph_dml_cell_at(&graph, SIZE_MAX, &cell, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "dml cell index is out of range");
	CHECK(bytes_are(&cell, sizeof(cell), 0U));
	index = SIZE_MAX;
	EXPECT(sqlparser_query_graph_span_index_at(&graph, dml.rows, SIZE_MAX, &index, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "span index is out of range");
	CHECK(index == 0U);
	bad_span.offset = SIZE_MAX;
	bad_span.count = 1U;
	EXPECT(sqlparser_query_graph_span_index_at(&graph, bad_span, 0U, &index, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "span index is out of range");
	CHECK(index == 0U);
	EXPECT(sqlparser_query_graph_span_index_at(NULL, dml.rows, 0U, &index, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "span index is out of range");
	CHECK(index == 0U);
	memset(&cell, 0xA5, sizeof(cell));
	EXPECT(sqlparser_query_graph_dml_cell_at(NULL, 0U, &cell, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "dml cell index is out of range");
	CHECK(bytes_are(&cell, sizeof(cell), 0U));

	/* The optional error pointer does not affect status or output clearing. */
	CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, 0U,
		&index, NULL) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml_cell_at(&graph, index, &cell, NULL) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_span_index_at(NULL, dml.rows, 0U,
		NULL, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(sqlparser_query_graph_dml_cell_at(NULL, 0U, NULL, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	index = SIZE_MAX;
	CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, SIZE_MAX,
		&index, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(index == 0U);
	memset(&cell, 0xA5, sizeof(cell));
	CHECK(sqlparser_query_graph_dml_cell_at(&graph, SIZE_MAX, &cell,
		NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(bytes_are(&cell, sizeof(cell), 0U));

	stale = graph;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].insert_cell[0][0]";
	patch.sql = "7";
	patches.items = &patch;
	patches.count = 1U;
	EXPECT(sqlparser_apply_patch(handle, &patches, &error), SQLPARSER_STATUS_OK, NULL);
	index = SIZE_MAX;
	EXPECT(sqlparser_query_graph_span_index_at(&stale, dml.rows, 0U, &index, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "span index is out of range");
	CHECK(index == 0U);
	memset(&cell, 0xA5, sizeof(cell));
	EXPECT(sqlparser_query_graph_dml_cell_at(&stale, 0U, &cell, &error),
		SQLPARSER_STATUS_INVALID_ARGUMENT, "dml cell index is out of range");
	CHECK(bytes_are(&cell, sizeof(cell), 0U));
	CHECK(sqlparser_query_graph_dml_cell_at(&stale, 0U, &cell, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(bytes_are(&cell, sizeof(cell), 0U));
	EXPECT(sqlparser_statement_query_graph(handle, 0U, &graph, &error), SQLPARSER_STATUS_OK, NULL);
	EXPECT(sqlparser_query_graph_dml_cell_at(&graph, 0U, &cell, &error), SQLPARSER_STATUS_OK, NULL);
	CHECK(cell.literal.integer_value == 7LL);

	sqlparser_handle_destroy(handle);
	return 0;
fail:
	sqlparser_handle_destroy(handle);
	return 1;
}

int main(void)
{
	if (check_bytes_and_alignment() != 0 ||
	    check_graph_errors(SQLPARSER_DIALECT_POSTGRESQL) != 0 ||
	    check_graph_errors(SQLPARSER_DIALECT_MYSQL) != 0) return 1;
	puts("Error clear bytes, guards, alignments, and graph error semantics passed");
	return 0;
}
