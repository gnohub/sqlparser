/* Regression coverage for render scaffold public. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "sqlparser/sqlparser.h"
#ifdef SQLPARSER_RENDER_PUBLIC_WRAP
#include "../../src/core/sqlparser_ast_internal.h"
#endif

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "public render [%s] line %d: %s\n", case_name, __LINE__, #x); exit(1); } } while (0)
static const char *case_name = "initialization";
static int dump, strict, expected_template_mode = -1;
static size_t corpus_inputs, dialect_override_inputs, clauses, select_lists, targets, mutations, statuses;
static size_t failures, positive_cases, lifetime_checks, measured_calls;
static size_t insert_cells, assignments, session_values, fragment_cases;

#ifdef SQLPARSER_RENDER_PUBLIC_WRAP
static int count_parses;
static size_t fixed_parses[6], other_parses;
static const char *fixed_templates[] = {
    "SELECT NULL FROM __sqlparser_source__",
    "SELECT 1 FROM __sqlparser_source__ WHERE TRUE",
    "SELECT 1 FROM __sqlparser_source__ ORDER BY 1",
    "INSERT INTO __sqlparser_target__ VALUES (NULL)",
    "UPDATE __sqlparser_target__ SET __sqlparser_column__ = NULL",
    "SET __sqlparser_session_parameter__ = __sqlparser_placeholder__"
};
PgQueryOwnedParseResult __real_sqlparser_parse_owned_preserving_identifier_spelling(const char *);
PgQueryOwnedParseResult __wrap_sqlparser_parse_owned_preserving_identifier_spelling(const char *sql)
{
    if (count_parses) {
        size_t i;
        for (i = 0; i < COUNT(fixed_templates); i++)
            if (!strcmp(sql, fixed_templates[i])) { fixed_parses[i]++; break; }
        if (i == COUNT(fixed_templates)) other_parses++;
    }
    return __real_sqlparser_parse_owned_preserving_identifier_spelling(sql);
}
#endif

static void text(const char *s)
{
    if (!dump) return;
    if (s == NULL) { fputs("-", stdout); return; }
    for (; *s; s++) printf("%02x", (unsigned char)*s);
}
static void begin_case(const char *name, sqlparser_dialect_t dialect, const char *sql)
{
    case_name = name;
    if (dump) { printf("CASE|%d|", dialect); text(name); putchar('|'); text(sql); putchar('\n'); }
}
static void seed_error(sqlparser_error_t *e) { memset(e, 0xa5, sizeof(*e)); }
static int result(const char *operation, sqlparser_status_t s, const sqlparser_error_t *e)
{
    statuses++;
    CHECK(e->code == s);
    if (s == SQLPARSER_STATUS_OK) {
        CHECK(e->cursor == 0 && e->line == 0 && e->column == 0 && e->message[0] == '\0');
    } else {
        failures++;
        CHECK(memchr(e->message, '\0', sizeof(e->message)) != NULL);
    }
    if (dump) {
        printf("%s|%d|%d|%d|%d|", operation, s, e->cursor, e->line, e->column);
        text(e->message); putchar('\n');
    }
    if (strict && s != SQLPARSER_STATUS_OK) {
        fprintf(stderr, "operation=%s status=%d error=%s\n", operation, s, e->message);
        CHECK(s == SQLPARSER_STATUS_OK);
    }
    return s == SQLPARSER_STATUS_OK;
}
static sqlparser_handle_t *parse(const char *sql, const sqlparser_parse_options_t *o)
{
    sqlparser_handle_t *h = NULL;
    sqlparser_error_t e;
    sqlparser_status_t s;
    seed_error(&e);
    s = sqlparser_parse_with_options(sql, o, &h, &e);
    if (!result("parse", s, &e)) { CHECK(h == NULL); return NULL; }
    CHECK(h != NULL);
    return h;
}
static int output_result(const char *op, sqlparser_status_t s,
    const sqlparser_error_t *e, char *output)
{
    int ok = result(op, s, e);
    /* Absent WHERE/ORDER clauses may succeed with a NULL SQL pointer. */
    CHECK(ok || output == NULL);
    if (dump) { fputs("SQL|", stdout); text(output); putchar('\n'); }
    return ok;
}

/* On ANY failure stop using the handle and let the caller destroy it. This is
 * deliberately stronger than trying to distinguish terminal patch failures. */
static int inspect(sqlparser_handle_t *h, int compare_individual)
{
    sqlparser_error_t e;
    sqlparser_status_t status;
    size_t statement, n_statements = sqlparser_statement_count(h);
    char *whole = NULL;
    if (dump) printf("STATEMENTS|%zu\n", n_statements);
    for (statement = 0; statement < n_statements; statement++) {
        size_t n_clauses = 0, c, list = 0;
        seed_error(&e);
        status = sqlparser_statement_clause_count(h, statement, &n_clauses, &e);
        if (!result("clause-count", status, &e)) return 0;
        if (dump) printf("CLAUSES|%zu|%zu\n", statement, n_clauses);
        for (c = 0; c < n_clauses; c++) {
            sqlparser_clause_view_t view;
            char *actual = NULL;
            seed_error(&e);
            status = sqlparser_statement_clause(h, statement, c, &view, &e);
            if (!result("clause", status, &e)) return 0;
            if (dump) printf("CLAUSE|%zu|%zu|%d\n", statement, c, view.kind);
            seed_error(&e);
            status = sqlparser_statement_clause_sql(h, statement, c, &actual, &e);
            if (!output_result("clause-sql", status, &e, actual)) return 0;
            clauses++;
            if (view.kind == SQLPARSER_CLAUSE_KIND_SELECT_LIST) {
                CHECK(actual != NULL);
                select_lists++;
                if (compare_individual) {
                    size_t count = 0, target, used = 0, capacity = 1;
                    char *joined = malloc(capacity), *by_view = NULL, *by_selector = NULL;
                    CHECK(joined != NULL); joined[0] = '\0';
                    seed_error(&e);
                    status = sqlparser_select_target_count(h, statement, list, &count, &e);
                    if (!result("target-count", status, &e)) { free(joined); sqlparser_string_free(actual); return 0; }
                    CHECK(count > 0);
                    for (target = 0; target < count; target++) {
                        char *one = NULL;
                        size_t length;
                        seed_error(&e);
                        status = sqlparser_select_target_sql(h, statement, list, target, &one, &e);
                        if (!output_result("target-sql", status, &e, one)) { free(joined); sqlparser_string_free(actual); return 0; }
                        CHECK(one != NULL);
                        length = strlen(one);
                        capacity += length + (target != 0 ? 2U : 0U);
                        joined = realloc(joined, capacity); CHECK(joined != NULL);
                        if (target) { joined[used++] = ','; joined[used++] = ' '; }
                        memcpy(joined + used, one, length + 1U); used += length;
                        targets++; sqlparser_string_free(one);
                    }
                    CHECK(strcmp(actual, joined) == 0); free(joined);
                    seed_error(&e);
                    status = sqlparser_clause_sql(&view, &by_view, &e);
                    if (!output_result("view-clause-sql", status, &e, by_view)) { sqlparser_string_free(actual); return 0; }
                    CHECK(by_view != NULL);
                    CHECK(strcmp(actual, by_view) == 0); sqlparser_string_free(by_view);
                    seed_error(&e);
                    status = sqlparser_selector_clause_sql(h, &view.selector, &by_selector, &e);
                    if (!output_result("selector-clause-sql", status, &e, by_selector)) { sqlparser_string_free(actual); return 0; }
                    CHECK(by_selector != NULL);
                    CHECK(strcmp(actual, by_selector) == 0); sqlparser_string_free(by_selector);
                }
                list++;
            }
            sqlparser_string_free(actual);
        }
    }
    /* Dedicated positives require SELECT rendering to work after every edit.
     * Full-statement deparse has independent existing source-rebase failures;
     * its exact status/output is covered in the paired corpus/edge paths. */
    if (compare_individual) return 1;
    seed_error(&e);
    status = sqlparser_deparse(h, &whole, &e);
    if (!output_result("deparse", status, &e, whole)) return 0;
    CHECK(whole != NULL);
    sqlparser_string_free(whole);
    return 1;
}

static int first_select(sqlparser_handle_t *h, size_t *statement, size_t *clause)
{
    sqlparser_error_t e;
    sqlparser_status_t s;
    size_t n_statements = sqlparser_statement_count(h);
    for (*statement = 0; *statement < n_statements; ++*statement) {
        size_t count = 0;
        seed_error(&e); s = sqlparser_statement_clause_count(h, *statement, &count, &e);
        if (!result("find-clause-count", s, &e)) return -1;
        for (*clause = 0; *clause < count; ++*clause) {
            sqlparser_clause_view_t v;
            seed_error(&e); s = sqlparser_statement_clause(h, *statement, *clause, &v, &e);
            if (!result("find-clause", s, &e)) return -1;
            if (v.kind == SQLPARSER_CLAUSE_KIND_SELECT_LIST) return 1;
        }
    }
    return 0;
}
static void corpus_extra(const char *sql, const sqlparser_parse_options_t *o);

static void corpus_case(const char *sql, sqlparser_dialect_t dialect)
{
    sqlparser_parse_options_t options;
    sqlparser_handle_t *h;
    sqlparser_error_t e;
    sqlparser_status_t s;
    size_t statement, clause;
    sqlparser_parse_options_default(&options); options.dialect = dialect;
    corpus_extra(sql, &options);
    if (dump) fputs("PHASE|corpus-clause-cold\n", stdout);
    h = parse(sql, &options);
    if (h) { (void)inspect(h, 0); sqlparser_handle_destroy(h); }
    /* Reparse before the mutation path, including after a failed original
     * reader. Never inspect or mutate a handle after it returned an error. */
    if (dump) fputs("PHASE|corpus-clause-mutation\n", stdout);
    h = parse(sql, &options);
    if (!h) return;
    if (first_select(h, &statement, &clause) == 1) {
        seed_error(&e);
        s = sqlparser_statement_set_clause_sql(h, statement, clause,
            "42 AS resolved_value, '日本 café' AS resolved_text", &e);
        if (result("replace-select-list", s, &e)) {
            if (dump) fputs("PHASE|corpus-clause-postmutation\n", stdout);
            mutations++; (void)inspect(h, 0);
        }
    }
    sqlparser_handle_destroy(h);
}
static void corpora(const char *directory)
{
    static const struct { const char *file; sqlparser_dialect_t dialect; } files[] = {
        {"sql_batch_input.json", SQLPARSER_DIALECT_POSTGRESQL},
        {"mysql_dialect_input.json", SQLPARSER_DIALECT_MYSQL},
        {"oracle_dialect_input.json", SQLPARSER_DIALECT_ORACLE},
        {"sqlserver_dialect_input.json", SQLPARSER_DIALECT_SQLSERVER},
        {"dameng_dialect_input.json", SQLPARSER_DIALECT_DAMENG},
        {"vastbase_oracle_dialect_input.json", SQLPARSER_DIALECT_VASTBASE_ORACLE},
        {"vastbase_mysql_dialect_input.json", SQLPARSER_DIALECT_VASTBASE_MYSQL},
        {"vastbase_postgresql_dialect_input.json", SQLPARSER_DIALECT_VASTBASE_POSTGRESQL},
        {"vastbase_sqlserver_dialect_input.json", SQLPARSER_DIALECT_VASTBASE_SQLSERVER},
        {"kingbase_oracle_dialect_input.json", SQLPARSER_DIALECT_KINGBASE_ORACLE},
        {"kingbase_mysql_dialect_input.json", SQLPARSER_DIALECT_KINGBASE_MYSQL},
        {"kingbase_postgresql_dialect_input.json", SQLPARSER_DIALECT_KINGBASE_POSTGRESQL},
        {"kingbase_sqlserver_dialect_input.json", SQLPARSER_DIALECT_KINGBASE_SQLSERVER}
    };
    size_t f;
    for (f = 0; f < COUNT(files); f++) {
        char path[4096]; json_error_t error; json_t *root, *items, *item; size_t i;
        CHECK(snprintf(path, sizeof(path), "%s/%s", directory, files[f].file) < (int)sizeof(path));
        root = json_load_file(path, 0, &error);
        if (!root) fprintf(stderr, "%s: %s\n", path, error.text);
        CHECK(root != NULL);
        items = json_object_get(root, "items"); CHECK(json_is_array(items));
        json_array_foreach(items, i, item) {
            const char *sql = json_string_value(json_object_get(item, "sql"));
            const char *name = json_string_value(json_object_get(item, "name"));
            CHECK(sql != NULL && name != NULL);
            begin_case(name, files[f].dialect, sql);
            corpus_case(sql, files[f].dialect); corpus_inputs++;
            /* Preserve the inherited file-default diagnostic pass, and also
             * honor the few cross-dialect CLI fixtures' declared dialects. */
            {
                const char *declared = json_string_value(json_object_get(item, "dialect"));
                size_t d;
                static const char *names[] = {"postgresql", "mysql", "oracle", "sqlserver", "dameng", "vastbase-oracle", "vastbase-mysql", "vastbase-postgresql", "vastbase-sqlserver", "kingbase-oracle", "kingbase-mysql", "kingbase-postgresql", "kingbase-sqlserver"};
                if (declared) {
                    for (d = 0; d < COUNT(names) && strcmp(declared, names[d]); d++) {}
                    CHECK(d < COUNT(names));
                    if ((sqlparser_dialect_t)d != files[f].dialect) {
                        begin_case(name, (sqlparser_dialect_t)d, sql);
                        if (dump) fputs("DECLARED_DIALECT_PASS\n", stdout);
                        corpus_case(sql, (sqlparser_dialect_t)d); dialect_override_inputs++;
                    }
                }
            }
        }
        fprintf(stderr, "corpus %s: %zu inputs\n", files[f].file, json_array_size(items));
        json_decref(root);
    }
    CHECK(corpus_inputs == 3269U);
    CHECK(dialect_override_inputs == 7U);
}

static void positive_case(const char *name, sqlparser_dialect_t dialect,
    const char *sql, int do_mutations)
{
    sqlparser_parse_options_t o;
    sqlparser_handle_t *h;
    sqlparser_error_t e;
    sqlparser_status_t s;
    size_t statement, clause;
    char *saved = NULL, *copy;
    begin_case(name, dialect, sql);
    sqlparser_parse_options_default(&o); o.dialect = dialect;
    h = parse(sql, &o); CHECK(h != NULL);
    CHECK(inspect(h, 1));
    CHECK(first_select(h, &statement, &clause) == 1);
    seed_error(&e); s = sqlparser_statement_clause_sql(h, statement, clause, &saved, &e);
    CHECK(output_result("retained-clause", s, &e, saved));
    copy = strdup(saved); CHECK(copy != NULL);
    if (do_mutations) {
        sqlparser_selector_t selector = {0};
#define MUTATE(op, call) do { seed_error(&e); s = (call); CHECK(result(op, s, &e)); mutations++; CHECK(inspect(h, 1)); CHECK(strcmp(saved, copy) == 0); } while (0)
        MUTATE("set-target", sqlparser_select_set_target_sql(h, statement, 0, 0, "41 + 1 AS replaced", &e));
        MUTATE("insert-target", sqlparser_select_insert_target_sql(h, statement, 0, 1, "'quote '' 日本' AS inserted", &e));
        MUTATE("delete-target", sqlparser_select_delete_target(h, statement, 0, 1, &e));
        MUTATE("set-targets", sqlparser_select_set_targets_sql(h, statement, 0, "17 AS a, 23 AS b, 'café 日本' AS c", &e));
        CHECK(first_select(h, &statement, &clause) == 1);
        MUTATE("set-clause", sqlparser_statement_set_clause_sql(h, statement, clause, "1 AS a, 2 AS b", &e));
        selector.kind = SQLPARSER_SELECTOR_KIND_CLAUSE;
        selector.statement_index = statement; selector.item_index = clause;
        MUTATE("selector-set-clause", sqlparser_selector_set_clause_sql(h, &selector, "3 AS a, 4 AS b, 5 AS c", &e));
#undef MUTATE
    }
    sqlparser_handle_destroy(h);
    CHECK(strcmp(saved, copy) == 0); lifetime_checks++;
    sqlparser_string_free(saved); free(copy); positive_cases++;
}
static void positives(void)
{
    static const struct { const char *name, *sql; } pg[] = {
        {"scalar-values", "SELECT *, t.*, 1 AS i, -2 AS neg, 123456789012345678901 AS big, 1.25 AS decimal, 2.5e-10 AS exponent, NULL AS n, true AS b, false AS f, '' AS empty, 'quote '' done' AS quoted, '日本 café 😀' AS \"別名\", \"MiXeD\" AS \"A\"\"B\" FROM t"},
        {"expression-values", "SELECT a + 2 AS arithmetic, CAST(a AS text) AS casted, CASE WHEN a > 0 THEN 'yes' ELSE 'no' END AS conditional, coalesce(a, 0) AS called, ARRAY[1,2] AS arr, ROW(1,'a') AS record, B'101' AS bits, DATE '2020-01-01' AS date_value, INTERVAL '1 day' AS interval_value, a IS NULL AS is_null, data->>'key' AS json_value FROM t"},
        {"nested-select-owner", "SELECT (SELECT x, y FROM q WHERE y = 1) AS nested, u.a, (SELECT max(b) FROM r) AS maximum FROM (SELECT a, c FROM t) u WHERE EXISTS (SELECT 1 FROM z WHERE z.a = u.a) ORDER BY u.a"},
        {"cte-and-set-owners", "WITH c AS (SELECT 1 AS a, 2 AS b), d AS (SELECT a, b FROM c UNION ALL SELECT 3, 4) SELECT a, (SELECT max(b) FROM d) AS m FROM c UNION SELECT a, b FROM d ORDER BY a"},
        {"recursive-cte", "WITH RECURSIVE c(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM c WHERE n < 4) SELECT n, n * 2 AS twice FROM c"},
        {"intersect-except", "(SELECT a, b FROM t INTERSECT SELECT c, d FROM u) EXCEPT SELECT e, f FROM v"},
        {"insert-select", "INSERT INTO dest(a,b) SELECT x, (SELECT max(y) FROM q) FROM src RETURNING a"},
        {"binds-across-statements", "SELECT $1 AS a, $2 AS b, (SELECT $1) AS c; SELECT $3 AS d, $1 AS e; INSERT INTO t(a) SELECT $2"},
        {"window-and-aggregate", "SELECT count(*) AS n, sum(a) FILTER (WHERE b > 1) OVER (PARTITION BY c ORDER BY d) AS total, row_number() OVER () AS rn FROM t"}
    };
    size_t i;
    strict = 1;
    for (i = 0; i < COUNT(pg); i++) positive_case(pg[i].name, SQLPARSER_DIALECT_POSTGRESQL, pg[i].sql, 1);
    for (i = SQLPARSER_DIALECT_POSTGRESQL; i <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; i++)
        positive_case("dialect-family", (sqlparser_dialect_t)i,
            "SELECT 1 AS one, '日本 café' AS label FROM t; SELECT 2 AS two, NULL AS empty FROM t", 1);
    positive_case("mysql-quotes-and-binds", SQLPARSER_DIALECT_MYSQL,
        "SELECT `a`, \"text\" AS `label`, ? AS `p`, (SELECT ? FROM q) AS `nested` FROM t; SELECT ? AS `tail`", 1);
    positive_case("oracle-quotes-and-binds", SQLPARSER_DIALECT_ORACLE,
        "SELECT \"a\", q'[quote ' 日本]' AS \"label\", :first AS p, (SELECT :second FROM dual) AS nested FROM dual; SELECT :first FROM dual", 1);
    positive_case("sqlserver-quotes-and-binds", SQLPARSER_DIALECT_SQLSERVER,
        "SELECT [a], N'日本' AS [label], @first AS p, (SELECT @second FROM q) AS nested FROM t; SELECT @first AS tail", 1);
    strict = 0;
}

static void edge_cases(void)
{
    static const struct { sqlparser_dialect_t dialect; const char *sql; } cases[] = {
        {SQLPARSER_DIALECT_POSTGRESQL, "SET standard_conforming_strings = off; SELECT 'a\\b' AS x, E'a\\tb' AS y; SET standard_conforming_strings = on; SELECT 'c\\d' AS z"},
        {SQLPARSER_DIALECT_MYSQL, "SET sql_mode = 'ANSI_QUOTES'; SELECT \"id\", 'text' AS \"label\" FROM t; SET sql_mode = ''; SELECT \"text\" AS label"},
        {SQLPARSER_DIALECT_MYSQL, "SET sql_mode = 'NO_BACKSLASH_ESCAPES'; SELECT 'a\\b' AS x; SET sql_mode = ''; SELECT 'c\\td' AS y"},
        {SQLPARSER_DIALECT_SQLSERVER, "SET QUOTED_IDENTIFIER OFF; SELECT \"text\" AS [label]; SET QUOTED_IDENTIFIER ON; SELECT \"id\" FROM t"},
        {SQLPARSER_DIALECT_ORACLE, "SELECT q'{日本 ''}' AS \"quoted\", :p AS \"bind\" FROM dual MINUS SELECT 'x', :q FROM dual"},
        {SQLPARSER_DIALECT_MYSQL, "UPDATE t SET a = (SELECT x FROM q) WHERE b = ? ORDER BY a LIMIT 1"},
        {SQLPARSER_DIALECT_MYSQL, "DELETE FROM t WHERE a IN (SELECT x FROM q) ORDER BY a LIMIT 1"},
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT"},
        {SQLPARSER_DIALECT_POSTGRESQL, "SELECT FROM t"},
        {SQLPARSER_DIALECT_POSTGRESQL, "VALUES (1, '日本'), (2, 'café')"},
        {SQLPARSER_DIALECT_POSTGRESQL, ""}
    };
    size_t i;
    for (i = 0; i < COUNT(cases); i++) {
        begin_case("session-and-edge", cases[i].dialect, cases[i].sql);
        corpus_case(cases[i].sql, cases[i].dialect);
    }
}

/* Retain this concrete baseline-known, post-insert full-deparse diagnostic.
 * Clause rendering succeeds, but reference whole-statement deparse returns status 4.
 * Do not reuse the handle afterward; paired transcripts determine parity. */
static void mixed_expression_deparse_diagnostic(void)
{
    const char *sql = "SELECT a + 2 AS arithmetic, CAST(a AS text) AS casted, CASE WHEN a > 0 THEN 'yes' ELSE 'no' END AS conditional, coalesce(a, 0) AS called, ARRAY[1,2] AS arr, ROW(1,'a') AS record, B'101' AS bits, DATE '2020-01-01' AS date_value, INTERVAL '1 day' AS interval_value, a IS NULL AS is_null, data->>'key' AS json_value FROM t";
    sqlparser_parse_options_t o;
    sqlparser_handle_t *h;
    sqlparser_error_t e;
    sqlparser_status_t s;
    char *out = NULL;
    begin_case("known-mixed-expression-post-insert-deparse", SQLPARSER_DIALECT_POSTGRESQL, sql);
    sqlparser_parse_options_default(&o); h = parse(sql, &o); CHECK(h);
    seed_error(&e);
    s = sqlparser_select_set_target_sql(h, 0, 0, 0, "41 + 1 AS replaced", &e);
    if (!result("diagnostic-set-target", s, &e)) { sqlparser_handle_destroy(h); return; }
    seed_error(&e);
    s = sqlparser_select_insert_target_sql(h, 0, 0, 1, "'quote '' 日本' AS inserted", &e);
    if (!result("diagnostic-insert-target", s, &e)) { sqlparser_handle_destroy(h); return; }
    mutations += 2U;
    if (inspect(h, 1)) {
        seed_error(&e); s = sqlparser_deparse(h, &out, &e);
        (void)output_result("diagnostic-post-insert-deparse", s, &e, out);
        sqlparser_string_free(out);
    }
    sqlparser_handle_destroy(h);
}

static void limits_and_errors(void)
{
    static const size_t limits[] = {0, 1, 2, 6, 7, 8, 15, 16, 31, 32, 63, 64, 127, 256};
    const char *input = "SELECT '日本 café' AS label, 123 AS value; SELECT 7 AS other";
    size_t i;
    for (i = 0; i < COUNT(limits); i++) {
        sqlparser_parse_options_t o; sqlparser_handle_t *h;
        begin_case("output-limit", SQLPARSER_DIALECT_POSTGRESQL, input);
        if (dump) printf("LIMIT|%zu\n", limits[i]);
        sqlparser_parse_options_default(&o); o.limits.max_output_bytes = limits[i];
        h = parse(input, &o); if (h) { (void)inspect(h, 0); sqlparser_handle_destroy(h); }
    }
    for (i = 0; i < 5; i++) {
        sqlparser_parse_options_t o; sqlparser_handle_t *h; sqlparser_error_t e;
        sqlparser_status_t s; char *out = NULL;
        begin_case("invalid-read-output", SQLPARSER_DIALECT_POSTGRESQL, "SELECT 1, 2");
        sqlparser_parse_options_default(&o); h = parse("SELECT 1, 2", &o); CHECK(h);
        seed_error(&e);
        switch (i) {
            case 0: s = sqlparser_statement_clause_sql(h, SIZE_MAX, 0, &out, &e); break;
            case 1: s = sqlparser_statement_clause_sql(h, 0, SIZE_MAX, &out, &e); break;
            case 2: s = sqlparser_statement_clause_sql(h, 0, 0, NULL, &e); break;
            case 3: s = sqlparser_select_target_sql(h, 0, 0, SIZE_MAX, &out, &e); break;
            default: s = sqlparser_select_target_sql(h, 0, SIZE_MAX, 0, &out, &e); break;
        }
        CHECK(s == SQLPARSER_STATUS_INVALID_ARGUMENT);
        CHECK(!output_result("invalid-read", s, &e, out)); sqlparser_handle_destroy(h);
    }
    for (i = 0; i < 3; i++) {
        sqlparser_parse_options_t o; sqlparser_handle_t *h; sqlparser_error_t e; sqlparser_status_t s;
        begin_case("terminal-mutation-error", SQLPARSER_DIALECT_POSTGRESQL, "SELECT 1, 2");
        sqlparser_parse_options_default(&o); h = parse("SELECT 1, 2", &o); CHECK(h);
        seed_error(&e);
        s = sqlparser_select_set_target_sql(h, 0, i == 0 ? SIZE_MAX : 0, i == 1 ? SIZE_MAX : 0,
            i == 2 ? "SELECT" : "3", &e);
        CHECK(!result("invalid-mutation", s, &e)); sqlparser_handle_destroy(h);
    }
}

/* All readers stop on the first error. Independent reader families always use
 * a fresh parse, so an existing unsupported path cannot poison later evidence. */
static int inspect_direct(sqlparser_handle_t *h)
{
    size_t statement;
    sqlparser_error_t e;
    sqlparser_status_t s;
    for (statement = 0; statement < sqlparser_statement_count(h); statement++) {
        size_t list, lists = 0, where, wheres = 0;
        sqlparser_statement_kind_t kind;
        seed_error(&e); s = sqlparser_statement_kind(h, statement, &kind, &e);
        if (!result("direct-kind", s, &e)) return 0;
        if (dump) printf("DIRECT|%zu|%d\n", statement, kind);
        seed_error(&e); s = sqlparser_select_target_list_count(h, statement, &lists, &e);
        if (!result("direct-select-list-count", s, &e)) return 0;
        for (list = 0; list < lists; list++) {
            size_t target, count = 0;
            seed_error(&e); s = sqlparser_select_target_count(h, statement, list, &count, &e);
            if (!result("direct-target-count", s, &e)) return 0;
            for (target = 0; target < count; target++) {
                char *out = NULL;
                seed_error(&e); s = sqlparser_select_target_sql(h, statement, list, target, &out, &e);
                if (!output_result("direct-target-sql", s, &e, out)) return 0;
                targets++; sqlparser_string_free(out);
            }
        }
        seed_error(&e); s = sqlparser_statement_where_count(h, statement, &wheres, &e);
        if (!result("direct-where-count", s, &e)) return 0;
        for (where = 0; where < wheres; where++) {
            char *out = NULL;
            seed_error(&e); s = sqlparser_statement_where_sql(h, statement, where, &out, &e);
            if (!output_result("direct-where-sql", s, &e, out)) return 0;
            sqlparser_string_free(out);
        }
        if (kind == SQLPARSER_STATEMENT_KIND_INSERT) {
            sqlparser_insert_source_kind_t source;
            size_t row, rows = 0, column, columns = 0;
            seed_error(&e); s = sqlparser_insert_source_kind(h, statement, &source, &e);
            if (!result("direct-insert-source", s, &e)) return 0;
            if (source != SQLPARSER_INSERT_SOURCE_VALUES) continue;
            seed_error(&e); s = sqlparser_insert_row_count(h, statement, &rows, &e);
            if (!result("direct-insert-rows", s, &e)) return 0;
            seed_error(&e); s = sqlparser_insert_column_count(h, statement, &columns, &e);
            if (!result("direct-insert-columns", s, &e)) return 0;
            if (dump) printf("CELLS|%zu|%zu|%zu\n", statement, rows, columns);
            for (row = 0; row < rows; row++) for (column = 0; column < columns; column++) {
                char *out = NULL;
                seed_error(&e); s = sqlparser_insert_cell_sql(h, statement, row, column, &out, &e);
                if (!output_result("direct-insert-cell", s, &e, out)) return 0;
                insert_cells++; sqlparser_string_free(out);
            }
        } else if (kind == SQLPARSER_STATEMENT_KIND_UPDATE) {
            size_t assignment, count = 0;
            seed_error(&e); s = sqlparser_update_assignment_count(h, statement, &count, &e);
            if (!result("direct-update-count", s, &e)) return 0;
            for (assignment = 0; assignment < count; assignment++) {
                char *out = NULL;
                seed_error(&e); s = sqlparser_update_assignment_sql(h, statement, assignment, &out, &e);
                if (!output_result("direct-update-assignment", s, &e, out)) return 0;
                assignments++; sqlparser_string_free(out);
            }
        }
    }
    return 1;
}

static int inspect_sessions(sqlparser_handle_t *h)
{
    size_t statement;
    sqlparser_error_t e;
    sqlparser_status_t s;
    for (statement = 0; statement < sqlparser_statement_count(h); statement++) {
        sqlparser_query_graph_view_t graph;
        sqlparser_graph_session_t session;
        size_t item;
        seed_error(&e); s = sqlparser_statement_query_graph(h, statement, &graph, &e);
        if (!result("session-graph", s, &e)) return 0;
        seed_error(&e); s = sqlparser_query_graph_session(&graph, &session, &e);
        if (!result("session-summary", s, &e)) return 0;
        if (dump) printf("SESSION|%zu|%d|%zu\n", statement, session.action, session.item_count);
        for (item = 0; item < session.item_count; item++) {
            sqlparser_graph_session_item_t v;
            size_t i;
            seed_error(&e); s = sqlparser_query_graph_session_item_at(&graph, item, &v, &e);
            if (!result("session-item", s, &e)) return 0;
            if (dump) {
                printf("SESSION_ITEM|%zu|%d|%d|%zu|%zu|", v.index, v.scope, v.target_kind, v.value_offset, v.value_count);
                text(v.name); putchar('\n');
            }
            for (i = 0; i < v.value_count; i++) {
                sqlparser_graph_session_value_t value;
                seed_error(&e); s = sqlparser_query_graph_session_value_at(&graph, v.value_offset + i, &value, &e);
                if (!result("session-value", s, &e)) return 0;
                session_values++;
                if (dump) {
                    printf("SESSION_VALUE|%zu|%d|", value.index, value.kind); text(value.name); putchar('|');
                    text(value.text); printf("|%d|", value.literal.kind); text(value.literal.string_value); putchar('|');
                    text(value.literal.float_value); printf("|%lld|%d|%d|", value.literal.integer_value, value.literal.boolean_value, value.literal.quoted_identifier);
                    text(value.bind_key); printf("|%d|", value.bind_kind); text(value.bind_sql);
                    printf("|%zu|%d\n", value.bind_position, value.has_bind_position);
                }
            }
        }
    }
    return 1;
}

static void corpus_extra(const char *sql, const sqlparser_parse_options_t *o)
{
    sqlparser_handle_t *h;
    if (dump) fputs("PHASE|corpus-direct-cold\n", stdout);
    h = parse(sql, o);
    if (h) {
        if (inspect_direct(h)) {
            if (dump) fputs("PHASE|corpus-direct-warm\n", stdout);
            (void)inspect_direct(h);
        }
        sqlparser_handle_destroy(h);
    }
    if (dump) fputs("PHASE|corpus-session-cold\n", stdout);
    h = parse(sql, o);
    if (h) {
        if (inspect_sessions(h)) {
            if (dump) fputs("PHASE|corpus-session-warm\n", stdout);
            (void)inspect_sessions(h);
        }
        sqlparser_handle_destroy(h);
    }
}

enum render_path { TARGET, WHERE, ORDER, INSERT, UPDATE, SESSION };
static const char *path_names[] = {"target", "where", "order", "insert", "update", "session"};

static int order_clause(sqlparser_handle_t *h, size_t *out)
{
    sqlparser_error_t e; sqlparser_status_t s; size_t i, n = 0;
    seed_error(&e); s = sqlparser_statement_clause_count(h, 0, &n, &e);
    if (!result("order-clause-count", s, &e)) return 0;
    for (i = 0; i < n; i++) {
        sqlparser_clause_view_t v;
        seed_error(&e); s = sqlparser_statement_clause(h, 0, i, &v, &e);
        if (!result("order-clause", s, &e)) return 0;
        if (v.kind == SQLPARSER_CLAUSE_KIND_ORDER_BY) { *out = i; return 1; }
    }
    CHECK(0); return 0;
}
static int render_one(sqlparser_handle_t *h, enum render_path path, size_t index, int selector_api, char **out)
{
    sqlparser_error_t e; sqlparser_status_t s;
    sqlparser_selector_t selector = {0};
    selector.statement_index = 0; selector.item_index = index;
    *out = NULL;
    seed_error(&e);
    switch (path) {
        case TARGET:
            selector.kind = SQLPARSER_SELECTOR_KIND_SELECT_TARGET;
            selector.item_index = 0; selector.column_index = index;
            s = selector_api ? sqlparser_selector_select_target_sql(h, &selector, out, &e) :
                sqlparser_select_target_sql(h, 0, 0, index, out, &e); break;
        case WHERE:
            selector.kind = SQLPARSER_SELECTOR_KIND_WHERE;
            s = selector_api ? sqlparser_selector_where_sql(h, &selector, out, &e) :
                sqlparser_statement_where_sql(h, 0, index, out, &e); break;
        case ORDER:
            selector.kind = SQLPARSER_SELECTOR_KIND_CLAUSE;
            s = selector_api ? sqlparser_selector_clause_sql(h, &selector, out, &e) :
                sqlparser_statement_clause_sql(h, 0, index, out, &e); break;
        case INSERT:
            selector.kind = SQLPARSER_SELECTOR_KIND_INSERT_CELL;
            selector.item_index = 0; selector.column_index = index;
            s = selector_api ? sqlparser_selector_insert_cell_sql(h, &selector, out, &e) :
                sqlparser_insert_cell_sql(h, 0, 0, index, out, &e); break;
        case UPDATE:
            selector.kind = SQLPARSER_SELECTOR_KIND_ASSIGNMENT;
            s = selector_api ? sqlparser_selector_update_assignment_sql(h, &selector, out, &e) :
                sqlparser_update_assignment_sql(h, 0, index, out, &e); break;
        default:
            s = sqlparser_export_view_json(h, 0, out, &e); break;
    }
    return output_result(path_names[path], s, &e, *out);
}
static int mutate_one(sqlparser_handle_t *h, enum render_path path, size_t index, const char *replacement)
{
    sqlparser_error_t e; sqlparser_status_t s;
    seed_error(&e);
    switch (path) {
        case TARGET: s = sqlparser_select_set_target_sql(h, 0, 0, index, replacement, &e); break;
        case WHERE: s = sqlparser_statement_set_where_sql(h, 0, index, replacement, &e); break;
        case ORDER: s = sqlparser_statement_set_clause_sql(h, 0, index, replacement, &e); break;
        case INSERT: s = sqlparser_insert_set_cell_sql(h, 0, 0, index, replacement, &e); break;
        case UPDATE: s = sqlparser_update_set_assignment_sql(h, 0, index, replacement, &e); break;
        default: {
            sqlparser_literal_value_t value = {0};
            value.kind = SQLPARSER_LITERAL_KIND_STRING; value.string_value = replacement;
            s = sqlparser_statement_set_literal(h, 0, 0, &value, &e); break;
        }
    }
    if (!result("fragment-mutation", s, &e)) return 0;
    mutations++; return 1;
}

/* Retain four separately allocated outputs through a mutation and handle
 * destruction. Direct/selector and cold/warm reads must agree exactly. */
static void render_case(const char *name, sqlparser_dialect_t dialect, const char *sql,
    enum render_path path, size_t index, const char *replacement, int require_success)
{
    sqlparser_parse_options_t o; sqlparser_handle_t *h;
    char *out[4] = {0}, *copies[4] = {0}; size_t i;
    begin_case(name, dialect, sql);
    if (dump) printf("RENDER_PATH|%s|%zu\n", path_names[path], index);
    sqlparser_parse_options_default(&o); o.dialect = dialect;
    h = parse(sql, &o); if (!h) { CHECK(!require_success); return; }
    if (path == ORDER && !order_clause(h, &index)) goto done;
    for (i = 0; i < 4; i++) {
        if (dump) printf("PHASE|%s\n", i == 0 ? "cold" : i == 1 ? "warm" : i == 2 ? "postmutation-cold" : "postmutation-warm");
        if (i == 2 && replacement && !mutate_one(h, path, index, replacement)) { CHECK(!require_success); goto done; }
        if (!render_one(h, path, index, i & 1, &out[i])) { CHECK(!require_success); goto done; }
        CHECK(out[i]); copies[i] = strdup(out[i]); CHECK(copies[i]);
        if (i & 1) CHECK(strcmp(out[i - 1], out[i]) == 0);
        if (i) CHECK(out[i] != out[i - 1]);
        if (path == SESSION && !inspect_sessions(h)) { CHECK(!require_success); goto done; }
    }
    fragment_cases++;
done:
    sqlparser_handle_destroy(h);
    for (i = 0; i < COUNT(out); i++) if (out[i]) {
        CHECK(copies[i]); CHECK(strcmp(out[i], copies[i]) == 0); lifetime_checks++;
        sqlparser_string_free(out[i]); free(copies[i]);
    }
}

static int rename_insert_relation(sqlparser_handle_t *h)
{
    sqlparser_error_t e; sqlparser_status_t s;
    seed_error(&e);
    s = sqlparser_statement_set_relation_name(h, 0, 0, NULL, "renamed_target", &e);
    if (!result("semantic-relation-rename", s, &e)) return 0;
    mutations++; return 1;
}

/* A semantic relation rename deliberately uses the supported AST mutation
 * path. Scalar cells then have no complete source replay, exercising the
 * INSERT scaffold through the public cell reader instead of a source slice. */
static void insert_scaffold_cases(void)
{
    static const char *values[] = {"DEFAULT", "NULL", "42", "1.25", "'日本 café'", "'quote '' done'"};
    size_t dialect, value;
    for (dialect = SQLPARSER_DIALECT_POSTGRESQL; dialect <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; dialect++)
    for (value = 0; value < COUNT(values); value++) {
        char sql[512]; char *out[3] = {0}, *copies[3] = {0}; size_t i;
        sqlparser_parse_options_t o; sqlparser_handle_t *h;
        CHECK(snprintf(sql, sizeof(sql), "INSERT INTO t(a) VALUES (%s)", values[value]) < (int)sizeof(sql));
        begin_case("insert-after-semantic-rename", (sqlparser_dialect_t)dialect, sql);
        sqlparser_parse_options_default(&o); o.dialect = (sqlparser_dialect_t)dialect;
        h = parse(sql, &o); CHECK(h);
        CHECK(render_one(h, INSERT, 0, 0, &out[0]));
        copies[0] = strdup(out[0]); CHECK(copies[0]);
        CHECK(rename_insert_relation(h));
        for (i = 1; i < COUNT(out); i++) {
            if (dump) printf("PHASE|insert-scaffold-%s\n", i == 1 ? "cold" : "warm");
            CHECK(render_one(h, INSERT, 0, i == 2, &out[i]));
            copies[i] = strdup(out[i]); CHECK(copies[i]);
            CHECK(strcmp(out[0], out[i]) == 0); CHECK(out[i] != out[i-1]); insert_cells++;
        }
        sqlparser_handle_destroy(h);
        for (i = 0; i < COUNT(out); i++) {
            CHECK(strcmp(out[i], copies[i]) == 0); lifetime_checks++;
            sqlparser_string_free(out[i]); free(copies[i]);
        }
        fragment_cases++;
    }
}

static void render_positives(void)
{
    static const struct { const char *name, *sql, *replacement; enum render_path path; size_t index; int required; } pg[] = {
        {"target-complex", "SELECT CASE WHEN a > $1 THEN '日本 café' ELSE 'quote '' done' END AS \"A\"\"B\" FROM t", "coalesce(a, 8) AS changed", TARGET, 0, 1},
        {"where-complex", "SELECT a FROM t WHERE \"MiXeD\" = $1 AND (a IN (SELECT a FROM q) OR data->>'名' = '日本')", "a > 42 AND b IS NOT NULL", WHERE, 0, 1},
        {"order-complex", "SELECT a FROM t ORDER BY \"MiXeD\" COLLATE \"C\" DESC NULLS FIRST, CASE WHEN a > $1 THEN '日本' ELSE 'é' END", "a DESC NULLS LAST, lower(b) ASC", ORDER, 0, 1},
        {"insert-default", "INSERT INTO t(a,b,c,d) VALUES (DEFAULT, NULL, $1, '日本 café')", "42 + 7", INSERT, 0, 1},
        {"insert-bind", "INSERT INTO t(a,b,c,d) VALUES (DEFAULT, NULL, $1, '日本 café')", "DEFAULT", INSERT, 2, 1},
        {"insert-complex", "INSERT INTO t(a,b) VALUES (CASE WHEN $1 > 0 THEN ARRAY[1,2] ELSE ARRAY[3,4] END, (SELECT max(x) FROM q))", "coalesce($2, 99)", INSERT, 0, 1},
        {"insert-subquery", "INSERT INTO t(a,b) VALUES (ROW(1, '日本'), (SELECT max(x) FROM q))", "'quote '' café'", INSERT, 1, 1},
        {"update-default", "UPDATE t SET a = DEFAULT, b = NULL, c = $1, d = '日本 café' WHERE id = $2", "42 + 7", UPDATE, 0, 1},
        {"update-bind", "UPDATE t SET a = DEFAULT, b = NULL, c = $1, d = '日本 café' WHERE id = $2", "DEFAULT", UPDATE, 2, 1},
        {"update-complex", "UPDATE t SET a = CASE WHEN $1 > 0 THEN data->>'名' ELSE 'café' END, b = (SELECT max(x) FROM q) WHERE id = $2", "coalesce($3, '日本')", UPDATE, 0, 1},
        {"update-subscript", "UPDATE t SET a[2] = 7, b = ROW(1, '日本') WHERE id = $1", "17", UPDATE, 1, 1},
        {"session-interval", "SET TIME ZONE INTERVAL '02:30' HOUR TO MINUTE", "03:45", SESSION, 0, 1},
        {"session-number", "SET work_mem = 4096", NULL, SESSION, 0, 1},
        {"session-list", "SET search_path TO \"日本\", public, '$user'", "café", SESSION, 0, 1},
        {"session-string", "SET application_name = '日本 café'", "quote ' new", SESSION, 0, 1},
        {"session-default", "SET TIME ZONE DEFAULT; SET search_path FROM CURRENT; RESET ALL", NULL, SESSION, 0, 1}
    };
    size_t i;
    for (i = 0; i < COUNT(pg); i++) render_case(pg[i].name, SQLPARSER_DIALECT_POSTGRESQL,
        pg[i].sql, pg[i].path, pg[i].index, pg[i].replacement, pg[i].required);
    for (i = SQLPARSER_DIALECT_POSTGRESQL; i <= SQLPARSER_DIALECT_KINGBASE_SQLSERVER; i++) {
        render_case("family-where", (sqlparser_dialect_t)i, "SELECT a FROM t WHERE a = 1 AND b = '日本 café'", WHERE, 0, "a > 42", 1);
        render_case("family-order", (sqlparser_dialect_t)i, "SELECT a FROM t ORDER BY a DESC, b ASC", ORDER, 0, "b DESC, a ASC", 1);
        render_case("family-insert", (sqlparser_dialect_t)i, "INSERT INTO t(a,b) VALUES (1 + 2, '日本 café')", INSERT, 0, "coalesce(7, 8)", 1);
        render_case("family-update", (sqlparser_dialect_t)i, "UPDATE t SET a = 1 + 2, b = '日本 café' WHERE id = 1", UPDATE, 0, "coalesce(7, 8)", 1);
    }
    render_case("mysql-insert-binds", SQLPARSER_DIALECT_MYSQL, "INSERT INTO `t`(`a`,`b`) VALUES (coalesce(?, '日本'), DEFAULT)", INSERT, 0, "concat(?, 'café')", 1);
    render_case("oracle-insert-quotes", SQLPARSER_DIALECT_ORACLE, "INSERT INTO \"t\"(\"a\",\"b\") VALUES (coalesce(:p, q'[日本 '' café]'), DEFAULT)", INSERT, 0, "coalesce(:q, 'next')", 1);
    render_case("sqlserver-update-binds", SQLPARSER_DIALECT_SQLSERVER, "UPDATE [t] SET [a] = coalesce(@p, N'日本'), [b] = DEFAULT WHERE id = @q", UPDATE, 0, "coalesce(@r, N'café')", 1);
    render_case("session-sql-mode", SQLPARSER_DIALECT_MYSQL, "SET sql_mode = 'ANSI_QUOTES'; SELECT \"a\" FROM t", SESSION, 0, "NO_BACKSLASH_ESCAPES", 0);
    render_case("session-quoted-identifier", SQLPARSER_DIALECT_SQLSERVER, "SET QUOTED_IDENTIFIER OFF; SELECT \"日本\" AS [a]", SESSION, 0, NULL, 0);
}

static void render_limits_and_errors(void)
{
    static const size_t limits[] = {0, 1, 2, 6, 7, 8, 15, 16, 31, 32, 63, 64, 127, 256};
    static const char *sql[] = {
        "SELECT '日本 café' AS label FROM t",
        "SELECT a FROM t WHERE a = '日本 café'",
        "SELECT a FROM t ORDER BY a DESC NULLS LAST",
        "INSERT INTO t(a) VALUES (coalesce(1, 2))",
        "UPDATE t SET a = coalesce(1, 2)",
        "SET application_name = '日本 café'"
    };
    size_t path, l;
    for (path = 0; path < COUNT(sql); path++) for (l = 0; l < COUNT(limits); l++) {
        sqlparser_parse_options_t o; sqlparser_handle_t *h; char *out = NULL; size_t index = 0;
        begin_case("fragment-output-limit", SQLPARSER_DIALECT_POSTGRESQL, sql[path]);
        if (dump) printf("LIMIT_PATH|%s|%zu\n", path_names[path], limits[l]);
        sqlparser_parse_options_default(&o); o.limits.max_output_bytes = limits[l];
        h = parse(sql[path], &o); if (!h) continue;
        if (path != ORDER || order_clause(h, &index))
            (void)render_one(h, (enum render_path)path, index, 0, &out);
        sqlparser_handle_destroy(h); sqlparser_string_free(out);
    }
    for (path = TARGET; path <= UPDATE; path++) {
        sqlparser_parse_options_t o; sqlparser_handle_t *h; char *out = NULL;
        begin_case("fragment-invalid-index", SQLPARSER_DIALECT_POSTGRESQL, sql[path]);
        if (dump) printf("INVALID_PATH|%s\n", path_names[path]);
        sqlparser_parse_options_default(&o); h = parse(sql[path], &o); CHECK(h);
        CHECK(!render_one(h, (enum render_path)path, SIZE_MAX, 0, &out)); CHECK(out == NULL);
        sqlparser_handle_destroy(h);
        begin_case("fragment-invalid-mutation", SQLPARSER_DIALECT_POSTGRESQL, sql[path]);
        h = parse(sql[path], &o); CHECK(h);
        { size_t index = 0;
          if (path != ORDER || order_clause(h, &index)) CHECK(!mutate_one(h, (enum render_path)path, index, "SELECT"));
        }
        sqlparser_handle_destroy(h);
    }
}

static void template_counts(void)
{
#ifdef SQLPARSER_RENDER_PUBLIC_WRAP
    static const char *sql[] = {
        "SELECT a + 2 AS label FROM t",
        "SELECT a FROM t WHERE a > 42",
        "SELECT a FROM t ORDER BY a DESC",
        "INSERT INTO t(a) VALUES (7)",
        "UPDATE t SET a = coalesce(1, 2)",
        "SET TIME ZONE INTERVAL '02:30' HOUR TO MINUTE"
    };
    static const char *replacement[] = {"43 AS changed", "a > 44", "a ASC", "8", "coalesce(7, 8)", "03:45"};
    size_t path;
    if (expected_template_mode < 0) return;
    for (path = 0; path < COUNT(sql); path++) {
        sqlparser_parse_options_t o; sqlparser_handle_t *h; size_t index = 0, i, total;
        char *out = NULL; int ok;
        begin_case("fixed-template-observation", SQLPARSER_DIALECT_POSTGRESQL, sql[path]);
        sqlparser_parse_options_default(&o); h = parse(sql[path], &o); CHECK(h);
        if (path == ORDER) CHECK(order_clause(h, &index));
        memset(fixed_parses, 0, sizeof(fixed_parses)); other_parses = 0; count_parses = 1;
        ok = render_one(h, (enum render_path)path, index, 0, &out);
        count_parses = 0; total = 0;
        for (i = 0; i < COUNT(fixed_parses); i++) total += fixed_parses[i];
        fprintf(stderr, "TEMPLATE path=%s phase=cold fixed=%zu other=%zu status=%s\n", path_names[path], total, other_parses, ok ? "ok" : "baseline-error");
        CHECK(total == (expected_template_mode || path == INSERT ? 0U : 1U));
        CHECK(fixed_parses[path] == total); measured_calls += total;
        sqlparser_string_free(out); out = NULL;
        if (ok) {
            memset(fixed_parses, 0, sizeof(fixed_parses)); other_parses = 0; count_parses = 1;
            ok = mutate_one(h, (enum render_path)path, index, replacement[path]);
            count_parses = 0; total = 0;
            for (i = 0; i < COUNT(fixed_parses); i++) total += fixed_parses[i];
            fprintf(stderr, "TEMPLATE path=%s phase=mutation fixed=%zu other=%zu status=%s\n", path_names[path], total, other_parses, ok ? "ok" : "baseline-error");
            /* Raw fragment setters must still parse the supplied expression.
             * The typed session-literal setter does not need user SQL parsing. */
            if (path != SESSION) CHECK(other_parses > 0U);
            if (ok && path == INSERT) ok = rename_insert_relation(h);
            if (ok) {
                memset(fixed_parses, 0, sizeof(fixed_parses)); other_parses = 0; count_parses = 1;
                ok = render_one(h, (enum render_path)path, index, 0, &out);
                count_parses = 0; total = 0;
                for (i = 0; i < COUNT(fixed_parses); i++) total += fixed_parses[i];
                fprintf(stderr, "TEMPLATE path=%s phase=postmutation fixed=%zu other=%zu status=%s\n", path_names[path], total, other_parses, ok ? "ok" : "baseline-error");
                CHECK(total == (expected_template_mode ? 0U : 1U));
                CHECK(fixed_parses[path] == total); measured_calls += total;
                sqlparser_string_free(out);
            }
        }
        sqlparser_handle_destroy(h);
    }
#else
    CHECK(expected_template_mode < 0);
#endif
}

int main(int argc, char **argv)
{
    const char *directory = "tests/cases";
    int run_corpora = 1, i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump")) dump = 1;
        else if (!strcmp(argv[i], "--no-corpora")) run_corpora = 0;
        else if (!strcmp(argv[i], "--corpora") && i + 1 < argc) directory = argv[++i];
        else if (!strcmp(argv[i], "--measure") && i + 1 < argc) {
            const char *mode = argv[++i]; CHECK(!strcmp(mode, "baseline") || !strcmp(mode, "scaffold"));
            expected_template_mode = !strcmp(mode, "scaffold");
        } else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    positives(); edge_cases(); mixed_expression_deparse_diagnostic();
    limits_and_errors(); render_positives(); insert_scaffold_cases(); render_limits_and_errors(); template_counts();
    if (run_corpora) corpora(directory);
    fprintf(stderr, "public render passed: corpus=%zu positive=%zu clauses=%zu select_lists=%zu individual_targets=%zu mutations=%zu retained_outputs=%zu statuses=%zu existing_errors=%zu measured_fixed_parses=%zu\n",
        corpus_inputs, positive_cases, clauses, select_lists, targets, mutations,
        lifetime_checks, statuses, failures, measured_calls);
    fprintf(stderr, "Fragment coverage: declared_dialect_cases=%zu fragment_cases=%zu insert_cells=%zu assignments=%zu session_values=%zu\n", dialect_override_inputs, fragment_cases, insert_cells, assignments, session_values);
    return 0;
}
