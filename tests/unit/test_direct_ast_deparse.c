#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pg_query.h"
#include "src/pg_query_deparse_ast.h"

typedef struct {
  const PgQuery__ParseResult *ast;
  size_t calls;
  int nested;
  int fail_nested;
} ReaderState;

static void reader(void *context, const void *slot, const char *native_string) {
  ReaderState *state = context;
  const char *const *source = slot;
  PgQueryDeparseResult nested;
  PgQuery__ParseResult bad = PG_QUERY__PARSE_RESULT__INIT;
  assert(source != NULL && *source != NULL);
  assert(native_string != *source);
  assert(strcmp(native_string, *source) == 0);
  ++state->calls;
  if (state->nested) return;
  state->nested = 1;
  if (state->fail_nested) {
    bad.version = -1;
    nested = pg_query_deparse_ast(&bad);
    assert(nested.error != NULL);
  } else {
    nested = pg_query_deparse_ast(state->ast);
    assert(nested.error == NULL && nested.query != NULL);
  }
  pg_query_free_deparse_result(nested);
}

int main(void) {
  const char *sql[] = {
    "SELECT Foo, 'hello', 1.234, B'101' FROM Bar WHERE Foo IN (SELECT Key FROM Other)",
    "WITH q AS (SELECT 1 n) SELECT row_number() OVER (ORDER BY n) FROM q",
    "INSERT INTO t (a, b) VALUES (1, 'x'), (2, 'y') RETURNING a",
    "UPDATE t SET a = lower(b) WHERE c = 4 RETURNING *",
    "DELETE FROM t USING u WHERE t.a = u.a",
    "CREATE TABLE sample (id int PRIMARY KEY, label text DEFAULT 'n', CHECK (id > 0))",
    "CREATE INDEX ix ON sample ((lower(label))) WHERE id > 1",
    "ALTER TABLE sample ADD COLUMN more int",
    "GRANT SELECT ON sample TO PUBLIC",
    "SET search_path TO public; RESET search_path; BEGIN; COMMIT",
    "MERGE INTO t USING u ON t.id = u.id WHEN MATCHED THEN UPDATE SET v = u.v",
    "SELECT ARRAY[1,2], CASE WHEN true THEN 1 ELSE 2 END, x::numeric(3,2) FROM t"
  };
  size_t i;
  for (i=0; i<sizeof(sql)/sizeof(sql[0]); ++i) {
    PgQueryProtobufParseResult parsed = pg_query_parse_protobuf(sql[i]);
    PgQuery__ParseResult *ast;
    PgQueryDeparseResult direct, wire, observed;
    PostgresDeparseOpts opts = {0};
    ReaderState state = {0};
    size_t len;
    uint8_t *before, *after;
    assert(parsed.error == NULL);
    ast = pg_query__parse_result__unpack(NULL, parsed.parse_tree.len, (uint8_t*)parsed.parse_tree.data);
    assert(ast != NULL);
    len = pg_query__parse_result__get_packed_size(ast);
    before = malloc(len); after = malloc(len);
    assert(before && after);
    assert(pg_query__parse_result__pack(ast,before)==len);
    direct = pg_query_deparse_ast(ast);
    wire = pg_query_deparse_protobuf(parsed.parse_tree);
    assert(direct.error == NULL && wire.error == NULL);
    assert(direct.query && wire.query && strcmp(direct.query, wire.query)==0);
    state.ast = ast;
    state.fail_nested = i % 2;
    observed = pg_query_deparse_ast_opts_with_reader(ast, opts, reader, &state);
    assert(observed.error == NULL && observed.query != NULL);
    assert(strcmp(direct.query, observed.query)==0);
    assert(pg_query__parse_result__pack(ast,after)==len);
    assert(memcmp(before,after,len)==0);
    pg_query_free_deparse_result(observed);
    pg_query_free_deparse_result(direct);
    pg_query_free_deparse_result(wire);
    free(before); free(after);
    pg_query__parse_result__free_unpacked(ast,NULL);
    pg_query_free_protobuf_parse_result(parsed);
  }
  {
    PgQuery__Node invalid = PG_QUERY__NODE__INIT;
    PgQuery__RawStmt stmt = PG_QUERY__RAW_STMT__INIT;
    PgQuery__RawStmt *statements[] = {&stmt};
    PgQuery__ParseResult ast = PG_QUERY__PARSE_RESULT__INIT;
    PgQueryDeparseResult bad, good;
    PgQueryProtobufParseResult parsed = pg_query_parse_protobuf("SELECT 1");
    PgQuery__ParseResult *valid = pg_query__parse_result__unpack(NULL, parsed.parse_tree.len, (uint8_t*)parsed.parse_tree.data);
    ast.version = valid->version;
    ast.n_stmts = 1; ast.stmts = statements; stmt.stmt = &invalid;
    invalid.node_case = (PgQuery__Node__NodeCase)999999;
    bad = pg_query_deparse_ast(&ast);
    assert(bad.error && strstr(bad.error->message,"unsupported protobuf node type"));
    pg_query_free_deparse_result(bad);
    good = pg_query_deparse_ast(valid);
    assert(good.query && !good.error);
    pg_query_free_deparse_result(good);
    pg_query__parse_result__free_unpacked(valid,NULL);
    pg_query_free_protobuf_parse_result(parsed);
  }
  pg_query_exit();
  puts("direct AST parity, borrowed immutability, source identities, nested calls and error recovery: 12 SQL cases passed");
  return 0;
}
