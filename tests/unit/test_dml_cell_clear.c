#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlparser/sqlparser.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		goto fail; \
	} \
} while (0)

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

static int check_case(sqlparser_dialect_t dialect, const char *sql)
{
	static const unsigned char patterns[] = {0xA5U, 0x3CU, 0xFFU};
	const size_t alignment = _Alignof(sqlparser_graph_dml_cell_t);
	const size_t guard = 16U * alignment;
	const size_t size = sizeof(sqlparser_graph_dml_cell_t) + 3U * guard;
	unsigned char *storage = NULL;
	sqlparser_handle_t *handle = NULL;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_query_graph_view_t graph;
	sqlparser_graph_dml_t dml;
	sqlparser_graph_dml_cell_t expected;
	size_t index;
	size_t cell_index;
	size_t offset;
	size_t pattern;

	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	CHECK(sqlparser_parse_with_options(sql, &options, &handle, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_statement_query_graph(handle, 0U, &graph, &error) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_query_graph_dml(&graph, &dml, &error) == SQLPARSER_STATUS_OK);
	CHECK(dml.rows.count > 0U);
	storage = (unsigned char *)malloc(size);
	CHECK(storage != NULL);

	for (index = 0U; index < dml.rows.count; index++) {
		CHECK(sqlparser_query_graph_span_index_at(&graph, dml.rows, index,
			&cell_index, &error) == SQLPARSER_STATUS_OK);
		memset(&expected, 0, sizeof(expected));
		CHECK(sqlparser_query_graph_dml_cell_at(&graph, cell_index,
			&expected, &error) == SQLPARSER_STATUS_OK);
		for (offset = 0U; offset < guard; offset += alignment) {
			for (pattern = 0U; pattern < sizeof(patterns); pattern++) {
				const size_t prefix = guard + offset;
				unsigned char *bytes = storage + prefix;
				sqlparser_graph_dml_cell_t *cell = (sqlparser_graph_dml_cell_t *)(void *)bytes;

				memset(storage, patterns[pattern], size);
				memset(&error, patterns[pattern], sizeof(error));
				CHECK(sqlparser_query_graph_dml_cell_at(&graph, cell_index,
					cell, &error) == SQLPARSER_STATUS_OK);
				CHECK(error_matches(&error, SQLPARSER_STATUS_OK, NULL));
				/* Same borrowed pointers, all inactive fields and every padding byte. */
				CHECK(memcmp(cell, &expected, sizeof(expected)) == 0);
				CHECK(bytes_are(storage, prefix, patterns[pattern]));
				CHECK(bytes_are(bytes + sizeof(*cell), size - prefix - sizeof(*cell), patterns[pattern]));

				memset(cell, patterns[pattern], sizeof(*cell));
				CHECK(sqlparser_query_graph_dml_cell_at(&graph, cell_index,
					cell, NULL) == SQLPARSER_STATUS_OK);
				CHECK(memcmp(cell, &expected, sizeof(expected)) == 0);
				CHECK(bytes_are(storage, prefix, patterns[pattern]));
				CHECK(bytes_are(bytes + sizeof(*cell), size - prefix - sizeof(*cell), patterns[pattern]));

				memset(cell, patterns[pattern], sizeof(*cell));
				CHECK(sqlparser_query_graph_dml_cell_at(&graph, SIZE_MAX,
					cell, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
				CHECK(error_matches(&error, SQLPARSER_STATUS_INVALID_ARGUMENT,
					"dml cell index is out of range"));
				CHECK(bytes_are(cell, sizeof(*cell), 0U));
				CHECK(bytes_are(storage, prefix, patterns[pattern]));
				CHECK(bytes_are(bytes + sizeof(*cell), size - prefix - sizeof(*cell), patterns[pattern]));
			}
		}
	}
	/* The output-pointer error retains priority over invalid graph/index errors. */
	memset(&error, 0xA5, sizeof(error));
	CHECK(sqlparser_query_graph_dml_cell_at(NULL, SIZE_MAX, NULL,
		&error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(error_matches(&error, SQLPARSER_STATUS_INVALID_ARGUMENT,
		"out_cell must not be NULL"));
	CHECK(sqlparser_query_graph_dml_cell_at(NULL, SIZE_MAX, NULL,
		NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	free(storage);
	sqlparser_handle_destroy(handle);
	return 0;
fail:
	free(storage);
	sqlparser_handle_destroy(handle);
	return 1;
}

int main(void)
{
	int failures = 0;

	failures += check_case(SQLPARSER_DIALECT_POSTGRESQL,
		"INSERT INTO t(a,b,c,d,e,f,g,h) VALUES (1,'text',true,NULL,1.25,DEFAULT,$1,abs(2))");
	failures += check_case(SQLPARSER_DIALECT_MYSQL,
		"INSERT INTO t(a,b,c,d,e,f,g,h) VALUES (1,'text',true,NULL,1.25,DEFAULT,?,abs(2))");
	if (failures != 0) return 1;
	puts("DML cell full-byte clearing, alignment, guards and errors passed");
	return 0;
}
