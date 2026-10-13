/* Regression coverage for render scaffold. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <pthread.h>
#endif

#include "../../src/core/sqlparser_ast_internal.h"
#include "../../src/core/sqlparser_render_scaffold.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "render scaffold:%d: %s\n", __LINE__, #condition); \
		exit(1); \
	} \
} while (0)

typedef struct {
	sqlparser_render_scaffold_kind_t kind;
	const char *sql;
	const char *replacement_sql;
} fixture_t;

static const fixture_t fixtures[] = {
	{ SQLPARSER_RENDER_SCAFFOLD_SELECT_TARGET,
	  "SELECT NULL FROM __sqlparser_source__",
	  "SELECT coalesce(a, 2) AS output FROM __sqlparser_source__" },
	{ SQLPARSER_RENDER_SCAFFOLD_WHERE,
	  "SELECT 1 FROM __sqlparser_source__ WHERE TRUE",
	  "SELECT 1 FROM __sqlparser_source__ WHERE a = 2 AND b IS NOT NULL" },
	{ SQLPARSER_RENDER_SCAFFOLD_ORDER_BY,
	  "SELECT 1 FROM __sqlparser_source__ ORDER BY 1",
	  "SELECT 1 FROM __sqlparser_source__ ORDER BY coalesce(a, 2) DESC NULLS LAST" },
	{ SQLPARSER_RENDER_SCAFFOLD_INSERT_CELL,
	  "INSERT INTO __sqlparser_target__ VALUES (NULL)",
	  "INSERT INTO __sqlparser_target__ VALUES ((SELECT 2))" },
	{ SQLPARSER_RENDER_SCAFFOLD_UPDATE_ASSIGNMENT,
	  "UPDATE __sqlparser_target__ SET __sqlparser_column__ = NULL",
	  "UPDATE __sqlparser_target__ SET __sqlparser_column__ = 10 + 2" },
	{ SQLPARSER_RENDER_SCAFFOLD_VARIABLE_SET_ARG,
	  "SET __sqlparser_session_parameter__ = __sqlparser_placeholder__",
	  "SET __sqlparser_session_parameter__ = 'read committed'" }
};

typedef struct {
	size_t size;
	unsigned char bytes[1024];
	char sql[256];
} expected_t;

static void check_packed(const PgQuery__ParseResult *ast, const expected_t *expected)
{
	unsigned char bytes[1024];
	size_t size = pg_query__parse_result__get_packed_size(ast);
	CHECK(size == expected->size && size <= sizeof(bytes));
	CHECK(pg_query__parse_result__pack(ast, bytes) == size);
	CHECK(memcmp(bytes, expected->bytes, size) == 0);
}

/* Check every active message, repeated array and nondefault string resides in
 * this instance, excluding immutable protobuf descriptors/default strings.
 * This also catches accidentally returning a pointer to a helper-local node. */
static int contained(const sqlparser_render_scaffold_t *scaffold,
	const void *pointer, size_t size)
{
	uintptr_t start = (uintptr_t)scaffold;
	uintptr_t value = (uintptr_t)pointer;
	return value >= start && value - start <= sizeof(*scaffold) &&
	       size <= sizeof(*scaffold) - (size_t)(value - start);
}

static void check_storage(const sqlparser_render_scaffold_t *scaffold,
	const ProtobufCMessage *message)
{
	const char *base = (const char *)message;
	unsigned i;
	CHECK(message != NULL && message->descriptor != NULL);
	CHECK(contained(scaffold, message, message->descriptor->sizeof_message));
	CHECK(message->n_unknown_fields == 0U && message->unknown_fields == NULL);
	for (i = 0U; i < message->descriptor->n_fields; ++i) {
		const ProtobufCFieldDescriptor *field = &message->descriptor->fields[i];
		if ((field->flags & PROTOBUF_C_FIELD_FLAG_ONEOF) != 0U &&
		    *(const int *)(base + field->quantifier_offset) != (int)field->id) {
			continue;
		}
		if (field->type == PROTOBUF_C_TYPE_MESSAGE) {
			if (field->label == PROTOBUF_C_LABEL_REPEATED) {
				ProtobufCMessage *const *items =
					*(ProtobufCMessage *const *const *)(base + field->offset);
				size_t count = *(const size_t *)(base + field->quantifier_offset);
				size_t j;
				if (count == 0U) {
					CHECK(items == NULL);
					continue;
				}
				CHECK(contained(scaffold, items, count * sizeof(*items)));
				for (j = 0U; j < count; ++j) check_storage(scaffold, items[j]);
			} else {
				const ProtobufCMessage *child =
					*(ProtobufCMessage *const *)(base + field->offset);
				if (child != NULL) check_storage(scaffold, child);
			}
		} else if (field->type == PROTOBUF_C_TYPE_STRING) {
			const char *value = *(char *const *)(base + field->offset);
			CHECK(value != NULL);
			if (value != protobuf_c_empty_string) {
				CHECK(contained(scaffold, value, 1U));
				CHECK(contained(scaffold, value, strlen(value) + 1U));
			}
		}
	}
}

static void check_deparse(const PgQuery__ParseResult *ast, const char *expected)
{
	PgQueryDeparseResult result = pg_query_deparse_ast(ast);
	CHECK(result.error == NULL && result.query != NULL);
	CHECK(strcmp(result.query, expected) == 0);
	pg_query_free_deparse_result(result);
}

static void make_expected(expected_t expected[COUNT(fixtures)])
{
	size_t i;
	for (i = 0U; i < COUNT(fixtures); ++i) {
		PgQuery__ParseResult *parsed = NULL;
		PgQueryDeparseResult direct, packed;
		PgQueryProtobuf buffer;
		sqlparser_error_t error;
		CHECK(sqlparser_parse_wrapper_ast(fixtures[i].sql, &parsed, &error) == SQLPARSER_STATUS_OK);
		CHECK(parsed != NULL && parsed->version == PG_VERSION_NUM);
		expected[i].size = pg_query__parse_result__get_packed_size(parsed);
		CHECK(expected[i].size <= sizeof(expected[i].bytes));
		CHECK(pg_query__parse_result__pack(parsed, expected[i].bytes) == expected[i].size);
		direct = pg_query_deparse_ast(parsed);
		CHECK(direct.error == NULL && direct.query != NULL);
		CHECK(strlen(direct.query) < sizeof(expected[i].sql));
		strcpy(expected[i].sql, direct.query);
		buffer.len = expected[i].size;
		buffer.data = (char *)expected[i].bytes;
		/* Independent packed-input native-tree conversion/deparse route. */
		packed = pg_query_deparse_protobuf(buffer);
		CHECK(packed.error == NULL && packed.query != NULL);
		CHECK(strcmp(direct.query, packed.query) == 0);
		pg_query_free_deparse_result(packed);
		pg_query_free_deparse_result(direct);
		pg_query__parse_result__free_unpacked(parsed, NULL);
	}
}

static void check_simultaneous(const expected_t expected[COUNT(fixtures)])
{
	sqlparser_render_scaffold_t scaffolds[COUNT(fixtures)];
	size_t i, round;
	for (i = 0U; i < COUNT(fixtures); ++i) {
		CHECK(sqlparser_render_scaffold_init(&scaffolds[i], fixtures[i].kind, NULL) == SQLPARSER_STATUS_OK);
	}
	for (round = 0U; round < COUNT(fixtures); ++round) {
		for (i = 0U; i < COUNT(fixtures); ++i) {
			size_t index = (i + round) % COUNT(fixtures);
			check_storage(&scaffolds[i], &scaffolds[i].ast.base);
			check_packed(&scaffolds[i].ast, &expected[index]);
			check_deparse(&scaffolds[i].ast, expected[index].sql);
			check_packed(&scaffolds[i].ast, &expected[index]);
		}
		for (i = 0U; i < COUNT(fixtures); ++i) {
			size_t index = (i + round + 1U) % COUNT(fixtures);
			CHECK(sqlparser_render_scaffold_init(&scaffolds[i], fixtures[index].kind, NULL) == SQLPARSER_STATUS_OK);
		}
	}
}

static void check_nested(const expected_t expected[COUNT(fixtures)], size_t depth)
{
	sqlparser_render_scaffold_t scaffold;
	size_t index = depth % COUNT(fixtures);
	CHECK(sqlparser_render_scaffold_init(&scaffold, fixtures[index].kind, NULL) == SQLPARSER_STATUS_OK);
	check_storage(&scaffold, &scaffold.ast.base);
	check_packed(&scaffold.ast, &expected[index]);
	if (depth > 0U) check_nested(expected, depth - 1U);
	check_deparse(&scaffold.ast, expected[index].sql);
	check_packed(&scaffold.ast, &expected[index]);
}

static PgQuery__Node **replacement_slot(PgQuery__ParseResult *ast,
	sqlparser_render_scaffold_kind_t kind)
{
	PgQuery__Node *statement = ast->stmts[0]->stmt;
	switch (kind) {
	case SQLPARSER_RENDER_SCAFFOLD_SELECT_TARGET:
		return &statement->select_stmt->target_list[0];
	case SQLPARSER_RENDER_SCAFFOLD_WHERE:
		return &statement->select_stmt->where_clause;
	case SQLPARSER_RENDER_SCAFFOLD_ORDER_BY:
		return &statement->select_stmt->sort_clause[0];
	case SQLPARSER_RENDER_SCAFFOLD_INSERT_CELL:
		return &statement->insert_stmt->select_stmt->select_stmt->values_lists[0]->list->items[0];
	case SQLPARSER_RENDER_SCAFFOLD_UPDATE_ASSIGNMENT:
		return &statement->update_stmt->target_list[0]->res_target->val;
	case SQLPARSER_RENDER_SCAFFOLD_VARIABLE_SET_ARG:
		return &statement->variable_set_stmt->args[0];
	default:
		CHECK(0);
		return NULL;
	}
}

static void check_borrowed_replacements(const expected_t expected[COUNT(fixtures)])
{
	size_t i;
	for (i = 0U; i < COUNT(fixtures); ++i) {
		sqlparser_render_scaffold_t scaffold;
		PgQuery__ParseResult *parsed = NULL;
		PgQuery__Node *replacement = NULL;
		PgQueryDeparseResult parsed_sql;
		sqlparser_error_t error;
		CHECK(sqlparser_parse_wrapper_ast(fixtures[i].replacement_sql, &parsed, &error) == SQLPARSER_STATUS_OK);
		CHECK(sqlparser_clone_proto_node(*replacement_slot(parsed, fixtures[i].kind), &replacement, &error) == SQLPARSER_STATUS_OK);
		CHECK(replacement != NULL);
		sqlparser_mark_proto_generated(&replacement->base);
		parsed_sql = pg_query_deparse_ast(parsed);
		CHECK(parsed_sql.error == NULL && parsed_sql.query != NULL);
		/* Free the source before borrowing the independently owned clone. */
		pg_query__parse_result__free_unpacked(parsed, NULL);
		CHECK(sqlparser_render_scaffold_init(&scaffold, fixtures[i].kind, &error) == SQLPARSER_STATUS_OK);
		*replacement_slot(&scaffold.ast, fixtures[i].kind) = replacement;
		check_deparse(&scaffold.ast, parsed_sql.query);
		/* Reinitialization only drops a borrow. It cannot destroy the clone. */
		CHECK(sqlparser_render_scaffold_init(&scaffold, fixtures[i].kind, NULL) == SQLPARSER_STATUS_OK);
		check_packed(&scaffold.ast, &expected[i]);
		*replacement_slot(&scaffold.ast, fixtures[i].kind) = replacement;
		check_deparse(&scaffold.ast, parsed_sql.query);
		CHECK(sqlparser_render_scaffold_init(&scaffold, fixtures[i].kind, NULL) == SQLPARSER_STATUS_OK);
		sqlparser_free_proto_node(replacement);
		check_storage(&scaffold, &scaffold.ast.base);
		check_packed(&scaffold.ast, &expected[i]);
		pg_query_free_deparse_result(parsed_sql);
	}
}

static void check_invalid_kind(const expected_t *expected)
{
	sqlparser_render_scaffold_t scaffold;
	sqlparser_error_t error;
	CHECK(sqlparser_render_scaffold_init(&scaffold, fixtures[0].kind, NULL) == SQLPARSER_STATUS_OK);
	CHECK(sqlparser_render_scaffold_init(NULL, fixtures[0].kind, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(error.code == SQLPARSER_STATUS_INVALID_ARGUMENT && error.message[0] != '\0');
	CHECK(sqlparser_render_scaffold_init(NULL, fixtures[0].kind, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(sqlparser_render_scaffold_init(&scaffold, (sqlparser_render_scaffold_kind_t)-1, &error) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	CHECK(error.code == SQLPARSER_STATUS_INVALID_ARGUMENT && error.message[0] != '\0');
	check_packed(&scaffold.ast, expected);
	CHECK(sqlparser_render_scaffold_init(&scaffold, (sqlparser_render_scaffold_kind_t)INT_MAX, NULL) == SQLPARSER_STATUS_INVALID_ARGUMENT);
	check_packed(&scaffold.ast, expected);
}

#ifndef _WIN32
static void *thread_main(void *context)
{
	const expected_t *expected = (const expected_t *)context;
	size_t round;
	for (round = 0U; round < 16U; ++round) {
		sqlparser_render_scaffold_t scaffold;
		size_t i;
		for (i = 0U; i < COUNT(fixtures); ++i) {
			CHECK(sqlparser_render_scaffold_init(&scaffold, fixtures[i].kind, NULL) == SQLPARSER_STATUS_OK);
			check_storage(&scaffold, &scaffold.ast.base);
			check_packed(&scaffold.ast, &expected[i]);
			check_deparse(&scaffold.ast, expected[i].sql);
			check_packed(&scaffold.ast, &expected[i]);
		}
	}
	return NULL;
}

static void check_concurrent(expected_t expected[COUNT(fixtures)])
{
	pthread_t threads[4];
	size_t i;
	for (i = 0U; i < COUNT(threads); ++i)
		CHECK(pthread_create(&threads[i], NULL, thread_main, expected) == 0);
	for (i = 0U; i < COUNT(threads); ++i)
		CHECK(pthread_join(threads[i], NULL) == 0);
}
#endif

int main(void)
{
	expected_t expected[COUNT(fixtures)];
	make_expected(expected);
	check_simultaneous(expected);
	check_nested(expected, 11U);
	check_borrowed_replacements(expected);
	check_invalid_kind(&expected[0]);
#ifndef _WIN32
	check_concurrent(expected);
#endif
	printf("render scaffold: six packed-AST/deparse contracts, simultaneous and nested instances, borrowed replacement ownership, invalid kinds");
#ifndef _WIN32
	printf(", four concurrent threads (384 deparses)");
#endif
	printf(" passed; stack object %zu bytes\n", sizeof(sqlparser_render_scaffold_t));
	return 0;
}
