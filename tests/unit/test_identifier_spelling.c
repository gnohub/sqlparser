#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pg_query.h"
#include "sqlparser/sqlparser.h"
#include "sqlparser_internal.h"
#include "../../src/core/sqlparser_ast_internal.h"

typedef struct {
	const char *owner_type;
	const char *field_name;
	const char *value;
} expected_name_t;

typedef struct {
	sqlparser_dialect_t dialect;
	const char *sql;
	size_t statement_index;
	expected_name_t names[8];
	size_t name_count;
} spelling_case_t;

static int statement_has_name(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const expected_name_t *expected)
{
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	size_t count;
	size_t index;

	memset(&error, 0, sizeof(error));
	if (sqlparser_statement_name_count(
		    handle,
		    statement_index,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: AST name count failed: %s\n", error.message);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		memset(&name, 0, sizeof(name));
		if (sqlparser_statement_name(
			    handle,
			    statement_index,
			    index,
			    &name,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(stderr, "FAIL: AST name lookup failed: %s\n", error.message);
			return 0;
		}
		if (name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, expected->owner_type) == 0 &&
		    strcmp(name.field_name, expected->field_name) == 0 &&
		    strcmp(name.value, expected->value) == 0) {
			return 1;
		}
	}
	return 0;
}

static int statement_find_name_index(
	sqlparser_handle_t *handle,
	size_t statement_index,
	const char *owner_type,
	const char *field_name,
	const char *value,
	size_t *out_index)
{
	sqlparser_error_t error;
	sqlparser_name_view_t name;
	size_t count;
	size_t index;

	*out_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_statement_name_count(
		    handle,
		    statement_index,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		return 0;
	}
	for (index = 0U; index < count; index++) {
		memset(&name, 0, sizeof(name));
		if (sqlparser_statement_name(
			    handle,
			    statement_index,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, owner_type) == 0 &&
		    strcmp(name.field_name, field_name) == 0 &&
		    strcmp(name.value, value) == 0) {
			*out_index = index;
			return 1;
		}
	}
	return 0;
}

static int deparse_matches_exact(
	sqlparser_handle_t *handle,
	const char *expected,
	sqlparser_error_t *error)
{
	char *sql;
	int matches;

	sql = NULL;
	matches =
		sqlparser_deparse(handle, &sql, error) == SQLPARSER_STATUS_OK &&
		sql != NULL && strcmp(sql, expected) == 0;
	if (!matches) {
		fprintf(
			stderr,
			"FAIL: exact SQL mismatch: %s\n",
			sql != NULL ? sql : error->message);
	}
	sqlparser_string_free(sql);
	return matches;
}

static int standard_parse_has_spelling(
	const char *sql,
	const char *expected,
	const char *unexpected)
{
	PgQueryParseResult result;
	int valid;

	result = pg_query_parse(sql);
	if (result.error != NULL) {
		fprintf(
			stderr,
			"FAIL: standard libpg_query parse failed: %s\n",
			result.error->message);
		pg_query_free_parse_result(result);
		return 0;
	}
	valid =
		result.parse_tree != NULL &&
		strstr(result.parse_tree, expected) != NULL &&
		(unexpected == NULL ||
		 strstr(result.parse_tree, unexpected) == NULL);
	if (!valid) {
		fprintf(
			stderr,
			"FAIL: standard libpg_query identifier contract changed: %s\n",
			sql);
	}
	pg_query_free_parse_result(result);
	return valid;
}

static int standard_protobuf_deparse_has_spelling(
	const char *sql,
	const char *expected,
	const char *unexpected)
{
	PgQueryProtobufParseResult parse_result;
	PgQueryDeparseResult deparse_result;
	int valid;

	parse_result = pg_query_parse_protobuf(sql);
	if (parse_result.error != NULL) {
		fprintf(
			stderr,
			"FAIL: standard protobuf parse failed: %s\n",
			parse_result.error->message);
		pg_query_free_protobuf_parse_result(parse_result);
		return 0;
	}
	deparse_result = pg_query_deparse_protobuf(parse_result.parse_tree);
	valid =
		deparse_result.error == NULL &&
		deparse_result.query != NULL &&
		strstr(deparse_result.query, expected) != NULL &&
		(unexpected == NULL ||
		 strstr(deparse_result.query, unexpected) == NULL);
	if (!valid) {
		fprintf(
			stderr,
			"FAIL: standard protobuf identifier contract changed: %s\n",
			sql);
	}
	pg_query_free_deparse_result(deparse_result);
	pg_query_free_protobuf_parse_result(parse_result);
	return valid;
}

static int sqlparser_parse_is_rejected(const char *sql)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_status_t status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	status = sqlparser_parse(sql, &handle, &error);
	sqlparser_handle_destroy(handle);
	return status == SQLPARSER_STATUS_PARSE_ERROR;
}

static int standard_fingerprints_match(const char *left_sql, const char *right_sql)
{
	PgQueryFingerprintResult left;
	PgQueryFingerprintResult right;
	int valid;

	left = pg_query_fingerprint(left_sql);
	right = pg_query_fingerprint(right_sql);
	valid =
		left.error == NULL &&
		right.error == NULL &&
		left.fingerprint_str != NULL &&
		right.fingerprint_str != NULL &&
		strcmp(left.fingerprint_str, right.fingerprint_str) == 0;
	if (!valid) {
		fprintf(
			stderr,
			"FAIL: standard libpg_query fingerprint canonicalization changed\n");
	}
	pg_query_free_fingerprint_result(left);
	pg_query_free_fingerprint_result(right);
	return valid;
}

static int xmltable_internal_option_semantics_match(void)
{
	static const struct {
		const char *option;
		const char *message;
	} cases[] = {
		{
			"__PG__IS_NOT_NULL",
			"cannot be used in XMLTABLE"
		},
		{
			"\"__pg__is_not_null\"",
			"cannot be used in XMLTABLE"
		},
		{
			"\"__PG__IS_NOT_NULL\"",
			"unrecognized column option"
		}
	};
	char sql[256];
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		PgQueryProtobufParseResult preserve_result;
		PgQueryProtobufParseResult standard_result;
		int valid;

		(void)snprintf(
			sql,
			sizeof(sql),
			"SELECT * FROM XMLTABLE('/a' PASSING '<a/>' "
			"COLUMNS x text %s true)",
			cases[index].option);
		standard_result = pg_query_parse_protobuf(sql);
		preserve_result =
			sqlparser_parse_protobuf_preserving_identifier_spelling(
				sql);
		valid =
			standard_result.error != NULL &&
			standard_result.error->message != NULL &&
			strstr(
				standard_result.error->message,
				cases[index].message) != NULL &&
			preserve_result.error != NULL &&
			preserve_result.error->message != NULL &&
			strstr(
				preserve_result.error->message,
				cases[index].message) != NULL;
		if (!valid) {
			fprintf(
				stderr,
				"FAIL: XMLTABLE option semantics changed: %s\n",
				sql);
		}
		pg_query_free_protobuf_parse_result(preserve_result);
		pg_query_free_protobuf_parse_result(standard_result);
		if (!valid) {
			return 0;
		}
	}
	return 1;
}

static int query_graph_identifier_semantics(void)
{
	static const struct {
		const char *sql;
		const char *relation_name;
		sqlparser_graph_relation_kind_t expected_kind;
	} relation_cases[] = {
		{
			"WITH \"C\" AS (SELECT 1 AS id) SELECT * FROM \"c\"",
			"c",
			SQLPARSER_GRAPH_REL_BASE
		},
		{
			"SELECT 1 FROM \"DUAL\"",
			"DUAL",
			SQLPARSER_GRAPH_REL_BASE
		}
	};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		int expected_has_relation;
	} alias_cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT \"T\".id FROM foo AS \"t\"",
			0
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT T.id FROM foo AS t",
			1
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT \"T\".id FROM foo \"t\"",
			0
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT t.id FROM foo T",
			1
		}
	};
	static const char prefix_63[] =
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	sqlparser_graph_field_t field;
	sqlparser_graph_relation_t relation;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	sqlparser_parse_options_t options;
	char sql[256];
	size_t case_index;
	size_t index;
	int found;

	for (case_index = 0U;
	     case_index < sizeof(relation_cases) / sizeof(relation_cases[0]);
	     case_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse(
			    relation_cases[case_index].sql,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier graph relation case failed: %s\n",
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		found = 0;
		for (index = 0U; index < graph.relation_count; index++) {
			if (sqlparser_query_graph_relation_at(
				    &graph,
				    index,
				    &relation,
				    &error) == SQLPARSER_STATUS_OK &&
			    relation.object_name != NULL &&
			    strcmp(
				    relation.object_name,
				    relation_cases[case_index].relation_name) == 0 &&
			    relation.kind ==
				    relation_cases[case_index].expected_kind) {
				found = 1;
				break;
			}
		}
		sqlparser_handle_destroy(handle);
		if (!found) {
			fprintf(
				stderr,
				"FAIL: identifier graph relation semantics changed: %s\n",
				relation_cases[case_index].sql);
			return 0;
		}
	}

	for (case_index = 0U;
	     case_index < sizeof(alias_cases) / sizeof(alias_cases[0]);
	     case_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = alias_cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    alias_cases[case_index].sql,
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: alias graph case failed: %s\n",
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		found = 0;
		for (index = 0U; index < graph.field_count; index++) {
			if (sqlparser_query_graph_field_at(
				    &graph,
				    index,
				    &field,
				    &error) == SQLPARSER_STATUS_OK &&
			    field.column_name != NULL &&
			    strcmp(field.column_name, "id") == 0 &&
			    field.has_relation ==
				    alias_cases[case_index].expected_has_relation) {
				found = 1;
				break;
			}
		}
		sqlparser_handle_destroy(handle);
		if (!found) {
			fprintf(
				stderr,
				"FAIL: identifier alias semantics changed: %s\n",
				alias_cases[case_index].sql);
			return 0;
		}
	}

	if (snprintf(
		    sql,
		    sizeof(sql),
		    "SELECT %sY.id FROM foo AS %sX",
		    prefix_63,
		    prefix_63) < 0) {
		return 0;
	}
	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(sql, &handle, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(
		    handle,
		    0U,
		    &graph,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: long identifier graph case failed: %s\n", error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	found = 0;
	for (index = 0U; index < graph.field_count; index++) {
		if (sqlparser_query_graph_field_at(
			    &graph,
			    index,
			    &field,
			    &error) == SQLPARSER_STATUS_OK &&
		    field.column_name != NULL &&
		    strcmp(field.column_name, "id") == 0 &&
		    field.has_relation) {
			found = 1;
			break;
		}
	}
	sqlparser_handle_destroy(handle);
	if (!found) {
		fprintf(stderr, "FAIL: PostgreSQL 63-byte identifier keys did not match\n");
		return 0;
	}
	return 1;
}

static int session_identifier_semantics(void)
{
	static const struct {
		const char *sql;
		sqlparser_graph_session_value_kind_t expected_kind;
	} cases[] = {
		{
			"SET search_path TO \"NONE\"",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER
		},
		{
			"SET search_path TO NONE",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD
		},
		{
			"SET ROLE \"CURRENT\"",
			SQLPARSER_GRAPH_SESSION_VALUE_IDENTIFIER
		},
		{
			"SET ROLE CURRENT",
			SQLPARSER_GRAPH_SESSION_VALUE_KEYWORD
		}
	};
	sqlparser_graph_session_item_t item;
	sqlparser_graph_session_value_t value;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse(cases[index].sql, &handle, &error) !=
			    SQLPARSER_STATUS_OK ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_session_item_at(
			    &graph,
			    0U,
			    &item,
			    &error) != SQLPARSER_STATUS_OK ||
		    item.value_count != 1U ||
		    sqlparser_query_graph_session_value_at(
			    &graph,
			    item.value_offset,
			    &value,
			    &error) != SQLPARSER_STATUS_OK ||
		    value.kind != cases[index].expected_kind) {
			fprintf(
				stderr,
				"FAIL: session identifier semantics changed for %s: %s\n",
				cases[index].sql,
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int ast_deparse_preserves_dialect(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *required,
	const char *also_required,
	const char *forbidden)
{
	PgQueryDeparseResult deparse_result;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_parse_options_t options;
	int valid;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	if (sqlparser_parse_with_options(
		    sql,
		    &options,
		    &handle,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: AST deparse canary did not parse: %s\n", sql);
		return 0;
	}
	if (sqlparser_validate_ast_identifier_spelling(handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: AST deparse canary spelling audit failed for %s: %s\n",
			sql,
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	deparse_result =
		sqlparser_deparse_protobuf_for_handle(
			handle,
			handle->parse_tree,
			0U,
			0U,
			handle->parser_sql_len);
	valid =
		deparse_result.error == NULL &&
		deparse_result.query != NULL &&
		(required == NULL ||
		 strstr(deparse_result.query, required) != NULL) &&
		(also_required == NULL ||
		 strstr(deparse_result.query, also_required) != NULL) &&
		(forbidden == NULL ||
		 strstr(deparse_result.query, forbidden) == NULL);
	if (!valid) {
		fprintf(
			stderr,
			"FAIL: AST deparse canary changed source spelling: %s\noutput: %s\n",
			sql,
			deparse_result.query != NULL ?
				deparse_result.query :
				"(null)");
	}
	pg_query_free_deparse_result(deparse_result);
	sqlparser_handle_destroy(handle);
	return valid;
}

static int ast_deparse_preserves(
	const char *sql,
	const char *required,
	const char *also_required,
	const char *forbidden)
{
	return ast_deparse_preserves_dialect(
		SQLPARSER_DIALECT_POSTGRESQL,
		sql,
		required,
		also_required,
		forbidden);
}

static int mutated_keyword_alias_is_quoted(void)
{
	sqlparser_handle_t *handle;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	sqlparser_status_t status;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT 1 AS select FROM t",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: keyword alias mutation setup failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "ResTarget") == 0 &&
		    strcmp(name.field_name, "name") == 0 &&
		    strcmp(name.value, "select") == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "FROM",
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: keyword alias mutation failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	status = sqlparser_deparse(handle, &deparsed, &error);
	if (status != SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "AS \"FROM\" FROM t") == NULL) {
		fprintf(
			stderr,
			"FAIL: keyword alias mutation borrowed a syntax token: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int mutated_type_prefix_is_not_synthetic(void)
{
	sqlparser_handle_t *handle;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT 1::foo.bit",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: type prefix mutation setup failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "TypeName") == 0 &&
		    strcmp(name.field_name, "names") == 0 &&
		    strcmp(name.value, "foo") == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "pg_catalog",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "::pg_catalog.bit") == NULL) {
		fprintf(
			stderr,
			"FAIL: generated pg_catalog type prefix was treated as synthetic: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int mutated_name_preserves_siblings(
	const char *sql,
	const char *owner_type,
	const char *field_name,
	const char *old_value,
	const char *new_value,
	const char *required,
	const char *forbidden)
{
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	reparsed = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(sql, &handle, &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: sibling spelling mutation setup failed: %s\n", sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, owner_type) == 0 &&
		    strcmp(name.field_name, field_name) == 0 &&
		    strcmp(name.value, old_value) == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    new_value,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, required) == NULL ||
	    (forbidden != NULL && strstr(deparsed, forbidden) != NULL) ||
	    sqlparser_parse(deparsed, &reparsed, &error) !=
		    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: name mutation changed a sibling identifier: %s\noutput: %s\n",
			sql,
			deparsed != NULL ? deparsed : error.message);
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int mutated_dialect_keeps_unmodified_spelling(
	sqlparser_dialect_t dialect,
	const char *sql,
	const char *old_alias,
	const char *new_alias,
	const char *expected)
{
	sqlparser_handle_t *handle;
	sqlparser_name_view_t name;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = dialect;
	if (sqlparser_parse_with_options(
		    sql,
		    &options,
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: transformed dialect mutation setup failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "ResTarget") == 0 &&
		    strcmp(name.field_name, "name") == 0 &&
		    strcmp(name.value, old_alias) == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    new_alias,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, expected) == NULL) {
		fprintf(
			stderr,
			"FAIL: transformed dialect mutation changed an unmodified identifier: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int mutation_marker_lifecycle(void)
{
	sqlparser_handle_t *clone;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_name_view_t name;
	sqlparser_error_t error;
	char *deparsed;
	size_t count;
	size_t index;
	size_t target_index;

	handle = NULL;
	clone = NULL;
	reparsed = NULL;
	deparsed = NULL;
	target_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT Foo AS OldAlias FROM MixedTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: mutation marker lifecycle setup failed\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "ResTarget") == 0 &&
		    strcmp(name.field_name, "name") == 0 &&
		    strcmp(name.value, "OldAlias") == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "SELECT",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 1U ||
	    sqlparser_handle_clone(handle, &clone, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(clone, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "Foo AS \"SELECT\" FROM MixedTable") == NULL) {
		fprintf(
			stderr,
			"FAIL: cloned identifier mutation lost provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(clone);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	sqlparser_handle_destroy(clone);
	clone = NULL;
	if (sqlparser_select_insert_target_sql(
		    handle,
		    0U,
		    0U,
		    0U,
		    "Added",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "SELECT Added, Foo AS \"SELECT\" FROM MixedTable") ==
		    NULL) {
		fprintf(
			stderr,
			"FAIL: structural mutation lost identifier provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	target_index = (size_t)-1;
	if (sqlparser_statement_name_count(
		    handle,
		    0U,
		    &count,
		    &error) != SQLPARSER_STATUS_OK) {
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < count; index++) {
		if (sqlparser_statement_name(
			    handle,
			    0U,
			    index,
			    &name,
			    &error) == SQLPARSER_STATUS_OK &&
		    name.owner_type != NULL &&
		    name.field_name != NULL &&
		    name.value != NULL &&
		    strcmp(name.owner_type, "ResTarget") == 0 &&
		    strcmp(name.field_name, "name") == 0 &&
		    strcmp(name.value, "SELECT") == 0) {
			target_index = index;
			break;
		}
	}
	if (target_index == (size_t)-1 ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "OldAlias",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 0U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "SELECT Added, Foo AS OldAlias FROM MixedTable") ==
		    NULL ||
	    sqlparser_parse(deparsed, &reparsed, &error) !=
		    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: restoring an identifier did not clear provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int failed_commit_preserves_mutation_provenance(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_limits_t limits;
	char *deparsed;
	size_t spelling_count;
	size_t target_index;
	sqlparser_status_t status;
	unsigned long generation;

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT Foo AS OldAlias, KeepCol FROM MixedTable; SELECT 1",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_find_name_index(
		    handle,
		    0U,
		    "ResTarget",
		    "name",
		    "OldAlias",
		    &target_index) ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    target_index,
		    "SELECT",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 1U) {
		fprintf(
			stderr,
			"FAIL: failed-commit provenance setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	spelling_count = handle->identifier_spelling_count;
	generation = handle->generation;
	limits = handle->limits;
	handle->limits.max_statement_count = 1U;
	status = sqlparser_select_set_target_sql(
		handle,
		0U,
		0U,
		0U,
		"\"AddedOne\" AS \"AliasOne\", "
		"\"AddedTwo\" AS \"AliasTwo\"",
		&error);
	handle->limits = limits;
	if (status != SQLPARSER_STATUS_RESOURCE_LIMIT ||
	    handle->generation != generation ||
	    handle->identifier_mutation_count != 1U ||
	    handle->identifier_spelling_count != spelling_count ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strcmp(
		    deparsed,
		    "SELECT Foo AS \"SELECT\", KeepCol FROM MixedTable; SELECT 1") != 0) {
		fprintf(
			stderr,
			"FAIL: failed commit changed identifier provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	spelling_count = handle->identifier_spelling_count;
	generation = handle->generation;
	limits = handle->limits;
	handle->limits.max_statement_count = 1U;
	status = sqlparser_select_set_target_sql(
		handle,
		0U,
		0U,
		0U,
		"\"AddedOne\"",
		&error);
	handle->limits = limits;
	if (status != SQLPARSER_STATUS_RESOURCE_LIMIT ||
	    handle->generation != generation ||
	    handle->identifier_mutation_count != 1U ||
	    handle->identifier_spelling_count != spelling_count ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strcmp(
		    deparsed,
		    "SELECT Foo AS \"SELECT\", KeepCol FROM MixedTable; SELECT 1") != 0) {
		fprintf(
			stderr,
			"FAIL: failed whole-target commit changed identifier provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	generation = handle->generation;
	status = sqlparser_select_set_target_sql(
		handle,
		0U,
		0U,
		0U,
		"\"AddedOne\"",
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    handle->generation != generation + 1UL ||
	    handle->identifier_mutation_count != 0U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strcmp(
		    deparsed,
		    "SELECT \"AddedOne\", KeepCol FROM MixedTable; SELECT 1") != 0) {
		fprintf(
			stderr,
			"FAIL: successful whole-target commit retained old provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int removed_select_target_paths_preserve_mutation_provenance(void)
{
	typedef enum {
		REMOVE_SELECT_TARGET_LIST = 0,
		REMOVE_SELECT_TARGET_WITH_COLUMNS = 1,
		REMOVE_SELECT_TARGET = 2
	} removal_action_t;
	static const char *structured_parts[] = {"structured_col"};
	static const sqlparser_identifier_path_view_t structured_column = {
		structured_parts,
		1U
	};
	static const struct {
		removal_action_t action;
		const char *expected_sql;
	} cases[] = {
		{REMOVE_SELECT_TARGET_LIST, "SELECT list_col, keep_col FROM MixedTable; SELECT 1"},
		{REMOVE_SELECT_TARGET_WITH_COLUMNS, "SELECT structured_col, KeepCol FROM MixedTable; SELECT 1"},
		{REMOVE_SELECT_TARGET, "SELECT KeepCol FROM MixedTable; SELECT 1"}
	};
	static const char original_sql[] =
		"SELECT Foo AS \"SELECT\", KeepCol FROM MixedTable; SELECT 1";
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_limits_t limits;
	sqlparser_selector_t target_selector;
	char *deparsed;
	size_t case_index;
	size_t spelling_count;
	size_t target_index;
	size_t attempt;
	sqlparser_status_t status;
	unsigned long generation;

	memset(&target_selector, 0, sizeof(target_selector));
	target_selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET;
	target_selector.statement_index = 0U;
	target_selector.item_index = 0U;
	target_selector.column_index = 0U;
	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		handle = NULL;
		deparsed = NULL;
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse(
			    "SELECT Foo AS OldAlias, KeepCol FROM MixedTable; SELECT 1",
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    !statement_find_name_index(
			    handle,
			    0U,
			    "ResTarget",
			    "name",
			    "OldAlias",
			    &target_index) ||
		    sqlparser_statement_set_name(
			    handle,
			    0U,
			    target_index,
			    "SELECT",
			    &error) != SQLPARSER_STATUS_OK ||
		    handle->identifier_mutation_count != 1U) {
			fprintf(
				stderr,
				"FAIL: removed-target provenance case %lu setup failed: %s\n",
				(unsigned long)case_index,
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}

		for (attempt = 0U; attempt < 2U; attempt++) {
			generation = handle->generation;
			spelling_count = handle->identifier_spelling_count;
			limits = handle->limits;
			if (attempt == 0U) {
				handle->limits.max_statement_count = 1U;
			}
			if (cases[case_index].action == REMOVE_SELECT_TARGET_LIST) {
				status = sqlparser_select_set_targets_sql(
					handle,
					0U,
					0U,
					"list_col, keep_col",
					&error);
			} else if (cases[case_index].action ==
				   REMOVE_SELECT_TARGET_WITH_COLUMNS) {
				status = sqlparser_selector_replace_select_target_with_columns(
					handle,
					&target_selector,
					&structured_column,
					1U,
					&error);
			} else {
				status = sqlparser_select_delete_target(
					handle,
					0U,
					0U,
					0U,
					&error);
			}
			handle->limits = limits;
			if (sqlparser_deparse(handle, &deparsed, &error) !=
				    SQLPARSER_STATUS_OK ||
			    deparsed == NULL) {
				fprintf(
					stderr,
					"FAIL: removed-target provenance case %lu deparse failed: %s\n",
					(unsigned long)case_index,
					error.message);
				sqlparser_string_free(deparsed);
				sqlparser_handle_destroy(handle);
				return 0;
			}
			if (attempt == 0U) {
				if (status != SQLPARSER_STATUS_RESOURCE_LIMIT ||
				    handle->generation != generation ||
				    handle->identifier_mutation_count != 1U ||
				    handle->identifier_spelling_count != spelling_count ||
				    strcmp(deparsed, original_sql) != 0) {
					fprintf(
						stderr,
						"FAIL: removed-target failure case %lu changed provenance: %s\n",
						(unsigned long)case_index,
						deparsed);
					sqlparser_string_free(deparsed);
					sqlparser_handle_destroy(handle);
					return 0;
				}
			} else if (status != SQLPARSER_STATUS_OK ||
				   handle->generation != generation + 1UL ||
				   handle->identifier_mutation_count != 0U ||
				   strcmp(deparsed, cases[case_index].expected_sql) != 0) {
				fprintf(
					stderr,
					"FAIL: removed-target success case %lu retained provenance: %s\n",
					(unsigned long)case_index,
					deparsed);
				sqlparser_string_free(deparsed);
				sqlparser_handle_destroy(handle);
				return 0;
			}
			sqlparser_string_free(deparsed);
			deparsed = NULL;
		}
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int relation_group_preserves_source_spelling(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *expected;
	} borrowed_cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT * FROM OldSchema.OldTable",
			"SELECT * FROM OldTable.OldSchema"
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT * FROM \"OldSchema\".\"OldTable\"",
			"SELECT * FROM \"OldTable\".\"OldSchema\""
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT * FROM [OldSchema].[OldTable]",
			"SELECT * FROM [OldTable].[OldSchema]"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT * FROM `OldSchema`.`OldTable`",
			"SELECT * FROM `OldTable`.`OldSchema`"
		}
	};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *sql;
		const char *new_schema;
		const char *after_rename;
		const char *after_swap;
	} renamed_borrowed_cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT * FROM OldSchema.OldTable",
			"new_schema",
			"SELECT * FROM new_schema.OldTable",
			"SELECT * FROM OldTable.new_schema"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT * FROM [OldSchema].[OldTable]",
			"NewSchema",
			"SELECT * FROM \"NewSchema\".[OldTable]",
			"SELECT * FROM [OldTable].\"NewSchema\""
		}
	};
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_relation_view_t relation;
	char *deparsed;
	size_t case_index;

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT * FROM MixedSchema.MixedTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_set_relation_name(
		    handle,
		    0U,
		    0U,
		    NULL,
		    "MixedTable",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 3U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "FROM MixedTable") == NULL ||
	    strstr(deparsed, "\"MixedTable\"") != NULL) {
		fprintf(
			stderr,
			"FAIL: removing a relation qualifier changed table spelling: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	deparsed = NULL;
	if (sqlparser_statement_set_relation_name(
		    handle,
		    0U,
		    0U,
		    "MixedSchema",
		    "MixedTable",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 0U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "FROM MixedSchema.MixedTable") == NULL) {
		fprintf(
			stderr,
			"FAIL: restoring a relation did not clear group provenance: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	deparsed = NULL;
	if (sqlparser_parse(
		    "SELECT * FROM MixedTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_set_relation_name(
		    handle,
		    0U,
		    0U,
		    "new_schema",
		    "MixedTable",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "FROM new_schema.MixedTable") == NULL ||
	    strstr(deparsed, "\"MixedTable\"") != NULL) {
		fprintf(
			stderr,
			"FAIL: adding a relation qualifier changed table spelling: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);

	for (case_index = 0U;
	     case_index <
		     sizeof(borrowed_cases) /
			     sizeof(borrowed_cases[0]);
	     case_index++) {
		handle = NULL;
		deparsed = NULL;
		memset(&relation, 0, sizeof(relation));
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = borrowed_cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    borrowed_cases[case_index].sql,
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_relation(
			    handle,
			    0U,
			    0U,
			    &relation,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_set_relation_name(
			    handle,
			    0U,
			    0U,
			    relation.table_name,
			    relation.schema_name,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(
			    deparsed,
			    borrowed_cases[case_index].expected) !=
			    0) {
			fprintf(
				stderr,
				"FAIL: borrowed relation component spelling case %lu was not preserved: %s\n",
				(unsigned long)case_index,
				deparsed != NULL ?
					deparsed :
					error.message);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	for (case_index = 0U;
	     case_index <
		     sizeof(renamed_borrowed_cases) /
			     sizeof(renamed_borrowed_cases[0]);
	     case_index++) {
		handle = NULL;
		deparsed = NULL;
		memset(&relation, 0, sizeof(relation));
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect =
			renamed_borrowed_cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    renamed_borrowed_cases[case_index].sql,
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_set_relation_name(
			    handle,
			    0U,
			    0U,
			    renamed_borrowed_cases[case_index].new_schema,
			    "OldTable",
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(
			    deparsed,
			    renamed_borrowed_cases[case_index]
				    .after_rename) != 0) {
			fprintf(
				stderr,
				"FAIL: renamed relation spelling case %lu mismatch: %s\n",
				(unsigned long)case_index,
				deparsed != NULL ?
					deparsed :
					error.message);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_string_free(deparsed);
		deparsed = NULL;
		if (sqlparser_statement_relation(
			    handle,
			    0U,
			    0U,
			    &relation,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_set_relation_name(
			    handle,
			    0U,
			    0U,
			    relation.table_name,
			    relation.schema_name,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(
			    deparsed,
			    renamed_borrowed_cases[case_index]
				    .after_swap) != 0) {
			fprintf(
				stderr,
				"FAIL: borrowed renamed relation spelling case %lu mismatch: %s\n",
				(unsigned long)case_index,
				deparsed != NULL ?
					deparsed :
					error.message);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int selector_mutation_adapters_preserve_source_provenance(void)
{
	static const char relation_sql[] =
		"SELECT * FROM \"OldSchema\".\"OldTable\"";
	static const char swapped_relation_sql[] =
		"SELECT * FROM \"OldTable\".\"OldSchema\"";
	static const char qualified_relation_sql[] =
		"SELECT \"OldSchema\".\"OldTable\".id "
		"FROM \"OldSchema\".\"OldTable\"";
	static const char swapped_qualified_relation_sql[] =
		"SELECT \"OldTable\".\"OldSchema\".id "
		"FROM \"OldTable\".\"OldSchema\"";
	static const char name_sql[] =
		"SELECT Foo AS \"OldAlias\" FROM \"MixedTable\"";
	static const char renamed_name_sql[] =
		"SELECT Foo AS \"SELECT\" FROM \"MixedTable\"";
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	sqlparser_relation_view_t relation;
	sqlparser_selector_t selector;
	size_t name_index;
	size_t spelling_count;
	unsigned long generation;
	sqlparser_status_t status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	memset(&relation, 0, sizeof(relation));
	memset(&selector, 0, sizeof(selector));
	selector.kind = SQLPARSER_SELECTOR_KIND_RELATION;
	selector.statement_index = 0U;
	selector.item_index = 0U;
	if (sqlparser_parse(relation_sql, &handle, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_statement_relation(
		    handle,
		    0U,
		    0U,
		    &relation,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: selector relation provenance setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	generation = handle->generation;
	spelling_count = handle->identifier_spelling_count;
	status = sqlparser_selector_set_relation_name(
		handle,
		&selector,
		relation.schema_name,
		relation.table_name,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    handle->generation != generation ||
	    handle->identifier_mutation_count != 0U ||
	    handle->identifier_spelling_count != spelling_count ||
	    !deparse_matches_exact(handle, relation_sql, &error)) {
		fprintf(
			stderr,
			"FAIL: selector relation no-op changed provenance\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	status = sqlparser_selector_set_relation_name(
		handle,
		&selector,
		relation.table_name,
		relation.schema_name,
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 3U ||
	    !deparse_matches_exact(
		    handle,
		    swapped_relation_sql,
		    &error)) {
		fprintf(
			stderr,
			"FAIL: selector relation borrowed spelling was lost\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	status = sqlparser_selector_set_relation_name(
		handle,
		&selector,
		"OldSchema",
		"OldTable",
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 0U ||
	    !deparse_matches_exact(handle, relation_sql, &error)) {
		fprintf(
			stderr,
			"FAIL: selector relation restore retained %lu provenance entries\n",
			(unsigned long)handle->identifier_mutation_count);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	memset(&relation, 0, sizeof(relation));
	if (sqlparser_parse(qualified_relation_sql, &handle, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_statement_relation(
		    handle,
		    0U,
		    0U,
		    &relation,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_selector_set_relation_name(
		    handle,
		    &selector,
		    relation.table_name,
		    relation.schema_name,
		    &error) != SQLPARSER_STATUS_OK ||
	    !deparse_matches_exact(
		    handle,
		    swapped_qualified_relation_sql,
		    &error)) {
		fprintf(
			stderr,
			"FAIL: selector relation qualifier propagation failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	name_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	memset(&selector, 0, sizeof(selector));
	if (sqlparser_parse(name_sql, &handle, &error) !=
		    SQLPARSER_STATUS_OK ||
	    !statement_find_name_index(
		    handle,
		    0U,
		    "ResTarget",
		    "name",
		    "OldAlias",
		    &name_index)) {
		fprintf(
			stderr,
			"FAIL: selector name provenance setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	selector.kind = SQLPARSER_SELECTOR_KIND_NAME;
	selector.statement_index = 0U;
	selector.item_index = name_index;
	generation = handle->generation;
	spelling_count = handle->identifier_spelling_count;
	status = sqlparser_selector_set_name(
		handle,
		&selector,
		"OldAlias",
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    handle->generation != generation ||
	    handle->identifier_mutation_count != 0U ||
	    handle->identifier_spelling_count != spelling_count ||
	    !deparse_matches_exact(handle, name_sql, &error)) {
		fprintf(
			stderr,
			"FAIL: selector name no-op changed provenance\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	status = sqlparser_selector_set_name(
		handle,
		&selector,
		"SELECT",
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 1U ||
	    !deparse_matches_exact(handle, renamed_name_sql, &error)) {
		fprintf(
			stderr,
			"FAIL: selector name keyword mutation lost provenance\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	status = sqlparser_selector_set_name(
		handle,
		&selector,
		"OldAlias",
		&error);
	if (status != SQLPARSER_STATUS_OK ||
	    handle->identifier_mutation_count != 0U ||
	    !deparse_matches_exact(handle, name_sql, &error)) {
		fprintf(
			stderr,
			"FAIL: selector name restore retained provenance\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(handle);
	return 1;
}

static int structured_target_identifier_rendering_is_exact(void)
{
	static const char *plain_name[] = {"plain_name"};
	static const char *keyword_name[] = {"select"};
	static const char *sysdate_name[] = {"sysdate"};
	static const sqlparser_identifier_path_view_t columns[] = {
		{plain_name, 1U},
		{keyword_name, 1U},
		{sysdate_name, 1U}
	};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *expected_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT plain_name, `select`, `sysdate` FROM t"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"SELECT plain_name, \"select\", \"sysdate\" FROM t"
		}
	};
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	sqlparser_selector_t selector;
	char *deparsed;
	size_t case_index;

	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		handle = NULL;
		deparsed = NULL;
		memset(&error, 0, sizeof(error));
		memset(&selector, 0, sizeof(selector));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET;
		selector.statement_index = 0U;
		selector.item_index = 0U;
		selector.column_index = 0U;
		if (sqlparser_parse_with_options(
			    "SELECT * FROM t",
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_selector_replace_select_target_with_columns(
			    handle,
			    &selector,
			    columns,
			    sizeof(columns) / sizeof(columns[0]),
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_validate_ast_identifier_spelling(
			    handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(deparsed, cases[case_index].expected_sql) != 0) {
			fprintf(
				stderr,
				"FAIL: structured target identifier case %lu was not exact: %s\n",
				(unsigned long)case_index,
				deparsed != NULL ? deparsed : error.message);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int patched_select_identifier_spelling_preserved(void)
{
	static const expected_name_t ast_names[] = {
		{"ColumnRef", "fields", "MiXeD"},
		{"ColumnRef", "fields", "Uq"},
		{"ColumnRef", "fields", "PlainCase"},
		{"ResTarget", "name", "AliasCase"}
	};
	static const struct {
		sqlparser_dialect_t dialect;
		const char *patch_sql;
		const char *targets[3];
		size_t target_count;
		const char *deparsed_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"u.\"MiXeD\", \"Uq\".PlainCase AS AliasCase",
			{
				"u.\"MiXeD\"",
				"\"Uq\".PlainCase AS AliasCase"
			},
			2U,
			"SELECT u.\"MiXeD\", \"Uq\".PlainCase AS AliasCase "
			"FROM abc u"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"u.`MiXeD`, `Uq`.PlainCase AS AliasCase",
			{
				"u.`MiXeD`",
				"`Uq`.PlainCase AS AliasCase"
			},
			2U,
			"SELECT u.`MiXeD`, `Uq`.PlainCase AS AliasCase "
			"FROM abc u"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"u.[MiXeD], [Uq].PlainCase AS AliasCase, "
			"\"DQ\".\"QuotedCase\"",
			{
				"u.[MiXeD]",
				"[Uq].PlainCase AS AliasCase",
				"\"DQ\".\"QuotedCase\""
			},
			3U,
			"SELECT u.[MiXeD], [Uq].PlainCase AS AliasCase, "
			"\"DQ\".\"QuotedCase\" FROM abc u"
		}
	};
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	char *deparsed;
	char *target_sql;
	size_t case_index;
	size_t name_index;
	size_t target_count;
	size_t target_index;

	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		handle = NULL;
		reparsed = NULL;
		deparsed = NULL;
		target_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    "SELECT * FROM abc u",
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_select_set_targets_sql(
			    handle,
			    0U,
			    0U,
			    cases[case_index].patch_sql,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_select_target_count(
			    handle,
			    0U,
			    0U,
			    &target_count,
			    &error) != SQLPARSER_STATUS_OK ||
		    target_count != cases[case_index].target_count) {
			fprintf(
				stderr,
				"FAIL: patched identifier spelling case %lu setup failed: %s\n",
				(unsigned long)case_index,
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		for (target_index = 0U;
		     target_index < cases[case_index].target_count;
		     target_index++) {
			if (sqlparser_select_target_sql(
				    handle,
				    0U,
				    0U,
				    target_index,
				    &target_sql,
				    &error) != SQLPARSER_STATUS_OK ||
			    target_sql == NULL ||
			    strcmp(
				    target_sql,
				    cases[case_index].targets[target_index]) != 0) {
				fprintf(
					stderr,
					"FAIL: patched target %lu lost identifier spelling: %s\n",
					(unsigned long)target_index,
					target_sql != NULL ? target_sql : error.message);
				sqlparser_string_free(target_sql);
				sqlparser_handle_destroy(handle);
				return 0;
			}
			sqlparser_string_free(target_sql);
			target_sql = NULL;
		}
		for (name_index = 0U;
		     name_index < sizeof(ast_names) / sizeof(ast_names[0]);
		     name_index++) {
			if (!statement_has_name(
				    handle,
				    0U,
				    &ast_names[name_index])) {
				fprintf(
					stderr,
					"FAIL: patched identifier spelling case %lu lost AST name %s\n",
					(unsigned long)case_index,
					ast_names[name_index].value);
				sqlparser_handle_destroy(handle);
				return 0;
			}
		}
		if (sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(deparsed, cases[case_index].deparsed_sql) != 0 ||
		    sqlparser_parse_with_options(
			    deparsed,
			    &options,
			    &reparsed,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: patched identifier spelling case %lu changed on deparse: %s\n",
				(unsigned long)case_index,
				deparsed != NULL ? deparsed : error.message);
			sqlparser_handle_destroy(reparsed);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int patched_nested_identifier_spelling_preserved(void)
{
	static const expected_name_t names[] = {
		{"ColumnRef", "fields", "X"},
		{"RangeVar", "schemaname", "DbO"},
		{"RangeVar", "relname", "T"},
		{"Alias", "aliasname", "Q"},
		{"ResTarget", "name", "Z"}
	};
	static const char patch_sql[] =
		"(SELECT [X] FROM [DbO].[T] AS [Q] "
		"WHERE [Q].[X] = 1) AS [Z]";
	static const char expected_sql[] =
		"SELECT (SELECT [X] FROM [DbO].[T] AS [Q] "
		"WHERE [Q].[X] = 1) AS [Z] FROM abc";
	static const struct {
		const char *patch_sql;
		const char *expected_sql;
	} nested_cases[] = {
		{
			"[X] COLLATE [CaseColl] AS [Z]",
			"SELECT [X] COLLATE [CaseColl] AS [Z] FROM abc"
		},
		{
			"(WITH [CteX]([ColAlias]) AS "
			"(SELECT [X] FROM [DbO].[T]) "
			"SELECT [ColAlias] FROM [CteX]) AS [Z]",
			"SELECT (WITH [CteX]([ColAlias]) AS "
			"(SELECT [X] FROM [DbO].[T]) "
			"SELECT [ColAlias] FROM [CteX]) AS [Z] FROM abc"
		},
		{
			"(SELECT SUM([X]) OVER [WinCase] FROM [DbO].[T] "
			"WINDOW [WinCase] AS (PARTITION BY [X])) AS [Z]",
			"SELECT (SELECT SUM([X]) OVER [WinCase] "
			"FROM [DbO].[T] WINDOW [WinCase] AS "
			"(PARTITION BY [X])) AS [Z] FROM abc"
		}
	};
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	char *deparsed;
	size_t index;

	handle = NULL;
	reparsed = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_SQLSERVER;
	if (sqlparser_parse_with_options(
		    "SELECT * FROM abc",
		    &options,
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_select_set_target_sql(
		    handle,
		    0U,
		    0U,
		    0U,
		    patch_sql,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: nested identifier patch setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
		if (!statement_has_name(handle, 0U, &names[index])) {
			fprintf(
				stderr,
				"FAIL: nested identifier patch lost AST name %s\n",
				names[index].value);
			sqlparser_handle_destroy(handle);
			return 0;
		}
	}
	if (sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strcmp(deparsed, expected_sql) != 0 ||
	    sqlparser_parse_with_options(
		    deparsed,
		    &options,
		    &reparsed,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: nested identifier patch changed on deparse: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);

	handle = NULL;
	reparsed = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse_with_options(
		    "SELECT * FROM abc",
		    &options,
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_select_set_target_sql(
		    handle,
		    0U,
		    0U,
		    0U,
		    "(SELECT [Q].[X] FROM "
		    "(SELECT [X] FROM [DbO].[T]) AS [Q]) AS [Z]",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strcmp(
		    deparsed,
		    "SELECT (SELECT [Q].[X] FROM "
		    "(SELECT [X] FROM [DbO].[T]) AS [Q]) AS [Z] FROM abc") !=
		    0 ||
	    sqlparser_parse_with_options(
		    deparsed,
		    &options,
		    &reparsed,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: derived-table alias patch changed on deparse: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	for (index = 0U;
	     index < sizeof(nested_cases) / sizeof(nested_cases[0]);
	     index++) {
		handle = NULL;
		reparsed = NULL;
		deparsed = NULL;
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse_with_options(
			    "SELECT * FROM abc",
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_select_set_target_sql(
			    handle,
			    0U,
			    0U,
			    0U,
			    nested_cases[index].patch_sql,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(
			    deparsed,
			    nested_cases[index].expected_sql) != 0 ||
		    sqlparser_parse_with_options(
			    deparsed,
			    &options,
			    &reparsed,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: nested identifier case %lu changed on deparse: %s\n",
				(unsigned long)index,
				deparsed != NULL ? deparsed : error.message);
			sqlparser_handle_destroy(reparsed);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int patched_alias_locations_preserve_spelling(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *patch_sql;
		const char *expected_sql;
		size_t alias_count;
		size_t column_alias_count;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"(SELECT \"Dup\".\"C1\", Dup.C1 FROM "
			"source_table \"Dup\", "
			"(SELECT 1) Dup(C1))",
			"SELECT (SELECT \"Dup\".\"C1\", Dup.C1 FROM "
			"source_table \"Dup\", "
			"(SELECT 1) Dup(C1)) FROM base_table",
			2U,
			1U
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"(SELECT `Dup`.`C1`, Dup.C1 FROM "
			"source_table `Dup`, "
			"(SELECT 1) Dup(C1))",
			"SELECT (SELECT `Dup`.`C1`, Dup.C1 FROM "
			"source_table `Dup`, "
			"(SELECT 1) Dup(C1)) FROM base_table",
			2U,
			1U
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"(SELECT [Dup].[C1], Dup.C1, \"Dup\".\"C1\" FROM "
			"source_table [Dup], "
			"(SELECT 1) Dup(C1), "
			"(SELECT 1) \"Dup\"(\"C1\"))",
			"SELECT (SELECT [Dup].[C1], Dup.C1, \"Dup\".\"C1\" FROM "
			"source_table [Dup], "
			"(SELECT 1) Dup(C1), "
			"(SELECT 1) \"Dup\"(\"C1\")) FROM base_table",
			3U,
			2U
		}
	};
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_name_view_t name;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	sqlparser_query_graph_view_t reparsed_graph;
	sqlparser_statement_kind_t kind;
	sqlparser_statement_kind_t reparsed_kind;
	sqlparser_error_t error;
	char *deparsed;
	size_t alias_count;
	size_t case_index;
	size_t column_alias_count;
	size_t name_count;
	size_t name_index;

	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		handle = NULL;
		reparsed = NULL;
		deparsed = NULL;
		alias_count = 0U;
		column_alias_count = 0U;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    "SELECT * FROM base_table",
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_select_set_target_sql(
			    handle,
			    0U,
			    0U,
			    0U,
			    cases[case_index].patch_sql,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_name_count(
			    handle,
			    0U,
			    &name_count,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: alias location case %lu setup failed: %s\n",
				(unsigned long)case_index,
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		for (name_index = 0U; name_index < name_count; name_index++) {
			if (sqlparser_statement_name(
				    handle,
				    0U,
				    name_index,
				    &name,
				    &error) != SQLPARSER_STATUS_OK ||
			    name.owner_type == NULL ||
			    name.field_name == NULL ||
			    name.value == NULL ||
			    strcmp(name.owner_type, "Alias") != 0) {
				continue;
			}
			if (strcmp(name.field_name, "aliasname") == 0 &&
			    strcmp(name.value, "Dup") == 0) {
				alias_count++;
			} else if (strcmp(name.field_name, "colnames") == 0 &&
				   strcmp(name.value, "C1") == 0) {
				column_alias_count++;
			}
		}
		if (alias_count != cases[case_index].alias_count ||
		    column_alias_count !=
			    cases[case_index].column_alias_count ||
		    sqlparser_statement_kind(
			    handle,
			    0U,
			    &kind,
			    &error) != SQLPARSER_STATUS_OK ||
		    kind != SQLPARSER_STATEMENT_KIND_SELECT ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(deparsed, cases[case_index].expected_sql) != 0 ||
		    sqlparser_parse_with_options(
			    deparsed,
			    &options,
			    &reparsed,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_kind(
			    reparsed,
			    0U,
			    &reparsed_kind,
			    &error) != SQLPARSER_STATUS_OK ||
		    reparsed_kind != kind ||
		    sqlparser_statement_query_graph(
			    reparsed,
			    0U,
			    &reparsed_graph,
			    &error) != SQLPARSER_STATUS_OK ||
		    reparsed_graph.block_count != graph.block_count ||
		    reparsed_graph.relation_count != graph.relation_count ||
		    reparsed_graph.target_count != graph.target_count ||
		    reparsed_graph.field_count != graph.field_count ||
		    reparsed_graph.value_count != graph.value_count ||
		    reparsed_graph.predicate_count != graph.predicate_count) {
			fprintf(
				stderr,
				"FAIL: alias location case %lu changed spelling or semantics: %s\n",
				(unsigned long)case_index,
				deparsed != NULL ? deparsed : error.message);
			sqlparser_handle_destroy(reparsed);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int patched_identifier_owner_spelling_preserved(void)
{
	static const struct {
		const char *name;
		const char *patch_sql;
		const char *expected_sql;
	} cases[] = {
		{
			"alias-and-cte-columns-over-ten",
			"(WITH \"CteCase\"("
			"\"C01\", C02, \"C03\", C04, \"C05\", C06, "
			"\"C07\", C08, \"C09\", C10, \"C11\", C12"
			") AS (VALUES (1, 2, 3, 4, 5, 6, "
			"7, 8, 9, 10, 11, 12)) "
			"SELECT * FROM \"CteCase\" \"AliasCase\"("
			"\"A01\", A02, \"A03\", A04, \"A05\", A06, "
			"\"A07\", A08, \"A09\", A10, \"A11\", A12))",
			"SELECT (WITH \"CteCase\"("
			"\"C01\", C02, \"C03\", C04, \"C05\", C06, "
			"\"C07\", C08, \"C09\", C10, \"C11\", C12"
			") AS (VALUES (1, 2, 3, 4, 5, 6, "
			"7, 8, 9, 10, 11, 12)) "
			"SELECT * FROM \"CteCase\" \"AliasCase\"("
			"\"A01\", A02, \"A03\", A04, \"A05\", A06, "
			"\"A07\", A08, \"A09\", A10, \"A11\", A12)) "
			"FROM base_table"
		},
		{
			"join-using",
			"(SELECT * FROM LeftTable l JOIN RightTable r "
			"USING (\"QuotedKey\", PlainKey, \"OtherKey\"))",
			"SELECT (SELECT * FROM LeftTable l JOIN RightTable r "
			"USING (\"QuotedKey\", PlainKey, \"OtherKey\")) "
			"FROM base_table"
		},
		{
			"multiple-window-definitions",
			"(SELECT sum(ValueCol) OVER \"QuotedWin\", "
			"count(*) OVER PlainWin FROM SourceTable "
			"WINDOW \"QuotedWin\" AS (ORDER BY ValueCol), "
			"PlainWin AS (ORDER BY ValueCol))",
			"SELECT (SELECT sum(ValueCol) OVER \"QuotedWin\", "
			"count(*) OVER PlainWin FROM SourceTable "
			"WINDOW \"QuotedWin\" AS (ORDER BY ValueCol), "
			"PlainWin AS (ORDER BY ValueCol)) FROM base_table"
		},
		{
			"cte-search-cycle",
			"(WITH RECURSIVE SearchGraph(\"FromId\", ToId) AS "
			"(VALUES (1, 2) UNION ALL "
			"SELECT \"FromId\", ToId FROM SearchGraph) "
			"SEARCH DEPTH FIRST BY \"FromId\", ToId SET SearchSeq "
			"CYCLE \"FromId\", ToId SET IsCycle USING PathCols "
			"SELECT * FROM SearchGraph)",
			"SELECT (WITH RECURSIVE SearchGraph(\"FromId\", ToId) AS "
			"(VALUES (1, 2) UNION ALL "
			"SELECT \"FromId\", ToId FROM SearchGraph) "
			"SEARCH DEPTH FIRST BY \"FromId\", ToId SET SearchSeq "
			"CYCLE \"FromId\", ToId SET IsCycle USING PathCols "
			"SELECT * FROM SearchGraph) FROM base_table"
		},
		{
			"column-definition",
			"(SELECT * FROM json_to_record("
			"'{\"MixedCol\":1,\"PlainCol\":\"x\"}') "
			"RecAlias(\"MixedCol\" int, PlainCol text))",
			"SELECT (SELECT * FROM json_to_record("
			"'{\"MixedCol\":1,\"PlainCol\":\"x\"}') "
			"RecAlias(\"MixedCol\" int, PlainCol text)) "
			"FROM base_table"
		},
		{
			"xml-expression",
			"xmlelement(name ElementCase, "
			"xmlattributes(1 AS \"AttrCase\", 2 AS PlainAttr), "
			"'content')",
			"SELECT xmlelement(name ElementCase, "
			"xmlattributes(1 AS \"AttrCase\", 2 AS PlainAttr), "
			"'content') FROM base_table"
		}
	};
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_parse_options_t options;
	sqlparser_query_graph_view_t graph;
	sqlparser_query_graph_view_t reparsed_graph;
	sqlparser_statement_kind_t kind;
	sqlparser_statement_kind_t reparsed_kind;
	sqlparser_error_t error;
	char *deparsed;
	size_t case_index;
	size_t name_count;
	size_t reparsed_name_count;
	int all_valid;
	int graph_valid;

	all_valid = 1;
	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		handle = NULL;
		reparsed = NULL;
		deparsed = NULL;
		graph_valid = 0;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = SQLPARSER_DIALECT_POSTGRESQL;
		if (sqlparser_parse_with_options(
			    "SELECT * FROM base_table",
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_select_set_target_sql(
			    handle,
			    0U,
			    0U,
			    0U,
			    cases[case_index].patch_sql,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier owner case %s setup failed: %s\n",
				cases[case_index].name,
				error.message);
			sqlparser_handle_destroy(handle);
			all_valid = 0;
			continue;
		}
		memset(&error, 0, sizeof(error));
		if (sqlparser_validate_ast_identifier_spelling(handle, &error) !=
		    SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier owner case %s failed AST spelling audit: %s\n",
				cases[case_index].name,
				error.message);
			all_valid = 0;
		}
		memset(&error, 0, sizeof(error));
		if (sqlparser_statement_kind(
			    handle,
			    0U,
			    &kind,
			    &error) != SQLPARSER_STATUS_OK ||
		    kind != SQLPARSER_STATEMENT_KIND_SELECT ||
		    sqlparser_statement_name_count(
			    handle,
			    0U,
			    &name_count,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK ||
		    graph.block_count == 0U ||
		    graph.target_count == 0U) {
			fprintf(
				stderr,
				"FAIL: identifier owner case %s View setup failed: %s\n",
				cases[case_index].name,
				error.message);
			all_valid = 0;
		} else {
			graph_valid = 1;
		}
		memset(&error, 0, sizeof(error));
		if (sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL) {
			fprintf(
				stderr,
				"FAIL: identifier owner case %s did not deparse: %s\n",
				cases[case_index].name,
				error.message);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			all_valid = 0;
			continue;
		}
		if (strcmp(deparsed, cases[case_index].expected_sql) != 0) {
			fprintf(
				stderr,
				"FAIL: identifier owner case %s changed spelling\n"
				"expected: %s\n"
				"output:   %s\n",
				cases[case_index].name,
				cases[case_index].expected_sql,
				deparsed);
			all_valid = 0;
		}
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse_with_options(
			    deparsed,
			    &options,
			    &reparsed,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier owner case %s did not reparse: %s\n",
				cases[case_index].name,
				error.message);
			all_valid = 0;
		} else {
			memset(&error, 0, sizeof(error));
			if (sqlparser_validate_ast_identifier_spelling(
			    reparsed,
			    &error) != SQLPARSER_STATUS_OK) {
				fprintf(
					stderr,
					"FAIL: identifier owner case %s reparsed AST spelling audit failed: %s\n",
					cases[case_index].name,
					error.message);
				all_valid = 0;
			}
		}
		memset(&error, 0, sizeof(error));
		if (reparsed != NULL &&
		    graph_valid &&
		    (sqlparser_statement_kind(
			    reparsed,
			    0U,
			    &reparsed_kind,
			    &error) != SQLPARSER_STATUS_OK ||
		    reparsed_kind != kind ||
		    sqlparser_statement_name_count(
			    reparsed,
			    0U,
			    &reparsed_name_count,
			    &error) != SQLPARSER_STATUS_OK ||
		    reparsed_name_count != name_count ||
		    sqlparser_statement_query_graph(
			    reparsed,
			    0U,
			    &reparsed_graph,
			    &error) != SQLPARSER_STATUS_OK ||
		    reparsed_graph.block_count != graph.block_count ||
		    reparsed_graph.relation_count != graph.relation_count ||
		    reparsed_graph.target_count != graph.target_count ||
		    reparsed_graph.field_count != graph.field_count ||
		    reparsed_graph.value_count != graph.value_count ||
		    reparsed_graph.predicate_count != graph.predicate_count)) {
			fprintf(
				stderr,
				"FAIL: identifier owner case %s changed View semantics: %s\n",
				cases[case_index].name,
				error.message);
			all_valid = 0;
		}
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	return all_valid;
}

static int generated_identifier_preserves_patch_delimiter(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *patch_target;
		const char *expected_target;
		const char *expected_sql;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"\"OldName\"",
			"\"NeWName\"",
			"SELECT \"NeWName\", BaseCol FROM SourceTable"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"`OldName`",
			"`NeWName`",
			"SELECT `NeWName`, BaseCol FROM SourceTable"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"[OldName]",
			"[NeWName]",
			"SELECT [NeWName], BaseCol FROM SourceTable"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"\"OldName\"",
			"\"NeWName\"",
			"SELECT \"NeWName\", BaseCol FROM SourceTable"
		}
	};
	sqlparser_handle_t *handle;
	sqlparser_parse_options_t options;
	sqlparser_error_t error;
	char *deparsed;
	char *target_sql;
	expected_name_t renamed_name;
	size_t case_index;
	size_t target_index;

	renamed_name.owner_type = "ColumnRef";
	renamed_name.field_name = "fields";
	renamed_name.value = "NeWName";
	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		handle = NULL;
		deparsed = NULL;
		target_sql = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    "SELECT BaseCol FROM SourceTable",
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_select_insert_target_sql(
			    handle,
			    0U,
			    0U,
			    0U,
			    cases[case_index].patch_target,
			    &error) != SQLPARSER_STATUS_OK ||
		    !statement_find_name_index(
			    handle,
			    0U,
			    "ColumnRef",
			    "fields",
			    "OldName",
			    &target_index) ||
		    sqlparser_statement_set_name(
			    handle,
			    0U,
			    target_index,
			    "NeWName",
			    &error) != SQLPARSER_STATUS_OK ||
		    !statement_has_name(handle, 0U, &renamed_name) ||
		    handle->identifier_mutation_count != 1U ||
		    handle->identifier_mutations[0].source_present ||
		    sqlparser_select_target_sql(
			    handle,
			    0U,
			    0U,
			    0U,
			    &target_sql,
			    &error) != SQLPARSER_STATUS_OK ||
		    target_sql == NULL ||
		    strcmp(target_sql, cases[case_index].expected_target) != 0 ||
		    sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(deparsed, cases[case_index].expected_sql) != 0) {
			fprintf(
				stderr,
				"FAIL: renamed patched identifier case %lu lost its delimiter: %s\n",
				(unsigned long)case_index,
				deparsed != NULL ?
					deparsed :
					(target_sql != NULL ? target_sql : error.message));
			sqlparser_string_free(target_sql);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_string_free(target_sql);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int identifier_role_classifier_is_fail_closed(void)
{
	static const struct {
		const char *sql;
		const char *owner_type;
		const char *field_name;
		const char *value;
	} denied[] = {
		{
			"CREATE ROLE r SUPERUSER",
			"DefElem",
			"defname",
			"superuser"
		},
		{
			"CREATE FUNCTION f() RETURNS integer LANGUAGE sql "
			"IMMUTABLE AS 'SELECT 1'",
			"DefElem",
			"arg",
			"immutable"
		},
		{
			"SELECT 1 IN (1)",
			"AExpr",
			"name",
			"="
		},
		{
			"SELECT 1 = ANY (SELECT 1)",
			"SubLink",
			"oper_name",
			"="
		},
		{
			"SELECT a FROM t ORDER BY a USING >",
			"SortBy",
			"use_op",
			">"
		},
		{
			"CREATE TABLE t (a int, EXCLUDE USING btree (a WITH =))",
			"Constraint",
			"exclusions",
			"="
		},
		{
			"CREATE DOMAIN d AS int NOT NULL",
			"Constraint",
			"keys",
			"value"
		},
		{
			"CREATE TABLE identity_t "
			"(id bigint GENERATED ALWAYS AS IDENTITY)",
			"Constraint",
			"generated_when",
			"a"
		},
		{
			"CREATE TABLE child_t "
			"(id int REFERENCES parent_t(id) MATCH FULL "
			"ON UPDATE CASCADE ON DELETE SET NULL)",
			"Constraint",
			"fk_matchtype",
			"f"
		},
		{
			"CREATE TABLE child_t "
			"(id int REFERENCES parent_t(id) MATCH FULL "
			"ON UPDATE CASCADE ON DELETE SET NULL)",
			"Constraint",
			"fk_upd_action",
			"c"
		},
		{
			"CREATE TABLE child_t "
			"(id int REFERENCES parent_t(id) MATCH FULL "
			"ON UPDATE CASCADE ON DELETE SET NULL)",
			"Constraint",
			"fk_del_action",
			"n"
		},
		{
			"CREATE TABLE child_p PARTITION OF parent_p "
			"FOR VALUES FROM (1) TO (2)",
			"PartitionBoundSpec",
			"strategy",
			"r"
		},
		{
			"ALTER TABLE t REPLICA IDENTITY FULL",
			"ReplicaIdentityStmt",
			"identity_type",
			"f"
		},
		{
			"ALTER DOMAIN d SET NOT NULL",
			"AlterDomainStmt",
			"subtype",
			"N"
		},
		{
			"CREATE ACCESS METHOD am TYPE INDEX HANDLER h",
			"CreateAmStmt",
			"amtype",
			"i"
		},
		{
			"ALTER EVENT TRIGGER et ENABLE",
			"AlterEventTrigStmt",
			"tgenabled",
			"O"
		},
		{
			"SET TRANSACTION ISOLATION LEVEL SERIALIZABLE",
			"VariableSetStmt",
			"name",
			"TRANSACTION"
		},
		{
			"CREATE POLICY p ON t FOR SELECT USING (true)",
			"CreatePolicyStmt",
			"cmd_name",
			"select"
		},
		{
			"SELECT 'a' SIMILAR TO 'a'",
			"FuncCall",
			"funcname",
			"similar_to_escape"
		},
		{
			"SELECT 'a' LIKE 'a' ESCAPE '!'",
			"FuncCall",
			"funcname",
			"pg_catalog"
		},
		{
			"SELECT 'a' LIKE 'a' ESCAPE '!'",
			"FuncCall",
			"funcname",
			"like_escape"
		},
		{
			"CREATE TEMP TABLE t(a int)",
			"RangeVar",
			"relpersistence",
			"t"
		},
		{
			"CREATE TYPE mood AS ENUM ('sad')",
			"CreateEnumStmt",
			"vals",
			"sad"
		},
		{
			"CREATE TRIGGER tr BEFORE INSERT ON t "
			"EXECUTE FUNCTION f('arg')",
			"CreateTrigStmt",
			"args",
			"arg"
		},
		{
			"COMMENT ON TABLE t IS 'comment'",
			"CommentStmt",
			"comment",
			"comment"
		},
		{
			"NOTIFY c, 'payload'",
			"NotifyStmt",
			"payload",
			"payload"
		},
		{
			"SELECT 'LiteralCase'",
			"AConst",
			"sval",
			"LiteralCase"
		}
	};
	static const expected_name_t explicit_function_schema = {
		"FuncCall",
		"funcname",
		"public"
	};
	static const expected_name_t explicit_function_name = {
		"FuncCall",
		"funcname",
		"MixedFn"
	};
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	char *deparsed;
	size_t index;
	size_t option_index;

	for (index = 0U; index < sizeof(denied) / sizeof(denied[0]); index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		if (sqlparser_parse(denied[index].sql, &handle, &error) !=
		    SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier role case did not parse: %s: %s\n",
				denied[index].sql,
				error.message);
			return 0;
		}
		if (statement_find_name_index(
			    handle,
			    0U,
			    denied[index].owner_type,
			    denied[index].field_name,
			    denied[index].value,
			    &option_index)) {
			fprintf(
				stderr,
				"FAIL: semantic string was exposed as name: %s.%s=%s\n",
				denied[index].owner_type,
				denied[index].field_name,
				denied[index].value);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_handle_destroy(handle);
	}

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT lhs LIKE public.\"MixedFn\"(1) FROM source_table",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_has_name(handle, 0U, &explicit_function_schema) ||
	    !statement_has_name(handle, 0U, &explicit_function_name)) {
		fprintf(
			stderr,
			"FAIL: explicit pattern RHS function identifiers were not exposed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	deparsed = NULL;
	option_index = (size_t)-1;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "CREATE FOREIGN TABLE ft(a int) SERVER s "
		    "OPTIONS (\"RemoteKey\" 'v')",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_find_name_index(
		    handle,
		    0U,
		    "DefElem",
		    "defname",
		    "RemoteKey",
		    &option_index) ||
	    sqlparser_statement_set_name(
		    handle,
		    0U,
		    option_index,
		    "NewKey",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "NewKey") == NULL) {
		fprintf(
			stderr,
			"FAIL: generic option identifier was not safely mutable: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int defelem_context_classifier_is_fail_closed(void)
{
	static const struct {
		const char *owner_type;
		const char *field_name;
		const char *defelem_field;
		int expected;
	} cases[] = {
		{
			"CreateForeignTableStmt",
			"options",
			"defname",
			1
		},
		{
			"CreateForeignTableStmt",
			"options",
			"defnamespace",
			0
		},
		{
			"IndexElem",
			"opclassopts",
			"defnamespace",
			1
		},
		{
			"CreateRoleStmt",
			"options",
			"defname",
			0
		},
		{
			"CreateFunctionStmt",
			"options",
			"defnamespace",
			0
		}
	};
	PgQuery__DefElem def_elem = PG_QUERY__DEF_ELEM__INIT;
	PgQuery__AlterTableCmd alter_table_cmd =
		PG_QUERY__ALTER_TABLE_CMD__INIT;
	PgQuery__Constraint constraint = PG_QUERY__CONSTRAINT__INIT;
	const ProtobufCFieldDescriptor *defname_field;
	const ProtobufCFieldDescriptor *defnamespace_field;
	sqlparser_name_context_t dynamic_context;
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		sqlparser_name_context_t context;
		const ProtobufCFieldDescriptor *field;
		int actual;

		memset(&context, 0, sizeof(context));
		context.owner_type = cases[index].owner_type;
		context.field_name = cases[index].field_name;
		field = protobuf_c_message_descriptor_get_field_by_name(
			def_elem.base.descriptor,
			cases[index].defelem_field);
		actual = field != NULL &&
			sqlparser_name_atom_is_identifier(
				(ProtobufCMessage *)&def_elem,
				&context,
				def_elem.base.descriptor,
				field);
		if (field == NULL || actual != cases[index].expected) {
			fprintf(
				stderr,
				"FAIL: DefElem context classifier mismatch: %s.%s -> %s\n",
				cases[index].owner_type,
				cases[index].field_name,
				cases[index].defelem_field);
			return 0;
		}
	}

	defname_field = protobuf_c_message_descriptor_get_field_by_name(
		def_elem.base.descriptor,
		"defname");
	defnamespace_field = protobuf_c_message_descriptor_get_field_by_name(
		def_elem.base.descriptor,
		"defnamespace");
	if (defname_field == NULL || defnamespace_field == NULL) {
		fprintf(stderr, "FAIL: DefElem name descriptor is missing\n");
		return 0;
	}

	memset(&dynamic_context, 0, sizeof(dynamic_context));
	dynamic_context.owner_type = "Constraint";
	dynamic_context.field_name = "options";
	dynamic_context.field_owner = (ProtobufCMessage *)&constraint;
	constraint.contype = PG_QUERY__CONSTR_TYPE__CONSTR_PRIMARY;
	if (!sqlparser_name_atom_is_identifier(
		    (ProtobufCMessage *)&def_elem,
		    &dynamic_context,
		    def_elem.base.descriptor,
		    defname_field)) {
		fprintf(
			stderr,
			"FAIL: index constraint reloption was not exposed as a name\n");
		return 0;
	}
	constraint.contype = PG_QUERY__CONSTR_TYPE__CONSTR_IDENTITY;
	if (sqlparser_name_atom_is_identifier(
		    (ProtobufCMessage *)&def_elem,
		    &dynamic_context,
		    def_elem.base.descriptor,
		    defname_field)) {
		fprintf(
			stderr,
			"FAIL: identity sequence option was exposed as a name\n");
		return 0;
	}

	memset(&dynamic_context, 0, sizeof(dynamic_context));
	dynamic_context.owner_type = "AlterTableCmd";
	dynamic_context.field_name = "def";
	dynamic_context.field_owner = (ProtobufCMessage *)&alter_table_cmd;
	alter_table_cmd.subtype =
		PG_QUERY__ALTER_TABLE_TYPE__AT_AlterColumnGenericOptions;
	if (!sqlparser_name_atom_is_identifier(
		    (ProtobufCMessage *)&def_elem,
		    &dynamic_context,
		    def_elem.base.descriptor,
		    defname_field)) {
		fprintf(
			stderr,
			"FAIL: ALTER TABLE generic option was not exposed as a name\n");
		return 0;
	}
	if (sqlparser_name_atom_is_identifier(
		    (ProtobufCMessage *)&def_elem,
		    &dynamic_context,
		    def_elem.base.descriptor,
		    defnamespace_field)) {
		fprintf(
			stderr,
			"FAIL: ALTER TABLE generic option namespace was exposed as a name\n");
		return 0;
	}
	alter_table_cmd.subtype =
		PG_QUERY__ALTER_TABLE_TYPE__AT_SetRelOptions;
	if (!sqlparser_name_atom_is_identifier(
		    (ProtobufCMessage *)&def_elem,
		    &dynamic_context,
		    def_elem.base.descriptor,
		    defnamespace_field)) {
		fprintf(
			stderr,
			"FAIL: ALTER TABLE reloption namespace was not exposed as a name\n");
		return 0;
	}
	alter_table_cmd.subtype = PG_QUERY__ALTER_TABLE_TYPE__AT_SetIdentity;
	if (sqlparser_name_atom_is_identifier(
		    (ProtobufCMessage *)&def_elem,
		    &dynamic_context,
		    def_elem.base.descriptor,
		    defname_field)) {
		fprintf(
			stderr,
			"FAIL: ALTER TABLE identity option was exposed as a name\n");
		return 0;
	}
	return 1;
}

static int semantic_control_fields_fail_closed(void)
{
	PgQuery__RangeVar range_var = PG_QUERY__RANGE_VAR__INIT;
	PgQuery__Aggref aggref = PG_QUERY__AGGREF__INIT;
	PgQuery__ColumnDef column_def = PG_QUERY__COLUMN_DEF__INIT;
	PgQuery__PartitionBoundSpec partition_bound =
		PG_QUERY__PARTITION_BOUND_SPEC__INIT;
	PgQuery__RangeTblEntry range_tbl_entry =
		PG_QUERY__RANGE_TBL_ENTRY__INIT;
	PgQuery__ReplicaIdentityStmt replica_identity =
		PG_QUERY__REPLICA_IDENTITY_STMT__INIT;
	PgQuery__AlterDomainStmt alter_domain =
		PG_QUERY__ALTER_DOMAIN_STMT__INIT;
	PgQuery__Constraint constraint = PG_QUERY__CONSTRAINT__INIT;
	PgQuery__CreateAmStmt create_am = PG_QUERY__CREATE_AM_STMT__INIT;
	PgQuery__AlterEventTrigStmt alter_event_trigger =
		PG_QUERY__ALTER_EVENT_TRIG_STMT__INIT;
	const struct {
		ProtobufCMessage *message;
		const char *field_name;
	} cases[] = {
		{(ProtobufCMessage *)&range_var, "relpersistence"},
		{(ProtobufCMessage *)&aggref, "aggkind"},
		{(ProtobufCMessage *)&column_def, "storage"},
		{(ProtobufCMessage *)&column_def, "identity"},
		{(ProtobufCMessage *)&column_def, "generated"},
		{(ProtobufCMessage *)&partition_bound, "strategy"},
		{(ProtobufCMessage *)&range_tbl_entry, "relkind"},
		{(ProtobufCMessage *)&replica_identity, "identity_type"},
		{(ProtobufCMessage *)&alter_domain, "subtype"},
		{(ProtobufCMessage *)&constraint, "generated_when"},
		{(ProtobufCMessage *)&constraint, "fk_matchtype"},
		{(ProtobufCMessage *)&constraint, "fk_upd_action"},
		{(ProtobufCMessage *)&constraint, "fk_del_action"},
		{(ProtobufCMessage *)&create_am, "amtype"},
		{(ProtobufCMessage *)&alter_event_trigger, "tgenabled"}
	};
	size_t index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		const ProtobufCFieldDescriptor *field;
		sqlparser_name_search_t search;
		char **slot;
		char *saved;
		sqlparser_status_t status;

		field = protobuf_c_message_descriptor_get_field_by_name(
			cases[index].message->descriptor,
			cases[index].field_name);
		if (field == NULL ||
		    field->type != PROTOBUF_C_TYPE_STRING ||
		    sqlparser_name_atom_is_identifier(
			    cases[index].message,
			    NULL,
			    cases[index].message->descriptor,
			    field)) {
			fprintf(
				stderr,
				"FAIL: semantic control field was exposed as a name: %s.%s\n",
				cases[index].message->descriptor->short_name,
				cases[index].field_name);
			return 0;
		}
		slot = (char **)((unsigned char *)cases[index].message +
				field->offset);
		saved = *slot;
		*slot = (char *)"x";
		memset(&search, 0, sizeof(search));
		status = sqlparser_walk_message_names(
			cases[index].message,
			NULL,
			&search);
		*slot = saved;
		if (status != SQLPARSER_STATUS_OK || search.seen != 0U) {
			fprintf(
				stderr,
				"FAIL: semantic control field entered the public name walk: %s.%s\n",
				cases[index].message->descriptor->short_name,
				cases[index].field_name);
			return 0;
		}
	}
	return 1;
}

static int graph_name_selector_uses_public_name_ordinal(void)
{
	sqlparser_graph_field_t field;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	sqlparser_error_t error;
	char *deparsed;
	size_t field_index;
	int found;

	handle = NULL;
	deparsed = NULL;
	found = 0;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT 'LiteralCase', 1 = ANY (SELECT 1), "
		    "'a' LIKE 'a' ESCAPE '!', Wanted FROM t",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(
		    handle,
		    0U,
		    &graph,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: graph selector alignment setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	for (field_index = 0U; field_index < graph.field_count; field_index++) {
		memset(&field, 0, sizeof(field));
		if (sqlparser_query_graph_field_at(
			    &graph,
			    field_index,
			    &field,
			    &error) == SQLPARSER_STATUS_OK &&
		    field.column_name != NULL &&
		    strcmp(field.column_name, "Wanted") == 0 &&
		    field.has_selector) {
			found = 1;
			break;
		}
	}
	if (!found ||
	    sqlparser_selector_set_name(
		    handle,
		    &field.selector,
		    "Changed",
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(deparsed, "'LiteralCase'") == NULL ||
	    strstr(deparsed, "= ANY") == NULL ||
	    strstr(deparsed, "ESCAPE '!'") == NULL ||
	    strstr(deparsed, "Changed") == NULL ||
	    strstr(deparsed, "FROM t") == NULL ||
	    strstr(deparsed, "Wanted") != NULL) {
		fprintf(
			stderr,
			"FAIL: graph name selector ordinal was not aligned: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int unconsumed_identifier_tag_fails_closed(void)
{
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	PgQuery__Node *statement;
	PgQuery__NotifyStmt *notify_stmt;
	char *deparsed;
	size_t mutation_index;
	size_t target_index;
	int mutation_created;
	sqlparser_status_t status;

	handle = NULL;
	deparsed = NULL;
	statement = NULL;
	notify_stmt = NULL;
	mutation_index = 0U;
	mutation_created = 0;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "NOTIFY MixedChannel, 'PayloadCase'",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: unconsumed tag parse failed: %s\n",
			error.message);
		return 0;
	}
	if (statement_find_name_index(
		    handle,
		    0U,
		    "NotifyStmt",
		    "payload",
		    "PayloadCase",
		    &target_index)) {
		fprintf(stderr, "FAIL: NOTIFY payload was exposed as a name\n");
		sqlparser_handle_destroy(handle);
		return 0;
	}
	if (sqlparser_get_statement_node(
		    handle,
		    0U,
		    &statement,
		    &error) != SQLPARSER_STATUS_OK ||
	    statement == NULL ||
	    statement->notify_stmt == NULL) {
		fprintf(
			stderr,
			"FAIL: unconsumed tag statement lookup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	notify_stmt = statement->notify_stmt;
	if (sqlparser_handle_prepare_identifier_mutation(
		    handle,
		    0U,
		    &notify_stmt->payload,
		    (ProtobufCMessage *)notify_stmt,
		    &mutation_index,
		    &mutation_created,
		    &error) != SQLPARSER_STATUS_OK ||
	    !mutation_created ||
	    sqlparser_replace_proto_string(
		    &notify_stmt->payload,
		    "ChangedPayload",
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: unconsumed tag setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	handle->identifier_mutations[mutation_index].slot =
		&notify_stmt->payload;
	handle->identifier_mutations[mutation_index].value =
		notify_stmt->payload;
	if (sqlparser_handle_commit_ast(handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: unconsumed tag commit failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	status = sqlparser_deparse(handle, &deparsed, &error);
	if (status != SQLPARSER_STATUS_INTERNAL_ERROR ||
	    deparsed != NULL) {
		fprintf(
			stderr,
			"FAIL: unconsumed identifier tag did not fail closed\n");
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(handle);
	return 1;
}

static int patched_identifier_atoms_preserve_all_dialects(void)
{
	static const struct {
		sqlparser_dialect_t dialect;
		const char *alias_sql;
		const char *alias_value;
		const char *relation_sql;
		const char *relation_value;
	} cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"U&\"Mi\\0058ed\"",
			"MiXed",
			"U&\"d!0061t\" UESCAPE '!'",
			"dat"
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"`A``Case`",
			"A`Case",
			"`T``Case`",
			"T`Case"
		},
		{
			SQLPARSER_DIALECT_ORACLE,
			"\"A\"\"Case\"",
			"A\"Case",
			"\"T\"\"Case\"",
			"T\"Case"
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"[A]]Case]",
			"A]Case",
			"[T]]Case]",
			"T]Case"
		},
		{
			SQLPARSER_DIALECT_DAMENG,
			"\"A\"\"Case\"",
			"A\"Case",
			"\"T\"\"Case\"",
			"T\"Case"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_ORACLE,
			"\"A\"\"Case\"",
			"A\"Case",
			"\"T\"\"Case\"",
			"T\"Case"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_MYSQL,
			"`A``Case`",
			"A`Case",
			"`T``Case`",
			"T`Case"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
			"U&\"Mi\\0058ed\"",
			"MiXed",
			"U&\"d!0061t\" UESCAPE '!'",
			"dat"
		},
		{
			SQLPARSER_DIALECT_VASTBASE_SQLSERVER,
			"[A]]Case]",
			"A]Case",
			"[T]]Case]",
			"T]Case"
		}
	};
	sqlparser_parse_options_t options;
	size_t case_index;

	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		expected_name_t alias_name;
		expected_name_t relation_name;
		sqlparser_error_t error;
		sqlparser_graph_relation_t graph_relation;
		sqlparser_graph_target_t graph_target;
		sqlparser_handle_t *handle;
		sqlparser_handle_t *reparsed;
		sqlparser_patch_list_t patch_list;
		sqlparser_patch_t patches[2];
		sqlparser_query_graph_view_t graph;
		sqlparser_relation_view_t relation;
		char expected_sql[512];
		char name_selector[64];
		char *deparsed;
		size_t alias_index;
		unsigned long generation;

		handle = NULL;
		reparsed = NULL;
		deparsed = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    "SELECT OldCol AS OldAlias FROM OldTable",
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    !statement_find_name_index(
			    handle,
			    0U,
			    "ResTarget",
			    "name",
			    "OldAlias",
			    &alias_index)) {
			fprintf(
				stderr,
				"FAIL: dialect identifier atom case %lu setup failed: %s\n",
				(unsigned long)case_index,
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		if (snprintf(
			    name_selector,
			    sizeof(name_selector),
			    "stmt[0].name[%lu]",
			    (unsigned long)alias_index) <= 0 ||
		    snprintf(
			    expected_sql,
			    sizeof(expected_sql),
			    "SELECT OldCol AS %s FROM %s",
			    cases[case_index].alias_sql,
			    cases[case_index].relation_sql) <= 0) {
			fprintf(
				stderr,
				"FAIL: dialect identifier atom case %lu formatting failed\n",
				(unsigned long)case_index);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		memset(patches, 0, sizeof(patches));
		patches[0].op = SQLPARSER_PATCH_REPLACE;
		patches[0].selector = name_selector;
		patches[0].sql = cases[case_index].alias_sql;
		patches[1].op = SQLPARSER_PATCH_REPLACE;
		patches[1].selector = "stmt[0].relation[0]";
		patches[1].sql = cases[case_index].relation_sql;
		patch_list.items = patches;
		patch_list.count = 2U;
		generation = handle->generation;
		if (sqlparser_apply_patch(
			    handle,
			    &patch_list,
			    &error) != SQLPARSER_STATUS_OK ||
		    handle->generation != generation + 1UL) {
			fprintf(
				stderr,
				"FAIL: dialect identifier atom case %lu batch failed or advanced generation more than once: %s\n",
				(unsigned long)case_index,
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}

		alias_name.owner_type = "ResTarget";
		alias_name.field_name = "name";
		alias_name.value = cases[case_index].alias_value;
		relation_name.owner_type = "RangeVar";
		relation_name.field_name = "relname";
		relation_name.value = cases[case_index].relation_value;
		memset(&relation, 0, sizeof(relation));
		memset(&graph, 0, sizeof(graph));
		memset(&graph_relation, 0, sizeof(graph_relation));
		memset(&graph_target, 0, sizeof(graph_target));
		if (!statement_has_name(handle, 0U, &alias_name) ||
		    !statement_has_name(handle, 0U, &relation_name) ||
		    sqlparser_statement_relation(
			    handle,
			    0U,
			    0U,
			    &relation,
			    &error) != SQLPARSER_STATUS_OK ||
		    relation.table_name == NULL ||
		    strcmp(
			    relation.table_name,
			    cases[case_index].relation_value) != 0 ||
		    sqlparser_statement_query_graph(
			    handle,
			    0U,
			    &graph,
			    &error) != SQLPARSER_STATUS_OK ||
		    graph.relation_count != 1U ||
		    graph.target_count != 1U ||
		    sqlparser_query_graph_relation_at(
			    &graph,
			    0U,
			    &graph_relation,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_query_graph_target_at(
			    &graph,
			    0U,
			    &graph_target,
			    &error) != SQLPARSER_STATUS_OK ||
		    graph_relation.object_name == NULL ||
		    strcmp(
			    graph_relation.object_name,
			    cases[case_index].relation_value) != 0 ||
		    graph_target.output_name == NULL ||
		    strcmp(
			    graph_target.output_name,
			    cases[case_index].alias_value) != 0) {
			fprintf(
				stderr,
				"FAIL: dialect identifier atom case %lu changed AST/View semantics: %s\n",
				(unsigned long)case_index,
				error.message);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		if (sqlparser_validate_ast_identifier_spelling(
			    handle,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_deparse(handle, &deparsed, &error) !=
			    SQLPARSER_STATUS_OK ||
		    deparsed == NULL ||
		    strcmp(deparsed, expected_sql) != 0 ||
		    sqlparser_parse_with_options(
			    deparsed,
			    &options,
			    &reparsed,
			    &error) != SQLPARSER_STATUS_OK ||
		    sqlparser_validate_ast_identifier_spelling(
			    reparsed,
			    &error) != SQLPARSER_STATUS_OK ||
		    !statement_has_name(reparsed, 0U, &alias_name) ||
		    !statement_has_name(reparsed, 0U, &relation_name)) {
			fprintf(
				stderr,
				"FAIL: dialect identifier atom case %lu did not round-trip exactly: %s\n",
				(unsigned long)case_index,
				deparsed != NULL ? deparsed : error.message);
			sqlparser_handle_destroy(reparsed);
			sqlparser_string_free(deparsed);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
	}
	return 1;
}

static int patched_mysql_target_list_preserves_raw_identifiers(void)
{
	static const expected_name_t first_name = {
		"ColumnRef",
		"fields",
		"a"
	};
	static const expected_name_t second_name = {
		"ColumnRef",
		"fields",
		"b,c"
	};
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patch;
	sqlparser_query_graph_view_t graph;
	char *deparsed;
	char *target_sql;
	size_t target_count;

	handle = NULL;
	reparsed = NULL;
	deparsed = NULL;
	target_sql = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	options.dialect = SQLPARSER_DIALECT_MYSQL;
	if (sqlparser_parse_with_options(
		    "SELECT * FROM abc",
		    &options,
		    &handle,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: MySQL raw target-list setup failed: %s\n",
			error.message);
		return 0;
	}
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = "stmt[0].select_targets[0]";
	patch.sql = "`a`,`b,c`";
	patch_list.items = &patch;
	patch_list.count = 1U;
	if (sqlparser_apply_patch(
		    handle,
		    &patch_list,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_select_target_count(
		    handle,
		    0U,
		    0U,
		    &target_count,
		    &error) != SQLPARSER_STATUS_OK ||
	    target_count != 2U ||
	    !statement_has_name(handle, 0U, &first_name) ||
	    !statement_has_name(handle, 0U, &second_name) ||
	    sqlparser_statement_query_graph(
		    handle,
		    0U,
		    &graph,
		    &error) != SQLPARSER_STATUS_OK ||
	    graph.target_count != 2U) {
		fprintf(
			stderr,
			"FAIL: MySQL raw target-list AST/View mismatch: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	if (sqlparser_select_target_sql(
		    handle,
		    0U,
		    0U,
		    0U,
		    &target_sql,
		    &error) != SQLPARSER_STATUS_OK ||
	    target_sql == NULL ||
	    strcmp(target_sql, "`a`") != 0) {
		fprintf(
			stderr,
			"FAIL: first MySQL raw target changed: %s\n",
			target_sql != NULL ? target_sql : error.message);
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(target_sql);
	target_sql = NULL;
	if (sqlparser_select_target_sql(
		    handle,
		    0U,
		    0U,
		    1U,
		    &target_sql,
		    &error) != SQLPARSER_STATUS_OK ||
	    target_sql == NULL ||
	    strcmp(target_sql, "`b,c`") != 0 ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strcmp(deparsed, "SELECT `a`, `b,c` FROM abc") != 0 ||
	    sqlparser_parse_with_options(
		    deparsed,
		    &options,
		    &reparsed,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_validate_ast_identifier_spelling(
		    reparsed,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: MySQL raw target-list did not round-trip: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(deparsed);
		sqlparser_string_free(target_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(deparsed);
	sqlparser_string_free(target_sql);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int identifier_patch_batch_is_atomic(void)
{
	static const char baseline_sql[] =
		"SELECT OldCol AS OldAlias FROM OldTable; "
		"SELECT KeepCol FROM KeepTable";
	static const expected_name_t old_alias = {
		"ResTarget",
		"name",
		"OldAlias"
	};
	sqlparser_error_t error;
	sqlparser_graph_relation_t old_graph_relation;
	sqlparser_graph_target_t old_graph_target;
	sqlparser_handle_t *handle;
	sqlparser_handle_t *reparsed;
	sqlparser_parse_options_t options;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patches[2];
	sqlparser_query_graph_view_t old_graph;
	char name_selector[64];
	char *after_sql;
	char *after_view;
	char *before_sql;
	char *before_view;
	char *tree_copy;
	size_t alias_index;
	size_t mutation_count;
	size_t spelling_count;
	size_t tree_len;
	unsigned long generation;
	sqlparser_status_t status;

	handle = NULL;
	reparsed = NULL;
	after_sql = NULL;
	after_view = NULL;
	before_sql = NULL;
	before_view = NULL;
	tree_copy = NULL;
	memset(&error, 0, sizeof(error));
	sqlparser_parse_options_default(&options);
	if (sqlparser_parse_with_options(
		    baseline_sql,
		    &options,
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_find_name_index(
		    handle,
		    0U,
		    "ResTarget",
		    "name",
		    "OldAlias",
		    &alias_index) ||
	    sqlparser_deparse(handle, &before_sql, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_export_view_json(
		    handle,
		    0,
		    &before_view,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(
		    handle,
		    0U,
		    &old_graph,
		    &error) != SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: patch transaction setup failed: %s\n",
			error.message);
		sqlparser_string_free(before_view);
		sqlparser_string_free(before_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	if (snprintf(
		    name_selector,
		    sizeof(name_selector),
		    "stmt[0].name[%lu]",
		    (unsigned long)alias_index) <= 0) {
		fprintf(stderr, "FAIL: patch transaction selector formatting failed\n");
		sqlparser_string_free(before_view);
		sqlparser_string_free(before_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	tree_len = handle->parse_tree.len;
	tree_copy = (char *)malloc(tree_len);
	if (tree_copy == NULL) {
		fprintf(stderr, "FAIL: patch transaction tree snapshot allocation failed\n");
		sqlparser_string_free(before_view);
		sqlparser_string_free(before_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	memcpy(tree_copy, handle->parse_tree.data, tree_len);
	generation = handle->generation;
	mutation_count = handle->identifier_mutation_count;
	spelling_count = handle->identifier_spelling_count;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = name_selector;
	patches[0].sql = "\"GoodAlias\"";
	patches[1].op = SQLPARSER_PATCH_REPLACE;
	patches[1].selector = "stmt[0].relation[0]";
	patches[1].sql = "a + b";
	patch_list.items = patches;
	patch_list.count = 2U;
	status = sqlparser_apply_patch(handle, &patch_list, &error);
	if (status == SQLPARSER_STATUS_OK ||
	    handle->generation != generation ||
	    handle->identifier_mutation_count != mutation_count ||
	    handle->identifier_spelling_count != spelling_count ||
	    handle->parse_tree.len != tree_len ||
	    memcmp(handle->parse_tree.data, tree_copy, tree_len) != 0 ||
	    sqlparser_deparse(handle, &after_sql, &error) !=
		    SQLPARSER_STATUS_OK ||
	    after_sql == NULL ||
	    strcmp(after_sql, before_sql) != 0 ||
	    sqlparser_export_view_json(
		    handle,
		    0,
		    &after_view,
		    &error) != SQLPARSER_STATUS_OK ||
	    after_view == NULL ||
	    strcmp(after_view, before_view) != 0 ||
	    !statement_has_name(handle, 0U, &old_alias) ||
	    sqlparser_query_graph_relation_at(
		    &old_graph,
		    0U,
		    &old_graph_relation,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_query_graph_target_at(
		    &old_graph,
		    0U,
		    &old_graph_target,
		    &error) != SQLPARSER_STATUS_OK ||
	    old_graph_relation.object_name == NULL ||
	    strcmp(old_graph_relation.object_name, "OldTable") != 0 ||
	    old_graph_target.output_name == NULL ||
	    strcmp(old_graph_target.output_name, "OldAlias") != 0) {
		fprintf(
			stderr,
			"FAIL: failed patch batch changed original handle state\n");
		free(tree_copy);
		sqlparser_string_free(after_view);
		sqlparser_string_free(after_sql);
		sqlparser_string_free(before_view);
		sqlparser_string_free(before_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(after_view);
	after_view = NULL;
	sqlparser_string_free(after_sql);
	after_sql = NULL;

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = name_selector;
	patches[0].sql = "OldAlias";
	patch_list.items = patches;
	patch_list.count = 1U;
	if (sqlparser_apply_patch(
		    handle,
		    &patch_list,
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->generation != generation ||
	    handle->identifier_mutation_count != mutation_count ||
	    handle->identifier_spelling_count != spelling_count ||
	    handle->parse_tree.len != tree_len ||
	    memcmp(handle->parse_tree.data, tree_copy, tree_len) != 0) {
		fprintf(
			stderr,
			"FAIL: semantic and spelling no-op patch changed handle state: %s\n",
			error.message);
		free(tree_copy);
		sqlparser_string_free(before_view);
		sqlparser_string_free(before_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	patch_list.items = NULL;
	patch_list.count = 0U;
	if (sqlparser_apply_patch(
		    handle,
		    &patch_list,
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->generation != generation ||
	    handle->parse_tree.len != tree_len ||
	    memcmp(handle->parse_tree.data, tree_copy, tree_len) != 0) {
		fprintf(
			stderr,
			"FAIL: empty patch batch changed handle state: %s\n",
			error.message);
		free(tree_copy);
		sqlparser_string_free(before_view);
		sqlparser_string_free(before_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}

	memset(patches, 0, sizeof(patches));
	patches[0].op = SQLPARSER_PATCH_REPLACE;
	patches[0].selector = name_selector;
	patches[0].sql = "\"GoodAlias\"";
	patches[1].op = SQLPARSER_PATCH_REPLACE;
	patches[1].selector = "stmt[1].relation[0]";
	patches[1].sql = "\"NewTable\"";
	patch_list.items = patches;
	patch_list.count = 2U;
	if (sqlparser_apply_patch(
		    handle,
		    &patch_list,
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->generation != generation + 1UL ||
	    sqlparser_deparse(handle, &after_sql, &error) !=
		    SQLPARSER_STATUS_OK ||
	    after_sql == NULL ||
	    strstr(
		    after_sql,
		    "SELECT OldCol AS \"GoodAlias\" FROM OldTable") == NULL ||
	    strstr(
		    after_sql,
		    "SELECT KeepCol FROM \"NewTable\"") == NULL ||
	    sqlparser_parse_with_options(
		    after_sql,
		    &options,
		    &reparsed,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_count(reparsed) != 2U) {
		fprintf(
			stderr,
			"FAIL: successful multi-statement patch batch mismatch: %s\n",
			after_sql != NULL ? after_sql : error.message);
		free(tree_copy);
		sqlparser_handle_destroy(reparsed);
		sqlparser_string_free(after_sql);
		sqlparser_string_free(before_view);
		sqlparser_string_free(before_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	free(tree_copy);
	sqlparser_handle_destroy(reparsed);
	sqlparser_string_free(after_sql);
	sqlparser_string_free(before_view);
	sqlparser_string_free(before_sql);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int identifier_patch_atoms_reject_non_identifiers(void)
{
	static const char *const name_invalid[] = {
		"",
		"'text'",
		"a.b",
		"a + b",
		"a, b",
		"a AS b",
		"f()",
		"(a)",
		"*",
		"a; SELECT b",
		"\"unterminated"
	};
	static const char *const relation_invalid[] = {
		"",
		"'text'",
		"a + b",
		"a, b",
		"a AS b",
		"f()",
		"(a)",
		"*",
		"a; SELECT b",
		"\"unterminated"
	};
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_patch_list_t patch_list;
	sqlparser_patch_t patch;
	char name_selector[64];
	char *baseline_sql;
	char *current_sql;
	size_t alias_index;
	size_t index;
	unsigned long generation;

	handle = NULL;
	baseline_sql = NULL;
	current_sql = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT OldCol AS OldAlias FROM OldTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    !statement_find_name_index(
		    handle,
		    0U,
		    "ResTarget",
		    "name",
		    "OldAlias",
		    &alias_index) ||
	    sqlparser_deparse(handle, &baseline_sql, &error) !=
		    SQLPARSER_STATUS_OK ||
	    snprintf(
		    name_selector,
		    sizeof(name_selector),
		    "stmt[0].name[%lu]",
		    (unsigned long)alias_index) <= 0) {
		fprintf(
			stderr,
			"FAIL: invalid identifier atom setup failed: %s\n",
			error.message);
		sqlparser_string_free(baseline_sql);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	generation = handle->generation;
	memset(&patch, 0, sizeof(patch));
	patch.op = SQLPARSER_PATCH_REPLACE;
	patch.selector = name_selector;
	patch_list.items = &patch;
	patch_list.count = 1U;
	for (index = 0U;
	     index < sizeof(name_invalid) / sizeof(name_invalid[0]);
	     index++) {
		patch.sql = name_invalid[index];
		if (sqlparser_apply_patch(
			    handle,
			    &patch_list,
			    &error) == SQLPARSER_STATUS_OK ||
		    handle->generation != generation ||
		    sqlparser_deparse(handle, &current_sql, &error) !=
			    SQLPARSER_STATUS_OK ||
		    current_sql == NULL ||
		    strcmp(current_sql, baseline_sql) != 0) {
			fprintf(
				stderr,
				"FAIL: invalid name atom %s was accepted or changed state\n",
				name_invalid[index]);
			sqlparser_string_free(current_sql);
			sqlparser_string_free(baseline_sql);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_string_free(current_sql);
		current_sql = NULL;
	}
	patch.selector = "stmt[0].relation[0]";
	for (index = 0U;
	     index < sizeof(relation_invalid) / sizeof(relation_invalid[0]);
	     index++) {
		patch.sql = relation_invalid[index];
		if (sqlparser_apply_patch(
			    handle,
			    &patch_list,
			    &error) == SQLPARSER_STATUS_OK ||
		    handle->generation != generation ||
		    sqlparser_deparse(handle, &current_sql, &error) !=
			    SQLPARSER_STATUS_OK ||
		    current_sql == NULL ||
		    strcmp(current_sql, baseline_sql) != 0) {
			fprintf(
				stderr,
				"FAIL: invalid relation atom %s was accepted or changed state\n",
				relation_invalid[index]);
			sqlparser_string_free(current_sql);
			sqlparser_string_free(baseline_sql);
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_string_free(current_sql);
		current_sql = NULL;
	}
	sqlparser_string_free(baseline_sql);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int dropped_source_identifier_is_rejected(
	size_t mutation_kind,
	const char *sql)
{
	PgQuery__ColumnRef *column_ref;
	PgQuery__SelectStmt *select_stmt;
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_status_t status;

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(sql, &handle, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_handle_ensure_ast(handle, &error) !=
		    SQLPARSER_STATUS_OK ||
	    handle->ast == NULL ||
	    handle->ast->n_stmts != 1U ||
	    handle->ast->stmts == NULL ||
	    handle->ast->stmts[0] == NULL ||
	    handle->ast->stmts[0]->stmt == NULL ||
	    handle->ast->stmts[0]->stmt->node_case !=
		    PG_QUERY__NODE__NODE_SELECT_STMT ||
	    handle->ast->stmts[0]->stmt->select_stmt == NULL) {
		fprintf(
			stderr,
			"FAIL: dropped identifier audit setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	select_stmt = handle->ast->stmts[0]->stmt->select_stmt;
	if (mutation_kind == 0U || mutation_kind == 3U) {
		if (select_stmt->n_target_list != 1U ||
		    select_stmt->target_list == NULL ||
		    select_stmt->target_list[0] == NULL ||
		    select_stmt->target_list[0]->res_target == NULL ||
		    select_stmt->target_list[0]->res_target->name == NULL) {
			fprintf(stderr, "FAIL: alias identifier audit fixture is invalid\n");
			sqlparser_handle_destroy(handle);
			return 0;
		}
		free(select_stmt->target_list[0]->res_target->name);
		select_stmt->target_list[0]->res_target->name = NULL;
	} else if (mutation_kind == 1U) {
		if (select_stmt->n_from_clause != 1U ||
		    select_stmt->from_clause == NULL ||
		    select_stmt->from_clause[0] == NULL) {
			fprintf(stderr, "FAIL: relation identifier audit fixture is invalid\n");
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_free_proto_node(select_stmt->from_clause[0]);
		free(select_stmt->from_clause);
		select_stmt->from_clause = NULL;
		select_stmt->n_from_clause = 0U;
	} else {
		if (select_stmt->n_target_list != 1U ||
		    select_stmt->target_list == NULL ||
		    select_stmt->target_list[0] == NULL ||
		    select_stmt->target_list[0]->res_target == NULL ||
		    select_stmt->target_list[0]->res_target->val == NULL ||
		    select_stmt->target_list[0]->res_target->val->node_case !=
			    PG_QUERY__NODE__NODE_COLUMN_REF ||
		    select_stmt->target_list[0]->res_target->val->column_ref ==
			    NULL) {
			fprintf(stderr, "FAIL: qualified identifier audit fixture is invalid\n");
			sqlparser_handle_destroy(handle);
			return 0;
		}
		column_ref =
			select_stmt->target_list[0]->res_target->val->column_ref;
		if (column_ref->n_fields != 2U ||
		    column_ref->fields == NULL ||
		    column_ref->fields[0] == NULL) {
			fprintf(stderr, "FAIL: qualified identifier audit path is invalid\n");
			sqlparser_handle_destroy(handle);
			return 0;
		}
		sqlparser_free_proto_node(column_ref->fields[0]);
		memmove(
			&column_ref->fields[0],
			&column_ref->fields[1],
			sizeof(column_ref->fields[0]));
		column_ref->n_fields = 1U;
	}
	if (sqlparser_handle_commit_ast(handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: dropped identifier audit commit failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	handle->generation = 0UL;
	status = sqlparser_validate_ast_identifier_spelling(
		handle,
		&error);
	if (status != SQLPARSER_STATUS_INTERNAL_ERROR) {
		fprintf(
			stderr,
			"FAIL: generation-zero AST dropped source identifier kind %lu without rejection\n",
			(unsigned long)mutation_kind);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_handle_destroy(handle);
	return 1;
}

static int identifier_completeness_audit_is_fail_closed(void)
{
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	sqlparser_query_graph_view_t graph;
	char *deparsed;

	if (!dropped_source_identifier_is_rejected(
		    0U,
		    "SELECT q.Col AS AliasGone FROM SchemaName.TableName q") ||
	    !dropped_source_identifier_is_rejected(
		    1U,
		    "SELECT KeptCol FROM RelationGone") ||
	    !dropped_source_identifier_is_rejected(
		    2U,
		    "SELECT QualifierGone.KeptCol FROM SourceTable") ||
	    !dropped_source_identifier_is_rejected(
		    3U,
		    "SELECT KeptCol AS comment FROM SourceTable")) {
		return 0;
	}

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(
		    "SELECT KeepCol, DropCol FROM BaseTable",
		    &handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_select_delete_target(
		    handle,
		    0U,
		    0U,
		    1U,
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->generation == 0UL ||
	    sqlparser_validate_ast_identifier_spelling(
		    handle,
		    &error) != SQLPARSER_STATUS_OK ||
	    sqlparser_statement_query_graph(
		    handle,
		    0U,
		    &graph,
		    &error) != SQLPARSER_STATUS_OK ||
	    graph.target_count != 1U ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strcmp(deparsed, "SELECT KeepCol FROM BaseTable") != 0) {
		fprintf(
			stderr,
			"FAIL: legal generation-positive identifier deletion was rejected or changed semantics: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int discarded_generated_targets_release_spellings(void)
{
	static const char baseline_sql[] =
		"SELECT KeepColumn FROM KeepTable";
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *deparsed;
	size_t spelling_count;
	size_t iteration;

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(baseline_sql, &handle, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_handle_ensure_ast(handle, &error) !=
		    SQLPARSER_STATUS_OK) {
		fprintf(
			stderr,
			"FAIL: discarded generated target setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	spelling_count = handle->identifier_spelling_count;
	for (iteration = 0U; iteration < 64U; iteration++) {
		if (sqlparser_select_insert_target_sql(
			    handle,
			    0U,
			    0U,
			    0U,
			    "FirstCase, SecondCase",
			    &error) != SQLPARSER_STATUS_UNSUPPORTED ||
		    handle->identifier_spelling_count != spelling_count) {
			fprintf(
				stderr,
				"FAIL: discarded generated targets retained spelling groups at iteration %lu\n",
				(unsigned long)iteration);
			sqlparser_handle_destroy(handle);
			return 0;
		}
	}
	if (sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strcmp(deparsed, baseline_sql) != 0) {
		fprintf(
			stderr,
			"FAIL: discarded generated targets changed handle state: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

static int discarded_assignment_fragments_release_spellings(void)
{
	static const char baseline_sql[] =
		"UPDATE KeepTable SET KeepColumn = 1";
	sqlparser_error_t error;
	sqlparser_handle_t *handle;
	char *deparsed;
	size_t retained_count;
	size_t iteration;

	handle = NULL;
	deparsed = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse(baseline_sql, &handle, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_update_insert_assignment_sql(
		    handle,
		    0U,
		    1U,
		    "\"AddedColumn\" = \"AddedValue\"",
		    &error) != SQLPARSER_STATUS_OK ||
	    handle->identifier_spelling_count == 0U) {
		fprintf(
			stderr,
			"FAIL: discarded assignment fragment setup failed: %s\n",
			error.message);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	retained_count = handle->identifier_spelling_count;
	for (iteration = 0U; iteration < 64U; iteration++) {
		if (sqlparser_update_insert_assignment_sql(
			    handle,
			    0U,
			    2U,
			    "\"DiscardedOne\" = \"FirstValue\", "
			    "\"DiscardedTwo\" = \"SecondValue\"",
			    &error) != SQLPARSER_STATUS_UNSUPPORTED ||
		    handle->identifier_spelling_count != retained_count) {
			fprintf(
				stderr,
				"FAIL: discarded assignment fragment retained spelling groups at iteration %lu: expected=%lu actual=%lu\n",
				(unsigned long)iteration,
				(unsigned long)retained_count,
				(unsigned long)
					handle->identifier_spelling_count);
			sqlparser_handle_destroy(handle);
			return 0;
		}
	}
	if (sqlparser_validate_ast_identifier_spelling(handle, &error) !=
		    SQLPARSER_STATUS_OK ||
	    sqlparser_deparse(handle, &deparsed, &error) !=
		    SQLPARSER_STATUS_OK ||
	    deparsed == NULL ||
	    strstr(
		    deparsed,
		    "\"AddedColumn\" = \"AddedValue\"") == NULL ||
	    strstr(deparsed, "DiscardedOne") != NULL ||
	    strstr(deparsed, "DiscardedTwo") != NULL) {
		fprintf(
			stderr,
			"FAIL: discarded assignment fragments damaged retained spelling: %s\n",
			deparsed != NULL ? deparsed : error.message);
		sqlparser_string_free(deparsed);
		sqlparser_handle_destroy(handle);
		return 0;
	}
	sqlparser_string_free(deparsed);
	sqlparser_handle_destroy(handle);
	return 1;
}

int main(void)
{
	static const char long_name[] =
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789";
	static const char shared_prefix[] =
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
		"AbCdEfGhIjKlMnOpQrStUvWxYz";
	static const char shared_name_a[] =
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
		"AbCdEfGhIjKlMnOpQrStUvWxYzA";
	static const char shared_name_b[] =
		"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
		"AbCdEfGhIjKlMnOpQrStUvWxYzB";
	static const spelling_case_t cases[] = {
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"select abc AS MixedAlias from SchemaCase.DDD",
			0U,
			{
				{"ColumnRef", "fields", "abc"},
				{"ResTarget", "name", "MixedAlias"},
				{"RangeVar", "schemaname", "SchemaCase"},
				{"RangeVar", "relname", "DDD"}
			},
			4U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"select firstCol from FirstTable; SELECT SecondCol FROM SecondTable",
			1U,
			{
				{"ColumnRef", "fields", "SecondCol"},
				{"RangeVar", "relname", "SecondTable"}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"WITH MixedCTE AS (SELECT SrcCol FROM BaseTable) "
			"SELECT MixedCTE.SrcCol FROM MixedCTE",
			0U,
			{
				{"CommonTableExpr", "ctename", "MixedCTE"},
				{"RangeVar", "relname", "BaseTable"},
				{"RangeVar", "relname", "MixedCTE"},
				{"ColumnRef", "fields", "SrcCol"}
			},
			4U
		},
		{
			SQLPARSER_DIALECT_MYSQL,
			"SELECT `MiX``Name` AS `AliasCase` FROM `DbCase`.`TableCase`",
			0U,
			{
				{"ColumnRef", "fields", "MiX`Name"},
				{"ResTarget", "name", "AliasCase"},
				{"RangeVar", "schemaname", "DbCase"},
				{"RangeVar", "relname", "TableCase"}
			},
			4U
		},
		{
			SQLPARSER_DIALECT_SQLSERVER,
			"SELECT [MiX]]Name] AS [AliasCase] FROM [DbCase].[dbo].[TableCase]",
			0U,
			{
				{"ColumnRef", "fields", "MiX]Name"},
				{"ResTarget", "name", "AliasCase"},
				{"RangeVar", "catalogname", "DbCase"},
				{"RangeVar", "schemaname", "dbo"},
				{"RangeVar", "relname", "TableCase"}
			},
			5U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT \"MiX\"\"Name\" FROM \"SchemaCase\".\"TableCase\"",
			0U,
			{
				{"ColumnRef", "fields", "MiX\"Name"},
				{"RangeVar", "schemaname", "SchemaCase"},
				{"RangeVar", "relname", "TableCase"}
			},
			3U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT U&\"MiX\" FROM U&\"TableCase\"",
			0U,
			{
				{"ColumnRef", "fields", "MiX"},
				{"RangeVar", "relname", "TableCase"}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789 FROM "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789",
			0U,
			{
				{"ColumnRef", "fields", long_name},
				{"RangeVar", "relname", long_name}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"PREPARE MiXeDPlan AS SELECT 1",
			0U,
			{
				{"PrepareStmt", "name", "MiXeDPlan"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE ROLE MiXeDRole",
			0U,
			{
				{"CreateRoleStmt", "role", "MiXeDRole"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE ROLE MiXeDRole SUPERUSER LOGIN NOINHERIT",
			0U,
			{
				{"CreateRoleStmt", "role", "MiXeDRole"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE POLICY PolicyCase ON TableCase AS PERMISSIVE",
			0U,
			{
				{"CreatePolicyStmt", "policy_name", "PolicyCase"},
				{"RangeVar", "relname", "TableCase"}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"GRANT RoleCase TO PUBLIC",
			0U,
			{{NULL, NULL, NULL}},
			0U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"INSERT INTO TargetTable VALUES (1) "
			"ON CONFLICT ON CONSTRAINT MiXeDConstraint DO NOTHING",
			0U,
			{
				{"RangeVar", "relname", "TargetTable"},
				{"InferClause", "conname", "MiXeDConstraint"}
			},
			2U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"UPDATE TargetTable SET TargetColumn = 1 "
			"WHERE CURRENT OF MiXeDCursor",
			0U,
			{
				{"RangeVar", "relname", "TargetTable"},
				{"ResTarget", "name", "TargetColumn"},
				{"CurrentOfExpr", "cursor_name", "MiXeDCursor"}
			},
			3U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT CURRENT_USER AS Current_User",
			0U,
			{
				{"ResTarget", "name", "Current_User"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE ROLE CamelRole; "
			"CREATE INDEX CAMELROLE ON CamelRole(CamelRole)",
			0U,
			{
				{"CreateRoleStmt", "role", "CamelRole"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"CREATE ROLE CamelRole; "
			"CREATE INDEX CAMELROLE ON CamelRole(CamelRole)",
			1U,
			{
				{"IndexStmt", "idxname", "CAMELROLE"},
				{"RangeVar", "relname", "CamelRole"},
				{"IndexElem", "name", "CamelRole"}
			},
			3U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT U&\"Mi\\0058ed\"",
			0U,
			{
				{"ColumnRef", "fields", "MiXed"}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz, "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYzA, "
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYzB",
			0U,
			{
				{"ColumnRef", "fields", shared_prefix},
				{"ColumnRef", "fields", shared_name_a},
				{"ColumnRef", "fields", shared_name_b}
			},
			3U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT \""
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789\"",
			0U,
			{
				{"ColumnRef", "fields", long_name}
			},
			1U
		},
		{
			SQLPARSER_DIALECT_POSTGRESQL,
			"SELECT U&\""
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789_"
			"AbCdEfGhIjKlMnOpQrStUvWxYz0123456789\"",
			0U,
			{
				{"ColumnRef", "fields", long_name}
			},
			1U
		}
	};
	sqlparser_parse_options_t options;
	sqlparser_handle_t *handle;
	sqlparser_error_t error;
	size_t case_index;
	size_t name_index;

	for (case_index = 0U;
	     case_index < sizeof(cases) / sizeof(cases[0]);
	     case_index++) {
		handle = NULL;
		memset(&error, 0, sizeof(error));
		sqlparser_parse_options_default(&options);
		options.dialect = cases[case_index].dialect;
		if (sqlparser_parse_with_options(
			    cases[case_index].sql,
			    &options,
			    &handle,
			    &error) != SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier spelling case %lu did not parse: %s\n",
				(unsigned long)case_index,
				error.message);
			return 1;
		}
		if (sqlparser_validate_ast_identifier_spelling(handle, &error) !=
		    SQLPARSER_STATUS_OK) {
			fprintf(
				stderr,
				"FAIL: identifier spelling audit case %lu failed: %s\n",
				(unsigned long)case_index,
				error.message);
			sqlparser_handle_destroy(handle);
			return 1;
		}
		for (name_index = 0U;
		     name_index < cases[case_index].name_count;
		     name_index++) {
			if (!statement_has_name(
				    handle,
				    cases[case_index].statement_index,
				    &cases[case_index].names[name_index])) {
				fprintf(
					stderr,
					"FAIL: identifier spelling case %lu lost %s.%s=%s\n",
					(unsigned long)case_index,
					cases[case_index].names[name_index].owner_type,
					cases[case_index].names[name_index].field_name,
					cases[case_index].names[name_index].value);
				sqlparser_handle_destroy(handle);
				return 1;
			}
		}
		sqlparser_handle_destroy(handle);
	}
	if (!standard_parse_has_spelling(
		    "SELECT MiXeDIdentifier",
		    "\"sval\":\"mixedidentifier\"",
		    "\"sval\":\"MiXeDIdentifier\"") ||
	    !standard_parse_has_spelling(
		    "SELECT "
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"") ||
	    !standard_parse_has_spelling(
		    "SELECT \""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"") ||
		    !standard_parse_has_spelling(
			    "SELECT U&\""
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
		    "\"sval\":\""
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"",
			    "\"sval\":\""
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"") ||
		    !standard_protobuf_deparse_has_spelling(
			    "SELECT MiXeDIdentifier",
			    "mixedidentifier",
			    "MiXeDIdentifier") ||
		    !standard_protobuf_deparse_has_spelling(
			    "SELECT "
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") ||
		    !standard_fingerprints_match(
			    "SELECT MiXeDIdentifier",
			    "SELECT mixedidentifier") ||
	    !standard_fingerprints_match(
		    "SELECT "
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		    "SELECT "
		    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") ||
	    !xmltable_internal_option_semantics_match()) {
			return 1;
		}
	if (!sqlparser_parse_is_rejected(
		    "CREATE ROLE MiXeDRole \"SUPERUSER\"") ||
	    !sqlparser_parse_is_rejected(
		    "CREATE ROLE MiXeDRole \"superuser\"") ||
	    !sqlparser_parse_is_rejected(
		    "CREATE POLICY PolicyCase ON TableCase AS \"PERMISSIVE\"")) {
		fprintf(
			stderr,
			"FAIL: quoted grammar-only identifiers were accepted\n");
		return 1;
	}

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse("GRANT RoleCase TO PUBLIC", &handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: unquoted PUBLIC did not parse: %s\n", error.message);
		return 1;
	}
	{
		static const expected_name_t public_role = {
			"RoleSpec",
			"rolename",
			"PUBLIC"
		};

		if (statement_has_name(handle, 0U, &public_role)) {
			fprintf(stderr, "FAIL: unquoted PUBLIC became a CSTRING role\n");
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse("GRANT RoleCase TO \"PUBLIC\"", &handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: quoted PUBLIC did not parse: %s\n", error.message);
		return 1;
	}
	{
		static const expected_name_t quoted_public_role = {
			"RoleSpec",
			"rolename",
			"PUBLIC"
		};

		if (!statement_has_name(handle, 0U, &quoted_public_role)) {
			fprintf(stderr, "FAIL: quoted PUBLIC did not remain a CSTRING role\n");
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	sqlparser_handle_destroy(handle);

	handle = NULL;
	memset(&error, 0, sizeof(error));
	if (sqlparser_parse("GRANT RoleCase TO \"public\"", &handle, &error) !=
	    SQLPARSER_STATUS_OK) {
		fprintf(stderr, "FAIL: quoted lowercase public did not parse: %s\n", error.message);
		return 1;
	}
	{
		static const expected_name_t quoted_lower_public_role = {
			"RoleSpec",
			"rolename",
			"public"
		};

		if (!statement_has_name(handle, 0U, &quoted_lower_public_role)) {
			fprintf(stderr, "FAIL: quoted lowercase public did not remain a CSTRING role\n");
			sqlparser_handle_destroy(handle);
			return 1;
		}
	}
	sqlparser_handle_destroy(handle);
	if (!query_graph_identifier_semantics() ||
	    !session_identifier_semantics() ||
	    !identifier_role_classifier_is_fail_closed() ||
	    !defelem_context_classifier_is_fail_closed() ||
	    !semantic_control_fields_fail_closed() ||
	    !graph_name_selector_uses_public_name_ordinal() ||
	    !mutation_marker_lifecycle() ||
	    !failed_commit_preserves_mutation_provenance() ||
	    !removed_select_target_paths_preserve_mutation_provenance() ||
	    !relation_group_preserves_source_spelling() ||
	    !selector_mutation_adapters_preserve_source_provenance() ||
	    !structured_target_identifier_rendering_is_exact() ||
	    !patched_select_identifier_spelling_preserved() ||
	    !patched_nested_identifier_spelling_preserved() ||
	    !patched_alias_locations_preserve_spelling() ||
	    !patched_identifier_owner_spelling_preserved() ||
	    !generated_identifier_preserves_patch_delimiter() ||
	    !patched_identifier_atoms_preserve_all_dialects() ||
	    !patched_mysql_target_list_preserves_raw_identifiers() ||
	    !identifier_patch_batch_is_atomic() ||
	    !identifier_patch_atoms_reject_non_identifiers() ||
	    !identifier_completeness_audit_is_fail_closed() ||
	    !discarded_generated_targets_release_spellings() ||
	    !discarded_assignment_fragments_release_spellings() ||
	    !unconsumed_identifier_tag_fails_closed() ||
	    !mutated_keyword_alias_is_quoted() ||
	    !mutated_type_prefix_is_not_synthetic() ||
	    !mutated_name_preserves_siblings(
		    "SELECT MiXeD.ColCase FROM SchemaCase.TableCase",
		    "ColumnRef",
		    "fields",
		    "MiXeD",
		    "other",
		    "other.ColCase",
		    "other.\"ColCase\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM MixedTable AS OldAlias",
		    "Alias",
		    "aliasname",
		    "OldAlias",
		    "new_alias",
		    "MixedTable AS new_alias",
		    "\"MixedTable\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM t AS Foo(Old)",
		    "Alias",
		    "colnames",
		    "Old",
		    "Foo",
		    "AS Foo(\"Foo\")",
		    NULL) ||
	    !mutated_name_preserves_siblings(
		    "WITH CteName(OldCol, KeepCol) AS "
		    "(SELECT 1, 2) SELECT * FROM CteName",
		    "CommonTableExpr",
		    "aliascolnames",
		    "OldCol",
		    "new_col",
		    "CteName(new_col, KeepCol)",
		    "\"KeepCol\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM MixedTable AS OldAlias",
		    "RangeVar",
		    "relname",
		    "MixedTable",
		    "other",
		    "other AS OldAlias",
		    "\"OldAlias\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM (SELECT 1) Foo(KeepCase)",
		    "Alias",
		    "aliasname",
		    "Foo",
		    "SELECT",
		    ") \"SELECT\"(KeepCase)",
		    "\"KeepCase\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT * FROM (SELECT 1) AS \"select\"(\"select\")",
		    "Alias",
		    "aliasname",
		    "select",
		    "other",
		    ") AS other(\"select\")",
		    NULL) ||
	    !mutated_name_preserves_siblings(
		    "WITH cte(comment, KeepCase) AS "
		    "(SELECT 1, 2) SELECT * FROM cte",
		    "CommonTableExpr",
		    "aliascolnames",
		    "comment",
		    "new_col",
		    "cte(new_col, KeepCase)",
		    "\"KeepCase\"") ||
	    !mutated_name_preserves_siblings(
		    "SELECT 1::comment.bit",
		    "TypeName",
		    "names",
		    "comment",
		    "pg_catalog",
		    "::pg_catalog.bit",
		    NULL) ||
	    !mutated_name_preserves_siblings(
		    "SET comment TO KeepCase",
		    "VariableSetStmt",
		    "name",
		    "comment",
		    "SET",
		    "SET \"SET\" TO KeepCase",
		    "\"KeepCase\"") ||
	    !mutated_dialect_keeps_unmodified_spelling(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT Foo, `Foo` AS `OldAlias` FROM `Foo`",
		    "OldAlias",
		    "NewAlias",
		    "SELECT Foo, `Foo` AS `NewAlias` FROM `Foo`") ||
	    !mutated_dialect_keeps_unmodified_spelling(
		    SQLPARSER_DIALECT_ORACLE,
		    "INSERT ALL INTO TargetTable (SameCase) VALUES (1) "
		    "SELECT \"SameCase\" AS KeepAlias FROM SourceTable",
		    "KeepAlias",
		    "ChangedAlias",
		    "SELECT \"SameCase\" AS")) {
		return 1;
	}
	if (!ast_deparse_preserves(
		    "SELECT row_number() OVER \"AS\" FROM T "
		    "WINDOW \"AS\" AS (ORDER BY Id)",
		    "OVER \"AS\"",
		    "WINDOW \"AS\" AS",
		    "WINDOW AS AS") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(a AS char), CAST(b AS char(1)), "
		    "CAST(c AS char(8)) FROM `t`",
		    "CAST(a AS char), CAST(b AS char(1)), CAST(c AS char(8))",
		    "FROM `t`",
		    NULL) ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_VASTBASE_MYSQL,
		    "SELECT CAST(a AS CHAR), CAST(b AS CHAR(1)), "
		    "CAST(c AS CHAR(8)) FROM `t`",
		    "CAST(a AS CHAR), CAST(b AS CHAR(1)), CAST(c AS CHAR(8))",
		    "FROM `t`",
		    NULL) ||
	    !ast_deparse_preserves(
		    "SELECT $1::character, $2::character(1), "
		    "$3::character(8) FROM t",
		    "$1::character, $2::character(1), $3::character(8)",
		    "FROM t",
		    NULL) ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_VASTBASE_POSTGRESQL,
		    "SELECT $1::CHARACTER, $2::CHARACTER(1), "
		    "$3::CHARACTER(8) FROM t",
		    "$1::CHARACTER, $2::CHARACTER(1), $3::CHARACTER(8)",
		    "FROM t",
		    NULL) ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(`A` AS bit) FROM pg_catalog.T",
		    "CAST(`A` AS bit)",
		    "FROM pg_catalog.T",
		    "bit(1)") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(`A` AS bit(1)) FROM pg_catalog.T",
		    "CAST(`A` AS bit(1))",
		    "FROM pg_catalog.T",
		    "::pg_catalog.bit") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(`A` AS double precision) FROM `precision`",
		    "CAST(`A` AS double precision)",
		    "FROM `precision`",
		    "FROM precision") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(1 AS bit) FROM `t`",
		    "CAST(1 AS bit)",
		    "FROM `t`",
		    "bit(1)") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT overlay('abc' PLACING 'x' FROM 1) FROM `t`",
		    "overlay(",
		    "FROM `t`",
		    "pg_catalog.overlay") ||
	    !ast_deparse_preserves_dialect(
		    SQLPARSER_DIALECT_MYSQL,
		    "SELECT CAST(`A` AS timestamp(3) with time zone) "
		    "FROM `zone`",
		    "with time zone",
		    "FROM `zone`",
		    "FROM zone") ||
	    !ast_deparse_preserves(
		    "SET ROLE NONE",
		    "SET role",
		    "NONE",
		    "'none'") ||
	    !ast_deparse_preserves(
		    "create role \"role\"",
		    "CREATE ROLE \"role\"",
		    NULL,
		    "CREATE ROLE role") ||
	    !ast_deparse_preserves(
		    "CREATE TEXT SEARCH DICTIONARY d "
		    "(foo = 'select', language = NONE)",
		    "foo = 'select'",
		    "language = NONE",
		    "foo = select") ||
	    !ast_deparse_preserves(
		    "COPY T TO STDOUT WITH (foo ON, language 'on')",
		    "foo ON",
		    "language 'on'",
		    "foo 'on'") ||
	    !ast_deparse_preserves(
		    "COPY T TO STDOUT WITH (foo (ON, 'on'))",
		    "foo (ON, 'on')",
		    NULL,
		    "foo (on, on)") ||
	    !ast_deparse_preserves(
		    "CREATE INDEX Idx ON T USING \"BtReE\" (C)",
		    "USING \"BtReE\"",
		    "(C)",
		    NULL) ||
	    !ast_deparse_preserves(
		    "SET \"timezone\" TO 'off'",
		    "SET \"timezone\"",
		    "'off'",
		    "TIME ZONE") ||
	    !ast_deparse_preserves(
		    "SET SCHEMA 'CaseSchema'",
		    "'CaseSchema'",
		    NULL,
		    "\"search_path\"") ||
	    !ast_deparse_preserves(
		    "SET search_path TO 'x', Bar",
		    "'x', Bar",
		    NULL,
		    "'Bar'") ||
	    !ast_deparse_preserves(
		    "SHOW \"timezone\"",
		    "SHOW \"timezone\"",
		    NULL,
		    "SHOW TIME ZONE") ||
	    !ast_deparse_preserves(
		    "SELECT C FROM T WHERE C LIKE "
		    "pg_catalog.like_escape($1, $2)",
		    "LIKE pg_catalog.like_escape(",
		    NULL,
		    " ESCAPE ") ||
	    !ast_deparse_preserves(
		    "SELECT pg_catalog.json_object('k', 1)",
		    "pg_catalog.json_object(",
		    NULL,
		    "SELECT JSON_OBJECT(") ||
	    !ast_deparse_preserves(
		    "SELECT pg_catalog.overlay('abcd', 'X', 2, 1)",
		    "pg_catalog.overlay(",
		    NULL,
		    " PLACING ") ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::bit",
		    "::bit",
		    NULL,
		    "::bit(1)") ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::bit(1), B'10'::bit(2)",
		    "::bit(1)",
		    "::bit(2)",
		    NULL) ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::pg_catalog.bit",
		    "::pg_catalog.bit",
		    NULL,
		    "::pg_catalog.bit(1)") ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::pg_catalog.bit(1), "
		    "B'1'::pg_catalog.varbit(8)",
		    "::pg_catalog.bit(1)",
		    "::pg_catalog.varbit(8)",
		    NULL) ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::other.bit(1)",
		    "::other.bit(1)",
		    NULL,
		    NULL) ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::\"bit\"(1)",
		    "::\"bit\"(1)",
		    NULL,
		    NULL) ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::\"pg_catalog\".\"bit\"(1)",
		    "::\"pg_catalog\".\"bit\"(1)",
		    NULL,
		    NULL) ||
	    !ast_deparse_preserves(
		    "SELECT B'1'::bit varying(1)",
		    "::bit varying(1)",
		    NULL,
		    "::bit(1)") ||
	    !ast_deparse_preserves(
		    "CREATE FUNCTION F() RETURNS int AS "
		    "'BEGIN RETURN 1; END' LANGUAGE PLpgSQL",
		    "LANGUAGE PLpgSQL",
		    NULL,
		    "LANGUAGE 'PLpgSQL'") ||
	    !ast_deparse_preserves(
		    "CREATE FUNCTION FQuoted() RETURNS int AS "
		    "'BEGIN RETURN 1; END' LANGUAGE 'PLpgSQL'",
		    "LANGUAGE 'PLpgSQL'",
		    NULL,
		    NULL) ||
	    !ast_deparse_preserves(
		    "CREATE DATABASE Db ENCODING 'UTF8' TEMPLATE Template0",
		    "ENCODING 'UTF8'",
		    "TEMPLATE Template0",
		    "'Template0'") ||
	    !ast_deparse_preserves(
		    "CREATE DATABASE Db ENCODING E'UT\\'F8' TEMPLATE Template0",
		    "ENCODING 'UT''F8'",
		    "TEMPLATE Template0",
		    "'Template0'") ||
	    !ast_deparse_preserves(
		    "CREATE DATABASE DbQuoted ALLOW_CONNECTIONS 'on'",
		    "ALLOW_CONNECTIONS 'on'",
		    NULL,
		    "ALLOW_CONNECTIONS ON") ||
	    !ast_deparse_preserves(
		    "CREATE DATABASE Db ALLOW_CONNECTIONS on IS_TEMPLATE 'on'",
		    "ALLOW_CONNECTIONS on",
		    "IS_TEMPLATE 'on'",
		    "IS_TEMPLATE ON") ||
	    !ast_deparse_preserves(
		    "SET TIME ZONE timezone",
		    "TO timezone",
		    NULL,
		    "TO \"timezone\"") ||
	    !ast_deparse_preserves(
		    "DO LANGUAGE 'PLpgSQL' 'BEGIN NULL; END'",
		    "LANGUAGE 'PLpgSQL'",
		    NULL,
		    "LANGUAGE PLpgSQL") ||
	    !ast_deparse_preserves(
		    "COPY T (C) FROM STDIN CSV FREEZE",
		    "COPY T(C)",
		    "CSV FREEZE",
		    NULL)) {
		return 1;
	}
	return 0;
}
